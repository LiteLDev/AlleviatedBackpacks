// Refill / Advanced Refill upgrades: settings, the refill pass, pick block and registration.

#include "upgrade/refill/Refill.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>

#include "ll/api/memory/Hook.h"

#include "mc/network/NetEventCallback.h"
#include "mc/network/ServerNetworkHandler.h"
#include "mc/network/packet/BlockPickRequestPacket.h"
#include "mc/server/ServerPlayer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "ui/menu/Menu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"

namespace abp::upgrade::refill {

namespace {

constexpr int kMainInventory = 36; ///< hotbar 0..8 + main inventory 9..35
constexpr int kHotbar        = 9;

/// Last inventory slot each filter slot's item was seen in, so that a stack that ran out (e.g. the
/// last block placed) comes back into the same slot.
/// `hand`: the selected slot that held the item at the previous pass (main-hand target), so an
/// emptied hand is refilled only when the item just ran out there, not whenever the player
/// scrolls to an empty slot.
struct LastSlots {
    std::array<int, kAdvancedSlots> slot{};
    std::array<int, kAdvancedSlots> hand{};
    LastSlots() {
        slot.fill(-1);
        hand.fill(-1);
    }
};

/// Parsed settings, re-parsed only when the settings JSON changes (the tick path runs often).
struct SettingsCache {
    nlohmann::json source;
    Settings       parsed;
    int            slots = -1;
};

Settings const& loadCached(UpgradeContext const& ctx, int slots) {
    auto& cache = ctx.transient<SettingsCache>();
    if (cache.slots != slots || cache.source != ctx.settings()) {
        cache.parsed = load(ctx, slots);
        cache.source = ctx.settings();
        cache.slots  = slots;
    }
    return cache.parsed;
}

int inventorySize(Player& player) { return std::min(kMainInventory, player.getInventory().getContainerSize()); }

/// Cheap pre-check before converting a game stack (conversion serializes NBT).
bool sameIdAux(ItemStack const& stack, core::ItemStackData const& ghost) {
    return !stack.isNull() && stack.getAuxValue() == ghost.aux && stack.getTypeName() == ghost.id;
}

int maxStackOf(core::ItemStackData const& item) { return std::clamp(item.maxStack, 1, 255); }

/// Puts `got` (already taken from the backpack) into the player's main inventory: merges into
/// equal stacks first, then the remembered slot, then the first free slot. Returns what is left.
core::ItemStackData giveToInventory(Player& player, core::ItemStackData got, int preferredSlot, bool& changed) {
    auto& inv  = player.getInventory();
    int   size = inventorySize(player);
    int   cap  = maxStackOf(got);
    for (int j = 0; j < size && !got.empty(); ++j) {
        auto const& cur = inv.getItem(j);
        if (!sameIdAux(cur, got) || cur.mCount >= cap) continue;
        auto d = game::ItemConv::toData(cur);
        if (!d.sameItem(got)) continue;
        int add = std::min(cap - d.count, got.count);
        inv.setItem(j, game::ItemConv::toStack(d, d.count + add));
        got.count -= add;
        changed    = true;
    }
    auto fill = [&](int j) {
        if (got.empty() || j < 0 || j >= size || !inv.getItem(j).isNull()) return;
        int put = std::min(cap, got.count);
        inv.setItem(j, game::ItemConv::toStack(got, put));
        got.count -= put;
        changed    = true;
    };
    fill(preferredSlot);
    for (int j = 0; j < size && !got.empty(); ++j) fill(j);
    return got.empty() ? core::ItemStackData{} : got;
}

/// Returns items to the backpack (they were just taken from it). Whatever the backpack refuses
/// goes to the player (or is dropped at their feet) so nothing is lost.
void giveBack(game::Backpack& bp, Player& player, core::ItemStackData const& rest) {
    if (rest.empty()) return;
    auto r    = bp.inventory().insert(rest, core::AccessContext{core::Source::Internal, &player});
    auto left = r.remainder;
    while (!left.empty()) {
        int  n     = std::min(left.count, maxStackOf(left));
        auto stack = game::ItemConv::toStack(left, n);
        if (!player.add(stack)) player.drop(stack, false);
        left.count -= n;
    }
}

int refillAny(Player& player, UpgradeContext const& ctx, Settings const& s, int i, LastSlots& last, bool& changed) {
    auto const& ghost = s.filter.ghosts[static_cast<std::size_t>(i)];
    auto&       inv   = player.getInventory();
    int         size  = inventorySize(player);
    int         sel   = player.getSelectedItemSlot();
    int         total = 0;
    int         seen  = -1;
    for (int j = 0; j < size; ++j) {
        auto const& cur = inv.getItem(j);
        if (!sameIdAux(cur, ghost)) continue;
        if (s.filter.matchNbt && !matchesGhost(s.filter, ghost, game::ItemConv::toData(cur))) continue;
        total += cur.mCount;
        if (seen < 0 || j == sel) seen = j;
    }
    if (auto const& off = player.getOffhandSlot(); sameIdAux(off, ghost)) {
        if (!s.filter.matchNbt || matchesGhost(s.filter, ghost, game::ItemConv::toData(off))) total += off.mCount;
    }
    if (seen >= 0) last.slot[static_cast<std::size_t>(i)] = seen;
    int missing = maxStackOf(ghost) - total;
    if (missing <= 0) return 0;
    auto got = ctx.backpack().inventory().extractMatching(
        [&](core::ItemStackData const& d) { return matchesGhost(s.filter, ghost, d); },
        missing,
        core::AccessContext{core::Source::Upgrade, &player}
    );
    if (got.empty()) return 0;
    int  taken = got.count;
    auto rest  = giveToInventory(player, std::move(got), last.slot[static_cast<std::size_t>(i)], changed);
    giveBack(ctx.backpack(), player, rest);
    return taken - rest.count;
}

int refillTarget(
    Player&               player,
    UpgradeContext const& ctx,
    Settings const&       s,
    int                   i,
    Target                target,
    LastSlots&            last,
    bool&                 changed
) {
    auto const&        ghost = s.filter.ghosts[static_cast<std::size_t>(i)];
    game::ItemLocation loc;
    switch (target) {
    case Target::MainHand:
        loc = game::selectedSlot(player);
        break;
    case Target::Offhand:
        loc = {game::ItemLocation::Kind::Offhand, 0};
        break;
    default:
        loc = {game::ItemLocation::Kind::Inventory, static_cast<int>(target) - static_cast<int>(Target::Hotbar1)};
        if (loc.slot < 0 || loc.slot >= kHotbar) return 0;
        break;
    }
    auto const&         cur  = game::itemAt(player, loc);
    auto&               hand = last.hand[static_cast<std::size_t>(i)];
    core::ItemStackData have;
    int                 want = maxStackOf(ghost);
    if (!cur.isNull()) {
        if (target == Target::MainHand) hand = -1;
        if (!sameIdAux(cur, ghost)) return 0; // another item occupies the slot: leave it alone
        have = game::ItemConv::toData(cur);
        if (!matchesGhost(s.filter, ghost, have)) return 0;
        if (target == Target::MainHand) hand = loc.slot;
        want = maxStackOf(have) - have.count;
        if (want <= 0) return 0;
    } else if (target == Target::MainHand) {
        // An empty hand is refilled only right after the item ran out in it; selecting some other
        // empty slot must not conjure the item there.
        bool ranOut = hand == loc.slot;
        hand        = -1;
        if (!ranOut) return 0;
    }
    auto got = ctx.backpack().inventory().extractMatching(
        [&](core::ItemStackData const& d) {
            return have.empty() ? matchesGhost(s.filter, ghost, d) : d.sameItem(have);
        },
        want,
        core::AccessContext{core::Source::Upgrade, &player}
    );
    if (got.empty()) return 0;
    if (target == Target::MainHand) hand = loc.slot;
    int  count  = got.count;
    auto result = have.empty() ? got : have;
    auto stack  = game::ItemConv::toStack(result, (have.empty() ? 0 : have.count) + count);
    if (loc.kind == game::ItemLocation::Kind::Inventory) {
        player.getInventory().setItem(loc.slot, stack);
        changed = true;
    } else {
        game::setItemAt(player, loc, stack);
    }
    return count;
}

} // namespace

// ---- settings ------------------------------------------------------------------------------------

Target nextTarget(Target t) { return static_cast<Target>((static_cast<int>(t) + 1) % kTargetCount); }

int slotsFor(bool advanced) { return advanced ? kAdvancedSlots : kBasicSlots; }

Settings load(UpgradeContext const& ctx, int slots) {
    Settings s;
    auto&    j = ctx.settings();
    s.filter   = core::Filter::fromJson(
        j.is_object() ? j.value("filter", nlohmann::json::object()) : nlohmann::json::object(),
        slots
    );
    s.filter.mode    = core::FilterMode::Allow;
    s.filter.matchBy = core::MatchBy::Item;
    s.targets.assign(static_cast<std::size_t>(slots), Target::Any);
    if (j.is_object()) {
        if (auto it = j.find("targets"); it != j.end() && it->is_array()) {
            for (std::size_t k = 0; k < it->size() && k < s.targets.size(); ++k) {
                int v        = (*it)[k].is_number_integer() ? (*it)[k].get<int>() : 0;
                s.targets[k] = v >= 0 && v < kTargetCount ? static_cast<Target>(v) : Target::Any;
            }
        }
    }
    return s;
}

void save(UpgradeContext const& ctx, Settings const& s) {
    auto& j = ctx.settings();
    if (!j.is_object()) j = nlohmann::json::object();
    j["filter"]   = s.filter.toJson();
    auto& targets = j["targets"] = nlohmann::json::array();
    for (auto t : s.targets) targets.push_back(static_cast<int>(t));
    ctx.saveSettings(false); // no inventory hooks depend on these settings
}

bool matchesGhost(core::Filter const& filter, core::ItemStackData const& ghost, core::ItemStackData const& item) {
    if (ghost.empty() || item.empty() || item.id != ghost.id || item.aux != ghost.aux) return false;
    return !filter.matchNbt || item.nbt() == ghost.nbt();
}

// ---- refill pass -------------------------------------------------------------------------------

int refillNow(Player& player, UpgradeContext const& ctx, bool advanced) {
    if (!player.isAlive()) return 0;
    // Don't fight with a player who is moving items around in one of our menus.
    if (ui::MenuManager::get().current(player)) return 0;
    auto const& s = loadCached(ctx, slotsFor(advanced));
    if (s.filter.isEmpty()) return 0;
    auto& last    = ctx.transient<LastSlots>();
    bool  changed = false;
    int   moved   = 0;
    for (int i = 0; i < static_cast<int>(s.filter.ghosts.size()); ++i) {
        auto const& ghost = s.filter.ghosts[static_cast<std::size_t>(i)];
        if (ghost.empty() || game::isBackpack(ghost)) continue;
        auto target  = advanced ? s.targets[static_cast<std::size_t>(i)] : Target::Any;
        moved       += target == Target::Any ? refillAny(player, ctx, s, i, last, changed)
                                             : refillTarget(player, ctx, s, i, target, last, changed);
    }
    if (changed) player.sendInventory(false);
    return moved;
}

// ---- pick block --------------------------------------------------------------------------------

bool pickFromBackpacks(Player& player, core::ItemStackData const& wanted) {
    if (wanted.id.empty() || !player.isAlive()) return false;
    if (ui::MenuManager::get().current(player)) return false;
    auto& inv  = player.getInventory();
    int   size = inventorySize(player);
    // Vanilla handles items that are already in the inventory (by id: the pick item's aux can
    // differ from the aux of the stack the player holds).
    for (int j = 0; j < size; ++j) {
        auto const& cur = inv.getItem(j);
        if (!cur.isNull() && cur.getTypeName() == wanted.id) return false;
    }
    core::AccessContext const ac{core::Source::Upgrade, &player};
    for (auto& [carried, bp] : game::carriedBackpacks(player)) {
        if (!hasActive(*bp, kAdvancedId)) continue;
        // Prefer the exact variant, else any stack with the same id.
        bool exact = bp->inventory()
                         .extractMatching(
                             [&](core::ItemStackData const& d) { return d.id == wanted.id && d.aux == wanted.aux; },
                             1,
                             ac,
                             true
                         )
                         .count
                   > 0;
        auto pred  = [&](core::ItemStackData const& d) { return d.id == wanted.id && (!exact || d.aux == wanted.aux); };
        if (bp->inventory().extractMatching(pred, 1, ac, true).empty()) continue; // none (or not extractable)
        int sel    = player.getSelectedItemSlot();
        int target = -1;
        if (sel >= 0 && sel < kHotbar && inv.getItem(sel).isNull()) target = sel;
        for (int j = 0; j < kHotbar && target < 0; ++j) {
            if (inv.getItem(j).isNull()) target = j;
        }
        if (target < 0) {
            // Hotbar full: move the held item into the main inventory, or else into this backpack.
            ItemStack held = inv.getItem(sel);
            for (int j = kHotbar; j < size && target < 0; ++j) {
                if (inv.getItem(j).isNull()) {
                    inv.setItem(j, held);
                    inv.setItem(sel, ItemStack::EMPTY_ITEM());
                    target = sel;
                }
            }
            if (target < 0) {
                auto heldData = game::ItemConv::toData(held);
                if (heldData.empty()) continue;
                auto sim = bp->inventory().insert(heldData, ac, true);
                if (sim.inserted != heldData.count || sim.voided > 0) continue; // must fit without voiding
                auto r = bp->inventory().insert(heldData, ac);
                inv.setItem(sel, ItemStack::EMPTY_ITEM());
                giveBack(*bp, player, r.remainder);
                target = sel;
            }
        }
        // The first stack's worth only (the hotbar slot takes one stack).
        auto probe = bp->inventory().extractMatching(pred, 1, ac, true);
        auto got   = bp->inventory().extractMatching(pred, maxStackOf(probe), ac);
        if (got.empty()) continue; // can't happen after the probe; nothing was moved for the target
        int put = std::min(got.count, maxStackOf(got));
        inv.setItem(target, game::ItemConv::toStack(got, put));
        if (got.count > put) giveBack(*bp, player, got.withCount(got.count - put));
        if (target != sel) player.setSelectedSlot(target);
        player.sendInventory(target != sel);
        log().info("refill: {} picked {} x{} from a backpack", player.getRealName(), got.id, put);
        return true;
    }
    return false;
}

bool pickBlockAt(Player& player, BlockPos const& pos, bool withData) {
    // Ignore requests for far-away blocks (pick reach is a few blocks).
    auto const& p  = player.getPosition();
    double      dx = pos.x + 0.5 - p.x, dy = pos.y + 0.5 - p.y, dz = pos.z + 0.5 - p.z;
    if (dx * dx + dy * dy + dz * dz > 16.0 * 16.0) return false;
    auto wanted = blockItem(player, pos, withData);
    return !wanted.empty() && pickFromBackpacks(player, wanted);
}

core::ItemStackData blockItem(Player& player, BlockPos const& pos, bool withData) {
    auto&               region = player.getDimensionBlockSource();
    auto const&         block  = region.getBlock(pos);
    core::ItemStackData out;
    if (block.isAir()) return out;
    auto inst = block.asItemInstance(region, pos, withData);
    if (!inst.isNull()) {
        out.id  = inst.getTypeName();
        out.aux = inst.getAuxValue();
    }
    if (out.id.empty()) {
        // No pick item reported: most blocks share their id with their item.
        out.id  = block.getTypeName();
        out.aux = 0;
        if (game::ItemConv::make(out.id, 1).isNull()) out.id.clear();
    }
    out.count = out.id.empty() ? 0 : 1;
    return out;
}

// ---- upgrades ----------------------------------------------------------------------------------

namespace {

class RefillUpgradeBase : public IUpgrade {
public:
    RefillUpgradeBase(bool advanced, UpgradeInfo info) : mAdvanced(advanced), mInfo(std::move(info)) {}

    [[nodiscard]] UpgradeInfo const& info() const override { return mInfo; }

    void onTick(TickContext const& tick) override { refillNow(tick.player, tick.ctx, mAdvanced); }

    [[nodiscard]] std::shared_ptr<ui::Menu> createSettingsMenu(Player& player, UpgradeContext const& ctx) override {
        return makeMenu(player, ctx, mAdvanced);
    }

    [[nodiscard]] std::vector<std::string> describe(Player& player, UpgradeContext const& ctx) const override {
        auto s     = load(ctx, slotsFor(mAdvanced));
        int  count = 0;
        for (auto const& g : s.filter.ghosts) count += g.empty() ? 0 : 1;
        std::vector<std::string> out{trFor(player, "abp.refill.describe.items", "§7Refilling {} item type(s)", count)};
        if (mAdvanced)
            out.push_back(trFor(player, "abp.refill.describe.pick", "§7Pick block takes items from the backpack"));
        return out;
    }

private:
    bool        mAdvanced;
    UpgradeInfo mInfo;
};

class RefillUpgrade final : public RefillUpgradeBase {
public:
    RefillUpgrade()
    : RefillUpgradeBase(
          false,
          UpgradeInfo{
              .id           = std::string(kBasicId),
              .itemId       = "alleviatedbackpacks:refill_upgrade",
              .nameKey      = "abp.refill.name",
              .nameFallback = "Refill Upgrade",
              .tier         = UpgradeTier::Basic,
              .group        = "refill",
              .hasSettings  = true,
          }
      ) {}
};

class AdvancedRefillUpgrade final : public RefillUpgradeBase {
public:
    AdvancedRefillUpgrade()
    : RefillUpgradeBase(
          true,
          UpgradeInfo{
              .id           = std::string(kAdvancedId),
              .itemId       = "alleviatedbackpacks:advanced_refill_upgrade",
              .nameKey      = "abp.refill.advanced_name",
              .nameFallback = "Advanced Refill Upgrade",
              .tier         = UpgradeTier::Advanced,
              .group        = "refill",
              .hasSettings  = true,
          }
      ) {}
};

// Pick block: the client sends a BlockPickRequestPacket for middle clicks (survival included);
// vanilla first looks in the inventory, then we look in the backpacks.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable" // the hook macro's _AutoHookCount
LL_TYPE_INSTANCE_HOOK(
    RefillBlockPickHook,
    ll::memory::HookPriority::Normal,
    ServerNetworkHandler,
    &ServerNetworkHandler::$handle,
    void,
    NetworkIdentifier const&      source,
    BlockPickRequestPacket const& packet
) {
    origin(source, packet);
    try {
        if (auto* player = thisFor<NetEventCallback>()->_getServerPlayer(source, packet.mSenderSubId)) {
            pickBlockAt(*player, *packet.mPos, static_cast<bool>(packet.mWithData));
        }
    } catch (std::exception const& e) {
        log().error("refill pick block failed: {}", e.what());
    }
}
#pragma clang diagnostic pop

class RefillPickBlock {
public:
    void install() { RefillBlockPickHook::hook(); }
    void uninstall() { RefillBlockPickHook::unhook(); }
};

} // namespace

} // namespace abp::upgrade::refill

ABP_REGISTER_UPGRADE(abp::upgrade::refill::RefillUpgrade);
ABP_REGISTER_UPGRADE(abp::upgrade::refill::AdvancedRefillUpgrade);
ABP_REGISTER_MODULE(abp::upgrade::refill::RefillPickBlock);
