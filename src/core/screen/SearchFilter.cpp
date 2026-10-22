#include "core/screen/SearchFilter.h"

#include <algorithm>

namespace abp::core::screen {

std::string stripFormatCodes(std::string_view text) {
    static constexpr std::string_view kSection = "\xC2\xA7"; // '§'
    std::string                       out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        if (text.substr(i, kSection.size()) == kSection) {
            i += kSection.size();
            if (i < text.size()) {
                ++i; // the code character, with its UTF-8 continuation bytes
                while (i < text.size() && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) ++i;
            }
            continue;
        }
        out += text[i++];
    }
    return out;
}

std::string searchFold(std::string_view text) {
    std::string out = stripFormatCodes(text);
    for (auto& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

namespace {
std::string_view trim(std::string_view s) {
    auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    while (!s.empty() && isSpace(s.front())) s.remove_prefix(1);
    while (!s.empty() && isSpace(s.back())) s.remove_suffix(1);
    return s;
}
} // namespace

SearchFilter::SearchFilter(std::string_view phrase) {
    auto trimmed = trim(phrase);
    if (trimmed.empty()) return;
    mBlank = false;
    // Java's String.split(" "): consecutive spaces yield empty terms (which match any name).
    std::size_t start = 0;
    while (start <= trimmed.size()) {
        auto end  = trimmed.find(' ', start);
        auto term = trimmed.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (term.starts_with('@')) mTerms.push_back({Kind::Mod, searchFold(term.substr(1))});
        else if (term.starts_with('#')) mTerms.push_back({Kind::Tooltip, searchFold(term.substr(1))});
        else mTerms.push_back({Kind::Name, searchFold(term)});
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
}

bool SearchFilter::needsTooltip() const {
    return std::any_of(mTerms.begin(), mTerms.end(), [](Term const& t) { return t.kind == Kind::Tooltip; });
}

bool SearchFilter::matches(SearchItem const& item) const {
    if (mBlank) return true;
    if (item.empty) return false;
    std::string name;
    bool        nameFolded = false;
    for (auto const& t : mTerms) {
        switch (t.kind) {
        case Kind::Mod:
            if (!t.text.empty() && searchFold(item.itemNamespace).find(t.text) == std::string::npos) return false;
            break;
        case Kind::Tooltip: {
            bool any = std::any_of(item.tooltipLines.begin(), item.tooltipLines.end(), [&](std::string const& line) {
                return searchFold(line).find(t.text) != std::string::npos;
            });
            if (!any) return false;
            break;
        }
        case Kind::Name:
            if (!nameFolded) {
                name       = searchFold(item.hoverName);
                nameFolded = true;
            }
            if (name.find(t.text) == std::string::npos) return false;
            break;
        }
    }
    return true;
}

} // namespace abp::core::screen
