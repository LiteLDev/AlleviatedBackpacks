#pragma once

// Render state of a backpack model (P2-11), the Bedrock counterpart of Sophisticated Core's
// `RenderInfo` tank / battery / item display parts. Built on the server, sent to companion clients
// inside `screen::RenderInfo::extra["model"]`, read by the client renderers. Pure (no game types).

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "core/ItemStackData.h"

namespace abp::core::render {

/// SB slices the tank fluid in 10 steps (fluid quads' V span) and the battery charge in 4 steps
/// (charge quad's U span), see BackpackDynamicModel.computeStepsFromModelUV.
inline constexpr int kFluidSteps  = 10;
inline constexpr int kChargeSteps = 4;

/// SB defaults (BackpackWrapper.DEFAULT_MAIN_COLOR / DEFAULT_ACCENT_COLOR), ARGB.
inline constexpr std::uint32_t kDefaultMainColor   = 0xFFCC613A;
inline constexpr std::uint32_t kDefaultAccentColor = 0xFF622E1A;

/// Worn placement (BackpackLayerRenderer.translateRotateAndScale): translate (0, -0.25, z) in the
/// body's space after the 180 degree Y and Z flips, z = -0.35 over a chestplate, -0.3 without;
/// baby mobs scale 0.5 then +1.5 Y.
inline constexpr float kWornYOffset        = -0.25f;
inline constexpr float kWornZOffset        = -0.3f;
inline constexpr float kWornZOffsetArmor   = -0.35f;
inline constexpr float kBabyBodyScale      = 0.5f;
inline constexpr float kBabyBodyYOffset    = 1.5f;
/// Upgrade particles on worn backpacks: 1 in 32 frames (BackpackLayerRenderer.renderUpgrades).
inline constexpr int kUpgradeParticleChance = 32;

/// Fluid look: which fluid texture the tank uses (our textures; SB uses the fluid's still texture).
enum class FluidLook : int { Water = 0, Lava = 1, Milk = 2, PowderSnow = 3, Honey = 4, Experience = 5, Potion = 6 };

struct TankModel {
    std::string   fluid;           ///< fluid id (core ids: "water", "lava", "potion:5", ...)
    float         ratio   = 0;     ///< fill ratio 0..1
    std::uint32_t color   = 0xFFFFFFFF; ///< ARGB tint of the fluid texture
    bool          lighter = false; ///< lighter than air: fills from the top (SB flips the slice)

    [[nodiscard]] bool operator==(TankModel const&) const = default;
};

struct DisplayModel {
    ItemStackData item;          ///< the displayed item (count 1)
    int           rotation = 0;  ///< degrees around the anchor's normal (SC: steps of 45)
    int           zOffset  = 0;  ///< SC MIN_Z_OFFSET..MAX_Z_OFFSET (-16..16), 1 = one item pixel
    std::string   side     = "front"; ///< SC DisplaySide: "front" | "left" | "right"

    [[nodiscard]] bool operator==(DisplayModel const&) const = default;
};

struct BackpackModel {
    std::optional<TankModel>    left;
    std::optional<TankModel>    right;
    std::optional<float>        battery; ///< charge ratio when a battery upgrade is installed
    std::optional<DisplayModel> display;
    /// Ids of upgrades that currently show activity particles ("cooking" burning, "jukebox"
    /// playing), SC's upgrade render data.
    std::vector<std::string>    activity;

    [[nodiscard]] bool           operator==(BackpackModel const&) const = default;
    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] static BackpackModel fromJson(nlohmann::json const& j);
};

enum class TankSide { Left, Right };

/// SC UpgradeHandler.initTankRenderInfoCallbacks: the first tank goes LEFT and every later one
/// RIGHT; a single tank goes RIGHT when its upgrade slot is in the second half (slot >= slots/2).
/// `tankSlots` are the upgrade-slot indexes of the tank upgrades in slot order.
[[nodiscard]] std::vector<TankSide> tankSides(std::vector<int> const& tankSlots, int upgradeSlots);

/// SB BackpackDynamicModel.ratioToStep: floor(ratio * steps + 1e-6), clamped to 0..steps.
[[nodiscard]] int ratioToStep(float ratio, int steps);

/// Fluid texture for a fluid id.
[[nodiscard]] FluidLook fluidLook(std::string_view fluid);

/// Display anchor (SB backpack_display_item.json): an 8x8 px quad on the front, centred at
/// (8, 9.6, 4.015) in block pixels, facing north. Item scale = 8/16, z offset step = scale/15.95.
inline constexpr float kDisplayCenterX     = 8.0f;
inline constexpr float kDisplayCenterY     = 9.6f;
inline constexpr float kDisplayCenterZ     = 4.015f;
inline constexpr float kDisplayScale       = 0.5f;
inline constexpr float kDisplayDepthOffset = 0.0015f;
inline constexpr float kDisplayPixelDivisor = 15.95f;
/// Depth of the displayed item in front of the anchor, in blocks (DisplayItemAnchor.applyTransform).
[[nodiscard]] float displayDepth(int zOffset);

} // namespace abp::core::render
