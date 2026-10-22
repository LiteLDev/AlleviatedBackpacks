#ifdef ABP_CLIENT

// Original 16x16 pixel art for the filter control buttons (drawn for this mod; no textures from
// Sophisticated Core). Each icon sits at (x+1, y+1) of an 18x18 button like SC's button icons.

#include "client/ui/widgets/FilterControl.h"

namespace abp::client::ui::filter_icons {

namespace {
using L = Painter::Layer;

PixelArt const kAllow{
    {
     "................",
     "................",
     "..............G.",
     ".............GG.",
     "............GGD.",
     "...........GGD..",
     "..........GGD...",
     ".G.......GGD....",
     ".GG.....GGD.....",
     ".DGG...GGD......",
     "..DGG.GGD.......",
     "...DGGGD........",
     "....DGD.........",
     ".....D..........",
     "................",
     "................",
     },
    {"G=3FD23F", "D=1E7A1E"},
};

PixelArt const kBlock{
    {
     "................",
     ".....RRRRRR.....",
     "...RRRRRRRRRR...",
     "..RRRD....RRRR..",
     "..RR.......RRR..",
     ".RRR......RRDRR.",
     ".RR......RRD.RR.",
     ".RR.....RRD..RR.",
     ".RR....RRD...RR.",
     ".RR...RRD....RR.",
     ".RRDRRD.....RRR.",
     "..RRR.......RR..",
     "..RRRR....DRRR..",
     "...RRRRRRRRRR...",
     ".....RRRRRR.....",
     "................",
     },
    {"R=D82020", "D=8A1010"},
};

PixelArt const kStorage{
    {
     "................",
     ".....BBBBBB.....",
     "....B......B....",
     "....B......B....",
     "..BBBBBBBBBBBB..",
     ".BLLLLLLLLLLLLB.",
     ".BLLLLLLLLLLLLB.",
     ".BLLLLDDDDLLLLB.",
     ".BLLLLDYYDLLLLB.",
     ".BBBBBBBBBBBBBB.",
     ".BLLLLLLLLLLLLB.",
     ".BLLLLLLLLLLLLB.",
     ".BLLLLLLLLLLLLB.",
     ".BDLLLLLLLLLLDB.",
     "..BBBBBBBBBBBB..",
     "................",
     },
    {"B=3B2412", "L=9A6332", "D=6B4320", "Y=E3C04A"},
};

PixelArt const kInventory{
    {
     "................",
     "................",
     ".OOOOOOOOOOOOOO.",
     ".OWWWWWWWWWWWWO.",
     ".OWWWWWWWWWWWWO.",
     ".OWWWWWWWWWWWWO.",
     ".OOOOOOGGOOOOOO.",
     ".OWWWWWGGWWWWWO.",
     ".OWWWWWWWWWWWWO.",
     ".OWWWWWWWWWWWWO.",
     ".OWWWWWWWWWWWWO.",
     ".OWWWWWWWWWWWWO.",
     ".ODWWWWWWWWWWDO.",
     ".OOOOOOOOOOOOOO.",
     "................",
     "................",
     },
    {"O=2E1D0C", "W=A8732F", "D=7A5020", "G=C8C8C8"},
};

PixelArt const kMatchItem{
    {
     "................",
     "................",
     "................",
     "................",
     "......OOOOOO....",
     ".....OWWWWWWO...",
     "....OWWLLLLWWO..",
     "...OWLLLLLLLDO..",
     "..OWLLLLLLLDO...",
     "..OLLLLLLLDDO...",
     "..ODDDDDDDDO....",
     "...OOOOOOOO.....",
     "................",
     "................",
     "................",
     "................",
     },
    {"O=3A3A3A", "W=FFFFFF", "L=D8D8D8", "D=9A9A9A"},
};

PixelArt const kMatchMod{
    {
     "................",
     "................",
     "................",
     "................",
     ".BB...BB........",
     ".BBB.BBB........",
     ".BBBBBBB..OOO...",
     ".BB.B.BB.O...O..",
     ".BB...BB.O...O..",
     ".BB...BB.O...O..",
     ".BB...BB..OOO...",
     "................",
     "..........DDD...",
     "..........D..D..",
     "..........DDD...",
     "................",
     },
    {"B=2B4FC0", "O=2B4FC0", "D=2B4FC0"},
};

PixelArt const kMatchTags{
    {
     "................",
     "................",
     "......YYYYYYYY..",
     ".....YLLLLLLLY..",
     "....YLLLLLLLLY..",
     "...YLLHHLLLLLY..",
     "..YLLLHHLLLLLY..",
     "..YLLLLLLLLLLY..",
     "..YLLLLLLLLLLY..",
     "...YLLLLLLLLLY..",
     "....YLLLLLLLLY..",
     ".....YLLLLLLLY..",
     "......YYYYYYYY..",
     "................",
     "................",
     "................",
     },
    {"Y=8A6A10", "L=F0D050", "H=3A2A05"},
};

PixelArt const kMatchDurability{
    {
     "................",
     "...WWWWWW.......",
     "..W......WW.....",
     ".........WSW....",
     "........WSW.W...",
     ".......WSW...W..",
     "......WSW.......",
     ".....WSW........",
     "....WSW.........",
     "...WSW..........",
     "..WSW...........",
     "...W............",
     "................",
     ".GGGGGGGGGGGG...",
     ".GGGGGGGGGGGG...",
     "................",
     },
    {"W=5A3A18", "S=9E7040", "G=3FD23F"},
};

PixelArt const kIgnoreDurability{
    {
     "................",
     "...WWWWWW.....R.",
     "..W......WW..R..",
     ".........WSWR...",
     "........WSWR.W..",
     ".......WSWR..W..",
     "......WSWR......",
     ".....WSWR.......",
     "....WSWR........",
     "...WSWR.........",
     "..WSWR..........",
     "...WR...........",
     "...R............",
     ".GGRGGGGGGGGG...",
     ".GRGGGGGGGGGG...",
     ".R..............",
     },
    {"W=5A3A18", "S=9E7040", "G=8A8A8A", "R=D82020"},
};

PixelArt const kAnyTag{
    {
     "................",
     "................",
     "......YYYYYYYY..",
     ".....YLLLLLLLY..",
     "....YLLLLLLLLY..",
     "...YLLHHLLLLLY..",
     "..YLLLHHLLLLLY..",
     "..YLLLLLLLLLLY..",
     "..YLLLLLLLLLLY..",
     "...YLLLLLLLLLY..",
     "....YLLLLLLLLY..",
     ".....YLLLLLLLY..",
     "......YYYYYYYY..",
     "............GG..",
     "...........GG...",
     "...........G....",
     },
    {"Y=8A6A10", "L=F0D050", "H=3A2A05", "G=3FD23F"},
};

PixelArt const kAllTags{
    {
     "................",
     ".......YYYYYYY..",
     "......YLLLLLLY..",
     ".....YLLLLLLLY..",
     ".....YYYYYYYYY..",
     "....YLLLLLLLY...",
     "...YLLLLLLLLY...",
     "...YYYYYYYYYY...",
     "..YLLLLLLLLY....",
     ".YLLHLLLLLLY....",
     ".YLLLLLLLLLY....",
     ".YLLLLLLLLLY....",
     "..YLLLLLLLLY....",
     "...YYYYYYYYY....",
     "................",
     "................",
     },
    {"Y=8A6A10", "L=F0D050", "H=3A2A05"},
};

PixelArt const kAddTag{
    {
     "................",
     "................",
     "....YYYYYYY.....",
     "...YLLLLLLY.....",
     "..YLHLLLLLY.....",
     ".YLLLLLLLLY.....",
     ".YLLLLLLLLY.....",
     "..YLLLLLLLY.....",
     "...YLLLLLLY.GG..",
     "....YYYYYYY.GG..",
     "..........GGGGGG",
     "..........GGGGGG",
     "............GG..",
     "............GG..",
     "................",
     "................",
     },
    {"Y=8A6A10", "L=F0D050", "H=3A2A05", "G=3FD23F"},
};

PixelArt const kRemoveTag{
    {
     "................",
     "................",
     "....YYYYYYY.....",
     "...YLLLLLLY.....",
     "..YLHLLLLLY.....",
     ".YLLLLLLLLY.....",
     ".YLLLLLLLLY.....",
     "..YLLLLLLLY.....",
     "...YLLLLLLY.....",
     "....YYYYYYY.....",
     "..........RRRRRR",
     "..........RRRRRR",
     "................",
     "................",
     "................",
     "................",
     },
    {"Y=8A6A10", "L=F0D050", "H=3A2A05", "R=D82020"},
};

PixelArt const kDirBoth{
    {
     "................",
     "....G...........",
     "...GGG..........",
     "..GGGGG.........",
     ".GGGGGGG........",
     "....G.......R...",
     "....G.......R...",
     "....G.......R...",
     "....G.......R...",
     "....G.......R...",
     "....G.......R...",
     "........RRRRRRR.",
     ".........RRRRR..",
     "..........RRR...",
     "...........R....",
     "................",
     },
    {"G=3FD23F", "R=D82020"},
};

PixelArt const kDirInput{
    {
     "................",
     ".......G........",
     ".......G........",
     ".......G........",
     ".......G........",
     "....GGGGGGG.....",
     ".....GGGGG......",
     "......GGG.......",
     ".......G........",
     "................",
     ".BB.........BB..",
     ".BB.........BB..",
     ".BBBBBBBBBBBBB..",
     ".BBBBBBBBBBBBB..",
     "................",
     "................",
     },
    {"G=3FD23F", "B=5A5A5A"},
};

PixelArt const kDirOutput{
    {
     "................",
     ".......R........",
     "......RRR.......",
     ".....RRRRR......",
     "....RRRRRRR.....",
     ".......R........",
     ".......R........",
     ".......R........",
     ".......R........",
     "................",
     ".BB.........BB..",
     ".BB.........BB..",
     ".BBBBBBBBBBBBB..",
     ".BBBBBBBBBBBBB..",
     "................",
     "................",
     },
    {"R=D82020", "B=5A5A5A"},
};
} // namespace

void draw(Painter& p, Icon icon, float x, float y) {
    PixelArt const* art = nullptr;
    switch (icon) {
    case Icon::Allow:
        art = &kAllow;
        break;
    case Icon::Block:
        art = &kBlock;
        break;
    case Icon::MatchStorage:
        art = &kStorage;
        break;
    case Icon::MatchInventory:
        art = &kInventory;
        break;
    case Icon::MatchItem:
        art = &kMatchItem;
        break;
    case Icon::MatchMod:
        art = &kMatchMod;
        break;
    case Icon::MatchTags:
        art = &kMatchTags;
        break;
    case Icon::MatchDurability:
        art = &kMatchDurability;
        break;
    case Icon::IgnoreDurability:
        art = &kIgnoreDurability;
        break;
    case Icon::MatchNbt:
        sb::icon(p, sb::Icon::MatchNbt, x + 1, y + 1);
        return;
    case Icon::IgnoreNbt:
        sb::icon(p, sb::Icon::IgnoreNbt, x + 1, y + 1);
        return;
    case Icon::MatchAnyTag:
        art = &kAnyTag;
        break;
    case Icon::MatchAllTags:
        art = &kAllTags;
        break;
    case Icon::AddTag:
        art = &kAddTag;
        break;
    case Icon::RemoveTag:
        art = &kRemoveTag;
        break;
    case Icon::DirectionBoth:
        art = &kDirBoth;
        break;
    case Icon::DirectionInput:
        art = &kDirInput;
        break;
    case Icon::DirectionOutput:
        art = &kDirOutput;
        break;
    }
    if (art) p.pixels(*art, x + 1, y + 1, 1, L::Foreground);
}

} // namespace abp::client::ui::filter_icons

#endif // ABP_CLIENT
