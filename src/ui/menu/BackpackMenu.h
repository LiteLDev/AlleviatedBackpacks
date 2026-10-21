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
/// (e.g. Sort, Transfer to/from chest, Craft). Up to 4 are shown directly, in `order`; with more,
/// 3 are shown at a time and the 4th spot becomes a "More buttons" button that pages through them.
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

/// Changes how a storage slot of the backpack menu looks (e.g. a memory slot shows its remembered
/// item as a ghost when empty). `shown` is the default display (possibly empty); `index` is the
/// backpack inventory index. Only the display changes: clicks still go to the storage slot.
struct BackpackSlotDecorator {
    std::string                                                                  id;
    int                                                                          order = 0;
    std::function<void(BackpackMenuContext const&, int index, ItemStack& shown)> decorate;
};
using BackpackSlotDecoratorRegistry = StaticRegistry<BackpackSlotDecorator>;

/// Overrides shift-clicking an item from the player inventory into the backpack menu. The first
/// handler (by `order`) returning a value decides how many items were accepted; nullopt = no
/// opinion (default behaviour).
struct BackpackQuickMoveHandler {
    std::string                                                                              id;
    int                                                                                      order = 0;
    std::function<std::optional<int>(BackpackMenuContext const&, ItemStack const& stack)> handle;
};
using BackpackQuickMoveRegistry = StaticRegistry<BackpackQuickMoveHandler>;

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
    /// Opens the first backpack the player carries: worn in the chest slot, then the offhand, then
    /// the main inventory (selected hotbar slot first). Returns false if none could be opened.
    static bool openCarried(Player& player);

    /// Default shift-click-in behaviour of the current view (what `quickMoveIn` does when no
    /// `BackpackQuickMoveHandler` decides): storage view -> storage; upgrades view -> upgrade slots.
    int quickMoveDefault(Player& player, ItemStack const& stack);
    /// Shift-click-in into the upgrade slots only (installs one upgrade). Returns 0 or 1.
    int quickMoveToUpgrades(Player& player, ItemStack const& stack);
    /// Shift-click-in into storage only (respects filters).
    int quickMoveToStorage(Player& player, ItemStack const& stack);
    /// Where the open backpack item sits on the player (nullopt when not bound to an item).
    [[nodiscard]] std::optional<game::ItemLocation> const& source() const { return mSource; }

    [[nodiscard]] std::shared_ptr<game::Backpack> const& backpack() const { return mBackpack; }
    [[nodiscard]] View                                   view() const { return mView; }
    /// Where the viewed backpack item is on the player (nullopt: not bound to an item).
    [[nodiscard]] std::optional<game::ItemLocation> const& source() const { return mSource; }
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
    int                               mButtonPage = 0; ///< page of feature buttons when more than 4 exist
};

namespace detail {
struct BackpackButtonRegistrar {
    explicit BackpackButtonRegistrar(BackpackButton b) { BackpackButtonRegistry::add(std::move(b)); }
};
struct BackpackSettingRegistrar {
    explicit BackpackSettingRegistrar(BackpackSetting s) { BackpackSettingRegistry::add(std::move(s)); }
};
struct BackpackSlotDecoratorRegistrar {
    explicit BackpackSlotDecoratorRegistrar(BackpackSlotDecorator d) { BackpackSlotDecoratorRegistry::add(std::move(d)); }
};
struct BackpackQuickMoveRegistrar {
    explicit BackpackQuickMoveRegistrar(BackpackQuickMoveHandler h) { BackpackQuickMoveRegistry::add(std::move(h)); }
};
} // namespace detail

} // namespace abp::ui

/// Adds a button to the backpack storage page (see `abp::ui::BackpackButton`).
#define ABP_REGISTER_BACKPACK_BUTTON(...)                                                                              \
    static ::abp::ui::detail::BackpackButtonRegistrar ABP_UNIQUE(abp_backpack_button_)(__VA_ARGS__)
/// Adds an entry to the backpack settings form (see `abp::ui::BackpackSetting`).
#define ABP_REGISTER_BACKPACK_SETTING(...)                                                                             \
    static ::abp::ui::detail::BackpackSettingRegistrar ABP_UNIQUE(abp_backpack_setting_)(__VA_ARGS__)
/// Adds a storage-slot display decorator (see `abp::ui::BackpackSlotDecorator`).
#define ABP_REGISTER_BACKPACK_SLOT_DECORATOR(...)                                                                      \
    static ::abp::ui::detail::BackpackSlotDecoratorRegistrar ABP_UNIQUE(abp_backpack_slot_decorator_)(__VA_ARGS__)
/// Adds a shift-click-in handler (see `abp::ui::BackpackQuickMoveHandler`).
#define ABP_REGISTER_BACKPACK_QUICK_MOVE(...)                                                                          \
    static ::abp::ui::detail::BackpackQuickMoveRegistrar ABP_UNIQUE(abp_backpack_quick_move_)(__VA_ARGS__)
