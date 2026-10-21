#pragma once

// Refill upgrade (basic + advanced): keeps the items in its filter stocked in the carrier's
// inventory, pulling them from the backpack. The advanced tier has more filter slots, lets every
// filter slot target a specific inventory slot (main hand, offhand, hotbar 1-9) and pulls
// pick-blocked items out of the backpack.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/Filter.h"
#include "core/ItemStackData.h"
#include "upgrade/IUpgrade.h"

class BlockPos;
class Player;

namespace abp::ui {
class Menu;
}

namespace abp::upgrade::refill {

inline constexpr std::string_view kBasicId       = "refill";
inline constexpr std::string_view kAdvancedId    = "advanced_refill";
inline constexpr int              kBasicSlots    = 9;
inline constexpr int              kAdvancedSlots = 12;

/// Where a filter slot's item is refilled to (advanced tier; the basic tier always uses `Any`).
enum class Target : int {
    Any      = 0, ///< keep one full stack's worth anywhere in the inventory
    MainHand = 1, ///< the selected hotbar slot
    Offhand  = 2,
    Hotbar1  = 3, ///< Hotbar1 + k = hotbar slot k (0..8)
};
inline constexpr int kTargetCount = 12; ///< Any, MainHand, Offhand, Hotbar 1..9

[[nodiscard]] Target nextTarget(Target t);

/// Persisted per-upgrade settings: `settings()["filter"]` (core::Filter) + `settings()["targets"]`.
struct Settings {
    core::Filter        filter;
    std::vector<Target> targets; ///< one per filter slot
};
[[nodiscard]] int      slotsFor(bool advanced);
[[nodiscard]] Settings load(UpgradeContext const& ctx, int slots);
void                   save(UpgradeContext const& ctx, Settings const& settings);

/// True if `item` is what filter slot `ghost` stands for (same id and aux; same NBT when the
/// filter matches NBT).
[[nodiscard]] bool
matchesGhost(core::Filter const& filter, core::ItemStackData const& ghost, core::ItemStackData const& item);

/// One refill pass for the upgrade in `ctx` carried by `player`. Returns how many items moved
/// from the backpack to the player.
int refillNow(Player& player, UpgradeContext const& ctx, bool advanced);

/// Pick block: if `player` has no item with `wanted`'s id in the inventory, takes a stack of it
/// (same aux preferred) from a carried backpack with an active Advanced Refill upgrade and puts it into
/// the hotbar (selected slot if empty, else a free hotbar slot which gets selected, else the held
/// item is moved away first). Returns true if items were pulled.
bool pickFromBackpacks(Player& player, core::ItemStackData const& wanted);
/// Same, for the block at `pos` (BlockPickRequestPacket).
bool pickBlockAt(Player& player, BlockPos const& pos, bool withData);
/// The item that picking the block at `pos` asks for (empty for air / unpickable blocks).
[[nodiscard]] core::ItemStackData blockItem(Player& player, BlockPos const& pos, bool withData);

/// The settings menu (ghost slots, target buttons for the advanced tier, NBT toggle).
[[nodiscard]] std::shared_ptr<ui::Menu> makeMenu(Player& player, UpgradeContext const& ctx, bool advanced);

} // namespace abp::upgrade::refill
