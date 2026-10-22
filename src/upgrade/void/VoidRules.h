#pragma once

// Pure decision rules of the Void upgrade (no game headers; unit-tested in tests/), following
// Sophisticated Core's VoidUpgradeWrapper together with the overflow handling of its
// InventoryHandler / InventoryHandlerSlotTracker / StorageContainerMenuBase.

#include <functional>
#include <string_view>

#include "core/BackpackInventory.h"

namespace abp::upgrade::voiding {

/// SC `VoidType`.
enum class VoidType : int {
    Always          = 0, ///< void every matching item (and fluid)
    SlotOverflow    = 1, ///< let one slot fill with the item, void what overflows it
    StorageOverflow = 2, ///< let the whole storage fill, void what does not fit
};

[[nodiscard]] constexpr std::string_view toString(VoidType t) {
    switch (t) {
    case VoidType::SlotOverflow:
        return "slot_overflow";
    case VoidType::StorageOverflow:
        return "storage_overflow";
    case VoidType::Always:
    default:
        return "always";
    }
}
/// SC `VoidType.fromName` (unknown: ALWAYS).
[[nodiscard]] constexpr VoidType voidTypeFromName(std::string_view n) {
    if (n == "slot_overflow") return VoidType::SlotOverflow;
    if (n == "storage_overflow") return VoidType::StorageOverflow;
    return VoidType::Always;
}
/// SC `VoidType.next` (the tab skips ALWAYS when `voidAlwaysEnabled` is off).
[[nodiscard]] constexpr VoidType next(VoidType t, bool voidAlwaysEnabled) {
    auto n = static_cast<VoidType>((static_cast<int>(t) + 1) % 3);
    if (n == VoidType::Always && !voidAlwaysEnabled) n = VoidType::SlotOverflow;
    return n;
}
/// SC `getVoidType`: ALWAYS turns into SLOT_OVERFLOW when the config disables it.
[[nodiscard]] constexpr VoidType effective(VoidType t, bool voidAlwaysEnabled) {
    return t == VoidType::Always && !voidAlwaysEnabled ? VoidType::SlotOverflow : t;
}

/// What one installed Void upgrade decides with.
struct VoidRules {
    VoidType type      = VoidType::Always; ///< effective type
    bool     workInGui = false;
    bool     matchDurability = false;
    bool     matchNbt        = false;
    /// SC `filterLogic.matchesFilter(stack)`.
    std::function<bool(core::ItemStackData const&)> matches;
};

/// SC `stacksMatchForOverflow`: same item, and durability / NBT when the filter matches them.
[[nodiscard]] inline bool overflowStacksMatch(VoidRules const& r, core::ItemStackData const& a, core::ItemStackData const& b) {
    if (a.id != b.id || a.aux != b.aux) return false;
    if (r.matchDurability && a.damage != b.damage) return false;
    return !r.matchNbt || (a.nbtHash() == b.nbtHash() && a.nbt() == b.nbt());
}

[[nodiscard]] inline bool isFull(core::BackpackInventory const& inv, int slot) {
    auto const& s = inv.at(slot);
    return !s.empty() && s.count >= inv.slotLimit(slot, s);
}

/// SC `hasSlotOverflowMatch`: a stored stack (full or partial) of the item; with both durability
/// and NBT matching only a full stack of exactly that item counts.
[[nodiscard]] inline bool hasSlotOverflowMatch(VoidRules const& r, core::BackpackInventory const& inv, core::ItemStackData const& item, int ignoreSlot = -1) {
    bool exact = r.matchDurability && r.matchNbt;
    for (int i = 0; i < inv.size(); ++i) {
        if (i == ignoreSlot) continue;
        auto const& s = inv.at(i);
        if (s.empty()) continue;
        if (exact ? (s.sameItem(item) && s.damage == item.damage && isFull(inv, i)) : overflowStacksMatch(r, item, s)) return true;
    }
    return false;
}

/// A full stack of exactly this item (SC slot tracker `hasOneFullStackOfItem`).
[[nodiscard]] inline bool hasFullStack(core::BackpackInventory const& inv, core::ItemStackData const& item) {
    for (int i = 0; i < inv.size(); ++i) {
        if (inv.at(i).sameItem(item) && isFull(inv, i)) return true;
    }
    return false;
}

[[nodiscard]] inline bool hasStack(core::BackpackInventory const& inv, core::ItemStackData const& item) {
    for (int i = 0; i < inv.size(); ++i) {
        if (inv.at(i).sameItem(item)) return true;
    }
    return false;
}

[[nodiscard]] inline bool hasEmptySlot(core::BackpackInventory const& inv) {
    for (int i = 0; i < inv.size(); ++i) {
        if (inv.at(i).empty()) return true;
    }
    return false;
}

/// The insert verdict of one Void upgrade.
///
/// Automatic inserts (pickup, automation, other upgrades) follow SC's insert path:
/// - ALWAYS voids matching stacks (unless the storage has neither room nor the item at all,
///   where SC rejects the stack before any upgrade sees it);
/// - SLOT_OVERFLOW voids everything once a full stack exists, otherwise fills the one slot of the
///   item and voids what overflows it (no new stacks are started);
/// - STORAGE_OVERFLOW fills the storage and voids what does not fit.
///
/// Player inserts (GUI) only void with "work in GUI": SLOT_OVERFLOW voids what does not fit the
/// clicked slot / the existing stack (SC menu overflow handling). ALWAYS in the GUI voids the slot
/// on the next tick (see the upgrade), STORAGE_OVERFLOW does nothing in the GUI (as SC).
[[nodiscard]] inline core::InsertVerdict verdict(VoidRules const& r, core::InsertQuery const& q) {
    using V = core::InsertVerdict;
    auto const& inv = q.inventory;
    if (q.ctx.source == core::Source::Internal) return V::Allow;
    if (!r.matches || !r.matches(q.stack)) return V::Allow;
    if (q.ctx.source == core::Source::Player) {
        if (!r.workInGui || r.type != VoidType::SlotOverflow) return V::Allow;
        if (q.slot < 0) return hasSlotOverflowMatch(r, inv, q.stack) ? V::VoidOverflow : V::Allow;
        auto const& cur = inv.at(q.slot);
        if (cur.empty()) return hasSlotOverflowMatch(r, inv, q.stack, q.slot) ? V::Void : V::Allow;
        return q.direct && cur.sameItem(q.stack) ? V::VoidOverflow : V::Allow;
    }
    switch (r.type) {
    case VoidType::Always:
        return hasEmptySlot(inv) || hasStack(inv, q.stack) ? V::Void : V::Allow;
    case VoidType::SlotOverflow:
        if (q.slot < 0) {
            if (hasFullStack(inv, q.stack)) return V::Void;
            return hasStack(inv, q.stack) || hasEmptySlot(inv) ? V::VoidOverflow : V::Allow;
        }
        if (inv.at(q.slot).empty()) return hasSlotOverflowMatch(r, inv, q.stack, q.slot) ? V::Void : V::Allow;
        return q.direct && inv.at(q.slot).sameItem(q.stack) ? V::VoidOverflow : V::Allow;
    case VoidType::StorageOverflow:
        if (q.slot < 0) return hasStack(inv, q.stack) || hasEmptySlot(inv) ? V::VoidOverflow : V::Allow;
        return q.direct && inv.at(q.slot).sameItem(q.stack) && !hasEmptySlot(inv) ? V::VoidOverflow : V::Allow;
    }
    return V::Allow;
}

} // namespace abp::upgrade::voiding
