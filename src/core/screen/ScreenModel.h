#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "nlohmann/json.hpp"

#include "core/BackpackData.h"
#include "core/BackpackInventory.h"
#include "core/ItemStackData.h"

namespace abp::core::screen {

/// Version of the companion protocol (net packets and the models below). Bump it when a
/// packet or model changes incompatibly; peers with a different major version ignore each other.
inline constexpr int kProtocolVersion = 1;

/// Item id the server sends for decoration slots of a menu (gray panes): drawn as nothing.
inline constexpr std::string_view kFillerId = "abp:filler";

/// One upgrade slot as the native backpack screen shows it.
struct UpgradeView {
    ItemStackData            item;               ///< empty = free slot
    std::string              id;                 ///< upgrade type id ("" for a free slot / unknown item)
    std::string              name;               ///< display name (server-localized)
    std::vector<std::string> lore;               ///< `IUpgrade::describe` lines
    bool                     enabled     = true;
    bool                     toggleable  = false; ///< shows an on/off switch
    bool                     runnable    = true;  ///< false: switch drawn as "inactive" (disabled on this server)
    bool                     hasSettings = false; ///< has a settings tab
    /// Free-form data for the upgrade's native tab (`client::ui::registerNativeTab`). Filled by
    /// server-side `net::TabDataProvider`s.
    nlohmann::json tab = nlohmann::json::object();

    [[nodiscard]] bool operator==(UpgradeView const&) const = default;
    [[nodiscard]] nlohmann::json   toJson() const;
    [[nodiscard]] static UpgradeView fromJson(nlohmann::json const& j);
};

/// Everything the client needs to draw Sophisticated Backpacks' backpack screen. Sent once when
/// the screen opens and then patched (`makePatch` / `applyPatch`).
struct BackpackScreenModel {
    /// "backpack": Sophisticated Backpacks' storage screen. "menu": any other server menu (filter
    /// editors, crafting, slot pickers, ...) until it has its own native screen: `menuRows` x 9
    /// `menuSlots` shown in SB style above the player inventory, clicks go to the menu's slots.
    std::string   kind        = "backpack";
    int           menuRows    = 0;
    std::vector<ItemStackData> menuSlots;
    std::uint32_t session     = 0;  ///< server-side id of this open screen
    int           containerId = -1; ///< id of the (hidden) container screen underneath
    std::string   uuid;
    std::string   title;
    int           tier         = 0;
    int           storageSlots = 0;
    std::vector<ItemStackData> slots;      ///< storage contents (counts may exceed 255)
    std::vector<int>           slotLimits; ///< max count of each non-empty slot (0 = item max)
    std::vector<UpgradeView>   upgrades;
    std::vector<ItemStackData> memory;     ///< remembered item per slot (empty entries = none)
    std::vector<bool>          noSort;     ///< slots excluded from sorting
    std::uint32_t              noSortColor = 0x80C71F; ///< RGB of the no-sort stripe (lime)
    std::string                sortBy      = "name";   ///< name | count | mod | tag
    bool                       nested      = false;    ///< opened from inside another backpack
    int                        lockedSlot  = -1;       ///< player inventory slot of this backpack
    double                     stackMultiplier = 1;
    std::uint32_t              mainColor   = 0xFFCC613A;
    std::uint32_t              accentColor = 0xFF622E1A;
    /// Red error overlay (SC `renderErrorOverlay`): message + highlighted slots.
    std::string                errorMessage;
    std::vector<int>           errorUpgradeSlots;
    std::vector<int>           errorStorageSlots;
    /// Extension data (other units; unknown keys are kept and patched as a whole).
    nlohmann::json             extra = nlohmann::json::object();

    [[nodiscard]] nlohmann::json             toJson() const;
    [[nodiscard]] static BackpackScreenModel fromJson(nlohmann::json const& j);
};

/// The changes from `before` to `after` (empty object = nothing changed). `before` and `after`
/// must describe the same session.
[[nodiscard]] nlohmann::json makePatch(BackpackScreenModel const& before, BackpackScreenModel const& after);
/// Applies a `makePatch` result. Returns false if the patch was malformed (model unchanged then
/// only partially; the caller should request a full model).
bool applyPatch(BackpackScreenModel& model, nlohmann::json const& patch);

/// Contents of a backpack for the Shift item tooltip (SC `ClientStorageContentsTooltipBase`).
struct ContentsModel {
    struct Upgrade {
        ItemStackData item;
        bool          toggleable = false;
        bool          enabled    = true;
        [[nodiscard]] bool operator==(Upgrade const&) const = default;
    };
    std::string              uuid;
    bool                     known = false; ///< the server found the backpack
    /// Lines above the upgrades: stack multiplier, tanks, energy (server-localized, `§` colours).
    std::vector<std::string> lines;
    std::vector<Upgrade>     upgrades;
    std::vector<ItemStackData> items; ///< compacted, most first

    [[nodiscard]] nlohmann::json       toJson() const;
    [[nodiscard]] static ContentsModel fromJson(nlohmann::json const& j);
};

/// Per-backpack render information for client-side renderers (backpack model parts, two-colour
/// tint, upgrade particles, placed-block item displays).
struct RenderInfo {
    std::string              uuid;
    int                      tier        = 0;
    std::uint32_t            mainColor   = 0xFFCC613A;
    std::uint32_t            accentColor = 0xFF622E1A;
    std::vector<std::string> upgrades; ///< ids of the installed, enabled upgrades (slot order)
    nlohmann::json           extra = nlohmann::json::object();

    [[nodiscard]] bool operator==(RenderInfo const&) const = default;
    [[nodiscard]] nlohmann::json    toJson() const;
    [[nodiscard]] static RenderInfo fromJson(nlohmann::json const& j);
};

/// Sophisticated Core `InventoryHelper.getCompactedStacksSortedByCount`: one entry per distinct
/// item (counts summed), largest count first (ties keep inventory order).
[[nodiscard]] std::vector<ItemStackData> compactSortedByCount(BackpackInventory const& inventory);

} // namespace abp::core::screen
