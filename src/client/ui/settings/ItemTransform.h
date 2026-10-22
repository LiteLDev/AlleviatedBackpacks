#pragma once

class ItemStack;

namespace abp::client::ui {
class Painter;
}

namespace abp::client::ui::settings {

/// Draws an item icon (16 x 16 GUI units times `scale`) centred on (cx, cy) through a transform of
/// the UI world matrix: first `rotZ` degrees in the screen plane (SC GuiHelper.renderGuiItem with a
/// rotation), then `rotY` / `rotX` degrees around the vertical / horizontal axis (SC
/// RotatablePreviewWidget). Flushes the painter first; draws immediately (on top of everything
/// recorded before). Client thread, inside a UI render callback only.
void drawItemTransformed(Painter& p, ItemStack const& stack, float cx, float cy, float scale, float rotX, float rotY, float rotZ);

} // namespace abp::client::ui::settings
