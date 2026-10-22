#include "upgrade/tank/FastTick.h"

#include <algorithm>
#include <exception>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"

#include "game/Backpack.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "upgrade/UpgradeManager.h"

namespace abp::upgrade::fasttick {

namespace {
struct Entry {
    std::int64_t                    player = 0;
    std::shared_ptr<game::Backpack> backpack;
    int                             slot = 0;
    std::string                     itemId;
    game::ItemLocation              where;
    int                             depth = 0;
    std::int64_t                    until = 0;
    TickFn                          fn;
};

std::int64_t                           gNow = 0;
std::unordered_map<std::string, Entry> gEntries; // server thread

bool stillInstalled(Entry const& e) {
    auto const& slots = e.backpack->data().upgrades;
    if (e.slot < 0 || e.slot >= static_cast<int>(slots.size())) return false;
    auto const& s = slots[static_cast<std::size_t>(e.slot)];
    return s.enabled && s.item.id == e.itemId;
}
} // namespace

std::int64_t now() { return gNow; }

void keepAlive(TickContext const& tick, std::string_view kind, TickFn fn) {
    auto const& ctx = tick.ctx;
    auto        id  = tick.player.getOrCreateUniqueID().rawID;
    auto        key = ctx.backpack().uuid() + ":" + std::to_string(ctx.slot()) + ":" + std::to_string(id) + ":" + std::string(kind);
    auto&       e   = gEntries[key];
    e.player        = id;
    e.backpack      = ctx.backpackPtr();
    e.slot          = ctx.slot();
    e.itemId        = ctx.state().item.id;
    e.where         = tick.where;
    e.depth         = tick.nestedDepth;
    e.until         = gNow + std::max(1, tick.interval) + 2;
    e.fn            = std::move(fn);
}

class FastTickDriver {
public:
    void install() {
        mListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>(
            [](ll::event::ServerLevelTickEvent& ev) { tick(ev.level()); }
        );
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mListener);
        gEntries.clear();
    }

private:
    static void tick(Level& level) {
        ++gNow;
        if (gEntries.empty()) return;
        // Copy the keys: a tick function may add entries (never remove others).
        std::vector<std::string> keys;
        keys.reserve(gEntries.size());
        for (auto const& [k, e] : gEntries) keys.push_back(k);
        for (auto const& k : keys) {
            auto it = gEntries.find(k);
            if (it == gEntries.end()) continue;
            auto& e = it->second;
            if (gNow > e.until || !e.backpack || !stillInstalled(e)) {
                gEntries.erase(it);
                continue;
            }
            auto* player = level.getPlayer(ActorUniqueID(e.player));
            if (!player || !player->isAlive() || player->isClientSide()) {
                gEntries.erase(it);
                continue;
            }
            auto fn = e.fn; // the entry may be refreshed (moved) while running
            try {
                fn(*player, UpgradeContext(e.backpack, e.slot, player), e.where, e.depth);
            } catch (std::exception const& ex) {
                log().error("upgrade fast tick failed: {}", ex.what());
            }
        }
    }

    ll::event::ListenerPtr mListener;
};

} // namespace abp::upgrade::fasttick

ABP_REGISTER_MODULE_ORDERED(abp::upgrade::fasttick::FastTickDriver, abp::ModuleOrder::Upgrades + 2);
