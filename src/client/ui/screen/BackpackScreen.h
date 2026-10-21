#pragma once

// The native Sophisticated Backpacks screen (client companion).
//
// How it is shown: the server opens every backpack menu as its usual fake chest; for a companion
// it also sends the screen model (net/NativeScreens.h). When the chest screen with that container
// id is on top, `ScreenHost` (BackpackScreen.cpp) replaces that screen view's rendering with
// UiRoot, where this layer draws SB's screen; the chest screen underneath keeps the cursor, the
// container session and ESC/E closing, and it never sees our clicks (the layer covers the
// whole screen). Item movements are sent as screen actions and done by the server through the
// menu framework, which syncs the inventory, the cursor and the model back.
//
// Layout values come from Sophisticated Core's StorageScreenBase (core/screen/StorageLayout.h).

#include <cstdint>

namespace abp::client::ui {

/// True while the native backpack screen is drawn this frame (client thread).
[[nodiscard]] bool nativeScreenActive();

} // namespace abp::client::ui
