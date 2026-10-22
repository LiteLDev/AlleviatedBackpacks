#ifdef ABP_CLIENT

#include "upgrade/smithing/ArmorStandPreview.h"

#include <memory>
#include <string>
#include <string_view>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/multiplayer/ClientLevel.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/item/ItemStack.h"

#include "client/Client.h"
#include "client/render/EntityPreview.h"
#include "client/ui/gfx/Painter.h"
#include "game/ItemConv.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace abp::upgrade::smithing::client {

namespace {

namespace render = ::abp::client::render;

/// SB SmithingUpgradeTab: the stand's body at 210 degrees, i.e. turned 30 degrees from facing the
/// viewer, and SmithingScreen.ARMOR_STAND_ANGLE: seen 25 degrees from above.
constexpr render::EntityPose kPose{.bodyYaw = 30.f, .headYaw = 30.f, .pitch = 0.f, .viewPitch = 25.f};

/// The display armor stand (client thread): created on first use, released when the world is left.
struct Stand {
    std::unique_ptr<render::DisplayEntity> entity;
    ClientLevel*                           level = nullptr;
    std::string                            equipped; ///< identity of what it wears ("" = nothing)
    bool                                   failed = false;
};
Stand gStand;

void release() {
    gStand.entity.reset(); // hands the entity to the level's garbage collector
    gStand = Stand{};
}

Actor* standActor() {
    auto* c     = ::abp::client::currentClient();
    auto* level = c ? c->getLevel() : nullptr;
    if (!level) return nullptr;
    if (gStand.level && gStand.level != level) {
        // Another world: the old entity's registry is gone, never touch it again.
        (void)gStand.entity.release(); // NOLINT: intentional leak of a dead handle
        gStand = Stand{};
    }
    if (gStand.failed) return nullptr;
    if (!gStand.entity) {
        gStand.level  = level;
        gStand.entity = render::createDisplayEntity("minecraft:armor_stand");
        if (!gStand.entity) {
            gStand.failed = true;
            log().warn("smithing preview: could not create the armor stand");
            return nullptr;
        }
        log().info("smithing preview: armor stand display entity ready");
    }
    return &gStand.entity->actor();
}

std::string identity(ItemStack const* item) {
    if (!item || item->isNull()) return {};
    auto d = game::ItemConv::toData(*item);
    return d.id + "|" + std::to_string(d.aux) + "|" + std::to_string(d.nbtHash());
}

/// SB updateArmorStandPreview: clear every slot, then the result in its equipment slot (armor) or
/// the off hand (anything else).
void equip(Actor& actor, ItemStack const* item) {
    auto id = identity(item);
    if (id == gStand.equipped) return;
    gStand.equipped = id;
    using SharedTypes::Legacy::ArmorSlot;
    for (auto slot : {ArmorSlot::Head, ArmorSlot::Torso, ArmorSlot::Legs, ArmorSlot::Feet}) actor.setArmor(slot, ItemStack::EMPTY_ITEM());
    actor.setOffhandSlot(ItemStack::EMPTY_ITEM());
    if (id.empty()) return;
    switch (standSlotFor(*item)) {
    case StandSlot::Head:
        actor.setArmor(ArmorSlot::Head, *item);
        break;
    case StandSlot::Chest:
        actor.setArmor(ArmorSlot::Torso, *item);
        break;
    case StandSlot::Legs:
        actor.setArmor(ArmorSlot::Legs, *item);
        break;
    case StandSlot::Feet:
        actor.setArmor(ArmorSlot::Feet, *item);
        break;
    case StandSlot::Offhand:
        actor.setOffhandSlot(*item);
        break;
    }
}

class StandLifetime {
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

StandSlot standSlotFor(ItemStack const& stack) {
    auto id   = stack.isNull() ? std::string{} : stack.getTypeName();
    auto ends = [&](std::string_view s) { return id.size() >= s.size() && id.compare(id.size() - s.size(), s.size(), s) == 0; };
    if (ends("_helmet") || ends("_skull") || ends("_head") || id == "minecraft:carved_pumpkin") return StandSlot::Head;
    if (ends("_chestplate") || id == "minecraft:elytra") return StandSlot::Chest;
    if (ends("_leggings")) return StandSlot::Legs;
    if (ends("_boots")) return StandSlot::Feet;
    return StandSlot::Offhand;
}

void drawArmorStand(::abp::client::ui::Painter& p, float feetX, float feetY, float scale, ItemStack const* item) {
    auto* actor = standActor();
    if (!actor) return;
    equip(*actor, item);
    if (!render::renderEntity(p, *actor, feetX, feetY, scale, kPose)) {
        if (!gStand.failed) log().warn("smithing preview: the armor stand can't be drawn");
        gStand.failed = true;
    }
}

} // namespace abp::upgrade::smithing::client

ABP_REGISTER_MODULE(abp::upgrade::smithing::client::StandLifetime);

#endif // ABP_CLIENT
