#pragma once

// Client: the armor stand preview of the smithing tab (SB SmithingUpgradeTab: an armor stand
// without base plate, with arms, body turned to 210 degrees and seen 25 degrees from above,
// rendered at scale 25 with its feet at the given point; the result is worn in its equipment
// slot, anything else held in the off hand).
//
// Drawn in software from the vanilla armor stand's box layout (sticks, shoulder plank, waist):
// each box is projected and its visible faces are filled scanline by scanline with the
// painter's rectangles, lit by a fixed light. The item is drawn where the stand wears or holds it.

#ifdef ABP_CLIENT

class ItemStack;

namespace abp::client::ui {
class Painter;
}

namespace abp::upgrade::smithing::client {

/// Where an item goes on the armor stand (SB: armor in its equipment slot, else the off hand).
enum class StandSlot { Head, Chest, Legs, Feet, Offhand };
[[nodiscard]] StandSlot standSlotFor(ItemStack const& stack);

/// Draws the stand with its feet at (feetX, feetY), `scale` GUI units per block, wearing `item`
/// (may be null / empty).
void drawArmorStand(::abp::client::ui::Painter& p, float feetX, float feetY, float scale, ItemStack const* item);

} // namespace abp::upgrade::smithing::client

#endif // ABP_CLIENT
