#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "ll/api/event/ListenerBase.h"

#include "mc/world/level/BlockPos.h"

#include "ui/menu/Menu.h"

class Player;

namespace abp::ui {

/// Shows `Menu`s as fake chest GUIs and keeps them server-authoritative.
///
/// How it works: a chest block (two for 54 slots) is sent to the player only, 3-4 blocks above
/// their feet at a spot no fake chest used in the last seconds, followed by a block-actor update
/// carrying the title (and the double-chest pairing). A few ticks later a `ContainerOpenPacket`
/// opens it and the slot displays are sent as inventory content. Replacing a shown menu closes it
/// with a server-initiated `ContainerClose`; the next chest is only placed once the client has
/// answered that close (which is confirmed, as every close of ours is), because the client drops
/// a `ContainerOpen` while it is still closing a screen. See AGENTS.md "Fake chest GUI rules".
/// While a menu is open every `ItemStackRequestPacket` of that player is handled here (never by
/// vanilla): items move through `Slot::take/accept`, the player's inventory and cursor are
/// edited on the server, a response is sent and all touched containers are re-synced. Closing
/// (by the player, `close()`, death, disconnect, teleport/dimension change, mod disable)
/// restores the real blocks and returns the cursor item to the player.
///
/// Thread: server thread only.
class MenuManager {
public:
    static MenuManager& get();

    void install();
    void uninstall();

    /// Opens `menu`, replacing any open menu (which gets `onClose`). Returns false if the player
    /// can't open a menu (not a server player, dead).
    bool open(Player& player, std::shared_ptr<Menu> menu);
    /// Same as `open` (kept for callers that switch between menus from inside a menu): switching
    /// is reliable on a real client and shows the new menu's title.
    bool swap(Player& player, std::shared_ptr<Menu> menu) { return open(player, std::move(menu)); }
    /// Closes the player's menu, if any.
    void close(Player& player);
    /// Closes every open menu (world stopping, mod disable).
    void closeAll();
    /// Closes and opens the same menu again (applies a changed title).
    void reopen(Player& player);
    /// Closes the menu, then runs `fn` a few ticks later (once the client screen is gone). Use it
    /// to show a form from a menu button.
    void closeThen(Player& player, std::function<void(Player&)> fn);

    /// Runs `fn` on the server thread `ticks` ticks from now (dropped when the world stops).
    void runLater(int ticks, std::function<void()> fn) { after(ticks, std::move(fn)); }

    [[nodiscard]] std::shared_ptr<Menu> current(Player const& player) const;
    /// True once the chest screen has been opened on the client (after the open delay).
    [[nodiscard]] bool                  isShown(Player const& player) const;
    [[nodiscard]] std::size_t           sessionCount() const { return mSessions.size(); }

    // ---- simulation (selftests, no client needed) ------------------------------------------
    struct SlotAddress {
        enum class Where { Menu, Inventory, Cursor, Offhand, Armor };
        Where where = Where::Menu;
        int   index = 0;
    };
    enum class ActionType { Take, Place, Swap, Drop, Destroy };
    struct SimAction {
        ActionType  type = ActionType::Take;
        SlotAddress src;
        SlotAddress dst;
        int         amount = 1;
    };
    /// Runs one item-stack action exactly like a client request would (requires an open menu).
    /// Returns whether it succeeded.
    bool simulate(Player& player, SimAction const& action);
    /// Pretends the client finished opening (skips the open delay; selftests).
    void forceShown(Player& player);
    /// The item displayed in a menu slot as last synced to the client.
    [[nodiscard]] ItemStack shownItem(Player const& player, int slot) const;

    /// Like `simulate`, but every `Where::Menu` address refers to `menuSlot` instead of the menu's
    /// own slot at that index. Native screens use it for slots the chest page does not show (a
    /// storage slot on another page, an upgrade slot while the storage view is shown): the action
    /// goes through exactly the same rules (locked slots, `Slot::click/take/accept`) as a click in
    /// the chest. Requires an open menu. Does not re-sync the player (see `resyncPlayer`).
    bool simulateOn(Player& player, SimAction const& action, std::shared_ptr<Slot> menuSlot);
    /// Sends the player's inventory and cursor to the client (after `simulate`/`simulateOn`).
    void resyncPlayer(Player& player);
    /// Container id of the player's open menu (-1 if none).
    [[nodiscard]] int containerIdOf(Player const& player) const;

    // ---- observers (additive hook: native renderers such as the client companion) ------------
    struct Observer {
        /// Right before the client is told to open `menu` (`containerId` = the screen's id). Also
        /// runs for `forceShown`.
        std::function<void(Player&, Menu&, int containerId)> beforeShow;
        /// Every server tick after the shown `menu` was ticked and synced.
        std::function<void(Player&, Menu&)> afterTick;
        /// The menu's session ended (closed, replaced, player gone: then `player` is null).
        std::function<void(Player*, Menu&)> closed;
    };
    /// Registers an observer; returns an id for `removeObserver`.
    int  addObserver(Observer observer);
    void removeObserver(int id);

    struct Session;

    // Internal (packet hooks).
    bool handleRequestPacket(Player& player, void const* packet);
    /// `containerType`: the type the client sent (echoed in the confirmation), -1 = chest.
    bool handleClosePacket(Player& player, int containerId, int containerType = -1);

private:
    void tick();
    void tickPendingCloses();
    void finish(Player* player, std::int64_t key, bool sendClose);
    void runDeferred();
    void sync(Player& player, Session& s, bool full);
    void after(int ticks, std::function<void()> fn);
    void place(Player& player, Session& s);
    [[nodiscard]] bool awaitingClose(std::int64_t key) const;
    std::vector<BlockPos> pickFakePositions(Player& player, bool twoBlocks);

    struct Delayed {
        int                   ticks;
        std::function<void()> fn;
    };
    /// A screen the server closed; the client's answering ContainerClose is still due.
    struct PendingClose {
        int                                       id;
        int                                       age = 0;
        std::vector<std::function<void(Player&)>> then; ///< `closeThen` callbacks
    };
    struct RecentFake {
        BlockPos     pos;
        std::int64_t until;
    };

    std::unordered_map<std::int64_t, std::unique_ptr<Session>>    mSessions;
    std::unordered_map<std::int64_t, std::vector<PendingClose>> mPendingCloses;
    std::unordered_map<std::int64_t, std::vector<RecentFake>>   mRecentFake;
    std::int64_t                                                mTick = 0;
    std::vector<ll::event::ListenerPtr>                        mListeners;
    std::vector<std::function<void()>>                         mDeferred;
    std::vector<Delayed>                                       mDelayed;
    bool                                                       mBusy = false;
    std::vector<std::pair<int, Observer>>                      mObservers;
    int                                                        mNextObserver = 1;

    void notifyBeforeShow(Player& player, Session& s);
};

} // namespace abp::ui
