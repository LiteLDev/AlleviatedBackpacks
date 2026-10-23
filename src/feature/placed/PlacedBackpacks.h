#pragma once

#include <compare>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/Tier.h"

#include "ll/api/event/ListenerBase.h"

#include "mc/world/level/BlockPos.h"

#include "core/ItemStackData.h"
#include "game/Backpack.h"
#include "ui/menu/BackpackMenu.h"

class Block;
class BlockPos;
class BlockSource;
class ItemStack;
class Player;

namespace abp::placed {

/// Legacy (phase 1) block of a placed backpack: one block, tier and facing as block states.
/// Converted to the per-tier blocks on the placed tick.
inline constexpr std::string_view kBlockId     = "alleviatedbackpacks:placed_backpack";
inline constexpr std::string_view kFacingState = "alleviatedbackpacks:facing"; ///< 0 south, 1 west, 2 north, 3 east
inline constexpr std::string_view kTierState   = "alleviatedbackpacks:tier";   ///< legacy block only: core::Tier value
/// SB BackpackBlock states (per-tier blocks): parts of the model (shape) and the open state.
inline constexpr std::string_view kLeftTankState  = "alleviatedbackpacks:left_tank";
inline constexpr std::string_view kRightTankState = "alleviatedbackpacks:right_tank";
inline constexpr std::string_view kBatteryState   = "alleviatedbackpacks:battery";
inline constexpr std::string_view kOpenState      = "alleviatedbackpacks:open";

/// Block of a placed backpack of `tier` (SB: one block per tier), "alleviatedbackpacks:placed_<tier>_backpack".
[[nodiscard]] std::string blockIdFor(core::Tier tier);
/// Every placed-backpack block id (the per-tier blocks and the legacy block).
[[nodiscard]] std::vector<std::string> blockIds();

/// Part / open states of a placed backpack block.
struct BlockParts {
    bool leftTank  = false;
    bool rightTank = false;
    bool battery   = false;
    bool open      = false;
    [[nodiscard]] bool operator==(BlockParts const&) const = default;
};

/// `config/placed.json`.
struct PlacedConfig {
    int  version = 1;
    /// Sneak + use a backpack on a block places it.
    bool allowPlacing = true;
    /// Ticks between `IUpgrade::onPlacedTick` calls for placed backpacks in loaded chunks.
    int  upgradeTickInterval = 2;
    /// Vanilla hoppers above/beside a placed backpack insert into it; hoppers below pull from it.
    bool hopperAutomation = true;
    /// Droppers facing a placed backpack insert into it.
    bool dropperAutomation = true;
};

/// A block position in a dimension.
struct BlockLocation {
    int dim = 0;
    int x   = 0;
    int y   = 0;
    int z   = 0;

    [[nodiscard]] auto operator<=>(BlockLocation const&) const = default;
};

[[nodiscard]] BlockLocation locationOf(BlockSource const& region, BlockPos const& pos);

/// GX-2 (EntityGriefing.cpp): true while a Wither or the Ender Dragon breaks blocks on this thread
/// (SB BackpackBlock.canEntityDestroy is false for Everlasting backpacks).
[[nodiscard]] bool entityBreakingBlocks();
/// SB `hasEverlastingUpgrade(world, pos)`: the backpack placed at `pos` has an Everlasting upgrade.
[[nodiscard]] bool everlastingAt(BlockSource const& region, BlockPos const& pos);

/// One placed backpack: where it is and which backpack (uuid) it shows.
struct PlacedEntry {
    BlockLocation       loc;
    std::string         uuid;
    core::ItemStackData item;       ///< the backpack item as it was placed (custom name, uuid, ...), count 1
    int                 facing = 2; ///< 0 south, 1 west, 2 north, 3 east
};

/// Backpacks placed as blocks (sneak + use a backpack on a block).
///
/// Custom blocks can't have block entities, so the placed block only carries tier and facing
/// as block states; the backpack it stands for is kept in a position -> uuid map persisted in the
/// world's LevelDB (key `abp:placed`). The contents stay in `game::Storage` under the same uuid,
/// so picking the block up returns the very same backpack item.
///
/// Never lose, never duplicate: every way the block can disappear (player break in survival or
/// creative, commands, anything that sets the block) goes through the block-changed event and
/// drops the backpack item exactly once (the map entry is removed when it drops). Explosions and
/// pistons can't move or break the block (block components).
///
/// Thread: server thread only.
class PlacedBackpacks {
public:
    static PlacedBackpacks& get();

    void install();
    void uninstall();

    // ---- queries ------------------------------------------------------------------------------
    [[nodiscard]] std::optional<PlacedEntry> at(BlockLocation const& loc) const;
    [[nodiscard]] std::optional<PlacedEntry> findUuid(std::string_view uuid) const;
    [[nodiscard]] std::vector<PlacedEntry>   entries() const;
    [[nodiscard]] std::size_t                count() const { return mEntries.size(); }

    /// The (loaded) backpack placed at `pos`, or nullptr. Checks the block too.
    [[nodiscard]] std::shared_ptr<game::Backpack> backpackAt(BlockSource& region, BlockPos const& pos);

    [[nodiscard]] static bool isBackpackBlock(Block const& block);
    /// Facing (0 south, 1 west, 2 north, 3 east) of a block whose front looks at a player with yaw `yaw`.
    [[nodiscard]] static int  facingTowards(float yaw);

    // ---- actions ------------------------------------------------------------------------------
    enum class PlaceResult { Ok, NotBackpack, Blocked, NoStorage, Failed };
    /// Places the backpack `item` as a block at `pos` facing `facing`. Binds a uuid to `item` if it
    /// has none (so `item` is changed). On `Ok` the caller must remove the item from where it was.
    PlaceResult place(BlockSource& region, BlockPos const& pos, ItemStack& item, int facing);
    /// Removes the backpack block at `pos` (without dropping it) and returns its item.
    std::optional<ItemStack> take(BlockSource& region, BlockPos const& pos);

    /// Sneak + use with a backpack in the main hand on `clicked` / `face`: places it. In creative
    /// the placed backpack is a clone with a new uuid and the item stays in the hand (SB).
    bool placeFromHand(Player& player, BlockPos const& clicked, int face);
    /// SB BackpackDispenseBehavior: a dispenser facing `dispenserFace` (Facing: 0 down, 1 up,
    /// 2 north, 3 south, 4 west, 5 east) places `item` (count 1) at `target`, the block in front of
    /// it. The backpack faces the dispenser (north when the dispenser faces up or down). On `Ok`
    /// the caller removes the item from the dispenser; on failure nothing happens.
    PlaceResult placeFromDispenser(BlockSource& region, BlockPos const& target, int dispenserFace, ItemStack& item);
    /// Our facing (0 south, 1 west, 2 north, 3 east) of a backpack placed by a dispenser facing
    /// `dispenserFace` (SB: the opposite of the dispenser's direction; north for up / down).
    [[nodiscard]] static int facingForDispenser(int dispenserFace);
    /// Fills / drains the backpack's tanks with the fluid container in the main hand (SB
    /// BackpackBlock.useItemOn). True if the click was a fluid container on a backpack with tanks.
    bool useFluidContainer(Player& player, BlockPos const& pos);
    /// The comparator output of the placed backpack at `pos` (SB: InventoryHelper.getAnalogOutputSignal).
    [[nodiscard]] int comparatorSignal(BlockSource& region, BlockPos const& pos);
    /// Players with the backpack at `loc` open (SB ContainerOpenersCounter).
    [[nodiscard]] int openers(BlockLocation const& loc) const;
    /// Part states the block at `loc` should have (tanks / battery of its backpack, open).
    [[nodiscard]] BlockParts partsFor(BlockLocation const& loc, game::Backpack& backpack) const;
    /// Sneak + use with an empty main hand: the backpack goes into the player's main hand (5 tick
    /// cooldown, wool break sound). Infinity upgrades allow it to operators only.
    bool pickUp(Player& player, BlockPos const& pos);
    /// Use: opens the backpack GUI (closes when the block goes away or the player walks off).
    bool open(Player& player, BlockPos const& pos, ui::BackpackMenu::View view = ui::BackpackMenu::View::Storage);
    /// Opens `backpack` for `player`: as a placed backpack when it is placed near the player, else
    /// like `BackpackMenu::openBackpack` (settings menus use it for their back buttons).
    static bool openBackpack(Player& player, std::shared_ptr<game::Backpack> const& backpack, ui::BackpackMenu::View view);

    /// Additive extension points (P2-13, linked storage).
    struct PlacementHooks {
        /// A creative player places `hand`: return the item to place instead (the hand item is then
        /// kept), or nullopt for the default (place `hand`, consumed). SB: placing a linked-storage
        /// endpoint in creative places a new secondary endpoint of its group.
        std::function<std::optional<ItemStack>(Player&, ItemStack const& hand)> creativeCopy;
        /// Two placed backpack items may stand for the same storage uuid (linked-storage endpoints
        /// share their group's storage). Default: never.
        std::function<bool(core::ItemStackData const& a, core::ItemStackData const& b)> mayShareUuid;
    };
    static void setPlacementHooks(PlacementHooks hooks);
    /// Two placed items may stand for one storage (PlacementHooks::mayShareUuid).
    [[nodiscard]] static bool sharesUuid(core::ItemStackData const& a, core::ItemStackData const& b);

    /// Runs the placed-backpack upgrade ticks / consistency checks now (selftests).
    void tickPlaced(int interval);
    /// Re-reads the position map from the world DB (selftests: persistence).
    void reload() { load(); }

    /// Block state value (facing / legacy tier) of a placed-backpack block, nullopt if not one.
    [[nodiscard]] static std::optional<int> stateOf(Block const& block, std::string_view state);
    /// Bool block state of a placed-backpack block (parts, open), nullopt if not one.
    [[nodiscard]] static std::optional<bool> boolStateOf(Block const& block, std::string_view state);
    /// Tier of a placed-backpack block, nullopt if not one.
    [[nodiscard]] static std::optional<core::Tier> tierOfBlock(Block const& block);

private:
    void load();
    void save() const;
    void add(PlacedEntry entry);
    void erase(BlockLocation const& loc);
    void onBlockChanged(BlockSource& region, BlockPos const& pos, Block const& previous, Block const& now);
    /// Spawns the backpack item of `entry` at its position; on failure keeps it in the pending
    /// drops (persisted, retried) so it is never lost.
    bool dropEntry(BlockSource& region, PlacedEntry const& entry);
    void retryPendingDrops();
    /// Closes every backpack GUI showing `uuid` (it was picked up / broken) and plain backpack GUIs
    /// of placed backpacks whose viewer walked away.
    void closeViewers(std::string const* uuid);
    [[nodiscard]] static ItemStack itemOf(PlacedEntry const& entry);
    void tick();
    /// Placed tick work for one entry with its block present: legacy block conversion, part /
    /// open states, the display entity, items falling into the block.
    void maintain(BlockSource& region, PlacedEntry const& entry, std::shared_ptr<game::Backpack> const& bp);
    /// Re-places blocks of Everlasting backpacks an explosion destroyed.
    void restoreProtected();
    struct Protected {
        PlacedEntry entry;
        Block const* block;
    };
    std::vector<Protected> mRestore;
    std::int64_t           mLastPurge = 0;
    std::map<BlockLocation, int> mSignals; ///< last comparator signal per placed backpack

    std::map<BlockLocation, PlacedEntry>           mEntries;
    std::unordered_map<std::string, BlockLocation> mByUuid;
    std::vector<PlacedEntry>                       mPendingDrops; ///< items that could not be spawned yet
    std::map<BlockLocation, int>                   mMismatches;   ///< consecutive checks without our block
    /// A sneak-use to place / pick up, run on the next tick (outside the item-use transaction).
    struct PendingAction {
        enum class Kind { Place, PickUp, Open, Fluid };
        std::int64_t player; ///< ActorUniqueID
        Kind         kind;
        BlockPos     pos;
        int          face;
    };
    std::vector<PendingAction>                     mActions;
    void                                           runActions();
    std::vector<BlockLocation>                     mOrphans;   ///< our block without an entry: removed next tick
    std::unordered_map<std::int64_t, std::int64_t> mCooldowns; ///< player unique id -> tick of last action
    std::vector<ll::event::ListenerPtr>            mListeners;
    int                                            mGuard = 0; ///< > 0 while we change blocks ourselves
    std::int64_t                                   mNow   = 0;
    int                                            mTicks = 0;
    int                                            mJukeboxTicks = 0; ///< ticks since the last jukebox report

    friend class PlacedGuard;
};

} // namespace abp::placed
