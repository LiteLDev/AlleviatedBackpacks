#ifdef ABP_CLIENT

#include "client/ui/widgets/SbStyle.h"

#include <vector>

namespace abp::client::ui::sb {

using L = Painter::Layer;

void panel(Painter& p, Rect const& r) {
    float x = r.x, y = r.y, w = r.w, h = r.h;
    p.fillRect({x + 1, y + 1, w - 2, h - 2}, color::PANEL);
    // Black outline with 2 unit rounded corners.
    p.fillRect({x + 2, y, w - 4, 1}, color::OUTLINE);
    p.fillRect({x + 2, y + h - 1, w - 4, 1}, color::OUTLINE);
    p.fillRect({x, y + 2, 1, h - 4}, color::OUTLINE);
    p.fillRect({x + w - 1, y + 2, 1, h - 4}, color::OUTLINE);
    p.fillRect({x + 1, y + 1, 1, 1}, color::OUTLINE);
    p.fillRect({x + w - 2, y + 1, 1, 1}, color::OUTLINE);
    p.fillRect({x + 1, y + h - 2, 1, 1}, color::OUTLINE);
    p.fillRect({x + w - 2, y + h - 2, 1, 1}, color::OUTLINE);
    // Bevel: white top-left, dark bottom-right.
    p.fillRect({x + 2, y + 1, w - 5, 2}, color::PANEL_LITE);
    p.fillRect({x + 1, y + 2, 2, h - 5}, color::PANEL_LITE);
    p.fillRect({x + 3, y + h - 3, w - 5, 2}, color::PANEL_DARK);
    p.fillRect({x + w - 3, y + 3, 2, h - 5}, color::PANEL_DARK);
}

void storageBackground(Painter& p, Rect const& r) { panel(p, r); }

void slotFrame(Painter& p, float x, float y) {
    p.fillRect({x - 1, y - 1, 18, 18}, color::SLOT);
    p.fillRect({x - 1, y - 1, 17, 1}, color::SLOT_DARK);
    p.fillRect({x - 1, y - 1, 1, 17}, color::SLOT_DARK);
    p.fillRect({x, y + 16, 17, 1}, color::PANEL_LITE);
    p.fillRect({x + 16, y, 1, 17}, color::PANEL_LITE);
}

void slotsBackground(Painter& p, float x, float y, int columns, int rows, int extra) {
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < columns; ++c) slotFrame(p, x + 1 + static_cast<float>(c) * 18, y + 1 + static_cast<float>(r) * 18);
    }
    for (int c = 0; c < extra; ++c) slotFrame(p, x + 1 + static_cast<float>(c) * 18, y + 1 + static_cast<float>(rows) * 18);
}

void upgradeColumn(Painter& p, float x, float y, int slots, std::vector<bool> const& switches) {
    if (slots <= 0) return;
    float h = static_cast<float>(6 + slots * 16 + 6);
    float w = 26;
    // Open to the right (it overlaps the storage background's left border).
    p.fillRect({x + 1, y + 1, w - 1, h - 2}, color::PANEL);
    p.fillRect({x + 2, y, w - 2, 1}, color::OUTLINE);
    p.fillRect({x + 2, y + h - 1, w - 2, 1}, color::OUTLINE);
    p.fillRect({x, y + 2, 1, h - 4}, color::OUTLINE);
    p.fillRect({x + 1, y + 1, 1, 1}, color::OUTLINE);
    p.fillRect({x + 1, y + h - 2, 1, 1}, color::OUTLINE);
    p.fillRect({x + 2, y + 1, w - 2, 2}, color::PANEL_LITE);
    p.fillRect({x + 1, y + 2, 2, h - 5}, color::PANEL_LITE);
    p.fillRect({x + 3, y + h - 3, w - 3, 2}, color::PANEL_DARK);
    // Switch notches (7 wide, sticking out 4 units to the left).
    bool previous = false;
    for (int i = 0; i < slots; ++i) {
        bool has = static_cast<std::size_t>(i) < switches.size() && switches[static_cast<std::size_t>(i)];
        if (has) {
            float ny = y + 5 + static_cast<float>(i) * 16 + (previous ? 1 : 0);
            float nh = 18 - (previous ? 1 : 0);
            p.fillRect({x - 4, ny, 6, nh}, color::PANEL);
            p.fillRect({x - 4, ny + (previous ? 0 : 1), 1, nh - (previous ? 1 : 2)}, color::OUTLINE);
            if (!previous) p.fillRect({x - 3, ny, 4, 1}, color::OUTLINE);
            p.fillRect({x - 3, ny + nh - 1, 4, 1}, color::OUTLINE);
            p.fillRect({x - 3, ny + (previous ? 0 : 1), 1, nh - (previous ? 1 : 2)}, color::PANEL_LITE);
            p.fillRect({x, ny + 1, 1, nh - 2}, color::PANEL);
        }
        previous = has;
    }
    for (int i = 0; i < slots; ++i) slotFrame(p, x + 6, y + 6 + static_cast<float>(i) * 16);
}

namespace {
void bevelButton(Painter& p, Rect const& r, bool hovered) {
    Color body  = hovered ? Color::rgb(0xA8A8D8) : Color::rgb(0x8B8B8B);
    Color light = hovered ? Color::rgb(0xFFFFFF) : Color::rgb(0xD8D8D8);
    Color dark  = hovered ? Color::rgb(0x40407A) : Color::rgb(0x373737);
    p.fillRect(r, body);
    p.fillRect({r.x, r.y, r.w - 1, 1}, light);
    p.fillRect({r.x, r.y, 1, r.h - 1}, light);
    p.fillRect({r.x + 1, r.bottom() - 1, r.w - 1, 1}, dark);
    p.fillRect({r.right() - 1, r.y + 1, 1, r.h - 1}, dark);
}
} // namespace

void buttonBackground(Painter& p, Rect const& r, bool hovered) { bevelButton(p, r, hovered); }

void smallButtonBackground(Painter& p, Rect const& r, bool hovered) { bevelButton(p, r, hovered); }

void upgradeSwitch(Painter& p, float x, float y, int state, bool hovered) {
    p.fillRect({x, y, 6, 12}, hovered ? Color::rgb(0x6F6F6F) : color::SLOT_DARK);
    p.fillRect({x, y, 6, 1}, hovered ? Color::rgb(0xFFFFFF) : Color::rgb(0x1E1E1E));
    switch (state) {
    case 1:
        switchPip(p, x + 1, y + 1, true);
        break;
    case 0:
        switchPip(p, x + 1, y + 1, false);
        break;
    default:
        p.fillRect({x + 1, y + 1, 4, 10}, Color::rgb(0x8B8B8B));
        p.fillRect({x + 1, y + 1, 4, 1}, Color::rgb(0xB5B5B5));
        break;
    }
}

void switchPip(Painter& p, float x, float y, bool on) {
    Color body = on ? Color::rgb(0x3BBF3B) : Color::rgb(0xC83232);
    Color lite = on ? Color::rgb(0x8CF08C) : Color::rgb(0xF07A7A);
    Color dark = on ? Color::rgb(0x1F6E1F) : Color::rgb(0x6E1F1F);
    p.fillRect({x, y, 4, 10}, body);
    p.fillRect({x, y, 4, 1}, lite);
    p.fillRect({x, y + 9, 4, 1}, dark);
    // ON lights the top half, OFF the bottom half (like a lever).
    if (on) p.fillRect({x + 1, y + 2, 2, 3}, lite);
    else p.fillRect({x + 1, y + 5, 2, 3}, lite);
}

void tabBackground(Painter& p, Rect const& r) {
    float x = r.x, y = r.y, w = r.w, h = r.h;
    panel(p, {x - 3, y, w + 3, h});
    // The left side is open (the strip continues under the storage background).
    p.fillRect({x - 3, y, 3, 1}, color::OUTLINE);
    p.fillRect({x - 3, y + 1, 3, 2}, color::PANEL_LITE);
    p.fillRect({x - 3, y + 3, 3, h - 6}, color::PANEL);
    p.fillRect({x - 3, y + h - 3, 3, 2}, color::PANEL_DARK);
    p.fillRect({x - 3, y + h - 1, 3, 1}, color::OUTLINE);
}

void controlBackground(Painter& p, Rect const& r) {
    p.fillRect(r, color::SLOT);
    p.fillRect({r.x, r.y, r.w - 1, 1}, color::SLOT_DARK);
    p.fillRect({r.x, r.y, 1, r.h - 1}, color::SLOT_DARK);
    p.fillRect({r.x + 1, r.bottom() - 1, r.w - 1, 1}, color::PANEL_LITE);
    p.fillRect({r.right() - 1, r.y + 1, 1, r.h - 1}, color::PANEL_LITE);
}

void scrollBar(Painter& p, Rect const& track, Rect const& thumb) {
    p.fillRect(track, Color::argb(0xFF000000));
    p.fillRect(thumb, Color::argb(0xFF808080));
    p.fillRect({thumb.x, thumb.y, thumb.w - 1, thumb.h - 1}, Color::argb(0xFFC0C0C0));
}

void tooltipBox(Painter& p, Rect const& b, Color bg, Color top, Color bottom) {
    auto fg = L::Foreground;
    p.fillRect({b.x - 3, b.y - 4, b.w + 6, 1}, bg, fg);
    p.fillRect({b.x - 3, b.bottom() + 3, b.w + 6, 1}, bg, fg);
    p.fillRect({b.x - 3, b.y - 3, b.w + 6, b.h + 6}, bg, fg);
    p.fillRect({b.x - 4, b.y - 3, 1, b.h + 6}, bg, fg);
    p.fillRect({b.right() + 3, b.y - 3, 1, b.h + 6}, bg, fg);
    p.fillRect({b.x - 3, b.y - 3, b.w + 6, 1}, top, fg);
    p.fillRect({b.x - 3, b.bottom() + 2, b.w + 6, 1}, bottom, fg);
    float sideTop = b.y - 2, sideH = b.h + 4;
    int   steps   = std::max(1, static_cast<int>(sideH / 2));
    for (int i = 0; i < steps; ++i) {
        float y0 = sideTop + sideH * static_cast<float>(i) / static_cast<float>(steps);
        float y1 = sideTop + sideH * static_cast<float>(i + 1) / static_cast<float>(steps);
        float t  = (static_cast<float>(i) + 0.5f) / static_cast<float>(steps);
        Color c{top.r + (bottom.r - top.r) * t, top.g + (bottom.g - top.g) * t, top.b + (bottom.b - top.b) * t, top.a + (bottom.a - top.a) * t};
        p.fillRect({b.x - 3, y0, 1, y1 - y0}, c, fg);
        p.fillRect({b.right() + 2, y0, 1, y1 - y0}, c, fg);
    }
}

// ---- icons ----------------------------------------------------------------------------------------

namespace {
PixelArt const kArrowUp{
    {
     "............",
     ".....GG.....",
     "....GLLG....",
     "...GLLLLG...",
     "..GLLLLLLG..",
     ".GGGLLLLGGG.",
     "...GLLLLG...",
     "...GLLLLG...",
     "...GLLLLG...",
     "...GLLLLG...",
     "...GGGGGG...",
     "............",
     },
    {"G=1E6B1E", "L=4CD14C"},
};
PixelArt const kArrowDown{
    {
     "............",
     "...RRRRRR...",
     "...RLLLLR...",
     "...RLLLLR...",
     "...RLLLLR...",
     "...RLLLLR...",
     ".RRRLLLLRRR.",
     "..RLLLLLLR..",
     "...RLLLLR...",
     "....RLLR....",
     ".....RR.....",
     "............",
     },
    {"R=7A1A1A", "L=E04545"},
};
PixelArt const kArrowUpFiltered{
    {
     "............",
     "....GG......",
     "...GLLG.....",
     "..GLLLLG....",
     ".GGGLLGGG...",
     "...GLLG.....",
     "...GLLG.....",
     "...GGGG.....",
     "......FFFFF.",
     ".......FFF..",
     "........F...",
     "............",
     },
    {"G=1E6B1E", "L=4CD14C", "F=4A4A4A"},
};
PixelArt const kArrowDownFiltered{
    {
     "............",
     "...RRRR.....",
     "...RLLR.....",
     "...RLLR.....",
     ".RRRLLRRR...",
     "..RLLLLR....",
     "...RLLR.....",
     "....RR......",
     "......FFFFF.",
     ".......FFF..",
     "........F...",
     "............",
     },
    {"R=7A1A1A", "L=E04545", "F=4A4A4A"},
};
PixelArt const kLetterA{
    {
     "............",
     "............",
     ".....XX.....",
     "....X..X....",
     "...X....X...",
     "...X....X...",
     "...XXXXXX...",
     "...X....X...",
     "...X....X...",
     "...X....X...",
     "............",
     "............",
     },
    {"X=2B2B2B"},
};
PixelArt const kLetterM{
    {
     "............",
     "............",
     "..X......X..",
     "..XX....XX..",
     "..X.X..X.X..",
     "..X..XX..X..",
     "..X......X..",
     "..X......X..",
     "..X......X..",
     "..X......X..",
     "............",
     "............",
     },
    {"X=2B2B2B"},
};
PixelArt const kDigit1{
    {
     "............",
     "............",
     ".....XX.....",
     "....XXX.....",
     "...X.XX.....",
     ".....XX.....",
     ".....XX.....",
     ".....XX.....",
     ".....XX.....",
     "...XXXXXX...",
     "............",
     "............",
     },
    {"X=2B2B2B"},
};
PixelArt const kLetterT{
    {
     "............",
     "............",
     "..XXXXXXXX..",
     ".....XX.....",
     ".....XX.....",
     ".....XX.....",
     ".....XX.....",
     ".....XX.....",
     ".....XX.....",
     ".....XX.....",
     "............",
     "............",
     },
    {"X=2B2B2B"},
};
PixelArt const kGear{
    {
     "................",
     ".......DD.......",
     "...DD.DLLD.DD...",
     "..DLLDDLLDDLLD..",
     "..DLLLLLLLLLLD..",
     "...DLLLLLLLLD...",
     ".DDDLLLDDLLLDDD.",
     "DLLLLLD..DLLLLLD",
     "DLLLLLD..DLLLLLD",
     ".DDDLLLDDLLLDDD.",
     "...DLLLLLLLLD...",
     "..DLLLLLLLLLLD..",
     "..DLLDDLLDDLLD..",
     "...DD.DLLD.DD...",
     ".......DD.......",
     "................",
     },
    {"D=373737", "L=9A9A9A"},
};
PixelArt const kSearch{
    {
     ".XXX...",
     "X...X..",
     "X...X..",
     "X...X..",
     ".XXXX..",
     "....XX.",
     ".....XX",
     },
    {"X=BBBBBB"},
};
PixelArt const kBackArrow{
    {
     "................",
     "................",
     "......GG........",
     ".....GLG........",
     "....GLLGGGGGG...",
     "...GLLLLLLLLLG..",
     "..GLLLLLLLLLLLG.",
     "...GLLLLLLLLLLG.",
     "....GLLGGGGGLLG.",
     ".....GLG....GLG.",
     "......GG....GLG.",
     "............GLG.",
     "............GGG.",
     "................",
     "................",
     "................",
     },
    {"G=1E6B1E", "L=4CD14C"},
};
PixelArt const kBackpackIcon{
    {
     "................",
     "......DDDD......",
     ".....D....D.....",
     "...DDDDDDDDDD...",
     "..DBBBBBBBBBBD..",
     "..DBBBBBBBBBBD..",
     "..DBBDDDDDDBBD..",
     "..DBBDLLLLDBBD..",
     "..DBBDLGGLDBBD..",
     "..DBBDLLLLDBBD..",
     "..DBBDDDDDDBBD..",
     "..DBBBBBBBBBBD..",
     "..DBBBBBBBBBBD..",
     "...DDDDDDDDDD...",
     "................",
     "................",
     },
    {"D=3B2614", "B=8A5A2E", "L=B5824E", "G=E0C060"},
};
// Original art: corner brackets and side ticks marking an empty upgrade slot (opaque, like the
// slot icon SC sets as the upgrade slots' noItemIcon).
PixelArt const kEmptyUpgradeSlot{
    {
     "................",
     ".OOO...OO...OOO.",
     ".O............O.",
     ".O............O.",
     "................",
     "................",
     "................",
     ".O............O.",
     ".O............O.",
     "................",
     "................",
     "................",
     ".O............O.",
     ".O............O.",
     ".OOO...OO...OOO.",
     "................",
     },
    {"O=262626"},
};
// Original art: two interlocked chain links (linked storage); the main endpoint adds a gold star.
#define ABP_LINK_ROWS                                                                                                  \
    ".DDDDDDDD.......", ".DLLLLLLD.......", ".DLDDDDLD.......", ".DLD..DDDDDDDDD.", ".DLD..DDLLLLLLD.",               \
        ".DLDDDDDLDDDDLD.", ".DLLLLLDL....LD.", ".DDDDDDDL....LD.", ".......DLDDDDLD.", ".......DLLLLLLD.",           \
        ".......DDDDDDDD.", "................", "................"
PixelArt const kLinkedSecondary{
    {"................", "................", "................", ABP_LINK_ROWS},
    {"D=2B1B33", "L=AE8BC7"},
};
PixelArt const kLinkedPrimary{
    {"............Y...", "...........YYY..", "............Y...", ABP_LINK_ROWS},
    {"D=2B1B33", "L=AE8BC7", "Y=F2C94C"},
};
#undef ABP_LINK_ROWS
PixelArt const kMemoryIcon{
    {
     "................",
     ".DDDDDDDDDDDDDD.",
     ".DSSSSSSSSSSSSD.",
     ".DSSSSSCCSSSSSD.",
     ".DSSSSCAACSSSSD.",
     ".DSSSCAAAACSSSD.",
     ".DSSCAAWAAACSSD.",
     ".DSCAAAAAAAACSD.",
     ".DSSCAAAAAACSSD.",
     ".DSSSCAAAACSSSD.",
     ".DSSSSCAACSSSSD.",
     ".DSSSSSCCSSSSSD.",
     ".DSSSSSSSSSSSSD.",
     ".DSSSSSSSSSSSSD.",
     ".DDDDDDDDDDDDDD.",
     "................",
     },
    {"D=373737", "S=8B8B8B", "C=2A8C8C", "A=7FE8E8", "W=FFFFFF"},
};
PixelArt const kNoSortIcon{
    {
     "................",
     ".XX..........XX.",
     ".XXX........XXX.",
     "..XXX.BBBB.XXX..",
     "...XXX....XXX...",
     "....XXXBBXXX....",
     ".....XXXXXX.....",
     "......XXXX......",
     "......XXXX......",
     ".....XXXXXX.....",
     "....XXXBBXXX....",
     "...XXX....XXX...",
     "..XXX.BBBB.XXX..",
     ".XXX........XXX.",
     ".XX..........XX.",
     "................",
     },
    {"X=C83232", "B=404040"},
};
PixelArt const kSelectAll{
    {
     "................",
     ".GGGG.GGGG.GGGG.",
     ".GLLG.GLLG.GLLG.",
     ".GLLG.GLLG.GLLG.",
     ".GGGG.GGGG.GGGG.",
     "................",
     ".GGGG.GGGG.GGGG.",
     ".GLLG.GLLG.GLLG.",
     ".GLLG.GLLG.GLLG.",
     ".GGGG.GGGG.GGGG.",
     "................",
     ".GGGG.GGGG.GGGG.",
     ".GLLG.GLLG.GLLG.",
     ".GLLG.GLLG.GLLG.",
     ".GGGG.GGGG.GGGG.",
     "................",
     },
    {"G=1E6B1E", "L=4CD14C"},
};
PixelArt const kUnselectAll{
    {
     "................",
     ".RDDD.DDDD.DDDR.",
     ".DRSD.DSSD.DSRD.",
     ".DSRD.DSSD.DRSD.",
     ".DDDR.DDDD.RDDD.",
     "......R..R......",
     ".DDDD.DRRD.DDDD.",
     ".DSSD.DRRD.DSSD.",
     ".DSSD.DRRD.DSSD.",
     ".DDDD.DRRD.DDDD.",
     "......R..R......",
     ".DDDR.DDDD.RDDD.",
     ".DSRD.DSSD.DRSD.",
     ".DRSD.DSSD.DSRD.",
     ".RDDD.DDDD.DDDR.",
     "................",
     },
    {"D=4A4A4A", "S=9A9A9A", "R=C83232"},
};
PixelArt const kNbt{
    {
     "................",
     "................",
     "................",
     "................",
     "X..X.XXX..XXXXX.",
     "XX.X.X..X...X...",
     "X.XX.XXX....X...",
     "X..X.X..X...X...",
     "X..X.XXX....X...",
     "................",
     "................",
     "................",
     "................",
     "................",
     "................",
     "................",
     },
    {"X=1E6B1E"},
};
PixelArt const kNbtStruck{
    {
     "................",
     "................",
     "................",
     "................",
     "X..X.XXX..XXXXX.",
     "XX.X.X..X...X...",
     "RRRRRRRRRRRRRRRR",
     "X..X.X..X...X...",
     "X..X.XXX....X...",
     "................",
     "................",
     "................",
     "................",
     "................",
     "................",
     "................",
     },
    {"X=4A4A4A", "R=C83232"},
};
PixelArt const kTabArrowOn{
    {
     "................",
     "..........DDDD..",
     "..........DTTD..",
     "..........DTTD..",
     "....G.....DTTD..",
     "....GG....DTTD..",
     ".GGGGLG...DTTD..",
     ".GLLLLLG..DTTD..",
     ".GLLLLLLG.DTTD..",
     ".GLLLLLG..DTTD..",
     ".GGGGLG...DTTD..",
     "....GG....DTTD..",
     "....G.....DTTD..",
     "..........DTTD..",
     "..........DDDD..",
     "................",
     },
    {"G=1E6B1E", "L=4CD14C", "D=373737", "T=C6C6C6"},
};
PixelArt const kTabArrowOff{
    {
     "................",
     "..........DDDD..",
     "..........DTTD..",
     "..........DTTD..",
     ".....G....DTTD..",
     ".....GG...DTTD..",
     "..GGGGLG..DTTD..",
     "..GLLLLLG.DTTD..",
     "..GGGGLG..DTTD..",
     ".....GG...DTTD..",
     ".....G....DTTD..",
     "..DDDDDDDDDDDD..",
     "..DSSSSSSSSSSD..",
     "..DSSSSSSSSSSD..",
     "..DDDDDDDDDDDD..",
     "................",
     },
    {"G=505050", "L=9A9A9A", "D=373737", "T=C6C6C6", "S=8B8B8B"},
};
PixelArt const kKeepTab{
    {
     "................",
     "..DDDDDD........",
     "..DTTTTD........",
     "..DTTTTDDDDDDD..",
     "..DTTTTTTTTTTD..",
     "..DTTTTTTTTTTD..",
     "..DTTTTTTTTTTD..",
     "..DTTTTTTTTTTD..",
     "..DTTTTKKKKTTD..",
     "..DTTTTKTTKTTD..",
     "..DTTTKKKKKKTD..",
     "..DTTTKKKKKKTD..",
     "..DTTTKKKKKKTD..",
     "..DDDDDDDDDDDD..",
     "................",
     "................",
     },
    {"D=373737", "T=C6C6C6", "K=1E6B1E"},
};
PixelArt const kKeepTabOff{
    {
     "................",
     "..DDDDDD........",
     "..DTTTTD........",
     "..DTTTTDDDDDDD..",
     "..DTTTTTTTTTTD..",
     "..DTTTTTTTTTTD..",
     "..DTTTTTTTTTTD..",
     "..DTTTTTTTTTTD..",
     "..DTTTTKKKKTTD..",
     "..DTTTTKTTTTTD..",
     "..DTTTKKKKKKTD..",
     "..DTTTKKKKKKTD..",
     "..DTTTKKKKKKTD..",
     "..DDDDDDDDDDDD..",
     "................",
     "................",
     },
    {"D=373737", "T=C6C6C6", "K=707070"},
};
PixelArt const kKeepSearch{
    {
     "................",
     "...XXXX.........",
     "..X....X........",
     ".X......X.......",
     ".X......X.......",
     ".X......X.......",
     ".X......X.......",
     "..X....X........",
     "...XXXXXX.......",
     "........XX......",
     ".........XX.GG..",
     "..........XGLLG.",
     "...........GLLG.",
     "............GG..",
     "................",
     "................",
     },
    {"X=373737", "G=1E6B1E", "L=4CD14C"},
};
PixelArt const kKeepSearchOff{
    {
     "................",
     "...XXXX.........",
     "..X....X........",
     ".X......X.......",
     ".X......X.......",
     ".X......X.......",
     ".X......X.......",
     "..X....X........",
     "...XXXXXX.......",
     "........XX......",
     ".........XX.....",
     "..........XX....",
     "...........XX...",
     "................",
     "................",
     "................",
     },
    {"X=707070"},
};
} // namespace

void colorSquare(Painter& p, float x, float y, std::uint32_t rgb) {
    p.fillRect({x + 3, y + 3, 12, 12}, Color::rgb(rgb, 200 / 255.f), L::Foreground);
}
void icon(Painter& p, Icon which, float x, float y, float alpha) {
    switch (which) {
    case Icon::TransferToStorage:
        p.pixels(kArrowUp, x, y, 1, L::Foreground, alpha);
        break;
    case Icon::TransferToStorageFiltered:
        p.pixels(kArrowUpFiltered, x, y, 1, L::Foreground, alpha);
        break;
    case Icon::TransferToInventory:
        p.pixels(kArrowDown, x, y, 1, L::Foreground, alpha);
        break;
    case Icon::TransferToInventoryFiltered:
        p.pixels(kArrowDownFiltered, x, y, 1, L::Foreground, alpha);
        break;
    case Icon::SortByName:
        p.pixels(kLetterA, x, y, 1, L::Foreground, alpha);
        break;
    case Icon::SortByMod:
        p.pixels(kLetterM, x, y, 1, L::Foreground, alpha);
        break;
    case Icon::SortByCount:
        p.pixels(kDigit1, x, y, 1, L::Foreground, alpha);
        break;
    case Icon::SortByTags:
        p.pixels(kLetterT, x, y, 1, L::Foreground, alpha);
        break;
    case Icon::Settings:
        p.pixels(kGear, x, y, 1, L::Foreground, alpha);
        break;
    case Icon::Search:
        p.pixels(kSearch, x, y, 1, L::Foreground, alpha);
        break;
    case Icon::Memory:
        p.fillRect({x, y, 16, 16}, Color::argb(0x808B8B8B), L::Foreground);
        break;
    case Icon::BackToBackpack:
        p.pixels(kBackArrow, x, y, 1, L::Foreground, alpha);
        break;
    case Icon::BackpackSettings:
        p.pixels(kBackpackIcon, x, y, 1, L::Foreground, alpha);
        break;
    case Icon::MemoryTab:
        p.pixels(kMemoryIcon, x, y, 1, L::Foreground, alpha);
        break;
    case Icon::NoSortTab:
        p.pixels(kNoSortIcon, x, y, 1, L::Foreground, alpha);
        break;
    case Icon::SelectAll:
        p.pixels(kSelectAll, x, y, 1, L::Foreground, alpha);
        break;
    case Icon::UnselectAll:
        p.pixels(kUnselectAll, x, y, 1, L::Foreground, alpha);
        break;
    case Icon::MatchNbt:
        p.pixels(kNbt, x, y, 1, L::Foreground, alpha);
        break;
    case Icon::IgnoreNbt:
        p.pixels(kNbtStruck, x, y, 1, L::Foreground, alpha);
        break;
    case Icon::ShiftClickTabOn:
        p.pixels(kTabArrowOn, x, y, 1, L::Foreground, alpha);
        break;
    case Icon::ShiftClickTabOff:
        p.pixels(kTabArrowOff, x, y, 1, L::Foreground, alpha);
        break;
    case Icon::KeepTabOpenOn:
        p.pixels(kKeepTab, x, y, 1, L::Foreground, alpha);
        break;
    case Icon::KeepTabOpenOff:
        p.pixels(kKeepTabOff, x, y, 1, L::Foreground, alpha);
        break;
    case Icon::KeepSearchOn:
        p.pixels(kKeepSearch, x, y, 1, L::Foreground, alpha);
        break;
    case Icon::KeepSearchOff:
        p.pixels(kKeepSearchOff, x, y, 1, L::Foreground, alpha);
        break;
    case Icon::EmptyUpgradeSlot:
        // Drawn like a slot background (under the hover highlight), not as a foreground icon.
        p.pixels(kEmptyUpgradeSlot, x, y, 1, L::Background, alpha);
        break;
    case Icon::LinkedPrimary:
        p.pixels(kLinkedPrimary, x, y, 1, L::Foreground, alpha);
        break;
    case Icon::LinkedSecondary:
        p.pixels(kLinkedSecondary, x, y, 1, L::Foreground, alpha);
        break;
    }
}

} // namespace abp::client::ui::sb

#endif // ABP_CLIENT
