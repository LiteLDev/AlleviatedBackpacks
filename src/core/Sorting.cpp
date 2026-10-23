#include "core/Sorting.h"

#include <algorithm>
#include <array>
#include <memory>
#include <unordered_map>
#include <tuple>

namespace abp::core {

namespace {
constexpr std::array<std::string_view, kSortByCount> kNames{"name", "count", "mod", "tag"};

std::string_view pathOf(std::string_view id) {
    auto p = id.find(':');
    return p == std::string_view::npos ? id : id.substr(p + 1);
}

bool nameLess(ItemStackData const& a, ItemStackData const& b) {
    return std::make_tuple(pathOf(a.id), a.itemNamespace(), a.aux, a.nbtHash(), std::string_view(a.nbt()))
         < std::make_tuple(pathOf(b.id), b.itemNamespace(), b.aux, b.nbtHash(), std::string_view(b.nbt()));
}

std::string lowerAscii(std::string s) {
    for (auto& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

/// SC InventorySorter.BY_TAGS `compareTags`: more tags first, then the (sorted) tag names.
/// `a` and `b` are sorted.
int compareTags(std::vector<std::string> const& a, std::vector<std::string> const& b) {
    if (a.size() != b.size()) return a.size() > b.size() ? -1 : 1;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}
} // namespace

std::string_view toString(SortBy by) {
    auto i = static_cast<int>(by);
    return (i >= 0 && i < kSortByCount) ? kNames[static_cast<std::size_t>(i)] : kNames[0];
}

std::optional<SortBy> sortByFromString(std::string_view s) {
    for (int i = 0; i < kSortByCount; ++i) {
        if (kNames[static_cast<std::size_t>(i)] == s) return static_cast<SortBy>(i);
    }
    return std::nullopt;
}

SortBy nextSortBy(SortBy by) {
    // Sophisticated Core's SortBy order: NAME -> MOD -> COUNT -> TAGS -> NAME.
    switch (by) {
    case SortBy::Name:
        return SortBy::Mod;
    case SortBy::Mod:
        return SortBy::Count;
    case SortBy::Count:
        return SortBy::Tag;
    case SortBy::Tag:
    default:
        return SortBy::Name;
    }
}

ItemLess comparatorFor(SortBy by, ITagProvider const* tags, ItemNameFn names) {
    // Display name (SC: `getHoverName().getString()`); without a provider the id path stands in.
    // Names are looked up once per item kind (comparisons happen O(n log n) times).
    auto nameOf = [names, memo = std::make_shared<std::unordered_map<std::string, std::string>>()](ItemStackData const& item) -> std::string const& {
        auto key = item.id + '#' + std::to_string(item.aux) + '#' + std::string(item.nbt());
        auto it  = memo->find(key);
        if (it == memo->end()) it = memo->emplace(std::move(key), names ? names(item) : std::string(pathOf(item.id))).first;
        return it->second;
    };
    switch (by) {
    case SortBy::Count:
        // SC BY_COUNT: total count (most first), then the registry name.
        return [](ItemStackData const& a, ItemStackData const& b) {
            if (a.count != b.count) return a.count > b.count;
            if (a.id != b.id) return a.id < b.id;
            return nameLess(a, b);
        };
    case SortBy::Mod:
        // SC BY_MOD: namespace, then the display name (case-sensitive).
        return [nameOf](ItemStackData const& a, ItemStackData const& b) {
            if (a.itemNamespace() != b.itemNamespace()) return a.itemNamespace() < b.itemNamespace();
            auto const& na = nameOf(a);
            auto const& nb = nameOf(b);
            if (na != nb) return na < nb;
            return nameLess(a, b);
        };
    case SortBy::Tag:
        // SC BY_TAGS: more tags first, then the sorted tag names, then the registry name. Tags are
        // looked up once per item id (comparisons happen O(n log n) times).
        return [tags, memo = std::make_shared<std::unordered_map<std::string, std::vector<std::string>>>()](
                   ItemStackData const& a,
                   ItemStackData const& b
               ) {
            auto tagsOf = [&](ItemStackData const& item) -> std::vector<std::string> const& {
                auto it = memo->find(item.id);
                if (it == memo->end()) {
                    auto list = tags ? tags->tagsOf(item) : std::vector<std::string>{};
                    std::sort(list.begin(), list.end());
                    it = memo->emplace(item.id, std::move(list)).first;
                }
                return it->second;
            };
            if (a.id != b.id) {
                int c = compareTags(tagsOf(a), tagsOf(b));
                if (c != 0) return c < 0;
                return a.id < b.id;
            }
            return nameLess(a, b);
        };
    case SortBy::Name:
    default:
        // SC BY_NAME: the display name in lower case.
        return [nameOf](ItemStackData const& a, ItemStackData const& b) {
            auto na = lowerAscii(nameOf(a));
            auto nb = lowerAscii(nameOf(b));
            if (na != nb) return na < nb;
            return nameLess(a, b);
        };
    }
}

bool memoryMatches(ItemStackData const& remembered, ItemStackData const& item) {
    return !remembered.empty() && !item.empty() && remembered.id == item.id && remembered.aux == item.aux;
}

bool memorySlotAccepts(SortLayout const& layout, ItemStackData const& remembered, ItemStackData const& item) {
    if (remembered.empty() || item.empty()) return false;
    return layout.memoryMatchNbt ? remembered.sameItem(item) : memoryMatches(remembered, item);
}

namespace {
bool sortOnce(BackpackInventory& inv, ItemLess const& less, SortLayout const& layout);
} // namespace

bool sortInventory(BackpackInventory& inv, ItemLess const& less, SortLayout const& layout) {
    if (sortOnce(inv, less, layout)) return true;
    // A memory slot may still hold some other item (remembered after it was filled). If that makes
    // the layout impossible, leave such items where they are and try again.
    SortLayout relaxed = layout;
    bool       changed = false;
    relaxed.locked.resize(static_cast<std::size_t>(inv.size()), false);
    for (int i = 0; i < inv.size() && i < static_cast<int>(layout.memory.size()); ++i) {
        auto const& m = layout.memory[static_cast<std::size_t>(i)];
        if (!m.empty() && !inv.at(i).empty() && !memorySlotAccepts(layout, m, inv.at(i)) && !relaxed.locked[static_cast<std::size_t>(i)]) {
            relaxed.locked[static_cast<std::size_t>(i)] = true;
            changed                                    = true;
        }
    }
    return changed && sortOnce(inv, less, relaxed);
}

namespace {
bool sortOnce(BackpackInventory& inv, ItemLess const& less, SortLayout const& layout) {
    int const n        = inv.size();
    // Blocked slots (`BackpackInventory::isSlotBlocked`) are left exactly as they are, like locked ones.
    auto isLocked = [&](int i) {
        return (i < static_cast<int>(layout.locked.size()) && layout.locked[static_cast<std::size_t>(i)]) || inv.isSlotBlocked(i);
    };
    auto memoryOf = [&](int i) -> ItemStackData const* {
        if (i >= static_cast<int>(layout.memory.size())) return nullptr;
        auto const& m = layout.memory[static_cast<std::size_t>(i)];
        return m.empty() ? nullptr : &m;
    };

    // Merge the movable stacks into one total per kind (first-seen order), then order the kinds.
    std::vector<ItemStackData> kinds;
    for (int i = 0; i < n; ++i) {
        auto const& s = inv.at(i);
        if (s.empty() || isLocked(i)) continue;
        auto it = std::find_if(kinds.begin(), kinds.end(), [&](ItemStackData const& k) { return k.sameItem(s); });
        if (it == kinds.end()) kinds.push_back(s);
        else it->count += s.count;
    }
    std::stable_sort(kinds.begin(), kinds.end(), less);
    std::vector<int> left(kinds.size());
    for (std::size_t k = 0; k < kinds.size(); ++k) left[k] = kinds[k].count;

    std::vector<ItemStackData> laid(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        if (isLocked(i)) laid[static_cast<std::size_t>(i)] = inv.at(i);
    }
    // 1. Memory slots take their own item first.
    for (int i = 0; i < n; ++i) {
        auto const* mem = memoryOf(i);
        if (!mem || isLocked(i)) continue;
        for (std::size_t k = 0; k < kinds.size(); ++k) {
            if (left[k] <= 0 || !memorySlotAccepts(layout, *mem, kinds[k])) continue;
            int put                            = std::min(left[k], inv.slotLimit(i, kinds[k]));
            laid[static_cast<std::size_t>(i)] = kinds[k].withCount(put);
            left[k]                           -= put;
            break;
        }
    }
    // 2. Everything else in order into the free, unreserved slots.
    int next = 0;
    for (std::size_t k = 0; k < kinds.size(); ++k) {
        while (left[k] > 0) {
            while (next < n && (isLocked(next) || memoryOf(next))) ++next;
            if (next >= n) return false; // would not fit (slot-dependent limits): keep the old layout
            int put                               = std::min(left[k], inv.slotLimit(next, kinds[k]));
            laid[static_cast<std::size_t>(next++)] = kinds[k].withCount(put);
            left[k]                              -= put;
        }
    }
    for (int i = 0; i < n; ++i) {
        if (isLocked(i)) continue;
        auto const& want = laid[static_cast<std::size_t>(i)];
        auto const& have = inv.at(i);
        if (want.empty() && have.empty()) continue;
        if (want == have) continue;
        inv.set(i, want);
    }
    return true;
}
} // namespace

} // namespace abp::core
