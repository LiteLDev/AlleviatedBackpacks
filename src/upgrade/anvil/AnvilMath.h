#pragma once

// Pure anvil arithmetic (no game types), shared by the anvil upgrade and unit-tested in
// tests/AnvilMathTest.cpp. The rules follow the vanilla anvil.

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace abp::upgrade::anvil::math {

/// Survival players can't use an anvil when the total cost reaches this many levels.
inline constexpr int kTooExpensive = 40;

/// Repairing with material: each unit restores up to a quarter of the max durability.
struct MaterialRepair {
    int unitsUsed = 0; ///< material items consumed (0 = no repair possible)
    int newDamage = 0;
};

[[nodiscard]] constexpr MaterialRepair repairWithMaterial(int damage, int maxDamage, int available) {
    MaterialRepair r{0, damage};
    if (maxDamage <= 0) return r;
    int step = std::min(r.newDamage, maxDamage / 4);
    while (step > 0 && r.unitsUsed < available) {
        r.newDamage -= step;
        ++r.unitsUsed;
        step = std::min(r.newDamage, maxDamage / 4);
    }
    return r;
}

/// Combining two damageable items of the same kind: the durability left on both plus a 12% bonus.
/// Returns the damage of the result (never negative).
[[nodiscard]] constexpr int combinedDamage(int maxDamage, int damageLeft, int damageRight) {
    int remaining = (maxDamage - damageLeft) + (maxDamage - damageRight) + maxDamage * 12 / 100;
    return std::max(0, maxDamage - remaining);
}

struct FinalCost {
    int  levels       = 0;     ///< what taking the result costs
    bool tooExpensive = false; ///< survival players can't take it
};

/// `priorWork`: the sum of both inputs' repair costs (prior work penalty); `work`: the cost of
/// what this operation does (repair, enchantments, rename); `renameCost`: the part of `work`
/// spent on renaming. A rename-only operation is capped at 39 levels like vanilla.
[[nodiscard]] constexpr FinalCost finalCost(int priorWork, int work, int renameCost, bool creative) {
    FinalCost c{priorWork + work, false};
    if (renameCost > 0 && renameCost == work && c.levels >= kTooExpensive) c.levels = kTooExpensive - 1;
    c.tooExpensive = !creative && c.levels >= kTooExpensive;
    return c;
}

/// The repair cost stored on the result (doubles + 1 for every anvil use except pure renames).
[[nodiscard]] constexpr int nextRepairCost(int leftRepairCost, int rightRepairCost, bool renameOnly) {
    int k = std::max(leftRepairCost, rightRepairCost);
    if (!renameOnly) k = k * 2 + 1;
    return k;
}

/// Cuts a UTF-8 string to at most `maxChars` code points without splitting a character.
[[nodiscard]] inline std::string truncateUtf8(std::string_view text, std::size_t maxChars) {
    std::size_t chars = 0;
    std::size_t i     = 0;
    while (i < text.size()) {
        auto c   = static_cast<unsigned char>(text[i]);
        int  len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
        if (chars == maxChars || i + static_cast<std::size_t>(len) > text.size()) break;
        i += static_cast<std::size_t>(len);
        ++chars;
    }
    return std::string(text.substr(0, i));
}

} // namespace abp::upgrade::anvil::math
