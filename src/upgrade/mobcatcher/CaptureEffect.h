#pragma once

// Mob Catcher capture effect (GX-1): SB `MobCatcherCaptureEffectPayload` +
// `MobCatcherCaptureEffectRenderer`. On capture the server sends companion players within 64 blocks
// the mob's look and where it is; their client draws a copy of the mob shrinking into the
// collapse position (the point where the player's look ray enters the mob's box) over
// ANIMATION_DURATION_TICKS (5): scale 1 - easeInCubic(progress) about that point. When it ends
// SB plays the poof / reverse portal particles and the pop sound there; the server does that here
// (for every player) once the animation's ticks have passed.
//
//   MobCaptureEffectPacket {"type": "minecraft:cow", "data": [[id, type, value], ...],
//                           "properties": "<base64 NBT>" (optional), "pos": [x, y, z],
//                           "collapse": [x, y, z], "yaw": deg, "pitch": deg,
//                           "size": [width, height], "ticks": 5}

#include "mc/deps/core/utility/optional_ref.h" // before the LeviLamina packet headers

#include "net/Packets.h"

namespace abp::upgrade::mobcatcher {

/// SB MobCatcherCaptureEffectRenderer.ANIMATION_DURATION_TICKS.
inline constexpr int kCaptureAnimationTicks = 5;
/// SB sendToPlayersNear(..., 64, ...).
inline constexpr float kCaptureEffectRange = 64.0f;

class MobCaptureEffectPacket : public net::JsonPacket<MobCaptureEffectPacket> {
    using JsonPacket::JsonPacket;
};

/// e2e: stretches the capture animation (ticks) so a screenshot can show it; 0 = SB's 5 ticks.
void setCaptureAnimationTicksForTests(int ticks);

} // namespace abp::upgrade::mobcatcher
