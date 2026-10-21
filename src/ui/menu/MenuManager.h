#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "ll/api/event/ListenerBase.h"

#include "ui/menu/Menu.h"

class Player;

namespace abp::ui {

/// Shows `Menu`s as fake chest GUIs and keeps them server-authoritative.
///
/// How it works: a chest block (two for 54 slots) is sent to the player only, at a spot above
/// their head, with a block-actor update carrying the title (and the double-chest pairing).
/// A `ContainerOpenPacket` opens it and the slot displays are sent as inventory content.
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
    /// Closes the player's menu, if any.
    void close(Player& player);
    /// Closes and opens the same menu again (applies a changed title).
    void reopen(Player& player);
    /// Closes the menu, then runs `fn` a few ticks later (once the client screen is gone). Use it
    /// to show a form from a menu button.
    void closeThen(Player& player, std::function<void(Player&)> fn);

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

    struct Session;

    // Internal (packet hooks).
    bool handleRequestPacket(Player& player, void const* packet);
    bool handleClosePacket(Player& player, int containerId);

private:
    void tick();
    void finish(Player* player, std::int64_t key, bool sendClose);
    void runDeferred();
    void sync(Player& player, Session& s, bool full);
    void after(int ticks, std::function<void()> fn);

    struct Delayed {
        int                   ticks;
        std::function<void()> fn;
    };

    std::unordered_map<std::int64_t, std::unique_ptr<Session>> mSessions;
    std::unordered_map<std::int64_t, int>                      mClosedIds;
    std::vector<ll::event::ListenerPtr>                        mListeners;
    std::vector<std::function<void()>>                         mDeferred;
    std::vector<Delayed>                                       mDelayed;
    bool                                                       mBusy = false;
};

} // namespace abp::ui
