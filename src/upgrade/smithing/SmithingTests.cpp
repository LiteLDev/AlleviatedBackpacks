// Selftests for the smithing upgrade: smithing.*

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"

#include "game/ItemConv.h"
#include "selftest/SelfTest.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/smithing/SmithingLogic.h"
#include "upgrade/smithing/SmithingUpgrade.h"

namespace abp::upgrade::smithing {
namespace {

using selftest::Context;
using Where  = ui::MenuManager::SlotAddress::Where;
using Action = ui::MenuManager::ActionType;

ItemStack make(std::string_view id, int count = 1) { return game::ItemConv::make(id, count); }

ItemStack cursorOf(Player& p) { return p.mPlayerUIContainer->getItem(0); }

ABP_REGISTER_SELFTEST("smithing.netherite_upgrade", [](Context& ctx) {
    auto sword = make("minecraft:diamond_sword");
    EnchantmentInstance sharp;
    sharp.mEnchantType = Enchant::Type::Sharpness;
    sharp.mLevel       = 4;
    (void)EnchantUtils::applyEnchant(sword, sharp, true);
    sword.setDamageValue(100);
    game::ItemConv::setDisplay(sword, "Old Faithful");
    auto o = compute(make("minecraft:netherite_upgrade_smithing_template"), sword, make("minecraft:netherite_ingot"));
    ctx.require(o.hasResult(), "netherite upgrade recipe found");
    ctx.check(o.result.getTypeName() == "minecraft:netherite_sword", "result is a netherite sword: " + o.result.getTypeName());
    ctx.check(o.result.mCount == 1, "one result");
    ctx.check(EnchantUtils::getEnchantLevel(Enchant::Type::Sharpness, o.result) == 4, "enchantments kept");
    ctx.check(o.result.getDamageValue() == 100, "damage kept: " + std::to_string(o.result.getDamageValue()));
    ctx.check(o.result.getCustomName() == "Old Faithful", "name kept");
    // Wrong combinations.
    ctx.check(!compute(make("minecraft:netherite_upgrade_smithing_template"), sword, make("minecraft:diamond")).hasResult(), "diamond is not netherite");
    ctx.check(!compute(make("minecraft:netherite_upgrade_smithing_template"), make("minecraft:iron_sword"), make("minecraft:netherite_ingot")).hasResult(), "iron sword can't be upgraded");
    ctx.check(!compute({}, sword, make("minecraft:netherite_ingot")).hasResult(), "template required");
});

ABP_REGISTER_SELFTEST("smithing.armor_trim", [](Context& ctx) {
    auto plate = make("minecraft:iron_chestplate");
    auto o     = compute(make("minecraft:coast_armor_trim_smithing_template"), plate, make("minecraft:gold_ingot"));
    ctx.require(o.hasResult(), "trim recipe found");
    ctx.check(o.result.getTypeName() == "minecraft:iron_chestplate", "same item");
    ctx.check(o.result.mUserData && o.result.mUserData->contains("Trim"), "trim data written");
    ctx.check(!o.result.hasSameUserData(plate), "result differs from the base");
    ctx.check(!compute(make("minecraft:coast_armor_trim_smithing_template"), make("minecraft:iron_sword"), make("minecraft:gold_ingot")).hasResult(), "swords can't be trimmed");
    ctx.check(!compute(make("minecraft:coast_armor_trim_smithing_template"), plate, make("minecraft:stone")).hasResult(), "stone is no trim material");
});

ABP_REGISTER_SELFTEST("smithing.menu_flow", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(core::Tier::Leather);
    int   slot   = ctx.installUpgrade(bp, "smithing");
    ctx.require(SmithingMenu::open(player, UpgradeContext(bp, slot)), "open smithing menu");
    mm.forceShown(player);
    ctx.require(ctx.give(player, make("minecraft:netherite_upgrade_smithing_template", 2), 0) == 0, "give templates");
    ctx.require(ctx.give(player, make("minecraft:netherite_ingot", 3), 1) == 1, "give ingots (material before the base)");
    ctx.require(ctx.give(player, make("minecraft:diamond_pickaxe"), 2) == 2, "give pickaxe");
    // Shift-clicks route to template, base and addition.
    for (int i = 0; i < 3; ++i) {
        ctx.check(mm.simulate(player, {Action::Place, {Where::Inventory, i}, {Where::Menu, 20}, 64}), "quick move " + std::to_string(i));
    }
    auto menu = std::dynamic_pointer_cast<SmithingMenu>(mm.current(player));
    ctx.require(menu != nullptr, "menu open");
    ctx.check(menu->ref().item("template").count == 2, "templates in the template slot");
    ctx.check(menu->ref().item("base").id == "minecraft:diamond_pickaxe", "pickaxe in the base slot");
    ctx.check(menu->ref().item("addition").count == 3, "ingots in the addition slot");
    co_await ctx.ticks(2);
    ctx.check(mm.shownItem(player, SmithingMenu::kResultSlot).getTypeName() == "minecraft:netherite_pickaxe", "result shown");
    ctx.check(mm.simulate(player, {Action::Take, {Where::Menu, SmithingMenu::kResultSlot}, {Where::Cursor, 0}, 1}), "take result");
    ctx.check(cursorOf(player).getTypeName() == "minecraft:netherite_pickaxe", "netherite pickaxe on the cursor");
    ctx.check(menu->ref().item("template").count == 1, "one template consumed");
    ctx.check(menu->ref().item("base").empty(), "base consumed");
    ctx.check(menu->ref().item("addition").count == 2, "one ingot consumed");
    co_await ctx.ticks(2);
    ctx.check(mm.shownItem(player, SmithingMenu::kResultSlot).isNull(), "no result without a base");
    // Leftovers stay in the upgrade after closing.
    mm.close(player);
    ctx.check(!bp->data().upgrades[static_cast<std::size_t>(slot)].settings["addition"].is_null(), "inputs kept in the upgrade");
    ctx.check(cursorOf(player).isNull(), "cursor returned");
});

} // namespace
} // namespace abp::upgrade::smithing
