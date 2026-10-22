// `/backpack world target` / `/backpack world untarget` (operators; e2e helper of the client
// scenario `world.ps1`, like `/backpack companion setup`): a simulated player "abp_e2e_target"
// stands 2.5 blocks in front of the caller with its back to them, wearing an iron backpack, and the
// caller's view is turned to its back. `untarget` removes the player and its backpack's storage.

#include <cmath>
#include <numbers>
#include <optional>

#include "ll/api/command/CommandHandle.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"

#include "feature/admin/AccessLog.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/Storage.h"
#include "mod/Commands.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace abp::access {

namespace {

constexpr std::string_view kTargetName = "abp_e2e_target";

struct Target {
    std::int64_t id;
    std::string  backpack;
    float        yaw;
};
std::optional<Target>& target() {
    static std::optional<Target> t;
    return t;
}

SimulatedPlayer* findTarget(Level& level) {
    auto& t = target();
    if (!t) return nullptr;
    ActorUniqueID id;
    id.rawID = t->id;
    auto* p  = level.getPlayer(id);
    return p && p->isSimulatedPlayer() ? static_cast<SimulatedPlayer*>(p) : nullptr;
}

void removeTarget(Level& level) {
    auto& t = target();
    if (!t) return;
    if (auto* sp = findTarget(level)) sp->simulateDisconnect();
    if (!t->backpack.empty()) {
        game::Storage::get().erase(t->backpack);
        admin::AccessLog::get().erase(t->backpack);
    }
    t.reset();
}

bool operatorOnly(CommandOrigin const& origin, CommandOutput& output) {
    if (origin.getPermissionsLevel() >= CommandPermissionLevel::GameDirectors) return true;
    output.error("operators only");
    return false;
}

class E2eTargetModule {
public:
    void install() {
        // Simulated players settle their body yaw over the first ticks: keep it facing away.
        mListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>(
            [](ll::event::ServerLevelTickEvent& ev) {
                if (auto* sp = findTarget(ev.level())) sp->setYBodyRotation(target()->yaw);
            }
        );
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mListener);
        if (auto level = ll::service::getLevel()) removeTarget(*level);
    }

private:
    ll::event::ListenerPtr mListener;
};

} // namespace
} // namespace abp::access

ABP_REGISTER_MODULE(abp::access::E2eTargetModule);

ABP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    using namespace abp;
    using namespace abp::access;
    cmd.overload().text("world").text("target").execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (!operatorOnly(origin, output)) return;
        auto* e = origin.getEntity();
        if (!e || !e->isPlayer()) return output.error("players only");
        auto& player = static_cast<Player&>(*e);
        auto& level  = player.getLevel();
        removeTarget(level); // one target at a time
        float yaw  = player.getRotation().y;
        float rad  = yaw * std::numbers::pi_v<float> / 180.0f;
        auto  feet = player.getFeetPos();
        float x    = feet.x - std::sin(rad) * 2.5f;
        float z    = feet.z + std::cos(rad) * 2.5f;
        // On the ground at that spot (the e2e world is flat; never inside a block).
        auto  y = player.getDimensionBlockSource().getAboveTopSolidBlock(
            static_cast<int>(std::floor(x)),
            static_cast<int>(std::floor(z)),
            false,
            false
        );
        Vec3  at{x, std::max(feet.y, static_cast<float>(y)), z};
        auto  sp = SimulatedPlayer::create(std::string(kTargetName), at, player.getDimensionId(), Vec2{0.0f, yaw});
        if (!sp) return output.error("could not spawn the target");
        sp->setYBodyRotation(yaw);
        auto bp = game::Storage::get().create(core::Tier::Iron);
        if (!bp) return output.error("no storage");
        bp->inventory().set(0, game::ItemConv::toData(game::ItemConv::make("minecraft:emerald", 7)));
        bp->data().mainColor   = 0xFF3C44AA;
        bp->data().accentColor = 0xFFFED83D;
        bp->markDirty();
        sp->setArmor(SharedTypes::Legacy::ArmorSlot::Torso, game::makeBackpackItem(core::Tier::Iron, bp->uuid()));
        target() = Target{sp->getOrCreateUniqueID().rawID, bp->uuid(), yaw};
        // Look at the target's back, at body height (eye 1.62, body 0.9..1.6, 2.5 blocks away).
        player.teleport(feet, player.getDimensionId(), Vec2{8.5f, yaw});
        log().info("world target: spawned {} wearing backpack {}", kTargetName, bp->uuid());
        output.success("world target: spawned " + std::string(kTargetName) + " wearing backpack " + bp->uuid());
    });
    cmd.overload().text("world").text("untarget").execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (!operatorOnly(origin, output)) return;
        auto level = ll::service::getLevel();
        if (!level) return output.error("no level");
        removeTarget(*level);
        output.success("world target removed");
    });
});
