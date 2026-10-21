#include <gtest/gtest.h>

#include "upgrade/cooking/CookingLogic.h"

using namespace abp::core;
using namespace abp::upgrade::cooking;

namespace {
ItemStackData item(char const* id, int n, int max = 64) { return ItemStackData::of(id, n, max); }

/// raw_iron -> iron_ingot, oak_log -> charcoal; coal burns 1600, oak_log 300, lava bucket 20000.
CookEnv env(int cookTime = 200) {
    CookEnv e;
    e.result = [](ItemStackData const& in) -> std::optional<ItemStackData> {
        if (in.id == "raw_iron") return item("iron_ingot", 1);
        if (in.id == "oak_log") return item("charcoal", 1);
        return std::nullopt;
    };
    e.burnTicks = [cookTime](ItemStackData const& f) {
        int base = f.id == "coal" ? 1600 : f.id == "oak_log" ? 300 : f.id == "lava_bucket" ? 20000 : 0;
        return base * cookTime / 200;
    };
    e.afterBurn = [](ItemStackData const& f) {
        if (f.id == "lava_bucket") return item("bucket", 1, 16);
        return f.withCount(f.count - 1);
    };
    e.cookTime = cookTime;
    return e;
}

void run(CookState& s, CookEnv const& e, int ticks) {
    for (int i = 0; i < ticks; ++i) step(s, e);
}
} // namespace

TEST(CookingLogic, CoalSmeltsEightItems) {
    CookState s;
    s.input = item("raw_iron", 10);
    s.fuel  = item("coal", 1);
    auto e  = env();
    run(s, e, 1600);
    EXPECT_EQ(s.output.id, "iron_ingot");
    EXPECT_EQ(s.output.count, 8);
    EXPECT_EQ(s.input.count, 2);
    EXPECT_TRUE(s.fuel.empty());
    run(s, e, 400);
    EXPECT_EQ(s.output.count, 8) << "no fuel left";
    EXPECT_FALSE(s.burning());
}

TEST(CookingLogic, FastCookersUseFuelTwiceAsFast) {
    CookState s;
    s.input = item("raw_iron", 10);
    s.fuel  = item("coal", 1);
    auto e  = env(100);
    run(s, e, 800);
    EXPECT_EQ(s.output.count, 8);
    step(s, e); // the last fuel tick ends
    EXPECT_FALSE(s.burning());
    EXPECT_EQ(s.output.count, 8);
}

TEST(CookingLogic, NoFuelWastedWithoutInput) {
    CookState s;
    s.fuel = item("coal", 3);
    auto e = env();
    run(s, e, 50);
    EXPECT_EQ(s.fuel.count, 3);
    EXPECT_FALSE(s.burning());
    s.input = item("stone", 1); // no recipe
    run(s, e, 50);
    EXPECT_EQ(s.fuel.count, 3);
}

TEST(CookingLogic, StopsWhenOutputFull) {
    CookState s;
    s.input  = item("raw_iron", 5);
    s.fuel   = item("coal", 5);
    s.output = item("iron_ingot", 63);
    auto e   = env();
    run(s, e, 1000);
    EXPECT_EQ(s.output.count, 64);
    EXPECT_EQ(s.input.count, 4);
    s.output = item("charcoal", 1); // different item blocks too
    s.cook   = 0;
    run(s, e, 400);
    EXPECT_EQ(s.input.count, 4);
    EXPECT_EQ(s.cook, 0);
}

TEST(CookingLogic, LavaBucketLeavesBucket) {
    CookState s;
    s.input = item("raw_iron", 1);
    s.fuel  = item("lava_bucket", 1, 1);
    auto e  = env();
    step(s, e);
    EXPECT_EQ(s.fuel.id, "bucket");
    EXPECT_TRUE(s.burning());
}

TEST(CookingLogic, ProgressAndProduceCallback) {
    CookState s;
    s.input = item("raw_iron", 2);
    s.fuel  = item("coal", 1);
    auto e  = env();
    int  produced = 0;
    e.onProduce   = [&](ItemStackData const& r) { produced += r.count; };
    run(s, e, 100);
    EXPECT_EQ(s.cookPercent(), 50);
    EXPECT_GT(s.burnPercent(), 90);
    run(s, e, 100);
    EXPECT_EQ(produced, 1);
    // Losing the input resets progress.
    run(s, e, 50);
    s.input.clear();
    step(s, e);
    EXPECT_EQ(s.cook, 0);
}

TEST(CookingLogic, JsonRoundTrip) {
    CookState s;
    s.input     = item("raw_iron", 2);
    s.fuel      = item("coal", 7);
    s.output    = item("iron_ingot", 3);
    s.burn      = 55;
    s.burnTotal = 1600;
    s.cook      = 12;
    s.cookTotal = 200;
    auto back   = CookState::fromJson(s.toJson());
    EXPECT_EQ(back, s);
    EXPECT_EQ(CookState::fromJson(nlohmann::json::object()), CookState{});
    EXPECT_EQ(CookState::fromJson(nlohmann::json{{"burn", "x"}}).burn, 0);
}

TEST(CookingLogic, AutoPullsFilteredInputAndFuel) {
    BackpackInventory inv(6);
    inv.set(0, item("oak_log", 10));
    inv.set(1, item("raw_iron", 5));
    inv.set(2, item("coal", 3));
    Filter inFilter(9), fuelFilter(9);
    inFilter.setGhost(0, item("raw_iron", 1));
    AutoEnv   a{inv, inFilter, fuelFilter};
    CookState s;
    auto      e = env();
    EXPECT_TRUE(autoExchange(s, e, a));
    EXPECT_EQ(s.input.id, "raw_iron");
    EXPECT_EQ(s.input.count, 5);
    // Logs are not input (filter); fuel filter is empty, logs are not auto-input -> fuel candidates.
    // Coal comes after the logs in slot order, so the logs burn first.
    EXPECT_EQ(s.fuel.id, "oak_log");
    EXPECT_EQ(inv.at(0).count, 9);
    for (int i = 0; i < 2000; ++i) {
        autoExchange(s, e, a);
        step(s, e);
    }
    autoExchange(s, e, a);
    EXPECT_TRUE(s.input.empty());
    EXPECT_TRUE(s.output.empty());
    EXPECT_EQ(inv.count([](ItemStackData const& x) { return x.id == "iron_ingot"; }), 5);
    EXPECT_EQ(inv.count([](ItemStackData const& x) { return x.id == "coal"; }), 3);
}

TEST(CookingLogic, AutoNeverBurnsItsOwnInputWithEmptyFuelFilter) {
    BackpackInventory inv(4);
    inv.set(0, item("oak_log", 2));
    inv.set(1, item("coal", 1));
    Filter    inFilter(9), fuelFilter(9);
    AutoEnv   a{inv, inFilter, fuelFilter};
    CookState s;
    auto      e = env();
    autoExchange(s, e, a);
    EXPECT_EQ(s.input.id, "oak_log");
    EXPECT_EQ(s.fuel.id, "coal");
    // An explicit fuel filter may name a cookable item.
    Filter logsAsFuel(9);
    logsAsFuel.setGhost(0, item("oak_log", 1));
    BackpackInventory inv2(4);
    inv2.set(0, item("oak_log", 2));
    inv2.set(1, item("raw_iron", 1));
    Filter    ironOnly(9);
    ironOnly.setGhost(0, item("raw_iron", 1));
    AutoEnv   b{inv2, ironOnly, logsAsFuel};
    CookState t;
    autoExchange(t, e, b);
    EXPECT_EQ(t.input.id, "raw_iron");
    EXPECT_EQ(t.fuel.id, "oak_log");
}

TEST(CookingLogic, AutoFuelOnlyWhenNeeded) {
    BackpackInventory inv(4);
    inv.set(0, item("coal", 5));
    Filter    inFilter(9), fuelFilter(9);
    AutoEnv   a{inv, inFilter, fuelFilter};
    CookState s;
    auto      e = env();
    autoExchange(s, e, a);
    EXPECT_TRUE(s.fuel.empty()) << "nothing to cook";
    EXPECT_EQ(inv.at(0).count, 5);
}

TEST(CookingLogic, AutoOutputStaysWhenStorageFull) {
    BackpackInventory inv(1);
    inv.set(0, item("stone", 64));
    Filter    inFilter(9), fuelFilter(9);
    AutoEnv   a{inv, inFilter, fuelFilter};
    CookState s;
    s.output = item("iron_ingot", 4);
    EXPECT_FALSE(pushOutput(s, a));
    EXPECT_EQ(s.output.count, 4);
}

TEST(CookingLogic, AutoDenyFuelFilterStillSparesInputs) {
    BackpackInventory inv(4);
    inv.set(0, item("oak_log", 3));
    inv.set(1, item("coal", 2));
    Filter inFilter(9), fuelFilter(9);
    fuelFilter.mode = FilterMode::Deny;
    fuelFilter.setGhost(0, item("lava_bucket", 1, 1));
    AutoEnv   a{inv, inFilter, fuelFilter};
    CookState s;
    auto      e = env();
    autoExchange(s, e, a);
    EXPECT_EQ(s.input.id, "oak_log");
    EXPECT_EQ(s.fuel.id, "coal") << "logs are input, not fuel";
}

TEST(CookingLogic, AutoReplacesSpentLavaBucket) {
    BackpackInventory inv(4);
    inv.set(0, item("raw_iron", 5));
    inv.set(1, item("coal", 2));
    Filter    inFilter(9), fuelFilter(9);
    AutoEnv   a{inv, inFilter, fuelFilter};
    CookState s;
    s.fuel = item("bucket", 1, 16);
    auto e = env();
    autoExchange(s, e, a);
    EXPECT_EQ(s.fuel.id, "coal");
    EXPECT_EQ(inv.count([](ItemStackData const& x) { return x.id == "bucket"; }), 1);
}

TEST(CookingLogic, AutoDoesNotRecookItsProducts) {
    BackpackInventory inv(4);
    inv.set(0, item("charcoal", 3)); // pretend charcoal smelts further and was produced here
    inv.set(1, item("coal", 2));
    Filter  inFilter(9), fuelFilter(9);
    AutoEnv a{inv, inFilter, fuelFilter};
    a.isProduct = [](ItemStackData const& x) { return x.id == "charcoal"; };
    auto e      = env();
    e.result    = [](ItemStackData const& in) -> std::optional<ItemStackData> {
        if (in.id == "charcoal") return item("ash", 1);
        return std::nullopt;
    };
    CookState s;
    autoExchange(s, e, a);
    EXPECT_TRUE(s.input.empty()) << "products are not cooked again by default";
    Filter only(9);
    only.setGhost(0, item("charcoal", 1));
    AutoEnv b{inv, only, fuelFilter};
    b.isProduct = a.isProduct;
    autoExchange(s, e, b);
    EXPECT_EQ(s.input.id, "charcoal") << "unless the input allow list names them";
}
