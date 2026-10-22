#pragma once

// Render hook scaffolding for backpack visuals (client build).
//
// Later units draw Sophisticated Backpacks' backpack model (parts per installed upgrade, two-colour
// tint), upgrade particles and the item display of placed backpacks through these hook points.
// The data they need per backpack comes from the server as `core::screen::RenderInfo` (tier,
// main/accent colour, enabled upgrades), cached client-side by uuid (`renderInfoFor`).
//
// Hook points:
//   1. Icon hooks      - every backpack icon drawn by the companion UI (native screen, contents
//                        tooltip). Built in: "abp:tint" draws the tier's grey "_dyed" texture
//                        multiplied by the backpack's main colour (the two-colour tint; proof that
//                        the hook works).
//   2. Carrier hooks   - every client tick for each backpack the local player wears or holds
//                        (upgrade particles, worn-model state).
//   3. Placed hooks    - placed backpack blocks near the player (item display, particles);
//                        registered now, dispatched once the placed-block renderer lands.

#include <cstdint>
#include <functional>
#include <string>

#include "core/screen/ScreenModel.h"

class ItemStack;
class Player;

namespace abp::client::ui {
class Painter;
}

namespace abp::client::render {

/// Render info of the backpack an item is bound to (requests it; nullptr until it arrived or for
/// items without a uuid).
[[nodiscard]] core::screen::RenderInfo const* renderInfoFor(ItemStack const& backpackItem);

struct IconContext {
    ui::Painter&                    painter;
    ItemStack const&                stack;
    float                           x, y, scale;
    core::screen::RenderInfo const& info;
};
/// Draws over (or instead of) a backpack icon. Return true to stop later hooks.
using IconHook = std::function<bool(IconContext const&)>;

struct CarrierContext {
    Player&                         player;
    ItemStack const&                stack;
    core::screen::RenderInfo const& info;
    bool                            worn; ///< chest slot (else hand)
};
using CarrierHook = std::function<void(CarrierContext const&)>;

struct PlacedContext {
    int                             x, y, z;
    core::screen::RenderInfo const& info;
};
using PlacedHook = std::function<void(PlacedContext const&)>;

/// Registration (any time; `order` ascending). Ids are unique: re-adding replaces.
void addIconHook(std::string id, int order, IconHook hook);
void addCarrierHook(std::string id, CarrierHook hook);
void addPlacedHook(std::string id, PlacedHook hook);
void removeHook(std::string const& id);

/// Called by the companion UI after drawing an item icon at (x, y); no-op for non-backpacks.
void decorateItemIcon(ui::Painter& painter, ItemStack const& stack, float x, float y, float scale = 1);
/// Dispatches placed-block hooks (for the placed-backpack renderer).
void dispatchPlaced(PlacedContext const& ctx);

/// Default backpack colours (no tint applied by the built-in hook).
inline constexpr std::uint32_t kDefaultMain   = 0xFFCC613A;
inline constexpr std::uint32_t kDefaultAccent = 0xFF622E1A;

} // namespace abp::client::render
