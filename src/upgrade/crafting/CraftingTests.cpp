// Selftests for the Crafting upgrade: crafting.*
// Run in-game with `/backpack selftest "crafting.*"`. UI behaviour is driven through the native
// tab's actions (the same packets the client companion sends).

#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "feature/gui/Gui.h"
#include "game/ItemConv.h"
#include "game/Storage.h"
#include "selftest/SelfTest.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/crafting/CraftingUpgrade.h"
#include "upgrade/crafting/Station.h"
#include "upgrade/crafting/StationTestSupport.h"

namespace abp::upgrade::crafting {
namespace {

using core::Tier;
using selftest::Context;
using namespace test;

std::string idOf(ItemStack const& s) { return s.isNull() ? std::string("<empty>") : s.getTypeName(); }

/// A backpack with the crafting upgrade installed; returns its context.
UpgradeContext withCrafting(Context& ctx, Tier tier = Tier::Iron) {
    auto bp   = ctx.createBackpack(tier);
    int  slot = ctx.installUpgrade(bp, std::string(kCraftingId));
    return UpgradeContext(bp, slot);
}

void setGrid(UpgradeContext const& u, std::initializer_list<std::pair<int, core::ItemStackData>> items) {
    for (int i = 0; i < 9; ++i) setGridAt(u, i, {});
    for (auto const& [i, d] : items) setGridAt(u, i, d);
}

std::string resultId(UpgradeContext const& u) {
    auto m = currentMatch(u);
    return m ? idOf(m->result) : std::string("<none>");
}

void planks2x2(UpgradeContext const& u, int count) {
    setGrid(
        u,
        {
            {0, data("minecraft:oak_planks", count)},
            {1, data("minecraft:oak_planks", count)},
            {3, data("minecraft:oak_planks", count)},
            {4, data("minecraft:oak_planks", count)}
    }
    );
}

ABP_REGISTER_SELFTEST("crafting.recipes_available", [](Context& ctx) {
    ctx.check(recipeCount(kCraftingTag) > 100, "crafting_table recipes: " + std::to_string(recipeCount(kCraftingTag)));
    ctx.check(recipeCount(kStonecutterTag) > 10, "stonecutter recipes: " + std::to_string(recipeCount(kStonecutterTag)));
    auto* u = upgrade::byId(kCraftingId);
    ctx.require(u != nullptr, "crafting upgrade registered");
    ctx.check(!u->info().toggleable && u->info().hasSettings, "SB: a tab, no on/off switch");
    ctx.require(!game::ItemConv::make("alleviatedbackpacks:crafting_upgrade").isNull(), "crafting upgrade item exists");
});

ABP_REGISTER_SELFTEST("crafting.defaults", [](Context& ctx) {
    auto u = withCrafting(ctx);
    ctx.check(shiftIntoStorage(u), "SB: shift-click result into the backpack by default");
    ctx.check(!refillEnabled(u), "SB: refill crafting grid is off by default");
});

ABP_REGISTER_SELFTEST("crafting.shaped_match", [](Context& ctx) {
    auto u = withCrafting(ctx);
    planks2x2(u, 1);
    ctx.check(resultId(u) == "minecraft:crafting_table", "2x2 planks top-left -> crafting table, got " + resultId(u));
    setGrid(
        u,
        {
            {4, data("minecraft:oak_planks", 1)},
            {5, data("minecraft:oak_planks", 1)},
            {7, data("minecraft:oak_planks", 1)},
            {8, data("minecraft:oak_planks", 1)}
    }
    );
    ctx.check(resultId(u) == "minecraft:crafting_table", "2x2 planks bottom-right -> crafting table, got " + resultId(u));
    setGrid(
        u,
        {
            {2, data("minecraft:oak_planks", 1)},
            {5, data("minecraft:oak_planks", 1)}
    }
    );
    auto m = currentMatch(u);
    ctx.check(m && idOf(m->result) == "minecraft:stick" && m->result.mCount == 4, "vertical planks -> 4 sticks, got " + resultId(u));
    setGrid(
        u,
        {
            {0, data("minecraft:dirt",    1)},
            {4, data("minecraft:diamond", 1)}
    }
    );
    ctx.check(!currentMatch(u), "nonsense grid has no result, got " + resultId(u));
});

ABP_REGISTER_SELFTEST("crafting.tag_and_shapeless", [](Context& ctx) {
    auto u = withCrafting(ctx);
    setGrid(
        u,
        {
            {0, data("minecraft:oak_planks",    1)},
            {1, data("minecraft:spruce_planks", 1)},
            {3, data("minecraft:birch_planks",  1)},
            {4, data("minecraft:jungle_planks", 1)}
    }
    );
    ctx.check(resultId(u) == "minecraft:crafting_table", "mixed planks (tag ingredient) -> crafting table, got " + resultId(u));
    setGrid(
        u,
        {
            {0, data("minecraft:paper",   1)},
            {4, data("minecraft:leather", 1)},
            {6, data("minecraft:paper",   1)},
            {8, data("minecraft:paper",   1)}
    }
    );
    ctx.check(resultId(u) == "minecraft:book", "scattered paper + leather -> book, got " + resultId(u));
});

ABP_REGISTER_SELFTEST("crafting.tab_data", [](Context& ctx) {
    auto& player = ctx.player();
    auto  u      = withCrafting(ctx);
    planks2x2(u, 2);
    NativeScreen screen(ctx, player, u.backpackPtr());
    auto         tab = screen.tabData(u.slot());
    ctx.require(tab.contains("grid") && tab["grid"].size() == 9, "tab data has the 3x3 grid: " + tab.dump());
    ctx.check(tab["grid"][0].value("id", "") == "minecraft:oak_planks" && tab["grid"][2].is_null(), "grid cells");
    ctx.check(tab["result"].value("id", "") == "minecraft:crafting_table", "result shown");
    ctx.check(tab["results"].size() == 1 && tab.value("selected", -1) == 0, "one matching recipe: " + tab["results"].dump());
    ctx.check(tab.value("shiftIntoStorage", false) && !tab.value("refill", true), "toggles");
});

ABP_REGISTER_SELFTEST("crafting.click_crafts_to_cursor", [](Context& ctx) {
    auto& player = ctx.player();
    clearPlayer(player);
    auto u = withCrafting(ctx);
    planks2x2(u, 2);
    NativeScreen screen(ctx, player, u.backpackPtr());
    screen.tab(u.slot(), {{"result", true}, {"button", 0}});
    ctx.check(idOf(cursor(player)) == "minecraft:crafting_table" && cursor(player).mCount == 1, "cursor holds 1 crafting table");
    for (int i : {0, 1, 3, 4}) ctx.check(gridAt(u, i).count == 1, "one plank consumed from cell " + std::to_string(i));
    screen.tab(u.slot(), {{"result", true}, {"button", 0}});
    ctx.check(cursor(player).mCount == 2, "a second click stacks on the cursor");
    for (int i : {0, 1, 3, 4}) ctx.check(gridAt(u, i).empty(), "cell " + std::to_string(i) + " used up");
    screen.tab(u.slot(), {{"result", true}, {"button", 0}});
    ctx.check(cursor(player).mCount == 2, "an empty grid crafts nothing");
    setCursor(player, ItemStack::EMPTY_ITEM());
});

ABP_REGISTER_SELFTEST("crafting.cursor_mismatch_keeps_grid", [](Context& ctx) {
    auto& player = ctx.player();
    clearPlayer(player);
    auto u = withCrafting(ctx);
    planks2x2(u, 1);
    NativeScreen screen(ctx, player, u.backpackPtr());
    setCursor(player, game::ItemConv::make("minecraft:dirt", 5));
    screen.tab(u.slot(), {{"result", true}, {"button", 0}});
    ctx.check(idOf(cursor(player)) == "minecraft:dirt" && cursor(player).mCount == 5, "the cursor keeps its dirt");
    ctx.check(gridAt(u, 0).count == 1, "nothing crafted");
    setCursor(player, ItemStack::EMPTY_ITEM());
});

ABP_REGISTER_SELFTEST("crafting.grid_slot_clicks", [](Context& ctx) {
    auto& player = ctx.player();
    clearPlayer(player);
    auto u = withCrafting(ctx);
    setGrid(u, {});
    NativeScreen screen(ctx, player, u.backpackPtr());
    setCursor(player, game::ItemConv::make("minecraft:oak_planks", 10));
    screen.tab(u.slot(), {{"grid", 4}, {"button", 1}});
    ctx.check(gridAt(u, 4).count == 1 && cursor(player).mCount == 9, "right click places one");
    screen.tab(u.slot(), {{"grid", 4}, {"button", 0}});
    ctx.check(gridAt(u, 4).count == 10 && cursor(player).isNull(), "left click places the rest");
    screen.tab(u.slot(), {{"grid", 4}, {"button", 1}});
    ctx.check(gridAt(u, 4).count == 5 && cursor(player).mCount == 5, "right click takes half");
    setCursor(player, ItemStack::EMPTY_ITEM());
    screen.tab(u.slot(), {{"grid", 4}, {"shift", true}});
    ctx.check(gridAt(u, 4).empty() && countIn(u.backpack(), "minecraft:oak_planks") == 5, "shift-click: into the backpack");
});

ABP_REGISTER_SELFTEST("crafting.grid_refuses_backpacks", [](Context& ctx) {
    auto& player = ctx.player();
    clearPlayer(player);
    auto u     = withCrafting(ctx);
    auto other = ctx.createBackpack(Tier::Leather);
    NativeScreen screen(ctx, player, u.backpackPtr());
    setCursor(player, ctx.backpackItem(*other));
    screen.tab(u.slot(), {{"grid", 0}, {"button", 0}});
    ctx.check(gridAt(u, 0).empty(), "backpacks can't go into the grid");
    ctx.check(!cursor(player).isNull(), "the backpack stays on the cursor");
    setCursor(player, ItemStack::EMPTY_ITEM());
});

ABP_REGISTER_SELFTEST("crafting.container_items_remain", [](Context& ctx) {
    auto u = withCrafting(ctx);
    setGrid(
        u,
        {
            {0, data("minecraft:milk_bucket", 1)},
            {1, data("minecraft:milk_bucket", 1)},
            {2, data("minecraft:milk_bucket", 1)},
            {3, data("minecraft:sugar",       1)},
            {4, data("minecraft:egg",         1)},
            {5, data("minecraft:sugar",       1)},
            {6, data("minecraft:wheat",       1)},
            {7, data("minecraft:wheat",       1)},
            {8, data("minecraft:wheat",       1)}
    }
    );
    ctx.require(resultId(u) == "minecraft:cake", "cake recipe matches, got " + resultId(u));
    auto out = craftOnce(u, ctx.player());
    ctx.check(idOf(out) == "minecraft:cake", "crafted a cake");
    for (int i = 0; i < 3; ++i) ctx.check(gridAt(u, i).id == "minecraft:bucket", "empty bucket left in cell " + std::to_string(i));
    for (int i = 3; i < 9; ++i) ctx.check(gridAt(u, i).empty(), "cell " + std::to_string(i) + " consumed");
});

ABP_REGISTER_SELFTEST("crafting.shift_result_into_backpack", [](Context& ctx) {
    auto& player = ctx.player();
    clearPlayer(player);
    auto u = withCrafting(ctx);
    setGrid(u, {{4, data("minecraft:oak_log", 10)}});
    ctx.require(resultId(u) == "minecraft:oak_planks", "log -> planks, got " + resultId(u));
    NativeScreen screen(ctx, player, u.backpackPtr());
    screen.tab(u.slot(), {{"result", true}, {"shift", true}});
    ctx.check(countIn(u.backpack(), "minecraft:oak_planks") == 40, "40 planks in the backpack, got " + std::to_string(countIn(u.backpack(), "minecraft:oak_planks")));
    ctx.check(gridAt(u, 4).empty(), "all logs used");
    ctx.check(countOnPlayer(player, "minecraft:oak_planks") == 0, "nothing went to the player");
});

ABP_REGISTER_SELFTEST("crafting.shift_result_into_inventory", [](Context& ctx) {
    auto& player = ctx.player();
    clearPlayer(player);
    auto u = withCrafting(ctx);
    setGrid(u, {{0, data("minecraft:oak_log", 3)}});
    NativeScreen screen(ctx, player, u.backpackPtr());
    screen.tab(u.slot(), {{"toggle", "shiftTarget"}});
    ctx.check(!shiftIntoStorage(u), "\"Shift Click Result Into Player's Inventory\"");
    ctx.check(!screen.tabData(u.slot()).value("shiftIntoStorage", true), "tab data follows");
    screen.tab(u.slot(), {{"result", true}, {"shift", true}});
    ctx.check(countOnPlayer(player, "minecraft:oak_planks") == 12, "12 planks in the player inventory");
    ctx.check(countIn(u.backpack(), "minecraft:oak_planks") == 0, "none in the backpack");
    clearPlayer(player);
});

ABP_REGISTER_SELFTEST("crafting.refill_from_backpack_then_player", [](Context& ctx) {
    auto& player = ctx.player();
    clearPlayer(player);
    auto u = withCrafting(ctx);
    NativeScreen screen(ctx, player, u.backpackPtr());
    screen.tab(u.slot(), {{"toggle", "refill"}});
    ctx.require(refillEnabled(u), "refill switched on");
    u.backpack().inventory().insert(data("minecraft:oak_log", 2), core::AccessContext{core::Source::Internal});
    ctx.give(player, game::ItemConv::make("minecraft:oak_log", 2), 9);
    setGrid(u, {{0, data("minecraft:oak_log", 1)}});
    // SC: the ingredient comes from the backpack first, then the player; the grid keeps its item.
    craftOnce(u, player);
    ctx.check(gridAt(u, 0).count == 1 && countIn(u.backpack(), "minecraft:oak_log") == 1, "refilled from the backpack");
    craftOnce(u, player);
    craftOnce(u, player);
    ctx.check(gridAt(u, 0).count == 1 && countIn(u.backpack(), "minecraft:oak_log") == 0 && countOnPlayer(player, "minecraft:oak_log") == 1,
              "then from the player");
    craftOnce(u, player);
    craftOnce(u, player);
    ctx.check(gridAt(u, 0).empty(), "the grid's own item is used last");
    // With refill, one shift-click crafts at most a stack's worth (SC repeated quick move limit).
    u.backpack().inventory().insert(data("minecraft:oak_log", 64 * 3), core::AccessContext{core::Source::Internal});
    setGrid(u, {{0, data("minecraft:oak_log", 1)}});
    int crafts = craftShift(u, player);
    ctx.check(crafts == 64, "64 crafts with refill, got " + std::to_string(crafts));
    clearPlayer(player);
});

ABP_REGISTER_SELFTEST("crafting.shift_stops_when_full", [](Context& ctx) {
    auto& player = ctx.player();
    auto  u      = withCrafting(ctx, Tier::Leather);
    auto& inv    = u.backpack().inventory();
    for (int i = 0; i < inv.size(); ++i) inv.set(i, data("minecraft:dirt", 64));
    for (int i = 0; i < 36; ++i) ctx.give(player, game::ItemConv::make("minecraft:dirt", 64), i);
    setGrid(u, {{0, data("minecraft:oak_log", 4)}});
    int crafts = craftShift(u, player);
    ctx.check(crafts == 0, "no room: nothing crafted, got " + std::to_string(crafts));
    ctx.check(gridAt(u, 0).count == 4, "no ingredients consumed");
    clearPlayer(player);
});

ABP_REGISTER_SELFTEST("crafting.result_cycler", [](Context& ctx) {
    auto& player = ctx.player();
    auto  u      = withCrafting(ctx);
    planks2x2(u, 1);
    auto first = currentMatch(u);
    ctx.require(first.has_value(), "a recipe");
    auto alt   = *first;
    alt.recipeId += "#alt";
    alt.result = game::ItemConv::make("minecraft:chest", 1);
    auto third   = *first;
    third.recipeId += "#third";
    third.result = game::ItemConv::make("minecraft:barrel", 1);
    injectResultsForTest(u, {*first, alt, third});
    NativeScreen screen(ctx, player, u.backpackPtr());
    ctx.check(screen.tabData(u.slot())["results"].size() == 3, "three results for the cycler");
    screen.tab(u.slot(), {{"next", true}});
    ctx.check(resultId(u) == "minecraft:chest", "Next -> second result, got " + resultId(u));
    screen.tab(u.slot(), {{"previous", true}});
    screen.tab(u.slot(), {{"previous", true}});
    ctx.check(resultId(u) == "minecraft:barrel", "Previous wraps around, got " + resultId(u));
    screen.tab(u.slot(), {{"select", 1}});
    ctx.check(resultId(u) == "minecraft:chest" && screen.tabData(u.slot()).value("selected", -1) == 1, "Select Result picks one");
});

ABP_REGISTER_SELFTEST("crafting.grid_travels_with_item", [](Context& ctx) {
    auto& player = ctx.player();
    auto  u      = withCrafting(ctx);
    auto  bp     = u.backpackPtr();
    setGrid(
        u,
        {
            {0, data("minecraft:iron_ingot", 5)},
            {8, data("minecraft:stick",      3)}
    }
    );
    auto item = upgrade::remove(bp, u.slot(), &player);
    ctx.require(item.has_value(), "upgrade removed");
    ctx.check(countIn(*bp, "minecraft:iron_ingot") == 0, "SB: the grid stays in the upgrade item");
    auto        other = ctx.createBackpack(Tier::Iron);
    std::string reason;
    ctx.require(upgrade::install(other, 0, *item, &player, &reason), "reinstall: " + reason);
    UpgradeContext u2(other, 0);
    ctx.check(gridAt(u2, 0).count == 5 && gridAt(u2, 8).count == 3, "grid restored in the other backpack");
});

ABP_REGISTER_SELFTEST("crafting.grid_persists", [](Context& ctx) {
    auto& storage = game::Storage::get();
    auto  u       = withCrafting(ctx);
    auto  uuid    = u.backpack().uuid();
    int   slot    = u.slot();
    setGrid(u, {{2, data("minecraft:diamond", 7)}});
    u = UpgradeContext(nullptr, 0);
    storage.unload(uuid);
    auto again = storage.find(uuid);
    ctx.require(again != nullptr, "reloaded");
    UpgradeContext u2(again, slot);
    ctx.check(gridAt(u2, 2).id == "minecraft:diamond" && gridAt(u2, 2).count == 7, "grid persisted with the backpack");
});

// SB has no limit on Crafting upgrades: two in one backpack keep separate grids, results and
// open-tab state (everything is keyed by upgrade slot).
ABP_REGISTER_SELFTEST("crafting.two_upgrades_independent", [](Context& ctx) {
    auto& player = ctx.player();
    clearPlayer(player);
    auto bp = ctx.createBackpack(Tier::Gold);
    int  s0 = ctx.installUpgrade(bp, std::string(kCraftingId), 0);
    int  s1 = ctx.installUpgrade(bp, std::string(kCraftingId), 1);
    ctx.require(s0 == 0 && s1 == 1, "two crafting upgrades installed");
    UpgradeContext a(bp, s0), b(bp, s1);
    setGrid(a, {});
    planks2x2(b, 2);
    ctx.check(resultId(a) == "<none>" && resultId(b) == "minecraft:crafting_table", "separate grids");
    NativeScreen screen(ctx, player, bp);
    ctx.check(screen.tabData(s0)["grid"][0].is_null() && screen.tabData(s1)["grid"][0].value("id", "") == "minecraft:oak_planks", "separate tab data");
    screen.tab(s1, {{"result", true}, {"button", 0}});
    ctx.check(idOf(cursor(player)) == "minecraft:crafting_table", "the second upgrade crafts");
    ctx.check(gridAt(b, 0).count == 1 && gridAt(a, 0).empty(), "only the second grid was used");
    setCursor(player, ItemStack::EMPTY_ITEM());
    feature::gui::setPlayerOption(player, feature::gui::kOpenTabFirst, nlohmann::json(true));
    screen.openTab(s0, *bp);
    ctx.give(player, game::ItemConv::make("minecraft:stick", 7), 9);
    screen.action({{"action", "click"}, {"area", "inventory"}, {"index", 9}, {"shift", true}});
    ctx.check(gridAt(a, 0).id == "minecraft:stick" && gridAt(a, 0).count == 7, "shift-click goes into the open (first) tab");
    ctx.check(gridAt(b, 2).empty() && gridAt(b, 0).id == "minecraft:oak_planks", "the second grid is untouched");
    feature::gui::setPlayerOption(player, feature::gui::kOpenTabFirst, std::nullopt);
    clearPlayer(player);
});

ABP_REGISTER_SELFTEST("crafting.shift_into_open_tab", [](Context& ctx) {
    auto& player = ctx.player();
    clearPlayer(player);
    auto u = withCrafting(ctx);
    setGrid(u, {});
    feature::gui::setPlayerOption(player, feature::gui::kOpenTabFirst, nlohmann::json(true));
    NativeScreen screen(ctx, player, u.backpackPtr());
    ctx.give(player, game::ItemConv::make("minecraft:oak_planks", 20), 9);
    // Tab closed: shift-click from the inventory goes into the backpack.
    screen.action({{"action", "click"}, {"area", "inventory"}, {"index", 9}, {"shift", true}});
    ctx.check(countIn(u.backpack(), "minecraft:oak_planks") == 20, "closed tab: into the storage");
    // Tab open + "Shift Click Into Open Tab First": into the grid.
    screen.openTab(u.slot(), u.backpack());
    ctx.give(player, game::ItemConv::make("minecraft:oak_planks", 12), 10);
    screen.action({{"action", "click"}, {"area", "inventory"}, {"index", 10}, {"shift", true}});
    ctx.check(gridAt(u, 0).count == 12, "open tab: into the first grid cell, got " + std::to_string(gridAt(u, 0).count));
    // Storage -> open tab first, too.
    int idx = -1;
    for (int i = 0; i < u.backpack().inventory().size(); ++i) {
        if (u.backpack().inventory().at(i).id == "minecraft:oak_planks") idx = i;
    }
    ctx.require(idx >= 0, "planks in the storage");
    screen.action({{"action", "click"}, {"area", "storage"}, {"index", idx}, {"shift", true}});
    ctx.check(gridAt(u, 0).count == 32, "storage shift-click merges into the grid, got " + std::to_string(gridAt(u, 0).count));
    ctx.check(countOnPlayer(player, "minecraft:oak_planks") == 0, "not into the inventory");
    // Setting off: the storage comes first again.
    feature::gui::setPlayerOption(player, feature::gui::kOpenTabFirst, nlohmann::json(false));
    ctx.give(player, game::ItemConv::make("minecraft:oak_planks", 5), 11);
    screen.action({{"action", "click"}, {"area", "inventory"}, {"index", 11}, {"shift", true}});
    ctx.check(gridAt(u, 0).count == 32 && countIn(u.backpack(), "minecraft:oak_planks") == 5, "setting off: storage first");
    // Closing the tab.
    screen.tab(u.slot(), {{"tabOpen", false}, {"uuid", u.backpack().uuid()}});
    ctx.check(!openStationTab(player, u.backpack()).has_value(), "tab closed");
    feature::gui::setPlayerOption(player, feature::gui::kOpenTabFirst, std::nullopt);
    clearPlayer(player);
});

} // namespace
} // namespace abp::upgrade::crafting
