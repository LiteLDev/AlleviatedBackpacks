#ifdef ABP_CLIENT

// Pump / Advanced Pump upgrade tabs on the native backpack screen (SC PumpUpgradeTab.Basic /
// .Advanced and FluidFilterControl).

#include <algorithm>

#include "client/Client.h"
#include "client/ui/screen/Tabs.h"
#include "client/ui/tabs/tank/FluidGfx.h"
#include "client/ui/widgets/SbStyle.h"

namespace abp::client::ui {
namespace {

namespace fg = fluidgfx;

std::vector<TooltipLine> lines(std::string const& text) {
    // SB button tooltips may span lines ("Interact With\nFluid Container in Hand").
    std::vector<TooltipLine> out;
    std::size_t              start = 0;
    while (true) {
        auto nl = text.find('\n', start);
        out.push_back({text.substr(start, nl == std::string::npos ? std::string::npos : nl - start), 0xFFFFFF});
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return out;
}

IconFn iconFn(fg::Icon i, bool crossed = false) {
    return [i, crossed](Painter& p, float x, float y) {
        fg::icon(p, i, x + 1, y + 1);
        if (crossed) fg::noSign(p, x + 1, y + 1);
    };
}

/// SC FluidFilterControl: `count` 18x18 fluid slots; click = set from the cursor's container.
class FluidFilter final : public Widget {
public:
    nlohmann::json const*            filter = nullptr; ///< owned by the tab
    std::function<void(int)>         onClick;

    [[nodiscard]] int slotAt(float x, float y) const {
        if (!hovered(x, y) || y < rect.y + 1 || y >= rect.y + 17) return -1;
        int i = static_cast<int>((x - rect.x) / 18);
        return i >= 0 && i < count() ? i : -1;
    }
    [[nodiscard]] int count() const { return filter && filter->is_array() ? static_cast<int>(filter->size()) : 0; }

    void render(Painter& p, float mx, float my, double) override {
        for (int i = 0; i < count(); ++i) {
            float x = rect.x + 1 + static_cast<float>(i) * 18, y = rect.y + 1;
            sb::slotFrame(p, x, y);
            auto const& f = (*filter)[static_cast<std::size_t>(i)];
            if (f.is_object()) fg::fluid(p, x, y, 16, 16, f);
            if (slotAt(mx, my) == i) p.fillRect({x, y, 16, 16}, sb::color::HOVER, Painter::Layer::Foreground);
        }
    }
    bool mouseDown(MouseButton, float x, float y) override {
        if (!hovered(x, y)) return false;
        int i = slotAt(x, y);
        if (i >= 0 && onClick) onClick(i);
        return true;
    }
    void tooltip(float x, float y, std::vector<TooltipLine>& out) const override {
        int i = slotAt(x, y);
        if (i < 0) return;
        auto const& f = (*filter)[static_cast<std::size_t>(i)];
        if (f.is_object()) out.push_back({f.value("name", std::string{}), 0xFFFFFF});
    }
};

class PumpTab final : public NativeTab {
public:
    Size build(TabContext const& ctx, Rect area, WidgetGroup& widgets) override {
        update(ctx);
        auto send   = ctx.sendTabAction;
        auto toggle = [send](char const* what) {
            return [send, what](int) {
                if (send) send(nlohmann::json{{"op", "toggle"}, {"what", what}});
            };
        };
        auto& io  = widgets.add(std::make_unique<ToggleButton>());
        io.rect   = {area.x + 3, area.y + 24, 18, 18};
        io.states = {
            {iconFn(fg::Icon::PumpInput), lines(ctr("abp.pump.button.input", "Input"))},
            {iconFn(fg::Icon::PumpOutput), lines(ctr("abp.pump.button.output", "Output"))},
        };
        io.current = [this] { return mPump.value("input", true) ? 0 : 1; };
        io.onClick = toggle("input");
        if (!mPump.value("advanced", false)) return {3 + 18 + 6, 24 + 18 + 6};

        struct Def {
            float       x;
            char const* what;
            fg::Icon    icon;
            std::string on, off;
        };
        Def const defs[] = {
            {21, "handlers", fg::Icon::Pipes, ctr("abp.pump.button.interact_with_tanks_and_pipes", "Interact With Tanks & Pipes"),
             ctr("abp.pump.button.do_not_interact_with_tanks_and_pipes", "Do Not Interact With Tanks & Pipes")},
            {39, "world", fg::Icon::World, ctr("abp.pump.button.interact_with_world", "Interact With World"),
             ctr("abp.pump.button.do_not_interact_with_world", "Do Not Interact With World")},
            {57, "hand", fg::Icon::Hand, ctr("abp.pump.button.interact_with_hand", "Interact With\nFluid Container in Hand"),
             ctr("abp.pump.button.do_not_interact_with_hand", "Do Not Interact With\nFluid Container in Hand")},
        };
        for (auto const& d : defs) {
            auto& b   = widgets.add(std::make_unique<ToggleButton>());
            b.rect    = {area.x + d.x, area.y + 24, 18, 18};
            b.states  = {{iconFn(d.icon), lines(d.on)}, {iconFn(d.icon, true), lines(d.off)}};
            std::string key = d.what;
            b.current = [this, key] { return mPump.value(key, false) ? 0 : 1; };
            b.onClick = toggle(d.what);
        }
        auto& filter  = widgets.add(std::make_unique<FluidFilter>());
        int   n       = std::max(1, static_cast<int>(mFilter.size()));
        filter.rect   = {area.x + 3, area.y + 44, static_cast<float>(n * 18), 18};
        filter.filter = &mFilter;
        filter.onClick = [send](int i) {
            if (send) send(nlohmann::json{{"op", "filter"}, {"index", i}});
        };
        float w = std::max(57.f + 18 + 6, 3.f + static_cast<float>(n * 18) + 6);
        return {w, 44 + 18 + 6};
    }

    void update(TabContext const& ctx) override {
        mPump   = ctx.upgrade.tab.value("pump", nlohmann::json::object());
        mFilter = mPump.value("filter", nlohmann::json::array());
    }

private:
    nlohmann::json mPump   = nlohmann::json::object();
    nlohmann::json mFilter = nlohmann::json::array();
};

} // namespace
} // namespace abp::client::ui

ABP_REGISTER_NATIVE_TAB("pump", [] { return std::make_unique<abp::client::ui::PumpTab>(); });
ABP_REGISTER_NATIVE_TAB("advanced_pump", [] { return std::make_unique<abp::client::ui::PumpTab>(); });

#endif // ABP_CLIENT
