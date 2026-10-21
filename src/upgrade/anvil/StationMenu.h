#pragma once

// Shared plumbing for "workstation" upgrade tabs (the anvil tab here, and the smithing unit's tab,
// which reuses it): a double-chest menu whose two top rows hold the station and whose four bottom
// rows show the backpack's storage (paged), so items move between the backpack, the station and
// the player inventory like in Sophisticated Backpacks' upgrade tabs. Station inputs are stored in
// the upgrade's settings, so they stay in the upgrade when the menu closes (and travel with the
// upgrade item when it is removed).

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mc/world/item/ItemStack.h"

#include "core/ItemStackData.h"
#include "game/PlayerScan.h"
#include "ui/menu/Menu.h"
#include "upgrade/IUpgrade.h"

namespace abp::upgrade::anvil {

/// An installed station upgrade (backpack + upgrade slot + expected upgrade id).
class StationRef {
public:
    StationRef(UpgradeContext ctx, std::string upgradeId) : mCtx(std::move(ctx)), mId(std::move(upgradeId)) {}

    [[nodiscard]] UpgradeContext const& ctx() const { return mCtx; }
    [[nodiscard]] std::string const&    id() const { return mId; }
    /// The upgrade is still installed in that slot.
    [[nodiscard]] bool                  valid() const;

    /// An item stored under `key` in the upgrade settings (empty when none / invalid).
    [[nodiscard]] core::ItemStackData item(std::string const& key) const;
    [[nodiscard]] ItemStack           stack(std::string const& key) const;
    /// Stores (or clears, for an empty item) the item under `key` and saves.
    void                              setItem(std::string const& key, core::ItemStackData const& data) const;
    void                              setStack(std::string const& key, ItemStack const& stack) const;
    /// Removes `n` items from the stack under `key`.
    void                              consume(std::string const& key, int n) const;

    /// A string setting (nullopt when missing / invalid).
    [[nodiscard]] std::optional<std::string> text(std::string const& key) const;
    void                                     setText(std::string const& key, std::optional<std::string> const& value) const;

private:
    UpgradeContext mCtx;
    std::string    mId;
};

/// A station input slot backed by `StationRef::item(key)`.
class InputSlot : public ui::Slot {
public:
    using Changed = std::function<void(Player&)>;

    InputSlot(StationRef ref, std::string key, Changed changed = {})
    : mRef(std::move(ref)),
      mKey(std::move(key)),
      mChanged(std::move(changed)) {}

    [[nodiscard]] ItemStack display(Player& player) const override;
    ItemStack               take(Player& player, int amount) override;
    int                     accept(Player& player, ItemStack const& stack) override;

private:
    StationRef  mRef;
    std::string mKey;
    Changed     mChanged;
};

class StationMenu;

/// The station's output slot. Taking it calls `craft`, which consumes the inputs and returns the
/// result plus a function that undoes the craft. If the framework hands the result back in the
/// same request (the destination was full), the craft is undone, so no item is ever lost.
class OutputSlot : public ui::Slot {
public:
    struct Crafted {
        ItemStack                    result;
        std::function<void(Player&)> undo;
    };
    using Preview = std::function<ItemStack(Player&)>;
    using Craft   = std::function<std::optional<Crafted>(Player&)>;

    OutputSlot(StationMenu& menu, Preview preview, Craft craft)
    : mMenu(menu),
      mPreview(std::move(preview)),
      mCraft(std::move(craft)) {}

    [[nodiscard]] ItemStack display(Player& player) const override { return mPreview ? mPreview(player) : ItemStack{}; }
    bool                    click(ui::ClickContext const& ctx) override { return ctx.type == ui::ClickType::Place; }
    ItemStack               take(Player& player, int amount) override;
    int                     accept(Player& player, ItemStack const& stack) override;

private:
    StationMenu& mMenu;
    Preview      mPreview;
    Craft        mCraft;
};

/// Base menu: rows 0/1 = station (subclass fills them in `buildStation`), rows 2..5 = backpack
/// storage. Slot 0 is "back to backpack", 9/17 page the storage.
class StationMenu : public ui::Menu {
public:
    static constexpr int kStorageStart = 18;
    static constexpr int kStorageSlots = 36;

    StationMenu(std::string title, StationRef ref);

    [[nodiscard]] StationRef const& ref() const { return mRef; }
    [[nodiscard]] int               page() const { return mPage; }
    [[nodiscard]] int               pageCount() const;

    void build(Player& player) final;
    void onOpen(Player& player) override;
    void onTick(Player& player) override;
    void onDestroyed() override;
    int  quickMoveIn(Player& player, ItemStack const& stack) final;

    /// Pending undo of the last output take (valid until the end of the current request).
    struct PendingUndo {
        ItemStack                    result;
        std::function<void(Player&)> undo;
    };
    std::optional<PendingUndo> pendingUndo;

protected:
    /// Fill the station slots (1..8, 10..16).
    virtual void buildStation(Player& player) = 0;
    /// Shift-click into the station; returns how many items were taken (rest goes to storage).
    virtual int  quickMoveStation(Player& /*player*/, ItemStack const& /*stack*/) { return 0; }

    /// A filler pane with an optional name.
    [[nodiscard]] static std::shared_ptr<ui::Slot> filler(std::string const& name = " ");

private:
    StationRef                        mRef;
    std::optional<game::ItemLocation> mSource;
    int                               mPage = 0;
};

/// Finds the first enabled upgrade with id `upgradeId` in `backpack`.
[[nodiscard]] std::optional<UpgradeContext> findInstalled(std::shared_ptr<game::Backpack> const& backpack, std::string_view upgradeId);

/// Short lore lines listing the items stored under `keys` (for `IUpgrade::describe`).
[[nodiscard]] std::vector<std::string> describeContents(Player& player, StationRef const& ref, std::vector<std::string> const& keys);

/// Extra ticks between closing one chest menu and opening the next (on top of `closeThen`'s delay).
inline constexpr int kSwitchExtraTicks = 12;

/// Closes the open chest menu (if any) and runs `openNext` once the client is ready for a new
/// container. The real client does not show a fake chest that replaces an open one directly or
/// right after a server-side close.
void switchMenu(Player& player, std::function<void(Player&)> openNext);
/// `switchMenu` to `menu`.
bool showMenu(Player& player, std::shared_ptr<ui::Menu> menu);

} // namespace abp::upgrade::anvil
