#pragma once

#include <string>

namespace abp {

/// `config/general.json`: foundation-wide settings.
struct GeneralConfig {
    int         version = 1;
    /// Language for server messages and GUI text when a player's locale has no translation
    /// ("en_US", "zh_CN", ...).
    std::string language = "en_US";
    /// Use each player's client locale for GUI text when a translation exists.
    bool        usePlayerLocale = true;
    /// Ticks between upgrade `onTick` calls for carried backpacks (20 ticks = 1 second).
    int         upgradeTickInterval = 10;
    /// BDS only: require clients to accept the resource pack (textures / names of the items).
    bool        requireResourcePack = true;
    /// Allow putting backpacks inside backpacks without an Inception upgrade.
    bool        allowNesting = false;
    /// Allow putting shulker boxes into backpacks.
    bool        allowShulkerBoxes = false;
};

} // namespace abp
