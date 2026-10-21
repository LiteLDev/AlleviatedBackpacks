#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "mc/world/item/ItemStack.h"

#include "core/ItemStackData.h"
#include "mod/Registry.h"

class Player;

namespace abp::game {
class Backpack;
}

namespace abp::ui {

class Menu;

/// What the player did to a menu slot.
enum class ClickType {
    Take,      ///< pick the slot's items up onto the cursor (left/right click)
    Place,     ///< put cursor items into the slot
    QuickMove, ///< shift-click / direct move between the menu and the player inventory
    Swap,      ///< swap cursor and slot
    Drop,      ///< drop items from the slot (Q)
};

struct ClickContext {
    Player&          player;
    Menu&            menu;
    int              slot;    ///< menu slot index
    ClickType        type;
    int              amount;  ///< requested amount (client side view)
    ItemStack const& offered; ///< Place/Swap/QuickMove-in: the item being put in (else empty)
};

/// One slot of a chest menu. Subclass it or use the ready-made slots below.
///
/// The framework calls `click()` first for every interaction; returning true means "handled as
/// a button press": nothing moves and the client view is reverted and refreshed. Otherwise items
/// move through `take()` / `accept()`.
class Slot {
public:
    virtual ~Slot() = default;
    /// What the client sees (count is clamped to 1..255; return an empty stack for nothing).
    [[nodiscard]] virtual ItemStack display(Player& player) const = 0;
    virtual bool                    click(ClickContext const& /*ctx*/) { return false; }
    /// Removes up to `amount` items and returns them (empty = refuse).
    virtual ItemStack take(Player& /*player*/, int /*amount*/) { return {}; }
    /// Offers `stack`; returns how many items were accepted (0 = refuse).
    virtual int accept(Player& /*player*/, ItemStack const& /*stack*/) { return 0; }
};

/// Builds an icon stack with a custom name (and lore). Name/lore are shown as given (translate
/// them first). Returns an empty stack if the item id is unknown.
[[nodiscard]] ItemStack icon(std::string_view itemId, std::string const& name, std::vector<std::string> const& lore = {}, int count = 1);

/// A non-interactive icon (fillers, labels).
class StaticSlot : public Slot {
public:
    explicit StaticSlot(ItemStack icon) : mIcon(std::move(icon)) {}
    [[nodiscard]] ItemStack display(Player&) const override { return mIcon; }
    bool                    click(ClickContext const&) override { return true; }

private:
    ItemStack mIcon;
};

/// A button: shows an icon and runs `onClick` on any interaction.
class ButtonSlot : public Slot {
public:
    using Callback = std::function<void(ClickContext const&)>;
    ButtonSlot(ItemStack icon, Callback onClick) : mIcon(std::move(icon)), mOnClick(std::move(onClick)) {}
    [[nodiscard]] ItemStack display(Player&) const override { return mIcon; }
    bool                    click(ClickContext const& ctx) override {
        if (mOnClick) mOnClick(ctx);
        return true;
    }

private:
    ItemStack mIcon;
    Callback  mOnClick;
};

/// A "ghost" slot (filters): placing an item copies it as a 1-count ghost without consuming it;
/// taking clears it. `get`/`set` connect it to your model (e.g. `core::Filter::ghosts[i]`).
class GhostSlot : public Slot {
public:
    using Getter = std::function<core::ItemStackData()>;
    using Setter = std::function<void(core::ItemStackData const&)>;
    GhostSlot(Getter get, Setter set) : mGet(std::move(get)), mSet(std::move(set)) {}
    [[nodiscard]] ItemStack display(Player& player) const override;
    bool                    click(ClickContext const& ctx) override;

private:
    Getter mGet;
    Setter mSet;
};

/// A slot bound to one index of a backpack's storage. Counts above the stack size are shown in
/// the lore ("Stored: 300"); the visible stack is capped at the item's max stack size.
class StorageSlot : public Slot {
public:
    StorageSlot(std::shared_ptr<game::Backpack> backpack, int index) : mBackpack(std::move(backpack)), mIndex(index) {}
    [[nodiscard]] ItemStack display(Player& player) const override;
    /// Runs the registered storage click handlers (`ABP_REGISTER_STORAGE_CLICK`); default: none.
    bool                    click(ClickContext const& ctx) override;
    ItemStack               take(Player& player, int amount) override;
    int                     accept(Player& player, ItemStack const& stack) override;

    [[nodiscard]] int index() const { return mIndex; }

private:
    std::shared_ptr<game::Backpack> mBackpack;
    int                             mIndex;
};

/// Extension point for clicks on backpack storage slots (e.g. Inception opens a nested backpack
/// when it is dropped with Q). Called before any item moves; return true to handle the click
/// (nothing moves). Handlers run in registration order; the first that returns true wins.
using StorageClickHandler =
    std::function<bool(ClickContext const& ctx, std::shared_ptr<game::Backpack> const& backpack, int index)>;
using StorageClickRegistry = StaticRegistry<StorageClickHandler>;

namespace detail {
struct StorageClickRegistrar {
    explicit StorageClickRegistrar(StorageClickHandler h) { StorageClickRegistry::add(std::move(h)); }
};
} // namespace detail

/// A slot made of callbacks, for one-off custom behaviour.
class CallbackSlot : public Slot {
public:
    std::function<ItemStack(Player&)>                    onDisplay;
    std::function<bool(ClickContext const&)>             onClick;
    std::function<ItemStack(Player&, int)>               onTake;
    std::function<int(Player&, ItemStack const&)>        onAccept;

    [[nodiscard]] ItemStack display(Player& p) const override { return onDisplay ? onDisplay(p) : ItemStack{}; }
    bool      click(ClickContext const& ctx) override { return onClick ? onClick(ctx) : false; }
    ItemStack take(Player& p, int n) override { return onTake ? onTake(p, n) : ItemStack{}; }
    int       accept(Player& p, ItemStack const& s) override { return onAccept ? onAccept(p, s) : 0; }
};

} // namespace abp::ui

/// Adds a backpack storage-slot click handler (see `abp::ui::StorageClickHandler`).
#define ABP_REGISTER_STORAGE_CLICK(...)                                                                                \
    static ::abp::ui::detail::StorageClickRegistrar ABP_UNIQUE(abp_storage_click_)(__VA_ARGS__)
