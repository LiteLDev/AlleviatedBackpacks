#pragma once

// Refill upgrade (SB RefillUpgradeWrapper): keeps one stack of each filtered item in the
// carrier's inventory, pulling it from the backpack every 5 ticks. The advanced tier has 12 filter
// slots (basic: 6), a target slot per filter slot (any / main hand / off hand / hotbar 1..9, chosen
// by scrolling over the slot in its tab) and pulls pick-blocked items out of the backpack.
// A placed backpack refills every player within 3 blocks.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/Filter.h"
#include "core/ItemStackData.h"
#include "upgrade/IUpgrade.h"
#include "upgrade/filter/FilterTab.h"

class BlockPos;
class Player;

namespace abp::ui {
class Menu;
}

namespace abp::upgrade::refill {

inline constexpr std::string_view kBasicId       = "refill";
inline constexpr std::string_view kAdvancedId    = "advanced_refill";
inline constexpr int              kBasicSlots    = 6;  ///< SB refillUpgrade.filterSlots
inline constexpr int              kAdvancedSlots = 12; ///< SB advancedRefillUpgrade.filterSlots
inline constexpr int              kCooldownTicks = 5;  ///< SB COOLDOWN
inline constexpr float            kPlacedRange   = 3;  ///< SB REFILL_RANGE (placed backpacks)

/// SB's `refillUpgrade` / `advancedRefillUpgrade` config sections (`config/refill.json`); filterSlots
/// 1..20, slotsInRow 1..6.
struct RefillConfig {
    int                              version               = 1;
    filtering::FilteredUpgradeConfig refillUpgrade         = {kBasicSlots, 3};
    filtering::FilteredUpgradeConfig advancedRefillUpgrade = {kAdvancedSlots, 4};
};

/// Where a filter slot's item is refilled to (SB TargetSlot; the basic tier always uses `Any`).
enum class Target : int {
    Any      = 0, ///< keep one full stack's worth anywhere in the inventory
    MainHand = 1, ///< the selected hotbar slot
    Offhand  = 2,
    Hotbar1  = 3, ///< Hotbar1 + k = hotbar slot k (0..8)
};
inline constexpr int kTargetCount = 12; ///< Any, MainHand, Offhand, Hotbar 1..9

[[nodiscard]] Target nextTarget(Target t);
[[nodiscard]] Target previousTarget(Target t);

/// Persisted per-upgrade settings: `settings()["filter"]` (core::Filter) + `settings()["targets"]`.
struct Settings {
    core::Filter        filter;
    std::vector<Target> targets; ///< one per filter slot
};
[[nodiscard]] int      slotsFor(bool advanced);
[[nodiscard]] int      slotsInRow(bool advanced); ///< tab layout: SB 3 (basic) / 4 (advanced)
[[nodiscard]] Settings load(UpgradeContext const& ctx, int slots);
void                   save(UpgradeContext const& ctx, Settings const& settings);
/// Sets filter slot `index` (SB: a new filter gets target "any", a cleared one loses its target).
/// Refuses (returns false) an item already in another filter slot.
bool setGhost(UpgradeContext const& ctx, bool advanced, int index, core::ItemStackData const& item);
void setTarget(UpgradeContext const& ctx, bool advanced, int index, Target target);

/// True if `item` is what filter slot `ghost` stands for (SB: same item, same components).
[[nodiscard]] bool
matchesGhost(core::Filter const& filter, core::ItemStackData const& ghost, core::ItemStackData const& item);

/// One refill pass for `player` (the carrier, or a player near a placed backpack). Returns how
/// many items moved from the backpack to the player.
int refillNow(Player& player, UpgradeContext const& ctx, bool advanced);

/// Pick block (SB pickBlock): when `player` (not creative) has no `wanted` item in the inventory,
/// takes a stack of it from a carried backpack with an active Advanced Refill upgrade into a
/// suitable hotbar slot (the held item goes into the backpack, or into the inventory; with no room
/// for it a "no space" status is shown). Returns true if items were pulled.
bool pickFromBackpacks(Player& player, core::ItemStackData const& wanted);
/// Same, for the block at `pos` (BlockPickRequestPacket).
bool pickBlockAt(Player& player, BlockPos const& pos, bool withData);
/// The item that picking the block at `pos` asks for (empty for air / unpickable blocks).
[[nodiscard]] core::ItemStackData blockItem(Player& player, BlockPos const& pos, bool withData);

/// The server settings menu (ghost slots, target buttons for the advanced tier): the chest UI of
/// simulated players in selftests. Players see the native tab (client/ui/tabs/refill).
[[nodiscard]] std::shared_ptr<ui::Menu> makeMenu(Player& player, UpgradeContext const& ctx, bool advanced);

} // namespace abp::upgrade::refill
