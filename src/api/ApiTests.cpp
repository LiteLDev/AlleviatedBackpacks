// Selftests of the capability API: api.*
// Run with `/backpack selftest "api.*"`.

#include "mc/world/level/block/VanillaStates.h"
#include "mc/world/level/block/states/BlockStateVariant.h"

#include "api/BackpackCapabilities.h"
#include "api/Service.h"
#include "feature/placed/TestSite.h"
#include "game/Backpack.h"
#include "game/ItemConv.h"
#include "selftest/SelfTest.h"
#include "upgrade/IUpgrade.h"
#include "upgrade/tank/Tank.h"

namespace abp::api {
namespace {

using selftest::Context;
namespace ids = fluids;

std::string idOf(ItemStack const& s) { return game::ItemConv::idOf(s); }

std::string show(FluidStack const& s) { return s.empty() ? "<empty>" : s.id + s.nbt + " x" + std::to_string(s.amount); }

ABP_REGISTER_SELFTEST("api.service", [](Context& ctx) {
    auto caps = capabilities();
    ctx.require(caps != nullptr, "the service is registered with LeviLamina's ServiceManager");
    ctx.check(caps.get() == &service(), "the registered service is the mod's instance");
    ctx.check(caps->apiVersion() >= 1, "api version");
    for (auto id : {ids::Water, ids::Lava, ids::Milk, ids::PowderSnow, ids::Potion, ids::Experience}) {
        ctx.check(caps->fluid(id).has_value(), "fluid registered: " + std::string(id));
    }
    ctx.check(caps->fluidHasTag(ids::Experience, fluid_tags::Experience), "abp:experience is tagged c:experience");
    ctx.check(!caps->fluidHasTag(ids::Water, fluid_tags::Experience), "water is not experience");
    ctx.check(caps->registerFluid(FluidInfo{std::string(ids::Water)}) == 0, "duplicate fluid ids are refused");
});

ABP_REGISTER_SELFTEST("api.item_containers", [](Context& ctx) {
    auto& caps = service();
    auto  item = [](std::string_view id, int aux = 0) { return game::ItemConv::make(id, 1, aux); };

    auto water = caps.containerContents(item("minecraft:water_bucket"));
    ctx.require(water.has_value(), "water bucket is a container");
    ctx.check(water->fluid == FluidStack{"minecraft:water", 1000}, "water bucket holds 1000 mB water, got " + show(water->fluid));
    ctx.check(idOf(water->empty) == "minecraft:bucket", "emptying it leaves a bucket");
    auto bottle = caps.containerContents(item("minecraft:potion", 0));
    ctx.check(bottle && bottle->fluid == FluidStack{"minecraft:water", 250}, "water bottle: 250 mB water");
    auto potion = caps.containerContents(item("minecraft:potion", 7));
    ctx.check(potion && potion->fluid == FluidStack{"minecraft:potion", 250, "{\"potion\":7}"}, "potion keeps its type in nbt");
    auto xp = caps.containerContents(item("minecraft:experience_bottle"));
    ctx.check(xp && xp->fluid == FluidStack{"abp:experience", 160}, "bottle o' enchanting: 160 mB experience");
    ctx.check(xp && idOf(xp->empty) == "minecraft:glass_bottle", "... leaving a glass bottle");
    ctx.check(caps.containerContents(item("minecraft:powder_snow_bucket"))->fluid.id == ids::PowderSnow, "powder snow bucket");
    ctx.check(caps.containerContents(item("minecraft:milk_bucket"))->fluid.id == ids::Milk, "milk bucket");
    ctx.check(!caps.containerContents(item("minecraft:stone")), "stone is no container");

    auto lavaBucket = caps.fillContainer(item("minecraft:bucket"), {"minecraft:lava", 1000});
    ctx.check(lavaBucket && idOf(lavaBucket->filled) == "minecraft:lava_bucket" && lavaBucket->amount == 1000, "bucket + lava");
    auto xpBottle = caps.fillContainer(item("minecraft:glass_bottle"), {"abp:experience", 1});
    ctx.check(xpBottle && idOf(xpBottle->filled) == "minecraft:experience_bottle" && xpBottle->amount == 160, "bottle + experience");
    auto back = caps.fillContainer(item("minecraft:glass_bottle"), potion->fluid);
    ctx.check(back && idOf(back->filled) == "minecraft:potion" && back->filled.getAuxValue() == 7, "bottle + potion #7");
    ctx.check(!caps.fillContainer(item("minecraft:bucket"), {"minecraft:honey", 1000}), "honey does not go into a bucket");

    // The item handler of a bucket: fill it, the container changes.
    auto h = caps.itemFluid(item("minecraft:bucket"));
    ctx.require(h != nullptr, "an empty bucket has a fluid handler");
    ctx.check(h->tankCapacity(0) == 1000, "bucket capacity 1000");
    ctx.check(h->fill({"minecraft:water", 999}, false) == 0, "a bucket takes whole buckets only");
    ctx.check(h->fill({"minecraft:water", 5000}, true) == 1000 && idOf(h->container()) == "minecraft:bucket", "simulate changes nothing");
    ctx.check(h->fill({"minecraft:water", 5000}, false) == 1000, "fill 1000 mB");
    ctx.check(idOf(h->container()) == "minecraft:water_bucket", "the bucket became a water bucket");
    ctx.check(h->drainAny(1000, false) == FluidStack{"minecraft:water", 1000}, "drain it again");
    ctx.check(idOf(h->container()) == "minecraft:bucket", "back to an empty bucket");
    ctx.check(caps.itemFluid(item("minecraft:stone")) == nullptr, "stone has no fluid handler");

    // A mod fluid and container: registered at runtime, usable by the tank, then removed again.
    auto fluidToken = caps.registerFluid(FluidInfo{"abptest:slime", "abp.api.test", "Slime", 0xFF00FF00});
    ctx.require(fluidToken != 0, "register a fluid");
    auto boxToken = caps.registerFluidContainer(FluidContainer{"minecraft:slime_ball", 0, "minecraft:clay_ball", {"abptest:slime", 500}});
    ctx.require(boxToken != 0, "register a container");
    ctx.check(caps.containerContents(item("minecraft:slime_ball"))->fluid == FluidStack{"abptest:slime", 500}, "slime ball holds slime");
    auto bp = ctx.createBackpack(core::Tier::Leather);
    upgrade::UpgradeContext t(bp, ctx.installUpgrade(bp, "tank"));
    auto in = game::ItemConv::toData(item("minecraft:slime_ball"));
    auto left = upgrade::tank::emptyContainer(in, upgrade::tank::fillerOf(t));
    ctx.check(left && left->id == "minecraft:clay_ball", "the tank takes a mod container");
    auto handler = backpack::fluidHandler(bp);
    ctx.require(handler != nullptr, "backpack fluid handler");
    ctx.check(handler->fluidInTank(0) == FluidStack{"abptest:slime", 500}, "tank holds 500 mB abptest:slime, got " + show(handler->fluidInTank(0)));
    auto out = upgrade::tank::fillContainer(game::ItemConv::toData(item("minecraft:clay_ball")), upgrade::tank::read(t).fluid(), upgrade::tank::drainerOf(t));
    ctx.check(out && out->id == "minecraft:slime_ball", "and fills the mod container back");
    // SC's tank also takes bottles o' enchanting (160 mB) and fills glass bottles from experience.
    (void)upgrade::tank::drainerOf(t)(upgrade::tank::read(t).fluid(), upgrade::tank::read(t).amount(), false);
    auto xpLeft = upgrade::tank::emptyContainer(game::ItemConv::toData(item("minecraft:experience_bottle")), upgrade::tank::fillerOf(t));
    ctx.check(xpLeft && xpLeft->id == "minecraft:glass_bottle", "a bottle o' enchanting pours into the tank");
    ctx.check(upgrade::tank::read(t).contents() == core::fluid::FluidStack{"experience", 160}, "as 160 mB experience");
    auto xpBack = upgrade::tank::fillContainer(game::ItemConv::toData(item("minecraft:glass_bottle")), "experience", upgrade::tank::drainerOf(t));
    ctx.check(xpBack && xpBack->id == "minecraft:experience_bottle" && upgrade::tank::read(t).empty(), "and fills a glass bottle back");
    ctx.check(caps.unregister(boxToken) && caps.unregister(fluidToken), "unregister");
    ctx.check(!caps.containerContents(item("minecraft:slime_ball")) && !caps.fluid("abptest:slime"), "gone after unregister");
});

ABP_REGISTER_SELFTEST("api.energy_item", [](Context& ctx) {
    auto& caps  = service();
    auto  token = caps.registerItemEnergy("minecraft:echo_shard", [&caps](ItemStack& stack) {
        return caps.makeItemEnergyStorage(stack, 10000, 500, 400);
    });
    ctx.require(token != 0, "register an energy item at runtime");
    ctx.check(caps.registerItemEnergy("minecraft:echo_shard", [](ItemStack&) { return nullptr; }) == 0, "one provider per item");

    auto stack = game::ItemConv::make("minecraft:echo_shard");
    auto e     = caps.itemEnergy(stack);
    ctx.require(e != nullptr, "the item has an energy storage");
    ctx.check(e->capacity() == 10000 && e->stored() == 0 && e->canReceive() && e->canExtract(), "empty 10000 FE storage");
    ctx.check(e->receive(1000, true) == 500 && e->stored() == 0, "simulated charge (limited to 500 per call)");
    ctx.check(e->receive(1000, false) == 500, "charge 500");
    ctx.check(e->receive(1000, false) == 500, "charge 500 more");
    ctx.check(caps.itemEnergy(stack)->stored() == 1000, "the charge is stored in the item");
    auto copy = stack; // the value travels with the item
    ctx.check(caps.itemEnergy(copy)->stored() == 1000, "copies keep the charge");
    ctx.check(e->extract(1000, false) == 400, "discharge (limited to 400 per call)");
    ctx.check(caps.itemEnergy(stack)->stored() == 600, "600 FE left");

    // Item -> item transfer with the shared helper.
    auto other = game::ItemConv::make("minecraft:echo_shard");
    auto dst   = caps.itemEnergy(other);
    ctx.check(transferEnergy(*e, *dst, 10000) == 400, "transfer moves min(extract limit, receive limit)");
    ctx.check(caps.itemEnergy(stack)->stored() == 200 && caps.itemEnergy(other)->stored() == 400, "200 / 400 FE");
    auto stone = game::ItemConv::make("minecraft:stone");
    ctx.check(caps.itemEnergy(stone) == nullptr, "stone stores no energy");

    ctx.check(caps.unregister(token), "unregister");
    ctx.check(caps.itemEnergy(stack) == nullptr, "no energy storage after unregister");
});

ABP_REGISTER_SELFTEST("api.backpack_tanks", [](Context& ctx) {
    auto& caps = service();
    auto  bp   = ctx.createBackpack(core::Tier::Iron); // 54 slots: phase-1 tanks hold 24000 mB
    ctx.check(caps.backpackFluid(bp->uuid()) == nullptr, "no tanks, no handler");
    ctx.check(caps.backpackEnergy(bp->uuid()) == nullptr, "no battery, no energy");
    ctx.installUpgrade(bp, "tank");
    ctx.installUpgrade(bp, "tank");
    auto h = caps.backpackFluid(bp->uuid());
    ctx.require(h != nullptr && h->tanks() == 2, "two tanks combined");
    ctx.check(h->tankCapacity(0) == upgrade::tank::capacity(*bp), "tank capacity");
    // SB: fill pours into each tank in slot order.
    ctx.check(h->fill({"minecraft:water", 30000}, false) == 30000, "fill 30000 mB water");
    ctx.check(h->fluidInTank(0).amount == 24000 && h->fluidInTank(1).amount == 6000, "first tank first");
    ctx.check(h->fill({"minecraft:lava", 1000}, true) == 0, "no room for lava");
    ctx.check(upgrade::tank::amountOf(bp, "water") == 30000, "the tank upgrade sees the same water");
    ctx.check(h->drain({"minecraft:water", 25000}, false) == FluidStack{"minecraft:water", 25000}, "drain 25000 across tanks");
    ctx.check(h->drainAny(10000, false) == FluidStack{"minecraft:water", 5000}, "drainAny drains the first non-empty tank");
    ctx.check(upgrade::tank::amountOf(bp, "water") == 0, "everything drained");
    // Potions keep their type through the phase-1 storage format.
    FluidStack potion{"minecraft:potion", 500, "{\"potion\":7}"};
    ctx.check(h->fill(potion, false) == 500, "fill a potion");
    ctx.check(upgrade::tank::read(upgrade::tank::tanks(bp)[0]).fluid() == "potion:7", "stored as potion:7");
    ctx.check(h->fluidInTank(0) == potion, "read back with its nbt");
    ctx.check(h->drain({"minecraft:potion", 500, "{\"potion\":8}"}, false).empty(), "another potion type does not drain");
    // The backpack item exposes the same tanks.
    auto item = ctx.backpackItem(*bp);
    auto ih   = caps.itemFluid(item);
    ctx.require(ih != nullptr, "backpack item fluid handler");
    ctx.check(ih->tanks() == 2 && ih->fluidInTank(0) == potion, "item handler sees the tanks");
    ctx.check(idOf(ih->container()) == idOf(item), "the backpack item itself is unchanged");
    ctx.check(caps.itemEnergy(item) == nullptr, "no battery: no item energy");
});

ABP_REGISTER_SELFTEST("api.cauldron", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& caps = service();
    auto& sp   = ctx.spawnPlayer();
    co_await placed::test::toGround(ctx, sp);
    placed::test::Site site(sp);
    auto&              region = site.region();
    auto               pos    = site.at(0, 0);
    ctx.require(site.set(pos, "minecraft:cauldron"), "place a cauldron");
    auto h = caps.blockFluid(region, pos);
    ctx.require(h != nullptr, "a cauldron has a fluid handler");
    ctx.check(h->tanks() == 1 && h->tankCapacity(0) == 1000 && h->fluidInTank(0).empty(), "one empty 1000 mB tank");
    ctx.check(h->fill({"minecraft:water", 500}, false) == 0, "only whole buckets");
    ctx.check(h->fill({"minecraft:milk", 1000}, false) == 0, "no milk in cauldrons");
    ctx.check(h->fill({"minecraft:water", 1000}, false) == 1000, "fill with water");
    ctx.check(region.getBlock(pos).getState<int>(VanillaStates::FillLevel()).value_or(-1) == 6, "the cauldron is full");
    ctx.check(caps.blockFluid(region, pos)->fluidInTank(0) == FluidStack{"minecraft:water", 1000}, "reads 1000 mB water");
    ctx.check(h->fill({"minecraft:water", 1000}, false) == 0, "a full cauldron takes nothing");
    ctx.check(h->drain({"minecraft:lava", 1000}, false).empty(), "draining lava from water fails");
    ctx.check(h->drainAny(1000, false) == FluidStack{"minecraft:water", 1000}, "drain the water");
    ctx.check(region.getBlock(pos).getState<int>(VanillaStates::FillLevel()).value_or(-1) == 0, "empty again");

    ctx.check(h->fill({"minecraft:lava", 1000}, false) == 1000, "fill with lava");
    ctx.check(caps.blockFluid(region, pos)->fluidInTank(0) == FluidStack{"minecraft:lava", 1000}, "a lava cauldron");
    ctx.check(caps.blockFluid(region, pos)->drainAny(1000, false).id == ids::Lava, "drain the lava");
    ctx.check(caps.blockFluid(region, pos)->fill({"minecraft:powder_snow", 1000}, false) == 1000, "fill with powder snow");
    ctx.check(caps.blockFluid(region, pos)->fluidInTank(0).id == ids::PowderSnow, "a powder snow cauldron");
    ctx.check(caps.blockFluid(region, pos)->drainAny(1000, false).amount == 1000, "drain the powder snow");

    // A partly filled cauldron reports its level but gives nothing.
    auto const* empty = &region.getBlock(pos);
    auto        half  = empty->setState<int>(VanillaStates::FillLevel(), 3);
    ctx.require(static_cast<bool>(half), "fill_level state");
    site.set(pos, *half);
    auto partial = caps.blockFluid(region, pos);
    ctx.check(partial->fluidInTank(0) == FluidStack{"minecraft:water", 500}, "half cauldron: 500 mB, got " + show(partial->fluidInTank(0)));
    ctx.check(partial->drainAny(1000, false).empty(), "a partial cauldron can't be drained");

    // Source blocks for pumps.
    auto src = site.at(2, 0);
    site.set(src, "minecraft:air");
    ctx.check(caps.placeFluidSource(region, src, {"minecraft:water", 999}, false) == 0, "placing needs a bucket");
    ctx.check(caps.placeFluidSource(region, src, {"minecraft:milk", 1000}, false) == 0, "milk has no block");
    ctx.check(caps.placeFluidSource(region, src, {"minecraft:water", 1000}, false) == 1000, "place a water source");
    ctx.check(region.getBlock(src).getTypeName().find("water") != std::string::npos, "water placed");
    ctx.check(caps.pickupFluidSource(region, src, true) == FluidStack{"minecraft:water", 1000}, "simulated pickup");
    ctx.check(caps.pickupFluidSource(region, src, false) == FluidStack{"minecraft:water", 1000}, "pick the water up");
    ctx.check(region.getBlock(src).isAir(), "air again");
    ctx.check(caps.placeFluidSource(region, src, {"minecraft:powder_snow", 1000}, false) == 1000, "place powder snow");
    ctx.check(caps.pickupFluidSource(region, src, false).id == ids::PowderSnow, "pick powder snow up");
    ctx.check(caps.pickupFluidSource(region, src, false).empty(), "nothing left to pick up");
});

ABP_REGISTER_SELFTEST("api.placed_backpack", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& caps = service();
    auto& sp   = ctx.spawnPlayer();
    co_await placed::test::toGround(ctx, sp);
    placed::test::Site site(sp);
    auto               pos = site.at(0, 1);
    site.set(site.at(0, 0), "minecraft:stone");
    site.set(pos, "minecraft:air");

    auto bp = ctx.createBackpack(core::Tier::Copper);
    ctx.installUpgrade(bp, "tank");
    site.touch(pos);
    auto item = ctx.backpackItem(*bp);
    ctx.require(
        placed::PlacedBackpacks::get().place(site.region(), pos, item, 2) == placed::PlacedBackpacks::PlaceResult::Ok,
        "place the backpack"
    );
    auto h = caps.blockFluid(site.region(), pos, Side::Up);
    ctx.require(h != nullptr, "the placed backpack has a fluid capability");
    ctx.check(h->tanks() == 1, "one tank");
    ctx.check(h->fill({"minecraft:lava", 3000}, false) == 3000, "a pipe fills 3000 mB lava");
    ctx.check(upgrade::tank::amountOf(bp, "lava") == 3000, "the lava is in the backpack's tank");
    ctx.check(h->drain({"minecraft:lava", 1000}, false).amount == 1000, "and drains it");
    ctx.check(caps.blockEnergy(site.region(), pos) == nullptr, "no battery: no energy capability");
    ctx.check(caps.blockFluid(site.region(), site.at(0, 0)) == nullptr, "stone has none");

    // Without a tank the block has no fluid handler.
    auto plain = ctx.createBackpack(core::Tier::Leather);
    auto pos2  = site.at(1, 1);
    site.set(site.at(1, 0), "minecraft:stone");
    site.set(pos2, "minecraft:air");
    site.touch(pos2);
    auto item2 = ctx.backpackItem(*plain);
    ctx.require(placed::PlacedBackpacks::get().place(site.region(), pos2, item2, 2) == placed::PlacedBackpacks::PlaceResult::Ok, "place a second one");
    ctx.check(caps.blockFluid(site.region(), pos2) == nullptr, "no tank: no fluid capability");
    co_return;
});

} // namespace
} // namespace abp::api
