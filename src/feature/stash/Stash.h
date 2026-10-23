#pragma once

// Right-click stashing (Sophisticated Backpacks `BackpackItem.overrideStackedOnOther` /
// `overrideOtherStackedOnMe` and Sophisticated Core's stash overlay), server side.
//
// With an item on the cursor, a right click on a slot either
//   - stashes the slot's items into the backpack on the cursor (cursor = backpack, count 1), or
//   - stashes the cursor into the backpack in the slot (slot = backpack, count 1).
// The client draws "+" / "-" on the slots where that would do something (green: the backpack
// already holds that item or a memory slot remembers it; yellow: only space) and shows
// "Right Click To Add" with the backpack's contents over the hovered one. The results come from
// the server (`query`), since only the server knows filters, slot limits and memory slots.
//
// Slots are addressed by location (`{"area": "...", "slot": n}`):
//   inventory 0-35, armor 0-3, offhand 0 - the player's own slots;
//   container n                     - the open vanilla container (chest, barrel, shulker box, ...);
//   vanilla.<container> n           - any other slot of the open vanilla screen (stashableContainer);
//   storage n                       - the open backpack screen's storage.
//
// Thread: server thread.

#include <optional>
#include <string_view>
#include <string>

#include "nlohmann/json.hpp"

#include "core/ItemStackData.h"

class ItemStack;
class Player;

namespace abp::game {
class Backpack;
}

namespace abp::feature::stash {

/// SC `IStashStorageItem.StashResult` (numeric values are the wire format).
enum class Result { NoSpace = 0, Space = 1, MatchAndSpace = 2 };

struct Location {
    enum class Area { Inventory, Armor, Offhand, Container, Storage, Vanilla };
    Area area = Area::Inventory;
    int  slot = 0;
    /// Area::Vanilla: the `ContainerEnumName` of the vanilla screen collection (the client maps
    /// its collection name, e.g. "crafting_input_items"); on the wire the area is
    /// "vanilla.<number>".
    int container = -1;

    [[nodiscard]] bool operator==(Location const&) const = default;
};

/// GX-3: SB's overrideStackedOnOther / overrideOtherStackedOnMe work on any slot of any container
/// menu. Besides the player's slots and chest-like containers, these vanilla screen containers
/// (`ContainerEnumName` values) take part: the crafting grids, the furnace / blast furnace /
/// smoker slots (taking from the result slot pops the furnace's experience, like vanilla), the
/// brewing stand, anvil, smithing table, enchanting table, grindstone, stonecutter, cartography
/// table, loom, beacon, villager trade inputs, crafter and horse equipment. Result previews
/// (crafting output, anvil result, ...) are not slots items can be taken from, so they don't.
[[nodiscard]] bool stashableContainer(int containerEnumName);

[[nodiscard]] std::optional<Location> parseLocation(nlohmann::json const& j);
[[nodiscard]] std::string            areaName(Location::Area area);
/// "inventory:5" (the key of query results).
[[nodiscard]] std::string key(Location const& loc);

/// SB `BackpackItem.getItemStashable`: can `item` go into `backpack` (and does the backpack
/// already hold that item / remember it in a memory slot)?
[[nodiscard]] Result stashable(Player* player, game::Backpack& backpack, core::ItemStackData const& item);
/// Same for a backpack item (a backpack that was never opened has no storage yet: Space).
[[nodiscard]] Result stashableInto(Player* player, ItemStack const& backpackItem, core::ItemStackData const& item);
/// SC `ClientEventHandler.getStashResultAndTooltip(inSlot, held)`: the overlay of one slot.
[[nodiscard]] Result overlay(Player& player, Location const& loc);

/// A right click with the cursor on `loc`. Returns the number of items moved (0 = nothing
/// happened; the click then falls back to the normal slot click).
int rightClick(Player& player, Location const& loc);

/// `{"slots": [{"area":..,"slot":..}, ..]}` -> `{"results": {"<key>": 1|2, ..}}` (NoSpace omitted).
[[nodiscard]] nlohmann::json query(Player& player, nlohmann::json const& body);

/// The item at a location as the server sees it (empty data when out of range / not open).
[[nodiscard]] core::ItemStackData itemAt(Player& player, Location const& loc);

/// A backpack with this uuid lies in the player's open vanilla container (its contents tooltip
/// may be shown to them).
[[nodiscard]] bool inOpenContainer(Player& player, std::string const& uuid);

} // namespace abp::feature::stash
