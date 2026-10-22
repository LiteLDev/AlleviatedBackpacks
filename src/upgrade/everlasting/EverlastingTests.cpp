// Selftests: everlasting.*

#include <cmath>
#include <vector>

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/shared_types/legacy/actor/ActorDamageCause.h"
#include "mc/world/actor/ActorFlags.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"

#include "mc/legacy/ActorUniqueID.h"
#include "mc/server/SimulatedPlayer.h"

#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/boss/WitherBoss.h"
#include "mc/world/actor/monster/EnderDragon.h"
#include "mc/world/phys/AABB.h"

#include "feature/admin/Admin.h"
#include "feature/placed/PlacedBackpacks.h"
#include "feature/placed/TestSite.h"
#include "feature/wearable/Wearable.h"
#include "feature/wearable/WearableConfig.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/Storage.h"
#include "mod/Config.h"
#include "selftest/SelfTest.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/everlasting/Everlasting.h"

namespace abp::upgrade::everlasting {
namespace {

using core::Tier;
using selftest::Context;
using Cause = SharedTypes::Legacy::ActorDamageCause;

/// Spawns dropped items for a test and removes whatever is left at the end.
class Drops {
public:
    explicit Drops(Player& player) : mPlayer(player) {}
    ~Drops() {
        for (auto const& id : mIds) {
            if (auto* a = mPlayer.getLevel().fetchEntity(id, false); a && !a->mRemoved) a->remove();
        }
    }
    Drops(Drops const&)            = delete;
    Drops& operator=(Drops const&) = delete;

    /// Spawns `stack` `dx` blocks east of the player (out of pickup reach).
    ActorUniqueID spawn(ItemStack const& stack, float dx, float dy = 1.0f) {
        auto  pos = mPlayer.getPosition() + Vec3{dx, dy, 3.0f};
        auto* actor =
            mPlayer.getLevel().getSpawner().spawnItem(mPlayer.getDimensionBlockSource(), stack, nullptr, pos, 0);
        if (!actor) {
            ++spawnFailures;
            return ActorUniqueID{};
        }
        static_cast<ItemActor*>(actor)->pickupDelay() = 32767;
        auto id                                       = actor->getOrCreateUniqueID();
        mIds.push_back(id);
        return id;
    }
    void track(ActorUniqueID id) { mIds.push_back(id); }
    int  spawnFailures = 0;

    [[nodiscard]] ItemActor* get(ActorUniqueID id) const {
        auto* a = mPlayer.getLevel().fetchEntity(id, false);
        if (!a || a->mRemoved || !a->hasType(ActorType::ItemEntity)) return nullptr;
        return static_cast<ItemActor*>(a);
    }

private:
    Player&                    mPlayer;
    std::vector<ActorUniqueID> mIds;
};

/// An everlasting backpack and a plain one (control).
struct Pair {
    std::shared_ptr<game::Backpack> everlasting;
    std::shared_ptr<game::Backpack> plain;
};
Pair makePair(Context& ctx) {
    Pair p{ctx.createBackpack(Tier::Leather), ctx.createBackpack(Tier::Leather)};
    ctx.installUpgrade(p.everlasting, std::string(kId));
    return p;
}

ABP_REGISTER_SELFTEST("everlasting.detects_backpacks", [](Context& ctx) {
    auto p = makePair(ctx);
    ctx.check(isEverlasting(ctx.backpackItem(*p.everlasting)), "backpack with the upgrade");
    ctx.check(!isEverlasting(ctx.backpackItem(*p.plain)), "backpack without the upgrade");
    ctx.check(!isEverlasting(game::makeBackpackItem(Tier::Leather)), "unbound backpack item");
    auto* type = upgrade::byId(kId);
    ctx.require(type != nullptr, "registered");
    ctx.check(!type->info().toggleable, "not toggleable");
    ctx.check(
        !upgrade::canInstall(
            p.everlasting,
            0,
            game::ItemConv::toData(game::ItemConv::make(type->info().itemId, 1)),
            nullptr
        ),
        "max one"
    );
    // Removing the upgrade turns it off.
    auto removed = upgrade::remove(p.everlasting, 0, nullptr);
    ctx.require(removed.has_value(), "removable");
    ctx.check(!isEverlasting(ctx.backpackItem(*p.everlasting)), "not everlasting after removal");
});

/// Sets a block for a test and puts air back afterwards.
class TempBlocks {
public:
    explicit TempBlocks(BlockSource& region) : mRegion(region) {}
    ~TempBlocks() {
        if (auto air = Block::tryGetFromRegistry(HashedString("minecraft:air"))) {
            for (auto const& pos : mPositions) (void)mRegion.setBlock(pos, *air, 3, nullptr, BlockChangeContext{});
        }
    }
    TempBlocks(TempBlocks const&)            = delete;
    TempBlocks& operator=(TempBlocks const&) = delete;

    bool set(BlockPos const& pos, std::string_view name) {
        auto block = Block::tryGetFromRegistry(HashedString(std::string(name)));
        if (!block) {
            error = "unknown block " + std::string(name);
            return false;
        }
        mPositions.push_back(pos);
        BlockChangeContext change{};
        change.mTickSchedulingEnabled = true;
        if (!mRegion.setBlock(pos, *block, 3, nullptr, change)) {
            error = "setBlock failed for " + std::string(name) + " at " + std::to_string(pos.x) + " "
                  + std::to_string(pos.y) + " " + std::to_string(pos.z);
            return false;
        }
        return true;
    }
    std::string error;

private:
    BlockSource&          mRegion;
    std::vector<BlockPos> mPositions;
};

// Real hazards in the world: lava, fire and an explosion.
ABP_REGISTER_SELFTEST("everlasting.world_hazards", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    co_await ctx.ticks(10);
    auto       p = makePair(ctx);
    Drops      drops(player);
    TempBlocks blocks(player.getDimensionBlockSource());
    // On the ground, 6 blocks away from the (possibly still falling) test player.
    auto const& pp     = player.getPosition();
    int         bx     = static_cast<int>(std::floor(pp.x));
    int         bz     = static_cast<int>(std::floor(pp.z)) + 6;
    auto        base   = BlockPos{bx, player.getDimensionBlockSource().getAboveTopSolidBlock(bx, bz, false, false), bz};
    auto        at     = [&](int dx) { return BlockPos{base.x + dx, base.y, base.z}; };
    auto        centre = [&](int dx) {
        auto b = at(dx);
        return Vec3{static_cast<float>(b.x) + 0.5f, static_cast<float>(b.y) + 0.2f, static_cast<float>(b.z) + 0.5f};
    };
    bool placed = blocks.set(at(-3), "minecraft:lava") && blocks.set(at(3), "minecraft:lava")
               && blocks.set(at(-6), "minecraft:fire") && blocks.set(at(6), "minecraft:fire");
    ctx.require(placed, "hazard blocks placed: " + blocks.error);
    auto spawnAt = [&](game::Backpack const& bp, int dx) {
        auto id = drops.spawn(ctx.backpackItem(bp), 0.0f);
        if (auto* a = drops.get(id)) a->teleport(centre(dx), a->getDimensionId());
        return id;
    };
    auto eLava = spawnAt(*p.everlasting, 3);
    auto nLava = spawnAt(*p.plain, -3);
    auto eFire = spawnAt(*p.everlasting, 6);
    auto nFire = spawnAt(*p.plain, -6);
    ctx.require(drops.get(eLava) && drops.get(nLava) && drops.get(eFire) && drops.get(nFire), "items spawned");
    co_await ctx.ticks(60);
    ctx.check(drops.get(eLava) != nullptr, "everlasting backpack survives lava");
    ctx.check(drops.get(eFire) != nullptr, "everlasting backpack survives fire");
    ctx.check(drops.get(nLava) == nullptr, "plain backpack burns in lava");
    ctx.check(drops.get(nFire) == nullptr, "plain backpack burns in fire");

    // Explosion right next to an everlasting and a plain backpack.
    auto eBoom = spawnAt(*p.everlasting, 10);
    auto nBoom = spawnAt(*p.plain, 11);
    ctx.require(drops.get(eBoom) && drops.get(nBoom), "explosion items spawned");
    auto mid = centre(10) + Vec3{0.5f, 0.3f, 0.0f};
    (void)player.getLevel().explode(player.getDimensionBlockSource(), nullptr, mid, 3.0f, false, false, 0.0f, true);
    co_await ctx.ticks(5);
    ctx.check(drops.get(eBoom) != nullptr, "everlasting backpack survives an explosion");
    ctx.check(drops.get(nBoom) == nullptr, "plain backpack is blown up");
});

// SB EverlastingBackpackItemEntity.ignoreExplosion: explosions don't push it either.
ABP_REGISTER_SELFTEST("everlasting.explosion_knockback", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    co_await placed::test::toGround(ctx, player); // the client's test player spawns above the world
    auto  p = makePair(ctx);
    Drops drops(player);
    auto const& pp   = player.getPosition();
    int         bx   = static_cast<int>(std::floor(pp.x)) + 14;
    int         bz   = static_cast<int>(std::floor(pp.z)) + 6;
    auto&       bs   = player.getDimensionBlockSource();
    int         y    = bs.getAboveTopSolidBlock(bx, bz, false, false);
    Vec3        spot{static_cast<float>(bx) + 0.5f, static_cast<float>(y) + 0.05f, static_cast<float>(bz) + 0.5f};
    auto        e = drops.spawn(ctx.backpackItem(*p.everlasting), 0.0f);
    ctx.require(drops.get(e) != nullptr, "everlasting item spawned");
    drops.get(e)->teleport(spot, drops.get(e)->getDimensionId());
    co_await ctx.ticks(20); // settle on the ground
    ctx.require(drops.get(e) != nullptr, "still there");
    auto before = drops.get(e)->getPosition();
    // 1.5 blocks west of it, at its height: a plain item there would be thrown east.
    (void)player.getLevel().explode(bs, nullptr, before + Vec3{-1.5f, 0.2f, 0.0f}, 3.0f, false, false, 0.0f, true);
    co_await ctx.ticks(4);
    auto* a = drops.get(e);
    ctx.require(a != nullptr, "survives the explosion");
    auto  after = a->getPosition();
    float moved = std::sqrt((after.x - before.x) * (after.x - before.x) + (after.z - before.z) * (after.z - before.z));
    ctx.info("everlasting item moved " + std::to_string(moved) + " blocks horizontally");
    ctx.check(moved < 0.05f, "not pushed by the explosion (moved " + std::to_string(moved) + ")");
});

ABP_REGISTER_SELFTEST("everlasting.indestructible", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    co_await ctx.ticks(10); // let the simulated player and its chunks load
    auto  p = makePair(ctx);
    Drops drops(player);
    auto  e = drops.spawn(ctx.backpackItem(*p.everlasting), 2.0f);
    auto  n = drops.spawn(ctx.backpackItem(*p.plain), -2.0f);
    ctx.require(
        drops.get(e) && drops.get(n),
        "items spawned (spawn failures: " + std::to_string(drops.spawnFailures) + ")"
    );
    ctx.check(drops.get(e)->isFireImmune(), "everlasting is fire immune");
    ctx.check(!drops.get(n)->isFireImmune(), "plain backpack is not fire immune");
    for (auto cause :
         {Cause::Fire,
          Cause::FireTick,
          Cause::Lava,
          Cause::Contact,
          Cause::BlockExplosion,
          Cause::EntityExplosion,
          Cause::Magic}) {
        if (auto* a = drops.get(e)) a->hurtByCause(1000.0f, cause);
    }
    if (auto* a = drops.get(n)) a->hurtByCause(1000.0f, Cause::Lava);
    co_await ctx.ticks(3);
    ctx.check(drops.get(e) != nullptr, "everlasting backpack survives fire, lava, cactus, explosions");
    ctx.check(drops.get(n) == nullptr, "plain backpack is destroyed by lava");
});

ABP_REGISTER_SELFTEST("everlasting.never_despawns", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    co_await ctx.ticks(10); // let the simulated player and its chunks load
    auto  p = makePair(ctx);
    Drops drops(player);
    auto  e = drops.spawn(ctx.backpackItem(*p.everlasting), 2.0f);
    auto  n = drops.spawn(ctx.backpackItem(*p.plain), -2.0f);
    ctx.require(
        drops.get(e) && drops.get(n),
        "items spawned (spawn failures: " + std::to_string(drops.spawnFailures) + ")"
    );
    int lifetime = drops.get(e)->lifeTime();
    ctx.info("item lifetime " + std::to_string(lifetime));
    drops.get(e)->age() = lifetime - 10;
    drops.get(n)->age() = lifetime - 10;
    co_await ctx.ticks(40);
    ctx.require(drops.get(e) != nullptr, "everlasting backpack did not despawn");
    ctx.check(drops.get(e)->age() < lifetime - 10, "age kept low");
    ctx.check(drops.get(n) == nullptr, "plain backpack despawned");
});

// SB: below `minY + 1` the backpack counts as in water: it floats up to the bottom of the world
// and then hovers there (no gravity, no motion) instead of being lost.
ABP_REGISTER_SELFTEST("everlasting.void_float", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    co_await ctx.ticks(10); // let the simulated player and its chunks load
    auto  p = makePair(ctx);
    Drops drops(player);
    auto  e = drops.spawn(ctx.backpackItem(*p.everlasting), 2.0f);
    ctx.require(drops.get(e) != nullptr, "spawned");
    co_await ctx.ticks(6); // tracked by the module
    auto&      region = player.getDimensionBlockSource();
    float      minY   = static_cast<float>(region.getMinHeight());
    auto       pos    = drops.get(e)->getPosition();
    // A void column: the flat test world has bedrock at the bottom, dig a hole through the floor.
    TempBlocks hole(region);
    int        bx = static_cast<int>(std::floor(pos.x)), bz = static_cast<int>(std::floor(pos.z));
    for (int y = region.getMinHeight(); y < region.getMinHeight() + 4; ++y) (void)hole.set(BlockPos{bx, y, bz}, "minecraft:air");
    drops.get(e)->teleport(Vec3{static_cast<float>(bx) + 0.5f, minY - 6.0f, static_cast<float>(bz) + 0.5f}, drops.get(e)->getDimensionId());
    // The out-of-world kill is ignored.
    drops.get(e)->outOfWorld();
    co_await ctx.ticks(2);
    ctx.require(drops.get(e) != nullptr, "outOfWorld() doesn't remove it");
    bool hovering = false;
    for (int i = 0; i < 40 && !hovering; ++i) {
        co_await ctx.ticks(5);
        auto* a = drops.get(e);
        ctx.require(a != nullptr, "survived the void");
        hovering = !a->getStatusFlag(ActorFlags::HasGravity);
    }
    auto* a = drops.get(e);
    ctx.check(hovering, "hovers (no gravity) once it floated out of the void");
    ctx.check(a->getPosition().y >= minY + 0.9f, "floated up to minY + 1 (y=" + std::to_string(a->getPosition().y) + ")");
    float y = a->getPosition().y;
    co_await ctx.ticks(20);
    ctx.check(drops.get(e) && std::abs(drops.get(e)->getPosition().y - y) < 0.05f, "stays where it hovers (y " + std::to_string(y) + " -> " + std::to_string(drops.get(e) ? drops.get(e)->getPosition().y : -999.0f) + ")");
    // Control: a plain backpack is killed by outOfWorld().
    auto n = drops.spawn(ctx.backpackItem(*p.plain), -2.0f);
    ctx.require(drops.get(n) != nullptr, "control spawned");
    drops.get(n)->outOfWorld();
    co_await ctx.ticks(2);
    ctx.check(drops.get(n) == nullptr, "plain backpack is lost in the void");
});

// SB: in water or lava it rises; out of it, it hovers.
ABP_REGISTER_SELFTEST("everlasting.liquid_float", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    co_await ctx.ticks(10);
    auto       p = makePair(ctx);
    Drops      drops(player);
    TempBlocks blocks(player.getDimensionBlockSource());
    auto const& pp   = player.getPosition();
    int         bx   = static_cast<int>(std::floor(pp.x)) - 8;
    int         bz   = static_cast<int>(std::floor(pp.z)) + 8;
    int         top  = player.getDimensionBlockSource().getAboveTopSolidBlock(bx, bz, false, false);
    BlockPos    lava{bx, top, bz};
    ctx.require(blocks.set(lava, "minecraft:lava"), "lava placed: " + blocks.error);
    auto id = drops.spawn(ctx.backpackItem(*p.everlasting), 0.0f);
    ctx.require(drops.get(id) != nullptr, "spawned");
    drops.get(id)->teleport(Vec3{static_cast<float>(bx) + 0.5f, static_cast<float>(top) + 0.1f, static_cast<float>(bz) + 0.5f}, drops.get(id)->getDimensionId());
    bool hovering = false;
    for (int i = 0; i < 40 && !hovering; ++i) {
        co_await ctx.ticks(5);
        auto* a = drops.get(id);
        ctx.require(a != nullptr, "survives the lava");
        hovering = !a->getStatusFlag(ActorFlags::HasGravity);
    }
    ctx.check(hovering, "floats out of the lava and hovers");
    if (auto* a = drops.get(id)) ctx.check(a->getPosition().y > static_cast<float>(top) + 0.15f, "rose in the lava (y=" + std::to_string(a->getPosition().y) + ")");
});

ABP_REGISTER_SELFTEST("everlasting.death_drop", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.spawnPlayer();
    co_await ctx.ticks(10);
    auto  p = makePair(ctx);
    Drops drops(player);
    ctx.require(ctx.give(player, ctx.backpackItem(*p.everlasting), 0) == 0, "give backpack");
    player.setPlayerGameType(GameType::Survival); // creative players drop nothing on death
    // With admin.keepBackpacksOnDeath the backpack is kept (nothing to protect): test the drop.
    struct KeepRestore {
        bool saved = config::get<admin::AdminConfig>().keepBackpacksOnDeath;
        ~KeepRestore() { config::mut<admin::AdminConfig>().keepBackpacksOnDeath = saved; }
    } keepRestore;
    config::mut<admin::AdminConfig>().keepBackpacksOnDeath = false;
    co_await ctx.ticks(2);
    player.kill();
    co_await ctx.ticks(20);
    auto findDropped = [&]() -> ItemActor* {
        for (auto* a : player.getLevel().getRuntimeActorList()) {
            if (!a || a->mRemoved || !a->hasType(ActorType::ItemEntity)) continue;
            auto& item = static_cast<ItemActor&>(*a);
            if (game::backpackUuid(item.item()) == p.everlasting->uuid()) {
                drops.track(a->getOrCreateUniqueID());
                return &item;
            }
        }
        return nullptr;
    };
    ItemActor* dropped = findDropped();
    if (!dropped && !player.isAlive() && game::isBackpack(player.getInventory().getItem(0))) {
        // The world keeps inventories on death (keepInventory): nothing drops. Check a backpack the
        // player throws instead, through the same vanilla drop path.
        ctx.info("keepInventory is on in this world: checking a thrown backpack instead");
        auto& thrower = ctx.spawnPlayer();
        co_await ctx.ticks(10);
        thrower.drop(ctx.backpackItem(*p.everlasting), false);
        co_await ctx.ticks(5);
        dropped = findDropped();
    }
    ctx.require(dropped != nullptr, "backpack dropped on death");
    auto id = dropped->getOrCreateUniqueID();
    ctx.check(dropped->isFireImmune(), "dropped backpack is everlasting");
    dropped->hurtByCause(1000.0f, Cause::Lava);
    co_await ctx.ticks(2);
    ctx.check(drops.get(id) != nullptr, "dropped backpack survives lava");
});

// A mob's backpack (U19) rolls its random upgrades lazily; once dropped, a rolled Everlasting
// upgrade must protect it right away.
ABP_REGISTER_SELFTEST("everlasting.mob_drop", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    co_await ctx.ticks(10);
    auto saved = config::get<wearable::WearableConfig>();
    struct Restore {
        wearable::WearableConfig saved;
        ~Restore() { config::mut<wearable::WearableConfig>() = saved; }
    } restore{saved};
    auto& mobs                = config::mut<wearable::WearableConfig>().mobs;
    mobs.upgradeChancePerSlot = 1.0f;
    mobs.upgradeBlacklist.clear();
    for (auto const& u : upgrade::all()) {
        if (u->info().id != kId) mobs.upgradeBlacklist.push_back(u->info().id);
    }
    Drops drops(player);
    auto  id = drops.spawn(wearable::makeMobBackpack(Tier::Leather, wearable::PendingLoot{.upgrades = true}), 2.0f);
    ctx.require(drops.get(id) != nullptr, "mob backpack dropped");
    ctx.check(drops.get(id)->isFireImmune(), "rolled Everlasting protects the dropped mob backpack");
    auto uuid = game::backpackUuid(drops.get(id)->item());
    ctx.require(uuid.has_value(), "contents generated on drop");
    drops.get(id)->hurtByCause(1000.0f, Cause::Lava);
    co_await ctx.ticks(2);
    ctx.check(drops.get(id) != nullptr, "survives lava");
    game::Storage::get().erase(*uuid);
});

// GX-2: SB BackpackBlock.canEntityDestroy is false for Everlasting backpacks: the Wither and the
// Ender Dragon can't break the placed block.
ABP_REGISTER_SELFTEST("everlasting.wither_and_dragon", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& sp = ctx.spawnPlayer();
    co_await placed::test::toGround(ctx, sp);
    auto& level = sp.getLevel();
    struct Difficulty {
        Level&                          level;
        SharedTypes::Legacy::Difficulty saved;
        ~Difficulty() { level.setDifficulty(saved); }
    } difficulty{level, level.getDifficulty()};
    level.setDifficulty(SharedTypes::Legacy::Difficulty::Normal); // bosses don't exist in peaceful

    for (std::string_view boss : {"minecraft:wither", "minecraft:ender_dragon"}) {
        placed::test::Site site(sp, boss == "minecraft:wither" ? 6 : 12);
        auto plainPos = site.at(0, 1), stonePos = site.at(1, 1), everPos = site.at(2, 1);
        for (auto const& p : {plainPos, stonePos, everPos}) site.set(p, "minecraft:air");
        site.set(stonePos, "minecraft:stone");
        auto plain = ctx.createBackpack(Tier::Leather);
        auto ever  = ctx.createBackpack(Tier::Leather);
        ctx.installUpgrade(ever, std::string(kId));
        for (auto const& [bp, pos] : {std::pair{plain, plainPos}, std::pair{ever, everPos}}) {
            site.touch(pos);
            auto item = ctx.backpackItem(*bp);
            ctx.require(
                placed::PlacedBackpacks::get().place(site.region(), pos, item, 2) == placed::PlacedBackpacks::PlaceResult::Ok,
                "placed"
            );
        }
        auto above = site.at(1, 6).center();
        auto* mob  = level.getSpawner().spawnMob(site.region(), ActorDefinitionIdentifier(std::string(boss)), nullptr, above, false, true, false);
        ctx.require(mob != nullptr, "spawned " + std::string(boss));
        AABB box{
            Vec3{static_cast<float>(plainPos.x), static_cast<float>(plainPos.y), static_cast<float>(plainPos.z)},
            Vec3{static_cast<float>(everPos.x) + 1.f, static_cast<float>(everPos.y) + 1.f, static_cast<float>(everPos.z) + 1.f}
        };
        if (boss == "minecraft:wither") {
            auto& wither = static_cast<WitherBoss&>(*static_cast<Actor*>(mob));
            for (int range : {0, 1, 2}) {
                if (site.region().getBlock(stonePos).isAir()) break;
                wither._destroyBlocks(level, box, site.region(), range, WitherBoss::WitherAttackType::Charge);
            }
        } else {
            auto& dragon = static_cast<EnderDragon&>(*static_cast<Actor*>(mob));
            (void)dragon._checkWalls(box);
        }
        co_await ctx.ticks(3);
        bool stoneGone = site.region().getBlock(stonePos).isAir();
        ctx.info(std::string(boss) + ": stone in the box broken=" + std::to_string(stoneGone) + ", plain backpack placed="
                 + std::to_string(placed::PlacedBackpacks::isBackpackBlock(site.region().getBlock(plainPos))));
        ctx.check(stoneGone, std::string(boss) + " breaks the blocks in the box");
        ctx.check(placed::PlacedBackpacks::isBackpackBlock(site.region().getBlock(everPos)), std::string(boss) + ": the Everlasting backpack stays");
        ctx.check(placed::PlacedBackpacks::get().findUuid(ever->uuid()).has_value(), "still registered");
        ctx.check(site.collectBackpackDrops(everPos, ever->uuid()) == 0, "and never dropped");
        site.collectBackpackDrops(plainPos, plain->uuid());
        mob->remove();
        co_await ctx.ticks(1);
    }
});

} // namespace
} // namespace abp::upgrade::everlasting
