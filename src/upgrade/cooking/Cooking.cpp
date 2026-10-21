#include "upgrade/cooking/Cooking.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/SaveContext.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/actor/FurnaceBlockActor.h"

#include "game/ItemConv.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"

ABP_REGISTER_CONFIG("cooking", abp::upgrade::cooking::CookingConfig);

namespace abp::upgrade::cooking {

namespace {

constexpr char const* kStateKey       = "state";
constexpr char const* kInputFilterKey = "inputFilter";
constexpr char const* kFuelFilterKey  = "fuelFilter";
constexpr char const* kProductsKey    = "products";

/// Per-world caches of recipe results and fuel values (cleared when the level changes).
struct Caches {
    Level const*                                                   level = nullptr;
    std::unordered_map<std::string, std::optional<core::ItemStackData>> recipes;
    std::unordered_map<std::string, int>                           fuel;
};

Caches& caches(Level const* level) {
    static Caches c;
    if (c.level != level || c.recipes.size() > 4096 || c.fuel.size() > 4096) {
        c.level = level;
        c.recipes.clear();
        c.fuel.clear();
    }
    return c;
}

std::string cacheKey(Kind kind, core::ItemStackData const& item) {
    return std::to_string(static_cast<int>(kind)) + "|" + item.id + "|" + std::to_string(item.aux) + "|"
         + std::to_string(item.nbtHash());
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

std::string_view nameFallback(Kind kind, bool automatic) {
    switch (kind) {
    case Kind::Smoking:
        return automatic ? "Auto-Smoking Upgrade" : "Smoking Upgrade";
    case Kind::Blasting:
        return automatic ? "Auto-Blasting Upgrade" : "Blasting Upgrade";
    default:
        return automatic ? "Auto-Smelting Upgrade" : "Smelting Upgrade";
    }
}

core::Filter filterAt(UpgradeContext const& ctx, char const* key, int slots) {
    auto const& s = ctx.settings();
    auto        it = s.find(key);
    return core::Filter::fromJson(it != s.end() ? *it : nlohmann::json::object(), slots);
}

int filterSlots(int configured) { return std::clamp(configured, 1, 9); }

/// Ids of items this Auto upgrade has produced (not cooked again unless explicitly filtered).
std::unordered_set<std::string> productIds(UpgradeContext const& ctx) {
    std::unordered_set<std::string> out;
    auto const&                     s  = ctx.settings();
    auto                            it = s.find(kProductsKey);
    if (it == s.end() || !it->is_array()) return out;
    for (auto const& v : *it)
        if (v.is_string()) out.insert(v.get<std::string>());
    return out;
}

void setProductIds(UpgradeContext const& ctx, std::unordered_set<std::string> const& ids) {
    std::vector<std::string> sorted(ids.begin(), ids.end());
    std::sort(sorted.begin(), sorted.end());
    if (sorted.size() > 256) sorted.resize(256);
    ctx.settings()[kProductsKey] = sorted;
    ctx.saveSettings(false);
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

int burnInterval(Kind kind) { return kind == Kind::Smelting ? 200 : 100; }

std::string_view blockId(Kind kind) {
    switch (kind) {
    case Kind::Smoking:
        return "minecraft:smoker";
    case Kind::Blasting:
        return "minecraft:blast_furnace";
    default:
        return "minecraft:furnace";
    }
}

std::optional<core::ItemStackData> recipeResult(Kind kind, core::ItemStackData const& input) {
    if (input.empty()) return std::nullopt;
    auto level = ll::service::getLevel();
    if (!level) return std::nullopt;
    auto& cache = caches(level.as_ptr());
    auto  key   = cacheKey(kind, input);
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

int vanillaBurnTicks(Kind kind, core::ItemStackData const& fuel) {
    if (fuel.empty()) return 0;
    auto  level = ll::service::getLevel();
    auto& cache = caches(level ? level.as_ptr() : nullptr);
    auto  key   = cacheKey(kind, fuel);
    if (auto it = cache.fuel.find(key); it != cache.fuel.end()) return it->second;
    int  ticks = 0;
    auto stack = game::ItemConv::toStack(fuel, 1);
    if (!stack.isNull()) {
        ticks = static_cast<int>(std::lround(FurnaceBlockActor::getBurnDuration(stack, static_cast<float>(burnInterval(kind)))));
    }
    cache.fuel.emplace(std::move(key), std::max(0, ticks));
    return std::max(0, ticks);
}

int xpFor(core::ItemStackData const& output, int count) {
    if (output.empty() || count <= 0) return 0;
    auto stack = game::ItemConv::toStack(output, 1);
    if (stack.isNull()) return 0;
    return std::max(0, FurnaceBlockActor::getXPRewardFromSmeltingItems(stack, count));
}

CookEnv makeEnv(Kind kind) {
    auto const& cfg   = config::get<CookingConfig>();
    double      speed = cfg.cookingSpeedMultiplier > 0.01 ? cfg.cookingSpeedMultiplier : 1.0;
    double      fuel  = cfg.fuelEfficiencyMultiplier > 0.01 ? cfg.fuelEfficiencyMultiplier : 1.0;
    CookEnv     env;
    env.cookTime = std::max(1, static_cast<int>(std::lround(burnInterval(kind) / speed)));
    env.result   = [kind](core::ItemStackData const& in) { return recipeResult(kind, in); };
    // Fuel lasts as many items as in the vanilla block, also when the speed multiplier changes.
    env.burnTicks = [kind, speed, fuel](core::ItemStackData const& f) {
        int base = vanillaBurnTicks(kind, f);
        return base <= 0 ? 0 : std::max(1, static_cast<int>(std::lround(base * fuel / speed)));
    };
    // The game's own fuel consumption: one item less, or its leftover (lava bucket -> bucket).
    env.afterBurn = [](core::ItemStackData const& f) -> core::ItemStackData {
        auto stack = game::ItemConv::toStack(f, f.count);
        if (!stack.isNull()) {
            stack.useAsFuel();
            auto left = game::ItemConv::toData(stack);
            if (!(left.sameItem(f) && left.count == f.count)) return left;
        }
        if (f.id == "minecraft:lava_bucket" && f.count <= 1) {
            return game::ItemConv::toData(game::ItemConv::make("minecraft:bucket", 1));
        }
        return f.withCount(f.count - 1);
    };
    return env;
}

// ---- upgrade -------------------------------------------------------------------------------------

CookingUpgrade::CookingUpgrade(Kind kind, bool automatic) : mKind(kind), mAuto(automatic) {
    std::string base = std::string(idSuffix(kind));
    std::string id   = automatic ? "auto_" + base : base;
    mInfo.id           = id;
    mInfo.itemId       = "alleviatedbackpacks:" + id + "_upgrade";
    mInfo.nameKey      = "abp.cooking." + id + ".name";
    mInfo.nameFallback = std::string(nameFallback(kind, automatic));
    mInfo.tier         = automatic ? UpgradeTier::Advanced : UpgradeTier::Basic;
    // Like Sophisticated Backpacks: one cooking upgrade per backpack.
    mInfo.group          = "cooking";
    mInfo.maxPerBackpack = 1;
    mInfo.hasSettings    = true;
    mInfo.toggleable     = true;
}

bool CookingUpgrade::installedAt(UpgradeContext const& ctx) const {
    auto const& slots = ctx.backpack().data().upgrades;
    if (ctx.slot() < 0 || ctx.slot() >= static_cast<int>(slots.size())) return false;
    auto const& s = slots[static_cast<std::size_t>(ctx.slot())];
    return !s.empty() && s.item.id == mInfo.itemId;
}

CookState CookingUpgrade::state(UpgradeContext const& ctx) {
    auto const& s  = ctx.settings();
    auto        it = s.find(kStateKey);
    return it != s.end() ? CookState::fromJson(*it) : CookState{};
}

void CookingUpgrade::setState(UpgradeContext const& ctx, CookState const& state) {
    ctx.settings()[kStateKey] = state.toJson();
    ctx.saveSettings(false);
}

core::Filter CookingUpgrade::inputFilter(UpgradeContext const& ctx) {
    return filterAt(ctx, kInputFilterKey, filterSlots(config::get<CookingConfig>().autoInputFilterSlots));
}

core::Filter CookingUpgrade::fuelFilter(UpgradeContext const& ctx) {
    return filterAt(ctx, kFuelFilterKey, filterSlots(config::get<CookingConfig>().autoFuelFilterSlots));
}

void CookingUpgrade::setInputFilter(UpgradeContext const& ctx, core::Filter const& filter) {
    ctx.settings()[kInputFilterKey] = filter.toJson();
    ctx.saveSettings(false);
}

void CookingUpgrade::setFuelFilter(UpgradeContext const& ctx, core::Filter const& filter) {
    ctx.settings()[kFuelFilterKey] = filter.toJson();
    ctx.saveSettings(false);
}

bool CookingUpgrade::advance(UpgradeContext const& ctx, Player* carrier, int ticks) const {
    if (!installedAt(ctx) || ticks <= 0) return false;
    auto st      = state(ctx);
    auto env     = makeEnv(mKind);
    bool changed = false;
    if (!mAuto) {
        for (int i = 0; i < ticks; ++i) changed |= step(st, env);
    } else {
        // Experience per result kind (stored results give their XP to the carrier right away).
        std::unordered_map<std::string, std::pair<core::ItemStackData, int>> produced;
        auto products = productIds(ctx);
        bool newProduct = false;
        env.onProduce   = [&](core::ItemStackData const& r) {
            auto& e = produced[r.id];
            e.first = r;
            e.second += r.count;
            if (products.insert(r.id).second) newProduct = true;
        };
        auto    inFilter   = inputFilter(ctx);
        auto    fuelFilter = CookingUpgrade::fuelFilter(ctx);
        AutoEnv a{ctx.backpack().inventory(), inFilter, fuelFilter, &game::ItemConv::tags(), {core::Source::Upgrade, carrier}};
        a.isProduct = [&products](core::ItemStackData const& item) { return products.contains(item.id); };
        changed |= autoExchange(st, env, a);
        // A storage scan that found nothing is not repeated within this call.
        bool outFailed   = !st.output.empty();
        bool inputFailed = st.input.empty();
        bool fuelFailed  = needsFuel(st, env);
        for (int i = 0; i < ticks; ++i) {
            if (!st.output.empty() && !outFailed) {
                if (pushOutput(st, a)) changed = true;
                outFailed = !st.output.empty();
            }
            if (st.input.empty() && !inputFailed) {
                if (pullInput(st, env, a)) {
                    changed    = true;
                    fuelFailed = false;
                } else {
                    inputFailed = true;
                }
            }
            if (!fuelFailed && needsFuel(st, env)) {
                if (pullFuel(st, env, a)) changed = true;
                else fuelFailed = true;
            }
            changed |= step(st, env);
        }
        if (!outFailed) changed |= pushOutput(st, a);
        if (carrier) {
            int xp = 0;
            for (auto const& [id, e] : produced) xp += xpFor(e.first, e.second);
            if (xp > 0) carrier->addExperience(xp);
        }
        if (newProduct) setProductIds(ctx, products);
    }
    if (changed) setState(ctx, st);
    return changed;
}

void CookingUpgrade::onTick(TickContext const& tick) { advance(tick.ctx, &tick.player, std::max(1, tick.interval)); }

std::shared_ptr<ui::Menu> CookingUpgrade::createSettingsMenu(Player& player, UpgradeContext const& ctx) {
    return makeMenu(player, *this, ctx);
}

std::vector<std::string> CookingUpgrade::describe(Player& player, UpgradeContext const& ctx) const {
    if (!installedAt(ctx)) return {};
    auto                     st = state(ctx);
    std::vector<std::string> out;
    out.push_back(trFor(player, "abp.cooking.describe.slots", "§7Input {} · Fuel {} · Output {}", st.input.count, st.fuel.count, st.output.count));
    if (st.cook > 0) {
        out.push_back(trFor(player, "abp.cooking.describe.cooking", "§6Cooking {}%", st.cookPercent()));
    }
    return out;
}

CookingUpgrade& upgradeOf(Kind kind, bool automatic) {
    std::string id = std::string(automatic ? "auto_" : "") + std::string(idSuffix(kind));
    return *static_cast<CookingUpgrade*>(byId(id));
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
