#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "game/Backpack.h"
#include "game/PlayerScan.h"
#include "ui/menu/BackpackMenu.h"

class Player;

namespace abp::upgrade::inception {

inline constexpr std::string_view kId = "inception";

/// `config/inception.json`.
struct InceptionConfig {
    int version = 1;
    /// Upgrades of backpacks stored inside a backpack with an Inception upgrade work as if the
    /// inner backpack were carried (server-wide switch; each upgrade also has its own setting).
    bool nestedUpgradesWork = true;
    /// How deep nested backpacks are scanned for active upgrades (1 = only backpacks directly in a
    /// carried backpack). 0 disables nested upgrades.
    int maxDepth = 3;
    /// Upgrade ids/groups that count as "storage related" (they move items into or out of a
    /// backpack). With the "non-storage only" setting these stay inactive in nested backpacks.
    std::vector<std::string> storageUpgrades = {
        "pickup",
        "advanced_pickup",
        "magnet",
        "advanced_magnet",
        "void",
        "advanced_void",
        "compacting",
        "advanced_compacting",
        "deposit",
        "advanced_deposit",
        "restock",
        "advanced_restock",
        "refill",
        "advanced_refill",
        "feeding",
        "advanced_feeding",
        "filter",
        "advanced_filter",
        "smelting",
        "auto_smelting",
        "smoking",
        "auto_smoking",
        "blasting",
        "auto_blasting",
        "stack",
        "crafting",
        "stonecutter",
        "tool_swapper",
        "advanced_tool_swapper",
    };
};

/// Which upgrades of the nested backpacks are active (per Inception upgrade setting).
enum class NestedUpgrades { All, NonStorage, None };

[[nodiscard]] std::string_view toString(NestedUpgrades mode);
[[nodiscard]] NestedUpgrades   nestedUpgradesFromString(std::string_view s);
/// The setting of the (enabled) Inception upgrade in `backpack`, nullopt if none is installed.
[[nodiscard]] std::optional<NestedUpgrades> nestedUpgradesOf(game::Backpack const& backpack);
/// True if the upgrade id/group is in `InceptionConfig::storageUpgrades`.
[[nodiscard]] bool isStorageRelated(std::string_view upgradeId, std::string_view group);

/// Storage slots of `backpack` that hold backpack items.
[[nodiscard]] std::vector<int> backpackSlots(game::Backpack& backpack);
/// The backpack stored in slot `index` of `parent` (assigns a uuid to the stored item if it has
/// none yet). nullptr if that slot doesn't hold a usable backpack.
[[nodiscard]] std::shared_ptr<game::Backpack> resolveNested(std::shared_ptr<game::Backpack> const& parent, int index);

/// Appends the backpacks nested in the directly carried ones (installed as the PlayerScan provider).
void appendNested(Player& player, std::vector<std::pair<game::CarriedBackpack, std::shared_ptr<game::Backpack>>>& list);

/// A backpack opened from inside another backpack's GUI. Shows a "Back" button (slot 46) that
/// returns to the containing backpack, and closes itself when the chain of containers changes.
class NestedBackpackMenu : public ui::BackpackMenu {
public:
    /// `ancestors`: uuids from the outermost backpack down to the direct parent (non-empty).
    /// `rootSource`: where the outermost backpack item is on the player (nullopt if not carried).
    NestedBackpackMenu(
        std::shared_ptr<game::Backpack>   backpack,
        std::optional<game::ItemLocation> rootSource,
        std::vector<std::string>          ancestors,
        std::string                       title
    );

    void build(Player& player) override;
    void onTick(Player& player) override;

    [[nodiscard]] std::optional<game::ItemLocation> const& rootSource() const { return mRootSource; }
    [[nodiscard]] std::vector<std::string> const&          ancestors() const { return mAncestors; }

    /// Returns to the containing backpack.
    void goBack(Player& player);
    /// True while the chain root -> ... -> this backpack is intact.
    [[nodiscard]] bool chainValid(Player& player) const;

private:
    std::optional<game::ItemLocation> mRootSource;
    std::vector<std::string>          mAncestors;
};

/// Opens the backpack in slot `index` of `parent`, as seen from `menu` (the menu showing
/// `parent`, used to know the chain back to the carried backpack). Returns false if that slot
/// holds no backpack.
bool openNested(Player& player, ui::Menu const* menu, std::shared_ptr<game::Backpack> const& parent, int index);

} // namespace abp::upgrade::inception
