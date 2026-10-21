#pragma once

// Game-independent cooking logic of the Smelting / Smoking / Blasting upgrades (and their Auto
// variants). Header-only and free of mc/ll includes so that it is unit-tested in tests/.

#include <algorithm>
#include <functional>
#include <optional>

#include "nlohmann/json.hpp"

#include "core/BackpackInventory.h"
#include "core/Filter.h"
#include "core/ItemStackData.h"

namespace abp::upgrade::cooking {

/// The furnace-like state of one cooking upgrade (persisted in the upgrade's settings, so it
/// travels with the upgrade item).
struct CookState {
    core::ItemStackData input;
    core::ItemStackData fuel;
    core::ItemStackData output;
    int                 burn      = 0; ///< ticks of fuel left
    int                 burnTotal = 0; ///< burn ticks of the fuel item being burnt (display)
    int                 cook      = 0; ///< cooking progress of the current input item
    int                 cookTotal = 0; ///< ticks needed per item (display)

    [[nodiscard]] bool burning() const { return burn > 0; }
    [[nodiscard]] int  burnPercent() const { return burnTotal > 0 ? std::clamp(burn * 100 / burnTotal, 0, 100) : 0; }
    [[nodiscard]] int  cookPercent() const { return cookTotal > 0 ? std::clamp(cook * 100 / cookTotal, 0, 100) : 0; }

    [[nodiscard]] nlohmann::json toJson() const {
        return nlohmann::json{
            {"input",     input.toJson() },
            {"fuel",      fuel.toJson()  },
            {"output",    output.toJson()},
            {"burn",      burn           },
            {"burnTotal", burnTotal      },
            {"cook",      cook           },
            {"cookTotal", cookTotal      },
        };
    }
    [[nodiscard]] static CookState fromJson(nlohmann::json const& j) {
        CookState s;
        if (!j.is_object()) return s;
        auto num = [&](char const* key) {
            auto it = j.find(key);
            return it != j.end() && it->is_number_integer() ? std::max(0, it->get<int>()) : 0;
        };
        if (auto it = j.find("input"); it != j.end()) s.input = core::ItemStackData::fromJson(*it);
        if (auto it = j.find("fuel"); it != j.end()) s.fuel = core::ItemStackData::fromJson(*it);
        if (auto it = j.find("output"); it != j.end()) s.output = core::ItemStackData::fromJson(*it);
        s.burn      = num("burn");
        s.burnTotal = num("burnTotal");
        s.cook      = num("cook");
        s.cookTotal = num("cookTotal");
        return s;
    }

    [[nodiscard]] bool operator==(CookState const& o) const {
        return input == o.input && fuel == o.fuel && output == o.output && burn == o.burn && burnTotal == o.burnTotal
            && cook == o.cook && cookTotal == o.cookTotal;
    }
};

/// What the cooking logic needs to know about the game.
struct CookEnv {
    /// The result of cooking one `input` item (nullopt / empty = no recipe).
    std::function<std::optional<core::ItemStackData>(core::ItemStackData const&)> result;
    /// Burn ticks of one `fuel` item (0 = not a fuel).
    std::function<int(core::ItemStackData const&)> burnTicks;
    /// What is left in the fuel slot after burning one item of `fuel` (count already reduced
    /// by one). Default: `fuel` with one less item. A lava bucket leaves a bucket.
    std::function<core::ItemStackData(core::ItemStackData const&)> afterBurn;
    /// Ticks needed to cook one item (furnace 200, smoker / blast furnace 100).
    int cookTime = 200;
    /// Called for every finished item (the produced stack).
    std::function<void(core::ItemStackData const&)> onProduce;
};

/// True if `out` can take `add` completely.
[[nodiscard]] inline bool fits(core::ItemStackData const& out, core::ItemStackData const& add) {
    if (add.empty()) return true;
    if (out.empty()) return add.count <= std::max(add.maxStack, 1);
    return out.sameItem(add) && out.count + add.count <= std::max(out.maxStack, 1);
}

/// The recipe result for the current input, if it can be cooked into the output slot.
[[nodiscard]] inline std::optional<core::ItemStackData> cookable(CookState const& s, CookEnv const& env) {
    if (s.input.empty() || !env.result) return std::nullopt;
    auto r = env.result(s.input.withCount(1));
    if (!r || r->empty() || !fits(s.output, *r)) return std::nullopt;
    return r;
}

/// Burns one item from the fuel slot. Returns false if the fuel slot holds no fuel.
inline bool igniteFuel(CookState& s, CookEnv const& env) {
    if (s.fuel.empty() || !env.burnTicks) return false;
    int ticks = env.burnTicks(s.fuel.withCount(1));
    if (ticks <= 0) return false;
    s.burn = s.burnTotal = ticks;
    if (env.afterBurn) {
        s.fuel = env.afterBurn(s.fuel);
    } else {
        s.fuel = s.fuel.withCount(s.fuel.count - 1);
    }
    return true;
}

/// Advances the state by one game tick, like a vanilla furnace:
/// - the fuel burns down every tick;
/// - when the input can be cooked (recipe and room in the output) and nothing burns, one fuel
///   item is ignited;
/// - while burning and cookable, progress grows; at `cookTime` one input item becomes output;
/// - without a cookable input progress resets, without fuel it slowly falls back.
/// Returns true if anything changed.
inline bool step(CookState& s, CookEnv const& env) {
    CookState before = s;
    if (s.burn > 0) --s.burn;
    auto res = cookable(s, env);
    if (res && s.burn <= 0) igniteFuel(s, env);
    s.cookTotal = std::max(1, env.cookTime);
    if (res && s.burn > 0) {
        if (++s.cook >= s.cookTotal) {
            s.cook = 0;
            if (s.output.empty()) s.output = *res;
            else s.output.count += res->count;
            s.input = s.input.withCount(s.input.count - 1);
            if (env.onProduce) env.onProduce(*res);
        }
    } else if (!res) {
        s.cook = 0;
    } else {
        s.cook = std::max(0, s.cook - 2);
    }
    if (s.burn <= 0) s.burnTotal = 0;
    return !(s == before);
}

/// Filters and storage access of the Auto variants.
struct AutoEnv {
    core::BackpackInventory&   inventory;
    core::Filter const&        inputFilter;
    core::Filter const&        fuelFilter;
    core::ITagProvider const*  tags = nullptr;
    core::AccessContext        access{core::Source::Upgrade};
};

/// Whether the Auto variant would pull `item` from the storage as input.
[[nodiscard]] inline bool isAutoInput(core::ItemStackData const& item, CookEnv const& env, AutoEnv const& a) {
    if (item.empty() || !env.result || !a.inputFilter.passes(item, a.tags)) return false;
    auto r = env.result(item.withCount(1));
    return r && !r->empty();
}

/// Whether the Auto variant would pull `item` from the storage as fuel. With an empty (allow all)
/// fuel filter, items that would be cooked as input are never burnt.
[[nodiscard]] inline bool isAutoFuel(core::ItemStackData const& item, CookEnv const& env, AutoEnv const& a) {
    if (item.empty() || !env.burnTicks || !a.fuelFilter.passes(item, a.tags)) return false;
    if (env.burnTicks(item.withCount(1)) <= 0) return false;
    if (a.fuelFilter.isEmpty() && isAutoInput(item, env, a)) return false;
    return true;
}

/// Auto variants: moves the output slot into the storage. Returns true if anything moved.
inline bool pushOutput(CookState& s, AutoEnv const& a) {
    if (s.output.empty()) return false;
    auto r     = a.inventory.insert(s.output, a.access);
    int  moved = r.inserted + r.voided;
    if (moved <= 0) return false;
    s.output = s.output.withCount(s.output.count - moved);
    return true;
}

/// Auto variants: fills an empty input slot with one stack of the first storage item that passes
/// the input filter and has a recipe. Returns true if anything moved.
inline bool pullInput(CookState& s, CookEnv const& env, AutoEnv const& a) {
    if (!s.input.empty()) return false;
    core::ItemStackData pick;
    for (int i = 0; i < a.inventory.size() && pick.empty(); ++i) {
        auto const& item = a.inventory.at(i);
        if (isAutoInput(item, env, a) && a.inventory.canExtract(i, 1, a.access)) pick = item;
    }
    if (pick.empty()) return false;
    auto got = a.inventory.extractMatching(
        [&](core::ItemStackData const& x) { return x.sameItem(pick); },
        std::max(pick.maxStack, 1),
        a.access
    );
    if (got.empty()) return false;
    s.input = got;
    s.cook  = 0;
    return true;
}

/// True when the next `step()` needs a new fuel item and the fuel slot is empty.
[[nodiscard]] inline bool needsFuel(CookState const& s, CookEnv const& env) {
    return s.fuel.empty() && s.burn <= 1 && cookable(s, env).has_value();
}

/// Auto variants: takes one fuel item from the storage when `needsFuel()`. Fuel is pulled one item
/// at a time so the rest stays visible in the backpack. Returns true if anything moved.
inline bool pullFuel(CookState& s, CookEnv const& env, AutoEnv const& a) {
    if (!needsFuel(s, env)) return false;
    for (int i = 0; i < a.inventory.size(); ++i) {
        if (!isAutoFuel(a.inventory.at(i), env, a)) continue;
        auto got = a.inventory.extract(i, 1, a.access);
        if (got.empty()) continue;
        s.fuel = got;
        return true;
    }
    return false;
}

/// One automatic exchange with the storage: push output, pull input, pull fuel.
inline bool autoExchange(CookState& s, CookEnv const& env, AutoEnv const& a) {
    bool changed = pushOutput(s, a);
    changed |= pullInput(s, env, a);
    changed |= pullFuel(s, env, a);
    return changed;
}

} // namespace abp::upgrade::cooking
