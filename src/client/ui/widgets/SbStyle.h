#pragma once

// Sophisticated Backpacks / Sophisticated Core look, drawn procedurally (original pixel art; no
// textures from the Java mod). Sizes and offsets follow SC's GuiHelper / ButtonDefinitions.

#include <vector>

#include "client/ui/gfx/Painter.h"

namespace abp::client::ui::sb {

namespace color {
inline constexpr Color PANEL      = Color::rgb(0xC6C6C6);
inline constexpr Color PANEL_DARK = Color::rgb(0x555555);
inline constexpr Color PANEL_LITE = Color::rgb(0xFFFFFF);
inline constexpr Color OUTLINE    = Color::rgb(0x000000);
inline constexpr Color SLOT       = Color::rgb(0x8B8B8B);
inline constexpr Color SLOT_DARK  = Color::rgb(0x373737);
inline constexpr Color TEXT       = Color::rgb(0x404040);        ///< 4210752
inline constexpr Color HOVER      = Color::argb(0x80FFFFFF);     ///< slot highlight
inline constexpr Color DISABLED   = Color::argb(0xC0101010);     ///< -1072689136: disabled upgrade slot
inline constexpr Color ERROR_SLOT = Color::argb(0xAAB02E26);     ///< DyeColor.RED | 0xAA alpha
inline constexpr Color ERROR_BG   = Color::argb(0xF0100010);
inline constexpr Color ERROR_EDGE = Color::rgb(0xB02E26);
inline constexpr Color SEARCH_BG  = Color::argb(0xFF777777);
} // namespace color

/// Vanilla container panel (rounded black outline, white top-left / grey bottom-right bevel).
void panel(Painter& p, Rect const& r);
/// The storage background: `panel` (SC's storage_background_*.png is the vanilla container art).
void storageBackground(Painter& p, Rect const& r);
/// Recessed 18x18 slot frame whose 16x16 item area starts at (x, y).
void slotFrame(Painter& p, float x, float y);
/// A block of slot frames (SC `renderSlotsBackground`): `columns` x `rows` full rows plus
/// `extra` slots in one more row, starting with the first frame's top-left at (x, y).
void slotsBackground(Painter& p, float x, float y, int columns, int rows, int extra);

/// SC's upgrade column: from x (= leftPos - 21) and y (= topPos), `slots` upgrade slots 16 apart,
/// with a switch notch on the left for every slot in `switches`.
void upgradeColumn(Painter& p, float x, float y, int slots, std::vector<bool> const& switches);

/// 18x18 button background (DEFAULT_BUTTON_BACKGROUND / _HOVERED_).
void buttonBackground(Painter& p, Rect const& r, bool hovered);
/// 12x12 small button background (SMALL_BUTTON_BACKGROUND / _HOVERED_).
void smallButtonBackground(Painter& p, Rect const& r, bool hovered);
/// Upgrade on/off switch, 6x12 (UPGRADE_SWITCH_*): `state` 1 = on, 0 = off, -1 = inactive.
void upgradeSwitch(Painter& p, float x, float y, int state, bool hovered);
/// One switch pip (4x10) as the contents tooltip shows it.
void switchPip(Painter& p, float x, float y, bool on);
/// Settings tab background (GuiHelper.renderTabBackground): the tab plus a 3 unit strip to the
/// left that merges it into the storage background.
void tabBackground(Painter& p, Rect const& r);
/// SC's control background (GuiHelper.renderControlBackground) for widgets inside tabs.
void controlBackground(Painter& p, Rect const& r);
/// Forge ScrollPanel bar: black track and a grey thumb with a light edge.
void scrollBar(Painter& p, Rect const& track, Rect const& thumb);
/// Vanilla tooltip box around a text area.
void tooltipBox(Painter& p, Rect const& body, Color background, Color borderTop, Color borderBottom);

// ---- icons (original pixel art) -------------------------------------------------------------------
enum class Icon {
    TransferToStorage,         ///< 12x12 green up arrow (also SC's Sort icon)
    TransferToStorageFiltered, ///< 12x12
    TransferToInventory,       ///< 12x12 red down arrow
    TransferToInventoryFiltered,
    SortByName,                ///< 12x12 "A"
    SortByMod,                 ///< "M"
    SortByCount,               ///< "1"
    SortByTags,                ///< "T"
    Settings,                  ///< 16x16 gear (backpack settings tab)
    Search,                    ///< magnifying glass (search hint), 7x7
    Memory,                    ///< 16x16 memory slot overlay
    // 16x16 settings screen icons
    BackToBackpack,
    BackpackSettings,
    MemoryTab,
    NoSortTab,
    SelectAll,
    UnselectAll,
    MatchNbt,
    IgnoreNbt,
    ShiftClickTabOn,
    ShiftClickTabOff,
    KeepTabOpenOn,
    KeepTabOpenOff,
    KeepSearchOn,
    KeepSearchOff,
};
/// SC ColorToggleButton face: the dye colour in a 12x12 square at (x+3, y+3) of an 18x18 button.
void colorSquare(Painter& p, float x, float y, std::uint32_t rgb);
void icon(Painter& p, Icon icon, float x, float y, float alpha = 1);

} // namespace abp::client::ui::sb
