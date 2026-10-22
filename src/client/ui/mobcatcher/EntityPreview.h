#pragma once

// Live 3D entity previews in the companion UI (Java `InventoryScreen.renderEntityInInventory`).
//
// A preview entity is a client-only display copy (`ActorFactory::clientCreateDisplayActor`, the
// mob spawner renderer's mechanism) that is never added to the level: it neither ticks nor shows
// in the world. The captured entity's synched data (variant, colours, baby/sheared flags, ...) is
// applied to it so it looks like the mob that was captured. It is drawn by the game's actor
// renderer straight into the UI pass.

#include <memory>
#include <string>

#include "nlohmann/json.hpp"

#include "client/ui/Geometry.h"

class Actor;

namespace abp::client::ui {

class Painter;

/// One display entity (client thread only; release before leaving the world).
class DisplayEntity {
public:
    virtual ~DisplayEntity() = default;
    [[nodiscard]] virtual Actor& actor() = 0;
};

/// Creates a display copy of entity `type` ("minecraft:cow") with `syncedData` ([[id, type,
/// value], ...] from the server) applied. nullptr when the type can't be created (no level, an
/// unknown or non-renderable entity).
[[nodiscard]] std::unique_ptr<DisplayEntity> createDisplayEntity(std::string const& type, nlohmann::json const& syncedData);

struct EntityPose {
    float bodyYaw = 0; ///< degrees; 0 faces the viewer
    float headYaw = 0; ///< degrees, relative to the screen like bodyYaw
    float pitch   = 0; ///< head pitch, degrees
};

/// Draws `actor` with its feet centred at (x, bottomY) (GUI units), `scale` GUI units per block
/// (Java `renderEntityInInventory`), clipped to `clip` when given (GUI units; SC's scroll panel
/// scissor). Flushes the painter first. Returns false if rendering failed (the caller should stop
/// trying for this entity).
bool renderEntity(Painter& painter, Actor& actor, float x, float bottomY, float scale, EntityPose const& pose, Rect const* clip = nullptr);

} // namespace abp::client::ui
