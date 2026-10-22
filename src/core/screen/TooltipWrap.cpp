#include "core/screen/TooltipWrap.h"

#include <cctype>

namespace abp::core::screen {

std::optional<int> tooltipWrapWidth(int mouseX, int screenWidth, int textWidth) {
    int tooltipX = mouseX + 12;
    if (tooltipX + textWidth + 4 <= screenWidth) return std::nullopt;
    tooltipX = mouseX - 16 - textWidth;
    if (tooltipX >= 4) return std::nullopt;
    return mouseX > screenWidth / 2 ? mouseX - 12 - 8 : screenWidth - 16 - mouseX;
}

namespace {
std::size_t utf8Len(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c >> 5) == 0x6) return 2;
    if ((c >> 4) == 0xE) return 3;
    if ((c >> 3) == 0x1E) return 4;
    return 1;
}

constexpr std::string_view kSection = "\xC2\xA7"; // §

/// Applies the formatting code `code` to the active formats (vanilla: a colour or §r resets).
void applyCode(std::string& active, char code) {
    char c = static_cast<char>(std::tolower(static_cast<unsigned char>(code)));
    if (c == 'r') {
        active.clear();
    } else if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) {
        active = std::string(kSection) + c;
    } else if (c >= 'k' && c <= 'o') {
        active += std::string(kSection) + c;
    }
}
} // namespace

int vanillaTextWidth(std::string_view text) {
    // Advances of the printable ASCII glyphs 0x20..0x7E in the default font.
    static constexpr unsigned char kAscii[95] = {
        4, 2, 5, 6, 6, 6, 6, 3, 5, 5, 5, 6, 2, 6, 2, 6, // space ! " # $ % & ' ( ) * + , - . /
        6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 2, 2, 5, 6, 5, 6, // 0-9 : ; < = > ?
        7, 6, 6, 6, 6, 6, 6, 6, 6, 4, 6, 6, 6, 6, 6, 6, // @ A-O (I = 4)
        6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 4, 6, 4, 6, 6, // P-Z [ \ ] ^ _
        3, 6, 6, 6, 6, 6, 5, 6, 6, 2, 6, 5, 3, 6, 6, 6, // ` a-o (f = 5, i = 2, k = 5, l = 3)
        6, 6, 6, 6, 4, 6, 6, 6, 6, 6, 6, 5, 2, 5, 7,    // p-z (t = 4) { | } ~
    };
    int         w = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        auto        c   = static_cast<unsigned char>(text[i]);
        std::size_t len = utf8Len(c);
        if (text.compare(i, kSection.size(), kSection) == 0) {
            i += kSection.size();
            if (i < text.size()) i += utf8Len(static_cast<unsigned char>(text[i])); // the code
            continue;
        }
        if (c >= 0x20 && c < 0x7F) {
            w += kAscii[c - 0x20];
        } else if (c >= 0x80) {
            char32_t cp = 0;
            if (len == 2 && i + 1 < text.size()) cp = ((c & 0x1F) << 6) | (text[i + 1] & 0x3F);
            else if (len == 3 && i + 2 < text.size()) cp = ((c & 0x0F) << 12) | ((text[i + 1] & 0x3F) << 6) | (text[i + 2] & 0x3F);
            else if (len == 4) cp = 0x10000;
            bool wide = (cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xAC00 && cp <= 0xD7AF) || (cp >= 0xF900 && cp <= 0xFAFF)
                     || (cp >= 0xFF00 && cp <= 0xFF60) || (cp >= 0x3000 && cp <= 0x303F) || cp >= 0x10000;
            w += wide ? 9 : 6;
        }
        i += len;
    }
    return w;
}

std::vector<std::string> wrapTooltipLine(std::string const& line, int maxWidth, TextWidthFn const& width) {
    std::vector<std::string> out;
    if (maxWidth <= 0 || !width || width(line) <= maxWidth) {
        out.push_back(line);
        return out;
    }
    std::size_t const n      = line.size();
    std::size_t       pos    = 0;
    std::string       prefix; // formats in effect at the start of the current line
    while (pos < n) {
        std::string active = prefix;
        std::string atSpace;
        std::size_t lastSpace = std::string::npos;
        std::size_t i         = pos;
        bool        visible   = false; // the line has a visible character yet
        while (i < n) {
            auto        c    = static_cast<unsigned char>(line[i]);
            std::size_t next = i + utf8Len(c);
            if (line.compare(i, kSection.size(), kSection) == 0 && next < n) {
                applyCode(active, line[next]);
                i = next + utf8Len(static_cast<unsigned char>(line[next]));
                continue;
            }
            if (visible && width(prefix + line.substr(pos, next - pos)) > maxWidth) break;
            if (c == ' ') {
                lastSpace = i;
                atSpace   = active;
            }
            visible = true;
            i       = next;
        }
        if (i >= n) {
            out.push_back(prefix + line.substr(pos));
            break;
        }
        bool        atWord = lastSpace != std::string::npos && lastSpace > pos;
        std::size_t cut    = atWord ? lastSpace : i;
        out.push_back(prefix + line.substr(pos, cut - pos));
        prefix = atWord ? atSpace : active;
        pos    = cut;
        if (pos < n && line[pos] == ' ') ++pos; // the space at the break is dropped
    }
    return out;
}

} // namespace abp::core::screen
