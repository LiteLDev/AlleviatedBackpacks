#include "upgrade/filter/FilterCommon.h"

#include <algorithm>

#include "mc/world/actor/player/Player.h"

#include "mod/I18n.h"
#include "ui/menu/BackpackMenu.h"
#include "upgrade/UpgradeManager.h"

namespace abp::upgrade::filtering {

void normalize(core::Filter& filter, UpgradeTier tier) {
    filter.ghosts.resize(static_cast<std::size_t>(slotsFor(tier)));
    filter.emptyAllowsAll = true;
    if (tier == UpgradeTier::Basic) {
        filter.matchBy         = core::MatchBy::Item;
        filter.matchNbt        = false;
        filter.matchDurability = false;
        filter.tags.clear();
    }
}

core::Filter load(UpgradeContext const& ctx, UpgradeTier tier) {
    auto const& settings = ctx.settings();
    auto        f        = core::Filter::fromJson(
        settings.is_object() ? settings.value(std::string(kFilterKey), nlohmann::json::object()) : nlohmann::json::object(),
        slotsFor(tier)
    );
    normalize(f, tier);
    return f;
}

void store(UpgradeContext const& ctx, UpgradeTier tier, core::Filter filter) {
    normalize(filter, tier);
    auto& settings = ctx.settings();
    if (!settings.is_object()) settings = nlohmann::json::object();
    settings[std::string(kFilterKey)] = filter.toJson();
    ctx.saveSettings(true);
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
    if (filter.isEmpty()) {
        out.push_back(
            allow ? trFor(player, "abp.filter_upgrade.lore.empty_allow", "§7Filter empty: §aaccepts everything")
                  : trFor(player, "abp.filter_upgrade.lore.empty_deny", "§7Deny list empty: §aaccepts everything")
        );
    } else if (filter.matchBy == core::MatchBy::Tag && !filter.tags.empty()) {
        out.push_back(
            allow ? trFor(player, "abp.filter_upgrade.lore.allow_tags", "§7Allow list: §f{} tags", filter.tags.size())
                  : trFor(player, "abp.filter_upgrade.lore.deny_tags", "§7Deny list: §f{} tags", filter.tags.size())
        );
    } else {
        out.push_back(
            allow ? trFor(player, "abp.filter_upgrade.lore.allow", "§7Allow list: §f{} items", items)
                  : trFor(player, "abp.filter_upgrade.lore.deny", "§7Deny list: §f{} items", items)
        );
    }
    if (tier == UpgradeTier::Advanced) {
        switch (filter.matchBy) {
        case core::MatchBy::Item:
            break;
        case core::MatchBy::Namespace:
            out.push_back(trFor(player, "abp.filter_upgrade.lore.match_namespace", "§7Matches by namespace (mod)"));
            break;
        case core::MatchBy::Tag:
            out.push_back(trFor(player, "abp.filter_upgrade.lore.match_tag", "§7Matches by tag"));
            break;
        }
        if (filter.matchBy == core::MatchBy::Item) {
            if (filter.matchNbt) out.push_back(trFor(player, "abp.filter_upgrade.lore.nbt", "§7Matches NBT"));
            if (filter.matchDurability) out.push_back(trFor(player, "abp.filter_upgrade.lore.durability", "§7Matches durability"));
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

namespace {
std::string titleFor(Player& player, UpgradeContext const& ctx) {
    if (auto* type = upgradeFor(ctx.state().item)) return displayName(*type, &player);
    return trFor(player, "abp.filter.title", "Filter");
}

ui::FilterMenu::Options optionsFor(UpgradeContext const& ctx, UpgradeTier tier) {
    auto o   = menuOptions(tier);
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
          return stillInstalled(ctx, itemId) ? load(ctx, tier) : core::Filter(slotsFor(tier));
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
