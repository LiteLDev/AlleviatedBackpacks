#ifdef ABP_CLIENT

#include "client/render/BackpackIcons.h"

#include <string>

#include "ll/api/memory/Hook.h"

#include "mc/client/gui/controls/UIControl.h"
#include "mc/client/gui/controls/renderers/InventoryItemRenderer.h"
#include "mc/world/item/ItemStack.h"

#include "client/render/ModelState.h"
#include "client/ui/gfx/Painter.h"
#include "core/Tier.h"
#include "feature/tiers/Tiers.h"
#include "game/BackpackItem.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace abp::client::render {

namespace {
constexpr std::string_view kIconDir = "textures/ui/alleviatedbackpacks/icons/";

ui::Color colorOf(std::uint32_t argb, float alpha) { return ui::Color::rgb(argb & 0xFFFFFF, alpha); }
} // namespace

bool drawBackpackIcon(ui::Painter& p, ItemStack const& stack, float x, float y, float scale, float alpha) {
    if (stack.isNull()) return false;
    auto tier = game::backpackTierOf(stack.getTypeName());
    if (!tier) return false;
    auto           snap = modelFor(stack);
    ModelSnapshot  defaults;
    defaults.tier       = static_cast<int>(*tier);
    // Unbound backpacks (e.g. SB's dyed creative-tab stacks) carry their colours on the item.
    if (auto c = tiers::itemColors(stack)) {
        defaults.mainColor   = c->main;
        defaults.accentColor = c->accent;
    }
    auto const&    s    = snap ? *snap : defaults;
    auto const&    m    = s.model;
    std::string    key  = std::string(kIconDir) + "c" + (m.battery ? "1" : "0") + (m.left ? "1" : "0") + (m.right ? "1" : "0");
    ui::Rect       r{x, y, 16 * scale, 16 * scale};
    using L = ui::Painter::Layer;
    // The item's own tier wins for the clips (a higher tier is applied to the storage on open).
    auto tierId = std::string(core::tierInfo(*tier).id);
    p.texture(key + "_cloth", r, alpha, L::Foreground, colorOf(s.mainColor, 1));
    p.texture(key + "_border", r, alpha, L::Foreground, colorOf(s.accentColor, 1));
    p.texture(key + "_fixed_" + tierId, r, alpha, L::Foreground);
    auto fluid = [&](std::optional<core::render::TankModel> const& t, char side) {
        if (!t || t->fluid.empty()) return;
        int level = core::render::ratioToStep(t->ratio, core::render::kFluidSteps);
        if (level <= 0) return;
        p.texture(key + "_fluid_" + side + "_" + std::to_string(level), r, alpha, L::Foreground, colorOf(t->color, 1));
    };
    fluid(m.left, 'l');
    fluid(m.right, 'r');
    if (m.battery) {
        int step = core::render::ratioToStep(*m.battery, core::render::kChargeSteps);
        if (step > 0) p.texture(key + "_charge_" + std::to_string(step), r, alpha, L::Foreground);
    }
    if (m.left || m.right) p.texture(key + "_glass", r, alpha, L::Foreground);
    return true;
}

} // namespace abp::client::render



#endif // ABP_CLIENT
