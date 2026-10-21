#pragma once

// Internal to src/client/input: state shared between the window thread (raw input events) and the
// client thread (game update / rendering / input handlers).

#include <array>
#include <atomic>
#include <cstdint>

#include "client/ui/UiLayer.h"

namespace abp::client::input::detail {

/// Snapshot of client-thread state that window-thread callbacks use to decide synchronously
/// whether to swallow an event. Written on the client thread, read anywhere.
struct Snapshot {
    std::atomic<bool>  inLevel{false};
    std::atomic<bool>  cursorScreen{false}; ///< a screen with a free cursor is on top
    std::atomic<bool>  overUi{false};       ///< the last cursor position is over a layer
    std::atomic<bool>  textFocus{false};    ///< a companion text box has focus
    std::atomic<bool>  vanillaTextFocus{false};
    std::atomic<float> invGuiScale{1.f};
    /// Keys a visible layer captures (bit per virtual-key code), rebuilt every frame.
    std::array<std::atomic<std::uint64_t>, 4> capturedKeys{};

    [[nodiscard]] bool captured(int vk) const {
        if (vk < 0 || vk >= 256) return false;
        return (capturedKeys[static_cast<std::size_t>(vk / 64)].load() >> (vk % 64)) & 1u;
    }
};

[[nodiscard]] Snapshot& snapshot();

void installSlotHover();
void uninstallSlotHover();
void clearSlotHover();

void registerHotkeys(); ///< once per process, at mod load
void installHotkeys();
void uninstallHotkeys();
/// Window thread: a key was pressed (not a repeat) and no layer took it.
void onHotkeyKeyDown(int vk);
/// Window thread: a mouse button was pressed outside the companion layers.
void onHotkeyMouseDown(ui::MouseButton button);

} // namespace abp::client::input::detail
