#pragma once

#include <any>
#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>

#include "core/BackpackData.h"

namespace abp::game {

/// A loaded backpack: persisted data plus runtime state. Owned by `Storage` through
/// `std::shared_ptr`; hold a shared_ptr (never a raw pointer) across ticks.
///
/// - Mutating the inventory marks the backpack dirty automatically (change listener).
/// - Mutating anything else in `data()` requires `markDirty()`.
/// - `revision()` increases on every change; menus use it to know when to redraw.
/// - Runtime hooks (inventory filters, limit modifiers, capabilities) are rebuilt by
///   `rebuildHooks()` from the installed, enabled upgrades. Never add hooks by hand from outside
///   an upgrade's `attach()`, they would be dropped on the next rebuild.
class Backpack : public std::enable_shared_from_this<Backpack> {
public:
    explicit Backpack(core::BackpackData data);
    Backpack(Backpack const&)            = delete;
    Backpack& operator=(Backpack const&) = delete;

    [[nodiscard]] std::string const&       uuid() const { return mData.uuid; }
    [[nodiscard]] core::Tier               tier() const { return mData.tier; }
    [[nodiscard]] core::BackpackData&      data() { return mData; }
    [[nodiscard]] core::BackpackData const& data() const { return mData; }
    [[nodiscard]] core::BackpackInventory& inventory() { return mData.inventory; }

    void                        markDirty();
    [[nodiscard]] bool          dirty() const { return mDirty; }
    void                        clearDirty() { mDirty = false; }
    [[nodiscard]] std::uint64_t revision() const { return mRevision; }

    /// Re-runs the hook builder (upgrade `attach()`es + foundation rules). Call after any change to
    /// upgrades (install/remove/toggle/settings that affect filters).
    void rebuildHooks();

    /// Capabilities are string flags set by upgrades during `attach()` (e.g. "allow_nesting" from
    /// an Inception upgrade). Cleared on every rebuild.
    void                                 addCapability(std::string cap) { mCapabilities.insert(std::move(cap)); }
    [[nodiscard]] bool                   hasCapability(std::string_view cap) const;
    [[nodiscard]] std::set<std::string, std::less<>> const& capabilities() const { return mCapabilities; }

    /// Runtime-only state for upgrades (cooldowns, caches), keyed by e.g. "<upgrade id>:<slot>".
    /// Lost when the backpack is unloaded.
    template <class T>
    T& transient(std::string const& key) {
        auto& slot = mTransient[key];
        if (!slot.has_value() || slot.type() != typeid(T)) slot = T{};
        return *std::any_cast<T>(&slot);
    }
    /// Drops the entry `key` and every entry whose key starts with `key + ":"`.
    void clearTransient(std::string const& key);

    /// The function `rebuildHooks()` runs (installed by the upgrade module).
    using HookBuilder = std::function<void(Backpack&)>;
    static void setHookBuilder(HookBuilder fn);

private:
    core::BackpackData                    mData;
    bool                                  mDirty    = false;
    std::uint64_t                         mRevision = 0;
    std::set<std::string, std::less<>>    mCapabilities;
    std::unordered_map<std::string, std::any> mTransient;
};

} // namespace abp::game
