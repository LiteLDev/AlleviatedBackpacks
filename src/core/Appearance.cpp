#include "core/Appearance.h"

#include <algorithm>
#include <format>

namespace abp::core {

std::optional<DyeColor> dyeFromItemId(std::string_view itemId) {
    constexpr std::string_view prefix = "minecraft:";
    constexpr std::string_view suffix = "_dye";
    if (!itemId.starts_with(prefix) || !itemId.ends_with(suffix)) return std::nullopt;
    auto name = itemId.substr(prefix.size(), itemId.size() - prefix.size() - suffix.size());
    for (auto const& d : kDyes) {
        if (d.id == name) return d.color;
    }
    return std::nullopt;
}

std::optional<DyeColor> dyeFromColor(std::uint32_t argb) {
    for (auto const& d : kDyes) {
        if ((d.argb & 0xFFFFFFu) == (argb & 0xFFFFFFu)) return d.color;
    }
    return std::nullopt;
}

std::optional<std::uint32_t> mixColors(std::span<std::uint32_t const> colors) {
    if (colors.empty()) return std::nullopt;
    if (colors.size() == 1) return colors[0] | 0xFF000000u;
    long long r = 0, g = 0, b = 0, maxSum = 0;
    for (auto c : colors) {
        int cr = static_cast<int>((c >> 16) & 0xFF), cg = static_cast<int>((c >> 8) & 0xFF), cb = static_cast<int>(c & 0xFF);
        r += cr;
        g += cg;
        b += cb;
        maxSum += std::max({cr, cg, cb});
    }
    auto      n        = static_cast<long long>(colors.size());
    long long ar       = r / n, ag = g / n, ab = b / n;
    long long avgMax   = maxSum / n;
    long long maxOfAvg = std::max({ar, ag, ab});
    if (maxOfAvg > 0) {
        ar = ar * avgMax / maxOfAvg;
        ag = ag * avgMax / maxOfAvg;
        ab = ab * avgMax / maxOfAvg;
    }
    auto clamp = [](long long v) { return static_cast<std::uint32_t>(std::clamp<long long>(v, 0, 255)); };
    return 0xFF000000u | (clamp(ar) << 16) | (clamp(ag) << 8) | clamp(ab);
}

std::string colorHex(std::uint32_t argb) { return std::format("#{:06X}", argb & 0xFFFFFFu); }

FillStats fillStats(BackpackInventory const& inventory) {
    FillStats s;
    s.totalSlots = inventory.size();
    ItemStackData const probe = ItemStackData::of("minecraft:stone", 1, 64);
    for (int i = 0; i < inventory.size(); ++i) {
        auto const& item = inventory.at(i);
        if (item.empty()) {
            s.capacity += inventory.slotLimit(i, probe);
            continue;
        }
        ++s.usedSlots;
        s.items    += item.count;
        s.capacity += std::max(inventory.slotLimit(i, item), item.count);
    }
    if (s.capacity > 0 && s.items > 0) s.percent = static_cast<int>(std::max<long long>(1, s.items * 100 / s.capacity));
    return s;
}

} // namespace abp::core
