// Selftests of the wearable unit: wearable.*
//   backpack items are chest-slot wearables; worn backpacks tick upgrades and open (command,
//   double sneak); mobs spawn wearing backpacks, drop them only to players; mob backpack loot is
//   generated lazily on first open; loot chests get backpacks injected.

#include <cmath>

#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/deps/shared_types/legacy/actor/ActorDamageCause.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/deps/core/math/Color.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/item/Item.h"
#include "mc/util/LootTableUtils.h"
#include "mc/util/Random.h"
#include "mc/world/actor/ActorDamageByActorSource.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/attribute/SharedAttributes.h"
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

#include "core/WorldRules.h"
#include "feature/admin/SbConfig.h"
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
#include "upgrade/jukebox/Jukebox.h"

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

/// Hostile mobs can't spawn in peaceful: raises the difficulty for the test, restores it after.
struct DifficultyGuard {
    Level&                            level;
    SharedTypes::Legacy::Difficulty   saved;
    explicit DifficultyGuard(Level& l) : level(l), saved(l.getDifficulty()) {
        if (saved == SharedTypes::Legacy::Difficulty::Peaceful) level.setDifficulty(SharedTypes::Legacy::Difficulty::Normal);
    }
    DifficultyGuard(DifficultyGuard const&)            = delete;
    DifficultyGuard& operator=(DifficultyGuard const&) = delete;
    ~DifficultyGuard() { level.setDifficulty(saved); }
};

class ProbeUpgrade: public upgrade::IUpgrade {
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

ABP_REGISTER_SELFTEST("wearable.worn_survives_refresh", [](Context& ctx) -> ll::coro::CoroTask<> {
    // Other units periodically rewrite carried backpack items (lore, tint); the worn backpack must
    // stay worn and a mob backpack's pending loot must survive until it is opened.
    auto& player = ctx.player();
    auto  item   = makeMobBackpack(Tier::Gold, PendingLoot{.table = "chests/simple_dungeon"});
    player.setArmor(ArmorSlot::Torso, item);
    co_await ctx.ticks(30);
    auto const& worn = player.getArmor(ArmorSlot::Torso);
    ctx.check(game::backpackTierOf(worn) == Tier::Gold, "still wearing the backpack");
    ctx.check(pendingLoot(worn).has_value(), "pending loot kept while worn");
    ctx.check(!game::backpackUuid(worn), "no storage created before the first open");
    ctx.require(openWorn(player), "open");
    auto uuid = game::backpackUuid(player.getArmor(ArmorSlot::Torso));
    ctx.check(uuid.has_value() && !pendingLoot(player.getArmor(ArmorSlot::Torso)), "opened: bound and loot generated");
    ui::MenuManager::get().close(player);
    player.setArmor(ArmorSlot::Torso, ItemStack::EMPTY_ITEM());
    if (uuid) game::Storage::get().erase(*uuid);
});

ABP_REGISTER_SELFTEST("wearable.dyed_worn", [](Context& ctx) -> ll::coro::CoroTask<> {
    // A dyed backpack (dyeable item colour) is the same item id, so it renders through the same
    // attachable; wearing it must not lose the dye.
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(Tier::Iron);
    auto  item   = ctx.backpackItem(*bp);
    ctx.require(item.mItem.get() != nullptr, "item");
    item.mItem.get()->setColor(item, mce::Color(0xB02E26u));
    ctx.require(item.mItem.get()->hasCustomColor(item), "backpack is dyeable");
    player.setArmor(ArmorSlot::Torso, item);
    co_await ctx.ticks(30);
    auto const& worn = player.getArmor(ArmorSlot::Torso);
    ctx.check(game::backpackUuid(worn) == bp->uuid(), "dyed backpack still worn");
    ctx.check(worn.mItem.get() && worn.mItem.get()->hasCustomColor(worn), "dye kept while worn");
    player.setArmor(ArmorSlot::Torso, ItemStack::EMPTY_ITEM());
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

/// Restores SB's server config when a test ends.
struct SbGuard {
    admin::SbServerConfig saved = admin::sbServer();
    SbGuard()                   = default;
    SbGuard(SbGuard const&)            = delete;
    SbGuard& operator=(SbGuard const&) = delete;
    ~SbGuard() { config::mut<admin::SbServerConfig>() = saved; }
    admin::SbServerConfig::EntityBackpackAdditions& add() { return config::mut<admin::SbServerConfig>().entityBackpackAdditions; }
};

ABP_REGISTER_SELFTEST("wearable.tier_choice", [](Context& ctx) {
    SbGuard      guard;
    std::mt19937 engine(1234);
    int          low[6]{}, high[6]{};
    for (int i = 0; i < 4000; ++i) {
        ++low[static_cast<int>(rollTier(0.0f, engine))];
        ++high[static_cast<int>(rollTier(6.75f, engine))];
    }
    ctx.check(low[0] > low[1] && low[1] > low[2] && low[2] > low[3], "weights 625/250/125/25: leather most common");
    ctx.check(high[0] == 0 && high[1] == 0, "high local difficulty: iron or better");
    guard.add().localDifficultyEffectsBackpackSpawns = false;
    int any = 0;
    for (int i = 0; i < 2000; ++i) any += rollTier(6.75f, engine) == Tier::Leather ? 1 : 0;
    ctx.check(any > 0, "local difficulty ignored when localDifficultyEffectsBackpackSpawns is off");
    guard.add() = {};
    guard.add().leatherWeight = guard.add().copperWeight = guard.add().ironWeight = 0;
    guard.add().goldWeight = guard.add().diamondWeight = 0;
    ctx.check(rollTier(0.0f, engine) == Tier::Netherite, "only netherite weighs anything");
    ctx.check(isEligibleMob("minecraft:zombie") && isEligibleMob("minecraft:enderman") && !isEligibleMob("minecraft:cow"), "entityLootTableList");
    ctx.check(lootTableFor("minecraft:wither_skeleton") == "chests/nether_bridge", "loot table per entity");
    auto egg = spawnEggColors("minecraft:zombie");
    ctx.check(egg && egg->first == 0x00AFAF && egg->second == 0x799C65, "zombie spawn egg colours");
});

ABP_REGISTER_SELFTEST("wearable.mob_spawns_with_backpack", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await groundPlayer(ctx);
    DifficultyGuard difficulty(ctx.player().getLevel());
    SbGuard         guard;
    guard.add().chance       = 1.0;
    guard.add().playJukebox  = false;
    // Vanilla may give a zombie a chestplate at spawn; SB then leaves it alone. Try a few.
    Mob*      zombie = nullptr;
    ItemStack chest;
    for (int attempt = 0; attempt < 8; ++attempt) {
        zombie = spawnMob(ctx, "minecraft:zombie", 3);
        co_await ctx.ticks(2); // added one tick after the spawn (raid check)
        chest = zombie->getArmor(ArmorSlot::Torso);
        if (game::isBackpack(chest)) break;
        ctx.info("attempt " + std::to_string(attempt) + ": chest slot " + (chest.isNull() ? std::string("empty") : chest.getTypeName()));
        zombie->despawn();
    }
    ctx.check(game::isBackpack(chest), "zombie wears a backpack");
    ctx.check(isMobWorn(chest), "marked as a mob backpack");
    ctx.check(!game::backpackUuid(chest).has_value(), "no storage yet (contents are created lazily)");
    auto loot = pendingLoot(chest);
    ctx.require(loot.has_value(), "pending data");
    ctx.check(loot->mainColor == 0xFF00AFAFu && loot->accentColor == 0xFF799C65u, "spawn egg colours");
    auto tier = game::backpackTierOf(chest).value_or(Tier::Leather);
    if (core::minDifficultyOf(tier) > 0 || !loot->table.empty()) {
        ctx.check(loot->table == "chests/simple_dungeon", "zombie loot table: " + loot->table);
        ctx.check(loot->lootFactor > 0.0f && loot->lootFactor <= (core::minDifficultyOf(tier) + 2) / 3.0f + 0.01f, "loot factor");
    }
    zombie->despawn();

    auto* cow = spawnMob(ctx, "minecraft:cow", -3);
    co_await ctx.ticks(2);
    ctx.check(cow->getArmor(ArmorSlot::Torso).isNull(), "cows never get backpacks");
    cow->despawn();

    guard.add().chance = 0.0;
    auto* plain        = spawnMob(ctx, "minecraft:zombie", 3);
    co_await ctx.ticks(2);
    ctx.check(!game::isBackpack(plain->getArmor(ArmorSlot::Torso)), "chance 0: no backpack");
    plain->despawn();
});

ABP_REGISTER_SELFTEST("wearable.mob_buffs", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await groundPlayer(ctx);
    DifficultyGuard difficulty(ctx.player().getLevel());
    SbGuard         guard;
    guard.add().chance = 0.0;
    auto* zombie       = spawnMob(ctx, "minecraft:zombie", 3);
    auto  base         = [&] { return zombie->getAttribute(SharedAttributes::HEALTH()).mPtr->mDefaultMaxValue; };
    float before       = base();
    ctx.require(equipMob(*zombie, Tier::Gold, false), "equip");
    ctx.check(std::abs(base() - before * 3) < 0.01f, "gold (min difficulty 2): base max health * 3 = " + std::to_string(base()));
    ctx.check(zombie->getMaxHealth() >= static_cast<int>(before * 3), "max health includes the bonus");
    ctx.check(zombie->hasAnyEffects(), "potion buffs applied");
    bool headOk = false;
    auto const& helmet = zombie->getArmor(ArmorSlot::Head);
    for (auto id : {"minecraft:diamond_helmet", "minecraft:golden_helmet"}) headOk = headOk || helmet.getTypeName() == id;
    ctx.check(headOk, "gold tier helmet is diamond or golden: " + helmet.getTypeName());
    zombie->despawn();
});

ABP_REGISTER_SELFTEST("wearable.mob_drops_to_players_only", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await groundPlayer(ctx);
    DifficultyGuard difficulty(ctx.player().getLevel());
    SbGuard         guard;
    guard.add().chance             = 0.0;
    guard.add().backpackDropChance = 1.0; // (1 + looting) * tier multiplier >= 1: always
    guard.add().dropToFakePlayers  = true; // the test player is a simulated player
    auto& player = ctx.player();
    auto& level  = player.getLevel();

    auto* zombie = spawnMob(ctx, "minecraft:zombie", 3);
    ctx.require(equipMob(*zombie, Tier::Gold, false), "equip");
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

    // dropToFakePlayers off (SB default): a simulated player's kill drops nothing.
    guard.add().dropToFakePlayers = false;
    auto* fake = spawnMob(ctx, "minecraft:zombie", -3);
    ctx.require(equipMob(*fake, Tier::Iron, false), "equip");
    auto posFake = fake->getPosition();
    fake->die(ActorDamageByActorSource(player, SharedTypes::Legacy::ActorDamageCause::EntityAttack));
    ctx.check(backpackDropsNear(level, posFake).empty(), "no drop to fake players");

    auto* other = spawnMob(ctx, "minecraft:zombie", -3);
    ctx.require(equipMob(*other, Tier::Iron, false), "equip");
    auto pos2 = other->getPosition();
    other->die(ActorDamageSource(SharedTypes::Legacy::ActorDamageCause::Fall, ""));
    ctx.check(other->getArmor(ArmorSlot::Torso).isNull(), "backpack removed from the dead mob");
    ctx.check(backpackDropsNear(level, pos2).empty(), "not killed by a player: no backpack drop");
    co_await ctx.ticks(1);
});

ABP_REGISTER_SELFTEST("wearable.mob_jukebox", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await groundPlayer(ctx);
    DifficultyGuard difficulty(ctx.player().getLevel());
    SbGuard         guard;
    guard.add().chance = 0.0;
    auto* zombie       = spawnMob(ctx, "minecraft:zombie", 3);
    ctx.require(equipMob(*zombie, Tier::Iron, true), "equip with a jukebox");
    auto chest = zombie->getArmor(ArmorSlot::Torso);
    auto uuid  = game::backpackUuid(chest);
    ctx.require(uuid.has_value(), "jukebox mobs get their storage at spawn");
    auto bp = game::Storage::get().find(*uuid);
    ctx.require(bp != nullptr, "storage");
    ctx.check(upgrade::hasActive(*bp, "jukebox"), "a jukebox upgrade is installed");
    ctx.check(bp->data().mainColor == 0xFF00AFAFu, "colours applied to the storage");
    co_await ctx.ticks(25);
    ctx.check(upgrade::jukebox::Service::get().playing(*uuid, 0), "the mob plays its music disc");
    zombie->die(ActorDamageSource(SharedTypes::Legacy::ActorDamageCause::Fall, ""));
    co_await ctx.ticks(2);
    ctx.check(!upgrade::jukebox::Service::get().playing(*uuid, 0), "music stops with the mob");
    ctx.check(game::Storage::get().find(*uuid) == nullptr, "no drop: the storage is gone");
});

ABP_REGISTER_SELFTEST("wearable.lazy_loot", [](Context& ctx) {
    ConfigGuard guard;
    guard.cfg().mobs.upgradeChancePerSlot = 1.0f;
    guard.cfg().mobs.upgradeBlacklist.clear();
    auto                   probe = std::make_shared<ProbeUpgrade>();
    upgrade::ScopedUpgrade scoped(probe);

    PendingLoot loot;
    loot.table       = "chests/simple_dungeon";
    loot.lootFactor  = 2.0f;
    loot.seed        = 4242;
    loot.upgrades    = true;
    loot.mainColor   = 0xFF112233u;
    loot.accentColor = 0xFF445566u;
    auto item = makeMobBackpack(Tier::Iron, loot);
    ctx.require(!item.isNull(), "mob backpack item");
    ctx.check(!game::backpackUuid(item), "no uuid before the first open");
    bool changed = false;
    auto bp      = game::Storage::get().forItem(item, &changed);
    ctx.require(bp != nullptr && changed, "first open creates the storage");
    auto uuid = bp->uuid();
    ctx.check(game::backpackUuid(item) == uuid, "uuid written into the item");
    ctx.check(!pendingLoot(item) && !isMobWorn(item), "pending loot consumed");
    ctx.check(bp->data().mainColor == 0xFF112233u && bp->data().accentColor == 0xFF445566u, "colours applied");
    auto stored = bp->inventory().count([](core::ItemStackData const& s) { return !s.empty(); });
    ctx.check(stored > 0, "loot generated into the backpack (" + std::to_string(stored) + " items)");
    bool noBackpacks = bp->inventory().count([](core::ItemStackData const& s) { return game::isBackpack(s); }) == 0;
    ctx.check(noBackpacks, "backpacks are removed from generated loot (SB)");
    int installed = 0;
    for (auto const& u : bp->data().upgrades) installed += u.empty() ? 0 : 1;
    ctx.check(installed > 0, "random upgrades installed (extra, on)");
    auto again = game::Storage::get().forItem(item, &changed);
    ctx.check(again == bp && !changed, "second open reuses the backpack");
    ctx.check(bp->inventory().count([](core::ItemStackData const& s) { return !s.empty(); }) == stored, "no refill");
    game::Storage::get().erase(uuid);
});

// ---- loot chests -------------------------------------------------------------------------------

ABP_REGISTER_SELFTEST("wearable.chest_loot_tables", [](Context& ctx) {
    auto const* dungeon = injectTableFor("chests/simple_dungeon");
    ctx.require(dungeon != nullptr, "simple_dungeon has an inject table");
    ctx.check(dungeon->emptyWeight == 90 && dungeon->entries.size() == 3, "simple_dungeon: 3 entries, empty 90");
    auto const* mineshaft = injectTableFor("chests/abandoned_mineshaft");
    ctx.check(mineshaft && mineshaft->emptyWeight == 84 && mineshaft->entries.size() == 5, "abandoned_mineshaft");
    ctx.check(injectTableFor("chests/shipwrecktreasure") && injectTableFor("chests/end_city_treasure"), "Bedrock table names");
    ctx.check(injectTableFor("chests/village/village_weaponsmith") == nullptr, "only SB's tables");
    ctx.check(normalizeTableName("loot_tables/chests/simple_dungeon.json") == "chests/simple_dungeon", "normalize");
});

ABP_REGISTER_SELFTEST("wearable.chest_loot", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await groundPlayer(ctx);
    auto& common = config::mut<admin::SbCommonConfig>();
    struct Restore {
        admin::SbCommonConfig& c;
        bool                   saved;
        ~Restore() { c.chestLootEnabled = saved; }
    } restore{common, common.chestLootEnabled};
    common.chestLootEnabled = true;

    // spawn_bonus_chest: leather backpack 100, empty 0 -> always one.
    auto box = std::make_unique<CraftingContainer>(9, 3);
    for (int i = 0; i < 20; ++i) {
        box->removeAllItems();
        ctx.require(injectChestLoot("loot_tables/chests/spawn_bonus_chest.json", *box) == "alleviatedbackpacks:leather_backpack", "bonus chest backpack");
    }
    // simple_dungeon: one roll, 10% of rolls give something.
    int hits = 0;
    for (int i = 0; i < 2000; ++i) {
        box->removeAllItems();
        auto id = injectChestLoot("loot_tables/chests/simple_dungeon.json", *box);
        if (id.empty()) continue;
        ++hits;
        ctx.require(
            id == "alleviatedbackpacks:leather_backpack" || id == "alleviatedbackpacks:copper_backpack" || id == "alleviatedbackpacks:pickup_upgrade",
            "simple_dungeon only gives its entries: " + id
        );
    }
    ctx.check(hits > 120 && hits < 300, "about 10% of simple_dungeon rolls inject something: " + std::to_string(hits));
    common.chestLootEnabled = false;
    box->removeAllItems();
    ctx.check(injectChestLoot("loot_tables/chests/spawn_bonus_chest.json", *box).empty(), "chestLootEnabled off");
    common.chestLootEnabled = true;

    // Vanilla fill + injection, as a loot chest is unpacked (a double-chest sized container, so the
    // bonus chest loot leaves room for the injected backpack).
    auto& player = ctx.player();
    auto& level  = player.getLevel();
    Random random(42, false);
    box = std::make_unique<CraftingContainer>(9, 6);
    Util::LootTableUtils::fillContainer(level, *box, random, "loot_tables/chests/spawn_bonus_chest.json", DimensionType(0), nullptr);
    ctx.check(countItems(*box) > 1, "vanilla loot generated");
    ctx.check(countId(*box, "alleviatedbackpacks:leather_backpack") == 1, "backpack injected into a filled container");
    box->removeAllItems();
    {
        SuppressInjection none;
        Util::LootTableUtils::fillContainer(level, *box, random, "loot_tables/chests/spawn_bonus_chest.json", DimensionType(0), nullptr);
        ctx.check(countId(*box, "alleviatedbackpacks:leather_backpack") == 0, "no injection while suppressed");
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
    auto*      chest     = static_cast<ChestBlockActor*>(actor);
    Container& container = *chest;
    int        tries = 0, injected = 0, items = 0;
    for (; tries < 80 && injected == 0; ++tries) {
        chest->setLootTable("loot_tables/chests/simple_dungeon.json", 99 + tries);
        chest->unPackLootTable(level, container, DimensionType(0), nullptr);
        injected = countId(container, "alleviatedbackpacks:leather_backpack") + countId(container, "alleviatedbackpacks:copper_backpack")
                 + countId(container, "alleviatedbackpacks:pickup_upgrade");
        items = countItems(container);
        ctx.check(injected <= 1, "at most one injected item per chest");
        container.removeAllItems();
    }
    region.setBlock(pos, *air, 3, nullptr, BlockChangeContext{});
    ctx.check(items > 1, "chest got vanilla loot");
    ctx.check(injected == 1, "a real dungeon chest got SB's loot after " + std::to_string(tries) + " chests");
});

} // namespace
} // namespace abp::wearable
