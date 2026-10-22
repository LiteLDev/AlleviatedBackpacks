#pragma once

// Shared server side of the "station" upgrades (Crafting, Stonecutter, Smelting & co.): their
// item slots live in the upgrade's settings and are shown in the upgrade's native SB tab.
//
// - Item moves between the upgrade, the backpack storage and the player (SB's merge orders).
// - `StateSlot` / `ResultSlot`: slot objects for those items; tab clicks run through
//   `ui::MenuManager::simulateOn` with them, exactly like a click on a chest slot.
// - The open tab: the client tab reports `{"tabOpen":true|false}`; shift-clicks from the player
//   inventory or the storage then go into the open station tab first when SB's
//   "Shift Click Into Open Tab First" setting is on (into the tab last otherwise).

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "nlohmann/json.hpp"

#include "core/ItemStackData.h"
#include "mod/Registry.h"
#include "ui/menu/Slot.h"
#include "upgrade/IUpgrade.h"

class Player;
class ItemStack;

namespace abp::upgrade::crafting {

/// Upgrade ids of the Crafting and Stonecutter upgrades.
inline constexpr std::string_view kCraftingId    = "crafting";
inline constexpr std::string_view kStonecutterId = "stonecutter";

// ---- items --------------------------------------------------------------------------------------

/// The item on the player's cursor.
[[nodiscard]] ItemStack cursorOf(Player& player);
/// Index of the installed upgrade with this id in `backpack`, or -1.
[[nodiscard]] int findUpgradeSlot(game::Backpack const& backpack, std::string_view upgradeId);

/// Free room for `stack` in the player's main inventory (36 slots).
[[nodiscard]] int playerRoomFor(Player& player, ItemStack const& stack);
/// Adds `count` items like `stack` to the player's main inventory (merging first, then empty
/// slots). Returns how many didn't fit.
int addToPlayer(Player& player, ItemStack const& stack, int count);

/// SB `mergeStackToStorage` / `mergeStackToPlayersInventory` in the given order: moves `item` into
/// the backpack storage and the player inventory. Returns what fit nowhere.
[[nodiscard]] core::ItemStackData moveOut(Player& player, game::Backpack& backpack, core::ItemStackData item, bool storageFirst);
/// Gives items back without losing any: storage first, then the player's inventory, then dropped
/// at the player. Without a player, whatever doesn't fit into the storage is returned.
[[nodiscard]] core::ItemStackData giveBack(Player* player, game::Backpack& backpack, core::ItemStackData item);
/// Drops `item` at the player (in stacks).
void dropAt(Player& player, core::ItemStackData const& item);

/// SC `extractFromStorageOrPlayer`: takes `count` items equal to `like` (item and components)
/// from the backpack storage, else from the player's inventory. True if taken.
bool extractFromStorageOrPlayer(Player& player, game::Backpack& backpack, core::ItemStackData const& like, int count = 1);

/// Whether `item` may be kept in an upgrade's own slots: never backpacks; shulker boxes only when
/// the backpack allows them.
[[nodiscard]] bool allowedInUpgradeSlot(game::Backpack const& backpack, core::ItemStackData const& item);

// ---- slots --------------------------------------------------------------------------------------

/// An item slot whose stack lives in the upgrade's settings (crafting grid cell, stonecutter input,
/// furnace input / fuel / output).
class StateSlot : public ui::Slot {
public:
    struct Ops {
        std::function<core::ItemStackData()>                  get;
        std::function<void(core::ItemStackData const&)>       set;
        /// SC `mayPlace` / `isItemValid` (null = anything).
        std::function<bool(core::ItemStackData const&)>       mayPlace;
        /// Max count in the slot (null = the item's max stack size).
        std::function<int(core::ItemStackData const&)>        maxCount;
        /// False once the upgrade is gone (everything is refused then).
        std::function<bool()>                                 valid;
    };
    explicit StateSlot(Ops ops) : mOps(std::move(ops)) {}

    [[nodiscard]] ItemStack display(Player& player) const override;
    bool                    click(ui::ClickContext const& c) override;
    ItemStack               take(Player& player, int amount) override;
    int                     accept(Player& player, ItemStack const& stack) override;

private:
    [[nodiscard]] bool alive() const { return !mOps.valid || mOps.valid(); }
    Ops                 mOps;
    core::ItemStackData mTaken; ///< taken during this action (a rolled-back move puts it back)
};

/// A crafting result slot: taking crafts once (only when the whole result fits on the cursor);
/// nothing can be placed into it.
class ResultSlot : public ui::Slot {
public:
    struct Ops {
        std::function<ItemStack()>        preview;   ///< what one craft gives now (empty = nothing)
        std::function<ItemStack(Player&)> craftOnce; ///< consume + return the result
        std::function<bool()>             valid;
    };
    ResultSlot(game::Backpack& backpack, Ops ops) : mBackpack(backpack), mOps(std::move(ops)) {}

    [[nodiscard]] ItemStack display(Player& player) const override;
    bool                    click(ui::ClickContext const& c) override;
    ItemStack               take(Player& player, int amount) override;
    /// Only reached when a crafted result couldn't be placed: it is given back, never lost.
    int accept(Player& player, ItemStack const& stack) override;

private:
    game::Backpack& mBackpack;
    Ops             mOps;
};

/// One left / right click on `slot` with the cursor (vanilla rules: take, place, merge, swap; a
/// slot that refuses the cursor item but holds the same item fills the cursor instead). Runs
/// through `MenuManager::simulateOn` (needs the player's open backpack menu) and re-syncs the
/// player. `output`: nothing can be placed into the slot (a result / furnace output), so a cursor
/// holding the same item picks the slot up instead. Returns whether anything changed.
bool clickSlot(Player& player, std::shared_ptr<ui::Slot> slot, int button, bool output = false);

// ---- open tab -----------------------------------------------------------------------------------

/// Shift-click into an open station tab: puts what it can of `item` into the upgrade's slots and
/// returns how many items it took.
struct StationTab {
    std::string upgradeId;
    std::function<int(Player& player, UpgradeContext const& ctx, core::ItemStackData const& item)> quickMoveIn;
};
using StationTabRegistry = StaticRegistry<StationTab>;

/// Handles `{"tabOpen":bool,"uuid":..}` from a station tab. Returns true if `data` was such a message.
bool handleTabOpen(Player& player, UpgradeContext const& ctx, nlohmann::json const& data);
/// The upgrade slot of the station tab the player has open in `backpack`'s screen (nullopt = none).
[[nodiscard]] std::optional<int> openStationTab(Player& player, game::Backpack const& backpack);
/// Records an open tab directly (selftests).
void setOpenStationTab(Player& player, game::Backpack const& backpack, std::optional<int> slot);

namespace detail {
struct StationTabRegistrar {
    explicit StationTabRegistrar(StationTab t) { StationTabRegistry::add(std::move(t)); }
};
} // namespace detail

} // namespace abp::upgrade::crafting

/// Registers the shift-click-in behaviour of a station tab (see `StationTab`).
#define ABP_REGISTER_STATION_TAB(...)                                                                                  \
    static ::abp::upgrade::crafting::detail::StationTabRegistrar ABP_UNIQUE(abp_station_tab_)(__VA_ARGS__)
