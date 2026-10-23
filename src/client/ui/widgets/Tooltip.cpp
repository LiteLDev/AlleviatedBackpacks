#ifdef ABP_CLIENT

#include "client/ui/widgets/Tooltip.h"

#include <algorithm>
#include <cmath>

#include "mc/client/game/ClientInstance.h"
#include "mc/client/multiplayer/ClientLevel.h"
#include "mc/safety/RedactableString.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"

#include "client/Client.h"
#include "client/ui/widgets/SbStyle.h"
#include "client/ui/widgets/Utf8.h"

namespace abp::client::ui {

namespace {
constexpr float LINE_H    = 10;
constexpr float TITLE_GAP = 2;
constexpr float MARGIN    = 4;

std::vector<std::string> wrap(Painter& p, std::string const& text, float maxW) {
    if (text.empty() || p.textWidth(text) <= maxW) return {text};
    std::vector<std::string> out;
    std::size_t              start = 0;
    while (start < text.size()) {
        std::size_t pos = start, lastBreak = std::string::npos;
        while (pos < text.size()) {
            char32_t    cp  = utf8::decode(text, pos);
            std::size_t nxt = utf8::next(text, pos);
            if (cp == U'§' && nxt < text.size()) nxt = utf8::next(text, nxt);
            if (pos > start && p.textWidth(text.substr(start, nxt - start)) > maxW) break;
            if (utf8::isSpace(cp) || (utf8::isWide(cp) && pos > start)) lastBreak = pos;
            pos = nxt;
        }
        if (pos >= text.size()) {
            out.push_back(text.substr(start));
            break;
        }
        std::size_t cut = lastBreak != std::string::npos && lastBreak > start ? lastBreak : pos;
        out.push_back(text.substr(start, cut - start));
        start = cut;
        while (start < text.size() && utf8::isSpace(utf8::decode(text, start))) start = utf8::next(text, start);
    }
    return out;
}
} // namespace

void drawTooltip(
    Painter&                        p,
    std::vector<TooltipLine> const& lines,
    float                           x,
    float                           y,
    TooltipImage const*             image,
    TooltipStyle const&             style,
    bool                            titleGap
) {
    if (lines.empty() && !image) return;
    p.flush();
    Rect const  screen = p.screen();
    float const maxW   = std::clamp(std::floor(screen.w * 0.45f), 120.f, 300.f);

    struct Row {
        std::string   text;
        std::uint32_t rgb;
    };
    std::vector<Row> rows;
    std::size_t      titleRows = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        for (auto& part : wrap(p, lines[i].text, maxW)) rows.push_back({std::move(part), lines[i].rgb});
        if (i == 0) titleRows = rows.size();
    }
    float gap = titleGap && (rows.size() > titleRows || image) ? TITLE_GAP : 0;
    float w   = image ? image->width : 0;
    for (auto const& r : rows) w = std::max(w, p.textWidth(r.text));
    w       = std::ceil(w);
    float h = static_cast<float>(rows.size()) * LINE_H - (rows.empty() ? 0 : 1) + gap + (image ? image->height + (rows.empty() ? 0 : 1) : 0);

    float bx = x + 12, by = y - 12;
    bool  left = std::any_of(lines.begin(), lines.end(), [](TooltipLine const& l) { return l.leftSide; });
    if (left) bx = std::max(x + 12 - 24 - w, screen.x + 4);
    else if (bx + w + 4 > screen.right() - MARGIN) bx = x - 16 - w;
    bx = std::clamp(bx, screen.x + MARGIN + 4, std::max(screen.x + MARGIN + 4, screen.right() - MARGIN - 4 - w));
    by = std::clamp(by, screen.y + MARGIN + 4, std::max(screen.y + MARGIN + 4, screen.bottom() - MARGIN - 4 - h));

    sb::tooltipBox(p, {bx, by, w, h}, style.background, style.borderTop, style.borderBottom);
    float ly = by;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        p.text(rows[i].text, bx, ly, Color::rgb(rows[i].rgb), true);
        ly += LINE_H;
        if (i + 1 == titleRows) ly += gap;
    }
    p.flush();
    if (image && image->draw) {
        image->draw(p, bx, rows.empty() ? by : ly + (titleRows == rows.size() ? 0 : 0));
        p.flush();
    }
}

std::vector<TooltipLine> itemTooltipLines(ItemStack const& stack) {
    std::vector<TooltipLine> out;
    if (stack.isNull()) return out;
    out.push_back({stack.getHoverName(), 0xFFFFFF});
    auto* client = currentClient();
    auto* level  = client ? client->getLevel() : nullptr;
    if (level && stack.mItem) {
        Bedrock::Safety::RedactableString text{std::string{}};
        try {
            stack.mItem->appendFormattedHovertext(stack, *level, text, false);
        } catch (...) {
        }
        std::string const& s = text.mUnredactedString;
        std::size_t        start = 0;
        while (start <= s.size()) {
            auto end  = s.find('\n', start);
            auto line = s.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!line.empty()) out.push_back({line, 0xAAAAAA});
            if (end == std::string::npos) break;
            start = end + 1;
        }
        // appendFormattedHovertext may repeat the name on its first line.
        if (out.size() > 1 && out[1].text == out[0].text) out.erase(out.begin() + 1);
    } else {
        for (auto& l : stack.getCustomLore()) out.push_back({l, 0xAA00AA});
    }
    return out;
}

} // namespace abp::client::ui

#endif // ABP_CLIENT
