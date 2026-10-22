#pragma once

// Mob Catcher / Advanced Mob Catcher upgrades (Sophisticated Backpacks `upgrades/mobcatcher`).
//
// Sneak + right-click a living mob with the backpack in hand: the mob's NBT is saved into the
// backpack (backpack-wide `settings["capturedMobs"]`, like SB's backpack contents tag) and the
// entity is removed. A captured mob covers an empty rectangle of storage slots shaped like its
// bounding box; those slots are blocked (`core::BackpackInventory::addSlotBlocker`). The native
// screen draws the mob over its footprint (client/ui/mobcatcher) and a left click on it releases
// the mob at the looked-at block (or 1-2 blocks ahead).

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "upgrade/mobcatcher/MobCatcherLogic.h"

class Actor;
class ItemStack;
class ItemStackBase;
class Player;

namespace abp::game {
class Backpack;
}

namespace abp::upgrade::mobcatcher {

/// SB `MobCatcherUpgradeConfig` (SB section "mobCatcherUpgrade"; here `config/mobcatcher.json`),
/// same names and defaults. Entries are entity ids ("minecraft:cow") or "#family" (Bedrock type families stand in
/// for Java entity tags).
struct MobCatcherConfig {
    int version = 1;
    /// Maximum slot cost a mob may have to be captured by the basic mob catcher (hard limit, 1..120).
    int basicMaxSlotCost = 18;
    /// Maximum slot cost a mob may have to be captured by the advanced mob catcher (1..120).
    int advancedMaxSlotCost = 72;
    /// Multiplier applied to animal mob slot cost (0.1..10).
    double animalMultiplier = 1.0;
    /// Multiplier applied to hostile mob slot cost (0.1..10).
    double hostileMultiplier = 2.0;
    /// When true, mobs that expose inventories (Bedrock `minecraft:inventory`) are not capturable.
    bool disallowInventoryEntities = false;
    /// Entity types (or #families) that cannot be captured.
    std::vector<std::string> entityBlockList = {"minecraft:wither"};
    /// Entity types (or #families) treated as hostile by slot cost and tier rules.
    std::vector<std::string> hostileOverrides = {"minecraft:enderman"};
    /// Entity types (or #families) treated as passive. Bedrock's villager is "villager_v2".
    std::vector<std::string> passiveOverrides = {"minecraft:villager", "minecraft:villager_v2"};
};

[[nodiscard]] MobCatcherConfig const& config();

/// Upgrade ids.
inline constexpr char const* kBasicId    = "mob_catcher";
inline constexpr char const* kAdvancedId = "advanced_mob_catcher";

/// The captured mobs stored in `backpack`.
[[nodiscard]] std::vector<CapturedMob> capturedMobs(game::Backpack const& backpack);
/// Replaces the stored mobs (marks the backpack dirty and rebuilds its slot blockers).
void saveCapturedMobs(game::Backpack& backpack, std::vector<CapturedMob> const& mobs);
/// Storage columns used for footprints of this backpack.
[[nodiscard]] int columnsOf(game::Backpack const& backpack);

/// The best installed mob catcher: nullopt = none, false = basic, true = advanced (SB
/// `getBestUpgrade`). Disabled-by-server types (admin type gate) don't count.
[[nodiscard]] std::optional<bool> bestCatcher(game::Backpack const& backpack);

/// The backpack in the player's hand if it has a mob catcher (`advanced` receives the tier).
[[nodiscard]] std::shared_ptr<game::Backpack> heldCatcherBackpack(Player& player, bool* advanced = nullptr);

/// True if `actor` counts as hostile for slot cost and tier rules (SB `isHostile`).
[[nodiscard]] bool isHostile(Actor const& actor);

struct CaptureResult {
    bool        success = false;
    std::string message; ///< localized for the player ("" when nothing applies)
};

/// Captures `target` into `backpack` (SB `capture`); `advanced` selects the tier rules. Sends no
/// message itself.
CaptureResult capture(Player& player, std::shared_ptr<game::Backpack> const& backpack, Actor& target, bool advanced);

/// Releases the captured mob `id` near `player` (SB `release`). Returns the message to show.
CaptureResult release(Player& player, std::shared_ptr<game::Backpack> const& backpack, std::string const& id);

/// Shows a capture/release message above the hotbar (SB `displayClientMessage(msg, true)`).
void showMessage(Player& player, CaptureResult const& result);

// ---- linked storage (GX-2) ------------------------------------------------------------------------
// SB resolves the mob catcher's backpack with `resolveForGlobalUpgradeProcessing`: a linked-storage
// endpoint works only as its group's PRIMARY endpoint (it then captures into / releases from the
// shared storage); a secondary endpoint is the no-op storage, so it never captures (the use
// passes) and releasing from its screen does nothing. The primary endpoint item carries SB's
// `linked_mob_catcher` projection (whether the group's storage has a catcher), refreshed while it
// is carried.

/// User data byte of SB's LINKED_MOB_CATCHER projection on a primary endpoint item.
inline constexpr std::string_view kLinkedMobCatcherKey = "abp_link_mob_catcher";

/// False for a linked endpoint that is not its group's registered primary (SB: Noop wrapper).
[[nodiscard]] bool processesGlobalUpgrades(ItemStackBase const& stack);
/// The LINKED_MOB_CATCHER projection written in `stack`.
[[nodiscard]] bool linkedCatcherProjection(ItemStackBase const& stack);
/// SB BackpackItem.inventoryTick: sets / clears the projection of an endpoint item. True if the
/// item changed.
bool refreshLinkedCatcherProjection(ItemStack& stack);

} // namespace abp::upgrade::mobcatcher
