#pragma once

#include <map>
#include <string>
#include <vector>

namespace abp::wearable {

/// `config/wearable.json`: worn backpacks, backpacks on spawned mobs and in loot chests.
struct WearableConfig {
    int version = 1;

    struct Worn {
        /// Sneak twice quickly while wearing a backpack (chest slot) to open it (Bedrock has no
        /// custom key bindings; `/backpack worn` does the same).
        bool doubleSneakToOpen      = true;
        /// Max ticks between the two sneak presses (20 ticks = 1 second).
        int  doubleSneakWindowTicks = 10;
    } worn;

    struct MobSpawn {
        /// Mobs listed in `entities` sometimes spawn wearing a backpack.
        bool  enabled = true;
        /// Base chance per spawn (0..1), multiplied by `difficultyMultiplier`.
        float chance  = 0.01f;
        /// Also mobs from monster spawners (blocks).
        bool  includeSpawnerMobs = false;
        std::map<std::string, float> difficultyMultiplier = {
            {"peaceful", 1.0f},
            {"easy",     0.5f},
            {"normal",   1.0f},
            {"hard",     1.5f},
        };
        std::vector<std::string> entities = {
            "minecraft:zombie",
            "minecraft:husk",
            "minecraft:drowned",
            "minecraft:zombie_villager_v2",
            "minecraft:skeleton",
            "minecraft:stray",
            "minecraft:bogged",
            "minecraft:wither_skeleton",
            "minecraft:piglin",
            "minecraft:piglin_brute",
            "minecraft:zombie_pigman",
        };
        /// Tier weights per dimension ("overworld", "nether", "the_end"); missing tiers weigh 0.
        std::map<std::string, std::map<std::string, int>> tierWeights = {
            {"overworld", {{"leather", 60}, {"copper", 22}, {"iron", 11}, {"gold", 5}, {"diamond", 2}, {"netherite", 0}}},
            {"nether", {{"leather", 0}, {"copper", 0}, {"iron", 30}, {"gold", 45}, {"diamond", 18}, {"netherite", 7}}},
            {"the_end", {{"leather", 0}, {"copper", 0}, {"iron", 20}, {"gold", 30}, {"diamond", 35}, {"netherite", 15}}},
        };
        /// Per difficulty: each higher tier's weight is multiplied by (1 + bonus)^tierIndex, so a
        /// positive bonus makes better backpacks more likely.
        std::map<std::string, float> difficultyTierBonus = {
            {"peaceful", 0.0f},
            {"easy",     -0.2f},
            {"normal",   0.0f},
            {"hard",     0.3f},
        };
        /// Give the mob helmet/leggings/boots of the backpack's material (empty slots only).
        bool equipArmor      = true;
        /// Long-lasting potion effects per tier ("effect" or "effect:amplifier").
        bool buffWithEffects = true;
        std::map<std::string, std::vector<std::string>> tierEffects = {
            {"iron", {"speed"}},
            {"gold", {"speed", "fire_resistance"}},
            {"diamond", {"speed", "strength", "fire_resistance"}},
            {"netherite", {"speed:1", "strength:1", "fire_resistance", "regeneration"}},
        };
        /// Fill the backpack with loot (generated lazily, when it is first opened).
        bool fillWithLoot = true;
        /// Loot table per mob (Bedrock table path without "loot_tables/" and ".json").
        std::map<std::string, std::string> lootTables = {
            {"minecraft:zombie", "chests/simple_dungeon"},
            {"minecraft:husk", "chests/desert_pyramid"},
            {"minecraft:drowned", "chests/underwater_ruin_big"},
            {"minecraft:zombie_villager_v2", "chests/simple_dungeon"},
            {"minecraft:skeleton", "chests/abandoned_mineshaft"},
            {"minecraft:stray", "chests/igloo_chest"},
            {"minecraft:bogged", "chests/simple_dungeon"},
            {"minecraft:wither_skeleton", "chests/nether_bridge"},
            {"minecraft:piglin", "chests/bastion_other"},
            {"minecraft:piglin_brute", "chests/bastion_treasure"},
            {"minecraft:zombie_pigman", "chests/ruined_portal"},
        };
        std::string defaultLootTable = "chests/simple_dungeon";
        /// How many times the loot table is rolled, per tier.
        std::map<std::string, int> lootRolls = {
            {"leather", 1}, {"copper", 1}, {"iron", 2}, {"gold", 2}, {"diamond", 3}, {"netherite", 3},
        };
        /// Install random upgrades (chance per upgrade slot).
        bool  addRandomUpgrades    = true;
        float upgradeChancePerSlot = 0.3f;
        std::vector<std::string> upgradeBlacklist = {};
        /// The backpack always drops when a player kills the mob; otherwise with this chance.
        float dropChanceNotKilledByPlayer = 0.0f;
    } mobs;

    struct ChestLootEntry {
        float                      chance = 0.0f; ///< chance that one item is added to the chest
        std::map<std::string, int> items;         ///< item id -> weight
    };
    struct ChestLoot {
        bool enabled = true;
        /// Bedrock loot table path (without "loot_tables/" and ".json") -> what to add.
        std::map<std::string, ChestLootEntry> tables = {
            {"chests/simple_dungeon",
             {0.2f,
              {{"alleviatedbackpacks:leather_backpack", 50},
               {"alleviatedbackpacks:copper_backpack", 20},
               {"alleviatedbackpacks:iron_backpack", 10},
               {"alleviatedbackpacks:upgrade_base", 20}}}},
            {"chests/abandoned_mineshaft",
             {0.2f,
              {{"alleviatedbackpacks:leather_backpack", 40},
               {"alleviatedbackpacks:copper_backpack", 30},
               {"alleviatedbackpacks:iron_backpack", 10},
               {"alleviatedbackpacks:upgrade_base", 20}}}},
            {"chests/desert_pyramid",
             {0.2f,
              {{"alleviatedbackpacks:copper_backpack", 35},
               {"alleviatedbackpacks:iron_backpack", 35},
               {"alleviatedbackpacks:gold_backpack", 15},
               {"alleviatedbackpacks:upgrade_base", 15}}}},
            {"chests/jungle_temple",
             {0.2f,
              {{"alleviatedbackpacks:copper_backpack", 35},
               {"alleviatedbackpacks:iron_backpack", 35},
               {"alleviatedbackpacks:gold_backpack", 15},
               {"alleviatedbackpacks:upgrade_base", 15}}}},
            {"chests/shipwreck_treasure",
             {0.2f,
              {{"alleviatedbackpacks:copper_backpack", 40},
               {"alleviatedbackpacks:iron_backpack", 35},
               {"alleviatedbackpacks:gold_backpack", 15},
               {"alleviatedbackpacks:upgrade_base", 10}}}},
            {"chests/pillager_outpost",
             {0.15f,
              {{"alleviatedbackpacks:leather_backpack", 40},
               {"alleviatedbackpacks:iron_backpack", 40},
               {"alleviatedbackpacks:upgrade_base", 20}}}},
            {"chests/nether_bridge",
             {0.25f,
              {{"alleviatedbackpacks:iron_backpack", 35},
               {"alleviatedbackpacks:gold_backpack", 45},
               {"alleviatedbackpacks:diamond_backpack", 10},
               {"alleviatedbackpacks:upgrade_base", 10}}}},
            {"chests/bastion_treasure",
             {0.3f,
              {{"alleviatedbackpacks:gold_backpack", 50},
               {"alleviatedbackpacks:diamond_backpack", 40},
               {"alleviatedbackpacks:netherite_backpack", 10}}}},
            {"chests/woodland_mansion",
             {0.3f,
              {{"alleviatedbackpacks:gold_backpack", 50},
               {"alleviatedbackpacks:diamond_backpack", 40},
               {"alleviatedbackpacks:upgrade_base", 10}}}},
            {"chests/end_city_treasure",
             {0.3f,
              {{"alleviatedbackpacks:diamond_backpack", 70}, {"alleviatedbackpacks:netherite_backpack", 30}}}},
            {"chests/ancient_city",
             {0.3f,
              {{"alleviatedbackpacks:diamond_backpack", 70}, {"alleviatedbackpacks:netherite_backpack", 30}}}},
        };
    } chestLoot;
};

} // namespace abp::wearable
