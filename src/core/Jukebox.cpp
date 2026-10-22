#include "core/Jukebox.h"

#include <algorithm>

namespace abp::core {

std::string_view repeatModeId(RepeatMode mode) {
    switch (mode) {
    case RepeatMode::All:
        return "all";
    case RepeatMode::One:
        return "one";
    case RepeatMode::None:
        return "no";
    }
    return "no";
}

std::optional<RepeatMode> repeatModeFromId(std::string_view id) {
    if (id == "all") return RepeatMode::All;
    if (id == "one") return RepeatMode::One;
    if (id == "no" || id == "none") return RepeatMode::None;
    return std::nullopt;
}

RepeatMode nextRepeatMode(RepeatMode mode) {
    switch (mode) {
    case RepeatMode::All:
        return RepeatMode::One;
    case RepeatMode::One:
        return RepeatMode::None;
    case RepeatMode::None:
        return RepeatMode::All;
    }
    return RepeatMode::None;
}

namespace {
bool isOccupied(std::vector<bool> const& occupied, int slot) {
    return slot >= 0 && slot < static_cast<int>(occupied.size()) && occupied[static_cast<std::size_t>(slot)];
}
} // namespace

void JukeboxQueue::sync(std::vector<bool> const& occupied) {
    // SC: discsRemoved leave playlist and history, discsAdded join the end of the playlist.
    for (int i = 0; i < static_cast<int>(std::max(occupied.size(), mKnown.size())); ++i) {
        bool was = isOccupied(mKnown, i);
        bool is  = isOccupied(occupied, i);
        if (was && !is) {
            std::erase(mPlaylist, i);
            std::erase(mHistory, i);
        } else if (!was && is) {
            mPlaylist.push_back(i);
        }
    }
    mKnown = occupied;
}

void JukeboxQueue::initPlaylist(std::vector<bool> const& occupied, bool excludeActive) {
    mPlaylist.clear();
    for (int i = 0; i < static_cast<int>(occupied.size()); ++i) {
        if (occupied[static_cast<std::size_t>(i)] && (!excludeActive || !mCurrent || i != *mCurrent)) mPlaylist.push_back(i);
    }
    if (mShuffle) std::shuffle(mPlaylist.begin(), mPlaylist.end(), mRng);
    mKnown = occupied;
}

std::optional<int> JukeboxQueue::playNext(std::vector<bool> const& occupied, bool startOverIfAtTheEnd) {
    sync(occupied);
    if (mPlaylist.empty() && startOverIfAtTheEnd) initPlaylist(occupied, false);
    if (mPlaylist.empty()) return std::nullopt;
    if (mCurrent && isOccupied(occupied, *mCurrent)) { // a removed disc never comes back
        mHistory.push_back(*mCurrent);
        if (mHistory.size() > occupied.size()) mHistory.pop_front();
    }
    mCurrent = mPlaylist.front();
    mPlaylist.pop_front();
    return mCurrent;
}

std::optional<int> JukeboxQueue::start(std::vector<bool> const& occupied, bool shuffle, std::optional<int> startWith) {
    mShuffle = shuffle;
    mCurrent.reset();
    mHistory.clear();
    initPlaylist(occupied, false);
    if (startWith && isOccupied(occupied, *startWith)) {
        std::erase(mPlaylist, *startWith);
        mPlaylist.push_front(*startWith);
    }
    return playNext(occupied, false);
}

std::optional<int> JukeboxQueue::onTrackEnd(std::vector<bool> const& occupied, RepeatMode mode) {
    if (mode == RepeatMode::One) {
        sync(occupied);
        if (mCurrent && isOccupied(occupied, *mCurrent)) return mCurrent;
        mCurrent.reset();
        return std::nullopt;
    }
    auto next = playNext(occupied, mode == RepeatMode::All);
    if (!next) mCurrent.reset();
    return next;
}

std::optional<int> JukeboxQueue::skipNext(std::vector<bool> const& occupied) { return playNext(occupied, true); }

std::optional<int> JukeboxQueue::skipPrevious(std::vector<bool> const& occupied) {
    sync(occupied);
    if (mHistory.empty()) return std::nullopt;
    if (mCurrent && isOccupied(occupied, *mCurrent)) mPlaylist.push_front(*mCurrent);
    mCurrent = mHistory.back();
    mHistory.pop_back();
    return mCurrent;
}

void JukeboxQueue::setShuffle(std::vector<bool> const& occupied, bool shuffle) {
    mShuffle = shuffle;
    initPlaylist(occupied, true);
}

} // namespace abp::core
