#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "core/Filter.h"
#include "upgrade/IUpgrade.h"

class Actor;
class Block;
class ItemStack;
class Player;

namespace abp::upgrade::toolswapper {

/// Upgrade ids / group (basic and advanced share the group: one Tool Swapper per backpack).
inline constexpr std::string_view kBasicId     = "tool_swapper";
inline constexpr std::string_view kAdvancedId  = "advanced_tool_swapper";
inline constexpr std::string_view kGroup       = "tool_swapper";
inline constexpr int              kFilterSlots = 9;

/// When a tool may be swapped in for a block (SB "tool swap mode").
enum class ToolMode : int {
    Any       = 0, ///< whatever is held (blocks, food, empty hand) is swapped for the best tool
    OnlyTools = 1, ///< only when the hand is empty or already holds a tool/weapon
    Off       = 2, ///< never swap tools for blocks
};

/// `config/toolswapper.json`
struct ToolSwapperConfig {
    int version = 1;
    /// Master switch for the Tool Swapper hooks.
    bool enabled = true;
    /// "Skip nearly broken tools" treats a tool with this many uses left (or fewer) as nearly broken.
    int lowDurabilityUses = 3;
    /// Default of the "skip nearly broken tools" option (basic upgrades always use it).
    bool skipLowDurabilityByDefault = true;
};

/// Effective settings of one installed Tool Swapper (basic: fixed defaults).
struct Settings {
    bool         weapons           = true;          ///< swap to the best weapon when attacking
    ToolMode     mode              = ToolMode::Any; ///< block-breaking swaps
    bool         skipLowDurability = true;          ///< ignore nearly broken tools
    bool         interact          = true;          ///< shears for sheep, axe/hoe/shovel on sneak-use
    bool         hasFilter         = false;         ///< advanced only
    core::Filter filter{kFilterSlots};
};

[[nodiscard]] bool     isAdvanced(UpgradeContext const& ctx);
[[nodiscard]] Settings settingsOf(UpgradeContext const& ctx);
/// Writes the editable fields (not the filter, which FilterMenu edits) to an advanced upgrade.
void saveSettings(UpgradeContext const& ctx, Settings const& s);

// ---- classification / scoring (game layer, server thread) ---------------------------------------
[[nodiscard]] bool isTool(ItemStack const& stack);   ///< pickaxe/axe/shovel/hoe/sword/shears/...
[[nodiscard]] bool isWeapon(ItemStack const& stack); ///< sword/axe/mace/trident
/// Mining speed of `stack` on `block`, efficiency included (1 = bare hand).
[[nodiscard]] float mineSpeed(ItemStack const& stack, Block const& block);
/// Whether breaking `block` with `stack` yields drops (true when the block needs no tool).
[[nodiscard]] bool canHarvest(ItemStack const& stack, Block const& block);
/// Attack damage of `stack` including sharpness (0 for non-weapons' bare value).
[[nodiscard]] float attackDamage(ItemStack const& stack);
/// Tool tag needed to "use" `block` (strip/till/path/carve), e.g. "minecraft:is_axe"; empty if none.
[[nodiscard]] std::string useToolFor(Block const& block);
/// Whether right-clicking `actor` with shears does something (sheep, mooshroom, snow golem, bogged).
[[nodiscard]] bool isShearable(Actor const& actor);

// ---- swapping -----------------------------------------------------------------------------------
/// The player starts breaking `block`: swap in the best tool from the carried backpacks.
bool swapForBlock(Player& player, Block const& block);
/// The player attacks: swap in the best weapon.
bool swapForAttack(Player& player);
/// The player right-clicks `actor` with an empty hand: swap in shears when useful.
bool swapForEntityUse(Player& player, Actor const& actor);
/// The player sneak-right-clicks `block` with an empty hand: swap in the tool that can use it.
bool swapForBlockUse(Player& player, Block const& block);

} // namespace abp::upgrade::toolswapper
