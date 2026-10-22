#pragma once

// Icons of the settings screen that the shared SbStyle set doesn't have (original pixel art, drawn
// from SC's icon sizes only; nothing is copied from the Java mod).

#include <cstdint>

#include "client/ui/gfx/Painter.h"

namespace abp::client::ui::settings {

enum class Icon {
    ItemDisplayTab,   ///< 16x16 tab icon
    Rotate,           ///< 16x16
    Depth,            ///< 16x16 (SC "z offset")
    SaveTemplate,     ///< 16x16
    LoadTemplate,     ///< 16x16
    ExportTemplate,   ///< 16x16
    AnotherPlayerOn,  ///< 16x16
    AnotherPlayerOff, ///< 16x16
};

void icon(Painter& p, Icon which, float x, float y, float alpha = 1);

/// SC SLOT_SELECTION (24x24): the frame around the item display's selected slot, drawn at
/// (slot x - 4, slot y - 4).
void slotSelection(Painter& p, float x, float y);

/// SC memorized stack overlay (GUI_CONTROLS 77,0 16x16): the faded veil over a memory slot.
void memoryOverlay(Painter& p, float x, float y);

} // namespace abp::client::ui::settings
