#pragma once
// AlleviatedBackpacks public API: experience maths (points <-> levels <-> liquid experience).
//
// Header-only and free of game headers. Matches SophisticatedCore's XpHelper (Java and Bedrock
// share the level curve: 7, 9, 11, ... points per level, +5 per level from 15, +9 from 30).

#include <algorithm>
#include <climits>

namespace abp::api::xp {

/// mB of liquid experience per experience point.
inline constexpr int kLiquidPerPoint = 20;

/// Experience points worth `liquid` mB (fractional).
[[nodiscard]] constexpr float liquidToExperience(int liquid) { return static_cast<float>(liquid) / kLiquidPerPoint; }
/// mB of liquid experience worth `points` points (truncated towards zero).
[[nodiscard]] constexpr int experienceToLiquid(float points) { return static_cast<int>(points * kLiquidPerPoint); }

/// Total points needed to reach the start of `level` from 0 (saturates at INT_MAX).
[[nodiscard]] constexpr int experienceForLevel(int level) {
    if (level <= 0) return 0;
    long long l = level;
    long long total;
    if (level < 16) total = l * (12 + l * 2) / 2;
    else if (level < 31) total = (l - 15) * (69 + (l - 15) * 5) / 2 + 315;
    else total = (l - 30) * (215 + (l - 30) * 9) / 2 + 1395;
    return static_cast<int>(std::min<long long>(total, INT_MAX));
}

/// The first level whose total (`experienceForLevel`) saturates at INT_MAX.
inline constexpr int kSaturatedLevel = 21864;

/// Points needed to go from `level` to `level + 1`.
[[nodiscard]] constexpr int experienceLimitOnLevel(int level) {
    if (level >= 30) return 112 + (level - 30) * 9;
    if (level >= 15) return 37 + (level - 15) * 5;
    return 7 + std::max(0, level) * 2;
}

/// The level a total of `points` reaches (whole levels).
[[nodiscard]] constexpr int levelForExperience(int points) {
    if (points <= 0) return 0;
    // experienceForLevel is non-decreasing and first saturates (INT_MAX) at level kSaturatedLevel.
    int lo = 0, hi = kSaturatedLevel;
    while (lo < hi) {
        int mid = lo + (hi - lo + 1) / 2;
        if (experienceForLevel(mid) <= points) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

/// The level a total of `points` reaches, with the progress into the next level as a fraction.
[[nodiscard]] constexpr double levelsForExperience(int points) {
    int level = levelForExperience(points);
    int rest  = std::max(0, points) - experienceForLevel(level);
    return level + static_cast<double>(rest) / experienceLimitOnLevel(level);
}

/// A player's total points from their level and progress (0..1) into the next level.
[[nodiscard]] constexpr int totalExperience(int level, float progress) {
    long long total = static_cast<long long>(experienceForLevel(level))
                    + static_cast<long long>(progress * static_cast<float>(experienceLimitOnLevel(level)));
    return static_cast<int>(std::clamp<long long>(total, 0, INT_MAX));
}

} // namespace abp::api::xp
