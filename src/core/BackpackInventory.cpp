#include "core/BackpackInventory.h"

#include <algorithm>
#include <climits>

namespace abp::core {

template <class Fn>
void BackpackInventory::insertSorted(std::vector<Hook<Fn>>& v, std::string owner, int priority, Fn fn) {
    auto it = std::upper_bound(v.begin(), v.end(), priority, [](int p, Hook<Fn> const& h) { return p < h.priority; });
    v.insert(it, Hook<Fn>{std::move(owner), priority, std::move(fn)});
}

void BackpackInventory::notify(int slot) const {
    for (auto const& l : mListeners) l.fn(slot);
}

void BackpackInventory::set(int slot, ItemStackData stack) {
    if (stack.empty()) stack.clear();
    mSlots.at(static_cast<std::size_t>(slot)) = std::move(stack);
    notify(slot);
}

std::vector<ItemStackData> BackpackInventory::resize(int newSize) {
    std::vector<ItemStackData> overflow;
    newSize = std::max(newSize, 0);
    for (std::size_t i = static_cast<std::size_t>(newSize); i < mSlots.size(); ++i) {
        if (!mSlots[i].empty()) overflow.push_back(std::move(mSlots[i]));
    }
    mSlots.resize(static_cast<std::size_t>(newSize));
    notify(-1);
    return overflow;
}

bool BackpackInventory::isEmpty() const {
    return std::all_of(mSlots.begin(), mSlots.end(), [](ItemStackData const& s) { return s.empty(); });
}

int BackpackInventory::slotLimit(int slot, ItemStackData const& item) const {
    int limit = std::max(item.maxStack, 1);
    for (auto const& m : mLimitModifiers) limit = m.fn(slot, item, limit);
    return std::max(limit, 1);
}

InsertVerdict BackpackInventory::checkInsert(int slot, ItemStackData const& stack, AccessContext const& ctx) const {
    InsertQuery q{*this, slot, stack, ctx};
    for (auto const& f : mInsertFilters) {
        if (auto v = f.fn(q); v != InsertVerdict::Allow) return v;
    }
    return InsertVerdict::Allow;
}

bool BackpackInventory::canExtract(int slot, int amount, AccessContext const& ctx) const {
    if (slot < 0 || slot >= size()) return false;
    ExtractQuery q{*this, slot, mSlots[static_cast<std::size_t>(slot)], amount, ctx};
    for (auto const& f : mExtractFilters) {
        if (!f.fn(q)) return false;
    }
    return true;
}

int BackpackInventory::fillSlot(int i, ItemStackData const& stack, int amount, bool simulate) {
    auto& cur = mSlots[static_cast<std::size_t>(i)];
    if (!cur.empty() && !cur.sameItem(stack)) return 0;
    int have  = cur.empty() ? 0 : cur.count;
    int space = slotLimit(i, cur.empty() ? stack : cur) - have;
    int moved = std::min(space, amount);
    if (moved <= 0) return 0;
    if (!simulate) {
        if (cur.empty()) cur = stack.withCount(moved);
        else cur.count += moved;
        notify(i);
    }
    return moved;
}

InsertResult BackpackInventory::insert(ItemStackData const& stack, AccessContext const& ctx, bool simulate) {
    InsertResult r;
    if (stack.empty()) return r;
    auto verdict = checkInsert(-1, stack, ctx);
    if (verdict == InsertVerdict::Deny) {
        r.remainder = stack;
        return r;
    }
    if (verdict == InsertVerdict::Void) {
        r.voided = stack.count;
        return r;
    }
    int remaining = stack.count;
    // Pass 0: preferred slots (e.g. memory slots), in the order given. Each slot is filled once
    // (as far as it goes); later passes skip it, which also keeps `simulate` from counting it twice.
    std::vector<int> done;
    for (auto const& p : mPreferences) {
        for (int i : p.fn(stack)) {
            if (remaining <= 0) break;
            if (i < 0 || i >= size() || std::find(done.begin(), done.end(), i) != done.end()) continue;
            if (checkInsert(i, stack, ctx) != InsertVerdict::Allow) continue;
            remaining -= fillSlot(i, stack, remaining, simulate);
            done.push_back(i);
        }
    }
    // Pass 1: merge into existing stacks. Pass 2: empty slots.
    for (int pass = 0; pass < 2 && remaining > 0; ++pass) {
        for (int i = 0; i < size() && remaining > 0; ++i) {
            if (!done.empty() && std::find(done.begin(), done.end(), i) != done.end()) continue;
            auto const& cur = mSlots[static_cast<std::size_t>(i)];
            if (pass == 0 ? (cur.empty() || !cur.sameItem(stack)) : !cur.empty()) continue;
            if (checkInsert(i, stack, ctx) != InsertVerdict::Allow) continue;
            remaining -= fillSlot(i, stack, remaining, simulate);
        }
    }
    r.inserted = stack.count - remaining;
    if (remaining > 0 && verdict == InsertVerdict::VoidOverflow) {
        r.voided  = remaining;
        remaining = 0;
    }
    r.remainder = stack.withCount(remaining);
    return r;
}

InsertResult BackpackInventory::insertInto(int slot, ItemStackData const& stack, AccessContext const& ctx, bool simulate) {
    InsertResult r;
    if (stack.empty()) return r;
    if (slot < 0 || slot >= size()) {
        r.remainder = stack;
        return r;
    }
    auto verdict = checkInsert(slot, stack, ctx);
    if (verdict == InsertVerdict::Deny) {
        r.remainder = stack;
        return r;
    }
    if (verdict == InsertVerdict::Void) {
        r.voided = stack.count;
        return r;
    }
    int moved  = fillSlot(slot, stack, stack.count, simulate);
    r.inserted = moved;
    int rest   = stack.count - moved;
    if (rest > 0 && verdict == InsertVerdict::VoidOverflow) {
        r.voided = rest;
        rest     = 0;
    }
    r.remainder = stack.withCount(rest);
    return r;
}

ItemStackData BackpackInventory::extract(int slot, int amount, AccessContext const& ctx, bool simulate) {
    if (slot < 0 || slot >= size() || amount <= 0) return {};
    auto& cur = mSlots[static_cast<std::size_t>(slot)];
    if (cur.empty()) return {};
    int n = std::min(amount, cur.count);
    if (!canExtract(slot, n, ctx)) return {};
    auto out = cur.withCount(n);
    if (!simulate) {
        cur.count -= n;
        if (cur.count <= 0) cur.clear();
        notify(slot);
    }
    return out;
}

ItemStackData BackpackInventory::extractMatching(
    std::function<bool(ItemStackData const&)> const& pred,
    int                                               amount,
    AccessContext const&                              ctx,
    bool                                              simulate
) {
    ItemStackData out;
    for (int i = size() - 1; i >= 0 && amount > 0; --i) {
        auto const& cur = mSlots[static_cast<std::size_t>(i)];
        if (cur.empty() || !pred(cur)) continue;
        if (!out.empty() && !out.sameItem(cur)) continue;
        auto got = extract(i, amount, ctx, simulate);
        if (got.empty()) continue;
        amount -= got.count;
        if (out.empty()) out = std::move(got);
        else out.count += got.count;
    }
    return out;
}

long long BackpackInventory::count(std::function<bool(ItemStackData const&)> const& pred) const {
    long long n = 0;
    for (auto const& s : mSlots) {
        if (!s.empty() && pred(s)) n += s.count;
    }
    return n;
}

void BackpackInventory::sort(std::function<bool(ItemStackData const&, ItemStackData const&)> const& less) {
    // Merge identical items into totals (first-seen order), then order the kinds. Totals are
    // 64-bit: with large stack multipliers several slots together can exceed INT_MAX (the
    // kind's own count, seen by `less`, saturates at INT_MAX).
    struct Kind {
        ItemStackData item;
        long long     total;
    };
    std::vector<Kind> kinds;
    for (auto const& s : mSlots) {
        if (s.empty()) continue;
        auto it = std::find_if(kinds.begin(), kinds.end(), [&](Kind const& k) { return k.item.sameItem(s); });
        if (it == kinds.end()) kinds.push_back({s, s.count});
        else it->total += s.count;
    }
    for (auto& k : kinds) k.item.count = static_cast<int>(std::min<long long>(k.total, INT_MAX));
    std::stable_sort(kinds.begin(), kinds.end(), [&](Kind const& a, Kind const& b) { return less(a.item, b.item); });
    std::vector<ItemStackData> laid(mSlots.size());
    std::size_t                next = 0;
    for (auto const& k : kinds) {
        long long left = k.total;
        while (left > 0) {
            if (next >= laid.size()) return; // would not fit (slot-dependent limits): keep the old layout
            int n        = static_cast<int>(std::min<long long>(left, slotLimit(static_cast<int>(next), k.item)));
            laid[next++] = k.item.withCount(n);
            left        -= n;
        }
    }
    mSlots = std::move(laid);
    notify(-1);
}

void BackpackInventory::addInsertFilter(std::string owner, int priority, InsertFilterFn fn) {
    insertSorted(mInsertFilters, std::move(owner), priority, std::move(fn));
}
void BackpackInventory::addExtractFilter(std::string owner, int priority, ExtractFilterFn fn) {
    insertSorted(mExtractFilters, std::move(owner), priority, std::move(fn));
}
void BackpackInventory::addLimitModifier(std::string owner, int priority, LimitModifierFn fn) {
    insertSorted(mLimitModifiers, std::move(owner), priority, std::move(fn));
}
void BackpackInventory::addChangeListener(std::string owner, ChangeListenerFn fn) {
    mListeners.push_back(Hook<ChangeListenerFn>{std::move(owner), 0, std::move(fn)});
}

void BackpackInventory::addSlotPreference(std::string owner, int priority, SlotPreferenceFn fn) {
    insertSorted(mPreferences, std::move(owner), priority, std::move(fn));
}

void BackpackInventory::removeHooks(std::string const& owner) {
    auto drop = [&](auto& v) {
        std::erase_if(v, [&](auto const& h) { return h.owner == owner; });
    };
    drop(mInsertFilters);
    drop(mExtractFilters);
    drop(mLimitModifiers);
    drop(mListeners);
    drop(mPreferences);
}

void BackpackInventory::clearHooksExcept(std::string_view keepOwner) {
    auto drop = [&](auto& v) {
        std::erase_if(v, [&](auto const& h) { return h.owner != keepOwner; });
    };
    drop(mInsertFilters);
    drop(mExtractFilters);
    drop(mLimitModifiers);
    drop(mListeners);
    drop(mPreferences);
}

void BackpackInventory::clearFilters() {
    mInsertFilters.clear();
    mExtractFilters.clear();
    mLimitModifiers.clear();
    mPreferences.clear();
}

nlohmann::json BackpackInventory::toJson() const {
    nlohmann::json items = nlohmann::json::array();
    for (int i = 0; i < size(); ++i) {
        auto const& s = mSlots[static_cast<std::size_t>(i)];
        if (s.empty()) continue;
        auto j    = s.toJson();
        j["slot"] = i;
        items.push_back(std::move(j));
    }
    return nlohmann::json{
        {"size",  size()          },
        {"items", std::move(items)},
    };
}

BackpackInventory BackpackInventory::fromJson(nlohmann::json const& j) {
    int size = 0;
    if (j.is_object()) {
        if (auto it = j.find("size"); it != j.end() && it->is_number_integer()) size = std::clamp(it->get<int>(), 0, 100000);
    }
    BackpackInventory inv(size);
    if (!j.is_object()) return inv;
    if (auto it = j.find("items"); it != j.end() && it->is_array()) {
        for (auto const& e : *it) {
            if (!e.is_object()) continue;
            auto s    = e.find("slot");
            int  slot = (s != e.end() && s->is_number_integer()) ? s->get<int>() : -1;
            if (slot < 0 || slot >= inv.size()) continue;
            inv.mSlots[static_cast<std::size_t>(slot)] = ItemStackData::fromJson(e);
        }
    }
    return inv;
}

} // namespace abp::core
