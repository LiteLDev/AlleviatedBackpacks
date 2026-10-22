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

// Vanilla inventory, hotbar and every other vanilla item slot: the layered 3D icon instead of the
// item sprite.
LL_TYPE_INSTANCE_HOOK(
    AbpBackpackSlotIconHook,
    ll::memory::HookPriority::Normal,
    InventoryItemRenderer,
    &InventoryItemRenderer::$render,
    void,
    ::MinecraftUIRenderContext& ctx,
    ::IClientInstance&          client,
    ::UIControl&                owner,
    int                         pass
) {
    auto const& stack = *this->mItemInstance;
    if (!stack.isNull() && game::backpackTierOf(stack.getTypeName())) {
        try {
            if (owner.mCachedPositionDirty) owner._setCachedPosition();
            auto const& pos  = *owner.mCachedPosition;
            auto const& size = *owner.mSize;
            float       side = std::min(size.x, size.y);
            ui::Painter painter(ctx);
            if (drawBackpackIcon(painter, stack, pos.x + (size.x - side) / 2, pos.y + (size.y - side) / 2, side / 16.0f)) return;
        } catch (std::exception const& e) {
            log().error("backpack icon failed: {}", e.what());
        }
    }
    origin(ctx, client, owner, pass);
}

class BackpackIconModule {
public:
    void install() { AbpBackpackSlotIconHook::hook(); }
    void uninstall() { AbpBackpackSlotIconHook::unhook(); }
};

} // namespace abp::client::render

ABP_REGISTER_MODULE(abp::client::render::BackpackIconModule);

#endif // ABP_CLIENT
