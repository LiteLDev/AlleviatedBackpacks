#pragma once

// Server side of the native upgrade tabs of "workstation" upgrades (anvil, smithing): the tab's
// slots are the station's slot objects (`InputSlot` / `OutputSlot`), driven by tab actions from
// the client (`{"click":"<slot>","button":0|1,"shift":b}`) like a click on a chest slot.

#include <memory>

#include "nlohmann/json.hpp"

class ItemStack;
class Player;

namespace abp::game {
class Backpack;
}
namespace abp::ui {
class Slot;
}

namespace abp::upgrade::anvil {

/// One click on a station slot of a native tab. Left/right with the cursor work like a chest slot
/// (take, place, half, one, swap). Shift-click moves the slot's content into the backpack storage
/// first and then into the player inventory (SC UpgradeContainerBase.mergeIntoStorageFirst: SB's
/// anvil and smithing default "shift click into storage"). Returns whether something moved.
bool clickStationSlot(Player& player, std::shared_ptr<ui::Slot> const& slot, int button, bool shift, game::Backpack& backpack);

/// JSON of a stack for tab data (null for an empty stack).
[[nodiscard]] nlohmann::json stackJson(ItemStack const& stack);

} // namespace abp::upgrade::anvil
