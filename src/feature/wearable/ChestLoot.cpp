// Backpacks and upgrades in generated loot chests (dungeons, mineshafts, temples, end cities, ...).
//
// Bedrock fills loot containers through `RandomizableBlockActorContainerBase::unPackLootTable`
// (chests, barrels, ...) and `Util::LootTableUtils::fillContainer` (also chest minecarts). Both are
// hooked; after vanilla filled the container, a configured item is put into a random empty slot
// by chance (`config/wearable.json` -> chestLoot). Overriding vanilla loot table files from the
// behavior pack would replace them entirely, so a hook is used instead.

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
#include "feature/wearable/WearableConfig.h"
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

std::string injectChestLoot(std::string_view tableName, Container& container) {
    auto const& c = config::get<WearableConfig>().chestLoot;
    if (!c.enabled || injectionSuppressed()) return {};
    auto entry = c.tables.find(normalizeTableName(tableName));
    if (entry == c.tables.end() || entry->second.items.empty()) return {};
    if (std::uniform_real_distribution<double>(0.0, 1.0)(rng()) >= entry->second.chance) return {};
    int total = 0;
    for (auto const& [_, w] : entry->second.items) total += std::max(0, w);
    if (total <= 0) return {};
    int         pick = std::uniform_int_distribution<int>(0, total - 1)(rng());
    std::string id;
    for (auto const& [itemId, w] : entry->second.items) {
        if (w <= 0) continue;
        if (pick < w) {
            id = itemId;
            break;
        }
        pick -= w;
    }
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
