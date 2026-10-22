#pragma once

// U3 - backpack GUI controls: sorting, transfer buttons, memory / no-sort slots, backpack and
// player settings, `/backpack open`.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "core/ItemStackData.h"
#include "core/Sorting.h"

class Player;

namespace abp::game {
class Backpack;
}

namespace abp::feature::gui {

/// `config/gui.json`: server defaults (players and backpacks can override the GUI options).
struct GuiConfig {
    int         version = 1;
    /// Sneak + use (right-click a block) with an empty hand opens the backpack worn in the chest slot.
    bool        sneakUseOpensWornBackpack = true;
    /// Defaults of the per-player / per-backpack options below.
    std::string defaultSortBy        = "name"; ///< name | count | mod | tag
    bool        defaultOpenTabFirst  = false; ///< SB `shiftClickOpenTab` default
    /// SB `allowOpeningOtherPlayerBackpacks`: players may open a backpack another player wears
    /// (and the backpack settings show the "Another player can open" toggle).
    bool        allowOpeningOtherPlayerBackpacks = true;
    /// SB `itemDisplayDisabled`: hides the Item Display settings tab.
    bool        itemDisplayDisabled = false;
    bool        defaultNotifications = true;
    bool        defaultKeepHotbar    = true;
};

/// Effective SB `allowOpeningOtherPlayerBackpacks`: off when either sophisticatedbackpacks-server.json
/// (SB's own key) or the older gui.json copy turns it off.
[[nodiscard]] bool otherPlayerOpeningAllowed();
/// Effective SB `itemDisplayDisabled` (either file).
[[nodiscard]] bool itemDisplayDisabled();

/// The GUI options. Resolution: backpack override -> player's global setting -> server default.
struct Options {
    core::SortBy sortBy = core::SortBy::Name;
    /// SB "Shift Click Into Open Tab First" (`shiftClickOpenTab`, default off).
    bool openTabFirst = false;
    /// Chat feedback (sorted / moved N items) and slot hints in item lore.
    bool notifications = true;
    /// "Move all" buttons leave the hotbar alone.
    bool keepHotbar = true;
};

/// Option keys (JSON keys in the backpack / player settings).
inline constexpr char const* kSortBy        = "sortBy";
inline constexpr char const* kOpenTabFirst  = "shiftClickOpenTab"; ///< SB's main setting name
inline constexpr char const* kNotifications = "notifications";
inline constexpr char const* kKeepHotbar    = "keepHotbar";

[[nodiscard]] Options serverDefaults();
/// The player's global options (server defaults + what the player saved).
[[nodiscard]] Options playerOptions(Player& player);
/// The player's saved global overrides (JSON object, possibly empty).
[[nodiscard]] nlohmann::json playerOverrides(Player& player);
/// Sets (or clears with nullopt) one global option of the player. Persisted in the world.
void setPlayerOption(Player& player, std::string const& key, std::optional<nlohmann::json> value);
/// Applies several global options at once (one write); `null` values clear the option.
void setPlayerOptions(Player& player, nlohmann::json const& patch);
/// The backpack's own overrides (JSON object, possibly empty).
[[nodiscard]] nlohmann::json backpackOverrides(game::Backpack const& backpack);
/// Sets (or clears with nullopt = "use my global setting") one option of the backpack.
void setBackpackOption(game::Backpack& backpack, std::string const& key, std::optional<nlohmann::json> value);
/// Effective options for `player` (may be null: server defaults) viewing `backpack`.
[[nodiscard]] Options effective(Player* player, game::Backpack const& backpack);
/// Applies the keys of `overrides` on top of `base`.
[[nodiscard]] Options apply(Options base, nlohmann::json const& overrides);

[[nodiscard]] std::string sortByLabel(Player& player, core::SortBy by);

// ---- memory / no-sort slots ----------------------------------------------------------------------
/// Remembered item per storage slot (size = inventory size; empty = no memory).
[[nodiscard]] std::vector<core::ItemStackData> const& memory(game::Backpack& backpack);
/// Remembers `item` (count ignored) for `slot`, or forgets it when `item` is empty. Rebuilds hooks.
void setMemory(game::Backpack& backpack, int slot, core::ItemStackData const& item);
/// Remembers the current item of every non-empty slot (keeps existing memories).
void memorizeAll(game::Backpack& backpack);
void clearMemory(game::Backpack& backpack);
[[nodiscard]] int memoryCount(game::Backpack& backpack);

/// Slots excluded from sorting (size = inventory size).
[[nodiscard]] std::vector<bool> const& noSort(game::Backpack& backpack);
void setNoSort(game::Backpack& backpack, int slot, bool on);
void setNoSortAll(game::Backpack& backpack, bool on);
[[nodiscard]] int noSortCount(game::Backpack& backpack);

// ---- actions ------------------------------------------------------------------------------------
/// Sorts the backpack with `by` (default: the effective mode), honouring no-sort and memory slots.
bool sortBackpack(game::Backpack& backpack, core::SortBy by);

struct TransferOptions {
    bool               matchingOnly = false; ///< only items already in the destination
    bool               keepHotbar   = true;  ///< leave the hotbar (slots 0..8) alone
    std::optional<int> lockedSlot;           ///< player inventory slot never touched (the open backpack)
    /// Player slots in index order (hotbar 0..8 first) instead of main inventory first.
    bool               slotOrder    = false;
    /// `matchingOnly` compares items exactly (id, aux and NBT; SC `ItemStackKey`) instead of by
    /// id and aux. Memory slots still match by `core::memoryMatches`.
    bool               exactMatch   = false;
};

/// Sophisticated Core's transfer buttons (`TransferItemsPayload`) for the native backpack screen:
/// - to the backpack: from the main inventory (slots 9..35, never the hotbar); `matching` = only
///   items already in the backpack or remembered by a memory slot;
/// - to the inventory, everything: into slots 9..35 (never the hotbar);
/// - to the inventory, `matching`: only items the player already has, into slots 0..35.
/// Returns the moved count.
int transferToBackpackSB(Player& player, game::Backpack& backpack, bool matching, std::optional<int> lockedSlot);
int transferToPlayerSB(Player& player, game::Backpack& backpack, bool matching, std::optional<int> lockedSlot);

/// Display name of an item as the server sees it (sorting by name); the id path when the server
/// has no translation.
[[nodiscard]] std::string itemDisplayName(core::ItemStackData const& item);
/// Moves the player's inventory items into the backpack (backpack filters apply). Returns the count.
int transferToBackpack(Player& player, game::Backpack& backpack, TransferOptions const& opt);
/// Moves the backpack's items into the player's inventory, as far as they fit. Returns the count.
int transferToPlayer(Player& player, game::Backpack& backpack, TransferOptions const& opt);

/// Sends `message` to the player if notifications are on for this backpack.
void notify(Player& player, game::Backpack const& backpack, std::string const& message);

/// Opens the memory / no-sort slot picker (a chest menu over the backpack's storage).
enum class PickMode { Memory, NoSort };
bool openSlotPicker(Player& player, std::shared_ptr<game::Backpack> backpack, PickMode mode, int page = 0);

// ---- SB memory "Match NBT" / "Ignore NBT" ---------------------------------------------------------
/// SC MemorySettingsCategory `ignoreNbt` (default true): memory slots match by item only.
[[nodiscard]] bool memoryIgnoresNbt(game::Backpack const& backpack);
/// Switches the mode and converts the remembered entries like SC (`setIgnoreNbt`).
void setMemoryIgnoreNbt(game::Backpack& backpack, bool ignore);
/// Whether `item` may go into a memory slot remembering `remembered` in this backpack's mode.
[[nodiscard]] bool memoryAccepts(bool ignoreNbt, core::ItemStackData const& remembered, core::ItemStackData const& item);

namespace detail {
/// The backpack's GUI settings section (`settings.gui`), created when missing.
nlohmann::json& guiSection(game::Backpack& backpack);
/// Read-only view (nullptr when the backpack has none).
[[nodiscard]] nlohmann::json const* guiSectionOf(game::Backpack const& backpack);
/// After editing `memory` / `noSort` / `memoryIgnoreNbt` in the section directly.
void slotSettingsChanged(game::Backpack& backpack, bool rebuildHooks);
} // namespace detail

} // namespace abp::feature::gui
