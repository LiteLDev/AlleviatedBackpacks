#pragma once

#include <concepts>
#include <memory>
#include <optional>
#include <string>
#include <typeinfo>
#include <vector>

#include "nlohmann/json.hpp"

#include "core/BackpackData.h"
#include "game/Backpack.h"
#include "game/PlayerScan.h"
#include "mod/Registry.h"

class Player;
class BlockSource;

namespace abp::ui {
class Menu;
}

namespace abp::upgrade {

enum class UpgradeTier { Basic, Advanced };

/// Static description of an upgrade type.
struct UpgradeInfo {
    std::string id;           ///< "pickup", "advanced_pickup" (unique, lowercase)
    std::string itemId;       ///< "alleviatedbackpacks:pickup_upgrade" (custom item in the BP)
    std::string nameKey;      ///< lang key, e.g. "abp.upgrade.pickup.name"
    std::string nameFallback; ///< English fallback, e.g. "Pickup Upgrade"
    UpgradeTier tier = UpgradeTier::Basic;
    /// Upgrades with the same group count together against `maxPerBackpack` (e.g. basic and
    /// advanced pickup share group "pickup"). Empty = `id`.
    std::string group;
    int         maxPerBackpack = 1;
    /// Ids or groups that can't be installed together with this one (checked both ways).
    std::vector<std::string> conflicts;
    bool                     hasSettings = false; ///< show a settings button on the upgrades page
    bool                     toggleable  = true;  ///< show an on/off toggle

    [[nodiscard]] std::string const& groupOrId() const { return group.empty() ? id : group; }
};

/// Identifies one installed upgrade: a backpack plus an upgrade-slot index. Cheap to copy and safe
/// to capture in callbacks (it holds a shared_ptr to the backpack). `player` is the acting or
/// carrying player when known; never keep it beyond the current call.
class UpgradeContext {
public:
    UpgradeContext(std::shared_ptr<game::Backpack> backpack, int slot, Player* player = nullptr)
    : mBackpack(std::move(backpack)),
      mSlot(slot),
      mPlayer(player) {}

    [[nodiscard]] game::Backpack&                        backpack() const { return *mBackpack; }
    [[nodiscard]] std::shared_ptr<game::Backpack> const& backpackPtr() const { return mBackpack; }
    [[nodiscard]] int                                    slot() const { return mSlot; }
    [[nodiscard]] Player*                                player() const { return mPlayer; }
    [[nodiscard]] UpgradeContext                         withPlayer(Player* p) const { return {mBackpack, mSlot, p}; }

    /// The persisted slot state (item, enabled flag, settings).
    [[nodiscard]] core::UpgradeSlot& state() const { return mBackpack->data().upgrades.at(static_cast<std::size_t>(mSlot)); }
    /// This upgrade's settings JSON (persisted with the backpack; travels with the item on removal).
    [[nodiscard]] nlohmann::json& settings() const { return state().settings; }
    /// Call after changing `settings()`. `rebuildHooks` re-runs every `attach()` (needed when the
    /// change affects filters / limits).
    void saveSettings(bool rebuildHooks = false) const {
        mBackpack->markDirty();
        if (rebuildHooks) mBackpack->rebuildHooks();
    }
    /// Owner string for inventory hooks registered in `attach()`.
    [[nodiscard]] std::string hookOwner() const { return "upgrade:" + std::to_string(mSlot); }
    /// Runtime-only per-upgrade state (cooldowns, caches); reset when the backpack unloads.
    template <class T>
    [[nodiscard]] T& transient() const {
        return mBackpack->transient<T>(hookOwner() + ":" + typeid(T).name());
    }

private:
    std::shared_ptr<game::Backpack> mBackpack;
    int                             mSlot;
    Player*                         mPlayer;
};

/// Context for periodic ticks of a carried backpack.
struct TickContext {
    UpgradeContext     ctx;
    Player&            player;   ///< the carrier
    game::ItemLocation where;    ///< where the backpack item is on the player
    int                interval; ///< game ticks since this backpack's previous tick
    /// > 0: this backpack is stored inside a carried backpack (Inception); `where` is then the
    /// outermost carried backpack's item, not this backpack's: don't write to it.
    int                nestedDepth = 0;
};

/// Context for periodic ticks of a backpack placed as a block (see `feature/placed`). There is
/// no carrier: `ctx.player()` is null.
struct PlacedTickContext {
    UpgradeContext ctx;
    BlockSource&   region;    ///< block source of the backpack's dimension
    int            dimension; ///< DimensionType id
    int            x, y, z;   ///< position of the backpack block
    int            facing;    ///< direction the backpack's front faces: 0 south, 1 west, 2 north, 3 east
    int            interval;  ///< game ticks since this backpack's previous placed tick
};

/// Behaviour of one upgrade type. Implementations are stateless singletons: all per-instance
/// state goes into `ctx.settings()` (persisted) or `ctx.transient<T>()` (runtime). Register with
/// `ABP_REGISTER_UPGRADE(MyUpgrade)` in the .cpp file.
///
/// Every hook runs on the server thread. Hooks other than install/remove run only while the
/// upgrade is installed AND enabled.
class IUpgrade {
public:
    virtual ~IUpgrade() = default;

    [[nodiscard]] virtual UpgradeInfo const& info() const = 0;

    // ---- lifecycle --------------------------------------------------------------------------
    /// Extra install checks (slot limits and conflicts are already checked by the manager).
    virtual bool canInstall(UpgradeContext const& /*ctx*/, std::string& /*reason*/) const { return true; }
    /// Like `canInstall`, with the item being installed (count 1; settings it carries from a
    /// previous removal are still embedded: read them with `upgrade::settingsOf(item)`). E.g. a
    /// Tank upgrade refuses a backpack too small for its contents. Default: `canInstall`.
    virtual bool canInstallItem(UpgradeContext const& ctx, core::ItemStackData const& /*item*/, std::string& reason) const {
        return canInstall(ctx, reason);
    }
    virtual void onInstall(UpgradeContext const& /*ctx*/) {}
    /// E.g. a Stack upgrade refuses removal while slots hold more than the base limit.
    virtual bool canRemove(UpgradeContext const& /*ctx*/, std::string& /*reason*/) const { return true; }
    virtual void onRemove(UpgradeContext const& /*ctx*/) {}
    /// Like `canRemove`, when the upgrade is being swapped for `replacement` in the GUI (the
    /// replacement still has to pass `canInstall` afterwards). E.g. a Stack upgrade allows a swap
    /// for a higher tier even while slots hold more than the base limit. Default: `canRemove`.
    virtual bool canReplaceWith(UpgradeContext const& ctx, core::ItemStackData const& /*replacement*/, std::string& reason) const {
        return canRemove(ctx, reason);
    }
    /// The player clicks this installed upgrade's slot in the GUI while holding `item` on the
    /// cursor (e.g. a Stack upgrade conversion item). Return nullopt to let the normal swap happen,
    /// or how many of `item` were used up (the cursor loses that many; 0 = refused) — then
    /// nothing is swapped.
    virtual std::optional<int> useItemOn(Player& /*player*/, UpgradeContext const& /*ctx*/, core::ItemStackData const& /*item*/) {
        return std::nullopt;
    }

    // ---- runtime ----------------------------------------------------------------------------
    /// Register inventory hooks (insert/extract filters, limit modifiers) with
    /// `ctx.hookOwner()` as owner, and capabilities via `ctx.backpack().addCapability()`.
    /// Called on every hook rebuild (load, upgrade changes).
    virtual void attach(UpgradeContext const& /*ctx*/, core::BackpackInventory& /*inventory*/) {}
    /// Periodic tick while the backpack is carried (interval: `GeneralConfig::upgradeTickInterval`).
    virtual void onTick(TickContext const& /*tick*/) {}
    /// A carrier is picking up `item` from the ground. Take what you want by reducing
    /// `item.count` (e.g. insert into the backpack with `Source::Pickup`). Called before the
    /// vanilla pickup; whatever is left goes to the player inventory as usual.
    virtual void onPickup(UpgradeContext const& /*ctx*/, core::ItemStackData& /*item*/) {}
    /// Periodic tick while the backpack is placed as a block in a loaded chunk (interval:
    /// `placed.upgradeTickInterval`). Override it for upgrades that work without a carrier
    /// (smelting, hoppers, ...). `onTick` is never called for placed backpacks.
    virtual void onPlacedTick(PlacedTickContext const& /*tick*/) {}

    // ---- UI ---------------------------------------------------------------------------------
    /// Settings button on the upgrades page. Default: opens `createSettingsMenu()` if it returns
    /// a menu. Override to show a form instead (close the chest menu first; see ui::MenuManager).
    virtual void openSettings(Player& player, UpgradeContext const& ctx);
    /// A chest menu for this upgrade's settings (e.g. a `ui::FilterMenu`), or nullptr.
    [[nodiscard]] virtual std::shared_ptr<ui::Menu> createSettingsMenu(Player& /*player*/, UpgradeContext const& /*ctx*/) {
        return nullptr;
    }
    /// Extra lore lines for the installed upgrade's icon (e.g. "Filter: 3 items").
    [[nodiscard]] virtual std::vector<std::string> describe(Player& /*player*/, UpgradeContext const& /*ctx*/) const {
        return {};
    }
};

// ---- registry -----------------------------------------------------------------------------------
using UpgradeRegistry = StaticRegistry<std::shared_ptr<IUpgrade>>;

/// All registered upgrades.
[[nodiscard]] std::vector<std::shared_ptr<IUpgrade>> const& all();
[[nodiscard]] IUpgrade* byId(std::string_view id);
[[nodiscard]] IUpgrade* byItemId(std::string_view itemId);

/// Registers an upgrade for the lifetime of this object (selftests with throwaway upgrade types).
class ScopedUpgrade {
public:
    explicit ScopedUpgrade(std::shared_ptr<IUpgrade> upgrade);
    ~ScopedUpgrade();
    ScopedUpgrade(ScopedUpgrade const&)            = delete;
    ScopedUpgrade& operator=(ScopedUpgrade const&) = delete;

private:
    std::shared_ptr<IUpgrade> mUpgrade;
};

namespace detail {
template <std::derived_from<IUpgrade> T>
struct UpgradeRegistrar {
    UpgradeRegistrar() { UpgradeRegistry::add(std::make_shared<T>()); }
};
} // namespace detail

} // namespace abp::upgrade

/// Registers upgrade type `T` (default-constructible, derived from `abp::upgrade::IUpgrade`).
#define ABP_REGISTER_UPGRADE(T) static ::abp::upgrade::detail::UpgradeRegistrar<T> ABP_UNIQUE(abp_upgrade_registrar_)
