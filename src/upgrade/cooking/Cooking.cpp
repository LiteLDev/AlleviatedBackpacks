#include "upgrade/cooking/Cooking.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <string>
#include <unordered_map>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/actor/item/ExperienceOrb.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/SaveContext.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/actor/FurnaceBlockActor.h"

#include "api/BackpackCapabilities.h"
#include "api/abp/Fluid.h"
#include "api/abp/Xp.h"
#include "game/ItemConv.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/inception/Inception.h"

ABP_REGISTER_CONFIG("cooking", abp::upgrade::cooking::CookingConfig);

namespace abp::upgrade::cooking {

namespace {

constexpr char const* kStateKey       = "state";
constexpr char const* kInputFilterKey = "inputFilter";
constexpr char const* kFuelFilterKey  = "fuelFilter";
constexpr int         kPlacedBatchTicks = 10; ///< placed: game ticks simulated per advance()

/// Per-world caches of recipe results and fuel values (cleared when the level changes).
struct Caches {
    Level const*                                                        level = nullptr;
    std::unordered_map<std::string, std::optional<core::ItemStackData>> recipes;
    std::unordered_map<std::string, int>                                fuel;
    std::unordered_map<std::string, float>                              xp;
};

Caches& caches(Level const* level) {
    static Caches c;
    if (c.level != level || c.recipes.size() > 4096 || c.fuel.size() > 4096 || c.xp.size() > 4096) {
        c.level = level;
        c.recipes.clear();
        c.fuel.clear();
        c.xp.clear();
    }
    return c;
}

std::string cacheKey(int kind, core::ItemStackData const& item) {
    return std::to_string(kind) + "|" + item.id + "|" + std::to_string(item.aux) + "|" + std::to_string(item.nbtHash());
}

std::string_view idSuffix(Kind kind) {
    switch (kind) {
    case Kind::Smoking:
        return "smoking";
    case Kind::Blasting:
        return "blasting";
    default:
        return "smelting";
    }
}

/// SB item names (en_US lang).
std::string_view nameFallback(Kind kind, bool automatic) {
    switch (kind) {
    case Kind::Smoking:
        return automatic ? "Auto-smoking Upgrade" : "Smoking Upgrade";
    case Kind::Blasting:
        return automatic ? "Auto-blasting Upgrade" : "Blasting Upgrade";
    default:
        return automatic ? "Auto-smelting Upgrade" : "Smelting Upgrade";
    }
}

double clampMultiplier(double v) { return std::clamp(std::isfinite(v) ? v : 1.0, 0.25, 4.0); }

/// Runtime cooldowns of one upgrade (SC keeps them in the wrapper).
struct Cooldowns {
    int cooldown = 0, output = 0, fuel = 0, input = 0;
};

core::Filter filterAt(UpgradeContext const& ctx, char const* key, int slots, bool emptyAllowsAll) {
    auto const& s  = ctx.settings();
    core::Filter f(slots);
    if (s.is_object()) {
        if (auto it = s.find(key); it != s.end()) f = core::Filter::fromJson(*it, slots);
    }
    f.emptyAllowsAll = emptyAllowsAll;
    return f;
}

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}

} // namespace

// ---- game data ----------------------------------------------------------------------------------

std::string_view recipeTag(Kind kind) {
    switch (kind) {
    case Kind::Smoking:
        return "smoker";
    case Kind::Blasting:
        return "blast_furnace";
    default:
        return "furnace";
    }
}

int recipeTime(Kind kind) { return kind == Kind::Smelting ? 200 : 100; }

float burnTimeModifier(Kind kind) { return kind == Kind::Smelting ? 1.f : 0.5f; }

std::optional<core::ItemStackData> recipeResult(Kind kind, core::ItemStackData const& input) {
    if (input.empty()) return std::nullopt;
    auto level = ll::service::getLevel();
    if (!level) return std::nullopt;
    auto& cache = caches(level.as_ptr());
    auto  key   = cacheKey(static_cast<int>(kind), input);
    if (auto it = cache.recipes.find(key); it != cache.recipes.end()) return it->second;
    std::optional<core::ItemStackData> out;
    auto                               stack = game::ItemConv::toStack(input, 1);
    if (!stack.isNull()) {
        ItemInstance result = level->getRecipes().getFurnaceRecipeResult(stack, HashedString(std::string(recipeTag(kind))));
        if (!result.isNull()) {
            if (auto tag = result.save(SaveContext{SaveContext::SaveUseCase::SaveToDisk})) {
                auto data = game::ItemConv::toData(ItemStack::fromTag(*tag));
                if (!data.empty()) out = std::move(data);
            }
        }
    }
    cache.recipes.emplace(std::move(key), out);
    return out;
}

int furnaceBurnTicks(core::ItemStackData const& fuel) {
    if (fuel.empty()) return 0;
    auto  level = ll::service::getLevel();
    auto& cache = caches(level ? level.as_ptr() : nullptr);
    auto  key   = cacheKey(-1, fuel);
    if (auto it = cache.fuel.find(key); it != cache.fuel.end()) return it->second;
    int  ticks = 0;
    auto stack = game::ItemConv::toStack(fuel, 1);
    if (!stack.isNull()) ticks = static_cast<int>(std::lround(FurnaceBlockActor::getBurnDuration(stack, 200.f)));
    ticks = std::max(0, ticks);
    cache.fuel.emplace(std::move(key), ticks);
    return ticks;
}

float smeltingExperience(core::ItemStackData const& result) {
    if (result.empty()) return 0;
    auto  level = ll::service::getLevel();
    auto& cache = caches(level ? level.as_ptr() : nullptr);
    auto  key   = cacheKey(-2, result);
    if (auto it = cache.xp.find(key); it != cache.xp.end()) return it->second;
    float xp    = 0;
    auto  stack = game::ItemConv::toStack(result, 1);
    if (!stack.isNull()) xp = std::max(0.f, FurnaceBlockActor::_getXPRewardMultiplier(stack));
    cache.xp.emplace(std::move(key), xp);
    return xp;
}

// ---- upgrade ------------------------------------------------------------------------------------

CookingUpgrade::CookingUpgrade(Kind kind, bool automatic) : mKind(kind), mAuto(automatic) {
    std::string base   = std::string(idSuffix(kind));
    std::string id     = automatic ? "auto_" + base : base;
    mInfo.id           = id;
    mInfo.itemId       = "alleviatedbackpacks:" + id + "_upgrade";
    mInfo.nameKey      = "abp.cooking." + id + ".name";
    mInfo.nameFallback = std::string(nameFallback(kind, automatic));
    mInfo.tier         = automatic ? UpgradeTier::Advanced : UpgradeTier::Basic;
    // SB group "furnace_upgrades": its limit (1) is SB's configurable maxUpgradesPerStorage entry,
    // checked by the admin unit.
    mInfo.group          = std::string(kGroup);
    mInfo.maxPerBackpack = kUnlimitedPerBackpack;
    mInfo.hasSettings    = true;
    mInfo.toggleable     = true;
}

namespace {
CookingUpgradeConfig manualConfig(Kind kind) {
    auto const& c = config::get<CookingConfig>();
    return kind == Kind::Smoking ? c.smokingUpgrade : kind == Kind::Blasting ? c.blastingUpgrade : c.smeltingUpgrade;
}
AutoCookingUpgradeConfig autoConfig(Kind kind) {
    auto const& c = config::get<CookingConfig>();
    return kind == Kind::Smoking ? c.autoSmokingUpgrade : kind == Kind::Blasting ? c.autoBlastingUpgrade : c.autoSmeltingUpgrade;
}
} // namespace

double CookingUpgrade::speedMultiplier() const {
    return clampMultiplier(mAuto ? autoConfig(mKind).smeltingSpeedMultiplier : manualConfig(mKind).smeltingSpeedMultiplier);
}
double CookingUpgrade::fuelMultiplier() const {
    return clampMultiplier(mAuto ? autoConfig(mKind).fuelEfficiencyMultiplier : manualConfig(mKind).fuelEfficiencyMultiplier);
}
int CookingUpgrade::inputFilterSlots() const { return std::clamp(autoConfig(mKind).inputFilterSlots, 1, 20); }
int CookingUpgrade::inputFilterSlotsInRow() const { return std::clamp(autoConfig(mKind).inputFilterSlotsInRow, 1, 6); }
int CookingUpgrade::fuelFilterSlots() const { return std::clamp(autoConfig(mKind).fuelFilterSlots, 1, 20); }
int CookingUpgrade::fuelFilterSlotsInRow() const { return std::clamp(autoConfig(mKind).fuelFilterSlotsInRow, 1, 6); }

bool CookingUpgrade::installedAt(UpgradeContext const& ctx) const {
    auto const& slots = ctx.backpack().data().upgrades;
    if (ctx.slot() < 0 || ctx.slot() >= static_cast<int>(slots.size())) return false;
    auto const& s = slots[static_cast<std::size_t>(ctx.slot())];
    return !s.empty() && s.item.id == mInfo.itemId;
}

CookState CookingUpgrade::state(UpgradeContext const& ctx) {
    auto const& s = ctx.settings();
    if (!s.is_object()) return {};
    auto it = s.find(kStateKey);
    return it != s.end() ? CookState::fromJson(*it) : CookState{};
}

void CookingUpgrade::setState(UpgradeContext const& ctx, CookState const& state) {
    auto& s = ctx.settings();
    if (!s.is_object()) s = nlohmann::json::object();
    s[kStateKey] = state.toJson();
    ctx.saveSettings(false);
}

core::Filter CookingUpgrade::inputFilter(UpgradeContext const& ctx) const {
    return filterAt(ctx, kInputFilterKey, inputFilterSlots(), false);
}

core::Filter CookingUpgrade::fuelFilter(UpgradeContext const& ctx) const {
    return filterAt(ctx, kFuelFilterKey, fuelFilterSlots(), true);
}

void CookingUpgrade::setInputFilter(UpgradeContext const& ctx, core::Filter const& filter) {
    auto& s = ctx.settings();
    if (!s.is_object()) s = nlohmann::json::object();
    s[kInputFilterKey] = filter.toJson();
    ctx.saveSettings(false);
}

void CookingUpgrade::setFuelFilter(UpgradeContext const& ctx, core::Filter const& filter) {
    auto& s = ctx.settings();
    if (!s.is_object()) s = nlohmann::json::object();
    s[kFuelFilterKey] = filter.toJson();
    ctx.saveSettings(false);
}

CookEnv CookingUpgrade::env(UpgradeContext const& ctx) const {
    CookEnv e;
    Kind    kind = mKind;
    e.result     = [kind](core::ItemStackData const& in) { return recipeResult(kind, in); };
    float mod    = burnTimeModifier(kind);
    e.burnTicks  = [mod](core::ItemStackData const& f) { return static_cast<int>(static_cast<float>(furnaceBurnTicks(f)) * mod); };
    // The fuel's remainder (lava bucket -> bucket), else one item less.
    e.afterBurn = [](core::ItemStackData const& f) -> core::ItemStackData {
        auto stack = game::ItemConv::toStack(f, f.count);
        if (!stack.isNull()) {
            stack.useAsFuel();
            auto left = game::ItemConv::toData(stack);
            if (!(left.sameItem(f) && left.count == f.count)) return left;
        }
        if (f.id == "minecraft:lava_bucket" && f.count <= 1) return game::ItemConv::toData(game::ItemConv::make("minecraft:bucket", 1));
        return f.withCount(f.count - 1);
    };
    e.experience               = [](core::ItemStackData const& r) { return smeltingExperience(r); };
    e.recipeTime               = recipeTime(kind);
    e.cookingSpeedMultiplier   = speedMultiplier();
    e.fuelEfficiencyMultiplier = fuelMultiplier();
    if (mAuto) {
        auto in        = inputFilter(ctx);
        auto fuel      = fuelFilter(ctx);
        e.inputAllowed = [in](core::ItemStackData const& d) { return in.passes(d, &game::ItemConv::tags()); };
        e.fuelAllowed  = [fuel](core::ItemStackData const& d) { return fuel.passes(d, &game::ItemConv::tags()); };
    }
    return e;
}

bool CookingUpgrade::advance(UpgradeContext const& ctx, int ticks) const {
    if (!installedAt(ctx) || ticks <= 0) return false;
    auto  before = state(ctx);
    auto  s      = before;
    auto& cd     = ctx.transient<Cooldowns>();
    s.cooldown       = cd.cooldown;
    s.outputCooldown = cd.output;
    s.fuelCooldown   = cd.fuel;
    s.inputCooldown  = cd.input;
    auto e           = env(ctx);
    if (!mAuto) {
        runManual(s, e, ticks);
    } else {
        // SC getInventoryForUpgradeProcessing: with Inception also the backpacks inside.
        auto invs = inception::upgradeInventories(ctx.backpackPtr());
        AutoEnv a{ctx.backpack().inventory(), core::AccessContext{core::Source::Upgrade, ctx.player()}, [&ctx](CookState& st) {
                      pushExperienceToTanks(ctx, st);
                  }};
        if (invs.size() > 1) {
            for (auto const& bp : invs) a.chain.push_back(&bp->inventory());
        }
        runAuto(s, e, a, ticks);
    }
    cd = Cooldowns{s.cooldown, s.outputCooldown, s.fuelCooldown, s.inputCooldown};
    // An idle furnace only advances its clock: nothing depends on it then, so don't persist it.
    auto idle = [](CookState const& x) { return !x.burning() && !x.cooking; };
    auto same = s;
    same.clock = before.clock;
    if (idle(before) && idle(s) && same.sameData(before)) return false;
    setState(ctx, s);
    return true;
}

void CookingUpgrade::onTick(TickContext const& tick) { advance(tick.ctx.withPlayer(&tick.player), std::max(1, tick.interval)); }

void CookingUpgrade::onPlacedTick(PlacedTickContext const& tick) {
    // Placed ticks come often: run the furnace in batches like the carried tick does.
    struct Batch {
        int ticks = 0;
    };
    auto& b  = tick.ctx.transient<Batch>();
    b.ticks += std::max(1, tick.interval);
    if (b.ticks < kPlacedBatchTicks) return;
    int n   = b.ticks;
    b.ticks = 0;
    advance(tick.ctx, n);
}

void CookingUpgrade::onRemove(UpgradeContext const& ctx) {
    auto s = state(ctx);
    s.stop();
    setState(ctx, s);
    ctx.transient<Cooldowns>() = {};
}

int CookingUpgrade::popExperience(UpgradeContext const& ctx, Player& player) {
    auto  s      = state(ctx);
    float stored = s.storedXp;
    int   whole  = static_cast<int>(std::floor(stored));
    float frac   = stored - static_cast<float>(whole);
    if (frac != 0.f && std::uniform_real_distribution<float>(0.f, 1.f)(rng()) < frac) ++whole;
    if (whole > 0) {
        ExperienceOrb::spawnOrbs(player.getDimensionBlockSource(), player.getPosition(), whole, ExperienceOrb::DropType::NoType, nullptr);
    }
    s.storedXp = std::max(0.f, stored - static_cast<float>(whole));
    if (s.storedXp != stored) setState(ctx, s);
    return whole;
}

void CookingUpgrade::pushExperienceToTanks(UpgradeContext const& ctx, CookState& state) {
    if (state.storedXp <= 0) return;
    auto handler = api::backpack::fluidHandler(ctx.backpackPtr());
    if (!handler) return;
    int amount = api::xp::experienceToLiquid(state.storedXp);
    if (amount <= 0) return;
    int filled = handler->fill(api::FluidStack{std::string(api::fluids::Experience), amount}, false);
    if (filled > 0) state.storedXp = std::max(0.f, state.storedXp - api::xp::liquidToExperience(filled));
}

std::vector<std::string> CookingUpgrade::describe(Player& player, UpgradeContext const&) const {
    // SB item tooltips (gray).
    std::vector<std::string> out;
    switch (mKind) {
    case Kind::Smoking:
        out.push_back(trFor(player, "abp.cooking.tooltip.smoking", "§7Smoking food in an upgrade tab"));
        break;
    case Kind::Blasting:
        out.push_back(trFor(player, "abp.cooking.tooltip.blasting", "§7Blasting ores and metal stuff in an upgrade tab"));
        break;
    default:
        out.push_back(trFor(player, "abp.cooking.tooltip.smelting", "§7Smelting in an upgrade tab"));
        break;
    }
    if (mAuto) out.push_back(trFor(player, "abp.cooking.tooltip.auto", "§7Automatically pulls/pushes items"));
    return out;
}

CookingUpgrade& upgradeOf(Kind kind, bool automatic) {
    std::string id = std::string(automatic ? "auto_" : "") + std::string(idSuffix(kind));
    return *static_cast<CookingUpgrade*>(byId(id));
}

CookingUpgrade* cookingAt(UpgradeContext const& ctx) {
    auto const& slots = ctx.backpack().data().upgrades;
    if (ctx.slot() < 0 || ctx.slot() >= static_cast<int>(slots.size())) return nullptr;
    auto const& s = slots[static_cast<std::size_t>(ctx.slot())];
    if (s.empty()) return nullptr;
    auto* type = upgradeFor(s.item);
    if (!type || type->info().group != kGroup) return nullptr;
    return static_cast<CookingUpgrade*>(type);
}

// ---- registration -------------------------------------------------------------------------------

namespace {
struct SmeltingUpgrade : CookingUpgrade {
    SmeltingUpgrade() : CookingUpgrade(Kind::Smelting, false) {}
};
struct SmokingUpgrade : CookingUpgrade {
    SmokingUpgrade() : CookingUpgrade(Kind::Smoking, false) {}
};
struct BlastingUpgrade : CookingUpgrade {
    BlastingUpgrade() : CookingUpgrade(Kind::Blasting, false) {}
};
struct AutoSmeltingUpgrade : CookingUpgrade {
    AutoSmeltingUpgrade() : CookingUpgrade(Kind::Smelting, true) {}
};
struct AutoSmokingUpgrade : CookingUpgrade {
    AutoSmokingUpgrade() : CookingUpgrade(Kind::Smoking, true) {}
};
struct AutoBlastingUpgrade : CookingUpgrade {
    AutoBlastingUpgrade() : CookingUpgrade(Kind::Blasting, true) {}
};
} // namespace

ABP_REGISTER_UPGRADE(SmeltingUpgrade);
ABP_REGISTER_UPGRADE(SmokingUpgrade);
ABP_REGISTER_UPGRADE(BlastingUpgrade);
ABP_REGISTER_UPGRADE(AutoSmeltingUpgrade);
ABP_REGISTER_UPGRADE(AutoSmokingUpgrade);
ABP_REGISTER_UPGRADE(AutoBlastingUpgrade);

} // namespace abp::upgrade::cooking
