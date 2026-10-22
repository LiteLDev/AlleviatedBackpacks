// Backpacks on spawned mobs: Sophisticated Backpacks' EntityBackpackAdditionHandler, with the
// settings of `sophisticatedbackpacks-server.json` -> entityBackpackAdditions.
//
// - Spawn: a mob listed in `entityLootTableList` whose chest slot is empty gets a backpack with a
//   1 / (int)(1 / chance) chance, unless it is a raider of a raid. The tier is a weighted roll
//   (625/250/125/25/5/1), restricted to copper+ / iron+ at mid / high local difficulty.
// - Its colours are the mob's spawn egg colours; loot from the mob's loot table is added lazily
//   on first open (partial rolls minDifficulty..minDifficulty+2, loot factor rolls / 3); 1 in 4
//   get a jukebox upgrade (advanced 25%) playing random music discs.
// - The mob gets difficulty + 2 random potion buffs for 30 minutes, + base max health *
//   minDifficulty, and tier-weighted helmet / leggings / boots (enchanted 1 in 6 - minDifficulty).
// - Drop: only to a player kill, chance (backpackDropChance + looting * lootingChanceIncrease) *
//   tier multiplier (1 / 1.25 / 1.5 / 3 / 4.5 / 6). Otherwise the backpack's storage goes away.
// - A mob leaving the level loses its backpack's storage, unless it is converting.
//
// Backpack contents are generated lazily (Storage create listener), so mobs that despawn cost
// nothing; only jukebox mobs get their storage at spawn (the jukebox must play).

#include <algorithm>
#include <bitset>
#include <cmath>
#include <exception>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/entity/MobDieEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/event/world/SpawnMobEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/util/LootTableUtils.h"
#include "mc/util/Random.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/ai/village/Raid.h"
#include "mc/world/actor/ai/village/Village.h"
#include "mc/world/actor/ai/village/VillageManager.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/attribute/BaseAttributeMap.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/effect/EffectDuration.h"
#include "mc/world/effect/MobEffect.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/inventory/CraftingContainer.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/phys/AABB.h"
#include "mc/world/level/BlockSource.h"

#include "nlohmann/json.hpp"

#include "core/WorldRules.h"
#include "feature/admin/AccessLog.h"
#include "feature/admin/SbConfig.h"
#include "feature/wearable/Wearable.h"
#include "feature/wearable/WearableConfig.h"
#include "game/Backpack.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/Storage.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/jukebox/Discs.h"
#include "upgrade/jukebox/Jukebox.h"

ABP_REGISTER_CONFIG("wearable", abp::wearable::WearableConfig);

namespace abp::wearable {

using SharedTypes::Legacy::ArmorSlot;

namespace {

constexpr int kPotionDurationTicks = 30 * 60 * 20; ///< SB: 30 minutes
constexpr int kMaxDifficulty       = 3;            ///< SB: MAX_DIFFICULTY

admin::SbServerConfig::EntityBackpackAdditions const& additions() { return admin::sbServer().entityBackpackAdditions; }

int nextInt(int bound) { return bound <= 1 ? 0 : std::uniform_int_distribution<int>(0, bound - 1)(rng()); }
double uniform() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng()); }

void setUserData(ItemStack& stack, std::function<void(CompoundTag&)> const& edit) {
    auto tag = stack.mUserData ? stack.mUserData->clone() : std::make_unique<CompoundTag>();
    edit(*tag);
    stack.setUserData(std::move(tag));
}

bool hasByte(ItemStack const& stack, std::string_view key) {
    return !stack.isNull() && stack.mUserData && stack.mUserData->contains(key, Tag::Type::Byte);
}

BaseAttributeMap& attributesOf(Mob& mob) { return const_cast<BaseAttributeMap&>(*static_cast<Actor&>(mob).getAttributes()); }

// ---- loot -----------------------------------------------------------------------------------------

void installRandomUpgrades(std::shared_ptr<game::Backpack> const& bp) {
    auto const&                     c = config::get<WearableConfig>().mobs;
    std::vector<upgrade::IUpgrade*> pool;
    for (auto const& u : upgrade::all()) {
        auto const& info = u->info();
        if (std::find(c.upgradeBlacklist.begin(), c.upgradeBlacklist.end(), info.id) != c.upgradeBlacklist.end()) continue;
        pool.push_back(u.get());
    }
    if (pool.empty()) return;
    for (int slot = 0; slot < static_cast<int>(bp->data().upgrades.size()); ++slot) {
        if (!bp->data().upgrades[static_cast<std::size_t>(slot)].empty()) continue;
        if (uniform() >= c.upgradeChancePerSlot) continue;
        std::shuffle(pool.begin(), pool.end(), rng());
        for (auto* u : pool) {
            auto stack = game::ItemConv::make(u->info().itemId);
            if (stack.isNull()) continue;
            auto item = game::ItemConv::toData(stack).withCount(1);
            if (!upgrade::canInstall(bp, slot, item, nullptr)) continue;
            if (upgrade::install(bp, slot, item, nullptr)) break;
        }
    }
}

/// SB fillWithLootFromTable: while the factor is > 0, roll the table, drop backpacks from the
/// result and keep a random size * min(1, factor) of the stacks; then LootHelper.fillWithLoot puts
/// them into random empty slots (overflow is voided).
std::vector<core::ItemStackData> rollLoot(PendingLoot const& loot) {
    std::vector<core::ItemStackData> out;
    auto                             level = ll::service::getLevel();
    if (!level || loot.table.empty() || loot.lootFactor <= 0) return out;
    SuppressInjection noInject;
    auto              box    = std::make_unique<CraftingContainer>(9, 3);
    Random            random(loot.seed, false);
    std::mt19937      pick(loot.seed);
    std::string       table  = "loot_tables/" + loot.table + ".json";
    float             factor = loot.lootFactor;
    while (factor > 0) {
        box->removeAllItems();
        Util::LootTableUtils::fillContainer(*level, *box, random, table, DimensionType(loot.dimension), nullptr);
        std::vector<core::ItemStackData> generated;
        for (int i = 0; i < box->getContainerSize(); ++i) {
            auto const& it = box->getItem(i);
            if (it.isNull() || game::isBackpack(it)) continue;
            generated.push_back(game::ItemConv::toData(it));
        }
        std::shuffle(generated.begin(), generated.end(), pick);
        auto n = static_cast<std::size_t>(static_cast<float>(generated.size()) * std::min(1.0f, factor));
        for (std::size_t i = 0; i < n && i < generated.size(); ++i) out.push_back(std::move(generated[i]));
        factor -= 1.0f;
    }
    box->removeAllItems();
    return out;
}

void putIntoRandomEmptySlots(game::Backpack& bp, std::vector<core::ItemStackData> loot, unsigned seed) {
    auto&            inv = bp.inventory();
    std::vector<int> empty;
    for (int i = 0; i < inv.size(); ++i) {
        if (inv.at(i).empty()) empty.push_back(i);
    }
    std::mt19937 shuffle(seed ^ 0x5bd1e995u);
    std::shuffle(empty.begin(), empty.end(), shuffle);
    for (auto& stack : loot) {
        if (empty.empty()) {
            log().warn("wearable: too much loot to add to backpack {}; overflow is voided", bp.uuid());
            return;
        }
        if (stack.empty()) continue;
        inv.set(empty.back(), std::move(stack));
        empty.pop_back();
    }
}

// ---- spawn eggs --------------------------------------------------------------------------------------

struct EggColors {
    std::string_view type;
    std::uint32_t    background;
    std::uint32_t    highlight;
};

/// Vanilla spawn egg colours (Java SpawnEggItem; the same colours as Bedrock's eggs).
constexpr EggColors kEggColors[] = {
    {"minecraft:creeper",            0x0DA70B, 0x000000},
    {"minecraft:drowned",            0x8FF1D7, 0x799C65},
    {"minecraft:enderman",           0x161616, 0x000000},
    {"minecraft:evocation_illager",  0x959B9B, 0x1E1C1A},
    {"minecraft:husk",               0x797061, 0xE6CC94},
    {"minecraft:piglin",             0x995F40, 0xF9F3A4},
    {"minecraft:piglin_brute",       0x592A10, 0xF9F3A4},
    {"minecraft:pillager",           0x532F36, 0x959B9B},
    {"minecraft:skeleton",           0xC1C1C1, 0x494949},
    {"minecraft:stray",              0x617677, 0xDDEAEA},
    {"minecraft:vex",                0x7A90A4, 0xE8EDF1},
    {"minecraft:vindicator",         0x959B9B, 0x275E61},
    {"minecraft:witch",              0x340000, 0x51A03E},
    {"minecraft:wither_skeleton",    0x141414, 0x474D4D},
    {"minecraft:zombie",             0x00AFAF, 0x799C65},
    {"minecraft:zombie_villager_v2", 0x563C33, 0x799C65},
    {"minecraft:zombie_villager",    0x563C33, 0x799C65},
    {"minecraft:zombie_pigman",      0xEA9393, 0x4C7129},
    {"minecraft:bogged",             0x8A9C72, 0x314D1B},
    {"minecraft:spider",             0x342D27, 0xA80E0E},
    {"minecraft:cave_spider",        0x0C424E, 0xA80E0E},
    {"minecraft:blaze",              0xF6B201, 0xFFF87E},
    {"minecraft:ravager",            0x757470, 0x5B5049},
    {"minecraft:silverfish",         0x6E6E6E, 0x303030},
    {"minecraft:slime",              0x51A03E, 0x7EBF6E},
};

// ---- equipment ----------------------------------------------------------------------------------------

/// SB's potion pool: one of resistance / regeneration (max amplifier 1), fire resistance,
/// absorption, health boost, speed, strength.
struct ApplicableEffect {
    std::vector<std::string_view> effects;
    int                           maxAmplifier;
};

void applyPotions(Mob& mob, int difficulty, int minDifficulty) {
    if (!additions().buffWithPotionEffects) return;
    static std::vector<ApplicableEffect> const kEffects{
        {{"resistance", "regeneration"}, 1},
        {{"fire_resistance"}, 1 << 30},
        {{"absorption"}, 1 << 30},
        {{"health_boost"}, 1 << 30},
        {{"speed"}, 1 << 30},
        {{"strength"}, 1 << 30},
    };
    auto pool = kEffects;
    std::shuffle(pool.begin(), pool.end(), rng());
    auto n = std::min<std::size_t>(pool.size(), static_cast<std::size_t>(difficulty + 2));
    for (std::size_t i = 0; i < n; ++i) {
        auto const& e         = pool[i];
        int         amplifier = std::min(std::max(minDifficulty, nextInt(difficulty + 1)), e.maxAmplifier);
        auto        name      = e.effects[static_cast<std::size_t>(nextInt(static_cast<int>(e.effects.size())))];
        auto const* effect    = MobEffect::getByName(std::string(name));
        if (!effect) continue;
        MobEffectInstance instance(effect->mId, EffectDuration{kPotionDurationTicks});
        instance.mAmplifier = amplifier;
        mob.addEffect(instance);
    }
}

/// Base max health without effects / modifiers (SB: maxHealth.getBaseValue()).
float baseMaxHealth(Mob& mob) {
    auto ref = mob.getAttribute(SharedAttributes::HEALTH());
    return ref.mPtr ? ref.mPtr->mDefaultMaxValue : static_cast<float>(mob.getMaxHealth());
}

void raiseHealth(Mob& mob, int minDifficulty) {
    if (!additions().buffHealth) return;
    float base     = baseMaxHealth(mob);
    float addition = base * static_cast<float>(minDifficulty);
    auto& attrs    = attributesOf(mob);
    if (addition > 0.1f) (void)attrs.setMaxValue(SharedAttributes::HEALTH(), base + addition);
    (void)attrs.setCurrentValue(SharedAttributes::HEALTH(), static_cast<float>(mob.getMaxHealth()));
}

std::string armorId(int material, std::string_view piece) {
    static constexpr std::string_view kMaterials[] = {"netherite", "diamond", "golden", "iron", "leather"};
    return "minecraft:" + std::string(kMaterials[std::clamp(material, 0, 4)]) + "_" + std::string(piece);
}

void equipArmorPiece(Mob& mob, int minDifficulty, core::ArmorRange range, ArmorSlot slot, std::string_view piece, float special) {
    int total = 0;
    for (int m = range.first; m < range.last; ++m) total += core::armorWeight(m);
    if (total <= 0) return;
    int roll     = nextInt(total);
    int material = range.first;
    for (int m = range.first; m < range.last; ++m) {
        if (roll < core::armorWeight(m)) {
            material = m;
            break;
        }
        roll -= core::armorWeight(m);
    }
    auto stack = game::ItemConv::make(armorId(material, piece));
    if (stack.isNull()) return;
    if (nextInt(6 - minDifficulty) == 0) {
        int level = static_cast<int>(5.0f + special * 18.0f + static_cast<float>(minDifficulty * 6));
        EnchantUtils::randomlyEnchant(mob.getLevel().getRandom(), stack, level, 0, false);
    }
    mob.setArmor(slot, stack);
}

// ---- jukebox ---------------------------------------------------------------------------------------------

/// Mobs whose backpack plays (storage created at spawn): unique id -> storage uuid.
struct JukeboxMob {
    std::string uuid;
    int         dim;
    Vec3        pos;
};
std::unordered_map<std::int64_t, JukeboxMob>& jukeboxMobs() {
    static std::unordered_map<std::int64_t, JukeboxMob> m;
    return m;
}

void addJukeboxUpgradeAndRandomDisc(std::shared_ptr<game::Backpack> const& bp) {
    bool advanced = uniform() < 0.25;
    auto stack    = game::ItemConv::make(advanced ? "alleviatedbackpacks:advanced_jukebox_upgrade" : "alleviatedbackpacks:jukebox_upgrade");
    if (stack.isNull() || bp->data().upgrades.empty()) return;
    auto item = game::ItemConv::toData(stack).withCount(1);
    if (!bp->data().upgrades[0].empty() || !upgrade::install(bp, 0, item, nullptr)) return;
    std::vector<std::string> discs;
    auto const&              blocked = additions().discBlockList;
    for (auto const& id : upgrade::jukebox::vanillaDiscIds()) {
        if (std::find(blocked.begin(), blocked.end(), id) == blocked.end()) discs.push_back(id);
    }
    if (discs.empty()) {
        log().warn("wearable: no music discs to add to the jukebox upgrade (none registered or all blocked)");
        return;
    }
    upgrade::UpgradeContext ctx(bp, 0);
    int numberOfDiscs = advanced ? nextInt(std::max(1, upgrade::jukebox::advancedSlots() / 3)) + 1 : 1;
    std::vector<std::string> used;
    for (int i = 0; i < numberOfDiscs * 2 && static_cast<int>(used.size()) < numberOfDiscs; ++i) {
        auto const& disc = discs[static_cast<std::size_t>(nextInt(static_cast<int>(discs.size())))];
        if (std::find(used.begin(), used.end(), disc) != used.end()) continue;
        auto d = game::ItemConv::make(disc);
        if (d.isNull()) continue;
        upgrade::jukebox::setDisc(ctx, static_cast<int>(used.size()), game::ItemConv::toData(d).withCount(1));
        used.push_back(disc);
    }
}

/// Deletes a backpack's storage unless a player has used it (SB removes the uuid from the item; the
/// contents are then unreachable anyway).
void dropStorage(std::string const& uuid) {
    upgrade::jukebox::Service::get().stopAll(uuid);
    if (admin::AccessLog::get().contains(uuid)) return;
    game::Storage::get().erase(uuid);
}

void removeUuid(ItemStack& stack) {
    if (auto uuid = game::backpackUuid(stack)) dropStorage(*uuid);
    setUserData(stack, [](CompoundTag& tag) { tag.remove(game::kUuidKey); });
}

bool isRaidRaider(Mob& mob) {
    auto& dim     = mob.getDimension();
    auto* manager = dim.mVillageManager.get();
    if (!manager) return false;
    auto const& p = mob.getPosition();
    BlockPos    pos{static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)), static_cast<int>(std::floor(p.z))};
    auto        village = manager->getClosestVillageWithRaid(pos).lock();
    if (!village) return false;
    auto* raid = village->mRaid.get();
    return raid && raid->isRaider(mob.getOrCreateUniqueID());
}

} // namespace

// ---- public API -------------------------------------------------------------------------------------------

std::mt19937& rng() {
    static std::mt19937 engine{std::random_device{}()};
    return engine;
}

bool isMobWorn(ItemStack const& stack) { return hasByte(stack, kMobWornKey); }

std::optional<PendingLoot> pendingLoot(ItemStack const& stack) {
    if (stack.isNull() || !stack.mUserData) return std::nullopt;
    auto const& tag = *stack.mUserData;
    if (!tag.contains(kPendingLootKey, Tag::Type::String)) return std::nullopt;
    std::string const& text = tag[kPendingLootKey];
    auto               j    = nlohmann::json::parse(text, nullptr, false);
    if (!j.is_object()) return std::nullopt;
    PendingLoot loot;
    try {
        loot.table      = j.value("table", std::string{});
        loot.lootFactor = j.contains("factor") ? j.value("factor", 0.0f) : static_cast<float>(j.value("rolls", 0)); // phase-1: rolls
        loot.dimension  = j.value("dim", 0);
        loot.seed       = j.value("seed", 0u);
        loot.upgrades   = j.value("upgrades", false);
        if (j.contains("main")) loot.mainColor = j.value("main", 0u);
        if (j.contains("accent")) loot.accentColor = j.value("accent", 0u);
        if (auto it = j.find("extra"); it != j.end() && it->is_array()) {
            for (auto const& e : *it) {
                auto item = core::ItemStackData::fromJson(e);
                if (!item.empty()) loot.extraItems.push_back(std::move(item));
            }
        }
    } catch (nlohmann::json::exception const&) {
        return std::nullopt;
    }
    return loot;
}

void setPendingLoot(ItemStack& stack, PendingLoot const& loot) {
    nlohmann::json j{
        {"table",    loot.table     },
        {"factor",   loot.lootFactor},
        {"dim",      loot.dimension },
        {"seed",     loot.seed      },
        {"upgrades", loot.upgrades  },
    };
    if (loot.mainColor) j["main"] = *loot.mainColor;
    if (loot.accentColor) j["accent"] = *loot.accentColor;
    if (!loot.extraItems.empty()) {
        auto extra = nlohmann::json::array();
        for (auto const& e : loot.extraItems) extra.push_back(e.toJson());
        j["extra"] = std::move(extra);
    }
    setUserData(stack, [&](CompoundTag& tag) { tag.putString(std::string(kPendingLootKey), j.dump()); });
}

ItemStack makeMobBackpack(core::Tier tier, PendingLoot const& loot) {
    auto stack = game::makeBackpackItem(tier);
    if (stack.isNull()) return stack;
    setPendingLoot(stack, loot);
    setUserData(stack, [&](CompoundTag& tag) { tag.putByte(std::string(kMobWornKey), 1); });
    return stack;
}

void fillBackpack(std::shared_ptr<game::Backpack> const& bp, PendingLoot const& loot) {
    if (!bp) return;
    if (loot.mainColor) bp->data().mainColor = *loot.mainColor;
    if (loot.accentColor) bp->data().accentColor = *loot.accentColor;
    putIntoRandomEmptySlots(*bp, rollLoot(loot), loot.seed);
    for (auto const& extra : loot.extraItems) {
        // SB fillWithExtraItems: into the inventory (upgrades too, they are items in SB's container).
        auto r = bp->inventory().insert(extra, core::AccessContext{core::Source::Internal});
        if (r.inserted < extra.count) log().warn("wearable: extra item {} did not fit into backpack {}", extra.id, bp->uuid());
    }
    if (loot.upgrades) installRandomUpgrades(bp);
    bp->markDirty();
}

bool consumePendingLoot(ItemStack& stack) {
    auto loot = pendingLoot(stack);
    auto uuid = game::backpackUuid(stack);
    if (!loot || !uuid) return false;
    auto bp = game::Storage::get().find(*uuid);
    if (!bp) return false;
    fillBackpack(bp, *loot);
    setUserData(stack, [](CompoundTag& tag) { tag.remove(kPendingLootKey); });
    return true;
}

std::optional<std::string> lootTableFor(std::string_view typeName) {
    for (auto const& mapping : additions().entityLootTableList) {
        auto bar = mapping.find('|');
        if (bar == std::string::npos) continue;
        if (std::string_view(mapping).substr(0, bar) != typeName) continue;
        auto table = mapping.substr(bar + 1);
        return table == "null" ? std::string{} : table;
    }
    return std::nullopt;
}

bool isEligibleMob(std::string_view typeName) { return lootTableFor(typeName).has_value(); }

std::optional<std::pair<std::uint32_t, std::uint32_t>> spawnEggColors(std::string_view typeName) {
    for (auto const& e : kEggColors) {
        if (e.type == typeName) return std::make_pair(e.background, e.highlight);
    }
    return std::nullopt;
}

float effectiveDifficultyAt(Mob& mob) {
    auto& level      = mob.getLevel();
    int   difficulty = static_cast<int>(level.getDifficulty());
    auto  ticks      = static_cast<std::int64_t>(level.getCurrentTick().tickID);
    int   phase      = static_cast<int>((static_cast<std::int64_t>(level.getTime()) / 24000) % 8);
    // Bedrock keeps no per-chunk inhabited time: the regional part only grows with world age.
    return core::effectiveDifficulty(difficulty, ticks, 0, core::moonBrightness(phase));
}

core::Tier rollTier(float effectiveDifficulty, std::mt19937& engine) {
    auto const& c   = additions();
    int         idx = c.localDifficultyEffectsBackpackSpawns ? core::difficultyIndex(effectiveDifficulty) : 0;
    auto        pool =
        core::tierPool({c.leatherWeight, c.copperWeight, c.ironWeight, c.goldWeight, c.diamondWeight, c.netheriteWeight}, idx, c.minBackpackTierMidDifficulty, c.minBackpackTierHighDifficulty);
    int total = core::totalWeight(pool);
    if (total <= 0) return core::Tier::Leather;
    return core::pickWeighted(pool, std::uniform_int_distribution<int>(0, total - 1)(engine));
}

bool equipMob(Mob& mob, std::optional<core::Tier> forcedTier, std::optional<bool> forcedJukebox) {
    auto const& c        = additions();
    float       eff      = effectiveDifficultyAt(mob);
    auto        tier     = forcedTier.value_or(rollTier(eff, rng()));
    int         minD     = core::minDifficultyOf(tier);
    auto        backpack = game::makeBackpackItem(tier);
    if (backpack.isNull()) return false;
    setUserData(backpack, [](CompoundTag& tag) { tag.putByte(std::string(kMobWornKey), 1); });
    bool playMusic = forcedJukebox.value_or(c.playJukebox && nextInt(4) == 0);

    // SB: everything below only for mobs with a spawn egg (colours come from it).
    if (auto egg = spawnEggColors(mob.getTypeName())) {
        PendingLoot loot;
        loot.mainColor    = egg->first | 0xFF000000u;
        loot.accentColor  = egg->second | 0xFF000000u;
        loot.dimension    = static_cast<int>(mob.getDimensionId());
        loot.seed         = static_cast<unsigned>(rng()());
        loot.upgrades     = config::get<WearableConfig>().mobs.addRandomUpgrades;
        int partialRolls  = std::uniform_int_distribution<int>(minD, minD + 2)(rng());
        auto table        = lootTableFor(mob.getTypeName());
        if (partialRolls != 0 && c.addLoot && table && !table->empty()) {
            loot.table      = *table;
            loot.lootFactor = static_cast<float>(partialRolls) / static_cast<float>(kMaxDifficulty);
        }
        setPendingLoot(backpack, loot);
        if (playMusic) {
            // The jukebox must play from the mob: the storage is created now with the colours
            // (create listener); the loot still waits for the first open.
            PendingLoot colours;
            colours.mainColor   = loot.mainColor;
            colours.accentColor = loot.accentColor;
            colours.seed        = loot.seed;
            setPendingLoot(backpack, colours);
            setUserData(backpack, [](CompoundTag& tag) { tag.putByte(std::string(kMobJukeboxKey), 1); });
            bool changed = false;
            auto bp      = game::Storage::get().forItem(backpack, &changed);
            if (bp && !bp->data().upgrades.empty()) {
                addJukeboxUpgradeAndRandomDisc(bp);
                if (loot.upgrades) installRandomUpgrades(bp); // AlleviatedBackpacks extra, after the jukebox
                jukeboxMobs()[mob.getOrCreateUniqueID().rawID] = {bp->uuid(), static_cast<int>(mob.getDimensionId()), mob.getPosition()};
            } else {
                setUserData(backpack, [](CompoundTag& tag) { tag.remove(kMobJukeboxKey); });
            }
            PendingLoot rest;
            rest.table      = loot.table;
            rest.lootFactor = loot.lootFactor;
            rest.dimension  = loot.dimension;
            rest.seed       = loot.seed;
            if (!rest.table.empty()) setPendingLoot(backpack, rest);
        }
    }
    mob.setArmor(ArmorSlot::Torso, backpack);
    // Health before the potions: Bedrock's health boost effect then adds on top of the raised max
    // (SB's attribute modifier and health boost add up the same way).
    raiseHealth(mob, minD);
    applyPotions(mob, std::max(minD, nextInt(kMaxDifficulty + 1)), minD);
    if (c.equipWithArmor) {
        auto  ranges  = core::armorRanges(tier);
        float special = core::specialMultiplier(eff);
        equipArmorPiece(mob, minD, ranges.helmet, ArmorSlot::Head, "helmet", special);
        equipArmorPiece(mob, minD, ranges.leggings, ArmorSlot::Legs, "leggings", special);
        equipArmorPiece(mob, minD, ranges.boots, ArmorSlot::Feet, "boots", special);
    }
    mob.sendArmor(std::bitset<5>{0b01111});
    return true;
}

bool handleMobDeath(Mob& mob, Player* killer) {
    if (mob.isPlayer()) return false;
    auto const& chest = mob.getArmor(ArmorSlot::Torso);
    if (!game::isBackpack(chest) || !isMobWorn(chest)) return false;
    ItemStack item = chest;
    mob.setArmor(ArmorSlot::Torso, ItemStack::EMPTY_ITEM());
    jukeboxMobs().erase(mob.getOrCreateUniqueID().rawID);
    auto const& c    = additions();
    bool        drop = killer != nullptr && (c.dropToFakePlayers || !killer->isSimulatedPlayer());
    if (drop) {
        int  looting = EnchantUtils::getEnchantLevel(Enchant::Type::Looting, killer->getSelectedItem());
        auto tier    = game::backpackTierOf(item).value_or(core::Tier::Leather);
        drop         = uniform() < core::dropChance(
                               std::clamp(c.backpackDropChance, 0.0, 1.0),
                               std::clamp(c.lootingChanceIncreasePerLevel, 0.0, 0.3),
                               looting,
                               tier
                           );
    }
    if (!drop) {
        removeUuid(item);
        return false;
    }
    if (hasByte(item, kMobJukeboxKey)) {
        // SB putJukeboxItemsInContainerAndRemoveStorageUuid: the upgrades (with their discs) leave
        // the storage as extra items of the dropped backpack, which gets fresh storage on open.
        auto loot = pendingLoot(item).value_or(PendingLoot{});
        if (auto uuid = game::backpackUuid(item); uuid) {
            if (auto bp = game::Storage::get().find(*uuid)) {
                upgrade::jukebox::Service::get().stopAll(*uuid);
                for (int slot = 0; slot < static_cast<int>(bp->data().upgrades.size()); ++slot) {
                    if (bp->data().upgrades[static_cast<std::size_t>(slot)].empty()) continue;
                    if (auto removed = upgrade::remove(bp, slot, nullptr)) loot.extraItems.push_back(*removed);
                }
                loot.mainColor   = bp->data().mainColor;
                loot.accentColor = bp->data().accentColor;
            }
        }
        removeUuid(item);
        setPendingLoot(item, loot);
    }
    setUserData(item, [](CompoundTag& tag) {
        tag.remove(kMobWornKey);
        tag.remove(kMobJukeboxKey);
    });
    mob.drop(item, false);
    return true;
}

namespace {

/// Event wiring, lazy loot generation, jukebox mobs.
class MobBackpacks {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::SpawnedMobEvent>([this](ll::event::SpawnedMobEvent& ev) {
            auto mob = ev.mob();
            if (!mob) return;
            Mob& m = *mob;
            if (m.isPlayer() || m.getLevel().isClientSide() || !isEligibleMob(m.getTypeName())) return;
            double chance = std::clamp(additions().chance, 0.0, 1.0);
            if (chance <= 0.0) return;
            int bound = chance >= 1.0 ? 1 : static_cast<int>(1.0 / chance);
            if (nextInt(bound) != 0) return;
            // Next tick (SB defers too): raid raiders are registered with their raid after the
            // spawn, and equipment set by the spawn rules is in place by then.
            mPending.push_back(m.getOrCreateUniqueID().rawID);
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::MobDieEvent>([](ll::event::MobDieEvent& ev) {
            auto&   mob    = ev.self();
            Player* killer = nullptr;
            auto    id     = ev.source().getDamagingEntityUniqueID();
            if (id.rawID == -1) id = ev.source().getEntityUniqueID();
            if (auto* a = mob.getLevel().fetchEntity(id, false); a && a->isPlayer()) killer = static_cast<Player*>(a);
            try {
                handleMobDeath(mob, killer);
            } catch (std::exception const& e) {
                log().error("wearable: mob death handling failed: {}", e.what());
            }
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([this](ll::event::ServerLevelTickEvent& ev) {
            tick(ev.level());
        }));
        mCreateListener = game::Storage::get().addCreateListener([](ItemStack& stack, std::shared_ptr<game::Backpack> const& bp) {
            auto loot = pendingLoot(stack);
            if (!loot) return;
            try {
                fillBackpack(bp, *loot);
            } catch (std::exception const& e) {
                // The backpack keeps what was generated so far; the uuid is written anyway.
                log().error("wearable: generating loot for backpack {} failed: {}", bp->uuid(), e.what());
            }
            setUserData(stack, [&](CompoundTag& tag) {
                tag.remove(kPendingLootKey);
                if (!stack.mUserData || !stack.mUserData->contains(kMobJukeboxKey)) tag.remove(kMobWornKey);
            });
        });
        ui::MenuManager::Observer o;
        o.beforeShow = [](Player& player, ui::Menu& menu, int) {
            auto* bm = dynamic_cast<ui::BackpackMenu*>(&menu);
            if (!bm || !bm->source()) return;
            ItemStack stack = game::itemAt(player, *bm->source());
            if (consumePendingLoot(stack)) game::setItemAt(player, *bm->source(), stack);
        };
        mObserver = ui::MenuManager::get().addObserver(std::move(o));
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        game::Storage::get().removeCreateListener(mCreateListener);
        ui::MenuManager::get().removeObserver(mObserver);
        mPending.clear();
        jukeboxMobs().clear();
    }

private:
    void tick(Level& level) {
        ++mTicks;
        if (!mPending.empty()) {
            auto pending = std::move(mPending);
            mPending.clear();
            for (auto id : pending) {
                ActorUniqueID uid;
                uid.rawID = id;
                auto* actor = level.fetchEntity(uid, false);
                if (!actor || !actor->isAlive() || actor->isPlayer()) continue;
                auto& mob = static_cast<Mob&>(*actor);
                if (!mob.getArmor(ArmorSlot::Torso).isNull() || isRaidRaider(mob)) continue;
                try {
                    equipMob(mob);
                } catch (std::exception const& e) {
                    log().error("wearable: equipping {} failed: {}", mob.getTypeName(), e.what());
                }
            }
        }
        if (mTicks % 10 == 0) tickJukeboxMobs(level);
    }

    /// Jukebox mobs play at their position (SB onLivingUpdate); a mob that left the level loses
    /// its backpack's storage, unless it converted (the new mob wears the same backpack).
    static void tickJukeboxMobs(Level& level) {
        auto&                                          mobs = jukeboxMobs();
        std::vector<std::pair<std::int64_t, JukeboxMob>> converted;
        for (auto it = mobs.begin(); it != mobs.end();) {
            ActorUniqueID uid;
            uid.rawID   = it->first;
            auto* actor = level.fetchEntity(uid, false);
            auto  uuid  = it->second.uuid;
            if (actor && actor->isAlive() && game::backpackUuid(actor->getArmor(ArmorSlot::Torso)) == uuid) {
                it->second.pos = actor->getPosition();
                it->second.dim = static_cast<int>(actor->getDimensionId());
                if (auto bp = game::Storage::get().find(uuid)) {
                    auto& jukebox = upgrade::jukebox::Service::get();
                    jukebox.tickPlaced(bp, it->second.dim, it->second.pos);
                    if (!jukebox.playing(uuid, 0)) (void)jukebox.play(upgrade::UpgradeContext(bp, 0), nullptr);
                }
                ++it;
                continue;
            }
            // Converting (zombie -> drowned, piglin -> zombified piglin, ...): the new mob wears it.
            Mob* successor = nullptr;
            if (auto dim = level.getDimension(it->second.dim).lock()) {
                auto& region = dim->getBlockSourceFromMainChunkSource();
                auto  p      = it->second.pos;
                for (auto* a : region.getEntities(AABB{Vec3{p.x - 4, p.y - 4, p.z - 4}, Vec3{p.x + 4, p.y + 4, p.z + 4}}, 0.0f)) {
                    if (!a || a->isPlayer() || !a->isAlive() || !a->hasCategory(ActorCategory::Mob)) continue;
                    if (game::backpackUuid(a->getArmor(ArmorSlot::Torso)) == uuid) successor = static_cast<Mob*>(a);
                }
            }
            if (successor) {
                converted.emplace_back(successor->getOrCreateUniqueID().rawID, it->second);
                it = mobs.erase(it);
                continue;
            }
            dropStorage(uuid);
            it = mobs.erase(it);
        }
        for (auto& [id, data] : converted) mobs[id] = std::move(data); // after the loop: no rehash under it
    }

    std::vector<ll::event::ListenerPtr> mListeners;
    std::vector<std::int64_t>           mPending;
    int                                 mCreateListener = 0;
    int                                 mObserver       = 0;
    std::int64_t                        mTicks          = 0;
};

} // namespace
} // namespace abp::wearable

ABP_REGISTER_MODULE(abp::wearable::MobBackpacks);
