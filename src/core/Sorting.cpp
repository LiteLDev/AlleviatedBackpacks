#include "core/Sorting.h"

#include <algorithm>
#include <array>
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

std::string firstTag(ItemStackData const& item, ITagProvider const* tags) {
    if (!tags) return {};
    auto all = tags->tagsOf(item);
    if (all.empty()) return {};
    return *std::min_element(all.begin(), all.end());
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

SortBy nextSortBy(SortBy by) { return static_cast<SortBy>((static_cast<int>(by) + 1) % kSortByCount); }

ItemLess comparatorFor(SortBy by, ITagProvider const* tags) {
    switch (by) {
    case SortBy::Count:
        return [](ItemStackData const& a, ItemStackData const& b) {
            if (a.count != b.count) return a.count > b.count;
            return nameLess(a, b);
        };
    case SortBy::Mod:
        return [](ItemStackData const& a, ItemStackData const& b) {
            if (a.itemNamespace() != b.itemNamespace()) return a.itemNamespace() < b.itemNamespace();
            return nameLess(a, b);
        };
    case SortBy::Tag:
        return [tags](ItemStackData const& a, ItemStackData const& b) {
            auto ta = firstTag(a, tags), tb = firstTag(b, tags);
            if (ta != tb) {
                if (ta.empty()) return false; // untagged items go last
                if (tb.empty()) return true;
                return ta < tb;
            }
            return nameLess(a, b);
        };
    case SortBy::Name:
    default:
        return [](ItemStackData const& a, ItemStackData const& b) { return nameLess(a, b); };
    }
}

bool memoryMatches(ItemStackData const& remembered, ItemStackData const& item) {
    return !remembered.empty() && !item.empty() && remembered.id == item.id && remembered.aux == item.aux;
}

bool sortInventory(BackpackInventory& inv, ItemLess const& less, SortLayout const& layout) {
    int const n        = inv.size();
    auto      isLocked = [&](int i) {
        return i < static_cast<int>(layout.locked.size()) && layout.locked[static_cast<std::size_t>(i)];
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
            if (left[k] <= 0 || !memoryMatches(*mem, kinds[k])) continue;
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

} // namespace abp::core
