// Selftests for the anvil upgrade: anvil.*

#include <format>

#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"
#include "mc/world/item/enchanting/ItemEnchants.h"

#include "game/ItemConv.h"
#include "selftest/SelfTest.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/anvil/AnvilLogic.h"
#include "upgrade/anvil/AnvilUpgrade.h"

namespace abp::upgrade::anvil {
namespace {

using selftest::Context;
using Where  = ui::MenuManager::SlotAddress::Where;
using Action = ui::MenuManager::ActionType;

ItemStack make(std::string_view id, int count = 1) { return game::ItemConv::make(id, count); }

ItemStack withEnchant(ItemStack s, Enchant::Type type, int level) {
    EnchantmentInstance inst;
    inst.mEnchantType = type;
    inst.mLevel       = level;
    (void)EnchantUtils::applyEnchant(s, inst, true);
    return s;
}

ItemStack book(Enchant::Type type, int level) { return withEnchant(make("minecraft:enchanted_book"), type, level); }

ItemStack damaged(std::string_view id, int damage) {
    auto s = make(id);
    s.setDamageValue(static_cast<short>(damage));
    return s;
}

int enchantLevel(ItemStack const& s, Enchant::Type type) { return EnchantUtils::getEnchantLevel(type, s); }

ItemStack cursorOf(Player& p) { return p.mPlayerUIContainer->getItem(0); }

void setLevel(Player& p, int level) { p.addLevels(level - playerLevel(p)); }

std::string describe(AnvilOutcome const& o) {
    return std::format(
        "result={} cost={} tooExpensive={} rightUsed={}",
        o.result.isNull() ? "<none>" : o.result.getTypeName(),
        o.cost,
        o.tooExpensive,
        o.rightUsed
    );
}

// ---- logic ------------------------------------------------------------------------------------

ABP_REGISTER_SELFTEST("anvil.repair_with_material", [](Context& ctx) {
    auto sword = damaged("minecraft:diamond_sword", 1000);
    auto o     = compute(sword, make("minecraft:diamond", 5), std::nullopt, false);
    ctx.require(o.hasResult(), "diamonds repair a diamond sword: " + describe(o));
    ctx.check(o.result.getDamageValue() == 0, "fully repaired after 3 diamonds, damage " + std::to_string(o.result.getDamageValue()));
    ctx.check(o.rightUsed == 3, "3 diamonds used, got " + std::to_string(o.rightUsed));
    ctx.check(o.cost == 3, "cost 3, got " + std::to_string(o.cost));
    ctx.check(o.result.getBaseRepairCost() == 1, "prior work penalty set to 1");
    auto one = compute(sword, make("minecraft:diamond", 1), std::nullopt, false);
    ctx.check(one.rightUsed == 1 && one.result.getDamageValue() == 610, "one diamond restores a quarter");
    ctx.check(!compute(sword, make("minecraft:iron_ingot", 5), std::nullopt, false).hasResult(), "wrong material refused");
    ctx.check(!compute(make("minecraft:diamond_sword"), make("minecraft:diamond", 1), std::nullopt, false).hasResult(), "undamaged item: nothing to repair");
});

ABP_REGISTER_SELFTEST("anvil.combine_items", [](Context& ctx) {
    // Durability: 250 - 200 = 50 left, 250 - 150 = 100 left, + 12% bonus (30) -> damage 70.
    auto a = withEnchant(damaged("minecraft:iron_sword", 200), Enchant::Type::Sharpness, 2);
    auto b = withEnchant(damaged("minecraft:iron_sword", 150), Enchant::Type::Sharpness, 2);
    auto o = compute(a, b, std::nullopt, false);
    ctx.require(o.hasResult(), "two swords combine: " + describe(o));
    ctx.check(o.result.getDamageValue() == 70, "combined durability, damage " + std::to_string(o.result.getDamageValue()));
    ctx.check(enchantLevel(o.result, Enchant::Type::Sharpness) == 3, "equal levels go up by one");
    ctx.check(o.cost > 2, "cost includes repair and enchantments: " + std::to_string(o.cost));
    ctx.check(o.rightUsed == 1, "the right item is consumed");
    // Max level is a cap.
    auto s5 = withEnchant(make("minecraft:diamond_sword"), Enchant::Type::Sharpness, 5);
    auto o5 = compute(s5, s5, std::nullopt, false);
    ctx.check(!o5.hasResult() || enchantLevel(o5.result, Enchant::Type::Sharpness) == 5, "sharpness stays at V");
    // A right item that adds nothing is not consumed by a rename.
    auto twin = compute(make("minecraft:diamond_sword"), make("minecraft:diamond_sword"), std::string("Twin"), false);
    ctx.check(twin.hasResult() && twin.renameOnly && twin.rightUsed == 0 && twin.cost == 1, "rename next to a useless item: " + describe(twin));
    // Different items don't combine.
    ctx.check(!compute(make("minecraft:diamond_sword"), make("minecraft:stone", 1), std::nullopt, false).hasResult(), "sword + stone refused");
    ctx.check(!compute(damaged("minecraft:diamond_sword", 50), damaged("minecraft:iron_sword", 5), std::nullopt, false).hasResult(), "different tools refused");
});

ABP_REGISTER_SELFTEST("anvil.enchanted_books", [](Context& ctx) {
    auto o = compute(make("minecraft:diamond_sword"), book(Enchant::Type::Sharpness, 3), std::nullopt, false);
    ctx.require(o.hasResult(), "book applies to a sword: " + describe(o));
    ctx.check(enchantLevel(o.result, Enchant::Type::Sharpness) == 3, "sharpness III applied");
    ctx.check(o.cost >= 1, "costs levels");
    // Incompatible enchantments (sharpness vs smite) and wrong item types are refused.
    auto sharp = withEnchant(make("minecraft:diamond_sword"), Enchant::Type::Sharpness, 3);
    ctx.check(!compute(sharp, book(Enchant::Type::Smite, 1), std::nullopt, false).hasResult(), "smite on sharpness refused");
    ctx.check(!compute(make("minecraft:diamond_pickaxe"), book(Enchant::Type::Sharpness, 1), std::nullopt, false).hasResult(), "sharpness on a pickaxe refused");
    // Compatible extra enchantment is added and keeps the existing one.
    auto both = compute(sharp, book(Enchant::Type::Unbreaking, 2), std::nullopt, false);
    ctx.check(both.hasResult() && enchantLevel(both.result, Enchant::Type::Unbreaking) == 2 && enchantLevel(both.result, Enchant::Type::Sharpness) == 3, "unbreaking added next to sharpness");
    // Book + book merges.
    auto books = compute(book(Enchant::Type::Sharpness, 1), book(Enchant::Type::Sharpness, 1), std::nullopt, false);
    ctx.check(books.hasResult() && enchantLevel(books.result, Enchant::Type::Sharpness) == 2, "two sharpness I books make II");
});

ABP_REGISTER_SELFTEST("anvil.rename_and_penalty", [](Context& ctx) {
    auto sword = make("minecraft:diamond_sword");
    auto o     = compute(sword, {}, std::string("Excalibur"), false);
    ctx.require(o.hasResult(), "rename gives a result");
    ctx.check(o.result.getCustomName() == "Excalibur", "named Excalibur");
    ctx.check(o.cost == 1 && o.renameOnly, "rename costs 1 level");
    ctx.check(o.result.getBaseRepairCost() == 0, "renaming adds no prior work penalty");
    ctx.check(!compute(sword, {}, std::nullopt, false).hasResult(), "no input change -> no result");
    // Removing a custom name.
    auto named  = o.result;
    auto remove = compute(named, {}, std::string(""), false);
    ctx.check(remove.hasResult() && remove.result.getCustomName().empty(), "blank name removes the custom name");
    ctx.check(!compute(named, {}, std::string("Excalibur"), false).hasResult(), "same name -> no result");
    // Prior work penalty adds to the cost.
    auto worked = sword;
    worked.setRepairCost(7);
    auto p = compute(worked, {}, std::string("X"), false);
    ctx.check(p.cost == 8, "rename with penalty 7 costs 8, got " + std::to_string(p.cost));
    auto r = compute(worked, book(Enchant::Type::Unbreaking, 1), std::nullopt, false);
    ctx.check(r.hasResult() && r.result.getBaseRepairCost() == 15, "penalty doubles + 1 after use");
    // Too expensive (survival) at 40+ levels; creative ignores it; rename-only is capped at 39.
    auto old = sword;
    old.setRepairCost(39);
    auto te = compute(old, book(Enchant::Type::Unbreaking, 3), std::nullopt, false);
    ctx.check(te.hasResult() && te.tooExpensive, "39 + work is too expensive: " + describe(te));
    ctx.check(!compute(old, book(Enchant::Type::Unbreaking, 3), std::nullopt, true).tooExpensive, "creative is never too expensive");
    auto ancient = sword;
    ancient.setRepairCost(63);
    auto capped = compute(ancient, {}, std::string("Old"), false);
    ctx.check(capped.cost == 39 && !capped.tooExpensive, "rename-only capped at 39: " + describe(capped));
});

// ---- menu -------------------------------------------------------------------------------------

struct Fixture {
    std::shared_ptr<game::Backpack> bp;
    int                             slot = -1;
};

Fixture openAnvil(Context& ctx, Player& player) {
    Fixture f;
    f.bp   = ctx.createBackpack(core::Tier::Leather);
    f.slot = ctx.installUpgrade(f.bp, "anvil");
    ctx.require(AnvilMenu::open(player, UpgradeContext(f.bp, f.slot)), "open anvil menu");
    ui::MenuManager::get().forceShown(player);
    return f;
}

bool place(Player& player, int invSlot, int menuSlot, int count) {
    auto& mm = ui::MenuManager::get();
    if (!mm.simulate(player, {Action::Take, {Where::Inventory, invSlot}, {Where::Cursor, 0}, count})) return false;
    return mm.simulate(player, {Action::Place, {Where::Cursor, 0}, {Where::Menu, menuSlot}, count});
}

ABP_REGISTER_SELFTEST("anvil.menu_take_result", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto  f      = openAnvil(ctx, player);
    ctx.require(ctx.give(player, damaged("minecraft:diamond_sword", 1000), 0) == 0, "give sword");
    ctx.require(ctx.give(player, make("minecraft:diamond", 5), 1) == 1, "give diamonds");
    ctx.require(place(player, 0, AnvilMenu::kLeftSlot, 1), "sword into the left slot");
    ctx.require(place(player, 1, AnvilMenu::kRightSlot, 5), "diamonds into the right slot");
    co_await ctx.ticks(2);
    auto shown = mm.shownItem(player, AnvilMenu::kResultSlot);
    ctx.check(!shown.isNull() && shown.getTypeName() == "minecraft:diamond_sword", "result shown");
    ctx.check(mm.shownItem(player, AnvilMenu::kCostSlot).getTypeName() == "minecraft:experience_bottle", "cost shown");

    // Not enough levels: nothing happens.
    setLevel(player, 0);
    ctx.check(!mm.simulate(player, {Action::Take, {Where::Menu, AnvilMenu::kResultSlot}, {Where::Cursor, 0}, 1}), "can't take without levels");
    ctx.check(cursorOf(player).isNull(), "cursor still empty");

    // With levels: take it, pay 3 levels, consume the sword and 3 diamonds.
    setLevel(player, 10);
    ctx.check(mm.simulate(player, {Action::Take, {Where::Menu, AnvilMenu::kResultSlot}, {Where::Cursor, 0}, 1}), "take result");
    auto got = cursorOf(player);
    ctx.check(!got.isNull() && got.getDamageValue() == 0, "repaired sword on the cursor");
    ctx.check(playerLevel(player) == 7, "paid 3 levels, level now " + std::to_string(playerLevel(player)));
    ctx.check(f.bp->data().upgrades[static_cast<std::size_t>(f.slot)].settings.value("left", nlohmann::json()).is_null(), "left input consumed");
    auto menu = std::dynamic_pointer_cast<AnvilMenu>(mm.current(player));
    ctx.require(menu != nullptr, "anvil menu still open");
    ctx.check(menu->ref().item("right").count == 2, "2 diamonds left");
    // The upgrade never breaks: still installed.
    ctx.check(hasActive(*f.bp, "anvil"), "anvil upgrade still installed");
    mm.close(player);
    ctx.check(cursorOf(player).isNull(), "cursor returned on close");
});

ABP_REGISTER_SELFTEST("anvil.menu_undo_when_blocked", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto  f      = openAnvil(ctx, player);
    ctx.require(ctx.give(player, make("minecraft:diamond_sword"), 0) == 0, "give sword");
    ctx.require(ctx.give(player, book(Enchant::Type::Sharpness, 2), 1) == 1, "give book");
    ctx.require(ctx.give(player, make("minecraft:dirt", 3), 2) == 2, "give dirt");
    ctx.require(place(player, 0, AnvilMenu::kLeftSlot, 1), "sword in");
    ctx.require(place(player, 1, AnvilMenu::kRightSlot, 1), "book in");
    setLevel(player, 20);
    // The cursor holds dirt: the result can't go there, so the anvil must undo the take.
    ctx.require(mm.simulate(player, {Action::Take, {Where::Inventory, 2}, {Where::Cursor, 0}, 3}), "dirt to cursor");
    (void)mm.simulate(player, {Action::Take, {Where::Menu, AnvilMenu::kResultSlot}, {Where::Cursor, 0}, 1});
    co_await ctx.ticks(1);
    auto menu = std::dynamic_pointer_cast<AnvilMenu>(mm.current(player));
    ctx.require(menu != nullptr, "menu open");
    ctx.check(!menu->ref().item("left").empty() && !menu->ref().item("right").empty(), "inputs restored");
    ctx.check(playerLevel(player) == 20, "levels refunded, level " + std::to_string(playerLevel(player)));
    ctx.check(cursorOf(player).getTypeName() == "minecraft:dirt", "dirt still on the cursor");
    mm.close(player);
});

ABP_REGISTER_SELFTEST("anvil.menu_rename_and_persist", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto  f      = openAnvil(ctx, player);
    ctx.require(ctx.give(player, make("minecraft:iron_pickaxe"), 4) == 4, "give pickaxe");
    // Junk goes to the backpack storage, gear goes into the left slot.
    ctx.require(ctx.give(player, make("minecraft:cobblestone", 16), 5) == 5, "give cobblestone");
    ctx.check(mm.simulate(player, {Action::Place, {Where::Inventory, 5}, {Where::Menu, 30}, 16}), "quick move cobblestone");
    ctx.check(f.bp->inventory().count([](core::ItemStackData const& s) { return s.id == "minecraft:cobblestone"; }) == 16, "cobblestone stored in the backpack");
    ctx.check(mm.simulate(player, {Action::Place, {Where::Inventory, 4}, {Where::Menu, 30}, 1}), "quick move in");
    auto menu = std::dynamic_pointer_cast<AnvilMenu>(mm.current(player));
    ctx.require(menu != nullptr, "menu open");
    ctx.check(menu->ref().item("left").id == "minecraft:iron_pickaxe", "pickaxe in the left slot");
    // The form result is stored as the pending name.
    AnvilMenu::setPendingName(menu->ref(), "Digger");
    co_await ctx.ticks(2);
    auto shown = mm.shownItem(player, AnvilMenu::kResultSlot);
    ctx.check(shown.getCustomName() == "Digger", "result shows the new name");
    // Closing keeps the input in the upgrade; it travels with the upgrade item.
    mm.close(player);
    ctx.check(!f.bp->data().upgrades[static_cast<std::size_t>(f.slot)].settings["left"].is_null(), "input kept after close");
    std::string reason;
    auto        removed = upgrade::remove(f.bp, f.slot, &player, &reason);
    ctx.require(removed.has_value(), "remove upgrade: " + reason);
    auto other = ctx.createBackpack(core::Tier::Leather);
    ctx.require(upgrade::install(other, 0, *removed, &player, &reason), "reinstall: " + reason);
    ctx.check(!other->data().upgrades[0].settings["left"].is_null(), "input travelled with the upgrade item");
    // Selling point: the anvil button shows on the backpack page.
    ctx.require(ui::BackpackMenu::openBackpack(player, other), "open backpack");
    mm.forceShown(player);
    co_await ctx.ticks(2);
    bool found = false;
    for (int i = 48; i < 52; ++i) found = found || mm.shownItem(player, i).getTypeName() == "minecraft:anvil";
    ctx.check(found, "anvil button on the storage page");
    for (int i = 48; i < 52; ++i) {
        if (mm.shownItem(player, i).getTypeName() == "minecraft:anvil") {
            (void)mm.simulate(player, {Action::Take, {Where::Menu, i}, {Where::Cursor, 0}, 1});
            break;
        }
    }
    // The backpack closes first; the anvil opens a few ticks later.
    bool opened = false;
    for (int t = 0; t < 40 && !opened; ++t) {
        co_await ctx.ticks(1);
        opened = std::dynamic_pointer_cast<AnvilMenu>(mm.current(player)) != nullptr;
    }
    ctx.check(opened, "button opens the anvil");
    mm.close(player);
});

} // namespace
} // namespace abp::upgrade::anvil
