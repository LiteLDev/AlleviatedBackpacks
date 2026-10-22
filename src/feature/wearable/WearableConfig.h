#pragma once

#include <string>
#include <vector>

namespace abp::wearable {

/// `config/wearable.json`: AlleviatedBackpacks extras around worn and mob backpacks that
/// Sophisticated Backpacks doesn't have; all off by default. SB's own settings (entity backpack
/// additions, chest loot) are in `sophisticatedbackpacks-server.json` / `-common.json`.
struct WearableConfig {
    int version = 2;

    struct Worn {
        /// Sneak twice quickly while wearing a backpack (chest slot) to open it. Not in SB (the
        /// client mod's Open Backpack key does it); off by default. `/backpack worn` always works.
        bool doubleSneakToOpen      = false;
        /// Max ticks between the two sneak presses (20 ticks = 1 second).
        int  doubleSneakWindowTicks = 10;
    } worn;

    struct MobExtras {
        /// Install random upgrades into backpacks of spawned mobs (chance per upgrade slot). Not in
        /// SB (only the jukebox there); off by default.
        bool                     addRandomUpgrades    = false;
        float                    upgradeChancePerSlot = 0.3f;
        std::vector<std::string> upgradeBlacklist     = {};
    } mobs;
};

} // namespace abp::wearable
