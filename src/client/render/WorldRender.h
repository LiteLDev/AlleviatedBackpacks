#pragma once

// Client-only entities in the world pass (GX-1).
//
// SB draws extra models in the level (the mob catcher's capture effect at AFTER_BLOCK_ENTITIES,
// item entities and item frames through the backpack's baked model). Bedrock draws data-driven
// models through actor renderers; these helpers draw a display entity (EntityPreview.h) with a
// given transform inside the level renderer's pass:
//   - `addWorldPass` registers a callback run every frame right after the entities and block
//     entities (the player level renderer's `renderEntityEffects`), with the pass's render context;
//   - `renderEntityInWorld` draws an actor in that context (or any world-pass context, e.g. inside
//     an actor renderer hook) with a transform in camera-relative world space (blocks).
//
// Client thread only.

#include <functional>
#include <string>

#include "mc/_HeaderOutputPredefine.h" // glm

class Actor;
class BaseActorRenderContext;
class Vec3;

namespace abp::client::render {

using WorldPass = std::function<void(BaseActorRenderContext&)>;
/// Registers (or replaces) a world pass callback.
void addWorldPass(std::string id, WorldPass pass);
void removeWorldPass(std::string const& id);

/// Camera-relative origin of the world pass: world position `p` minus the camera target.
[[nodiscard]] Vec3 toCameraSpace(BaseActorRenderContext const& rc, Vec3 const& p);

/// Draws `actor` (a display entity, see EntityPreview.h) in a world pass: `local` (camera-relative
/// world space, blocks) is applied to the pass's world matrix, then the actor's renderer draws it at
/// the origin facing `yaw` (degrees, Bedrock: 0 = south). `lightAt` (world) is where its lighting is
/// sampled. Faults are caught and logged; returns false once the actor can't be drawn.
/// A client-only entity drawn by the level itself (ILevel::addDisplayEntity, as an item frame's
/// item): the level renders it with the other entities, attachables included (the v2 renderers
/// prepare attachables only for the level's entities, so a free display copy shows none in the
/// world). nullptr without a world. Position it with Actor::moveTo; remove it with
/// removeLevelDisplayEntity before leaving the world.
[[nodiscard]] Actor* addLevelDisplayEntity(std::string const& type, Vec3 const& at);
void                 removeLevelDisplayEntity(Actor& actor);

bool renderEntityInWorld(BaseActorRenderContext& rc, Actor& actor, glm::mat4 const& local, float yaw, Vec3 const* lightAt);

} // namespace abp::client::render
