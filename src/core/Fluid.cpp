#include "core/Fluid.h"

#include <algorithm>
#include <array>
#include <charconv>

namespace abp::core::fluid {

namespace {
constexpr std::string_view kBucketId = "minecraft:bucket";
constexpr std::string_view kBottleId = "minecraft:glass_bottle";

struct BucketEntry {
    std::string_view item;
    std::string_view fluid;
};
constexpr std::array<BucketEntry, 4> kBuckets{{
    {"minecraft:water_bucket", kWater},
    {"minecraft:lava_bucket", kLava},
    {"minecraft:milk_bucket", kMilk},
    {"minecraft:powder_snow_bucket", kPowderSnow},
}};

struct PotionEntry {
    std::string_view item;   ///< "minecraft:potion"
    std::string_view family; ///< "potion"
};
constexpr std::array<PotionEntry, 3> kPotions{{
    {"minecraft:potion", "potion"},
    {"minecraft:splash_potion", "splash_potion"},
    {"minecraft:lingering_potion", "lingering_potion"},
}};

std::optional<int> parseAux(std::string_view text) {
    int  v   = 0;
    auto res = std::from_chars(text.data(), text.data() + text.size(), v);
    if (res.ec != std::errc{} || res.ptr != text.data() + text.size() || v < 0 || v > 32767) return std::nullopt;
    return v;
}
} // namespace

std::string_view family(std::string_view fluid) {
    auto colon = fluid.find(':');
    return colon == std::string_view::npos ? fluid : fluid.substr(0, colon);
}

int potionAux(std::string_view fluid) {
    auto colon = fluid.find(':');
    if (colon == std::string_view::npos) return 0;
    return parseAux(fluid.substr(colon + 1)).value_or(0);
}

bool isKnown(std::string_view fluid) {
    if (fluid == kExperience || fluid == kHoney) return true;
    for (auto const& b : kBuckets)
        if (b.fluid == fluid) return true;
    auto colon = fluid.find(':');
    if (colon == std::string_view::npos) return false;
    auto fam = fluid.substr(0, colon);
    auto aux = parseAux(fluid.substr(colon + 1));
    if (!aux) return false;
    for (auto const& p : kPotions) {
        if (p.family != fam) continue;
        // A plain water bottle is the fluid "water", never "potion:0".
        return !(fam == "potion" && *aux == 0);
    }
    return false;
}

std::optional<Contents> contentsOf(std::string_view itemId, int aux) {
    for (auto const& b : kBuckets) {
        if (b.item == itemId) return Contents{{std::string(b.fluid), kBucket}, std::string(kBucketId)};
    }
    if (itemId == "minecraft:honey_bottle") return Contents{{std::string(kHoney), kBottle}, std::string(kBottleId)};
    for (auto const& p : kPotions) {
        if (p.item != itemId || aux < 0) continue;
        if (p.family == "potion" && aux == 0) return Contents{{std::string(kWater), kBottle}, std::string(kBottleId)};
        return Contents{{std::string(p.family) + ":" + std::to_string(aux), kBottle}, std::string(kBottleId)};
    }
    return std::nullopt;
}

std::optional<Filled> fill(std::string_view emptyId, std::string_view fluid) {
    if (emptyId == kBucketId) {
        for (auto const& b : kBuckets) {
            if (b.fluid == fluid) return Filled{{std::string(b.item), 0}, kBucket};
        }
        return std::nullopt;
    }
    if (emptyId == kBottleId) {
        if (fluid == kWater) return Filled{{"minecraft:potion", 0}, kBottle};
        if (fluid == kHoney) return Filled{{"minecraft:honey_bottle", 0}, kBottle};
        if (!isKnown(fluid)) return std::nullopt;
        auto fam = family(fluid);
        for (auto const& p : kPotions) {
            if (p.family == fam) return Filled{{std::string(p.item), potionAux(fluid)}, kBottle};
        }
    }
    return std::nullopt;
}

bool isEmptyContainer(std::string_view itemId) { return itemId == kBucketId || itemId == kBottleId; }

std::optional<ContainerItem> displayItem(std::string_view fluid) {
    if (auto f = fill(kBucketId, fluid)) return f->item;
    if (auto f = fill(kBottleId, fluid)) return f->item;
    return std::nullopt;
}

int tankCapacity(int storageSlots, int stackMultiplier, int perRow) {
    long long rows = std::max(1, storageSlots / 9);
    long long cap  = rows * std::max(0, perRow) * std::max(1, stackMultiplier);
    return static_cast<int>(std::min<long long>(cap, 1'000'000'000LL));
}

int Tank::fill(FluidStack const& stack, bool simulate, bool exact) {
    if (stack.empty() || !accepts(stack.id)) return 0;
    int n = std::min(stack.amount, space());
    if (n <= 0 || (exact && n < stack.amount)) return 0;
    if (!simulate) {
        mContents.id = stack.id;
        mContents.amount += n;
    }
    return n;
}

FluidStack Tank::drain(std::string_view fluid, int max, bool simulate, bool exact) {
    if (empty() || max <= 0) return {};
    if (!fluid.empty() && fluid != mContents.id) return {};
    int n = std::min(max, amount());
    if (exact && n < max) return {};
    FluidStack out{mContents.id, n};
    if (!simulate) {
        mContents.amount -= n;
        if (mContents.amount <= 0) mContents = {};
    }
    return out;
}

nlohmann::json Tank::toJson() const {
    if (empty()) return nlohmann::json::object();
    return {{"fluid", mContents.id}, {"amount", mContents.amount}};
}

Tank Tank::fromJson(nlohmann::json const& j, int capacity) {
    FluidStack s;
    if (j.is_object()) {
        if (auto it = j.find("fluid"); it != j.end() && it->is_string()) s.id = it->get<std::string>();
        if (auto it = j.find("amount"); it != j.end() && it->is_number_integer()) {
            s.amount = static_cast<int>(std::clamp<long long>(it->get<long long>(), 0, 2'000'000'000LL));
        }
    }
    return Tank(std::move(s), capacity);
}

} // namespace abp::core::fluid
