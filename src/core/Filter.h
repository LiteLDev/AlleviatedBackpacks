#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/ItemStackData.h"

namespace abp::core {

/// Answers tag questions about an item (the game layer implements it with the item registry;
/// tests use a fake). Tags are full names like "minecraft:logs".
class ITagProvider {
public:
    virtual ~ITagProvider() = default;
    [[nodiscard]] virtual bool                     hasTag(ItemStackData const& item, std::string_view tag) const = 0;
    [[nodiscard]] virtual std::vector<std::string> tagsOf(ItemStackData const& item) const                       = 0;
};

enum class FilterMode : int {
    Allow = 0, ///< only matching items pass
    Deny  = 1, ///< matching items are rejected
};

enum class MatchBy : int {
    Item      = 0, ///< same item id (and aux)
    Namespace = 1, ///< same id namespace as a ghost item ("mod" match)
    Tag       = 2, ///< item has one of `tags` (or a tag of a ghost item when `tags` is empty)
};

/// A reusable item filter: a fixed-size list of ghost slots plus match options.
///
/// Matching rules:
/// - `MatchBy::Item`: a ghost with the same id (and aux) matches; with `matchNbt` the NBT blob
///   must be equal too, with `matchDurability` the damage must be equal too.
/// - `MatchBy::Namespace`: a ghost with the same namespace matches.
/// - `MatchBy::Tag`: the item has one of `tags`; if `tags` is empty, one of the tags of any ghost.
///
/// `passes()`: Allow mode passes matching items and, when the filter is completely empty,
/// everything (`emptyAllowsAll`, default true). Deny mode passes non-matching items.
struct Filter {
    FilterMode                 mode            = FilterMode::Allow;
    MatchBy                    matchBy         = MatchBy::Item;
    bool                       matchNbt        = false;
    bool                       matchDurability = false;
    bool                       emptyAllowsAll  = true;
    std::vector<ItemStackData> ghosts;          ///< fixed size; empty entries are unused slots (count is 1 when set)
    std::vector<std::string>   tags;            ///< explicit tags for MatchBy::Tag

    Filter() = default;
    explicit Filter(int slots) : ghosts(static_cast<std::size_t>(slots)) {}

    [[nodiscard]] bool isEmpty() const;
    [[nodiscard]] bool matches(ItemStackData const& item, ITagProvider const* tags = nullptr) const;
    [[nodiscard]] bool passes(ItemStackData const& item, ITagProvider const* tags = nullptr) const;

    /// Sets ghost slot `i` to a 1-count copy of `item` (or clears it when `item` is empty).
    void setGhost(int i, ItemStackData const& item);
    /// True if a ghost of the same item already exists (used to avoid duplicates).
    [[nodiscard]] bool hasGhost(ItemStackData const& item) const;

    [[nodiscard]] nlohmann::json toJson() const;
    /// Reads JSON written by `toJson`; keeps `slots` ghost entries (pads/truncates), -1 = as stored.
    [[nodiscard]] static Filter fromJson(nlohmann::json const& j, int slots = -1);
};

} // namespace abp::core
