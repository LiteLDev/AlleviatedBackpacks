#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "api/abp/Fluid.h"
#include "core/Fluid.h"

namespace abp::core {

/// The fluids and swap containers known to the capability API (the pure part of
/// `api::CapabilityService`; the service owns one instance and lets mods extend it).
class FluidRegistry {
public:
    /// A registry holding AlleviatedBackpacks' built-in fluids (water, lava, milk, powder snow,
    /// honey, the three potion kinds, abp:experience) and the vanilla containers (buckets,
    /// bottles, bottle o' enchanting).
    [[nodiscard]] static FluidRegistry withBuiltins();

    // ---- fluids -----------------------------------------------------------------------------
    /// Adds a fluid. False if the id is empty / has no namespace, or is already registered.
    bool addFluid(api::FluidInfo info);
    bool removeFluid(std::string_view id);
    [[nodiscard]] api::FluidInfo const*              fluid(std::string_view id) const;
    [[nodiscard]] std::vector<api::FluidInfo> const& fluids() const { return mFluids; }
    [[nodiscard]] bool                               hasTag(std::string_view fluidId, std::string_view tag) const;

    // ---- swap containers --------------------------------------------------------------------
    /// Adds a container. False if incomplete (items, fluid, amount > 0, aux >= -1) or if the
    /// same filled item + aux is already registered.
    bool addContainer(api::FluidContainer container);
    bool removeContainer(std::string_view filledItem, int filledAux);
    [[nodiscard]] std::vector<api::FluidContainer> const& containers() const { return mContainers; }

    struct Contents {
        api::FluidStack fluid;     ///< what one filled item holds
        std::string     emptyItem; ///< what is left after emptying it
        [[nodiscard]] bool operator==(Contents const&) const = default;
    };
    /// What the filled container `itemId` / `aux` holds (exact aux entries first, then "any aux").
    [[nodiscard]] std::optional<Contents> contentsOf(std::string_view itemId, int aux) const;

    struct Filled {
        std::string item;
        int         aux    = 0;
        int         amount = 0; ///< mB the container takes
        [[nodiscard]] bool operator==(Filled const&) const = default;
    };
    /// Filling the empty container `emptyItem` with the fluid of `fluid` (its amount is ignored).
    [[nodiscard]] std::optional<Filled> fill(std::string_view emptyItem, api::FluidStack const& fluid) const;

    [[nodiscard]] bool isEmptyContainer(std::string_view itemId) const;

private:
    std::vector<api::FluidInfo>      mFluids;
    std::vector<api::FluidContainer> mContainers;
};

/// The integer stored under `key` in a canonical nbt text (`{"key":7}`), if that is all it holds.
[[nodiscard]] std::optional<int> singleIntNbt(std::string_view nbt, std::string_view key);

/// Phase-1 fluid ids (`core/Fluid.h`: "water", "potion:7", "experience", ...) are what the Tank
/// upgrade persists. These map them to and from API stacks ("minecraft:water",
/// {"minecraft:potion", nbt {"potion":7}}, "abp:experience", ...). API fluids without a phase-1
/// id keep their id (plus nbt text, appended) as the stored string, so the mapping round-trips.
[[nodiscard]] api::FluidStack   toApi(fluid::FluidStack const& legacy);
[[nodiscard]] std::string       legacyIdOf(api::FluidStack const& stack);
[[nodiscard]] fluid::FluidStack fromApi(api::FluidStack const& stack);

} // namespace abp::core
