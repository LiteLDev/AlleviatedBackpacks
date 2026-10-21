#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "game/Backpack.h"
#include "game/PlayerScan.h"
#include "mod/Registry.h"
#include "ui/menu/Menu.h"
#include "upgrade/IUpgrade.h"

class Player;

namespace abp::upgrade::crafting {

class TabMenu;

inline constexpr std::string_view kCraftingId    = "crafting";
inline constexpr std::string_view kStonecutterId = "stonecutter";

/// Settings key shared by the crafting-type upgrades: shift-clicking the result puts it into the
/// backpack (true, default) or into the player's inventory (false). What doesn't fit goes to the
/// other one.
inline constexpr std::string_view kShiftIntoBackpackKey = "shiftIntoBackpack";

// ---- tabs ---------------------------------------------------------------------------------------

/// A "crafting-type" upgrade with its own tab menu (crafting, stonecutter, ...). Registered tabs
/// share one backpack button ("Crafting") and link to each other from inside their menus.
struct TabEntry {
    std::string upgradeId;
    int         order = 0;
    std::string iconItem;      ///< e.g. "minecraft:crafting_table"
    std::string labelKey;      ///< lang key of the tab name
    std::string labelFallback; ///< English fallback
    std::function<std::shared_ptr<TabMenu>(Player&, UpgradeContext const&)> make;
};
using TabRegistry = StaticRegistry<TabEntry>;

namespace detail {
struct TabRegistrar {
    explicit TabRegistrar(TabEntry e) { TabRegistry::add(std::move(e)); }
};
} // namespace detail

/// Index of the installed upgrade with this id in `backpack`, or -1.
[[nodiscard]] int findUpgradeSlot(game::Backpack const& backpack, std::string_view upgradeId);

/// Opens the tab of `upgradeId` in `backpack` (the upgrade must be installed), inside the open
/// chest screen when there is one (see `switchMenu`).
bool openTab(Player& player, std::shared_ptr<game::Backpack> backpack, std::string_view upgradeId);

/// Shows `menu`: swapped into the open chest screen when the player has one (`MenuManager::swap`),
/// otherwise opened. On a real client, closing a chest and opening another right away is
/// unreliable (the new screen is dropped or closed again), so tabs never close the screen.
void switchMenu(Player& player, std::shared_ptr<ui::Menu> menu);

/// A backpack menu for `backpack`, bound to the player's backpack item when they carry it.
[[nodiscard]] std::shared_ptr<ui::Menu> makeBackpackMenu(Player& player, std::shared_ptr<game::Backpack> backpack);

// ---- moving items -------------------------------------------------------------------------------

/// Free room for `stack` in the player's main inventory (36 slots).
[[nodiscard]] int playerRoomFor(Player& player, ItemStack const& stack);

/// Adds `count` items like `stack` to the player's main inventory (merging first, then empty
/// slots). Returns how many didn't fit. Exact, so it pairs with `playerRoomFor` for simulation.
int addToPlayer(Player& player, ItemStack const& stack, int count);

/// Moves `item` into the backpack, then into the player's inventory. Returns what fit nowhere.
[[nodiscard]] core::ItemStackData stash(Player& player, game::Backpack& backpack, core::ItemStackData item);

/// Puts all of `stack` into the backpack and/or the player inventory (preferred target first).
/// With `simulate` nothing changes. Returns false (and changes nothing) if it doesn't all fit.
bool deliver(Player& player, game::Backpack& backpack, ItemStack const& stack, bool intoBackpack, bool simulate);

/// Gives items back without losing any: backpack first, then the player's inventory, then dropped
/// at the player. Without a player, whatever doesn't fit into the backpack is returned.
[[nodiscard]] core::ItemStackData giveBack(Player* player, game::Backpack& backpack, core::ItemStackData item);

/// Whether `item` may be kept in an upgrade's own slots (crafting grid, stonecutter input) of
/// `backpack`: the backpack's containment rules apply (never backpacks; shulker boxes only when
/// the config or a capability allows them in the backpack).
[[nodiscard]] bool allowedInUpgradeSlot(game::Backpack const& backpack, core::ItemStackData const& item);

// ---- menu ---------------------------------------------------------------------------------------

/// Base of the crafting-type tab menus (double chest):
///   rows 0-2: the upgrade's own UI; column 0 = back / shift-target / extra toggle,
///             slot 8 = the other tab, slot 17 = backpack page,
///   rows 3-5: 27 slots of the backpack's storage,
/// so ingredients can be moved between the backpack and the upgrade without leaving the menu.
class TabMenu : public ui::Menu {
public:
    TabMenu(std::string title, upgrade::UpgradeContext ctx, std::string upgradeId);

    void build(Player& player) final;
    void onTick(Player& player) override;
    void onDestroyed() override;
    /// Shift-click from the inventory: into the backpack storage.
    int quickMoveIn(Player& player, ItemStack const& stack) override;

    [[nodiscard]] upgrade::UpgradeContext const& context() const { return mCtx; }
    /// The upgrade is still installed in its slot (the menu closes otherwise).
    [[nodiscard]] bool valid() const;
    [[nodiscard]] int  storagePage() const { return mStoragePage; }
    [[nodiscard]] bool shiftIntoBackpack() const;

    static constexpr int kStorageStart = 27;
    static constexpr int kStorageSlots = 27;
    static constexpr int kBackSlot     = 0;
    static constexpr int kShiftSlot    = 9;
    static constexpr int kPageSlot     = 17;
    static constexpr int kOtherTabSlot = 8;

protected:
    /// Fill the upgrade's part (rows 0-2, except the shared buttons).
    virtual void buildTab(Player& player) = 0;

    upgrade::UpgradeContext mCtx;
    std::string             mUpgradeId;

private:
    void buildShared(Player& player);

    std::optional<game::ItemLocation> mSource;
    int                               mStoragePage = 0;
};

/// A real item slot whose stack lives in the upgrade's settings (crafting grid cell, stonecutter
/// input). Shift-click moves it into the backpack (then the inventory).
class StackSlot : public ui::Slot {
public:
    using Getter = std::function<core::ItemStackData()>;
    using Setter = std::function<void(core::ItemStackData const&)>;
    StackSlot(TabMenu& menu, Getter get, Setter set) : mMenu(menu), mGet(std::move(get)), mSet(std::move(set)) {}

    [[nodiscard]] ItemStack display(Player& player) const override;
    bool                    click(ui::ClickContext const& c) override;
    ItemStack               take(Player& player, int amount) override;
    int                     accept(Player& player, ItemStack const& stack) override;

private:
    TabMenu& mMenu;
    Getter   mGet;
    Setter   mSet;
};

/// A result slot: click crafts once (onto the cursor / the target slot), shift-click crafts as
/// often as possible into the backpack or the inventory, Q drops one craft.
class ResultSlot : public ui::Slot {
public:
    struct Ops {
        std::function<ItemStack()>        preview;   ///< what one craft gives now (empty = nothing)
        std::function<ItemStack(Player&)> craftOnce; ///< consume + return the result
        std::function<int(Player&, bool)> craftMax;  ///< (player, intoBackpack) -> crafts
    };
    ResultSlot(TabMenu& menu, Ops ops) : mMenu(menu), mOps(std::move(ops)) {}

    [[nodiscard]] ItemStack display(Player& player) const override;
    bool                    click(ui::ClickContext const& c) override;
    ItemStack               take(Player& player, int amount) override;
    /// Only reached when a crafted result couldn't be placed: it goes to the backpack/inventory.
    int accept(Player& player, ItemStack const& stack) override;

private:
    TabMenu& mMenu;
    Ops      mOps;
};

} // namespace abp::upgrade::crafting

/// Registers a crafting-type tab (see `abp::upgrade::crafting::TabEntry`).
#define ABP_REGISTER_CRAFTING_TAB(...)                                                                                 \
    static ::abp::upgrade::crafting::detail::TabRegistrar ABP_UNIQUE(abp_crafting_tab_)(__VA_ARGS__)
