// Filter upgrade (basic / advanced): restricts what automation (hoppers, placed-backpack I/O,
// other containers: `core::Source::Automation`) may insert into or extract from the backpack.
// Players, pickups and other upgrades are never affected (as in Sophisticated Backpacks).
//
// Settings: {"filter": core::Filter, "direction": 0 both | 1 input | 2 output}.

#include "upgrade/filter/FilterUpgrade.h"

#include <algorithm>
#include <memory>
#include <vector>

#include "mc/world/actor/player/Player.h"

#include "game/Backpack.h"
#include "game/ItemConv.h"
#include "net/NativeScreens.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/filter/FilterCommon.h"

namespace abp::upgrade::filter {

namespace {
/// Runs before other upgrades' insert filters (e.g. Void) so a refused item is never voided.
constexpr int kPriority = -100;
} // namespace

Direction directionOf(UpgradeContext const& ctx) {
    auto const& s = ctx.settings();
    if (!s.is_object()) return Direction::Both;
    auto it = s.find(std::string(kDirectionKey));
    if (it == s.end() || !it->is_number_integer()) return Direction::Both;
    int v = it->get<int>();
    return (v >= 0 && v <= 2) ? static_cast<Direction>(v) : Direction::Both;
}

void setDirection(UpgradeContext const& ctx, Direction d) {
    auto& s = ctx.settings();
    if (!s.is_object()) s = nlohmann::json::object();
    s[std::string(kDirectionKey)] = static_cast<int>(d);
    ctx.saveSettings(true);
}

std::string directionName(Player& player, Direction d) {
    switch (d) {
    case Direction::Input:
        return trFor(player, "abp.filter_upgrade.direction.input", "Input");
    case Direction::Output:
        return trFor(player, "abp.filter_upgrade.direction.output", "Output");
    case Direction::Both:
    default:
        return trFor(player, "abp.filter_upgrade.direction.both", "Input & Output");
    }
}

// ---- menu ----------------------------------------------------------------------------------------

namespace {
class FilterUpgradeMenu : public filtering::UpgradeFilterMenu {
public:
    using UpgradeFilterMenu::UpgradeFilterMenu;

protected:
    void addButtons(Player& player) override {
        auto const& ctx = context();
        auto        dir = directionOf(ctx);
        std::string_view iconId;
        switch (dir) {
        case Direction::Input:
            iconId = "minecraft:hopper";
            break;
        case Direction::Output:
            iconId = "minecraft:dropper";
            break;
        case Direction::Both:
        default:
            iconId = "minecraft:piston";
            break;
        }
        auto itemId = ctx.state().item.id;
        setSlot(
            controlSlot(kDirectionButton),
            std::make_shared<ui::ButtonSlot>(
                ui::icon(
                    iconId,
                    trFor(player, "abp.filter_upgrade.direction", "Direction: {}", directionName(player, dir)),
                    {trFor(player, "abp.filter_upgrade.direction.help", "§7Which automated transfers the filter checks"),
                     trFor(player, "abp.filter.click_to_cycle", "§7Click to cycle")}
                ),
                [this, itemId](ui::ClickContext const&) {
                    auto const& c = context();
                    if (!filtering::stillInstalled(c, itemId)) return;
                    setDirection(c, static_cast<Direction>((static_cast<int>(directionOf(c)) + 1) % 3));
                    invalidate();
                }
            )
        );
    }
};

class FilterUpgradeBase : public IUpgrade {
public:
    explicit FilterUpgradeBase(UpgradeInfo info) : mInfo(std::move(info)) {}

    [[nodiscard]] UpgradeInfo const& info() const override { return mInfo; }

    void attach(UpgradeContext const& ctx, core::BackpackInventory& inv) override {
        // SC FilteredItemHandler.matchesFilters: the active Filter upgrades of a direction decide
        // together. One filter: its result. Several allow lists: any of them may match. Several
        // with at least one block list: every one must pass. The first active Filter upgrade
        // registers the combined hooks; the others register none.
        int first = -1;
        forEachInstalled(ctx.backpackPtr(), true, [&](IUpgrade& u, UpgradeContext const& c) {
            if (first < 0 && u.info().groupOrId() == mInfo.groupOrId()) first = c.slot();
        });
        if (first != ctx.slot()) return;
        // The filters taking part in automated inserts (Both / Input) and extracts (Both / Output).
        auto inFilters  = std::make_shared<std::vector<core::Filter>>();
        auto outFilters = std::make_shared<std::vector<core::Filter>>();
        forEachInstalled(ctx.backpackPtr(), true, [&](IUpgrade& u, UpgradeContext const& c) {
            if (u.info().groupOrId() != mInfo.groupOrId()) return;
            auto filter = filtering::load(c, u.info().tier);
            auto dir    = directionOf(c);
            if (dir != Direction::Output) inFilters->push_back(filter);
            if (dir != Direction::Input) outFilters->push_back(filter);
        });
        auto weak = std::weak_ptr<game::Backpack>(ctx.backpackPtr());
        // SC FilterUpgradeWrapper: a ContentsFilterLogic (STORAGE = items already in the backpack).
        auto passes = [weak](core::Filter const& filter, core::ItemStackData const& item) {
            if (!filter.filterByStorage) return filter.passes(item, &filtering::matcher());
            auto bp = weak.lock();
            return bp && filtering::matchesContents(filter, *bp, item);
        };
        auto decide = [passes](std::vector<core::Filter> const& active, core::ItemStackData const& item) {
            if (active.empty()) return true;
            if (active.size() == 1) return passes(active.front(), item);
            bool matchAll = std::any_of(active.begin(), active.end(), [](core::Filter const& f) {
                return f.mode != core::FilterMode::Allow;
            });
            if (matchAll) return std::all_of(active.begin(), active.end(), [&](core::Filter const& f) { return passes(f, item); });
            return std::any_of(active.begin(), active.end(), [&](core::Filter const& f) { return passes(f, item); });
        };
        if (!inFilters->empty()) {
            inv.addInsertFilter(ctx.hookOwner(), kPriority, [decide, inFilters](core::InsertQuery const& q) {
                if (q.ctx.source != core::Source::Automation) return core::InsertVerdict::Allow;
                return decide(*inFilters, q.stack) ? core::InsertVerdict::Allow : core::InsertVerdict::Deny;
            });
        }
        if (!outFilters->empty()) {
            inv.addExtractFilter(ctx.hookOwner(), kPriority, [decide, outFilters](core::ExtractQuery const& q) {
                if (q.ctx.source != core::Source::Automation) return true;
                return decide(*outFilters, q.stack);
            });
        }
    }

    [[nodiscard]] std::shared_ptr<ui::Menu> createSettingsMenu(Player& player, UpgradeContext const& ctx) override {
        return std::make_shared<FilterUpgradeMenu>(player, ctx, mInfo.tier);
    }

    [[nodiscard]] std::vector<std::string> describe(Player& player, UpgradeContext const& ctx) const override {
        auto lines = filtering::describe(player, filtering::load(ctx, mInfo.tier), mInfo.tier);
        lines.push_back(
            trFor(player, "abp.filter_upgrade.lore.direction", "§7Direction: §f{}", directionName(player, directionOf(ctx)))
        );
        return lines;
    }

private:
    UpgradeInfo mInfo;
};

class FilterUpgrade final : public FilterUpgradeBase {
public:
    FilterUpgrade()
    : FilterUpgradeBase(UpgradeInfo{
          .id           = std::string(kBasicId),
          .itemId       = "alleviatedbackpacks:filter_upgrade",
          .nameKey      = "abp.upgrade.filter.name",
          .nameFallback = "Filter Upgrade",
          .tier         = UpgradeTier::Basic,
          .group        = "filter",
          .maxPerBackpack = kMaxPerBackpack,
          .hasSettings  = true,
          .toggleable   = true,
      }) {}
};

class AdvancedFilterUpgrade final : public FilterUpgradeBase {
public:
    AdvancedFilterUpgrade()
    : FilterUpgradeBase(UpgradeInfo{
          .id           = std::string(kAdvancedId),
          .itemId       = "alleviatedbackpacks:advanced_filter_upgrade",
          .nameKey      = "abp.upgrade.advanced_filter.name",
          .nameFallback = "Advanced Filter Upgrade",
          .tier         = UpgradeTier::Advanced,
          .group        = "filter",
          .maxPerBackpack = kMaxPerBackpack,
          .hasSettings  = true,
          .toggleable   = true,
      }) {}
};
} // namespace

} // namespace abp::upgrade::filter

ABP_REGISTER_UPGRADE(abp::upgrade::filter::FilterUpgrade);
ABP_REGISTER_UPGRADE(abp::upgrade::filter::AdvancedFilterUpgrade);

// ---- native tab (client: direction button at +3,+24, FilterTab "filter" at +3,+44) --------------

namespace abp::upgrade::filter {
namespace {
[[maybe_unused]] bool const kTabRegistered = (filtering::registerStandardFilterTab("filter"), true);
} // namespace
} // namespace abp::upgrade::filter

ABP_REGISTER_TAB_DATA(abp::net::TabDataProvider{
    "filter",
    [](Player&, abp::upgrade::UpgradeContext const& ctx) {
        return nlohmann::json{{"direction", static_cast<int>(abp::upgrade::filter::directionOf(ctx))}};
    },
});
ABP_REGISTER_TAB_ACTION(abp::net::TabActionHandler{
    "filter",
    [](Player&, abp::upgrade::UpgradeContext const& ctx, nlohmann::json const& data) {
        using namespace abp::upgrade::filter;
        if (!data.is_object() || data.value("op", std::string{}) != "direction") return false;
        // SC Direction.next(): BOTH -> INPUT -> OUTPUT.
        setDirection(ctx, static_cast<Direction>((static_cast<int>(directionOf(ctx)) + 1) % 3));
        abp::log().info("filter tab: upgrade slot={} op=direction direction={}", ctx.slot(), static_cast<int>(directionOf(ctx)));
        return true;
    },
});
