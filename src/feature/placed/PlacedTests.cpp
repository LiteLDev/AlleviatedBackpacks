// Selftests of the placeable backpack block: placed.*
// Run with `/backpack selftest "placed.*"`.

#include <limits>

#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/DropperBlock.h"
#include "mc/world/level/block/VanillaStates.h"

#include "feature/placed/TestSite.h"
#include "game/Storage.h"
#include "selftest/SelfTest.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/jukebox/Jukebox.h"

namespace abp::placed::test {
namespace {

using core::Tier;
using selftest::Context;

core::AccessContext const kInternal{core::Source::Internal};

core::ItemStackData data(std::string_view id, int count) { return game::ItemConv::toData(game::ItemConv::make(id, count)); }

/// Places `bp` as a block at `pos` through the API (no player).
void placeAt(Context& ctx, Site& site, game::Backpack const& bp, BlockPos const& pos, int facing = 2) {
    site.touch(pos);
    auto item = ctx.backpackItem(bp);
    auto r    = PlacedBackpacks::get().place(site.region(), pos, item, facing);
    ctx.require(
        r == PlacedBackpacks::PlaceResult::Ok,
        "place() failed: " + std::to_string(static_cast<int>(r)) + " at " + pos.toString() + " block "
            + site.region().getBlock(pos).getTypeName() + " loaded " + std::to_string(site.region().areChunksFullyLoaded(pos, 0))
    );
}

constexpr int kCooldownTicks = 10; // > the per-player action cooldown

ABP_REGISTER_SELFTEST("placed.place_and_pick_up", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& sp = ctx.spawnPlayer();
    co_await toGround(ctx, sp);
    Site site(sp);
    auto ground = site.at(0, 0);
    auto target = site.at(0, 1);
    site.set(ground, "minecraft:stone");
    site.set(target, "minecraft:air");
    site.set(site.at(0, 2), "minecraft:air");

    auto bp = ctx.createBackpack(Tier::Iron);
    bp->inventory().insert(data("minecraft:diamond", 10), kInternal);
    auto item = ctx.backpackItem(*bp);
    game::ItemConv::setDisplay(item, "My Pack");
    ctx.require(sp.simulateSetItem(item, true, 0), "hold the backpack");

    // Use without sneaking does not place.
    useOn(sp, ground, 1);
    ctx.check(site.region().getBlock(target).isAir(), "plain use does not place");
    ui::MenuManager::get().close(sp);
    co_await ctx.ticks(kCooldownTicks);

    sp.simulateSneaking();
    useOn(sp, ground, 1);
    site.touch(target);
    auto const& placedBlock = site.region().getBlock(target);
    ctx.require(PlacedBackpacks::isBackpackBlock(placedBlock), "sneak + use places the backpack block");
    ctx.check(PlacedBackpacks::stateOf(placedBlock, kTierState) == static_cast<int>(Tier::Iron), "tier block state");
    ctx.check(
        PlacedBackpacks::stateOf(placedBlock, kFacingState) == PlacedBackpacks::facingTowards(sp.getRotation().y),
        "faces the player"
    );
    ctx.check(sp.getInventory().getItem(0).isNull(), "item consumed");
    auto entry = PlacedBackpacks::get().at(locationOf(site.region(), target));
    ctx.require(entry.has_value(), "position registered");
    ctx.check(entry->uuid == bp->uuid(), "position -> uuid");

    // Sneak + use with an empty hand picks it up into the hand (even while wearing another
    // backpack, whose "sneak + use opens the worn backpack" must not win on a placed backpack).
    auto worn = ctx.createBackpack(Tier::Leather);
    game::setItemAt(sp, {game::ItemLocation::Kind::Armor, static_cast<int>(SharedTypes::Legacy::ArmorSlot::Torso)}, ctx.backpackItem(*worn));
    co_await ctx.ticks(kCooldownTicks);
    useOn(sp, target, 1);
    ctx.check(ui::MenuManager::get().current(sp) == nullptr, "the worn backpack did not open");
    ctx.check(site.region().getBlock(target).isAir(), "block removed on pick up");
    ctx.check(!PlacedBackpacks::get().at(locationOf(site.region(), target)), "entry removed");
    auto const& held = sp.getInventory().getItem(0);
    ctx.require(game::isBackpack(held), "backpack back in the hand");
    ctx.check(game::backpackUuid(held) == bp->uuid(), "same uuid");
    ctx.check(held.getCustomName() == "My Pack", "custom name kept");
    ctx.check(countIn(*bp, "minecraft:diamond") == 10, "contents intact");
    ctx.check(site.collectBackpackDrops(target, bp->uuid()) == 0, "nothing dropped");
    sp.simulateStopSneaking();
});

ABP_REGISTER_SELFTEST("placed.open_gui", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& sp = ctx.spawnPlayer();
    co_await toGround(ctx, sp);
    Site site(sp);
    auto pos = site.at(0, 1);
    site.set(pos, "minecraft:air");
    auto bp = ctx.createBackpack(Tier::Gold);
    placeAt(ctx, site, *bp, pos);
    auto& mm = ui::MenuManager::get();
    useOn(sp, pos, 1); // empty hand, not sneaking
    auto menu = std::dynamic_pointer_cast<ui::BackpackMenu>(mm.current(sp));
    ctx.require(menu != nullptr, "use opens the backpack GUI");
    ctx.check(menu->backpack()->uuid() == bp->uuid(), "the placed backpack is shown");
    mm.forceShown(sp);
    // Store something through the GUI.
    ctx.require(ctx.give(sp, game::ItemConv::make("minecraft:emerald", 7), 5) == 5, "give emeralds");
    ctx.check(
        mm.simulate(sp, {ui::MenuManager::ActionType::Place, {ui::MenuManager::SlotAddress::Where::Inventory, 5}, {ui::MenuManager::SlotAddress::Where::Menu, 0}, 7}),
        "move emeralds in"
    );
    ctx.check(countIn(*bp, "minecraft:emerald") == 7, "stored in the placed backpack");
    // The GUI closes when the block goes away.
    auto item = PlacedBackpacks::get().take(site.region(), pos);
    ctx.require(item.has_value(), "take");
    co_await ctx.ticks(3);
    ctx.check(mm.current(sp) == nullptr, "GUI closed after the block was removed");
});

ABP_REGISTER_SELFTEST("placed.deposit_wins_on_containers", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& sp = ctx.spawnPlayer();
    co_await toGround(ctx, sp);
    Site site(sp);
    auto chest = site.at(0, 0);
    auto above = site.at(0, 1);
    site.set(chest, "minecraft:chest");
    site.set(above, "minecraft:air");
    site.touch(above);
    auto* c = containerAt(site.region(), chest);
    ctx.require(c != nullptr, "chest container");
    auto count = PlacedBackpacks::get().count();

    // With a Deposit upgrade, sneak + use on a chest deposits; it never places the backpack.
    auto bp = ctx.createBackpack(Tier::Iron);
    ctx.installUpgrade(bp, "deposit");
    bp->inventory().insert(data("minecraft:cobblestone", 20), kInternal);
    ctx.require(sp.simulateSetItem(ctx.backpackItem(*bp), true, 0), "hold the backpack");
    sp.simulateSneaking();
    useOn(sp, chest, 1);
    co_await ctx.ticks(2);
    ctx.check(!PlacedBackpacks::isBackpackBlock(site.region().getBlock(above)), "not placed on the chest");
    ctx.check(PlacedBackpacks::get().count() == count, "no placed backpack registered");
    ctx.check(game::isBackpack(sp.getInventory().getItem(0)), "backpack still in the hand");
    ctx.check(countIn(*c, "minecraft:cobblestone") > 0, "deposit ran instead");

    // Without such an upgrade, sneak + use on the chest places the backpack on top of it.
    co_await ctx.ticks(kCooldownTicks + 10);
    auto plain = ctx.createBackpack(Tier::Leather);
    ctx.require(sp.simulateSetItem(ctx.backpackItem(*plain), true, 0), "hold a plain backpack");
    useOn(sp, chest, 1);
    ctx.check(PlacedBackpacks::isBackpackBlock(site.region().getBlock(above)), "a plain backpack is placed on the chest");
    sp.simulateStopSneaking();
});

ABP_REGISTER_SELFTEST("placed.jukebox_plays_at_block", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace jb = upgrade::jukebox;
    auto& sp = ctx.spawnPlayer();
    co_await toGround(ctx, sp);
    Site site(sp);
    auto pos = site.at(0, 1);
    site.set(pos, "minecraft:air");
    auto bp   = ctx.createBackpack(Tier::Leather);
    int  slot = ctx.installUpgrade(bp, std::string(jb::kBasicId));
    upgrade::UpgradeContext uctx(bp, slot, &sp);
    jb::setDisc(uctx, 0, data("minecraft:music_disc_cat", 1));
    placeAt(ctx, site, *bp, pos);
    auto& svc = jb::Service::get();
    PlacedBackpacks::get().tickPlaced(1000); // reports the placed backpack to the jukebox service
    std::string reason;
    ctx.require(svc.play(uctx, &sp, std::nullopt, &reason), "play at the placed backpack: " + reason);
    co_await ctx.ticks(50); // > the service's 2 s "not reported" timeout
    ctx.check(svc.playing(bp->uuid(), slot), "keeps playing while placed (reported by the placed ticker)");
    (void)PlacedBackpacks::get().take(site.region(), pos);
    ctx.check(!svc.playing(bp->uuid(), slot), "stops when picked up");
});

ABP_REGISTER_SELFTEST("placed.viewers_closed", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& a = ctx.spawnPlayer();
    co_await toGround(ctx, a);
    auto& b = ctx.spawnPlayer();
    co_await toGround(ctx, b);
    Site site(a);
    auto pos = site.at(0, 1);
    site.set(pos, "minecraft:air");
    auto bp = ctx.createBackpack(Tier::Leather);
    placeAt(ctx, site, *bp, pos);
    auto& mm = ui::MenuManager::get();
    // A plain backpack GUI (as reopened from a settings page) follows the placed reach rules.
    ctx.require(ui::BackpackMenu::openBackpack(a, bp), "open plain GUI");
    co_await ctx.ticks(12);
    ctx.check(mm.current(a) != nullptr, "stays open while near");
    auto far = a.getFeetPos();
    a.teleport(Vec3{far.x + 30.0f, far.y, far.z}, a.getDimensionId());
    co_await ctx.ticks(12);
    ctx.check(mm.current(a) == nullptr, "closes when the viewer walks away");
    a.teleport(far, a.getDimensionId());
    co_await ctx.ticks(2);
    // Picked up by someone else: every viewer's GUI closes.
    ctx.require(PlacedBackpacks::get().open(a, pos), "open placed GUI");
    co_await ctx.ticks(10);
    ctx.require(b.getSelectedItem().isNull(), "b has an empty hand");
    ctx.require(PlacedBackpacks::get().pickUp(b, pos), "b picks it up");
    co_await ctx.ticks(1);
    ctx.check(mm.current(a) == nullptr, "a's GUI closed when b picked the backpack up");
    ctx.check(game::backpackUuid(b.getSelectedItem()) == bp->uuid(), "b holds it");
});

ABP_REGISTER_SELFTEST("placed.break_drops_once", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& sp = ctx.spawnPlayer();
    co_await toGround(ctx, sp);
    Site site(sp);
    auto pos = site.at(0, 1);
    site.set(pos, "minecraft:air");
    auto bp = ctx.createBackpack(Tier::Copper);
    bp->inventory().insert(data("minecraft:gold_ingot", 33), kInternal);
    placeAt(ctx, site, *bp, pos);

    // Survival break by a player.
    ctx.check(sp.simulateDestroyBlock(pos, ScriptModuleMinecraft::ScriptFacing::Up), "player starts mining the block");
    for (int i = 0; i < 100 && !site.region().getBlock(pos).isAir(); ++i) co_await ctx.ticks(1);
    ctx.check(site.region().getBlock(pos).isAir(), "block gone");
    ctx.check(!PlacedBackpacks::get().findUuid(bp->uuid()), "entry removed");
    ctx.check(site.collectBackpackDrops(pos, bp->uuid()) == 1, "exactly one backpack item dropped (survival)");
    ctx.check(countIn(*bp, "minecraft:gold_ingot") == 33, "contents intact");

    // Creative break.
    placeAt(ctx, site, *bp, pos);
    sp.setPlayerGameType(GameType::Creative);
    ctx.check(sp.simulateDestroyBlock(pos, ScriptModuleMinecraft::ScriptFacing::Up), "creative destroy");
    for (int i = 0; i < 20 && !site.region().getBlock(pos).isAir(); ++i) co_await ctx.ticks(1);
    ctx.check(site.region().getBlock(pos).isAir(), "creative: block gone");
    ctx.check(site.collectBackpackDrops(pos, bp->uuid()) == 1, "exactly one backpack item dropped (creative)");
    sp.setPlayerGameType(GameType::Survival);

    // Any other removal (commands, scripts, ...): the generic block-changed path.
    placeAt(ctx, site, *bp, pos);
    BlockChangeContext cc{};
    site.region().removeBlock(pos, cc);
    site.region().removeBlock(pos, cc); // a second change must not drop again
    ctx.check(site.collectBackpackDrops(pos, bp->uuid()) == 1, "exactly one backpack item dropped (removeBlock)");
    co_await ctx.ticks(1);
});

ABP_REGISTER_SELFTEST("placed.explosion_proof", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& sp = ctx.spawnPlayer();
    co_await toGround(ctx, sp);
    Site site(sp, 7);
    auto pos = site.at(0, 1);
    site.set(pos, "minecraft:air");
    auto bp = ctx.createBackpack(Tier::Leather);
    placeAt(ctx, site, *bp, pos);
    auto type = BlockType::tryGetFromRegistry(HashedString(std::string(kBlockId)));
    ctx.require(type.has_value(), "block registered");
    auto& level = site.region().getLevel();
    level.explode(site.region(), nullptr, site.at(1, 1).center(), 2.0f, false, true, std::numeric_limits<float>::max(), false);
    co_await ctx.ticks(2);
    ctx.check(PlacedBackpacks::isBackpackBlock(site.region().getBlock(pos)), "survives an explosion");
    ctx.check(PlacedBackpacks::get().findUuid(bp->uuid()).has_value(), "still registered");
    ctx.check(site.collectBackpackDrops(pos, bp->uuid()) == 0, "no drop");
    // Pistons can't move it either (`minecraft:movable` = immovable in the block definition).
});

ABP_REGISTER_SELFTEST("placed.persist_and_orphans", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& sp = ctx.spawnPlayer();
    co_await toGround(ctx, sp);
    Site site(sp);
    auto pos = site.at(0, 1);
    site.set(pos, "minecraft:air");
    auto bp = ctx.createBackpack(Tier::Diamond);
    placeAt(ctx, site, *bp, pos, 3);
    auto& placed = PlacedBackpacks::get();
    placed.reload();
    auto e = placed.at(locationOf(site.region(), pos));
    ctx.require(e.has_value(), "entry survives a reload from the world DB");
    ctx.check(e->uuid == bp->uuid() && e->facing == 3, "uuid and facing persisted");
    ctx.check(game::backpackUuid(game::ItemConv::toStack(e->item, 1)) == bp->uuid(), "item persisted");

    // A backpack block without a backpack (e.g. a picked block item) is removed.
    auto orphan = site.at(0, 1, 2);
    auto type   = BlockType::tryGetFromRegistry(HashedString(std::string(kBlockId)));
    ctx.require(type.has_value() && type->mDefaultState != nullptr, "block type");
    site.set(orphan, *type->mDefaultState);
    co_await ctx.ticks(3);
    ctx.check(site.region().getBlock(orphan).isAir(), "orphan block removed");

    // Facing math.
    ctx.check(PlacedBackpacks::facingTowards(0.0f) == 2, "yaw 0 (looking south) -> faces north");
    ctx.check(PlacedBackpacks::facingTowards(90.0f) == 3, "yaw 90 (looking west) -> faces east");
    ctx.check(PlacedBackpacks::facingTowards(180.0f) == 0, "yaw 180 -> faces south");
    ctx.check(PlacedBackpacks::facingTowards(-90.0f) == 1, "yaw -90 -> faces west");
});

ABP_REGISTER_SELFTEST("placed.refuse_and_no_copies", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& sp = ctx.spawnPlayer();
    co_await toGround(ctx, sp);
    Site site(sp);
    auto pos = site.at(0, 1);
    site.set(pos, "minecraft:stone");
    auto bp   = ctx.createBackpack(Tier::Leather);
    auto item = ctx.backpackItem(*bp);
    ctx.check(PlacedBackpacks::get().place(site.region(), pos, item, 2) == PlacedBackpacks::PlaceResult::Blocked, "occupied spot refused");
    site.set(pos, "minecraft:air");
    placeAt(ctx, site, *bp, pos);
    auto copy  = ctx.backpackItem(*bp);
    auto other = site.at(0, 1, 2);
    site.set(other, "minecraft:air");
    ctx.check(
        PlacedBackpacks::get().place(site.region(), other, copy, 2) == PlacedBackpacks::PlaceResult::Failed,
        "a copy of an already placed backpack is refused"
    );
    ctx.check(site.region().getBlock(other).isAir(), "no second block");
    auto stone = game::ItemConv::make("minecraft:stone", 1);
    ctx.check(PlacedBackpacks::get().place(site.region(), other, stone, 2) == PlacedBackpacks::PlaceResult::NotBackpack, "not a backpack");
    co_await ctx.ticks(1);
});

class PlacedTickUpgrade : public upgrade::IUpgrade {
public:
    upgrade::UpgradeInfo mInfo{
        .id           = "selftest_placed_tick",
        .itemId       = "minecraft:structure_void",
        .nameKey      = "abp.selftest.upgrade",
        .nameFallback = "Selftest Upgrade",
    };
    int  placedTicks = 0;
    int  carriedTicks = 0;
    int  lastX = 0, lastY = 0, lastZ = 0;
    bool hadPlayer = false;
    [[nodiscard]] upgrade::UpgradeInfo const& info() const override { return mInfo; }
    void onTick(upgrade::TickContext const&) override { ++carriedTicks; }
    void onPlacedTick(upgrade::PlacedTickContext const& t) override {
        ++placedTicks;
        lastX     = t.x;
        lastY     = t.y;
        lastZ     = t.z;
        hadPlayer = hadPlayer || t.ctx.player() != nullptr;
    }
};

ABP_REGISTER_SELFTEST("placed.upgrades_tick", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto                   test = std::make_shared<PlacedTickUpgrade>();
    upgrade::ScopedUpgrade scoped(test);
    auto&                  sp = ctx.spawnPlayer();
    co_await toGround(ctx, sp);
    Site site(sp);
    auto pos = site.at(0, 1);
    site.set(pos, "minecraft:air");
    auto bp = ctx.createBackpack(Tier::Leather);
    ctx.installUpgrade(bp, "selftest_placed_tick");
    placeAt(ctx, site, *bp, pos);
    co_await ctx.ticks(20);
    ctx.check(test->placedTicks > 0, "onPlacedTick runs for a placed backpack");
    ctx.check(test->carriedTicks == 0, "onTick does not run for a placed backpack");
    ctx.check(test->lastX == pos.x && test->lastY == pos.y && test->lastZ == pos.z, "tick gets the block position");
    ctx.check(!test->hadPlayer, "no carrier");
    (void)PlacedBackpacks::get().take(site.region(), pos);
    int before = test->placedTicks;
    co_await ctx.ticks(10);
    ctx.check(test->placedTicks == before, "no placed ticks after pick up");
});

ABP_REGISTER_SELFTEST("placed.vanilla_hoppers", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& sp = ctx.spawnPlayer();
    co_await toGround(ctx, sp);
    Site site(sp);
    auto below = site.at(0, 0); // hopper under the backpack: pulls from it
    auto pos   = site.at(0, 1);
    auto above = site.at(0, 2); // hopper above, pointing down: pushes into it
    site.set(site.at(0, -1), "minecraft:stone");
    site.set(below, "minecraft:hopper");
    site.set(pos, "minecraft:air");
    site.set(above, "minecraft:hopper");
    auto* top    = containerAt(site.region(), above);
    auto* bottom = containerAt(site.region(), below);
    ctx.require(top && bottom, "hopper containers");
    auto bp = ctx.createBackpack(Tier::Leather);
    bp->inventory().insert(data("minecraft:diamond", 2), kInternal);
    placeAt(ctx, site, *bp, pos);
    top->setItem(0, game::ItemConv::make("minecraft:cobblestone", 3));
    co_await ctx.ticks(120);
    int cobble   = countIn(*bp, "minecraft:cobblestone") + countIn(*bottom, "minecraft:cobblestone");
    int diamonds = countIn(*bp, "minecraft:diamond") + countIn(*bottom, "minecraft:diamond");
    ctx.check(countIn(*top, "minecraft:cobblestone") == 0, "hopper above emptied into the backpack");
    ctx.check(cobble == 3 && diamonds == 2, "no items lost or duplicated (cobble " + std::to_string(cobble) + ", diamonds " + std::to_string(diamonds) + ")");
    ctx.check(countIn(*bottom, "minecraft:diamond") == 2, "hopper below pulled the diamonds out");
});

ABP_REGISTER_SELFTEST("placed.automation_full_backpack", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& sp = ctx.spawnPlayer();
    co_await toGround(ctx, sp);
    Site site(sp);
    auto pos   = site.at(0, 1);
    auto above = site.at(0, 2);
    site.set(site.at(0, 0), "minecraft:stone");
    site.set(pos, "minecraft:air");
    site.set(above, "minecraft:hopper");
    auto* top = containerAt(site.region(), above);
    ctx.require(top != nullptr, "hopper container");
    auto bp = ctx.createBackpack(Tier::Leather);
    // Fill the backpack completely: automation can't push more in.
    for (int i = 0; i < bp->inventory().size(); ++i) bp->inventory().set(i, data("minecraft:dirt", 64));
    placeAt(ctx, site, *bp, pos);
    top->setItem(0, game::ItemConv::make("minecraft:cobblestone", 2));
    co_await ctx.ticks(40);
    ctx.check(countIn(*top, "minecraft:cobblestone") == 2, "a full backpack accepts nothing");
});

ABP_REGISTER_SELFTEST("placed.dropper", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& sp = ctx.spawnPlayer();
    co_await toGround(ctx, sp);
    Site site(sp);
    auto pos = site.at(0, 1);
    auto dp  = site.at(1, 1); // east of the backpack, facing west
    site.set(site.at(0, 0), "minecraft:stone");
    site.set(pos, "minecraft:air");
    auto dropper = Block::tryGetFromRegistry(HashedString("minecraft:dropper"));
    ctx.require(dropper.has_value(), "dropper block");
    auto west = dropper->setState(VanillaStates::FacingDirection(), 4);
    ctx.require(west.has_value(), "dropper facing west");
    site.set(dp, *west);
    auto* c = containerAt(site.region(), dp);
    ctx.require(c != nullptr, "dropper container");
    auto bp = ctx.createBackpack(Tier::Leather);
    placeAt(ctx, site, *bp, pos);
    c->setItem(0, game::ItemConv::make("minecraft:gold_ingot", 5));
    auto const& type = static_cast<DropperBlock const&>(site.region().getBlock(dp).getBlockType());
    type.dispenseFrom(site.region(), dp);
    ctx.check(countIn(*bp, "minecraft:gold_ingot") == 1, "dropper inserted one item");
    ctx.check(countIn(*c, "minecraft:gold_ingot") == 4, "dropper lost exactly one");
    ctx.check(site.dropsNear(pos).empty(), "nothing spat into the world");
    co_await ctx.ticks(1);
});

} // namespace
} // namespace abp::placed::test
