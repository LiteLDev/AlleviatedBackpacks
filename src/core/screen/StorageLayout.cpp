#include "core/screen/StorageLayout.h"

#include <algorithm>
#include <cmath>

#include "core/Capacity.h"

namespace abp::core::screen {

using namespace geometry;

int storageRows(int storageSlots) {
    if (storageSlots <= 0) return 0;
    int columns = storageSlots <= 81 ? 9 : 12;
    return (storageSlots + columns - 1) / columns;
}

int StorageLayout::upgradeHeightWithoutBottom() const {
    return kUpgradeBottomHeight + static_cast<int>(upgradeSlots.size()) * kUpgradeSlotHeight;
}

int StorageLayout::titleMaxWidth() const {
    int titleLineEnd = searchBox.x + searchBox.w - kSearchMinWidth - kTitleControlPadding;
    return std::max(0, titleLineEnd - kTitleX);
}

Rect StorageLayout::scrollBar() const {
    return {scrollPanel.x + scrollPanel.w - kScrollBarWidth, scrollPanel.y, kScrollBarWidth, scrollPanel.h};
}

StorageLayout computeLayout(LayoutInput const& in) {
    StorageLayout l;
    int baseSlots = capacity::baseSlotsFor(in.storageSlots, in.columnsTaken);
    l.rows        = storageRows(baseSlots);

    // updateDimensionsAndSlotPositions(height)
    int displayable = std::min((in.screenHeight - kHeightWithoutStorageSlots) / kSlotSize, l.rows);
    displayable     = std::max(displayable, std::min(1, l.rows));
    l.visibleRows   = displayable;
    l.imageHeight   = kHeightWithoutStorageSlots + displayable * kSlotSize;
    bool regular9   = baseSlots <= 81;
    l.slotsOnLine   = regular9 ? 9 : 12;
    l.playerInventoryXOffset = regular9 ? 0 : 27;
    l.imageWidth    = l.slotsOnLine * kSlotSize + 14;
    l.storageColumns = std::max(1, l.slotsOnLine - in.columnsTaken);
    if (displayable < l.rows) {
        l.scroll = true;
        l.wider  = true;
        l.playerInventoryXOffset += 3; // WIDER_9: 3, WIDER_12: 30
        l.imageWidth += 6;
    }
    l.inventoryLabelY = l.imageHeight - 94;
    l.inventoryLabelX = 8 + l.playerInventoryXOffset;

    // updatePlayerSlotsPositions(): 3 rows of main inventory, a 4 unit gap, then the hotbar.
    int y = l.inventoryLabelY + 12;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 9; ++col) {
            l.playerSlots[static_cast<std::size_t>(9 + col + row * 9)] = {l.playerInventoryXOffset + 8 + col * 18, y};
        }
        y += 18;
    }
    y += 4;
    for (int col = 0; col < 9; ++col) l.playerSlots[static_cast<std::size_t>(col)] = {l.playerInventoryXOffset + 8 + col * 18, y};

    // updateUpgradeSlotsPositions() + addUpgradeSwitches()
    for (int i = 0; i < in.upgradeSlots; ++i) {
        l.upgradeSlots.push_back({kUpgradeSlotX, kUpgradeSlotsY + i * kUpgradeSlotHeight});
        bool hasSwitch = static_cast<std::size_t>(i) < in.upgradeSwitches.size() && in.upgradeSwitches[static_cast<std::size_t>(i)];
        l.upgradeSwitches.push_back(hasSwitch ? Rect{kSwitchX, kSwitchY + i * kUpgradeSlotHeight, 6, 12} : Rect{});
    }
    if (in.upgradeSlots > 0) {
        l.upgradeColumn = {-kUpgradeInventoryOffset, 0, 26, l.upgradeHeightWithoutBottom() + kUpgradeBottomHeight};
    }

    // addSortButtons()
    switch (in.sortButtons) {
    case SortButtonsPosition::TitleLineRight:
        l.sortButton = {l.imageWidth - 31, 4, kSmallButton, kSmallButton};
        break;
    case SortButtonsPosition::BelowUpgradeTabs:
        // (settingsTabControl.x + 2, settingsTabControl.y + max(0, height + 2))
        l.sortButton = {l.imageWidth + 2, kTabsY + std::max(0, in.tabsHeight + 2), kSmallButton, kSmallButton};
        break;
    case SortButtonsPosition::BelowUpgrades:
        l.sortButton = {
            -kUpgradeInventoryOffset - 2,
            l.upgradeHeightWithoutBottom() + kUpgradeBottomHeight + 2,
            kSmallButton,
            kSmallButton
        };
        break;
    case SortButtonsPosition::Hidden:
        break;
    }
    if (l.sortButton.w > 0) l.sortByButton = {l.sortButton.x + 12, l.sortButton.y, kSmallButton, kSmallButton};

    // addSearchBox(): from x 7 to the sort buttons (title line) or the right edge.
    bool titleLineSort = in.sortButtons == SortButtonsPosition::TitleLineRight && l.sortButton.w > 0;
    int  xEnd          = titleLineSort ? l.sortButton.x - 1 : l.imageWidth - 7;
    l.searchBox        = {7, 5, std::max(kSearchMinWidth, xEnd - 7), 10};

    // updateTransferButtonsPositions()
    l.transferToStorage   = {l.inventoryLabelX + 137, l.inventoryLabelY - 2, kSmallButton, kSmallButton};
    l.transferToInventory = {l.inventoryLabelX + 149, l.inventoryLabelY - 2, kSmallButton, kSmallButton};

    l.tabsOrigin  = {l.imageWidth, kTabsY};
    l.storageArea = {kSlotsXOffset, kSlotsYOffset, l.storageColumns * kSlotSize, l.visibleRows * kSlotSize};
    l.scrollPanel = {kSlotsXOffset, kSlotsYOffset, l.storageColumns * kSlotSize + kScrollBarWidth, l.visibleRows * kSlotSize};
    if (in.columnsTaken > 0) {
        int x            = kSlotsXOffset + l.storageColumns * kSlotSize + (l.scroll ? kScrollBarWidth : 0);
        l.inventoryParts = {x, kSlotsYOffset, 36 * (in.columnsTaken / 2), l.visibleRows * kSlotSize};
    }
    return l;
}

// ---- scrolling -----------------------------------------------------------------------------------

int scrollContentHeight(int storageSlots, int columns) {
    if (columns <= 0) return 0;
    int rows = storageSlots / columns + (storageSlots % columns > 0 ? 1 : 0);
    return rows * kSlotSize;
}

int scrollMax(StorageLayout const& layout, int storageSlots) {
    return scrollContentHeight(storageSlots, layout.storageColumns) - layout.scrollPanel.h;
}

namespace {
void applyLimits(ScrollState& state, StorageLayout const& layout, int storageSlots) {
    int max = scrollMax(layout, storageSlots);
    if (max < 0) max /= 2;
    if (state.distance > max) state.distance = max;
    if (state.distance < 0) state.distance = 0;
}

int barHeight(StorageLayout const& layout, int storageSlots) {
    int content = std::max(1, scrollContentHeight(storageSlots, layout.storageColumns));
    int h       = layout.scrollPanel.h;
    int bar     = h * h / content;
    if (bar < 32) bar = 32;
    if (bar > h) bar = h;
    return bar;
}
} // namespace

void scrollBy(ScrollState& state, StorageLayout const& layout, int storageSlots, double notches) {
    if (!layout.scroll || notches == 0) return;
    state.distance += -notches * kSlotSize;
    applyLimits(state, layout, storageSlots);
}

Rect scrollThumb(StorageLayout const& layout, int storageSlots, ScrollState const& state) {
    auto bar    = layout.scrollBar();
    int  height = barHeight(layout, storageSlots);
    int  extra  = scrollContentHeight(storageSlots, layout.storageColumns) - layout.scrollPanel.h;
    int  top    = bar.y;
    if (extra > 0) {
        top = static_cast<int>(state.distance) * (layout.scrollPanel.h - height) / extra + bar.y;
        if (top < bar.y) top = bar.y;
    }
    return {bar.x, top, bar.w, height};
}

void scrollDrag(ScrollState& state, StorageLayout const& layout, int storageSlots, double dy) {
    if (!layout.scroll) return;
    double travel = layout.scrollPanel.h - barHeight(layout, storageSlots);
    if (travel <= 0) return;
    state.distance += scrollMax(layout, storageSlots) * (dy / travel);
    applyLimits(state, layout, storageSlots);
}

// ---- placement -----------------------------------------------------------------------------------

std::vector<PlacedSlot> placeStorageSlots(
    StorageLayout const&     layout,
    int                      storageSlots,
    std::vector<bool> const& matches,
    ScrollState const&       scroll,
    int*                     visibleCount
) {
    std::vector<PlacedSlot> out;
    int const               cols = layout.storageColumns;
    auto matchesAt = [&](int i) { return matches.empty() || (static_cast<std::size_t>(i) < matches.size() && matches[static_cast<std::size_t>(i)]); };
    int visible = 0;
    if (!layout.scroll) {
        // updateStorageSlotsPositions(): matching slots flow from the top-left without gaps.
        for (int i = 0; i < storageSlots; ++i) {
            if (!matchesAt(i)) continue;
            out.push_back({i, {8 + (visible % cols) * kSlotSize, 18 + (visible / cols) * kSlotSize}});
            ++visible;
        }
    } else {
        // InventoryScrollPanel::updateSlotsPosition()
        int rowOffset = scroll.rowOffset();
        int filtered  = 0;
        for (int i = 0; i < storageSlots; ++i) {
            int  row     = filtered / cols - rowOffset;
            bool matched = matchesAt(i);
            if (matched) ++filtered;
            int column = visible % cols;
            int y      = layout.scrollPanel.y + row * kSlotSize + 1;
            int x      = layout.scrollPanel.x + column * kSlotSize + 1;
            if (y < 1 || y > layout.scrollPanel.h || !matched) continue;
            ++visible;
            out.push_back({i, {x, y}});
        }
    }
    if (visibleCount) *visibleCount = visible;
    return out;
}

} // namespace abp::core::screen
