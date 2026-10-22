#pragma once

#include <array>
#include <vector>

namespace abp::core::screen {

/// A point / rectangle in GUI units, relative to the top-left corner of the storage background
/// (Java's `leftPos` / `topPos`) unless stated otherwise.
struct Point {
    int x = 0, y = 0;
    [[nodiscard]] bool operator==(Point const&) const = default;
};

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;

    [[nodiscard]] bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    [[nodiscard]] bool contains(double px, double py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    [[nodiscard]] int  right() const { return x + w; }
    [[nodiscard]] int  bottom() const { return y + h; }
    [[nodiscard]] bool operator==(Rect const&) const = default;
};

/// Where Sophisticated Core puts the sort buttons (client config `sortButtonsPosition`).
enum class SortButtonsPosition { TitleLineRight, BelowUpgrades, BelowUpgradeTabs, Hidden };

/// Geometry constants of Sophisticated Core's `StorageScreenBase` (GUI units).
namespace geometry {
inline constexpr int kHeightWithoutStorageSlots = 114; ///< title row + player inventory part
inline constexpr int kSlotsXOffset              = 7;   ///< slot background left edge
inline constexpr int kSlotsYOffset              = 17;  ///< slot background top edge
inline constexpr int kSlotSize                  = 18;
inline constexpr int kUpgradeInventoryOffset    = 21; ///< upgrade column starts this far left of the background
inline constexpr int kUpgradeTopHeight          = 7;
inline constexpr int kUpgradeSlotHeight         = 16; ///< upgrade slots are 16 apart (frames overlap)
inline constexpr int kUpgradeBottomHeight       = 6;
inline constexpr int kUpgradeSlotX              = -15;
inline constexpr int kUpgradeSlotsY             = 6;
inline constexpr int kSwitchX                   = -22; ///< upgrade on/off switch, 6x12
inline constexpr int kSwitchY                   = 8;
inline constexpr int kScrollBarWidth            = 6;
inline constexpr int kTabWidth                  = 21; ///< closed tab
inline constexpr int kTabHeight                 = 24;
inline constexpr int kTabSpacing                = 1;
inline constexpr int kTabsY                     = 4;
inline constexpr int kSmallButton               = 12;
inline constexpr int kButton                    = 18;
inline constexpr int kSearchMinWidth            = 10;
inline constexpr int kTitleControlPadding       = 2;
inline constexpr int kPlayerInventoryHeight     = 97; ///< bottom part of the background texture
inline constexpr int kTitleX                    = 8;
inline constexpr int kTitleY                    = 6;
inline constexpr int kTextColor                 = 0x404040;
} // namespace geometry

struct LayoutInput {
    int storageSlots = 27;
    int upgradeSlots = 1;
    /// Storage columns taken by upgrade inventory parts (SB `getInventoryColumnsTaken`: 2 per Tank /
    /// Battery upgrade). `storageSlots` is then the remaining slot count; rows and the 9/12 column
    /// background come from the base layout (`capacity::baseSlotsFor`).
    int columnsTaken = 0;
    /// Height of the screen in GUI units (decides how many rows fit before the scroll bar).
    int screenHeight = 240;
    SortButtonsPosition sortButtons = SortButtonsPosition::TitleLineRight;
    /// Height of the settings tab column (`SettingsTabControl.getHeight`), for
    /// `SortButtonsPosition::BelowUpgradeTabs`.
    int tabsHeight = 0;
    /// Per upgrade slot: the upgrade can be switched on/off (shows a switch).
    std::vector<bool> upgradeSwitches;
};

/// Layout of the backpack screen, computed exactly like `StorageScreenBase` does.
struct StorageLayout {
    int  slotsOnLine    = 9;  ///< background columns: 9 (<= 81 slots) or 12
    int  storageColumns = 9;  ///< slotsOnLine - columnsTaken
    int  rows           = 3;  ///< storage rows (`BackpackWrapper::getNumberOfSlotRows`)
    int  visibleRows    = 3;  ///< rows that fit on screen
    bool scroll         = false; ///< rows don't fit: InventoryScrollPanel
    bool wider          = false; ///< the "_wider" background (scroll bar room, +6)
    int  playerInventoryXOffset = 0;
    int  imageWidth  = 176;
    int  imageHeight = 168;
    int  inventoryLabelX = 8;
    int  inventoryLabelY = 74;

    /// Item positions (16x16 icon top-left) of the player inventory slots, indexed like the
    /// Bedrock inventory: 0..8 hotbar, 9..35 main inventory.
    std::array<Point, 36> playerSlots{};
    std::vector<Point>    upgradeSlots;   ///< item positions of the upgrade slots
    std::vector<Rect>     upgradeSwitches; ///< per upgrade slot; w = 0 when it has no switch
    Rect upgradeColumn;  ///< background of the upgrade column (x < 0)
    Rect sortButton;     ///< 12x12; w = 0 when hidden
    Rect sortByButton;   ///< 12x12
    Rect searchBox;      ///< maximized (focused) search box
    Rect transferToStorage;   ///< 12x12
    Rect transferToInventory; ///< 12x12
    Point tabsOrigin;    ///< top-left of the first tab (the backpack settings tab)
    Rect  scrollPanel;   ///< slot area incl. the scroll bar (valid when `scroll`)
    Rect  storageArea;   ///< where storage slot backgrounds go (rows that fit)
    /// Upgrade inventory parts (SC `initUpgradeInventoryControls`, when columns are taken): the
    /// first part's top-left, right of the slots (and the scroll bar); each part is 36 wide and
    /// `inventoryParts.h` (= visible rows x 18) high. w = 36 x columnsTaken / 2.
    Rect  inventoryParts;

    /// Height of the upgrade column without its bottom border (`getUpgradeHeightWithoutBottom`).
    [[nodiscard]] int upgradeHeightWithoutBottom() const;
    /// Title area width before the (collapsed) search box: Java's `getTitleMaxWidth` with the
    /// search box minimized.
    [[nodiscard]] int titleMaxWidth() const;
    /// The scroll bar rectangle (valid when `scroll`).
    [[nodiscard]] Rect scrollBar() const;
};

[[nodiscard]] StorageLayout computeLayout(LayoutInput const& in);

/// Storage slot rows of a backpack (`ceil(slots/9)` up to 81 slots, else `ceil(slots/12)`).
[[nodiscard]] int storageRows(int storageSlots);

// ---- scroll panel (NeoForge ScrollPanel as used by InventoryScrollPanel) -------------------------

struct ScrollState {
    double distance = 0; ///< scrolled distance in GUI units

    [[nodiscard]] int rowOffset() const { return static_cast<int>(distance) / geometry::kSlotSize; }
};

/// Content height of the panel: all storage slots, `ceil(slots/columns)` rows of 18.
[[nodiscard]] int scrollContentHeight(int storageSlots, int columns);
[[nodiscard]] int scrollMax(StorageLayout const& layout, int storageSlots);
/// Wheel: `notches` > 0 scrolls up (content moves down), 18 units per notch, clamped.
void scrollBy(ScrollState& state, StorageLayout const& layout, int storageSlots, double notches);
/// Height and top (relative to the background) of the scroll bar thumb.
[[nodiscard]] Rect scrollThumb(StorageLayout const& layout, int storageSlots, ScrollState const& state);
/// Dragging the thumb by `dy` GUI units (mouse movement while the bar is held).
void scrollDrag(ScrollState& state, StorageLayout const& layout, int storageSlots, double dy);

// ---- storage slot placement (search filter + scroll) ---------------------------------------------

struct PlacedSlot {
    int   index; ///< backpack storage index
    Point pos;   ///< item position (relative to the background)
};

/// Places the storage slots that match the search filter (`matches[i]`; empty vector = all),
/// re-flowing them without gaps like `updateStorageSlotsPositions` / `InventoryScrollPanel`.
/// Only slots in the visible rows are returned. `visibleCount` receives the number of slot
/// backgrounds to draw.
[[nodiscard]] std::vector<PlacedSlot> placeStorageSlots(
    StorageLayout const&     layout,
    int                      storageSlots,
    std::vector<bool> const& matches,
    ScrollState const&       scroll,
    int*                     visibleCount = nullptr
);

} // namespace abp::core::screen
