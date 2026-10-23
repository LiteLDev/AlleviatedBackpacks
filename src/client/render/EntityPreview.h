#pragma once

// Live 3D entity previews in the companion UI (Java `InventoryScreen.renderEntityInInventory`).
// Reusable: the Mob Catcher draws captured mobs with it; other units can draw any entity (e.g. an
// armor stand wearing an item) in a UI panel.
//
//   auto e = render::createDisplayEntity("minecraft:cow", syncedData);   // once, keep it
//   render::renderEntity(painter, e->actor(), x, bottomY, scale, pose, &clip); // every frame
//
// A display entity is a client-only copy (`ActorFactory::clientCreateDisplayActor`, as the mob
// spawner's display entity) that is never added to the level: it neither ticks nor shows in the
// world. Synched data from the server (variant, colours, baby/sheared flags, ...) can be applied so
// it looks like a real entity. It is drawn through the same path as vanilla's UI actor renderers
// (paper doll): the actor's renderer is looked up by its renderer id and `ActorRenderer::renderGui`
// draws it with an `ActorRenderData` marked "in UI", in the screen's world matrix.
//
// Client thread only. Display entities must be released before the world is left (the owner
// hands them to the level's garbage collector).

#include <memory>
#include <string>

#include "nlohmann/json.hpp"

#include "client/ui/Geometry.h"

class Actor;

namespace abp::client::ui {
class Painter;
}

namespace abp::client::render {

class DisplayEntity {
public:
    virtual ~DisplayEntity() = default;
    [[nodiscard]] virtual Actor& actor() = 0;
};

/// Creates a display copy of entity `type` ("minecraft:cow") with `syncedData` ([[id, dataType,
/// value], ...], e.g. from the server's `SynchedActorData::packAll`) applied. nullptr when there is
/// no level or the type can't be created.
[[nodiscard]] std::unique_ptr<DisplayEntity> createDisplayEntity(std::string const& type, nlohmann::json const& syncedData = nlohmann::json::array());

/// Applies actor properties (e.g. `minecraft:climate_variant`) to a display entity: `propertiesNbt` is
/// the base64 binary NBT of an entity's saved "properties" compound. False when it can't be applied.
bool applyProperties(Actor& actor, std::string const& propertiesNbt);

struct EntityPose {
    float bodyYaw = 0; ///< degrees; 0 faces the viewer
    float headYaw = 0; ///< degrees, same reference as bodyYaw
    float pitch   = 0; ///< head pitch, degrees
    /// Degrees the whole model is tilted towards the viewer about the screen's horizontal axis,
    /// i.e. seen from above (SB SmithingScreen.ARMOR_STAND_ANGLE: 25 degrees). 0 = side view.
    float viewPitch = 0;
    /// Height (blocks above the feet) of the point the model turns about; that point is drawn at
    /// (x, bottomY). 0 = the feet (SB's entity previews); 0.5 = a block model's centre (GX-1, SC's
    /// item display preview turns the backpack about its centre).
    float pivotY = 0;
};

/// Draws `actor` with its feet centred at (x, bottomY) (GUI units), `scale` GUI units per block,
/// clipped to `clip` when given (GUI units). Flushes the painter first. A renderer fault is caught
/// and logged with its stage; the next frame tries the next path. Returns false once the entity
/// can't be drawn by any path (it is not tried again).
bool renderEntity(ui::Painter& painter, Actor& actor, float x, float bottomY, float scale, EntityPose const& pose, ui::Rect const* clip = nullptr);

} // namespace abp::client::render
