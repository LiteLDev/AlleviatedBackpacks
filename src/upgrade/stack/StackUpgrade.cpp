// Stack upgrades (Starter, Tier 1-4, Omega) and Stack downgrades (Tier 1-3): per-slot limit
// multipliers. Several stack upgrades in one backpack multiply each other. Removing, swapping
// or installing one is refused when a slot would hold more than the new limit allows.
// Conversion items turn an installed stack upgrade into a higher tier in place.

#include "upgrade/stack/StackUpgrade.h"

#include <algorithm>
#include <climits>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <unordered_map>

#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"

#include "api/BackpackCapabilities.h"
#include "core/Capacity.h"

#include "core/Codec.h"
#include "core/StackMultiplier.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "upgrade/UpgradeManager.h"

ABP_REGISTER_CONFIG("stack", abp::upgrade::stack::StackConfig);

namespace abp::upgrade::stack {

namespace {

constexpr TierDef kTiers[] = {
    {"stack_starter",          "Stack Upgrade Starter Tier", &StackConfig::starterTier   },
    {"stack_tier_1",           "Stack Upgrade Tier 1",       &StackConfig::tier1         },
    {"stack_tier_2",           "Stack Upgrade Tier 2",       &StackConfig::tier2         },
    {"stack_tier_3",           "Stack Upgrade Tier 3",       &StackConfig::tier3         },
    {"stack_tier_4",           "Stack Upgrade Tier 4",       &StackConfig::tier4         },
    {"stack_omega",            "Stack Upgrade Omega Tier",   &StackConfig::omegaTier     },
    {"stack_downgrade_tier_1", "Stack Downgrade Tier 1",     &StackConfig::downgradeTier1},
    {"stack_downgrade_tier_2", "Stack Downgrade Tier 2",     &StackConfig::downgradeTier2},
    {"stack_downgrade_tier_3", "Stack Downgrade Tier 3",     &StackConfig::downgradeTier3},
};

constexpr ConversionDef kConversions[] = {
    {"alleviatedbackpacks:stack_conversion_starter_to_tier_1", "stack_starter", "stack_tier_1"},
    {"alleviatedbackpacks:stack_conversion_starter_to_tier_2", "stack_starter", "stack_tier_2"},
    {"alleviatedbackpacks:stack_conversion_starter_to_tier_3", "stack_starter", "stack_tier_3"},
    {"alleviatedbackpacks:stack_conversion_starter_to_tier_4", "stack_starter", "stack_tier_4"},
    {"alleviatedbackpacks:stack_conversion_tier_1_to_tier_2",  "stack_tier_1",  "stack_tier_2"},
    {"alleviatedbackpacks:stack_conversion_tier_1_to_tier_3",  "stack_tier_1",  "stack_tier_3"},
    {"alleviatedbackpacks:stack_conversion_tier_1_to_tier_4",  "stack_tier_1",  "stack_tier_4"},
    {"alleviatedbackpacks:stack_conversion_tier_2_to_tier_3",  "stack_tier_2",  "stack_tier_3"},
    {"alleviatedbackpacks:stack_conversion_tier_2_to_tier_4",  "stack_tier_2",  "stack_tier_4"},
    {"alleviatedbackpacks:stack_conversion_tier_3_to_tier_4",  "stack_tier_3",  "stack_tier_4"},
};

constexpr std::string_view kGroup = "stack";

std::string msg(Player* player, std::string_view key, std::string_view fallback, std::string const& arg) {
    return player ? trFor(*player, key, fallback, arg) : tr(key, fallback, arg);
}

/// Bumped by `/backpack reload`: invalidates the caches below and the multipliers captured by
/// limit modifiers. Server thread only.
std::uint64_t gGeneration = 1;

struct AffectsCache {
    std::uint64_t                         generation = 0;
    std::unordered_map<std::string, bool> byId;
};

std::array<std::string, std::size(kTiers)> const& tierItemIds() {
    static auto const ids = [] {
        std::array<std::string, std::size(kTiers)> out;
        for (std::size_t i = 0; i < out.size(); ++i) out[i] = itemIdOf(kTiers[i].id);
        return out;
    }();
    return ids;
}

/// Applies config changes after `/backpack reload`.
class StackModule {
public:
    void install() { config::onReload([] { ++gGeneration; }); }
    void uninstall() {}
};

} // namespace

std::span<TierDef const>       tiers() { return kTiers; }
std::span<ConversionDef const> conversions() { return kConversions; }

std::string itemIdOf(std::string_view upgradeId) {
    return std::string(game::kNamespace) + ":" + std::string(upgradeId) + "_upgrade";
}

TierDef const* tierById(std::string_view upgradeId) {
    for (auto const& t : kTiers)
        if (t.id == upgradeId) return &t;
    return nullptr;
}

TierDef const* tierByItem(std::string_view itemId) {
    auto const& ids = tierItemIds();
    for (std::size_t i = 0; i < ids.size(); ++i)
        if (ids[i] == itemId) return &kTiers[i];
    return nullptr;
}

ConversionDef const* conversionByItem(std::string_view itemId) {
    for (auto const& c : kConversions)
        if (c.itemId == itemId) return &c;
    return nullptr;
}

double multiplierOf(TierDef const& tier) {
    double m = config::get<StackConfig>().*tier.multiplier;
    return (m > 0.0 && std::isfinite(m)) ? m : 1.0;
}

bool affects(core::ItemStackData const& item) {
    // Called for every slot limit lookup: memoize per item id (cleared on config reload).
    static AffectsCache cache;
    if (cache.generation != gGeneration) {
        cache.byId.clear();
        cache.generation = gGeneration;
    }
    if (auto it = cache.byId.find(item.id); it != cache.byId.end()) return it->second;
    bool result = true;
    for (auto const& pattern : config::get<StackConfig>().nonStackableItems) {
        if (core::globMatch(pattern, item.id)) {
            result = false;
            break;
        }
    }
    cache.byId.emplace(item.id, result);
    return result;
}

namespace {
std::vector<double> multipliersOf(game::Backpack const& backpack, int skipSlot, double extra) {
    std::vector<double> ms;
    auto const&         slots = backpack.data().upgrades;
    for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
        auto const& s = slots[static_cast<std::size_t>(i)];
        if (i == skipSlot || s.empty() || !s.enabled) continue;
        if (auto const* t = tierByItem(s.item.id)) ms.push_back(multiplierOf(*t));
    }
    if (extra != 1.0) ms.push_back(extra);
    return ms;
}
} // namespace

double totalMultiplier(game::Backpack const& backpack, int skipSlot, double extra) {
    return core::stack::combine(multipliersOf(backpack, skipSlot, extra));
}

int slotLimit(game::Backpack const& backpack, int skipSlot, double extra) {
    return core::stack::slotLimitOf(multipliersOf(backpack, skipSlot, extra));
}

int minimumMultiplierRequired(std::shared_ptr<game::Backpack> const& backpack, int slot) {
    if (!backpack) return 0;
    UpgradeContext ctx(backpack, slot);
    // SC: ceil(contents / base capacity), the base capacity being the capacity at multiplier 1.
    // Handlers report the capacity at the current (SB-adjusted) multiplier: divide it back out.
    auto tank   = api::backpack::tankOf(ctx);
    auto energy = tank ? nullptr : api::backpack::energyOf(ctx);
    if (!tank && !energy) return 0;
    double current  = core::capacity::adjustedMultiplier(api::backpack::stackMultiplier(*backpack));
    auto   required = [&](long long amount, long long capacity) {
        if (amount <= 0 || capacity <= 0) return 0;
        double base = static_cast<double>(capacity) / std::max(current, 1e-9);
        if (base <= 0.0) return 0;
        return static_cast<int>(std::min(std::ceil(static_cast<float>(amount) / base), static_cast<double>(INT_MAX)));
    };
    int need = 0;
    if (tank) {
        for (int i = 0; i < tank->tanks(); ++i) need = std::max(need, required(tank->fluidInTank(i).amount, tank->tankCapacity(i)));
    }
    if (energy) need = std::max(need, required(energy->stored(), energy->capacity()));
    return need;
}

/// The error sets of the last refused `fits` on this thread (StackUpgrade::refusal*Slots are asked
/// right after the refusing check).
struct Refusal {
    std::vector<int> upgrades;
    std::vector<int> storage;
};
thread_local Refusal tLastRefusal;

bool fits(
    std::shared_ptr<game::Backpack> const& backpack,
    double                                 multiplier,
    int                                    ignoreUpgradeSlot,
    Player*                                player,
    std::string&                           reason,
    std::vector<int>*                      errorUpgradeSlots
) {
    int    limit     = core::stack::slotLimitOf(multiplier);
    double effective = static_cast<double>(limit) / 64.0;
    bool   slotsOver = core::stack::firstSlotNeedingMore(backpack->inventory(), multiplier) >= 0;
    std::vector<int> upgradesOver;
    auto const&      slots = backpack->data().upgrades;
    for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
        if (i == ignoreUpgradeSlot || slots[static_cast<std::size_t>(i)].empty()) continue;
        if (minimumMultiplierRequired(backpack, i) > effective) upgradesOver.push_back(i);
    }
    // SC isMultiplierHighEnough's error sets, for the native screen's red overlay (GX-1):
    // slotsOverMultiplier (storage slots) and the stackable-contents upgrades (their slots and
    // inventory parts, e.g. the tank and battery bars).
    tLastRefusal = {};
    if (slotsOver) {
        auto const& inv = backpack->inventory();
        for (int i = 0; i < inv.size(); ++i) {
            auto const& st = inv.at(i);
            if (st.count <= 1 || st.maxStack <= 0) continue;
            if (static_cast<double>(st.count) / static_cast<double>(st.maxStack) > effective) tLastRefusal.storage.push_back(i);
        }
    }
    tLastRefusal.upgrades = upgradesOver;
    if (!slotsOver && upgradesOver.empty()) return true;
    reason = msg(
        player,
        "abp.stack.error.low_multiplier",
        "There's at least one slot or upgrade that requires stack limit higher than {}",
        std::to_string(limit)
    );
    if (errorUpgradeSlots) *errorUpgradeSlots = std::move(upgradesOver);
    return false;
}
std::string formatMultiplier(double m) {
    auto num = [](double v) {
        if (v >= 1e6 || std::floor(v) == v) return std::format("{:.0f}", v);
        return std::format("{:g}", v);
    };
    if (m < 1.0 && m > 0.0) return "÷" + num(1.0 / m);
    return "×" + num(m);
}

/// One Stack upgrade / downgrade tier.
class StackUpgrade : public IUpgrade {
public:
    explicit StackUpgrade(TierDef const& def) : mDef(def) {
        mInfo.id           = std::string(def.id);
        mInfo.itemId       = itemIdOf(def.id);
        mInfo.nameKey      = "abp.stack." + std::string(def.id) + ".name";
        mInfo.nameFallback = std::string(def.fallback);
        mInfo.group        = std::string(kGroup);
        mInfo.toggleable   = false; // like SB: stack upgrades can't be turned off
        mInfo.hasSettings  = false;
    }

    [[nodiscard]] UpgradeInfo const& info() const override {
        mInfo.maxPerBackpack = std::max(1, config::get<StackConfig>().maxPerBackpack);
        return mInfo;
    }

    [[nodiscard]] double multiplier() const { return multiplierOf(mDef); }

    // SC StackUpgradeItem: the checks below project the slot limit after the change.
    bool canInstall(UpgradeContext const& ctx, std::string& reason) const override {
        // checkExtraInsertConditions: current limit / 64 x this multiplier; the target slot is ignored.
        double whenAdded = static_cast<double>(slotLimit(ctx.backpack(), ctx.slot())) / 64.0 * multiplier();
        return fits(ctx.backpackPtr(), whenAdded, ctx.slot(), ctx.player(), reason);
    }

    bool canRemove(UpgradeContext const& ctx, std::string& reason) const override {
        double whenRemoved = static_cast<double>(slotLimit(ctx.backpack(), ctx.slot())) / 64.0;
        return fits(ctx.backpackPtr(), whenRemoved, -1, ctx.player(), reason);
    }

    bool canReplaceWith(UpgradeContext const& ctx, core::ItemStackData const& replacement, std::string& reason) const override {
        auto const* other = tierByItem(replacement.id);
        if (!other) return canRemove(ctx, reason);
        double next = multiplierOf(*other);
        if (next >= multiplier()) return true; // same or higher tier never lowers the limit
        double whenRemoved = static_cast<double>(slotLimit(ctx.backpack(), ctx.slot())) / 64.0;
        return fits(ctx.backpackPtr(), whenRemoved * next, -1, ctx.player(), reason);
    }

    // SC UpgradeSlotChangeResult.fail(..., errorUpgradeSlots, slotsOverMultiplier, errorInventoryParts)
    // of the check that just refused.
    [[nodiscard]] std::vector<int> refusalStorageSlots(UpgradeContext const&) const override { return tLastRefusal.storage; }
    [[nodiscard]] std::optional<std::vector<int>> refusalUpgradeSlots(UpgradeContext const&) const override { return tLastRefusal.upgrades; }

    void attach(UpgradeContext const& ctx, core::BackpackInventory& inventory) override {
        // All stack upgrades together make one modifier (the product), registered by the first one.
        auto const& slots = ctx.backpack().data().upgrades;
        for (int i = 0; i < ctx.slot(); ++i) {
            auto const& s = slots[static_cast<std::size_t>(i)];
            if (!s.empty() && s.enabled && tierByItem(s.item.id)) return;
        }
        // The product is cached and recomputed after `/backpack reload` (config multipliers).
        struct Cached {
            std::uint64_t generation = 0;
            int           limit      = 64;
        };
        auto cached = std::make_shared<Cached>(Cached{gGeneration, slotLimit(ctx.backpack())});
        auto weak   = std::weak_ptr<game::Backpack>(ctx.backpackPtr());
        inventory.addLimitModifier(ctx.hookOwner(), 0, [cached, weak](int, core::ItemStackData const& item, int current) {
            if (cached->generation != gGeneration) {
                if (auto bp = weak.lock()) cached->limit = slotLimit(*bp);
                cached->generation = gGeneration;
            }
            // SC InventoryHandler.getBaseStackLimit; non-stackable items keep their own size.
            return affects(item) ? core::stack::stackLimit(current, cached->limit) : current;
        });
    }

    // SC StorageContainerMenuBase: right-clicking an installed stack upgrade with a conversion item
    // that fits it replaces the upgrade in place (if the target could be placed there), uses up one
    // conversion item unless in creative and plays the smithing table sound. Otherwise nothing
    // happens (no message).
    std::optional<int> useItemOn(Player& player, UpgradeContext const& ctx, core::ItemStackData const& item) override {
        auto const* conv = conversionByItem(item.id);
        if (!conv) return std::nullopt;
        if (conv->from != mDef.id) return 0;
        auto const* targetTier = tierById(conv->to);
        auto        target     = game::ItemConv::toData(game::ItemConv::make(itemIdOf(conv->to), 1));
        if (!targetTier || target.empty()) return 0;
        // slot.mayPlace(target): the target's install check with this slot ignored.
        double      whenPlaced = static_cast<double>(slotLimit(ctx.backpack(), ctx.slot())) / 64.0 * multiplierOf(*targetTier);
        std::string reason;
        if (!fits(ctx.backpackPtr(), whenPlaced, ctx.slot(), &player, reason)) return 0;
        auto& state = ctx.state();
        state.item  = target;
        ctx.backpack().markDirty();
        ctx.backpack().rebuildHooks();
        player.getLevel().playSound(SharedTypes::Legacy::LevelSoundEvent::SmithingTableUse, player.getPosition(), 0.8f, 1.0f);
        log().info("stack upgrade converted: {} -> {}", itemIdOf(mDef.id), itemIdOf(conv->to));
        return player.isCreative() ? 0 : 1;
    }

    [[nodiscard]] std::vector<std::string> describe(Player& player, UpgradeContext const& ctx) const override {
        return {
            trFor(player, "abp.stack.describe.multiplier", "§7Stack size multiplier: §f{}", formatMultiplier(multiplier())),
            trFor(player, "abp.stack.describe.total", "§7Slot limit (all stack upgrades): §f{}", std::to_string(slotLimit(ctx.backpack()))),
        };
    }

private:
    TierDef const&      mDef;
    mutable UpgradeInfo mInfo;
};

namespace {
template <std::size_t I>
struct TierUpgrade : StackUpgrade {
    TierUpgrade() : StackUpgrade(kTiers[I]) {}
};
} // namespace

} // namespace abp::upgrade::stack

ABP_REGISTER_UPGRADE(abp::upgrade::stack::TierUpgrade<0>);
ABP_REGISTER_UPGRADE(abp::upgrade::stack::TierUpgrade<1>);
ABP_REGISTER_UPGRADE(abp::upgrade::stack::TierUpgrade<2>);
ABP_REGISTER_UPGRADE(abp::upgrade::stack::TierUpgrade<3>);
ABP_REGISTER_UPGRADE(abp::upgrade::stack::TierUpgrade<4>);
ABP_REGISTER_UPGRADE(abp::upgrade::stack::TierUpgrade<5>);
ABP_REGISTER_UPGRADE(abp::upgrade::stack::TierUpgrade<6>);
ABP_REGISTER_UPGRADE(abp::upgrade::stack::TierUpgrade<7>);
ABP_REGISTER_UPGRADE(abp::upgrade::stack::TierUpgrade<8>);
ABP_REGISTER_MODULE(abp::upgrade::stack::StackModule);
