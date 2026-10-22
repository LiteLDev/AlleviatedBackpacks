#include "upgrade/jukebox/Discs.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <variant>

#include "mc/deps/core/string/HashedString.h"
#include "mc/sound/SoundEventIdentifier.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/components/RecordItemComponent.h"
#include "mc/world/level/block/LevelSoundEventMap.h"

#include "game/ItemConv.h"

namespace abp::upgrade::jukebox {

namespace {

struct VanillaDisc {
    std::string_view key;     ///< sound "record.<key>", item "minecraft:music_disc_<key>"
    std::string_view title;   ///< artist - title
    int              seconds; ///< fallback length when the item has no record component
};

// Track lengths are only a fallback: the record component's duration wins.
constexpr std::array<VanillaDisc, 22> kVanilla{{
    {"13", "C418 - 13", 178},
    {"cat", "C418 - cat", 185},
    {"blocks", "C418 - blocks", 345},
    {"chirp", "C418 - chirp", 185},
    {"far", "C418 - far", 174},
    {"mall", "C418 - mall", 197},
    {"mellohi", "C418 - mellohi", 96},
    {"stal", "C418 - stal", 150},
    {"strad", "C418 - strad", 188},
    {"ward", "C418 - ward", 251},
    {"11", "C418 - 11", 71},
    {"wait", "C418 - wait", 238},
    {"pigstep", "Lena Raine - Pigstep", 149},
    {"otherside", "Lena Raine - otherside", 195},
    {"5", "Samuel Åberg - 5", 178},
    {"relic", "Aaron Cherof - Relic", 218},
    {"creator", "Lena Raine - Creator", 176},
    {"creator_music_box", "Lena Raine - Creator (Music Box)", 73},
    {"precipice", "Aaron Cherof - Precipice", 299},
    {"tears", "Amos Roddy - Tears", 175},
    {"lava_chicken", "Hyper Potions - Lava Chicken", 134},
    {"bounce", "bounce", 180},
}};

constexpr std::string_view kItemPrefix  = "minecraft:music_disc_";
constexpr std::string_view kSoundPrefix = "record.";

VanillaDisc const* vanillaBySound(std::string_view sound) {
    if (!sound.starts_with(kSoundPrefix)) return nullptr;
    auto key = sound.substr(kSoundPrefix.size());
    for (auto const& d : kVanilla)
        if (d.key == key) return &d;
    return nullptr;
}

VanillaDisc const* vanillaByItem(std::string_view id) {
    if (!id.starts_with(kItemPrefix)) return nullptr;
    auto key = id.substr(kItemPrefix.size());
    for (auto const& d : kVanilla)
        if (d.key == key) return &d;
    return nullptr;
}

std::string soundName(SoundEventIdentifier const& id) {
    auto const& v = id.mSound.get();
    if (auto const* e = std::get_if<SharedTypes::Legacy::LevelSoundEvent>(&v)) return LevelSoundEventMap::getName(*e);
    if (auto const* s = std::get_if<std::string>(&v)) return *s;
    return {};
}

std::string prettyKey(std::string_view id) {
    auto colon = id.find(':');
    auto name  = std::string(colon == std::string_view::npos ? id : id.substr(colon + 1));
    for (auto& c : name)
        if (c == '_') c = ' ';
    return name;
}

} // namespace

std::optional<DiscInfo> discInfo(ItemStack const& stack) {
    if (stack.isNull()) return std::nullopt;
    auto const* item = stack.mItem.get();
    if (!item) return std::nullopt;
    std::string id   = game::ItemConv::idOf(stack);
    DiscInfo    info{};
    bool        have = false;
    if (auto* comp = item->getComponent(RecordItemComponent::getIdentifier())) {
        auto const* rec  = static_cast<RecordItemComponent const*>(comp);
        info.soundEvent  = soundName(rec->mSoundEvent.get());
        float seconds    = rec->mDuration;
        info.durationTicks = seconds > 0.0f ? static_cast<int>(std::lround(seconds * 20.0f)) : 0;
        have             = !info.soundEvent.empty();
    }
    auto const* vanilla = have ? vanillaBySound(info.soundEvent) : vanillaByItem(id);
    if (!have) {
        if (!vanilla) return std::nullopt;
        info.soundEvent = std::string(kSoundPrefix) + std::string(vanilla->key);
    }
    if (vanilla) {
        info.key   = vanilla->key;
        info.title = vanilla->title;
        if (info.durationTicks <= 0) info.durationTicks = vanilla->seconds * 20;
    } else {
        info.title = prettyKey(id);
    }
    if (info.durationTicks <= 0) info.durationTicks = 180 * 20;
    return info;
}

std::optional<DiscInfo> discInfo(core::ItemStackData const& item) {
    if (item.empty()) return std::nullopt;
    return discInfo(game::ItemConv::toStack(item, 1));
}

std::vector<std::string> const& vanillaDiscIds() {
    static std::vector<std::string> ids = [] {
        std::vector<std::string> out;
        for (auto const& d : kVanilla) out.push_back(std::string(kItemPrefix) + std::string(d.key));
        return out;
    }();
    return ids;
}

std::string soundName(DiscInfo const& disc) {
    return disc.key.empty() ? disc.soundEvent : "abp.jukebox3d." + disc.key;
}

std::string formatTime(int ticks) {
    int seconds = std::max(0, ticks) / 20;
    auto s      = std::to_string(seconds % 60);
    return std::to_string(seconds / 60) + ":" + (s.size() < 2 ? "0" + s : s);
}

} // namespace abp::upgrade::jukebox
