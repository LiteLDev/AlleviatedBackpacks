// Selftests of the Feeding upgrade: `/backpack selftest "feeding.*"`.

#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/GameType.h"

#include "game/ItemConv.h"
#include "game/Storage.h"
#include "selftest/SelfTest.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/feeding/Feeding.h"

namespace abp::upgrade::feeding {
namespace {

using selftest::Context;
using core::Tier;
using Where  = ui::MenuManager::SlotAddress::Where;
using Action = ui::MenuManager::ActionType;

constexpr unsigned kRegeneration = 10;
constexpr unsigned kAbsorption   = 22;

core::ItemStackData data(std::string_view id, int count) {
    return game::ItemConv::toData(game::ItemConv::make(id, count));
}

int countIn(game::Backpack& bp, std::string_view id) {
    return static_cast<int>(bp.inventory().count([&](core::ItemStackData const& s) { return s.id == id; }));
}

int countOnPlayer(Player& p, std::string_view id) {
    int n = 0;
    for (int i = 0; i < 36; ++i) {
        auto const& it = p.getInventory().getItem(i);
        if (!it.isNull() && it.getTypeName() == id) n += it.mCount;
    }
    return n;
}

/// A survival player with the given food level and full health.
Player& hungryPlayer(Context& ctx, float hunger) {
    auto& p = ctx.player();
    p.setPlayerGameType(GameType::Survival);
    setHealth(p, static_cast<float>(p.getMaxHealth()));
    setHunger(p, hunger);
    ctx.require(eaterState(p).hunger == static_cast<int>(hunger), "hunger set to " + std::to_string(static_cast<int>(hunger)));
    return p;
}

/// Runs one upgrade tick of the feeding upgrade in `slot` for `p` (the backpack need not be carried).
void tick(std::shared_ptr<game::Backpack> const& bp, int slot, Player& p) {
    auto* type = upgradeFor(bp->data().upgrades.at(static_cast<std::size_t>(slot)).item);
    if (!type) return;
    type->onTick(TickContext{UpgradeContext(bp, slot, &p), p, {}, 10});
}

void setFilter(std::shared_ptr<game::Backpack> const& bp, int slot, core::Filter const& f) {
    UpgradeContext ctx(bp, slot);
    if (!ctx.settings().is_object()) ctx.settings() = nlohmann::json::object();
    ctx.settings()[kFilterKey] = f.toJson();
    ctx.saveSettings(true);
}

// ---- basic -------------------------------------------------------------------------------------

ABP_REGISTER_SELFTEST("feeding.food_lookup", [](Context& ctx) {
    ctx.check(nutritionOf(data("minecraft:cooked_beef", 1)).value_or(-1) == 8, "steak nutrition 8");
    ctx.check(nutritionOf(data("minecraft:apple", 1)).value_or(-1) == 4, "apple nutrition 4");
    ctx.check(nutritionOf(data("minecraft:bread", 1)).value_or(-1) == 5, "bread nutrition 5");
    ctx.check(!nutritionOf(data("minecraft:stone", 1)).has_value(), "stone is not food");
    ctx.check(nutritionOf(data("minecraft:mushroom_stew", 1)).has_value(), "mushroom stew is food");
    ctx.check(!game::ItemConv::make("alleviatedbackpacks:feeding_upgrade").isNull(), "feeding upgrade item exists");
    ctx.check(!game::ItemConv::make("alleviatedbackpacks:advanced_feeding_upgrade").isNull(), "advanced item exists");
});

ABP_REGISTER_SELFTEST("feeding.basic_no_waste", [](Context& ctx) {
    auto  bp   = ctx.createBackpack(Tier::Leather);
    int   slot = ctx.installUpgrade(bp, "feeding");
    auto& p    = hungryPlayer(ctx, 17); // missing 3
    bp->inventory().set(0, data("minecraft:cooked_beef", 4));
    bp->inventory().set(1, data("minecraft:apple", 4));
    tick(bp, slot, p);
    ctx.check(countIn(*bp, "minecraft:cooked_beef") == 4 && countIn(*bp, "minecraft:apple") == 4, "nothing eaten at 17/20");
    ctx.check(eaterState(p).hunger == 17, "hunger unchanged");

    setHunger(p, 15); // missing 5: the apple (4) fits, the steak (8) does not
    tick(bp, slot, p);
    ctx.check(countIn(*bp, "minecraft:cooked_beef") == 4, "steak kept");
    ctx.check(countIn(*bp, "minecraft:apple") == 3, "one apple eaten");
    ctx.check(eaterState(p).hunger == 19, "hunger 15 + 4 = " + std::to_string(eaterState(p).hunger));

    setHunger(p, 10); // missing 10: first fitting slot (steak) wins
    tick(bp, slot, p);
    ctx.check(countIn(*bp, "minecraft:cooked_beef") == 3, "steak eaten first");
    ctx.check(eaterState(p).hunger == 18, "hunger 10 + 8");
});

ABP_REGISTER_SELFTEST("feeding.not_when_full", [](Context& ctx) {
    auto  bp   = ctx.createBackpack(Tier::Leather);
    int   slot = ctx.installUpgrade(bp, "advanced_feeding");
    auto& p    = hungryPlayer(ctx, 20);
    bp->inventory().set(0, data("minecraft:golden_apple", 3));
    setHealth(p, 2); // hurt, but not hungry
    tick(bp, slot, p);
    ctx.check(countIn(*bp, "minecraft:golden_apple") == 3, "nothing eaten when food is full");
    p.setPlayerGameType(GameType::Creative);
    setHunger(p, 2);
    tick(bp, slot, p);
    ctx.check(countIn(*bp, "minecraft:golden_apple") == 3, "creative players are not fed");
    p.setPlayerGameType(GameType::Survival);
});

ABP_REGISTER_SELFTEST("feeding.skips_harmful_and_nonfood", [](Context& ctx) {
    auto  bp   = ctx.createBackpack(Tier::Leather);
    int   slot = ctx.installUpgrade(bp, "feeding");
    auto& p    = hungryPlayer(ctx, 0);
    char const* harmful[] = {"minecraft:rotten_flesh", "minecraft:spider_eye", "minecraft:pufferfish", "minecraft:poisonous_potato", "minecraft:chicken"};
    int i = 0;
    for (auto id : harmful) bp->inventory().set(i++, data(id, 2));
    bp->inventory().set(i++, data("minecraft:stone", 2));
    tick(bp, slot, p);
    for (auto id : harmful) ctx.check(countIn(*bp, id) == 2, std::string(id) + " not eaten");
    ctx.check(eaterState(p).hunger == 0, "still starving");
    bp->inventory().set(i, data("minecraft:bread", 1));
    tick(bp, slot, p);
    ctx.check(countIn(*bp, "minecraft:bread") == 0, "safe food after them is eaten");
    ctx.check(eaterState(p).hunger == 5, "bread gave 5");
});

ABP_REGISTER_SELFTEST("feeding.whitelist_harmful", [](Context& ctx) {
    auto  bp   = ctx.createBackpack(Tier::Leather);
    int   slot = ctx.installUpgrade(bp, "feeding");
    auto& p    = hungryPlayer(ctx, 0);
    bp->inventory().set(0, data("minecraft:rotten_flesh", 2));
    core::Filter f(kBasicFilterSlots);
    f.setGhost(0, data("minecraft:rotten_flesh", 1));
    setFilter(bp, slot, f);
    tick(bp, slot, p);
    ctx.check(countIn(*bp, "minecraft:rotten_flesh") == 1, "allow-listed rotten flesh eaten");
    ctx.check(eaterState(p).hunger == 4, "rotten flesh gave 4");
});

ABP_REGISTER_SELFTEST("feeding.filters", [](Context& ctx) {
    auto  bp   = ctx.createBackpack(Tier::Leather);
    int   slot = ctx.installUpgrade(bp, "feeding");
    auto& p    = hungryPlayer(ctx, 0);
    bp->inventory().set(0, data("minecraft:apple", 2));
    bp->inventory().set(1, data("minecraft:bread", 2));
    core::Filter f(kBasicFilterSlots);
    f.mode = core::FilterMode::Deny;
    f.setGhost(0, data("minecraft:apple", 1));
    setFilter(bp, slot, f);
    tick(bp, slot, p);
    ctx.check(countIn(*bp, "minecraft:apple") == 2, "denied apple kept");
    ctx.check(countIn(*bp, "minecraft:bread") == 1, "bread eaten");

    f.mode = core::FilterMode::Allow; // only apples
    setFilter(bp, slot, f);
    tick(bp, slot, p);
    ctx.check(countIn(*bp, "minecraft:apple") == 1, "allowed apple eaten");
    ctx.check(countIn(*bp, "minecraft:bread") == 1, "bread not on allow list");
});

// ---- vanilla eating behaviour --------------------------------------------------------------------

ABP_REGISTER_SELFTEST("feeding.returns_bowl", [](Context& ctx) {
    auto  bp   = ctx.createBackpack(Tier::Leather);
    int   slot = ctx.installUpgrade(bp, "feeding");
    auto& p    = hungryPlayer(ctx, 0);
    bp->inventory().set(3, data("minecraft:mushroom_stew", 1));
    int bowlsBefore = countOnPlayer(p, "minecraft:bowl");
    tick(bp, slot, p);
    ctx.check(countIn(*bp, "minecraft:mushroom_stew") == 0, "stew eaten");
    ctx.check(eaterState(p).hunger == 6, "stew gave 6 (got " + std::to_string(eaterState(p).hunger) + ")");
    ctx.check(countIn(*bp, "minecraft:bowl") == 1, "bowl returned into the backpack");
    ctx.check(countOnPlayer(p, "minecraft:bowl") == bowlsBefore, "no extra bowl on the player");

    bp->inventory().set(4, data("minecraft:honey_bottle", 1));
    setHunger(p, 0);
    tick(bp, slot, p);
    ctx.check(countIn(*bp, "minecraft:honey_bottle") == 0, "honey eaten");
    ctx.check(countIn(*bp, "minecraft:glass_bottle") == 1, "glass bottle returned");
});

/// Refuses bowls in the backpack so the feeding upgrade has to hand them to the player.
class NoBowlsUpgrade : public IUpgrade {
public:
    UpgradeInfo mInfo{
        .id           = "selftest_no_bowls",
        .itemId       = "minecraft:structure_void",
        .nameKey      = "abp.selftest.upgrade",
        .nameFallback = "Selftest Upgrade",
    };
    [[nodiscard]] UpgradeInfo const& info() const override { return mInfo; }
    void attach(UpgradeContext const& ctx, core::BackpackInventory& inv) override {
        inv.addInsertFilter(ctx.hookOwner(), 0, [](core::InsertQuery const& q) {
            return q.stack.id == "minecraft:bowl" ? core::InsertVerdict::Deny : core::InsertVerdict::Allow;
        });
    }
};

ABP_REGISTER_SELFTEST("feeding.bowl_to_player", [](Context& ctx) {
    ScopedUpgrade scoped(std::make_shared<NoBowlsUpgrade>());
    auto          bp   = ctx.createBackpack(Tier::Iron);
    int           slot = ctx.installUpgrade(bp, "feeding");
    ctx.installUpgrade(bp, "selftest_no_bowls");
    auto& p = hungryPlayer(ctx, 0);
    bp->inventory().set(0, data("minecraft:beetroot_soup", 1));
    int before = countOnPlayer(p, "minecraft:bowl");
    tick(bp, slot, p);
    ctx.check(countIn(*bp, "minecraft:beetroot_soup") == 0, "soup eaten");
    ctx.check(countIn(*bp, "minecraft:bowl") == 0, "backpack refused the bowl");
    ctx.check(countOnPlayer(p, "minecraft:bowl") == before + 1, "bowl went to the player");
});

ABP_REGISTER_SELFTEST("feeding.golden_apple_effects", [](Context& ctx) {
    auto  bp   = ctx.createBackpack(Tier::Leather);
    int   slot = ctx.installUpgrade(bp, "feeding");
    auto& p    = hungryPlayer(ctx, 10);
    p.removeAllEffects();
    bp->inventory().set(0, data("minecraft:golden_apple", 2));
    tick(bp, slot, p);
    ctx.check(countIn(*bp, "minecraft:golden_apple") == 1, "golden apple eaten");
    ctx.check(eaterState(p).hunger == 14, "golden apple gave 4");
    ctx.check(p.getEffect(kRegeneration) != nullptr, "regeneration applied");
    ctx.check(p.getEffect(kAbsorption) != nullptr, "absorption applied");
    p.removeAllEffects();
});

// ---- advanced ------------------------------------------------------------------------------------

ABP_REGISTER_SELFTEST("feeding.advanced_modes", [](Context& ctx) {
    auto  bp   = ctx.createBackpack(Tier::Leather);
    int   slot = ctx.installUpgrade(bp, "advanced_feeding");
    auto& p    = hungryPlayer(ctx, 17); // missing 3
    bp->inventory().set(0, data("minecraft:cooked_beef", 10));
    UpgradeContext uc(bp, slot);
    auto           setMode = [&](HungerMode m, bool hurt) {
        if (!uc.settings().is_object()) uc.settings() = nlohmann::json::object();
        uc.settings()[kHungerModeKey]  = static_cast<int>(m);
        uc.settings()[kEatWhenHurtKey] = hurt;
        uc.saveSettings();
    };
    // Defaults: half, eat when hurt.
    auto o = optionsOf(uc, true);
    ctx.check(o.mode == HungerMode::Half && o.eatWhenHurt, "advanced defaults: half + hurt");
    tick(bp, slot, p);
    ctx.check(countIn(*bp, "minecraft:cooked_beef") == 10, "half: missing 3 < 4 -> not eaten");
    setHunger(p, 16);
    tick(bp, slot, p);
    ctx.check(countIn(*bp, "minecraft:cooked_beef") == 9, "half: missing 4 -> eaten");

    setMode(HungerMode::Full, false);
    setHunger(p, 13);
    tick(bp, slot, p);
    ctx.check(countIn(*bp, "minecraft:cooked_beef") == 9, "full: missing 7 -> not eaten");
    setHunger(p, 12);
    tick(bp, slot, p);
    ctx.check(countIn(*bp, "minecraft:cooked_beef") == 8, "full: missing 8 -> eaten");

    setMode(HungerMode::Always, false);
    setHunger(p, 19);
    tick(bp, slot, p);
    ctx.check(countIn(*bp, "minecraft:cooked_beef") == 7, "always: missing 1 -> eaten");
    ctx.check(eaterState(p).hunger == 20, "food capped at 20");
});

ABP_REGISTER_SELFTEST("feeding.advanced_hurt", [](Context& ctx) {
    auto  bp   = ctx.createBackpack(Tier::Leather);
    int   slot = ctx.installUpgrade(bp, "advanced_feeding");
    auto& p    = hungryPlayer(ctx, 19);
    UpgradeContext uc(bp, slot);
    uc.settings()                  = nlohmann::json::object();
    uc.settings()[kHungerModeKey]  = static_cast<int>(HungerMode::Full);
    uc.settings()[kEatWhenHurtKey] = true;
    uc.saveSettings();
    bp->inventory().set(0, data("minecraft:cooked_beef", 2));
    setHealth(p, 15);
    tick(bp, slot, p);
    ctx.check(countIn(*bp, "minecraft:cooked_beef") == 2, "healthy enough: no waste");
    setHealth(p, 6);
    tick(bp, slot, p);
    ctx.check(countIn(*bp, "minecraft:cooked_beef") == 1, "below half health: eaten despite waste");
    uc.settings()[kEatWhenHurtKey] = false;
    uc.saveSettings();
    setHunger(p, 19);
    setHealth(p, 6);
    tick(bp, slot, p);
    ctx.check(countIn(*bp, "minecraft:cooked_beef") == 1, "option off: not eaten");
});

ABP_REGISTER_SELFTEST("feeding.advanced_filter_tags", [](Context& ctx) {
    auto  bp   = ctx.createBackpack(Tier::Leather);
    int   slot = ctx.installUpgrade(bp, "advanced_feeding");
    auto& p    = hungryPlayer(ctx, 0);
    bp->inventory().set(0, data("minecraft:apple", 1));
    bp->inventory().set(1, data("minecraft:cooked_cod", 1));
    core::Filter f(kAdvancedFilterSlots);
    f.matchBy = core::MatchBy::Tag;
    f.tags    = {"minecraft:is_fish"};
    setFilter(bp, slot, f);
    ctx.check(filterOf(UpgradeContext(bp, slot), true).ghosts.size() == kAdvancedFilterSlots, "16 filter slots");
    tick(bp, slot, p);
    ctx.check(countIn(*bp, "minecraft:apple") == 1, "apple not tagged as fish");
    ctx.check(countIn(*bp, "minecraft:cooked_cod") == 0, "cod (is_fish) eaten");
});

// ---- integration -------------------------------------------------------------------------------

ABP_REGISTER_SELFTEST("feeding.carried_tick", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  bp   = ctx.createBackpack(Tier::Leather);
    int   slot = ctx.installUpgrade(bp, "feeding");
    auto& p    = hungryPlayer(ctx, 2);
    bp->inventory().set(0, data("minecraft:bread", 3));
    upgrade::setEnabled(bp, slot, false);
    ctx.require(ctx.give(p, ctx.backpackItem(*bp), 5) == 5, "give backpack");
    co_await ctx.ticks(25);
    ctx.check(countIn(*bp, "minecraft:bread") == 3, "disabled upgrade does not feed");
    upgrade::setEnabled(bp, slot, true);
    setHunger(ctx.player(), 2);
    co_await ctx.ticks(25);
    ctx.check(countIn(*bp, "minecraft:bread") < 3, "carried backpack fed the player");
    ctx.check(eaterState(ctx.player()).hunger >= 7, "hunger went up");
});

ABP_REGISTER_SELFTEST("feeding.settings_menu", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& mm   = ui::MenuManager::get();
    auto& p    = ctx.player();
    auto  bp   = ctx.createBackpack(Tier::Iron);
    int   adv  = ctx.installUpgrade(bp, "advanced_feeding");
    auto* type = byId("advanced_feeding");
    ctx.require(type && type->info().hasSettings, "advanced feeding has settings");
    auto menu = type->createSettingsMenu(p, UpgradeContext(bp, adv, &p));
    ctx.require(menu != nullptr, "settings menu");
    ctx.check(menu->size() == 27, "advanced settings: single chest (16 ghosts + controls)");
    ctx.require(mm.open(p, menu), "open");
    co_await ctx.ticks(3);
    mm.forceShown(p);
    UpgradeContext uc(bp, adv);
    ctx.check(optionsOf(uc, true).mode == HungerMode::Half, "default half");
    (void)mm.simulate(p, {Action::Take, {Where::Menu, 18 + 5}, {Where::Cursor, 0}, 1});
    ctx.check(optionsOf(uc, true).mode == HungerMode::Full, "click cycles to full");
    (void)mm.simulate(p, {Action::Take, {Where::Menu, 18 + 5}, {Where::Cursor, 0}, 1});
    ctx.check(optionsOf(uc, true).mode == HungerMode::Always, "click cycles to always");
    (void)mm.simulate(p, {Action::Take, {Where::Menu, 18 + 6}, {Where::Cursor, 0}, 1});
    ctx.check(!optionsOf(uc, true).eatWhenHurt, "hurt toggle off");
    // Ghost slot 15 (the 16th) takes a filter item.
    ctx.require(ctx.give(p, game::ItemConv::make("minecraft:apple", 3), 4) == 4, "give apples");
    (void)mm.simulate(p, {Action::Take, {Where::Inventory, 4}, {Where::Cursor, 0}, 3});
    (void)mm.simulate(p, {Action::Place, {Where::Cursor, 0}, {Where::Menu, 15}, 3});
    auto f = filterOf(uc, true);
    ctx.check(f.ghosts.size() == 16 && f.ghosts[15].id == "minecraft:apple", "16th ghost slot set");
    co_await ctx.ticks(2);
    ctx.check(mm.shownItem(p, 18 + 5).getTypeName() == "minecraft:cooked_beef", "hunger button shown");
    mm.close(p);

    // The basic upgrade: 9 ghost slots, only the allow/deny switch.
    int  basicSlot = -1;
    auto removed   = upgrade::remove(bp, adv, nullptr);
    ctx.require(removed.has_value(), "remove advanced");
    basicSlot       = ctx.installUpgrade(bp, "feeding");
    auto basicMenu  = byId("feeding")->createSettingsMenu(p, UpgradeContext(bp, basicSlot, &p));
    ctx.require(basicMenu != nullptr, "basic settings menu");
    ctx.require(mm.open(p, basicMenu), "open basic");
    co_await ctx.ticks(3);
    mm.forceShown(p);
    (void)mm.simulate(p, {Action::Take, {Where::Menu, 18}, {Where::Cursor, 0}, 1});
    ctx.check(filterOf(UpgradeContext(bp, basicSlot), false).mode == core::FilterMode::Deny, "basic allow/deny toggles");
    ctx.check(filterOf(UpgradeContext(bp, basicSlot), false).ghosts.size() == kBasicFilterSlots, "9 basic ghost slots");
    mm.close(p);
    ctx.check(p.getInventory().getItem(4).mCount == 3 || countOnPlayer(p, "minecraft:apple") == 3, "apples returned");
});

ABP_REGISTER_SELFTEST("feeding.exclusive_group", [](Context& ctx) {
    auto bp = ctx.createBackpack(Tier::Iron);
    ctx.installUpgrade(bp, "feeding");
    auto adv = game::ItemConv::toData(game::ItemConv::make("alleviatedbackpacks:advanced_feeding_upgrade"));
    ctx.check(!upgrade::canInstall(bp, 1, adv, nullptr), "basic and advanced feeding don't stack");
});

} // namespace
} // namespace abp::upgrade::feeding
