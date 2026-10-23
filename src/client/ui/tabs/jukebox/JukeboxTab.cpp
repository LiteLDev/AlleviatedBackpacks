// Native tab of the jukebox upgrades (SC JukeboxUpgradeTab.Basic / .Advanced).
//
// Layout (SC): disc slots from (x+3, y+24), `slotsInRow` per row (basic: 4, one slot); the button
// row 3 below the slots: basic Stop (x+3), Play (x+21); advanced Previous (x+3), Stop (x+21),
// Play (x+39), Next (x+57) and below them Shuffle (x+12) and Repeat (x+48). The playing disc's slot
// gets SC's green "remaining time" overlay (0x5500CC00, shrinking from the left). The tab is sized
// by its buttons (+6 border), as SC's SettingsTabBase does.
//
// Server side: upgrade/jukebox/JukeboxUpgrade.cpp (tab data "jukebox", tab action "jukebox"); disc
// slot clicks go to the jukebox's server menu through the tab menu bridge ("menuClick").

#ifdef ABP_CLIENT

#include <chrono>
#include <algorithm>
#include <memory>
#include <vector>

#include "mc/world/item/ItemStack.h"

#include "client/Client.h"
#include "client/input/InputRouter.h"
#include "client/ui/screen/Tabs.h"
#include "core/ItemStackData.h"
#include "game/ItemConv.h"

namespace abp::client::ui {

namespace {

using L = Painter::Layer;

// ---- icons (original pixel art, 16x16, drawn at the button's +1,+1) -------------------------------
PixelArt const kPlay{
    {
     "................",
     "................",
     "....DD..........",
     "....DLD.........",
     "....DLLD........",
     "....DLLLD.......",
     "....DLLLLD......",
     "....DLLLLLD.....",
     "....DLLLLLD.....",
     "....DLLLLD......",
     "....DLLLD.......",
     "....DLLD........",
     "....DLD.........",
     "....DD..........",
     "................",
     "................",
     },
    {"D=1E6B1E", "L=4CD14C"},
};
PixelArt const kStop{
    {
     "................",
     "................",
     "................",
     "...DDDDDDDDDD...",
     "...DLLLLLLLLD...",
     "...DLLLLLLLLD...",
     "...DLLLLLLLLD...",
     "...DLLLLLLLLD...",
     "...DLLLLLLLLD...",
     "...DLLLLLLLLD...",
     "...DLLLLLLLLD...",
     "...DLLLLLLLLD...",
     "...DDDDDDDDDD...",
     "................",
     "................",
     "................",
     },
    {"D=6B1E1E", "L=D14C4C"},
};
PixelArt const kPrevious{
    {
     "................",
     "................",
     "................",
     "..DD.....DD..DD.",
     "..DD....DLD.DLD.",
     "..DD...DLLDDLLD.",
     "..DD..DLLLLLLLD.",
     "..DD.DLLLLLLLLD.",
     "..DD.DLLLLLLLLD.",
     "..DD..DLLLLLLLD.",
     "..DD...DLLDDLLD.",
     "..DD....DLD.DLD.",
     "..DD.....DD..DD.",
     "................",
     "................",
     "................",
     },
    {"D=2B2B6B", "L=6C8CE0"},
};
PixelArt const kNext{
    {
     "................",
     "................",
     "................",
     ".DD..DD.....DD..",
     ".DLD.DLD....DD..",
     ".DLLDDLLD...DD..",
     ".DLLLLLLLD..DD..",
     ".DLLLLLLLLD.DD..",
     ".DLLLLLLLLD.DD..",
     ".DLLLLLLLD..DD..",
     ".DLLDDLLD...DD..",
     ".DLD.DLD....DD..",
     ".DD..DD.....DD..",
     "................",
     "................",
     "................",
     },
    {"D=2B2B6B", "L=6C8CE0"},
};
PixelArt const kShuffleOn{
    {
     "................",
     "................",
     "...........L....",
     "DDD.......DLL...",
     "LLLD....DDLLLL..",
     "DDDLD..DLLLLL...",
     "...DLDDLDDDL....",
     "....DLLD...L....",
     "....DLLD...L....",
     "...DLDDLDDDL....",
     "DDDLD..DLLLLL...",
     "LLLD....DDLLLL..",
     "DDD.......DLL...",
     "...........L....",
     "................",
     "................",
     },
    {"D=1E6B1E", "L=4CD14C"},
};
PixelArt const kShuffleOff{
    {
     "................",
     "................",
     "................",
     "................",
     "LLLLLLLLLLLL....",
     "DDDDDDDDDDDLL...",
     "...........LLL..",
     "...........LL...",
     "...........LL...",
     "...........LLL..",
     "DDDDDDDDDDDLL...",
     "LLLLLLLLLLLL....",
     "................",
     "................",
     "................",
     "................",
     },
    {"D=373737", "L=8B8B8B"},
};
PixelArt const kRepeatAll{
    {
     "................",
     "................",
     "........L.......",
     "..DDDDDDLL......",
     ".DLLLLLLLLL.....",
     ".DL.....LL......",
     ".DL.....L.......",
     ".DL..........LD.",
     ".DL..........LD.",
     ".......L.....LD.",
     "......LL.....LD.",
     ".....LLLLLLLLLD.",
     "......LLDDDDDD..",
     ".......L........",
     "................",
     "................",
     },
    {"D=1E6B1E", "L=4CD14C"},
};
PixelArt const kRepeatOne{
    {
     "................",
     "................",
     "........L.......",
     "..DDDDDDLL......",
     ".DLLLLLLLLL.....",
     ".DL.....LL......",
     ".DL.....L..WW...",
     ".DL.......WWW.LD",
     ".DL........WW.LD",
     ".......L...WW.LD",
     "......LL...WW.LD",
     ".....LLLLLLLLLLD",
     "......LLDDDDDDD.",
     ".......L........",
     "................",
     "................",
     },
    {"D=1E6B1E", "L=4CD14C", "W=FFFFFF"},
};
PixelArt const kNoRepeat{
    {
     "................",
     "................",
     "........L.......",
     "..DDDDDDLL...RR.",
     ".DLLLLLLLLL.RR..",
     ".DL.....LL.RR...",
     ".DL.....L.RR....",
     ".DL......RR..LD.",
     ".DL.....RR...LD.",
     ".......RR....LD.",
     "......RR.....LD.",
     ".....RRLLLLLLLD.",
     "....RR.LDDDDDD..",
     "...RR..L........",
     "................",
     "................",
     },
    {"D=373737", "L=8B8B8B", "R=C83232"},
};

IconFn art(PixelArt const& a) {
    return [&a](Painter& p, float x, float y) { p.pixels(a, x + 1, y + 1, 1, L::Foreground); };
}

std::vector<TooltipLine> line(std::string_view key, std::string_view fallback) { return {{ctr(key, fallback), 0xFFFFFF}}; }

class JukeboxTab final : public NativeTab {
public:
    Size build(TabContext const& ctx, Rect area, WidgetGroup& widgets) override {
        mArea      = area;
        auto j     = data(ctx);
        mAdvanced  = j.value("advanced", false);
        mPerRow    = std::max(1, j.value("slotsInRow", 4));
        int count  = static_cast<int>(j.value("discs", nlohmann::json::array()).size());
        mCount     = std::max(1, count);
        update(ctx);

        // Disc slots (SC renderSlotsBackground at x+3, y+24; slots at +1).
        auto& grid   = widgets.add(std::make_unique<SlotGrid>());
        mGrid        = &grid;
        int rows     = (mCount + mPerRow - 1) / mPerRow;
        grid.columns = mPerRow;
        grid.count   = mCount;
        grid.rect    = {area.x + 3, area.y + 24, static_cast<float>(mPerRow) * 18, static_cast<float>(rows) * 18};
        grid.item    = [this](int i) -> ItemStack const* {
            return i >= 0 && i < static_cast<int>(mDiscs.size()) && !mDiscs[static_cast<std::size_t>(i)].isNull() ? &mDiscs[static_cast<std::size_t>(i)] : nullptr;
        };
        auto send    = ctx.sendTabAction;
        grid.onClick = [this, send](int i, int button, bool) {
            if (!send || i < 0 || i >= static_cast<int>(mDiscMenu.size())) return;
            send(nlohmann::json{{"menuClick", mDiscMenu[static_cast<std::size_t>(i)]}, {"button", button}, {"shift", input::shiftDown()}});
        };

        // SC getBottomSlotY: TOP_Y + full rows * 18 (+18 for a partial row).
        float bottom = 24 + static_cast<float>(mCount / mPerRow) * 18 + (mCount % mPerRow > 0 ? 18 : 0);
        float by     = area.y + bottom + 3;
        float right = 0, lowest = 0;
        auto  addButton = [&](float x, float y, PixelArt const& icon, std::vector<TooltipLine> tip, std::string op) {
            auto& b        = widgets.add(std::make_unique<Button>());
            b.size         = Button::Size::Normal;
            b.rect         = {area.x + x, y, 18, 18};
            b.icon         = art(icon);
            b.tooltipLines = std::move(tip);
            b.onClick      = [send, op = std::move(op)](int button) {
                if (button == 0 && send) send(nlohmann::json{{"jukebox", op}});
            };
            right  = std::max(right, x + 18);
            lowest = std::max(lowest, y - area.y + 18);
        };
        if (!mAdvanced) {
            addButton(3, by, kStop, line("abp.jukebox.stop", "Stop"), "stop");
            addButton(21, by, kPlay, line("abp.jukebox.play", "Play"), "play");
        } else {
            addButton(3, by, kPrevious, line("abp.jukebox.previous", "Previous"), "previous");
            addButton(21, by, kStop, line("abp.jukebox.stop", "Stop"), "stop");
            addButton(39, by, kPlay, line("abp.jukebox.play", "Play"), "play");
            addButton(57, by, kNext, line("abp.jukebox.next", "Next"), "next");

            auto& shuffle   = widgets.add(std::make_unique<ToggleButton>());
            shuffle.rect    = {area.x + 12, by + 20, 18, 18};
            shuffle.states  = {
                {art(kShuffleOff), line("abp.jukebox.shuffle_off", "Shuffle Disabled")},
                {art(kShuffleOn), line("abp.jukebox.shuffle_on", "Shuffle Enabled")},
            };
            shuffle.current = [this] { return mShuffle ? 1 : 0; };
            shuffle.onClick = [send](int button) {
                if (button == 0 && send) send(nlohmann::json{{"jukebox", "shuffle"}});
            };
            auto& repeat   = widgets.add(std::make_unique<ToggleButton>());
            repeat.rect    = {area.x + 48, by + 20, 18, 18};
            repeat.states  = {
                {art(kRepeatAll), line("abp.jukebox.repeat_all", "Repeat All")},
                {art(kRepeatOne), line("abp.jukebox.repeat_one", "Repeat One")},
                {art(kNoRepeat), line("abp.jukebox.no_repeat", "Repeat Disabled")},
            };
            repeat.current = [this] { return mRepeat; };
            repeat.onClick = [send](int button) {
                if (button == 0 && send) send(nlohmann::json{{"jukebox", "repeat"}});
            };
            right  = std::max(right, 48.f + 18);
            lowest = std::max(lowest, by + 20 - area.y + 18);
        }
        return {right + 6, lowest + 6};
    }

    void update(TabContext const& ctx) override {
        auto j = data(ctx);
        mDiscs.clear();
        for (auto const& d : j.value("discs", nlohmann::json::array())) {
            core::ItemStackData item;
            try {
                if (!d.is_null()) item = core::ItemStackData::fromJson(d);
            } catch (...) {
            }
            mDiscs.push_back(item.empty() ? ItemStack{} : game::ItemConv::toStack(item, 1));
        }
        mDiscMenu.clear();
        for (auto const& m : j.value("discMenu", nlohmann::json::array())) mDiscMenu.push_back(m.is_number_integer() ? m.get<int>() : -1);
        mShuffle  = j.value("shuffle", false);
        auto r    = j.value("repeat", std::string("no"));
        mRepeat   = r == "all" ? 0 : r == "one" ? 1 : 2;
        int playing  = j.value("playing", -1);
        int elapsed  = j.value("elapsed", 0);
        int duration = j.value("duration", 0);
        // SC computes the remaining time every frame from the game time (getDiscFinishTime); the
        // server sends whole seconds, so the client runs its own clock between updates. The true
        // elapsed time is in [elapsed, elapsed + 19] ticks: the estimate is kept in that range.
        double est = estimatedElapsed();
        if (playing != mPlaying || duration != mDuration || est < elapsed || est > elapsed + 40) {
            setBase(elapsed);
        } else if (est > elapsed + 19) {
            setBase(elapsed + 19);
        }
        mPlaying  = playing;
        mElapsed  = elapsed;
        mDuration = duration;
    }

    void render(Painter& p, TabContext const&, Rect area, float, float) override {
        // SC renderPlaytimeOverLay: the remaining part of the playing disc's slot in green.
        if (!mAdvanced || mPlaying < 0 || mPlaying >= mCount || mDuration <= 0) return;
        float remaining = static_cast<float>((mDuration - estimatedElapsed()) / mDuration);
        if (remaining <= 0) return;
        int   over = 16 - static_cast<int>(16 * remaining);
        float x    = area.x + 4 + static_cast<float>(mPlaying % mPerRow) * 18;
        float y    = area.y + 25 + static_cast<float>(mPlaying / mPerRow) * 18;
        p.fillRect({x + static_cast<float>(over), y, static_cast<float>(16 - over), 16}, Color::argb(0x5500CC00), L::Background);
    }

private:
    static nlohmann::json data(TabContext const& ctx) {
        auto it = ctx.upgrade.tab.find("jukebox");
        return it != ctx.upgrade.tab.end() && it->is_object() ? *it : nlohmann::json::object();
    }

    Rect                   mArea;
    SlotGrid*              mGrid     = nullptr;
    bool                   mAdvanced = false;
    int                    mPerRow   = 4;
    int                    mCount    = 1;
    std::vector<ItemStack> mDiscs;
    std::vector<int>       mDiscMenu;
    bool                   mShuffle  = false;
    int                    mRepeat   = 2;
    int                    mPlaying  = -1;
    int                    mElapsed  = 0;
    int                    mDuration = 0;
    double                 mBaseTicks = 0; ///< elapsed ticks at mBaseAt
    double                 mBaseAt    = -1;

    static double nowSeconds() {
        using namespace std::chrono;
        return duration<double>(steady_clock::now().time_since_epoch()).count();
    }
    void setBase(double ticks) {
        mBaseTicks = ticks;
        mBaseAt    = nowSeconds();
    }
    /// Elapsed ticks now (20 per second since the last base), never past the disc's length.
    [[nodiscard]] double estimatedElapsed() const {
        if (mBaseAt < 0 || mPlaying < 0) return mElapsed;
        // At most 2 s past the last server value: a stalled server stops the overlay too.
        return std::min<double>({mBaseTicks + (nowSeconds() - mBaseAt) * 20.0, static_cast<double>(mElapsed) + 40.0, static_cast<double>(mDuration)});
    }
};

} // namespace

ABP_REGISTER_NATIVE_TAB("jukebox", [] { return std::make_unique<JukeboxTab>(); });
ABP_REGISTER_NATIVE_TAB("advanced_jukebox", [] { return std::make_unique<JukeboxTab>(); });

} // namespace abp::client::ui

#endif // ABP_CLIENT
