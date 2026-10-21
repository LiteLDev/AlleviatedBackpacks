#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/BackpackInventory.h"
#include "core/Filter.h"

namespace abp::core {

/// How the Sort button orders items.
enum class SortBy : int {
    Name  = 0, ///< by item name (id path, then namespace)
    Count = 1, ///< by total count, most first
    Mod   = 2, ///< by namespace ("mod"), then name
    Tag   = 3, ///< by the item's first tag (items without tags last), then name
};
inline constexpr int kSortByCount = 4;

[[nodiscard]] std::string_view       toString(SortBy by);
[[nodiscard]] std::optional<SortBy>  sortByFromString(std::string_view s);
/// The next mode (wraps around), for "cycle sort mode" buttons.
[[nodiscard]] SortBy                 nextSortBy(SortBy by);

using ItemLess = std::function<bool(ItemStackData const&, ItemStackData const&)>;

/// A strict weak ordering of item kinds for `by`. The items passed in are merged totals (one per
/// kind), so `Count` compares totals. `tags` is needed for `SortBy::Tag` (nullptr: no tags).
[[nodiscard]] ItemLess comparatorFor(SortBy by, ITagProvider const* tags = nullptr);

/// Per-slot sorting constraints.
struct SortLayout {
    /// Slots left exactly as they are (no-sort slots). Shorter than the inventory = rest false.
    std::vector<bool>          locked;
    /// Memory slots: a non-empty entry means only items matching it (`memoryMatches`) may be
    /// placed there; sorting fills them with their item first.
    std::vector<ItemStackData> memory;
};

/// True if `item` may go into a memory slot remembering `remembered` (same id and aux; NBT is
/// ignored so damaged / enchanted variants still match).
[[nodiscard]] bool memoryMatches(ItemStackData const& remembered, ItemStackData const& item);

/// Sorts `inv` like `BackpackInventory::sort`, honouring `layout`: locked slots are untouched,
/// memory slots receive their remembered item first (and nothing else), the remaining kinds are
/// merged and laid out in `less` order into the other free slots. Uses `inv.slotLimit`.
/// Returns false (and changes nothing) if the result would not fit.
bool sortInventory(BackpackInventory& inv, ItemLess const& less, SortLayout const& layout = {});

} // namespace abp::core
