#pragma once

// Shared code of the Deposit and Restock upgrades: moving items between a backpack and a block
// container, their filter settings and settings menu, and the "use the backpack on a container"
// interaction that runs them.

#include <memory>
#include <string>
#include <vector>

#include "core/Filter.h"
#include "core/ItemStackData.h"
#include "upgrade/IUpgrade.h"

class Container;
class Player;

namespace abp::ui {
class Menu;
}

namespace abp::upgrade::transfer {

/// Settings keys (in the upgrade's settings JSON).
inline constexpr std::string_view kFilterKey   = "filter";
/// Advanced only: ignore the filter list and match the items already present on the other side
/// (deposit: in the container; restock: in the backpack).
inline constexpr std::string_view kContentsKey = "contents";

[[nodiscard]] constexpr int filterSlots(bool advanced) { return advanced ? 16 : 9; }

/// The outcome of one deposit / restock run.
struct Result {
    int stacks = 0; ///< source stacks something was moved from (the number shown to the player)
    int items  = 0; ///< total items moved
};

/// The filter an upgrade uses for one run. `reference` is what "contents" mode matches against
/// (ignored otherwise). Basic upgrades only use the allow/deny list matched by item.
[[nodiscard]] core::Filter
effectiveFilter(UpgradeContext const& ctx, bool advanced, std::vector<core::ItemStackData> const& reference);
[[nodiscard]] bool contentsMode(UpgradeContext const& ctx, bool advanced);

/// One-count copies of the distinct items in a container / a backpack inventory.
[[nodiscard]] std::vector<core::ItemStackData> distinctItems(Container& container);
[[nodiscard]] std::vector<core::ItemStackData> distinctItems(core::BackpackInventory const& inventory);

/// Moves up to `amount` of `item` into `container` the way a hopper would through `face`
/// (merges into equal stacks first, then fills empty slots; honours `canPushInItem`, slot
/// availability and stack limits). Returns how many went (or would go, with `simulate`) in.
int insertIntoContainer(Container& container, int face, core::ItemStackData const& item, int amount, bool simulate);

/// The upgrades' settings menu: a filter editor (basic: allow/deny list with 9 slots;
/// advanced: 16 slots, match options and a "contents" mode toggle).
[[nodiscard]] std::shared_ptr<ui::Menu> settingsMenu(Player& player, UpgradeContext const& ctx, bool advanced, bool deposit);
/// Lore lines describing the settings of an installed upgrade.
[[nodiscard]] std::vector<std::string> describe(Player& player, UpgradeContext const& ctx, bool advanced, bool deposit);

/// Shows `text` above the hotbar (the Java mod's action bar message).
void sendTip(Player& player, std::string const& text);

/// What `interact()` did.
struct Interaction {
    bool        handled = false; ///< the backpack has an active Deposit or Restock upgrade
    Result      deposited;
    Result      restocked;
    std::string message; ///< for the player (empty when not handled)
};

/// Runs every active Deposit / Restock upgrade of `backpack` (in upgrade-slot order) against
/// `container`, accessed through block face `face`. Doesn't send the message.
Interaction interact(std::shared_ptr<game::Backpack> const& backpack, Player& player, Container& container, int face);

} // namespace abp::upgrade::transfer

namespace abp::upgrade::deposit {
/// Moves the items the upgrade's filter lets through from the backpack into `container`.
transfer::Result run(UpgradeContext const& ctx, Container& container, int face, bool advanced);
} // namespace abp::upgrade::deposit

namespace abp::upgrade::restock {
/// Moves the items the upgrade's filter lets through from `container` into the backpack.
transfer::Result run(UpgradeContext const& ctx, Container& container, int face, bool advanced);
} // namespace abp::upgrade::restock
