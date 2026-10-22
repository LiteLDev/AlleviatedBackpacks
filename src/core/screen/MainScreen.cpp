#include "core/screen/MainScreen.h"

#include <algorithm>
#include <cmath>

#include "core/screen/CountFormat.h"

namespace abp::core::screen {

CountOverlay countOverlay(long long count, std::string overrideText, bool yellow) {
    CountOverlay o;
    o.yellow  = yellow;
    o.special = count > 99; // shouldUseSpecialCountRender
    if (!overrideText.empty()) {
        o.text = std::move(overrideText);
    } else if (o.special) {
        o.text = abbreviateCount(count);
    } else if (count != 1 && count > 0) {
        o.text = std::to_string(count); // vanilla renderItemDecorations
    }
    return o;
}

float countScale(float textWidth) { return textWidth <= 0 ? 1.f : std::min(1.f, 16.f / textWidth); }

int quickCraftPlaceCount(QuickCraft type, int carriedCount, int carriedMaxStack, int slots, int slotMax) {
    int place = carriedCount;
    switch (type) {
    case QuickCraft::Split:
        place = slots > 0 ? static_cast<int>(std::floor(static_cast<float>(carriedCount) / static_cast<float>(slots))) : carriedCount;
        break;
    case QuickCraft::One:
        place = 1;
        break;
    case QuickCraft::Clone:
        place = carriedMaxStack;
        break;
    }
    return std::min(slotMax, place);
}

DragPreview dragPreview(int place, int slotCount, bool slotEmpty, int slotLimit) {
    DragPreview d;
    d.count         = place + (slotEmpty ? 0 : slotCount);
    int limit       = slotEmpty ? 64 : slotLimit;
    bool capped     = d.count > limit;
    d.overlay       = countOverlay(d.count, capped ? abbreviateCount(limit) : std::string{}, capped);
    return d;
}

int quickCraftRemainder(QuickCraft type, int carriedCount, int carriedMaxStack, std::vector<DragSlot> const& slots) {
    if (type == QuickCraft::Clone) return carriedMaxStack;
    int remainder = carriedCount;
    int n         = static_cast<int>(slots.size());
    for (auto const& s : slots) {
        int place = std::min(quickCraftPlaceCount(type, carriedCount, carriedMaxStack, n, s.max) + s.count, s.max);
        remainder -= place - s.count;
    }
    return remainder;
}

bool showsCountLine(int baseSlotLimit, int itemMaxStack) { return baseSlotLimit != itemMaxStack; }

namespace {
std::size_t codePointLength(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c >> 5) == 0x6) return 2;
    if ((c >> 4) == 0xE) return 3;
    if ((c >> 3) == 0x1E) return 4;
    return 1;
}
} // namespace

std::vector<std::string> wrapText(std::string_view text, float maxWidth, std::function<float(std::string_view)> const& width) {
    std::vector<std::string> lines;
    std::size_t              start = 0;
    while (start < text.size()) {
        // Hard line breaks first.
        auto nl = text.find('\n', start);
        if (nl != std::string_view::npos && width(text.substr(start, nl - start)) <= maxWidth) {
            lines.emplace_back(text.substr(start, nl - start));
            start = nl + 1;
            continue;
        }
        std::size_t end       = start;
        std::size_t lastSpace = std::string_view::npos;
        while (end < text.size() && text[end] != '\n') {
            std::size_t next = end + codePointLength(static_cast<unsigned char>(text[end]));
            if (next > text.size()) next = text.size();
            if (width(text.substr(start, next - start)) > maxWidth) break;
            if (text[end] == ' ') lastSpace = end;
            end = next;
        }
        if (end >= text.size() || text[end] == '\n') {
            lines.emplace_back(text.substr(start, end - start));
            start = end < text.size() ? end + 1 : end;
            continue;
        }
        if (lastSpace != std::string_view::npos && lastSpace > start) {
            lines.emplace_back(text.substr(start, lastSpace - start));
            start = lastSpace + 1; // the break consumes the space
        } else {
            if (end == start) end = start + codePointLength(static_cast<unsigned char>(text[start])); // at least one character
            lines.emplace_back(text.substr(start, end - start));
            start = end;
        }
        while (start < text.size() && text[start] == ' ') ++start;
    }
    if (lines.empty()) lines.emplace_back();
    return lines;
}

int errorTextHeight(std::size_t lines) { return lines > 1 ? 8 + 2 + static_cast<int>(lines - 1) * 10 : 8; }

} // namespace abp::core::screen
