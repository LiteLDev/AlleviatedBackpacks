#ifdef ABP_CLIENT

#include "client/ui/settings/ItemTransform.h"

#include "mc/client/game/IClientInstance.h"
#include "mc/client/game/IMinecraftGame.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/actor/ItemRenderer.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/deps/core/math/Matrix.h"
#include "mc/deps/minecraft_renderer/renderer/MeshContext.h"
#include "mc/deps/renderer/Camera.h"
#include "mc/deps/renderer/MatrixStack.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"

#include "client/ui/gfx/Painter.h"

namespace abp::client::ui::settings {

void drawItemTransformed(Painter& p, ItemStack const& stack, float cx, float cy, float scale, float rotX, float rotY, float rotZ) {
    if (stack.isNull() || !stack.mItem) return;
    p.flush();
    auto& ctx      = p.raw();
    auto* renderer = ctx.mClient.getItemRenderer();
    if (!renderer) return;
    ::ScreenContext& screen = ctx.mScreenContext;
    ::mce::Camera&   camera = static_cast<::mce::MeshContext&>(screen).camera;
    {
        auto     ref = camera.worldMatrixStack->push(false);
        ::Matrix* m  = ref.mat;
        // Pivot around the icon centre: T(c) * Rx * Ry * Rz * T(-c).
        m->translate(cx, cy, 0.f);
        if (rotX != 0.f) m->rotate(rotX, 1.f, 0.f, 0.f);
        if (rotY != 0.f) m->rotate(rotY, 0.f, 1.f, 0.f);
        if (rotZ != 0.f) m->rotate(rotZ, 0.f, 0.f, 1.f);
        m->translate(-cx, -cy, 0.f);
        BaseActorRenderContext rc(ctx.mScreenContext, ctx.mClient, ctx.mClient.getMinecraftGame_DEPRECATED());
        float x = cx - 8.f * scale, y = cy - 8.f * scale;
        renderer->renderGuiItemNew(rc, stack, 0, x, y, false, 1.f, 1.f, scale, 0);
        if (stack.mItem->isGlint(stack)) renderer->renderGuiItemNew(rc, stack, 0, x, y, true, 1.f, 1.f, scale, 0);
    } // the matrix ref pops on destruction
}

} // namespace abp::client::ui::settings

#endif // ABP_CLIENT
