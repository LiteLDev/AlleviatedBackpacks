#pragma once

#include <cstdint>
#include <optional>
#include <random>
#include <string_view>
#include <vector>

namespace abp::core {

/// What the jukebox does when a track ends by itself.
enum class RepeatMode {
    All,  ///< play the next disc, wrapping around after the last one
    One,  ///< play the same disc again
    None, ///< play the next disc, stop after the last one
};

[[nodiscard]] std::string_view           repeatModeId(RepeatMode mode);             ///< "all", "one", "none"
[[nodiscard]] std::optional<RepeatMode> repeatModeFromId(std::string_view id);
[[nodiscard]] RepeatMode                 nextRepeatMode(RepeatMode mode);           ///< All -> One -> None -> All

/// Play order of a jukebox's disc slots (pure logic; the game layer owns the sound).
///
/// Slots are indices into the jukebox's disc slots; `occupied[i]` tells whether slot i holds a
/// disc. Every call takes the current occupancy, so discs added or removed while playing are
/// picked up (new discs join the order, removed ones are skipped).
///
/// - In order: slot order, wrapping after the last occupied slot.
/// - Shuffled: a random permutation of the occupied slots; reshuffled after each full pass
///   (never starting the new pass with the disc that just ended, when there is a choice).
class JukeboxQueue {
public:
    explicit JukeboxQueue(std::uint32_t seed = 0x5eed1234u) : mRng(seed) {}

    /// Starts a new pass. With `startWith` (an occupied slot) that slot plays first; otherwise the
    /// first slot of the order. Returns the slot to play, nullopt when nothing is occupied.
    std::optional<int> start(std::vector<bool> const& occupied, bool shuffle, std::optional<int> startWith = std::nullopt);

    /// The slot currently playing (nullopt before `start` / after the queue ran out).
    [[nodiscard]] std::optional<int> current() const { return mCurrent; }
    [[nodiscard]] bool               shuffled() const { return mShuffle; }

    /// The current track ended by itself: the next slot according to `mode`, or nullopt to stop.
    std::optional<int> onTrackEnd(std::vector<bool> const& occupied, RepeatMode mode);
    /// "Next" button: always moves on (wrapping), whatever the repeat mode.
    std::optional<int> skipNext(std::vector<bool> const& occupied);
    /// "Previous" button: the slot before the current one (wrapping).
    std::optional<int> skipPrevious(std::vector<bool> const& occupied);
    /// Turns shuffle on/off while playing; the current disc keeps playing.
    void setShuffle(std::vector<bool> const& occupied, bool shuffle);

    /// The planned order of the current pass (tests / UI).
    [[nodiscard]] std::vector<int> const& order() const { return mOrder; }

private:
    void sync(std::vector<bool> const& occupied);
    void rebuild(std::vector<bool> const& occupied, std::optional<int> first);
    /// Advances; `wrapped` tells whether a new pass had to start.
    std::optional<int> advance(std::vector<bool> const& occupied, bool& wrapped);

    std::vector<int>   mOrder;
    int                mPos = -1;
    std::optional<int> mCurrent;
    bool               mShuffle = false;
    std::mt19937       mRng;
};

} // namespace abp::core
