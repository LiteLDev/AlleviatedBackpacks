#pragma once

#include <compare>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ll/api/event/ListenerBase.h"

#include "core/ItemStackData.h"
#include "game/Backpack.h"
#include "ui/menu/BackpackMenu.h"

class Block;
class BlockPos;
class BlockSource;
class ItemStack;
class Player;

namespace abp::placed {

/// Block id of a placed backpack (one block type; tier and facing are block states).
inline constexpr std::string_view kBlockId     = "alleviatedbackpacks:placed_backpack";
inline constexpr std::string_view kFacingState = "alleviatedbackpacks:facing"; ///< 0 south, 1 west, 2 north, 3 east
inline constexpr std::string_view kTierState   = "alleviatedbackpacks:tier";   ///< core::Tier value

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

    /// Sneak + use with a backpack in the main hand on `clicked` / `face`: places it.
    bool placeFromHand(Player& player, BlockPos const& clicked, int face);
    /// Sneak + use with an empty hand: the backpack goes into the player's hand.
    bool pickUp(Player& player, BlockPos const& pos);
    /// Use: opens the backpack GUI (closes when the block goes away or the player walks off).
    bool open(Player& player, BlockPos const& pos, ui::BackpackMenu::View view = ui::BackpackMenu::View::Storage);
    /// Opens `backpack` for `player`: as a placed backpack when it is placed near the player, else
    /// like `BackpackMenu::openBackpack` (settings menus use it for their back buttons).
    static bool openBackpack(Player& player, std::shared_ptr<game::Backpack> const& backpack, ui::BackpackMenu::View view);

    /// Runs the placed-backpack upgrade ticks / consistency checks now (selftests).
    void tickPlaced(int interval);
    /// Re-reads the position map from the world DB (selftests: persistence).
    void reload() { load(); }

    /// Block state value (tier / facing) of a placed-backpack block, nullopt if not one.
    [[nodiscard]] static std::optional<int> stateOf(Block const& block, std::string_view state);

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

    std::map<BlockLocation, PlacedEntry>           mEntries;
    std::unordered_map<std::string, BlockLocation> mByUuid;
    std::vector<PlacedEntry>                       mPendingDrops; ///< items that could not be spawned yet
    std::map<BlockLocation, int>                   mMismatches;   ///< consecutive checks without our block
    std::vector<BlockLocation>                     mOrphans;   ///< our block without an entry: removed next tick
    std::unordered_map<std::int64_t, std::int64_t> mCooldowns; ///< player unique id -> tick of last action
    std::vector<ll::event::ListenerPtr>            mListeners;
    int                                            mGuard = 0; ///< > 0 while we change blocks ourselves
    std::int64_t                                   mNow   = 0;
    int                                            mTicks = 0;

    friend class PlacedGuard;
};

} // namespace abp::placed
