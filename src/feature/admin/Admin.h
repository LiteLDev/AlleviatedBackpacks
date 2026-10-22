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

/// `config/admin.json`: AlleviatedBackpacks' own admin settings. Sophisticated Backpacks' server
/// rules (disallowed items, slot counts, nerfs, ...) live in `sophisticatedbackpacks-server.json`
/// (see SbConfig.h); the keys here are extras SB doesn't have, all off / neutral by default.
struct AdminConfig {
    int version = 2;
    /// Minimum command permission level for the admin extras of `/backpack` (info <uuid>, remove,
    /// openfor, stats, cleanup): 0 = any player, 1 = operators (default), 2 = admin, 3 = host,
    /// 4 = owner/console. `/sbp` always needs operator rights (SB: op level 2).
    int commandPermissionLevel = 1;
    /// Deprecated (kept so existing deny-lists keep working): merged with SB's `disallowedItems` in
    /// `sophisticatedbackpacks-server.json`, where new entries belong.
    std::vector<std::string> disallowedItems{};
    /// Upgrade ids (or groups) that are disabled on this server: they can't be installed and
    /// installed ones stop working (no ticks, no pickups, no filters) until re-enabled. Not in SB
    /// (Sophisticated Core's `enabledItems` disables crafting instead); default: none.
    std::vector<std::string> disabledUpgrades{};
    /// Keep backpacks on death: carried backpacks are not dropped and come back on respawn.
    /// Not in SB; default off. Ignored when the keepInventory game rule is on.
    bool keepBackpacksOnDeath = false;
    /// Max levels of backpacks inside backpacks (Inception upgrade or `general.allowNesting`).
    /// Not in SB (no limit there); default 0 = unlimited.
    int maxNestingDepth = 0;
    /// Ticks between scans that record which player carries which backpack (`/backpack info`).
    int holderScanInterval = 20;
};

// ---- rules ----------------------------------------------------------------------------------------

/// True if `itemId` may not go into backpacks: SB `disallowedItems` (ids, or globs `*`/`?`; no
/// namespace means "minecraft:"), or a container item (shulker box) while
/// `containerItemsDisallowed` is on.
[[nodiscard]] bool isDisallowed(std::string_view itemId);
/// True if the upgrade id or group is disabled by config.
[[nodiscard]] bool isUpgradeDisabled(std::string_view id, std::string_view group);
/// SB `maxUpgradesPerStorage`: false (and `reason`, SC's "Only %dx %s can be used in %s") when one
/// more upgrade (item id `upgradeItemId`, group `group`) would exceed a configured maximum.
[[nodiscard]] bool withinMaxUpgrades(
    game::Backpack const& backpack,
    std::string_view      upgradeItemId,
    std::string_view      group,
    Player*               player,
    std::string*          reason
);
/// One SB `maxUpgradesPerStorage` limit reached by adding an upgrade (SC
/// `checkUpgradePerStorageTypeLimit`): the limit (0 = not allowed in this storage), whether it is
/// the item's own entry (else the group's) and the upgrade slots already counting against it
/// (the slots SC highlights).
struct UpgradeLimitHit {
    int              max    = 0;
    bool             byItem = false;
    std::vector<int> slots;
};
/// The limit one more upgrade (`upgradeItemId`, `group`) would exceed, ignoring upgrade slot
/// `excludeSlot`; nullopt when it fits.
[[nodiscard]] std::optional<UpgradeLimitHit>
maxUpgradesHit(game::Backpack const& backpack, std::string_view upgradeItemId, std::string_view group, int excludeSlot = -1);
/// SC's message for `hit` ("Only 3x Stack Upgrades can be used in Backpack", "... can't be used in ...").
[[nodiscard]] std::string maxUpgradesMessage(
    UpgradeLimitHit const& hit,
    game::Backpack const&  backpack,
    std::string_view       upgradeItemId,
    std::string_view       group,
    Player*                player
);
/// "Iron Backpack" (the item name of a tier, in `player`'s language when given).
[[nodiscard]] std::string backpackDisplayName(core::Tier tier, Player const* player = nullptr);
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
/// make the admin a holder, nor an access log entry.
void markInspected(std::string const& uuid);
[[nodiscard]] bool isInspected(std::string const& uuid);
/// Opens `backpack`'s GUI for `player` after `ticks` server ticks (lets the chat screen that ran
/// the command close first; a container opened while chat is still up is ignored by the client).
void openLater(Player& player, std::shared_ptr<game::Backpack> backpack, std::string title, int ticks);
/// A player has opened this backpack (SB access log), carried it, or it existed before tracking.
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

/// SB `/sbp removeNonPlayer <onlyWithEmptyInventory>`: removes stored backpacks no player ever
/// opened (no access log record), with `onlyWithEmptyInventory` only those holding nothing (no
/// items, no upgrades, no other data). Linked-storage groups and backpacks in use are kept.
/// Returns the removed uuids.
std::vector<std::string> removeNonPlayer(bool onlyWithEmptyInventory = false);
/// SB BackpackStorage.load (`isPlayerBackpackOrNotEmpty`): deletes stored backpacks without an
/// access log record that hold nothing (no items, no upgrades, default colours and settings).
/// Runs once per world load (logs the count). Placed, linked and in-use backpacks are kept.
std::vector<std::string> dropEmptyStorages();
/// Settings key marking an unopened mob backpack whose loot is still to be generated (counts as
/// data for `dropEmptyStorages`).
inline constexpr std::string_view kPendingLootSetting = "pendingLoot";
/// Removes stored backpacks that hold nothing (no items, no upgrades, default colours) and that
/// nobody is using right now. Items bound to them get fresh storage on next open. Returns the uuids.
std::vector<std::string> cleanupEmpty();
/// Uuids carried right now by online players.
[[nodiscard]] std::vector<std::string> carriedNow();

/// Storage uuids that maintenance must never delete (e.g. linked-storage groups). Providers are
/// registered by other units at static init.
using ProtectedUuidProvider = std::vector<std::string> (*)();
void addProtectedUuidProvider(ProtectedUuidProvider provider);

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
