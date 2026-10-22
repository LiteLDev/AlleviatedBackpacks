// Calls `IUpgrade::onTick` for every enabled upgrade of every backpack each online player carries,
// every `GeneralConfig::upgradeTickInterval` ticks.

#include <algorithm>
#include <exception>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"

#include "game/PlayerScan.h"
#include "mod/Config.h"
#include "mod/GeneralConfig.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "upgrade/UpgradeManager.h"

namespace abp::game {

class PlayerTicker {
public:
    void install() {
        mListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>(
            [this](ll::event::ServerLevelTickEvent& ev) { tick(ev.level()); }
        );
    }
    void uninstall() { ll::event::EventBus::getInstance().removeListener(mListener); }

private:
    void tick(Level& level) {
        int interval = std::max(1, config::get<GeneralConfig>().upgradeTickInterval);
        if (++mTicks < interval) return;
        mTicks = 0;
        if (upgrade::all().empty()) return;
        level.forEachPlayer([&](Player& player) {
            if (player.isClientSide() || !player.isAlive() || player.isSpectator()) return true; // SB inventoryTick
            for (auto& [carried, bp] : carriedBackpacks(player, false, true)) {
                upgrade::forEachInstalled(bp, true, [&](upgrade::IUpgrade& u, upgrade::UpgradeContext const& ctx) {
                    if (!carried.upgradeActive(u.info().id, u.info().groupOrId())) return;
                    try {
                        u.onTick(upgrade::TickContext{ctx.withPlayer(&player), player, carried.where, interval, carried.depth});
                    } catch (std::exception const& e) {
                        log().error("upgrade {} onTick failed: {}", u.info().id, e.what());
                    }
                });
            }
            return true;
        });
    }

    ll::event::ListenerPtr mListener;
    int                    mTicks = 0;
};

} // namespace abp::game

ABP_REGISTER_MODULE_ORDERED(abp::game::PlayerTicker, abp::ModuleOrder::Upgrades + 1);
