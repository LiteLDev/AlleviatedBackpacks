#pragma once

// Sophisticated Backpacks' key bindings, registered with LeviLamina's KeyRegistry so they show up
// (and can be remapped) in Settings > Keyboard & Mouse:
//
// | Binding                      | Default            | Works                                         |
// |------------------------------|--------------------|-----------------------------------------------|
// | Open Backpack                | B                  | in game; over a backpack in any inventory     |
// |                              |                    | screen; in the backpack screen (nested/back/close) |
// | Inventory Interaction        | C                  | in game: Deposit/Restock on the looked-at block |
// | Tool Swap                    | unbound            | in game                                       |
// | Toggle Upgrade 1 / 2         | Alt+Z / Alt+X      | anywhere in a world                           |
// | Toggle Upgrade 3..5          | unbound            | anywhere in a world (with Alt)                |
// | Sort                         | middle mouse       | backpack screen, not over the player inventory |
// | Transfer to Storage/Inventory| [ / ] (Shift = all)| backpack screen                               |
//
// Toggle Upgrade keys need Alt held (the game's bindings are single keys, so the modifier is
// fixed and the binding's name says "Alt+").

#include "client/ui/UiLayer.h"

namespace abp::client::input {

enum class KeyAction {
    OpenBackpack,
    InventoryInteraction,
    ToolSwap,
    ToggleUpgrade1,
    ToggleUpgrade2,
    ToggleUpgrade3,
    ToggleUpgrade4,
    ToggleUpgrade5,
    Sort,
    TransferToStorage,
    TransferToInventory,
};

/// The keyboard key `vk` is (one of) the keys currently bound to `action`. Any thread.
[[nodiscard]] bool keyMatches(KeyAction action, int vk);
/// The mouse button is bound to `action` (e.g. Sort = middle button). Any thread.
[[nodiscard]] bool mouseMatches(KeyAction action, ui::MouseButton button);

} // namespace abp::client::input
