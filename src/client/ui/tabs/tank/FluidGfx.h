#pragma once

// Drawing helpers shared by the Tank, Battery, Pump and Experience Pump screens: the fluid / energy
// bars (SC TankInventoryControl / BatteryInventoryControl look, drawn from SB's dimensions) and the
// 16x16 button icons. All pixel art here is original.

#include <cstdint>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "client/ui/gfx/Painter.h"
#include "client/ui/widgets/Widgets.h"

class ItemStack;

namespace abp::client::ui::fluidgfx {

/// A fluid column `w` x `h` (SC renderTiledFluidTextureAtlas: a "still" pattern tinted with the
/// fluid colour, ARGB).
void fluid(Painter& p, float x, float y, float w, float h, std::uint32_t argb);

/// SC BAR_BACKGROUND_TOP / MIDDLE / BOTTOM: an 18 wide recessed column of height `h`.
void barBackground(Painter& p, float x, float y, float h);
/// SC tank OVERLAY (16x18 per row): level marks on the glass, `h / 18` rows from `y`.
void tankOverlay(Painter& p, float x, float y, float h);
/// SC battery OVERLAY (16x18 per row): the empty charge cells.
void batteryCells(Painter& p, float x, float y, float h);
/// SC CHARGE_SEGMENT (16x6) in `rgb`.
void chargeSegment(Painter& p, float x, float y, std::uint32_t rgb);
/// SC CONNECTION_TOP / _BOTTOM (16x4).
void batteryConnectionTop(Painter& p, float x, float y);
void batteryConnectionBottom(Painter& p, float x, float y);

enum class Icon {
    PumpInput,   ///< SC "pump_input" / "xp_pump_input": into the backpack
    PumpOutput,  ///< "pump_output": out of the backpack
    World,       ///< "interact_with_world"
    Hand,        ///< "interact_with_hand"
    Pipes,       ///< "interact_with_tanks_and_pipes"
    XpOutput,    ///< "xp_pump_output"
    XpKeep,      ///< "xp_pump_keep"
    Off,         ///< "xp_pump_off"
    Mend,        ///< "mend_items"
    TakeAll,     ///< "take_all_experience"
    Take,
    Store,
    StoreAll,    ///< "store_all_experience"
    TankInput,   ///< empty tank input slot
    TankOutput,  ///< empty tank output slot
    BatteryInput,
    BatteryOutput,
};
/// A 16x16 icon at (x, y).
void icon(Painter& p, Icon i, float x, float y, float alpha = 1);
/// SB's "do not ..." variants: a red no-entry sign over a 16x16 icon at (x, y).
void noSign(Painter& p, float x, float y);
/// SC tank tab ARROW (15x8) pointing down at (x, y).
void arrowDown(Painter& p, float x, float y);

/// Java "%,d".
[[nodiscard]] std::string grouped(long long n);

/// A slot inside an upgrade tab showing item `index` of the tab's server menu (`tab["menu"]`);
/// clicks go to that menu slot (`menuClick`). Empty: `emptyIcon` faded plus `emptyTooltip`.
class MenuSlot : public Widget {
public:
    int                                                   index = 0;
    Icon                                                  emptyIcon = Icon::TankInput;
    bool                                                  drawEmptyIcon = true;
    std::string                                           emptyTooltip;
    std::function<void(nlohmann::json)>                   send;
    std::vector<ItemStack>*                               stacks = nullptr; ///< owned by the tab
    std::vector<long long>*                               counts = nullptr;

    void render(Painter& p, float mx, float my, double time) override;
    bool mouseDown(MouseButton button, float x, float y) override;
    void tooltip(float x, float y, std::vector<TooltipLine>& out) const override;
};

/// Reads `tab["menu"]["slots"]` into stacks / counts.
void readMenuSlots(nlohmann::json const& tab, std::vector<ItemStack>& stacks, std::vector<long long>& counts);

} // namespace abp::client::ui::fluidgfx
