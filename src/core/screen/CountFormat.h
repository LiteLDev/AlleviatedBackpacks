#pragma once

#include <string>

namespace abp::core::screen {

/// Sophisticated Core's stack count abbreviation for slot overlays and the contents tooltip.
///
/// Counts with at most `maxCharacters` digits are written in full with `,` grouping ("1,234");
/// larger ones get a `k` / `m` / `b` suffix and as many decimals (rounded down) as fit into
/// `maxCharacters` characters: 12345 -> "12.3k", 123456 -> "123k", 1234567 -> "1.23m".
[[nodiscard]] std::string abbreviateCount(long long count, int maxCharacters = 4);

/// "1,234,567" (`,` grouping, no abbreviation).
[[nodiscard]] std::string groupedCount(long long count);

/// Java's `DecimalFormat("0.###")` for stack size multipliers: 2 -> "2", 1.5 -> "1.5",
/// 0.125 -> "0.125", 1/3 -> "0.333".
[[nodiscard]] std::string formatMultiplier(double value);

} // namespace abp::core::screen
