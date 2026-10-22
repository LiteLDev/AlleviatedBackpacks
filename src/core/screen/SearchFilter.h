#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace abp::core::screen {

/// What the search box needs to know about a slot's item.
struct SearchItem {
    bool                     empty = true;
    std::string              hoverName;    ///< displayed name (formatting codes are ignored)
    std::string              itemNamespace; ///< "minecraft", "alleviatedbackpacks", ...
    std::vector<std::string> tooltipLines; ///< every tooltip line (for `#` terms)
};

/// The backpack screen's search box filter (Sophisticated Core `updateSearchFilter`):
/// - a blank phrase matches every slot (also empty ones);
/// - otherwise the phrase is split at spaces and every term must match a non-empty stack:
///   `@mod` -> the item's namespace contains `mod` (a bare `@` matches anything),
///   `#text` -> some tooltip line contains `text`,
///   anything else -> the displayed name contains the term.
/// Matching ignores case (ASCII) and `§` formatting codes.
class SearchFilter {
public:
    SearchFilter() = default;
    explicit SearchFilter(std::string_view phrase);

    [[nodiscard]] bool blank() const { return mBlank; }
    [[nodiscard]] bool matches(SearchItem const& item) const;

    /// Whether some term needs `SearchItem::tooltipLines` (so callers can skip building them).
    [[nodiscard]] bool needsTooltip() const;

private:
    enum class Kind { Name, Mod, Tooltip };
    struct Term {
        Kind        kind;
        std::string text; ///< lowercase
    };
    bool              mBlank = true;
    std::vector<Term> mTerms;
};

/// Lowercase (ASCII) copy without `§x` formatting codes.
[[nodiscard]] std::string searchFold(std::string_view text);

} // namespace abp::core::screen
