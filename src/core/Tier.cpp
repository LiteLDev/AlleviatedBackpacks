#include "core/Tier.h"

namespace abp::core {

std::optional<Tier> tierFromId(std::string_view id) {
    for (auto const& info : kTiers) {
        if (info.id == id) return info.tier;
    }
    return std::nullopt;
}

std::optional<Tier> tierFromInt(int value) {
    if (value < 0 || value >= static_cast<int>(kTiers.size())) return std::nullopt;
    return static_cast<Tier>(value);
}

std::optional<Tier> nextTier(Tier t) { return tierFromInt(static_cast<int>(t) + 1); }

} // namespace abp::core
