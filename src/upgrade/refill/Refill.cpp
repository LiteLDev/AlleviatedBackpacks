// Refill / Advanced Refill upgrades (SB RefillUpgradeWrapper): settings, the refill pass, the
// 5-tick ticker (carried and placed backpacks), pick block, native tab data and registration.

#include "upgrade/refill/Refill.h"

#include <algorithm>
#include <cmath>
#include <exception>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/network/NetEventCallback.h"
#include "mc/network/ServerNetworkHandler.h"
#include "mc/network/packet/BlockPickRequestPacket.h"
#include "mc/server/ServerPlayer.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"

#include "feature/placed/PlacedBackpacks.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "game/Storage.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "net/NativeScreens.h"
#include "ui/menu/Menu.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/deposit/ContainerTransfer.h"
#include "upgrade/inception/Inception.h"

namespace abp::upgrade::refill {

namespace {

constexpr int kMainInventory = 36; ///< hotbar 0..8 + main inventory 9..35
constexpr int kHotbar        = 9;

/// Parsed settings, re-parsed only when the settings JSON changes (the tick path runs often).
struct SettingsCache {
    std::uint64_t revision = 0;
    std::string   item;
    Settings      parsed;
    int           slots = -1;
};

/// Re-parsed when the backpack changed (settings edits mark it dirty) or the upgrade was swapped.
Settings const& loadCached(UpgradeContext const& ctx, int slots) {
    auto& cache = ctx.transient<SettingsCache>();
    auto  rev   = ctx.backpack().revision();
    if (cache.slots != slots || cache.revision != rev || cache.item != ctx.state().item.id) {
        cache.parsed   = load(ctx, slots);
        cache.revision = rev;
        cache.item     = ctx.state().item.id;
        cache.slots    = slots;
    }
    return cache.parsed;
}

int inventorySize(Player& player) { return std::min(kMainInventory, player.getInventory().getContainerSize()); }

/// Cheap pre-check before converting a game stack (conversion serializes NBT).
bool sameIdAux(ItemStack const& stack, core::ItemStackData const& ghost) {
    return !stack.isNull() && stack.getAuxValue() == ghost.aux && stack.getTypeName() == ghost.id;
}

/// SB isSameItemSameComponents(stack, filter).
bool sameAsGhost(ItemStack const& stack, core::ItemStackData const& ghost) {
    return sameIdAux(stack, ghost) && game::ItemConv::toData(stack).nbt() == ghost.nbt();
}

int maxStackOf(core::ItemStackData const& item) { return std::clamp(item.maxStack, 1, 255); }

ItemStack const& cursorOf(Player& player) { return player.mPlayerUIContainer->getItem(0); }

/// SC getInventoryForUpgradeProcessing: the backpack's inventory plus, with Inception, those of the
/// backpacks inside it.
std::vector<std::shared_ptr<game::Backpack>> inventoriesOf(UpgradeContext const& ctx) {
    return inception::upgradeInventories(ctx.backpackPtr());
}

core::ItemStackData extractFrom(
    std::vector<std::shared_ptr<game::Backpack>> const& invs,
    core::ItemStackData const&                          ghost,
    int                                                 amount,
    Player&                                             player,
    bool                                                simulate
) {
    core::ItemStackData got;
    for (auto const& bp : invs) {
        if (amount <= 0) break;
        auto part = bp->inventory().extractMatching(
            [&](core::ItemStackData const& d) { return d.sameItem(ghost); },
            amount,
            core::AccessContext{core::Source::Upgrade, &player},
            simulate
        );
        if (part.empty()) continue;
        if (got.empty()) got = part;
        else got.count += part.count;
        amount -= part.count;
    }
    return got;
}

/// SB getCountMissingInHandler: how many of `ghost` the inventory lacks to one full stack.
int missingInInventory(Player& player, core::ItemStackData const& ghost) {
    int   missing = maxStackOf(ghost);
    auto& inv     = player.getInventory();
    for (int j = 0, n = inventorySize(player); j < n && missing > 0; ++j) {
        auto const& cur = inv.getItem(j);
        if (sameAsGhost(cur, ghost)) missing -= std::min<int>(cur.mCount, missing);
    }
    if (auto const& off = player.getOffhandSlot(); missing > 0 && sameAsGhost(off, ghost)) missing -= std::min<int>(off.mCount, missing);
    return missing;
}

/// SB refillAnywhereInInventory: merge into equal stacks, then the first free slots. Returns how
/// many items were placed.
int fillAnywhere(Player& player, core::ItemStackData const& got) {
    auto& inv  = player.getInventory();
    int   size = inventorySize(player);
    int   cap  = maxStackOf(got);
    int   left = got.count;
    for (int j = 0; j < size && left > 0; ++j) {
        auto const& cur = inv.getItem(j);
        if (!sameAsGhost(cur, got) || cur.mCount >= cap) continue;
        int add = std::min(cap - static_cast<int>(cur.mCount), left);
        inv.setItem(j, game::ItemConv::toStack(got, cur.mCount + add));
        left -= add;
    }
    if (auto const& off = player.getOffhandSlot(); left > 0 && sameAsGhost(off, got) && off.mCount < cap) {
        int add = std::min(cap - static_cast<int>(off.mCount), left);
        player.setOffhandSlot(game::ItemConv::toStack(got, off.mCount + add));
        left -= add;
    }
    for (int j = 0; j < size && left > 0; ++j) {
        if (!inv.getItem(j).isNull()) continue;
        int put = std::min(cap, left);
        inv.setItem(j, game::ItemConv::toStack(got, put));
        left -= put;
    }
    return got.count - left;
}

game::ItemLocation locationOf(Player& player, Target target) {
    switch (target) {
    case Target::MainHand:
        return game::selectedSlot(player);
    case Target::Offhand:
        return {game::ItemLocation::Kind::Offhand, 0};
    default:
        return {game::ItemLocation::Kind::Inventory, static_cast<int>(target) - static_cast<int>(Target::Hotbar1)};
    }
}

/// SB tryRefillFilter.
int refillOne(Player& player, UpgradeContext const& ctx, core::ItemStackData const& ghost, Target target, bool& changed) {
    game::ItemLocation loc{};
    int                missing = 0;
    if (target == Target::Any) {
        missing = missingInInventory(player, ghost);
    } else {
        loc = locationOf(player, target);
        if (loc.kind == game::ItemLocation::Kind::Inventory && (loc.slot < 0 || loc.slot >= kHotbar)) return 0;
        auto const& cur = game::itemAt(player, loc);
        if (!cur.isNull() && !sameAsGhost(cur, ghost)) return 0; // SB: the filler refuses another item
        missing = maxStackOf(ghost) - (cur.isNull() ? 0 : static_cast<int>(cur.mCount));
    }
    // SB: the item on the cursor counts too.
    if (auto const& carried = cursorOf(player); sameAsGhost(carried, ghost)) missing -= std::min<int>(missing, carried.mCount);
    if (missing <= 0) return 0;
    auto invs      = inventoriesOf(ctx);
    auto extracted = extractFrom(invs, ghost, missing, player, true);
    if (extracted.empty()) return 0;
    int placed = 0;
    if (target == Target::Any) {
        placed = fillAnywhere(player, extracted);
        if (placed > 0) changed = true;
    } else {
        auto const& cur  = game::itemAt(player, loc);
        int         have = cur.isNull() ? 0 : static_cast<int>(cur.mCount);
        placed           = extracted.count;
        auto stack       = game::ItemConv::toStack(cur.isNull() ? extracted : game::ItemConv::toData(cur), have + placed);
        if (loc.kind == game::ItemLocation::Kind::Inventory) {
            player.getInventory().setItem(loc.slot, stack);
            changed = true;
        } else {
            game::setItemAt(player, loc, stack);
        }
    }
    if (placed > 0) (void)extractFrom(invs, ghost, placed, player, false);
    return placed;
}

} // namespace

// ---- settings ------------------------------------------------------------------------------------

Target nextTarget(Target t) { return static_cast<Target>((static_cast<int>(t) + 1) % kTargetCount); }
Target previousTarget(Target t) { return static_cast<Target>((static_cast<int>(t) + kTargetCount - 1) % kTargetCount); }

int slotsFor(bool advanced) {
    auto const& c = config::get<RefillConfig>();
    return std::clamp((advanced ? c.advancedRefillUpgrade : c.refillUpgrade).filterSlots, 1, 20);
}
int slotsInRow(bool advanced) {
    auto const& c = config::get<RefillConfig>();
    return std::clamp((advanced ? c.advancedRefillUpgrade : c.refillUpgrade).slotsInRow, 1, 6);
}

Settings load(UpgradeContext const& ctx, int slots) {
    Settings s;
    auto&    j = ctx.settings();
    // As stored (padded to `slots`): ghosts beyond `slots` (a basic refill of phase 1 had 9
    // slots, SB has 6) are kept in the settings but neither shown nor used.
    s.filter = core::Filter::fromJson(j.is_object() ? j.value("filter", nlohmann::json::object()) : nlohmann::json::object(), -1);
    if (static_cast<int>(s.filter.ghosts.size()) < slots) s.filter.ghosts.resize(static_cast<std::size_t>(slots));
    s.filter.mode     = core::FilterMode::Allow;
    s.filter.matchBy  = core::MatchBy::Item;
    s.filter.matchNbt = true; // SB: same item, same components
    s.targets.assign(s.filter.ghosts.size(), Target::Any);
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

bool setGhost(UpgradeContext const& ctx, bool advanced, int index, core::ItemStackData const& item) {
    int  slots = slotsFor(advanced);
    auto s     = load(ctx, slots);
    if (index < 0 || index >= slots) return false;
    auto ghost = item.empty() ? core::ItemStackData{} : item.withCount(1);
    if (!ghost.empty()) {
        for (int k = 0; k < slots; ++k) {
            if (k != index && s.filter.ghosts[static_cast<std::size_t>(k)].sameItem(ghost)) return false; // no duplicates
        }
    }
    bool wasEmpty = s.filter.ghosts[static_cast<std::size_t>(index)].empty();
    s.filter.setGhost(index, ghost);
    // SB onFilterChange: a cleared filter slot loses its target; a new one starts at "any".
    if (ghost.empty() || wasEmpty) s.targets[static_cast<std::size_t>(index)] = Target::Any;
    save(ctx, s);
    return true;
}

void setTarget(UpgradeContext const& ctx, bool advanced, int index, Target target) {
    int  slots = slotsFor(advanced);
    auto s     = load(ctx, slots);
    if (!advanced || index < 0 || index >= slots || s.filter.ghosts[static_cast<std::size_t>(index)].empty()) return;
    s.targets[static_cast<std::size_t>(index)] = target;
    save(ctx, s);
}

bool matchesGhost(core::Filter const&, core::ItemStackData const& ghost, core::ItemStackData const& item) {
    return !ghost.empty() && !item.empty() && item.sameItem(ghost);
}

// ---- refill pass -------------------------------------------------------------------------------

int refillNow(Player& player, UpgradeContext const& ctx, bool advanced) {
    if (!player.isAlive()) return 0;
    auto const& s = loadCached(ctx, slotsFor(advanced));
    if (s.filter.isEmpty()) return 0;
    bool changed = false;
    int  moved   = 0;
    for (int i = 0; i < std::min(static_cast<int>(s.filter.ghosts.size()), slotsFor(advanced)); ++i) {
        auto const& ghost = s.filter.ghosts[static_cast<std::size_t>(i)];
        if (ghost.empty() || game::isBackpack(ghost)) continue;
        moved += refillOne(player, ctx, ghost, advanced ? s.targets[static_cast<std::size_t>(i)] : Target::Any, changed);
    }
    if (changed) player.sendInventory(false);
    return moved;
}

// ---- pick block --------------------------------------------------------------------------------

namespace {
/// Vanilla Inventory.getSuitableHotbarSlot.
int suitableHotbarSlot(Player& player) {
    auto& inv = player.getInventory();
    int   sel = std::clamp(player.getSelectedItemSlot(), 0, kHotbar - 1);
    for (int i = 0; i < kHotbar; ++i) {
        int j = (sel + i) % kHotbar;
        if (inv.getItem(j).isNull()) return j;
    }
    for (int i = 0; i < kHotbar; ++i) {
        int j = (sel + i) % kHotbar;
        if (!inv.getItem(j).isEnchanted()) return j;
    }
    return sel;
}

/// SB canMoveSelectedToInventory.
bool canMoveSelectedToInventory(Player& player, int sel) {
    auto& inv   = player.getInventory();
    auto  held  = game::ItemConv::toData(inv.getItem(sel));
    int   toAdd = held.count;
    for (int j = 0, n = inventorySize(player); j < n; ++j) {
        if (j == sel) continue;
        auto const& cur = inv.getItem(j);
        if (cur.isNull()) return true;
        if (sameAsGhost(cur, held)) {
            toAdd -= maxStackOf(held) - cur.mCount;
            if (toAdd <= 0) return true;
        }
    }
    return false;
}

core::InsertResult insertInto(
    std::vector<std::shared_ptr<game::Backpack>> const& invs,
    core::ItemStackData                                 item,
    Player&                                             player,
    bool                                                simulate,
    core::Source                                        source = core::Source::Upgrade
) {
    core::InsertResult total;
    for (auto const& bp : invs) {
        if (item.empty()) break;
        auto r          = bp->inventory().insert(item, core::AccessContext{source, &player}, simulate);
        total.inserted += r.inserted;
        total.voided   += r.voided;
        item            = r.remainder;
    }
    total.remainder = item;
    return total;
}
} // namespace

bool pickFromBackpacks(Player& player, core::ItemStackData const& wanted) {
    if (wanted.id.empty() || !player.isAlive() || player.isCreative()) return false;
    auto& inv = player.getInventory();
    // SB (client): the item is already in the inventory: vanilla picks it.
    for (int j = 0, n = inventorySize(player); j < n; ++j) {
        if (sameIdAux(inv.getItem(j), wanted)) return false;
    }
    for (auto& [carried, bp] : game::carriedBackpacks(player, false, true)) {
        if (!carried.upgradeActive(kAdvancedId, "refill") || !hasActive(*bp, kAdvancedId)) continue;
        auto invs = inception::upgradeInventories(bp);
        // SB: an exact match in the backpack; a stack's worth of it.
        core::ItemStackData match;
        for (auto const& b : invs) {
            auto m = b->inventory().extractMatching(
                [&](core::ItemStackData const& d) { return d.id == wanted.id && d.aux == wanted.aux; },
                1,
                core::AccessContext{core::Source::Upgrade, &player},
                true
            );
            if (!m.empty()) {
                match = m.withCount(1);
                break;
            }
        }
        if (match.empty()) continue;
        int  amount = maxStackOf(match);
        auto probe  = extractFrom(invs, match, amount, player, true);
        if (probe.empty()) continue;
        int sel = suitableHotbarSlot(player);
        if (sel != player.getSelectedItemSlot()) player.setSelectedSlot(sel);
        ItemStack held     = inv.getItem(sel);
        auto      heldData = game::ItemConv::toData(held);
        auto      got      = extractFrom(invs, match, amount, player, false);
        if (got.empty()) continue;
        bool done = heldData.empty();
        if (!done) {
            auto sim = insertInto(invs, heldData, player, true);
            if (sim.remainder.empty() && sim.voided == 0) {
                (void)insertInto(invs, heldData, player, false);
                done = true;
            } else if (canMoveSelectedToInventory(player, sel)) {
                inv.setItem(sel, ItemStack::EMPTY_ITEM());
                auto rest = heldData;
                // Merge into equal stacks first, then free slots (vanilla Inventory.add).
                for (int j = 0, n = inventorySize(player); j < n && !rest.empty(); ++j) {
                    if (j == sel) continue;
                    auto const& cur = inv.getItem(j);
                    if (!sameAsGhost(cur, rest) || cur.mCount >= maxStackOf(rest)) continue;
                    int add = std::min(maxStackOf(rest) - static_cast<int>(cur.mCount), rest.count);
                    inv.setItem(j, game::ItemConv::toStack(rest, cur.mCount + add));
                    rest.count -= add;
                }
                for (int j = 0, n = inventorySize(player); j < n && !rest.empty(); ++j) {
                    if (j == sel || !inv.getItem(j).isNull()) continue;
                    inv.setItem(j, game::ItemConv::toStack(rest, rest.count));
                    rest.count = 0;
                }
                done = true;
            }
        }
        if (!done) {
            // Put it back (Internal: no filter or Void may refuse / eat it); a rest goes to the player.
            auto rest = insertInto(invs, got, player, false, core::Source::Internal).remainder;
            if (!rest.empty()) {
                auto stack = game::ItemConv::toStack(rest, rest.count);
                if (!player.add(stack)) player.drop(stack, false);
            }
            upgrade::transfer::sendTip(
                player,
                trFor(player, "abp.refill.status.no_space_for_mainhand_item", "There's no place for item in hand in either backpack or player's inventory")
            );
            return false;
        }
        inv.setItem(sel, game::ItemConv::toStack(got, got.count));
        player.sendInventory(true);
        log().info("refill: {} picked {} x{} from a backpack", player.getRealName(), got.id, got.count);
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

bool isAdvancedId(std::string_view id) { return id == kAdvancedId; }

class RefillUpgradeBase : public IUpgrade {
public:
    RefillUpgradeBase(bool advanced, UpgradeInfo info) : mAdvanced(advanced), mInfo(std::move(info)) {}

    [[nodiscard]] UpgradeInfo const& info() const override { return mInfo; }

    // Ticking: RefillTicker below (SB COOLDOWN = 5 ticks, carried and placed backpacks).

    [[nodiscard]] std::shared_ptr<ui::Menu> createSettingsMenu(Player& player, UpgradeContext const& ctx) override {
        return makeMenu(player, ctx, mAdvanced);
    }

    [[nodiscard]] std::vector<std::string> describe(Player& player, UpgradeContext const&) const override {
        std::vector<std::string> out{trFor(player, "abp.refill.tooltip", "§7Keeps refilling stack of selected items in player's inventory")};
        if (mAdvanced) {
            out.push_back(trFor(player, "abp.refill.advanced_tooltip1", "§7Allows for more precise target slot selection"));
            out.push_back(trFor(player, "abp.refill.advanced_tooltip2", "§7Also allows middle click picking blocks from backpack"));
        }
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

/// Every 5 ticks (SB cooldown): carried backpacks refill their carrier; placed backpacks refill
/// every player within 3 blocks of the block.
class RefillTicker {
public:
    void install() {
        mListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>(
            [this](ll::event::ServerLevelTickEvent& ev) { tick(ev.level()); }
        );
    }
    void uninstall() { ll::event::EventBus::getInstance().removeListener(mListener); }

private:
    static void forRefills(std::shared_ptr<game::Backpack> const& bp, auto&& fn) {
        upgrade::forEachInstalled(bp, true, [&](IUpgrade& u, UpgradeContext const& ctx) {
            if (u.info().groupOrId() == "refill") fn(u, ctx);
        });
    }

    void tick(Level& level) {
        if (++mTicks < kCooldownTicks) return;
        mTicks = 0;
        if (!game::Storage::get().available()) return;
        try {
            level.forEachPlayer([&](Player& player) {
                if (player.isClientSide() || !player.isAlive()) return true;
                for (auto& [carried, bp] : game::carriedBackpacks(player, false, true)) {
                    forRefills(bp, [&](IUpgrade& u, UpgradeContext const& ctx) {
                        if (carried.upgradeActive(u.info().id, u.info().groupOrId())) {
                            refillNow(player, ctx.withPlayer(&player), isAdvancedId(u.info().id));
                        }
                    });
                }
                return true;
            });
            for (auto const& e : placed::PlacedBackpacks::get().entries()) {
                auto bp = game::Storage::get().find(e.uuid);
                if (!bp) continue;
                // SB: AABB(pos).inflate(3).
                AABB box{
                    Vec3{static_cast<float>(e.loc.x) - kPlacedRange, static_cast<float>(e.loc.y) - kPlacedRange, static_cast<float>(e.loc.z) - kPlacedRange},
                    Vec3{static_cast<float>(e.loc.x) + 1 + kPlacedRange, static_cast<float>(e.loc.y) + 1 + kPlacedRange, static_cast<float>(e.loc.z) + 1 + kPlacedRange}
                };
                forRefills(bp, [&](IUpgrade& u, UpgradeContext const& ctx) {
                    level.forEachPlayer([&](Player& player) {
                        if (player.isClientSide() || !player.isAlive() || static_cast<int>(player.getDimensionId()) != e.loc.dim) return true;
                        auto const& b = player.getAABB();
                        if (b.max.x < box.min.x || b.min.x > box.max.x || b.max.y < box.min.y || b.min.y > box.max.y || b.max.z < box.min.z
                            || b.min.z > box.max.z) {
                            return true;
                        }
                        refillNow(player, ctx.withPlayer(&player), isAdvancedId(u.info().id));
                        return true;
                    });
                });
            }
        } catch (std::exception const& ex) {
            log().error("refill tick failed: {}", ex.what());
        }
    }

    ll::event::ListenerPtr mListener;
    int                    mTicks = 0;
};

// ---- native tab (client/ui/tabs/refill) ---------------------------------------------------------

nlohmann::json tabData(Player& player, UpgradeContext const& ctx) {
    bool           adv = ctx.state().item.id == "alleviatedbackpacks:advanced_refill_upgrade";
    auto           s   = load(ctx, slotsFor(adv));
    nlohmann::json ghosts  = nlohmann::json::array();
    nlohmann::json targets = nlohmann::json::array();
    for (std::size_t i = 0; i < static_cast<std::size_t>(slotsFor(adv)) && i < s.filter.ghosts.size(); ++i) {
        ghosts.push_back(s.filter.ghosts[i].empty() ? nlohmann::json(nullptr) : s.filter.ghosts[i].toJson());
        targets.push_back(static_cast<int>(s.targets[i]));
    }
    return nlohmann::json{
        {"refill", {{"advanced", adv}, {"slotsInRow", slotsInRow(adv)}, {"ghosts", std::move(ghosts)}, {"targets", std::move(targets)}}},
        {"tabLabel", adv ? trFor(player, "abp.refill.tab.advanced", "Adv. Refill") : trFor(player, "abp.refill.tab", "Refill")},
        {"tabTooltip", adv ? trFor(player, "abp.refill.tab.advanced_tooltip", "Advanced Refill Settings") : trFor(player, "abp.refill.tab.tooltip", "Refill Settings")},
    };
}

bool tabAction(Player& player, UpgradeContext const& ctx, nlohmann::json const& data) {
    if (!data.contains("refill")) return false;
    bool adv   = ctx.state().item.id == "alleviatedbackpacks:advanced_refill_upgrade";
    auto op    = data.value("refill", std::string{});
    int  index = data.value("index", -1);
    if (op == "ghost") {
        // SC filter slot: a copy of the cursor item (the cursor keeps it); an empty cursor clears.
        auto const& cursor = cursorOf(player);
        (void)setGhost(ctx, adv, index, cursor.isNull() ? core::ItemStackData{} : game::ItemConv::toData(cursor).withCount(1));
        return true;
    }
    if (op == "target" && adv) {
        auto s = load(ctx, slotsFor(adv));
        if (index < 0 || index >= static_cast<int>(s.targets.size())) return false;
        auto t = s.targets[static_cast<std::size_t>(index)];
        auto next = data.value("dir", 1) >= 0 ? nextTarget(t) : previousTarget(t);
        setTarget(ctx, adv, index, next);
        log().info("refill: {} set filter slot {} target to {}", player.getRealName(), index + 1, static_cast<int>(next));
        return true;
    }
    return false;
}

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
ABP_REGISTER_MODULE(abp::upgrade::refill::RefillTicker);
ABP_REGISTER_TAB_DATA(abp::net::TabDataProvider{"refill", &abp::upgrade::refill::tabData});
ABP_REGISTER_TAB_ACTION(abp::net::TabActionHandler{"refill", &abp::upgrade::refill::tabAction});

ABP_REGISTER_CONFIG("refill", abp::upgrade::refill::RefillConfig);
