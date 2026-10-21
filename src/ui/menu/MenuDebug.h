#pragma once

#include <format>
#include <utility>

#include "mod/Config.h"
#include "mod/Log.h"

namespace abp::ui {

/// `config/menu.json`: chest GUI diagnostics.
struct MenuConfig {
    int  version = 1;
    /// Log every fake-chest packet the server sends (UpdateBlock, BlockActorData, ContainerOpen,
    /// ContainerClose) and every ContainerClose it receives; on the client build also what the
    /// client receives and sees. For debugging menus on a real client; off by default. The client
    /// part is only hooked if this is on when the mod is enabled (not after `/backpack reload`).
    bool debugPackets = false;
};

[[nodiscard]] inline bool menuDebug() { return config::get<MenuConfig>().debugPackets; }

template <class... Args>
void menuDebugLog(std::format_string<Args...> fmt, Args&&... args) {
    if (menuDebug()) log().info("[menu-dbg] {}", std::format(fmt, std::forward<Args>(args)...));
}

} // namespace abp::ui
