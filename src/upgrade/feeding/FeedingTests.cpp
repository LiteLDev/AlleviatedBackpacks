// Selftests of the Feeding upgrade: `/backpack selftest "feeding.*"`.

#include <cmath>

#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/PlayerUISlot.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/GameType.h"

#include "game/ItemConv.h"
#include "game/Storage.h"
#include "mod/Config.h"
#include "net/NativeScreens.h"
#include "selftest/SelfTest.h"
#include "ui/menu/BackpackMenu.h"
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

core::ItemStackData data(std::string_view id, int count) { return game::ItemConv::toData(game::ItemConv::make(id, count)); }

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
Player& hungryPlayer(Context& ctx, float food) {
    auto& p = ctx.player();
    p.setPlayerGameType(GameType::Survival);
    setHealth(p, static_cast<float>(p.getMaxHealth()));
    setHunger(p, food);
    ctx.require(eaterState(p).foodLevel == static_cast<int>(food), "food level set to " + std::to_string(static_cast<int>(food)));
    return p;
}

/// One feeding attempt (no cooldown) of the upgrade in `slot` for `p`.
bool feed(std::shared_ptr<game::Backpack> const& bp, int slot, Player& p) {
    auto* type = upgradeFor(bp->data().upgrades.at(static_cast<std::size_t>(slot)).item);
    return feedPlayerAndGetHungry(p, UpgradeContext(bp, slot, &p), type && type->info().id == kAdvancedId);
}

void setFilter(std::shared_ptr<game::Backpack> const& bp, int slot, core::Filter const& f) {
    writeSetting(UpgradeContext(bp, slot), kFilterKey, f.toJson());
}

/// The native tab data / actions registered for `group` (what the client tab gets and sends).
nlohmann::json tabData(Player& p, UpgradeContext const& ctx, std::string_view group) {
    auto out = nlohmann::json::object();
    for (auto const& e : net::TabDataRegistry::entries()) {
        if (e.upgrade == group && e.data) out.update(e.data(p, ctx));
    }
    return out;
}
bool tabAction(Player& p, UpgradeContext const& ctx, std::string_view group, nlohmann::json const& data) {
    for (auto const& h : net::TabActionRegistry::entries()) {
        if (h.upgrade == group && h.handle && h.handle(p, ctx, data)) return true;
    }
    return false;
}
void setCursor(Player& p, ItemStack const& item) { p.setPlayerUIItem(PlayerUISlot::CursorSelected, item, false); }

ABP_REGISTER_SELFTEST("feeding.registration_and_config", [](Context& ctx) {
    ctx.check(nutritionOf(data("minecraft:cooked_beef", 1)).value_or(-1) == 8, "steak nutrition 8");
    ctx.check(nutritionOf(data("minecraft:bread", 1)).value_or(-1) == 5, "bread nutrition 5");
    ctx.check(!nutritionOf(data("minecraft:stone", 1)).has_value(), "stone is not food");
    ctx.check(!game::ItemConv::make("alleviatedbackpacks:feeding_upgrade").isNull(), "feeding upgrade item exists");
    ctx.check(!game::ItemConv::make("alleviatedbackpacks:advanced_feeding_upgrade").isNull(), "advanced item exists");
    auto const& c = config::get<FeedingConfig>();
    ctx.check(c.feedingUpgrade.filterSlots == 9 && c.feedingUpgrade.slotsInRow == 3, "basic config = SB");
    ctx.check(c.advancedFeedingUpgrade.filterSlots == 16 && c.advancedFeedingUpgrade.slotsInRow == 4, "advanced config = SB");
    auto bp = ctx.createBackpack(Tier::Iron);
    ctx.installUpgrade(bp, "feeding");
    ctx.check(canInstall(bp, 1, data("alleviatedbackpacks:advanced_feeding_upgrade", 1), nullptr), "SB: no feeding limit");
    auto o = optionsOf(UpgradeContext(bp, 0));
    ctx.check(o.level == HungerLevel::Half && o.feedImmediatelyWhenHurt, "defaults HALF + feed when hurt");
    ctx.check(filterOf(UpgradeContext(bp, 0), false).mode == core::FilterMode::Deny, "default block list");
});

ABP_REGISTER_SELFTEST("feeding.half_hunger_default", [](Context& ctx) {
    auto  bp   = ctx.createBackpack(Tier::Leather);
    int   slot = ctx.installUpgrade(bp, "feeding");
    auto& p    = hungryPlayer(ctx, 17); // hunger 3
    bp->inventory().set(0, data("minecraft:cooked_beef", 4)); // 8: 8/2 = 4 > 3
    bp->inventory().set(1, data("minecraft:apple", 4));       // 4: 4/2 = 2 <= 3
    bool hungry = feed(bp, slot, p);
    ctx.check(countIn(*bp, "minecraft:cooked_beef") == 4, "steak kept (would waste more than half)");
    ctx.check(countIn(*bp, "minecraft:apple") == 3, "one apple eaten");
    ctx.check(eaterState(p).foodLevel == 20 && !hungry, "food 17 + 4 (capped), not hungry any more");
    setHunger(p, 16); // hunger 4: steak 8/2 = 4 fits, first slot wins
    feed(bp, slot, p);
    ctx.check(countIn(*bp, "minecraft:cooked_beef") == 3, "steak eaten at hunger 4");
    setHunger(p, 4); // hunger 16: fed and still hungry
    ctx.check(feed(bp, slot, p), "fed and still hungry -> fast cooldown");
});

ABP_REGISTER_SELFTEST("feeding.not_when_full_and_hurt", [](Context& ctx) {
    auto  bp   = ctx.createBackpack(Tier::Leather);
    int   slot = ctx.installUpgrade(bp, "feeding");
    auto& p    = hungryPlayer(ctx, 20);
    bp->inventory().set(0, data("minecraft:cooked_beef", 3));
    setHealth(p, 2); // hurt, but not hungry at all
    feed(bp, slot, p);
    ctx.check(countIn(*bp, "minecraft:cooked_beef") == 3, "nothing eaten at full food");
    setHunger(p, 19); // hunger 1, hurt: SC feeds immediately when hurt (default)
    feed(bp, slot, p);
    ctx.check(countIn(*bp, "minecraft:cooked_beef") == 2, "hurt: fed despite wasting nutrition");
    setHunger(p, 19);
    writeSetting(UpgradeContext(bp, slot), kWhenHurtKey, false);
    feed(bp, slot, p);
    ctx.check(countIn(*bp, "minecraft:cooked_beef") == 2, "do not consider health: not fed");
    setHealth(p, static_cast<float>(p.getMaxHealth()));
});

ABP_REGISTER_SELFTEST("feeding.no_harmful_exclusions", [](Context& ctx) {
    auto  bp   = ctx.createBackpack(Tier::Leather);
    int   slot = ctx.installUpgrade(bp, "feeding");
    auto& p    = hungryPlayer(ctx, 0);
    bp->inventory().set(0, data("minecraft:stone", 10));
    bp->inventory().set(1, data("minecraft:rotten_flesh", 2));
    feed(bp, slot, p);
    ctx.check(countIn(*bp, "minecraft:stone") == 10, "non-food untouched");
    ctx.check(countIn(*bp, "minecraft:rotten_flesh") == 1, "SB eats rotten flesh too (no exclusions)");
    p.removeAllEffects();
});

ABP_REGISTER_SELFTEST("feeding.filters", [](Context& ctx) {
    auto  bp   = ctx.createBackpack(Tier::Leather);
    int   slot = ctx.installUpgrade(bp, "feeding");
    auto& p    = hungryPlayer(ctx, 0);
    bp->inventory().set(0, data("minecraft:bread", 5));
    bp->inventory().set(1, data("minecraft:baked_potato", 5));
    core::Filter f(9);
    f.mode = core::FilterMode::Deny;
    f.setGhost(0, data("minecraft:bread", 1));
    setFilter(bp, slot, f);
    feed(bp, slot, p);
    ctx.check(countIn(*bp, "minecraft:bread") == 5 && countIn(*bp, "minecraft:baked_potato") == 4, "block list skips bread");
    f.mode = core::FilterMode::Allow;
    setFilter(bp, slot, f);
    setHunger(p, 0);
    feed(bp, slot, p);
    ctx.check(countIn(*bp, "minecraft:bread") == 4, "allow list eats bread");
    setFilter(bp, slot, [] {
        core::Filter e(9);
        e.mode = core::FilterMode::Allow;
        return e;
    }());
    setHunger(p, 0);
    feed(bp, slot, p);
    ctx.check(countIn(*bp, "minecraft:bread") == 4 && countIn(*bp, "minecraft:baked_potato") == 4, "SC: empty allow list feeds nothing");
});

ABP_REGISTER_SELFTEST("feeding.returns_bowl", [](Context& ctx) {
    auto  bp   = ctx.createBackpack(Tier::Leather);
    int   slot = ctx.installUpgrade(bp, "feeding");
    auto& p    = hungryPlayer(ctx, 0);
    bp->inventory().set(3, data("minecraft:mushroom_stew", 1));
    int bowlsBefore = countOnPlayer(p, "minecraft:bowl");
    feed(bp, slot, p);
    ctx.check(countIn(*bp, "minecraft:mushroom_stew") == 0, "stew eaten");
    ctx.check(eaterState(p).foodLevel == 6, "stew gave 6 (got " + std::to_string(eaterState(p).foodLevel) + ")");
    ctx.check(countIn(*bp, "minecraft:bowl") == 1, "bowl returned into the backpack");
    ctx.check(countOnPlayer(p, "minecraft:bowl") == bowlsBefore, "no extra bowl on the player");
});

ABP_REGISTER_SELFTEST("feeding.golden_apple_effects", [](Context& ctx) {
    auto  bp   = ctx.createBackpack(Tier::Leather);
    int   slot = ctx.installUpgrade(bp, "feeding");
    auto& p    = hungryPlayer(ctx, 10);
    p.removeAllEffects();
    bp->inventory().set(0, data("minecraft:golden_apple", 2));
    feed(bp, slot, p);
    ctx.check(countIn(*bp, "minecraft:golden_apple") == 1, "golden apple eaten");
    ctx.check(p.getEffect(kRegeneration) != nullptr && p.getEffect(kAbsorption) != nullptr, "effects applied");
    p.removeAllEffects();
});

ABP_REGISTER_SELFTEST("feeding.advanced_levels", [](Context& ctx) {
    auto  bp   = ctx.createBackpack(Tier::Leather);
    int   slot = ctx.installUpgrade(bp, "advanced_feeding");
    auto& p    = hungryPlayer(ctx, 17); // hunger 3
    bp->inventory().set(0, data("minecraft:cooked_beef", 10));
    UpgradeContext uc(bp, slot);
    ctx.check(filterOf(uc, true).ghosts.size() == 16, "16 filter slots");
    writeSetting(uc, kHungerLevelKey, "full");
    feed(bp, slot, p);
    ctx.check(countIn(*bp, "minecraft:cooked_beef") == 10, "FULL: no waste allowed");
    writeSetting(uc, kHungerLevelKey, "any");
    feed(bp, slot, p);
    ctx.check(countIn(*bp, "minecraft:cooked_beef") == 9, "ANY: fed when a tiny bit hungry");
});

ABP_REGISTER_SELFTEST("feeding.cooldown", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  bp   = ctx.createBackpack(Tier::Leather);
    int   slot = ctx.installUpgrade(bp, "feeding");
    auto& p    = hungryPlayer(ctx, 0);
    bp->inventory().set(0, data("minecraft:bread", 10));
    auto* type = byId(kBasicId);
    TickContext t{UpgradeContext(bp, slot, &p), p, {}, 10};
    type->onTick(t);
    ctx.check(countIn(*bp, "minecraft:bread") == 9, "first tick feeds");
    type->onTick(t);
    ctx.check(countIn(*bp, "minecraft:bread") == 9, "still hungry: waits 10 ticks");
    co_await ctx.ticks(11);
    type->onTick(t);
    ctx.check(countIn(*bp, "minecraft:bread") == 8, "fed again after 10 ticks");
});

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
});

ABP_REGISTER_SELFTEST("feeding.placed_range", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  bp   = ctx.createBackpack(Tier::Leather);
    int   slot = ctx.installUpgrade(bp, "feeding");
    auto& p    = hungryPlayer(ctx, 0);
    bp->inventory().set(0, data("minecraft:bread", 5));
    auto  feet = p.getFeetPos();
    auto& reg  = p.getDimensionBlockSource();
    int   x = static_cast<int>(std::floor(feet.x)), y = static_cast<int>(std::floor(feet.y)), z = static_cast<int>(std::floor(feet.z));
    PlacedTickContext far{UpgradeContext(bp, slot), reg, static_cast<int>(p.getDimensionId()), x + 6, y, z, 2, 2};
    byId(kBasicId)->onPlacedTick(far);
    ctx.check(countIn(*bp, "minecraft:bread") == 5, "player 6 blocks away is not fed");
    co_await ctx.ticks(101);
    setHunger(p, 0); // a backpack carried from an earlier test may have fed the player meanwhile
    PlacedTickContext near{UpgradeContext(bp, slot), reg, static_cast<int>(p.getDimensionId()), x + 2, y, z, 2, 2};
    byId(kBasicId)->onPlacedTick(near);
    ctx.check(countIn(*bp, "minecraft:bread") == 4, "player within 3 blocks is fed");
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
    ctx.require(mm.open(p, menu), "open");
    co_await ctx.ticks(3);
    mm.forceShown(p);
    UpgradeContext uc(bp, adv);
    int control = menu->size() - 9;
    (void)mm.simulate(p, {Action::Take, {Where::Menu, control + 5}, {Where::Cursor, 0}, 1});
    ctx.check(optionsOf(uc).level == HungerLevel::Full, "HALF -> FULL");
    (void)mm.simulate(p, {Action::Take, {Where::Menu, control + 5}, {Where::Cursor, 0}, 1});
    ctx.check(optionsOf(uc).level == HungerLevel::Any, "FULL -> ANY");
    (void)mm.simulate(p, {Action::Take, {Where::Menu, control + 6}, {Where::Cursor, 0}, 1});
    ctx.check(!optionsOf(uc).feedImmediatelyWhenHurt, "hurt toggle off");
    mm.close(p);
});

ABP_REGISTER_SELFTEST("feeding.native_tab", [](Context& ctx) {
    auto& p  = ctx.player();
    auto  bp = ctx.createBackpack(Tier::Iron);
    int   b  = ctx.installUpgrade(bp, "feeding");
    int   a  = ctx.installUpgrade(bp, "advanced_feeding");
    UpgradeContext basic(bp, b, &p), adv(bp, a, &p);
    auto           t = tabData(p, adv, "feeding");
    ctx.check(t.value("title", "") == "Adv. Feeding" && t.value("tooltip", "") == "Advanced Feeding Settings", "SB tab texts");
    ctx.check(tabData(p, basic, "feeding").value("title", "") == "Feeding", "basic title");
    ctx.check(t.value("hungerLevel", -1) == 1 && t.value("whenHurt", false), "HALF + hurt by default");
    ctx.check(t["filter"].value("type", "") == "allowList" && t["filter"].value("slots", 0) == 16, "advanced allow/block filter, 16 slots");
    ctx.check(tabAction(p, adv, "feeding", {{"op", "hungerLevel"}}) && optionsOf(adv).level == HungerLevel::Full, "HALF -> FULL");
    ctx.check(tabAction(p, adv, "feeding", {{"op", "whenHurt"}}) && !optionsOf(adv).feedImmediatelyWhenHurt, "hurt off");
    ctx.check(!tabAction(p, basic, "feeding", {{"op", "hungerLevel"}}), "the basic tab has no buttons");
    setCursor(p, game::ItemConv::make("minecraft:stone", 1));
    tabAction(p, basic, "feeding", {{"filter", "filter"}, {"op", "slot"}, {"index", 0}});
    ctx.check(filterOf(basic, false).ghosts[0].empty(), "non-food refused in the filter");
    setCursor(p, game::ItemConv::make("minecraft:bread", 1));
    tabAction(p, basic, "feeding", {{"filter", "filter"}, {"op", "slot"}, {"index", 0}});
    setCursor(p, ItemStack::EMPTY_ITEM());
    ctx.check(filterOf(basic, false).ghosts[0].id == "minecraft:bread", "food accepted in the filter");
});

} // namespace
} // namespace abp::upgrade::feeding
