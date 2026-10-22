// Selftests of the Alchemy upgrades: `/backpack selftest "alchemy.*"`.

#include "mc/server/SimulatedPlayer.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/effect/EffectDuration.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/GameType.h"

#include "feature/placed/TestSite.h"
#include "game/ItemConv.h"
#include "net/NativeScreens.h"
#include "selftest/SelfTest.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/alchemy/Alchemy.h"
#include "upgrade/feeding/Feeding.h"

namespace abp::upgrade::alchemy {
namespace {

using core::Tier;
using selftest::Context;

core::AccessContext const kInternal{core::Source::Internal};

// Bedrock potion ids (item aux values).
constexpr int kNightVision    = 5;
constexpr int kFireResistance = 12;
constexpr int kFireResLong    = 13;
constexpr int kSwiftness      = 14;
constexpr int kWaterBreathing = 19;
constexpr int kHealing        = 21;
constexpr int kSlowFalling    = 40;
constexpr int kNightVisionEffect = 16; // MobEffectIds::NightVision

core::ItemStackData data(std::string_view id, int count = 1, int aux = 0) {
    return game::ItemConv::toData(game::ItemConv::make(id, count, aux));
}
core::ItemStackData potion(int aux, int count = 1) { return data(kPotion, count, aux); }

int countIn(game::Backpack& bp, std::string_view id, int aux = -1) {
    return static_cast<int>(bp.inventory().count([&](core::ItemStackData const& s) { return s.id == id && (aux < 0 || s.aux == aux); }));
}

bool hasEffect(Actor& a, int id) {
    auto const* e = a.getEffect(static_cast<uint>(id));
    return e && e->mId != 0;
}

void addEffect(Actor& a, int id, int duration, int amplifier = 0) {
    MobEffectInstance inst(static_cast<uint>(id), EffectDuration{duration});
    inst.mAmplifier = amplifier;
    a.addEffect(inst);
}

/// A clean survival test player standing on the ground of the U2 test area.
ll::coro::CoroTask<> readyPlayer(Context& ctx, Player& p) {
    co_await placed::test::toGround(ctx, p);
    p.setPlayerGameType(GameType::Survival);
    p.removeAllEffects();
    p.stopFire();
    feeding::setHealth(p, static_cast<float>(p.getMaxHealth()));
}

struct Rig {
    std::shared_ptr<game::Backpack> bp;
    int                             slot;
    [[nodiscard]] UpgradeContext    ctx(Player* p = nullptr) const { return {bp, slot, p}; }
};

/// A backpack with an (advanced) alchemy upgrade and `filter` in its first filter slot.
Rig rig(Context& ctx, bool advanced, core::ItemStackData const& filter) {
    Rig r{ctx.createBackpack(Tier::Iron), 0};
    r.slot = ctx.installUpgrade(r.bp, advanced ? kAdvancedId : kBasicId);
    ctx.require(setFilter(r.ctx(), 0, filter), "filter " + filter.id + " accepted");
    return r;
}

/// Waits until the running application of `r` is done (at most `limit` ticks).
ll::coro::CoroTask<> waitApplied(Context& ctx, Rig const& r, int limit = 60) {
    for (int i = 0; i < limit && applying(r.ctx()); ++i) co_await ctx.ticks(1);
    ctx.check(!applying(r.ctx()), "application finished");
}

// ---- items / settings --------------------------------------------------------------------------------

ABP_REGISTER_SELFTEST("alchemy.items_and_defaults", [](Context& ctx) {
    ctx.check(!game::ItemConv::make(kBasicItem).isNull(), "alchemy upgrade item exists");
    ctx.check(!game::ItemConv::make(kAdvancedItem).isNull(), "advanced alchemy upgrade item exists");
    ctx.check(byId(kBasicId) && byId(kAdvancedId), "both upgrades registered");
    ctx.check(byId(kBasicId)->info().toggleable && byId(kBasicId)->info().hasSettings, "toggleable with a tab");

    auto def = [](core::ItemStackData const& item) { return defaultCondition(kindOf(item.id), itemEffects(item)); };
    ctx.check(def(potion(kFireResistance)) == Condition::OnFire, "fire resistance -> When on Fire");
    ctx.check(def(potion(kWaterBreathing)) == Condition::UnderWater, "water breathing -> When Under Water");
    ctx.check(def(potion(kHealing)) == Condition::Hurt, "healing -> When Hurt");
    ctx.check(def(potion(kSwiftness)) == Condition::Sprinting, "swiftness -> When Sprinting");
    ctx.check(def(potion(kSlowFalling)) == Condition::Falling, "slow falling -> When Falling");
    ctx.check(def(potion(kNightVision)) == Condition::Always, "night vision -> Keep Active");
    ctx.check(def(potion(0)) == Condition::Never, "water bottle -> Never");
    ctx.check(def(data(kMilkBucket)) == Condition::NegativeEffect, "milk -> negative effect");
    ctx.check(def(data(kSplashPotion, 1, kFireResistance)) == Condition::OnFire, "splash potions too");

    Settings s = Settings::fromJson({}, 4);
    ctx.check(canBeFilter(s, potion(kFireResistance)), "potion is a filter item");
    ctx.check(!canBeFilter(s, data("minecraft:bread")), "bread is not");
    ctx.check(!canBeFilter(s, data("minecraft:lingering_potion", 1, kFireResistance)), "lingering potions are not (SB)");
    ctx.check(canBeFilter(s, data("minecraft:ominous_bottle")), "ominous bottle is");
    ctx.check(canBeFilter(s, data("minecraft:enchanted_golden_apple")), "effect food is");
});

ABP_REGISTER_SELFTEST("alchemy.filter_slots", [](Context& ctx) {
    auto r = rig(ctx, false, potion(kFireResistance));
    auto s = settingsOf(r.ctx());
    ctx.check(s.filters.size() == 4u, "basic: 4 filter slots");
    ctx.check(s.filters[0].condition == Condition::OnFire, "default condition set");
    ctx.check(!setFilter(r.ctx(), 1, potion(kFireResistance)), "duplicate refused");
    ctx.check(!setFilter(r.ctx(), 1, data("minecraft:stone")), "non-alchemy item refused");
    ctx.check(setFilter(r.ctx(), 0, {}), "clear");
    ctx.check(settingsOf(r.ctx()).filters[0].condition == Condition::Never, "cleared -> Never");
    auto a = rig(ctx, true, potion(kHealing));
    auto as = settingsOf(a.ctx());
    ctx.check(as.filters.size() == 8u, "advanced: 8 filter slots");
    ctx.check(as.filters[0].condition == Condition::Hurt && std::abs(as.filters[0].value - 0.75f) < 1e-4f, "HURT 75%");
    ctx.check(as.matchAll && as.matchDuration && as.matchAmplifier, "match toggles default to true");
});

// ---- conditions on a real player ---------------------------------------------------------------

ABP_REGISTER_SELFTEST("alchemy.on_fire", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p = ctx.player();
    co_await readyPlayer(ctx, p);
    auto r = rig(ctx, false, potion(kFireResistance));
    r.bp->inventory().insert(potion(kFireResistance, 2), kInternal);
    ctx.check(!checkNow(r.ctx(&p), {&p}), "not on fire: nothing used");
    p.setOnFire(8, true);
    co_await ctx.ticks(1);
    ctx.require(stateOf(p).onFire, "player burns");
    ctx.require(checkNow(r.ctx(&p), {&p}), "on fire: a potion is used");
    ctx.check(applying(r.ctx()), "drinking takes the use time");
    ctx.check(countIn(*r.bp, kPotion) == 1, "one potion taken");
    co_await ctx.ticks(10);
    ctx.check(applying(r.ctx()) && !hasEffect(p, effect::FireResistance), "not applied before the use time");
    co_await waitApplied(ctx, r);
    ctx.check(hasEffect(p, effect::FireResistance), "fire resistance applied");
    ctx.check(countIn(*r.bp, kGlassBottle) == 1, "glass bottle returned to the backpack");
    ctx.check(!checkNow(r.ctx(&p), {&p}), "effect present: not used again");
    ctx.check(countIn(*r.bp, kPotion) == 1, "second potion kept");
    p.stopFire();
});

ABP_REGISTER_SELFTEST("alchemy.under_water", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p = ctx.player();
    co_await readyPlayer(ctx, p);
    placed::test::Site site(p, 0, 0);
    auto r = rig(ctx, false, potion(kWaterBreathing));
    r.bp->inventory().insert(potion(kWaterBreathing), kInternal);
    ctx.check(!checkNow(r.ctx(&p), {&p}), "dry: nothing used");
    site.set(site.at(0, 0), "minecraft:water");
    site.set(site.at(0, 1), "minecraft:water");
    co_await ctx.ticks(3);
    ctx.require(stateOf(p).underWater, "head under water");
    ctx.require(checkNow(r.ctx(&p), {&p}), "under water: potion used");
    co_await waitApplied(ctx, r);
    ctx.check(hasEffect(p, effect::WaterBreathing), "water breathing applied");
});

ABP_REGISTER_SELFTEST("alchemy.hurt_threshold", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p = ctx.player();
    co_await readyPlayer(ctx, p);
    auto r = rig(ctx, false, potion(kHealing));
    r.bp->inventory().insert(potion(kHealing, 2), kInternal);
    feeding::setHealth(p, 16); // 80%
    ctx.check(!checkNow(r.ctx(&p), {&p}), "80% health is not below 75%");
    feeding::setHealth(p, 10);
    ctx.require(checkNow(r.ctx(&p), {&p}), "50% health: healing used");
    co_await waitApplied(ctx, r);
    ctx.check(p.getHealth() > 10, "healed (" + std::to_string(p.getHealth()) + ")");
    // Scroll the threshold down to 40%: 50% is no longer below it.
    auto s = settingsOf(r.ctx());
    for (int i = 0; i < 7; ++i) s.scrollValue(0, -1);
    writeSettings(r.ctx(), s);
    ctx.check(percent(settingsOf(r.ctx()).filters[0].value) == 40, "threshold 40%");
    feeding::setHealth(p, 10);
    co_await ctx.ticks(kCheckInterval + 1);
    ctx.check(!checkNow(r.ctx(&p), {&p}), "50% is not below 40%");
});

ABP_REGISTER_SELFTEST("alchemy.splash_one_tick", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p = ctx.player();
    co_await readyPlayer(ctx, p);
    auto splash = data(kSplashPotion, 1, kFireResistance);
    auto r      = rig(ctx, false, splash);
    auto set = settingsOf(r.ctx());
    set.filters[0].condition = Condition::Always;
    writeSettings(r.ctx(), set);
    r.bp->inventory().insert(splash, kInternal);
    ctx.require(checkNow(r.ctx(&p), {&p}), "splash potion thrown");
    ctx.check(hasEffect(p, effect::FireResistance), "applied right away");
    co_await ctx.ticks(2);
    ctx.check(!applying(r.ctx()), "done after 1 tick");
    ctx.check(countIn(*r.bp, kGlassBottle) == 0 && countIn(*r.bp, kSplashPotion) == 0, "nothing returned");
});

ABP_REGISTER_SELFTEST("alchemy.milk_negative_effect", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p = ctx.player();
    co_await readyPlayer(ctx, p);
    auto r = rig(ctx, false, data(kMilkBucket));
    r.bp->inventory().insert(data(kMilkBucket), kInternal);
    addEffect(p, effect::Speed, 600);
    ctx.check(!checkNow(r.ctx(&p), {&p}), "only a beneficial effect: no milk");
    addEffect(p, effect::Poison, 600);
    ctx.require(checkNow(r.ctx(&p), {&p}), "poisoned: milk used");
    co_await waitApplied(ctx, r);
    ctx.check(!hasEffect(p, effect::Poison), "poison cured");
    ctx.check(countIn(*r.bp, kBucket) == 1, "bucket returned");
});

ABP_REGISTER_SELFTEST("alchemy.ominous_bottle", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p = ctx.player();
    co_await readyPlayer(ctx, p);
    auto bottle = data(kOminousBottle, 1, 2);
    auto r      = rig(ctx, false, bottle);
    r.bp->inventory().insert(data(kOminousBottle, 2, 2), kInternal);
    ctx.require(checkNow(r.ctx(&p), {&p}), "no Bad Omen: bottle used");
    co_await waitApplied(ctx, r);
    auto const* omen = p.getEffect(static_cast<uint>(effect::BadOmen));
    ctx.check(omen && omen->mId != 0, "Bad Omen applied");
    ctx.check(omen && omen->mAmplifier == 2, "level from the bottle");
    co_await ctx.ticks(kCheckInterval + 1);
    ctx.check(!checkNow(r.ctx(&p), {&p}), "Bad Omen present: not used");
    p.removeAllEffects();
});

ABP_REGISTER_SELFTEST("alchemy.golden_apple", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p = ctx.player();
    co_await readyPlayer(ctx, p);
    auto apple = data(kGoldenApple);
    auto r     = rig(ctx, false, apple);
    r.bp->inventory().insert(data(kGoldenApple, 2), kInternal);
    ctx.require(checkNow(r.ctx(&p), {&p}), "golden apple eaten");
    co_await waitApplied(ctx, r);
    ctx.check(hasEffect(p, effect::Absorption) || hasEffect(p, effect::Regeneration), "golden apple effects");
    ctx.check(countIn(*r.bp, kGoldenApple) == 1, "one apple used");
    p.removeAllEffects();
});

ABP_REGISTER_SELFTEST("alchemy.advanced_match_options", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p = ctx.player();
    co_await readyPlayer(ctx, p);
    // Basic (everything matched): a long fire resistance potion doesn't match the plain filter.
    auto b = rig(ctx, false, potion(kFireResistance));
    b.bp->inventory().insert(potion(kFireResLong), kInternal);
    auto bs = settingsOf(b.ctx());
    bs.filters[0].condition = Condition::Always;
    writeSettings(b.ctx(), bs);
    ctx.check(!checkNow(b.ctx(&p), {&p}), "basic: exact potion required");
    // Advanced, Ignore Duration: it does.
    auto a  = rig(ctx, true, potion(kFireResistance));
    a.bp->inventory().insert(potion(kFireResLong), kInternal);
    auto as = settingsOf(a.ctx());
    as.filters[0].condition = Condition::Always;
    as.matchDuration        = false;
    writeSettings(a.ctx(), as);
    ctx.require(checkNow(a.ctx(&p), {&p}), "ignore duration: long potion used");
    co_await waitApplied(ctx, a);
    ctx.check(hasEffect(p, effect::FireResistance), "applied");
    p.removeAllEffects();
});

// ---- toggling, tab actions ---------------------------------------------------------------------

ABP_REGISTER_SELFTEST("alchemy.disabled_does_nothing", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p = ctx.player();
    co_await readyPlayer(ctx, p);
    auto r = rig(ctx, false, potion(kNightVision));
    r.bp->inventory().insert(potion(kNightVision), kInternal);
    setEnabled(r.bp, r.slot, false);
    ctx.give(p, ctx.backpackItem(*r.bp));
    co_await ctx.ticks(3 * kCheckInterval);
    ctx.check(!hasEffect(p, kNightVisionEffect) && countIn(*r.bp, kPotion) == 1, "switched off: not used");
    setEnabled(r.bp, r.slot, true);
    for (int i = 0; i < 80 && !hasEffect(p, kNightVisionEffect); ++i) co_await ctx.ticks(1);
    ctx.check(hasEffect(p, kNightVisionEffect), "switched on: the carried backpack's ticker used night vision");
    p.removeAllEffects();
});

ABP_REGISTER_SELFTEST("alchemy.tab_actions", [](Context& ctx) {
    auto& p = ctx.player();
    auto  r = rig(ctx, false, potion(kFireResistance));
    auto  act = [&](Rig const& rr, nlohmann::json data) {
        for (auto const& h : net::TabActionRegistry::entries()) {
            if (h.upgrade == kGroup && h.handle) return h.handle(p, rr.ctx(&p), data);
        }
        return false;
    };
    auto tabData = [&](Rig const& rr) {
        for (auto const& d : net::TabDataRegistry::entries()) {
            if (d.upgrade == kGroup && d.data) return d.data(p, rr.ctx(&p));
        }
        return nlohmann::json{};
    };
    auto j = tabData(r);
    ctx.check(j.value("tabLabel", "") == "Alchemy", "tab title (got " + j.value("tabLabel", "") + ")");
    ctx.check(j["alchemy"]["filters"].size() == 4u, "4 filters in the tab data");

    ctx.check(act(r, {{"alchemy", "condition"}, {"index", 0}}), "toggle condition");
    ctx.check(settingsOf(r.ctx()).filters[0].condition == Condition::Falling, "ON_FIRE -> FALLING");
    ctx.check(!act(r, {{"alchemy", "condition"}, {"index", 1}}), "no toggling without a filter");
    ctx.check(!act(r, {{"alchemy", "matchAll"}}), "basic: no match toggles");

    // Filter slot clicks use the cursor item (copied, not consumed); an empty cursor clears.
    p.mPlayerUIContainer->setItem(0, game::ItemConv::make(kPotion, 3, kHealing));
    ctx.check(act(r, {{"alchemy", "filter"}, {"index", 1}}), "set filter from the cursor");
    auto s = settingsOf(r.ctx());
    ctx.check(s.filters[1].filter.aux == kHealing && s.filters[1].condition == Condition::Hurt, "healing filter, HURT");
    ctx.check(p.mPlayerUIContainer->getItem(0).mCount == 3, "cursor not consumed");
    ctx.check(act(r, {{"alchemy", "scroll"}, {"index", 1}, {"dir", 1}}), "scroll");
    ctx.check(percent(settingsOf(r.ctx()).filters[1].value) == 80, "80%");
    p.mPlayerUIContainer->setItem(0, ItemStack{});
    ctx.check(act(r, {{"alchemy", "filter"}, {"index", 1}}), "clear with an empty cursor");
    ctx.check(settingsOf(r.ctx()).filters[1].filter.empty(), "cleared");

    auto a = rig(ctx, true, potion(kFireResistance));
    ctx.check(tabData(a).value("tabLabel", "") == "Adv. Alchemy", "advanced tab title");
    ctx.check(act(a, {{"alchemy", "matchAll"}}) && !settingsOf(a.ctx()).matchAll, "All Effects Missing");
    ctx.check(act(a, {{"alchemy", "matchDuration"}}) && !settingsOf(a.ctx()).matchDuration, "Ignore Duration");
    ctx.check(act(a, {{"alchemy", "matchAmplifier"}}) && !settingsOf(a.ctx()).matchAmplifier, "Ignore Amplifier");
    ctx.check(!tabData(a)["alchemy"].value("entityMatchOption", true), "carried backpack: no entity target option");
    ctx.check(!act(a, {{"alchemy", "entityMatch"}}), "entity target refused when not placed");
});

// ---- placed backpack ---------------------------------------------------------------------------

ABP_REGISTER_SELFTEST("alchemy.placed_targets", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p = ctx.player();
    co_await readyPlayer(ctx, p);
    placed::test::Site site(p, 2, 0);
    auto r = rig(ctx, true, potion(kNightVision));
    r.bp->inventory().insert(potion(kNightVision, 2), kInternal);
    auto pos  = site.at(0, 0);
    site.touch(pos);
    auto item = ctx.backpackItem(*r.bp);
    ctx.require(placed::PlacedBackpacks::get().place(site.region(), pos, item, 2) == placed::PlacedBackpacks::PlaceResult::Ok, "placed");
    auto tick = [&] {
        auto* type = upgradeFor(r.bp->data().upgrades.at(static_cast<std::size_t>(r.slot)).item);
        type->onPlacedTick(PlacedTickContext{r.ctx(), site.region(), static_cast<int>(p.getDimensionId()), pos.x, pos.y, pos.z, 2, 2});
    };
    auto set = settingsOf(r.ctx());
    set.entityMatch = EntityMatch::Entities;
    writeSettings(r.ctx(), set);
    co_await ctx.ticks(kCheckInterval + 1);
    tick();
    ctx.check(!applying(r.ctx()), "Target Entities Only: the player is skipped");
    set.entityMatch = EntityMatch::Players;
    writeSettings(r.ctx(), set);
    co_await ctx.ticks(kCheckInterval + 1);
    tick();
    ctx.require(applying(r.ctx()), "Target Players Only: the player within 3 blocks gets it");
    co_await waitApplied(ctx, r);
    ctx.check(hasEffect(p, kNightVisionEffect), "night vision applied from the placed backpack");
    p.removeAllEffects();
});

// ---- GX-2: SC's applyTo loop and the splash ---------------------------------------------------

/// Filter 0 = splash Swiftness, filter 1 = Fire Resistance potion, both "Keep Active".
Rig twoFilterRig(Context& ctx) {
    auto splash = data(kSplashPotion, 1, kSwiftness);
    auto r      = rig(ctx, false, splash);
    ctx.require(setFilter(r.ctx(), 1, potion(kFireResistance)), "second filter");
    auto s                   = settingsOf(r.ctx());
    s.filters[0].condition   = Condition::Always;
    s.filters[1].condition   = Condition::Always;
    writeSettings(r.ctx(), s);
    return r;
}

ABP_REGISTER_SELFTEST("alchemy.second_filter_same_check", [](Context& ctx) -> ll::coro::CoroTask<> {
    // SC: the applying-exit runs after each slot, and the filter loop doesn't stop: filter 1 still
    // visits slot 0 after filter 0's splash started applying, finds its potion there and uses it.
    auto& p = ctx.player();
    co_await readyPlayer(ctx, p);
    auto r = twoFilterRig(ctx);
    r.bp->inventory().set(0, potion(kFireResistance));
    r.bp->inventory().set(1, data(kSplashPotion, 1, kSwiftness));
    ctx.require(checkNow(r.ctx(&p), {&p}), "one check");
    ctx.check(hasEffect(p, effect::Speed), "splash (filter 0, slot 1) thrown at once");
    ctx.check(countIn(*r.bp, kSplashPotion) == 0 && countIn(*r.bp, kPotion) == 0, "both items taken in the same check");
    ctx.check(applying(r.ctx()), "the potion (filter 1, slot 0) is being drunk");
    co_await waitApplied(ctx, r);
    ctx.check(hasEffect(p, effect::FireResistance), "fire resistance applied from the second item");
    ctx.check(countIn(*r.bp, kGlassBottle) == 1, "its bottle returned");
    p.removeAllEffects();
});

ABP_REGISTER_SELFTEST("alchemy.second_filter_first_slot_only", [](Context& ctx) -> ll::coro::CoroTask<> {
    // ... but only the first slot: with the potion in slot 1 filter 1 stops after slot 0.
    auto& p = ctx.player();
    co_await readyPlayer(ctx, p);
    auto r = twoFilterRig(ctx);
    r.bp->inventory().set(0, data(kSplashPotion, 1, kSwiftness));
    r.bp->inventory().set(1, potion(kFireResistance));
    ctx.require(checkNow(r.ctx(&p), {&p}), "one check");
    ctx.check(hasEffect(p, effect::Speed), "splash used");
    ctx.check(countIn(*r.bp, kPotion, kFireResistance) == 1, "the potion in slot 1 is not reached in this check");
    co_await waitApplied(ctx, r);
    ctx.check(!hasEffect(p, effect::FireResistance), "no fire resistance yet");
    co_await ctx.ticks(kCheckInterval + 1);
    ctx.require(checkNow(r.ctx(&p), {&p}), "next check");
    co_await waitApplied(ctx, r);
    ctx.check(hasEffect(p, effect::FireResistance) && countIn(*r.bp, kPotion, kFireResistance) == 0, "used by the next check");
    p.removeAllEffects();
});

ABP_REGISTER_SELFTEST("alchemy.inception_inventory", [](Context& ctx) -> ll::coro::CoroTask<> {
    // SC getInventoryForUpgradeProcessing: with Inception the potion may sit in a backpack inside,
    // and the bottle goes back through the same chain.
    auto& p = ctx.player();
    co_await readyPlayer(ctx, p);
    auto r = rig(ctx, false, potion(kNightVision));
    ctx.require(ctx.installUpgrade(r.bp, "inception") >= 0, "inception installed");
    auto inner = ctx.createBackpack(Tier::Leather);
    inner->inventory().insert(potion(kNightVision), kInternal);
    ctx.require(r.bp->inventory().insert(game::ItemConv::toData(ctx.backpackItem(*inner)), core::AccessContext{core::Source::Player}).inserted == 1, "inner stored");
    ctx.require(checkNow(r.ctx(&p), {&p}), "the potion inside the inner backpack is used");
    ctx.check(countIn(*inner, kPotion) == 0, "taken from the inner backpack");
    co_await waitApplied(ctx, r);
    ctx.check(hasEffect(p, kNightVisionEffect), "applied");
    ctx.check(countIn(*r.bp, kGlassBottle) == 1, "the bottle goes into the first inventory with room (main)");
    p.removeAllEffects();
});

ABP_REGISTER_SELFTEST("alchemy.splash_falloff", [](Context& ctx) -> ll::coro::CoroTask<> {
    // ThrownPotion.applySplash: distance to the feet, < 4 blocks, 1 - d / 4; 1 for the entity hit.
    Vec3 impact{0.f, 1.62f, 0.f};
    int  a = 0, b = 0, c = 0, d = 0;
    auto hits = splashHits(
        impact,
        {{reinterpret_cast<Actor*>(&a), Vec3{0.f, 0.f, 0.f}},
         {reinterpret_cast<Actor*>(&b), Vec3{2.f, 1.62f, 0.f}},
         {reinterpret_cast<Actor*>(&c), Vec3{0.f, 1.62f, 3.9f}},
         {reinterpret_cast<Actor*>(&d), Vec3{4.1f, 1.62f, 0.f}}},
        reinterpret_cast<Actor*>(&a)
    );
    ctx.require(hits.size() == 3u, "three within 4 blocks (got " + std::to_string(hits.size()) + ")");
    ctx.check(std::abs(hits[0].scale - 1.f) < 1e-4f, "entity hit: full strength");
    ctx.check(std::abs(hits[1].scale - 0.5f) < 1e-4f, "2 blocks: half");
    ctx.check(std::abs(hits[2].scale - 0.025f) < 1e-3f, "3.9 blocks: 2.5%");

    // In the world: a bystander 2 blocks away gets a shorter effect than the wearer.
    auto& p = ctx.player();
    co_await readyPlayer(ctx, p);
    auto& other = ctx.spawnPlayer("abp_splash_bystander");
    auto  feet  = p.getFeetPos();
    other.teleport(Vec3{feet.x + 2.f, feet.y, feet.z}, p.getDimensionId(), Vec2{});
    co_await ctx.ticks(5);
    other.removeAllEffects();
    auto impactAt = splashImpact(p);
    ctx.check(std::abs(impactAt.y - p.getEyePos().y) < 1e-3f, "potion breaks at eye height");
    auto splash = data(kSplashPotion, 1, kFireResistance);
    auto r      = rig(ctx, false, splash);
    auto s      = settingsOf(r.ctx());
    s.filters[0].condition = Condition::Always;
    writeSettings(r.ctx(), s);
    r.bp->inventory().insert(splash, kInternal);
    ctx.require(checkNow(r.ctx(&p), {&p}), "splash thrown");
    auto const* mine   = p.getEffect(static_cast<uint>(effect::FireResistance));
    auto const* theirs = other.getEffect(static_cast<uint>(effect::FireResistance));
    ctx.require(mine && mine->mId != 0, "wearer affected");
    ctx.require(theirs && theirs->mId != 0, "bystander 2 blocks away affected");
    int dm = mine->mDuration->mValue, dt = theirs->mDuration->mValue;
    ctx.info("splash durations: wearer " + std::to_string(dm) + ", bystander " + std::to_string(dt));
    ctx.check(dt < dm && dt > dm / 4, "bystander gets a shorter effect (fall-off)");
    p.removeAllEffects();
    other.removeAllEffects();
});

} // namespace
} // namespace abp::upgrade::alchemy
