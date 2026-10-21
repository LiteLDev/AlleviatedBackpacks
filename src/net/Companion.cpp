#include "net/Companion.h"

#include <mutex>
#include <unordered_map>

#include "ll/api/command/CommandHandle.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/event/server/ServerStoppingEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/network/NetEventCallback.h"
#include "mc/network/NetworkIdentifier.h"
#include "mc/network/ServerNetworkHandler.h"
#include "mc/server/ServerPlayer.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"

#include "mod/Commands.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "mod/Module.h"
#include "net/Packets.h"

namespace abp::net {

namespace {
std::int64_t keyOf(Player const& p) { return p.getOrCreateUniqueID().rawID; }

std::unordered_map<std::int64_t, CompanionInfo> gCompanions; // server thread
TestSink                                        gSink;
std::function<bool(Player const&)>              gJsonUi;
} // namespace

std::string modVersion() {
    auto const& manifest = Mod::getInstance().getSelf().getManifest();
    return manifest.version ? manifest.version->to_string() : std::string("0.0.0");
}

std::vector<std::string> serverFeatures() {
    return {"native_screen", "contents_tooltip", "keybinds", "render_info"};
}

bool hasCompanion(Player const& player) { return gCompanions.contains(keyOf(player)); }

std::optional<CompanionInfo> companionInfo(Player const& player) {
    auto it = gCompanions.find(keyOf(player));
    if (it == gCompanions.end()) return std::nullopt;
    return it->second;
}

void setCompanion(Player const& player, std::optional<CompanionInfo> info) {
    if (info) gCompanions[keyOf(player)] = std::move(*info);
    else gCompanions.erase(keyOf(player));
}

std::size_t companionCount() { return gCompanions.size(); }

UiMode uiModeFor(Player const& player) {
    if (hasCompanion(player)) return UiMode::Native;
    if (gJsonUi && gJsonUi(player)) return UiMode::JsonUi;
    return UiMode::Chest;
}

void setJsonUiDecider(std::function<bool(Player const&)> decider) { gJsonUi = std::move(decider); }

void setTestSink(TestSink sink) { gSink = std::move(sink); }

namespace detail {
void send(Player& player, ll::network::Packet const& packet, std::string_view name, nlohmann::json const& body) {
    if (!hasCompanion(player)) return;
    if (player.isSimulatedPlayer()) {
        if (gSink) gSink(player, name, body);
        return;
    }
    packet.sendTo(player);
}
} // namespace detail

Player* senderOf(NetworkIdentifier const& source, NetEventCallback& callback, SubClientId subId) {
    auto handler = ll::service::getServerNetworkHandler();
    if (!handler) return nullptr;
    // Only act when the packet reached this process's server (never on the client side).
    if (static_cast<NetEventCallback*>(handler.as_ptr()) != &callback) return nullptr;
    return handler->$_getServerPlayer(source, subId);
}

// ---- handshake ----------------------------------------------------------------------------------

class HelloHandler : public ll::network::PacketHandlerBase<HelloHandler, HelloPacket> {
public:
    void handlePacket(NetworkIdentifier const& source, NetEventCallback& callback, HelloPacket const& packet) const {
        auto* player = senderOf(source, callback, packet.mSenderSubId);
        if (!player) return;
        if (!packet.valid()) {
            log().warn(
                "companion: {} has an incompatible client mod (protocol {}, server {}); using the vanilla UI",
                player->getRealName(),
                packet.version,
                core::screen::kProtocolVersion
            );
            // Tell the client so it stops asking; it knows the version from the packet header.
            WelcomePacket reply{nlohmann::json{{"version", modVersion()}, {"incompatible", true}}};
            reply.sendTo(*player);
            return;
        }
        CompanionInfo info;
        info.version = packet.body.value("version", std::string{});
        if (auto it = packet.body.find("features"); it != packet.body.end() && it->is_array()) {
            for (auto const& f : *it) {
                if (f.is_string()) info.features.push_back(f.get<std::string>());
            }
        }
        bool fresh = !hasCompanion(*player);
        setCompanion(*player, info);
        if (fresh) log().info("companion: {} uses the client mod {} (native screens on)", player->getRealName(), info.version);
        send(*player, WelcomePacket{nlohmann::json{{"version", modVersion()}, {"features", serverFeatures()}}});
    }
};

// ---- module ---------------------------------------------------------------------------------------

class CompanionRegistry {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerDisconnectEvent>([](ll::event::PlayerDisconnectEvent& ev) {
            setCompanion(ev.self(), std::nullopt);
        }));
        // Client: local worlds come and go (and reuse player ids).
        mListeners.push_back(bus.emplaceListener<ll::event::ServerStoppingEvent>([](auto&) { gCompanions.clear(); }));
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        gCompanions.clear();
        gSink = nullptr;
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace abp::net

ABP_REGISTER_MODULE_ORDERED(abp::net::CompanionRegistry, abp::ModuleOrder::Menu + 5);

// `/backpack companion`: who runs the client mod.
ABP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    cmd.overload().text("companion").execute([](CommandOrigin const& origin, CommandOutput& output) {
        auto* entity = origin.getEntity();
        if (entity && entity->isPlayer()) {
            auto& player = static_cast<Player&>(*entity);
            auto  info   = abp::net::companionInfo(player);
            output.success(
                info ? "companion: yes (client mod " + info->version + ")" : std::string("companion: no (vanilla UI)")
            );
        }
        output.success("companion: " + std::to_string(abp::net::companionCount()) + " player(s) with the client mod");
    });
});
