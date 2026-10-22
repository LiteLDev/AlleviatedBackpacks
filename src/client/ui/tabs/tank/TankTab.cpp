#ifdef ABP_CLIENT

// Tank upgrade on the native backpack screen: the fluid bar right of the storage slots (SC
// TankInventoryControl) and the settings tab with the four container slots (SC TankUpgradeTab).

#include <algorithm>
#include <cstdio>

#include "mc/client/player/LocalPlayer.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/item/ItemStack.h"

#include "api/abp/Xp.h"
#include "client/Client.h"
#include "client/net/ClientNet.h"
#include "client/ui/screen/InventoryParts.h"
#include "client/ui/screen/Tabs.h"
#include "client/ui/tabs/tank/FluidGfx.h"
#include "client/ui/widgets/SbStyle.h"

namespace abp::client::ui {
namespace {

namespace fg = fluidgfx;

std::string oneDecimal(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.1f", v);
    return buf;
}

/// SC TankInventoryControl.
class TankBar final : public InventoryPart {
public:
    void render(Painter& p, InventoryPartContext const& ctx, float, float) override {
        auto const& a    = ctx.area;
        float       left = a.x + 9;
        fg::barBackground(p, left, a.y, a.h);
        auto const& t = ctx.upgrade.tab.value("fluidTank", nlohmann::json::object());
        auto const& f = t.value("fluid", nlohmann::json());
        int         capacity = t.value("capacity", 0);
        if (f.is_object() && capacity > 0) {
            int amount = f.value("amount", 0);
            int level  = static_cast<int>((a.h - 2) * (static_cast<float>(amount) / static_cast<float>(capacity)));
            level      = std::clamp(level, 0, static_cast<int>(a.h) - 2);
            if (level > 0) fg::fluid(p, a.x + 10, a.y + 1 + a.h - 2 - static_cast<float>(level), 16, static_cast<float>(level), f.value("color", 0xFFFFFFFFu));
        }
        fg::tankOverlay(p, left + 1, a.y, a.h);
    }

    bool click(InventoryPartContext const& ctx, int, float mx, float my) override {
        auto const& a    = ctx.area;
        float       left = a.x + 9;
        if (mx < left || mx >= left + 18 || my < a.y || my >= a.y + a.h) return false;
        auto* player = currentPlayer();
        if (!player || player->mPlayerUIContainer->getItem(0).mCount > 1) return false;
        net::sendScreenAction({{"action", "tab"}, {"slot", ctx.slot}, {"data", {{"op", "click"}}}});
        return true;
    }

    bool tooltip(InventoryPartContext const& ctx, float mx, float my, std::vector<TooltipLine>& out) const override {
        auto const& a  = ctx.area;
        float       sx = a.x + 10, sy = a.y + 1;
        if (mx < sx || mx >= sx + 16 || my < sy || my >= sy + a.h - 2) return false;
        auto const& t        = ctx.upgrade.tab.value("fluidTank", nlohmann::json::object());
        auto const& f        = t.value("fluid", nlohmann::json());
        int         capacity = t.value("capacity", 0);
        int         amount   = f.is_object() ? f.value("amount", 0) : 0;
        if (f.is_object()) out.push_back({f.value("name", std::string{}), 0xFFFFFF});
        if (f.is_object() && f.value("xp", false)) {
            double have = api::xp::levelsForExperience(static_cast<int>(api::xp::liquidToExperience(amount)));
            double max  = api::xp::levelsForExperience(static_cast<int>(api::xp::liquidToExperience(capacity)));
            out.push_back({ctr("abp.tank.xp_contents_tooltip", "{} out of {} xp levels", oneDecimal(have), oneDecimal(max)), 0xFFFFFF});
        } else {
            out.push_back({ctr("abp.tank.contents_tooltip", "{} mB out of {} mB", fg::grouped(amount), fg::grouped(capacity)), 0xFFFFFF});
        }
        return true;
    }

    void errorOverlay(Painter& p, InventoryPartContext const& ctx) override {
        p.fillRect({ctx.area.x + 10, ctx.area.y + 1, 16, ctx.area.h - 2}, sb::color::ERROR_SLOT, Painter::Layer::Foreground);
    }
};

/// SC TankUpgradeTab: input / output slots, their result slots below, arrows between.
class TankTab final : public NativeTab {
public:
    Size build(TabContext const& ctx, Rect area, WidgetGroup& widgets) override {
        struct Def {
            int         index;
            float       x, y;
            fg::Icon    icon;
            std::string tooltip;
        };
        Def const defs[] = {
            {0, 3, 24, fg::Icon::TankInput, ctr("abp.tank.slot.input", "Input")},
            {1, 24, 24, fg::Icon::TankOutput, ctr("abp.tank.slot.output", "Output")},
            {2, 3, 56, fg::Icon::TankInput, {}},
            {3, 24, 56, fg::Icon::TankOutput, {}},
        };
        for (auto const& d : defs) {
            auto& s        = widgets.add(std::make_unique<fg::MenuSlot>());
            s.rect         = {area.x + d.x, area.y + d.y, 18, 18};
            s.index        = d.index;
            s.emptyIcon    = d.icon;
            s.emptyTooltip = d.tooltip;
            s.send         = ctx.sendTabAction;
            s.stacks       = &mStacks;
            s.counts       = &mCounts;
            if (d.index >= 2) s.drawEmptyIcon = false; // result slots have no background icon
        }
        update(ctx);
        return {48, 80};
    }

    void update(TabContext const& ctx) override { fg::readMenuSlots(ctx.upgrade.tab, mStacks, mCounts); }

    void render(Painter& p, TabContext const&, Rect area, float, float) override {
        fg::arrowDown(p, area.x + 3 + 1, area.y + 24 + 18 + 3);
        fg::arrowDown(p, area.x + 24 + 1, area.y + 24 + 18 + 3);
    }

private:
    std::vector<ItemStack> mStacks;
    std::vector<long long> mCounts;
};

} // namespace
} // namespace abp::client::ui

ABP_REGISTER_INVENTORY_PART("tank", [] { return std::make_unique<abp::client::ui::TankBar>(); });
ABP_REGISTER_NATIVE_TAB("tank", [] { return std::make_unique<abp::client::ui::TankTab>(); });

#endif // ABP_CLIENT
