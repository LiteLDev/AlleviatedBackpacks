// Server side of the Shift contents tooltip (SC `RequestBackpackInventoryContentsPayload`) and
// of the render info sync.

#include <chrono>
#include <unordered_map>

#include "mc/world/actor/player/Player.h"

#include "core/screen/CountFormat.h"
#include "core/screen/ScreenModel.h"
#include "game/Backpack.h"
#include "game/BackpackItem.h"
#include "game/PlayerScan.h"
#include "game/Storage.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "net/Companion.h"
#include "net/NativeScreens.h"
#include "net/Packets.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/stack/StackUpgrade.h"
#include "upgrade/tank/Tank.h"

namespace abp::net {

namespace {
using Clock = std::chrono::steady_clock;

/// The client asks at most every 20 ticks per backpack (SC REFRESH_INTERVAL); the server drops
/// anything faster than that (minus some slack for tick jitter) and caps the overall rate.
constexpr auto kPerBackpack = std::chrono::milliseconds(800);
constexpr int  kPerSecond   = 20;

struct RateState {
    std::unordered_map<std::string, Clock::time_point> last;
    Clock::time_point                                   windowStart{};
    int                                                 inWindow = 0;
};
std::unordered_map<std::int64_t, RateState> gRates;

bool allowed(Player& player, std::string const& uuid) {
    auto& r   = gRates[player.getOrCreateUniqueID().rawID];
    auto  now = Clock::now();
    if (now - r.windowStart > std::chrono::seconds(1)) {
        r.windowStart = now;
        r.inWindow    = 0;
    }
    if (++r.inWindow > kPerSecond) return false;
    auto it = r.last.find(uuid);
    if (it != r.last.end() && now - it->second < kPerBackpack) return false;
    r.last[uuid] = now;
    if (r.last.size() > 128) r.last.clear();
    return true;
}

/// Only backpacks the player can see: carried (also nested ones), the open backpack screen's
/// backpack and the backpacks stored in it. Nobody can read another player's backpack by uuid.
bool canSee(Player& player, std::string const& uuid) {
    for (auto const& [carried, bp] : game::carriedBackpacks(player, false, true)) {
        if (carried.uuid && *carried.uuid == uuid) return true;
    }
    auto menu = ui::MenuManager::get().current(player);
    auto* bm  = menu ? dynamic_cast<ui::BackpackMenu*>(menu.get()) : nullptr;
    if (!bm) return false;
    auto& bp = *bm->backpack();
    if (bp.uuid() == uuid) return true;
    auto& inv = bp.inventory();
    for (int i = 0; i < inv.size(); ++i) {
        auto const& d = inv.at(i);
        if (!d.empty() && game::isBackpack(d) && game::backpackUuid(d) == uuid) return true;
    }
    return false;
}

std::shared_ptr<game::Backpack> lookup(std::string const& uuid) {
    auto& storage = game::Storage::get();
    if (uuid.empty() || uuid.size() > 64 || !storage.available()) return nullptr;
    return storage.find(uuid);
}
} // namespace

void detail::forgetContentsRates(Player const& player) { gRates.erase(player.getOrCreateUniqueID().rawID); }

core::screen::ContentsModel buildContents(Player& player, std::shared_ptr<game::Backpack> const& backpack) {
    core::screen::ContentsModel m;
    if (!backpack) return m;
    m.uuid  = backpack->uuid();
    m.known = true;
    double mult = upgrade::stack::totalMultiplier(*backpack);
    if (mult > 1) {
        m.lines.push_back(trFor(
            player,
            "abp.companion.tooltip.stack_multiplier",
            "§aStack multiplier: §f×{}",
            core::screen::formatMultiplier(mult)
        ));
    }
    for (auto const& ctx : upgrade::tank::tanks(backpack)) {
        auto tank = upgrade::tank::read(ctx);
        if (tank.empty()) {
            m.lines.push_back("§9" + trFor(player, "abp.companion.tooltip.fluid_empty", "Empty tank"));
        } else {
            m.lines.push_back(trFor(
                player,
                "abp.companion.tooltip.fluid",
                "§f{} mB §9{}",
                core::screen::abbreviateCount(tank.amount()),
                upgrade::tank::fluidName(&player, tank.fluid())
            ));
        }
    }
    for (auto const& provider : ContentsLineRegistry::entries()) {
        if (provider) provider(player, *backpack, m.lines);
    }
    for (auto const& s : backpack->data().upgrades) {
        if (s.empty()) continue;
        core::screen::ContentsModel::Upgrade u;
        u.item    = s.item;
        u.enabled = s.enabled;
        if (auto* type = upgrade::upgradeFor(s.item)) u.toggleable = type->info().toggleable;
        m.upgrades.push_back(std::move(u));
    }
    m.items = core::screen::compactSortedByCount(backpack->inventory());
    return m;
}

core::screen::RenderInfo buildRenderInfo(game::Backpack const& backpack) {
    core::screen::RenderInfo r;
    r.uuid        = backpack.uuid();
    r.tier        = static_cast<int>(backpack.tier());
    r.mainColor   = backpack.data().mainColor;
    r.accentColor = backpack.data().accentColor;
    for (auto const& s : backpack.data().upgrades) {
        if (s.empty() || !s.enabled) continue;
        if (auto* type = upgrade::upgradeFor(s.item)) r.upgrades.push_back(type->info().id);
    }
    // Providers only read (the backpack's capacities need non-const accessors).
    for (auto const& provider : RenderInfoRegistry::entries()) {
        if (provider) provider(const_cast<game::Backpack&>(backpack), r.extra);
    }
    return r;
}

class ContentsRequestHandler : public ll::network::PacketHandlerBase<ContentsRequestHandler, ContentsRequestPacket> {
public:
    void handlePacket(NetworkIdentifier const& source, NetEventCallback& callback, ContentsRequestPacket const& packet) const {
        auto* player = senderOf(source, callback, packet.mSenderSubId);
        if (!player || !packet.valid() || !hasCompanion(*player)) return;
        auto uuid = packet.body.value("uuid", std::string{});
        if (!allowed(*player, uuid)) return;
        try {
            auto bp = canSee(*player, uuid) ? lookup(uuid) : nullptr;
            auto m  = bp ? buildContents(*player, bp) : core::screen::ContentsModel{};
            if (!bp) m.uuid = uuid;
            send(*player, ContentsPacket{m.toJson()});
            if (bp) send(*player, RenderInfoPacket{buildRenderInfo(*bp).toJson()});
        } catch (std::exception const& e) {
            log().error("contents request failed: {}", e.what());
        }
    }
};

class RenderInfoRequestHandler : public ll::network::PacketHandlerBase<RenderInfoRequestHandler, RenderInfoRequestPacket> {
public:
    void handlePacket(NetworkIdentifier const& source, NetEventCallback& callback, RenderInfoRequestPacket const& packet) const {
        auto* player = senderOf(source, callback, packet.mSenderSubId);
        if (!player || !packet.valid() || !hasCompanion(*player)) return;
        auto uuid = packet.body.value("uuid", std::string{});
        if (!allowed(*player, "render:" + uuid)) return;
        // Render info is appearance only (tier, colours, enabled upgrade ids): other players'
        // worn / placed backpacks need it too, so it isn't limited to visible contents.
        if (auto bp = lookup(uuid)) send(*player, RenderInfoPacket{buildRenderInfo(*bp).toJson()});
    }
};

class KeyActionHandler : public ll::network::PacketHandlerBase<KeyActionHandler, KeyActionPacket> {
public:
    void handlePacket(NetworkIdentifier const& source, NetEventCallback& callback, KeyActionPacket const& packet) const {
        auto* player = senderOf(source, callback, packet.mSenderSubId);
        if (!player || !packet.valid() || !hasCompanion(*player)) return;
        try {
            handleKeyAction(*player, packet.body);
        } catch (std::exception const& e) {
            log().error("key action failed: {}", e.what());
        }
    }
};

} // namespace abp::net
