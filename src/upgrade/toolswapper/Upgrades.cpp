// Tool Swapper upgrade types (SB ToolSwapperUpgradeItem: basic = no tab and no key swap, advanced =
// tab + key swap), the advanced settings (native tab data / actions; chest menu for selftests).

#include <memory>
#include <string>
#include <vector>

#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "game/ItemConv.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "net/NativeScreens.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/FilterMenu.h"
#include "ui/menu/Menu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/toolswapper/ToolSwapper.h"

namespace abp::upgrade::toolswapper {

namespace {

std::string weaponText(Player& p, bool on) {
    return on ? trFor(p, "abp.toolswapper.swap_weapon", "Swap Weapon") : trFor(p, "abp.toolswapper.do_not_swap_weapon", "Do Not Swap Weapon");
}

std::string modeText(Player& p, ToolMode m) {
    switch (m) {
    case ToolMode::Any:
        return trFor(p, "abp.toolswapper.swap_tools", "Swap Tools");
    case ToolMode::OnlyTools:
        return trFor(p, "abp.toolswapper.only_swap_for_tools", "Only Swap For Other Tools");
    case ToolMode::Off:
    default:
        return trFor(p, "abp.toolswapper.do_not_swap_tools", "Do Not Swap Tools");
    }
}

ToolMode nextMode(ToolMode m) { return static_cast<ToolMode>((static_cast<int>(m) + 1) % 3); }

/// Chest UI of the advanced settings (simulated players in selftests; players get the native tab).
class SettingsMenu : public ui::Menu {
public:
    SettingsMenu(std::string title, UpgradeContext ctx) : Menu(std::move(title), Rows::Three), mCtx(std::move(ctx)) {}

    void build(Player& player) override {
        for (int i = 0; i < size(); ++i) setSlot(i, std::make_shared<ui::StaticSlot>(ui::icon("minecraft:gray_stained_glass_pane", " ")));
        auto s = settingsOf(mCtx);
        setSlot(11, std::make_shared<ui::ButtonSlot>(ui::icon("minecraft:iron_sword", weaponText(player, s.swapWeapon)), [this](ui::ClickContext const&) {
                    setSwapWeapon(mCtx, !settingsOf(mCtx).swapWeapon);
                    invalidate();
                }));
        setSlot(13, std::make_shared<ui::ButtonSlot>(ui::icon("minecraft:iron_pickaxe", modeText(player, s.mode)), [this](ui::ClickContext const&) {
                    setToolMode(mCtx, nextMode(settingsOf(mCtx).mode));
                    invalidate();
                }));
        setSlot(
            15,
            std::make_shared<ui::ButtonSlot>(
                ui::icon("minecraft:hopper", trFor(player, "abp.toolswapper.filter", "Tool filter")),
                [ctx = mCtx](ui::ClickContext const& c) {
                    ui::FilterMenu::Options opts;
                    opts.onBack = [ctx](Player& back) { ui::MenuManager::get().open(back, std::make_shared<SettingsMenu>(trFor(back, "abp.toolswapper.tab", "Tool Swap"), ctx)); };
                    ui::MenuManager::get().open(c.player, ui::FilterMenu::forUpgrade(c.player, ctx, "filter", kFilterSlots, opts));
                }
            )
        );
        setSlot(18, std::make_shared<ui::ButtonSlot>(ui::icon("minecraft:arrow", trFor(player, "abp.menu.back_generic", "Back")), [bp = mCtx.backpackPtr()](ui::ClickContext const& c) {
                    ui::BackpackMenu::openBackpack(c.player, bp, {}, ui::BackpackMenu::View::Upgrades);
                }));
    }

private:
    UpgradeContext mCtx;
};

std::vector<std::string> tooltipLines(Player& player, bool advanced) {
    std::vector<std::string> out{trFor(
        player,
        "abp.toolswapper.tooltip",
        "§7Automatically swaps item in player's hand for the one effective on the block/entity when these are left clicked."
    )};
    if (advanced) {
        out.push_back(trFor(
            player,
            "abp.toolswapper.advanced_tooltip",
            "§7Has filter options and enables swapping wrench like tools for block/entity player is looking at with {}",
            trFor(player, "abp.toolswapper.key", "Swap tool based on current block/entity")
        ));
    }
    return out;
}

class ToolSwapperUpgrade : public IUpgrade {
public:
    [[nodiscard]] UpgradeInfo const& info() const override {
        static UpgradeInfo const i{
            .id           = std::string(kBasicId),
            .itemId       = "alleviatedbackpacks:tool_swapper_upgrade",
            .nameKey      = "abp.toolswapper.name",
            .nameFallback = "Tool Swapper Upgrade",
            .tier         = UpgradeTier::Basic,
            .group        = std::string(kGroup),
            .hasSettings  = false, // SB: the basic one has no settings tab
        };
        return i;
    }
    [[nodiscard]] std::vector<std::string> describe(Player& player, UpgradeContext const&) const override { return tooltipLines(player, false); }
};

class AdvancedToolSwapperUpgrade : public IUpgrade {
public:
    [[nodiscard]] UpgradeInfo const& info() const override {
        static UpgradeInfo const i{
            .id           = std::string(kAdvancedId),
            .itemId       = "alleviatedbackpacks:advanced_tool_swapper_upgrade",
            .nameKey      = "abp.toolswapper.advanced.name",
            .nameFallback = "Advanced Tool Swapper Upgrade",
            .tier         = UpgradeTier::Advanced,
            .group        = std::string(kGroup),
            .hasSettings  = true,
        };
        return i;
    }
    [[nodiscard]] std::shared_ptr<ui::Menu> createSettingsMenu(Player& player, UpgradeContext const& ctx) override {
        return std::make_shared<SettingsMenu>(trFor(player, "abp.toolswapper.tab", "Tool Swap"), ctx);
    }
    [[nodiscard]] std::vector<std::string> describe(Player& player, UpgradeContext const&) const override { return tooltipLines(player, true); }
};

// ---- native tab (client/ui/tabs/toolswapper) ------------------------------------------------------

nlohmann::json tabData(Player& player, UpgradeContext const& ctx) {
    if (!isAdvanced(ctx)) return nlohmann::json::object();
    auto           s      = settingsOf(ctx);
    nlohmann::json ghosts = nlohmann::json::array();
    for (auto const& g : s.filter.ghosts) ghosts.push_back(g.empty() ? nlohmann::json(nullptr) : g.toJson());
    return nlohmann::json{
        {"toolswapper",
         {{"swapWeapon", s.swapWeapon},
          {"mode", std::string(toString(s.mode))},
          {"filter",
           {{"allow", s.filter.mode == core::FilterMode::Allow},
            {"primary", static_cast<int>(s.filter.matchBy)},
            {"durability", s.filter.matchDurability},
            {"nbt", s.filter.matchNbt},
            {"slotsInRow", kSlotsInRow},
            {"ghosts", std::move(ghosts)}}}}},
        {"title", trFor(player, "abp.toolswapper.tab", "Tool Swap")},
        {"tooltip", trFor(player, "abp.toolswapper.tab.tooltip", "Advanced Tool Swapper Settings")},
    };
}

bool tabAction(Player& player, UpgradeContext const& ctx, nlohmann::json const& data) {
    if (!isAdvanced(ctx) || !data.contains("toolswapper")) return false;
    auto op = data.value("toolswapper", std::string{});
    auto s  = settingsOf(ctx);
    log().info("tool swapper: {} tab action {}", player.getRealName(), op);
    if (op == "weapon") {
        setSwapWeapon(ctx, !s.swapWeapon);
        return true;
    }
    if (op == "mode") {
        setToolMode(ctx, nextMode(s.mode));
        return true;
    }
    auto& f = s.filter;
    if (op == "allow") f.mode = f.mode == core::FilterMode::Allow ? core::FilterMode::Deny : core::FilterMode::Allow;
    else if (op == "primary") f.matchBy = static_cast<core::MatchBy>((static_cast<int>(f.matchBy) + 1) % 3);
    else if (op == "durability") f.matchDurability = !f.matchDurability;
    else if (op == "nbt") f.matchNbt = !f.matchNbt;
    else if (op == "slot") {
        // SC filter slot: a copy of the cursor item (no duplicates); an empty cursor clears it.
        int idx = data.value("index", -1);
        if (idx < 0 || idx >= static_cast<int>(f.ghosts.size())) return false;
        auto const& cursor = player.mPlayerUIContainer->getItem(0);
        auto        item   = cursor.isNull() ? core::ItemStackData{} : game::ItemConv::toData(cursor).withCount(1);
        if (!item.empty() && f.hasGhost(item)) return true;
        f.setGhost(idx, item);
    } else {
        return false;
    }
    setFilter(ctx, f);
    return true;
}

} // namespace

} // namespace abp::upgrade::toolswapper

ABP_REGISTER_UPGRADE(abp::upgrade::toolswapper::ToolSwapperUpgrade);
ABP_REGISTER_UPGRADE(abp::upgrade::toolswapper::AdvancedToolSwapperUpgrade);
ABP_REGISTER_TAB_DATA(abp::net::TabDataProvider{"advanced_tool_swapper", &abp::upgrade::toolswapper::tabData});
ABP_REGISTER_TAB_ACTION(abp::net::TabActionHandler{"advanced_tool_swapper", &abp::upgrade::toolswapper::tabAction});
