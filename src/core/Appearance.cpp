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

std::uint32_t calculateColor(std::uint32_t base, std::uint32_t defaultColor, std::span<std::uint32_t const> dyes) {
    if (dyes.empty()) return base;
    // Integer / float steps as in SophisticatedCore's ColorHelper (Java int and float math).
    int rgb[3]          = {0, 0, 0};
    int sumMaxComponent = 0;
    int numberOfColors  = 0;
    if (base != defaultColor) {
        float r = static_cast<float>((base >> 16) & 0xFF), g = static_cast<float>((base >> 8) & 0xFF), b = static_cast<float>(base & 0xFF);
        sumMaxComponent = static_cast<int>(static_cast<float>(sumMaxComponent) + std::max(r, std::max(g, b)));
        rgb[0]          = static_cast<int>(static_cast<float>(rgb[0]) + r);
        rgb[1]          = static_cast<int>(static_cast<float>(rgb[1]) + g);
        rgb[2]          = static_cast<int>(static_cast<float>(rgb[2]) + b);
        ++numberOfColors;
    }
    for (auto dye : dyes) {
        int r = static_cast<int>((dye >> 16) & 0xFF), g = static_cast<int>((dye >> 8) & 0xFF), b = static_cast<int>(dye & 0xFF);
        sumMaxComponent += std::max({r, g, b});
        rgb[0]          += r;
        rgb[1]          += g;
        rgb[2]          += b;
        ++numberOfColors;
    }
    int   avgR            = rgb[0] / numberOfColors;
    int   avgG            = rgb[1] / numberOfColors;
    int   avgB            = rgb[2] / numberOfColors;
    float avgMaxComponent = static_cast<float>(sumMaxComponent) / static_cast<float>(numberOfColors);
    float maxAvgComponent = static_cast<float>(std::max({avgR, avgG, avgB}));
    auto  scale           = [&](int c) {
        if (maxAvgComponent <= 0.0f) return 0; // Java: (int) NaN == 0
        return std::clamp(static_cast<int>(static_cast<float>(c) * avgMaxComponent / maxAvgComponent), 0, 255);
    };
    avgR = scale(avgR);
    avgG = scale(avgG);
    avgB = scale(avgB);
    return 0xFF000000u | (static_cast<std::uint32_t>(avgR) << 16) | (static_cast<std::uint32_t>(avgG) << 8) | static_cast<std::uint32_t>(avgB);
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
