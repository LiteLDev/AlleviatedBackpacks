#pragma once

// 3D backpack icons in the GUI (P2-11).
//
// SB renders the backpack model in the GUI with its "gui" display transform (rotation 30/225/0,
// translation 0/1.25/0, scale 0.9), tinted per backpack and with the part configuration (front
// pouch / battery, pouches / tanks) and the tank fluids / battery charge. Bedrock GUI icons are
// sprites, so tools/models/backpack.ps1 renders that view of our model offline, split into layers
// per part configuration (textures/ui/alleviatedbackpacks/icons/c<battery><left tank><right tank>_*):
// cloth and border in grey (tinted here with the main / accent colour), the untinted parts per
// tier, the tank fluid per level (tinted with the fluid colour), the battery charge per step and
// the glass. The companion UI (Painter::item) draws backpacks with these layers instead of the item
// sprite. Vanilla slots keep the item sprite: the default model view (the same render), tinted
// with the main colour through the item colour.

class ItemStack;

namespace abp::client::ui {
class Painter;
}

namespace abp::client::render {

/// Draws `stack` as the layered 3D backpack icon (16 * scale GUI units, top left at x, y).
/// Returns false (draws nothing) for anything that isn't a backpack.
bool drawBackpackIcon(ui::Painter& painter, ItemStack const& stack, float x, float y, float scale = 1, float alpha = 1);

} // namespace abp::client::render
