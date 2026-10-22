#pragma once

// Right-click stashing, client side (SB `BackpackItem.overrideStackedOnOther` /
// `overrideOtherStackedOnMe`, SC `ClientEventHandler.onContainerScreenForeground` /
// `onDrawScreen`).
//
// While an item is on the cursor in a container screen (vanilla screens and the native backpack
// screen), every other slot where a right click would stash gets a sign: "+" at (x+10, y+8) on a
// backpack that can take the cursor, "-" at (x+1, y) on an item the backpack on the cursor can
// take; green when the backpack already holds that item (or remembers it in a memory slot),
// yellow (0xFFFF00) when it only has space. The hovered slot shows "Right Click To Add" with the
// backpack's contents instead. A right click there stashes (server action `stash`), anything else
// stays a normal click. Not in the creative inventory.
//
// The results come from the server (key action `stashQuery` -> `StashInfoPacket`), refreshed
// every 10 ticks and whenever the cursor or a slot changes. Slots are addressed like the server's
// `feature::stash::Location`: area "inventory" | "armor" | "offhand" | "container" | "storage".

#include <string_view>

class ItemStack;

namespace abp::client::ui {
class Painter;
}

namespace abp::client::input::stash {

/// SC `IStashStorageItem.StashResult` as seen by the client (None = unknown / no space).
enum class Result { None = 0, Space = 1, Match = 2 };

/// Stashing works in this screen right now (an item on the cursor, not the creative inventory).
[[nodiscard]] bool active();
/// The overlay result for a slot showing `item` (the last server answer for that slot and the
/// current cursor). Registers the slot for the next query. Client thread, every frame.
[[nodiscard]] Result result(std::string_view area, int slot, ItemStack const& item);
/// SC `renderStashSign` over the slot's 16x16 item area (`scale` = item size / 16).
void drawSign(ui::Painter& painter, Result result, ItemStack const& item, float x, float y, float scale = 1);
/// SC `renderSpecialTooltip` for the hovered slot: "Right Click To Add" + the contents of the
/// backpack involved. Returns false (draws nothing) when the slot isn't stashable.
bool drawTooltip(ui::Painter& painter, std::string_view area, int slot, ItemStack const& item, float mouseX, float mouseY);
/// A right click with the cursor on the slot: sends the stash action when the overlay says it
/// does something. Returns true when the click was taken.
bool rightClick(std::string_view area, int slot, ItemStack const& item);

/// The vanilla screen's hovered slot shows the stashing tooltip (its own hover text is hidden).
[[nodiscard]] bool vanillaTooltipShown();

} // namespace abp::client::input::stash
