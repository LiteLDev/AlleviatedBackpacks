// Tool Swapper upgrade types and the advanced settings menu.

#include <memory>
#include <string>
#include <vector>

#include "mc/world/actor/player/Player.h"

#include "mod/I18n.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/FilterMenu.h"
#include "ui/menu/Menu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/toolswapper/ToolSwapper.h"

namespace abp::upgrade::toolswapper {

namespace {

std::string onOff(Player& p, bool v) {
    return v ? trFor(p, "abp.toolswapper.on", "§aOn") : trFor(p, "abp.toolswapper.off", "§cOff");
}

std::string modeName(Player& p, ToolMode m) {
    switch (m) {
    case ToolMode::Any:
        return trFor(p, "abp.toolswapper.mode.any", "§aAny held item");
    case ToolMode::OnlyTools:
        return trFor(p, "abp.toolswapper.mode.tools", "§eOnly when holding a tool");
    case ToolMode::Off:
        return trFor(p, "abp.toolswapper.mode.off", "§cOff");
    }
    return {};
}

int filterCount(Settings const& s) {
    int n = 0;
    for (auto const& g : s.filter.ghosts) n += g.empty() ? 0 : 1;
    return n;
}

/// Advanced Tool Swapper settings: weapon swapping, tool mode, durability, interaction swaps, filter.
class SettingsMenu : public ui::Menu {
public:
    SettingsMenu(std::string title, UpgradeContext ctx) : Menu(std::move(title), Rows::Three), mCtx(std::move(ctx)) {}

    void build(Player& player) override {
        for (int i = 0; i < size(); ++i) {
            setSlot(i, std::make_shared<ui::StaticSlot>(ui::icon("minecraft:gray_stained_glass_pane", " ")));
        }
        auto s      = settingsOf(mCtx);
        auto click  = trFor(player, "abp.toolswapper.click_to_toggle", "§7Click to toggle");
        auto toggle = [&](int slot, std::string_view icon, std::string name, std::string desc, auto change) {
            setSlot(
                slot,
                std::make_shared<ui::ButtonSlot>(
                    ui::icon(icon, std::move(name), {std::move(desc), click}),
                    [this, change](ui::ClickContext const&) {
                        auto cur = settingsOf(mCtx);
                        change(cur);
                        saveSettings(mCtx, cur);
                        invalidate();
                    }
                )
            );
        };
        toggle(
            10,
            "minecraft:iron_sword",
            trFor(player, "abp.toolswapper.weapons", "Swap weapons: {}", onOff(player, s.weapons)),
            trFor(player, "abp.toolswapper.weapons.desc", "§7Take the best weapon when attacking"),
            [](Settings& c) { c.weapons = !c.weapons; }
        );
        toggle(
            11,
            "minecraft:iron_pickaxe",
            trFor(player, "abp.toolswapper.mode", "Swap tools: {}", modeName(player, s.mode)),
            trFor(player, "abp.toolswapper.mode.desc", "§7Take the best tool when breaking blocks"),
            [](Settings& c) { c.mode = static_cast<ToolMode>((static_cast<int>(c.mode) + 1) % 3); }
        );
        toggle(
            12,
            "minecraft:anvil",
            trFor(
                player,
                "abp.toolswapper.durability",
                "Skip nearly broken tools: {}",
                onOff(player, s.skipLowDurability)
            ),
            trFor(player, "abp.toolswapper.durability.desc", "§7Leave tools that are about to break in the backpack"),
            [](Settings& c) { c.skipLowDurability = !c.skipLowDurability; }
        );
        toggle(
            13,
            "minecraft:shears",
            trFor(player, "abp.toolswapper.interact", "Swap for interactions: {}", onOff(player, s.interact)),
            trFor(
                player,
                "abp.toolswapper.interact.desc",
                "§7Empty hand: shears for sheep; sneak-use: axe/hoe/shovel for logs, dirt, paths"
            ),
            [](Settings& c) { c.interact = !c.interact; }
        );
        setSlot(
            15,
            std::make_shared<ui::ButtonSlot>(
                ui::icon(
                    "minecraft:hopper",
                    trFor(player, "abp.toolswapper.filter", "Tool filter"),
                    {trFor(player, "abp.toolswapper.filter.count", "§7Filter: §f{} items", filterCount(s)),
                     trFor(player, "abp.toolswapper.filter.desc", "§7Which tools may be swapped in"),
                     trFor(player, "abp.toolswapper.click_to_open", "§eClick to open")}
                ),
                [ctx = mCtx](ui::ClickContext const& c) {
                    ui::MenuManager::get().closeThen(c.player, [ctx](Player& p) {
                        ui::FilterMenu::Options opts;
                        opts.onBack = [ctx](Player& back) { openSettingsMenu(back, ctx); };
                        ui::MenuManager::get().open(
                            p,
                            ui::FilterMenu::forUpgrade(p, ctx, "filter", kFilterSlots, opts)
                        );
                    });
                }
            )
        );
        setSlot(
            18,
            std::make_shared<ui::ButtonSlot>(
                ui::icon("minecraft:arrow", trFor(player, "abp.menu.back_generic", "Back")),
                [bp = mCtx.backpackPtr()](ui::ClickContext const& c) {
                    ui::MenuManager::get().closeThen(c.player, [bp](Player& p) {
                        ui::BackpackMenu::openBackpack(p, bp, {}, ui::BackpackMenu::View::Upgrades);
                    });
                }
            )
        );
    }

    static void openSettingsMenu(Player& player, UpgradeContext const& ctx);

private:
    UpgradeContext mCtx;
};

std::shared_ptr<ui::Menu> makeSettingsMenu(Player& player, UpgradeContext const& ctx) {
    auto* type  = upgradeFor(ctx.state().item);
    auto  title = type ? displayName(*type, &player)
                       : trFor(player, "abp.toolswapper.advanced.name", "Advanced Tool Swapper Upgrade");
    return std::make_shared<SettingsMenu>(std::move(title), ctx);
}

/// Opens the settings menu after closing whatever chest menu is open: replacing a double chest by
/// a single one in place leaves the client without a screen.
void SettingsMenu::openSettingsMenu(Player& player, UpgradeContext const& ctx) {
    auto& mm = ui::MenuManager::get();
    if (!mm.current(player)) {
        mm.open(player, makeSettingsMenu(player, ctx));
        return;
    }
    mm.closeThen(player, [ctx](Player& p) { ui::MenuManager::get().open(p, makeSettingsMenu(p, ctx)); });
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
        };
        return i;
    }
    [[nodiscard]] std::vector<std::string> describe(Player& player, UpgradeContext const&) const override {
        return {trFor(player, "abp.toolswapper.describe", "§7Swaps in the best tool or weapon from the backpack")};
    }
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
        return makeSettingsMenu(player, ctx);
    }
    void openSettings(Player& player, UpgradeContext const& ctx) override {
        SettingsMenu::openSettingsMenu(player, ctx);
    }
    [[nodiscard]] std::vector<std::string> describe(Player& player, UpgradeContext const& ctx) const override {
        auto s = settingsOf(ctx);
        return {
            trFor(player, "abp.toolswapper.weapons", "Swap weapons: {}", onOff(player, s.weapons)),
            trFor(player, "abp.toolswapper.mode", "Swap tools: {}", modeName(player, s.mode)),
            trFor(player, "abp.toolswapper.filter.count", "§7Filter: §f{} items", filterCount(s)),
        };
    }
};

} // namespace

} // namespace abp::upgrade::toolswapper

ABP_REGISTER_UPGRADE(abp::upgrade::toolswapper::ToolSwapperUpgrade);
ABP_REGISTER_UPGRADE(abp::upgrade::toolswapper::AdvancedToolSwapperUpgrade);
