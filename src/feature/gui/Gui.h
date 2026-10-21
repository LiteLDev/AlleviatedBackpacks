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
    bool        defaultOpenTabFirst  = true;
    bool        defaultNotifications = true;
    bool        defaultKeepHotbar    = true;
};

/// The GUI options. Resolution: backpack override -> player's global setting -> server default.
struct Options {
    core::SortBy sortBy = core::SortBy::Name;
    /// Shift-click on the upgrades page: into the upgrade slots first (true) or storage first.
    bool openTabFirst = true;
    /// Chat feedback (sorted / moved N items) and slot hints in item lore.
    bool notifications = true;
    /// "Move all" buttons leave the hotbar alone.
    bool keepHotbar = true;
};

/// Option keys (JSON keys in the backpack / player settings).
inline constexpr char const* kSortBy        = "sortBy";
inline constexpr char const* kOpenTabFirst  = "openTabFirst";
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
};
/// Moves the player's inventory items into the backpack (backpack filters apply). Returns the count.
int transferToBackpack(Player& player, game::Backpack& backpack, TransferOptions const& opt);
/// Moves the backpack's items into the player's inventory, as far as they fit. Returns the count.
int transferToPlayer(Player& player, game::Backpack& backpack, TransferOptions const& opt);

/// Sends `message` to the player if notifications are on for this backpack.
void notify(Player& player, game::Backpack const& backpack, std::string const& message);

/// Opens the memory / no-sort slot picker (a chest menu over the backpack's storage).
enum class PickMode { Memory, NoSort };
bool openSlotPicker(Player& player, std::shared_ptr<game::Backpack> backpack, PickMode mode, int page = 0);

} // namespace abp::feature::gui
