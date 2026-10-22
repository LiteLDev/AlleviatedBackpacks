#pragma once

// Jukebox sound packets (companion protocol, server -> client), Sophisticated Core's
// `PlayDiscPayload` / `StopDiscPlaybackPayload`:
//
//   JukeboxPlayPacket  {"uuid": "<backpack>", "sound": "<sound definition>",
//                       "entity": <ActorUniqueID raw> | "pos": [x, y, z]}
//   JukeboxStopPacket  {"uuid": "<backpack>"}
//
// The client plays the record attached to the entity (it follows the carrier every tick, like SC's
// `EntityBoundSoundInstance`; for the local player it plays at the listener, SC's
// `PlayerDirectStorageSoundInstance`) or at the block position of a placed backpack. One sound per
// backpack: a new Play replaces the backpack's previous sound. Sent to companion players within
// `kSoundRange` blocks only (SC `sendToPlayersNear(..., 128, ...)`).

#include "net/Packets.h"

namespace abp::upgrade::jukebox {

/// SC: play / stop payloads go to players within 128 blocks.
inline constexpr float kSoundRange = 128.0f;

class JukeboxPlayPacket : public net::JsonPacket<JukeboxPlayPacket> {
    using JsonPacket::JsonPacket;
};
class JukeboxStopPacket : public net::JsonPacket<JukeboxStopPacket> {
    using JsonPacket::JsonPacket;
};

} // namespace abp::upgrade::jukebox
