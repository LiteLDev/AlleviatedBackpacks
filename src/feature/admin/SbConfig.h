#pragma once

// Sophisticated Backpacks / Sophisticated Core server and common configuration, key for key.
//
//   config/sophisticatedbackpacks-server.json   SB `Config.Server` ("server" section, without the
//                                                per-upgrade sections: each upgrade unit keeps its
//                                                own file with SB's keys for that upgrade)
//   config/sophisticatedbackpacks-common.json   SB `Config.Common`
//   config/sophisticatedcore-common.json        SC `Config.Common`
//
// Names, defaults and ranges are SB's. Values outside SB's range are clamped where they are read
// (the accessors below), like NeoForge's defineInRange would reject them.

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "core/Tier.h"

class ItemStackBase;

namespace abp::admin {

struct SbServerConfig {
    int version = 1;

    /// List of items that are not allowed to be put in backpacks - e.g. "minecraft:shulker_box".
    /// (AlleviatedBackpacks also accepts globs: "minecraft:*_shulker_box".)
    std::vector<std::string> disallowedItems{};
    /// Determines if container items (those that override canFitInsideContainerItems to false) are
    /// able to fit in backpacks. On Bedrock these are the shulker boxes.
    bool containerItemsDisallowed = false;
    /// List of blocks that inventory interaction upgrades can't interact with - e.g.
    /// "minecraft:shulker_box".
    std::vector<std::string> noInteractionBlocks{};
    /// List of blocks that are not allowed to connect to backpacks - e.g. "minecraft:hopper".
    std::vector<std::string> noConnectionBlocks{};
    /// If true, disallows all blocks from connecting to backpacks.
    bool allBlockConnectionsDisallowed = false;

    struct BackpackSettings {
        /// Number of inventory slots in the backpack (1..144).
        int inventorySlotCount;
        /// Number of upgrade slots in the backpack (0..10).
        int upgradeSlotCount;
    };
    BackpackSettings leatherBackpack{27, 1};
    BackpackSettings copperBackpack{45, 1};
    BackpackSettings ironBackpack{54, 2};
    BackpackSettings goldBackpack{81, 3};
    BackpackSettings diamondBackpack{108, 5};
    BackpackSettings netheriteBackpack{120, 7};

    /// Settings for Spawning Entities with Backpack.
    struct EntityBackpackAdditions {
        /// Chance of an entity spawning with Backpack (0..1).
        double chance                = 0.01;
        /// Turns on/off addition of loot into backpacks.
        bool   addLoot               = true;
        /// Turns on/off buffing the entity that wears backpack with potion effects. These are
        /// scaled based on how much loot is added.
        bool   buffWithPotionEffects = true;
        /// Turns on/off buffing the entity that wears backpack with additional health. Health is
        /// scaled based on backpack tier the mob wears.
        bool   buffHealth            = true;
        /// Turns on/off equipping the entity that wears backpack with armor. What armor material
        /// and how enchanted it is scales based on backpack tier the mob wears.
        bool   equipWithArmor        = true;
        /// Map of entities that can spawn with backpack and related loot tables (if adding a loot
        /// is enabled) in format of "EntityRegistryName|LootTableName" (Bedrock loot table paths
        /// without "loot_tables/" and ".json"; "null" = no loot).
        std::vector<std::string> entityLootTableList = {
            "minecraft:creeper|chests/desert_pyramid",
            "minecraft:drowned|chests/shipwrecktreasure",
            "minecraft:enderman|chests/end_city_treasure",
            "minecraft:evocation_illager|chests/woodland_mansion",
            "minecraft:husk|chests/desert_pyramid",
            "minecraft:piglin|chests/bastion_bridge",
            "minecraft:piglin_brute|chests/bastion_treasure",
            "minecraft:pillager|chests/pillager_outpost",
            "minecraft:skeleton|chests/simple_dungeon",
            "minecraft:stray|chests/igloo_chest",
            "minecraft:vex|chests/woodland_mansion",
            "minecraft:vindicator|chests/woodland_mansion",
            "minecraft:witch|chests/buriedtreasure",
            "minecraft:wither_skeleton|chests/nether_bridge",
            "minecraft:zombie|chests/simple_dungeon",
            "minecraft:zombie_villager_v2|chests/village/village_armorer",
            "minecraft:zombie_pigman|chests/bastion_other",
        };
        /// List of music discs that are not supposed to be played by entities.
        std::vector<std::string> discBlockList = {"botania:record_gaia_1", "botania:record_gaia_2"};
        /// Turns on/off a chance that the entity that wears backpack gets jukebox upgrade and plays
        /// a music disc.
        bool   playJukebox                   = true;
        /// Determines whether backpack drops to fake players if killed by them in addition to real
        /// ones that it always drops to (Bedrock: simulated players).
        bool   dropToFakePlayers             = false;
        /// Chance of mob dropping backpack when killed by player (0..1).
        double backpackDropChance            = 0.5;
        /// Chance increase per looting level of mob dropping backpack (0..0.3).
        double lootingChanceIncreasePerLevel = 0.15;
        /// Weights of selecting each backpack when an entity spawns with a backpack (0..9999).
        int    leatherWeight                 = 625;
        int    copperWeight                  = 250;
        int    ironWeight                    = 125;
        int    goldWeight                    = 25;
        int    diamondWeight                 = 5;
        int    netheriteWeight               = 1;
        /// Minimum tier of backpack mobs are equipped with at mid local difficulty (above 1/3 of
        /// max, 0 is leather) (0..6).
        int    minBackpackTierMidDifficulty  = 1;
        /// Minimum tier of backpack mobs are equipped with at high local difficulty (above 2/3 of
        /// max, 0 is leather) (0..6).
        int    minBackpackTierHighDifficulty = 2;
        /// If local difficulty is taken into consideration when determining the difficulty. If
        /// local difficulty is high enough then it will use difficulty settings above.
        bool   localDifficultyEffectsBackpackSpawns = true;
    } entityBackpackAdditions;

    struct Nerfs {
        /// Determines if too many backpacks in player's inventory cause slowness to the player.
        bool        tooManyBackpacksSlowness            = false;
        /// Maximum number of backpacks in player's inventory that will not cause slowness (1..27).
        int         maxNumberOfBackpacks                = 3;
        /// Ratio of slowness levels per every backpack above the maximum number allowed. (number
        /// of backpacks above the max gets multiplied by this number and ceiled) (0.1..5).
        double      slownessLevelsPerAdditionalBackpack = 1;
        /// Determines if active upgrades will only work in the backpack that's worn by the player.
        /// Active upgrades are for example magnet, pickup, cooking, feeding upgrades.
        bool        onlyWornBackpackTriggersUpgrades    = false;
        /// Effect that is applied to player when they have too many backpacks.
        std::string nerfEffect                          = "minecraft:slowness";
    } nerfs;

    /// Maximum number of upgrades of type per backpack in format of
    /// "UpgradeRegistryName[or UpgradeGroup]|MaxNumber".
    std::vector<std::string> maxUpgradesPerStorage = {"stack_upgrades|3", "furnace_upgrades|1", "jukebox_upgrades|1"};

    /// Turns on/off item fluid handler of backpack in its item form.
    bool itemFluidHandlerEnabled          = true;
    /// Determines whether player can right click on backpack that another player is wearing to open
    /// it. If off will turn off that capability for everyone and remove related settings from
    /// backpack.
    bool allowOpeningOtherPlayerBackpacks = true;
    /// Allows disabling item display settings.
    bool itemDisplayDisabled              = false;
    /// Allows disabling logic that dedupes backpacks with the same UUID in players' inventory.
    bool tickDedupeLogicDisabled          = false;
    /// Determines if backpacks can be placed in container items (on Bedrock: shulker boxes).
    bool canBePlacedInContainerItems      = false;
};

struct SbCommonConfig {
    int  version          = 1;
    /// Turns on/off loot added to various vanilla chest loot tables.
    bool chestLootEnabled = true;
};

struct ScCommonConfig {
    int version = 1;
    /// Disable / enable any items here (disables their recipes), "item_id|true" or "|false". Every
    /// supported item is filled in with "|true" on world start.
    std::vector<std::string> enabledItems{};
};

[[nodiscard]] SbServerConfig const& sbServer();
[[nodiscard]] SbCommonConfig const& sbCommon();
[[nodiscard]] ScCommonConfig const& scCommon();

/// Configured storage / upgrade slots of a tier (`<tier>Backpack` settings, clamped to SB's range).
[[nodiscard]] int inventorySlotCount(core::Tier tier);
[[nodiscard]] int upgradeSlotCount(core::Tier tier);

/// `entityBackpackAdditions` values clamped to SB's ranges.
[[nodiscard]] double clampedChance(double v, double lo, double hi);

/// `noInteractionBlocks` contains the block (id, "minecraft:" assumed).
[[nodiscard]] bool isBlockInteractionDisallowed(std::string_view blockId);
/// `allBlockConnectionsDisallowed` or `noConnectionBlocks` contains the block.
[[nodiscard]] bool isBlockConnectionDisallowed(std::string_view blockId);
/// SB `canBePlacedInContainerItems`: whether `item` may go into a container item (shulker box).
[[nodiscard]] bool itemFitsContainerItems(ItemStackBase const& item);
/// SC `enabledItems`: false when the item is listed with "|false".
[[nodiscard]] bool isItemEnabled(std::string_view itemId);

} // namespace abp::admin
