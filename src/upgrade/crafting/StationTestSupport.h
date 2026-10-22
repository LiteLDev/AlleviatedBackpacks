#pragma once

// Selftest helpers of the station upgrades (crafting, stonecutter, cooking): drive the native
// tab exactly like the client companion does (`ScreenActionPacket` bodies through
// `net::handleScreenAction`) on a simulated player marked as a companion.

#include <memory>
#include <string>

#include "nlohmann/json.hpp"

#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "game/Backpack.h"
#include "game/ItemConv.h"
#include "net/Companion.h"
#include "net/NativeScreens.h"
#include "selftest/SelfTest.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"

namespace abp::upgrade::crafting::test {

/// The backpack's native screen opened for `player` (a companion while this lives).
class NativeScreen {
public:
    NativeScreen(selftest::Context& ctx, Player& player, std::shared_ptr<game::Backpack> const& backpack) : mPlayer(player) {
        net::setCompanion(player, net::CompanionInfo{"test", {"native_screen"}});
        net::setTestSink([](Player&, std::string_view, nlohmann::json const&) {});
        ctx.require(ui::BackpackMenu::openBackpack(player, backpack), "open the backpack screen");
        ui::MenuManager::get().forceShown(player);
        mSession = net::nativeSession(player);
        ctx.require(mSession != 0, "native session");
    }
    ~NativeScreen() {
        ui::MenuManager::get().close(mPlayer);
        net::setTestSink(nullptr);
        net::setCompanion(mPlayer, std::nullopt);
    }
    NativeScreen(NativeScreen const&)            = delete;
    NativeScreen& operator=(NativeScreen const&) = delete;

    /// `{"action":"tab","slot":slot,"data":data}`.
    bool tab(int slot, nlohmann::json data) const {
        return net::handleScreenAction(mPlayer, {{"session", mSession}, {"action", "tab"}, {"slot", slot}, {"data", std::move(data)}});
    }
    /// Any screen action (`session` is added).
    bool action(nlohmann::json body) const {
        body["session"] = mSession;
        return net::handleScreenAction(mPlayer, body);
    }
    /// The tab data the client would get for upgrade slot `slot` now.
    [[nodiscard]] nlohmann::json tabData(int slot) const {
        auto menu = ui::MenuManager::get().current(mPlayer);
        auto* bm  = menu ? dynamic_cast<ui::BackpackMenu*>(menu.get()) : nullptr;
        if (!bm) return nlohmann::json::object();
        auto m = net::buildScreenModel(mPlayer, *bm);
        if (slot < 0 || slot >= static_cast<int>(m.upgrades.size())) return nlohmann::json::object();
        return m.upgrades[static_cast<std::size_t>(slot)].tab;
    }
    /// Reports the tab of upgrade slot `slot` as open (the client does it when the tab opens).
    void openTab(int slot, game::Backpack const& backpack) const { tab(slot, {{"tabOpen", true}, {"uuid", backpack.uuid()}}); }

    [[nodiscard]] std::uint32_t session() const { return mSession; }

private:
    Player&       mPlayer;
    std::uint32_t mSession = 0;
};

inline ItemStack cursor(Player& p) { return p.mPlayerUIContainer->getItem(0); }

inline void setCursor(Player& p, ItemStack const& s) {
    p.mPlayerUIContainer->setItem(0, s);
}

inline int countOnPlayer(Player& p, std::string_view id) {
    int n = 0;
    for (int i = 0; i < 36; ++i) {
        auto const& it = p.getInventory().getItem(i);
        if (!it.isNull() && it.getTypeName() == id) n += it.mCount;
    }
    return n;
}

inline int countIn(game::Backpack& bp, std::string_view id) {
    return static_cast<int>(bp.inventory().count([&](core::ItemStackData const& s) { return s.id == id; }));
}

inline core::ItemStackData data(std::string_view id, int count) {
    return game::ItemConv::toData(game::ItemConv::make(id, std::min(count, 64))).withCount(count);
}

inline void clearPlayer(Player& p) {
    for (int i = 0; i < 36; ++i) p.getInventory().setItem(i, ItemStack::EMPTY_ITEM());
    setCursor(p, ItemStack::EMPTY_ITEM());
}

} // namespace abp::upgrade::crafting::test
