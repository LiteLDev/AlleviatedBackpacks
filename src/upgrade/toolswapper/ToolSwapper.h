#pragma once

// Tool Swapper upgrades (SB ToolSwapperUpgradeWrapper). Basic: no settings tab, defaults only.
// Advanced: "Swap Weapon" toggle, tool swap mode (Swap Tools / Only Swap For Other Tools / Do Not
// Swap Tools), an 8 slot filter (4 per row) and the tool swap key (wrench-like tools for the
// block / entity the player looks at).
//
// Triggers (server side):
//   - starting to break a block while holding a tool or weapon: the backpack tool that breaks the
//     block fastest (an instantly breaking one wins) replaces it;
//   - attacking while holding a tool: the best sword, else the best axe;
//   - the tool swap key (advanced): a tool that strips / scrapes / un-waxes / flattens / shears the
//     looked-at block, or works on the looked-at entity (shears, bucket for cows, lead for animals).
// Never in creative / spectator, never when holding a backpack. One Tool Swapper per backpack.

#include <optional>
#include <string>
#include <string_view>

#include "core/Filter.h"
#include "upgrade/IUpgrade.h"
#include "upgrade/filter/FilterTab.h"

class Actor;
class Block;
class BlockPos;
class ItemStack;
class Player;

namespace abp::upgrade::toolswapper {

/// Upgrade ids / group (basic and advanced share the group: one Tool Swapper per backpack).
inline constexpr std::string_view kBasicId     = "tool_swapper";
inline constexpr std::string_view kAdvancedId  = "advanced_tool_swapper";
inline constexpr std::string_view kGroup       = "tool_swapper";
inline constexpr int              kFilterSlots = 8; ///< SB toolSwapperUpgrade.filterSlots
inline constexpr int              kSlotsInRow  = 4; ///< SB toolSwapperUpgrade.slotsInRow

/// SB ToolSwapMode (serialized "name" / "onlyTools" / "noSwap"); `next()` cycles in this order.
enum class ToolMode : int {
    Any       = 0, ///< "Swap Tools"
    OnlyTools = 1, ///< "Only Swap For Other Tools": not when holding a weapon
    Off       = 2, ///< "Do Not Swap Tools"
};
[[nodiscard]] std::string_view toString(ToolMode mode);
[[nodiscard]] ToolMode         toolModeFromString(std::string_view s);

/// `config/toolswapper.json`
struct ToolSwapperConfig {
    int version = 2;
    /// Master switch for the Tool Swapper hooks.
    bool enabled = true;
    /// SB `toolSwapperUpgrade` section: filterSlots (1..20, default 8), slotsInRow (1..6, default 4).
    filtering::FilteredUpgradeConfig toolSwapperUpgrade = {kFilterSlots, kSlotsInRow};
};
/// Configured filter slot count / slots per row (clamped to SB's ranges).
[[nodiscard]] int filterSlots();
[[nodiscard]] int filterSlotsInRow();
/// The advanced Tool Swapper's filter control (SC FilterLogicControl.Advanced, Allow / Block).
[[nodiscard]] filtering::FilterSpec filterSpec();

/// Effective settings of one installed Tool Swapper (basic: defaults).
struct Settings {
    bool         swapWeapon = true;          ///< SB default
    ToolMode     mode       = ToolMode::Any; ///< SB default
    core::Filter filter     = defaultFilter();

    /// SC FilterLogic defaults for the tool swapper: deny list, match item, empty passes all.
    [[nodiscard]] static core::Filter defaultFilter();
};

[[nodiscard]] bool     isAdvanced(UpgradeContext const& ctx);
[[nodiscard]] Settings settingsOf(UpgradeContext const& ctx);
void                   setSwapWeapon(UpgradeContext const& ctx, bool on);
void                   setToolMode(UpgradeContext const& ctx, ToolMode mode);
void                   setFilter(UpgradeContext const& ctx, core::Filter const& filter);

// ---- classification (game layer, server thread) -------------------------------------------------
[[nodiscard]] bool isSword(ItemStack const& stack); ///< SB SwordRegistry / SWORD_SWEEP
/// SB canPerformToolAction: axe, hoe, pickaxe, shovel or shears.
[[nodiscard]] bool isTool(ItemStack const& stack);
[[nodiscard]] bool isAxe(ItemStack const& stack);
/// The item's own destroy speed on `block` (SB ItemStack.getDestroySpeed: no enchantments).
[[nodiscard]] float destroySpeed(ItemStack const& stack, Block const& block);
/// SB BlockState.getDestroyProgress for a player holding `stack` (efficiency included; >= 1 = instant).
[[nodiscard]] float destroyProgress(ItemStack const& stack, Block const& block);
/// SB isGoodAtBreakingBlock: harvests the block and is faster than 1.5 (or breaks it instantly).
[[nodiscard]] bool goodAtBreaking(ItemStack const& stack, Block const& block);
[[nodiscard]] float attackDamage(ItemStack const& stack);
/// SB itemWorksOnBlock: strip / scrape / wax off (axes), flatten (shovels), carve / harvest
/// (shears) on `block` at `pos`.
[[nodiscard]] bool worksOnBlock(ItemStack const& stack, Block const& block, Player& player, BlockPos const& pos);
/// SB itemWorksOnEntity: shears on a shearable mob, ToolRegistry entity tools (bucket: cow, lead:
/// animals).
[[nodiscard]] bool worksOnEntity(ItemStack const& stack, Actor const& actor);
[[nodiscard]] bool isShearable(Actor const& actor);

// ---- triggers -----------------------------------------------------------------------------------
/// SB onBlockClick: the player starts breaking the block at `pos`. Returns true when a Tool
/// Swapper handled it (swapped, or the held tool is already right).
bool onBlockClick(Player& player, BlockPos const& pos);
/// SB onAttackEntity.
bool onAttack(Player& player);

/// Result of the tool swap key.
enum class KeyResult { Swapped, NoTarget, HoldingBackpack, NoUpgrade, NoToolForBlock, NoToolForEntity };
/// The tool swap key (SB BlockToolSwapPayload / EntityToolSwapPayload): looks at the player's
/// crosshair target and swaps in a tool that works on it; shows SB's status messages.
KeyResult onSwapKey(Player& player);

} // namespace abp::upgrade::toolswapper
