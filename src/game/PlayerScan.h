#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/Tier.h"

class ItemStack;
class Player;

namespace abp::game {

class Backpack;

/// Where an item sits on a player.
struct ItemLocation {
    enum class Kind {
        Inventory, ///< `slot` = inventory index 0..35 (0..8 hotbar)
        Offhand,   ///< `slot` unused (0)
        Armor,     ///< `slot` = SharedTypes::Legacy::ArmorSlot value (1 = chest)
    };
    Kind kind = Kind::Inventory;
    int  slot = 0;

    [[nodiscard]] bool operator==(ItemLocation const&) const = default;
};

/// The item at `loc` (an empty stack if out of range).
[[nodiscard]] ItemStack const& itemAt(Player& player, ItemLocation loc);
/// Replaces the item at `loc` and syncs it to the client.
void setItemAt(Player& player, ItemLocation loc, ItemStack const& item);

/// The location of the player's selected hotbar slot.
[[nodiscard]] ItemLocation selectedSlot(Player& player);

struct CarriedBackpack {
    ItemLocation               where;
    core::Tier                 tier;
    std::optional<std::string> uuid; ///< nullopt until the backpack has been opened once

    // ---- nested backpacks (reported by `carriedBackpacks` through the nested provider) -------
    /// 0 = carried directly by the player. > 0 = stored inside another carried backpack (e.g. by an
    /// Inception upgrade); `where` is then the location of the outermost carried backpack item,
    /// NOT of this backpack (never write to `where` for nested entries).
    int                        depth = 0;
    std::optional<std::string> parent; ///< uuid of the containing backpack (nested entries)
    /// Optional gate for nested entries: which of this backpack's upgrades are active while it is
    /// nested. Empty = all.
    std::function<bool(std::string_view upgradeId, std::string_view group)> upgradeGate;

    /// True if the upgrade (id, group) of this backpack should run (ticks, pickup, ...).
    [[nodiscard]] bool upgradeActive(std::string_view upgradeId, std::string_view group) const {
        return !upgradeGate || upgradeGate(upgradeId, group);
    }
};

/// All backpack items a player carries: main inventory, offhand and chest armor slot.
[[nodiscard]] std::vector<CarriedBackpack> scanBackpacks(Player& player);

/// Like `scanBackpacks`, resolved to loaded backpacks (items without uuid are skipped unless
/// `assignUuids`, which creates their storage and writes the uuid back into the item).
///
/// With `includeNested`, also reports backpacks nested inside carried backpacks when a nested
/// provider is installed (the Inception upgrade does that); those entries come after the directly
/// carried ones and have `depth > 0`. The upgrade ticker and pickup dispatch use this so that
/// upgrades of nested backpacks work as if carried.
[[nodiscard]] std::vector<std::pair<CarriedBackpack, std::shared_ptr<Backpack>>>
carriedBackpacks(Player& player, bool assignUuids = false, bool includeNested = false);

/// Extension point: appends nested backpacks (depth > 0) for the directly carried ones in `list`.
/// Installed by at most one feature (Inception); nullptr removes it.
using NestedBackpackProvider =
    std::function<void(Player& player, std::vector<std::pair<CarriedBackpack, std::shared_ptr<Backpack>>>& list)>;
void setNestedBackpackProvider(NestedBackpackProvider provider);

} // namespace abp::game
