#ifdef ABP_CLIENT

// The 3D backpack model on dropped items and in item frames (GX-1).
//
// SB's backpack item has the block model (models/block/backpack.json) as its item model, so item
// entities and item frames draw the 3D backpack with that model's display transforms:
//   ground - ItemEntityRenderer: bob sin((age + partial) / 10 + bobOffs) * 0.1 + 0.1 plus 0.25 x
//            the ground scale, spin (age + partial) / 20 + bobOffs radians, then "ground"
//            (translation 0/3/0 px, scale 0.5);
//   fixed  - ItemFrameRenderer: 1/16 in front of the wall, turned by the frame's rotation (45
//            degree steps), scale 0.5, then "fixed" (translation 0/0/-2.25 px, scale 0.75);
// both followed by translate(-0.5, -0.5, -0.5) to the block model's corner.
//
// Bedrock draws item entities with ItemRenderer (a flat sprite) and the item of an item frame
// with the item-in-hand renderer's mesh (`renderObject`). For backpacks those sprites are skipped
// and a client-only display entity wearing the backpack - the placed backpack's
// `alleviatedbackpacks:backpack_display`, added to the level as a display entity like an item
// frame's item, so the level draws its attachable model - stands at the item (moved and turned
// every frame) with SB's transform on the attachable's xf bone (ModelState.h EntityXf).

#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/multiplayer/ClientLevel.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/actor/ActorRenderData.h"
#include "mc/client/renderer/actor/ItemRenderer.h"
#include "mc/client/renderer/game/ItemInHandRenderer.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/minecraft_renderer/game/ItemContextFlags.h"
#include "mc/deps/minecraft_renderer/game/ItemRenderCall.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/VanillaStates.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/ItemFrameBlockActor.h"
#include "mc/world/phys/AABB.h"

#include "client/Client.h"
#include "client/render/ModelState.h"
#include "client/render/WorldRender.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace abp::client::render {

namespace {

constexpr float kPi = 3.14159265f;

/// A level display entity per item entity / item frame (dropped when not shown for 2 seconds).
struct Entry {
    Actor*      actor = nullptr;
    std::string worn;
    std::string display;
    int         lastTick = 0;
};
struct Cache {
    std::unordered_map<void const*, Entry> entries; ///< by item entity / frame block actor
    ClientLevel*                           level  = nullptr;
    int                                    tick   = 0;
    bool                                   failed = false;
};
Cache gCache;

void drop(Entry& e) {
    if (!e.actor) return;
    setEntityXf(e.actor, std::nullopt);
    removeLevelDisplayEntity(*e.actor);
    e.actor = nullptr;
}

void releaseAll() {
    auto* c = currentClient();
    if (c && c->getLevel() == gCache.level) {
        for (auto& [_, e] : gCache.entries) drop(e);
    }
    gCache = Cache{};
}

std::string identity(ItemStack const& item) {
    if (item.isNull()) return {};
    auto d = game::ItemConv::toData(item);
    return d.id + "|" + std::to_string(d.aux) + "|" + std::to_string(d.nbtHash());
}

/// The display entity for `owner` at `at` (world), wearing `stack` and holding its display item.
Actor* entityFor(void const* owner, ItemStack const& stack, Vec3 const& at) {
    auto* c     = currentClient();
    auto* level = c ? c->getLevel() : nullptr;
    if (!level) return nullptr;
    if (gCache.level && gCache.level != level) gCache = Cache{}; // another world: its entities are gone
    gCache.level = level;
    if (gCache.failed) return nullptr;
    auto& e = gCache.entries[owner];
    if (!e.actor) {
        e.actor = addLevelDisplayEntity(std::string(kDisplayEntityId), at);
        if (!e.actor) {
            gCache.entries.erase(owner);
            gCache.failed = true;
            log().warn("item entity models: could not add the display entity");
            return nullptr;
        }
    }
    e.lastTick  = gCache.tick;
    auto& actor = *e.actor;
    if (auto id = identity(stack); id != e.worn) {
        e.worn = id;
        actor.setArmor(SharedTypes::Legacy::ArmorSlot::Torso, stack);
    }
    ItemStack display;
    if (auto snap = modelFor(stack); snap && snap->model.display && !snap->model.display->item.empty()) {
        display = game::ItemConv::toStack(snap->model.display->item, 1);
    }
    if (auto id = identity(display); id != e.display) {
        e.display = id;
        actor.setCarriedItem(display.isNull() ? ItemStack::EMPTY_ITEM() : display);
    }
    return &actor;
}

/// Places the display entity: at `at` (world) facing `yaw` (Bedrock degrees; the model turns by
/// 180 - yaw, so 180 keeps the block model's orientation, front north), with `xf` on its bone.
void place(Actor& actor, Vec3 const& at, float yaw, EntityXf const& xf) {
    actor.moveTo(at, Vec2{0.f, yaw});
    actor.setYHeadRotations(yaw, yaw);
    setEntityXf(&actor, xf);
}

// ---- ground ---------------------------------------------------------------------------------------

bool gLoggedGround = false;

/// SB ItemEntityRenderer with the "ground" transform. `at` is the item's interpolated position.
void ground(ItemActor& item, float frameAlpha, Vec3 const& at) {
    ItemStack const& stack = *item.mItem;
    auto*            actor = entityFor(&item, stack, at);
    if (!actor) return;
    float age  = static_cast<float>(item.mAge) + frameAlpha;
    float bob  = std::sin(age / 10.f + item.mBobOffs) * 0.1f + 0.1f;
    float spin = (age / 20.f + item.mBobOffs) * 180.f / kPi;
    // translate(0, bob + 0.25 * 0.5) . ground (translate 3 px, scale 0.5) . translate(-0.5) to the
    // block corner = 0.5 x the model (origin at its bottom centre) raised by bob + 1/16.
    EntityXf xf;
    xf.py    = (bob + 0.125f + 0.1875f - 0.25f) * 16.f;
    xf.scale = 0.5f;
    place(*actor, at, 180.f - spin, xf);
    if (!gLoggedGround) {
        gLoggedGround = true;
        log().info("item entity models: a dropped backpack draws its 3D model");
    }
}

// ---- item frames ------------------------------------------------------------------------------------

/// Item frames near the player holding a backpack (scanned every few ticks).
struct FramedBackpack {
    BlockActor const* key;
    BlockPos          pos;
    int               facing; ///< Bedrock facing_direction: 0 down, 1 up, 2 north, 3 south, 4 west, 5 east
    float             rotation;
    ItemStack         stack;
};
std::vector<FramedBackpack> gFrames;
bool                        gLoggedFrame = false;

Vec3 facingVector(int facing) {
    switch (facing) {
    case 0:
        return {0, -1, 0};
    case 1:
        return {0, 1, 0};
    case 2:
        return {0, 0, -1};
    case 3:
        return {0, 0, 1};
    case 4:
        return {-1, 0, 0};
    default:
        return {1, 0, 0};
    }
}

/// SB ItemFrameRenderer with the "fixed" transform: 1/16 off the frame's back, turned by the
/// frame's rotation about the frame's normal, translate(0, 0, -2.25 px) . scale 0.5 x 0.75 .
/// translate(-0.5) = 0.375 x the model with its centre (0, 0.5) on the anchor, pushed 1.125 px
/// out of the wall.
void framed(FramedBackpack const& f) {
    Vec3 d = facingVector(f.facing);
    Vec3 at{static_cast<float>(f.pos.x) + 0.5f - d.x * 0.4375f, static_cast<float>(f.pos.y) + 0.5f - d.y * 0.4375f,
            static_cast<float>(f.pos.z) + 0.5f - d.z * 0.4375f};
    auto* actor = entityFor(f.key, f.stack, at);
    if (!actor) return;
    EntityXf xf;
    xf.scale  = 0.375f;
    float yaw = 180.f;
    float t   = f.rotation * kPi / 180.f;
    float cy = -0.5f * 0.375f, cz = -0.0703125f; // the scaled centre's offset, the push out of the wall
    if (f.facing >= 2) {
        // On a wall: facing the frame's direction, turned about the model's front axis (the bone
        // turns about the model origin, so the centre's offset turns too).
        static constexpr float kYaw[6] = {0, 0, 180, 0, 90, -90};
        yaw   = kYaw[f.facing];
        xf.rz = -f.rotation;
        xf.px = std::sin(t) * cy * 16.f; // geometry x is mirrored
        xf.py = std::cos(t) * cy * 16.f;
        xf.pz = cz * 16.f;
    } else {
        // On the floor (or ceiling): lying on its back (front up / down), turned about the normal.
        float s = f.facing == 1 ? 1.f : -1.f;
        yaw     = 180.f - f.rotation;
        xf.rx   = -90.f * s;
        xf.py   = -cz * s * 16.f;
        xf.pz   = cy * s * 16.f;
    }
    place(*actor, at, yaw, xf);
    if (!gLoggedFrame) {
        gLoggedFrame = true;
        log().info("item entity models: a framed backpack draws its 3D model (frame {},{},{} facing {} rotation {})", f.pos.x, f.pos.y, f.pos.z, f.facing, f.rotation);
    }
}

void scanFrames() {
    gFrames.clear();
    auto* player = currentPlayer();
    if (!player) return;
    auto& region = player->getDimensionBlockSource();
    auto  p      = player->getPosition();
    AABB  box{Vec3{p.x - 48.f, p.y - 48.f, p.z - 48.f}, Vec3{p.x + 48.f, p.y + 48.f, p.z + 48.f}};
    for (auto type : {BlockActorType::ItemFrame, BlockActorType::GlowItemFrame}) {
        for (auto* be : region.fetchBlockEntities(type, box)) {
            if (!be) continue;
            auto const& frame = static_cast<ItemFrameBlockActor const&>(*be);
            auto const& item  = static_cast<ItemStackBase const&>(frame.mItem);
            if (item.isNull() || !game::backpackTierOf(item.getTypeName())) continue;
            ItemStack stack = game::ItemConv::make(item.getTypeName(), 1, item.getAuxValue());
            if (item.mUserData) stack.setUserData(item.mUserData->clone());
            int facing = region.getBlock(be->getPosition()).getState<int>(VanillaStates::FacingDirection()).value_or(2);
            gFrames.push_back({be, be->getPosition(), facing, frame.mRotation, std::move(stack)});
        }
    }
    for (auto const& f : gFrames) framed(f);
}

/// The item-in-hand renderer's mesh of the backpack item asked for last on this thread: the frame
/// renderer draws a framed item's flat sprite with it (`renderObject`), skipped while framed
/// backpacks are shown as models.
thread_local ItemRenderCall const* tBackpackCall = nullptr;
bool                               gLoggedSprite = false;

} // namespace

LL_TYPE_INSTANCE_HOOK(
    AbpItemEntityRenderHook,
    ll::memory::HookPriority::Normal,
    ItemRenderer,
    &ItemRenderer::$render,
    void,
    ::BaseActorRenderContext& renderContext,
    ::ActorRenderData&        renderData
) {
    if (Actor* actor = renderData.mActor) {
        auto* item = ItemActor::tryGetFromEntity(actor->getEntityContext(), false);
        if (item && !item->mIsInItemFrame) {
            ItemStack const& stack = *item->mItem;
            if (!stack.isNull() && game::backpackTierOf(stack.getTypeName()) && !gCache.failed) {
                // The render position is camera relative: back to world space.
                Vec3 const& rel = renderData.position;
                Vec3        cam = toCameraSpace(renderContext, Vec3{0.f, 0.f, 0.f});
                ground(*item, renderContext.getFrameAlpha(*item), Vec3{rel.x - cam.x, rel.y - cam.y, rel.z - cam.z});
                return;
            }
        }
    }
    origin(renderContext, renderData);
}

LL_TYPE_INSTANCE_HOOK(
    AbpGetRenderCallHook,
    ll::memory::HookPriority::Normal,
    ItemInHandRenderer,
    &ItemInHandRenderer::getRenderCallAtFrame,
    ::ItemRenderCall const&,
    ::BaseActorRenderContext& renderContext,
    ::ItemStack const&        item,
    int                       frame
) {
    auto const& call = origin(renderContext, item, frame);
    tBackpackCall    = !item.isNull() && game::backpackTierOf(item.getTypeName()) ? &call : nullptr;
    return call;
}

LL_TYPE_INSTANCE_HOOK(
    AbpRenderObjectHook,
    ll::memory::HookPriority::Normal,
    ItemInHandRenderer,
    &ItemInHandRenderer::renderObject,
    void,
    ::BaseActorRenderContext&       renderContext,
    ::ItemRenderCall const&         renderObject,
    ::dragon::RenderMetadata const& renderMetadata,
    ::ItemContextFlags              itemFlags
) {
    // A world-pass item outside any hand or UI: an item frame's. Backpacks there are models.
    constexpr unsigned kHandOrUi = static_cast<unsigned>(ItemContextFlags::InHand) | static_cast<unsigned>(ItemContextFlags::FirstPersonPass)
                                 | static_cast<unsigned>(ItemContextFlags::UIPass);
    if (tBackpackCall == &renderObject && (static_cast<unsigned>(itemFlags) & kHandOrUi) == 0 && !gFrames.empty() && !gCache.failed) {
        if (!gLoggedSprite) {
            gLoggedSprite = true;
            log().info("item entity models: a framed backpack's sprite is replaced by its model");
        }
        return;
    }
    origin(renderContext, renderObject, renderMetadata, itemFlags);
}

class ItemEntityModels {
public:
    void install() {
        AbpItemEntityRenderHook::hook();
        AbpGetRenderCallHook::hook();
        AbpRenderObjectHook::hook();
        auto& bus = ll::event::EventBus::getInstance();
        mExit     = bus.emplaceListener<ll::event::ClientExitLevelEvent>([](auto&) {
            releaseAll();
            gFrames.clear();
        });
        mTick = bus.emplaceListener<ll::event::ClientLevelTickEvent>([](auto&) {
            ++gCache.tick;
            if (gCache.tick % 5 == 0) scanFrames();
            for (auto const& f : gFrames) {
                if (auto it = gCache.entries.find(f.key); it != gCache.entries.end()) it->second.lastTick = gCache.tick;
            }
            // Item entities / frames not shown for 2 seconds: their display entities go.
            std::erase_if(gCache.entries, [](auto& kv) {
                if (gCache.tick - kv.second.lastTick <= 40) return false;
                drop(kv.second);
                return true;
            });
        });
    }
    void uninstall() {
        AbpItemEntityRenderHook::unhook();
        AbpGetRenderCallHook::unhook();
        AbpRenderObjectHook::unhook();
        auto& bus = ll::event::EventBus::getInstance();
        bus.removeListener(mExit);
        bus.removeListener(mTick);
        releaseAll();
        gFrames.clear();
    }

private:
    ll::event::ListenerPtr mExit;
    ll::event::ListenerPtr mTick;
};

} // namespace abp::client::render

ABP_REGISTER_MODULE(abp::client::render::ItemEntityModels);

#endif // ABP_CLIENT
