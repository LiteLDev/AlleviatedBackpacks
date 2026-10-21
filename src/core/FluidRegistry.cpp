#include "core/FluidRegistry.h"

#include <algorithm>
#include <array>
#include <climits>

#include "nlohmann/json.hpp"

namespace abp::core {

namespace fl = fluid;
namespace ids = api::fluids;

namespace {
constexpr std::string_view kBucket = "minecraft:bucket";
constexpr std::string_view kBottle = "minecraft:glass_bottle";

api::FluidInfo info(std::string_view id, std::string_view key, std::string_view name, std::uint32_t color, std::vector<std::string> tags, std::string_view block = {}) {
    return api::FluidInfo{std::string(id), std::string(key), std::string(name), color, std::move(tags), std::string(block)};
}

api::FluidContainer container(std::string_view filled, int aux, std::string_view empty, std::string_view fluid, int amount, std::string_view auxKey = {}) {
    return api::FluidContainer{std::string(filled), aux, std::string(empty), api::FluidStack{std::string(fluid), amount}, std::string(auxKey)};
}

/// Phase-1 id <-> API id for fluids without nbt.
struct Simple {
    std::string_view legacy;
    std::string_view api;
};
constexpr std::array<Simple, 6> kSimple{{
    {fl::kWater, ids::Water},
    {fl::kLava, ids::Lava},
    {fl::kMilk, ids::Milk},
    {fl::kPowderSnow, ids::PowderSnow},
    {fl::kHoney, ids::Honey},
    {fl::kExperience, ids::Experience},
}};

/// Phase-1 potion family <-> API potion fluid id.
constexpr std::array<Simple, 3> kPotions{{
    {"potion", ids::Potion},
    {"splash_potion", ids::SplashPotion},
    {"lingering_potion", ids::LingeringPotion},
}};
} // namespace

// ---- registry -------------------------------------------------------------------------------------

FluidRegistry FluidRegistry::withBuiltins() {
    FluidRegistry r;
    r.addFluid(info(ids::Water, "abp.api.fluid.water", "Water", 0xFF3F76E4, {"c:water"}, "minecraft:water"));
    r.addFluid(info(ids::Lava, "abp.api.fluid.lava", "Lava", 0xFFFF6A00, {"c:lava"}, "minecraft:lava"));
    r.addFluid(info(ids::Milk, "abp.api.fluid.milk", "Milk", 0xFFF7F7F7, {"c:milk"}));
    r.addFluid(info(ids::PowderSnow, "abp.api.fluid.powder_snow", "Powder Snow", 0xFFE3F0F7, {}, "minecraft:powder_snow"));
    r.addFluid(info(ids::Honey, "abp.api.fluid.honey", "Honey", 0xFFF9A825, {"c:honey"}));
    r.addFluid(info(ids::Potion, "abp.api.fluid.potion", "Potion", 0xFFB82BB8, {"c:potion"}));
    r.addFluid(info(ids::SplashPotion, "abp.api.fluid.splash_potion", "Splash Potion", 0xFFB82BB8, {"c:potion"}));
    r.addFluid(info(ids::LingeringPotion, "abp.api.fluid.lingering_potion", "Lingering Potion", 0xFFB82BB8, {"c:potion"}));
    r.addFluid(info(ids::Experience, "abp.api.fluid.experience", "Liquid Experience", 0xFF9CFF3F, {std::string(api::fluid_tags::Experience)}));

    r.addContainer(container("minecraft:water_bucket", 0, kBucket, ids::Water, api::kBucketVolume));
    r.addContainer(container("minecraft:lava_bucket", 0, kBucket, ids::Lava, api::kBucketVolume));
    r.addContainer(container("minecraft:milk_bucket", 0, kBucket, ids::Milk, api::kBucketVolume));
    r.addContainer(container("minecraft:powder_snow_bucket", 0, kBucket, ids::PowderSnow, api::kBucketVolume));
    // A potion with aux 0 is a water bottle; every other aux is a potion of that type.
    r.addContainer(container("minecraft:potion", 0, kBottle, ids::Water, api::kBottleVolume));
    r.addContainer(container("minecraft:honey_bottle", 0, kBottle, ids::Honey, api::kBottleVolume));
    r.addContainer(container("minecraft:experience_bottle", 0, kBottle, ids::Experience, api::kXpBottleVolume));
    r.addContainer(container("minecraft:potion", -1, kBottle, ids::Potion, api::kBottleVolume, api::kPotionNbtKey));
    r.addContainer(container("minecraft:splash_potion", -1, kBottle, ids::SplashPotion, api::kBottleVolume, api::kPotionNbtKey));
    r.addContainer(container("minecraft:lingering_potion", -1, kBottle, ids::LingeringPotion, api::kBottleVolume, api::kPotionNbtKey));
    return r;
}

bool FluidRegistry::addFluid(api::FluidInfo info) {
    if (info.id.empty() || info.id.find(':') == std::string::npos || fluid(info.id)) return false;
    mFluids.push_back(std::move(info));
    return true;
}

bool FluidRegistry::removeFluid(std::string_view id) {
    return std::erase_if(mFluids, [&](api::FluidInfo const& f) { return f.id == id; }) > 0;
}

api::FluidInfo const* FluidRegistry::fluid(std::string_view id) const {
    auto it = std::find_if(mFluids.begin(), mFluids.end(), [&](api::FluidInfo const& f) { return f.id == id; });
    return it == mFluids.end() ? nullptr : &*it;
}

bool FluidRegistry::hasTag(std::string_view fluidId, std::string_view tag) const {
    auto const* f = fluid(fluidId);
    return f && f->hasTag(tag);
}

bool FluidRegistry::addContainer(api::FluidContainer c) {
    if (c.filledItem.empty() || c.emptyItem.empty() || c.fluid.empty() || c.filledAux < -1) return false;
    if (c.filledItem == c.emptyItem && c.filledAux == 0) return false;
    for (auto const& e : mContainers) {
        if (e.filledItem == c.filledItem && e.filledAux == c.filledAux) return false;
    }
    mContainers.push_back(std::move(c));
    return true;
}

bool FluidRegistry::removeContainer(std::string_view filledItem, int filledAux) {
    return std::erase_if(mContainers, [&](api::FluidContainer const& c) {
               return c.filledItem == filledItem && c.filledAux == filledAux;
           })
         > 0;
}

std::optional<FluidRegistry::Contents> FluidRegistry::contentsOf(std::string_view itemId, int aux) const {
    if (itemId.empty() || aux < 0) return std::nullopt;
    for (auto const& c : mContainers) {
        if (c.filledAux == aux && c.filledItem == itemId) return Contents{c.fluid, c.emptyItem};
    }
    for (auto const& c : mContainers) {
        if (c.filledAux != -1 || c.filledItem != itemId) continue;
        auto f = c.fluid;
        if (!c.auxNbtKey.empty()) f.nbt = api::intNbt(c.auxNbtKey, aux);
        return Contents{std::move(f), c.emptyItem};
    }
    return std::nullopt;
}

std::optional<FluidRegistry::Filled> FluidRegistry::fill(std::string_view emptyItem, api::FluidStack const& fluid) const {
    if (emptyItem.empty() || fluid.id.empty()) return std::nullopt;
    for (auto const& c : mContainers) {
        if (c.filledAux >= 0 && c.emptyItem == emptyItem && c.fluid.id == fluid.id && c.fluid.nbt == fluid.nbt) {
            return Filled{c.filledItem, c.filledAux, c.fluid.amount};
        }
    }
    for (auto const& c : mContainers) {
        if (c.filledAux != -1 || c.emptyItem != emptyItem || c.fluid.id != fluid.id) continue;
        if (c.auxNbtKey.empty()) {
            if (c.fluid.nbt == fluid.nbt) return Filled{c.filledItem, 0, c.fluid.amount};
            continue;
        }
        auto aux = singleIntNbt(fluid.nbt, c.auxNbtKey);
        if (!aux || *aux < 0 || *aux > 32767) continue;
        // An exact entry for that aux means the item holds another fluid there (potion aux 0 = water).
        bool taken = std::any_of(mContainers.begin(), mContainers.end(), [&](api::FluidContainer const& e) {
            return e.filledAux == *aux && e.filledItem == c.filledItem;
        });
        if (!taken) return Filled{c.filledItem, *aux, c.fluid.amount};
    }
    return std::nullopt;
}

bool FluidRegistry::isEmptyContainer(std::string_view itemId) const {
    return std::any_of(mContainers.begin(), mContainers.end(), [&](api::FluidContainer const& c) { return c.emptyItem == itemId; });
}

// ---- nbt / phase-1 ids ----------------------------------------------------------------------------

std::optional<int> singleIntNbt(std::string_view nbt, std::string_view key) {
    if (nbt.empty()) return std::nullopt;
    auto j = nlohmann::json::parse(nbt, nullptr, false);
    if (!j.is_object() || j.size() != 1) return std::nullopt;
    auto it = j.find(std::string(key));
    if (it == j.end() || !it->is_number_integer()) return std::nullopt;
    auto v = it->get<long long>();
    if (v < INT_MIN || v > INT_MAX) return std::nullopt;
    return static_cast<int>(v);
}

api::FluidStack toApi(fl::FluidStack const& legacy) {
    if (legacy.empty()) return {};
    for (auto const& s : kSimple) {
        if (s.legacy == legacy.id) return {std::string(s.api), legacy.amount};
    }
    auto colon = legacy.id.find(':');
    if (colon != std::string::npos && legacy.id.find('{') == std::string::npos) {
        auto fam = std::string_view(legacy.id).substr(0, colon);
        for (auto const& p : kPotions) {
            if (p.legacy == fam && fl::isKnown(legacy.id)) {
                return {std::string(p.api), legacy.amount, api::intNbt(api::kPotionNbtKey, fl::potionAux(legacy.id))};
            }
        }
    }
    // Not a phase-1 id: "<api id>[<nbt>]".
    auto brace = legacy.id.find('{');
    if (brace == std::string::npos) return {legacy.id, legacy.amount};
    return {legacy.id.substr(0, brace), legacy.amount, legacy.id.substr(brace)};
}

std::string legacyIdOf(api::FluidStack const& stack) {
    if (stack.id.empty()) return {};
    if (stack.nbt.empty()) {
        for (auto const& s : kSimple) {
            if (s.api == stack.id) return std::string(s.legacy);
        }
    }
    for (auto const& p : kPotions) {
        if (p.api != stack.id) continue;
        auto aux = singleIntNbt(stack.nbt, api::kPotionNbtKey);
        if (!aux) break;
        auto id = std::string(p.legacy) + ":" + std::to_string(*aux);
        if (fl::isKnown(id)) return id;
        break;
    }
    return stack.id + stack.nbt;
}

fl::FluidStack fromApi(api::FluidStack const& stack) {
    if (stack.empty()) return {};
    return {legacyIdOf(stack), stack.amount};
}

} // namespace abp::core
