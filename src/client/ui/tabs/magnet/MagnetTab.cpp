#ifdef ABP_CLIENT

// Native settings tab of the Magnet upgrades (SC MagnetUpgradeTab): "Pickup Items" toggle at
// (x+3, y+24), "Pickup Experience" toggle at (x+21, y+24), the contents filter control (Allow /
// Block / Match Backpack Contents) at (x+3, y+44). Server side: upgrade/magnet (tab data + actions).
// Icons are original pixel art.

#include "client/Client.h"
#include "client/ui/widgets/FilterControl.h"
#include "client/ui/tabs/magnet/MfvcTab.h"

namespace abp::client::ui::tabs::magnet {

namespace {
using L = Painter::Layer;

PixelArt const kMagnet{
    {
     "................",
     "....RRRRRRRR....",
     "...RRRRRRRRRR...",
     "..RRRDDDDDDRRR..",
     "..RRD......DRR..",
     "..RRD......DRR..",
     "..RRD......DRR..",
     "..WWS......WWS..",
     "..WWS......WWS..",
     "................",
     "......Y..Y......",
     "................",
     "......GGGG......",
     ".....GYYYYG.....",
     "......GGGG......",
     "................",
     },
    {"R=D02828", "D=8C1414", "W=E6E6E6", "S=9A9A9A", "Y=F2D23C", "G=A0841C"},
};

PixelArt const kOrb{
    {
     "................",
     "................",
     "................",
     "......GGGG......",
     ".....GLLLLG.....",
     "....GLYYYYLG....",
     "...GLYYWYYYLG...",
     "...GLYWWYYYLG...",
     "...GLYYYYYYLG...",
     "...GLYYYYYYLG...",
     "....GLYYYYLG....",
     ".....GLLLLG.....",
     "......GGGG......",
     "................",
     "................",
     "................",
     },
    {"G=2E7D12", "L=7ED321", "Y=D8F23A", "W=FFFFE0"},
};

/// The red slash of the "do not" states.
PixelArt const kSlash{
    {
     "................",
     "..............XX",
     ".............XXX",
     "............XXX.",
     "...........XXX..",
     "..........XXX...",
     ".........XXX....",
     "........XXX.....",
     ".......XXX......",
     "......XXX.......",
     ".....XXX........",
     "....XXX.........",
     "...XXX..........",
     "..XXX...........",
     ".XXX............",
     ".XX.............",
     },
    {"X=E01E1E"},
};

void draw(Painter& p, PixelArt const& art, float x, float y, bool slashed) {
    p.pixels(art, x + 1, y + 1, 1, L::Foreground);
    if (slashed) p.pixels(kSlash, x + 1, y + 1, 1, L::Foreground);
}

class MagnetTab final : public FilterTab {
public:
    MagnetTab()
    : FilterTab("filter", 44, [this](TabContext const& ctx, Rect area, WidgetGroup& widgets) { addButtons(ctx, area, widgets); }) {}

    Size build(TabContext const& ctx, Rect area, WidgetGroup& widgets) override {
        return logTabBuilt(ctx, area, FilterTab::build(ctx, area, widgets), 44);
    }

    void update(TabContext const& ctx) override {
        read(ctx);
        FilterTab::update(ctx);
    }

private:
    void read(TabContext const& ctx) {
        mItems = ctx.upgrade.tab.value("pickupItems", true);
        mXp    = ctx.upgrade.tab.value("pickupXp", true);
    }

    void addButtons(TabContext const& ctx, Rect area, WidgetGroup& widgets) {
        read(ctx);
        auto send = ctx.sendTabAction;

        auto& items  = widgets.add(std::make_unique<ToggleButton>());
        items.size   = Button::Size::Normal;
        items.rect   = {area.x + 3, area.y + 24, 18, 18};
        items.states = {
            {[](Painter& p, float x, float y) { draw(p, kMagnet, x, y, false); },
             {TooltipLine{ctr("abp.magnet.button.pickup_items", "Pickup Items")}}},
            {[](Painter& p, float x, float y) { draw(p, kMagnet, x, y, true); },
             {TooltipLine{ctr("abp.magnet.button.do_not_pickup_items", "Do Not Pickup Items")}}},
        };
        items.current = [this] { return mItems ? 0 : 1; };
        items.onClick = [send](int) {
            if (send) send(nlohmann::json{{"op", "pickupItems"}});
        };

        auto& xp  = widgets.add(std::make_unique<ToggleButton>());
        xp.size   = Button::Size::Normal;
        xp.rect   = {area.x + 21, area.y + 24, 18, 18};
        xp.states = {
            {[](Painter& p, float x, float y) { draw(p, kOrb, x, y, false); },
             {TooltipLine{ctr("abp.magnet.button.pickup_xp", "Pickup Experience")},
              TooltipLine{"§o" + ctr("abp.magnet.button.pickup_xp.detail", "Requires Tank Upgrade"), 0x555555}}},
            {[](Painter& p, float x, float y) { draw(p, kOrb, x, y, true); },
             {TooltipLine{ctr("abp.magnet.button.do_not_pickup_xp", "Do Not Pickup Experience")}}},
        };
        xp.current = [this] { return mXp ? 0 : 1; };
        xp.onClick = [send](int) {
            if (send) send(nlohmann::json{{"op", "pickupXp"}});
        };
    }

    bool mItems = true;
    bool mXp    = true;
};

ABP_REGISTER_NATIVE_TAB("magnet", [] { return std::make_unique<MagnetTab>(); });
ABP_REGISTER_NATIVE_TAB("advanced_magnet", [] { return std::make_unique<MagnetTab>(); });

} // namespace
} // namespace abp::client::ui::tabs::magnet

#endif // ABP_CLIENT
