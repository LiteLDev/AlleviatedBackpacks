// Selftests of the cooking upgrades: `backpack selftest "cooking.*"`.

#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "game/ItemConv.h"
#include "selftest/SelfTest.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/cooking/Cooking.h"

namespace abp::upgrade::cooking {
namespace {

using selftest::Context;
using core::Tier;
using Where  = ui::MenuManager::SlotAddress::Where;
using Action = ui::MenuManager::ActionType;

core::ItemStackData data(std::string_view id, int count) { return game::ItemConv::toData(game::ItemConv::make(id, count)); }

int countOf(game::Backpack& bp, std::string_view id) {
    return static_cast<int>(bp.inventory().count([&](core::ItemStackData const& s) { return s.id == id; }));
}

int inventoryCount(Player& p, std::string_view id) {
    int n = 0;
    for (int i = 0; i < 36; ++i) {
        auto const& it = p.getInventory().getItem(i);
        if (!it.isNull() && it.getTypeName() == id) n += it.mCount;
    }
    return n;
}

/// A backpack with the upgrade `id` installed; returns its context.
UpgradeContext withUpgrade(Context& ctx, std::shared_ptr<game::Backpack>& bp, std::string const& id, Tier tier = Tier::Leather) {
    bp       = ctx.createBackpack(tier);
    int slot = ctx.installUpgrade(bp, id);
    return UpgradeContext(bp, slot);
}

std::string str(core::ItemStackData const& d) { return d.empty() ? "empty" : d.id + " x" + std::to_string(d.count); }

ABP_REGISTER_SELFTEST("cooking.recipes_and_fuel", [](Context& ctx) {
    auto check = [&](Kind k, char const* in, char const* out) {
        auto r = recipeResult(k, data(in, 1));
        if (out) ctx.check(r && r->id == out, std::string(recipeTag(k)) + ": " + in + " -> " + (r ? r->id : "none"));
        else ctx.check(!r, std::string(recipeTag(k)) + ": " + in + " should not cook");
    };
    check(Kind::Smelting, "minecraft:raw_iron", "minecraft:iron_ingot");
    check(Kind::Smelting, "minecraft:beef", "minecraft:cooked_beef");
    check(Kind::Smelting, "minecraft:diamond", nullptr);
    check(Kind::Smoking, "minecraft:beef", "minecraft:cooked_beef");
    check(Kind::Smoking, "minecraft:raw_iron", nullptr);
    check(Kind::Blasting, "minecraft:raw_iron", "minecraft:iron_ingot");
    check(Kind::Blasting, "minecraft:beef", nullptr);

    int coalFurnace = vanillaBurnTicks(Kind::Smelting, data("minecraft:coal", 1));
    int coalBlast   = vanillaBurnTicks(Kind::Blasting, data("minecraft:coal", 1));
    ctx.info("coal burn ticks: furnace=" + std::to_string(coalFurnace) + " blast=" + std::to_string(coalBlast));
    ctx.check(coalFurnace == 1600, "coal burns 1600 ticks in a furnace");
    ctx.check(coalBlast == 800, "coal burns 800 ticks in a blast furnace");
    ctx.check(vanillaBurnTicks(Kind::Smelting, data("minecraft:stone", 1)) == 0, "stone is no fuel");
    ctx.check(vanillaBurnTicks(Kind::Smelting, data("minecraft:lava_bucket", 1)) > 0, "lava bucket is fuel");
    int xp = xpFor(data("minecraft:iron_ingot", 1), 10);
    ctx.info("xp for 10 iron ingots: " + std::to_string(xp));
    ctx.check(xp >= 5, "smelting xp for 10 iron ingots = " + std::to_string(xp));
});

ABP_REGISTER_SELFTEST("cooking.smelting_runs", [](Context& ctx) {
    std::shared_ptr<game::Backpack> bp;
    auto                            u  = withUpgrade(ctx, bp, "smelting");
    auto&                           up = upgradeOf(Kind::Smelting, false);
    CookState                       st;
    st.input = data("minecraft:raw_iron", 3);
    st.fuel  = data("minecraft:coal", 2);
    CookingUpgrade::setState(u, st);
    up.advance(u, nullptr, 599);
    auto s = CookingUpgrade::state(u);
    ctx.check(s.output.count == 2, "2 ingots after 599 ticks, got " + str(s.output));
    up.advance(u, nullptr, 1);
    s = CookingUpgrade::state(u);
    ctx.check(s.output.id == "minecraft:iron_ingot" && s.output.count == 3, "3 ingots after 600 ticks, got " + str(s.output));
    ctx.check(s.input.empty(), "input used up");
    ctx.check(s.fuel.count == 1 && s.burning(), "one coal burning, one left");
    ctx.check(countOf(*bp, "minecraft:iron_ingot") == 0, "plain smelting does not touch the storage");
});

ABP_REGISTER_SELFTEST("cooking.fast_kinds", [](Context& ctx) {
    std::shared_ptr<game::Backpack> a, b, c;
    auto                            smoke = withUpgrade(ctx, a, "smoking");
    auto                            blast = withUpgrade(ctx, b, "blasting");
    auto                            smelt = withUpgrade(ctx, c, "smelting");
    CookState                       st;
    st.input = data("minecraft:beef", 4);
    st.fuel  = data("minecraft:coal", 1);
    CookingUpgrade::setState(smoke, st);
    CookingUpgrade::setState(smelt, st);
    st.input = data("minecraft:raw_gold", 4);
    CookingUpgrade::setState(blast, st);
    upgradeOf(Kind::Smoking, false).advance(smoke, nullptr, 200);
    upgradeOf(Kind::Blasting, false).advance(blast, nullptr, 200);
    upgradeOf(Kind::Smelting, false).advance(smelt, nullptr, 200);
    ctx.check(CookingUpgrade::state(smoke).output.count == 2, "smoker: 2 in 200 ticks, got " + str(CookingUpgrade::state(smoke).output));
    ctx.check(CookingUpgrade::state(blast).output.count == 2, "blast: 2 in 200 ticks, got " + str(CookingUpgrade::state(blast).output));
    ctx.check(CookingUpgrade::state(smelt).output.count == 1, "furnace: 1 in 200 ticks, got " + str(CookingUpgrade::state(smelt).output));
    // The smoker refuses non-food: its state keeps the input untouched.
    CookState ore;
    ore.input = data("minecraft:raw_iron", 2);
    ore.fuel  = data("minecraft:coal", 1);
    CookingUpgrade::setState(smoke, ore);
    upgradeOf(Kind::Smoking, false).advance(smoke, nullptr, 300);
    auto s = CookingUpgrade::state(smoke);
    ctx.check(s.input.count == 2 && s.fuel.count == 1 && s.output.empty(), "smoker doesn't smelt ore nor burn fuel for it");
});

ABP_REGISTER_SELFTEST("cooking.lava_bucket_leaves_bucket", [](Context& ctx) {
    std::shared_ptr<game::Backpack> bp;
    auto                            u = withUpgrade(ctx, bp, "smelting");
    CookState                       st;
    st.input = data("minecraft:raw_iron", 1);
    st.fuel  = data("minecraft:lava_bucket", 1);
    CookingUpgrade::setState(u, st);
    upgradeOf(Kind::Smelting, false).advance(u, nullptr, 1);
    auto s = CookingUpgrade::state(u);
    ctx.check(s.fuel.id == "minecraft:bucket" && s.fuel.count == 1, "bucket left, got " + str(s.fuel));
    ctx.check(s.burning(), "lava burning");
});

ABP_REGISTER_SELFTEST("cooking.carried_backpack_ticks", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto&                           player = ctx.player();
    std::shared_ptr<game::Backpack> bp;
    auto                            u = withUpgrade(ctx, bp, "smoking");
    CookState                       st;
    st.input = data("minecraft:beef", 1);
    st.fuel  = data("minecraft:coal", 1);
    CookingUpgrade::setState(u, st);
    ctx.require(ctx.give(player, ctx.backpackItem(*bp), 2) == 2, "give backpack");
    co_await ctx.ticks(140);
    auto s = CookingUpgrade::state(u);
    ctx.check(s.output.id == "minecraft:cooked_beef" && s.output.count == 1, "beef smoked while carried, got " + str(s.output));
    // Disabled upgrades don't cook.
    st.input = data("minecraft:beef", 1);
    st.output.clear();
    st.cook = 0;
    CookingUpgrade::setState(u, st);
    upgrade::setEnabled(bp, u.slot(), false);
    co_await ctx.ticks(140);
    ctx.check(CookingUpgrade::state(u).output.empty(), "disabled upgrade does nothing");
});

ABP_REGISTER_SELFTEST("cooking.menu_slots_and_xp", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    std::shared_ptr<game::Backpack> bp;
    auto                            u  = withUpgrade(ctx, bp, "smelting");
    auto&                           up = upgradeOf(Kind::Smelting, false);
    auto const&                     L  = layout(false);
    ctx.require(ctx.give(player, game::ItemConv::make("minecraft:raw_iron", 3), 5) == 5, "give iron");
    ctx.require(ctx.give(player, game::ItemConv::make("minecraft:coal", 2), 6) == 6, "give coal");
    ctx.require(ctx.give(player, game::ItemConv::make("minecraft:diamond", 4), 7) == 7, "give diamonds");
    auto menu = up.createSettingsMenu(player, u);
    ctx.require(menu != nullptr && mm.open(player, menu), "open cooking menu");
    mm.forceShown(player);

    (void)mm.simulate(player, {Action::Place, {Where::Inventory, 7}, {Where::Menu, L.input}, 4});
    ctx.check(CookingUpgrade::state(u).input.empty(), "diamonds refused as input");
    // From the cursor (a shift-click would fall back to any accepting slot, i.e. the fuel slot).
    ctx.check(mm.simulate(player, {Action::Take, {Where::Inventory, 6}, {Where::Cursor, 0}, 2}), "coal to cursor");
    ctx.check(!mm.simulate(player, {Action::Place, {Where::Cursor, 0}, {Where::Menu, L.input}, 2}), "coal refused as input");
    ctx.check(CookingUpgrade::state(u).input.empty(), "input still empty");
    ctx.check(mm.simulate(player, {Action::Place, {Where::Cursor, 0}, {Where::Menu, L.fuel}, 2}), "coal into fuel");
    ctx.check(mm.simulate(player, {Action::Place, {Where::Inventory, 5}, {Where::Menu, L.input}, 3}), "iron into input");
    (void)mm.simulate(player, {Action::Place, {Where::Inventory, 7}, {Where::Menu, L.fuel}, 4});
    auto s = CookingUpgrade::state(u);
    ctx.check(s.input.count == 3 && s.fuel.count == 2, "slots hold iron x3 and coal x2: " + str(s.input) + ", " + str(s.fuel));
    ctx.check(inventoryCount(player, "minecraft:diamond") == 4, "diamonds stay in the inventory");

    up.advance(u, nullptr, 250);
    co_await ctx.ticks(3);
    auto flame = mm.shownItem(player, L.flame);
    ctx.check(!flame.isNull() && flame.getTypeName() == "minecraft:blaze_powder", "flame shows burning");
    auto out = mm.shownItem(player, L.output);
    ctx.check(!out.isNull() && out.getTypeName() == "minecraft:iron_ingot", "output slot shows the ingot");
    // Buttons never move items.
    ctx.check(!mm.simulate(player, {Action::Take, {Where::Menu, L.flame}, {Where::Cursor, 0}, 1}), "flame is not takeable");

    up.advance(u, nullptr, 400);
    int xpBefore = player.getXpEarnedAtCurrentLevel();
    ctx.check(mm.simulate(player, {Action::Take, {Where::Menu, L.output}, {Where::Inventory, 9}, 3}), "take output");
    ctx.check(inventoryCount(player, "minecraft:iron_ingot") == 3, "3 ingots in the inventory");
    ctx.check(CookingUpgrade::state(u).output.empty(), "output emptied");
    int xpAfter = player.getXpEarnedAtCurrentLevel();
    ctx.info("xp " + std::to_string(xpBefore) + " -> " + std::to_string(xpAfter));
    ctx.check(xpAfter > xpBefore, "taking results gives experience");
    ctx.check(mm.simulate(player, {Action::Take, {Where::Inventory, 7}, {Where::Cursor, 0}, 4}), "diamonds to cursor");
    ctx.check(!mm.simulate(player, {Action::Place, {Where::Cursor, 0}, {Where::Menu, L.output}, 4}), "output slot refuses items");
    ctx.check(CookingUpgrade::state(u).output.empty(), "output still empty");
    mm.close(player);
});

ABP_REGISTER_SELFTEST("cooking.auto_smelting", [](Context& ctx) {
    auto&                           player = ctx.player();
    std::shared_ptr<game::Backpack> bp;
    auto                            u  = withUpgrade(ctx, bp, "auto_smelting");
    auto&                           up = upgradeOf(Kind::Smelting, true);
    core::AccessContext             internal{core::Source::Internal};
    bp->inventory().insert(data("minecraft:raw_iron", 5), internal);
    bp->inventory().insert(data("minecraft:coal", 2), internal);
    bp->inventory().insert(data("minecraft:oak_log", 4), internal); // after the coal: coal burns first
    auto filter = CookingUpgrade::inputFilter(u);
    filter.setGhost(0, data("minecraft:raw_iron", 1));
    CookingUpgrade::setInputFilter(u, filter);
    int xpBefore = player.getXpEarnedAtCurrentLevel();
    for (int i = 0; i < 120; ++i) up.advance(u, &player, 10);
    ctx.check(countOf(*bp, "minecraft:iron_ingot") == 5, "5 ingots stored, got " + std::to_string(countOf(*bp, "minecraft:iron_ingot")));
    ctx.check(countOf(*bp, "minecraft:raw_iron") == 0, "raw iron used");
    ctx.check(countOf(*bp, "minecraft:oak_log") == 4, "logs not in the input filter and coal comes first: untouched");
    ctx.check(countOf(*bp, "minecraft:coal") == 1, "one coal burnt, got " + std::to_string(countOf(*bp, "minecraft:coal")));
    auto s = CookingUpgrade::state(u);
    ctx.check(s.input.empty() && s.output.empty(), "slots drained: in " + str(s.input) + " out " + str(s.output));
    ctx.check(player.getXpEarnedAtCurrentLevel() > xpBefore, "carrier got the experience");
});

ABP_REGISTER_SELFTEST("cooking.auto_fuel_filter", [](Context& ctx) {
    std::shared_ptr<game::Backpack> bp;
    auto                            u = withUpgrade(ctx, bp, "auto_blasting");
    core::AccessContext             internal{core::Source::Internal};
    bp->inventory().insert(data("minecraft:coal", 3), internal);
    bp->inventory().insert(data("minecraft:charcoal", 3), internal);
    bp->inventory().insert(data("minecraft:raw_copper", 2), internal);
    bp->inventory().insert(data("minecraft:beef", 2), internal); // blast furnace can't cook food
    auto fuel = CookingUpgrade::fuelFilter(u);
    fuel.setGhost(0, data("minecraft:charcoal", 1));
    CookingUpgrade::setFuelFilter(u, fuel);
    for (int i = 0; i < 30; ++i) upgradeOf(Kind::Blasting, true).advance(u, nullptr, 10);
    ctx.check(countOf(*bp, "minecraft:copper_ingot") == 2, "copper smelted");
    ctx.check(countOf(*bp, "minecraft:coal") == 3, "coal kept (not in the fuel filter)");
    ctx.check(countOf(*bp, "minecraft:charcoal") == 2, "one charcoal burnt");
    ctx.check(countOf(*bp, "minecraft:beef") == 2, "beef ignored by the blast furnace");
    // Deny mode: everything except charcoal.
    fuel.mode = core::FilterMode::Deny;
    CookingUpgrade::setFuelFilter(u, fuel);
    bp->inventory().insert(data("minecraft:raw_copper", 20), internal);
    auto s = CookingUpgrade::state(u);
    s.burn = 0;
    CookingUpgrade::setState(u, s);
    for (int i = 0; i < 30; ++i) upgradeOf(Kind::Blasting, true).advance(u, nullptr, 10);
    ctx.check(countOf(*bp, "minecraft:charcoal") == 2, "deny list: charcoal kept");
    ctx.check(countOf(*bp, "minecraft:coal") < 3, "deny list: coal burnt");
});

ABP_REGISTER_SELFTEST("cooking.auto_menu_filters", [](Context& ctx) {
    auto&                           mm     = ui::MenuManager::get();
    auto&                           player = ctx.player();
    std::shared_ptr<game::Backpack> bp;
    auto                            u  = withUpgrade(ctx, bp, "auto_smoking");
    auto&                           up = upgradeOf(Kind::Smoking, true);
    auto const&                     L  = layout(true);
    auto                            menu = up.createSettingsMenu(player, u);
    ctx.require(menu != nullptr && menu->size() == 27 && mm.open(player, menu), "open auto menu");
    mm.forceShown(player);
    ctx.require(ctx.give(player, game::ItemConv::make("minecraft:chicken", 5), 4) == 4, "give chicken");
    ctx.check(mm.simulate(player, {Action::Take, {Where::Inventory, 4}, {Where::Cursor, 0}, 5}), "to cursor");
    (void)mm.simulate(player, {Action::Place, {Where::Cursor, 0}, {Where::Menu, L.inputFilterStart + 1}, 5});
    auto f = CookingUpgrade::inputFilter(u);
    ctx.check(f.ghosts.size() == 9 && f.ghosts[1].id == "minecraft:chicken", "input filter ghost set");
    (void)mm.simulate(player, {Action::Place, {Where::Cursor, 0}, {Where::Menu, L.fuelFilterStart}, 5});
    ctx.check(CookingUpgrade::fuelFilter(u).ghosts[0].id == "minecraft:chicken", "fuel filter ghost set");
    (void)mm.simulate(player, {Action::Take, {Where::Menu, L.inputModeToggle}, {Where::Cursor, 0}, 1});
    ctx.check(CookingUpgrade::inputFilter(u).mode == core::FilterMode::Deny, "input filter mode toggled");
    ctx.check(CookingUpgrade::fuelFilter(u).mode == core::FilterMode::Allow, "fuel filter mode unchanged");
    mm.close(player);
    ctx.check(inventoryCount(player, "minecraft:chicken") == 5, "ghosts don't consume items");
});

ABP_REGISTER_SELFTEST("cooking.state_travels_with_item", [](Context& ctx) {
    std::shared_ptr<game::Backpack> a;
    auto                            u = withUpgrade(ctx, a, "blasting");
    CookState                       st;
    st.input  = data("minecraft:raw_gold", 7);
    st.fuel   = data("minecraft:coal", 3);
    st.output = data("minecraft:gold_ingot", 2);
    CookingUpgrade::setState(u, st);
    auto removed = upgrade::remove(a, u.slot(), nullptr);
    ctx.require(removed.has_value(), "remove");
    auto        b = ctx.createBackpack(Tier::Leather);
    std::string why;
    ctx.require(upgrade::install(b, 0, *removed, nullptr, &why), "reinstall: " + why);
    auto s = CookingUpgrade::state(UpgradeContext(b, 0));
    ctx.check(s.input.count == 7 && s.fuel.count == 3 && s.output.count == 2, "contents kept in the upgrade item");
});

ABP_REGISTER_SELFTEST("cooking.one_per_backpack", [](Context& ctx) {
    std::shared_ptr<game::Backpack> bp;
    (void)withUpgrade(ctx, bp, "smelting", Tier::Gold);
    auto item = game::ItemConv::toData(game::ItemConv::make(upgradeOf(Kind::Blasting, true).info().itemId, 1));
    ctx.require(!item.empty(), "auto blasting item registered");
    ctx.check(!upgrade::canInstall(bp, 1, item, nullptr), "only one cooking upgrade per backpack");
    for (auto k : {Kind::Smelting, Kind::Smoking, Kind::Blasting}) {
        for (bool a : {false, true}) {
            auto id = upgradeOf(k, a).info().itemId;
            ctx.check(!game::ItemConv::make(id, 1).isNull(), id + " is registered");
        }
    }
});

ABP_REGISTER_SELFTEST("cooking.menu_rollback_keeps_items", [](Context& ctx) {
    auto&                           mm     = ui::MenuManager::get();
    auto&                           player = ctx.player();
    std::shared_ptr<game::Backpack> bp;
    auto                            u  = withUpgrade(ctx, bp, "smelting");
    auto&                           up = upgradeOf(Kind::Smelting, false);
    auto const&                     L  = layout(false);
    CookState                       st;
    st.input  = data("minecraft:raw_iron", 1);
    st.fuel   = data("minecraft:lava_bucket", 1);
    st.output = data("minecraft:iron_ingot", 5);
    st.cook   = 150;
    CookingUpgrade::setState(u, st);
    up.advance(u, nullptr, 1); // lava ignites, the bucket stays in the fuel slot
    ctx.require(CookingUpgrade::state(u).fuel.id == "minecraft:bucket", "bucket in the fuel slot");
    ctx.require(ctx.give(player, game::ItemConv::make("minecraft:dirt", 1), 3) == 3, "give dirt");
    auto menu = up.createSettingsMenu(player, u);
    ctx.require(menu != nullptr && mm.open(player, menu), "open");
    mm.forceShown(player);
    int xp0 = player.getXpEarnedAtCurrentLevel();
    // Swapping with an item the slot refuses rolls back without losing anything.
    for (int slot : {L.output, L.fuel, L.input}) {
        (void)mm.simulate(player, {Action::Swap, {Where::Menu, slot}, {Where::Inventory, 3}, 1});
    }
    auto s = CookingUpgrade::state(u);
    ctx.check(s.output.count == 5, "results kept: " + str(s.output));
    ctx.check(s.fuel.id == "minecraft:bucket", "bucket kept: " + str(s.fuel));
    ctx.check(s.input.count == 1 && s.cook >= 150, "input and progress kept: " + str(s.input) + " cook " + std::to_string(s.cook));
    ctx.check(inventoryCount(player, "minecraft:dirt") == 1, "dirt kept");
    // Experience is paid once per result.
    // Reopening must not pay twice either.
    mm.close(player);
    menu = up.createSettingsMenu(player, u);
    ctx.require(menu != nullptr && mm.open(player, menu), "reopen");
    mm.forceShown(player);
    ctx.check(mm.simulate(player, {Action::Take, {Where::Menu, L.output}, {Where::Inventory, 10}, 5}), "take results");
    int xp1 = player.getXpEarnedAtCurrentLevel();
    ctx.info("xp " + std::to_string(xp0) + " -> " + std::to_string(xp1));
    ctx.check(xp1 - xp0 >= 3 && xp1 - xp0 <= 4, "5 ingots pay 3 or 4 xp in total, got " + std::to_string(xp1 - xp0));
    ctx.check(mm.simulate(player, {Action::Take, {Where::Menu, L.fuel}, {Where::Inventory, 11}, 1}), "take the bucket");
    ctx.check(inventoryCount(player, "minecraft:bucket") == 1, "bucket in the inventory");
    mm.close(player);
});

ABP_REGISTER_SELFTEST("cooking.auto_no_recook_chain", [](Context& ctx) {
    std::shared_ptr<game::Backpack> bp;
    auto                            u = withUpgrade(ctx, bp, "auto_smelting");
    core::AccessContext             internal{core::Source::Internal};
    bp->inventory().insert(data("minecraft:cobblestone", 2), internal);
    bp->inventory().insert(data("minecraft:coal", 1), internal);
    for (int i = 0; i < 100; ++i) upgradeOf(Kind::Smelting, true).advance(u, nullptr, 10);
    ctx.check(countOf(*bp, "minecraft:stone") == 2, "cobblestone -> stone: " + std::to_string(countOf(*bp, "minecraft:stone")));
    ctx.check(countOf(*bp, "minecraft:smooth_stone") == 0, "stone is not cooked again into smooth stone");
    // Naming stone in the input filter cooks it on purpose.
    auto f = CookingUpgrade::inputFilter(u);
    f.setGhost(0, data("minecraft:stone", 1));
    CookingUpgrade::setInputFilter(u, f);
    for (int i = 0; i < 50; ++i) upgradeOf(Kind::Smelting, true).advance(u, nullptr, 10);
    ctx.check(countOf(*bp, "minecraft:smooth_stone") == 2, "explicit filter: smooth stone made");
});

} // namespace
} // namespace abp::upgrade::cooking