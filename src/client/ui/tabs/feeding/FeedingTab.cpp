#ifdef ABP_CLIENT

// Native settings tabs of the Feeding upgrades (SC FeedingUpgradeTab):
// - Basic: the filter control (Allow / Block) at (x+3, y+24).
// - Advanced: hunger level toggle (ANY / HALF / FULL) at (x+3, y+24), "feed immediately when
//   hurt" toggle at (x+21, y+24), the advanced filter control at (x+3, y+44).
// Server side: upgrade/feeding (tab data + actions). Icons are original pixel art.

#include <algorithm>
#include <string>
#include <vector>

#include "client/Client.h"
#include "client/ui/widgets/FilterControl.h"
#include "client/ui/tabs/magnet/MfvcTab.h"

namespace abp::client::ui::tabs::feeding {

namespace {
using L = Painter::Layer;

// Drumsticks: FULL = the whole shank, HALF = half eaten, ANY = only a bite left.
PixelArt const kShankFull{
    {
     "................",
     "................",
     "..........BB....",
     ".........BWWB...",
     "........BWWB....",
     "......MMBBB.....",
     ".....MLLLM......",
     "....MLLLLLM.....",
     "...MLLLLLLLM....",
     "...MLLLLLLDM....",
     "...MLLLLLDDM....",
     "...MDLLLDDM.....",
     "....MDDDDM......",
     ".....MMMM.......",
     "................",
     "................",
     },
    {"B=5A3A1E", "W=F4EEDC", "M=6E2C14", "L=C8642C", "D=9A4418"},
};

PixelArt const kShankHalf{
    {
     "................",
     "................",
     "..........BB....",
     ".........BWWB...",
     "........BWWB....",
     "......MMBBB.....",
     ".....MLLLM......",
     "....MLLLLLM.....",
     "...MLLLLLLLM....",
     "...M.......M....",
     "...M.......M....",
     "...M......M.....",
     "....M....M......",
     ".....MMMM.......",
     "................",
     "................",
     },
    {"B=5A3A1E", "W=F4EEDC", "M=6E2C14", "L=C8642C"},
};

PixelArt const kShankAny{
    {
     "................",
     "................",
     "..........BB....",
     ".........BWWB...",
     "........BWWB....",
     "......MMBBB.....",
     ".....MLLLM......",
     "....M.....M.....",
     "...M.......M....",
     "...M.......M....",
     "...M.......M....",
     "...M......M.....",
     "....M....M......",
     ".....MMMM.......",
     "................",
     "................",
     },
    {"B=5A3A1E", "W=F4EEDC", "M=6E2C14", "L=C8642C"},
};

PixelArt const kHeart{
    {
     "................",
     "................",
     "................",
     "...KKK...KKK....",
     "..KRRRK.KRRRK...",
     ".KRWRRRKRRRRRK..",
     ".KRWRRRRRRRRRK..",
     ".KRRRRRRRRRRRK..",
     "..KRRRRRRRRDK...",
     "...KRRRRRRDK....",
     "....KRRRRDK.....",
     ".....KRRDK......",
     "......KDK.......",
     ".......K........",
     "................",
     "................",
     },
    {"K=3C0A0A", "R=E02828", "W=FFB4B4", "D=A01414"},
};

PixelArt const kHeartGray{
    {
     "................",
     "................",
     "................",
     "...KKK...KKK....",
     "..KGGGK.KGGGK...",
     ".KGWGGGKGGGGGK..",
     ".KGWGGGGGGGGGK..",
     ".KGGGGGGGGGGGK..",
     "..KGGGGGGGGDK...",
     "...KGGGGGGDK....",
     "....KGGGGDK.....",
     ".....KGGDK......",
     "......KDK.......",
     ".......K........",
     "................",
     "................",
     },
    {"K=2A2A2A", "G=9A9A9A", "W=D4D4D4", "D=6A6A6A"},
};

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

IconFn art(PixelArt const& a, bool slashed = false) {
    return [&a, slashed](Painter& p, float x, float y) {
        p.pixels(a, x + 1, y + 1, 1, L::Foreground);
        if (slashed) p.pixels(kSlash, x + 1, y + 1, 1, L::Foreground);
    };
}

/// SC's multi-line button tooltips: one line per "\n", all in the same colour.
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

class AdvancedFeedingTab final : public FilterTab {
public:
    AdvancedFeedingTab()
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
        mLevel = std::clamp(ctx.upgrade.tab.value("hungerLevel", 1), 0, 2);
        mHurt  = ctx.upgrade.tab.value("whenHurt", true);
    }

    void addButtons(TabContext const& ctx, Rect area, WidgetGroup& widgets) {
        read(ctx);
        auto send = ctx.sendTabAction;

        auto& level  = widgets.add(std::make_unique<ToggleButton>());
        level.size   = Button::Size::Normal;
        level.rect   = {area.x + 3, area.y + 24, 18, 18};
        level.states = {
            {art(kShankAny),
             lines(ctr("abp.feeding.button.hunger_level_any", "Feed as soon as player is tiny bit hungry\nwastes quite a few hunger points of the food"))},
            {art(kShankHalf),
             lines(ctr("abp.feeding.button.hunger_level_half", "Only feed when player is hungry enough\nto only waste half hunger points of the food at most"))},
            {art(kShankFull),
             lines(ctr("abp.feeding.button.hunger_level_full", "Only feed when player is hungry enough\nto not waste any hunger points of the food at all"))},
        };
        level.current = [this] { return mLevel; };
        level.onClick = [send](int) {
            if (send) send(nlohmann::json{{"op", "hungerLevel"}});
        };

        auto& hurt  = widgets.add(std::make_unique<ToggleButton>());
        hurt.size   = Button::Size::Normal;
        hurt.rect   = {area.x + 21, area.y + 24, 18, 18};
        hurt.states = {
            {art(kHeart),
             lines(ctr("abp.feeding.button.feed_immediately_when_hurt", "Feed player immediately when hurt\nIgnores hunger setting when player is not at max health"))},
            {art(kHeartGray, true),
             lines(ctr("abp.feeding.button.do_not_consider_health", "Do not consider health\nIgnores player's health and only feeds based on hunger setting"))},
        };
        hurt.current = [this] { return mHurt ? 0 : 1; };
        hurt.onClick = [send](int) {
            if (send) send(nlohmann::json{{"op", "whenHurt"}});
        };
    }

    int  mLevel = 1;
    bool mHurt  = true;
};

/// SC FeedingUpgradeTab.Basic: only the filter control, at (x+3, y+24).
class BasicFeedingTab final : public FilterTab {
public:
    BasicFeedingTab() : FilterTab("filter", 24) {}
    Size build(TabContext const& ctx, Rect area, WidgetGroup& widgets) override {
        return logTabBuilt(ctx, area, FilterTab::build(ctx, area, widgets), 24);
    }
};

ABP_REGISTER_NATIVE_TAB("feeding", [] { return std::make_unique<BasicFeedingTab>(); });
ABP_REGISTER_NATIVE_TAB("advanced_feeding", [] { return std::make_unique<AdvancedFeedingTab>(); });

} // namespace
} // namespace abp::client::ui::tabs::feeding

#endif // ABP_CLIENT
