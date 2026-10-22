#pragma once

// Client side of the companion protocol (see net/Packets.h). Client thread only.

#include <cstdint>
#include <functional>
#include <string>

#include "nlohmann/json.hpp"

#include "core/screen/ScreenModel.h"

namespace abp::client::net {

/// The server answered our Hello with a compatible Welcome (this connection).
[[nodiscard]] bool serverReady();

/// Key binding actions (`KeyActionPacket`); dropped when the server has no companion support.
void sendKeyAction(nlohmann::json body);
/// Native screen actions (`ScreenActionPacket`); the current session id is added.
void sendScreenAction(nlohmann::json body);

// ---- native screen model ----------------------------------------------------------------------

/// Model of the backpack screen the server opened for us (nullptr when none).
[[nodiscard]] core::screen::BackpackScreenModel const* screenModel();
/// What the screen host shows: the open screen, or for a short grace period after the server
/// closed it the last one (so a menu switch - close + open - never flashes the world).
[[nodiscard]] core::screen::BackpackScreenModel const* displayModel();
/// Bumps on every change of the screen model (open, patch, close).
[[nodiscard]] std::uint64_t screenRevision();
/// Forget the screen model (its container screen went away).
void dropScreen(std::uint32_t session);

// ---- contents tooltip ---------------------------------------------------------------------------

struct CachedContents {
    core::screen::ContentsModel model;
    double                      receivedAt = 0; ///< seconds (steady clock)
};
/// Cached contents of a backpack (nullptr = never received). Asks the server for fresh contents
/// at most every 20 ticks (1 s) while called (SC's REFRESH_INTERVAL).
[[nodiscard]] CachedContents const* contents(std::string const& uuid);
/// Render info of a backpack (nullptr = unknown yet; requests it).
[[nodiscard]] core::screen::RenderInfo const* renderInfo(std::string const& uuid);
/// Asks the server again for the render info of a backpack that is being rendered (at most once a
/// second per backpack), so model parts, colours, fluid levels and charge stay current.
void refreshRenderInfo(std::string const& uuid);

/// Listeners for screen model changes (e.g. the screen host).
using ScreenListener = std::function<void()>;
void onScreenChanged(ScreenListener fn);

} // namespace abp::client::net
