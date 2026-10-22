#include "core/WorldRules.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace abp::core {

float moonBrightness(int phase) {
    static constexpr float kBrightness[8] = {1.0f, 0.75f, 0.5f, 0.25f, 0.0f, 0.25f, 0.5f, 0.75f};
    return kBrightness[((phase % 8) + 8) % 8];
}

float effectiveDifficulty(int difficulty, std::int64_t levelTime, std::int64_t inhabitedTime, float moon) {
    if (difficulty <= 0) return 0.0f;
    difficulty  = std::min(difficulty, 3);
    bool  hard  = difficulty == 3;
    float f     = 0.75f;
    float g     = std::clamp((static_cast<float>(levelTime) - 72000.0f) / 1440000.0f, 0.0f, 1.0f) * 0.25f;
    f          += g;
    float h     = 0.0f;
    h          += std::clamp(static_cast<float>(inhabitedTime) / 3600000.0f, 0.0f, 1.0f) * (hard ? 1.0f : 0.75f);
    h          += std::clamp(moon * 0.25f, 0.0f, g);
    if (difficulty == 1) h *= 0.5f;
    f += h;
    return static_cast<float>(difficulty) * f;
}

float specialMultiplier(float eff) {
    if (eff < 2.0f) return 0.0f;
    if (eff > 4.0f) return 1.0f;
    return (eff - 2.0f) / 2.0f;
}

int difficultyIndex(float eff, int buckets) {
    if (buckets <= 1) return 0;
    int i = static_cast<int>(std::floor(static_cast<float>(buckets) / 6.75f * eff - 0.1f));
    return std::clamp(i, 0, buckets - 1);
}

std::vector<TierChoice> tierPool(std::array<int, 6> const& weights, int index, int minMid, int minHigh) {
    int first = 0;
    if (index == 1) first = std::clamp(minMid, 0, 6);
    else if (index >= 2) first = std::clamp(minHigh, 0, 6);
    std::vector<TierChoice> out;
    for (int i = first; i < 6; ++i) out.push_back({static_cast<Tier>(i), std::clamp(weights[static_cast<std::size_t>(i)], 0, 9999)});
    return out;
}

int totalWeight(std::vector<TierChoice> const& pool) {
    int total = 0;
    for (auto const& c : pool) total += std::max(0, c.weight);
    return total;
}

Tier pickWeighted(std::vector<TierChoice> const& pool, int roll) {
    for (auto const& c : pool) {
        if (c.weight <= 0) continue;
        if (roll < c.weight) return c.tier;
        roll -= c.weight;
    }
    for (auto it = pool.rbegin(); it != pool.rend(); ++it) {
        if (it->weight > 0) return it->tier;
    }
    return Tier::Leather;
}

int minDifficultyOf(Tier tier) {
    switch (tier) {
    case Tier::Leather:
        return 0;
    case Tier::Copper:
    case Tier::Iron:
        return 1;
    case Tier::Gold:
        return 2;
    case Tier::Diamond:
        return 3;
    case Tier::Netherite:
        return 4;
    }
    return 0;
}

double dropChanceMultiplier(Tier tier) {
    switch (tier) {
    case Tier::Leather:
        return 1.0;
    case Tier::Copper:
        return 1.25;
    case Tier::Iron:
        return 1.5;
    case Tier::Gold:
        return 3.0;
    case Tier::Diamond:
        return 4.5;
    case Tier::Netherite:
        return 6.0;
    }
    return 1.0;
}

double dropChance(double base, double perLootingLevel, int lootingLevel, Tier tier) {
    return (base + std::max(0, lootingLevel) * perLootingLevel) * dropChanceMultiplier(tier);
}

ArmorRanges armorRanges(Tier tier) {
    switch (tier) {
    case Tier::Leather:
        return {{3, 5}, {3, 5}, {3, 5}};
    case Tier::Copper:
        return {{2, 4}, {3, 5}, {3, 5}};
    case Tier::Iron:
        return {{2, 4}, {2, 4}, {2, 4}};
    case Tier::Gold:
        return {{1, 3}, {1, 3}, {1, 3}};
    case Tier::Diamond:
        return {{0, 2}, {0, 2}, {0, 2}};
    case Tier::Netherite:
        return {{0, 1}, {0, 1}, {0, 1}};
    }
    return {{3, 5}, {3, 5}, {3, 5}};
}

int armorWeight(int material) {
    static constexpr int kWeights[5] = {1, 3, 9, 27, 81};
    return material >= 0 && material < 5 ? kWeights[material] : 0;
}

int slownessLevels(int count, int maxBackpacks, double levelsPerAdditional) {
    if (count <= maxBackpacks) return 0;
    double levels = std::ceil(static_cast<double>(count - maxBackpacks) * levelsPerAdditional);
    return std::clamp(static_cast<int>(levels), 0, 10);
}

char nearestFormatCode(std::uint32_t rgb) {
    struct Code {
        char          code;
        std::uint32_t rgb;
    };
    // Bedrock's text colours (the material colours §g..§v included).
    static constexpr Code kCodes[] = {
        {'0', 0x000000}, {'1', 0x0000AA}, {'2', 0x00AA00}, {'3', 0x00AAAA}, {'4', 0xAA0000}, {'5', 0xAA00AA},
        {'6', 0xFFAA00}, {'7', 0xAAAAAA}, {'8', 0x555555}, {'9', 0x5555FF}, {'a', 0x55FF55}, {'b', 0x55FFFF},
        {'c', 0xFF5555}, {'d', 0xFF55FF}, {'e', 0xFFFF55}, {'f', 0xFFFFFF}, {'g', 0xDDD605}, {'h', 0xE3D4D1},
        {'i', 0xCECACA}, {'j', 0x443A3B}, {'m', 0x971607}, {'n', 0xB4684D}, {'p', 0xDEB12D}, {'q', 0x47A036},
        {'s', 0x2CBAA8}, {'t', 0x21497B}, {'u', 0x9A5CC6}, {'v', 0xEB7114},
    };
    auto r = static_cast<int>((rgb >> 16) & 0xFF), g = static_cast<int>((rgb >> 8) & 0xFF), b = static_cast<int>(rgb & 0xFF);
    char best     = 'f';
    long bestDist = std::numeric_limits<long>::max();
    for (auto const& c : kCodes) {
        long dr = r - static_cast<int>((c.rgb >> 16) & 0xFF);
        long dg = g - static_cast<int>((c.rgb >> 8) & 0xFF);
        long db = b - static_cast<int>(c.rgb & 0xFF);
        // Weighted ("redmean"-like) distance: closer to how the swatches look than plain RGB.
        long d = 2 * dr * dr + 4 * dg * dg + 3 * db * db;
        if (d < bestDist) {
            bestDist = d;
            best     = c.code;
        }
    }
    return best;
}

std::string shortDateTime(int year, int month, int day, int hour, int minute) {
    int  h12 = hour % 12 == 0 ? 12 : hour % 12;
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%d/%d/%02d, %d:%02d %s", month, day, ((year % 100) + 100) % 100, h12, minute, hour < 12 ? "AM" : "PM");
    return buf;
}

} // namespace abp::core
