#pragma once

// Render hook scaffolding for backpack visuals (client build).
//
// The backpack model itself (P2-11) is data driven - attachables + `query.abp_model`, see
// ModelState.h - and backpack icons are drawn by BackpackIcons.h; these hooks let other units add
// to them.
// The data they need per backpack comes from the server as `core::screen::RenderInfo` (tier,
// main/accent colour, enabled upgrades), cached client-side by uuid (`renderInfoFor`).
//
// Hook points:
//   1. Icon hooks      - every backpack icon drawn by the companion UI (native screen, contents
//                        tooltip), after the layered 3D icon (BackpackIcons.h).
//   2. Carrier hooks   - every client tick for each backpack the local player wears or holds
//                        (upgrade particles, worn-model state).
//   3. Placed hooks    - placed backpack blocks near the player (item display, particles);
//                        registered now, dispatched once the placed-block renderer lands.

#include <cstdint>
#include <functional>
#include <string>

#include "core/render/BackpackModel.h"
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

/// Default backpack colours (SB BackpackWrapper.DEFAULT_MAIN_COLOR / DEFAULT_ACCENT_COLOR).
inline constexpr std::uint32_t kDefaultMain   = core::render::kDefaultMainColor;
inline constexpr std::uint32_t kDefaultAccent = core::render::kDefaultAccentColor;

} // namespace abp::client::render
