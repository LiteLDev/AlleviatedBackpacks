#pragma once

// Upgrade inventory parts of the native backpack screen (Sophisticated Core
// `UpgradeInventoryPartBase` / `initUpgradeInventoryControls`).
//
// Upgrades that take storage columns (Tank, Battery: `upgrade/tank/Columns.h` on the server) get a
// control drawn right of the storage slots (and right of the scroll bar), 36 units per upgrade, in
// upgrade slot order, as high as the visible storage rows. The server sends the columns in each
// such upgrade's tab data (`"inventoryColumns"`, and the backpack's `"columnsTaken"`); units add
// the control with `ABP_REGISTER_INVENTORY_PART("tank", [] { return std::make_unique<TankBar>(); })`.
//
// The screen host calls the functions at the end of this header (additive contract with the
// backpack screen: layout input, drawing, click on release, tooltip, error overlay).

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "client/ui/Geometry.h"
#include "client/ui/gfx/Painter.h"
#include "client/ui/widgets/Tooltip.h"
#include "core/screen/ScreenModel.h"
#include "mod/Registry.h"

namespace abp::client::ui {

struct InventoryPartContext {
    core::screen::BackpackScreenModel const& model;
    int                                      slot; ///< upgrade slot index
    core::screen::UpgradeView const&         upgrade;
    Rect                                     area; ///< absolute: the part's 36 x (visible rows x 18) area
};

class InventoryPart {
public:
    virtual ~InventoryPart() = default;
    virtual void render(Painter& p, InventoryPartContext const& ctx, float mx, float my) = 0;
    /// SC `handleMouseReleased`. Return true when handled.
    virtual bool click(InventoryPartContext const& /*ctx*/, int /*button*/, float /*mx*/, float /*my*/) { return false; }
    /// Adds tooltip lines when hovered; return true to show them.
    virtual bool tooltip(InventoryPartContext const& /*ctx*/, float /*mx*/, float /*my*/, std::vector<TooltipLine>& /*out*/) const {
        return false;
    }
    /// SC `renderErrorOverlay` (the upgrade's slot is part of an error).
    virtual void errorOverlay(Painter& /*p*/, InventoryPartContext const& /*ctx*/) {}
};

using InventoryPartFactory = std::function<std::unique_ptr<InventoryPart>()>;
void registerInventoryPart(std::string upgradeId, InventoryPartFactory factory);

/// Storage columns taken by upgrade inventory parts in `model` (0 without).
[[nodiscard]] int inventoryColumnsTaken(core::screen::BackpackScreenModel const& model);

// ---- host side (BackpackScreen) --------------------------------------------------------------------
/// `parts`: absolute rect of the first part (layout `inventoryParts` + screen origin); w = all parts.
void renderInventoryParts(Painter& p, core::screen::BackpackScreenModel const& model, Rect parts, float mx, float my);
bool clickInventoryParts(core::screen::BackpackScreenModel const& model, Rect parts, int button, float mx, float my);
bool inventoryPartsTooltip(core::screen::BackpackScreenModel const& model, Rect parts, float mx, float my, std::vector<TooltipLine>& out);
void inventoryPartsErrorOverlay(Painter& p, core::screen::BackpackScreenModel const& model, Rect parts);

namespace detail {
struct InventoryPartRegistrar {
    InventoryPartRegistrar(std::string id, InventoryPartFactory f) { registerInventoryPart(std::move(id), std::move(f)); }
};
} // namespace detail

} // namespace abp::client::ui

#define ABP_REGISTER_INVENTORY_PART(id, factory)                                                                       \
    static ::abp::client::ui::detail::InventoryPartRegistrar ABP_UNIQUE(abp_inventory_part_)(id, factory)
