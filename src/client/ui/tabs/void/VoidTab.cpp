#ifdef ABP_CLIENT

// Native settings tab of the Void upgrades (SC VoidUpgradeTab): "Works in GUI" toggle at
// (x+3, y+24), void type toggle (Void Always / Void Slot Overflow / Void Storage Overflow; without
// "Void Always" and with the red "Void Always option is disabled" line when the server config
// turns it off) at (x+21, y+24), the filter control (Allow / Block) at (x+3, y+44).
// Server side: upgrade/void (tab data + actions). Icons are original pixel art.

#include <algorithm>
#include <string>
#include <vector>

#include "client/Client.h"
#include "client/ui/widgets/FilterControl.h"
#include "client/ui/tabs/magnet/MfvcTab.h"

namespace abp::client::ui::tabs::voiding {

namespace {
using L = Painter::Layer;

/// A small window with a mouse pointer (works in the GUI too).
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

/// A hopper (only automation / other upgrades).
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

/// A dark swirl (everything matching is voided).
PixelArt const kVoidAlways{
    {
     "................",
     ".....PPPPPP.....",
     "...PPMMMMMMPP...",
     "..PMMKKKKKKMMP..",
     "..PMKKMMMMKKMP..",
     ".PMKKMPPPPMKKMP.",
     ".PMKMPKKKKPMKMP.",
     ".PMKMPKVVKPMKMP.",
     ".PMKMPKVVKPMKMP.",
     ".PMKMPKKKKPMKMP.",
     ".PMKKMPPPPMKKMP.",
     "..PMKKMMMMKKMP..",
     "..PMMKKKKKKMMP..",
     "...PPMMMMMMPP...",
     ".....PPPPPP.....",
     "................",
     },
    {"P=8A3CC8", "M=4A1A78", "K=14081E", "V=000000"},
};

/// One slot whose overflow drops into a small void.
PixelArt const kVoidSlot{
    {
     "................",
     ".DDDDDDDD.......",
     ".DSSSSSSW.......",
     ".DSYYYYSW.......",
     ".DSYYYYSW..A....",
     ".DSYYYYSW..AA...",
     ".DSYYYYSWAAAAA..",
     ".DSSSSSSW..AA...",
     ".WWWWWWWW..A....",
     "................",
     "..........PPPP..",
     ".........PMMMMP.",
     ".........PMKKMP.",
     ".........PMKKMP.",
     ".........PMMMMP.",
     "..........PPPP..",
     },
    {"D=373737", "S=8B8B8B", "W=FFFFFF", "Y=E6C23C", "A=F0F0F0", "P=8A3CC8", "M=4A1A78", "K=000000"},
};

/// A full chest whose overflow drops into a small void.
PixelArt const kVoidStorage{
    {
     "................",
     ".BBBBBBBBB......",
     ".BLLLLLLLB......",
     ".BLLLLLLLB.A....",
     ".BBBBYBBBB.AA...",
     ".BLLLYLLLBAAAAA.",
     ".BLLLLLLLB.AA...",
     ".BDLLLLLDB.A....",
     ".BBBBBBBBB......",
     "................",
     "..........PPPP..",
     ".........PMMMMP.",
     ".........PMKKMP.",
     ".........PMKKMP.",
     ".........PMMMMP.",
     "..........PPPP..",
     },
    {"B=3B2412", "L=9A6332", "D=6B4320", "Y=E3C04A", "A=F0F0F0", "P=8A3CC8", "M=4A1A78", "K=000000"},
};

IconFn art(PixelArt const& a) {
    return [&a](Painter& p, float x, float y) { p.pixels(a, x + 1, y + 1, 1, L::Foreground); };
}

std::vector<TooltipLine> lines(std::string const& text, std::uint32_t rgb = 0xFFFFFF) {
    std::vector<TooltipLine> out;
    std::size_t              start = 0;
    while (true) {
        auto nl = text.find('\n', start);
        out.push_back({text.substr(start, nl == std::string::npos ? std::string::npos : nl - start), rgb});
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return out;
}

std::vector<TooltipLine> concat(std::vector<TooltipLine> a, std::vector<TooltipLine> const& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

class VoidTab final : public FilterTab {
public:
    VoidTab()
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
        mWorkInGui     = ctx.upgrade.tab.value("workInGui", false);
        mType          = std::clamp(ctx.upgrade.tab.value("voidType", 0), 0, 2);
        mAlwaysEnabled = ctx.upgrade.tab.value("voidAlwaysEnabled", true);
    }

    void addButtons(TabContext const& ctx, Rect area, WidgetGroup& widgets) {
        read(ctx);
        auto send = ctx.sendTabAction;

        auto& gui  = widgets.add(std::make_unique<ToggleButton>());
        gui.size   = Button::Size::Normal;
        gui.rect   = {area.x + 3, area.y + 24, 18, 18};
        gui.states = {
            {art(kWorksInGui), {TooltipLine{ctr("abp.void.button.works_in_gui", "Works in GUI as well")}}},
            {art(kOnlyAutomatic), {TooltipLine{ctr("abp.void.button.only_automatic", "Only works with other upgrades/automation")}}},
        };
        gui.current = [this] { return mWorkInGui ? 0 : 1; };
        gui.onClick = [send](int) {
            if (send) send(nlohmann::json{{"op", "workInGui"}});
        };

        // SC VOID_TYPE / VOID_TYPE_OVERFLOW_ONLY: the detail lines are gray, the "disabled" line red.
        std::vector<TooltipLine> disabled;
        if (!mAlwaysEnabled) disabled = lines(ctr("abp.void.button.void_always_disabled", "Void Always option is disabled"), 0xFF5555);
        auto& type  = widgets.add(std::make_unique<ToggleButton>());
        type.size   = Button::Size::Normal;
        type.rect   = {area.x + 21, area.y + 24, 18, 18};
        type.states = {
            {art(kVoidAlways), {TooltipLine{ctr("abp.void.button.void_always", "Void Always")}}},
            {art(kVoidSlot),
             concat(
                 concat(
                     {TooltipLine{ctr("abp.void.button.void_slot_overflow", "Void Slot Overflow")}},
                     lines(ctr("abp.void.button.void_slot_overflow.detail", "Allows single slot to be filled with the item\nand voids anything that overflows"), 0xAAAAAA)
                 ),
                 disabled
             )},
            {art(kVoidStorage),
             concat(
                 concat(
                     {TooltipLine{ctr("abp.void.button.void_storage_overflow", "Void Storage Overflow")}},
                     lines(ctr("abp.void.button.void_storage_overflow.detail", "Allows whole storage to be filled with the item\n and voids anything that overflows"), 0xAAAAAA)
                 ),
                 disabled
             )},
        };
        type.current = [this] { return mType; };
        type.onClick = [send](int) {
            if (send) send(nlohmann::json{{"op", "voidType"}});
        };
    }

    bool mWorkInGui     = false;
    int  mType          = 0;
    bool mAlwaysEnabled = true;
};

ABP_REGISTER_NATIVE_TAB("void", [] { return std::make_unique<VoidTab>(); });
ABP_REGISTER_NATIVE_TAB("advanced_void", [] { return std::make_unique<VoidTab>(); });

} // namespace
} // namespace abp::client::ui::tabs::voiding

#endif // ABP_CLIENT
