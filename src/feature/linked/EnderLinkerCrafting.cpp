// GX-2: SC's `ender_linker_endpoint` crafting recipe (EnderLinkerEndpointRecipe) on Bedrock.
//
// SC: an Ender Linker and one compatible storage, nothing else, in a crafting grid:
//   unlinked linker + unlinked backpack -> CREATE_PRIMARY: the crafted item is the linker, bound
//       to a new group; the backpack stays in the grid (recipe remainder) as the group's PRIMARY
//       endpoint;
//   unlinked linker + linked endpoint   -> BIND_EXISTING_ENDPOINT: the linker, bound to the
//       endpoint's group; the endpoint stays in the grid;
//   bound linker + unlinked backpack    -> ADD_SECONDARY: the crafted item is the backpack, now a
//       secondary endpoint; the linker is used up (only an empty backpack without upgrades: else
//       the recipe doesn't match and the grid says why);
//   anything else                       -> no recipe ("Storage is already linked").
// The link happens when the result is taken (ItemCraftedEvent: completeCraft).
//
// Bedrock: data recipes can't depend on item data, and a shapeless recipe can't list results of
// different types ("unsupported multiple output types for result list in shapeless"). The recipes
// `ender_linker_endpoint_<tier>` (shapeless: a linker + a <tier> backpack -> a linker) carry the
// craft; the consumed-item hook decides SC's plan from the real inputs and refuses invalid crafts
// with SC's message, and when the request succeeded the plan is applied to what was delivered:
//   CREATE / BIND: the backpack (linked as the primary / unchanged) goes back into its crafting
//       grid slot, and the delivered linker (unlinked linkers are interchangeable, so the result
//       stays a plain linker that stacks the way the client predicted) becomes the bound one;
//   ADD: the delivered linker is replaced by the backpack, now a secondary endpoint (the bound
//       linker was used up).
// The output slot's preview is the recipe's result (a linker) for every plan.

#include <chrono>
#include <functional>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/network/packet/InventorySlotPacket.h"
#include "mc/network/packet/InventorySlotPacketPayload.h"
#include "mc/platform/UUID.h"
#include "mc/world/ContainerID.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/SimpleSparseContainer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/containers/ContainerEnumName.h"
#include "mc/world/containers/FullContainerName.h"
#include "mc/world/inventory/network/ItemStackNetResult.h"
#include "mc/world/inventory/network/crafting/CraftHandlerBase.h"
#include "mc/world/inventory/network/crafting/CraftHandlerCrafting.h"
#include "mc/world/inventory/network/crafting/ItemStackRequestActionCraftHandler.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"

#include "feature/linked/Linked.h"
#include "feature/linked/LinkedCrafting.h"
#include "game/BackpackItem.h"
#include "game/PlayerScan.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace abp::linked {

namespace {

constexpr int  kClaimTicks = 100; ///< how long a delivered linker is looked for
constexpr auto kStaleAfter = std::chrono::seconds(2);

struct PendingCraft {
    std::optional<ItemStack>              linker;   ///< the consumed linker (count 1)
    std::optional<ItemStack>              endpoint; ///< the consumed backpack
    int                                   endpointSlot = -1; ///< its crafting grid slot (PlayerUIContainer index)
    int                                   others       = 0;
    std::set<std::pair<int, int>>         seen; ///< (container, slot) inputs already counted
    std::optional<CraftPlan>              plan;
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
};
std::unordered_map<CraftHandlerBase const*, PendingCraft> gPending;

/// A crafted plain linker that still has to be bound / taken back once it shows up.
struct LinkerTask {
    std::int64_t player = 0;
    CraftPlan    plan   = CraftPlan::CreatePrimary;
    ItemStack    bound; ///< CREATE / BIND: the bound linker (its data)
    int          age = 0;
};
std::vector<LinkerTask> gTasks;

struct Resync {
    std::int64_t player   = 0;
    int          gridSlot = -1;
    int          ticks    = 0;
};
std::vector<Resync> gResyncs;

Player& playerOf(CraftHandlerBase& handler) { return handler.mCraftRequestHandler.mPlayer; }

/// The craft makes Ender Linkers only (our endpoint recipes; also the clear recipe).
bool craftsLinkers(CraftHandlerBase& handler) {
    bool any = false, other = false;
    for (auto& [result, count] : *handler.mCraftRequestHandler.mCraftResults) {
        if (result.isNull()) continue;
        if (isLinker(result)) any = true;
        else other = true;
    }
    (void)any;
    return !other; // unknown (not filled in yet) counts as ours: the inputs decide
}

/// Returns false to refuse the craft.
bool onConsumed(CraftHandlerBase& handler, FullContainerName const& container, int slot, ItemStack const& item) {
    if (item.isNull()) return true;
    if (auto it = gPending.find(&handler); it != gPending.end() && std::chrono::steady_clock::now() - it->second.started > kStaleAfter) {
        gPending.erase(it);
    }
    // Only a linker and one backpack, nothing else: SC's endpoint recipe (no other recipe takes
    // exactly these; the craft results may not be known yet when the inputs are consumed).
    auto& p   = gPending[&handler];
    auto  key = std::make_pair(static_cast<int>(container.mName), slot);
    if (!p.seen.insert(key).second) return true; // the same input reported again
    if (isLinker(item)) {
        if (p.linker) {
            ++p.others;
        } else {
            ItemStack one = item;
            one.set(1);
            p.linker = one;
        }
    } else if (game::isBackpack(item)) {
        if (p.endpoint) {
            ++p.others;
        } else {
            p.endpoint     = item;
            p.endpointSlot = container.mName == ContainerEnumName::CraftingInputContainer ? slot : -1;
        }
    } else {
        ++p.others;
    }
    if (!p.linker || !p.endpoint || p.others > 0 || p.plan) return true;
    auto plan  = planFor(*p.linker, *p.endpoint);
    auto check = plan ? Service::get().checkLink(*p.linker, *p.endpoint) : LinkResult::AlreadyLinked;
    if (check != LinkResult::Success) {
        Player& player = playerOf(handler);
        feedback(player, check, nullptr); // SC CraftingDiagnostic message
        log().info("ender linker craft refused for {}: {}", player.getRealName(), resultName(check));
        gPending.erase(&handler);
        return false;
    }
    p.plan = plan;
    return true;
}

/// Where a delivered result can be: the cursor, the inventory and the offhand.
struct Found {
    game::ItemLocation loc{};
    bool               cursor = false;
    ItemStack          stack;
};

std::optional<Found> findDelivered(Player& player, std::function<bool(ItemStack const&)> const& matches) {
    ItemStack const& cursor = player.mPlayerUIContainer->getItem(0);
    if (matches(cursor)) return Found{{}, true, cursor};
    for (int i = 0; i < 36; ++i) {
        game::ItemLocation loc{game::ItemLocation::Kind::Inventory, i};
        auto const&        s = game::itemAt(player, loc);
        if (matches(s)) return Found{loc, false, s};
    }
    game::ItemLocation off{game::ItemLocation::Kind::Offhand, 0};
    if (matches(game::itemAt(player, off))) return Found{off, false, game::itemAt(player, off)};
    return std::nullopt;
}

void sendUiSlot(Player& player, int slot) {
    InventorySlotPacket pkt{InventorySlotPacketPayload{ContainerID::PlayerOnlyUi, slot, player.mPlayerUIContainer->getItem(slot)}};
    pkt.sendTo(player);
}

/// Replaces a delivered stack in place, keeping its item net id: the crafting screen's next
/// request names the stack by the id the client knows (a new id would get it rejected). The
/// client gets the new stack (data, type) through a slot update.
void store(Player& player, Found const& f, ItemStack const& stack) {
    ItemStack* ref = nullptr;
    if (f.cursor) ref = &const_cast<ItemStack&>(player.mPlayerUIContainer->getItem(0));
    else if (f.loc.kind == game::ItemLocation::Kind::Inventory) ref = &const_cast<ItemStack&>(player.getInventory().getItem(f.loc.slot));
    if (!ref) {
        game::setItemAt(player, f.loc, stack);
        return;
    }
    auto id = ref->mNetIdVariant.get();
    *ref    = stack;
    if (!ref->isNull()) ref->mNetIdVariant.get() = id;
    if (f.cursor) {
        sendUiSlot(player, 0);
    } else {
        InventorySlotPacket pkt{InventorySlotPacketPayload{ContainerID::Inventory, f.loc.slot, *ref}};
        pkt.sendTo(player);
    }
}

void give(Player& player, ItemStack stack) {
    if (stack.isNull() || stack.mCount <= 0) return;
    if (!player.add(stack) && !stack.isNull() && stack.mCount > 0) player.drop(stack, false);
}

/// The delivered (plain) linker of a craft: bound (CREATE / BIND, `bound` holds the bound linker)
/// or taken back (ADD). Returns false while it isn't found (and `last` is false).
bool handleDeliveredLinker(Player& player, LinkerTask const& t, bool last) {
    auto delivered = findDelivered(player, [](ItemStack const& s) { return isLinker(s) && !linkerGroup(s); });
    if (!delivered) {
        if (!last) return false;
        log().warn("ender linker craft by {}: the crafted linker was not found ({})", player.getRealName(), planName(t.plan));
        return true;
    }
    ItemStack stack = delivered->stack;
    if (t.plan == CraftPlan::AddSecondary) {
        stack.remove(1); // the bound linker was used up: the extra plain one goes back
        store(player, *delivered, stack.mCount > 0 ? stack : ItemStack{});
    } else if (t.bound.mUserData) {
        ItemStack bound = stack;
        bound.set(1);
        bound.setUserData(t.bound.mUserData->clone());
        if (stack.mCount <= 1) {
            store(player, *delivered, bound);
        } else {
            stack.remove(1);
            store(player, *delivered, stack);
            give(player, bound);
        }
    }
    player.sendInventory(false);
    return true;
}

/// Applies a successful endpoint craft. Everything valuable is settled at once (the link, the
/// backpack back into its grid slot or to the player); only the delivered plain linker may have
/// to be waited for (a task). `inRequest`: still inside the craft request (the grid slot may take
/// the backpack back).
void applyCraft(Player& player, CraftPlan plan, ItemStack const& consumedLinker, ItemStack const& consumedEndpoint, int gridSlot, bool inRequest) {
    // SC: the endpoint stays in its crafting grid slot (recipe remainder).
    auto giveEndpoint = [&](ItemStack const& stack) {
        if (inRequest && gridSlot >= 0 && player.mPlayerUIContainer->getItem(gridSlot).isNull()) {
            player.mPlayerUIContainer->setItem(gridSlot, stack);
            sendUiSlot(player, gridSlot);
            return;
        }
        give(player, stack);
    };
    ItemStack linker   = consumedLinker;
    ItemStack endpoint = consumedEndpoint;
    linker.set(1);
    auto                      r = Service::get().link(player.getUuid().asString(), linker, endpoint);
    std::optional<LinkerTask> task;
    if (r != LinkResult::Success) {
        // Should not happen (checked when consumed): undo the craft.
        giveEndpoint(consumedEndpoint);
        give(player, consumedLinker);
        task = LinkerTask{player.getOrCreateUniqueID().rawID, CraftPlan::AddSecondary, {}, 0}; // take the crafted one back
    } else if (plan == CraftPlan::AddSecondary) {
        // The crafted item is the backpack: it takes the place of the delivered plain linker.
        auto delivered = findDelivered(player, [](ItemStack const& s) { return isLinker(s) && !linkerGroup(s); });
        if (delivered && delivered->stack.mCount == 1) {
            store(player, *delivered, endpoint);
        } else {
            give(player, endpoint);
            task = LinkerTask{player.getOrCreateUniqueID().rawID, plan, {}, 0};
        }
    } else {
        giveEndpoint(endpoint);
        task = LinkerTask{player.getOrCreateUniqueID().rawID, plan, linker, 0};
    }
    if (task && !handleDeliveredLinker(player, *task, false)) gTasks.push_back(std::move(*task));
    player.sendInventory(false);
    // The client ignores slot updates of containers its request is still in flight for: send the
    // server truth again once the craft's response is through.
    gResyncs.push_back(Resync{player.getOrCreateUniqueID().rawID, inRequest ? gridSlot : -1, 2});
    log().info("ender linker craft by {} ({}): {}", player.getRealName(), planName(plan), resultName(r));
}

void runResyncs() {
    auto level = ll::service::getLevel();
    for (auto it = gResyncs.begin(); it != gResyncs.end();) {
        if (--it->ticks > 0) {
            ++it;
            continue;
        }
        if (auto* player = level ? level->getPlayer(ActorUniqueID{it->player}) : nullptr) {
            player->sendInventory(false);
            sendUiSlot(*player, 0);
            if (it->gridSlot >= 0) sendUiSlot(*player, it->gridSlot);
        }
        it = gResyncs.erase(it);
    }
}

void onPostRequest(CraftHandlerBase& handler, bool success) {
    auto it = gPending.find(&handler);
    if (it == gPending.end()) return;
    auto p = std::move(it->second);
    gPending.erase(it);
    if (!success || !p.plan || !p.linker || !p.endpoint) return;
    if (!craftsLinkers(handler)) {
        std::string results;
        for (auto& [r, n] : *handler.mCraftRequestHandler.mCraftResults) results += " " + (r.isNull() ? std::string("<null>") : r.getTypeName());
        log().info("ender linker craft: the results are not linkers ({}); not an endpoint craft", results);
        return;
    }
    if (p.others > 0) return; // more than a linker and a backpack: not SC's endpoint recipe
    Player& player = playerOf(handler);
    log().info("ender linker craft ({}) by {}", planName(*p.plan), player.getRealName());
    applyCraft(player, *p.plan, *p.linker, *p.endpoint, p.endpointSlot, true);
}

// LL_TYPE_INSTANCE_HOOK declares an unused counter in manual-registration mode.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"

LL_TYPE_INSTANCE_HOOK(
    LinkerCraftConsumeHook,
    ll::memory::HookPriority::High,
    CraftHandlerCrafting,
    &CraftHandlerCrafting::$handleConsumedItem,
    ItemStackNetResult,
    FullContainerName const& container,
    uchar const              slot,
    ItemStack const&         consumedItem
) {
    auto result = origin(container, slot, consumedItem);
    if (result == ItemStackNetResult::Success) {
        try {
            if (!onConsumed(*this, container, slot, consumedItem)) return ItemStackNetResult::Error;
        } catch (std::exception const& e) {
            log().error("ender linker craft hook failed: {}", e.what());
        }
    }
    return result;
}

LL_TYPE_INSTANCE_HOOK(
    LinkerCraftStartHook,
    ll::memory::HookPriority::High,
    CraftHandlerBase,
    &CraftHandlerBase::handleCraftAction,
    ItemStackNetResult,
    ItemStackRequestActionCraftBase const& requestAction,
    ItemStackNetResult                     currentResult
) {
    gPending.erase(this);
    return origin(requestAction, currentResult);
}

LL_TYPE_INSTANCE_HOOK(LinkerCraftPostHook, ll::memory::HookPriority::High, CraftHandlerBase, &CraftHandlerBase::postRequest, void, bool wasSuccess) {
    origin(wasSuccess);
    try {
        onPostRequest(*this, wasSuccess);
    } catch (std::exception const& e) {
        log().error("ender linker craft hook failed: {}", e.what());
    }
}

#pragma clang diagnostic pop

class EnderLinkerCrafting {
public:
    void install() {
        LinkerCraftStartHook::hook();
        LinkerCraftConsumeHook::hook();
        LinkerCraftPostHook::hook();
        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) {
            if (!gResyncs.empty()) runResyncs();
            if (gTasks.empty()) return;
            auto level = ll::service::getLevel();
            for (auto it = gTasks.begin(); it != gTasks.end();) {
                bool  last   = ++it->age >= kClaimTicks;
                auto* player = level ? level->getPlayer(ActorUniqueID{it->player}) : nullptr;
                if (!player || handleDeliveredLinker(*player, *it, last)) it = gTasks.erase(it);
                else ++it;
            }
        });
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mTick);
        LinkerCraftPostHook::unhook();
        LinkerCraftConsumeHook::unhook();
        LinkerCraftStartHook::unhook();
        gPending.clear();
        gTasks.clear();
        gResyncs.clear();
    }

private:
    ll::event::ListenerPtr mTick;
};

} // namespace

std::optional<CraftPlan> planFor(ItemStack const& linker, ItemStack const& endpoint) {
    auto target = linkerGroup(linker);
    if (target && !Service::get().group(*target)) target.reset(); // a linker of a deleted group is unlinked
    bool linked = isEndpoint(endpoint);
    if (!target) return linked ? CraftPlan::BindExisting : CraftPlan::CreatePrimary;
    if (linked) return std::nullopt;
    return CraftPlan::AddSecondary;
}

std::string_view planName(CraftPlan plan) {
    switch (plan) {
    case CraftPlan::CreatePrimary:
        return "linker_endpoint_primary";
    case CraftPlan::BindExisting:
        return "linker_endpoint_bind";
    case CraftPlan::AddSecondary:
        return "linker_endpoint_secondary";
    }
    return "?";
}

std::size_t pendingCraftClaims() { return gTasks.size(); }

bool finalizeCraft(Player& player, CraftPlan plan, ItemStack const& consumedLinker, ItemStack const& consumedEndpoint, int gridSlot) {
    applyCraft(player, plan, consumedLinker, consumedEndpoint, gridSlot, true);
    return true;
}

} // namespace abp::linked

ABP_REGISTER_MODULE(abp::linked::EnderLinkerCrafting);
