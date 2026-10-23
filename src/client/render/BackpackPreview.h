#pragma once

// The real 3D backpack model in a UI panel (GX-1): SC ItemDisplaySettingsTab's rotatable preview
// (`renderItemDisplayPreviewItem`) draws the backpack's baked model plus its display item
// (SB BackpackItemStackRenderer), turned about the model's centre.
//
// Bedrock draws the backpack model through its attachable (ModelState.h), so the preview is a
// client-only copy of the placed-backpack display entity (`alleviatedbackpacks:backpack_display`,
// EntityPreview.h) wearing the backpack and holding its display item: exactly what a placed
// backpack shows, tinted, with its tanks / battery / item display.

class ItemStack;

namespace abp::client::ui {
class Painter;
struct Rect;
} // namespace abp::client::ui

namespace abp::client::render {

/// SC renderItemDisplayPreviewItem: `stack`'s model centred at (cx, cy), `scale` GUI units per
/// block, turned `xRot` degrees about the screen's horizontal axis (seen from above) and `yRot`
/// degrees about its vertical axis (SB passes yAxisRotation + 180). Clipped to `clip` when given.
/// Returns false (draws nothing) when the model can't be drawn (no world, not a backpack, ...).
bool drawBackpackModel(ui::Painter& painter, ItemStack const& stack, float cx, float cy, float scale, float xRot, float yRot, ui::Rect const* clip = nullptr);

} // namespace abp::client::render
