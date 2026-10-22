#pragma once

// Client-side render state of backpacks (P2-11), readable from any thread.
//
// The 3D backpack model (attachables on players / mobs / the placed-backpack display entity) is
// data driven: its render controllers pick parts, fluid levels and the battery charge through the
// Molang query `query.abp_model(field)`, which resolves the backpack being rendered (owner actor +
// attachable slot -> item -> uuid) and answers from this snapshot. The two-colour tint (cloth =
// main colour, border = accent colour) goes through the shader's change colours, which
// `BackpackModelRender.cpp` sets while one of our attachables is set up / drawn.
//
// The snapshot is filled on the client thread from `net::renderInfo` (G2's RenderInfo sync) and
// refreshed about once a second for backpacks that were rendered recently.

#include <cstdint>
#include <optional>
#include <string>

#include "core/render/BackpackModel.h"

class ItemStackBase;

namespace abp::client::render {

struct ModelSnapshot {
    int                         tier        = 0;
    std::uint32_t               mainColor   = core::render::kDefaultMainColor;
    std::uint32_t               accentColor = core::render::kDefaultAccentColor;
    core::render::BackpackModel model;
};

/// Snapshot of a backpack (nullopt = unknown yet; asks the server). Thread safe.
[[nodiscard]] std::optional<ModelSnapshot> modelFor(std::string const& uuid);
/// Same for a backpack item (nullopt for non-backpacks and items without uuid).
[[nodiscard]] std::optional<ModelSnapshot> modelFor(ItemStackBase const& item);

/// Fields of `query.abp_model(n)` (render controllers / animations of the backpack attachables).
enum class ModelField : int {
    Known       = 0,  ///< 1 when the backpack's render info arrived
    Context     = 1,  ///< 1 worn, 2 main hand, 3 off hand, 4 placed (display entity), 0 other
    Battery     = 2,  ///< battery part instead of the front pouch
    LeftTank    = 3,
    RightTank   = 4,
    Charge      = 5,  ///< 0..kChargeSteps
    LeftLevel   = 6,  ///< 0..kFluidSteps
    RightLevel  = 7,
    LeftUp      = 8,  ///< lighter-than-air fluid (fills from the top)
    RightUp     = 9,
    LeftLook    = 10, ///< core::render::FluidLook
    RightLook   = 11,
    HasDisplay  = 12, ///< an item display is set
    DisplayRot  = 13, ///< item display rotation (degrees)
    DisplayZ    = 14, ///< item display z offset (item pixels, SC -16..16)
};

/// Entity id of the placed-backpack display entity.
inline constexpr std::string_view kDisplayEntityId = "alleviatedbackpacks:backpack_display";

} // namespace abp::client::render
