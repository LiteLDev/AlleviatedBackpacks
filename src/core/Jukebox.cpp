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
        return "none";
    }
    return "all";
}

std::optional<RepeatMode> repeatModeFromId(std::string_view id) {
    if (id == "all") return RepeatMode::All;
    if (id == "one") return RepeatMode::One;
    if (id == "none") return RepeatMode::None;
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
    return RepeatMode::All;
}

namespace {
bool isOccupied(std::vector<bool> const& occupied, int slot) {
    return slot >= 0 && slot < static_cast<int>(occupied.size()) && occupied[static_cast<std::size_t>(slot)];
}

std::vector<int> occupiedSlots(std::vector<bool> const& occupied) {
    std::vector<int> out;
    for (int i = 0; i < static_cast<int>(occupied.size()); ++i)
        if (occupied[static_cast<std::size_t>(i)]) out.push_back(i);
    return out;
}
} // namespace

void JukeboxQueue::rebuild(std::vector<bool> const& occupied, std::optional<int> first) {
    mOrder = occupiedSlots(occupied);
    if (mShuffle) std::shuffle(mOrder.begin(), mOrder.end(), mRng);
    mPos = -1;
    if (first) {
        auto it = std::find(mOrder.begin(), mOrder.end(), *first);
        if (it != mOrder.end()) {
            if (mShuffle) {
                std::iter_swap(mOrder.begin(), it);
                mPos = 0;
            } else {
                mPos = static_cast<int>(it - mOrder.begin());
            }
        }
    }
}

void JukeboxQueue::sync(std::vector<bool> const& occupied) {
    if (!mShuffle) {
        // In order: the order is just the occupied slots; position = the current slot or the last
        // slot before it (so that advancing continues after it).
        mOrder = occupiedSlots(occupied);
        mPos   = -1;
        if (mCurrent) {
            for (int i = 0; i < static_cast<int>(mOrder.size()); ++i)
                if (mOrder[static_cast<std::size_t>(i)] <= *mCurrent) mPos = i;
        }
        return;
    }
    std::vector<int> kept;
    int              pos = -1;
    for (int i = 0; i < static_cast<int>(mOrder.size()); ++i) {
        int s = mOrder[static_cast<std::size_t>(i)];
        if (isOccupied(occupied, s)) kept.push_back(s);
        if (i == mPos) pos = static_cast<int>(kept.size()) - 1;
    }
    // New discs join the rest of the current pass at random places.
    for (int s : occupiedSlots(occupied)) {
        if (std::find(kept.begin(), kept.end(), s) != kept.end()) continue;
        int lo = pos + 1;
        int hi = static_cast<int>(kept.size());
        int at = lo >= hi ? hi : std::uniform_int_distribution<int>(lo, hi)(mRng);
        kept.insert(kept.begin() + at, s);
    }
    mOrder = std::move(kept);
    mPos   = pos;
}

std::optional<int> JukeboxQueue::start(std::vector<bool> const& occupied, bool shuffle, std::optional<int> startWith) {
    mShuffle = shuffle;
    if (startWith && !isOccupied(occupied, *startWith)) startWith.reset();
    rebuild(occupied, startWith);
    if (mOrder.empty()) {
        mCurrent.reset();
        return std::nullopt;
    }
    if (mPos < 0) mPos = 0;
    mCurrent = mOrder[static_cast<std::size_t>(mPos)];
    return mCurrent;
}

std::optional<int> JukeboxQueue::advance(std::vector<bool> const& occupied, bool& wrapped) {
    wrapped = false;
    sync(occupied);
    if (mOrder.empty()) {
        mCurrent.reset();
        return std::nullopt;
    }
    if (mPos + 1 < static_cast<int>(mOrder.size())) {
        ++mPos;
    } else {
        wrapped = true;
        auto last = mCurrent;
        rebuild(occupied, std::nullopt);
        if (mShuffle && mOrder.size() > 1 && last && mOrder.front() == *last) std::swap(mOrder[0], mOrder[1]);
        mPos = 0;
    }
    mCurrent = mOrder[static_cast<std::size_t>(mPos)];
    return mCurrent;
}

std::optional<int> JukeboxQueue::onTrackEnd(std::vector<bool> const& occupied, RepeatMode mode) {
    if (mode == RepeatMode::One && mCurrent && isOccupied(occupied, *mCurrent)) return mCurrent;
    bool wrapped = false;
    auto next    = advance(occupied, wrapped);
    if (mode == RepeatMode::None && wrapped) {
        mCurrent.reset();
        return std::nullopt;
    }
    return next;
}

std::optional<int> JukeboxQueue::skipNext(std::vector<bool> const& occupied) {
    bool wrapped = false;
    return advance(occupied, wrapped);
}

std::optional<int> JukeboxQueue::skipPrevious(std::vector<bool> const& occupied) {
    sync(occupied);
    if (mOrder.empty()) {
        mCurrent.reset();
        return std::nullopt;
    }
    bool currentKept = mCurrent && mPos >= 0 && mOrder[static_cast<std::size_t>(mPos)] == *mCurrent;
    if (currentKept || mPos < 0) {
        // Step back (wrapping). When the current disc was removed, mPos already points at the
        // disc before it, which is the "previous" one.
        mPos = mPos > 0 ? mPos - 1 : static_cast<int>(mOrder.size()) - 1;
    }
    mCurrent = mOrder[static_cast<std::size_t>(mPos)];
    return mCurrent;
}

void JukeboxQueue::setShuffle(std::vector<bool> const& occupied, bool shuffle) {
    if (shuffle == mShuffle) return;
    mShuffle = shuffle;
    auto cur = mCurrent && isOccupied(occupied, *mCurrent) ? mCurrent : std::nullopt;
    rebuild(occupied, cur);
    if (!cur && mShuffle) mPos = -1;
    if (!mShuffle && !cur) sync(occupied);
}

} // namespace abp::core
