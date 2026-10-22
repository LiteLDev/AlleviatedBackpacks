#pragma once

// Pure rules of the Mob Catcher upgrades (Sophisticated Backpacks `upgrades/mobcatcher`): slot cost,
// footprint shape, where a captured mob goes in the storage grid, and the captured-mob record. No
// game headers: shared by the server unit, the client overlay and the unit tests (tests/).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "core/BackpackInventory.h"

namespace abp::upgrade::mobcatcher {

/// Backpack-wide settings key holding the captured mobs (SB keeps them in the backpack contents,
/// not on the upgrade item, so they survive swapping basic <-> advanced).
inline constexpr char const* kCapturedMobsKey = "capturedMobs";

/// Width x height of storage slots a captured mob covers.
struct Footprint {
    int width  = 1;
    int height = 1;

    [[nodiscard]] int  area() const { return width * height; }
    [[nodiscard]] bool operator==(Footprint const&) const = default;
};

/// SB `getEffectiveMaxHealth`: at least 1, and never below the current health.
[[nodiscard]] inline double effectiveMaxHealth(double maxHealth, double currentHealth) {
    return std::max(1.0, std::max(maxHealth, currentHealth));
}

/// SB `getSlotCost`: ceil((max/2 + min(current, max)/2) * multiplier), at least 1.
[[nodiscard]] inline int slotCost(double maxHealth, double currentHealth, double multiplier) {
    double max  = effectiveMaxHealth(maxHealth, currentHealth);
    double cur  = std::max(0.0, currentHealth);
    double base = max / 2.0 + std::min(cur, max) / 2.0;
    return std::max(1, static_cast<int>(std::ceil(base * multiplier)));
}

/// SB `MobCatcherStorage.getFootprint`: the rectangle (area >= cost) whose aspect best matches the
/// mob's bounding box widened by 1.4, scored by aspect error * 10 + overfill * 0.75.
[[nodiscard]] inline Footprint footprintFor(double bbWidth, double bbHeight, int cost) {
    constexpr double kAspectWidening = 1.4;
    constexpr double kAspectWeight   = 10.0;
    constexpr double kOverfillWeight = 0.75;
    constexpr double kEps            = 0.001;
    // Java computes the aspect from floats (`float / float * double`).
    float  w            = std::max(static_cast<float>(bbWidth), 0.25f);
    float  h            = std::max(static_cast<float>(bbHeight), 0.25f);
    double targetAspect = static_cast<double>(w / h) * kAspectWidening;
    int    n            = std::max(1, cost);

    Footprint best{1, n};
    double    bestScore   = std::numeric_limits<double>::max();
    double    bestAspect  = std::numeric_limits<double>::max();
    int       bestOverfill = std::numeric_limits<int>::max();
    for (int fw = 1; fw <= n; ++fw) {
        for (int fh = 1; fh <= n; ++fh) {
            int area = fw * fh;
            if (area < cost) continue;
            double aspectError = std::abs(std::log((static_cast<double>(fw) / fh) / targetAspect));
            int    overfill    = area - cost;
            double score       = aspectError * kAspectWeight + overfill * kOverfillWeight;
            bool   better;
            if (score < bestScore - kEps) better = true;
            else if (score > bestScore + kEps) better = false;
            else if (aspectError < bestAspect - kEps) better = true;
            else if (aspectError > bestAspect + kEps) better = false;
            else if (fw != best.width) better = fw > best.width;
            else if (fh != best.height) better = fh < best.height;
            else better = overfill < bestOverfill;
            if (better) {
                bestScore    = score;
                bestAspect   = aspectError;
                bestOverfill = overfill;
                best         = {fw, fh};
            }
        }
    }
    return best;
}

/// Storage columns of a backpack (SB `getColumns`: 9 up to 81 slots, else 12; no columns are
/// taken by upgrade inventory parts in this port).
[[nodiscard]] inline int columnsFor(int storageSlots) { return storageSlots <= 81 ? 9 : 12; }

/// One captured mob (SB `CapturedMob`).
struct CapturedMob {
    std::string id;         ///< random uuid of this capture
    std::string entityType; ///< "minecraft:cow"
    std::string nbt;        ///< base64 binary NBT of the entity (server only; not sent to clients)
    int         slot       = 0; ///< top-left storage slot of the footprint
    int         width      = 1;
    int         height     = 1;
    int         slotCost   = 1;
    bool        hostile    = false;
    std::string displayName; ///< custom name ("" = the entity type's localized name)
    int         currentHealth = 0;
    int         maxHealth     = 1;
    /// Client preview / ambient sound data (variant, colour, baby, ...), free-form.
    nlohmann::json look = nlohmann::json::object();

    /// SB `occupiesSlot`.
    [[nodiscard]] bool occupiesSlot(int inventorySlot, int columns) const {
        if (columns <= 0) return false;
        int left = slot % columns, top = slot / columns;
        int x = inventorySlot % columns, y = inventorySlot / columns;
        return x >= left && x < left + width && y >= top && y < top + height;
    }

    [[nodiscard]] nlohmann::json toJson(bool withNbt = true) const {
        nlohmann::json j{
            {"id",            id           },
            {"entityType",    entityType   },
            {"slot",          slot         },
            {"width",         width        },
            {"height",        height       },
            {"slotCost",      slotCost     },
            {"hostile",       hostile      },
            {"displayName",   displayName  },
            {"currentHealth", currentHealth},
            {"maxHealth",     maxHealth    },
            {"look",          look         },
        };
        if (withNbt) j["nbt"] = nbt;
        return j;
    }

    [[nodiscard]] static std::optional<CapturedMob> fromJson(nlohmann::json const& j) {
        if (!j.is_object()) return std::nullopt;
        CapturedMob m;
        auto str = [&](char const* k) { auto it = j.find(k); return it != j.end() && it->is_string() ? it->get<std::string>() : std::string{}; };
        auto num = [&](char const* k, int def) { auto it = j.find(k); return it != j.end() && it->is_number_integer() ? it->get<int>() : def; };
        m.id         = str("id");
        m.entityType = str("entityType");
        m.nbt        = str("nbt");
        m.slot       = num("slot", -1);
        m.width      = num("width", 0);
        m.height     = num("height", 0);
        m.slotCost   = num("slotCost", 1);
        if (auto it = j.find("hostile"); it != j.end() && it->is_boolean()) m.hostile = it->get<bool>();
        m.displayName   = str("displayName");
        m.currentHealth = std::max(0, num("currentHealth", 0));
        m.maxHealth     = std::max(1, num("maxHealth", 1));
        if (auto it = j.find("look"); it != j.end() && it->is_object()) m.look = *it;
        if (m.id.empty() || m.entityType.empty() || m.slot < 0 || m.width < 1 || m.height < 1) return std::nullopt;
        return m;
    }
};

/// Reads a captured-mob list (invalid entries are dropped).
[[nodiscard]] inline std::vector<CapturedMob> parseMobs(nlohmann::json const& arr) {
    std::vector<CapturedMob> out;
    if (!arr.is_array()) return out;
    for (auto const& e : arr) {
        if (auto m = CapturedMob::fromJson(e)) out.push_back(std::move(*m));
    }
    return out;
}

/// Writes a captured-mob list ordered by slot, like SB's serializer.
[[nodiscard]] inline nlohmann::json mobsToJson(std::vector<CapturedMob> mobs, bool withNbt = true) {
    std::stable_sort(mobs.begin(), mobs.end(), [](CapturedMob const& a, CapturedMob const& b) { return a.slot < b.slot; });
    nlohmann::json arr = nlohmann::json::array();
    for (auto const& m : mobs) arr.push_back(m.toJson(withNbt));
    return arr;
}

/// True if any captured mob covers `slot` (SB `isSlotBlocked`).
[[nodiscard]] inline bool isSlotCovered(std::vector<CapturedMob> const& mobs, int slot, int columns) {
    return std::any_of(mobs.begin(), mobs.end(), [&](CapturedMob const& m) { return m.occupiesSlot(slot, columns); });
}

/// Storage slots covered by the mobs (in range of the inventory), ascending.
[[nodiscard]] inline std::vector<int> coveredSlots(std::vector<CapturedMob> const& mobs, int columns, int inventorySlots) {
    std::vector<int> out;
    for (auto const& m : mobs) {
        for (int y = 0; y < m.height; ++y) {
            for (int x = 0; x < m.width; ++x) {
                int s = m.slot + y * columns + x;
                if (s < inventorySlots) out.push_back(s);
            }
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

/// SB `findEmptyRectangle`: the first top-left slot (row by row) whose whole footprint is in the
/// grid, empty, not blocked and not covered by another captured mob.
[[nodiscard]] inline std::optional<int> findEmptyRectangle(
    core::BackpackInventory const&  inventory,
    int                             columns,
    Footprint                       footprint,
    std::vector<CapturedMob> const& mobs
) {
    int slots = inventory.size();
    if (columns <= 0 || slots <= 0) return std::nullopt;
    int rows = (slots + columns - 1) / columns;
    for (int y = 0; y <= rows - footprint.height; ++y) {
        for (int x = 0; x <= columns - footprint.width; ++x) {
            int  start = y * columns + x;
            bool ok    = true;
            for (int dy = 0; dy < footprint.height && ok; ++dy) {
                for (int dx = 0; dx < footprint.width && ok; ++dx) {
                    int s = start + dy * columns + dx;
                    ok    = s < slots && !inventory.isSlotBlocked(s) && inventory.at(s).empty() && !isSlotCovered(mobs, s, columns);
                }
            }
            if (ok) return start;
        }
    }
    return std::nullopt;
}

/// SB `canFitBasicTier`: every mob is passive and within the basic catcher's cost limit.
[[nodiscard]] inline bool canFitBasicTier(std::vector<CapturedMob> const& mobs, int basicMaxSlotCost) {
    return std::all_of(mobs.begin(), mobs.end(), [&](CapturedMob const& m) { return !m.hostile && m.slotCost <= basicMaxSlotCost; });
}

/// SB `getTargetSlot`: where a mob goes when the column count changes (kept inside the grid).
[[nodiscard]] inline int targetSlot(CapturedMob const& mob, int columns, int targetColumns, int inventorySlots) {
    int rows    = std::max(1, (inventorySlots + columns - 1) / columns);
    int targetX = std::min(mob.slot % columns, std::max(0, targetColumns - mob.width));
    int targetY = std::min(mob.slot / columns, std::max(0, rows - mob.height));
    return targetY * targetColumns + targetX;
}

// ---- health tooltip (SB ClientMobCatcherHealthTooltip) ------------------------------------------

inline constexpr int kHeartSize       = 9;
inline constexpr int kMaxInlineHearts = 10;

/// What the heart row shows: inline (one heart per 2 max health, up to 10 hearts) or the compact
/// "Nx[full] [half] Mx[empty]" form above 20 max health.
struct HeartRow {
    bool compact    = false;
    int  inlineHearts = 0; ///< inline: number of heart containers
    int  current    = 0;   ///< clamped current health
    int  full       = 0;   ///< compact: full hearts
    bool half       = false;
    int  empty      = 0;   ///< compact: empty hearts
};

[[nodiscard]] inline HeartRow heartRow(int currentHealth, int maxHealth) {
    HeartRow r;
    int max      = std::max(1, maxHealth);
    r.current    = std::min(max, std::max(0, currentHealth));
    r.compact    = max > kMaxInlineHearts * 2;
    r.inlineHearts = (max + 1) / 2;
    r.full       = r.current / 2;
    r.half       = r.current % 2 == 1;
    r.empty      = (max + 1) / 2 - r.full - (r.half ? 1 : 0);
    return r;
}

/// Inline heart `i`: 2 = full, 1 = half, 0 = empty container only.
[[nodiscard]] inline int inlineHeart(HeartRow const& r, int i) {
    int remaining = r.current - i * 2;
    return remaining >= 2 ? 2 : remaining == 1 ? 1 : 0;
}

// ---- ambient sounds (SB MobCatcherUpgradeWrapper.clientTick) ------------------------------------

inline constexpr int   kMinAmbientSoundDelay        = 220;
inline constexpr int   kAmbientSoundDelayVariation  = 260;
inline constexpr float kMuffledAmbientVolume        = 0.08f;
inline constexpr float kMuffledAmbientVolumeVariation = 0.03f;
inline constexpr float kMuffledAmbientPitch         = 0.62f;
inline constexpr float kMuffledAmbientPitchVariation = 0.08f;

// ---- preview pose (SB MobCatcherInventoryControl) -----------------------------------------------

/// Java `UUID.hashCode()` of a canonical uuid string ("8-4-4-4-12" hex): (msb ^ lsb) folded.
[[nodiscard]] inline std::int32_t javaUuidHash(std::string const& uuid) {
    std::uint64_t msb = 0, lsb = 0;
    int           n   = 0;
    for (char c : uuid) {
        int v;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        else continue;
        if (n < 16) msb = (msb << 4) | static_cast<std::uint64_t>(v);
        else if (n < 32) lsb = (lsb << 4) | static_cast<std::uint64_t>(v);
        ++n;
    }
    std::uint64_t x = msb ^ lsb;
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(x >> 32) ^ static_cast<std::uint32_t>(x));
}

[[nodiscard]] inline int floorMod(long long a, int m) { return static_cast<int>(((a % m) + m) % m); }

/// SB `getUuidFloat`.
[[nodiscard]] inline float uuidFloat(std::int32_t hash, int salt) {
    return static_cast<float>(floorMod(static_cast<long long>(hash) + static_cast<long long>(salt) * 31, 1000)) / 999.f;
}

/// SB `getCycleFloat`.
[[nodiscard]] inline float cycleFloat(std::int32_t hash, int cycle, int salt) {
    // Java int arithmetic wraps: hash * 31 + cycle * 131 + salt * 17.
    auto v = static_cast<std::int32_t>(
        static_cast<std::uint32_t>(hash) * 31u + static_cast<std::uint32_t>(cycle) * 131u + static_cast<std::uint32_t>(salt) * 17u
    );
    return static_cast<float>(floorMod(v, 1000)) / 999.f;
}

/// Head yaw / pitch offsets of the preview's idle look-around (SB `getIdlePoseOffset`).
[[nodiscard]] inline float idlePoseOffset(std::int32_t hash, float renderTime, int salt, float amplitude) {
    constexpr float kMinCycle = 340.f, kCycleVariation = 180.f, kMove = 30.f, kHold = 15.f;
    float cycleLength = kMinCycle + uuidFloat(hash, salt + 4) * kCycleVariation;
    int   cycle       = static_cast<int>(std::floor(renderTime / cycleLength));
    float phase       = renderTime - static_cast<float>(cycle) * cycleLength;
    float target      = (cycleFloat(hash, cycle, salt) - 0.5f) * 2.f * amplitude;
    auto  smooth      = [](float v) { return v * v * (3.f - 2.f * v); };
    if (phase < kMove) return smooth(phase / kMove) * target;
    if (phase < kMove + kHold) return target;
    if (phase < kMove * 2.f + kHold) return (1.f - smooth((phase - kMove - kHold) / kMove)) * target;
    return 0.f;
}

/// SB `getRenderScale`: GUI units per block for a `width` x `height` footprint area.
[[nodiscard]] inline int previewScale(double bbWidth, double bbHeight, int width, int height) {
    double w     = std::max(bbWidth, 0.25);
    double h     = std::max(bbHeight, 0.25);
    double scale = std::min((width - 6) / w, (height - 6) / h) * 0.5625;
    return std::max(8, static_cast<int>(scale));
}

} // namespace abp::upgrade::mobcatcher
