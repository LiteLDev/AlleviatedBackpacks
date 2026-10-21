#pragma once

#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>

#include "core/Tier.h"
#include "game/PlayerScan.h"

class Container;
class ItemStack;
class Mob;
class Player;

namespace abp::game {
class Backpack;
}

namespace abp::wearable {

/// The chest armor slot, where worn backpacks live.
[[nodiscard]] game::ItemLocation wornSlot();
/// Opens the backpack the player wears (chest slot). False if none.
bool openWorn(Player& player);

// ---- mobs ---------------------------------------------------------------------------------------

/// User-data key marking a backpack put on a mob by spawning (removed when it drops).
inline constexpr std::string_view kMobWornKey = "abp_mob_worn";
/// User-data key holding the contents to generate on first open (JSON string).
inline constexpr std::string_view kPendingLootKey = "abp_pending_loot";

/// Contents generated lazily when the backpack is first opened.
struct PendingLoot {
    std::string   table;         ///< "chests/simple_dungeon" (empty = no loot)
    int           rolls     = 1; ///< times the table is rolled
    int           dimension = 0;
    unsigned      seed      = 0;
    bool          upgrades  = false; ///< install random upgrades
};

[[nodiscard]] bool                       isMobWorn(ItemStack const& stack);
[[nodiscard]] std::optional<PendingLoot> pendingLoot(ItemStack const& stack);
/// A backpack item of `tier` carrying `loot` and the mob marker (no uuid: storage is created on
/// first open).
[[nodiscard]] ItemStack makeMobBackpack(core::Tier tier, PendingLoot const& loot);
/// Generates `loot` into `backpack` (loot table rolls, random upgrades).
void fillBackpack(std::shared_ptr<game::Backpack> const& backpack, PendingLoot const& loot);

/// The mob type may wear backpacks (config `mobs.entities`).
[[nodiscard]] bool isEligibleMob(std::string_view typeName);
/// Spawn chance for the current difficulty.
[[nodiscard]] double spawnChance(int difficulty);
/// Picks a tier for a mob in `dimension` (0 overworld, 1 nether, 2 end) at `difficulty`.
[[nodiscard]] std::optional<core::Tier> rollTier(int dimension, int difficulty, std::mt19937& rng);
/// Puts a backpack (and armor/effects per config) on `mob`. `tier` overrides the roll.
bool equipMob(Mob& mob, std::optional<core::Tier> tier = std::nullopt);
/// Handles a mob's death: removes a spawned backpack from its chest slot and drops it if a player
/// killed it (or by config chance). Returns true if a backpack was dropped.
bool handleMobDeath(Mob& mob, bool killedByPlayer);

/// Shared RNG of the unit (server thread).
[[nodiscard]] std::mt19937& rng();

// ---- chest loot ---------------------------------------------------------------------------------

/// "loot_tables/chests/x.json" -> "chests/x".
[[nodiscard]] std::string normalizeTableName(std::string_view tableName);
/// Adds a configured backpack/upgrade to `container` (random empty slot) by chance. Returns the
/// id added, or "".
std::string injectChestLoot(std::string_view tableName, Container& container);

/// While alive, loot fills don't get backpacks injected (our own fills).
struct SuppressInjection {
    SuppressInjection();
    ~SuppressInjection();
    SuppressInjection(SuppressInjection const&)            = delete;
    SuppressInjection& operator=(SuppressInjection const&) = delete;
};
[[nodiscard]] bool injectionSuppressed();

} // namespace abp::wearable
