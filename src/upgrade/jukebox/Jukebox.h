#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ll/api/event/ListenerBase.h"

#include "mc/deps/core/math/Vec3.h"

#include "core/Jukebox.h"
#include "upgrade/IUpgrade.h"
#include "upgrade/jukebox/Discs.h"

class Player;

namespace abp::upgrade::jukebox {

/// `config/jukebox.json`
struct JukeboxConfig {
    int  version = 1;
    /// Players within this many blocks of a playing backpack hear it (vanilla records: 64).
    int  range = 64;
    /// Note particles above a playing backpack.
    bool particles = true;
};

inline constexpr std::string_view kBasicId    = "jukebox";
inline constexpr std::string_view kAdvancedId = "advanced_jukebox";
inline constexpr int              kBasicSlots    = 1;
inline constexpr int              kAdvancedSlots = 12;

// ---- per-upgrade settings (ctx.settings()) --------------------------------------------------------
//   {"discs": [item|null, ...], "shuffle": bool, "repeat": "all"|"one"|"none"}

/// Number of disc slots of the jukebox installed in `ctx` (1 basic, 12 advanced; 0 if not a jukebox).
[[nodiscard]] int discSlots(UpgradeContext const& ctx);
/// True while `ctx` still refers to an installed jukebox upgrade.
[[nodiscard]] bool isJukebox(UpgradeContext const& ctx);
[[nodiscard]] core::ItemStackData disc(UpgradeContext const& ctx, int index);
void                              setDisc(UpgradeContext const& ctx, int index, core::ItemStackData const& item);
[[nodiscard]] std::vector<bool>   occupiedDiscs(UpgradeContext const& ctx);
[[nodiscard]] bool                shuffle(UpgradeContext const& ctx);
[[nodiscard]] core::RepeatMode    repeatMode(UpgradeContext const& ctx);
void                              setShuffle(UpgradeContext const& ctx, bool on);
void                              setRepeatMode(UpgradeContext const& ctx, core::RepeatMode mode);

/// One sound packet the service sent (observer hook for selftests).
struct SoundEvent {
    enum class Kind { Play, Stop };
    Kind         kind;
    std::int64_t listener;   ///< ActorUniqueID of the receiving player
    std::string  sound;      ///< sound name
    float        volume = 1; ///< Play only
    float        offset = 0; ///< Play only: playback position in seconds (late joiners)
    bool         positional = false;
};

/// Plays jukebox upgrades. One playback per installed jukebox (backpack uuid + upgrade slot).
///
/// Sound: Bedrock has no sound that can be attached to an entity from the server, so a carried
/// backpack plays a non-positional (2D) copy of the record sound (`abp.jukebox.<disc>` from our
/// resource pack) to every player within `range` of the carrier: the music "follows" the carrier
/// because it plays in their ears; others get a volume scaled by their distance when they start
/// hearing it, and a StopSound when they leave the range or dimension. Players who come into range
/// mid-track are sent the current playback position. A placed backpack plays the positional (3D)
/// copy (`abp.jukebox3d.<disc>`) at the block, like a vanilla jukebox.
///
/// Playback stops when the disc or the upgrade is removed or disabled, the backpack leaves the
/// carrier's inventory (dropped, stored, placed), the carrier dies or leaves, or a placed backpack
/// is no longer reported by `tickPlaced` (picked up / unloaded).
class Service {
public:
    static Service& get();

    void install();
    void uninstall();

    /// Starts playing. `index`: the disc slot to start with (nullopt: the first in play order).
    /// `starter` is the player who pressed Play: the carrier if they carry the backpack, else the
    /// placed backpack (see `tickPlaced`) or any player carrying it. Returns false (with a
    /// translated reason) if there is nothing to play or nowhere to play it from.
    bool play(UpgradeContext const& ctx, Player* starter, std::optional<int> index = std::nullopt, std::string* reason = nullptr);
    void stop(std::string const& uuid, int slot);
    /// Stops every jukebox of a backpack (e.g. when a placed backpack is broken / picked up).
    void stopAll(std::string const& uuid);
    bool next(UpgradeContext const& ctx);
    bool previous(UpgradeContext const& ctx);
    /// Applies changed settings (shuffle, discs) to a running playback.
    void settingsChanged(UpgradeContext const& ctx);

    struct Status {
        int index;         ///< disc slot playing
        int elapsedTicks;
        int durationTicks;
    };
    [[nodiscard]] std::optional<Status> status(std::string const& uuid, int slot) const;
    [[nodiscard]] bool                  playing(std::string const& uuid, int slot) const { return status(uuid, slot).has_value(); }
    /// ActorUniqueIDs of the players currently hearing a playback.
    [[nodiscard]] std::vector<std::int64_t> listeners(std::string const& uuid, int slot) const;
    [[nodiscard]] std::size_t               count() const { return mPlaybacks.size(); }

    /// For placed backpacks (a block feature): call at least once a second while the backpack is
    /// placed and loaded at `pos` in `dim`. Its jukeboxes then play at the block; they stop if this
    /// isn't called for 2 seconds. Call `stopAll(uuid)` when the backpack is picked up / broken.
    void tickPlaced(std::shared_ptr<game::Backpack> const& backpack, int dim, Vec3 const& pos);

    /// Runs `fn` on the server thread after `ticks` ticks (menu switching).
    void runLater(int ticks, std::function<void()> fn) { mLater.emplace_back(mNow + std::max(1, ticks), std::move(fn)); }

    // ---- selftests ----------------------------------------------------------------------------
    void setObserver(std::function<void(SoundEvent const&)> fn) { mObserver = std::move(fn); }
    /// Overrides every track's length (ticks); nullopt = real lengths.
    void setDurationOverride(std::optional<int> ticks) { mDurationOverride = ticks; }

private:
    struct Placed {
        int          dim;
        Vec3         pos;
        std::int64_t lastSeen;
    };
    struct Playback {
        std::weak_ptr<game::Backpack> backpack;
        std::string                   uuid;
        int                           slot = 0;
        std::string                   upgradeItem;
        bool                          carried  = true;
        std::int64_t                  carrier  = 0; ///< ActorUniqueID (carried)
        core::JukeboxQueue            queue;
        int                           index = -1;
        core::ItemStackData           disc;
        DiscInfo                      info;
        std::string                   sound;
        std::int64_t                  start = 0; ///< service tick the track started
        std::int64_t                  end   = 0;
        std::set<std::int64_t>        heard; ///< listeners currently playing the sound
        int                           phase = 0;
    };
    using Key = std::pair<std::string, int>;

    struct Emitter {
        bool  carried;
        int   dim;
        Vec3  pos;
        Player* carrier;
    };

    void tick();
    bool startTrack(Playback& p, int index, UpgradeContext const& ctx);
    void stopSounds(Playback& p);
    void forgetOthers(Playback const& p, std::int64_t listener);
    void spawnNotes(Emitter const& e);
    void updateListeners(Playback& p, Emitter const& e, bool force);
    [[nodiscard]] std::optional<Emitter> emitter(Playback& p, bool deepCheck);
    [[nodiscard]] std::optional<UpgradeContext> validContext(Playback const& p) const;
    void                                 sendPlay(Player& to, Playback const& p, Emitter const& e, float volume, float offset);
    void                                 sendStop(Player& to, std::string const& sound);
    void                                 emit(SoundEvent const& ev) const;

    std::map<Key, Playback>                       mPlaybacks;
    std::unordered_map<std::string, Placed>       mPlaced;
    ll::event::ListenerPtr                        mTick;
    std::int64_t                                  mNow = 0;
    std::function<void(SoundEvent const&)>        mObserver;
    std::optional<int>                            mDurationOverride;
    std::vector<std::pair<std::int64_t, std::function<void()>>> mLater;
};

} // namespace abp::upgrade::jukebox
