#include "core/screen/CountFormat.h"

#include <cmath>
#include <cstdio>

namespace abp::core::screen {

std::string groupedCount(long long count) {
    bool        negative = count < 0;
    auto        value    = negative ? -static_cast<unsigned long long>(count) : static_cast<unsigned long long>(count);
    std::string digits   = std::to_string(value);
    std::string out;
    int         lead = static_cast<int>(digits.size() % 3);
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i != 0 && (static_cast<int>(i) - lead) % 3 == 0) out += ',';
        out += digits[i];
    }
    return negative ? "-" + out : out;
}

namespace {
int digitCount(long long v) {
    int n = 1;
    while (v >= 10) {
        v /= 10;
        ++n;
    }
    return n;
}

/// `value` with exactly `decimals` digits after the point, rounded down (Java RoundingMode.DOWN).
std::string truncated(double value, int decimals) {
    double scale  = std::pow(10.0, decimals);
    auto   scaled = static_cast<long long>(std::floor(value * scale + 1e-9));
    auto   whole  = scaled / static_cast<long long>(scale);
    auto   frac   = scaled % static_cast<long long>(scale);
    char   buf[32];
    std::snprintf(buf, sizeof(buf), "%lld.%0*lld", whole, decimals, frac);
    return buf;
}
} // namespace

std::string abbreviateCount(long long count, int maxCharacters) {
    if (count <= 0) return groupedCount(count);
    int digits = digitCount(count);
    if (digits <= maxCharacters) return groupedCount(count);

    static constexpr char const* kSuffixes[] = {"k", "m", "b"};
    int  exponent = (digits - maxCharacters) / 3 + 1;
    if (exponent > 3) exponent = 3;
    double division        = static_cast<double>(count) / std::pow(1000.0, exponent);
    int    wholeDigits     = digits - exponent * 3;
    int    precisionDigits = maxCharacters - 1 - wholeDigits;

    std::string number;
    if (wholeDigits > 3 || precisionDigits <= 0) {
        number = groupedCount(static_cast<long long>(division));
    } else if (precisionDigits == 2) {
        number = truncated(division, 2);
    } else {
        number = truncated(division, 1);
    }
    return number + kSuffixes[exponent - 1];
}

std::string formatMultiplier(double value) {
    if (!std::isfinite(value)) return "0";
    // Round half-even to 3 decimals like DecimalFormat, then strip trailing zeros.
    double      rounded = std::nearbyint(value * 1000.0) / 1000.0;
    char        buf[64];
    std::snprintf(buf, sizeof(buf), "%.3f", rounded);
    std::string s = buf;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    if (s == "-0") s = "0";
    return s;
}

} // namespace abp::core::screen
