// Native tab of the Alchemy / Advanced Alchemy upgrades (SC `AlchemyUpgradeTab`).
//
// Layout (SC values, relative to the tab's top-left; TOP_POS = 24):
//   - potion filter slots in 2 columns 36 apart, rows 20 apart: slot item at (4 + col*36,
//     24 + off + 1 + row*20), its condition toggle at (21 + col*36, 24 + off + row*20);
//   - Advanced (off = 20): toggles at (3, 24), (21, 24), (39, 24) for Any/All Effects Missing,
//     Match/Ignore Duration and Match/Ignore Amplifier, plus (57, 24) for the entity target when
//     the backpack isn't carried by a player.
// The open tab size is the controls' extent plus SC's 6 unit right / bottom border.
//
// Icons are original pixel art (no SC textures).

#ifdef ABP_CLIENT

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "mc/world/item/ItemStack.h"

#include "client/Client.h"
#include "client/ui/screen/Tabs.h"
#include "client/ui/widgets/SbStyle.h"
#include "client/ui/widgets/Tooltip.h"
#include "client/ui/widgets/Widgets.h"
#include "game/ItemConv.h"
#include "upgrade/alchemy/AlchemyRules.h"

namespace abp::client::ui {
namespace {

namespace al = abp::upgrade::alchemy;
using L      = Painter::Layer;

constexpr float kTopPos      = 24;
constexpr float kColumnStep  = 18 + 18;
constexpr float kRowStep     = 20;
constexpr float kRightBorder = 6;
constexpr float kBottom      = 6;

// ---- icons (16x16, original) -------------------------------------------------------------------

PixelArt const kNever{
    {
     "................",
     ".....RRRRRR.....",
     "...RRRRRRRRRR...",
     "..RRRrr...RRRR..",
     "..RRr....RRRRR..",
     ".RRr....RRRr.RR.",
     ".RR....RRRr..RR.",
     ".RR...RRRr...RR.",
     ".RR..RRRr....RR.",
     ".RR.RRRr.....RR.",
     ".RRRRRr.....rRR.",
     "..RRRRr....rRR..",
     "..RRRR...rrRRR..",
     "...RRRRRRRRRR...",
     ".....RRRRRR.....",
     "................",
     },
    {"R=B3201E", "r=7A1612"},
};
PixelArt const kCycle{
    {
     "................",
     "................",
     "...GGGGGGGGG....",
     "..GgggggggggG...",
     "..Gg.......gG...",
     "..Gg......GGGG..",
     "..Gg.....GGggGG.",
     "..Gg......GggG..",
     "..GG.......GG...",
     ".GGgG.......gG..",
     "GGggGG......gG..",
     ".GggG.......gG..",
     "..GG.......ggG..",
     "...GgggggggggG..",
     "....GGGGGGGGG...",
     "................",
     },
    {"G=2F6E22", "g=6CC24A"},
};
PixelArt const kCycleDisabled{kCycle.rows, {"G=4A4A4A", "g=8A8A8A"}};
PixelArt const kFire{
    {
     "................",
     ".......E........",
     "......EOE.......",
     "......EOE.......",
     ".....EOYOE..E...",
     "....EOYYOE.EOE..",
     "....EOYYYOEOOE..",
     "...EOYYWYYOOYOE.",
     "...EOYWWWYYYYOE.",
     "..EOYYWWWWYYYOE.",
     "..EOYWWWWWWYYOE.",
     "..EOYWWWWWWWYOE.",
     "..EOYYWWWWWYYOE.",
     "...EOYYYYYYYOE..",
     "....EEOOOOOOEE..",
     "......EEEEEE....",
     },
    {"E=8C2A0A", "O=E8641E", "Y=FFC23A", "W=FFF2B0"},
};
PixelArt const kWater{
    {
     "................",
     "................",
     "..DBBBBBBBBBBD..",
     "..BLLBBBBLLBBB..",
     "..BBBLLLLBBLLB..",
     "..BBBBBBBBBBBB..",
     "..BBBBBwBBBBBB..",
     "..BLLBBBBLLBBB..",
     "..BBBLLLLBBLLB..",
     "..BBBBBBBBBwBB..",
     "..BBwBBBBBBBBB..",
     "..BLLBBBBLLBBB..",
     "..BBBLLLLBBLLB..",
     "..BBBBBBBBBBBB..",
     "..DDDDDDDDDDDD..",
     "................",
     },
    {"B=3B66C8", "L=79A4EE", "D=27458C", "w=CFE3FF"},
};
PixelArt const kFalling{
    {
     "................",
     "..w....w....w...",
     "..w....w....w...",
     "..w.........w...",
     "................",
     "......SS........",
     "...A..SS..A.....",
     "...AA.TT.AA.....",
     "....ATTTTA......",
     "......TT........",
     "......TT........",
     ".....LLLL.......",
     "....LL..LL......",
     "...LL....LL.....",
     "...FF....FF.....",
     "................",
     },
    {"w=E8E8E8", "S=D9A47A", "A=C08860", "T=2E9E9A", "L=303A8C", "F=4A4A4A"},
};
PixelArt const kSprinting{
    {
     "................",
     "..........SS....",
     "..........SS....",
     "........TTT.....",
     "..ww...TTTTAA...",
     ".......TTT...A..",
     ".www..ATTT......",
     "......A.TT......",
     "..ww....LLL.....",
     ".......LL.LL....",
     ".www..LL...LL...",
     ".....LL.....LF..",
     "....FF.......F..",
     "................",
     "................",
     "................",
     },
    {"w=E8E8E8", "S=D9A47A", "A=C08860", "T=2E9E9A", "L=303A8C", "F=4A4A4A"},
};
PixelArt const kNegative{
    {
     "................",
     "................",
     "...KKK...KKK....",
     "..KPPPK.KGGGK...",
     ".KPPpPPKGgGGGK..",
     ".KPpPPPPGGGGGK..",
     ".KPPPPPPGGGGGK..",
     ".KPPPPPPGGGGGK..",
     "..KPPPPPGGGGK...",
     "...KPPPPGGGK....",
     "....KPPPGGK.....",
     ".....KPPGK......",
     "......KPK.......",
     ".......K........",
     "................",
     "................",
     },
    {"K=1A1A1A", "P=4A2A3E", "p=6E4A60", "G=4E9A2E", "g=86D05A"},
};
PixelArt const kMining{
    {
     "................",
     "....KKKKK.......",
     "...KLLLLLKK.....",
     "..KLK...KLLK....",
     "..K......KLK....",
     ".........WKLK...",
     "........W..KLK..",
     ".......W....KK..",
     "......W......K..",
     ".....W..........",
     "....W...........",
     "...W............",
     "..W.............",
     ".W..............",
     "................",
     "................",
     },
    {"K=3A3A3A", "L=A6A6A6", "W=8B5A2B"},
};
PixelArt const kHurt{
    {
     "................",
     "................",
     "................",
     "...KKK...KKK....",
     "..KRRRK.KDDDK...",
     ".KRRWRRKDDDDDK..",
     ".KRWRRRRDDDDDK..",
     ".KRRRRRRDDDDDK..",
     "..KRRRRRDDDDK...",
     "...KRRRRDDDK....",
     "....KRRRDDK.....",
     ".....KRRDK......",
     "......KRK.......",
     ".......K........",
     "................",
     "................",
     },
    {"K=1A1A1A", "R=D81E1E", "W=FF9A9A", "D=3C3C3C"},
};
PixelArt const kSquaresAny{
    {
     "................",
     "................",
     "..DDDDD..GGGGG..",
     "..DddDD..GggGG..",
     "..DdDDD..GgGGG..",
     "..DDDDD..GGGGG..",
     "..DDDDD..GGGGG..",
     "................",
     "................",
     "..GGGGG..GGGGG..",
     "..GggGG..GggGG..",
     "..GgGGG..GgGGG..",
     "..GGGGG..GGGGG..",
     "..GGGGG..GGGGG..",
     "................",
     "................",
     },
    {"D=5A5A5A", "d=8A8A8A", "G=3E8C2C", "g=7ED05A"},
};
PixelArt const kSquaresAll{kSquaresAny.rows, {"D=5A5A5A", "d=8A8A8A", "G=5A5A5A", "g=8A8A8A"}};
PixelArt const kDuration{
    {
     "................",
     "................",
     "................",
     "................",
     "................",
     "..X....XXX.XXX..",
     ".XX..X.X.X.X.X..",
     "..X....X.X.X.X..",
     "..X..X.X.X.X.X..",
     ".XXX...XXX.XXX..",
     "................",
     "................",
     "................",
     "................",
     "................",
     "................",
     },
    {"X=1E1E1E"},
};
PixelArt const kAmplifier{
    {
     "................",
     "................",
     "..XXXXX.XXXXX...",
     "...XXX...XXX....",
     "...XXX...XXX....",
     "...XXX...XXX....",
     "...XXX...XXX....",
     "...XXX...XXX....",
     "...XXX...XXX....",
     "...XXX...XXX....",
     "...XXX...XXX....",
     "...XXX...XXX....",
     "..XXXXX.XXXXX...",
     "................",
     "................",
     "................",
     },
    {"X=1E1E1E"},
};
PixelArt const kZombieHead{
    {
     "GGGGGGGGGG",
     "GgGGGGGGgG",
     "GGGGGGGGGG",
     "GGGGGGGGGG",
     "GKKGGGGKKG",
     "GKKGGGGKKG",
     "GGGGddGGGG",
     "GGGddddGGG",
     "GGdGGGGdGG",
     "GGGGGGGGGG",
     },
    {"G=4E8A3A", "g=5FA048", "K=1E2E1A", "d=2F5A24"},
};
PixelArt const kPlayerHead{
    {
     "HHHHHHHHHH",
     "HHHHHHHHHH",
     "HSSSSSSSSH",
     "SSSSSSSSSS",
     "SWBSSSSBWS",
     "SSSSSSSSSS",
     "SSSSnnSSSS",
     "SSmSSSSmSS",
     "SSSmmmmSSS",
     "SSSSSSSSSS",
     },
    {"H=5A3A22", "S=D9A47A", "W=F0F0F0", "B=3A5AA0", "n=C08860", "m=8A4A3A"},
};
PixelArt const kEmptyPotion{
    {
     "................",
     "......XXXX......",
     ".......XX.......",
     ".......XX.......",
     "......X..X......",
     ".....X....X.....",
     "....X......X....",
     "....X......X....",
     "....X......X....",
     "....X......X....",
     ".....X....X.....",
     "......XXXX......",
     "................",
     "................",
     "................",
     "................",
     },
    {"X=6E6E6E"},
};

void art(Painter& p, PixelArt const& a, float x, float y) { p.pixels(a, x, y, 1, L::Foreground); }

void conditionIcon(Painter& p, al::Condition c, float x, float y) {
    switch (c) {
    case al::Condition::Never:
        return art(p, kNever, x, y);
    case al::Condition::Always:
        return art(p, kCycle, x, y);
    case al::Condition::UnderWater:
        return art(p, kWater, x, y);
    case al::Condition::OnFire:
        return art(p, kFire, x, y);
    case al::Condition::Falling:
        return art(p, kFalling, x, y);
    case al::Condition::Mining:
        return art(p, kMining, x, y);
    case al::Condition::Sprinting:
        return art(p, kSprinting, x, y);
    case al::Condition::Hurt:
        return art(p, kHurt, x, y);
    case al::Condition::NegativeEffect:
        return art(p, kNegative, x, y);
    }
}

std::string conditionLabel(al::Condition c, float value) {
    switch (c) {
    case al::Condition::Never:
        return ctr("abp.alchemy.condition.never", "Never");
    case al::Condition::Always:
        return ctr("abp.alchemy.condition.always", "Keep Active");
    case al::Condition::UnderWater:
        return ctr("abp.alchemy.condition.under_water", "When Under Water");
    case al::Condition::OnFire:
        return ctr("abp.alchemy.condition.on_fire", "When on Fire");
    case al::Condition::Falling:
        return ctr("abp.alchemy.condition.falling", "When Falling");
    case al::Condition::Mining:
        return ctr("abp.alchemy.condition.mining", "When Mining");
    case al::Condition::Sprinting:
        return ctr("abp.alchemy.condition.sprinting", "When Sprinting");
    case al::Condition::Hurt: {
        // SC: the value in DARK_GREEN inside the white label.
        std::string pct = value < 0 ? std::string("%s") : "§2" + std::to_string(al::percent(value)) + "%§f";
        return ctr("abp.alchemy.condition.hurt", "When Hurt (Below {} Health)", pct);
    }
    case al::Condition::NegativeEffect:
        return ctr("abp.alchemy.condition.negative_effect", "When Affected by Negative Effect");
    }
    return {};
}

// ---- widgets ------------------------------------------------------------------------------------

struct FilterView {
    ItemStack    stack;
    bool         empty     = true;
    al::Condition condition = al::Condition::Never;
    float        value     = -1;
};

/// One potion filter slot (SC FilterSlotItemHandler with EMPTY_POTION_SLOT_BACKGROUND).
class PotionSlot final : public Widget {
public:
    std::function<FilterView const*()> view;
    std::function<void()>              onClick;

    void render(Painter& p, float mx, float my, double) override {
        float x = rect.x + 1, y = rect.y + 1;
        sb::slotFrame(p, x, y);
        auto const* v = view ? view() : nullptr;
        if (v && !v->empty) p.item(v->stack, x, y);
        else p.pixels(kEmptyPotion, x, y, 1, L::Background);
        if (hovered(mx, my)) p.fillRect({x, y, 16, 16}, sb::color::HOVER, L::Foreground);
    }
    bool mouseDown(MouseButton, float x, float y) override {
        if (!hovered(x, y)) return false;
        if (onClick) onClick();
        return true;
    }
    void tooltip(float x, float y, std::vector<TooltipLine>& out) const override {
        if (!hovered(x, y)) return;
        auto const* v = view ? view() : nullptr;
        if (!v || v->empty) return;
        auto lines = itemTooltipLines(v->stack);
        out.insert(out.end(), lines.begin(), lines.end());
    }
};

/// The condition toggle next to a filter slot (SC ALCHEMY_CONDITION toggle button): disabled
/// icon without a filter, the HURT threshold in the tooltip and adjustable with the wheel.
class ConditionButton final : public Widget {
public:
    std::function<FilterView const*()> view;
    std::function<void()>              onClick;
    std::function<void(int dir)>       onScroll;

    void render(Painter& p, float mx, float my, double) override {
        auto const* v = view ? view() : nullptr;
        if (!v || v->empty) {
            art(p, kCycleDisabled, rect.x + 1, rect.y + 1);
            return;
        }
        sb::buttonBackground(p, rect, hovered(mx, my));
        conditionIcon(p, v->condition, rect.x + 1, rect.y + 1);
    }
    bool mouseDown(MouseButton, float x, float y) override {
        if (!hovered(x, y)) return false;
        auto const* v = view ? view() : nullptr;
        if (v && !v->empty && onClick) onClick();
        return true;
    }
    bool scroll(float x, float y, float delta) override {
        auto const* v = view ? view() : nullptr;
        if (!hovered(x, y) || !v || v->empty || v->condition != al::Condition::Hurt) return false;
        if (onScroll && delta != 0) onScroll(delta > 0 ? 1 : -1);
        return true;
    }
    void tooltip(float x, float y, std::vector<TooltipLine>& out) const override {
        if (!hovered(x, y)) return;
        auto const* v = view ? view() : nullptr;
        if (!v || v->empty) return;
        out.push_back({conditionLabel(v->condition, v->value), 0xFFFFFF});
        if (v->value >= 0 && v->condition == al::Condition::Hurt) {
            out.push_back({ctr("abp.alchemy.condition.value_controls", "Scroll to Change Value"), 0x555555});
        }
    }
};

IconFn iconOf(PixelArt const& a) {
    return [&a](Painter& p, float x, float y) { art(p, a, x + 1, y + 1); };
}
IconFn iconOf(PixelArt const& a, PixelArt const& overlay) {
    return [&a, &overlay](Painter& p, float x, float y) {
        art(p, a, x + 1, y + 1);
        art(p, overlay, x + 1, y + 1);
    };
}
IconFn headIcon(PixelArt const& head) {
    return [&head](Painter& p, float x, float y) { art(p, head, x + 4, y + 4); };
}
void bothHeads(Painter& p, float x, float y) {
    art(p, kPlayerHead, x + 1, y + 1);
    art(p, kZombieHead, x + 7, y + 7);
}

// ---- the tab ------------------------------------------------------------------------------------

class AlchemyTab final : public NativeTab {
public:
    Size build(TabContext const& ctx, Rect area, WidgetGroup& widgets) override {
        update(ctx);
        bool  advanced = mAdvanced;
        float off      = advanced ? 20.f : 0.f;
        float right = 0, bottom = 0;
        auto  extend = [&](Rect const& r) {
            right  = std::max(right, r.x + r.w - area.x);
            bottom = std::max(bottom, r.y + r.h - area.y);
        };
        auto send = ctx.sendTabAction;

        if (advanced) {
            auto toggle = [&](float dx, std::vector<ToggleButton::State> states, std::function<int()> current, char const* action) {
                auto& b   = widgets.add(std::make_unique<ToggleButton>());
                b.size    = Button::Size::Normal;
                b.rect    = {area.x + 3 + dx, area.y + kTopPos, 18, 18};
                b.states  = std::move(states);
                b.current = std::move(current);
                b.onClick = [send, action](int) {
                    if (send) send({{"alchemy", action}});
                };
                extend(b.rect);
            };
            // Boolean toggles: state 0 = true, 1 = false (SC toggles between the two).
            toggle(
                0,
                {{iconOf(kSquaresAny), {{ctr("abp.alchemy.any_effect_missing", "Any Effect Missing"), 0xFFFFFF}}},
                 {iconOf(kSquaresAll), {{ctr("abp.alchemy.all_effects_missing", "All Effects Missing"), 0xFFFFFF}}}},
                [this] { return mMatchAll ? 0 : 1; },
                "matchAll"
            );
            toggle(
                18,
                {{iconOf(kDuration), {{ctr("abp.alchemy.match_duration", "Match Duration"), 0xFFFFFF}}},
                 {iconOf(kDuration, kNever), {{ctr("abp.alchemy.ignore_duration", "Ignore Duration"), 0xFFFFFF}}}},
                [this] { return mMatchDuration ? 0 : 1; },
                "matchDuration"
            );
            toggle(
                36,
                {{iconOf(kAmplifier), {{ctr("abp.alchemy.match_amplifier", "Match Amplifier"), 0xFFFFFF}}},
                 {iconOf(kAmplifier, kNever), {{ctr("abp.alchemy.ignore_amplifier", "Ignore Amplifier"), 0xFFFFFF}}}},
                [this] { return mMatchAmplifier ? 0 : 1; },
                "matchAmplifier"
            );
            if (mEntityMatchOption) {
                toggle(
                    54,
                    {{headIcon(kPlayerHead), {{ctr("abp.alchemy.entity_match.players", "Target Players Only"), 0xFFFFFF}}},
                     {bothHeads,
                      {{ctr("abp.alchemy.entity_match.players_and_entities", "Target Both Players & Entities"), 0xFFFFFF}}},
                     {headIcon(kZombieHead), {{ctr("abp.alchemy.entity_match.entities", "Target Entities Only"), 0xFFFFFF}}}},
                    [this] { return static_cast<int>(mEntityMatch); },
                    "entityMatch"
                );
            }
        }

        for (int i = 0; i < static_cast<int>(mFilters.size()); ++i) {
            float col = static_cast<float>(i % 2), row = static_cast<float>(i / 2);
            auto  view = [this, i]() -> FilterView const* {
                return i < static_cast<int>(mFilters.size()) ? &mFilters[static_cast<std::size_t>(i)] : nullptr;
            };
            auto& slot = widgets.add(std::make_unique<PotionSlot>());
            // Slot item at (4 + col*36, TOP_POS + off + 1 + row*20); the frame starts 1 unit before.
            slot.rect    = {area.x + 3 + col * kColumnStep, area.y + kTopPos + off + row * kRowStep, 18, 18};
            slot.view    = view;
            slot.onClick = [send, i] {
                if (send) send({{"alchemy", "filter"}, {"index", i}});
            };
            extend(slot.rect);

            auto& cond    = widgets.add(std::make_unique<ConditionButton>());
            cond.rect     = {area.x + 21 + col * kColumnStep, area.y + kTopPos + off + row * kRowStep, 18, 18};
            cond.view     = view;
            cond.onClick  = [send, i] {
                if (send) send({{"alchemy", "condition"}, {"index", i}});
            };
            cond.onScroll = [send, i](int dir) {
                if (send) send({{"alchemy", "scroll"}, {"index", i}, {"dir", dir}});
            };
            extend(cond.rect);
        }
        return {right + kRightBorder, bottom + kBottom};
    }

    void update(TabContext const& ctx) override {
        auto const& tab = ctx.upgrade.tab;
        auto        it  = tab.find("alchemy");
        if (it == tab.end() || !it->is_object()) return;
        auto const& j      = *it;
        mAdvanced          = j.value("advanced", false);
        mEntityMatchOption = j.value("entityMatchOption", false);
        auto s             = al::Settings::fromJson(j, -1);
        mMatchAll          = s.matchAll;
        mMatchDuration     = s.matchDuration;
        mMatchAmplifier    = s.matchAmplifier;
        mEntityMatch       = s.entityMatch;
        // Widgets keep pointers into mFilters: keep its size stable once built.
        if (mFilters.size() != s.filters.size()) mFilters.resize(s.filters.size());
        for (std::size_t i = 0; i < s.filters.size(); ++i) {
            auto& v     = mFilters[i];
            auto  d     = s.filters[i];
            v.empty     = d.filter.empty();
            v.condition = d.condition;
            v.value     = d.value;
            v.stack     = v.empty ? ItemStack{} : game::ItemConv::toStack(d.filter, 1);
        }
    }

private:
    std::vector<FilterView> mFilters;
    bool                    mAdvanced          = false;
    bool                    mEntityMatchOption = false;
    bool                    mMatchAll          = true;
    bool                    mMatchDuration     = true;
    bool                    mMatchAmplifier    = true;
    al::EntityMatch         mEntityMatch       = al::EntityMatch::PlayersAndEntities;
};

} // namespace
} // namespace abp::client::ui

ABP_REGISTER_NATIVE_TAB("alchemy", [] { return std::make_unique<abp::client::ui::AlchemyTab>(); });
ABP_REGISTER_NATIVE_TAB("advanced_alchemy", [] { return std::make_unique<abp::client::ui::AlchemyTab>(); });

#endif // ABP_CLIENT
