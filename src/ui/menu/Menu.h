#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ui/menu/Slot.h"

class Player;

namespace abp::ui {

/// A server-side chest GUI shown to one player (see `MenuManager`).
///
/// Subclass it and fill slots in `build()`; the framework calls `build()` when the menu opens and
/// again after `invalidate()`. Slot contents are re-sent automatically every tick when their
/// display changes, so model changes (e.g. backpack contents) show up without extra calls.
///
///     class MyMenu : public ui::Menu {
///     public:
///         MyMenu() : Menu("My title", Rows::Three) {}
///         void build(Player& p) override {
///             setSlot(13, std::make_shared<ui::ButtonSlot>(ui::icon("minecraft:emerald", "Hi"),
///                 [](ui::ClickContext const& c) { c.player.sendMessage("clicked"); }));
///         }
///     };
///     ui::MenuManager::get().open(player, std::make_shared<MyMenu>());
class Menu : public std::enable_shared_from_this<Menu> {
public:
    enum class Rows { Three = 3, Six = 6 }; ///< single (27) or double (54) chest

    Menu(std::string title, Rows rows);
    virtual ~Menu() = default;

    [[nodiscard]] std::string const& title() const { return mTitle; }
    /// Changes the title; takes effect when the menu is (re)opened (`MenuManager::reopen`).
    void                             setTitle(std::string title) { mTitle = std::move(title); }
    [[nodiscard]] Rows               rows() const { return mRows; }
    [[nodiscard]] int                size() const { return static_cast<int>(mRows) * 9; }

    void                setSlot(int index, std::shared_ptr<Slot> slot);
    [[nodiscard]] Slot* slot(int index) const;
    void                clearSlots();

    /// Re-runs `build()` before the next sync.
    void               invalidate() { mNeedsBuild = true; }
    [[nodiscard]] bool needsBuild() const { return mNeedsBuild; }

    /// A player inventory slot (0..35) that can't be touched while this menu is open (e.g. the
    /// backpack item being viewed).
    std::optional<int> lockedInventorySlot;

    // ---- overridable --------------------------------------------------------------------------
    /// Fill the slots. Called on open and after `invalidate()`.
    virtual void build(Player& player) = 0;
    virtual void onOpen(Player& /*player*/) {}
    /// Called when the menu goes away while the player is still there (closed by the player,
    /// replaced by another menu, died/teleported, disconnect, mod disable).
    virtual void onClose(Player& /*player*/) {}
    /// Always called exactly once when the menu goes away (after `onClose` if that ran), even if
    /// the player object is already gone. Put cleanup that must not be skipped here.
    virtual void onDestroyed() {}
    /// Every server tick while open.
    virtual void onTick(Player& /*player*/) {}
    /// Shift-click from the player's inventory into the menu. Returns how many items were
    /// accepted. Default: `accept()` on each slot in order, same-item slots first.
    virtual int quickMoveIn(Player& player, ItemStack const& stack);
    /// Optional change counter: when it returns a value equal to the previous sync's, the
    /// per-tick display diff is skipped (cheap for big menus). nullopt = always diff.
    [[nodiscard]] virtual std::optional<std::uint64_t> contentVersion() const { return std::nullopt; }

    /// Internal: build if needed.
    void ensureBuilt(Player& player);

private:
    std::string                        mTitle;
    Rows                               mRows;
    std::vector<std::shared_ptr<Slot>> mSlots;
    bool                               mNeedsBuild = true;
};

} // namespace abp::ui
