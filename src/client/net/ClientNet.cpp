#ifdef ABP_CLIENT

#include "client/net/ClientNet.h"

#include <chrono>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/network/NetEventCallback.h"
#include "mc/network/ServerNetworkHandler.h"

#include "client/Client.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "net/Packets.h"

namespace abp::client::net {

namespace {
using Clock = std::chrono::steady_clock;
using abp::net::ContentsPacket;
using abp::net::ContentsRequestPacket;
using abp::net::HelloPacket;
using abp::net::KeyActionPacket;
using abp::net::RenderInfoPacket;
using abp::net::RenderInfoRequestPacket;
using abp::net::ScreenActionPacket;
using abp::net::ScreenClosePacket;
using abp::net::ScreenOpenPacket;
using abp::net::ScreenUpdatePacket;
using abp::net::WelcomePacket;

constexpr int    kHelloEveryTicks = 40; ///< 2 s
constexpr int    kHelloAttempts   = 15;
constexpr double kContentsEvery   = 1.0; ///< SC REFRESH_INTERVAL: 20 ticks

double now() {
    static auto const start = Clock::now();
    return std::chrono::duration<double>(Clock::now() - start).count();
}

struct State {
    bool                                             ready        = false;
    bool                                             incompatible = false;
    int                                              helloSent    = 0;
    int                                              helloTimer   = 0;
    std::optional<core::screen::BackpackScreenModel> screen;
    std::optional<core::screen::BackpackScreenModel> closing; ///< last screen, for the switch grace period
    double                                           closingAt = 0;
    std::uint64_t                                    screenRev = 0;
    std::unordered_map<std::string, CachedContents>  contents;
    std::unordered_map<std::string, double>          contentsAsked;
    std::unordered_map<std::string, core::screen::RenderInfo> render;
    std::unordered_map<std::string, double>          renderAsked;
};
State                       gState;
std::vector<ScreenListener> gListeners;

void screenChanged() {
    ++gState.screenRev;
    for (auto const& fn : gListeners) {
        try {
            fn();
        } catch (std::exception const& e) {
            log().error("screen listener failed: {}", e.what());
        }
    }
}

/// True when a server-to-client packet really reached the client (not this process's server).
bool fromServer(NetEventCallback& callback) {
    auto handler = ll::service::getServerNetworkHandler();
    return !handler || static_cast<NetEventCallback*>(handler.as_ptr()) != &callback;
}

void reset() {
    bool hadScreen = gState.screen.has_value();
    gState         = {};
    if (hadScreen) screenChanged();
}

void sendHello() {
    HelloPacket hello{nlohmann::json{
        {"version", abp::net::modVersion()},
        {"features", {"native_screen", "contents_tooltip", "keybinds", "render_info"}},
    }};
    hello.sendToServer();
    ++gState.helloSent;
    if (gState.helloSent == 1) log().info("companion: hello sent to the server");
}
} // namespace

bool serverReady() { return gState.ready; }

void sendKeyAction(nlohmann::json body) {
    if (!gState.ready) return;
    KeyActionPacket{std::move(body)}.sendToServer();
}

void sendScreenAction(nlohmann::json body) {
    if (!gState.ready || !gState.screen) return;
    body["session"] = gState.screen->session;
    ScreenActionPacket{std::move(body)}.sendToServer();
}

core::screen::BackpackScreenModel const* screenModel() { return gState.screen ? &*gState.screen : nullptr; }

std::uint64_t screenRevision() { return gState.screenRev; }

void dropScreen(std::uint32_t session) {
    if (!gState.screen || gState.screen->session != session) return;
    gState.closing   = std::move(gState.screen);
    gState.closingAt = now();
    gState.screen.reset();
    screenChanged();
}

core::screen::BackpackScreenModel const* displayModel() {
    if (gState.screen) return &*gState.screen;
    constexpr double kGrace = 0.35;
    if (gState.closing && now() - gState.closingAt < kGrace) return &*gState.closing;
    return nullptr;
}

CachedContents const* contents(std::string const& uuid) {
    if (uuid.empty()) return nullptr;
    if (gState.ready) {
        auto& asked = gState.contentsAsked[uuid];
        if (now() - asked >= kContentsEvery || asked == 0) {
            asked = now();
            ContentsRequestPacket{nlohmann::json{{"uuid", uuid}}}.sendToServer();
        }
    }
    auto it = gState.contents.find(uuid);
    return it == gState.contents.end() ? nullptr : &it->second;
}

core::screen::RenderInfo const* renderInfo(std::string const& uuid) {
    if (uuid.empty()) return nullptr;
    auto it = gState.render.find(uuid);
    if (it != gState.render.end()) return &it->second;
    if (gState.ready) {
        auto& asked = gState.renderAsked[uuid];
        if (asked == 0 || now() - asked >= 5.0) {
            asked = now();
            RenderInfoRequestPacket{nlohmann::json{{"uuid", uuid}}}.sendToServer();
        }
    }
    return nullptr;
}

void refreshRenderInfo(std::string const& uuid) {
    if (uuid.empty() || !gState.ready) return;
    auto& asked = gState.renderAsked[uuid];
    if (asked != 0 && now() - asked < 1.0) return;
    asked = now();
    RenderInfoRequestPacket{nlohmann::json{{"uuid", uuid}}}.sendToServer();
}

void onScreenChanged(ScreenListener fn) { gListeners.push_back(std::move(fn)); }

// ---- packet handlers (client thread) --------------------------------------------------------------

class WelcomeHandler : public ll::network::PacketHandlerBase<WelcomeHandler, WelcomePacket> {
public:
    void handlePacket(NetworkIdentifier const&, NetEventCallback& cb, WelcomePacket const& p) const {
        if (!fromServer(cb)) return;
        if (!p.valid() || p.body.value("incompatible", false)) {
            if (!gState.incompatible) {
                log().warn("companion: the server's AlleviatedBackpacks is incompatible (protocol {}); using the vanilla UI", p.version);
            }
            gState.incompatible = true;
            gState.ready        = false;
            return;
        }
        if (!gState.ready) log().info("companion: server ready (server mod {})", p.body.value("version", std::string{}));
        gState.ready = true;
    }
};

class ScreenOpenHandler : public ll::network::PacketHandlerBase<ScreenOpenHandler, ScreenOpenPacket> {
public:
    void handlePacket(NetworkIdentifier const&, NetEventCallback& cb, ScreenOpenPacket const& p) const {
        if (!fromServer(cb) || !p.valid()) return;
        gState.screen = core::screen::BackpackScreenModel::fromJson(p.body);
        gState.closing.reset();
        log().info(
            "companion: native screen {} for '{}' ({} slots, container {})",
            gState.screen->session,
            gState.screen->title,
            gState.screen->storageSlots,
            gState.screen->containerId
        );
        screenChanged();
    }
};

class ScreenUpdateHandler : public ll::network::PacketHandlerBase<ScreenUpdateHandler, ScreenUpdatePacket> {
public:
    void handlePacket(NetworkIdentifier const&, NetEventCallback& cb, ScreenUpdatePacket const& p) const {
        if (!fromServer(cb) || !p.valid() || !gState.screen) return;
        if (p.body.value("session", 0u) != gState.screen->session) return;
        if (!core::screen::applyPatch(*gState.screen, p.body.value("patch", nlohmann::json::object()))) {
            log().warn("companion: bad screen patch; asking for the full model");
            sendScreenAction(nlohmann::json{{"action", "resync"}});
        }
        screenChanged();
    }
};

class ScreenCloseHandler : public ll::network::PacketHandlerBase<ScreenCloseHandler, ScreenClosePacket> {
public:
    void handlePacket(NetworkIdentifier const&, NetEventCallback& cb, ScreenClosePacket const& p) const {
        if (!fromServer(cb) || !p.valid()) return;
        dropScreen(p.body.value("session", 0u));
    }
};

class ContentsHandler : public ll::network::PacketHandlerBase<ContentsHandler, ContentsPacket> {
public:
    void handlePacket(NetworkIdentifier const&, NetEventCallback& cb, ContentsPacket const& p) const {
        if (!fromServer(cb) || !p.valid()) return;
        auto m = core::screen::ContentsModel::fromJson(p.body);
        if (m.uuid.empty()) return;
        if (gState.contents.size() > 256) gState.contents.clear();
        auto uuid               = m.uuid;
        gState.contents[uuid]   = CachedContents{std::move(m), now()};
    }
};

class RenderInfoHandler : public ll::network::PacketHandlerBase<RenderInfoHandler, RenderInfoPacket> {
public:
    void handlePacket(NetworkIdentifier const&, NetEventCallback& cb, RenderInfoPacket const& p) const {
        if (!fromServer(cb) || !p.valid()) return;
        auto r = core::screen::RenderInfo::fromJson(p.body);
        if (r.uuid.empty()) return;
        if (gState.render.size() > 512) gState.render.clear();
        auto uuid           = r.uuid;
        gState.render[uuid] = std::move(r);
    }
};

// ---- module -----------------------------------------------------------------------------------------

class ClientNetModule {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::ClientLevelTickEvent>([](auto&) {
            if (gState.ready || gState.incompatible || gState.helloSent >= kHelloAttempts) return;
            if (!currentPlayer()) return;
            if (--gState.helloTimer > 0) return;
            gState.helloTimer = kHelloEveryTicks;
            sendHello();
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::ClientExitLevelEvent>([](auto&) { reset(); }));
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        reset();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace abp::client::net

ABP_REGISTER_MODULE(abp::client::net::ClientNetModule);

#endif // ABP_CLIENT
