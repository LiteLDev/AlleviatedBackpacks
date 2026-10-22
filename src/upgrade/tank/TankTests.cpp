// Selftests of the Tank upgrade and the inventory columns it takes: tank.*
// Run with `/backpack selftest "tank.*"`.

#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/PlayerUISlot.h"
#include "mc/world/item/ItemStack.h"

#include "api/BackpackCapabilities.h"
#include "core/Capacity.h"
#include "game/Backpack.h"
#include "game/ItemConv.h"
#include "net/NativeScreens.h"
#include "selftest/SelfTest.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/tank/Columns.h"
#include "upgrade/tank/MenuBits.h"
#include "upgrade/tank/Tank.h"

namespace abp::upgrade::tank {
namespace {

using selftest::Context;
namespace fl = core::fluid;

core::ItemStackData itemOf(std::string_view id, int count = 1, int aux = 0) {
    return game::ItemConv::toData(game::ItemConv::make(id, count, aux));
}

ABP_REGISTER_SELFTEST("tank.capacity", [](Context& ctx) {
    auto leather = ctx.createBackpack(core::Tier::Leather);
    ctx.installUpgrade(leather, "tank");
    ctx.check(capacity(*leather) == 12000, "leather: 3 rows x 4000 = 12000, got " + std::to_string(capacity(*leather)));
    ctx.check(maxInOut(*leather) == 1000, "leather: max(1000, 20 x 3) per operation");
    auto diamond = ctx.createBackpack(core::Tier::Diamond);
    ctx.installUpgrade(diamond, "tank");
    ctx.check(capacity(*diamond) == 36000, "diamond: 9 rows of 12 x 4000 = 36000, got " + std::to_string(capacity(*diamond)));
    auto iron = ctx.createBackpack(core::Tier::Iron);
    ctx.installUpgrade(iron, "tank");
    ctx.installUpgrade(iron, "tank");
    ctx.check(capacity(*iron) == 24000, "iron keeps its 6 base rows with two tanks: 24000, got " + std::to_string(capacity(*iron)));
});

ABP_REGISTER_SELFTEST("tank.columns", [](Context& ctx) {
    auto bp = ctx.createBackpack(core::Tier::Iron); // 54 slots, 6 rows
    auto& inv = bp->inventory();
    inv.set(53, itemOf("minecraft:diamond", 5));
    inv.set(0, itemOf("minecraft:stone", 10));
    int a = ctx.installUpgrade(bp, "tank");
    ctx.check(inv.size() == 42 && bp->data().columnsTaken == 2, "a tank takes 2 columns: 42 slots, got " + std::to_string(inv.size()));
    ctx.check(inv.at(0).id == "minecraft:stone", "slot 0 keeps its item");
    ctx.check(inv.at(1).id == "minecraft:diamond" && inv.at(1).count == 5, "the item from the last column moved forward");
    ctx.installUpgrade(bp, "tank");
    ctx.check(inv.size() == 30 && bp->data().columnsTaken == 4, "two tanks: 30 slots");
    auto removed = upgrade::remove(bp, a, nullptr);
    ctx.require(removed.has_value(), "remove a tank");
    ctx.check(inv.size() == 42 && bp->data().columnsTaken == 2, "removing gives the columns back");

    // A full inventory can't give columns: SB's message, nothing changes.
    auto full = ctx.createBackpack(core::Tier::Leather);
    for (int i = 0; i < full->inventory().size(); ++i) full->inventory().set(i, itemOf("minecraft:dirt", 1));
    std::string reason;
    ctx.check(!upgrade::canInstall(full, 0, itemOf(kItemId), nullptr, &reason), "full backpack refuses a tank");
    ctx.check(reason.find("last 6 slots") != std::string::npos, "message names the 6 slots: " + reason);
    ctx.check(full->inventory().size() == 27, "inventory unchanged");
    // Persisted: the reduced inventory survives a reload.
    auto back = core::BackpackData::fromJson(bp->data().toJson());
    ctx.check(back && back->inventory.size() == 42 && back->columnsTaken == 2, "columns survive save/load");
});

ABP_REGISTER_SELFTEST("tank.install_limits", [](Context& ctx) {
    auto bp = ctx.createBackpack(core::Tier::Diamond);
    ctx.installUpgrade(bp, "tank");
    ctx.installUpgrade(bp, "tank");
    std::string reason;
    ctx.check(!upgrade::canInstall(bp, 2, itemOf(kItemId), nullptr, &reason), "a third tank is refused");
    ctx.check(reason.find("already two Tank Upgrades") != std::string::npos, "SB message: " + reason);

    // A tank holding more than a small backpack's capacity can't go into it.
    UpgradeContext t(bp, 0);
    write(t, fl::Tank({"water", 30000}, capacity(*bp)));
    auto item = upgrade::remove(bp, 0, nullptr);
    ctx.require(item.has_value(), "remove the filled tank");
    auto small = ctx.createBackpack(core::Tier::Leather); // 12000 mB
    reason.clear();
    ctx.check(!upgrade::canInstall(small, 0, *item, nullptr, &reason), "too small for 30000 mB");
    ctx.check(reason.find("at least 2.5x higher capacity") != std::string::npos, "SB message with 2.5x: " + reason);
    auto big = ctx.createBackpack(core::Tier::Netherite);
    ctx.require(upgrade::install(big, 0, *item, nullptr, &reason), "fits a netherite backpack: " + reason);
    ctx.check(read(UpgradeContext(big, 0)).contents() == FluidStack{"water", 30000}, "contents travel with the item");
});

ABP_REGISTER_SELFTEST("tank.max_in_out", [](Context& ctx) {
    auto           bp = ctx.createBackpack(core::Tier::Leather);
    UpgradeContext t(bp, ctx.installUpgrade(bp, "tank"));
    ctx.check(fillTank(t, {"minecraft:water", 5000}, false) == 1000, "one operation moves a bucket");
    ctx.check(fillTank(t, {"minecraft:water", 5000}, false, true) == 5000, "ignoring the limit moves all");
    ctx.check(fillTank(t, {"minecraft:lava", 1000}, true) == 0, "another fluid is refused");
    ctx.check(drainTank(t, 9000, false).amount == 1000, "drain: a bucket per operation");
    auto h = api::backpack::fluidHandler(bp);
    ctx.require(h != nullptr, "backpack fluid handler");
    ctx.check(h->fill({"minecraft:water", 100000}, false) == 1000, "the API handler is limited too");
    ctx.check(read(t).amount() == 6000, "6000 mB in the tank, got " + std::to_string(read(t).amount()));
});

ABP_REGISTER_SELFTEST("tank.slots", [](Context& ctx) {
    auto           bp = ctx.createBackpack(core::Tier::Leather);
    UpgradeContext t(bp, ctx.installUpgrade(bp, "tank"));
    // Input: a water bucket is drained, the empty bucket goes to the input result slot.
    fluidui::setSlotItem(t, "in", itemOf("minecraft:water_bucket"));
    ctx.check(processSlots(t), "input processed");
    ctx.check(read(t).contents() == FluidStack{"water", 1000}, "1000 mB water");
    ctx.check(fluidui::slotItem(t, "in").empty(), "input slot empty");
    ctx.check(fluidui::slotItem(t, "inResult").id == "minecraft:bucket", "empty bucket in the result slot");
    // Output: a stack of glass bottles; one is filled per step and moved to the output result.
    fluidui::setSlotItem(t, "out", itemOf("minecraft:glass_bottle", 3));
    ctx.check(processSlots(t), "output processed");
    ctx.check(read(t).amount() == 750, "250 mB went into a bottle");
    ctx.check(fluidui::slotItem(t, "out").count == 2, "two bottles left in the output slot");
    auto res = fluidui::slotItem(t, "outResult");
    ctx.check(res.id == "minecraft:potion" && res.aux == 0, "a water bottle in the output result");
    // XP bottles count as 160 mB of experience (SB custom handler).
    auto           bp2 = ctx.createBackpack(core::Tier::Leather);
    UpgradeContext x(bp2, ctx.installUpgrade(bp2, "tank"));
    fluidui::setSlotItem(x, "in", itemOf("minecraft:experience_bottle", 2));
    ctx.check(processSlots(x), "xp bottle drained");
    ctx.check(read(x).contents() == FluidStack{"experience", 160}, "160 mB experience");
    ctx.check(fluidui::slotItem(x, "in").count == 1 && fluidui::slotItem(x, "inResult").id == "minecraft:glass_bottle", "one bottle left, glass bottle out");
    // Items that are no fluid containers are refused by the slot.
    fluidui::SettingsItemSlot slot(t, "in", nullptr, 64, nullptr);
    ctx.check(slot.accept(ctx.player(), game::ItemConv::make("minecraft:stone")) == 0, "stone refused");
});

ABP_REGISTER_SELFTEST("tank.cooldown", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(core::Tier::Leather);
    UpgradeContext t(bp, ctx.installUpgrade(bp, "tank"));
    ctx.require(ctx.give(player, ctx.backpackItem(*bp), 9) == 9, "carry the backpack");
    fluidui::setSlotItem(t, "out", itemOf("minecraft:bucket", 3));
    write(t, fl::Tank({"water", 3000}, capacity(*bp)));
    co_await ctx.ticks(15);
    int first = 3 - fluidui::slotItem(t, "out").count;
    ctx.check(first == 1, "one bucket in the first 15 ticks (20 tick cooldown), got " + std::to_string(first));
    // A full (unstackable) water bucket in the result slot blocks the next one, like in SB.
    co_await ctx.ticks(30);
    ctx.check(3 - fluidui::slotItem(t, "out").count == 1, "blocked while the result slot holds a water bucket");
    fluidui::setSlotItem(t, "outResult", {});
    co_await ctx.ticks(25);
    int later = 3 - fluidui::slotItem(t, "out").count;
    ctx.check(later == 2, "the next bucket once the result was taken, got " + std::to_string(later));
});

ABP_REGISTER_SELFTEST("tank.cursor_click", [](Context& ctx) {
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(core::Tier::Leather);
    UpgradeContext t(bp, ctx.installUpgrade(bp, "tank"), &player);
    auto r = interactWithCursor(t, game::ItemConv::make("minecraft:lava_bucket"));
    ctx.check(r && game::ItemConv::idOf(*r) == "minecraft:bucket", "a lava bucket on the cursor is emptied");
    ctx.check(read(t).contents() == FluidStack{"lava", 1000}, "1000 mB lava");
    r = interactWithCursor(t, game::ItemConv::make("minecraft:bucket"));
    ctx.check(r && game::ItemConv::idOf(*r) == "minecraft:lava_bucket", "an empty bucket is filled");
    ctx.check(read(t).empty(), "tank empty again");
    ctx.check(!interactWithCursor(t, game::ItemConv::make("minecraft:stone")), "stone does nothing");
    ctx.check(!interactWithCursor(t, game::ItemConv::make("minecraft:water_bucket", 2)), "count > 1 does nothing");

    // Through the native screen's tab action (SB TankClickPayload).
    player.setPlayerUIItem(PlayerUISlot::CursorSelected, game::ItemConv::make("minecraft:water_bucket"), false);
    bool handled = false;
    for (auto const& h : net::TabActionRegistry::entries()) {
        if (h.upgrade == "tank" && h.handle) handled |= h.handle(player, t, nlohmann::json{{"op", "click"}});
    }
    ctx.check(handled, "the tab action is handled");
    ctx.check(game::ItemConv::idOf(fluidui::cursorOf(player)) == "minecraft:bucket", "the cursor got the empty bucket");
    ctx.check(read(t).contents() == FluidStack{"water", 1000}, "the water is in the tank");
    player.setPlayerUIItem(PlayerUISlot::CursorSelected, ItemStack{}, false);
});

ABP_REGISTER_SELFTEST("tank.render_info", [](Context& ctx) {
    auto bp = ctx.createBackpack(core::Tier::Iron); // 2 upgrade slots: slot 1 is the right half
    UpgradeContext t(bp, ctx.installUpgrade(bp, "tank", 1));
    write(t, fl::Tank({"water", 12000}, capacity(*bp)));
    auto info = net::buildRenderInfo(*bp);
    ctx.check(info.extra.contains("tanks") && info.extra["tanks"].contains("right"), "a single tank in the right half renders right");
    ctx.check(info.extra["tanks"]["right"].value("fillRatio", 0.f) == 0.5f, "fill ratio 0.5");
    ctx.installUpgrade(bp, "tank", 0);
    info = net::buildRenderInfo(*bp);
    ctx.check(info.extra["tanks"].contains("left") && info.extra["tanks"].contains("right"), "two tanks: left and right");
});

ABP_REGISTER_SELFTEST("tank.tab_data", [](Context& ctx) {
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(core::Tier::Leather);
    UpgradeContext t(bp, ctx.installUpgrade(bp, "tank"), &player);
    write(t, fl::Tank({"experience", 3200}, capacity(*bp)));
    nlohmann::json tab = nlohmann::json::object();
    for (auto const& p : net::TabDataRegistry::entries()) {
        if (p.upgrade == "tank" && p.data) tab.update(p.data(player, t));
    }
    ctx.check(tab["fluidTank"]["capacity"] == 12000, "capacity sent");
    ctx.check(tab["fluidTank"]["fluid"]["amount"] == 3200 && tab["fluidTank"]["fluid"]["xp"] == true, "experience flagged");
    ctx.check(tab["columnsTaken"] == 2 && tab["inventoryColumns"] == 2, "columns sent");
});

} // namespace
} // namespace abp::upgrade::tank
