#ifdef ABP_CLIENT

// Native SB tabs of the cooking upgrades (SC CookingUpgradeTab / AutoCookingUpgradeTab).
//
// Smelting / Smoking / Blasting: SC's 68x54 furnace control at (3,24): input slot at (1,1), fuel at
// (1,37), the result in a 26x26 frame with the item at (47,19), the burn flame (14x14, shrinking
// bottom-up) at (1,20) and the cook arrow (22x16, growing left to right) at (19,18).
// Auto variants: the Advanced input filter at (3,24), the furnace control 3 below it and the fuel
// filter (slots only) 4 below that.

#include <algorithm>
#include <chrono>

#include "mc/world/item/ItemStack.h"

#include "client/Client.h"
#include "client/ui/screen/Tabs.h"
#include "client/ui/widgets/FilterControl.h"
#include "client/ui/tabs/crafting/StationWidgets.h"
#include "client/ui/widgets/SbStyle.h"

namespace abp::client::ui::station {

namespace {

double nowSeconds() {
    static auto const start = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

struct FurnaceData {
    std::vector<ItemStack> slots = std::vector<ItemStack>(3);
    bool                   burning = false, cooking = false;
    int                    burnLeft = 0, burnTotal = 0, cookLeft = 0, cookTotal = 0;
    double                 receivedAt = 0;

    /// Ticks since the server's values, extrapolated while the furnace burns (the server runs
    /// upgrades in batches; SC's client computes the bars from the game time every frame).
    [[nodiscard]] float elapsed() const {
        if (!burning) return 0;
        return static_cast<float>(std::clamp((nowSeconds() - receivedAt) * 20.0, 0.0, 12.0));
    }
    /// SC CookingLogicControl.getBurnProgress / getCookProgress (elapsed fractions).
    [[nodiscard]] float burnProgress() const {
        if (!burning || burnTotal <= 0) return 0;
        return 1.f - std::max(static_cast<float>(burnLeft) - elapsed(), 0.f) / static_cast<float>(burnTotal);
    }
    [[nodiscard]] float cookProgress() const {
        if (!cooking || cookTotal <= 0) return 0;
        return std::clamp(1.f - std::max(static_cast<float>(cookLeft) - elapsed(), 0.f) / static_cast<float>(cookTotal), 0.f, 1.f);
    }
};

/// SC CookingLogicControl background and progress bars.
class FurnaceControl final : public Widget {
public:
    FurnaceData const* data = nullptr;

    void render(Painter& p, float, float, double) override {
        float x = rect.x, y = rect.y;
        sb::slotFrame(p, x + 1, y + 1);
        sb::slotFrame(p, x + 1, y + 37);
        icon(p, Icon::FlameEmpty, x + 1, y + 20);
        icon(p, Icon::ArrowEmpty, x + 19, y + 18);
        resultSlotFrame(p, x + 42, y + 14);
        if (!data) return;
        p.flush(); // the bars go over the empty pictures
        iconBottomUp(p, Icon::FlameLit, x + 1, y + 20, data->burnProgress(), 14);
        iconLeftRight(p, Icon::ArrowFull, x + 19, y + 18, data->cookProgress(), 22);
    }
};

class CookingTab final : public NativeTab {
public:
    explicit CookingTab(bool automatic) : mAuto(automatic) {}

    Size build(TabContext const& ctx, Rect area, WidgetGroup& widgets) override {
        reportTabOpen(ctx, widgets);
        readData(ctx);
        auto  send = ctx.sendTabAction;
        float x    = area.x + 3;
        float y    = area.y + 24;
        float w    = 68;
        mInput = mFuel = nullptr;
        if (mAuto) {
            // The shared FilterControl (SC FilterLogicControl.Advanced) for the input filter.
            mInput = &widgets.add(std::make_unique<FilterControl>("input", send));
            mInput->setPosition(x, y);
            mInput->setState(ctx.upgrade.tab.value("inputFilter", nlohmann::json::object()));
            (void)mInput->consumeSizeChange();
            w  = std::max(w, mInput->width());
            y += mInput->height() + 3;
        }
        auto& control = widgets.add(std::make_unique<FurnaceControl>());
        control.rect  = {x, y, 68, 54};
        control.data  = &mData;
        // SC moveSlotsToView: input (1,1), fuel (1,37), output (47,19).
        float const offsets[3][2] = {
            {1,  1 },
            {1,  37},
            {47, 19}
        };
        for (int i = 0; i < 3; ++i) {
            auto& s   = widgets.add(std::make_unique<ItemSlot>());
            s.frame   = false;
            s.rect    = {x + offsets[i][0] - 1, y + offsets[i][1] - 1, 18, 18};
            s.item    = [this, i] { return &mData.slots[static_cast<std::size_t>(i)]; };
            s.onClick = [send, i](int button, bool shift) {
                if (send) send({{"slot", i}, {"button", button}, {"shift", shift}});
            };
        }
        y += 54;
        if (mAuto) {
            y    += 4;
            mFuel = &widgets.add(std::make_unique<FilterControl>("fuel", send));
            mFuel->setPosition(x, y);
            mFuel->setState(ctx.upgrade.tab.value("fuelFilter", nlohmann::json::object()));
            (void)mFuel->consumeSizeChange();
            w  = std::max(w, mFuel->width());
            y += mFuel->height();
        }
        return {3 + w + 6, y + 6 - area.y};
    }

    void update(TabContext const& ctx) override {
        readData(ctx);
        if (mInput) mInput->setState(ctx.upgrade.tab.value("inputFilter", nlohmann::json::object()));
        if (mFuel) mFuel->setState(ctx.upgrade.tab.value("fuelFilter", nlohmann::json::object()));
    }

    bool needsRebuild() override {
        bool a = mInput && mInput->consumeSizeChange();
        bool b = mFuel && mFuel->consumeSizeChange();
        return a || b;
    }

private:
    void readData(TabContext const& ctx) {
        auto const& t = ctx.upgrade.tab;
        if (auto s = t.find("slots"); s != t.end() && s->is_array()) {
            for (std::size_t i = 0; i < 3 && i < s->size(); ++i) mData.slots[i] = stackFromJson((*s)[i]);
        }
        FurnaceData next = mData;
        next.burning     = t.value("burning", false);
        next.cooking     = t.value("cooking", false);
        next.burnLeft    = t.value("burnLeft", 0);
        next.burnTotal   = t.value("burnTotal", 0);
        next.cookLeft    = t.value("cookLeft", 0);
        next.cookTotal   = t.value("cookTotal", 0);
        if (next.burning != mData.burning || next.cooking != mData.cooking || next.burnLeft != mData.burnLeft
            || next.cookLeft != mData.cookLeft || next.burnTotal != mData.burnTotal || next.cookTotal != mData.cookTotal) {
            next.receivedAt = nowSeconds();
        }
        mData = std::move(next);
    }

    bool          mAuto;
    FurnaceData   mData;
    FilterControl* mInput = nullptr; ///< owned by the tab's widget group (valid while open)
    FilterControl* mFuel  = nullptr;
};

} // namespace

} // namespace abp::client::ui::station

ABP_REGISTER_NATIVE_TAB("smelting", [] { return std::make_unique<abp::client::ui::station::CookingTab>(false); });
ABP_REGISTER_NATIVE_TAB("smoking", [] { return std::make_unique<abp::client::ui::station::CookingTab>(false); });
ABP_REGISTER_NATIVE_TAB("blasting", [] { return std::make_unique<abp::client::ui::station::CookingTab>(false); });
ABP_REGISTER_NATIVE_TAB("auto_smelting", [] { return std::make_unique<abp::client::ui::station::CookingTab>(true); });
ABP_REGISTER_NATIVE_TAB("auto_smoking", [] { return std::make_unique<abp::client::ui::station::CookingTab>(true); });
ABP_REGISTER_NATIVE_TAB("auto_blasting", [] { return std::make_unique<abp::client::ui::station::CookingTab>(true); });

#endif // ABP_CLIENT
