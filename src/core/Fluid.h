#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "nlohmann/json.hpp"

namespace abp::core::fluid {

/// Bedrock has no fluid API, so the Tank / Pump / XP Pump upgrades model fluids as typed amounts
/// in millibuckets (mB). A fluid id is one of:
///
/// | id                       | containers                                        |
/// | ------------------------ | ------------------------------------------------- |
/// | `water`                  | water bucket (1000), water bottle (potion aux 0, 250) |
/// | `lava`                   | lava bucket (1000)                                |
/// | `milk`                   | milk bucket (1000)                                |
/// | `powder_snow`            | powder snow bucket (1000)                         |
/// | `honey`                  | honey bottle (250)                                |
/// | `potion:<aux>`           | potion with that aux (250), aux != 0              |
/// | `splash_potion:<aux>`    | splash potion (250)                               |
/// | `lingering_potion:<aux>` | lingering potion (250)                            |
/// | `experience`             | none: only the XP Pump moves it (20 mB per XP point) |
inline constexpr int kBucket       = 1000;
inline constexpr int kBottle       = 250;
inline constexpr int kXpPerPoint   = 20; ///< mB of `experience` per experience point
inline constexpr int kPerSlotRow   = 4000; ///< default tank capacity per 9 storage slots

inline constexpr std::string_view kWater      = "water";
inline constexpr std::string_view kLava       = "lava";
inline constexpr std::string_view kMilk       = "milk";
inline constexpr std::string_view kPowderSnow = "powder_snow";
inline constexpr std::string_view kHoney      = "honey";
inline constexpr std::string_view kExperience = "experience";

/// An amount of one fluid. Empty when `id` is empty or `amount <= 0`.
struct FluidStack {
    std::string id;
    int         amount = 0;

    [[nodiscard]] bool empty() const { return id.empty() || amount <= 0; }
    [[nodiscard]] bool operator==(FluidStack const&) const = default;
};

/// A filled container as an item: id + aux (potions).
struct ContainerItem {
    std::string id;
    int         aux = 0;
    [[nodiscard]] bool operator==(ContainerItem const&) const = default;
};

/// What a filled container holds and what is left after emptying it.
struct Contents {
    FluidStack  fluid;
    std::string emptyId; ///< "minecraft:bucket" / "minecraft:glass_bottle"
};

/// The fluid inside a filled container item (nullopt if `itemId`/`aux` is not one).
[[nodiscard]] std::optional<Contents> contentsOf(std::string_view itemId, int aux);

/// Filling an empty container (`minecraft:bucket`, `minecraft:glass_bottle`) with `fluid`: the
/// filled item and the amount it takes. nullopt if that container can't hold that fluid.
struct Filled {
    ContainerItem item;
    int           amount = 0;
};
[[nodiscard]] std::optional<Filled> fill(std::string_view emptyId, std::string_view fluid);

/// True for items that can be filled with at least one fluid.
[[nodiscard]] bool isEmptyContainer(std::string_view itemId);

/// The "display" container of a fluid (bucket if one exists, else a bottle), e.g. for icons.
/// nullopt for fluids without a container (experience).
[[nodiscard]] std::optional<ContainerItem> displayItem(std::string_view fluid);

/// Known fluid id (see the table above)?
[[nodiscard]] bool isKnown(std::string_view fluid);

/// Base fluid of a potion fluid ("potion", "splash_potion", "lingering_potion"), or the id itself.
[[nodiscard]] std::string_view family(std::string_view fluid);
/// The aux value of a potion fluid ("potion:7" -> 7), 0 for other fluids.
[[nodiscard]] int potionAux(std::string_view fluid);

/// Tank capacity: `perRow` mB per 9 storage slots (at least one row), times the stack multiplier
/// of the backpack (stack upgrades). Clamped to 1'000'000'000.
[[nodiscard]] int tankCapacity(int storageSlots, int stackMultiplier, int perRow = kPerSlotRow);

/// One fluid tank: at most one fluid kind, `amount <= capacity` for fills (an over-full tank, e.g.
/// after a capacity drop, keeps its contents but accepts nothing).
class Tank {
public:
    Tank() = default;
    Tank(FluidStack contents, int capacity) : mContents(std::move(contents)), mCapacity(capacity) { normalize(); }

    [[nodiscard]] FluidStack const& contents() const { return mContents; }
    [[nodiscard]] std::string const& fluid() const { return mContents.id; }
    [[nodiscard]] int               amount() const { return mContents.amount; }
    [[nodiscard]] int               capacity() const { return mCapacity; }
    [[nodiscard]] bool              empty() const { return mContents.empty(); }
    [[nodiscard]] int               space() const { return mCapacity > amount() ? mCapacity - amount() : 0; }
    void                            setCapacity(int capacity) { mCapacity = capacity < 0 ? 0 : capacity; }

    /// Whether `fluid` can go in (same fluid or empty tank).
    [[nodiscard]] bool accepts(std::string_view fluid) const { return !fluid.empty() && (empty() || mContents.id == fluid); }

    /// Adds up to `stack.amount`. `exact`: all or nothing. Returns the amount added.
    int fill(FluidStack const& stack, bool simulate = false, bool exact = false);
    /// Removes up to `max` mB of `fluid` (empty `fluid` = whatever is inside). `exact`: all or
    /// nothing. Returns what was removed.
    FluidStack drain(std::string_view fluid, int max, bool simulate = false, bool exact = false);

    /// {"fluid": id, "amount": n} (capacity is derived, not stored).
    [[nodiscard]] nlohmann::json toJson() const;
    /// Reads `{"fluid","amount"}` from `j` (missing/invalid -> empty).
    [[nodiscard]] static Tank fromJson(nlohmann::json const& j, int capacity);

private:
    void normalize() {
        if (mContents.empty()) mContents = {};
        if (mCapacity < 0) mCapacity = 0;
    }

    FluidStack mContents;
    int        mCapacity = 0;
};

} // namespace abp::core::fluid
