// Sophisticated Backpacks' chest loot (SBInjectLootSubProvider + SBLootModifierProvider): one roll
// of a small inject table (a backpack or an upgrade, or nothing) is added to these vanilla chests,
// gated by `chestLootEnabled` (sophisticatedbackpacks-common.json). Bedrock table names:
//
//   spawn_bonus_chest    leather 100                                                (empty 0)
//   simple_dungeon       leather 5, copper 3, pickup upgrade 2                       (empty 90)
//   abandoned_mineshaft  leather 7, copper 5, iron 3, gold 1, magnet upgrade 2       (empty 84)
//   desert_pyramid       copper 5, iron 3, gold 1, magnet upgrade 2                  (empty 89)
//   shipwrecktreasure    iron 4, gold 2, advanced magnet upgrade 2                   (empty 92)
//   woodland_mansion     iron 4, gold 2, advanced magnet upgrade 2                   (empty 92)
//   nether_bridge        iron 5, gold 3, feeding upgrade 2                           (empty 90)
//   bastion_treasure     iron 3, gold 5, feeding upgrade 2                           (empty 90)
//   end_city_treasure    diamond 3, gold 5, advanced magnet upgrade 2                (empty 90)
//
// Bedrock fills loot containers through `RandomizableBlockActorContainerBase::unPackLootTable`
// (chests, barrels, ...) and `Util::LootTableUtils::fillContainer` (also chest minecarts). Both are
// hooked; after vanilla filled the container, the rolled item goes into a random empty slot (Java
// shuffles the injected item into the loot the same way). Overriding vanilla loot table files from
// the behavior pack would replace them entirely, so a hook is used instead.

#include <algorithm>
#include <map>
#include <vector>

#include "ll/api/memory/Hook.h"

#include "mc/util/LootTableUtils.h"
#include "mc/util/Random.h"
#include "mc/world/Container.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/actor/RandomizableBlockActorContainerBase.h"
#include "mc/world/level/dimension/DimensionType.h"

#include "feature/wearable/Wearable.h"
#include "feature/admin/SbConfig.h"
#include "game/ItemConv.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace abp::wearable {

namespace {
int gSuppress = 0; ///< SuppressInjection depth (server thread)
int gInUnpack = 0; ///< inside unPackLootTable: the outer hook injects
} // namespace

SuppressInjection::SuppressInjection() { ++gSuppress; }
SuppressInjection::~SuppressInjection() { --gSuppress; }
bool injectionSuppressed() { return gSuppress > 0; }

std::string normalizeTableName(std::string_view name) {
    std::string s(name);
    for (auto& ch : s) {
        if (ch == '\\') ch = '/';
    }
    if (s.starts_with("loot_tables/")) s.erase(0, 12);
    if (s.ends_with(".json")) s.erase(s.size() - 5);
    return s;
}

InjectTable const* injectTableFor(std::string_view table) {
    static std::map<std::string, InjectTable, std::less<>> const kTables{
        {"chests/spawn_bonus_chest", {{{"alleviatedbackpacks:leather_backpack", 100}}, 0}},
        {"chests/simple_dungeon",
         {{{"alleviatedbackpacks:leather_backpack", 5}, {"alleviatedbackpacks:copper_backpack", 3}, {"alleviatedbackpacks:pickup_upgrade", 2}}, 90}},
        {"chests/abandoned_mineshaft",
         {{{"alleviatedbackpacks:leather_backpack", 7},
           {"alleviatedbackpacks:copper_backpack", 5},
           {"alleviatedbackpacks:iron_backpack", 3},
           {"alleviatedbackpacks:gold_backpack", 1},
           {"alleviatedbackpacks:magnet_upgrade", 2}},
          84}},
        {"chests/desert_pyramid",
         {{{"alleviatedbackpacks:copper_backpack", 5},
           {"alleviatedbackpacks:iron_backpack", 3},
           {"alleviatedbackpacks:gold_backpack", 1},
           {"alleviatedbackpacks:magnet_upgrade", 2}},
          89}},
        {"chests/shipwrecktreasure",
         {{{"alleviatedbackpacks:iron_backpack", 4}, {"alleviatedbackpacks:gold_backpack", 2}, {"alleviatedbackpacks:advanced_magnet_upgrade", 2}}, 92}},
        {"chests/woodland_mansion",
         {{{"alleviatedbackpacks:iron_backpack", 4}, {"alleviatedbackpacks:gold_backpack", 2}, {"alleviatedbackpacks:advanced_magnet_upgrade", 2}}, 92}},
        {"chests/nether_bridge",
         {{{"alleviatedbackpacks:iron_backpack", 5}, {"alleviatedbackpacks:gold_backpack", 3}, {"alleviatedbackpacks:feeding_upgrade", 2}}, 90}},
        {"chests/bastion_treasure",
         {{{"alleviatedbackpacks:iron_backpack", 3}, {"alleviatedbackpacks:gold_backpack", 5}, {"alleviatedbackpacks:feeding_upgrade", 2}}, 90}},
        {"chests/end_city_treasure",
         {{{"alleviatedbackpacks:diamond_backpack", 3}, {"alleviatedbackpacks:gold_backpack", 5}, {"alleviatedbackpacks:advanced_magnet_upgrade", 2}}, 90}},
    };
    auto it = kTables.find(table);
    return it == kTables.end() ? nullptr : &it->second;
}

std::string injectChestLoot(std::string_view tableName, Container& container) {
    if (!admin::sbCommon().chestLootEnabled || injectionSuppressed()) return {};
    auto const* table = injectTableFor(normalizeTableName(tableName));
    if (!table) return {};
    int total = std::max(0, table->emptyWeight);
    for (auto const& e : table->entries) total += std::max(0, e.weight);
    if (total <= 0) return {};
    int         pick = std::uniform_int_distribution<int>(0, total - 1)(rng());
    std::string id;
    for (auto const& e : table->entries) {
        if (e.weight <= 0) continue;
        if (pick < e.weight) {
            id = std::string(e.item);
            break;
        }
        pick -= e.weight;
    }
    if (id.empty()) return {}; // the empty entry
    auto stack = game::ItemConv::make(id);
    if (stack.isNull()) return {};
    std::vector<int> empty;
    for (int i = 0; i < container.getContainerSize(); ++i) {
        if (container.getItem(i).isNull()) empty.push_back(i);
    }
    if (empty.empty()) return {};
    int slot = empty[std::uniform_int_distribution<std::size_t>(0, empty.size() - 1)(rng())];
    container.setItem(slot, stack);
    return id;
}

namespace {

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable" // LL hook macros declare an unused counter

LL_TYPE_INSTANCE_HOOK(
    UnpackLootTableHook,
    ll::memory::HookPriority::Normal,
    RandomizableBlockActorContainerBase,
    &RandomizableBlockActorContainerBase::unPackLootTable,
    void,
    Level&        level,
    Container&    container,
    DimensionType dimensionId,
    Actor*        actor
) {
    std::string table = *mLootTable; // vanilla clears it while unpacking
    ++gInUnpack;
    try {
        origin(level, container, dimensionId, actor);
    } catch (...) {
        --gInUnpack;
        throw;
    }
    --gInUnpack;
    if (!table.empty()) injectChestLoot(table, container);
}

LL_STATIC_HOOK(
    FillContainerHook,
    ll::memory::HookPriority::Normal,
    &Util::LootTableUtils::fillContainer,
    void,
    Level&             level,
    Container&         container,
    Random&            random,
    std::string const& tableName,
    DimensionType      dimensionId,
    Actor*             entity
) {
    origin(level, container, random, tableName, dimensionId, entity);
    if (gInUnpack == 0) injectChestLoot(tableName, container);
}

#pragma clang diagnostic pop

class ChestLoot {
public:
    void install() {
        UnpackLootTableHook::hook();
        FillContainerHook::hook();
    }
    void uninstall() {
        UnpackLootTableHook::unhook();
        FillContainerHook::unhook();
    }
};

} // namespace
} // namespace abp::wearable

ABP_REGISTER_MODULE(abp::wearable::ChestLoot);
