#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/Tier.h"

class Player;

namespace abp::game {
class Backpack;
}

namespace abp::admin {

/// `config/admin.json`: server rules and admin commands.
struct AdminConfig {
    int version = 1;
    /// Minimum command permission level for the admin sub-commands of `/backpack` (list, give by
    /// uuid, remove, removenonplayer, openfor, info <uuid>, stats, cleanup):
    /// 0 = any player, 1 = operators (default), 2 = admin, 3 = host, 4 = owner/console.
    int commandPermissionLevel = 1;
    /// Items that can't be put into backpacks (by players, pickup, automation or upgrades).
    /// Item ids or globs (`*`, `?`); ids without a namespace mean "minecraft:".
    /// Example: ["minecraft:bundle", "minecraft:*_shulker_box", "somemod:*"]. Default: none (as SB).
    std::vector<std::string> disallowedItems{};
    /// Upgrade ids (or groups) that are disabled on this server: they can't be installed and
    /// installed ones stop working (no ticks, no pickups, no filters) until re-enabled.
    std::vector<std::string> disabledUpgrades{};
    /// Keep backpacks on death: carried backpacks are not dropped and come back on respawn.
    /// Default off (as SB). Ignored when the keepInventory game rule is on.
    bool keepBackpacksOnDeath = false;
    /// Max levels of backpacks inside backpacks when nesting is allowed (Inception upgrade or
    /// `general.allowNesting`). 1 = a backpack may hold backpacks that hold none. 0 = unlimited.
    int maxNestingDepth = 3;
    /// Ticks between scans that record which player carries which backpack (for `/backpack list`
    /// and `/backpack removenonplayer`).
    int holderScanInterval = 20;
};

// ---- rules ----------------------------------------------------------------------------------------

/// True if `itemId` matches the configured deny-list.
[[nodiscard]] bool isDisallowed(std::string_view itemId);
/// True if the upgrade id or group is disabled by config.
[[nodiscard]] bool isUpgradeDisabled(std::string_view id, std::string_view group);
/// How many levels of backpacks the backpack `uuid` contains (0 = none). Stops counting above `limit`.
[[nodiscard]] int nestedDepth(std::string const& uuid, int limit);
/// Rebuilds the hooks of every loaded backpack (after a rules change).
void applyRules();

// ---- holders --------------------------------------------------------------------------------------

struct Holder {
    std::string name;
    std::string uuid;
};

struct HolderRecord {
    std::optional<Holder> first;
    std::optional<Holder> last;
    std::int64_t          lastSeen = 0; ///< unix seconds
};

/// Records that `player` carries `backpack` (first / last holder, time). Writes only on change or
/// when the stored time is stale.
void recordHolder(game::Backpack& backpack, Player& player);
[[nodiscard]] HolderRecord holderOf(game::Backpack const& backpack);
/// The backpack is being inspected by an admin (`/backpack openfor`): viewing it in the GUI does not
/// make the admin a holder.
void markInspected(std::string const& uuid);
/// Opens `backpack`'s GUI for `player` after `ticks` server ticks (lets the chat screen that ran
/// the command close first; a container opened while chat is still up is ignored by the client).
void openLater(Player& player, std::shared_ptr<game::Backpack> backpack, std::string title, int ticks);
/// A player has carried this backpack (or it existed before access tracking started).
[[nodiscard]] bool playerAccessed(game::Backpack const& backpack);

// ---- queries / maintenance ------------------------------------------------------------------------

struct Summary {
    std::string  uuid;
    core::Tier   tier = core::Tier::Leather;
    int          slots = 0;
    int          usedSlots = 0;
    long long    items = 0;
    int          upgrades = 0;
    HolderRecord holders;
    bool         legacy = false; ///< stored before access tracking started
};

[[nodiscard]] Summary summarize(std::shared_ptr<game::Backpack> const& backpack);

/// Resolves a full uuid or a unique prefix (>= 4 characters) of a stored backpack's uuid.
/// On failure returns nullopt and puts a translation key + fallback into `errorKey`/`errorFallback`.
[[nodiscard]] std::optional<std::string>
resolveUuid(std::string const& input, std::string* errorKey = nullptr, std::string* errorFallback = nullptr);

/// Every stored backpack (optionally only those whose first/last holder is `holderName`, case-insensitive).
[[nodiscard]] std::vector<Summary> listBackpacks(std::string const& holderName = {});

/// Removes stored backpacks no player ever carried (mob or otherwise spawned ones; SB's
/// "removenonplayer"). Returns the removed uuids.
std::vector<std::string> removeNonPlayer();
/// Removes stored backpacks that hold nothing (no items, no upgrades, default colours) and that
/// nobody is using right now. Items bound to them get fresh storage on next open. Returns the uuids.
std::vector<std::string> cleanupEmpty();
/// Uuids carried right now by online players.
[[nodiscard]] std::vector<std::string> carriedNow();

struct Stats {
    std::size_t stored = 0;
    std::size_t loaded = 0;
    std::size_t perTier[6]{};
    long long   items = 0;
    std::size_t empty = 0;
    std::size_t neverAccessed = 0;
    std::size_t unreadable = 0;
    std::size_t deathStashes = 0;
};
[[nodiscard]] Stats stats();

// ---- keep on death --------------------------------------------------------------------------------

/// Takes `player`'s backpacks out of the inventory into a persistent stash (death). Returns how
/// many were stashed. Does nothing unless enabled (or `force`) and keepInventory is off.
int stashOnDeath(Player& player, bool force = false);
/// Gives stashed backpacks back (respawn). Returns how many.
int restoreStash(Player& player);
[[nodiscard]] bool hasStash(Player const& player);

} // namespace abp::admin
