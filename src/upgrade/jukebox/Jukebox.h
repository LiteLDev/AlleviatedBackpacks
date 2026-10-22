#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ll/api/event/ListenerBase.h"

#include "mc/deps/core/math/Vec3.h"

#include "core/Jukebox.h"
#include "upgrade/IUpgrade.h"
#include "upgrade/jukebox/Discs.h"

class Player;

namespace abp::upgrade::jukebox {

/// `config/jukebox.json` (SC `JukeboxUpgradeConfig` of the advanced jukebox).
struct JukeboxConfig {
    int  version = 2;
    /// Disc slots of the advanced jukebox (SC `numberOfSlots`: default 12, 1..16).
    int  advancedSlots = 12;
    /// Disc slots per row in the advanced jukebox's tab (SC `slotsInRow`: default 4, 1..6).
    int  advancedSlotsInRow = 4;
    /// Note particles above a playing backpack.
    bool particles = true;
};

inline constexpr std::string_view kBasicId          = "jukebox";
inline constexpr std::string_view kAdvancedId       = "advanced_jukebox";
inline constexpr int              kBasicSlots       = 1;
inline constexpr int              kMaxAdvancedSlots = 16;
inline constexpr int              kAdvancedSlots    = 12; ///< default of `advancedSlots()` (SC numberOfSlots)

// ---- per-upgrade settings (ctx.settings()) --------------------------------------------------------
//   {"discs": [item|null, ...], "shuffle": bool, "repeat": "all"|"one"|"no"}
// Defaults as SC: shuffle off, repeat "no".

/// Disc slots of an advanced jukebox (config, clamped to 1..16).
[[nodiscard]] int advancedSlots();
/// Disc slots per row in the jukebox tab (basic: 4 as SC's basic tab; advanced: config, 1..6).
[[nodiscard]] int slotsInRow(UpgradeContext const& ctx);
[[nodiscard]] bool isAdvanced(UpgradeContext const& ctx);
/// Number of disc slots of the jukebox installed in `ctx` (1 basic, config advanced; 0 if not a jukebox).
[[nodiscard]] int discSlots(UpgradeContext const& ctx);
/// True while `ctx` still refers to an installed jukebox upgrade.
[[nodiscard]] bool isJukebox(UpgradeContext const& ctx);
[[nodiscard]] core::ItemStackData disc(UpgradeContext const& ctx, int index);
/// Sets a disc slot (SC: a change of the playing slot stops the playback).
void                              setDisc(UpgradeContext const& ctx, int index, core::ItemStackData const& item);
[[nodiscard]] std::vector<bool>   occupiedDiscs(UpgradeContext const& ctx);
[[nodiscard]] bool                shuffle(UpgradeContext const& ctx);
[[nodiscard]] core::RepeatMode    repeatMode(UpgradeContext const& ctx);
void                              setShuffle(UpgradeContext const& ctx, bool on);
void                              setRepeatMode(UpgradeContext const& ctx, core::RepeatMode mode);

/// One sound packet the service sent (observer hook for selftests).
struct SoundEvent {
    enum class Kind { Play, Stop };
    Kind                        kind;
    std::int64_t                listener;  ///< ActorUniqueID of the receiving player
    std::string                 sound;     ///< sound name (Play)
    bool                        companion; ///< Jukebox packet (else the vanilla PlaySound / StopSound fallback)
    std::optional<std::int64_t> entity;    ///< Play: the entity the sound follows (else it plays at `pos`)
    Vec3                        pos{};     ///< Play: where it starts
};

/// Plays jukebox upgrades like Sophisticated Core's `JukeboxUpgradeWrapper` +
/// `ServerStorageSoundHandler`. One playback per installed jukebox (backpack uuid + upgrade slot).
///
/// Where it plays (fixed for the whole playback): at the block of a placed backpack, else attached
/// to the carrier entity. When a disc starts, every companion player within 128 blocks gets a
/// `JukeboxPlayPacket`: the client mod attaches the record to the carrier (it follows them) or
/// plays it at the block. Players without the client mod get the vanilla positional PlaySound at
/// the same spot (it doesn't follow). Stopping sends `JukeboxStopPacket` (vanilla: StopSound) to
/// everyone who got the sound.
///
/// Keep-alive (SC: every 5 ticks, dropped after 10 without one): every 5 ticks the playback checks
/// that the upgrade is still installed and enabled, the playing disc is still in its slot and the
/// backpack is still carried by the carrier (or still placed at the block); otherwise it stops.
/// So placing a carried backpack, picking up a placed one, dropping it, removing or disabling the
/// upgrade, or taking out / swapping the playing disc all stop the music.
class Service {
public:
    static Service& get();

    void install();
    void uninstall();

    /// SC `play`: starts playing (nothing happens when already playing). `index`: the disc slot to
    /// start with (nullopt: the first of the playlist). `starter` is the player who pressed Play.
    /// Returns false (with a translated reason) if there is nothing to play or nowhere to play it.
    bool play(UpgradeContext const& ctx, Player* starter, std::optional<int> index = std::nullopt, std::string* reason = nullptr);
    void stop(std::string const& uuid, int slot);
    /// Stops every jukebox of a backpack (e.g. when a placed backpack is broken / picked up).
    void stopAll(std::string const& uuid);
    /// SC `next` / `previous`: only while playing.
    bool next(UpgradeContext const& ctx);
    bool previous(UpgradeContext const& ctx);
    /// Applies a changed shuffle setting to a running playback (rebuilds its playlist).
    void settingsChanged(UpgradeContext const& ctx);
    /// A disc slot changed: SC stops the playback when it is the playing slot.
    void discChanged(UpgradeContext const& ctx, int index);

    struct Status {
        int index;         ///< disc slot playing
        int elapsedTicks;
        int durationTicks;
    };
    [[nodiscard]] std::optional<Status> status(std::string const& uuid, int slot) const;
    [[nodiscard]] bool                  playing(std::string const& uuid, int slot) const { return status(uuid, slot).has_value(); }
    /// ActorUniqueIDs of the players who got the playing sound.
    [[nodiscard]] std::vector<std::int64_t> listeners(std::string const& uuid, int slot) const;
    /// The entity the sound follows (nullopt: plays at a block, or not playing).
    [[nodiscard]] std::optional<std::int64_t> emitterEntity(std::string const& uuid, int slot) const;
    [[nodiscard]] std::size_t                 count() const { return mPlaybacks.size(); }

    /// A backpack that is neither carried by a player nor placed (e.g. worn by a mob) reports where
    /// it is, at least every 2 seconds; it then plays there (and stops when the reports stop).
    /// Placed backpacks need no reports: the service asks `feature/placed` directly.
    void tickPlaced(std::shared_ptr<game::Backpack> const& backpack, int dim, Vec3 const& pos);

    // ---- selftests ----------------------------------------------------------------------------
    void setObserver(std::function<void(SoundEvent const&)> fn) { mObserver = std::move(fn); }
    /// Overrides every track's length (ticks); nullopt = real lengths.
    void setDurationOverride(std::optional<int> ticks) { mDurationOverride = ticks; }

private:
    struct Emitter {
        std::optional<std::int64_t> entity; ///< carrier (ActorUniqueID); nullopt = placed block
        int                         dim = 0;
        Vec3                        pos{};
    };
    struct Playback {
        std::weak_ptr<game::Backpack> backpack;
        std::string                   uuid;
        int                           slot = 0;
        std::string                   upgradeItem;
        Emitter                       where;
        core::JukeboxQueue            queue;
        int                           index = -1;
        core::ItemStackData           disc;
        DiscInfo                      info;
        std::string                   sound;
        std::int64_t                  start = 0; ///< service tick the track started
        std::int64_t                  end   = 0;
        std::set<std::int64_t>        heard;   ///< players who got the sound
        std::set<std::int64_t>        vanilla; ///< ... of them without the client mod
        int                           phase = 0;
    };
    using Key = std::pair<std::string, int>;

    void tick();
    bool startTrack(Playback& p, int index, UpgradeContext const& ctx);
    void stopSounds(Playback& p);
    void spawnNotes(Playback const& p) const;
    /// Where the backpack is now; nullopt when it is no longer there (keep-alive lost). `deep`
    /// re-checks that the carrier still carries it.
    [[nodiscard]] std::optional<Emitter>        locate(Playback const& p, bool deep) const;
    [[nodiscard]] std::optional<UpgradeContext> validContext(Playback const& p) const;
    void                                        emit(SoundEvent const& ev) const;

    struct Reported {
        int          dim = 0;
        Vec3         pos{};
        std::int64_t lastSeen = 0;
    };
    [[nodiscard]] std::optional<Reported> reported(std::string const& uuid) const;

    std::map<Key, Playback>                mPlaybacks;
    std::map<std::string, Reported>        mReported;
    ll::event::ListenerPtr                 mTick;
    std::int64_t                           mNow = 0;
    std::function<void(SoundEvent const&)> mObserver;
    std::optional<int>                     mDurationOverride;
};

} // namespace abp::upgrade::jukebox
