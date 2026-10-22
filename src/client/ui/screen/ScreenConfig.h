#pragma once

// Client settings of the native backpack screen: Sophisticated Core's client config
// (`Config.CLIENT`), in `config/client_gui.json` of the client mod.

#include <string>

#include "core/screen/StorageLayout.h"

namespace abp::client::ui {

struct ClientGuiConfig {
    int version = 1;
    /// SC `sortButtonsPosition`: "title_line_right" (default), "below_upgrades",
    /// "below_upgrade_tabs" or "hidden".
    std::string sortButtonsPosition = "title_line_right";
    /// SC `playButtonSound`: click sound when the screen's buttons are clicked.
    bool playButtonSound = true;
    /// SC `InventoryScrollPanel.restrictScrollToScrollbar` (set by SC's Mouse Tweaks compat): the
    /// mouse wheel over a storage slot does not scroll the storage.
    bool restrictScrollToScrollbar = false;
};

[[nodiscard]] ClientGuiConfig const& clientGuiConfig();
[[nodiscard]] core::screen::SortButtonsPosition sortButtonsPosition();

/// SC `GuiSoundHelper.playButtonClickSound` (UI click, when `playButtonSound` is on).
void playButtonClickSound();

} // namespace abp::client::ui
