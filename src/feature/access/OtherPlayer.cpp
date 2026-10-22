#include "feature/access/OtherPlayer.h"

#include <cmath>
#include <numbers>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"

#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/legacy/ActorRuntimeID.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"
#include "mc/world/phys/AABB.h"

#include "feature/admin/SbConfig.h"
#include "feature/gui/Gui.h"
#include "game/Backpack.h"
#include "game/BackpackItem.h"
#include "game/PlayerScan.h"
#include "game/Storage.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "net/NativeScreens.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/deposit/ContainerTransfer.h"

namespace abp::access {

namespace {

constexpr float kMaxReach      = 6.0f; ///< eye -> hit (lenient: the client already ray-traced)
constexpr float kMaxViewDistSq = 8.0f * 8.0f;

game::ItemLocation chestSlot() {
    return {game::ItemLocation::Kind::Armor, static_cast<int>(SharedTypes::Legacy::ArmorSlot::Torso)};
}

float radians(float deg) { return deg * std::numbers::pi_v<float> / 180.0f; }

/// Viewers of another player's backpack: their menu closes when the target takes the backpack
/// off or walks away (SB's AnotherPlayer backpack context does the same through its slot).
struct Viewer {
    std::int64_t viewer;
    std::int64_t target;
    std::string  uuid;
};
std::vector<Viewer>& viewers() {
    static std::vector<Viewer> v;
    return v;
}

} // namespace

std::string_view resultName(OpenResult r) {
    switch (r) {
    case OpenResult::Opened:
        return "opened";
    case OpenResult::Disabled:
        return "disabled";
    case OpenResult::NotPlayer:
        return "not_player";
    case OpenResult::TooFar:
        return "too_far";
    case OpenResult::NotBody:
        return "not_body";
    case OpenResult::NotBack:
        return "not_back";
    case OpenResult::NoBackpack:
        return "no_backpack";
    case OpenResult::NotAllowed:
        return "not_allowed";
    case OpenResult::Refused:
        return "refused";
    }
    return "unknown";
}

bool hitsBack(Player& source, Player& target, Vec3 const& hit) {
    // SB: hitVector (local to the target's position) y in [0.9, 1.6) ...
    auto  feet  = target.getFeetPos();
    float localY = hit.y - feet.y;
    bool  body   = localY >= 0.9f && localY < 1.6f;
    if (!body) return false;
    // ... and the direction source -> target (flattened) agrees with the target's body direction.
    auto  from = source.getFeetPos();
    Vec3  to{feet.x - from.x, feet.y - from.y, feet.z - from.z};
    float len = std::sqrt(to.x * to.x + to.y * to.y + to.z * to.z);
    if (len < 1e-4f) return false;
    to.x /= len;
    to.z /= len;
    float pitch = radians(target.getRotation().x);
    float yaw   = radians(target.getYBodyRotation());
    Vec3  view{-std::sin(yaw) * std::cos(pitch), -std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
    return to.x * view.x + to.z * view.z > 0.0f;
}

bool anotherPlayerCanOpen(Player& target, game::Backpack const& backpack) {
    std::string key(kAnotherPlayerCanOpen);
    // The backpack's override lives in the GUI settings section (`gui::setBackpackOption`).
    auto const& settings = backpack.data().settings;
    if (settings.is_object()) {
        if (auto gui = settings.find("gui"); gui != settings.end() && gui->is_object()) {
            if (auto it = gui->find(key); it != gui->end() && it->is_boolean()) return it->get<bool>();
        }
    }
    auto global = feature::gui::playerOverrides(target);
    if (global.is_object()) {
        if (auto it = global.find(key); it != global.end() && it->is_boolean()) return it->get<bool>();
    }
    return true; // SB default
}

OpenResult tryOpen(Player& source, Player& target, Vec3 const& hit) {
    if (!feature::gui::otherPlayerOpeningAllowed()) return OpenResult::Disabled;
    if (&source == &target || target.getDimensionId() != source.getDimensionId() || !target.isAlive()) return OpenResult::NotPlayer;
    auto const& box = target.getAABB();
    bool        inBox = hit.x >= box.min.x - 0.5f && hit.x <= box.max.x + 0.5f && hit.y >= box.min.y - 0.5f && hit.y <= box.max.y + 0.5f
              && hit.z >= box.min.z - 0.5f && hit.z <= box.max.z + 0.5f;
    if (!inBox || source.getPosition().distanceTo(hit) > kMaxReach) return OpenResult::TooFar;
    auto feet   = target.getFeetPos();
    float localY = hit.y - feet.y;
    if (!(localY >= 0.9f && localY < 1.6f)) return OpenResult::NotBody;
    if (!hitsBack(source, target, hit)) return OpenResult::NotBack;

    ItemStack worn = game::itemAt(target, chestSlot());
    if (!game::isBackpack(worn)) return OpenResult::NoBackpack;
    bool changed = false;
    auto bp      = game::Storage::get().forItem(worn, &changed);
    if (!bp) return OpenResult::NoBackpack;
    if (changed) game::setItemAt(target, chestSlot(), worn);
    if (!anotherPlayerCanOpen(target, *bp)) {
        upgrade::transfer::sendTip(
            source,
            trFor(source, "abp.access.cannot_open", "This backpack cannot be open by another player")
        );
        return OpenResult::NotAllowed;
    }
    if (!ui::BackpackMenu::openBackpack(source, bp)) return OpenResult::Refused;
    viewers().push_back({source.getOrCreateUniqueID().rawID, target.getOrCreateUniqueID().rawID, bp->uuid()});
    return OpenResult::Opened;
}

namespace {

void checkViewers(Level& level) {
    auto& list = viewers();
    if (list.empty()) return;
    auto& mm = ui::MenuManager::get();
    std::erase_if(list, [&](Viewer const& v) {
        ActorUniqueID vid, tid;
        vid.rawID    = v.viewer;
        tid.rawID    = v.target;
        auto* viewer = level.getPlayer(vid);
        if (!viewer) return true;
        auto current = mm.current(*viewer);
        if (!current) return true; // closed
        // Another backpack opened: done. Any other menu (an upgrade's filter, settings) belongs to
        // the viewed backpack, keep watching.
        auto menu = std::dynamic_pointer_cast<ui::BackpackMenu>(current);
        if (menu && menu->backpack() && menu->backpack()->uuid() != v.uuid) return true;
        auto* target = level.getPlayer(tid);
        bool  keep   = target && target->isAlive() && target->getDimensionId() == viewer->getDimensionId()
                  && game::backpackUuid(game::itemAt(*target, chestSlot())) == v.uuid
                  && target->getPosition().distanceToSqr(viewer->getPosition()) <= kMaxViewDistSq;
        if (keep) return false;
        mm.close(*viewer);
        return true;
    });
}

class OtherPlayerModule {
public:
    void install() {
        mListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>(
            [](ll::event::ServerLevelTickEvent& ev) { checkViewers(ev.level()); }
        );
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mListener);
        viewers().clear();
    }

private:
    ll::event::ListenerPtr mListener;
};

bool onKeyAction(Player& player, nlohmann::json const& body) {
    if (!body.contains("target") || !body["target"].is_number_unsigned() && !body["target"].is_number_integer()) return false;
    auto hitJson = body.value("hit", nlohmann::json::array());
    if (!hitJson.is_array() || hitJson.size() != 3 || !hitJson[0].is_number() || !hitJson[1].is_number() || !hitJson[2].is_number()) {
        return false;
    }
    Vec3 hit{hitJson[0].get<float>(), hitJson[1].get<float>(), hitJson[2].get<float>()};
    ActorRuntimeID rid;
    rid.rawID   = body["target"].get<std::uint64_t>();
    auto* actor = player.getLevel().getRuntimeEntity(rid, false);
    if (!actor || !actor->isPlayer()) return false;
    auto& target = static_cast<Player&>(*actor);
    auto  result = tryOpen(player, target, hit);
    log().info("access: {} -> {}'s backpack: {}", player.getRealName(), target.getRealName(), resultName(result));
    return result == OpenResult::Opened;
}

} // namespace
} // namespace abp::access

ABP_REGISTER_MODULE(abp::access::OtherPlayerModule);
// SB's backpack settings toggle (BackpackMainSettingsTab: ANOTHER_PLAYER_CAN_OPEN), stored as the
// backpack's GUI option; hidden in effect when the server disallows opening (it is never asked).
ABP_REGISTER_BACKPACK_SETTING(abp::ui::BackpackSetting{
    .id    = "anotherPlayerCanOpen",
    .order = 60,
    .label =
        [](Player& player, abp::game::Backpack const& bp) {
            bool on = abp::access::anotherPlayerCanOpen(player, bp);
            return on ? abp::trFor(player, "abp.access.setting.on", "Another player can open")
                      : abp::trFor(player, "abp.access.setting.off", "Another player can NOT open");
        },
    .onSelect =
        [](Player& player, std::shared_ptr<abp::game::Backpack> bp) {
            bool on = abp::access::anotherPlayerCanOpen(player, *bp);
            abp::feature::gui::setBackpackOption(*bp, std::string(abp::access::kAnotherPlayerCanOpen), !on);
        },
});
ABP_REGISTER_KEY_ACTION(abp::net::KeyActionExtension{"openOtherPlayer", &abp::access::onKeyAction});
