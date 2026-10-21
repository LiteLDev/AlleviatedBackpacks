#include <gtest/gtest.h>

#include <algorithm>
#include <set>

#include "core/Jukebox.h"

using namespace abp::core;

namespace {
std::vector<bool> slots(std::initializer_list<int> filled, int size = 12) {
    std::vector<bool> v(static_cast<std::size_t>(size), false);
    for (int i : filled) v[static_cast<std::size_t>(i)] = true;
    return v;
}
} // namespace

TEST(JukeboxRepeat, Ids) {
    for (auto m : {RepeatMode::All, RepeatMode::One, RepeatMode::None}) EXPECT_EQ(repeatModeFromId(repeatModeId(m)), m);
    EXPECT_EQ(repeatModeFromId("bogus"), std::nullopt);
    EXPECT_EQ(nextRepeatMode(RepeatMode::All), RepeatMode::One);
    EXPECT_EQ(nextRepeatMode(RepeatMode::One), RepeatMode::None);
    EXPECT_EQ(nextRepeatMode(RepeatMode::None), RepeatMode::All);
}

TEST(JukeboxQueue, EmptyPlaysNothing) {
    JukeboxQueue q;
    EXPECT_EQ(q.start(slots({}), false), std::nullopt);
    EXPECT_EQ(q.current(), std::nullopt);
    EXPECT_EQ(q.skipNext(slots({})), std::nullopt);
    EXPECT_EQ(q.skipPrevious(slots({})), std::nullopt);
}

TEST(JukeboxQueue, InOrderRepeatAll) {
    JukeboxQueue q;
    auto         occ = slots({1, 4, 7});
    EXPECT_EQ(q.start(occ, false), 1);
    EXPECT_EQ(q.onTrackEnd(occ, RepeatMode::All), 4);
    EXPECT_EQ(q.onTrackEnd(occ, RepeatMode::All), 7);
    EXPECT_EQ(q.onTrackEnd(occ, RepeatMode::All), 1); // wraps
}

TEST(JukeboxQueue, InOrderRepeatNoneStopsAfterLast) {
    JukeboxQueue q;
    auto         occ = slots({0, 2});
    EXPECT_EQ(q.start(occ, false), 0);
    EXPECT_EQ(q.onTrackEnd(occ, RepeatMode::None), 2);
    EXPECT_EQ(q.onTrackEnd(occ, RepeatMode::None), std::nullopt);
    EXPECT_EQ(q.current(), std::nullopt);
}

TEST(JukeboxQueue, SingleDiscRepeatNonePlaysOnce) {
    JukeboxQueue q;
    auto         occ = slots({0}, 1);
    EXPECT_EQ(q.start(occ, false), 0);
    EXPECT_EQ(q.onTrackEnd(occ, RepeatMode::None), std::nullopt);
}

TEST(JukeboxQueue, RepeatOne) {
    JukeboxQueue q;
    auto         occ = slots({3, 5});
    EXPECT_EQ(q.start(occ, false, 5), 5);
    EXPECT_EQ(q.onTrackEnd(occ, RepeatMode::One), 5);
    EXPECT_EQ(q.onTrackEnd(occ, RepeatMode::One), 5);
    // The buttons still move on.
    EXPECT_EQ(q.skipNext(occ), 3);
    EXPECT_EQ(q.onTrackEnd(occ, RepeatMode::One), 3);
    // The repeated disc was taken out: continue with the next one.
    EXPECT_EQ(q.onTrackEnd(slots({5}), RepeatMode::One), 5);
}

TEST(JukeboxQueue, SkipButtonsWrap) {
    JukeboxQueue q;
    auto         occ = slots({2, 6, 9});
    EXPECT_EQ(q.start(occ, false), 2);
    EXPECT_EQ(q.skipPrevious(occ), 9);
    EXPECT_EQ(q.skipPrevious(occ), 6);
    EXPECT_EQ(q.skipNext(occ), 9);
    EXPECT_EQ(q.skipNext(occ), 2);
}

TEST(JukeboxQueue, StartWith) {
    JukeboxQueue q;
    auto         occ = slots({0, 1, 2});
    EXPECT_EQ(q.start(occ, false, 1), 1);
    EXPECT_EQ(q.skipNext(occ), 2);
    EXPECT_EQ(q.start(occ, false, 5), 0); // unoccupied start slot: first disc
}

TEST(JukeboxQueue, DiscsChangeWhilePlaying) {
    JukeboxQueue q;
    EXPECT_EQ(q.start(slots({0, 4}), false), 0);
    // A disc added between: played next.
    EXPECT_EQ(q.onTrackEnd(slots({0, 2, 4}), RepeatMode::All), 2);
    // The playing disc removed: next continues after its slot.
    EXPECT_EQ(q.skipNext(slots({0, 4})), 4);
    EXPECT_EQ(q.skipPrevious(slots({4, 8})), 8);
    EXPECT_EQ(q.skipPrevious(slots({3, 5})), 5);
    EXPECT_EQ(q.skipPrevious(slots({3})), 3);
}

TEST(JukeboxQueue, ShufflePlaysEveryDiscOncePerPass) {
    JukeboxQueue q(42);
    auto         occ   = slots({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11});
    auto         first = q.start(occ, true);
    ASSERT_TRUE(first);
    std::set<int> seen{*first};
    for (int i = 0; i < 11; ++i) {
        auto n = q.onTrackEnd(occ, RepeatMode::All);
        ASSERT_TRUE(n);
        seen.insert(*n);
    }
    EXPECT_EQ(seen.size(), 12u);
    auto last = q.current();
    auto next = q.onTrackEnd(occ, RepeatMode::All); // new pass
    ASSERT_TRUE(next);
    EXPECT_NE(next, last) << "a new pass never starts with the disc that just ended";
}

TEST(JukeboxQueue, ShuffleChangesOrder) {
    // Over a few seeds at least one shuffled pass differs from slot order.
    bool differs = false;
    for (std::uint32_t seed = 1; seed < 6 && !differs; ++seed) {
        JukeboxQueue q(seed);
        q.start(slots({0, 1, 2, 3, 4, 5, 6, 7}), true);
        differs = !std::is_sorted(q.order().begin(), q.order().end());
    }
    EXPECT_TRUE(differs);
}

TEST(JukeboxQueue, ShuffleRepeatNoneStopsAfterPass) {
    JukeboxQueue q(7);
    auto         occ = slots({0, 1, 2});
    ASSERT_TRUE(q.start(occ, true));
    EXPECT_TRUE(q.onTrackEnd(occ, RepeatMode::None));
    EXPECT_TRUE(q.onTrackEnd(occ, RepeatMode::None));
    EXPECT_EQ(q.onTrackEnd(occ, RepeatMode::None), std::nullopt);
}

TEST(JukeboxQueue, ShuffleNewDiscJoinsCurrentPass) {
    JukeboxQueue q(3);
    auto         occ = slots({0, 1});
    ASSERT_TRUE(q.start(occ, true));
    auto          more = slots({0, 1, 5});
    std::set<int> seen{*q.current()};
    seen.insert(*q.onTrackEnd(more, RepeatMode::None));
    seen.insert(*q.onTrackEnd(more, RepeatMode::None));
    EXPECT_EQ(seen, (std::set<int>{0, 1, 5}));
    EXPECT_EQ(q.onTrackEnd(more, RepeatMode::None), std::nullopt);
}

TEST(JukeboxQueue, ToggleShuffleKeepsCurrent) {
    JukeboxQueue q(9);
    auto         occ = slots({0, 1, 2, 3});
    EXPECT_EQ(q.start(occ, false, 2), 2);
    q.setShuffle(occ, true);
    EXPECT_TRUE(q.shuffled());
    EXPECT_EQ(q.current(), 2);
    EXPECT_EQ(q.order().front(), 2);
    q.setShuffle(occ, false);
    EXPECT_EQ(q.current(), 2);
    EXPECT_EQ(q.skipNext(occ), 3);
}
