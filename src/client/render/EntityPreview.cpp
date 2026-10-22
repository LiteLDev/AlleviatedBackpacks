#ifdef ABP_CLIENT

#include "client/render/EntityPreview.h"

#include <cstddef>
#include <excpt.h>
#include <memory>
#include <new>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <windows.h>

#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/multiplayer/ClientLevel.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/actor/ActorRenderData.h"
#include "mc/client/renderer/actor/ActorRenderDispatcher.h"
#include "mc/client/renderer/actor/ActorRenderer.h"
#include "mc/client/renderer/actor/DataDrivenRenderer.h"
#include "mc/client/renderer/actor/v2/DataDrivenRendererV2RequiredData.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/deps/core/math/Matrix.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/ecs/gamerefs_entity/GameRefsEntity.h"
#include "mc/deps/game_refs/OwnerPtr.h"
#include "mc/deps/input/RectangleArea.h"
#include "mc/deps/minecraft_renderer/framebuilder/CustomSurfaceShaderMetadata.h"
#include "mc/deps/renderer/Camera.h"
#include "mc/deps/renderer/MatrixStack.h"
#include "mc/entity/components/ActorRotationComponent.h"
#include "mc/util/MolangVariableMap.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorFactory.h"
#include "mc/world/actor/BuiltInActorComponents.h"
#include "mc/world/actor/DataItem.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/SynchedActorDataEntityWrapper.h"
#include "mc/world/level/ActorGarbageCollector.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/dimension/Dimension.h"

#include "client/Client.h"
#include "client/ui/gfx/Painter.h"
#include "mod/Log.h"

namespace abp::client::render {

namespace {

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
static_assert(offsetof(Actor, mLevel) == 0x1D8, "the humanoid renderer reads Actor::mLevel at +0x1D8");
#pragma clang diagnostic pop

/// Layout of `ActorRenderData` (no constructor is exported; its references are plain pointers in
/// memory). Checked against the game's type below.
struct RenderDataLayout {
    Actor*                                        actor;
    ItemStack const*                              itemStack;
    Vec3                                          position;
    Vec2                                          rotation;
    Vec2                                          headRot;
    bool                                          glint;
    bool                                          ignoreLighting;
    bool                                          isInUI;
    float                                         deltaTime;
    AnimationComponent*                           animationComponent;
    MolangVariableMap*                            variables;
    alignas(4) std::byte                          cssMetadata[8];
};
static_assert(sizeof(RenderDataLayout) == sizeof(ActorRenderData));
static_assert(alignof(RenderDataLayout) == alignof(ActorRenderData));

// ---- the per-entity state and fault reporting ---------------------------------------------------------------

enum class Stage : int { Gui = 0, World = 1, None = 2 };

char const* stageName(int s) {
    switch (s) {
    case 0:
        return "renderGui (UI path)";
    case 1:
        return "dispatcher (world path)";
    default:
        return "none";
    }
}

struct EntityState {
    int  stage   = 0;     ///< path tried next / in use
    bool logged  = false; ///< success logged once
};
std::unordered_map<Actor const*, EntityState>& states() {
    static std::unordered_map<Actor const*, EntityState> m;
    return m;
}

struct Fault {
    unsigned long code    = 0;
    std::uintptr_t address = 0; ///< faulting instruction (RVA when inside the game)
    std::uintptr_t target  = 0; ///< accessed address for access violations
};

int captureFault(EXCEPTION_POINTERS* info, Fault* out) {
    out->code = info->ExceptionRecord->ExceptionCode;
    auto pc   = reinterpret_cast<std::uintptr_t>(info->ExceptionRecord->ExceptionAddress);
    auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    out->address = pc >= base ? pc - base : pc;
    if (out->code == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2) {
        out->target = static_cast<std::uintptr_t>(info->ExceptionRecord->ExceptionInformation[1]);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

/// Vanilla's UI actor path (PaperDollBaseActorRenderContext::renderGui): the actor's renderer
/// draws an `ActorRenderData` marked "in UI" in the current world matrix.
bool sehRenderGui(ActorRenderer* renderer, BaseActorRenderContext* rc, ActorRenderData* data, Fault* fault) {
    __try {
        renderer->renderGui(*rc, *data);
        return true;
    } __except (captureFault(GetExceptionInformation(), fault)) {
        return false;
    }
}

/// The world path: the dispatcher picks the actor's renderer itself.
bool sehRenderWorld(ActorRenderDispatcher* dispatcher, BaseActorRenderContext* rc, Actor* actor, Vec2 const* rot, bool ignoreLighting, Fault* fault) {
    __try {
        dispatcher->render(*rc, *actor, Vec3{0.f, 0.f, 0.f}, Vec3{0.f, 0.f, 0.f}, *rot, ignoreLighting);
        return true;
    } __except (captureFault(GetExceptionInformation(), fault)) {
        return false;
    }
}

class DisplayEntityImpl final : public DisplayEntity {
public:
    DisplayEntityImpl(OwnerPtr<EntityContext> owner, Actor* actor) : mOwner(std::move(owner)), mActor(actor) {}
    ~DisplayEntityImpl() override {
        states().erase(mActor);
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
    auto        id = static_cast<ushort>(e[0].get<int>());
    auto const& v  = e[2];
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

/// The renderer the actor's id names (data-driven first, like the paper doll), or nullptr.
ActorRenderer* rendererFor(ActorRenderDispatcher& dispatcher, Actor& actor, std::shared_ptr<ActorRenderer>& keep) {
    HashedString const& id = actor.getActorRendererId();
    if (auto dd = dispatcher.getDataDrivenRenderer(id)) keep = dd;
    else keep = dispatcher.getRenderer(id);
    if (!keep) keep = dispatcher._getRenderer(actor);
    return keep.get();
}

} // namespace

std::unique_ptr<DisplayEntity> createDisplayEntity(std::string const& type, nlohmann::json const& syncedData) {
    auto* client = currentClient();
    auto* level  = client ? client->getLevel() : nullptr;
    auto* player = currentPlayer();
    if (!level || !player || type.empty()) return nullptr;
    // Like the mob spawner's display entity: created next to an entity of the world (the local
    // player), so it has a dimension to take lighting / biome data from while rendering.
    OwnerPtr<EntityContext> owner =
        level->getActorFactory().clientCreateDisplayActor(ActorDefinitionIdentifier(type), player, player->getPosition(), Vec2{0.f, 0.f});
    auto actor = owner.tryUnwrap();
    if (!actor) return nullptr;
    actor->setDimension(player->getDimension().weak_from_this());
    // A display copy is never added to the level, so its level pointer stays null; humanoid
    // renderers read the level's data (version-gated geometry/animation choices) through it and
    // fault on a null level (Minecraft.Windows.exe+0x1C38A59: `mov rcx,[actor+0x1D8]; mov rax,[rcx]`).
    actor->mLevel = static_cast<ILevel*>(level);
    auto entity = std::make_unique<DisplayEntityImpl>(std::move(owner), actor.as_ptr());
    // Actors added to the level get their renderer id from the client's entity definitions; a
    // display copy gets it from the same query.
    if (actor->getActorRendererId().getString().empty()) {
        auto const& query = actor->queryEntityRenderer();
        actor->setActorRendererId(query.getString().empty() ? HashedString(type) : query);
    }
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

bool renderEntity(ui::Painter& painter, Actor& actor, float x, float bottomY, float scale, EntityPose const& pose, ui::Rect const* clip) {
    auto& state = states()[&actor];
    if (state.stage >= static_cast<int>(Stage::None)) return false;
    painter.flush();
    auto&       ui         = painter.raw();
    auto&       client     = static_cast<IClientInstance&>(ui.mClient);
    auto&       screen     = static_cast<ScreenContext&>(ui.mScreenContext);
    auto        dispatcher = client.getEntityRenderDispatcher();
    if (!dispatcher) return false;

    // Pose (SB prepareEntityForRender): body and head yaw, head pitch.
    // With the transform below, yaw 180 faces the viewer (SB's preview faces the screen).
    float bodyYaw  = pose.bodyYaw + 180.f;
    float headYaw  = pose.headYaw + 180.f;
    auto& rot      = actor.mBuiltInComponents->mActorRotationComponent;
    *rot->mRot     = Vec2{pose.pitch, headYaw};
    *rot->mRotPrev = Vec2{pose.pitch, headYaw};
    actor.setYHeadRotations(headYaw, headYaw);
    if (auto* mob = Mob::tryGetFromEntity(actor.getEntityContext(), false)) mob->setYBodyRotations(bodyYaw, bodyYaw);

    BaseActorRenderContext rc(screen, client, client.getMinecraftGame_DEPRECATED());
    // Data-driven (v2) actor renderers use the level renderer's processed renderer registry and
    // continuous data; the world pass sets them on its context, a fresh context has neither.
    if (auto* levelRenderer = client.getLevelRenderer()) {
        if (auto* data = levelRenderer->mDataDrivenRendererV2RequiredData.get(); data && data->mEnabled) {
            rc.mProcessedV2DataDrivenRenderers = data->mProcessedData.get();
            rc.mV2DataDrivenContinuousData     = data->mContinuousData.get();
        }
    }
    // Data-driven renderers keep per-actor continuous data (previous transforms, animation state)
    // keyed by the render id; display copies are not in the level, so give each its own.
    rc.mRenderUniqueIdOverride = static_cast<int64>(0x7A00000000000000ll | (reinterpret_cast<std::uintptr_t>(&actor) >> 4));
    std::shared_ptr<ActorRenderer> keep;
    ActorRenderer*                 renderer = rendererFor(*dispatcher, actor, keep);

    mce::Camera& camera = screen.camera;
    auto         ref    = camera.worldMatrixStack->push(false);
    Matrix&      m      = *ref.mat;
    glm::mat4    local  = glm::translate(glm::mat4(1.f), glm::vec3(x, bottomY, 0.f));
    local               = glm::scale(local, glm::vec3(scale, scale, scale)); // GUI units per block
    // Model y is up, the UI y is down: turn the model 180 deg about the view axis (SB ENTITY_RENDER_ANGLE),
    // which keeps its front (+Z) towards the viewer.
    local               = glm::rotate(local, glm::pi<float>(), glm::vec3(0, 0, 1));
    // Seen from above: tilt the top of the model towards the viewer (calibrated in the client:
    // a negative angle about X in this flipped frame).
    if (pose.viewPitch != 0.f) local = glm::rotate(local, -glm::radians(pose.viewPitch), glm::vec3(1, 0, 0));
    *m._m               = *m._m * local;
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

    Fault fault;
    bool  ok    = false;
    int   stage = state.stage;
    if (stage == static_cast<int>(Stage::Gui) && renderer) {
        RenderDataLayout data{};
        data.actor              = &actor;
        data.position           = Vec3{0.f, 0.f, 0.f};
        // ActorRenderData rotations are (yaw, pitch) (unlike ActorRotationComponent's (pitch, yaw)).
        data.rotation           = Vec2{bodyYaw, pose.pitch};
        data.headRot            = Vec2{headYaw, pose.pitch};
        data.ignoreLighting     = true;
        data.isInUI             = true;
        data.deltaTime          = 0.05f;
        data.animationComponent = &actor.getAnimationComponent();
        data.variables          = &*actor.mMolangVariables;
        ok = sehRenderGui(renderer, &rc, reinterpret_cast<ActorRenderData*>(&data), &fault);
    } else if (stage == static_cast<int>(Stage::Gui)) {
        stage = static_cast<int>(Stage::World); // no named renderer: the dispatcher's own lookup
    }
    if (stage == static_cast<int>(Stage::World)) {
        Vec2 rotation{pose.pitch, bodyYaw};
        ok = sehRenderWorld(dispatcher.get(), &rc, &actor, &rotation, true, &fault);
    }
    if (clip) ui.restoreSavedClippingRectangle();

    if (!ok) {
        log().warn(
            "entity preview: {} faulted in {} (renderer '{}' {}, v2 data {}): exception 0x{:08X} at Minecraft.Windows.exe+0x{:X}, address 0x{:X}",
            actor.getTypeName(),
            stageName(stage),
            actor.getActorRendererId().getString(),
            renderer ? "found" : "missing",
            rc.mProcessedV2DataDrivenRenderers ? "set" : "missing",
            fault.code,
            fault.address,
            fault.target
        );
        state.stage = stage + 1; // the next frame tries the next path
        return state.stage < static_cast<int>(Stage::None);
    }
    state.stage = stage;
    if (!state.logged) {
        state.logged = true;
        log().info("entity preview: {} renders via {} (renderer '{}')", actor.getTypeName(), stageName(stage), actor.getActorRendererId().getString());
    }
    return true;
}

} // namespace abp::client::render

#endif // ABP_CLIENT
