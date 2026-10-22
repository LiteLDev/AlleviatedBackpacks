#pragma once

// Infinity Upgrade (Admin) and Survival Infinity Upgrade (Sophisticated Core `upgrades/infinity`):
// every non-empty storage slot becomes infinite. It reports an unlimited count, taking items out
// never depletes it, and items can only be put into empty slots, by a player with the variant's
// permission level (admin: operators; survival: everybody). Only such a player can remove the
// upgrade, and (admin variant) break or pick up the placed backpack. No other upgrade can share a
// backpack with it, in either direction. The upgrades have no recipes (creative inventory only).

#include <string_view>

#include "upgrade/IUpgrade.h"

class Player;

namespace abp::upgrade::infinity {

inline constexpr std::string_view kAdminId    = "infinity";
inline constexpr std::string_view kSurvivalId = "survival_infinity";

/// SC permission levels: 2 for the admin variant (Bedrock: operator / GameDirectors and up),
/// 0 for the survival variant (everybody).
[[nodiscard]] bool hasPermission(Player const& player, int level);

class InfinityUpgrade : public IUpgrade {
public:
    explicit InfinityUpgrade(bool admin = true) : mAdmin(admin) {}

    [[nodiscard]] UpgradeInfo const& info() const override;
    [[nodiscard]] int                permissionLevel() const { return mAdmin ? 2 : 0; }

    bool canInstall(UpgradeContext const& ctx, std::string& reason) const override;
    bool allowsInstallOf(UpgradeContext const& self, IUpgrade const& other, std::string& reason) const override;
    bool canRemove(UpgradeContext const& ctx, std::string& reason) const override;
    void attach(UpgradeContext const& ctx, core::BackpackInventory& inventory) override;

private:
    bool mAdmin;
};

class SurvivalInfinityUpgrade : public InfinityUpgrade {
public:
    SurvivalInfinityUpgrade() : InfinityUpgrade(false) {}
};

/// The highest permission level an installed infinity upgrade of `backpack` asks for
/// (-1 = none installed).
[[nodiscard]] int requiredLevel(game::Backpack const& backpack);
/// Whether `player` may break or pick up the placed `backpack` (SB BackpackBlock /
/// CommonEventHandler: every infinity upgrade's permission level is needed).
[[nodiscard]] bool mayTakePlaced(Player const& player, game::Backpack const& backpack);

} // namespace abp::upgrade::infinity
