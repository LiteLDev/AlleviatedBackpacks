#pragma once

// Opening another player's backpack (SB CommonEventHandler.interactWithEntity +
// AnotherPlayerBackpackOpenPayload): right click another player's back, where their worn backpack
// is. The click must hit the body (local y in [0.9, 1.6)) from behind (the direction from the
// clicking player to the target, flattened, points the way the target's body faces). Needs the
// server config `allowOpeningOtherPlayerBackpacks` (default on) and the target's
// `anotherPlayerCanOpen` setting (default on: backpack override, else the player's own setting).
// Only the backpack in the chest armor slot counts.
//
// The client mod detects the click (crosshair on a player, right mouse button) and sends the key
// action {"action":"openOtherPlayer","target":<runtime id>,"hit":[x,y,z]}; the server checks
// everything again with its own positions.

#include <string_view>

class Player;
class Vec3;

namespace abp::game {
class Backpack;
}

namespace abp::access {

/// Setting key (gui options: per backpack override, per player global) - SB's MainSetting name.
inline constexpr std::string_view kAnotherPlayerCanOpen = "anotherPlayerCanOpen";

enum class OpenResult { Opened, Disabled, NotPlayer, TooFar, NotBody, NotBack, NoBackpack, NotAllowed, Refused };
[[nodiscard]] std::string_view resultName(OpenResult r);

/// Whether the hit (world position `hit` on `target`) is on the target's back, from `source`.
[[nodiscard]] bool hitsBack(Player& source, Player& target, Vec3 const& hit);
/// The target's effective `anotherPlayerCanOpen` for its worn backpack.
[[nodiscard]] bool anotherPlayerCanOpen(Player& target, game::Backpack const& backpack);
/// Runs the whole check and opens the target's worn backpack for `source`.
OpenResult tryOpen(Player& source, Player& target, Vec3 const& hit);

} // namespace abp::access
