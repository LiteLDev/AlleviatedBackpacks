#pragma once

// Server side of the native backpack screen (client companion).
//
// Mechanism: a backpack menu is still opened as the usual server-authoritative fake chest
// (`ui::MenuManager`). For a companion player the server additionally sends the screen model
// (`ScreenOpenPacket`, right before the chest's ContainerOpen) and model patches every tick
// (`ScreenUpdatePacket`). The client hides the chest screen's own rendering and draws the
// Sophisticated Backpacks screen in its place; the chest screen underneath keeps the cursor,
// ESC/E closing and the container session. Every item movement the native screen asks for
// (`ScreenActionPacket`) runs through `MenuManager::simulate`/`simulateOn`, i.e. through the very
// same slot objects and rules as a click in the chest (locked backpack slot, filters, upgrade
// install/remove checks). Nothing the client claims about items is trusted.
//
// Screen actions (`ScreenActionPacket` body, all carry "session"):
//   {"action":"click","area":"storage|inventory|upgrade","index":n,"button":0|1,"shift":b,"ctrl":b}
//   {"action":"double","area":"storage|inventory","index":n}        collect matching items on the cursor
//   {"action":"drop","area":"storage|inventory","index":n,"all":b}  Q / Ctrl+Q over a slot
//   {"action":"hotbar","area":"storage|inventory","index":n,"hotbar":0..8}  number keys
//   {"action":"dropCursor","all":b}                           click outside the screen with an item
//   {"action":"sort"} | {"action":"sortBy"} | {"action":"transfer","to":"storage|inventory","matching":b}
//   {"action":"toggleUpgrade","slot":n} | {"action":"settings"} | {"action":"upgradeSettings","slot":n}
//   {"action":"tab","slot":n,"data":{..}}  (upgrade native tab -> `ABP_REGISTER_TAB_ACTION`)
//   {"action":"openNested","index":n} | {"action":"back"} | {"action":"close"} | {"action":"resync"}

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "nlohmann/json.hpp"

#include "core/screen/ScreenModel.h"
#include "mod/Registry.h"

class Player;

namespace abp::game {
class Backpack;
}
namespace abp::ui {
class BackpackMenu;
}
namespace abp::upgrade {
class UpgradeContext;
}

namespace abp::net {

/// Data an upgrade's native tab needs (sent in `UpgradeView::tab`), e.g. filter contents or
/// cooking progress. Runs on every model rebuild of an open screen.
struct TabDataProvider {
    std::string                                                              upgrade; ///< id or group
    std::function<nlohmann::json(Player&, upgrade::UpgradeContext const&)> data;
};
using TabDataRegistry = StaticRegistry<TabDataProvider>;

/// Handles `{"action":"tab","slot":n,"data":{..}}` sent by an upgrade's native tab. Return true
/// when handled (the screen is re-synced afterwards).
struct TabActionHandler {
    std::string                                                                           upgrade; ///< id or group
    std::function<bool(Player&, upgrade::UpgradeContext const&, nlohmann::json const& data)> handle;
};
using TabActionRegistry = StaticRegistry<TabActionHandler>;

/// Extra lines for the backpack contents tooltip, after the stack multiplier and tank lines (e.g.
/// "§f1.2k§c FE" from an energy unit). Lines are formatted for `player` (`trFor`).
using ContentsLineProvider = std::function<void(Player& player, game::Backpack& backpack, std::vector<std::string>& lines)>;
using ContentsLineRegistry = StaticRegistry<ContentsLineProvider>;

/// Intercepts a right click (button 1, no Shift) with an item on the cursor on a slot of the native
/// screen, before the default click handling: `area` is "storage" or "inventory", `menu` the open
/// backpack menu (nullptr on other menus, "inventory" only). Return true when handled (the
/// player's inventory and the screen are re-synced), false to fall through. E.g. the Ender
/// Linker's "right click a storage with the linker" (Sophisticated Core's overrideStackedOnOther).
struct ScreenClickInterceptor {
    std::string                                                                                   id;
    std::function<bool(Player&, ui::BackpackMenu* menu, std::string_view area, int index)> handle;
};
using ScreenClickInterceptRegistry = StaticRegistry<ScreenClickInterceptor>;

/// A key action (`KeyActionPacket` {"action": name, ...}) added by a feature unit. Returns
/// whether it did something.
struct KeyActionExtension {
    std::string                                              action;
    std::function<bool(Player&, nlohmann::json const& body)> handle;
};
using KeyActionRegistry = StaticRegistry<KeyActionExtension>;

/// The model `player` sees for `menu` (session 0, containerId -1).
[[nodiscard]] core::screen::BackpackScreenModel buildScreenModel(Player& player, ui::BackpackMenu& menu);
/// Session id of the player's open native screen (0 = none).
[[nodiscard]] std::uint32_t nativeSession(Player const& player);
/// Last model sent to the player (nullptr = none). Selftests.
[[nodiscard]] core::screen::BackpackScreenModel const* sentModel(Player const& player);
/// Runs one screen action (the packet handler and selftests). Returns whether it was accepted.
bool handleScreenAction(Player& player, nlohmann::json const& body);

/// Contents of a backpack for the Shift tooltip.
[[nodiscard]] core::screen::ContentsModel buildContents(Player& player, std::shared_ptr<game::Backpack> const& backpack);
[[nodiscard]] core::screen::RenderInfo    buildRenderInfo(game::Backpack const& backpack);

/// Runs one key action (`KeyActionPacket`; see KeyActions.cpp). Returns whether it did something.
bool handleKeyAction(Player& player, nlohmann::json const& body);

namespace detail {
/// Contents.cpp: drop the request rate state of a player who left.
void forgetContentsRates(Player const& player);
struct TabDataRegistrar {
    explicit TabDataRegistrar(TabDataProvider p) { TabDataRegistry::add(std::move(p)); }
};
struct TabActionRegistrar {
    explicit TabActionRegistrar(TabActionHandler h) { TabActionRegistry::add(std::move(h)); }
};
struct ContentsLineRegistrar {
    explicit ContentsLineRegistrar(ContentsLineProvider p) { ContentsLineRegistry::add(std::move(p)); }
};
struct ScreenClickInterceptRegistrar {
    explicit ScreenClickInterceptRegistrar(ScreenClickInterceptor i) { ScreenClickInterceptRegistry::add(std::move(i)); }
};
struct KeyActionRegistrar {
    explicit KeyActionRegistrar(KeyActionExtension h) { KeyActionRegistry::add(std::move(h)); }
};
} // namespace detail

} // namespace abp::net

/// Adds data for an upgrade's native tab (see `abp::net::TabDataProvider`).
#define ABP_REGISTER_TAB_DATA(...) static ::abp::net::detail::TabDataRegistrar ABP_UNIQUE(abp_tab_data_)(__VA_ARGS__)
/// Adds a handler for actions of an upgrade's native tab (see `abp::net::TabActionHandler`).
#define ABP_REGISTER_TAB_ACTION(...)                                                                                   \
    static ::abp::net::detail::TabActionRegistrar ABP_UNIQUE(abp_tab_action_)(__VA_ARGS__)
/// Adds contents tooltip lines (see `abp::net::ContentsLineProvider`).
#define ABP_REGISTER_CONTENTS_LINES(...)                                                                               \
    static ::abp::net::detail::ContentsLineRegistrar ABP_UNIQUE(abp_contents_lines_)(__VA_ARGS__)
/// Adds a native screen right-click interceptor (see `abp::net::ScreenClickInterceptor`).
#define ABP_REGISTER_SCREEN_CLICK(...)                                                                                 \
    static ::abp::net::detail::ScreenClickInterceptRegistrar ABP_UNIQUE(abp_screen_click_)(__VA_ARGS__)
/// Adds a key action (see `abp::net::KeyActionExtension`).
#define ABP_REGISTER_KEY_ACTION(...)                                                                                   \
    static ::abp::net::detail::KeyActionRegistrar ABP_UNIQUE(abp_key_action_)(__VA_ARGS__)
