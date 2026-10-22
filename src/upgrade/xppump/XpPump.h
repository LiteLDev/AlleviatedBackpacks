#pragma once

#include <string_view>

#include "upgrade/IUpgrade.h"

class Player;

namespace abp::upgrade::xppump {

/// Experience Pump upgrade (SB "Experience Pump Upgrade", Sophisticated Core
/// `XpPumpUpgradeWrapper`): moves experience between the carrier (or players within 3 blocks of a
/// placed backpack) and the backpack's tanks as liquid experience (20 mB per point), every 5
/// ticks. It needs a Tank upgrade to store anything.
///
/// Settings JSON: {"direction": "input"|"output"|"keep"|"off", "level": n, "levelsToStore": n,
/// "levelsToTake": n, "mendItems": bool}.
inline constexpr std::string_view kId     = "xp_pump";
inline constexpr std::string_view kItemId = "alleviatedbackpacks:xp_pump_upgrade";

enum class Direction { Input, Output, Keep, Off };

struct Settings {
    Direction direction     = Direction::Input;
    int       level         = 10;
    int       levelsToStore = 1;
    int       levelsToTake  = 1;
    bool      mendItems     = true;

    [[nodiscard]] static Settings of(UpgradeContext const& ctx);
    void                          save(UpgradeContext const& ctx) const;
};

/// The player's total experience points (SB `XpHelper.getPlayerTotalExperience`).
[[nodiscard]] int  playerXp(Player& player);
[[nodiscard]] int  playerLevel(Player& player);
void               setPlayerXp(Player& player, long long total);

/// SB automatic step (`interactWithPlayer`) and `mendItems`.
void interactWithPlayer(Player& player, UpgradeContext const& ctx);
void mendItems(Player& player, UpgradeContext const& ctx);
/// The tab's manual actions: "take" (levelsToTake levels), "store" (levelsToStore levels),
/// "takeAll", "storeAll".
void manualAction(Player& player, UpgradeContext const& ctx, std::string_view action);

} // namespace abp::upgrade::xppump
