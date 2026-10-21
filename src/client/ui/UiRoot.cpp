#ifdef ABP_CLIENT

#include "client/ui/UiRoot.h"

#include <algorithm>
#include <chrono>
#include <exception>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/render/UIRenderEvent.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/screens/ScreenController.h"
#include "mc/client/gui/screens/ScreenView.h"
#include "mc/client/gui/screens/UIScene.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"

#include "client/Client.h"
#include "client/ui/gfx/Painter.h"
#include "mod/Log.h"

namespace abp::client::ui {

namespace {
ll::event::ListenerPtr gAfterRender;
auto const             gStart = std::chrono::steady_clock::now();
} // namespace

UiRoot& UiRoot::get() {
    static UiRoot instance;
    return instance;
}

void UiRoot::add(IUiLayer* layer) { mLayers.push_back(layer); }

void UiRoot::install() {
    gAfterRender = ll::event::EventBus::getInstance().emplaceListener<ll::event::AfterUIRenderEvent>([this](auto& ev) {
        auto& view = ev.screenView();
        // Several views render with focus in one frame (e.g. "toast_screen" after the inventory);
        // only the top-most scene hosts the layers.
        bool host = view.mHasFocus;
        if (auto* client = currentClient(); host && client) {
            auto top   = client->getTopScreenName();
            auto scene = ev.uiRenderContext().mCurrentScene.getScreenName();
            host       = top.empty() || scene == top;
        }
        if (!host) return;
        try {
            renderFrame(view, ev.uiRenderContext());
        } catch (std::exception const& e) {
            log().error("companion UI render failed: {}", e.what());
        }
    });
}

void UiRoot::uninstall() {
    if (gAfterRender) ll::event::EventBus::getInstance().removeListener(gAfterRender);
    gAfterRender.reset();
    mVisible.clear();
}

void UiRoot::renderFrame(ScreenView& view, MinecraftUIRenderContext& ctx) {
    Painter painter(ctx);
    auto*   client           = currentClient();
    mFrame.screen            = painter.screen();
    mFrame.screenName        = client ? client->getScreenName() : std::string{};
    mFrame.inLevel           = client && client->getLevel() != nullptr;
    auto& controller         = view.mController;
    mFrame.isContainerScreen = controller && controller->_isContainerScreen();
    mFrame.time              = std::chrono::duration<double>(std::chrono::steady_clock::now() - gStart).count();

    std::vector<IUiLayer*> visible;
    for (auto* layer : mLayers) {
        if (layer->visible(mFrame)) visible.push_back(layer);
    }
    std::stable_sort(visible.begin(), visible.end(), [](auto* a, auto* b) { return a->zOrder() < b->zOrder(); });
    for (auto* layer : visible) {
        layer->render(painter, mFrame);
        painter.flush();
    }
    std::reverse(visible.begin(), visible.end());
    mVisible = std::move(visible);
}

bool UiRoot::dispatchMouseButton(MouseButton button, bool down, float x, float y) {
    for (auto* l : mVisible) {
        if (l->onMouseButton(button, down, x, y)) return true;
    }
    return false;
}

bool UiRoot::dispatchMouseMove(float x, float y) {
    for (auto* l : mVisible) {
        if (l->onMouseMove(x, y)) return true;
    }
    return false;
}

bool UiRoot::dispatchScroll(float x, float y, float delta) {
    for (auto* l : mVisible) {
        if (l->onScroll(x, y, delta)) return true;
    }
    return false;
}

bool UiRoot::dispatchKey(int vk, bool down) {
    for (auto* l : mVisible) {
        if (l->onKey(vk, down)) return true;
    }
    return false;
}

bool UiRoot::dispatchChar(std::string const& utf8) {
    for (auto* l : mVisible) {
        if (l->wantsTextInput() && l->onChar(utf8)) return true;
    }
    return false;
}

bool UiRoot::wantsTextInput() const {
    return std::any_of(mVisible.begin(), mVisible.end(), [](auto* l) { return l->wantsTextInput(); });
}

bool UiRoot::contains(float x, float y) const {
    return std::any_of(mVisible.begin(), mVisible.end(), [&](auto* l) { return l->contains(x, y); });
}

bool UiRoot::capturesKey(int vk) const {
    return std::any_of(mVisible.begin(), mVisible.end(), [&](auto* l) { return l->capturesKey(vk); });
}

} // namespace abp::client::ui

#endif // ABP_CLIENT
