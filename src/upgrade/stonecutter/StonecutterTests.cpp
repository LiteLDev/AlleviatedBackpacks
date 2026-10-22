// Selftests for the Stonecutter upgrade: stonecutter.*
// Run in-game with `/backpack selftest "stonecutter.*"`. UI behaviour is driven through the
// native tab's actions (the same packets the client companion sends).

#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "feature/gui/Gui.h"
#include "game/ItemConv.h"
#include "selftest/SelfTest.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/crafting/Station.h"
#include "upgrade/crafting/StationTestSupport.h"
#include "upgrade/stonecutter/RecentResults.h"
#include "upgrade/stonecutter/StonecutterUpgrade.h"

namespace abp::upgrade::stonecutter {
namespace {

using core::Tier;
using selftest::Context;
using namespace crafting::test;

std::string idOf(ItemStack const& s) { return s.isNull() ? std::string("<empty>") : s.getTypeName(); }

UpgradeContext withStonecutter(Context& ctx) {
    auto bp   = ctx.createBackpack(Tier::Iron);
    int  slot = ctx.installUpgrade(bp, std::string(crafting::kStonecutterId));
    return UpgradeContext(bp, slot);
}

/// Index of the option producing `resultId` (-1 if none).
int optionIndex(UpgradeContext const& u, std::string_view resultId) {
    auto const& list = options(u);
    for (int i = 0; i < static_cast<int>(list.size()); ++i) {
        if (idOf(list[static_cast<std::size_t>(i)].result) == resultId) return i;
    }
    return -1;
}

ABP_REGISTER_SELFTEST("stonecutter.options", [](Context& ctx) {
    auto u = withStonecutter(ctx);
    auto* type = upgrade::byId(crafting::kStonecutterId);
    ctx.require(type != nullptr, "registered");
    ctx.check(!type->info().toggleable, "SB: no on/off switch");
    ctx.check(shiftIntoStorage(u) && !refillInput(u), "SB defaults: shift into the backpack, no refill");
    ctx.check(options(u).empty(), "no input, no options");
    setInput(u, data("minecraft:stone", 1));
    auto list = options(u);
    ctx.check(list.size() >= 3, "stone has several outputs, got " + std::to_string(list.size()));
    ctx.check(optionIndex(u, "minecraft:stone_bricks") >= 0, "stone -> stone bricks offered");
    setInput(u, data("minecraft:dirt", 1));
    ctx.check(options(u).empty(), "dirt has no stonecutter outputs");
});

ABP_REGISTER_SELFTEST("stonecutter.selection_is_stored", [](Context& ctx) {
    auto u = withStonecutter(ctx);
    setInput(u, data("minecraft:stone", 4));
    int bricks = optionIndex(u, "minecraft:stone_bricks");
    ctx.require(bricks >= 0 && selectRecipe(u, bricks), "select bricks");
    ctx.check(!selectedId(u).empty(), "the recipe id is stored on the upgrade");
    setInput(u, data("minecraft:oak_log", 4));
    ctx.check(!selectedOption(u).has_value() && selectedIndex(u) == -1, "nothing selected for logs");
    ctx.check(craftOnce(u, ctx.player()).isNull(), "nothing crafted without a valid selection");
    ctx.check(inputOf(u).count == 4, "input untouched");
    setInput(u, data("minecraft:stone", 2));
    ctx.check(selectedIndex(u) == optionIndex(u, "minecraft:stone_bricks"), "SC: the stored recipe is selected again for stone");
});

ABP_REGISTER_SELFTEST("stonecutter.tab_clicks", [](Context& ctx) {
    auto& player = ctx.player();
    clearPlayer(player);
    auto u = withStonecutter(ctx);
    NativeScreen screen(ctx, player, u.backpackPtr());
    setCursor(player, game::ItemConv::make("minecraft:stone", 3));
    screen.tab(u.slot(), {{"input", true}, {"button", 0}});
    ctx.check(inputOf(u).count == 3 && cursor(player).isNull(), "stone placed into the input");
    auto tab = screen.tabData(u.slot());
    ctx.check(tab["recipes"].size() == options(u).size() && tab.value("selected", 0) == -1, "recipes listed, none selected: " + tab.dump());
    int bricks = optionIndex(u, "minecraft:stone_bricks");
    screen.tab(u.slot(), {{"recipe", bricks}});
    ctx.check(screen.tabData(u.slot()).value("selected", -1) == bricks, "recipe selected");
    ctx.check(screen.tabData(u.slot())["output"].value("id", "") == "minecraft:stone_bricks", "output shows the result");
    screen.tab(u.slot(), {{"output", true}, {"button", 0}});
    ctx.check(idOf(cursor(player)) == "minecraft:stone_bricks" && inputOf(u).count == 2, "one cut onto the cursor");
    screen.tab(u.slot(), {{"output", true}, {"button", 0}});
    ctx.check(cursor(player).mCount == 2 && inputOf(u).count == 1, "a second click stacks on the cursor");
    setCursor(player, ItemStack::EMPTY_ITEM());
    // Recent results come first for this player (SC RecentCraftedResultStorage).
    auto recent = screen.tabData(u.slot())["recent"];
    ctx.check(recent.is_array() && recent[static_cast<std::size_t>(bricks)] == 0, "bricks is the most recent result: " + recent.dump());
});

ABP_REGISTER_SELFTEST("stonecutter.shift_output", [](Context& ctx) {
    auto& player = ctx.player();
    clearPlayer(player);
    auto u = withStonecutter(ctx);
    setInput(u, data("minecraft:stone", 10));
    ctx.require(selectRecipe(u, optionIndex(u, "minecraft:stone_bricks")), "select bricks");
    NativeScreen screen(ctx, player, u.backpackPtr());
    screen.tab(u.slot(), {{"output", true}, {"shift", true}});
    ctx.check(countIn(u.backpack(), "minecraft:stone_bricks") == 10 && inputOf(u).empty(), "all cut into the backpack");
    setInput(u, data("minecraft:stone", 4));
    ctx.require(selectedIndex(u) >= 0, "selected again for new stone");
    screen.tab(u.slot(), {{"toggle", "shiftTarget"}});
    screen.tab(u.slot(), {{"output", true}, {"shift", true}});
    ctx.check(countOnPlayer(player, "minecraft:stone_bricks") == 4, "\"Into Player's Inventory\": 4 bricks on the player");
    clearPlayer(player);
});

ABP_REGISTER_SELFTEST("stonecutter.refill_input", [](Context& ctx) {
    auto& player = ctx.player();
    clearPlayer(player);
    auto u = withStonecutter(ctx);
    setRefillInput(u, true);
    u.backpack().inventory().insert(data("minecraft:stone", 3), core::AccessContext{core::Source::Internal});
    setInput(u, data("minecraft:stone", 1));
    ctx.require(selectRecipe(u, optionIndex(u, "minecraft:stone_bricks")), "select bricks");
    craftOnce(u, player);
    ctx.check(inputOf(u).count == 1 && countIn(u.backpack(), "minecraft:stone") == 2, "SC: the input is refilled from the backpack");
    int crafts = craftShift(u, player);
    ctx.check(crafts == 3 && inputOf(u).empty(), "shift: the rest, got " + std::to_string(crafts));
});

ABP_REGISTER_SELFTEST("stonecutter.input_travels_with_item", [](Context& ctx) {
    auto& player = ctx.player();
    auto  u      = withStonecutter(ctx);
    auto  bp     = u.backpackPtr();
    setInput(u, data("minecraft:stone", 5));
    ctx.require(selectRecipe(u, optionIndex(u, "minecraft:stone_bricks")), "select");
    auto item = upgrade::remove(bp, u.slot(), &player);
    ctx.require(item.has_value(), "removed");
    ctx.check(countIn(*bp, "minecraft:stone") == 0, "SB: the input stays in the upgrade item");
    auto        other = ctx.createBackpack(Tier::Iron);
    std::string reason;
    ctx.require(upgrade::install(other, 0, *item, &player, &reason), "reinstall: " + reason);
    UpgradeContext u2(other, 0);
    ctx.check(inputOf(u2).count == 5 && selectedOption(u2).has_value(), "input and recipe restored");
});

ABP_REGISTER_SELFTEST("stonecutter.shift_into_open_tab", [](Context& ctx) {
    auto& player = ctx.player();
    clearPlayer(player);
    auto u = withStonecutter(ctx);
    feature::gui::setPlayerOption(player, feature::gui::kOpenTabFirst, nlohmann::json(true));
    NativeScreen screen(ctx, player, u.backpackPtr());
    screen.openTab(u.slot(), u.backpack());
    ctx.give(player, game::ItemConv::make("minecraft:stone", 16), 9);
    screen.action({{"action", "click"}, {"area", "inventory"}, {"index", 9}, {"shift", true}});
    ctx.check(inputOf(u).count == 16, "open tab: into the input");
    ctx.give(player, game::ItemConv::make("minecraft:dirt", 4), 10);
    screen.action({{"action", "click"}, {"area", "inventory"}, {"index", 10}, {"shift", true}});
    ctx.check(countIn(u.backpack(), "minecraft:dirt") == 4, "another item: into the storage");
    screen.tab(u.slot(), {{"input", true}, {"shift", true}});
    ctx.check(inputOf(u).empty() && countIn(u.backpack(), "minecraft:stone") == 16, "input shift-click: into the backpack");
    feature::gui::setPlayerOption(player, feature::gui::kOpenTabFirst, std::nullopt);
    clearPlayer(player);
});

ABP_REGISTER_SELFTEST("stonecutter.recent_results", [](Context& ctx) {
    auto& p = ctx.player();
    std::string ing = "abp:selftest_ingredient";
    for (auto const* r : {"a", "b", "c", "d", "e", "b"}) recordResult(p, kRecentScope, ing, r);
    auto list = recentResults(p, kRecentScope, ing);
    ctx.check(list.size() == 4 && list[0] == "b" && list[1] == "e" && list[2] == "d" && list[3] == "c", "4 most recent, newest first");
    ctx.check(!recordResult(p, kRecentScope, ing, "b"), "the latest again changes nothing");
});

} // namespace
} // namespace abp::upgrade::stonecutter
