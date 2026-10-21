#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/ItemStackData.h"

class ItemStack;
class Player;

namespace abp::upgrade::jukebox {

/// What the jukebox needs to know about a music disc.
struct DiscInfo {
    std::string soundEvent;    ///< the disc's own sound event, e.g. "record.cat"
    std::string key;           ///< vanilla disc key ("cat") when our RP defines `abp.jukebox.<key>`; else empty
    int         durationTicks; ///< length of the track in game ticks
    std::string title;         ///< "C418 - cat" (untranslated; artist and title are proper names)
};

/// Disc data from the item's `minecraft:record` component (vanilla and add-on discs), with a
/// built-in table as fallback for vanilla discs. nullopt if the item is not a music disc.
[[nodiscard]] std::optional<DiscInfo> discInfo(ItemStack const& stack);
[[nodiscard]] std::optional<DiscInfo> discInfo(core::ItemStackData const& item);

/// Item ids of every vanilla music disc (selftests).
[[nodiscard]] std::vector<std::string> const& vanillaDiscIds();

/// Sound names in our resource pack (see assets/pack/rp/sounds.d/jukebox.json).
[[nodiscard]] std::string followSoundName(DiscInfo const& disc); ///< 2D (follows the carrier)
[[nodiscard]] std::string blockSoundName(DiscInfo const& disc);  ///< 3D (placed backpacks)

/// "m:ss"
[[nodiscard]] std::string formatTime(int ticks);

} // namespace abp::upgrade::jukebox
