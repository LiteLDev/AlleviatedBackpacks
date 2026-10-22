// Selftests of the cooking upgrades: `backpack selftest "cooking.*"`.

#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "api/BackpackCapabilities.h"
#include "api/abp/Fluid.h"
#include "feature/gui/Gui.h"
#include "feature/placed/PlacedBackpacks.h"
#include "feature/placed/TestSite.h"
#include "game/ItemConv.h"
#include "selftest/SelfTest.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/cooking/Cooking.h"
#include "upgrade/crafting/Station.h"
#include "upgrade/crafting/StationTestSupport.h"

namespace abp::upgrade::cooking {
namespace {

using selftest::Context;
using core::Tier;
using namespace crafting::test;

/// A backpack with the upgrade `id` installed; returns its context.
UpgradeContext withUpgrade(Context& ctx, std::shared_ptr<game::Backpack>& bp, std::string const& id, Tier tier = Tier::Leather) {
    bp       = ctx.createBackpack(tier);
    int slot = ctx.installUpgrade(bp, id);
    return UpgradeContext(bp, slot);
}

std::string str(core::ItemStackData const& d) { return d.empty() ? "empty" : d.id + " x" + std::to_string(d.count); }

void load(UpgradeContext const& u, core::ItemStackData input, core::ItemStackData fuel) {
    CookState st;
    st.input = std::move(input);
    st.fuel  = std::move(fuel);
    CookingUpgrade::setState(u, st);
}

ABP_REGISTER_SELFTEST("cooking.recipes_and_fuel", [](Context& ctx) {
    auto out = recipeResult(Kind::Smelting, data("minecraft:raw_iron", 1));
    ctx.check(out && out->id == "minecraft:iron_ingot", "raw iron smelts to an iron ingot");
    ctx.check(recipeResult(Kind::Smoking, data("minecraft:beef", 1)).has_value(), "smoker cooks beef");
    ctx.check(!recipeResult(Kind::Smoking, data("minecraft:raw_iron", 1)).has_value(), "smoker doesn't smelt ores");
    ctx.check(recipeResult(Kind::Blasting, data("minecraft:raw_iron", 1)).has_value(), "blast furnace smelts ores");
    ctx.check(!recipeResult(Kind::Blasting, data("minecraft:beef", 1)).has_value(), "blast furnace doesn't cook food");
    ctx.check(furnaceBurnTicks(data("minecraft:coal", 1)) == 1600, "coal burns 1600 ticks, got " + std::to_string(furnaceBurnTicks(data("minecraft:coal", 1))));
    ctx.check(furnaceBurnTicks(data("minecraft:dirt", 1)) == 0, "dirt doesn't burn");
    ctx.check(smeltingExperience(data("minecraft:iron_ingot", 1)) > 0.f, "iron ingots give experience");
    ctx.check(recipeTime(Kind::Smelting) == 200 && recipeTime(Kind::Smoking) == 100 && recipeTime(Kind::Blasting) == 100, "SC recipe times");
    ctx.check(burnTimeModifier(Kind::Smelting) == 1.f && burnTimeModifier(Kind::Blasting) == 0.5f, "SC burn time modifiers");
});

ABP_REGISTER_SELFTEST("cooking.info", [](Context& ctx) {
    for (auto const* id : {"smelting", "smoking", "blasting", "auto_smelting", "auto_smoking", "auto_blasting"}) {
        auto* u = upgrade::byId(id);
        ctx.require(u != nullptr, std::string(id) + " registered");
        ctx.check(u->info().groupOrId() == kGroup && u->info().maxPerBackpack == upgrade::kUnlimitedPerBackpack, std::string(id) + ": SB furnace upgrade group, limited only by maxUpgradesPerStorage");
        ctx.check(u->info().toggleable && u->info().hasSettings, std::string(id) + ": switch and tab");
    }
    ctx.check(upgrade::byId("auto_smelting")->info().nameFallback == "Auto-smelting Upgrade", "SB item name");
});

ABP_REGISTER_SELFTEST("cooking.one_per_backpack", [](Context& ctx) {
    std::shared_ptr<game::Backpack> bp;
    (void)withUpgrade(ctx, bp, "smelting", Tier::Gold);
    std::string reason;
    auto        item = game::ItemConv::toData(game::ItemConv::make("alleviatedbackpacks:auto_blasting_upgrade"));
    ctx.check(!upgrade::canInstall(bp, 1, item, nullptr, &reason), "a second furnace upgrade is refused");
});

ABP_REGISTER_SELFTEST("cooking.smelting_runs", [](Context& ctx) {
    std::shared_ptr<game::Backpack> bp;
    auto                            u = withUpgrade(ctx, bp, "smelting");
    auto&                           type = upgradeOf(Kind::Smelting, false);
    load(u, data("minecraft:raw_iron", 3), data("minecraft:coal", 1));
    type.advance(u, 401);
    auto s = CookingUpgrade::state(u);
    ctx.check(s.output.id == "minecraft:iron_ingot" && s.output.count == 2, "2 ingots after 400 ticks, got " + str(s.output));
    ctx.check(s.input.count == 1 && s.fuel.empty(), "input and fuel used");
    ctx.check(s.burning() && s.storedXp > 0, "still burning, experience stored on the upgrade");
});

ABP_REGISTER_SELFTEST("cooking.fast_kinds", [](Context& ctx) {
    std::shared_ptr<game::Backpack> bp;
    auto                            u = withUpgrade(ctx, bp, "blasting");
    load(u, data("minecraft:raw_iron", 10), data("minecraft:coal", 1));
    upgradeOf(Kind::Blasting, false).advance(u, 801);
    auto s = CookingUpgrade::state(u);
    ctx.check(s.output.count == 8, "blast furnace: 8 items per coal in 800 ticks, got " + str(s.output));
    upgradeOf(Kind::Blasting, false).advance(u, 200);
    ctx.check(CookingUpgrade::state(u).output.count == 8, "fuel is spent");
});

ABP_REGISTER_SELFTEST("cooking.lava_and_wet_sponge", [](Context& ctx) {
    std::shared_ptr<game::Backpack> bp;
    auto                            u = withUpgrade(ctx, bp, "smelting");
    load(u, data("minecraft:wet_sponge", 1), data("minecraft:lava_bucket", 1));
    upgradeOf(Kind::Smelting, false).advance(u, 201);
    auto s = CookingUpgrade::state(u);
    ctx.check(s.output.id == "minecraft:sponge", "wet sponge dried, got " + str(s.output));
    ctx.check(s.fuel.id == "minecraft:water_bucket", "SB: the empty bucket caught the water, got " + str(s.fuel));
});

ABP_REGISTER_SELFTEST("cooking.carried_and_disabled", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto&                           player = ctx.player();
    std::shared_ptr<game::Backpack> bp;
    auto                            u = withUpgrade(ctx, bp, "smoking");
    load(u, data("minecraft:beef", 2), data("minecraft:coal", 1));
    ctx.require(ctx.give(player, ctx.backpackItem(*bp), 2) == 2, "give backpack");
    co_await ctx.ticks(140);
    auto s = CookingUpgrade::state(u);
    ctx.check(s.output.id == "minecraft:cooked_beef" && s.output.count == 1, "beef smoked while carried, got " + str(s.output));
    // SC pause: a disabled upgrade keeps its remaining burn time and progress.
    upgrade::setEnabled(bp, u.slot(), false);
    auto paused = CookingUpgrade::state(u);
    co_await ctx.ticks(140);
    auto after = CookingUpgrade::state(u);
    ctx.check(after.sameData(paused), "disabled: nothing changes");
    NativeScreen screen(ctx, player, bp);
    auto         tab = screen.tabData(u.slot());
    ctx.check(!tab.value("burning", true) && !tab.value("cooking", true), "disabled: no flame, no progress shown");
    upgrade::setEnabled(bp, u.slot(), true);
    ctx.check(screen.tabData(u.slot()).value("burning", false), "enabled again: still burning");
    co_await ctx.ticks(120);
    ctx.check(CookingUpgrade::state(u).output.count == 2, "resumed: second beef done");
    player.getInventory().setItem(2, ItemStack::EMPTY_ITEM());
});

ABP_REGISTER_SELFTEST("cooking.tab_slots_and_xp", [](Context& ctx) {
    auto& player = ctx.player();
    clearPlayer(player);
    std::shared_ptr<game::Backpack> bp;
    auto                            u = withUpgrade(ctx, bp, "smelting");
    NativeScreen                    screen(ctx, player, bp);
    // Only cookable items go into the input, only fuel into the fuel slot.
    setCursor(player, game::ItemConv::make("minecraft:dirt", 4));
    screen.tab(u.slot(), {{"slot", 0}, {"button", 0}});
    ctx.check(CookingUpgrade::state(u).input.empty() && cursor(player).mCount == 4, "dirt refused by the input");
    setCursor(player, game::ItemConv::make("minecraft:raw_iron", 4));
    screen.tab(u.slot(), {{"slot", 0}, {"button", 0}});
    ctx.check(CookingUpgrade::state(u).input.count == 4 && cursor(player).isNull(), "raw iron into the input");
    setCursor(player, game::ItemConv::make("minecraft:raw_iron", 2));
    screen.tab(u.slot(), {{"slot", 1}, {"button", 0}});
    ctx.check(CookingUpgrade::state(u).fuel.empty(), "raw iron is no fuel");
    setCursor(player, game::ItemConv::make("minecraft:coal", 1));
    screen.tab(u.slot(), {{"slot", 1}, {"button", 0}});
    ctx.check(CookingUpgrade::state(u).fuel.count == 1, "coal into the fuel slot");
    setCursor(player, ItemStack::EMPTY_ITEM());
    auto& type = upgradeOf(Kind::Smelting, false);
    type.advance(u, 401);
    auto tab = screen.tabData(u.slot());
    ctx.check(tab.value("kind", "") == "smelting" && !tab.value("auto", true), "tab kind");
    ctx.check(tab["slots"][2].value("count", 0) == 2, "tab shows 2 ingots: " + tab.dump());
    ctx.check(tab.value("burning", false) && tab.value("burnTotal", 0) == 1600 && tab.value("cookTotal", 0) == 200, "burn / cook times");
    // Nothing can be put into the output.
    setCursor(player, game::ItemConv::make("minecraft:iron_ingot", 1));
    screen.tab(u.slot(), {{"slot", 2}, {"button", 0}});
    ctx.check(cursor(player).mCount == 3 && CookingUpgrade::state(u).output.empty(), "same item: the output is picked up into the cursor");
    ctx.check(CookingUpgrade::state(u).storedXp < 1.f, "taking the results pops the stored experience");
    setCursor(player, ItemStack::EMPTY_ITEM());
});

ABP_REGISTER_SELFTEST("cooking.shift_clicks", [](Context& ctx) {
    auto& player = ctx.player();
    clearPlayer(player);
    std::shared_ptr<game::Backpack> bp;
    auto                            u = withUpgrade(ctx, bp, "smelting");
    feature::gui::setPlayerOption(player, feature::gui::kOpenTabFirst, nlohmann::json(true));
    NativeScreen screen(ctx, player, bp);
    screen.openTab(u.slot(), *bp);
    ctx.give(player, game::ItemConv::make("minecraft:coal", 5), 9);
    ctx.give(player, game::ItemConv::make("minecraft:raw_iron", 7), 10);
    ctx.give(player, game::ItemConv::make("minecraft:oak_log", 3), 11);
    screen.action({{"action", "click"}, {"area", "inventory"}, {"index", 9}, {"shift", true}});
    screen.action({{"action", "click"}, {"area", "inventory"}, {"index", 10}, {"shift", true}});
    auto s = CookingUpgrade::state(u);
    ctx.check(s.fuel.id == "minecraft:coal" && s.fuel.count == 5, "coal: into the fuel slot, got " + str(s.fuel));
    ctx.check(s.input.id == "minecraft:raw_iron" && s.input.count == 7, "raw iron: into the input, got " + str(s.input));
    screen.action({{"action", "click"}, {"area", "inventory"}, {"index", 11}, {"shift", true}});
    ctx.check(countIn(*bp, "minecraft:oak_log") == 3, "logs (input and fuel taken): into the storage");
    screen.tab(u.slot(), {{"slot", 1}, {"shift", true}});
    ctx.check(CookingUpgrade::state(u).fuel.empty() && countIn(*bp, "minecraft:coal") == 5, "fuel shift-click: into the storage");
    feature::gui::setPlayerOption(player, feature::gui::kOpenTabFirst, std::nullopt);
    clearPlayer(player);
});

ABP_REGISTER_SELFTEST("cooking.auto_filters", [](Context& ctx) {
    auto& player = ctx.player();
    clearPlayer(player);
    std::shared_ptr<game::Backpack> bp;
    auto                            u    = withUpgrade(ctx, bp, "auto_smelting");
    auto&                           type = upgradeOf(Kind::Smelting, true);
    core::AccessContext             internal{core::Source::Internal};
    bp->inventory().insert(data("minecraft:raw_iron", 10), internal);
    bp->inventory().insert(data("minecraft:coal", 2), internal);
    ctx.check(type.inputFilter(u).ghosts.size() == 8 && type.fuelFilter(u).ghosts.size() == 4, "SB: 8 input / 4 fuel filter slots");
    type.advance(u, 20);
    auto s = CookingUpgrade::state(u);
    ctx.check(s.input.empty(), "SB: an empty input allow list matches nothing, got " + str(s.input));
    ctx.check(s.fuel.id == "minecraft:coal", "an empty fuel filter matches everything");
    // Put raw iron into the input filter through the tab.
    NativeScreen screen(ctx, player, bp);
    setCursor(player, game::ItemConv::make("minecraft:dirt", 1));
    screen.tab(u.slot(), {{"filter", "input"}, {"ghost", 0}, {"button", 0}});
    ctx.check(type.inputFilter(u).isEmpty(), "uncookable items can't go into the input filter");
    setCursor(player, game::ItemConv::make("minecraft:raw_iron", 1));
    screen.tab(u.slot(), {{"filter", "input"}, {"ghost", 0}, {"button", 0}});
    ctx.check(type.inputFilter(u).ghosts[0].id == "minecraft:raw_iron" && cursor(player).mCount == 1, "ghost set, cursor kept");
    setCursor(player, ItemStack::EMPTY_ITEM());
    auto tab = screen.tabData(u.slot());
    ctx.check(tab["inputFilter"].value("slots", 0) == 8 && tab["inputFilter"].value("perRow", 0) == 4, "filter layout in the tab data");
    type.advance(u, 61);
    ctx.check(CookingUpgrade::state(u).input.count == 10, "raw iron pulled as a full stack, got " + str(CookingUpgrade::state(u).input));
    type.advance(u, 500);
    ctx.check(countIn(*bp, "minecraft:iron_ingot") > 0, "results pushed into the backpack");
    screen.tab(u.slot(), {{"filter", "input"}, {"op", "allowList"}});
    ctx.check(type.inputFilter(u).mode == core::FilterMode::Deny, "allow / deny toggle");
    screen.tab(u.slot(), {{"filter", "input"}, {"ghost", 0}, {"button", 0}});
    ctx.check(type.inputFilter(u).ghosts[0].empty(), "empty cursor clears the ghost");
});

ABP_REGISTER_SELFTEST("cooking.auto_xp_into_tank", [](Context& ctx) {
    std::shared_ptr<game::Backpack> bp;
    auto                            u = withUpgrade(ctx, bp, "auto_smelting", Tier::Gold);
    ctx.installUpgrade(bp, "tank");
    auto& type = upgradeOf(Kind::Smelting, true);
    auto  f    = type.inputFilter(u);
    f.setGhost(0, data("minecraft:raw_iron", 1));
    CookingUpgrade::setInputFilter(u, f);
    core::AccessContext internal{core::Source::Internal};
    bp->inventory().insert(data("minecraft:raw_iron", 8), internal);
    bp->inventory().insert(data("minecraft:coal", 1), internal);
    type.advance(u, 1700);
    ctx.check(countIn(*bp, "minecraft:iron_ingot") == 8, "8 ingots pushed, got " + std::to_string(countIn(*bp, "minecraft:iron_ingot")));
    auto handler = api::backpack::fluidHandler(bp);
    ctx.require(handler != nullptr, "the backpack has a tank");
    auto fluid = handler->fluidInTank(0);
    ctx.check(fluid.id == api::fluids::Experience && fluid.amount > 0, "SB: experience poured into the tank as liquid experience, got " + fluid.id + " " + std::to_string(fluid.amount));
    ctx.check(CookingUpgrade::state(u).storedXp < 0.05f, "stored experience drained");
});

ABP_REGISTER_SELFTEST("cooking.state_travels_with_item", [](Context& ctx) {
    auto&                           player = ctx.player();
    std::shared_ptr<game::Backpack> bp;
    auto                            u = withUpgrade(ctx, bp, "smelting");
    load(u, data("minecraft:raw_iron", 5), data("minecraft:coal", 3));
    upgradeOf(Kind::Smelting, false).advance(u, 250);
    auto item = upgrade::remove(bp, u.slot(), &player);
    ctx.require(item.has_value(), "removed");
    auto        other = ctx.createBackpack(Tier::Leather);
    std::string reason;
    ctx.require(upgrade::install(other, 0, *item, &player, &reason), "reinstall: " + reason);
    auto s = CookingUpgrade::state(UpgradeContext(other, 0));
    ctx.check(s.input.count == 4 && s.fuel.count == 2 && s.output.count == 1, "items travel with the upgrade: " + str(s.input) + " " + str(s.fuel) + " " + str(s.output));
    ctx.check(!s.burning() && !s.cooking, "SC: removing pauses (burning fuel and progress are lost)");
});

BlockPos placeForTest(Context& ctx, placed::test::Site& site, game::Backpack const& bp) {
    auto pos = site.at(0, 1);
    site.set(pos, "minecraft:air"); // also marks it for cleanup
    auto item = ctx.backpackItem(bp);
    auto r    = placed::PlacedBackpacks::get().place(site.region(), pos, item, 2);
    ctx.require(r == placed::PlacedBackpacks::PlaceResult::Ok, "place() failed: " + std::to_string(static_cast<int>(r)));
    return pos;
}

ABP_REGISTER_SELFTEST("cooking.placed_keeps_cooking", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& sp = ctx.spawnPlayer();
    co_await placed::test::toGround(ctx, sp);
    placed::test::Site              site(sp);
    std::shared_ptr<game::Backpack> bp;
    auto                            u = withUpgrade(ctx, bp, "smoking");
    load(u, data("minecraft:beef", 3), data("minecraft:coal", 1));
    (void)placeForTest(ctx, site, *bp);
    co_await ctx.ticks(330);
    auto s = CookingUpgrade::state(u);
    ctx.check(s.output.id == "minecraft:cooked_beef" && s.output.count == 3, "smoked while placed: " + str(s.output));
    ctx.check(s.storedXp > 0, "experience stored on the upgrade");
});

} // namespace
} // namespace abp::upgrade::cooking
