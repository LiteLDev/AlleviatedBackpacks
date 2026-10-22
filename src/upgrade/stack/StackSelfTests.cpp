// Selftests for the Stack upgrades: `backpack selftest "stack.*"`.

#include <climits>
#include <set>
#include <string>
#include <vector>

#include "ll/api/service/Bedrock.h"

#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/PlayerUISlot.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/crafting/Recipe.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/GameType.h"

#include "api/BackpackCapabilities.h"
#include "core/StackMultiplier.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "game/Storage.h"
#include "selftest/SelfTest.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/stack/StackUpgrade.h"

namespace abp::upgrade::stack {
namespace {

using core::Tier;
using selftest::Context;
using Where  = ui::MenuManager::SlotAddress::Where;
using Action = ui::MenuManager::ActionType;

core::AccessContext const kInternal{core::Source::Internal};
core::AccessContext const kPlayer{core::Source::Player};

core::ItemStackData data(std::string_view id, int count) {
    auto d = game::ItemConv::toData(game::ItemConv::make(id, 1));
    d.count = count;
    return d;
}
core::ItemStackData upgradeItem(std::string_view id) { return data(itemIdOf(id), 1); }

int limitOf(game::Backpack& bp, std::string_view id) { return bp.inventory().slotLimit(0, data(id, 1)); }

long long countOf(game::Backpack& bp, std::string_view id) {
    return bp.inventory().count([&](core::ItemStackData const& s) { return s.id == id; });
}

ItemStack cursorOf(Player& p) { return p.mPlayerUIContainer->getItem(0); }
void      setCursor(Player& p, ItemStack const& item) { p.setPlayerUIItem(PlayerUISlot::CursorSelected, item, false); }

std::string slotItem(game::Backpack& bp, int slot) { return bp.data().upgrades[static_cast<std::size_t>(slot)].item.id; }

ABP_REGISTER_SELFTEST("stack.items_registered", [](Context& ctx) {
    for (auto const& t : tiers()) {
        auto item = game::ItemConv::make(itemIdOf(t.id), 1);
        ctx.check(!item.isNull(), "item not registered: " + itemIdOf(t.id));
        auto* u = upgrade::byId(t.id);
        ctx.require(u != nullptr, "upgrade not registered: " + std::string(t.id));
        ctx.check(u->info().itemId == itemIdOf(t.id), "item id of " + std::string(t.id));
        ctx.check(u->info().groupOrId() == "stack", "group of " + std::string(t.id));
        ctx.check(u->info().maxPerBackpack == upgrade::kUnlimitedPerBackpack, "limit only through maxUpgradesPerStorage");
        ctx.check(!u->info().toggleable, "stack upgrades can't be turned off");
    }
    for (auto const& c : conversions()) {
        ctx.check(!game::ItemConv::make(c.itemId, 1).isNull(), "item not registered: " + std::string(c.itemId));
        ctx.check(tierById(c.from) && tierById(c.to), "conversion tiers exist");
        ctx.check(multiplierOf(*tierById(c.to)) > multiplierOf(*tierById(c.from)), "conversions go up");
    }
});

ABP_REGISTER_SELFTEST("stack.recipes_loaded", [](Context& ctx) {
    auto level = ll::service::getLevel();
    ctx.require(level.has_value(), "level");
    std::set<std::string> ids;
    for (auto const& [tag, recipes] : *level->getRecipes().mRecipes) {
        for (auto const& [id, recipe] : recipes) {
            // SC upgrade_clear recipes (recipes/clear/) are counted elsewhere.
            if (id.starts_with("alleviatedbackpacks:stack_") && !id.ends_with("_clear")) ids.insert(id);
        }
    }
    std::vector<std::string> expected;
    for (auto const& t : tiers()) expected.push_back(itemIdOf(t.id));
    expected.push_back(itemIdOf("stack_tier_1") + "_from_starter");
    for (auto const& c : conversions()) expected.emplace_back(c.itemId);
    for (auto const& id : expected) ctx.check(ids.contains(id), "recipe not loaded: " + id);
    ctx.check(ids.size() == expected.size(), "recipe count " + std::to_string(ids.size()));
});

ABP_REGISTER_SELFTEST("stack.limits", [](Context& ctx) {
    auto bp = ctx.createBackpack(Tier::Leather);
    ctx.check(limitOf(*bp, "minecraft:stone") == 64, "base limit 64");
    ctx.installUpgrade(bp, "stack_tier_1");
    ctx.check(limitOf(*bp, "minecraft:stone") == 128, "tier 1: stone x2");
    ctx.check(limitOf(*bp, "minecraft:ender_pearl") == 32, "tier 1: ender pearls 16 x2");
    ctx.check(limitOf(*bp, "minecraft:iron_sword") == 2, "tier 1: unstackables stack (x2)");
    ctx.check(limitOf(*bp, "minecraft:bundle") == 1, "bundles are excluded (config)");
    auto r = bp->inventory().insert(data("minecraft:stone", 300), kPlayer);
    ctx.check(r.inserted == 300, "300 stone fit");
    ctx.check(bp->inventory().at(0).count == 128 && bp->inventory().at(1).count == 128 && bp->inventory().at(2).count == 44, "128/128/44 layout");
    // Starter tier: x1.5, rounded down per item.
    auto starter = ctx.createBackpack(Tier::Leather);
    ctx.installUpgrade(starter, "stack_starter");
    ctx.check(limitOf(*starter, "minecraft:stone") == 96, "starter: 96");
    ctx.check(limitOf(*starter, "minecraft:ender_pearl") == 24, "starter: 24 pearls");
    ctx.check(limitOf(*starter, "minecraft:iron_sword") == 1, "starter: swords stay at 1");
});

ABP_REGISTER_SELFTEST("stack.multiply_and_max", [](Context& ctx) {
    auto bp = ctx.createBackpack(Tier::Iron); // 2 upgrade slots
    ctx.installUpgrade(bp, "stack_starter");
    ctx.installUpgrade(bp, "stack_tier_2");
    ctx.check(limitOf(*bp, "minecraft:stone") == 384, "1.5 x 4 = x6 -> 384");
    ctx.check(limitOf(*bp, "minecraft:iron_sword") == 6, "swords x6");
    auto big = ctx.createBackpack(Tier::Netherite); // 7 upgrade slots
    ctx.installUpgrade(big, "stack_tier_1");
    ctx.installUpgrade(big, "stack_tier_1");
    ctx.installUpgrade(big, "stack_downgrade_tier_1");
    ctx.check(limitOf(*big, "minecraft:stone") == 32, "2 x 2 x 1/8 -> 32");
    std::string reason;
    ctx.check(!upgrade::canInstall(big, 3, upgradeItem("stack_tier_4"), nullptr, &reason), "a 4th stack upgrade is refused");
    ctx.check(!reason.empty(), "refusal has a reason");
});

ABP_REGISTER_SELFTEST("stack.downgrade", [](Context& ctx) {
    auto bp = ctx.createBackpack(Tier::Leather);
    bp->inventory().insert(data("minecraft:stone", 64), kInternal);
    std::string reason;
    ctx.check(!upgrade::install(bp, 0, upgradeItem("stack_downgrade_tier_1"), nullptr, &reason), "downgrade refused while a slot holds 64");
    ctx.check(reason.find('8') != std::string::npos, "reason names the new limit 8: " + reason);
    bp->inventory().extract(0, 56, kInternal);
    ctx.require(upgrade::install(bp, 0, upgradeItem("stack_downgrade_tier_1"), nullptr, &reason), "install downgrade: " + reason);
    ctx.check(limitOf(*bp, "minecraft:stone") == 8, "downgrade 1: 8");
    ctx.check(limitOf(*bp, "minecraft:ender_pearl") == 2, "downgrade 1: 2 pearls");
    ctx.check(limitOf(*bp, "minecraft:iron_sword") == 1, "never below 1");
    bp->inventory().insert(data("minecraft:stone", 12), kInternal);
    ctx.check(bp->inventory().at(0).count == 8 && bp->inventory().at(1).count == 8 && bp->inventory().at(2).count == 4, "8 per slot");
    // Removing a downgrade only raises the limit: always allowed.
    ctx.check(upgrade::remove(bp, 0, nullptr).has_value(), "downgrade removable");
});

ABP_REGISTER_SELFTEST("stack.omega", [](Context& ctx) {
    auto& storage = game::Storage::get();
    auto  bp      = ctx.createBackpack(Tier::Leather);
    ctx.installUpgrade(bp, "stack_omega");
    ctx.check(limitOf(*bp, "minecraft:stone") == INT_MAX, "omega: INT_MAX");
    auto r = bp->inventory().insert(data("minecraft:stone", 1000000), kPlayer);
    ctx.check(r.inserted == 1000000 && bp->inventory().at(0).count == 1000000, "a million stone in one slot");
    bp->inventory().set(1, data("minecraft:stone", INT_MAX - 5));
    bp->inventory().sort([](core::ItemStackData const& a, core::ItemStackData const& b) { return a.id < b.id; });
    ctx.check(countOf(*bp, "minecraft:stone") == 1000000LL + INT_MAX - 5, "sort keeps every item (no overflow)");
    auto uuid = bp->uuid();
    bp->markDirty();
    bp.reset();
    storage.unload(uuid);
    auto again = storage.find(uuid);
    ctx.require(again != nullptr, "reload");
    ctx.check(countOf(*again, "minecraft:stone") == 1000000LL + INT_MAX - 5, "huge counts persist");
    ctx.check(limitOf(*again, "minecraft:stone") == INT_MAX, "limit restored after reload");
    std::string reason;
    ctx.check(!upgrade::remove(again, 0, nullptr, &reason).has_value(), "omega can't be removed while overfull");
});

ABP_REGISTER_SELFTEST("stack.remove_rules", [](Context& ctx) {
    auto bp = ctx.createBackpack(Tier::Iron);
    ctx.installUpgrade(bp, "stack_starter", 0);
    ctx.installUpgrade(bp, "stack_tier_1", 1);
    ctx.check(limitOf(*bp, "minecraft:stone") == 192, "x3 -> 192");
    bp->inventory().set(0, data("minecraft:stone", 150));
    std::string reason;
    ctx.check(!upgrade::remove(bp, 0, nullptr, &reason).has_value(), "removing starter (-> 128) refused with 150");
    ctx.check(!reason.empty(), "reason given");
    ctx.check(!upgrade::remove(bp, 1, nullptr).has_value(), "removing tier 1 (-> 96) refused");
    bp->inventory().extract(0, 30, kInternal); // 120
    ctx.check(!upgrade::remove(bp, 1, nullptr).has_value(), "removing tier 1 still refused at 120");
    ctx.check(upgrade::remove(bp, 0, nullptr).has_value(), "removing starter allowed at 120 (<= 128)");
    ctx.check(limitOf(*bp, "minecraft:stone") == 128, "now x2");
    // replace(): a lower tier that doesn't fit leaves everything as it was; non-upgrades are refused.
    bp->inventory().set(0, data("minecraft:stone", 120));
    ctx.check(!upgrade::replace(bp, 1, upgradeItem("stack_starter"), nullptr, &reason).has_value(), "replace with starter (96) refused");
    ctx.check(slotItem(*bp, 1) == itemIdOf("stack_tier_1") && limitOf(*bp, "minecraft:stone") == 128, "replace failure is a no-op");
    ctx.check(!upgrade::replace(bp, 1, data("minecraft:stone", 1), nullptr).has_value(), "replace with a non-upgrade refused");
    ctx.check(slotItem(*bp, 1) == itemIdOf("stack_tier_1"), "still tier 1");
    auto old = upgrade::replace(bp, 1, upgradeItem("stack_tier_3"), nullptr, &reason);
    ctx.check(old && old->id == itemIdOf("stack_tier_1") && limitOf(*bp, "minecraft:stone") == 512, "replace with tier 3");
    ctx.check(upgrade::replace(bp, 1, upgradeItem("stack_tier_1"), nullptr).has_value(), "and back to tier 1");
    // Unstackables count too: 2 swords need x2.
    bp->inventory().set(0, {});
    bp->inventory().set(1, data("minecraft:iron_sword", 2));
    ctx.check(!upgrade::remove(bp, 1, nullptr).has_value(), "2 stacked swords block removal");
    bp->inventory().extract(1, 1, kInternal);
    ctx.check(upgrade::remove(bp, 1, nullptr).has_value(), "1 sword: removal allowed");
    ctx.check(limitOf(*bp, "minecraft:stone") == 64, "back to 64");
});

ABP_REGISTER_SELFTEST("stack.gui_swap_and_convert", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(Tier::Leather);
    ctx.installUpgrade(bp, "stack_tier_1");
    bp->inventory().set(0, data("minecraft:stone", 100));
    ctx.require(ui::BackpackMenu::openBackpack(player, bp, "Stack", ui::BackpackMenu::View::Upgrades), "open upgrades view");
    mm.forceShown(player);
    co_await ctx.ticks(2);
    mm.forceShown(player);

    // Swap tier 1 for tier 2 while a slot holds more than the base limit (SB allows higher tiers).
    setCursor(player, game::ItemConv::make(itemIdOf("stack_tier_2"), 1));
    // Handled swaps are "button presses" for the framework (the client is re-synced): check state.
    (void)mm.simulate(player, {Action::Swap, {Where::Menu, 0}, {Where::Cursor, 0}, 1});
    ctx.check(slotItem(*bp, 0) == itemIdOf("stack_tier_2"), "tier 2 installed");
    ctx.check(game::ItemConv::idOf(cursorOf(player)) == itemIdOf("stack_tier_1"), "tier 1 on the cursor");
    ctx.check(limitOf(*bp, "minecraft:stone") == 256, "limit 256");
    // Back to tier 1 (128 >= 100): allowed.
    (void)mm.simulate(player, {Action::Swap, {Where::Menu, 0}, {Where::Cursor, 0}, 1});
    ctx.check(slotItem(*bp, 0) == itemIdOf("stack_tier_1"), "tier 1 installed again");
    // Starter would give 96 < 100: refused, nothing changes.
    setCursor(player, game::ItemConv::make(itemIdOf("stack_starter"), 1));
    ctx.check(!mm.simulate(player, {Action::Swap, {Where::Menu, 0}, {Where::Cursor, 0}, 1}), "swap to starter refused");
    ctx.check(slotItem(*bp, 0) == itemIdOf("stack_tier_1"), "tier 1 still installed");
    ctx.check(game::ItemConv::idOf(cursorOf(player)) == itemIdOf("stack_starter"), "starter still on the cursor");
    ctx.check(limitOf(*bp, "minecraft:stone") == 128, "limit still 128");
    // Two tier 2 upgrades on the cursor: one goes in, tier 1 goes to the inventory.
    for (int i = 9; i < 36; ++i) player.getInventory().setItem(i, ItemStack{});
    setCursor(player, game::ItemConv::make(itemIdOf("stack_tier_2"), 2));
    (void)mm.simulate(player, {Action::Swap, {Where::Menu, 0}, {Where::Cursor, 0}, 2});
    ctx.check(slotItem(*bp, 0) == itemIdOf("stack_tier_2"), "stacked cursor: tier 2 installed");
    ctx.check(cursorOf(player).mCount == 1, "stacked cursor: one tier 2 left on the cursor");
    bool tier1InInventory = false;
    for (int i = 0; i < 36; ++i) {
        if (game::ItemConv::idOf(player.getInventory().getItem(i)) == itemIdOf("stack_tier_1")) tier1InInventory = true;
    }
    ctx.check(tier1InInventory, "old tier 1 went to the inventory");
    std::string why;
    ctx.require(upgrade::replace(bp, 0, upgradeItem("stack_tier_1"), &player, &why).has_value(), "replace back: " + why);
    // Taking the upgrade out (-> 64) is refused too.
    setCursor(player, ItemStack{});
    ctx.check(!mm.simulate(player, {Action::Take, {Where::Menu, 0}, {Where::Cursor, 0}, 1}), "removal refused");
    ctx.check(cursorOf(player).isNull() && slotItem(*bp, 0) == itemIdOf("stack_tier_1"), "nothing moved");

    // A conversion item for another tier does nothing (and is not used up).
    setCursor(player, game::ItemConv::make("alleviatedbackpacks:stack_conversion_starter_to_tier_2", 1));
    (void)mm.simulate(player, {Action::Swap, {Where::Menu, 0}, {Where::Cursor, 0}, 1});
    ctx.check(slotItem(*bp, 0) == itemIdOf("stack_tier_1"), "wrong conversion: unchanged");
    ctx.check(cursorOf(player).mCount == 1, "wrong conversion: not consumed");
    // The matching one converts in place and uses one item.
    setCursor(player, game::ItemConv::make("alleviatedbackpacks:stack_conversion_tier_1_to_tier_3", 2));
    (void)mm.simulate(player, {Action::Swap, {Where::Menu, 0}, {Where::Cursor, 0}, 1});
    ctx.check(slotItem(*bp, 0) == itemIdOf("stack_tier_3"), "converted to tier 3");
    ctx.check(cursorOf(player).mCount == 1, "one conversion item used");
    ctx.check(limitOf(*bp, "minecraft:stone") == 512, "limit 512");
    ctx.check(countOf(*bp, "minecraft:stone") == 100, "contents untouched");
    setCursor(player, ItemStack{});
    mm.close(player);
});

ABP_REGISTER_SELFTEST("stack.gui_counts_and_split", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(Tier::Leather);
    ctx.installUpgrade(bp, "stack_tier_3"); // x8
    bp->inventory().set(0, data("minecraft:stone", 300));
    bp->inventory().set(1, data("minecraft:iron_sword", 5));
    bp->inventory().set(2, data("minecraft:dirt", 1500));
    ctx.require(ctx.give(player, ctx.backpackItem(*bp), 0) == 0, "give backpack");
    ctx.require(ui::BackpackMenu::openFor(player, {game::ItemLocation::Kind::Inventory, 0}), "open");
    mm.forceShown(player);
    co_await ctx.ticks(3);

    auto shown = mm.shownItem(player, 0);
    ctx.check(shown.mCount == 64, "300 stone shown as 64, got " + std::to_string(shown.mCount));
    auto lore = shown.getCustomLore();
    ctx.check(!lore.empty() && lore[0].find("300") != std::string::npos, "lore shows the real count");
    ctx.check(mm.shownItem(player, 1).mCount == 1, "5 swords shown as 1");
    auto dirtLore = mm.shownItem(player, 2).getCustomLore();
    ctx.check(!dirtLore.empty() && dirtLore[0].find("1500") != std::string::npos, "lore shows exact 1500");

    // Taking gives at most one vanilla stack.
    ctx.check(mm.simulate(player, {Action::Take, {Where::Menu, 0}, {Where::Cursor, 0}, 200}), "take to cursor");
    ctx.check(cursorOf(player).mCount == 64, "cursor holds 64");
    ctx.check(bp->inventory().at(0).count == 236, "236 left");
    // Shift-click out: one stack per click.
    ctx.check(mm.simulate(player, {Action::Take, {Where::Menu, 0}, {Where::Inventory, 5}, 64}), "quick move out");
    ctx.check(player.getInventory().getItem(5).mCount == 64, "64 in the inventory");
    ctx.check(bp->inventory().at(0).count == 172, "172 left");
    // Place the cursor back onto the big slot: merges beyond 64.
    ctx.check(mm.simulate(player, {Action::Place, {Where::Cursor, 0}, {Where::Menu, 0}, 64}), "place back");
    ctx.check(bp->inventory().at(0).count == 236 && cursorOf(player).isNull(), "merged to 236");
    // Shift-click in merges into the big slot too.
    ctx.check(mm.simulate(player, {Action::Place, {Where::Inventory, 5}, {Where::Menu, 10}, 64}), "quick move in");
    ctx.check(bp->inventory().at(0).count == 300, "back to 300");
    // Swords come out one at a time.
    ctx.check(mm.simulate(player, {Action::Take, {Where::Menu, 1}, {Where::Cursor, 0}, 5}), "take sword");
    ctx.check(cursorOf(player).mCount == 1 && bp->inventory().at(1).count == 4, "one sword taken");
    co_await ctx.ticks(2);
    ctx.check(mm.shownItem(player, 0).mCount == 64, "display still capped at 64");
    mm.close(player);
    ctx.check(countOf(*bp, "minecraft:stone") == 300, "no stone lost or duplicated");
});

ABP_REGISTER_SELFTEST("stack.sc_limits", [](Context& ctx) {
    // SC InventoryHandler.getBaseStackLimit with the slot limit computed first.
    auto bp = ctx.createBackpack(Tier::Iron);
    ctx.installUpgrade(bp, "stack_starter", 0); // 96
    ctx.check(limitOf(*bp, "minecraft:snowball") == 24, "starter: 16 * 1 + 32 * 16 / 64 = 24");
    ctx.check(limitOf(*bp, "minecraft:diamond_sword") == 1, "starter: unstackables 1 * 1 + 32 / 64 = 1");
    ctx.installUpgrade(bp, "stack_downgrade_tier_3", 1); // 1.5 / 32 -> slot limit 3
    ctx.check(limitOf(*bp, "minecraft:stone") == 3, "1.5 / 32 -> 3");
    ctx.check(limitOf(*bp, "minecraft:snowball") == 1, "max(1, (int) (16 * 3 / 64.0)) = 1");
    // Shulker boxes and bundles keep their size (SC nonStackableItems).
    auto big = ctx.createBackpack(Tier::Leather);
    ctx.installUpgrade(big, "stack_tier_4");
    ctx.check(limitOf(*big, "minecraft:red_shulker_box") == 1, "shulker boxes stay at 1");
    ctx.check(limitOf(*big, "minecraft:undyed_shulker_box") == 1, "undyed shulker box stays at 1");
    ctx.check(limitOf(*big, "minecraft:iron_sword") == 16, "swords x16");
    ctx.check(limitOf(*big, "minecraft:stone") == 1024, "stone x16");
});

ABP_REGISTER_SELFTEST("stack.error_text", [](Context& ctx) {
    auto bp = ctx.createBackpack(Tier::Leather);
    ctx.installUpgrade(bp, "stack_tier_1");
    bp->inventory().set(0, data("minecraft:stone", 100));
    std::string reason;
    ctx.check(!upgrade::remove(bp, 0, nullptr, &reason).has_value(), "removal refused");
    ctx.check(reason == "There's at least one slot or upgrade that requires stack limit higher than 64", "SB text: " + reason);
});

ABP_REGISTER_SELFTEST("stack.tank_needs_multiplier", [](Context& ctx) {
    auto bp = ctx.createBackpack(Tier::Iron); // 2 upgrade slots
    ctx.installUpgrade(bp, "stack_tier_1", 0);
    ctx.installUpgrade(bp, "tank", 1);
    auto tank = api::backpack::tankOf(UpgradeContext(bp, 1));
    ctx.require(tank != nullptr, "tank handler");
    int cap = tank->tankCapacity(0);
    ctx.require(cap > 0, "tank capacity");
    // More than the base capacity (the capacity without the stack upgrade): x2 needed.
    // The tank moves at most its IO rate per call: fill in steps.
    int target = cap / 2 + 1000, filled = 0;
    for (int guard = 0; filled < target && guard < 10000; ++guard) {
        int n = tank->fill(api::FluidStack{"minecraft:water", target - filled}, false);
        if (n <= 0) break;
        filled += n;
    }
    ctx.require(filled == target, "filled " + std::to_string(filled));
    ctx.check(minimumMultiplierRequired(bp, 1) == 2, "tank needs x2, got " + std::to_string(minimumMultiplierRequired(bp, 1)));
    std::string reason;
    ctx.check(!upgrade::remove(bp, 0, nullptr, &reason).has_value(), "stack upgrade can't go while the tank needs it");
    ctx.check(reason.find("64") != std::string::npos, "reason: " + reason);
    for (int drained = 0, guard = 0; drained < 2000 && guard < 10000; ++guard) {
        int n = tank->drainAny(2000 - drained, false).amount;
        if (n <= 0) break;
        drained += n;
    }
    ctx.check(minimumMultiplierRequired(bp, 1) == 1, "within the base capacity");
    ctx.check(upgrade::remove(bp, 0, nullptr, &reason).has_value(), "removable now: " + reason);
});

ABP_REGISTER_SELFTEST("stack.convert_creative", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(Tier::Leather);
    ctx.installUpgrade(bp, "stack_starter");
    ctx.require(ui::BackpackMenu::openBackpack(player, bp, "Stack", ui::BackpackMenu::View::Upgrades), "open upgrades view");
    mm.forceShown(player);
    co_await ctx.ticks(2);
    mm.forceShown(player);
    player.setPlayerGameType(GameType::Creative);
    setCursor(player, game::ItemConv::make("alleviatedbackpacks:stack_conversion_starter_to_tier_4", 1));
    (void)mm.simulate(player, {Action::Swap, {Where::Menu, 0}, {Where::Cursor, 0}, 1});
    ctx.check(slotItem(*bp, 0) == itemIdOf("stack_tier_4"), "converted to tier 4");
    ctx.check(cursorOf(player).mCount == 1, "creative: conversion item kept");
    player.setPlayerGameType(GameType::Survival);
    setCursor(player, ItemStack{});
    mm.close(player);
});

} // namespace
} // namespace abp::upgrade::stack
