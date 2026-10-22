#pragma once

// SC LargeResultSelector: the modal "expanded output list with search" of the stonecutter tab
// (176x122, centred on the screen above a dimmed backdrop): a search box (focused when it opens),
// 9x5 results of 16x18 with the selected / recent / hovered backgrounds and a scroll bar. Picking
// a result selects it and closes the list; clicking outside or Esc closes it.

#include <functional>
#include <vector>

#include "mc/world/item/ItemStack.h"

namespace abp::client::ui::station {

struct ResultEntry {
    int       index = -1; ///< recipe index sent back on selection
    ItemStack stack;
    bool      recent = false;
};

/// Opens the selector over the current backpack screen (closes with the screen).
void openLargeResultSelector(std::vector<ResultEntry> results, int selectedIndex, std::function<void(int index)> onSelect);
void closeLargeResultSelector();
[[nodiscard]] bool largeResultSelectorOpen();

} // namespace abp::client::ui::station
