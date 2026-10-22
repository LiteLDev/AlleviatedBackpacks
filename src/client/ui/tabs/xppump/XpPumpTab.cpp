#ifdef ABP_CLIENT

// Experience Pump upgrade tab on the native backpack screen (SC XpPumpUpgradeTab): automation
// direction, the "%s lvls" level selector, the mending toggle and the take / store buttons.

#include <algorithm>
#include <cmath>

#include "client/Client.h"
#include "client/ui/screen/Tabs.h"
#include "client/ui/tabs/tank/FluidGfx.h"
#include "client/ui/widgets/SbStyle.h"

namespace abp::client::ui {
namespace {

namespace fg = fluidgfx;

IconFn iconFn(fg::Icon i, bool crossed = false) {
    return [i, crossed](Painter& p, float x, float y) {
        fg::icon(p, i, x + 1, y + 1);
        if (crossed) fg::noSign(p, x + 1, y + 1);
    };
}

constexpr std::uint32_t kControls = 0x555555; ///< ChatFormatting.DARK_GRAY (+ italic)

/// SC Button that also reacts to the mouse wheel (take / store: number of levels).
class ScrollButton final : public Button {
public:
    std::function<void(float delta)> onScroll;
    bool scroll(float x, float y, float delta) override {
        if (!hovered(x, y) || !onScroll) return false;
        onScroll(delta);
        return true;
    }
};

/// SC XpPumpUpgradeTab.LevelSelector: 54x18, "%s lvls" centred, the wheel changes the level.
class LevelSelector final : public Widget {
public:
    std::function<int()>             level;
    std::function<void(float delta)> onScroll;

    void render(Painter& p, float, float, double) override {
        sb::controlBackground(p, rect);
        auto text = ctr("abp.xppump.level_select", "{} lvls", "§f" + std::to_string(level ? level() : 0) + "§7");
        float w   = p.textWidth(text);
        float x   = rect.x + std::floor((rect.w - w) / 2);
        p.text(text, x, rect.y + 5, Color::rgb(0xAAAAAA));
    }
    bool scroll(float x, float y, float delta) override {
        if (!hovered(x, y) || !onScroll) return false;
        onScroll(delta);
        return true;
    }
    bool mouseDown(MouseButton, float x, float y) override { return hovered(x, y); }
    void tooltip(float x, float y, std::vector<TooltipLine>& out) const override {
        if (!hovered(x, y)) return;
        out.push_back({ctr("abp.xppump.level_select.tooltip", "Level at which Pump Stops"), 0xFFFFFF});
        out.push_back({"§o" + ctr("abp.xppump.level_select.tooltip.controls", "Scroll to change"), kControls});
    }
};

class XpPumpTab final : public NativeTab {
public:
    Size build(TabContext const& ctx, Rect area, WidgetGroup& widgets) override {
        update(ctx);
        auto send = ctx.sendTabAction;
        auto op   = [send](nlohmann::json data) {
            if (send) send(std::move(data));
        };
        float yOff = 24;
        auto& dir  = widgets.add(std::make_unique<ToggleButton>());
        dir.rect   = {area.x + 3, area.y + yOff, 18, 18};
        dir.states = {
            {iconFn(fg::Icon::PumpInput), {{ctr("abp.xppump.button.input", "Pump Experience From Player"), 0xFFFFFF}}},
            {iconFn(fg::Icon::XpOutput), {{ctr("abp.xppump.button.output", "Pump Experience To Player"), 0xFFFFFF}}},
            {iconFn(fg::Icon::XpKeep), {{ctr("abp.xppump.button.keep", "Keep Player at Level"), 0xFFFFFF}}},
            {iconFn(fg::Icon::Off), {{ctr("abp.xppump.button.off", "Pump Off"), 0xFFFFFF}}},
        };
        dir.current = [this] {
            auto d = mXp.value("direction", std::string("input"));
            return d == "output" ? 1 : d == "keep" ? 2 : d == "off" ? 3 : 0;
        };
        dir.onClick = [op](int) { op({{"op", "direction"}}); };

        auto& sel    = widgets.add(std::make_unique<LevelSelector>());
        sel.rect     = {area.x + 21, area.y + yOff, 54, 18};
        sel.level    = [this] { return mXp.value("level", 10); };
        sel.onScroll = [op](float d) { op({{"op", "level"}, {"delta", d > 0 ? 1 : -1}}); };
        yOff += 20;

        if (mXp.value("mendingOn", true)) {
            auto& mend   = widgets.add(std::make_unique<ToggleButton>());
            mend.rect    = {area.x + 3, area.y + yOff, 18, 18};
            mend.states  = {
                {iconFn(fg::Icon::Mend), {{ctr("abp.xppump.button.mend_items", "Repair Items With Mending"), 0xFFFFFF}}},
                {iconFn(fg::Icon::Mend, true), {{ctr("abp.xppump.button.do_not_mend_items", "Do Not Repair Items"), 0xFFFFFF}}},
            };
            mend.current = [this] { return mXp.value("mendItems", true) ? 0 : 1; };
            mend.onClick = [op](int) { op({{"op", "mend"}}); };
            yOff += 20;
        }

        auto& takeAll        = widgets.add(std::make_unique<Button>());
        takeAll.size         = Button::Size::Normal;
        takeAll.rect         = {area.x + 3, area.y + yOff, 18, 18};
        takeAll.icon         = iconFn(fg::Icon::TakeAll);
        takeAll.tooltipLines = {{ctr("abp.xppump.button.take_all_experience", "Take All Experience"), 0xFFFFFF}};
        takeAll.onClick      = [op](int b) {
            (void)b; // SC: the action buttons ignore which mouse button
            op({{"op", "action"}, {"what", "takeAll"}});
        };

        auto& take   = widgets.add(std::make_unique<ScrollButton>());
        take.size    = Button::Size::Normal;
        take.rect    = {area.x + 21, area.y + yOff, 18, 18};
        take.icon    = iconFn(fg::Icon::Take);
        take.onClick = [op](int b) {
            (void)b; // SC: the action buttons ignore which mouse button
            op({{"op", "action"}, {"what", "take"}});
        };
        take.onScroll       = [op](float d) { op({{"op", "levelsToTake"}, {"delta", d > 0 ? 1 : -1}}); };
        take.dynamicTooltip = [this]() -> std::vector<TooltipLine> {
            return {
                {ctr("abp.xppump.button.take_levels", "Take {} Levels", "§a" + std::to_string(mXp.value("levelsToTake", 1)) + "§f"), 0xFFFFFF},
                {"§o" + ctr("abp.xppump.button.take_levels.controls", "Scroll to change # of levels"), kControls},
            };
        };

        auto& store   = widgets.add(std::make_unique<ScrollButton>());
        store.size    = Button::Size::Normal;
        store.rect    = {area.x + 39, area.y + yOff, 18, 18};
        store.icon    = iconFn(fg::Icon::Store);
        store.onClick = [op](int b) {
            (void)b; // SC: the action buttons ignore which mouse button
            op({{"op", "action"}, {"what", "store"}});
        };
        store.onScroll       = [op](float d) { op({{"op", "levelsToStore"}, {"delta", d > 0 ? 1 : -1}}); };
        store.dynamicTooltip = [this]() -> std::vector<TooltipLine> {
            return {
                {ctr("abp.xppump.button.store_levels", "Store {} Levels", "§c" + std::to_string(mXp.value("levelsToStore", 1)) + "§f"), 0xFFFFFF},
                {"§o" + ctr("abp.xppump.button.store_levels.controls", "Scroll to change # of levels"), kControls},
            };
        };

        auto& storeAll        = widgets.add(std::make_unique<Button>());
        storeAll.size         = Button::Size::Normal;
        storeAll.rect         = {area.x + 57, area.y + yOff, 18, 18};
        storeAll.icon         = iconFn(fg::Icon::StoreAll);
        storeAll.tooltipLines = {{ctr("abp.xppump.button.store_all_experience", "Store All Experience"), 0xFFFFFF}};
        storeAll.onClick      = [op](int b) {
            (void)b; // SC: the action buttons ignore which mouse button
            op({{"op", "action"}, {"what", "storeAll"}});
        };
        return {21 + 54 + 6, yOff + 18 + 6};
    }

    void update(TabContext const& ctx) override { mXp = ctx.upgrade.tab.value("xpPump", nlohmann::json::object()); }

private:
    nlohmann::json mXp = nlohmann::json::object();
};

} // namespace
} // namespace abp::client::ui

ABP_REGISTER_NATIVE_TAB("xp_pump", [] { return std::make_unique<abp::client::ui::XpPumpTab>(); });

#endif // ABP_CLIENT
