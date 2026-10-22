#pragma once

// Game-independent logic of the Smelting / Smoking / Blasting upgrades and their Auto variants,
// following Sophisticated Core's `CookingLogic` and `AutoCookingUpgradeWrapper`. Header-only and
// free of mc/ll includes so that it is unit-tested in tests/.
//
// SC keeps absolute game times (burn / cook "finish" times). Here every upgrade has its own clock
// (`CookState::clock`) that advances once per simulated tick, so a disabled upgrade (not ticked)
// keeps its remaining burn and cook time exactly like SC's pause/unpause.

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>

#include "nlohmann/json.hpp"

#include "core/BackpackInventory.h"
#include "core/Filter.h"
#include "core/ItemStackData.h"

namespace abp::upgrade::cooking {

/// SC CookingLogic.COOK_INPUT_SLOT / FUEL_SLOT / COOK_OUTPUT_SLOT.
inline constexpr int kInputSlot  = 0;
inline constexpr int kFuelSlot   = 1;
inline constexpr int kOutputSlot = 2;

/// SC canSmelt: an output stack never grows beyond 99 (nor the item's max stack size).
inline constexpr int kMaxOutputStack = 99;

/// SC CookingUpgradeWrapper / AutoCookingUpgradeWrapper cooldowns (ticks).
inline constexpr int kNothingToDoCooldown    = 10;
inline constexpr int kNoInventorySpaceCooldown = 60;

/// The furnace state of one cooking upgrade (persisted in the upgrade's settings, so it travels
/// with the upgrade item like SC's data components).
struct CookState {
    core::ItemStackData input;
    core::ItemStackData fuel;
    core::ItemStackData output;
    std::int64_t        clock      = 0;  ///< ticks this upgrade has run
    std::int64_t        burnFinish = -1; ///< SC BURN_TIME_FINISH (burning while >= clock)
    int                 burnTotal  = 0;  ///< SC BURN_TIME_TOTAL
    std::int64_t        cookFinish = -1; ///< SC COOK_TIME_FINISH
    int                 cookTotal  = 0;  ///< SC COOK_TIME_TOTAL
    bool                cooking    = false; ///< SC IS_COOKING
    float               storedXp   = 0;     ///< SC STORED_XP
    // Runtime (not persisted): SC's cooldowns.
    int cooldown       = 0;
    int outputCooldown = 0;
    int fuelCooldown   = 0;
    int inputCooldown  = 0;

    [[nodiscard]] bool burning() const { return burnFinish >= clock; }
    /// Ticks of fuel left / of cooking left (0 when not burning / cooking).
    [[nodiscard]] int burnLeft() const { return burning() ? static_cast<int>(std::max<std::int64_t>(burnFinish - clock, 0)) : 0; }
    [[nodiscard]] int cookLeft() const { return cooking ? static_cast<int>(std::max<std::int64_t>(cookFinish - clock, 0)) : 0; }
    /// SC CookingLogicControl.getProgress: the elapsed fraction (the flame is drawn bottom-up
    /// without it, the arrow left-to-right up to it); 0 when not burning / cooking.
    [[nodiscard]] float burnProgress() const {
        if (!burning() || burnTotal <= 0) return 0;
        return 1.f - std::min(1.f, static_cast<float>(burnLeft()) / static_cast<float>(burnTotal));
    }
    [[nodiscard]] float cookProgress() const {
        if (!cooking || cookTotal <= 0) return 0;
        return 1.f - std::min(1.f, static_cast<float>(cookLeft()) / static_cast<float>(cookTotal));
    }

    [[nodiscard]] nlohmann::json toJson() const {
        return nlohmann::json{
            {"input",      input.toJson() },
            {"fuel",       fuel.toJson()  },
            {"output",     output.toJson()},
            {"clock",      clock          },
            {"burnFinish", burnFinish     },
            {"burnTotal",  burnTotal      },
            {"cookFinish", cookFinish     },
            {"cookTotal",  cookTotal      },
            {"cooking",    cooking        },
            {"storedXp",   storedXp       },
        };
    }
    [[nodiscard]] static CookState fromJson(nlohmann::json const& j) {
        CookState s;
        if (!j.is_object()) return s;
        auto num = [&](char const* key, std::int64_t def) -> std::int64_t {
            auto it = j.find(key);
            return it != j.end() && it->is_number_integer() ? it->get<std::int64_t>() : def;
        };
        if (auto it = j.find("input"); it != j.end()) s.input = core::ItemStackData::fromJson(*it);
        if (auto it = j.find("fuel"); it != j.end()) s.fuel = core::ItemStackData::fromJson(*it);
        if (auto it = j.find("output"); it != j.end()) s.output = core::ItemStackData::fromJson(*it);
        s.clock      = std::max<std::int64_t>(0, num("clock", 0));
        s.burnFinish = num("burnFinish", -1);
        s.burnTotal  = static_cast<int>(std::max<std::int64_t>(0, num("burnTotal", 0)));
        s.cookFinish = num("cookFinish", -1);
        s.cookTotal  = static_cast<int>(std::max<std::int64_t>(0, num("cookTotal", 0)));
        if (auto it = j.find("cooking"); it != j.end() && it->is_boolean()) s.cooking = it->get<bool>();
        if (auto it = j.find("storedXp"); it != j.end() && it->is_number()) s.storedXp = std::max(0.f, it->get<float>());
        return s;
    }

    /// Persisted fields equal (cooldowns are runtime only).
    [[nodiscard]] bool sameData(CookState const& o) const {
        return input == o.input && fuel == o.fuel && output == o.output && clock == o.clock && burnFinish == o.burnFinish
            && burnTotal == o.burnTotal && cookFinish == o.cookFinish && cookTotal == o.cookTotal && cooking == o.cooking
            && storedXp == o.storedXp;
    }

    /// SC pause() on removal: the burning fuel and the cooking progress are lost, items stay.
    void stop() {
        burnTotal  = 0;
        cookFinish = -1;
        cookTotal  = 0;
        cooking    = false;
        burnFinish = clock - 1;
    }
};

/// What the cooking logic needs to know about the game.
struct CookEnv {
    /// The result of cooking one `input` item (nullopt / empty = no recipe).
    std::function<std::optional<core::ItemStackData>(core::ItemStackData const&)> result;
    /// Burn ticks of one `fuel` item for this kind (SC `getBurnTime * burnTimeModifier`, before the
    /// config multipliers; 0 = not a fuel).
    std::function<int(core::ItemStackData const&)> burnTicks;
    /// What is left in the fuel slot after burning one item of `fuel` (SC: the crafting remainder,
    /// e.g. lava bucket -> bucket, else one item less).
    std::function<core::ItemStackData(core::ItemStackData const&)> afterBurn;
    /// Experience of one smelted result item (SC recipe experience).
    std::function<float(core::ItemStackData const&)> experience;
    /// Recipe cooking time in ticks (smelting 200, smoking / blasting 100).
    int    recipeTime              = 200;
    double cookingSpeedMultiplier  = 1.0;
    double fuelEfficiencyMultiplier = 1.0;
    /// Extra checks for the input / fuel slot (the Auto variants' filters). Null = recipe / fuel only.
    std::function<bool(core::ItemStackData const&)> inputAllowed;
    std::function<bool(core::ItemStackData const&)> fuelAllowed;
};

/// SC `(int) (recipe.getCookingTime() * (1 / cookingSpeedMultiplier))`.
[[nodiscard]] inline int cookTime(CookEnv const& env) {
    double speed = env.cookingSpeedMultiplier > 0 ? env.cookingSpeedMultiplier : 1.0;
    return static_cast<int>(env.recipeTime * (1 / speed));
}

/// SC `(int) (burnTime * fuelEfficiencyMultiplier / cookingSpeedMultiplier)`.
[[nodiscard]] inline int burnTime(CookEnv const& env, core::ItemStackData const& fuel) {
    if (fuel.empty() || !env.burnTicks) return 0;
    int base = env.burnTicks(fuel.withCount(1));
    if (base <= 0) return 0;
    double speed = env.cookingSpeedMultiplier > 0 ? env.cookingSpeedMultiplier : 1.0;
    return static_cast<int>(base * env.fuelEfficiencyMultiplier / speed);
}

[[nodiscard]] inline std::optional<core::ItemStackData> recipeFor(CookEnv const& env, core::ItemStackData const& input) {
    if (input.empty() || !env.result) return std::nullopt;
    auto r = env.result(input.withCount(1));
    if (!r || r->empty()) return std::nullopt;
    return r;
}

/// SC isInput: has a recipe (and passes the Auto input filter).
[[nodiscard]] inline bool isInput(CookEnv const& env, core::ItemStackData const& item) {
    return recipeFor(env, item).has_value() && (!env.inputAllowed || env.inputAllowed(item));
}
/// SC isFuel: burns (and passes the Auto fuel filter).
[[nodiscard]] inline bool isFuel(CookEnv const& env, core::ItemStackData const& item) {
    return !item.empty() && env.burnTicks && env.burnTicks(item.withCount(1)) > 0 && (!env.fuelAllowed || env.fuelAllowed(item));
}

/// SC CookingComponentItemHandler.isItemValid.
[[nodiscard]] inline bool isItemValid(CookEnv const& env, CookState const& s, int slot, core::ItemStackData const& stack) {
    if (stack.empty()) return true;
    core::ItemStackData const& cur = slot == kInputSlot ? s.input : slot == kFuelSlot ? s.fuel : s.output;
    if (!cur.empty() && cur.sameItem(stack)) return true;
    if (slot == kInputSlot) return isInput(env, stack);
    if (slot == kFuelSlot) return isFuel(env, stack);
    return true;
}

/// SC canSmelt.
[[nodiscard]] inline bool canSmelt(CookState const& s, core::ItemStackData const& recipeOutput) {
    if (s.input.empty() || recipeOutput.empty()) return false;
    if (s.output.empty()) return true;
    if (!s.output.sameItem(recipeOutput)) return false;
    int sum = s.output.count + recipeOutput.count;
    if (sum <= kMaxOutputStack && sum <= std::max(s.output.maxStack, 1)) return true;
    return sum <= std::max(recipeOutput.maxStack, 1);
}

namespace detail {

inline bool isWetSponge(core::ItemStackData const& d) {
    return d.id == "minecraft:wet_sponge" || (d.id == "minecraft:sponge" && d.aux == 1);
}

/// SC smelt().
inline void smelt(CookState& s, CookEnv const& env, core::ItemStackData const& recipeOutput) {
    if (!canSmelt(s, recipeOutput)) return;
    if (s.output.empty()) s.output = recipeOutput;
    else if (s.output.sameItem(recipeOutput)) s.output.count += recipeOutput.count;
    // Drying a wet sponge fills an empty bucket in the fuel slot.
    if (isWetSponge(s.input) && !s.fuel.empty() && s.fuel.id == "minecraft:bucket") {
        s.fuel = core::ItemStackData::of("minecraft:water_bucket", 1, 1);
    }
    if (env.experience) s.storedXp += std::max(0.f, env.experience(recipeOutput)); // SC: the recipe's experience per smelt
    s.input = s.input.withCount(s.input.count - 1);
}

/// SC updateFuel().
inline void updateFuel(CookState& s, CookEnv const& env, core::ItemStackData const& recipeOutput) {
    if (s.burning() || !canSmelt(s, recipeOutput)) return;
    int ticks = burnTime(env, s.fuel);
    if (ticks <= 0) return;
    s.burnFinish = s.clock + ticks;
    s.burnTotal  = ticks;
    if (!s.burning()) return;
    s.fuel = env.afterBurn ? env.afterBurn(s.fuel) : s.fuel.withCount(s.fuel.count - 1);
}

/// SC updateCookingProgress().
inline void updateCookingProgress(CookState& s, CookEnv const& env, core::ItemStackData const& recipeOutput) {
    if (s.cooking && s.cookFinish <= s.clock) {
        smelt(s, env, recipeOutput);
        if (canSmelt(s, recipeOutput)) {
            int t        = cookTime(env);
            s.cookFinish = s.clock + t;
            s.cookTotal  = t;
        } else {
            s.cooking = false;
        }
    } else if (!s.cooking) {
        s.cooking    = true;
        int t        = cookTime(env);
        s.cookFinish = s.clock + t;
        s.cookTotal  = t;
    }
}

/// SC updateCookingCooldown().
inline void updateCookingCooldown(CookState& s) {
    std::int64_t remaining = s.cookFinish - s.clock;
    if (remaining + 2 > s.cookTotal) s.cooking = false;
    else s.cookFinish = s.clock + std::min<std::int64_t>(remaining + 2, s.cookTotal);
}

} // namespace detail

/// SC CookingLogic.tick() at the current clock. Returns whether it did something (false starts
/// the 10 tick "nothing to do" cooldown). Does not advance the clock.
inline bool tick(CookState& s, CookEnv const& env) {
    bool didSomething = true;
    if (s.burning() || (!s.fuel.empty() && !s.input.empty())) {
        auto recipe = recipeFor(env, s.input);
        if (!recipe && s.cooking) s.cooking = false;
        if (recipe) {
            detail::updateFuel(s, env, *recipe);
            if (s.burning() && canSmelt(s, *recipe)) detail::updateCookingProgress(s, env, *recipe);
            else if (!s.burning()) didSomething = false;
        }
    }
    if (!s.burning() && s.cooking) detail::updateCookingCooldown(s);
    else didSomething = false;
    return didSomething;
}

/// A manual cooking upgrade (CookingUpgradeWrapper.tick) for `ticks` game ticks: the wrapper
/// sleeps 10 ticks whenever the logic had nothing to do.
inline void runManual(CookState& s, CookEnv const& env, int ticks) {
    for (int i = 0; i < ticks; ++i) {
        if (s.cooldown > 0) {
            --s.cooldown;
        } else if (!tick(s, env)) {
            s.cooldown = kNothingToDoCooldown - 1; // SC isInCooldown: skips the next 9 ticks
        }
        ++s.clock;
    }
}

/// The Auto variants' storage access (SC `getInventoryForUpgradeProcessing`).
struct AutoEnv {
    core::BackpackInventory& inventory;
    core::AccessContext      access{core::Source::Upgrade};
    /// Called after output was pushed into the storage (SC tryPushingXpToTanks).
    std::function<void(CookState&)> pushXp;
};

namespace detail {

inline int insertAmount(AutoEnv const& a, core::ItemStackData const& stack, bool simulate) {
    auto r = a.inventory.insert(stack, a.access, simulate);
    return r.inserted + r.voided;
}

/// SC tryPushingOutput().
inline void tryPushingOutput(CookState& s, CookEnv const& env, AutoEnv const& a) {
    if (s.outputCooldown > 0) {
        --s.outputCooldown;
        return;
    }
    if (!s.output.empty() && insertAmount(a, s.output, true) > 0) {
        int moved = insertAmount(a, s.output, false);
        s.output  = s.output.withCount(s.output.count - moved);
        if (a.pushXp) a.pushXp(s);
    } else {
        s.outputCooldown = kNoInventorySpaceCooldown;
    }
    // A fuel slot item that doesn't burn (the bucket a lava bucket leaves) goes to the storage.
    if (!s.fuel.empty() && (!env.burnTicks || env.burnTicks(s.fuel.withCount(1)) <= 0) && insertAmount(a, s.fuel, true) > 0) {
        int moved = insertAmount(a, s.fuel, false);
        s.fuel    = s.fuel.withCount(s.fuel.count - moved);
    }
}

/// SC tryPullingGetUnsucessful(): fills `slot` up to a full stack of a valid storage item.
/// Returns true when nothing could be pulled.
inline bool tryPulling(core::ItemStackData& slot, std::function<bool(core::ItemStackData const&)> const& valid, AutoEnv const& a) {
    core::ItemStackData toExtract;
    if (slot.empty()) {
        for (int i = 0; i < a.inventory.size() && toExtract.empty(); ++i) {
            auto const& st = a.inventory.at(i);
            if (!st.empty() && valid(st)) toExtract = st.withCount(std::max(st.maxStack, 1));
        }
        if (toExtract.empty()) return true;
    } else if (slot.count >= std::max(slot.maxStack, 1) || !valid(slot)) {
        return true;
    } else {
        toExtract = slot.withCount(std::max(slot.maxStack, 1) - slot.count);
    }
    auto same = [&](core::ItemStackData const& x) { return x.sameItem(toExtract); };
    if (a.inventory.extractMatching(same, toExtract.count, a.access, true).empty()) return true;
    auto got = a.inventory.extractMatching(same, toExtract.count, a.access, false);
    if (got.empty()) return true;
    int have = slot.empty() ? 0 : slot.count;
    slot     = got.withCount(got.count + have);
    return false;
}

} // namespace detail

/// One AutoCookingUpgradeWrapper.tick(): push output, pull fuel, pull input, cook.
inline void autoTick(CookState& s, CookEnv const& env, AutoEnv const& a) {
    detail::tryPushingOutput(s, env, a);
    if (s.fuelCooldown > 0) {
        --s.fuelCooldown;
    } else if (detail::tryPulling(s.fuel, [&](core::ItemStackData const& x) { return isFuel(env, x); }, a)) {
        s.fuelCooldown = kNoInventorySpaceCooldown;
    }
    if (s.inputCooldown > 0) {
        --s.inputCooldown;
    } else if (detail::tryPulling(s.input, [&](core::ItemStackData const& x) { return isInput(env, x); }, a)) {
        s.inputCooldown = kNoInventorySpaceCooldown;
    }
    if (!tick(s, env) && s.outputCooldown <= 0 && s.fuelCooldown <= 0 && s.inputCooldown <= 0) {
        s.cooldown = kNothingToDoCooldown - 1; // SC isInCooldown: skips the next 9 ticks
    }
}

/// An Auto cooking upgrade for `ticks` game ticks.
inline void runAuto(CookState& s, CookEnv const& env, AutoEnv const& a, int ticks) {
    for (int i = 0; i < ticks; ++i) {
        if (s.cooldown > 0) --s.cooldown;
        else autoTick(s, env, a);
        ++s.clock;
    }
}

} // namespace abp::upgrade::cooking
