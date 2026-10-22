#ifdef ABP_CLIENT

// Native settings tabs of the Pickup, Filter, Restock and Deposit upgrades (SC PickupUpgradeTab,
// FilterUpgradeTab, SB RestockUpgradeTab, DepositUpgradeTab): the shared FilterControl at
// (x+3, y+24); the Filter upgrade adds its direction button at (x+3, y+24) and moves the control
// to (x+3, y+44). The server side is FilterTab.h (`registerSettingsFilterTab`).

#include <algorithm>

#include "client/Client.h"
#include "client/ui/widgets/FilterControl.h"

namespace abp::upgrade::filtering::client_tabs {

namespace {
using namespace abp::client;
using namespace abp::client::ui;

/// SC FilterUpgradeTab: the direction toggle (BOTH / INPUT / OUTPUT) above the filter control.
class FilterUpgradeTab final : public FilterTab {
public:
    FilterUpgradeTab()
    : FilterTab("filter", 44, [this](TabContext const& ctx, Rect area, WidgetGroup& widgets) { addDirection(ctx, area, widgets); }) {}

    void update(TabContext const& ctx) override {
        mDirection = ctx.upgrade.tab.value("direction", 0);
        FilterTab::update(ctx);
    }

private:
    void addDirection(TabContext const& ctx, Rect area, WidgetGroup& widgets) {
        mDirection   = ctx.upgrade.tab.value("direction", 0);
        auto& button = widgets.add(std::make_unique<ToggleButton>());
        button.size  = Button::Size::Normal;
        button.rect  = {area.x + 3, area.y + 24, 18, 18};
        auto state   = [](filter_icons::Icon icon, std::string text) {
            return ToggleButton::State{
                [icon](Painter& p, float x, float y) { filter_icons::draw(p, icon, x, y); },
                {TooltipLine{std::move(text)}},
            };
        };
        button.states = {
            state(filter_icons::Icon::DirectionBoth, ctr("abp.filter_control.direction_both", "Input & Output")),
            state(filter_icons::Icon::DirectionInput, ctr("abp.filter_control.direction_input", "Input")),
            state(filter_icons::Icon::DirectionOutput, ctr("abp.filter_control.direction_output", "Output")),
        };
        button.current = [this] { return std::clamp(mDirection, 0, 2); };
        auto send      = ctx.sendTabAction;
        button.onClick = [send](int) {
            if (send) send(nlohmann::json{{"op", "direction"}});
        };
    }

    int mDirection = 0;
};

ABP_REGISTER_NATIVE_TAB("pickup", [] { return std::make_unique<FilterTab>(); });
ABP_REGISTER_NATIVE_TAB("advanced_pickup", [] { return std::make_unique<FilterTab>(); });
ABP_REGISTER_NATIVE_TAB("restock", [] { return std::make_unique<FilterTab>(); });
ABP_REGISTER_NATIVE_TAB("advanced_restock", [] { return std::make_unique<FilterTab>(); });
ABP_REGISTER_NATIVE_TAB("deposit", [] { return std::make_unique<FilterTab>(); });
ABP_REGISTER_NATIVE_TAB("advanced_deposit", [] { return std::make_unique<FilterTab>(); });
ABP_REGISTER_NATIVE_TAB("filter", [] { return std::make_unique<FilterUpgradeTab>(); });
ABP_REGISTER_NATIVE_TAB("advanced_filter", [] { return std::make_unique<FilterUpgradeTab>(); });
} // namespace

} // namespace abp::upgrade::filtering::client_tabs

#endif // ABP_CLIENT
