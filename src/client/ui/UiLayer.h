#pragma once

#include <string>

#include "client/ui/Geometry.h"

namespace abp::client::ui {

class Painter;

/// Per-frame information about the screen being drawn on. GUI units throughout.
struct FrameInfo {
    Rect        screen;
    std::string screenName; ///< e.g. "inventory_screen", "chest_screen", "hud_screen"
    bool        isContainerScreen = false;
    bool        inLevel           = false;
    float       mouseX = -1, mouseY = -1; ///< last known cursor position (-1 when unknown)
    double      time   = 0;               ///< seconds since mod start (animations)
};

enum class MouseButton { Left, Right, Middle };

/// Something the client companion draws and that can receive input (ported from BEI). Layers
/// live for the whole session (static instances registered with `ABP_REGISTER_UI_LAYER`).
///
/// Rendering happens on the client thread after the focused screen's UI pass, or in place of a
/// replaced screen view (the native backpack screen). Input arrives on the client thread through
/// `input::InputRouter`, in descending zOrder, only for layers visible in the last frame;
/// returning true consumes the event (vanilla UI does not see it).
class IUiLayer {
public:
    virtual ~IUiLayer() = default;

    [[nodiscard]] virtual char const* name() const   = 0;
    [[nodiscard]] virtual int         zOrder() const = 0;
    [[nodiscard]] virtual bool        visible(FrameInfo const& frame) const = 0;
    virtual void                      render(Painter& painter, FrameInfo const& frame) = 0;

    virtual bool onMouseButton(MouseButton, bool /*down*/, float /*x*/, float /*y*/) { return false; }
    virtual bool onMouseMove(float /*x*/, float /*y*/) { return false; }
    virtual bool onScroll(float /*x*/, float /*y*/, float /*delta*/) { return false; }
    /// Windows virtual-key code.
    virtual bool onKey(int /*vk*/, bool /*down*/) { return false; }
    /// Typed text (UTF-8), only delivered while some layer has text focus.
    virtual bool onChar(std::string const& /*utf8*/) { return false; }
    [[nodiscard]] virtual bool wantsTextInput() const { return false; }
    /// The point is over this layer (its clicks / wheel never reach vanilla).
    [[nodiscard]] virtual bool contains(float /*x*/, float /*y*/) const { return false; }
    /// The layer wants every key press while visible (decided on the window thread from the last
    /// frame): e.g. the native screen consumes the keys it binds.
    [[nodiscard]] virtual bool capturesKey(int /*vk*/) const { return false; }
};

} // namespace abp::client::ui
