#ifdef ABP_CLIENT

// Native settings tab of the Compacting upgrades (SC CompactingUpgradeTab): "Compact Only
// Uncraftable" / "Compact Anything" toggle at (x+3, y+24), "Works in GUI" toggle at (x+21, y+24),
// the filter control (Allow / Block) at (x+3, y+44). Server side: upgrade/compacting (tab data +
// actions). Icons are original pixel art.

#include "client/Client.h"
#include "client/ui/widgets/FilterControl.h"
#include "client/ui/tabs/magnet/MfvcTab.h"

namespace abp::client::ui::tabs::compacting {

namespace {
using L = Painter::Layer;

/// Small items <-> one block (the result crafts back: only uncraftable).
PixelArt const kOnlyUncraftable{
    {
     "................",
     ".II.II...BBBBBB.",
     ".IS.IS...BLLLLB.",
     ".........BLLLLB.",
     ".II.II...BLLLLB.",
     ".IS.IS...BLLLLB.",
     ".........BBBBBB.",
     "................",
     "...A......A.....",
     "..AA......AA....",
     ".AAAAAAAAAAAA...",
     "..AA......AA....",
     "...A......A.....",
     "................",
     "................",
     "................",
     },
    {"I=D8D8D8", "S=8A8A8A", "B=5A5A5A", "L=C8C8C8", "A=3FD23F"},
};

/// Small items -> one block (any recipe, one way).
PixelArt const kAnything{
    {
     "................",
     ".II.II...BBBBBB.",
     ".IS.IS...BLLLLB.",
     ".........BLLLLB.",
     ".II.II...BLLLLB.",
     ".IS.IS...BLLLLB.",
     ".........BBBBBB.",
     "................",
     "..........A.....",
     "..........AA....",
     "..AAAAAAAAAAA...",
     "..........AA....",
     "..........A.....",
     "................",
     "................",
     "................",
     },
    {"I=D8D8D8", "S=8A8A8A", "B=5A5A5A", "L=C8C8C8", "A=E3A03C"},
};

PixelArt const kWorksInGui{
    {
     "................",
     ".KKKKKKKKKKKKK..",
     ".KBBBBBBBBBBBK..",
     ".KKKKKKKKKKKKK..",
     ".KGGGGGGGGGGGK..",
     ".KGSSGSSGSSGGK..",
     ".KGSSGSSGSSGGK..",
     ".KGGGGGGGWGGGK..",
     ".KGSSGSSGWWGGK..",
     ".KGSSGSSGWWWGK..",
     ".KGGGGGGGWWWWK..",
     ".KKKKKKKKWWWWWK.",
     ".........WWWKKK.",
     ".........WKWK...",
     "..........K.WK..",
     "............K...",
     },
    {"K=2A2A2A", "B=3C6EC8", "G=C6C6C6", "S=8B8B8B", "W=FFFFFF"},
};

PixelArt const kOnlyAutomatic{
    {
     "................",
     "................",
     ".KKKKKKKKKKKKKK.",
     ".KHHHHHHHHHHHHK.",
     ".KHDDDDDDDDDDHK.",
     ".KHHHHHHHHHHHHK.",
     "..KHHHHHHHHHHK..",
     "...KHHHHHHHHK...",
     "....KHHHHHHK....",
     ".....KHHHHK.....",
     "......KHHK......",
     "......KHHK......",
     "......KDDK......",
     ".......KK.......",
     "................",
     "................",
     },
    {"K=1E1E1E", "H=5A5A5A", "D=343434"},
};

IconFn art(PixelArt const& a) {
    return [&a](Painter& p, float x, float y) { p.pixels(a, x + 1, y + 1, 1, L::Foreground); };
}

class CompactingTab final : public FilterTab {
public:
    CompactingTab()
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
        mAnything  = ctx.upgrade.tab.value("compactAnything", false);
        mWorkInGui = ctx.upgrade.tab.value("workInGui", false);
    }

    void addButtons(TabContext const& ctx, Rect area, WidgetGroup& widgets) {
        read(ctx);
        auto send = ctx.sendTabAction;

        auto& mode  = widgets.add(std::make_unique<ToggleButton>());
        mode.size   = Button::Size::Normal;
        mode.rect   = {area.x + 3, area.y + 24, 18, 18};
        mode.states = {
            {art(kOnlyUncraftable), {TooltipLine{ctr("abp.compacting.button.compact_only_uncraftable", "Compact Only Uncraftable")}}},
            {art(kAnything), {TooltipLine{ctr("abp.compacting.button.compact_anything", "Compact Anything")}}},
        };
        mode.current = [this] { return mAnything ? 1 : 0; };
        mode.onClick = [send](int) {
            if (send) send(nlohmann::json{{"op", "compactAnything"}});
        };

        auto& gui  = widgets.add(std::make_unique<ToggleButton>());
        gui.size   = Button::Size::Normal;
        gui.rect   = {area.x + 21, area.y + 24, 18, 18};
        gui.states = {
            {art(kWorksInGui), {TooltipLine{ctr("abp.compacting.button.works_in_gui", "Works in GUI as well")}}},
            {art(kOnlyAutomatic), {TooltipLine{ctr("abp.compacting.button.only_automatic", "Only works with other upgrades/automation")}}},
        };
        gui.current = [this] { return mWorkInGui ? 0 : 1; };
        gui.onClick = [send](int) {
            if (send) send(nlohmann::json{{"op", "workInGui"}});
        };
    }

    bool mAnything  = false;
    bool mWorkInGui = false;
};

ABP_REGISTER_NATIVE_TAB("compacting", [] { return std::make_unique<CompactingTab>(); });
ABP_REGISTER_NATIVE_TAB("advanced_compacting", [] { return std::make_unique<CompactingTab>(); });

} // namespace
} // namespace abp::client::ui::tabs::compacting

#endif // ABP_CLIENT
