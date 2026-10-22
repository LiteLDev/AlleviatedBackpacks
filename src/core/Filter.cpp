#include "core/Filter.h"

#include <algorithm>

namespace abp::core {

Filter::Filter(int slots, FilterDefaults defaults) : ghosts(static_cast<std::size_t>(std::max(0, slots))) {
    mode           = defaults.allowList ? FilterMode::Allow : FilterMode::Deny;
    emptyAllowsAll = defaults.emptyAllowListMatchesEverything;
    matchAnyTag    = true;
}

bool Filter::hasOnlyEmptyFilters() const {
    return std::all_of(ghosts.begin(), ghosts.end(), [](ItemStackData const& g) { return g.empty(); });
}

bool Filter::isEmpty() const {
    if (matchBy == MatchBy::Tag) return tags.empty();
    return hasOnlyEmptyFilters();
}

bool Filter::stackMatchesFilter(ItemStackData const& item, ItemStackData const& filter, ITagProvider const* tagProvider) const {
    if (filter.empty() || item.empty()) return false;
    if (matchBy == MatchBy::Namespace) {
        if (item.itemNamespace() != filter.itemNamespace()) return false;
    } else if (matchBy == MatchBy::Item) {
        if (item.id != filter.id || item.aux != filter.aux) return false;
    }
    if (matchDurability && item.damage != filter.damage) return false;
    if (!matchNbt) return true;
    if (tagProvider) return tagProvider->componentsEqualIgnoringDamage(item, filter);
    return item.nbtHash() == filter.nbtHash() && item.nbt() == filter.nbt();
}

bool Filter::isTagMatch(ItemStackData const& item, ITagProvider const* tagProvider) const {
    if (matchAnyTag) {
        if (!tagProvider) return false;
        return std::any_of(tags.begin(), tags.end(), [&](std::string const& t) { return tagProvider->hasTag(item, t); });
    }
    // Match all: vacuously true without tags (as SC).
    if (tags.empty()) return true;
    if (!tagProvider) return false;
    return std::all_of(tags.begin(), tags.end(), [&](std::string const& t) { return tagProvider->hasTag(item, t); });
}

bool Filter::matches(ItemStackData const& item, ITagProvider const* tagProvider) const {
    if (item.empty()) return false;
    if (matchBy == MatchBy::Tag) return isTagMatch(item, tagProvider);
    return std::any_of(ghosts.begin(), ghosts.end(), [&](ItemStackData const& g) {
        return stackMatchesFilter(item, g, tagProvider);
    });
}

bool Filter::passes(ItemStackData const& item, ITagProvider const* tagProvider) const {
    if (mode == FilterMode::Allow) {
        if (matchBy == MatchBy::Tag) return isTagMatch(item, tagProvider);
        if (hasOnlyEmptyFilters() && emptyAllowsAll) return true;
        return matches(item, tagProvider);
    }
    if (matchBy == MatchBy::Tag) return !isTagMatch(item, tagProvider);
    return hasOnlyEmptyFilters() || !matches(item, tagProvider);
}

bool Filter::matchesContents(
    ItemStackData const&                             item,
    std::span<ItemStackData const>                   reference,
    ITagProvider const*                              tagProvider,
    std::function<bool(ItemStackData const&)> const& extra
) const {
    if (item.empty()) return false;
    if (!filterByStorage && !filterByInventory) return passes(item, tagProvider);
    for (auto const& r : reference) {
        if (stackMatchesFilter(item, r, tagProvider)) return true;
    }
    return extra && extra(item);
}

ContentsFilterType Filter::contentsType() const {
    if (filterByStorage) return ContentsFilterType::Storage;
    return mode == FilterMode::Allow ? ContentsFilterType::Allow : ContentsFilterType::Block;
}

void Filter::setContentsType(ContentsFilterType type) {
    switch (type) {
    case ContentsFilterType::Allow:
        filterByStorage = false;
        mode            = FilterMode::Allow;
        break;
    case ContentsFilterType::Block:
        filterByStorage = false;
        mode            = FilterMode::Deny;
        break;
    case ContentsFilterType::Storage:
        filterByStorage = true;
        break;
    }
}

ContentsFilterType Filter::nextContentsType() const {
    auto next = static_cast<ContentsFilterType>((static_cast<int>(contentsType()) + 1) % 3);
    if (matchBy == MatchBy::Tag && next == ContentsFilterType::Storage) {
        next = static_cast<ContentsFilterType>((static_cast<int>(next) + 1) % 3);
    }
    return next;
}

DepositFilterType Filter::depositType() const {
    if (filterByInventory) return DepositFilterType::Inventory;
    return mode == FilterMode::Allow ? DepositFilterType::Allow : DepositFilterType::Block;
}

void Filter::setDepositType(DepositFilterType type) {
    switch (type) {
    case DepositFilterType::Allow:
        filterByInventory = false;
        mode              = FilterMode::Allow;
        break;
    case DepositFilterType::Block:
        filterByInventory = false;
        mode              = FilterMode::Deny;
        break;
    case DepositFilterType::Inventory:
        filterByInventory = true;
        break;
    }
}

void Filter::cyclePrimaryMatch(bool contents) {
    auto next = static_cast<MatchBy>((static_cast<int>(matchBy) + 1) % 3);
    if (next == MatchBy::Tag && contents && contentsType() == ContentsFilterType::Storage) {
        // onTagsMatchSelected runs before the match changes: STORAGE -> next type (ALLOW).
        setContentsType(nextContentsType());
    }
    matchBy = next;
}

void Filter::setGhost(int i, ItemStackData const& item) {
    if (i < 0) return;
    if (static_cast<std::size_t>(i) >= ghosts.size()) ghosts.resize(static_cast<std::size_t>(i) + 1);
    ghosts[static_cast<std::size_t>(i)] = item.empty() ? ItemStackData{} : item.withCount(1);
}

bool Filter::hasGhost(ItemStackData const& item) const {
    return std::any_of(ghosts.begin(), ghosts.end(), [&](ItemStackData const& g) {
        return !g.empty() && g.sameItem(item) && g.damage == item.damage;
    });
}

bool Filter::addTag(std::string const& tag) {
    if (tag.empty()) return false;
    auto it = std::lower_bound(tags.begin(), tags.end(), tag);
    if (it != tags.end() && *it == tag) return false;
    tags.insert(it, tag);
    return true;
}

bool Filter::removeTag(std::string const& tag) {
    auto it = std::find(tags.begin(), tags.end(), tag);
    if (it == tags.end()) return false;
    tags.erase(it);
    return true;
}

bool Filter::hasTag(std::string_view tag) const {
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

std::vector<std::string> Filter::tagsToAdd(std::vector<std::string> itemTags) const {
    std::sort(itemTags.begin(), itemTags.end());
    itemTags.erase(std::unique(itemTags.begin(), itemTags.end()), itemTags.end());
    std::erase_if(itemTags, [&](std::string const& t) { return t.empty() || hasTag(t); });
    return itemTags;
}

nlohmann::json Filter::toJson() const {
    nlohmann::json g = nlohmann::json::array();
    for (auto const& s : ghosts) g.push_back(s.toJson());
    return nlohmann::json{
        {"v",         2                        },
        {"mode",      static_cast<int>(mode)   },
        {"matchBy",   static_cast<int>(matchBy)},
        {"nbt",       matchNbt                 },
        {"dur",       matchDurability          },
        {"emptyAll",  emptyAllowsAll           },
        {"anyTag",    matchAnyTag              },
        {"storage",   filterByStorage          },
        {"inventory", filterByInventory        },
        {"ghosts",    std::move(g)             },
        {"tags",      tags                     },
    };
}

Filter Filter::fromJson(nlohmann::json const& j, int slots) {
    Filter f;
    if (j.is_object()) try {
        f.mode            = j.value("mode", 0) == 1 ? FilterMode::Deny : FilterMode::Allow;
        int mb            = j.value("matchBy", 0);
        f.matchBy         = (mb >= 0 && mb <= 2) ? static_cast<MatchBy>(mb) : MatchBy::Item;
        f.matchNbt        = j.value("nbt", false);
        f.matchDurability = j.value("dur", false);
        f.emptyAllowsAll  = j.value("emptyAll", true);
        f.matchAnyTag     = j.value("anyTag", true);
        f.filterByStorage   = j.value("storage", false);
        f.filterByInventory = j.value("inventory", false);
        if (auto it = j.find("ghosts"); it != j.end() && it->is_array()) {
            for (auto const& g : *it) f.ghosts.push_back(ItemStackData::fromJson(g));
        }
        if (auto it = j.find("tags"); it != j.end() && it->is_array()) {
            for (auto const& t : *it) {
                if (t.is_string()) f.addTag(t.get<std::string>());
            }
        }
    } catch (nlohmann::json::exception const&) {
        f = Filter{}; // wrong field types: default filter
    }
    if (slots >= 0) f.ghosts.resize(static_cast<std::size_t>(slots));
    return f;
}

Filter Filter::fromJson(nlohmann::json const& j, int slots, FilterDefaults defaults) {
    if (!j.is_object() || j.empty()) return Filter(std::max(0, slots), defaults);
    auto f = fromJson(j, slots);
    if (!j.contains("mode")) f.mode = defaults.allowList ? FilterMode::Allow : FilterMode::Deny;
    f.emptyAllowsAll = defaults.emptyAllowListMatchesEverything;
    return f;
}

} // namespace abp::core
