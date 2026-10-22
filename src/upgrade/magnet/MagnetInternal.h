#pragma once

// Magnet internals shared by the upgrade and its selftests.

#include <cstdint>

#include "mc/world/level/BlockPos.h"

#include "ui/menu/FilterMenu.h"
#include "upgrade/magnet/Magnet.h"

class AABB;
class Actor;
class BlockSource;
class ExperienceOrb;
class ItemActor;

namespace abp::upgrade::magnet {

/// The XP value of an orb (0 if unknown).
[[nodiscard]] int orbValue(ExperienceOrb const& orb);
/// SC `new AABB(pos).inflate(range)`: the block at `pos` grown by `range` blocks on every side.
[[nodiscard]] AABB rangeBox(BlockPos const& pos, int range);
/// The block position of a carrier (Java `Entity.blockPosition()`: the block of the feet).
[[nodiscard]] BlockPos carrierPos(Player& player);

/// SC `canNotPickup`: blocked by the remote movement tags (`carried` = a player is the magnet).
[[nodiscard]] bool isBlockedByTags(Actor& entity, bool carried);
/// SC item eligibility without the filter: alive, not on infinite pickup delay, tags.
[[nodiscard]] bool isItemPullable(ItemActor& item, bool carried);

/// Inserts into SC's `getInventoryForUpgradeProcessing` (the backpack, plus with Inception the
/// backpacks stored inside it). Returns how many were taken (stored or voided).
int insertForUpgrade(UpgradeContext const& ctx, core::ItemStackData const& item, Player* player);

/// SC `canFillStorageWithXp`: the backpack's fluid handler accepts liquid experience.
[[nodiscard]] bool canFillStorageWithXp(std::shared_ptr<game::Backpack> const& backpack);

/// One magnet run (SC `MagnetUpgradeWrapper.tick` without the cooldown check) around `pos`.
/// `player` is the carrier (null for a placed backpack). Returns the cooldown SC would set.
int runMagnet(UpgradeContext const& ctx, Player* player, BlockSource& region, BlockPos const& pos);

/// SC `MagnetUpgradeWrapper.globalPostTick` clock: true when carried magnets run at `gameTime`.
/// Call once per game tick, before running magnets.
bool advanceSharedClock(std::uint64_t gameTime);
/// Resets the shared clock (world unload, selftests).
void resetSharedClock();

/// Chest-menu fallback of the settings (filter editor plus the two toggles in the control row).
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
