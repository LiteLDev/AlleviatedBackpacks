#ifdef ABP_CLIENT

#include "client/render/BackpackIcons.h"

#include <string>
#include <vector>

#include "ll/api/memory/Hook.h"

#include "mc/client/gui/controls/UIControl.h"
#include "mc/client/gui/controls/renderers/IInventoryItemRendererRenderContext.h"
#include "mc/client/gui/controls/renderers/InventoryItemRenderOwnerData.h"
#include "mc/client/gui/controls/renderers/InventoryItemRenderer.h"
#include "mc/client/gui/screens/ComponentRenderBatch.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
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

// ---- vanilla item slots (GX-1) ---------------------------------------------------------------------
//
// SB renders its backpack model (both colours, every part) wherever an item is drawn. Vanilla
// Bedrock slots (inventory, hotbar, containers) draw items through InventoryItemRenderer: `render`
// gets the UI render context, `_render` the slot's screen position and scale and draws the icon
// sprite (one atlas sprite, one item colour). For backpacks `_render` draws the layered icon above
// instead (count and durability are separate controls and stay vanilla).

// Vanilla item slots are batched into one shared mesh on the item atlas (begin/endSharedMeshBatch):
// images drawn while it is open would sample the atlas. Icons met inside a batch are drawn right
// after it ends, before anything later in the screen.

namespace {
struct DeferredIcon {
    ItemStack stack;
    float     x, y, scale;
};
thread_local MinecraftUIRenderContext* tSlotContext = nullptr;
thread_local int                       tMeshBatchDepth = 0;
thread_local std::vector<DeferredIcon> tDeferred;
bool                                   gSlotLogged = false;

void drawDeferred(MinecraftUIRenderContext& ctx) {
    if (tDeferred.empty()) return;
    auto icons = std::move(tDeferred);
    tDeferred.clear();
    ui::Painter p(ctx);
    for (auto const& d : icons) drawBackpackIcon(p, d.stack, d.x, d.y, d.scale);
    p.flush();
}
} // namespace

LL_TYPE_INSTANCE_HOOK(
    AbpBeginSharedMeshBatchHook,
    ll::memory::HookPriority::Normal,
    MinecraftUIRenderContext,
    &MinecraftUIRenderContext::$beginSharedMeshBatch,
    void,
    ::ComponentRenderBatch& renderBatch
) {
    ++tMeshBatchDepth;
    origin(renderBatch);
}

LL_TYPE_INSTANCE_HOOK(
    AbpEndSharedMeshBatchHook,
    ll::memory::HookPriority::Normal,
    MinecraftUIRenderContext,
    &MinecraftUIRenderContext::$endSharedMeshBatch,
    void,
    ::ComponentRenderBatch& renderBatch
) {
    origin(renderBatch);
    if (tMeshBatchDepth > 0) --tMeshBatchDepth;
    if (tMeshBatchDepth == 0) drawDeferred(*this);
}

LL_TYPE_INSTANCE_HOOK(
    AbpInventoryItemRenderHook,
    ll::memory::HookPriority::Normal,
    InventoryItemRenderer,
    &InventoryItemRenderer::$render,
    void,
    ::MinecraftUIRenderContext& renderContext,
    ::IClientInstance&          client,
    ::UIControl&                owner,
    int                         pass
) {
    auto* previous = tSlotContext;
    tSlotContext   = &renderContext;
    origin(renderContext, client, owner, pass);
    tSlotContext = previous;
}

LL_TYPE_INSTANCE_HOOK(
    AbpInventoryItemDrawHook,
    ll::memory::HookPriority::Normal,
    InventoryItemRenderer,
    &InventoryItemRenderer::_render,
    void,
    ::IInventoryItemRendererRenderContext& renderContext,
    ::InventoryItemRenderOwnerData const&  owner,
    int                                    pass
) {
    ItemStack const& stack = *mItemInstance;
    if (tSlotContext && !mHidingIcon && !stack.isNull() && game::backpackTierOf(stack.getTypeName())) {
        if (pass == 0) {
            float scale = owner.mScale;
            if (tMeshBatchDepth > 0) {
                tDeferred.push_back({stack, owner.mScreenAlignedX, owner.mScreenAlignedY, scale});
            } else {
                ui::Painter p(*tSlotContext);
                drawBackpackIcon(p, stack, owner.mScreenAlignedX, owner.mScreenAlignedY, scale);
                p.flush();
            }
            if (!gSlotLogged) {
                gSlotLogged = true;
                log().info("backpack icons: vanilla slots draw the layered model icon (scale {})", scale);
            }
        }
        return;
    }
    origin(renderContext, owner, pass);
}

class VanillaSlotIcons {
public:
    void install() {
        AbpBeginSharedMeshBatchHook::hook();
        AbpEndSharedMeshBatchHook::hook();
        AbpInventoryItemRenderHook::hook();
        AbpInventoryItemDrawHook::hook();
    }
    void uninstall() {
        AbpInventoryItemRenderHook::unhook();
        AbpInventoryItemDrawHook::unhook();
        AbpBeginSharedMeshBatchHook::unhook();
        AbpEndSharedMeshBatchHook::unhook();
    }
};

} // namespace abp::client::render

ABP_REGISTER_MODULE(abp::client::render::VanillaSlotIcons);


#endif // ABP_CLIENT
