#ifdef ABP_CLIENT

// Battery upgrade on the native backpack screen: the charge bar right of the storage slots (SC
// BatteryInventoryControl) and the settings tab with its input / output slots (SC BatteryUpgradeTab).

#include <algorithm>

#include "mc/world/item/ItemStack.h"

#include "client/Client.h"
#include "client/ui/screen/InventoryParts.h"
#include "client/ui/screen/Tabs.h"
#include "client/ui/tabs/tank/FluidGfx.h"
#include "client/ui/widgets/SbStyle.h"

namespace abp::client::ui {
namespace {

namespace fg = fluidgfx;

constexpr std::uint32_t kTopBarColor    = 0xFF1A1A;
constexpr std::uint32_t kBottomBarColor = 0xFFFF40;

/// SC BatteryInventoryControl.
class BatteryBar final : public InventoryPart {
public:
    void render(Painter& p, InventoryPartContext const& ctx, float, float) override {
        auto const& a    = ctx.area;
        float       left = a.x + 9;
        fg::barBackground(p, left, a.y, a.h);
        int rows = static_cast<int>(a.h) / 18;
        fg::batteryCells(p, left + 1, a.y, static_cast<float>(rows * 18));
        // renderCharge: 6 unit segments from the bottom, yellow to red.
        auto const& e        = ctx.upgrade.tab.value("energy", nlohmann::json::object());
        int         stored   = e.value("stored", 0);
        int         capacity = e.value("capacity", 0);
        int         segments = static_cast<int>(a.h) / 6;
        if (capacity > 0 && segments > 0) {
            int level = static_cast<int>(static_cast<float>(segments) * (static_cast<float>(stored) / static_cast<float>(capacity)));
            for (int i = 0; i < std::min(level, segments); ++i) {
                float pct = segments > 1 ? static_cast<float>(i) / static_cast<float>(segments - 1) : 0.f;
                auto  mix = [pct](std::uint32_t from, std::uint32_t to, int shift) {
                    float a = static_cast<float>((from >> shift) & 255), b = static_cast<float>((to >> shift) & 255);
                    return static_cast<std::uint32_t>(static_cast<int>(a * (1 - pct) + b * pct)) << shift;
                };
                std::uint32_t rgb = mix(kBottomBarColor, kTopBarColor, 16) | mix(kBottomBarColor, kTopBarColor, 8) | mix(kBottomBarColor, kTopBarColor, 0);
                fg::chargeSegment(p, left + 1, a.y + a.h - static_cast<float>((i + 1) * 6), rgb);
            }
        }
        fg::batteryConnectionTop(p, left + 1, a.y);
        fg::batteryConnectionBottom(p, left + 1, a.y + a.h - 4);
    }

    bool tooltip(InventoryPartContext const& ctx, float mx, float my, std::vector<TooltipLine>& out) const override {
        auto const& a  = ctx.area;
        float       sx = a.x + 10, sy = a.y + 1;
        if (mx < sx || mx >= sx + 16 || my < sy || my >= sy + a.h - 2) return false;
        auto const& e = ctx.upgrade.tab.value("energy", nlohmann::json::object());
        out.push_back({ctr("abp.battery.contents_tooltip", "{} FE out of {} FE", fg::grouped(e.value("stored", 0)), fg::grouped(e.value("capacity", 0))), 0xFFFFFF});
        return true;
    }

    void errorOverlay(Painter& p, InventoryPartContext const& ctx) override {
        p.fillRect({ctx.area.x + 10, ctx.area.y + 1, 16, ctx.area.h - 2}, sb::color::ERROR_SLOT, Painter::Layer::Foreground);
    }
};

/// SC BatteryUpgradeTab: input and output slots.
class BatteryTab final : public NativeTab {
public:
    Size build(TabContext const& ctx, Rect area, WidgetGroup& widgets) override {
        for (int i = 0; i < 2; ++i) {
            auto& s        = widgets.add(std::make_unique<fg::MenuSlot>());
            s.rect         = {area.x + (i == 0 ? 3.f : 24.f), area.y + 24, 18, 18};
            s.index        = i;
            s.emptyIcon    = i == 0 ? fg::Icon::BatteryInput : fg::Icon::BatteryOutput;
            s.emptyTooltip = i == 0 ? ctr("abp.battery.slot.input", "Input") : ctr("abp.battery.slot.output", "Output");
            s.send         = ctx.sendTabAction;
            s.stacks       = &mStacks;
            s.counts       = &mCounts;
        }
        update(ctx);
        return {48, 48};
    }

    void update(TabContext const& ctx) override { fg::readMenuSlots(ctx.upgrade.tab, mStacks, mCounts); }

private:
    std::vector<ItemStack> mStacks;
    std::vector<long long> mCounts;
};

} // namespace
} // namespace abp::client::ui

ABP_REGISTER_INVENTORY_PART("battery", [] { return std::make_unique<abp::client::ui::BatteryBar>(); });
ABP_REGISTER_NATIVE_TAB("battery", [] { return std::make_unique<abp::client::ui::BatteryTab>(); });

#endif // ABP_CLIENT
