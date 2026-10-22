#pragma once

// Sophisticated Core's backpack contents tooltip (ClientStorageContentsTooltipBase) for backpack
// items: with Shift held (or an item on the cursor) the tooltip shows the stack multiplier, tanks,
// energy, the installed upgrades with their on/off pips and the inventory compacted and sorted by
// count (9 per row, 20 units per row, abbreviated counts); otherwise the item tooltip gets
// "Press <Left Shift> to View Contents". The data comes from the server (ContentsPacket), asked
// for at most every 20 ticks.

#include <vector>

#include "client/ui/widgets/Tooltip.h"

class ItemStack;

namespace abp::client::tooltip {

/// Shift held or something on the cursor (SB's condition for the contents image).
[[nodiscard]] bool contentsWanted();
/// "§7Press <§bLeft Shift§7> to View Contents" in the game's language.
[[nodiscard]] std::string pressForContentsLine();
/// The tooltip lines of a backpack item (vanilla lines, with the "press" line when the contents
/// are not shown) and, when shown, draws the contents image below. Returns false if `stack` is not
/// a backpack.
bool drawBackpackTooltip(ui::Painter& painter, ItemStack const& stack, float x, float y, std::vector<ui::TooltipLine> extra = {});

} // namespace abp::client::tooltip
