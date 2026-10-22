#pragma once

// Selftest support for the station native tabs (anvil, smithing): opens a backpack as a native
// screen for the simulated test player and sends tab actions like the client would.

#include <cstdint>
#include <memory>
#include <string>

#include "nlohmann/json.hpp"

#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/PlayerUISlot.h"
#include "mc/world/item/ItemStack.h"

#include "game/Backpack.h"
#include "net/Companion.h"
#include "net/NativeScreens.h"
#include "selftest/SelfTest.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"

namespace abp::upgrade::anvil::test {

/// The test player as a companion with the backpack open natively (closed again on destruction).
class NativeScope {
public:
    NativeScope(selftest::Context& ctx, Player& player, std::shared_ptr<game::Backpack> const& bp) : mPlayer(player) {
        net::setCompanion(player, net::CompanionInfo{"test", {"native_screen"}});
        net::setTestSink([](Player&, std::string_view, nlohmann::json const&) {});
        ctx.require(ui::BackpackMenu::openBackpack(player, bp), "open backpack");
        ui::MenuManager::get().forceShown(player);
        mSession = net::nativeSession(player);
        ctx.require(mSession != 0, "native session");
    }
    ~NativeScope() {
        ui::MenuManager::get().close(mPlayer);
        net::setTestSink(nullptr);
        net::setCompanion(mPlayer, std::nullopt);
    }
    NativeScope(NativeScope const&)            = delete;
    NativeScope& operator=(NativeScope const&) = delete;

    /// Sends a tab action of upgrade slot `slot`.
    bool tab(int slot, nlohmann::json data) {
        return net::handleScreenAction(
            mPlayer,
            nlohmann::json{{"session", mSession}, {"action", "tab"}, {"slot", slot}, {"data", std::move(data)}}
        );
    }
    /// The tab data of upgrade slot `slot` in the last model sent to the client.
    [[nodiscard]] nlohmann::json tabData(int slot) const {
        auto const* m = net::sentModel(mPlayer);
        if (!m || slot < 0 || slot >= static_cast<int>(m->upgrades.size())) return {};
        return m->upgrades[static_cast<std::size_t>(slot)].tab;
    }
    void setCursor(ItemStack const& stack) { mPlayer.setPlayerUIItem(PlayerUISlot::CursorSelected, stack, false); }
    [[nodiscard]] ItemStack cursor() const { return mPlayer.mPlayerUIContainer->getItem(0); }

private:
    Player&       mPlayer;
    std::uint32_t mSession = 0;
};

} // namespace abp::upgrade::anvil::test
