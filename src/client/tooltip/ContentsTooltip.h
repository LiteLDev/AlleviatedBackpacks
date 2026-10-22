#pragma once

// Sophisticated Backpacks' item tooltips (client):
//
// - Backpack items (SB `BackpackItem.appendHoverText` / `getTooltipImage`):
//     "Press <Left Shift> to View Contents" (gray, "Left Shift" aqua) while Shift is up;
//     with advanced tooltips "UUID: <uuid>" (dark gray), or the linked storage group / endpoint;
//     with Shift held (or an item on the cursor) SC's contents image
//     (`ClientStorageContentsTooltipBase`): the stack multiplier / tank / energy lines, "Upgrades"
//     with on/off pips (4x10) for upgrades that can be disabled, "Inventory" with the contents
//     compacted and sorted by count, 9 per row, cells max(18, width(count) + 2) wide.
//     The data comes from the server (ContentsPacket), asked for at most every 20 ticks.
// - Upgrade items (SC `UpgradeItemBase.appendHoverText`): the `<descriptionId>.tooltip` lines
//   from the resource pack's lang, dark gray.
// - Linked storage endpoints: the role header (SB `ClientLinkedStorageTooltip`) above the
//   contents; the linked storage unit supplies it through `setLinkedEndpointProvider`.

#include <functional>
#include <optional>
#include <string>
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
/// SC `ClientEventHandler.renderSpecialTooltip`: "Right Click To Add" and the contents image of
/// `backpack` (right-click stashing).
void drawRightClickToAdd(ui::Painter& painter, ItemStack const& backpack, float x, float y);

// ---- linked storage (additive hook for the linked storage unit) --------------------------------

/// A backpack item that is a linked storage endpoint (SB `LinkedStorageEndpointData`).
struct LinkedEndpoint {
    /// SB `ClientLinkedStorageTooltip.getDescription`: "Main Linked" / "Linked", or
    /// "<role> (<group name>)", already localized.
    std::string description;
    /// Draws the 16x16 role icon with its top-left at (x, y) (SB draws it at leftX - 2).
    std::function<void(ui::Painter&, float x, float y)> drawIcon;
    std::string groupId;    ///< advanced tooltip: "Linked Storage Group: <id>"
    std::string endpointId; ///< advanced tooltip: "Linked Storage Endpoint: <id>"
    /// Storage whose contents the Shift image shows (the resolved group storage); empty = the
    /// backpack's own uuid.
    std::string contentsUuid;
};
using LinkedEndpointProvider = std::function<std::optional<LinkedEndpoint>(ItemStack const& backpack)>;
/// Installs the provider (nullptr removes it). Client thread.
void setLinkedEndpointProvider(LinkedEndpointProvider provider);

} // namespace abp::client::tooltip
