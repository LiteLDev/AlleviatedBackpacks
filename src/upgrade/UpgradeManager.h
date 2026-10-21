#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "upgrade/IUpgrade.h"

namespace abp::upgrade {

/// Installing / removing / toggling upgrades in a backpack. The GUI uses these; feature units can
/// too (e.g. selftests). All functions run on the server thread.

/// The upgrade type of an upgrade item, or nullptr.
[[nodiscard]] IUpgrade* upgradeFor(core::ItemStackData const& item);

/// Whether `item` may go into upgrade slot `slot` (which must be empty). `reason` gets a
/// translated message for `player` (or the default locale) on failure.
[[nodiscard]] bool canInstall(
    std::shared_ptr<game::Backpack> const& backpack,
    int                                    slot,
    core::ItemStackData const&             item,
    Player*                                player,
    std::string*                           reason = nullptr
);

/// Installs one upgrade item (count 1) into an empty slot. Settings carried by the item (from a
/// previous removal) are restored. Rebuilds hooks.
bool install(
    std::shared_ptr<game::Backpack> const& backpack,
    int                                    slot,
    core::ItemStackData const&             item,
    Player*                                player,
    std::string*                           reason = nullptr
);

/// Removes the upgrade in `slot`. Returns the item to give back (with its settings embedded so
/// they survive moving the upgrade to another backpack), or nullopt if it can't be removed.
std::optional<core::ItemStackData>
remove(std::shared_ptr<game::Backpack> const& backpack, int slot, Player* player, std::string* reason = nullptr);

/// Swaps the upgrade in `slot` for `item` (count 1) in one step: the old one must allow it
/// (`IUpgrade::canReplaceWith`) and the new one must pass `canInstall` as if the slot were free.
/// Returns the old item (settings embedded, like `remove`), or nullopt with nothing changed.
std::optional<core::ItemStackData> replace(
    std::shared_ptr<game::Backpack> const& backpack,
    int                                    slot,
    core::ItemStackData const&             item,
    Player*                                player,
    std::string*                           reason = nullptr
);

/// Turns an installed upgrade on or off. Rebuilds hooks.
void setEnabled(std::shared_ptr<game::Backpack> const& backpack, int slot, bool enabled);

/// Calls `fn` for every installed upgrade (`activeOnly`: only enabled ones).
void forEachInstalled(
    std::shared_ptr<game::Backpack> const&                    backpack,
    bool                                                      activeOnly,
    std::function<void(IUpgrade&, UpgradeContext const&)> const& fn
);

/// True if an enabled upgrade with this id (or group) is installed.
[[nodiscard]] bool hasActive(game::Backpack const& backpack, std::string_view idOrGroup);

/// Display name of an upgrade (translated for `player` when given).
[[nodiscard]] std::string displayName(IUpgrade const& upgrade, Player const* player = nullptr);

/// Lets the player's carried backpacks' upgrades take items being picked up. Returns how many
/// items were taken (item.count is reduced accordingly).
int dispatchPickup(Player& player, core::ItemStackData& item);

// ---- server rules (append-only extension points, used by the admin unit) ------------------------

/// Server-wide gate for upgrade types (e.g. a config deny-list). A type the gate rejects can't be
/// installed and behaves as switched off everywhere: no `attach()`, no ticks, no pickups,
/// `hasActive()` is false. Default (no gate): every type is allowed. Rebuild the hooks of loaded
/// backpacks after changing what the gate returns.
using TypeGate = std::function<bool(IUpgrade const&)>;
void setTypeGate(TypeGate gate);
/// True if the gate allows this upgrade type.
[[nodiscard]] bool typeAllowed(IUpgrade const& upgrade);

/// Extra backpack-wide rules applied on every hook rebuild, after the foundation rule and before
/// the upgrades' `attach()`. `fn` registers inventory hooks (use an owner such as "abp:<unit>").
/// Re-adding an `id` replaces it. Rebuild the hooks of loaded backpacks after (un)registering.
using RuleBuilder = std::function<void(game::Backpack&)>;
void addRuleBuilder(std::string id, RuleBuilder fn);
void removeRuleBuilder(std::string const& id);

} // namespace abp::upgrade
