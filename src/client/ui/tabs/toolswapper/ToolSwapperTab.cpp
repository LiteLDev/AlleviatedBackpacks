// Native tab of the Advanced Tool Swapper upgrade (SB ToolSwapperUpgradeTab; the basic one has no
// tab). Layout (SB): "Swap Weapon" toggle at (x+3, y+24), tool swap mode toggle at (x+21, y+24),
// the advanced filter control (SC FilterLogicControl.Advanced) at (x+3, y+44): Allow / Block at
// +0, primary match (item / mod / tags) at +18, durability at +36, NBT at +54, the 8 filter slots
// 21 below, 4 per row.
//
// Server side: upgrade/toolswapper/Upgrades.cpp (tab data / action "toolswapper").

#ifdef ABP_CLIENT

#include <memory>
#include <vector>

#include "mc/world/item/ItemStack.h"

#include "client/Client.h"
#include "client/ui/screen/Tabs.h"
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
PixelArt const kAllow{
    {
     "................",
     "................",
     "................",
     "............GG..",
     "...........GGG..",
     "..........GGG...",
     ".........GGG....",
     "..GG....GGG.....",
     "..GGG..GGG......",
     "...GGGGGG.......",
     "....GGGG........",
     ".....GG.........",
     "................",
     "................",
     "................",
     "................",
     },
    {"G=3BBF3B"},
};
PixelArt const kBlock{
    {
     "................",
     "................",
     "...RR......RR...",
     "...RRR....RRR...",
     "....RRR..RRR....",
     ".....RRRRRR.....",
     "......RRRR......",
     "......RRRR......",
     ".....RRRRRR.....",
     "....RRR..RRR....",
     "...RRR....RRR...",
     "...RR......RR...",
     "................",
     "................",
     "................",
     "................",
     },
    {"R=D02020"},
};
PixelArt const kMatchItem{
    {
     "................",
     "...DDDDDDDDDD...",
     "...DLLLLLLLLD...",
     "...DL......LD...",
     "...DL.WWWW.LD...",
     "...DL..WW..LD...",
     "...DL..WW..LD...",
     "...DL..WW..LD...",
     "...DL..WW..LD...",
     "...DL.WWWW.LD...",
     "...DL......LD...",
     "...DLLLLLLLLD...",
     "...DDDDDDDDDD...",
     "................",
     "................",
     "................",
     },
    {"D=373737", "L=8B8B8B", "W=FFFFFF"},
};
PixelArt const kMatchMod{
    {
     "................",
     "...DDDDDDDDDD...",
     "...DLLLLLLLLD...",
     "...DL......LD...",
     "...DLW....WLD...",
     "...DLWW..WWLD...",
     "...DLW.WW.WLD...",
     "...DLW....WLD...",
     "...DLW....WLD...",
     "...DLW....WLD...",
     "...DL......LD...",
     "...DLLLLLLLLD...",
     "...DDDDDDDDDD...",
     "................",
     "................",
     "................",
     },
    {"D=373737", "L=8B8B8B", "W=FFFFFF"},
};
PixelArt const kMatchTags{
    {
     "................",
     "...DDDDDDDDDD...",
     "...DLLLLLLLLD...",
     "...DL.W..W.LD...",
     "...DLWWWWWWLD...",
     "...DL.W..W.LD...",
     "...DL.W..W.LD...",
     "...DLWWWWWWLD...",
     "...DL.W..W.LD...",
     "...DL......LD...",
     "...DL......LD...",
     "...DLLLLLLLLD...",
     "...DDDDDDDDDD...",
     "................",
     "................",
     "................",
     },
    {"D=373737", "L=8B8B8B", "W=FFFFFF"},
};
PixelArt const kDurability{
    {
     "................",
     "................",
     "................",
     "................",
     "................",
     "................",
     "..DDDDDDDDDDDD..",
     "..DGGGGGGGGYYD..",
     "..DGGGGGGGGYYD..",
     "..DDDDDDDDDDDD..",
     "................",
     "................",
     "................",
     "................",
     "................",
     "................",
     },
    {"D=1E1E1E", "G=3BBF3B", "Y=8B8B8B"},
};
PixelArt const kNbt{
    {
     "................",
     "................",
     "....WW....WW....",
     "...W........W...",
     "...W........W...",
     "...W..W..W..W...",
     "..W..........W..",
     "...W........W...",
     "...W..W..W..W...",
     "...W........W...",
     "...W........W...",
     "....WW....WW....",
     "................",
     "................",
     "................",
     "................",
     },
    {"W=E0C030"},
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

class ToolSwapperTab final : public NativeTab {
public:
    Size build(TabContext const& ctx, Rect area, WidgetGroup& widgets) override {
        mDurability = mNbt = nullptr; // widgets of an earlier build are gone
        update(ctx);
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

        // SC FilterLogicControl.Advanced at (x+3, y+44).
        float fx = area.x + 3, fy = area.y + 44;
        auto  toggle = [&](float dx, std::vector<ToggleButton::State> states, std::function<int()> current, std::string o) -> ToggleButton& {
            auto& b   = widgets.add(std::make_unique<ToggleButton>());
            b.rect    = {fx + dx, fy, 18, 18};
            b.states  = std::move(states);
            b.current = std::move(current);
            b.onClick = op(std::move(o));
            return b;
        };
        toggle(
            0,
            {{layered({&kAllow}), tip(ctr("abp.toolswapper.filter.allow", "Allow"))}, {layered({&kBlock}), tip(ctr("abp.toolswapper.filter.block", "Block"))}},
            [this] { return mAllow ? 0 : 1; },
            "allow"
        );
        toggle(
            18,
            {{layered({&kMatchItem}), tip(ctr("abp.toolswapper.filter.match_item", "Match Item"))},
             {layered({&kMatchMod}), tip(ctr("abp.toolswapper.filter.match_mod", "Match Mod"))},
             {layered({&kMatchTags}), tip(ctr("abp.toolswapper.filter.match_tags", "Match Tags"))}},
            [this] { return mPrimary; },
            "primary"
        );
        mDurability = &toggle(
            36,
            {{layered({&kDurability, &kCross}), tip(ctr("abp.toolswapper.filter.ignore_durability", "Ignore Durability"))},
             {layered({&kDurability}), tip(ctr("abp.toolswapper.filter.match_durability", "Match Durability"))}},
            [this] { return mMatchDurability ? 1 : 0; },
            "durability"
        );
        mNbt = &toggle(
            54,
            {{layered({&kNbt, &kCross}), tip(ctr("abp.toolswapper.filter.ignore_nbt", "Ignore NBT"))}, {layered({&kNbt}), tip(ctr("abp.toolswapper.filter.match_nbt", "Match NBT"))}},
            [this] { return mMatchNbt ? 1 : 0; },
            "nbt"
        );
        auto& grid   = widgets.add(std::make_unique<SlotGrid>());
        int   rows   = (static_cast<int>(mGhosts.size()) + mPerRow - 1) / mPerRow;
        grid.columns = mPerRow;
        grid.count   = static_cast<int>(mGhosts.size());
        grid.rect    = {fx, fy + 21, static_cast<float>(mPerRow) * 18, static_cast<float>(rows) * 18};
        grid.item    = [this](int i) -> ItemStack const* {
            return i >= 0 && i < static_cast<int>(mGhosts.size()) && !mGhosts[static_cast<std::size_t>(i)].isNull() ? &mGhosts[static_cast<std::size_t>(i)] : nullptr;
        };
        grid.onClick = [send](int i, int, bool) {
            if (send) send(nlohmann::json{{"toolswapper", "slot"}, {"index", i}});
        };
        update(ctx);
        return {3 + static_cast<float>(mPerRow) * 18 + 6, 44 + 21 + static_cast<float>(rows) * 18 + 6};
    }

    void update(TabContext const& ctx) override {
        auto it = ctx.upgrade.tab.find("toolswapper");
        if (it == ctx.upgrade.tab.end() || !it->is_object()) return;
        auto const& j = *it;
        mSwapWeapon   = j.value("swapWeapon", true);
        auto mode     = j.value("mode", std::string("name"));
        mMode         = mode == "onlyTools" ? 1 : mode == "noSwap" ? 2 : 0;
        auto f        = j.value("filter", nlohmann::json::object());
        mAllow        = f.value("allow", false);
        mPrimary      = f.value("primary", 0);
        mMatchDurability = f.value("durability", false);
        mMatchNbt        = f.value("nbt", false);
        mPerRow          = std::max(1, f.value("slotsInRow", 4));
        mGhosts.clear();
        for (auto const& g : f.value("ghosts", nlohmann::json::array())) {
            core::ItemStackData d;
            try {
                if (!g.is_null()) d = core::ItemStackData::fromJson(g);
            } catch (...) {
            }
            mGhosts.push_back(d.empty() ? ItemStack{} : game::ItemConv::toStack(d, 1));
        }
        // SC: durability / NBT buttons are hidden in TAGS mode.
        if (mDurability) mDurability->visible = mPrimary != 2;
        if (mNbt) mNbt->visible = mPrimary != 2;
    }

private:
    bool                   mSwapWeapon      = true;
    int                    mMode            = 0;
    bool                   mAllow           = false;
    int                    mPrimary         = 0;
    bool                   mMatchDurability = false;
    bool                   mMatchNbt        = false;
    int                    mPerRow          = 4;
    std::vector<ItemStack> mGhosts;
    ToggleButton*          mDurability = nullptr; ///< owned by the tab's widget group
    ToggleButton*          mNbt        = nullptr;
};

} // namespace

ABP_REGISTER_NATIVE_TAB("advanced_tool_swapper", [] { return std::make_unique<ToolSwapperTab>(); });

} // namespace abp::client::ui

#endif // ABP_CLIENT
