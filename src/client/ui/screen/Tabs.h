#pragma once

// Tab API of the native backpack screen.
//
// Sophisticated Backpacks shows the settings of an installed upgrade in a tab on the right of the
// backpack screen (one tab open at a time; the Backpack Settings tab always comes first). Units
// add the tab of their upgrade with `registerNativeTab` (or `ABP_REGISTER_NATIVE_TAB`):
//
//     class MagnetTab : public abp::client::ui::NativeTab {
//         Size build(TabContext const& ctx, Rect area, WidgetGroup& widgets) override {
//             auto& grid  = widgets.add(std::make_unique<SlotGrid>());
//             grid.rect   = {area.x + 3, area.y + 22, 9 * 18, 18};
//             grid.count  = 9;
//             grid.ghost  = true;
//             grid.item   = [this](int i) { return mFilter.size() > i ? &mFilter[i] : nullptr; };
//             grid.onClick = [ctx](int i, int button, bool) { ctx.sendTabAction({{"slot", i}, {"button", button}}); };
//             return {9 * 18 + 6 + 6, 22 + 18 + 6};
//         }
//         void update(TabContext const& ctx) override { /* read ctx.upgrade.tab (server data) */ }
//     };
//     ABP_REGISTER_NATIVE_TAB("magnet", [] { return std::make_unique<MagnetTab>(); });
//
// Data flows server -> client through `UpgradeView::tab` (server: `ABP_REGISTER_TAB_DATA`) and
// client -> server through `TabContext::sendTabAction` (server: `ABP_REGISTER_TAB_ACTION`).
// Optional keys of the tab data read by the host: "tabLabel" (open tab title, SC `translUpgrade`,
// e.g. "Adv. Pickup") and "tabTooltip" (closed tab tooltip, e.g. "Advanced Pickup Settings"); both
// default to the upgrade's name.
// Upgrades with settings but without a native tab get the built-in generic tab (description +
// a button that opens the upgrade's server-side settings UI).

#include <functional>
#include <memory>
#include <string>

#include "nlohmann/json.hpp"

#include "client/ui/widgets/Widgets.h"
#include "core/screen/ScreenModel.h"
#include "mod/Registry.h"

class ItemStack;

namespace abp::client::ui {

struct TabContext {
    core::screen::BackpackScreenModel const& model;
    int                                      slot; ///< upgrade slot index
    core::screen::UpgradeView const&         upgrade;
    ItemStack const*                         upgradeIcon = nullptr;
    /// Sends `{"action":"tab","slot":slot,"data":data}` to the server's tab action handlers.
    std::function<void(nlohmann::json data)> sendTabAction;
    /// Sends any native screen action (see net/NativeScreens.h).
    std::function<void(nlohmann::json body)> sendScreenAction;
};

/// The content of one open upgrade tab.
class NativeTab {
public:
    struct Size {
        float w = 0, h = 0;
    };
    virtual ~NativeTab() = default;
    /// Adds the open tab's widgets inside `area` (the tab's top-left corner; the title is drawn at
    /// +20,+8 by the host, so content usually starts at y + 22). Returns the open tab size
    /// (content extent plus SC's 6 unit right/bottom border).
    virtual Size build(TabContext const& ctx, Rect area, WidgetGroup& widgets) = 0;
    /// The screen model changed (new server data in `ctx.upgrade.tab`). Called only while the tab
    /// is open, after `build`. Widgets added in `build` live until the tab closes or is rebuilt:
    /// a tab may keep pointers to them for `update`, but must not use them at any other time.
    virtual void update(TabContext const& /*ctx*/) {}
    /// Asked after `update`: true makes the host clear the widgets and call `build` again (the
    /// open size changed, e.g. a filter switched to tag matching).
    virtual bool needsRebuild() { return false; }
    /// Extra drawing above the widgets.
    virtual void render(Painter& /*p*/, TabContext const& /*ctx*/, Rect /*area*/, float /*mx*/, float /*my*/) {}
};

using NativeTabFactory = std::function<std::unique_ptr<NativeTab>()>;

/// Registers the tab of an upgrade id (`"*"` = fallback for every upgrade with settings).
void registerNativeTab(std::string upgradeId, NativeTabFactory factory);
/// A new tab for `upgradeId` (the fallback when none is registered; nullptr when no fallback).
[[nodiscard]] std::unique_ptr<NativeTab> createNativeTab(std::string const& upgradeId);
/// True if a tab (not the fallback) is registered for `upgradeId`.
[[nodiscard]] bool hasNativeTab(std::string const& upgradeId);

namespace detail {
struct NativeTabRegistrar {
    NativeTabRegistrar(std::string id, NativeTabFactory f) { registerNativeTab(std::move(id), std::move(f)); }
};
} // namespace detail

} // namespace abp::client::ui

/// Registers a native upgrade tab: ABP_REGISTER_NATIVE_TAB("magnet", [] { return std::make_unique<MagnetTab>(); });
#define ABP_REGISTER_NATIVE_TAB(id, factory)                                                                           \
    static ::abp::client::ui::detail::NativeTabRegistrar ABP_UNIQUE(abp_native_tab_)(id, factory)
