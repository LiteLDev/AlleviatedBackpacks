#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "core/ItemStackData.h"
#include "core/Tier.h"
#include "game/PlayerScan.h"

class Actor;
class Container;
class FilterGroup;
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

// ---- piglins (SB BackpackItem.makesPiglinsNeutral) ----------------------------------------------

/// True if `wearer` is a player wearing a Gold Backpack in the chest slot (counts as gold armour).
[[nodiscard]] bool makesPiglinsNeutral(Actor const& wearer);
/// True if a target filter tests for golden armour (`has_equipment` ... `golden_*`).
[[nodiscard]] bool isGoldArmorFilter(FilterGroup const& group);
/// How many times a piglin target match was refused for a Gold Backpack wearer (selftests).
[[nodiscard]] int piglinTargetsSkipped();

// ---- mobs (SB EntityBackpackAdditionHandler) ----------------------------------------------------

/// User-data key marking a backpack put on a mob by spawning (SB: spawnedWithBackpack).
inline constexpr std::string_view kMobWornKey = "abp_mob_worn";
/// User-data key marking a mob backpack that got a jukebox upgrade (SB: spawnedWithJukeboxUpgrade).
inline constexpr std::string_view kMobJukeboxKey = "abp_mob_jukebox";
/// User-data key holding the contents to generate on first open (JSON string).
inline constexpr std::string_view kPendingLootKey = "abp_pending_loot";

/// Contents generated lazily when the backpack is first opened (SB: LOOT_TABLE + LOOT_FACTOR,
/// colours, and the CONTAINER items a dropped jukebox mob backpack carries).
struct PendingLoot {
    std::string                      table;          ///< "chests/simple_dungeon" (empty = no loot)
    float                            lootFactor = 0; ///< partial rolls / 3: 1 = one full roll
    int                              dimension  = 0;
    unsigned                         seed       = 0;
    bool                             upgrades   = false; ///< AlleviatedBackpacks extra: random upgrades
    std::optional<std::uint32_t>     mainColor;          ///< ARGB, from the spawn egg
    std::optional<std::uint32_t>     accentColor;
    std::vector<core::ItemStackData> extraItems; ///< put into the backpack on first open
};

[[nodiscard]] bool                       isMobWorn(ItemStack const& stack);
[[nodiscard]] std::optional<PendingLoot> pendingLoot(ItemStack const& stack);
void                                     setPendingLoot(ItemStack& stack, PendingLoot const& loot);
/// A backpack item of `tier` carrying `loot` and the mob marker (no uuid: storage is created on
/// first open).
[[nodiscard]] ItemStack makeMobBackpack(core::Tier tier, PendingLoot const& loot);
/// Generates `loot` into `backpack` (SB: loot table rolls scaled by the loot factor, backpacks
/// removed, random empty slots, overflow voided; colours; extra items).
void fillBackpack(std::shared_ptr<game::Backpack> const& backpack, PendingLoot const& loot);
/// Fills a bound backpack item's pending loot now (first GUI open of a backpack that already has
/// storage, e.g. a jukebox mob's). Returns true if the stack changed (caller writes it back).
bool consumePendingLoot(ItemStack& stack);

/// SB `entityBackpackAdditions.entityLootTableList`: the mob type may wear backpacks.
[[nodiscard]] bool isEligibleMob(std::string_view typeName);
/// The loot table for a mob type ("" = listed with "null"; nullopt = not listed).
[[nodiscard]] std::optional<std::string> lootTableFor(std::string_view typeName);
/// Java spawn egg colours (background, highlight; RGB) of a vanilla mob type.
[[nodiscard]] std::optional<std::pair<std::uint32_t, std::uint32_t>> spawnEggColors(std::string_view typeName);
/// Vanilla local difficulty at the mob's position (no chunk inhabited time on Bedrock).
[[nodiscard]] float effectiveDifficultyAt(Mob& mob);
/// SB's tier choice for a mob at an effective difficulty.
[[nodiscard]] core::Tier rollTier(float effectiveDifficulty, std::mt19937& rng);
/// Puts a backpack on `mob` with everything SB adds (colours, lazy loot, jukebox, potion effects,
/// health, armor). `tier` overrides the roll, `jukebox` the 1/4 chance.
bool equipMob(Mob& mob, std::optional<core::Tier> tier = std::nullopt, std::optional<bool> jukebox = std::nullopt);
/// Handles a mob's death: drops the backpack when a player killed it and the drop chance hits
/// (SB handleBackpackDrop); otherwise the backpack's storage goes away. Returns true if dropped.
bool handleMobDeath(Mob& mob, Player* killer);

/// Shared RNG of the unit (server thread).
[[nodiscard]] std::mt19937& rng();

// ---- GX-2: conversions and creepers (SB handleLivingConversion / removeBeneficialEffects) -------

/// User-data key of a worn mob backpack: the max health bonus its wearer got (SB's
/// BACKPACK_BEARER_HEALTH_BONUS modifier; float).
inline constexpr std::string_view kMobHealthBonusKey = "abp_mob_health_bonus";

/// SB handleLivingConversion: the mob `sourceId` (wearing a mob backpack) turned into `outcome`.
/// The outcome wears the backpack with its flags (and its jukebox keeps playing from it) and gets
/// the health bonus, keeping the health ratio. Called by the transformation hook.
void transferToConversion(
    std::int64_t       sourceId,
    std::string const& sourceType,
    Mob&               outcome,
    ItemStack const&   backpack,
    float              sourceHealth,
    float              sourceMaxHealth
);
/// SB removeBeneficialEffects: a creeper that spawned with a backpack loses its beneficial
/// effects when it explodes (before its effect cloud is made). Returns how many were removed.
int stripBeneficialEffects(Mob& creeper);
/// The last creeper explosion the hook saw (selftests): effects removed / kept.
struct CreeperStrip {
    int removed = -1;
    int kept    = -1;
};
[[nodiscard]] CreeperStrip lastCreeperStrip();

// ---- chest loot (SB inject tables) ----------------------------------------------------------------

/// "loot_tables/chests/x.json" -> "chests/x".
[[nodiscard]] std::string normalizeTableName(std::string_view tableName);
/// SB's inject table for a Bedrock loot table: one roll over (item, weight) plus an empty weight.
struct InjectEntry {
    std::string_view item;
    int              weight;
};
struct InjectTable {
    std::vector<InjectEntry> entries;
    int                      emptyWeight;
};
[[nodiscard]] InjectTable const* injectTableFor(std::string_view normalizedTable);
/// Rolls the inject table of `tableName` (gated by `chestLootEnabled`) and puts the item into a
/// random empty slot of `container`. Returns the id added, or "".
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
