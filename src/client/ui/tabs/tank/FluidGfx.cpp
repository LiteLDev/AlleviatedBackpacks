#ifdef ABP_CLIENT

#include "client/ui/tabs/tank/FluidGfx.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "mc/world/item/ItemStack.h"

#include "client/input/InputRouter.h"
#include "client/ui/widgets/SbStyle.h"
#include "client/ui/widgets/Tooltip.h"
#include "core/ItemStackData.h"
#include "core/screen/ScreenModel.h"
#include "game/ItemConv.h"

namespace abp::client::ui::fluidgfx {

namespace {

Color shade(std::uint32_t argb, float f) {
    auto c = Color::argb(argb | 0xFF000000u);
    auto s = [f](float v) { return std::clamp(f >= 1 ? v + (1 - v) * (f - 1) : v * f, 0.f, 1.f); };
    return {s(c.r), s(c.g), s(c.b), 1};
}

/// A 16x16 "still fluid" pattern: per row, runs of shade 0 (dark), 1 (base), 2 (light).
struct Run {
    int x, w, shade;
};
std::array<std::vector<Run>, 16> const& pattern() {
    static auto const runs = [] {
        std::array<std::vector<Run>, 16> out;
        for (int y = 0; y < 16; ++y) {
            int prev = -1, start = 0;
            for (int x = 0; x <= 16; ++x) {
                int s = 1;
                if (x < 16) {
                    // Wavy bands: a light crest and a dark trough drifting diagonally.
                    int v = (x * 3 + y * 5 + ((x * y) % 7)) % 16;
                    s     = v < 3 ? 2 : v > 12 ? 0 : 1;
                }
                if (x == 16 || s != prev) {
                    if (prev >= 0) out[static_cast<std::size_t>(y)].push_back({start, x - start, prev});
                    prev  = s;
                    start = x;
                }
            }
        }
        return out;
    }();
    return runs;
}

PixelArt art(std::vector<std::string_view> rows, std::vector<std::string_view> palette) { return {std::move(rows), std::move(palette)}; }

std::vector<std::string_view> const kPalette = {
    "k=1E1E1E", "G=7DD87D", "g=3B9A3B", "R=EE6060", "r=B32424", "S=D8D8D8", "s=9A9A9A", "d=5E5E5E",
    "n=5DA130", "b=7A5230", "w=3F76E4", "O=F2FFB0", "o=9BE03A", "q=373737",
};

PixelArt const& iconArt(Icon i) {
    static PixelArt const pumpInput = art(
        {"................", "................", "................", "......k.........", ".....kGk........", "....kGGk........",
         "...kGGGkkkkkkk..", "..kGGGGGGGGGGGk.", "..kgggggggggggk.", "...kgggkkkkkkk..", "....kggk........", ".....kgk........",
         "......k.........", "................", "................", "................"},
        kPalette
    );
    static PixelArt const pumpOutput = art(
        {"................", "................", "................", ".........k......", "........kRk.....", "........kRRk....",
         "..kkkkkkkRRRk...", ".kRRRRRRRRRRRk..", ".krrrrrrrrrrrk..", "..kkkkkkkrrrk...", "........krrk....", "........krk.....",
         ".........k......", "................", "................", "................"},
        kPalette
    );
    static PixelArt const world = art(
        {"................", "................", "................", ".kkkkk..........", ".kSSSk..........", ".kSsdk....kkkkkk",
         ".kSsdkkkk.knnnnk", ".kSsddddk.knnnnk", ".kSsddddk.kbnbbk", ".kSsdkkkk.kbbbbk", ".kSsdk....kbbnbk", ".kdddk....kbbbbk",
         ".kkkkk....kkkkkk", "................", "................", "................"},
        kPalette
    );
    static PixelArt const hand = art(
        {"................", "................", "................", ".kkkkk..........", ".kSSSk..........", ".kSsdk....k....k",
         ".kSsdkkkk.kk..kk", ".kSsddddk.kwwwwk", ".kSsddddk.kwwwwk", ".kSsdkkkk.kwwwwk", ".kSsdk.....kwwk.", ".kdddk.....kkkk.",
         ".kkkkk..........", "................", "................", "................"},
        kPalette
    );
    static PixelArt const pipes = art(
        {"................", "................", "................", ".kkkkk..........", ".kSSSk..........", ".kSsdk....k.....",
         ".kSsdkkkk.kkkkkk", ".kSsddddk.ksSSSS", ".kSsddddk.ksssss", ".kSsdkkkk.kkkkkk", ".kSsdk....k.....", ".kdddk..........",
         ".kkkkk..........", "................", "................", "................"},
        kPalette
    );
    static PixelArt const xpOutput = art(
        {"................", "......kkkk......", "......kRrk......", "......kRrk......", "......kRrk......", "......kRrk......",
         "..kkkkkRrkkkkk..", "...kRRRRrrrrk...", "....kRRRrrrk....", ".....kRRrrk.....", "......kRrk......", ".......kk.......",
         "................", "................", "................", "................"},
        kPalette
    );
    static PixelArt const xpKeep = art(
        {"................", "...kk...........", "..kGgk..........", ".kGGggk.........", "kGGGgggk........", "kkkGgkkk..kkkk..",
         "...kGgk...kRrk..", "...kGgk...kRrk..", "...kGgk...kRrk..", "...kGgk...kRrk..", "...kkkk.kkkRrkkk", ".........kRRrrk.",
         "..........kRrk..", "...........kk...", "................", "................"},
        kPalette
    );
    static PixelArt const mend = art(
        {"................", "...O........kkk.", "..OOO......kSSk.", "...O......kSSk..", ".........kSSk...", "........kSSk....",
         ".......kSSk.....", "..kk..kSSk......", "..kSkkSSk.......", "...kSSSk....O...", "....kSk....OOO..", "...kbkSk....O...",
         "..kbk.kk........", ".kbk............", ".kk.............", "................"},
        kPalette
    );
    static PixelArt const take = art(
        {"................", ".....kk.........", "....kGgk........", "...kGGggk.......", "..kGGGgggk......", "..kkkGgkkk......",
         "....kGgk........", "....kGgk........", "....kGgk........", "....kGgk........", "....kkkk........", "............oo..",
         "...........oOOo.", "...........oOOo.", "............oo..", "................"},
        kPalette
    );
    static PixelArt const takeAll = art(
        {"................", ".....kk.........", "....kGgk........", "...kGGggk.......", "..kGGkkggk......", "..kkkGgkkk......",
         "...kGGggk.......", "..kGGGgggk......", "..kkkGgkkk......", "....kGgk........", "....kkkk........", "............oo..",
         "...........oOOo.", "...........oOOo.", "............oo..", "................"},
        kPalette
    );
    static PixelArt const store = art(
        {"................", "....kkkk........", "....kRrk........", "....kRrk........", "....kRrk........", "..kkkRrkkk......",
         "..kRRRrrrk......", "...kRRrrk.......", "....kRrk........", ".....kk.........", "................", "............oo..",
         "...........oOOo.", "...........oOOo.", "............oo..", "................"},
        kPalette
    );
    static PixelArt const storeAll = art(
        {"................", "....kkkk........", "....kRrk........", "..kkkRrkkk......", "..kRRRrrrk......", "...kRRrrk.......",
         "..kkkRrkkk......", "..kRRkkrrk......", "...kRRrrk.......", "....kRrk........", ".....kk.........", "............oo..",
         "...........oOOo.", "...........oOOo.", "............oo..", "................"},
        kPalette
    );
    static PixelArt const tankInput = art(
        {"................", "................", "...qqqqqqqqqq...", "..q..........q..", "..qqqqqqqqqqqq..", "..q..........q..",
         "..q..........q..", "...q........q...", "...q........q...", "...q........q...", "....q......q....", "....q......q....",
         ".....qqqqqq.....", "................", "................", "................"},
        kPalette
    );
    static PixelArt const tankOutput = art(
        {"................", "................", "...qqqqqqqqqq...", "..q..........q..", "..q..........q..", "..q..........q..",
         "..q..........q..", "...q........q...", "...q........q...", "...qqqqqqqqqq...", "....q......q....", "....q......q....",
         ".....qqqqqq.....", "................", "................", "................"},
        kPalette
    );
    static PixelArt const batteryInput = art(
        {"................", "................", "................", "...qqq....qqq...", ".qqqqqqqqqqqqqq.", ".q............q.",
         ".q.....qq.....q.", ".q.....qq.....q.", ".q...qqqqqq...q.", ".q...qqqqqq...q.", ".q.....qq.....q.", ".q.....qq.....q.",
         ".q............q.", ".qqqqqqqqqqqqqq.", "................", "................"},
        kPalette
    );
    static PixelArt const batteryOutput = art(
        {"................", "................", "................", "...qqq....qqq...", ".qqqqqqqqqqqqqq.", ".q............q.",
         ".q............q.", ".q............q.", ".q...qqqqqq...q.", ".q...qqqqqq...q.", ".q............q.", ".q............q.",
         ".q............q.", ".qqqqqqqqqqqqqq.", "................", "................"},
        kPalette
    );
    switch (i) {
    case Icon::PumpInput:
        return pumpInput;
    case Icon::PumpOutput:
        return pumpOutput;
    case Icon::World:
        return world;
    case Icon::Hand:
        return hand;
    case Icon::Pipes:
        return pipes;
    case Icon::XpOutput:
        return xpOutput;
    case Icon::XpKeep:
        return xpKeep;
    case Icon::Mend:
        return mend;
    case Icon::TakeAll:
        return takeAll;
    case Icon::Take:
        return take;
    case Icon::Store:
        return store;
    case Icon::StoreAll:
        return storeAll;
    case Icon::TankInput:
        return tankInput;
    case Icon::TankOutput:
        return tankOutput;
    case Icon::BatteryInput:
        return batteryInput;
    case Icon::BatteryOutput:
        return batteryOutput;
    case Icon::Off:
        break;
    }
    static PixelArt const none;
    return none;
}

} // namespace

void fluid(Painter& p, float x, float y, float w, float h, std::uint32_t argb) {
    if (w <= 0 || h <= 0) return;
    Color const shades[3] = {shade(argb, 0.84f), shade(argb, 1.f), shade(argb, 1.16f)};
    auto const& pat       = pattern();
    // Tiles from the top-left of the column, like the vanilla atlas sprite tiling.
    for (int row = 0; row < static_cast<int>(h); ++row) {
        auto const& runs = pat[static_cast<std::size_t>(row % 16)];
        for (float tx = 0; tx < w; tx += 16) {
            for (auto const& r : runs) {
                float rx = tx + static_cast<float>(r.x);
                if (rx >= w) break;
                float rw = std::min(static_cast<float>(r.w), w - rx);
                p.fillRect({x + rx, y + static_cast<float>(row), rw, 1}, shades[r.shade]);
            }
        }
    }
}

void barBackground(Painter& p, float x, float y, float h) {
    using namespace sb::color;
    p.fillRect({x, y, 18, h}, SLOT);
    p.fillRect({x, y, 17, 1}, SLOT_DARK);
    p.fillRect({x, y, 1, h - 1}, SLOT_DARK);
    p.fillRect({x + 1, y + h - 1, 17, 1}, PANEL_LITE);
    p.fillRect({x + 17, y + 1, 1, h - 1}, PANEL_LITE);
}

void tankOverlay(Painter& p, float x, float y, float h) {
    // Level marks on both sides of the glass: long every 9 units, short every 3.
    auto mark = Color::rgb(0x3C3C3C);
    int  rows = static_cast<int>(h) / 18;
    for (int r = 0; r < rows; ++r) {
        for (int i = 0; i < 6; ++i) {
            float my  = y + static_cast<float>(r * 18 + i * 3 + 1);
            float len = i % 3 == 0 ? 4.f : 2.f;
            p.fillRect({x, my, len, 1}, mark);
            p.fillRect({x + 16 - len, my, len, 1}, mark);
        }
    }
}

void batteryCells(Painter& p, float x, float y, float h) {
    int cells = static_cast<int>(h) / 6;
    for (int c = 0; c < cells; ++c) {
        float cy = y + static_cast<float>(c * 6);
        p.fillRect({x, cy, 16, 6}, Color::rgb(0x161616));
        p.fillRect({x + 1, cy + 1, 14, 1}, Color::rgb(0x2C2C2C));
        p.fillRect({x + 1, cy + 2, 14, 3}, Color::rgb(0x202020));
    }
}

void chargeSegment(Painter& p, float x, float y, std::uint32_t rgb) {
    p.fillRect({x + 1, y + 1, 14, 1}, shade(rgb, 1.35f));
    p.fillRect({x + 1, y + 2, 14, 3}, shade(rgb, 1.f));
    p.fillRect({x + 1, y + 5, 14, 1}, shade(rgb, 0.7f));
}

void batteryConnectionTop(Painter& p, float x, float y) {
    p.fillRect({x, y, 16, 4}, Color::rgb(0x3A3A3A));
    p.fillRect({x, y + 3, 16, 1}, Color::rgb(0x262626));
    for (float tx : {x + 1, x + 12}) {
        p.fillRect({tx, y, 3, 3}, Color::rgb(0xD9921C));
        p.fillRect({tx, y, 3, 1}, Color::rgb(0xF4C24A));
    }
}

void batteryConnectionBottom(Painter& p, float x, float y) {
    p.fillRect({x, y, 16, 4}, Color::rgb(0x3A3A3A));
    p.fillRect({x, y, 16, 1}, Color::rgb(0x262626));
    p.fillRect({x + 1, y + 1, 14, 1}, Color::rgb(0x505050));
}

void noSign(Painter& p, float x, float y) {
    auto ring  = Color::rgb(0xB01E1E);
    auto edge  = Color::rgb(0x5A0C0C);
    for (int py = 0; py < 16; ++py) {
        for (int px = 0; px < 16; ++px) {
            float dx = static_cast<float>(px) - 7.5f, dy = static_cast<float>(py) - 7.5f;
            float d  = std::sqrt(dx * dx + dy * dy);
            bool  slash = d < 6.5f && std::abs((px - 7.5f) + (py - 7.5f)) <= 1.0f;
            if (d >= 7.5f && d < 8.3f) p.fillRect({x + px, y + py, 1, 1}, edge);
            else if ((d >= 5.6f && d < 7.5f) || slash) p.fillRect({x + px, y + py, 1, 1}, ring);
        }
    }
}

void icon(Painter& p, Icon i, float x, float y, float alpha) {
    if (i == Icon::Off) {
        noSign(p, x, y);
        return;
    }
    p.pixels(iconArt(i), x, y, 1, Painter::Layer::Background, alpha);
}

void arrowDown(Painter& p, float x, float y) {
    auto c = Color::rgb(0x8B8B8B);
    p.fillRect({x + 5, y, 5, 3}, c);
    float rows[5][2] = {{0, 15}, {1, 13}, {3, 9}, {5, 5}, {7, 1}};
    for (int r = 0; r < 5; ++r) p.fillRect({x + rows[r][0], y + 3 + static_cast<float>(r), rows[r][1], 1}, c);
}

std::string grouped(long long n) {
    auto        s     = std::to_string(n < 0 ? -n : n);
    int         first = static_cast<int>(s.size()) % 3;
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (i != 0 && (static_cast<int>(i) - first) % 3 == 0) out += ',';
        out += s[i];
    }
    return n < 0 ? "-" + out : out;
}

// ---- MenuSlot ---------------------------------------------------------------------------------------

void readMenuSlots(nlohmann::json const& tab, std::vector<ItemStack>& stacks, std::vector<long long>& counts) {
    stacks.clear();
    counts.clear();
    auto menu = tab.find("menu");
    if (menu == tab.end() || !menu->is_object()) return;
    auto const& slots = menu->value("slots", nlohmann::json::array());
    for (auto const& s : slots) {
        core::ItemStackData d;
        try {
            if (!s.is_null()) d = core::ItemStackData::fromJson(s);
        } catch (...) {
        }
        bool filler = d.id == core::screen::kFillerId;
        counts.push_back(filler || d.empty() ? 0 : d.count);
        stacks.push_back(filler || d.empty() ? ItemStack{} : game::ItemConv::toStack(d, 1));
    }
}

void MenuSlot::render(Painter& p, float mx, float my, double) {
    if (!visible) return;
    float x = rect.x + 1, y = rect.y + 1;
    sb::slotFrame(p, x, y);
    auto const* s = stacks && index < static_cast<int>(stacks->size()) ? &(*stacks)[static_cast<std::size_t>(index)] : nullptr;
    if (s && !s->isNull()) {
        p.item(*s, x, y);
        long long n = counts && index < static_cast<int>(counts->size()) ? (*counts)[static_cast<std::size_t>(index)] : 1;
        if (n > 1) {
            auto t = std::to_string(n);
            p.text(t, x + 17 - p.textWidth(t), y + 9, Color::rgb(0xFFFFFF), true);
        }
    } else if (drawEmptyIcon) {
        icon(p, emptyIcon, x, y);
    }
    if (hovered(mx, my)) p.fillRect({x, y, 16, 16}, sb::color::HOVER, Painter::Layer::Foreground);
}

bool MenuSlot::mouseDown(MouseButton button, float x, float y) {
    if (!hovered(x, y)) return false;
    if (send) send(nlohmann::json{{"menuClick", index}, {"button", button == MouseButton::Right ? 1 : 0}, {"shift", input::shiftDown()}});
    return true;
}

void MenuSlot::tooltip(float x, float y, std::vector<TooltipLine>& out) const {
    if (!hovered(x, y)) return;
    auto const* s = stacks && index < static_cast<int>(stacks->size()) ? &(*stacks)[static_cast<std::size_t>(index)] : nullptr;
    if (s && !s->isNull()) {
        auto lines = itemTooltipLines(*s);
        out.insert(out.end(), lines.begin(), lines.end());
    } else if (!emptyTooltip.empty()) {
        out.push_back({emptyTooltip, 0xFFFFFF});
    }
}

} // namespace abp::client::ui::fluidgfx

#endif // ABP_CLIENT
