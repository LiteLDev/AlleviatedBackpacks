#ifdef ABP_CLIENT

#include "client/ui/settings/SettingsIcons.h"

namespace abp::client::ui::settings {

namespace {
using L = Painter::Layer;

// A picture frame with a gem in it: "show this item".
PixelArt const kItemDisplay{
    {
     "................",
     ".FFFFFFFFFFFFFF.",
     ".FWWWWWWWWWWWWF.",
     ".FWBBBBBBBBBBWF.",
     ".FWBBBBCCBBBBWF.",
     ".FWBBBCAACBBBWF.",
     ".FWBBCAAAACBBWF.",
     ".FWBCAAWAAACBWF.",
     ".FWBBCAAAACBBWF.",
     ".FWBBBCAACBBBWF.",
     ".FWBBBBCCBBBBWF.",
     ".FWBBBBBBBBBBWF.",
     ".FWWWWWWWWWWWWF.",
     ".FFFFFFFFFFFFFF.",
     "......FFFF......",
     ".....FFFFFF.....",
     },
    {"F=6B4423", "W=A0703C", "B=2B2B33", "C=8B1A1A", "A=E85050"},
};
// Circular arrow (clockwise).
PixelArt const kRotate{
    {
     "................",
     ".....XXXXX......",
     "...XX.....XX....",
     "..X.........X...",
     "..X.......XXXXX.",
     ".X.........XXX..",
     ".X..........X...",
     ".X..............",
     ".X..............",
     ".X..............",
     "..X.........X...",
     "..X........X....",
     "...XX....XX.....",
     ".....XXXX.......",
     "................",
     "................",
     },
    {"X=303030"},
};
// Arrows in and out of a block face: depth.
PixelArt const kDepth{
    {
     "................",
     ".......XX.......",
     "......XXXX......",
     ".....XXXXXX.....",
     ".......XX.......",
     "..GGGGGXXGGGGG..",
     "..G.....XX...G..",
     "..G..........G..",
     "..G..........G..",
     "..G.....XX...G..",
     "..GGGGGXXGGGGG..",
     ".......XX.......",
     ".....XXXXXX.....",
     "......XXXX......",
     ".......XX.......",
     "................",
     },
    {"X=303030", "G=707070"},
};
// Arrow down into a tray: save.
PixelArt const kSave{
    {
     "................",
     ".......GG.......",
     ".......GG.......",
     ".......GG.......",
     ".......GG.......",
     "....GGGGGGGG....",
     ".....GGGGGG.....",
     "......GGGG......",
     ".......GG.......",
     "..D..........D..",
     "..D..........D..",
     "..DDDDDDDDDDDD..",
     "..DLLLLLLLLLLD..",
     "..DDDDDDDDDDDD..",
     "................",
     "................",
     },
    {"G=2E9E2E", "D=404040", "L=909090"},
};
// Arrow up out of a tray: load.
PixelArt const kLoad{
    {
     "................",
     ".......BB.......",
     "......BBBB......",
     ".....BBBBBB.....",
     "....BBBBBBBB....",
     ".......BB.......",
     ".......BB.......",
     ".......BB.......",
     ".......BB.......",
     "..D..........D..",
     "..D..........D..",
     "..DDDDDDDDDDDD..",
     "..DLLLLLLLLLLD..",
     "..DDDDDDDDDDDD..",
     "................",
     "................",
     },
    {"B=2E5EC8", "D=404040", "L=909090"},
};
// A sheet of paper with an arrow out to the right: export.
PixelArt const kExport{
    {
     "................",
     "..PPPPPPP.......",
     "..PWWWWWPP......",
     "..PWWWWWPWP.....",
     "..PWLLLWPPPP....",
     "..PWWWWWWWWP....",
     "..PWLLLLWWWP.O..",
     "..PWWWWWWWOOOOO.",
     "..PWLLLLWWWP.O..",
     "..PWWWWWWWWP....",
     "..PWLLLWWWWP....",
     "..PWWWWWWWWP....",
     "..PPPPPPPPPP....",
     "................",
     "................",
     "................",
     },
    {"P=505050", "W=F0F0F0", "L=A0A0A0", "O=E08A1E"},
};
// A player head next to an open backpack flap.
PixelArt const kAnotherPlayer{
    {
     "................",
     "..HHHH..........",
     "..HSSH...DDDDD..",
     "..SESE..DBBBBBD.",
     "..SSSS..DBLLLBD.",
     "...SS...DBBBBBD.",
     "..CCCC..DBBBBBD.",
     ".CCCCCC.DBBBBBD.",
     ".CCCCCC.DBBBBBD.",
     ".S.CC.S.DBBBBBD.",
     "...LL...DDDDDDD.",
     "...LL...........",
     "...L.L..........",
     "................",
     "................",
     "................",
     },
    {"H=4A3020", "S=E0A87C", "E=303060", "C=2E8C9E", "L=3C3C8C", "D=3B2614", "B=8A5A2E"},
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
     "..........R...R.",
     "...........R.R..",
     "............R...",
     "...........R.R..",
     "..........R...R.",
     "................",
     "................",
     "................",
     },
    {"R=E02020"},
};
} // namespace

void icon(Painter& p, Icon which, float x, float y, float alpha) {
    switch (which) {
    case Icon::ItemDisplayTab:
        p.pixels(kItemDisplay, x, y, 1, L::Foreground, alpha);
        break;
    case Icon::Rotate:
        p.pixels(kRotate, x, y, 1, L::Foreground, alpha);
        break;
    case Icon::Depth:
        p.pixels(kDepth, x, y, 1, L::Foreground, alpha);
        break;
    case Icon::SaveTemplate:
        p.pixels(kSave, x, y, 1, L::Foreground, alpha);
        break;
    case Icon::LoadTemplate:
        p.pixels(kLoad, x, y, 1, L::Foreground, alpha);
        break;
    case Icon::ExportTemplate:
        p.pixels(kExport, x, y, 1, L::Foreground, alpha);
        break;
    case Icon::AnotherPlayerOn:
        p.pixels(kAnotherPlayer, x, y, 1, L::Foreground, alpha);
        break;
    case Icon::AnotherPlayerOff:
        p.pixels(kAnotherPlayer, x, y, 1, L::Foreground, alpha * 0.6f);
        p.pixels(kCross, x, y, 1, L::Foreground, alpha);
        break;
    }
}

void slotSelection(Painter& p, float x, float y) {
    // Two unit wide bright frame with dark inner edge, open in the middle of each side like SC's.
    Color const light = Color::rgb(0xFFFFFF);
    Color const dark  = Color::rgb(0x3F3F3F);
    auto        side  = [&](float rx, float ry, float rw, float rh, Color c) { p.fillRect({rx, ry, rw, rh}, c, L::Foreground); };
    side(x, y, 24, 2, light);
    side(x, y + 22, 24, 2, light);
    side(x, y + 2, 2, 20, light);
    side(x + 22, y + 2, 2, 20, light);
    side(x + 2, y + 2, 20, 1, dark);
    side(x + 2, y + 21, 20, 1, dark);
    side(x + 2, y + 3, 1, 18, dark);
    side(x + 21, y + 3, 1, 18, dark);
}

void memoryOverlay(Painter& p, float x, float y) { p.fillRect({x, y, 16, 16}, Color::argb(0x808B8B8B), L::Foreground); }

} // namespace abp::client::ui::settings

#endif // ABP_CLIENT
