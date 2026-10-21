#pragma once

// Server side of the companion handshake: which players run the AlleviatedBackpacks client mod.
//
// The client mod sends `HelloPacket` once it is in a world (every few seconds until answered);
// the server records the player and answers `WelcomePacket`. Server-to-client companion packets
// are only ever sent to recorded players (`send`), so vanilla clients never receive them.
//
// The client mod is required: companions get native Sophisticated Backpacks screens for every menu
// (MenuManager "virtual" presentation, see NativeScreens.cpp), players without it are refused
// backpack screens with a message, and selftests' simulated players keep the chest sessions.
// `uiModeFor` reports what a player gets.

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "nlohmann/json.hpp"

#include "mc/common/SubClientId.h"

class Player;
class NetworkIdentifier;
class NetEventCallback;

namespace ll::network {
class Packet;
}

namespace abp::net {

struct CompanionInfo {
    std::string              version;  ///< client mod version
    std::vector<std::string> features; ///< e.g. "native_screen", "contents_tooltip", "keybinds"
};

/// True if `player`'s client runs the companion (sent a valid Hello). Server thread.
[[nodiscard]] bool hasCompanion(Player const& player);
[[nodiscard]] std::optional<CompanionInfo> companionInfo(Player const& player);
/// Records (or forgets, with nullopt) a companion. The handshake uses it; selftests mark
/// simulated players with it (their packets then go to the test sink, see `setTestSink`).
void setCompanion(Player const& player, std::optional<CompanionInfo> info);
/// Number of players currently recorded.
[[nodiscard]] std::size_t companionCount();

/// How the server shows backpack screens to a player.
enum class UiMode {
    Native,   ///< companion client: native SB screens (no container screen at all)
    JsonUi,   ///< reserved (the JSON UI unit was cancelled; only if a decider is installed)
    Chest,    ///< no client mod: screens are refused (simulated players: chest sessions)
};
/// Native for companions; otherwise JsonUi if a decider says so (none is installed), else Chest.
[[nodiscard]] UiMode uiModeFor(Player const& player);
/// Optional decider for a resource-pack UI (none ships). nullptr removes it.
void setJsonUiDecider(std::function<bool(Player const&)> decider);

/// Receives server-to-client packets meant for simulated players (selftests).
using TestSink = std::function<void(Player& player, std::string_view packet, nlohmann::json const& body)>;
void setTestSink(TestSink sink);

namespace detail {
void send(Player& player, ll::network::Packet const& packet, std::string_view name, nlohmann::json const& body);
}

/// Sends a companion packet to `player` (dropped if the player has no companion).
template <class P>
void send(Player& player, P const& packet) {
    detail::send(player, packet, packet.getName(), packet.body);
}

/// The server player that sent a client-to-server packet, or nullptr when `callback` is not this
/// process's server network handler (a client must never act on client-to-server packets).
[[nodiscard]] Player* senderOf(NetworkIdentifier const& source, NetEventCallback& callback, SubClientId subId);

/// The `features` this server advertises in Welcome.
[[nodiscard]] std::vector<std::string> serverFeatures();

} // namespace abp::net
