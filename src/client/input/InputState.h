#pragma once

// Internal to src/client/input: state shared between the window thread (raw input events) and the
// client thread (game update / rendering / input handlers).

#include <array>
#include <atomic>
#include <cstdint>

#include "client/ui/UiLayer.h"

class ContainerScreenController;

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
    /// The hovered vanilla slot holds a backpack in one of the player's own slots (Open Backpack
    /// on a mouse button swallows that click).
    std::atomic<bool>  hoverBackpack{false};
    std::atomic<std::int64_t> rightPressMs{0}; ///< steady-clock ms of the last right button press
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
/// The vanilla container screen controller that ticked last (cleared when it leaves). Client
/// thread; only valid while that screen is shown.
[[nodiscard]] ::ContainerScreenController const* activeContainerController();
void                                             forgetContainerController();

void registerHotkeys(); ///< once per process, at mod load
void installHotkeys();
void uninstallHotkeys();
/// Window thread: a key was pressed (not a repeat) and no layer took it.
void onHotkeyKeyDown(int vk);
/// Window thread: a mouse button (key-mapping code, e.g. -97 = middle, -96/-95 = X1/X2) was
/// pressed outside the companion layers. Returns true when the click should be swallowed.
bool onHotkeyMouseDown(int code);
[[nodiscard]] int mouseButtonCode(ui::MouseButton button);
[[nodiscard]] int mouseExtraCode(int which); ///< 1 = X1, 2 = X2

// ---- right-click stashing (Stash.cpp) ----------------------------------------------------------
void installStash();
void uninstallStash();

} // namespace abp::client::input::detail
