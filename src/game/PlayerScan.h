#pragma once

#include <memory>
#include <optional>
#include <string>
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
};

/// All backpack items a player carries: main inventory, offhand and chest armor slot.
[[nodiscard]] std::vector<CarriedBackpack> scanBackpacks(Player& player);

/// Like `scanBackpacks`, resolved to loaded backpacks (items without uuid are skipped unless
/// `assignUuids`, which creates their storage and writes the uuid back into the item).
[[nodiscard]] std::vector<std::pair<CarriedBackpack, std::shared_ptr<Backpack>>>
carriedBackpacks(Player& player, bool assignUuids = false);

} // namespace abp::game
