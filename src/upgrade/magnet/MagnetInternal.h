#pragma once

// Magnet internals shared by the upgrade and its selftests.

#include "ui/menu/FilterMenu.h"
#include "upgrade/magnet/Magnet.h"

class AABB;
class ExperienceOrb;
class ItemActor;

namespace abp::upgrade::magnet {

enum class PullResult {
    Nothing, ///< no candidate items in range
    Pulled,  ///< everything that was eligible went in
    Blocked, ///< at least one eligible item (or part of it) did not fit
};

/// The XP value of an orb (0 if unknown).
[[nodiscard]] int orbValue(ExperienceOrb const& orb);
/// The carrier's bounding box grown by `range` blocks.
[[nodiscard]] AABB rangeBox(Player& player, int range);
/// Pickup delay / own-drop delay / item frame checks (not the filter).
[[nodiscard]] bool isItemPullable(ItemActor& item, Player& carrier, MagnetConfig const& cfg);

/// One pull of item entities into `ctx`'s backpack.
PullResult pullItems(UpgradeContext const& ctx, Player& player, Settings const& s, int range, MagnetConfig const& cfg);
/// One pull of XP orbs (consumers first, then the player).
void pullXp(UpgradeContext const& ctx, Player& player, int range);

/// Filter editor plus "pick up items" / "pick up XP" toggles in the control row.
class MagnetMenu : public ui::FilterMenu {
public:
    static constexpr int kItemsButton = 5; ///< control-row index
    static constexpr int kXpButton    = 6;

    MagnetMenu(Player& player, UpgradeContext const& ctx, int slots, bool advanced);
    void build(Player& player) override;
private:
    UpgradeContext mCtx;
};

} // namespace abp::upgrade::magnet
