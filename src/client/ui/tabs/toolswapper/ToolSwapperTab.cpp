// Native tab of the Advanced Tool Swapper upgrade (SB ToolSwapperUpgradeTab; the basic one has no
// tab). Layout (SB): "Swap Weapon" toggle at (x+3, y+24), tool swap mode toggle at (x+21, y+24),
// the shared advanced filter control (SC FilterLogicControl.Advanced, incl. TAGS mode) at (x+3, y+44),
// slots from config/toolswapper.json `toolSwapperUpgrade` (SB: 8, 4 per row).
//
// Server side: upgrade/toolswapper/Upgrades.cpp (tab data / action "toolswapper").

#ifdef ABP_CLIENT

#include <memory>
#include <vector>

#include "mc/world/item/ItemStack.h"

#include "client/Client.h"
#include "client/ui/screen/Tabs.h"
#include "client/ui/widgets/FilterControl.h"
#include "core/ItemStackData.h"
#include "game/ItemConv.h"

namespace abp::client::ui {

namespace {

using L = Painter::Layer;

// ---- original pixel art (16x16, drawn at the button's +1,+1) --------------------------------------
PixelArt const kSword{
    {
     "................",
     "............WWW.",
     "...........WLLW.",
     "..........WLLW..",
     ".........WLLW...",
     "........WLLW....",
     ".......WLLW.....",
     "..D...WLLW......",
     "..DD.WLLW.......",
     "...DDLLW........",
     "....DDW.........",
     "...BBDD.........",
     "..BBB.DD........",
     ".BBB............",
     ".BB.............",
     "................",
     },
    {"W=FFFFFF", "L=C6C6C6", "D=5A3C1E", "B=7A5530"},
};
PixelArt const kPickaxe{
    {
     "................",
     "....LLLLLLL.....",
     "...LWWWWWWWL....",
     "..LWL.....LWL...",
     "..LL...BB..LL...",
     "......BB........",
     ".....BB.........",
     "....BB..........",
     "...BB...........",
     "..BB............",
     ".BB.............",
     "................",
     "................",
     "................",
     "................",
     "................",
     },
    {"W=FFFFFF", "L=9A9A9A", "B=7A5530"},
};
PixelArt const kCross{
    {
     "................",
     "................",
     "................",
     "................",
     "................",
     "................",
     "................",
     "................",
     "................",
     "..........RR..RR",
     "...........RRRR.",
     "............RR..",
     "...........RRRR.",
     "..........RR..RR",
     "................",
     "................",
     },
    {"R=D02020"},
};
PixelArt const kToolsOnly{
    {
     "................",
     "................",
     "................",
     "................",
     "................",
     "................",
     "................",
     "................",
     "................",
     "...........YY...",
     "..........YYYY..",
     ".........YY.YYY.",
     "..............YY",
     "..........YY.YY.",
     "...........YYY..",
     "................",
     },
    {"Y=E0C030"},
};

IconFn layered(std::vector<PixelArt const*> arts) {
    return [arts = std::move(arts)](Painter& p, float x, float y) {
        for (auto const* a : arts) p.pixels(*a, x + 1, y + 1, 1, L::Foreground);
    };
}

std::vector<TooltipLine> tip(std::string const& title, std::string const& detail = {}) {
    std::vector<TooltipLine> out{{title, 0xFFFFFF}};
    if (!detail.empty()) out.push_back({"§7" + detail, 0xAAAAAA});
    return out;
}

class ToolSwapperTab final : public FilterTab {
public:
    ToolSwapperTab()
    : FilterTab("filter", 44, [this](TabContext const& ctx, Rect area, WidgetGroup& widgets) { addButtons(ctx, area, widgets); }) {}

    void update(TabContext const& ctx) override {
        read(ctx);
        FilterTab::update(ctx);
    }

private:
    void read(TabContext const& ctx) {
        auto it = ctx.upgrade.tab.find("toolswapper");
        if (it == ctx.upgrade.tab.end() || !it->is_object()) return;
        mSwapWeapon = it->value("swapWeapon", true);
        auto mode   = it->value("mode", std::string("name"));
        mMode       = mode == "onlyTools" ? 1 : mode == "noSwap" ? 2 : 0;
    }

    void addButtons(TabContext const& ctx, Rect area, WidgetGroup& widgets) {
        read(ctx);
        auto send = ctx.sendTabAction;
        auto op   = [send](std::string o) {
            return [send, o = std::move(o)](int) {
                if (send) send(nlohmann::json{{"toolswapper", o}});
            };
        };
        auto& weapon   = widgets.add(std::make_unique<ToggleButton>());
        weapon.rect    = {area.x + 3, area.y + 24, 18, 18};
        weapon.states  = {
            {layered({&kSword, &kCross}),
             tip(ctr("abp.toolswapper.do_not_swap_weapon", "Do Not Swap Weapon"), ctr("abp.toolswapper.do_not_swap_weapon.detail", "Weapon is never swapped into hand"))},
            {layered({&kSword}),
             tip(ctr("abp.toolswapper.swap_weapon", "Swap Weapon"), ctr("abp.toolswapper.swap_weapon.detail", "Swaps weapon into hand when a tool is clicked on entity"))},
        };
        weapon.current = [this] { return mSwapWeapon ? 1 : 0; };
        weapon.onClick = op("weapon");

        auto& tools   = widgets.add(std::make_unique<ToggleButton>());
        tools.rect    = {area.x + 21, area.y + 24, 18, 18};
        tools.states  = {
            {layered({&kPickaxe}),
             tip(ctr("abp.toolswapper.swap_tools", "Swap Tools"), ctr("abp.toolswapper.swap_tools.detail", "Swaps appropriate tool into hand when it holds weapon or another tool"))},
            {layered({&kPickaxe, &kToolsOnly}),
             tip(ctr("abp.toolswapper.only_swap_for_tools", "Only Swap For Other Tools"),
                 ctr("abp.toolswapper.only_swap_for_tools.detail", "Swaps tool into hand only if it holds another tool, does not swap for weapon"))},
            {layered({&kPickaxe, &kCross}),
             tip(ctr("abp.toolswapper.do_not_swap_tools", "Do Not Swap Tools"), ctr("abp.toolswapper.do_not_swap_tools.detail", "Tools are never swapped into hand"))},
        };
        tools.current = [this] { return mMode; };
        tools.onClick = op("mode");
    }

    bool mSwapWeapon = true;
    int  mMode       = 0;
};

} // namespace

ABP_REGISTER_NATIVE_TAB("advanced_tool_swapper", [] { return std::make_unique<ToolSwapperTab>(); });

} // namespace abp::client::ui

#endif // ABP_CLIENT