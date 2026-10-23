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
// | Toggle Upgrade 3..5          | unbound            | anywhere in a world                           |
// | Sort                         | middle mouse       | backpack screen, not over the player inventory |
// | Transfer to Storage/Inventory| [ / ] (Shift = all)| backpack screen                               |
//
// Toggle Upgrade 1/2 need Alt held (SB's KeyModifier.ALT defaults; the game's bindings are single
// keys, so the modifier stays fixed when they are remapped); 3..5 have no modifier, like SB's.
// Open Backpack may be bound to a mouse button (also the side buttons). Bedrock's controls list
// has no categories: each binding's description names SB's ("Sophisticated Backpacks" /
// "Sophisticated Mods").

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
/// The key `action` is bound to, for display ("V", "Middle Button", "Not Bound"). Client thread.
[[nodiscard]] std::string keyDisplayName(KeyAction action);

/// GX-3: the game's own bindings that container screens follow (vanilla AbstractContainerScreen
/// keyPressed: inventory key closes, drop key throws the hovered stack, hotbar keys swap), read
/// from the keyboard layout like ours ("key.inventory", "key.drop", "key.hotbar.1".."9").
enum class VanillaKey { Inventory, Drop, Hotbar1, Hotbar2, Hotbar3, Hotbar4, Hotbar5, Hotbar6, Hotbar7, Hotbar8, Hotbar9 };
/// `vk` is bound to `key` (defaults E, Q, 1..9 until the layout is read). Any thread.
[[nodiscard]] bool vanillaKeyMatches(VanillaKey key, int vk);
/// The hotbar slot (0..8) `vk` is bound to, or -1. Any thread.
[[nodiscard]] int hotbarKeyIndex(int vk);

} // namespace abp::client::input
