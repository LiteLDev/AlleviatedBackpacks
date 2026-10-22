#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <random>
#include <string_view>
#include <vector>

namespace abp::core {

/// What the jukebox does when a track ends by itself (SC `RepeatMode`, default `None`).
enum class RepeatMode {
    All,  ///< play the next disc of the playlist, starting over after the last one
    One,  ///< play the same disc again
    None, ///< play the rest of the playlist, then stop
};

[[nodiscard]] std::string_view           repeatModeId(RepeatMode mode); ///< "all", "one", "no"
/// Also accepts "none" (phase-1 settings).
[[nodiscard]] std::optional<RepeatMode> repeatModeFromId(std::string_view id);
[[nodiscard]] RepeatMode                 nextRepeatMode(RepeatMode mode); ///< All -> One -> None -> All

/// Play order of a jukebox's disc slots (pure logic; the game layer owns the sound). Follows
/// Sophisticated Core's `JukeboxUpgradeWrapper`:
///
/// - a playlist (the occupied slots in slot order, shuffled when shuffle is on) is consumed from
///   the front; "next" moves the playing slot into a history (capped at the slot count) and plays
///   the playlist's next slot, rebuilding the playlist when it ran out;
/// - "previous" puts the playing slot back at the front of the playlist and plays the last slot of
///   the history (nothing happens when the history is empty);
/// - discs added while playing join the end of the playlist, removed ones leave playlist and
///   history (every call takes the current occupancy);
/// - toggling shuffle rebuilds the playlist without the playing slot.
class JukeboxQueue {
public:
    explicit JukeboxQueue(std::uint32_t seed = 0x5eed1234u) : mRng(seed) {}

    /// Starts playing: builds the playlist and plays its first slot (or `startWith` when it is
    /// occupied). Returns the slot to play, nullopt when nothing is occupied.
    std::optional<int> start(std::vector<bool> const& occupied, bool shuffle, std::optional<int> startWith = std::nullopt);

    /// The slot currently playing (nullopt before `start` / after the queue ran out).
    [[nodiscard]] std::optional<int> current() const { return mCurrent; }
    [[nodiscard]] bool               shuffled() const { return mShuffle; }

    /// The current track ended by itself: the next slot according to `mode`, or nullopt to stop.
    std::optional<int> onTrackEnd(std::vector<bool> const& occupied, RepeatMode mode);
    /// "Next" button (SC `playNext()`): the next slot, starting the playlist over when it ran out.
    std::optional<int> skipNext(std::vector<bool> const& occupied);
    /// "Previous" button (SC `playPrevious()`): the last slot of the history; nullopt (keep
    /// playing) when the history is empty.
    std::optional<int> skipPrevious(std::vector<bool> const& occupied);
    /// Turns shuffle on/off; the playlist is rebuilt without the playing slot.
    void setShuffle(std::vector<bool> const& occupied, bool shuffle);

    /// The remaining playlist (tests / UI).
    [[nodiscard]] std::vector<int> order() const { return {mPlaylist.begin(), mPlaylist.end()}; }
    [[nodiscard]] std::vector<int> history() const { return {mHistory.begin(), mHistory.end()}; }

private:
    void               sync(std::vector<bool> const& occupied);
    void               initPlaylist(std::vector<bool> const& occupied, bool excludeActive);
    std::optional<int> playNext(std::vector<bool> const& occupied, bool startOverIfAtTheEnd);

    std::deque<int>    mPlaylist;
    std::deque<int>    mHistory;
    std::vector<bool>  mKnown; ///< occupancy seen last (to find added / removed discs)
    std::optional<int> mCurrent;
    bool               mShuffle = false;
    std::mt19937       mRng;
};

} // namespace abp::core
