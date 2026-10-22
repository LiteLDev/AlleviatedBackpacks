#pragma once

// Client: the armor stand preview of the smithing tab (SB SmithingUpgradeTab: an armor stand
// with its body turned to 210 degrees, seen 25 degrees from above, scale 25, feet at the given
// point, wearing the result in its equipment slot or holding anything else in the off hand).
//
// The stand is the game's own armor stand: a client-only display entity from
// `client/render/EntityPreview.h` (never added to the level), with the result equipped through
// `Actor::setArmor` / `setOffhandSlot`, drawn by the game's actor renderer (armor models, trims
// and enchantment glint included).

#ifdef ABP_CLIENT

class ItemStack;

namespace abp::client::ui {
class Painter;
}

namespace abp::upgrade::smithing::client {

/// Where an item goes on the armor stand (SB: armor in its equipment slot, else the off hand).
enum class StandSlot { Head, Chest, Legs, Feet, Offhand };
[[nodiscard]] StandSlot standSlotFor(ItemStack const& stack);

/// Draws the armor stand with its feet at (feetX, feetY), `scale` GUI units per block, wearing
/// `item` (may be null / empty). Draws nothing if the entity can't be created or rendered.
void drawArmorStand(::abp::client::ui::Painter& p, float feetX, float feetY, float scale, ItemStack const* item);

} // namespace abp::upgrade::smithing::client

#endif // ABP_CLIENT
