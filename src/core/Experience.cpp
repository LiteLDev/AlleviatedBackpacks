#include "core/Experience.h"

#include <algorithm>

namespace abp::core::xp {

namespace {
constexpr int kMaxLevel = 24791; // Bedrock's level cap
}

long long pointsForNextLevel(int level) {
    if (level < 0) level = 0;
    if (level < 15) return 2LL * level + 7;
    if (level < 30) return 5LL * level - 38;
    return 9LL * level - 158;
}

long long totalForLevel(int level) {
    level = std::clamp(level, 0, kMaxLevel);
    auto l = static_cast<long long>(level);
    // Closed forms of the sums of pointsForNextLevel(0..level-1).
    if (level <= 15) return l * l + 6 * l;
    if (level <= 30) return (5 * l * l - 81 * l + 720) / 2;
    return (9 * l * l - 325 * l + 4440) / 2;
}

LevelAndPoints fromTotal(long long total) {
    if (total <= 0) return {};
    // Binary search the highest level whose start is <= total.
    int lo = 0, hi = kMaxLevel;
    while (lo < hi) {
        int mid = lo + (hi - lo + 1) / 2;
        if (totalForLevel(mid) <= total) lo = mid;
        else hi = mid - 1;
    }
    return {lo, total - totalForLevel(lo)};
}

long long toTotal(int level, long long points) { return totalForLevel(level) + std::max(0LL, points); }

long long storeLevels(long long total, int levels) {
    if (total <= 0 || levels <= 0) return 0;
    auto cur    = fromTotal(total);
    int  target = std::max(0, cur.level - levels);
    return total - totalForLevel(target);
}

long long takeLevels(long long total, int levels) {
    if (levels <= 0) return 0;
    auto cur    = fromTotal(std::max(0LL, total));
    int  target = static_cast<int>(std::min<long long>(static_cast<long long>(cur.level) + levels, kMaxLevel));
    return std::max(0LL, totalForLevel(target) - std::max(0LL, total));
}

long long keepLevel(long long total, int keep) { return std::max(0LL, total) - totalForLevel(std::max(0, keep)); }

} // namespace abp::core::xp
