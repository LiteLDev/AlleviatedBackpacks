#ifdef ABP_CLIENT

#include "client/ui/mobcatcher/EntityPreview.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>

#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/multiplayer/ClientLevel.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/actor/ActorRenderDispatcher.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/deps/core/math/Matrix.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/input/RectangleArea.h"
#include "mc/deps/ecs/gamerefs_entity/GameRefsEntity.h"
#include "mc/deps/game_refs/OwnerPtr.h"
#include "mc/deps/renderer/Camera.h"
#include "mc/deps/renderer/MatrixStack.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorFactory.h"
#include "mc/world/actor/BuiltInActorComponents.h"
#include "mc/entity/components/ActorRotationComponent.h"
#include "mc/world/actor/DataItem.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/SynchedActorDataEntityWrapper.h"
#include "mc/world/level/ActorGarbageCollector.h"
#include "mc/world/level/BlockPos.h"

#include "client/Client.h"
#include "client/ui/gfx/Painter.h"
#include "mod/Config.h"
#include "mod/Log.h"

namespace abp::client::ui {

namespace {

class DisplayEntityImpl final : public DisplayEntity {
public:
    DisplayEntityImpl(OwnerPtr<EntityContext> owner, Actor* actor) : mOwner(std::move(owner)), mActor(actor) {}
    ~DisplayEntityImpl() override {
        // Hand the entity to the level's garbage collector (the game's way to drop an entity that
        // is not in the world); without a level the registry is gone with it anyway.
        try {
            auto* c = currentClient();
            if (auto* level = c ? c->getLevel() : nullptr; level && mOwner) {
                level->mActorGarbageCollector->garbageCollectEntity(std::move(mOwner));
            }
        } catch (...) {
        }
    }
    DisplayEntityImpl(DisplayEntityImpl const&)            = delete;
    DisplayEntityImpl& operator=(DisplayEntityImpl const&) = delete;

    [[nodiscard]] Actor& actor() override { return *mActor; }

private:
    OwnerPtr<EntityContext> mOwner;
    Actor*                  mActor;
};

std::unique_ptr<DataItem> toDataItem(nlohmann::json const& e) {
    if (!e.is_array() || e.size() != 3 || !e[0].is_number_integer() || !e[1].is_number_integer()) return nullptr;
    auto        id   = static_cast<ushort>(e[0].get<int>());
    auto const& v    = e[2];
    switch (static_cast<DataItemType>(e[1].get<int>())) {
    case DataItemType::Byte:
        return v.is_number() ? DataItem::create(id, static_cast<schar>(v.get<int>())) : nullptr;
    case DataItemType::Short:
        return v.is_number() ? DataItem::create(id, static_cast<short>(v.get<int>())) : nullptr;
    case DataItemType::Int:
        return v.is_number() ? DataItem::create(id, v.get<int>()) : nullptr;
    case DataItemType::Float:
        return v.is_number() ? DataItem::create(id, v.get<float>()) : nullptr;
    case DataItemType::String:
        return v.is_string() ? DataItem::create(id, v.get<std::string>()) : nullptr;
    case DataItemType::Int64:
        return v.is_number() ? DataItem::create(id, static_cast<int64>(v.get<std::int64_t>())) : nullptr;
    case DataItemType::Pos:
        if (v.is_array() && v.size() == 3) return DataItem::create(id, BlockPos{v[0].get<int>(), v[1].get<int>(), v[2].get<int>()});
        return nullptr;
    case DataItemType::Vec3:
        if (v.is_array() && v.size() == 3) return DataItem::create(id, Vec3{v[0].get<float>(), v[1].get<float>(), v[2].get<float>()});
        return nullptr;
    default:
        return nullptr;
    }
}

/// Calibration knobs for the UI transform (dev only: read from `config/entity_preview_debug.json`
/// when present).
struct Tuning {
    float z        = 0;     ///< depth of the entity origin in UI space
    float zScale   = 1;     ///< depth scale factor (relative to the GUI scale)
    float yawShift = 0;     ///< added to body/head yaw
    bool  lighting = false; ///< false: ignore world lighting
    int   flip     = 0;     ///< 0: rotate 180 deg about X, 1: about Z
};

Tuning const& tuning() {
    static Tuning t;
    static int    frames = 0;
    if (frames-- > 0) return t;
    frames = 60;
    try {
        auto path = abp::config::configDir() / "entity_preview_debug.json";
        if (!std::filesystem::exists(path)) return t;
        std::ifstream in(path);
        auto          j = nlohmann::json::parse(in, nullptr, false);
        if (!j.is_object()) return t;
        t.z        = j.value("z", t.z);
        t.zScale   = j.value("zScale", t.zScale);
        t.yawShift = j.value("yawShift", t.yawShift);
        t.lighting = j.value("lighting", t.lighting);
        t.flip     = j.value("flip", t.flip);
    } catch (...) {
    }
    return t;
}

} // namespace

std::unique_ptr<DisplayEntity> createDisplayEntity(std::string const& type, nlohmann::json const& syncedData) {
    auto* client = currentClient();
    auto* level  = client ? client->getLevel() : nullptr;
    if (!level || type.empty()) return nullptr;
    OwnerPtr<EntityContext> owner =
        level->getActorFactory().clientCreateDisplayActor(ActorDefinitionIdentifier(type), nullptr, Vec3{0.f, 0.f, 0.f}, Vec2{0.f, 0.f});
    auto actor = owner.tryUnwrap();
    if (!actor) return nullptr;
    auto entity = std::make_unique<DisplayEntityImpl>(std::move(owner), actor.as_ptr());
    if (syncedData.is_array() && !syncedData.empty()) {
        std::vector<std::unique_ptr<DataItem>> items;
        for (auto const& e : syncedData) {
            try {
                if (auto item = toDataItem(e)) items.push_back(std::move(item));
            } catch (...) {
            }
        }
        if (!items.empty()) actor->mEntityData->assignValues(items, *actor, std::nullopt);
    }
    return entity;
}

bool renderEntity(Painter& painter, Actor& actor, float x, float bottomY, float scale, EntityPose const& pose, Rect const* clip) {
    painter.flush();
    try {
        auto&       ui     = painter.raw();
        auto&       client = static_cast<IClientInstance&>(ui.mClient);
        auto&       screen = static_cast<ScreenContext&>(ui.mScreenContext);
        auto const& t      = tuning();
        auto        dispatcher = client.getEntityRenderDispatcher();
        if (!dispatcher) return false;

        // Pose (SB prepareEntityForRender): body and head yaw, head pitch.
        float bodyYaw = pose.bodyYaw + t.yawShift;
        float headYaw = pose.headYaw + t.yawShift;
        auto& rot      = actor.mBuiltInComponents->mActorRotationComponent;
        *rot->mRot     = Vec2{pose.pitch, headYaw};
        *rot->mRotPrev = Vec2{pose.pitch, headYaw};
        actor.setYHeadRotations(headYaw, headYaw);
        if (auto* mob = Mob::tryGetFromEntity(actor.getEntityContext(), false)) mob->setYBodyRotations(bodyYaw, bodyYaw);

        BaseActorRenderContext rc(screen, client, client.getMinecraftGame_DEPRECATED());
        mce::Camera&           camera = screen.camera;
        auto                   ref    = camera.worldMatrixStack->push(false);
        Matrix&                m      = *ref.mat;
        glm::mat4              local  = glm::translate(glm::mat4(1.f), glm::vec3(x, bottomY, t.z));
        local = glm::scale(local, glm::vec3(scale, scale, scale * t.zScale));
        local = glm::rotate(local, glm::pi<float>(), t.flip == 1 ? glm::vec3(0, 0, 1) : glm::vec3(1, 0, 0));
        *m._m = *m._m * local;
        camera.worldMatrixStack->_isDirty = true;
        if (clip) {
            ui.saveCurrentClippingRectangle();
            RectangleArea area{};
            area._x0 = clip->x;
            area._x1 = clip->right();
            area._y0 = clip->y;
            area._y1 = clip->bottom();
            ui.setClippingRectangle(area);
        }
        struct ClipRestore {
            MinecraftUIRenderContext& ui;
            bool                      active;
            ~ClipRestore() {
                if (active) ui.restoreSavedClippingRectangle();
            }
        } restore{ui, clip != nullptr};
        dispatcher->render(rc, actor, Vec3{0.f, 0.f, 0.f}, Vec3{0.f, 0.f, 0.f}, Vec2{pose.pitch, bodyYaw}, !t.lighting);
        return true;
    } catch (std::exception const& e) {
        log().warn("entity preview: rendering {} failed: {}", actor.getTypeName(), e.what());
        return false;
    } catch (...) {
        log().warn("entity preview: rendering {} failed", actor.getTypeName());
        return false;
    }
}

} // namespace abp::client::ui

#endif // ABP_CLIENT
