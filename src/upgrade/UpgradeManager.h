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

} // namespace abp::upgrade
