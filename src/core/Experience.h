#pragma once

namespace abp::core::xp {

/// Experience maths shared by Bedrock and Java: the points needed to go from `level` to
/// `level + 1` (7, 9, 11, ... then +5 per level from 15, +9 per level from 30).
[[nodiscard]] long long pointsForNextLevel(int level);

/// Total points needed to reach `level` from 0 (no progress into it).
[[nodiscard]] long long totalForLevel(int level);

struct LevelAndPoints {
    int       level  = 0;
    long long points = 0; ///< points earned into `level` (< pointsForNextLevel(level))
    [[nodiscard]] bool operator==(LevelAndPoints const&) const = default;
};

/// Splits a total point count into a level and the remaining points.
[[nodiscard]] LevelAndPoints fromTotal(long long total);

/// The total point count of `level` plus `points` into it.
[[nodiscard]] long long toTotal(int level, long long points);

/// Planning the XP Pump's transfers. All values are experience points.
///
/// - `storeLevels(total, n)`: points to take from a player with `total` points so they end at
///   the start of level `max(0, level - n)` (n = huge: everything).
/// - `takeLevels(total, n)`: points to give so the player reaches the start of level `level + n`.
/// - `keepLevel(total, keep)`: positive = points above the start of level `keep` (to store),
///   negative = points missing to reach the start of level `keep` (to give), 0 = on target.
[[nodiscard]] long long storeLevels(long long total, int levels);
[[nodiscard]] long long takeLevels(long long total, int levels);
[[nodiscard]] long long keepLevel(long long total, int keep);

/// Mending: one experience point repairs 2 durability.
inline constexpr int kDurabilityPerPoint = 2;
/// Points needed to repair `damage` (rounded up).
[[nodiscard]] constexpr long long pointsToRepair(int damage) {
    return damage <= 0 ? 0 : (static_cast<long long>(damage) + kDurabilityPerPoint - 1) / kDurabilityPerPoint;
}

} // namespace abp::core::xp
