#ifdef ABP_CLIENT

#include "client/render/BackpackPreview.h"

#include <memory>
#include <string>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/multiplayer/ClientLevel.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/item/ItemStack.h"

#include "client/Client.h"
#include "client/render/EntityPreview.h"
#include "client/render/ModelState.h"
#include "client/ui/Geometry.h"
#include "client/ui/gfx/Painter.h"
#include "feature/model/DebugPose.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace abp::client::render {

namespace {

/// The preview's display entity (client thread): created on first use, released with the world.
struct Preview {
    std::unique_ptr<DisplayEntity> entity;
    ClientLevel*                   level = nullptr;
    std::string                    worn;    ///< identity of the backpack it wears
    std::string                    display; ///< identity of the display item it holds
    bool                           failed = false;
};
Preview gPreview;

void release() {
    gPreview.entity.reset(); // hands the entity to the level's garbage collector
    gPreview = Preview{};
}

std::string identity(ItemStack const& item) {
    if (item.isNull()) return {};
    auto d = game::ItemConv::toData(item);
    return d.id + "|" + std::to_string(d.aux) + "|" + std::to_string(d.nbtHash());
}

Actor* previewActor() {
    auto* c     = currentClient();
    auto* level = c ? c->getLevel() : nullptr;
    if (!level) return nullptr;
    if (gPreview.level && gPreview.level != level) {
        // Another world: the old entity's registry is gone, never touch it again.
        (void)gPreview.entity.release(); // NOLINT: intentional leak of a dead handle
        gPreview = Preview{};
    }
    if (gPreview.failed) return nullptr;
    if (!gPreview.entity) {
        gPreview.level  = level;
        gPreview.entity = createDisplayEntity(std::string(kDisplayEntityId));
        if (!gPreview.entity) {
            gPreview.failed = true;
            log().warn("backpack preview: could not create the display entity");
            return nullptr;
        }
        log().info("backpack preview: display entity ready");
    }
    return &gPreview.entity->actor();
}

/// Wears `stack` and holds its display item (SB BackpackItemStackRenderer draws both).
void equip(Actor& actor, ItemStack const& stack) {
    if (auto id = identity(stack); id != gPreview.worn) {
        gPreview.worn = id;
        actor.setArmor(SharedTypes::Legacy::ArmorSlot::Torso, stack);
    }
    ItemStack display;
    if (auto snap = modelFor(stack); snap && snap->model.display && !snap->model.display->item.empty()) {
        display = game::ItemConv::toStack(snap->model.display->item, 1);
    }
    if (auto id = identity(display); id != gPreview.display) {
        gPreview.display = id;
        actor.setCarriedItem(display.isNull() ? ItemStack::EMPTY_ITEM() : display);
    }
}

class PreviewLifetime {
public:
    void install() {
        mExit = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientExitLevelEvent>([](auto&) { release(); });
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mExit);
        release();
    }

private:
    ll::event::ListenerPtr mExit;
};

} // namespace

bool drawBackpackModel(ui::Painter& p, ItemStack const& stack, float cx, float cy, float scale, float xRot, float yRot, ui::Rect const* clip) {
    if (stack.isNull() || !game::isBackpack(stack)) return false;
    auto* actor = previewActor();
    if (!actor) return false;
    equip(*actor, stack);
    // The display entity draws the backpack as placed: the block model with its front facing the
    // entity's yaw, its centre half a block above the entity's feet (the model's 0..1 block cube
    // turned about (0.5, 0.5, 0.5), as SC translates by -0.5 after rotating). Calibrated in the
    // client (e2e `rendering`, preview-cal-*): pose yaw 180 shows the front like SB's
    // yAxisRotation + 180 = 180, the same direction of turn, positive x seen from above.
    EntityPose pose;
    pose.bodyYaw   = yRot;
    pose.headYaw   = pose.bodyYaw;
    pose.viewPitch = xRot;
    pose.pivotY    = 0.5f;
    // e2e calibration (local worlds): `/backpack modeltest pose "0 yawSign yawOffset pitchSign pivot
    // scale dx dy"` (DebugPose context 0 is not used by the model animations).
    if (auto& cal = model::debugPose(0); cal.on) {
        pose.bodyYaw   = cal.v[0] * yRot + cal.v[1];
        pose.headYaw   = pose.bodyYaw;
        pose.viewPitch = cal.v[2] * xRot;
        pose.pivotY    = cal.v[3];
        scale *= cal.v[4];
        cx += cal.v[5];
        cy += cal.v[6];
    }
    if (!renderEntity(p, *actor, cx, cy, scale, pose, clip)) {
        if (!gPreview.failed) log().warn("backpack preview: the display entity can't be drawn");
        gPreview.failed = true;
        return false;
    }
    return true;
}

} // namespace abp::client::render

ABP_REGISTER_MODULE(abp::client::render::PreviewLifetime);

#endif // ABP_CLIENT
