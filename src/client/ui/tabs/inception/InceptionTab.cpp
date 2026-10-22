// Native tab of the Inception upgrade (SB InceptionUpgradeTab): one toggle button at (x+3, y+24)
// switching the inventory order the upgrades work with: "Upgrades work with inventory of main
// backpack first" <-> "... inventories of backpacks in backpack first". The server hides the tab
// (tab data "hidden") when upgradesUseInventoriesOfBackpacksInBackpack is off.
//
// Server side: upgrade/inception/Inception.cpp (tab data / action "inception").

#ifdef ABP_CLIENT

#include <memory>
#include <sstream>
#include <vector>

#include "client/Client.h"
#include "client/ui/screen/Tabs.h"

namespace abp::client::ui {

namespace {

using L = Painter::Layer;

// Original pixel art (16x16): the main backpack in front / the backpacks inside in front.
PixelArt const kMainFirst{
    {
     "................",
     "....DDDDDDDD....",
     "...DBBBBBBBBD...",
     "..DBBBBBBBBBBD..",
     "..DBBDDDDDDBBD..",
     "..DBBDLLLLDBBD..",
     "..DBBDDDDDDBBD..",
     "..DBBBBBBBBBBD..",
     "..DBBBBBBBBBBD..",
     "..DBBBGGGGBBBD..",
     "..DBBBGggGBBBD..",
     "..DBBBGGGGBBBD..",
     "..DBBBBBBBBBBD..",
     "...DDDDDDDDDD...",
     "................",
     "................",
     },
    {"D=3B2614", "B=A0703C", "L=D8B07A", "G=2E7D32", "g=8BD18E"},
};
PixelArt const kInceptedFirst{
    {
     "................",
     "..DDDDDDDD......",
     ".DBBBBBBBBD.....",
     ".DBBDDDDBBD.....",
     ".DBBBBBBBBD.....",
     ".DBBB.DDDDDDDD..",
     ".DBBBDbbbbbbbbD.",
     ".DBBBDbbDDDDbbD.",
     ".DBBBDbbbbbbbbD.",
     "..DDDDbbbGGbbbD.",
     ".....DbbbGgbbbD.",
     ".....DbbbbbbbbD.",
     "......DDDDDDDD..",
     "................",
     "................",
     "................",
     },
    {"D=3B2614", "B=7A5530", "b=C08A4C", "G=2E7D32", "g=8BD18E"},
};

std::vector<TooltipLine> lines(std::string const& text) {
    std::vector<TooltipLine> out;
    std::stringstream        ss(text);
    for (std::string l; std::getline(ss, l);) out.push_back({l, 0xFFFFFF});
    return out;
}

class InceptionTab final : public NativeTab {
public:
    Size build(TabContext const& ctx, Rect area, WidgetGroup& widgets) override {
        update(ctx);
        auto& b   = widgets.add(std::make_unique<ToggleButton>());
        b.rect    = {area.x + 3, area.y + 24, 18, 18};
        b.states  = {
            {[](Painter& p, float x, float y) { p.pixels(kMainFirst, x + 1, y + 1, 1, L::Foreground); },
             lines(ctr("abp.inception.inventory_order_main_first", "Upgrades work with inventory\nof main backpack first"))},
            {[](Painter& p, float x, float y) { p.pixels(kInceptedFirst, x + 1, y + 1, 1, L::Foreground); },
             lines(ctr("abp.inception.inventory_order_incepted_first", "Upgrades work with inventories\nof backpacks in backpack first"))},
        };
        b.current = [this] { return mInceptedFirst ? 1 : 0; };
        auto send = ctx.sendTabAction;
        b.onClick = [send](int) {
            if (send) send(nlohmann::json{{"inception", "toggle"}});
        };
        return {3 + 18 + 6, 24 + 18 + 6};
    }

    void update(TabContext const& ctx) override {
        auto it         = ctx.upgrade.tab.find("inception");
        mInceptedFirst  = it != ctx.upgrade.tab.end() && it->is_object() && it->value("order", std::string{}) == "incepted_first";
    }

private:
    bool mInceptedFirst = false;
};

} // namespace

ABP_REGISTER_NATIVE_TAB("inception", [] { return std::make_unique<InceptionTab>(); });

} // namespace abp::client::ui

#endif // ABP_CLIENT
