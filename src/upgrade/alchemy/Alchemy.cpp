// Alchemy & Advanced Alchemy upgrades: server side (item definitions, checks, applications, the
// ticker, the native tab's data and actions). See Alchemy.h.

#include "upgrade/alchemy/Alchemy.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <map>
#include <random>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/server/ServerStoppingEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Color.h"
#include "mc/deps/shared_types/legacy/LevelEvent.h"
#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/deps/shared_types/v1_26_20/block/MaterialType.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/util/VariantParameterList.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/ActorFlags.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/effect/EffectDuration.h"
#include "mc/world/effect/MobEffect.h"
#include "mc/world/effect/MobEffectIds.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/alchemy/Potion.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/phys/AABB.h"

#include "feature/placed/PlacedBackpacks.h"
#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "game/Storage.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "net/NativeScreens.h"
#include "upgrade/UpgradeManager.h"

ABP_REGISTER_CONFIG("alchemy", abp::upgrade::alchemy::AlchemyConfig);

namespace abp::upgrade::alchemy {

// ---- config / settings ----------------------------------------------------------------------------

int filterSlots(bool advanced) {
    auto const& c = config::get<AlchemyConfig>();
    return std::clamp(advanced ? c.advancedAlchemyFilterSlots : c.alchemyFilterSlots, 1, 20);
}

bool isAlchemy(IUpgrade const& type) { return type.info().group == kGroup; }

bool isAdvanced(UpgradeContext const& ctx) {
    auto const& item = ctx.state().item;
    return item.id == kAdvancedItem;
}

Settings settingsOf(UpgradeContext const& ctx) { return Settings::fromJson(ctx.settings(), filterSlots(isAdvanced(ctx))); }

void writeSettings(UpgradeContext const& ctx, Settings const& settings) {
    if (!ctx.settings().is_object()) ctx.settings() = nlohmann::json::object();
    ctx.settings().update(settings.toJson());
    ctx.saveSettings(false);
}

// ---- item data ------------------------------------------------------------------------------------

namespace {

std::vector<Effect> potionEffects(int aux) {
    std::vector<Effect> out;
    if (aux < 0 || aux >= 64) return out;
    auto const& potion = Potion::mPotionsById()[static_cast<std::size_t>(aux)];
    if (!potion) return out;
    for (auto const& e : *potion->mEffects) {
        if (e.mId == 0) continue; // MobEffectInstance::NO_EFFECT (water bottles, ...)
        out.push_back(Effect{static_cast<int>(e.mId), e.mDuration->mValue, e.mAmplifier});
    }
    return out;
}

MobEffect const* mobEffect(int id) {
    if (id <= 0 || id >= static_cast<int>(MobEffectIds::Count)) return nullptr;
    return MobEffect::mMobEffects()[static_cast<MobEffectIds>(id)].get();
}

} // namespace

std::vector<Effect> itemEffects(core::ItemStackData const& item) {
    if (item.empty()) return {};
    if (item.id == kPotion || item.id == kSplashPotion) return potionEffects(item.aux);
    if (auto const* food = foodEffects(item.id)) return *food;
    return {};
}

bool canBeFilter(Settings const& settings, core::ItemStackData const& item) {
    return !item.empty() && kindOf(item.id) != Kind::None && !settings.containsFilter(item);
}

bool setFilter(UpgradeContext const& ctx, int slot, core::ItemStackData const& item) {
    auto s = settingsOf(ctx);
    if (slot < 0 || slot >= static_cast<int>(s.filters.size())) return false;
    if (!item.empty() && !canBeFilter(s, item)) return false;
    auto kind = item.empty() ? Kind::None : kindOf(item.id);
    s.setFilter(slot, item, defaultCondition(kind, itemEffects(item)));
    writeSettings(ctx, s);
    return true;
}

// ---- entities -------------------------------------------------------------------------------------

namespace {

bool isMining(Actor& entity) {
    if (!entity.isPlayer()) return false;
    auto& player = static_cast<Player&>(entity);
    if (player.mDestroyingBlock) return true;
    return player.mGameMode && player.mGameMode->mDestroyProgress > 0.f;
}

EffectLookup lookupOf(Actor& entity) {
    return [&entity](int id) -> std::optional<Effect> {
        auto const* inst = entity.getEffect(static_cast<uint>(id));
        if (!inst || inst->mId == 0) return std::nullopt;
        return Effect{id, inst->mDuration->mValue, inst->mAmplifier};
    };
}

bool hasEffect(Actor& entity, int id) { return lookupOf(entity)(id).has_value(); }

/// (The display entity drawing a placed backpack, P2-11, is no living entity.)
bool isLiving(Actor& a) {
    return a.hasCategory(ActorCategory::Mob) && a.isAlive() && a.getTypeName() != "alleviatedbackpacks:backpack_display";
}

/// Living entities (players included: chunks don't list players) whose box intersects `box`.
std::vector<Actor*> livingIn(BlockSource& region, AABB const& box) {
    std::vector<Actor*> out;
    for (auto* a : region.getEntities(box, 0.f)) {
        if (a && !a->isPlayer() && isLiving(*a)) out.push_back(a);
    }
    auto dim = region.getDimensionId();
    region.getLevel().forEachPlayer([&](Player& p) {
        if (!p.isClientSide() && p.getDimensionId() == dim && isLiving(p) && box.intersects(p.getAABB())) out.push_back(&p);
        return true;
    });
    return out;
}

} // namespace

EntityState stateOf(Actor& entity) {
    EntityState s;
    s.underWater   = entity.isUnderLiquid(SharedTypes::v1_26_20::MaterialType::Water);
    s.onFire       = entity.isOnFire();
    s.fallDistance = entity.getFallDistance();
    s.mining       = isMining(entity);
    s.sprinting    = entity.isSprinting();
    s.health       = static_cast<float>(entity.getHealth());
    s.maxHealth    = static_cast<float>(entity.getMaxHealth());
    for (auto const& e : entity.getAllEffects()) {
        if (auto const* type = mobEffect(static_cast<int>(e.mId)); type && type->mIsHarmful) {
            s.harmfulEffect = true;
            break;
        }
    }
    return s;
}

// ---- applying -------------------------------------------------------------------------------------

namespace {

long long gNow = 0;

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}
float nextFloat() { return std::uniform_real_distribution<float>(0.f, 1.f)(rng()); }

/// A running application (SC `applying` + `remainingApplyTime` + `stackBeingAplied`).
struct Application {
    std::shared_ptr<game::Backpack> backpack;
    int                             slot = 0;
    ActorUniqueID                   target;
    core::ItemStackData             stack; ///< count 1
    Kind                            kind        = Kind::None;
    int                             useDuration = 0;
    int                             remaining   = 0;
};

using AppKey = std::pair<std::string, int>;
std::map<AppKey, Application> gApplications;

AppKey keyOf(UpgradeContext const& ctx) { return {ctx.backpack().uuid(), ctx.slot()}; }

struct CheckState {
    long long next = 0;
};

Actor* findActor(ActorUniqueID id) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    auto* a = level->fetchEntity(id, false);
    return a && a->isAlive() ? a : nullptr;
}

bool usesDrinkSound(Kind k) { return k == Kind::Potion || k == Kind::MilkBucket || k == Kind::OminousBottle; }

/// SC `triggerItemUseEffects`.
void useEffects(Actor& target, Kind kind) {
    auto& level = target.getLevel();
    if (usesDrinkSound(kind)) {
        level.playSound(SharedTypes::Legacy::LevelSoundEvent::Drink, target.getPosition(), 0.5f, nextFloat() * 0.1f + 0.9f);
    } else if (kind == Kind::GoldenApple || kind == Kind::EffectFood) {
        level.playSound(
            SharedTypes::Legacy::LevelSoundEvent::Eat,
            target.getPosition(),
            0.5f + 0.5f * static_cast<float>(rng()() % 2),
            (nextFloat() - nextFloat()) * 0.2f + 1.0f
        );
    }
}

void addEffect(Actor& target, Effect const& e, float scale) {
    auto const* type = mobEffect(e.id);
    if (!type) return;
    if (type->isInstantaneous()) {
        type->applyInstantaneousEffect(nullptr, nullptr, &target, e.amplifier, scale);
        return;
    }
    int duration = scale >= 1.f ? e.duration : static_cast<int>(scale * static_cast<float>(e.duration) + 0.5f);
    if (duration <= 20 && scale < 1.f) return; // SC ThrownPotion: too short to matter
    MobEffectInstance inst(static_cast<uint>(e.id), EffectDuration{duration});
    inst.mAmplifier = e.amplifier;
    target.addEffect(inst);
}

/// SC splash definition `startUsing`: a splash potion thrown at the target's eyes.
void splash(Actor& target, core::ItemStackData const& stack) {
    auto  effects = itemEffects(stack);
    auto& level   = target.getLevel();
    auto  eye     = target.getEyePos();
    level.playSound(SharedTypes::Legacy::LevelSoundEvent::Throw, target.getPosition(), 0.5f, 0.4f / (nextFloat() * 0.4f + 0.8f));
    std::vector<MobEffectInstance> instances;
    for (auto const& e : effects) {
        MobEffectInstance inst(static_cast<uint>(e.id), EffectDuration{e.duration});
        inst.mAmplifier = e.amplifier;
        instances.push_back(inst);
    }
    int color = static_cast<int>(MobEffectInstance::getAverageColorValue(instances).toARGB());
    level.broadcastLevelEvent(SharedTypes::Legacy::LevelEvent::ParticlesPotionSplash, eye, color, nullptr);
    if (effects.empty()) return;
    // ThrownPotion.applySplash: living entities in the box (4, 2, 4) around the impact, effect
    // strength falling off with the distance (full strength for the entity hit).
    auto const& b   = target.getAABB();
    AABB        box{Vec3{b.min.x - 4, b.min.y - 2, b.min.z - 4}, Vec3{b.max.x + 4, b.max.y + 2, b.max.z + 4}};
    float       mul = MobEffectInstance::splashDurationMultiplier();
    auto targets = livingIn(target.getDimensionBlockSource(), box);
    if (std::find(targets.begin(), targets.end(), &target) == targets.end()) targets.push_back(&target); // the entity hit
    for (auto* a : targets) {
        auto  p  = a->getPosition();
        float dx = p.x - eye.x, dy = p.y - eye.y, dz = p.z - eye.z;
        float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 >= 16.f && a != &target) continue;
        float scale = a == &target ? 1.f : 1.f - std::sqrt(d2) / 4.f;
        for (auto const& e : effects) {
            auto const* type = mobEffect(e.id);
            if (!type) continue;
            Effect scaled = e;
            if (!type->isInstantaneous()) scaled.duration = static_cast<int>(static_cast<float>(e.duration) * mul);
            addEffect(*a, scaled, scale);
        }
    }
}

bool isZombieVillager(Actor& a) {
    auto t = a.getEntityTypeId();
    return t == ActorType::ZombieVillagerV2 || t == ActorType::ZombieVillager;
}

bool isConverting(Actor& a) { return a.getStatusFlag(ActorFlags::Shaking) || a.getStatusFlag(ActorFlags::IsTransforming); }

/// SC `canApply` of the item definition `kind` for the filter item.
bool canApply(Kind kind, Actor& target, core::ItemStackData const& filter, Settings const& s) {
    auto lookup = lookupOf(target);
    switch (kind) {
    case Kind::OminousBottle:
        return !hasEffect(target, effect::BadOmen);
    case Kind::SplashPotion:
    case Kind::Potion:
        return shouldApplyEffects(lookup, itemEffects(filter), s.matchAll, s.matchAmplifier);
    case Kind::MilkBucket:
        return true;
    case Kind::GoldenApple:
        if (isZombieVillager(target)) return !isConverting(target) && hasEffect(target, effect::Weakness);
        if (target.isPlayer()) return shouldApplyEffects(lookup, itemEffects(filter), s.matchAll, s.matchAmplifier);
        return false;
    case Kind::EffectFood:
        return target.isPlayer() && shouldApplyEffects(lookup, itemEffects(filter), s.matchAll, s.matchAmplifier);
    case Kind::None:
        break;
    }
    return false;
}

int useDurationOf(core::ItemStackData const& stack) {
    ItemStack   s    = game::ItemConv::toStack(stack, 1);
    Item const* type = s.mItem.get();
    int         d    = type ? type->getMaxUseDuration(&s) : 0;
    return d > 0 ? d : 32;
}

/// Eats `stack` through the item's vanilla "finished using" path (nutrition, saturation, effects).
/// Returns what is left of the stack (a container), or nothing.
core::ItemStackData eat(Player& player, core::ItemStackData const& stack) {
    ItemStack   food = game::ItemConv::toStack(stack, 1);
    Item const* type = food.mItem.get();
    if (!type) return {};
    (void)type->useTimeDepleted(food, &player.getLevel(), &player);
    if (food.isNull() || food.mCount <= 0) return {};
    auto left = game::ItemConv::toData(food);
    return left.sameItem(stack) ? core::ItemStackData{} : left; // eaten in place: nothing left
}

/// SC `finishUsing` of the item definition. Returns the container to put back into the backpack.
core::ItemStackData finishUsing(Kind kind, core::ItemStackData const& stack, Actor& target) {
    switch (kind) {
    case Kind::OminousBottle:
        // OminousBottleItem: Bad Omen (amplifier = the bottle's level) for 100 minutes.
        addEffect(target, Effect{effect::BadOmen, 120000, std::clamp(stack.aux, 0, 4)}, 1.f);
        target.getLevel().playSound(SharedTypes::Legacy::LevelSoundEvent::ApplyEffectBadOmen, target.getPosition(), 1.f, 1.f);
        return {};
    case Kind::SplashPotion:
        return {}; // applied when thrown
    case Kind::Potion:
        for (auto const& e : itemEffects(stack)) addEffect(target, e, 1.f);
        return game::ItemConv::toData(game::ItemConv::make(kGlassBottle, 1));
    case Kind::MilkBucket:
        target.removeAllEffects();
        return game::ItemConv::toData(game::ItemConv::make(kBucket, 1));
    case Kind::GoldenApple:
        if (isZombieVillager(target) && hasEffect(target, effect::Weakness)) {
            VariantParameterList params{};
            params.mSelf = &target;
            (void)target.executeEvent("minecraft:start_transforming", params);
            return {};
        }
        [[fallthrough]];
    case Kind::EffectFood:
        if (target.isPlayer()) return eat(static_cast<Player&>(target), stack);
        return {};
    case Kind::None:
        break;
    }
    return {};
}

/// Puts a leftover into the backpack (SC `insertItem`); what doesn't fit drops at the target.
void returnToBackpack(game::Backpack& bp, core::ItemStackData const& left, Actor* near) {
    if (left.empty()) return;
    auto r = bp.inventory().insert(left, core::AccessContext{core::Source::Upgrade, nullptr});
    bp.markDirty();
    if (r.remainder.empty() || !near) return;
    ItemStack rest = game::ItemConv::toStack(r.remainder);
    if (near->isPlayer()) {
        auto& p = static_cast<Player&>(*near);
        if (p.addAndRefresh(rest) || rest.isNull() || rest.mCount <= 0) return;
        p.drop(rest, false);
    }
}

void finish(Application& app) {
    Actor* target = findActor(app.target);
    auto&  bp     = *app.backpack;
    if (!target) {
        returnToBackpack(bp, app.stack, nullptr); // target gone: the item goes back
        return;
    }
    if (app.kind != Kind::SplashPotion) useEffects(*target, app.kind); // SC hasItemUseEffects
    returnToBackpack(bp, finishUsing(app.kind, app.stack, *target), target);
}

/// SC `applyTo` for one target. Returns true when an item was taken.
bool applyTo(UpgradeContext const& ctx, Settings const& s, Actor& target) {
    auto state = stateOf(target);
    auto& bp   = ctx.backpack();
    auto& inv  = bp.inventory();
    for (auto const& f : s.filters) {
        if (f.filter.empty() || !test(f.condition, state, f.value)) continue;
        auto kind = kindOf(f.filter.id);
        if (kind == Kind::None || !canApply(kind, target, f.filter, s)) continue;
        auto filterEffects = itemEffects(f.filter);
        for (int i = 0; i < inv.size(); ++i) {
            auto const& stack = inv.at(i);
            if (stack.empty() || !accepts(kind, stack.id)) continue; // cheap check before the effects
            if (!stackMatches(kind, stack, itemEffects(stack), f.filter, filterEffects, s.matchAll, s.matchDuration, s.matchAmplifier)) {
                continue;
            }
            auto taken = inv.extract(i, 1, core::AccessContext{core::Source::Upgrade, nullptr});
            if (taken.empty()) continue;
            bp.markDirty();
            Application app;
            app.backpack = ctx.backpackPtr();
            app.slot     = ctx.slot();
            app.target   = target.getOrCreateUniqueID();
            app.stack    = taken.withCount(1);
            app.kind     = kind;
            if (kind == Kind::SplashPotion) {
                splash(target, app.stack);
                app.useDuration = 1;
            } else {
                app.useDuration = useDurationOf(app.stack);
            }
            app.remaining = app.useDuration;
            gApplications[keyOf(ctx)] = std::move(app);
            return true;
        }
    }
    return false;
}

bool runCheck(UpgradeContext const& ctx, std::vector<Actor*> const& targets) {
    auto s = settingsOf(ctx);
    for (auto* t : targets) {
        if (t && isLiving(*t) && applyTo(ctx, s, *t)) return true;
    }
    return false;
}

} // namespace

long long now() { return gNow; }

bool applying(UpgradeContext const& ctx) { return gApplications.contains(keyOf(ctx)); }

bool checkNow(UpgradeContext const& ctx, std::vector<Actor*> const& targets) {
    if (applying(ctx)) return false;
    bool started = runCheck(ctx, targets);
    if (!started) ctx.transient<CheckState>().next = gNow + kCheckInterval;
    return started;
}

bool check(UpgradeContext const& ctx, std::vector<Actor*> const& targets) {
    if (applying(ctx) || ctx.transient<CheckState>().next > gNow) return false;
    return checkNow(ctx, targets);
}

void tickApplications() {
    for (auto it = gApplications.begin(); it != gApplications.end();) {
        auto& app = it->second;
        --app.remaining;
        if (app.remaining > 0) {
            if (app.kind != Kind::SplashPotion && shouldTriggerUseEffects(app.useDuration, app.remaining)) {
                if (auto* t = findActor(app.target)) useEffects(*t, app.kind);
            }
            ++it;
            continue;
        }
        auto done = std::move(app);
        it        = gApplications.erase(it);
        try {
            finish(done);
        } catch (std::exception const& e) {
            log().error("alchemy upgrade: applying {} failed: {}", done.stack.id, e.what());
        }
        UpgradeContext(done.backpack, done.slot).transient<CheckState>().next = gNow + kCheckInterval;
    }
}

void abortApplications() {
    auto apps = std::move(gApplications);
    gApplications.clear();
    for (auto& [key, app] : apps) {
        try {
            returnToBackpack(*app.backpack, app.stack, nullptr);
            game::Storage::get().save(*app.backpack);
        } catch (std::exception const& e) {
            log().error("alchemy upgrade: returning {} failed: {}", app.stack.id, e.what());
        }
    }
}

// ---- upgrade types -----------------------------------------------------------------------------------

namespace {

template <bool Advanced>
class AlchemyUpgradeT : public IUpgrade {
public:
    [[nodiscard]] UpgradeInfo const& info() const override {
        static UpgradeInfo const kInfo = [] {
            UpgradeInfo i;
            i.id           = Advanced ? kAdvancedId : kBasicId;
            i.itemId       = Advanced ? kAdvancedItem : kBasicItem;
            i.nameKey      = Advanced ? "abp.alchemy.advanced.name" : "abp.alchemy.name";
            i.nameFallback = Advanced ? "Advanced Alchemy Upgrade" : "Alchemy Upgrade";
            i.tier         = Advanced ? UpgradeTier::Advanced : UpgradeTier::Basic;
            i.group        = kGroup;
            // SB: no conflicts; the per-storage limit is the global one (several may be installed).
            i.maxPerBackpack = 99;
            i.hasSettings    = true;
            i.toggleable     = true;
            return i;
        }();
        return kInfo;
    }

    void onRemove(UpgradeContext const& ctx) override {
        // A running application ends now (the item goes back into the backpack).
        auto it = gApplications.find(keyOf(ctx));
        if (it == gApplications.end()) return;
        auto app = std::move(it->second);
        gApplications.erase(it);
        returnToBackpack(*app.backpack, app.stack, findActor(app.target));
    }

    void onPlacedTick(PlacedTickContext const& tick) override {
        auto const& ctx = tick.ctx;
        if (applying(ctx) || ctx.transient<CheckState>().next > gNow) return;
        auto s = settingsOf(ctx);
        if (std::all_of(s.filters.begin(), s.filters.end(), [](FilterAttribute const& f) { return f.filter.empty(); })) {
            ctx.transient<CheckState>().next = gNow + kCheckInterval;
            return;
        }
        // SC: AABB(pos).inflate(CHECK_RADIUS) around the backpack block.
        float r = static_cast<float>(kCheckRadius);
        AABB  box{
            Vec3{static_cast<float>(tick.x) - r, static_cast<float>(tick.y) - r, static_cast<float>(tick.z) - r},
            Vec3{static_cast<float>(tick.x) + 1 + r, static_cast<float>(tick.y) + 1 + r, static_cast<float>(tick.z) + 1 + r}
        };
        std::vector<Actor*> targets;
        for (auto* a : livingIn(tick.region, box)) {
            if (entityMatches(s.entityMatch, a->isPlayer())) targets.push_back(a);
        }
        (void)checkNow(ctx, targets);
    }
};

using AlchemyUpgrade         = AlchemyUpgradeT<false>;
using AdvancedAlchemyUpgrade = AlchemyUpgradeT<true>;

/// Runs the checks of carried backpacks every tick (SC checks every 5 ticks and counts the use
/// time down every tick; the foundation's carried tick is coarser).
class AlchemyTicker {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListener = bus.emplaceListener<ll::event::ServerLevelTickEvent>([](ll::event::ServerLevelTickEvent& ev) { tick(ev.level()); });
        // An item being used goes back into its backpack before the storage is saved and dropped
        // (BDS stop, leaving a local world on the client).
        mStopping = bus.emplaceListener<ll::event::ServerStoppingEvent>(
            [](ll::event::ServerStoppingEvent&) { abortApplications(); },
            ll::event::EventPriority::High
        );
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        bus.removeListener(mListener);
        bus.removeListener(mStopping);
        abortApplications();
    }

private:
    static void tick(Level& level) {
        ++gNow;
        if (!gApplications.empty()) tickApplications();
        if (gNow % kCheckInterval != 0) return;
        level.forEachPlayer([](Player& player) {
            if (player.isClientSide() || !player.isAlive()) return true;
            for (auto& [carried, bp] : game::carriedBackpacks(player, false, true)) {
                if (!bp) continue;
                forEachInstalled(bp, true, [&](IUpgrade& u, UpgradeContext const& ctx) {
                    if (!isAlchemy(u) || !carried.upgradeActive(u.info().id, u.info().groupOrId())) return;
                    try {
                        (void)check(ctx.withPlayer(&player), {&player});
                    } catch (std::exception const& e) {
                        log().error("alchemy upgrade check failed: {}", e.what());
                    }
                });
            }
            return true;
        });
    }

    ll::event::ListenerPtr mListener;
    ll::event::ListenerPtr mStopping;
};

// ---- native tab: data and actions ------------------------------------------------------------------

/// SC `hasEntityMatchOption`: the storage is not carried by a player (a placed backpack).
bool hasEntityMatchOption(game::Backpack const& bp) { return placed::PlacedBackpacks::get().findUuid(bp.uuid()).has_value(); }

nlohmann::json tabData(Player& player, UpgradeContext const& ctx) {
    bool advanced = isAdvanced(ctx);
    auto s        = settingsOf(ctx);
    auto j        = s.toJson();
    j["advanced"]          = advanced;
    j["entityMatchOption"] = advanced && hasEntityMatchOption(ctx.backpack());
    j["applying"]          = applying(ctx);
    return {
        {"alchemy", std::move(j)},
        // Tab label / closed-tab tooltip (SC translUpgrade / translUpgradeTooltip).
        {"tabLabel", advanced ? trFor(player, "abp.alchemy.tab.advanced", "Adv. Alchemy") : trFor(player, "abp.alchemy.tab", "Alchemy")},
        {"tabTooltip",
         advanced ? trFor(player, "abp.alchemy.tab.advanced.tooltip", "Advanced Alchemy Settings")
                  : trFor(player, "abp.alchemy.tab.tooltip", "Alchemy Settings")},
    };
}

bool tabAction(Player& player, UpgradeContext const& ctx, nlohmann::json const& data) {
    auto what = data.value("alchemy", std::string{});
    if (what.empty()) return false;
    int  index = data.value("index", -1);
    auto s     = settingsOf(ctx);
    if (what == "filter") {
        // SC filter slot: the cursor item is copied in (not consumed); an empty cursor clears it.
        auto cursor = game::ItemConv::toData(player.mPlayerUIContainer->getItem(0));
        if (index < 0 || index >= static_cast<int>(s.filters.size())) return false;
        if (cursor.empty()) return setFilter(ctx, index, {});
        return setFilter(ctx, index, cursor.withCount(1));
    }
    if (what == "condition") {
        if (!s.toggleCondition(index)) return false;
    } else if (what == "scroll") {
        if (!s.scrollValue(index, data.value("dir", 0))) return false;
    } else if (!isAdvanced(ctx)) {
        return false; // the match toggles are advanced-only
    } else if (what == "matchAll") {
        s.matchAll = !s.matchAll;
    } else if (what == "matchDuration") {
        s.matchDuration = !s.matchDuration;
    } else if (what == "matchAmplifier") {
        s.matchAmplifier = !s.matchAmplifier;
    } else if (what == "entityMatch") {
        if (!hasEntityMatchOption(ctx.backpack())) return false;
        s.entityMatch = next(s.entityMatch);
    } else {
        return false;
    }
    writeSettings(ctx, s);
    return true;
}

} // namespace

} // namespace abp::upgrade::alchemy

ABP_REGISTER_UPGRADE(abp::upgrade::alchemy::AlchemyUpgrade);
ABP_REGISTER_UPGRADE(abp::upgrade::alchemy::AdvancedAlchemyUpgrade);
ABP_REGISTER_MODULE(abp::upgrade::alchemy::AlchemyTicker);
ABP_REGISTER_TAB_DATA(abp::net::TabDataProvider{abp::upgrade::alchemy::kGroup, &abp::upgrade::alchemy::tabData});
ABP_REGISTER_TAB_ACTION(abp::net::TabActionHandler{abp::upgrade::alchemy::kGroup, &abp::upgrade::alchemy::tabAction});
