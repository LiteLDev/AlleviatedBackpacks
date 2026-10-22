#pragma once

#include <atomic>
#include <vector>

#include "client/ui/UiLayer.h"
#include "mod/Registry.h"

class ScreenView;
class MinecraftUIRenderContext;

namespace abp::client::ui {

/// Hosts the IUiLayers: renders them after the focused screen's UI pass and dispatches input
/// to them (`dispatch*` run on the client thread, called by input::InputRouter).
class UiRoot {
public:
    static UiRoot& get();

    void install();
    void uninstall();

    void add(IUiLayer* layer); ///< static-init registration; layers are never removed

    [[nodiscard]] FrameInfo const& lastFrame() const noexcept { return mFrame; }
    void                           setMouse(float x, float y) noexcept { mFrame.mouseX = x, mFrame.mouseY = y; }

    bool               dispatchMouseButton(MouseButton button, bool down, float x, float y);
    bool               dispatchMouseMove(float x, float y);
    bool               dispatchScroll(float x, float y, float delta);
    bool               dispatchKey(int vk, bool down);
    bool               dispatchChar(std::string const& utf8);
    [[nodiscard]] bool wantsTextInput() const;
    [[nodiscard]] bool contains(float x, float y) const;
    [[nodiscard]] bool capturesKey(int vk) const;

    /// Renders all visible layers for this view. Called after the focused view's UI pass, and by
    /// screen hosts that replace a view's rendering.
    void renderFrame(ScreenView& view, MinecraftUIRenderContext& ctx);

private:
    std::vector<IUiLayer*> mLayers;
    std::vector<IUiLayer*> mVisible; // visible in the last frame, descending zOrder
    FrameInfo              mFrame;
};

namespace detail {
struct UiLayerRegistrar {
    explicit UiLayerRegistrar(IUiLayer* layer) { UiRoot::get().add(layer); }
};
} // namespace detail

} // namespace abp::client::ui

/// Registers a layer instance with static storage duration: ABP_REGISTER_UI_LAYER(gMyLayer);
#define ABP_REGISTER_UI_LAYER(INSTANCE)                                                                                \
    static ::abp::client::ui::detail::UiLayerRegistrar ABP_UNIQUE(abp_ui_layer_)(&(INSTANCE))
