#pragma once

// Upgrade inventory parts of the native backpack screen (SC `UpgradeInventoryPartBase` /
// `UpgradeGuiManager.getInventoryControl`): an upgrade whose `UpgradeInfo::inventoryColumnsTaken`
// is > 0 (tank, battery) gets that many storage columns right of the slots (and the scroll bar),
// `visibleRows * 18` high, and draws its part there. Parts are placed in upgrade slot order, each
// `columnsTaken * 18` wide (SC moves the next control 36 to the right).
//
// Register a part for an upgrade id in a client .cpp:
//
//     class TankPart : public abp::client::ui::InventoryPart { ... };
//     ABP_REGISTER_INVENTORY_PART("tank", TankPart);

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "client/ui/Geometry.h"
#include "client/ui/UiLayer.h"
#include "client/ui/widgets/Tooltip.h"
#include "core/screen/ScreenModel.h"

namespace abp::client::ui {

class Painter;

struct InventoryPartContext {
    core::screen::BackpackScreenModel const& model;
    int                                      slot; ///< upgrade slot index
    core::screen::UpgradeView const&         upgrade;
    /// Sends `{"action":"tab","slot":slot,"data":data}` (handled by `ABP_REGISTER_TAB_ACTION`).
    std::function<void(nlohmann::json data)> sendTabAction;
};

class InventoryPart {
public:
    virtual ~InventoryPart() = default;
    /// Draws the part into `area` (absolute GUI units).
    virtual void render(Painter& p, InventoryPartContext const& ctx, Rect const& area, float mouseX, float mouseY) = 0;
    /// A click inside `area`; return true when handled.
    virtual bool mouseClicked(InventoryPartContext const& /*ctx*/, Rect const& /*area*/, MouseButton /*button*/, float /*x*/, float /*y*/) {
        return false;
    }
    /// Tooltip lines when hovered.
    virtual void tooltip(InventoryPartContext const& /*ctx*/, Rect const& /*area*/, float /*x*/, float /*y*/, std::vector<TooltipLine>& /*out*/) {}
    /// SC `renderErrorOverlay`: the part is listed in the model's `errorInventoryParts`. Default: the
    /// red error colour over the whole area.
    virtual void renderErrorOverlay(Painter& p, Rect const& area);
};

using InventoryPartFactory = std::function<std::unique_ptr<InventoryPart>()>;

/// Registers the part drawn for upgrade `upgradeId` (first registration wins).
void registerInventoryPart(std::string upgradeId, InventoryPartFactory factory);
[[nodiscard]] bool                           hasInventoryPart(std::string const& upgradeId);
[[nodiscard]] std::unique_ptr<InventoryPart> createInventoryPart(std::string const& upgradeId);

namespace detail {
struct InventoryPartRegistrar {
    InventoryPartRegistrar(std::string id, InventoryPartFactory f) { registerInventoryPart(std::move(id), std::move(f)); }
};
} // namespace detail

} // namespace abp::client::ui

#define ABP_INVENTORY_PART_CAT2(a, b) a##b
#define ABP_INVENTORY_PART_CAT(a, b)  ABP_INVENTORY_PART_CAT2(a, b)
/// Registers inventory part type `T` (default-constructible) for upgrade id `id`.
#define ABP_REGISTER_INVENTORY_PART(id, T)                                                                             \
    static ::abp::client::ui::detail::InventoryPartRegistrar ABP_INVENTORY_PART_CAT(abp_inventory_part_, __COUNTER__)( \
        id,                                                                                                            \
        [] { return std::unique_ptr<::abp::client::ui::InventoryPart>(new T()); }                                      \
    )
