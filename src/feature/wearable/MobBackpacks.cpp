// Backpacks on spawned mobs (Sophisticated Backpacks' "entity backpack additions"):
// - eligible mobs sometimes spawn wearing a backpack in the chest slot (tier by dimension and
//   difficulty), optionally with matching armor and potion effects;
// - the backpack's contents (loot table rolls, random upgrades) are generated lazily, the first
//   time the backpack is opened (Storage create listener), so mobs that despawn cost nothing;
// - on death the backpack drops only when a player killed the mob (config chance otherwise).

#include <algorithm>
#include <bitset>
#include <cmath>
#include <exception>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/entity/MobDieEvent.h"
#include "ll/api/event/world/SpawnMobEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/util/LootTableUtils.h"
#include "mc/util/Random.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/effect/EffectDuration.h"
#include "mc/world/effect/MobEffect.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/inventory/CraftingContainer.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/dimension/DimensionType.h"

#include "nlohmann/json.hpp"

#include "feature/wearable/Wearable.h"
#include "feature/wearable/WearableConfig.h"
#include "game/Backpack.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/Storage.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "upgrade/UpgradeManager.h"

ABP_REGISTER_CONFIG("wearable", abp::wearable::WearableConfig);

namespace abp::wearable {

using SharedTypes::Legacy::ArmorSlot;

namespace {

WearableConfig::MobSpawn const& cfg() { return config::get<WearableConfig>().mobs; }

std::string_view difficultyName(int difficulty) {
    switch (difficulty) {
    case 0:
        return "peaceful";
    case 1:
        return "easy";
    case 3:
        return "hard";
    default:
        return "normal";
    }
}

std::string_view dimensionName(int dimension) {
    switch (dimension) {
    case 1:
        return "nether";
    case 2:
        return "the_end";
    default:
        return "overworld";
    }
}

template <class Map, class V>
V valueOr(Map const& map, std::string_view key, V fallback) {
    auto it = map.find(std::string(key));
    return it == map.end() ? fallback : static_cast<V>(it->second);
}

double uniform() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng()); }

std::string armorId(core::Tier tier, std::string_view piece) {
    std::string_view material;
    switch (tier) {
    case core::Tier::Leather:
        material = "leather";
        break;
    case core::Tier::Copper:
        material = "copper";
        break;
    case core::Tier::Iron:
        material = "iron";
        break;
    case core::Tier::Gold:
        material = "golden";
        break;
    case core::Tier::Diamond:
        material = "diamond";
        break;
    case core::Tier::Netherite:
        material = "netherite";
        break;
    }
    return "minecraft:" + std::string(material) + "_" + std::string(piece);
}

void equipArmor(Mob& mob, core::Tier tier) {
    std::pair<ArmorSlot, std::string_view> const pieces[] = {
        {ArmorSlot::Head, "helmet"},
        {ArmorSlot::Legs, "leggings"},
        {ArmorSlot::Feet, "boots"},
    };
    for (auto [slot, piece] : pieces) {
        if (!mob.getArmor(slot).isNull()) continue;
        auto item = game::ItemConv::make(armorId(tier, piece));
        if (item.isNull()) item = game::ItemConv::make("minecraft:chainmail_" + std::string(piece)); // older versions
        if (!item.isNull()) mob.setArmor(slot, item);
    }
}

void addEffects(Mob& mob, core::Tier tier) {
    auto const& effects = cfg().tierEffects;
    auto        it      = effects.find(std::string(core::tierInfo(tier).id));
    if (it == effects.end()) return;
    for (auto const& spec : it->second) {
        auto        colon = spec.find(':');
        std::string name  = spec.substr(0, colon);
        int         amp   = 0;
        if (colon != std::string::npos) {
            try {
                amp = std::stoi(spec.substr(colon + 1));
            } catch (...) {
                amp = 0;
            }
        }
        auto const* effect = MobEffect::getByName(name);
        if (!effect) {
            log().warn("wearable: unknown effect '{}' in config", name);
            continue;
        }
        MobEffectInstance instance(effect->mId,EffectDuration{1'000'000});
        instance.mAmplifier = std::clamp(amp, 0, 4);
        instance.mAmbient   = true;
        mob.addEffect(instance);
    }
}

void setUserData(ItemStack& stack, std::function<void(CompoundTag&)> const& edit) {
    auto tag = stack.mUserData ? stack.mUserData->clone() : std::make_unique<CompoundTag>();
    edit(*tag);
    stack.setUserData(std::move(tag));
}

void installRandomUpgrades(std::shared_ptr<game::Backpack> const& bp) {
    auto const&                    c = cfg();
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

} // namespace

std::mt19937& rng() {
    static std::mt19937 engine{std::random_device{}()};
    return engine;
}

bool isMobWorn(ItemStack const& stack) {
    return !stack.isNull() && stack.mUserData && stack.mUserData->contains(kMobWornKey, Tag::Type::Byte);
}

std::optional<PendingLoot> pendingLoot(ItemStack const& stack) {
    if (stack.isNull() || !stack.mUserData) return std::nullopt;
    auto const& tag = *stack.mUserData;
    if (!tag.contains(kPendingLootKey, Tag::Type::String)) return std::nullopt;
    std::string const& text = tag[kPendingLootKey];
    auto               j    = nlohmann::json::parse(text, nullptr, false);
    if (!j.is_object()) return std::nullopt;
    PendingLoot loot;
    try {
        loot.table     = j.value("table", std::string{});
        loot.rolls     = j.value("rolls", 1);
        loot.dimension = j.value("dim", 0);
        loot.seed      = j.value("seed", 0u);
        loot.upgrades  = j.value("upgrades", false);
    } catch (nlohmann::json::exception const&) {
        return std::nullopt;
    }
    return loot;
}

ItemStack makeMobBackpack(core::Tier tier, PendingLoot const& loot) {
    auto stack = game::makeBackpackItem(tier);
    if (stack.isNull()) return stack;
    nlohmann::json j{
        {"table",    loot.table    },
        {"rolls",    loot.rolls    },
        {"dim",      loot.dimension},
        {"seed",     loot.seed     },
        {"upgrades", loot.upgrades },
    };
    setUserData(stack, [&](CompoundTag& tag) {
        tag.putByte(std::string(kMobWornKey), 1);
        tag.putString(std::string(kPendingLootKey), j.dump());
    });
    return stack;
}

void fillBackpack(std::shared_ptr<game::Backpack> const& bp, PendingLoot const& loot) {
    if (!bp) return;
    auto level = ll::service::getLevel();
    if (level && !loot.table.empty() && loot.rolls > 0) {
        SuppressInjection noInject;
        auto              box   = std::make_unique<CraftingContainer>(9, 3);
        Random            random(loot.seed, false);
        std::string       table = "loot_tables/" + loot.table + ".json";
        // One roll fills at most 27 slots: never roll more than the backpack can hold.
        int rolls = std::min(loot.rolls, std::max(1, bp->inventory().size() / box->getContainerSize()));
        for (int r = 0; r < rolls; ++r) {
            box->removeAllItems();
            Util::LootTableUtils::fillContainer(*level, *box, random, table, DimensionType(loot.dimension), nullptr);
            for (int i = 0; i < box->getContainerSize(); ++i) {
                auto const& it = box->getItem(i);
                if (it.isNull()) continue;
                bp->inventory().insert(game::ItemConv::toData(it), core::AccessContext{core::Source::Internal});
            }
        }
        box->removeAllItems();
    }
    if (loot.upgrades) installRandomUpgrades(bp);
    bp->markDirty();
}

bool isEligibleMob(std::string_view typeName) {
    auto const& list = cfg().entities;
    return std::find(list.begin(), list.end(), typeName) != list.end();
}

double spawnChance(int difficulty) {
    auto const& c = cfg();
    return std::clamp(static_cast<double>(c.chance) * valueOr(c.difficultyMultiplier, difficultyName(difficulty), 1.0), 0.0, 1.0);
}

std::optional<core::Tier> rollTier(int dimension, int difficulty, std::mt19937& engine) {
    auto const& c       = cfg();
    auto        weights = c.tierWeights.find(std::string(dimensionName(dimension)));
    if (weights == c.tierWeights.end()) weights = c.tierWeights.find("overworld");
    if (weights == c.tierWeights.end()) return std::nullopt;
    double              bonus = valueOr(c.difficultyTierBonus, difficultyName(difficulty), 0.0);
    std::vector<double> w;
    for (auto const& info : core::kTiers) {
        double base = std::max(0, valueOr(weights->second, info.id, 0));
        w.push_back(base * std::pow(std::max(0.0, 1.0 + bonus), static_cast<double>(info.tier)));
    }
    double total = 0;
    for (double x : w) total += x;
    if (total <= 0) return std::nullopt;
    double pick = std::uniform_real_distribution<double>(0.0, total)(engine);
    for (std::size_t i = 0; i < w.size(); ++i) {
        if (pick < w[i]) return core::kTiers[i].tier;
        pick -= w[i];
    }
    for (std::size_t i = w.size(); i-- > 0;) {
        if (w[i] > 0) return core::kTiers[i].tier;
    }
    return std::nullopt;
}

bool equipMob(Mob& mob, std::optional<core::Tier> tier) {
    auto const& c          = cfg();
    int         dimension  = static_cast<int>(mob.getDimensionId());
    int         difficulty = static_cast<int>(mob.getLevel().getDifficulty());
    if (!tier) tier = rollTier(dimension, difficulty, rng());
    if (!tier) return false;
    PendingLoot loot;
    if (c.fillWithLoot) {
        auto it    = c.lootTables.find(mob.getTypeName());
        loot.table = it != c.lootTables.end() ? it->second : c.defaultLootTable;
        loot.rolls = valueOr(c.lootRolls, core::tierInfo(*tier).id, 1);
    }
    loot.dimension = dimension;
    loot.seed      = static_cast<unsigned>(rng()());
    loot.upgrades  = c.addRandomUpgrades;
    auto stack     = makeMobBackpack(*tier, loot);
    if (stack.isNull()) return false;
    mob.setArmor(ArmorSlot::Torso, stack);
    if (c.equipArmor) equipArmor(mob, *tier);
    if (c.buffWithEffects) addEffects(mob, *tier);
    mob.sendArmor(std::bitset<5>{0b01111});
    return true;
}

bool handleMobDeath(Mob& mob, bool killedByPlayer) {
    if (mob.isPlayer()) return false;
    auto const& chest = mob.getArmor(ArmorSlot::Torso);
    if (!game::isBackpack(chest) || !isMobWorn(chest)) return false;
    ItemStack item = chest;
    setUserData(item, [](CompoundTag& tag) { tag.remove(kMobWornKey); });
    mob.setArmor(ArmorSlot::Torso, ItemStack::EMPTY_ITEM());
    if (!killedByPlayer && uniform() >= cfg().dropChanceNotKilledByPlayer) return false;
    mob.drop(item, false);
    return true;
}

namespace {

bool isPlayerKill(ActorDamageSource const& source) {
    return source.getDamagingEntityType() == ActorType::Player || source.getEntityType() == ActorType::Player;
}

/// Event wiring + lazy loot generation.
class MobBackpacks {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::SpawnedMobEvent>([](ll::event::SpawnedMobEvent& ev) {
            auto mob = ev.mob();
            if (!mob) return;
            auto const& c = cfg();
            if (!c.enabled || (ev.fromSpawner() && !c.includeSpawnerMobs)) return;
            Mob& m = *mob;
            if (m.isPlayer() || !isEligibleMob(m.getTypeName())) return;
            if (uniform() >= spawnChance(static_cast<int>(m.getLevel().getDifficulty()))) return;
            try {
                equipMob(m);
            } catch (std::exception const& e) {
                log().error("wearable: equipping {} failed: {}", m.getTypeName(), e.what());
            }
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::MobDieEvent>([](ll::event::MobDieEvent& ev) {
            handleMobDeath(ev.self(), isPlayerKill(ev.source()));
        }));
        mCreateListener = game::Storage::get().addCreateListener(
            [](ItemStack& stack, std::shared_ptr<game::Backpack> const& bp) {
                auto loot = pendingLoot(stack);
                if (!loot && !isMobWorn(stack)) return;
                if (loot) {
                    try {
                        fillBackpack(bp, *loot);
                    } catch (std::exception const& e) {
                        // The backpack keeps what was generated so far; the uuid is written anyway.
                        log().error("wearable: generating loot for backpack {} failed: {}", bp->uuid(), e.what());
                    }
                }
                setUserData(stack, [](CompoundTag& tag) {
                    tag.remove(kPendingLootKey);
                    tag.remove(kMobWornKey);
                });
            }
        );
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        game::Storage::get().removeCreateListener(mCreateListener);
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
    int                                 mCreateListener = 0;
};

} // namespace
} // namespace abp::wearable

ABP_REGISTER_MODULE(abp::wearable::MobBackpacks);
