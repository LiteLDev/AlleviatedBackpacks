#pragma once

// Client companion (client build only): the parts of AlleviatedBackpacks that run in the game
// client itself, next to the integrated server of local worlds.
//
//   client/net/      companion protocol, client side (handshake, screen model, contents cache)
//   client/ui/       native UI host: Painter, UiRoot/layers, widgets, the backpack screen, Tab API
//   client/input/    input routing (window thread -> client thread), key bindings, slot hover
//   client/tooltip/  Shift contents tooltip on backpack items
//   client/render/   render hook scaffolding (backpack models, tint, particles, block displays)
//
// Threads: rendering, network handlers and posted input work run on the client thread; raw
// mouse/keyboard events arrive on the window thread (see input/InputRouter.h). Never keep game
// pointers (screens, players, items) across frames.

#include <string>
#include <string_view>

#include "core/Format.h"

class ClientInstance;
class LocalPlayer;
class ClientLevel;

namespace abp::client {

/// Current client instance, or nullptr before the game finished starting.
[[nodiscard]] ClientInstance* currentClient();
/// Local player, or nullptr when not in a world.
[[nodiscard]] LocalPlayer* currentPlayer();
/// True while the client is in a world.
[[nodiscard]] bool inLevel();

/// The game's current language ("en_US", "zh_CN", ...).
[[nodiscard]] std::string clientLocale();
/// Mod translation in the game's language (`assets/lang/<locale>/*.json`), `{}` placeholders.
[[nodiscard]] std::string translateClient(std::string_view key, std::string_view fallback);
template <class... Args>
[[nodiscard]] std::string ctr(std::string_view key, std::string_view fallback, Args const&... args) {
    return core::formatMessageArgs(translateClient(key, fallback), args...);
}

} // namespace abp::client
