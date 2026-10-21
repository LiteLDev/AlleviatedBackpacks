// Stack upgrades (Starter, Tier 1-4, Omega) and Stack downgrades (Tier 1-3): per-slot limit
// multipliers. Several stack upgrades in one backpack multiply each other. Removing, swapping
// or installing one is refused when a slot would hold more than the new limit allows.
// Conversion items turn an installed stack upgrade into a higher tier in place.

#include "upgrade/stack/StackUpgrade.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <unordered_map>

#include "mc/world/actor/player/Player.h"

#include "core/Codec.h"
#include "core/StackMultiplier.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "mod/Config.h"
#include "mod/I18n.h"
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

double totalMultiplier(game::Backpack const& backpack, int skipSlot, double extra) {
    std::vector<double> ms;
    auto const&         slots = backpack.data().upgrades;
    for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
        auto const& s = slots[static_cast<std::size_t>(i)];
        if (i == skipSlot || s.empty() || !s.enabled) continue;
        if (auto const* t = tierByItem(s.item.id)) ms.push_back(multiplierOf(*t));
    }
    if (extra != 1.0) ms.push_back(extra);
    return core::stack::combine(ms);
}

bool fits(game::Backpack& backpack, double multiplier, Player* player, std::string& reason) {
    if (core::stack::firstSlotOver(backpack.inventory(), multiplier, &affects) < 0) return true;
    reason = msg(
        player,
        "abp.stack.error.low_multiplier",
        "There's at least one slot that requires a stack limit higher than {}.",
        std::to_string(core::stack::baseSlotLimit(multiplier))
    );
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

    bool canInstall(UpgradeContext const& ctx, std::string& reason) const override {
        // The slot is still empty here; project the limit with this one added.
        return fits(ctx.backpack(), totalMultiplier(ctx.backpack(), ctx.slot(), multiplier()), ctx.player(), reason);
    }

    bool canRemove(UpgradeContext const& ctx, std::string& reason) const override {
        return fits(ctx.backpack(), totalMultiplier(ctx.backpack(), ctx.slot()), ctx.player(), reason);
    }

    bool canReplaceWith(UpgradeContext const& ctx, core::ItemStackData const& replacement, std::string& reason) const override {
        auto const* other = tierByItem(replacement.id);
        if (!other) return canRemove(ctx, reason);
        double next = multiplierOf(*other);
        if (next >= multiplier()) return true; // same or higher tier never lowers the limit
        return fits(ctx.backpack(), totalMultiplier(ctx.backpack(), ctx.slot(), next), ctx.player(), reason);
    }

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
            double        total      = 1.0;
        };
        auto cached = std::make_shared<Cached>(Cached{gGeneration, totalMultiplier(ctx.backpack())});
        auto weak   = std::weak_ptr<game::Backpack>(ctx.backpackPtr());
        inventory.addLimitModifier(ctx.hookOwner(), 0, [cached, weak](int, core::ItemStackData const& item, int current) {
            if (cached->generation != gGeneration) {
                if (auto bp = weak.lock()) cached->total = totalMultiplier(*bp);
                cached->generation = gGeneration;
            }
            return affects(item) ? core::stack::scaledLimit(current, cached->total) : current;
        });
    }

    std::optional<int> useItemOn(Player& player, UpgradeContext const& ctx, core::ItemStackData const& item) override {
        auto const* conv = conversionByItem(item.id);
        if (!conv) return std::nullopt;
        if (conv->from != mDef.id) {
            player.sendMessage(trFor(player, "abp.stack.conversion.wrong", "§cThis conversion doesn't fit that upgrade."));
            return 0;
        }
        auto const* targetTier = tierById(conv->to);
        auto        target     = game::ItemConv::toData(game::ItemConv::make(itemIdOf(conv->to), 1));
        if (!targetTier || target.empty()) return 0;
        // Conversions normally go up; with custom config multipliers they might not.
        std::string reason;
        if (!fits(ctx.backpack(), totalMultiplier(ctx.backpack(), ctx.slot(), multiplierOf(*targetTier)), &player, reason)) {
            player.sendMessage(reason);
            return 0;
        }
        auto& state = ctx.state();
        state.item  = target;
        ctx.backpack().markDirty();
        ctx.backpack().rebuildHooks();
        player.sendMessage(trFor(
            player,
            "abp.stack.conversion.done",
            "§aConverted to {}.",
            trFor(player, "abp.stack." + std::string(targetTier->id) + ".name", targetTier->fallback)
        ));
        return 1;
    }

    [[nodiscard]] std::vector<std::string> describe(Player& player, UpgradeContext const& ctx) const override {
        double total = totalMultiplier(ctx.backpack());
        return {
            trFor(player, "abp.stack.describe.multiplier", "§7Stack size multiplier: §f{}", formatMultiplier(multiplier())),
            trFor(player, "abp.stack.describe.total", "§7Slot limit (all stack upgrades): §f{}", std::to_string(core::stack::baseSlotLimit(total))),
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
