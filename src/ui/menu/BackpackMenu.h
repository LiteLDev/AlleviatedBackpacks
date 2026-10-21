#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "game/Backpack.h"
#include "game/PlayerScan.h"
#include "mod/Registry.h"
#include "ui/menu/Menu.h"

namespace abp::ui {

class BackpackMenu;

/// What a backpack-menu extension gets.
struct BackpackMenuContext {
    Player&                         player;
    BackpackMenu&                   menu;
    std::shared_ptr<game::Backpack> backpack;
};

/// A button that feature units add to the bottom row of the backpack storage page
/// (e.g. Sort, Transfer to/from chest, Craft). Up to 4 are shown directly, in `order`; the rest
/// go to a "More" form.
///
///     ABP_REGISTER_BACKPACK_BUTTON(abp::ui::BackpackButton{
///         .id = "sort", .order = 10,
///         .icon = [](auto const& c) { return abp::ui::icon("minecraft:hopper", abp::trFor(c.player, "abp.sort", "Sort")); },
///         .onClick = [](auto const& c, abp::ui::ClickContext const&) { ... },
///     });
struct BackpackButton {
    std::string                                                              id;
    int                                                                      order = 0;
    std::function<bool(BackpackMenuContext const&)>                          visible; ///< optional; default visible
    std::function<ItemStack(BackpackMenuContext const&)>                     icon;
    std::function<void(BackpackMenuContext const&, ClickContext const&)>     onClick;
    /// Label for the "More" form when the button overflows (default: the icon's custom name).
    std::function<std::string(BackpackMenuContext const&)>                   label;
};
using BackpackButtonRegistry = StaticRegistry<BackpackButton>;

/// An entry of the backpack settings form (the comparator button), e.g. sorting options or
/// colours. `onSelect` runs after the chest menu has closed; reopen the backpack with
/// `BackpackMenu::openFor` when done if you want to return to it.
struct BackpackSetting {
    std::string                                                     id;
    int                                                             order = 0;
    std::function<std::string(Player&, game::Backpack const&)>      label;
    std::function<void(Player&, std::shared_ptr<game::Backpack>)>   onSelect;
};
using BackpackSettingRegistry = StaticRegistry<BackpackSetting>;

/// The main backpack GUI: 45 storage slots per page + a control row; an upgrades page with the
/// upgrade slots, on/off toggles and settings buttons.
class BackpackMenu : public Menu {
public:
    enum class View { Storage, Upgrades };

    /// `source`: where the backpack item is on the player (locked while open; the menu closes if
    /// the item goes away). nullopt for backpacks opened by other means (commands, tests).
    BackpackMenu(std::shared_ptr<game::Backpack> backpack, std::optional<game::ItemLocation> source, std::string title);

    /// Opens the backpack item at `where` on `player` (assigns a uuid on first open). Returns false
    /// if there's no backpack there.
    static bool openFor(Player& player, game::ItemLocation where);
    /// Opens a backpack by object (no item binding), optionally on the upgrades page. If the player
    /// is carrying this backpack, the menu is bound to that item like `openFor`.
    static bool openBackpack(
        Player&                         player,
        std::shared_ptr<game::Backpack> backpack,
        std::string                     title = {},
        View                            view  = View::Storage
    );

    [[nodiscard]] std::shared_ptr<game::Backpack> const& backpack() const { return mBackpack; }
    [[nodiscard]] View                                   view() const { return mView; }
    [[nodiscard]] int                                    page() const { return mPage; }
    [[nodiscard]] int                                    pageCount() const;
    void                                                 showPage(int page);
    void                                                 showView(View view);

    void                         build(Player& player) override;
    void                         onTick(Player& player) override;
    void                         onDestroyed() override;
    int                          quickMoveIn(Player& player, ItemStack const& stack) override;
    [[nodiscard]] std::optional<std::uint64_t> contentVersion() const override;

    static constexpr int kPageSlots = 45;

private:
    void buildStorage(Player& player);
    void buildUpgrades(Player& player);
    void buildControls(Player& player);
    void openSettingsForm(Player& player);

    std::shared_ptr<game::Backpack>   mBackpack;
    std::optional<game::ItemLocation> mSource;
    View                              mView = View::Storage;
    int                               mPage = 0;
    std::uint64_t                     mLayout = 0; ///< bumps on page/view changes
};

namespace detail {
struct BackpackButtonRegistrar {
    explicit BackpackButtonRegistrar(BackpackButton b) { BackpackButtonRegistry::add(std::move(b)); }
};
struct BackpackSettingRegistrar {
    explicit BackpackSettingRegistrar(BackpackSetting s) { BackpackSettingRegistry::add(std::move(s)); }
};
} // namespace detail

} // namespace abp::ui

/// Adds a button to the backpack storage page (see `abp::ui::BackpackButton`).
#define ABP_REGISTER_BACKPACK_BUTTON(...)                                                                              \
    static ::abp::ui::detail::BackpackButtonRegistrar ABP_UNIQUE(abp_backpack_button_)(__VA_ARGS__)
/// Adds an entry to the backpack settings form (see `abp::ui::BackpackSetting`).
#define ABP_REGISTER_BACKPACK_SETTING(...)                                                                             \
    static ::abp::ui::detail::BackpackSettingRegistrar ABP_UNIQUE(abp_backpack_setting_)(__VA_ARGS__)
