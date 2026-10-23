// Selftests of P2-11 (model render state, placed block behaviour from SB's BackpackBlock):
// model.* and placed.* (`/backpack selftest "model.*"`).

#include <limits>

#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"

#include "core/render/BackpackModel.h"
#include "feature/gui/SbSettings.h"
#include "feature/model/RenderModel.h"
#include "feature/placed/DisplayEntity.h"
#include "feature/placed/TestSite.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/Storage.h"
#include "net/NativeScreens.h"
#include "selftest/SelfTest.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/tank/Tank.h"

namespace abp::placed::test {
namespace {

using core::Tier;
using selftest::Context;

core::AccessContext const kInternal{core::Source::Internal};

void placeAt(Context& ctx, Site& site, game::Backpack const& bp, BlockPos const& pos, int facing = 2) {
    site.touch(pos);
    auto item = ctx.backpackItem(bp);
    auto r    = PlacedBackpacks::get().place(site.region(), pos, item, facing);
    ctx.require(r == PlacedBackpacks::PlaceResult::Ok, "place() failed: " + std::to_string(static_cast<int>(r)));
}

ABP_REGISTER_SELFTEST("model.render_info", [](Context& ctx) {
    auto bp = ctx.createBackpack(Tier::Gold); // 3 upgrade slots
    auto m  = model::buildModel(*bp);
    ctx.check(!m.left && !m.right && !m.battery && !m.display, "no tanks / battery / display by default");
    int slot = ctx.installUpgrade(bp, "tank", 2); // second half -> a single tank goes right
    ctx.require(slot == 2, "tank installed in slot 2");
    upgrade::tank::fill(bp, {"water", 1000});
    m = model::buildModel(*bp);
    ctx.check(!m.left && m.right.has_value(), "a single tank in the second half of the slots is on the right");
    ctx.check(m.right && m.right->fluid == "water" && m.right->ratio > 0.0f && m.right->ratio < 1.0f, "fluid and fill ratio");
    ctx.check(m.right && m.right->color == 0xFF3F76E4, "water tint");
    ctx.installUpgrade(bp, "tank", 0);
    m = model::buildModel(*bp);
    ctx.check(m.left.has_value() && m.right.has_value(), "two tanks: left and right");
    auto info = net::buildRenderInfo(*bp);
    ctx.check(info.extra.contains("model"), "RenderInfo carries the model");
    ctx.check(core::render::BackpackModel::fromJson(info.extra["model"]) == m, "model round trip");
    ctx.check(info.mainColor == core::render::kDefaultMainColor && info.accentColor == core::render::kDefaultAccentColor, "SB default colours");
});

ABP_REGISTER_SELFTEST("model.item_display", [](Context& ctx) {
    auto bp = ctx.createBackpack(Tier::Iron);
    bp->inventory().insert(core::ItemStackData::of("minecraft:diamond", 5), kInternal);
    feature::gui::selectItemDisplaySlot(*bp, 0);
    feature::gui::rotateItemDisplay(*bp, 0, true);
    feature::gui::rotateItemDisplay(*bp, 0, true);
    feature::gui::changeItemDisplayZOffset(*bp, 0, -3);
    auto m = model::buildModel(*bp);
    ctx.require(m.display.has_value(), "display item from the item display settings");
    ctx.check(m.display->item.id == "minecraft:diamond" && m.display->item.count == 1, "the slot's item, one");
    ctx.check(m.display->rotation == 90 && m.display->zOffset == -3 && m.display->side == "front", "rotation / depth / side");
});

ABP_REGISTER_SELFTEST("placed.display_entity", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& sp = ctx.spawnPlayer();
    co_await toGround(ctx, sp);
    Site site(sp, 4);
    auto pos = site.at(0, 1);
    site.set(pos, "minecraft:air");
    auto bp = ctx.createBackpack(Tier::Diamond);
    bp->inventory().insert(core::ItemStackData::of("minecraft:emerald", 3), kInternal);
    feature::gui::selectItemDisplaySlot(*bp, 0);
    placeAt(ctx, site, *bp, pos, 1);
    co_await ctx.ticks(6);
    auto loc   = locationOf(site.region(), pos);
    auto* disp = display::find(site.region(), loc);
    ctx.require(disp != nullptr, "a display entity draws the placed backpack");
    auto const& worn = disp->getArmor(SharedTypes::Legacy::ArmorSlot::Torso);
    ctx.check(game::isBackpack(worn) && game::backpackUuid(worn) == bp->uuid(), "it wears the backpack");
    ctx.check(display::isCopy(worn), "as a marked copy");
    ctx.check(disp->getCarriedItem().getTypeName() == "minecraft:emerald" && display::isCopy(disp->getCarriedItem()), "it holds the displayed item");
    ctx.check(std::abs(disp->getRotation().y - 90.0f) < 1.0f, "facing west (yaw 90)");
    // Never two.
    disp->remove();
    co_await ctx.ticks(6);
    ctx.check(display::find(site.region(), loc) != nullptr, "respawned after removal");
    // Gone with the backpack.
    (void)PlacedBackpacks::get().take(site.region(), pos);
    co_await ctx.ticks(2);
    ctx.check(display::find(site.region(), loc) == nullptr, "removed with the block");
    // A copy in a player's inventory is deleted.
    ItemStack copy = ctx.backpackItem(*bp);
    auto      tag  = std::make_unique<CompoundTag>();
    tag->putByte(std::string(display::kCopyKey), 1);
    copy.setUserData(std::move(tag));
    sp.getInventory().setItem(5, copy);
    ctx.check(display::purgeCopies(sp) == 1 && sp.getInventory().getItem(5).isNull(), "display copies never stay in inventories");
});

ABP_REGISTER_SELFTEST("placed.parts_and_open_states", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& sp = ctx.spawnPlayer();
    co_await toGround(ctx, sp);
    Site site(sp, 5);
    auto pos = site.at(0, 1);
    site.set(pos, "minecraft:air");
    auto bp = ctx.createBackpack(Tier::Netherite); // 7 slots: slot 0 -> left
    ctx.installUpgrade(bp, "tank", 0);
    placeAt(ctx, site, *bp, pos);
    co_await ctx.ticks(4);
    auto const& b = site.region().getBlock(pos);
    ctx.check(PlacedBackpacks::boolStateOf(b, kLeftTankState) == true, "left_tank state");
    ctx.check(PlacedBackpacks::boolStateOf(b, kRightTankState) == false, "right_tank state");
    ctx.check(PlacedBackpacks::boolStateOf(b, kBatteryState) == false, "battery state");
    ctx.check(PlacedBackpacks::boolStateOf(b, kOpenState) == false, "closed");
    ctx.require(PlacedBackpacks::get().open(sp, pos), "open the GUI");
    co_await ctx.ticks(4);
    ctx.check(PlacedBackpacks::boolStateOf(site.region().getBlock(pos), kOpenState) == true, "open while a player has it open");
    ui::MenuManager::get().close(sp);
    co_await ctx.ticks(4);
    ctx.check(PlacedBackpacks::boolStateOf(site.region().getBlock(pos), kOpenState) == false, "closed again");
    ctx.check(PlacedBackpacks::stateOf(site.region().getBlock(pos), kFacingState) == 2, "facing kept");
});

ABP_REGISTER_SELFTEST("placed.comparator_signal", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& sp = ctx.spawnPlayer();
    co_await toGround(ctx, sp);
    Site site(sp, 6);
    auto pos = site.at(0, 1);
    site.set(pos, "minecraft:air");
    auto bp = ctx.createBackpack(Tier::Leather); // 27 slots
    placeAt(ctx, site, *bp, pos);
    auto& placed = PlacedBackpacks::get();
    ctx.check(placed.comparatorSignal(site.region(), pos) == 0, "empty: 0");
    bp->inventory().insert(core::ItemStackData::of("minecraft:stone", 1), kInternal);
    ctx.check(placed.comparatorSignal(site.region(), pos) == 1, "one item: 1");
    bp->inventory().insert(core::ItemStackData::of("minecraft:stone", 64 * 27 - 1), kInternal);
    ctx.check(placed.comparatorSignal(site.region(), pos) == 15, "full: 15");
});

ABP_REGISTER_SELFTEST("placed.item_pickup", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& sp = ctx.spawnPlayer();
    co_await toGround(ctx, sp);
    Site site(sp, 8);
    auto pos = site.at(0, 1);
    site.set(pos, "minecraft:air");
    auto bp = ctx.createBackpack(Tier::Iron);
    ctx.installUpgrade(bp, "pickup");
    placeAt(ctx, site, *bp, pos);
    auto  stack = game::ItemConv::make("minecraft:cobblestone", 7);
    auto& level = site.region().getLevel();
    auto* item  = level.getSpawner().spawnItem(site.region(), stack, nullptr, pos.center(), 0);
    ctx.require(item != nullptr, "item spawned in the block");
    co_await ctx.ticks(8);
    ctx.check(countIn(*bp, "minecraft:cobblestone") == 7, "items falling into the block are picked up by the pickup upgrade");
});

ABP_REGISTER_SELFTEST("placed.fluid_container", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& sp = ctx.spawnPlayer();
    co_await toGround(ctx, sp);
    Site site(sp, 9);
    auto pos = site.at(0, 1);
    site.set(pos, "minecraft:air");
    auto bp = ctx.createBackpack(Tier::Iron);
    ctx.installUpgrade(bp, "tank");
    placeAt(ctx, site, *bp, pos);
    sp.setSelectedSlot(0);
    ctx.require(sp.simulateSetItem(game::ItemConv::make("minecraft:water_bucket", 1), true, 0), "hold a water bucket");
    auto& placed = PlacedBackpacks::get();
    ctx.require(placed.useFluidContainer(sp, pos), "a fluid container on a backpack with a tank");
    ctx.check(upgrade::tank::amountOf(bp, "water") == 1000, "emptied into the tank");
    ctx.check(sp.getSelectedItem().getTypeName() == "minecraft:bucket", "empty bucket in the hand");
    ctx.require(placed.useFluidContainer(sp, pos), "the empty bucket");
    ctx.check(upgrade::tank::amountOf(bp, "water") == 0, "drained from the tank");
    ctx.check(sp.getSelectedItem().getTypeName() == "minecraft:water_bucket", "filled again");
    ctx.check(ui::MenuManager::get().current(sp) == nullptr, "no GUI opened");
});

ABP_REGISTER_SELFTEST("placed.waterlogged", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& sp = ctx.spawnPlayer();
    co_await toGround(ctx, sp);
    Site site(sp, 10);
    auto pos = site.at(0, 1);
    site.set(pos, "minecraft:water");
    auto bp = ctx.createBackpack(Tier::Copper);
    placeAt(ctx, site, *bp, pos);
    ctx.check(PlacedBackpacks::isBackpackBlock(site.region().getBlock(pos)), "placed into water");
    ctx.check(site.region().getExtraBlock(pos).getTypeName() == "minecraft:water", "waterlogged (water in the liquid layer)");
    (void)PlacedBackpacks::get().take(site.region(), pos);
    BlockChangeContext c{};
    site.region().setExtraBlock(pos, *Block::tryGetFromRegistry(HashedString("minecraft:air")), 3);
});

ABP_REGISTER_SELFTEST("placed.creative_clone", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& sp = ctx.spawnPlayer();
    co_await toGround(ctx, sp);
    Site site(sp, 11);
    auto ground = site.at(0, 0);
    auto target = site.at(0, 1);
    site.set(ground, "minecraft:stone");
    site.set(target, "minecraft:air");
    auto bp = ctx.createBackpack(Tier::Gold);
    bp->inventory().insert(core::ItemStackData::of("minecraft:diamond", 4), kInternal);
    // SB cloneSubbackpacks: a backpack inside the clone gets its own storage (one level).
    auto inner = ctx.createBackpack(Tier::Leather);
    inner->inventory().insert(core::ItemStackData::of("minecraft:emerald", 3), kInternal);
    bp->inventory().set(5, game::ItemConv::toData(ctx.backpackItem(*inner)));
    sp.setPlayerGameType(GameType::Creative);
    ctx.require(sp.simulateSetItem(ctx.backpackItem(*bp), true, 0), "hold the backpack");
    sp.simulateSneaking();
    useOn(sp, ground, 1);
    co_await ctx.ticks(2);
    site.touch(target);
    sp.simulateStopSneaking();
    sp.setPlayerGameType(GameType::Survival);
    auto e = PlacedBackpacks::get().at(locationOf(site.region(), target));
    ctx.require(e.has_value(), "placed in creative");
    ctx.check(e->uuid != bp->uuid(), "the block is a clone with a new uuid");
    ctx.check(game::backpackUuid(sp.getInventory().getItem(0)) == bp->uuid(), "the item stays in the hand");
    auto clone = game::Storage::get().find(e->uuid);
    ctx.require(clone != nullptr, "clone stored");
    ctx.check(countIn(*clone, "minecraft:diamond") == 4, "clone has the contents");
    auto nestedStack = game::ItemConv::toStack(clone->inventory().at(5), 1);
    auto nestedUuid  = game::backpackUuid(nestedStack);
    ctx.require(nestedUuid.has_value(), "the clone still holds the nested backpack");
    ctx.check(*nestedUuid != inner->uuid(), "the nested backpack of the clone has its own storage");
    ctx.check(game::backpackUuid(game::ItemConv::toStack(bp->inventory().at(5), 1)) == inner->uuid(),
              "the original keeps its nested backpack");
    auto nestedClone = game::Storage::get().find(*nestedUuid);
    ctx.require(nestedClone != nullptr, "nested clone stored");
    ctx.check(countIn(*nestedClone, "minecraft:emerald") == 3, "nested clone has the contents");
    (void)PlacedBackpacks::get().take(site.region(), target);
    game::Storage::get().erase(e->uuid);
    game::Storage::get().erase(*nestedUuid);
});

} // namespace
} // namespace abp::placed::test

namespace abp::placed::test {
namespace {
// A real comparator behind a placed backpack reads its fill level (vanilla circuit system).
ABP_REGISTER_SELFTEST("placed.comparator_block", [](selftest::Context& ctx) -> ll::coro::CoroTask<> {
    auto& sp = ctx.spawnPlayer();
    co_await toGround(ctx, sp);
    Site site(sp, 12);
    auto pos  = site.at(0, 1);
    auto comp = site.at(0, 1, 1);
    auto wire = site.at(0, 1, 2);
    for (auto const& p : {pos, comp, wire}) site.set(p, "minecraft:air");
    site.set(site.at(0, 0, 1), "minecraft:stone");
    site.set(site.at(0, 0, 2), "minecraft:stone");
    auto bp = ctx.createBackpack(core::Tier::Leather);
    for (int s = 0; s < bp->inventory().size(); ++s) bp->inventory().set(s, core::ItemStackData::of("minecraft:cobblestone", 64));
    placeAt(ctx, site, *bp, pos);
    auto run = [&](std::string const& c) {
        ServerCommandOrigin origin("abp-selftest", site.region().getLevel().asServer(), CommandPermissionLevel::Owner, sp.getDimensionId());
        (void)ll::command::CommandRegistrar::getServerInstance().executeCommand(c, origin);
    };
    run("setblock " + std::to_string(comp.x) + " " + std::to_string(comp.y) + " " + std::to_string(comp.z)
        + " unpowered_comparator [\"minecraft:cardinal_direction\"=\"north\"]");
    run("setblock " + std::to_string(wire.x) + " " + std::to_string(wire.y) + " " + std::to_string(wire.z) + " redstone_wire");
    site.touch(comp);
    site.touch(wire);
    co_await ctx.ticks(10);
    auto signalOf = [&]() {
        auto const& w = site.region().getBlock(wire);
        auto const* s = w.getBlockType().getBlockState(HashedString("redstone_signal"));
        if (!s) return -1;
        auto v = w.getState<int>(*s);
        return static_cast<int>(v ? *v : -1);
    };
    ctx.check(PlacedBackpacks::get().comparatorSignal(site.region(), pos) == 15, "full backpack: signal 15");
    ctx.check(signalOf() == 15, "the comparator outputs 15, got " + std::to_string(signalOf()));
    for (int s = 0; s < bp->inventory().size(); ++s) bp->inventory().set(s, {});
    co_await ctx.ticks(10);
    ctx.check(signalOf() == 0, "empty backpack: off, got " + std::to_string(signalOf()));
});
} // namespace
} // namespace abp::placed::test

namespace abp::placed::test {
namespace {
// SB push reaction DESTROY: a piston breaks the block and the backpack drops (never lost).
// Netherite: blast resistance 1200 (SB), a small explosion leaves it.
ABP_REGISTER_SELFTEST("placed.piston_and_netherite", [](selftest::Context& ctx) -> ll::coro::CoroTask<> {
    auto& sp = ctx.spawnPlayer();
    co_await toGround(ctx, sp);
    Site site(sp, 14);
    // Piston in the middle (powered from below), a backpack on both sides along x: whichever way
    // its facing_direction points, one gets pushed.
    auto piston = site.at(0, 1);
    auto east   = site.at(1, 1);
    auto west   = site.at(-1, 1);
    auto power  = site.at(0, 0);
    for (auto const& p : {piston, east, west, site.at(2, 1), site.at(-2, 1)}) site.set(p, "minecraft:air");
    auto bpE = ctx.createBackpack(core::Tier::Copper);
    auto bpW = ctx.createBackpack(core::Tier::Copper);
    placeAt(ctx, site, *bpE, east);
    placeAt(ctx, site, *bpW, west);
    auto run = [&](std::string const& c) {
        ServerCommandOrigin origin("abp-selftest", site.region().getLevel().asServer(), CommandPermissionLevel::Owner, sp.getDimensionId());
        (void)ll::command::CommandRegistrar::getServerInstance().executeCommand(c, origin);
    };
    auto at = [](BlockPos const& p) { return std::to_string(p.x) + " " + std::to_string(p.y) + " " + std::to_string(p.z); };
    run("setblock " + at(piston) + " piston [\"facing_direction\"=5]");
    site.touch(piston);
    run("setblock " + at(power) + " redstone_block");
    site.touch(power);
    co_await ctx.ticks(12);
    bool eastGone = !PlacedBackpacks::isBackpackBlock(site.region().getBlock(east));
    bool westGone = !PlacedBackpacks::isBackpackBlock(site.region().getBlock(west));
    ctx.check(eastGone != westGone, "the piston destroyed one block");
    auto const& gone = eastGone ? bpE : bpW;
    ctx.check(!PlacedBackpacks::get().findUuid(gone->uuid()).has_value(), "no longer placed");
    ctx.check(site.collectBackpackDrops(eastGone ? east : west, gone->uuid()) == 1, "the backpack dropped");
    site.set(power, "minecraft:stone");
    site.set(piston, "minecraft:air");
    for (auto const& p : {east, west, site.at(2, 1), site.at(-2, 1)}) site.set(p, "minecraft:air");
    auto pos2 = site.at(0, 1, 3);
    site.set(pos2, "minecraft:air");
    auto nbp = ctx.createBackpack(core::Tier::Netherite);
    placeAt(ctx, site, *nbp, pos2);
    site.region().getLevel().explode(site.region(), nullptr, site.at(1, 1, 3).center(), 2.0f, false, true, std::numeric_limits<float>::max(), false);
    co_await ctx.ticks(3);
    ctx.check(PlacedBackpacks::isBackpackBlock(site.region().getBlock(pos2)), "a netherite backpack resists a small explosion");
});
} // namespace
} // namespace abp::placed::test
