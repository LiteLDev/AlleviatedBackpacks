#pragma once

#include <functional>
#include <string>
#include <vector>

#include "core/ItemStackData.h"

namespace abp::core {

class BackpackInventory;

/// Who is moving items in or out. Filters use it to behave differently, e.g. a Void upgrade
/// that only voids picked-up items.
enum class Source {
    Player,     ///< a player clicking in the backpack GUI
    Pickup,     ///< items picked up from the ground (Pickup upgrade, auto-pickup)
    Automation, ///< hoppers / other containers (future)
    Upgrade,    ///< an upgrade moving items on its own (Feeding, Compacting, Restock, ...)
    Internal,   ///< the mod itself (migration, tier upgrade); filters should usually allow
};

struct AccessContext {
    Source source = Source::Player;
    void*  user   = nullptr; ///< game-defined pointer (game layer passes the acting Player*, may be null)
};

enum class InsertVerdict {
    Allow,        ///< no objection
    Deny,         ///< refuse the insertion (in auto placement: skip this slot)
    Void,         ///< accept everything and destroy it (Void upgrade)
    VoidOverflow, ///< insert what fits, destroy the rest (Void upgrade, overflow mode)
};

struct InsertQuery {
    BackpackInventory const& inventory;
    int                      slot; ///< -1 = whole-stack check before automatic placement
    ItemStackData const&     stack;
    AccessContext const&     ctx;
};

struct ExtractQuery {
    BackpackInventory const& inventory;
    int                      slot;
    ItemStackData const&     stack; ///< current slot content
    int                      amount;
    AccessContext const&     ctx;
};

using InsertFilterFn  = std::function<InsertVerdict(InsertQuery const&)>;
using ExtractFilterFn = std::function<bool(ExtractQuery const&)>; ///< false = deny
/// Returns the new per-slot limit given the current one (start: `item.maxStack`).
using LimitModifierFn = std::function<int(int slot, ItemStackData const& item, int currentLimit)>;
using ChangeListenerFn = std::function<void(int slot)>; ///< slot -1 = many / resized
/// Slots that `insert()` should try first for `stack` (e.g. memory slots), in order. Filters still
/// apply; slots out of range are ignored.
using SlotPreferenceFn = std::function<std::vector<int>(ItemStackData const& stack)>;
/// True if `slot` is blocked: inaccessible for every insert (any source) and for sorting, like
/// Sophisticated Core's `IInventorySlotBlocker` (e.g. slots covered by a captured mob).
using SlotBlockerFn = std::function<bool(int slot)>;

struct InsertResult {
    ItemStackData remainder; ///< what could not be inserted (empty when all went in or was voided)
    int           inserted = 0;
    int           voided   = 0;
};

/// The storage of one backpack.
///
/// Hooks (filters, limit modifiers, listeners) are runtime-only, keyed by an `owner` string so
/// that an upgrade can remove exactly what it added (`removeHooks(owner)`). They are run in
/// ascending `priority` order. For inserts the first non-Allow verdict wins.
///
/// Copying copies only the slots, never the hooks.
class BackpackInventory {
public:
    explicit BackpackInventory(int size = 0) : mSlots(static_cast<std::size_t>(size)) {}
    BackpackInventory(BackpackInventory const& o) : mSlots(o.mSlots) {}
    BackpackInventory& operator=(BackpackInventory const& o) {
        mSlots = o.mSlots;
        notify(-1);
        return *this;
    }
    BackpackInventory(BackpackInventory&&)            = default;
    BackpackInventory& operator=(BackpackInventory&&) = default;

    // ---- slots -------------------------------------------------------------------------------
    [[nodiscard]] int                  size() const { return static_cast<int>(mSlots.size()); }
    [[nodiscard]] ItemStackData const& at(int slot) const { return mSlots.at(static_cast<std::size_t>(slot)); }
    /// Unchecked overwrite (no filters, no limit check). Notifies listeners.
    void set(int slot, ItemStackData stack);
    /// Changes the slot count. Returns the stacks that no longer fit (when shrinking).
    std::vector<ItemStackData> resize(int newSize);
    [[nodiscard]] bool         isEmpty() const;

    /// Effective capacity of `slot` for `item` (>= 1): item.maxStack run through the limit modifiers.
    [[nodiscard]] int slotLimit(int slot, ItemStackData const& item) const;

    // ---- moving items --------------------------------------------------------------------------
    /// Inserts into any slots: first merges into existing stacks of the same item (in slot order),
    /// then fills empty slots.
    InsertResult insert(ItemStackData const& stack, AccessContext const& ctx, bool simulate = false);
    /// Inserts into one slot only (merges if the slot holds the same item).
    InsertResult insertInto(int slot, ItemStackData const& stack, AccessContext const& ctx, bool simulate = false);
    /// Takes up to `amount` from `slot`. Returns what was (or would be) taken.
    ItemStackData extract(int slot, int amount, AccessContext const& ctx, bool simulate = false);
    /// Takes up to `amount` items matching `pred` from any slots (last slot first).
    ItemStackData extractMatching(
        std::function<bool(ItemStackData const&)> const& pred,
        int                                               amount,
        AccessContext const&                              ctx,
        bool                                              simulate = false
    );
    /// Total count of items matching `pred`.
    [[nodiscard]] long long count(std::function<bool(ItemStackData const&)> const& pred) const;
    /// Merges equal stacks (respecting limits) and orders them with `less`; empty slots go last.
    void sort(std::function<bool(ItemStackData const&, ItemStackData const&)> const& less);

    /// True if a slot blocker (`addSlotBlocker`) blocks `slot`. Blocked slots refuse every insert
    /// (`checkInsert` returns Deny) and are skipped by `sort` / `sortInventory`.
    [[nodiscard]] bool isSlotBlocked(int slot) const;

    /// Checks whether an insert of `stack` into `slot` (-1 = whole-stack check) is allowed.
    [[nodiscard]] InsertVerdict checkInsert(int slot, ItemStackData const& stack, AccessContext const& ctx) const;
    [[nodiscard]] bool canExtract(int slot, int amount, AccessContext const& ctx) const;

    // ---- hooks -------------------------------------------------------------------------------
    void addInsertFilter(std::string owner, int priority, InsertFilterFn fn);
    void addExtractFilter(std::string owner, int priority, ExtractFilterFn fn);
    void addLimitModifier(std::string owner, int priority, LimitModifierFn fn);
    void addChangeListener(std::string owner, ChangeListenerFn fn);
    /// Preferred slots for automatic placement: `insert()` fills them before any other slot.
    void addSlotPreference(std::string owner, int priority, SlotPreferenceFn fn);
    /// Blocks slots (see `SlotBlockerFn`). Removed like the other hooks.
    void addSlotBlocker(std::string owner, SlotBlockerFn fn);
    /// Removes every hook registered with `owner`.
    void removeHooks(std::string const& owner);
    /// Removes all filters, limit modifiers, slot preferences and slot blockers (listeners stay).
    void clearFilters();
    /// Removes every hook (filters, modifiers, listeners) not owned by `keepOwner`.
    void clearHooksExcept(std::string_view keepOwner);

    // ---- persistence -----------------------------------------------------------------------------
    /// {"size":N,"items":[{"slot":i, ...ItemStackData}]}
    [[nodiscard]] nlohmann::json      toJson() const;
    [[nodiscard]] static BackpackInventory fromJson(nlohmann::json const& j);

private:
    template <class Fn>
    struct Hook {
        std::string owner;
        int         priority;
        Fn          fn;
    };
    template <class Fn>
    static void insertSorted(std::vector<Hook<Fn>>& v, std::string owner, int priority, Fn fn);

    void notify(int slot) const;
    /// Moves as much of `stack` as fits into slot `i` (no filter checks). Returns amount moved.
    int fillSlot(int i, ItemStackData const& stack, int amount, bool simulate);

    std::vector<ItemStackData>          mSlots;
    std::vector<Hook<InsertFilterFn>>   mInsertFilters;
    std::vector<Hook<ExtractFilterFn>>  mExtractFilters;
    std::vector<Hook<LimitModifierFn>>  mLimitModifiers;
    std::vector<Hook<ChangeListenerFn>> mListeners;
    std::vector<Hook<SlotPreferenceFn>> mPreferences;
    std::vector<Hook<SlotBlockerFn>>    mBlockers;
};

} // namespace abp::core
