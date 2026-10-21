#pragma once
// AlleviatedBackpacks public API: fluids (millibucket based, NeoForge-style handlers).
//
// Header-only and free of game headers, so any mod (and host-side unit tests) can include it.
// See docs/API.md. ABI rules: interfaces only ever get new virtual functions appended at the end;
// existing signatures never change.

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace abp::api {

inline constexpr int kBucketVolume   = 1000; ///< mB in a bucket
inline constexpr int kBottleVolume   = 250;  ///< mB in a (water / potion / honey) bottle
inline constexpr int kXpBottleVolume = 160;  ///< mB of experience in a bottle o' enchanting (8 points)

/// Ids of the fluids AlleviatedBackpacks registers.
namespace fluids {
inline constexpr std::string_view Water           = "minecraft:water";
inline constexpr std::string_view Lava            = "minecraft:lava";
inline constexpr std::string_view Milk            = "minecraft:milk";
inline constexpr std::string_view PowderSnow      = "minecraft:powder_snow";
inline constexpr std::string_view Honey           = "minecraft:honey";
/// Potions: the potion type (the item's aux value) is in the stack's nbt: `{"potion":<aux>}`.
inline constexpr std::string_view Potion          = "minecraft:potion";
inline constexpr std::string_view SplashPotion    = "minecraft:splash_potion";
inline constexpr std::string_view LingeringPotion = "minecraft:lingering_potion";
/// Liquid experience: 20 mB per experience point. Tagged `c:experience`.
inline constexpr std::string_view Experience      = "abp:experience";
} // namespace fluids

/// Fluid tags.
namespace fluid_tags {
/// Any liquid experience (SC/SB use `c:experience`; register your own XP fluid with this tag).
inline constexpr std::string_view Experience = "c:experience";
} // namespace fluid_tags

/// The nbt key holding a potion fluid's potion type (aux value of the potion item).
inline constexpr std::string_view kPotionNbtKey = "potion";

/// Canonical nbt text with a single integer entry: `{"key":value}`.
[[nodiscard]] inline std::string intNbt(std::string_view key, int value) {
    std::string out = "{\"";
    out += key;
    out += "\":";
    out += std::to_string(value);
    out += '}';
    return out;
}

/// An amount of one fluid.
///
/// `nbt` carries extra identity (e.g. the potion type) as compact JSON object text with sorted
/// keys and no spaces, `""` for none. Two stacks hold the same fluid when both `id` and `nbt` are
/// equal (NeoForge's "same fluid, same components"), so build it canonically (e.g. `intNbt`).
struct FluidStack {
    std::string id;
    int         amount = 0;
    std::string nbt;

    FluidStack() = default;
    FluidStack(std::string id, int amount, std::string nbt = {}) : id(std::move(id)), amount(amount), nbt(std::move(nbt)) {}

    [[nodiscard]] bool empty() const { return id.empty() || amount <= 0; }
    /// Same fluid and nbt (amounts ignored). Empty stacks match nothing.
    [[nodiscard]] bool isSameFluid(FluidStack const& other) const {
        return !empty() && !other.empty() && id == other.id && nbt == other.nbt;
    }
    [[nodiscard]] FluidStack copyWithAmount(int n) const { return n <= 0 ? FluidStack{} : FluidStack{id, n, nbt}; }
    void grow(int n) { amount += n; }
    void shrink(int n) {
        amount -= n;
        if (amount <= 0) *this = {};
    }
    [[nodiscard]] bool operator==(FluidStack const&) const = default;
};

/// Merges `add` into `into` up to `capacity` total. Returns the amount merged (0 if the fluids
/// differ). An empty `into` takes `add`'s fluid.
inline int mergeFluid(FluidStack& into, FluidStack const& add, int capacity) {
    if (add.empty()) return 0;
    if (!into.empty() && !into.isSameFluid(add)) return 0;
    int have = into.empty() ? 0 : into.amount;
    int n    = std::min(add.amount, std::max(0, capacity - have));
    if (n <= 0) return 0;
    if (into.empty()) into = add.copyWithAmount(n);
    else into.amount += n;
    return n;
}

/// A registered fluid (see `CapabilityService::registerFluid`).
struct FluidInfo {
    std::string              id;           ///< "namespace:path"
    std::string              nameKey;      ///< translation key of the display name
    std::string              nameFallback; ///< English name when the key is unknown
    std::uint32_t            color = 0xFFFFFFFF; ///< ARGB tint for gauges
    std::vector<std::string> tags;         ///< e.g. "c:experience"
    std::string              sourceBlock;  ///< block placed / picked up as a 1000 mB source ("" = none)

    [[nodiscard]] bool hasTag(std::string_view tag) const {
        return std::find(tags.begin(), tags.end(), tag) != tags.end();
    }
};

/// A swap container: one `filledItem` holds `fluid`; emptying it leaves `emptyItem` (and filling
/// `emptyItem` with `fluid.amount` of that fluid gives `filledItem`). E.g. water bucket <-> bucket.
struct FluidContainer {
    std::string filledItem;
    int         filledAux = 0; ///< aux (data value) of the filled item; -1: any aux, see `auxNbtKey`
    std::string emptyItem;
    FluidStack  fluid;         ///< contents of one filled item
    /// With `filledAux == -1`: the filled item's aux goes into the fluid's nbt as `{"<key>":aux}`
    /// (potions: key "potion"). Exact-aux entries win over these.
    std::string auxNbtKey;
};

/// Something holding fluids in one or more tanks. Mirrors NeoForge's `IFluidHandler`; `drainAny`
/// is NeoForge's `drain(int maxDrain, ...)`.
class IFluidHandler {
public:
    virtual ~IFluidHandler() = default;

    [[nodiscard]] virtual int        tanks() const                                     = 0;
    [[nodiscard]] virtual FluidStack fluidInTank(int tank) const                       = 0;
    [[nodiscard]] virtual int        tankCapacity(int tank) const                      = 0;
    [[nodiscard]] virtual bool       isFluidValid(int tank, FluidStack const& stack) const = 0;
    /// Fills up to `resource.amount`. Returns the amount accepted.
    virtual int fill(FluidStack const& resource, bool simulate) = 0;
    /// Drains up to `resource.amount` of exactly that fluid (id + nbt).
    virtual FluidStack drain(FluidStack const& resource, bool simulate) = 0;
    /// Drains up to `maxDrain` of whatever fluid comes first.
    virtual FluidStack drainAny(int maxDrain, bool simulate) = 0;
};

/// A single tank (like NeoForge's `FluidTank`). Override `onContentsChanged()` to persist.
class FluidTank : public IFluidHandler {
public:
    using Validator = std::function<bool(FluidStack const&)>;

    explicit FluidTank(int capacity, Validator validator = {})
    : mCapacity(std::max(0, capacity)),
      mValidator(std::move(validator)) {}

    [[nodiscard]] FluidStack const& fluid() const { return mFluid; }
    [[nodiscard]] int               amount() const { return mFluid.empty() ? 0 : mFluid.amount; }
    [[nodiscard]] int               capacity() const { return mCapacity; }
    [[nodiscard]] int               space() const { return std::max(0, mCapacity - amount()); }
    void                            setCapacity(int capacity) { mCapacity = std::max(0, capacity); }
    /// Sets the contents directly (no validation, no `onContentsChanged`), e.g. when loading.
    void setFluid(FluidStack fluid) { mFluid = fluid.empty() ? FluidStack{} : std::move(fluid); }
    [[nodiscard]] bool isValid(FluidStack const& stack) const { return !stack.empty() && (!mValidator || mValidator(stack)); }

    [[nodiscard]] int        tanks() const override { return 1; }
    [[nodiscard]] FluidStack fluidInTank(int tank) const override { return tank == 0 ? mFluid : FluidStack{}; }
    [[nodiscard]] int        tankCapacity(int tank) const override { return tank == 0 ? mCapacity : 0; }
    [[nodiscard]] bool       isFluidValid(int tank, FluidStack const& stack) const override { return tank == 0 && isValid(stack); }

    int fill(FluidStack const& resource, bool simulate) override {
        if (!isValid(resource)) return 0;
        if (!mFluid.empty() && !mFluid.isSameFluid(resource)) return 0;
        int n = std::min(resource.amount, space());
        if (n <= 0) return 0;
        if (!simulate) {
            if (mFluid.empty()) mFluid = resource.copyWithAmount(n);
            else mFluid.amount += n;
            onContentsChanged();
        }
        return n;
    }

    FluidStack drain(FluidStack const& resource, bool simulate) override {
        if (!mFluid.isSameFluid(resource)) return {};
        return drainAny(resource.amount, simulate);
    }

    FluidStack drainAny(int maxDrain, bool simulate) override {
        if (mFluid.empty() || maxDrain <= 0) return {};
        int  n   = std::min(maxDrain, mFluid.amount);
        auto out = mFluid.copyWithAmount(n);
        if (!simulate) {
            mFluid.shrink(n);
            onContentsChanged();
        }
        return out;
    }

protected:
    /// Called after every non-simulated change of the contents.
    virtual void onContentsChanged() {}

    FluidStack mFluid;
    int        mCapacity;
    Validator  mValidator;
};

/// Several handlers seen as one, tanks concatenated in order. Uses SB's backpack rules:
/// `fill` pours into each handler in turn until everything is placed; `drain(resource)` collects
/// that fluid from every handler; `drainAny` drains from the first handler that gives anything.
class CombinedFluidHandler : public IFluidHandler {
public:
    explicit CombinedFluidHandler(std::vector<std::shared_ptr<IFluidHandler>> parts) : mParts(std::move(parts)) {
        std::erase(mParts, nullptr);
    }

    [[nodiscard]] std::vector<std::shared_ptr<IFluidHandler>> const& parts() const { return mParts; }

    [[nodiscard]] int tanks() const override {
        int n = 0;
        for (auto const& p : mParts) n += p->tanks();
        return n;
    }
    [[nodiscard]] FluidStack fluidInTank(int tank) const override {
        auto [p, t] = locate(tank);
        return p ? p->fluidInTank(t) : FluidStack{};
    }
    [[nodiscard]] int tankCapacity(int tank) const override {
        auto [p, t] = locate(tank);
        return p ? p->tankCapacity(t) : 0;
    }
    [[nodiscard]] bool isFluidValid(int tank, FluidStack const& stack) const override {
        auto [p, t] = locate(tank);
        return p && p->isFluidValid(t, stack);
    }

    int fill(FluidStack const& resource, bool simulate) override {
        if (resource.empty()) return 0;
        int filled = 0;
        for (auto const& p : mParts) {
            filled += p->fill(resource.copyWithAmount(resource.amount - filled), simulate);
            if (filled >= resource.amount) break;
        }
        return filled;
    }

    FluidStack drain(FluidStack const& resource, bool simulate) override {
        if (resource.empty()) return {};
        int drained = 0;
        for (auto const& p : mParts) {
            drained += p->drain(resource.copyWithAmount(resource.amount - drained), simulate).amount;
            if (drained >= resource.amount) break;
        }
        return resource.copyWithAmount(drained);
    }

    FluidStack drainAny(int maxDrain, bool simulate) override {
        for (auto const& p : mParts) {
            auto got = p->drainAny(maxDrain, simulate);
            if (!got.empty()) return got;
        }
        return {};
    }

private:
    [[nodiscard]] std::pair<IFluidHandler*, int> locate(int tank) const {
        if (tank < 0) return {nullptr, 0};
        for (auto const& p : mParts) {
            int n = p->tanks();
            if (tank < n) return {p.get(), tank};
            tank -= n;
        }
        return {nullptr, 0};
    }

    std::vector<std::shared_ptr<IFluidHandler>> mParts;
};

} // namespace abp::api
