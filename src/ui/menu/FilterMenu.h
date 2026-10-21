#pragma once

#include <functional>
#include <memory>
#include <string>

#include "core/Filter.h"
#include "ui/menu/Menu.h"
#include "upgrade/IUpgrade.h"

namespace abp::ui {

/// Reusable filter editor (single or double chest): ghost slots on top, a control row at the
/// bottom (allow/deny, match by item/namespace/tag, NBT, durability, edit tags, clear, back).
///
/// Most upgrades store their filter in their settings JSON; use `forUpgrade()`:
///
///     std::shared_ptr<ui::Menu> createSettingsMenu(Player& p, upgrade::UpgradeContext const& ctx) override {
///         return ui::FilterMenu::forUpgrade(p, ctx, "filter", 9);
///     }
///     // in attach()/onPickup(): auto f = core::Filter::fromJson(ctx.settings()["filter"], 9);
struct FilterMenuOptions {
    int  slots           = 9;    ///< ghost slots (1..45)
    bool allowModeToggle = true; ///< show allow/deny
    bool allowMatchBy    = true; ///< show item/namespace/tag
    bool allowNbt        = true;
    bool allowDurability = true;
    /// "Back" button target (e.g. reopen the backpack). Empty = just close.
    std::function<void(Player&)> onBack;
};

class FilterMenu : public Menu {
public:
    using Options = FilterMenuOptions;
    using Getter = std::function<core::Filter()>;
    using Setter = std::function<void(core::Filter const&)>;

    FilterMenu(std::string title, Getter get, Setter set, Options options);

    /// A filter editor bound to `ctx.settings()[key]`; saving rebuilds the backpack hooks. The
    /// back button returns to the backpack GUI.
    static std::shared_ptr<FilterMenu>
    forUpgrade(Player& player, upgrade::UpgradeContext const& ctx, std::string key = "filter", int slots = 9, Options options = {});

    void build(Player& player) override;

private:
    void editTags(Player& player);

    Getter  mGet;
    Setter  mSet;
    Options mOptions;
};

} // namespace abp::ui
