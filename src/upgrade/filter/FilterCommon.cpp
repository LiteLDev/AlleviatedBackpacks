#include "upgrade/filter/FilterCommon.h"

#include <algorithm>
#include <format>

#include "mc/world/actor/player/Player.h"

#include "core/BackpackInventory.h"
#include "core/Sorting.h"
#include "feature/gui/Gui.h"
#include "game/Backpack.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "ui/menu/BackpackMenu.h"
#include "upgrade/UpgradeManager.h"

ABP_REGISTER_CONFIG("pickup", abp::upgrade::filtering::PickupConfig);
ABP_REGISTER_CONFIG("filter", abp::upgrade::filtering::FilterConfig);
ABP_REGISTER_CONFIG("restock", abp::upgrade::filtering::RestockConfig);
ABP_REGISTER_CONFIG("deposit", abp::upgrade::filtering::DepositConfig);

namespace abp::upgrade::filtering {

FilterSpec specFor(std::string_view group, UpgradeTier tier) {
    bool adv = tier == UpgradeTier::Advanced;
    if (group == "filter") {
        auto const& c = config::get<FilterConfig>();
        return specOf(adv ? c.advancedFilterUpgrade : c.filterUpgrade, adv, TypeButton::Contents);
    }
    if (group == "restock") {
        auto const& c = config::get<RestockConfig>();
        return specOf(adv ? c.advancedRestockUpgrade : c.restockUpgrade, adv, TypeButton::Contents);
    }
    if (group == "deposit") {
        auto const& c = config::get<DepositConfig>();
        return specOf(adv ? c.advancedDepositUpgrade : c.depositUpgrade, adv, TypeButton::Deposit);
    }
    auto const& c = config::get<PickupConfig>();
    return specOf(adv ? c.advancedPickupUpgrade : c.pickupUpgrade, adv, TypeButton::Contents);
}

FilterSpec specFor(UpgradeContext const& ctx, UpgradeTier tier) {
    auto* type = upgradeFor(ctx.state().item);
    return specFor(type ? std::string_view(type->info().groupOrId()) : std::string_view("pickup"), tier);
}

void normalize(core::Filter& filter, UpgradeTier tier, int slots) {
    filter.ghosts.resize(static_cast<std::size_t>(std::max(0, slots)));
    filter.emptyAllowsAll = false; // SC ContentsFilterLogic / DepositFilterLogic: an empty allow list matches nothing
    if (tier == UpgradeTier::Basic) {
        filter.matchBy         = core::MatchBy::Item;
        filter.matchNbt        = false;
        filter.matchDurability = false;
        filter.matchAnyTag     = true;
        filter.tags.clear();
    }
}

core::Filter read(nlohmann::json const& settings, int slots, core::FilterDefaults defaults, std::string_view group) {
    nlohmann::json stored = settings.is_object() ? settings.value(std::string(kFilterKey), nlohmann::json()) : nlohmann::json();
    auto           f      = core::Filter::fromJson(stored, slots, defaults);
    if (stored.is_object() && !stored.empty() && !stored.contains("v")) {
        // Phase 1: an empty allow list accepted everything (= SC's empty block list).
        auto old = core::Filter::fromJson(stored, slots);
        if (old.mode == core::FilterMode::Allow && old.emptyAllowsAll && old.isEmpty()) f.mode = core::FilterMode::Deny;
        // Phase 1 Deposit / Restock "contents" toggle.
        if (settings.value("contents", false)) {
            if (group == "deposit") f.filterByInventory = true;
            else f.filterByStorage = true;
        }
    }
    return f;
}

core::Filter load(UpgradeContext const& ctx, UpgradeTier tier) {
    auto* type  = upgradeFor(ctx.state().item);
    auto  group = type ? std::string_view(type->info().groupOrId()) : std::string_view("pickup");
    auto  spec  = specFor(group, tier);
    auto  f     = read(ctx.settings(), spec.slots, {}, group);
    normalize(f, tier, spec.slots);
    return f;
}

void store(UpgradeContext const& ctx, UpgradeTier tier, core::Filter filter) {
    normalize(filter, tier, specFor(ctx, tier).slots);
    auto& settings = ctx.settings();
    if (!settings.is_object()) settings = nlohmann::json::object();
    settings[std::string(kFilterKey)] = filter.toJson();
    settings.erase("contents"); // phase-1 key, now part of the filter
    ctx.saveSettings(true);
}

std::vector<core::ItemStackData> storedItems(game::Backpack& backpack) {
    std::vector<core::ItemStackData> out;
    auto const&                      inv = backpack.inventory();
    for (int i = 0; i < inv.size(); ++i) {
        auto const& s = inv.at(i);
        if (s.empty()) continue;
        if (std::none_of(out.begin(), out.end(), [&](core::ItemStackData const& o) { return o.sameItem(s); })) {
            out.push_back(s.withCount(1));
        }
    }
    return out;
}

bool matchesContents(core::Filter const& filter, game::Backpack& backpack, core::ItemStackData const& item) {
    if (!filter.filterByStorage) return filter.matchesContents(item, {}, &matcher());
    auto stored = storedItems(backpack);
    auto const& memory = feature::gui::memory(backpack);
    return filter.matchesContents(item, stored, &matcher(), [&](core::ItemStackData const& i) {
        return std::any_of(memory.begin(), memory.end(), [&](core::ItemStackData const& m) {
            return !m.empty() && core::memoryMatches(m, i);
        });
    });
}

ui::FilterMenu::Options menuOptions(UpgradeTier tier) {
    ui::FilterMenu::Options o;
    o.slots           = slotsFor(tier);
    bool advanced     = tier == UpgradeTier::Advanced;
    o.allowModeToggle = true;
    o.allowMatchBy    = advanced;
    o.allowNbt        = advanced;
    o.allowDurability = advanced;
    return o;
}

std::vector<std::string> describe(Player& player, core::Filter const& filter, UpgradeTier tier) {
    std::vector<std::string> out;
    int  items = static_cast<int>(std::count_if(filter.ghosts.begin(), filter.ghosts.end(), [](core::ItemStackData const& g) {
        return !g.empty();
    }));
    bool allow = filter.mode == core::FilterMode::Allow;
    if (filter.filterByStorage) {
        out.push_back(trFor(player, "abp.filter_upgrade.lore.storage", "§7Match Backpack Contents"));
    } else if (filter.isEmpty()) {
        out.push_back(
            allow ? trFor(player, "abp.filter_upgrade.lore.empty_allow", "§7Allow list empty: §cmatches nothing")
                  : trFor(player, "abp.filter_upgrade.lore.empty_deny", "§7Block list empty: §amatches everything")
        );
    } else if (filter.matchBy == core::MatchBy::Tag) {
        out.push_back(
            allow ? trFor(player, "abp.filter_upgrade.lore.allow_tags", "§7Allow: §f{} tags", filter.tags.size())
                  : trFor(player, "abp.filter_upgrade.lore.deny_tags", "§7Block: §f{} tags", filter.tags.size())
        );
    } else {
        out.push_back(
            allow ? trFor(player, "abp.filter_upgrade.lore.allow", "§7Allow: §f{} items", items)
                  : trFor(player, "abp.filter_upgrade.lore.deny", "§7Block: §f{} items", items)
        );
    }
    if (tier == UpgradeTier::Advanced) {
        switch (filter.matchBy) {
        case core::MatchBy::Item:
            break;
        case core::MatchBy::Namespace:
            out.push_back(trFor(player, "abp.filter_upgrade.lore.match_namespace", "§7Match Mod"));
            break;
        case core::MatchBy::Tag:
            out.push_back(
                filter.matchAnyTag ? trFor(player, "abp.filter_upgrade.lore.match_any_tag", "§7Match Any Tag")
                                   : trFor(player, "abp.filter_upgrade.lore.match_all_tags", "§7Match All Tags")
            );
            break;
        }
        if (filter.matchBy != core::MatchBy::Tag) {
            if (filter.matchNbt) out.push_back(trFor(player, "abp.filter_upgrade.lore.nbt", "§7Match NBT"));
            if (filter.matchDurability) out.push_back(trFor(player, "abp.filter_upgrade.lore.durability", "§7Match Durability"));
        }
    }
    return out;
}

bool stillInstalled(UpgradeContext const& ctx, std::string const& itemId) {
    auto const& slots = ctx.backpack().data().upgrades;
    if (ctx.slot() < 0 || ctx.slot() >= static_cast<int>(slots.size())) return false;
    auto const& s = slots[static_cast<std::size_t>(ctx.slot())];
    return !s.empty() && s.item.id == itemId;
}

nlohmann::json tabTexts(Player& player, std::string_view group, bool advanced) {
    struct Texts {
        std::string_view group, name, advName, tooltip, advTooltip;
    };
    static constexpr Texts kTexts[] = {
        {"pickup",  "Pickup",  "Adv. Pickup",  "Pickup Settings",  "Advanced Pickup Settings" },
        {"filter",  "Filter",  "Adv. Filter",  "Filter Settings",  "Advanced Filter Settings" },
        {"restock", "Restock", "Adv. Restock", "Restock Settings", "Advanced Restock Settings"},
        {"deposit", "Deposit", "Adv. Deposit", "Deposit Settings", "Advanced Deposit Settings"},
    };
    for (auto const& t : kTexts) {
        if (t.group != group) continue;
        auto base = std::format("abp.{}.tab{}", group, advanced ? ".advanced" : "");
        return nlohmann::json{
            {"title",   trFor(player, base, advanced ? t.advName : t.name)                    },
            {"tooltip", trFor(player, base + ".tooltip", advanced ? t.advTooltip : t.tooltip)},
        };
    }
    return nlohmann::json::object();
}

namespace {
std::string titleFor(Player& player, UpgradeContext const& ctx) {
    if (auto* type = upgradeFor(ctx.state().item)) return displayName(*type, &player);
    return trFor(player, "abp.filter.title", "Filter");
}

ui::FilterMenu::Options optionsFor(UpgradeContext const& ctx, UpgradeTier tier) {
    auto o   = menuOptions(tier);
    o.slots  = specFor(ctx, tier).slots;
    o.onBack = [bp = ctx.backpackPtr()](Player& p) {
        ui::BackpackMenu::openBackpack(p, bp, {}, ui::BackpackMenu::View::Upgrades);
    };
    return o;
}
} // namespace

UpgradeFilterMenu::UpgradeFilterMenu(Player& player, UpgradeContext ctxIn, UpgradeTier tier)
: UpgradeFilterMenu(player, ctxIn.withPlayer(nullptr), tier, 0) {}

// The menu outlives the call: never keep the opening player in the captured context.
UpgradeFilterMenu::UpgradeFilterMenu(Player& player, UpgradeContext ctx, UpgradeTier tier, int /*tag*/)
: ui::FilterMenu(
      titleFor(player, ctx),
      [ctx, tier, itemId = ctx.state().item.id] {
          return stillInstalled(ctx, itemId) ? load(ctx, tier) : core::Filter(specFor(ctx, tier).slots, core::FilterDefaults{});
      },
      [ctx, tier, itemId = ctx.state().item.id](core::Filter const& f) {
          if (stillInstalled(ctx, itemId)) store(ctx, tier, f);
      },
      optionsFor(ctx, tier)
  ),
  mCtx(std::move(ctx)) {}

void UpgradeFilterMenu::build(Player& player) {
    ui::FilterMenu::build(player);
    addButtons(player);
}

} // namespace abp::upgrade::filtering
