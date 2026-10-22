// Selftests of the anvil's native tab (server side): anvil.native_*

#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"

#include "game/ItemConv.h"
#include "selftest/SelfTest.h"
#include "upgrade/anvil/AnvilLogic.h"
#include "upgrade/anvil/AnvilUpgrade.h"
#include "upgrade/anvil/NativeTestSupport.h"

namespace abp::upgrade::anvil {
namespace {

using selftest::Context;
using test::NativeScope;

ItemStack make(std::string_view id, int count = 1) { return game::ItemConv::make(id, count); }
void      setLevel(Player& p, int level) { p.addLevels(level - playerLevel(p)); }

ABP_REGISTER_SELFTEST("anvil.native_repair_and_take", [](Context& ctx) {
    auto& p    = ctx.player();
    auto  bp   = ctx.createBackpack(core::Tier::Leather);
    int   slot = ctx.installUpgrade(bp, "anvil");
    NativeScope ns(ctx, p, bp);
    auto        anvil = [&] { return ns.tabData(slot)["anvil"]; };
    ctx.require(anvil().is_object(), "anvil tab data sent: " + ns.tabData(slot).dump());

    auto sword = make("minecraft:diamond_sword");
    sword.setDamageValue(1000);
    ns.setCursor(sword);
    ctx.check(ns.tab(slot, {{"click", "left"}, {"button", 0}}), "sword into the first slot");
    ctx.check(ns.cursor().isNull(), "cursor empty");
    ctx.check(!anvil()["left"].is_null(), "first slot shown");
    ctx.check(anvil().value("name", "") == make("minecraft:diamond_sword").getName(), "name box takes the item name: " + anvil().dump());
    ctx.check(anvil()["result"].is_null(), "no result without a change");

    ns.setCursor(make("minecraft:diamond", 5));
    ctx.check(ns.tab(slot, {{"click", "right"}, {"button", 0}}), "diamonds into the second slot");
    ctx.check(!anvil()["result"].is_null() && anvil().value("cost", 0) == 3, "repair costs 3: " + anvil().dump());

    setLevel(p, 1);
    ns.tab(slot, {{"click", "result"}, {"button", 0}}); // refreshes the tab data
    ctx.check(!anvil().value("canTake", true), "can't take with 1 level");
    ctx.check(ns.cursor().isNull(), "nothing taken");

    setLevel(p, 10);
    ctx.check(ns.tab(slot, {{"click", "result"}, {"button", 0}}), "take the result");
    ctx.check(ns.cursor().getTypeName() == "minecraft:diamond_sword" && ns.cursor().getDamageValue() == 0, "repaired sword on the cursor");
    ctx.check(playerLevel(p) == 7, "3 levels paid, level " + std::to_string(playerLevel(p)));
    ctx.check(anvil()["left"].is_null() && anvil().value("name", "x").empty(), "first slot and name box cleared");
    ctx.check(!anvil()["right"].is_null(), "2 diamonds left");
    ns.setCursor({});
});

ABP_REGISTER_SELFTEST("anvil.native_rename_inline", [](Context& ctx) {
    auto& p    = ctx.player();
    auto  bp   = ctx.createBackpack(core::Tier::Leather);
    int   slot = ctx.installUpgrade(bp, "anvil");
    NativeScope ns(ctx, p, bp);
    auto        anvil = [&] { return ns.tabData(slot)["anvil"]; };
    ns.setCursor(make("minecraft:iron_pickaxe"));
    ctx.require(ns.tab(slot, {{"click", "left"}, {"button", 0}}), "pickaxe in");

    // The client's own (translated) item name is not a rename.
    ns.tab(slot, {{"name", "Spitzhacke"}, {"default", "Spitzhacke"}});
    ctx.check(anvil()["result"].is_null(), "default name: no result");
    ns.tab(slot, {{"name", "Digger"}, {"default", "Spitzhacke"}});
    auto result = anvil()["result"];
    ctx.require(!result.is_null(), "typed name: a result");
    auto d = core::ItemStackData::fromJson(result);
    ctx.check(game::ItemConv::toStack(d, 1).getCustomName() == "Digger", "result renamed");
    ctx.check(anvil().value("cost", 0) == 1, "rename costs 1");
    // 50 characters at most.
    ns.tab(slot, {{"name", std::string(80, 'x')}});
    ctx.check(anvil().value("name", "").size() == 50, "cut to 50 characters");
    // Shift-click: the result goes into the backpack storage (SB default "shift click into storage").
    ns.tab(slot, {{"name", "Digger"}});
    setLevel(p, 5);
    ctx.check(ns.tab(slot, {{"click", "result"}, {"button", 0}, {"shift", true}}), "shift-click the result");
    ctx.check(bp->inventory().at(0).id == "minecraft:iron_pickaxe", "renamed pickaxe in the backpack");
    ctx.check(ns.cursor().isNull(), "not on the cursor");
    // Shift-click an input: into the storage too.
    ns.setCursor(make("minecraft:diamond", 3));
    ns.tab(slot, {{"click", "right"}, {"button", 0}});
    ctx.check(ns.tab(slot, {{"click", "right"}, {"button", 0}, {"shift", true}}), "shift-click the second slot");
    ctx.check(bp->inventory().count([](core::ItemStackData const& s) { return s.id == "minecraft:diamond"; }) == 3, "diamonds stored");
    // The name box is only editable with an item in the first slot.
    ns.tab(slot, {{"name", "Ghost"}});
    ctx.check(anvil().value("name", "").empty(), "no name without an item");
});

ABP_REGISTER_SELFTEST("anvil.native_too_expensive", [](Context& ctx) {
    auto& p    = ctx.player();
    auto  bp   = ctx.createBackpack(core::Tier::Leather);
    int   slot = ctx.installUpgrade(bp, "anvil");
    NativeScope ns(ctx, p, bp);
    auto        anvil = [&] { return ns.tabData(slot)["anvil"]; };
    auto        old   = make("minecraft:diamond_sword");
    old.setRepairCost(39);
    ns.setCursor(old);
    ns.tab(slot, {{"click", "left"}, {"button", 0}});
    EnchantmentInstance unbreaking;
    unbreaking.mEnchantType = Enchant::Type::Unbreaking;
    unbreaking.mLevel       = 3;
    auto book               = make("minecraft:enchanted_book");
    (void)EnchantUtils::applyEnchant(book, unbreaking, true);
    ns.setCursor(book);
    ns.tab(slot, {{"click", "right"}, {"button", 0}});
    ctx.check(anvil().value("tooExpensive", false) && anvil().value("cost", 0) >= 40, "too expensive: " + anvil().dump());
    ctx.check(anvil()["result"].is_null(), "no result to take");
    setLevel(p, 100);
    ctx.check(!ns.tab(slot, {{"click", "result"}, {"button", 0}}) && ns.cursor().isNull(), "can't be taken in survival");
});

} // namespace
} // namespace abp::upgrade::anvil
