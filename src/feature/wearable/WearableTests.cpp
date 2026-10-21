// Selftests of the wearable unit: wearable.*
//   backpack items are chest-slot wearables; worn backpacks tick upgrades and open (command,
//   double sneak); mobs spawn wearing backpacks, drop them only to players; mob backpack loot is
//   generated lazily on first open; loot chests get backpacks injected.

#include <cmath>

#include "mc/deps/shared_types/legacy/actor/ActorDamageCause.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/util/LootTableUtils.h"
#include "mc/util/Random.h"
#include "mc/world/actor/ActorDamageByActorSource.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/inventory/CraftingContainer.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "mc/world/level/dimension/DimensionType.h"

#include "feature/wearable/Wearable.h"
#include "feature/wearable/WearableConfig.h"
#include "game/Backpack.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "game/Storage.h"
#include "mod/Config.h"
#include "selftest/SelfTest.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"

namespace abp::wearable {
namespace {

using selftest::Context;
using SharedTypes::Legacy::ArmorSlot;
using core::Tier;

/// Restores the wearable config when a test ends.
struct ConfigGuard {
    WearableConfig saved = config::get<WearableConfig>();
    ConfigGuard()        = default;
    ConfigGuard(ConfigGuard const&)            = delete;
    ConfigGuard& operator=(ConfigGuard const&) = delete;
    ~ConfigGuard() { config::mut<WearableConfig>() = saved; }
    WearableConfig& cfg() { return config::mut<WearableConfig>(); }
};

class ProbeUpgrade : public upgrade::IUpgrade {
public:
    upgrade::UpgradeInfo mInfo{
        .id           = "wearable_probe",
        .itemId       = "minecraft:structure_void",
        .nameKey      = "abp.wearable.probe",
        .nameFallback = "Wearable Probe",
    };
    int                    ticks = 0;
    game::ItemLocation     lastWhere;
    [[nodiscard]] upgrade::UpgradeInfo const& info() const override { return mInfo; }
    void onTick(upgrade::TickContext const& t) override {
        ++ticks;
        lastWhere = t.where;
    }
};

/// Test players spawn at the world spawn, which may be "unset" (far above the world, no chunks
/// loaded). Moves the player down onto the ground and waits for the chunks around it.
ll::coro::CoroTask<> groundPlayer(Context& ctx) {
    auto&    p   = ctx.player();
    auto&    bs  = p.getDimensionBlockSource();
    auto     pos = p.getPosition();
    BlockPos column{static_cast<int>(std::floor(pos.x)), 0, static_cast<int>(std::floor(pos.z))};
    if (pos.y < bs.getMaxHeight() && bs.hasChunksAt(column, 2, false)) co_return;
    p.teleport(Vec3{pos.x, 0.0f, pos.z}, p.getDimensionId());
    for (int i = 0; i < 100 && !bs.hasChunksAt(column, 2, false); ++i) co_await ctx.ticks(2);
    ctx.require(bs.hasChunksAt(column, 2, false), "chunks around the test player did not load");
    auto y = bs.getAboveTopSolidBlock(column.x, column.z, false, false);
    p.teleport(Vec3{pos.x, static_cast<float>(y), pos.z}, p.getDimensionId());
    co_await ctx.ticks(2);
}

/// A position on the ground next to the player.
Vec3 near(Player& player, float dx) {
    auto  p  = player.getPosition();
    float x  = p.x + dx;
    auto& bs = player.getDimensionBlockSource();
    auto  y  = bs.getAboveTopSolidBlock(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(p.z)), false, false);
    return Vec3{x, static_cast<float>(y), p.z};
}

Mob* spawnMob(Context& ctx, std::string const& id, float dx) {
    auto& player = ctx.player();
    auto& level  = player.getLevel();
    auto* mob    = level.getSpawner().spawnMob(
        player.getDimensionBlockSource(),
        ActorDefinitionIdentifier(id),
        nullptr,
        near(player, dx),
        false,
        true,
        false
    );
    if (!mob) {
        auto p = near(player, dx);
        ctx.info("spawn pos " + std::to_string(p.x) + " " + std::to_string(p.y) + " " + std::to_string(p.z));
        ctx.fail(
            "could not spawn " + id + " (difficulty " + std::to_string(static_cast<int>(level.getDifficulty()))
            + "; hostile mobs need difficulty > peaceful)"
        );
    }
    return mob;
}

std::vector<ItemActor*> backpackDropsNear(Level& level, Vec3 const& pos) {
    std::vector<ItemActor*> out;
    for (auto* a : level.getRuntimeActorList()) {
        if (!a || a->getEntityTypeId() != ActorType::ItemEntity) continue;
        auto const& p = a->getPosition();
        if (std::abs(p.x - pos.x) > 4 || std::abs(p.z - pos.z) > 4 || std::abs(p.y - pos.y) > 6) continue;
        auto* ia = static_cast<ItemActor*>(a);
        if (game::isBackpack(ia->item())) out.push_back(ia);
    }
    return out;
}

int countId(Container& c, std::string_view id) {
    int n = 0;
    for (int i = 0; i < c.getContainerSize(); ++i) {
        auto const& it = c.getItem(i);
        if (!it.isNull() && it.getTypeName() == id) n += it.mCount;
    }
    return n;
}

int countItems(Container& c) {
    int n = 0;
    for (int i = 0; i < c.getContainerSize(); ++i) {
        if (!c.getItem(i).isNull()) ++n;
    }
    return n;
}

// ---- worn backpacks ----------------------------------------------------------------------------

ABP_REGISTER_SELFTEST("wearable.item_is_wearable", [](Context& ctx) {
    for (auto const& info : core::kTiers) {
        auto item = game::makeBackpackItem(info.tier);
        ctx.require(!item.isNull(), "backpack item exists");
        ctx.check(item.isHumanoidWearableItem(), std::string(info.id) + ": not wearable (BP minecraft:wearable missing)");
        ctx.check(item.getArmorSlot() == ArmorSlot::Torso, std::string(info.id) + ": not a chest-slot wearable");
    }
});

ABP_REGISTER_SELFTEST("wearable.worn_ticks_and_gui", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto                   probe = std::make_shared<ProbeUpgrade>();
    upgrade::ScopedUpgrade scoped(probe);
    auto&                  player = ctx.player();
    auto&                  mm     = ui::MenuManager::get();
    auto                   bp     = ctx.createBackpack(Tier::Iron);
    ctx.installUpgrade(bp, "wearable_probe");
    player.setArmor(ArmorSlot::Torso, ctx.backpackItem(*bp));
    auto carried = game::carriedBackpacks(player);
    ctx.check(
        carried.size() == 1 && carried[0].first.where == wornSlot() && carried[0].second == bp,
        "the worn backpack is found by the player scan"
    );
    co_await ctx.ticks(25);
    ctx.check(probe->ticks > 0, "upgrades tick while the backpack is worn");
    ctx.check(probe->lastWhere == wornSlot(), "tick location is the chest slot");

    ctx.require(openWorn(player), "open the worn backpack");
    co_await ctx.ticks(5);
    ctx.check(mm.current(player) != nullptr && mm.isShown(player), "menu shown for the worn backpack");
    // Taking the backpack off closes its menu.
    player.setArmor(ArmorSlot::Torso, ItemStack::EMPTY_ITEM());
    co_await ctx.ticks(3);
    ctx.check(mm.current(player) == nullptr, "menu closed when the backpack is taken off");
    ctx.check(!openWorn(player), "nothing to open without a worn backpack");
});

ABP_REGISTER_SELFTEST("wearable.use_opens_not_equips", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& sp = static_cast<SimulatedPlayer&>(ctx.player());
    auto& mm = ui::MenuManager::get();
    auto  bp = ctx.createBackpack(Tier::Copper);
    ctx.require(ctx.give(sp, ctx.backpackItem(*bp), sp.getSelectedItemSlot()) >= 0, "give backpack");
    sp.simulateUseItem();
    co_await ctx.ticks(5);
    ctx.check(mm.current(sp) != nullptr, "using a held backpack opens it");
    ctx.check(sp.getArmor(ArmorSlot::Torso).isNull(), "using a held backpack does not equip it");
    ctx.check(game::isBackpack(sp.getSelectedItem()), "the backpack stays in the hand");
    mm.close(sp);
});

ABP_REGISTER_SELFTEST("wearable.double_sneak_opens", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard guard;
    guard.cfg().worn.doubleSneakToOpen      = true;
    guard.cfg().worn.doubleSneakWindowTicks = 10;
    auto& sp = static_cast<SimulatedPlayer&>(ctx.player());
    auto& mm = ui::MenuManager::get();
    auto  bp = ctx.createBackpack(Tier::Leather);
    sp.setArmor(ArmorSlot::Torso, ctx.backpackItem(*bp));
    co_await ctx.ticks(2);
    // Two slow presses: nothing.
    sp.simulateSneaking();
    co_await ctx.ticks(2);
    sp.simulateStopSneaking();
    co_await ctx.ticks(15);
    sp.simulateSneaking();
    co_await ctx.ticks(2);
    ctx.check(mm.current(sp) == nullptr, "slow presses don't open the backpack");
    // A quick second press opens it.
    sp.simulateStopSneaking();
    co_await ctx.ticks(2);
    sp.simulateSneaking();
    co_await ctx.ticks(2);
    ctx.check(mm.current(sp) != nullptr, "double sneak opens the worn backpack");
    sp.simulateStopSneaking();
    mm.close(sp);
    sp.setArmor(ArmorSlot::Torso, ItemStack::EMPTY_ITEM());
});

// ---- mobs --------------------------------------------------------------------------------------

ABP_REGISTER_SELFTEST("wearable.tier_rolls", [](Context& ctx) {
    ConfigGuard  guard;
    std::mt19937 engine(1234);
    int          counts[3][6]{};
    for (int dim = 0; dim < 3; ++dim) {
        for (int i = 0; i < 2000; ++i) {
            auto t = rollTier(dim, 2, engine);
            ctx.require(t.has_value(), "a tier is always rolled");
            ++counts[dim][static_cast<int>(*t)];
        }
    }
    ctx.check(counts[0][0] > counts[0][3], "overworld: leather is more common than gold");
    ctx.check(counts[1][0] == 0 && counts[1][1] == 0, "nether: no leather/copper backpacks");
    ctx.check(counts[2][4] > counts[2][2], "end: diamond is more common than iron");
    // Hard difficulty favours better tiers.
    int normalGold = 0, hardGold = 0;
    for (int i = 0; i < 4000; ++i) {
        if (static_cast<int>(*rollTier(0, 2, engine)) >= 3) ++normalGold;
        if (static_cast<int>(*rollTier(0, 3, engine)) >= 3) ++hardGold;
    }
    ctx.check(hardGold > normalGold, "hard rolls better tiers (" + std::to_string(hardGold) + " vs " + std::to_string(normalGold) + ")");
    ctx.check(spawnChance(3) > spawnChance(1), "hard has a higher spawn chance than easy");
    guard.cfg().mobs.chance = 5.0f;
    ctx.check(spawnChance(2) == 1.0, "chance is clamped to 1");
    ctx.check(isEligibleMob("minecraft:zombie") && !isEligibleMob("minecraft:cow"), "eligible mobs from config");
});

ABP_REGISTER_SELFTEST("wearable.mob_spawns_with_backpack", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await groundPlayer(ctx);
    ConfigGuard guard;
    auto&       c = guard.cfg().mobs;
    c.enabled     = true;
    c.chance      = 1.0f;
    c.difficultyMultiplier.clear();
    c.equipArmor      = true;
    c.buffWithEffects = true;
    auto* zombie      = spawnMob(ctx, "minecraft:zombie", 3);
    auto  chest       = zombie->getArmor(ArmorSlot::Torso);
    ctx.check(game::isBackpack(chest), "zombie wears a backpack");
    ctx.check(isMobWorn(chest), "marked as a mob backpack");
    ctx.check(!game::backpackUuid(chest).has_value(), "no storage yet (contents are created lazily)");
    auto loot = pendingLoot(chest);
    ctx.check(loot && loot->table == "chests/simple_dungeon", "pending loot table from config");
    ctx.check(!zombie->getArmor(ArmorSlot::Feet).isNull(), "matching armor equipped");
    zombie->despawn();

    auto* cow = spawnMob(ctx, "minecraft:cow", -3);
    ctx.check(cow->getArmor(ArmorSlot::Torso).isNull(), "cows never get backpacks");
    cow->despawn();

    c.chance     = 0.0f;
    auto* plain  = spawnMob(ctx, "minecraft:zombie", 3);
    ctx.check(!game::isBackpack(plain->getArmor(ArmorSlot::Torso)), "chance 0: no backpack");
    plain->despawn();
});

ABP_REGISTER_SELFTEST("wearable.mob_drops_to_players_only", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await groundPlayer(ctx);
    ConfigGuard guard;
    guard.cfg().mobs.chance                      = 0.0f;
    guard.cfg().mobs.dropChanceNotKilledByPlayer = 0.0f;
    auto& player = ctx.player();
    auto& level  = player.getLevel();

    auto* zombie = spawnMob(ctx, "minecraft:zombie", 3);
    ctx.require(equipMob(*zombie, Tier::Gold), "equip");
    auto pos = zombie->getPosition();
    for (auto* old : backpackDropsNear(level, pos)) old->remove();
    zombie->die(ActorDamageByActorSource(player, SharedTypes::Legacy::ActorDamageCause::EntityAttack));
    ctx.check(zombie->getArmor(ArmorSlot::Torso).isNull(), "backpack left the chest slot");
    auto drops = backpackDropsNear(level, pos);
    ctx.check(drops.size() == 1, "killed by a player: the backpack drops (" + std::to_string(drops.size()) + ")");
    if (!drops.empty()) {
        auto const& item = drops[0]->item();
        ctx.check(game::backpackTierOf(item) == Tier::Gold, "dropped the gold backpack");
        ctx.check(!isMobWorn(item) && pendingLoot(item).has_value(), "drop keeps its pending loot, loses the mob marker");
    }
    for (auto* d : drops) d->remove();

    auto* other = spawnMob(ctx, "minecraft:zombie", -3);
    ctx.require(equipMob(*other, Tier::Iron), "equip");
    auto pos2 = other->getPosition();
    other->die(ActorDamageSource(SharedTypes::Legacy::ActorDamageCause::Fall, ""));
    ctx.check(other->getArmor(ArmorSlot::Torso).isNull(), "backpack removed from the dead mob");
    ctx.check(backpackDropsNear(level, pos2).empty(), "not killed by a player: no backpack drop");
    co_await ctx.ticks(1);
});

ABP_REGISTER_SELFTEST("wearable.lazy_loot", [](Context& ctx) {
    ConfigGuard guard;
    guard.cfg().mobs.upgradeChancePerSlot = 1.0f;
    guard.cfg().mobs.upgradeBlacklist.clear();
    auto                   probe = std::make_shared<ProbeUpgrade>();
    upgrade::ScopedUpgrade scoped(probe);

    PendingLoot loot{.table = "chests/simple_dungeon", .rolls = 2, .dimension = 0, .seed = 4242, .upgrades = true};
    auto        item = makeMobBackpack(Tier::Iron, loot);
    ctx.require(!item.isNull(), "mob backpack item");
    ctx.check(!game::backpackUuid(item), "no uuid before the first open");
    bool changed = false;
    auto bp      = game::Storage::get().forItem(item, &changed);
    ctx.require(bp != nullptr && changed, "first open creates the storage");
    auto uuid = bp->uuid();
    ctx.check(game::backpackUuid(item) == uuid, "uuid written into the item");
    ctx.check(!pendingLoot(item) && !isMobWorn(item), "pending loot consumed");
    auto stored = bp->inventory().count([](core::ItemStackData const& s) { return !s.empty(); });
    ctx.check(stored > 0, "loot generated into the backpack (" + std::to_string(stored) + " items)");
    int installed = 0;
    for (auto const& u : bp->data().upgrades) installed += u.empty() ? 0 : 1;
    ctx.check(installed > 0, "random upgrades installed");
    auto again = game::Storage::get().forItem(item, &changed);
    ctx.check(again == bp && !changed, "second open reuses the backpack");
    ctx.check(bp->inventory().count([](core::ItemStackData const& s) { return !s.empty(); }) == stored, "no refill");
    game::Storage::get().erase(uuid);
});

// ---- loot chests -------------------------------------------------------------------------------

ABP_REGISTER_SELFTEST("wearable.chest_loot", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await groundPlayer(ctx);
    ConfigGuard guard;
    guard.cfg().chestLoot.enabled = true;
    guard.cfg().chestLoot.tables["chests/simple_dungeon"] =
        WearableConfig::ChestLootEntry{1.0f, {{"alleviatedbackpacks:iron_backpack", 1}}};
    ctx.check(normalizeTableName("loot_tables/chests/simple_dungeon.json") == "chests/simple_dungeon", "normalize");

    auto& player = ctx.player();
    auto& level  = player.getLevel();
    auto  box    = std::make_unique<CraftingContainer>(9, 3);
    Random random(42, false);
    Util::LootTableUtils::fillContainer(level, *box, random, "loot_tables/chests/simple_dungeon.json", DimensionType(0), nullptr);
    ctx.check(countItems(*box) > 1, "vanilla loot generated");
    ctx.check(countId(*box, "alleviatedbackpacks:iron_backpack") == 1, "backpack injected into a filled container");
    box->removeAllItems();
    {
        SuppressInjection none;
        Util::LootTableUtils::fillContainer(level, *box, random, "loot_tables/chests/simple_dungeon.json", DimensionType(0), nullptr);
        ctx.check(countId(*box, "alleviatedbackpacks:iron_backpack") == 0, "no injection while suppressed");
    }
    box->removeAllItems();

    // A real chest block with a loot table, unpacked like when a player opens it.
    auto& region = player.getDimensionBlockSource();
    auto  p      = near(player, 2);
    BlockPos pos{static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)), static_cast<int>(std::floor(p.z))};
    auto chestBlock = Block::tryGetFromRegistry(HashedString("minecraft:chest"));
    auto air        = Block::tryGetFromRegistry(HashedString("minecraft:air"));
    ctx.require(chestBlock && air, "chest/air blocks");
    bool placed = region.setBlock(pos, *chestBlock, 3, nullptr, BlockChangeContext{});
    auto* actor = region.getBlockEntity(pos);
    ctx.require(
        actor && actor->getType() == BlockActorType::Chest,
        "chest block actor (placed=" + std::to_string(placed) + " block=" + region.getBlock(pos).getTypeName()
            + " actor=" + std::to_string(actor != nullptr) + ")"
    );
    auto* chest = static_cast<ChestBlockActor*>(actor);
    chest->setLootTable("loot_tables/chests/simple_dungeon.json", 99);
    Container& container = *chest;
    chest->unPackLootTable(level, container, DimensionType(0), nullptr);
    int backpacks = countId(container, "alleviatedbackpacks:iron_backpack");
    int items     = countItems(container);
    container.removeAllItems();
    region.setBlock(pos, *air, 3, nullptr, BlockChangeContext{});
    ctx.check(items > 1, "chest got vanilla loot");
    ctx.check(backpacks == 1, "chest got exactly one backpack (" + std::to_string(backpacks) + ")");
});

} // namespace
} // namespace abp::wearable
