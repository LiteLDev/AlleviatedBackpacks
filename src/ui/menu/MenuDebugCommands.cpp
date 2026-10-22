// `/backpack menudebug closeopen`: chest GUI diagnostics for real clients (only when
// `menu.json` `debugPackets` is on). Opens the carried backpack, closes it from the server two
// seconds later and opens it again a few ticks after that: the "server closes, then something opens
// a new chest right away" path (closeThen + open, tabs, forms) that the client must survive.
//
// `/backpack menudebug shutdownprobe [full]` (operators or console; e2e helper of the server
// scenario `shutdown.ps1`): spawns a simulated player "abp_shutdown_probe" carrying an iron backpack,
// opens it and puts 5 diamonds on its cursor (`full`: with a full inventory), so that stopping the
// server closes a menu with a cursor item (MenuManager's shutdown path must keep the item and must not
// spawn entities).

#include "ll/api/command/CommandHandle.h"
#include "ll/api/service/Bedrock.h"

#include "mc/legacy/ActorUniqueID.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/PlayerUISlot.h"
#include "mc/world/level/Level.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"

#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/Storage.h"
#include "mod/Commands.h"
#include "mod/Log.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuDebug.h"
#include "ui/menu/MenuManager.h"

namespace abp::ui {

namespace {
Player* findByKey(std::int64_t key) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    ActorUniqueID id;
    id.rawID = key;
    return level->getPlayer(id);
}

void shutdownProbe(CommandOrigin const& origin, CommandOutput& output, bool full) {
    if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors) return output.error("operators only");
    auto level = ll::service::getLevel();
    if (!level) return output.error("no level");
    auto spawn = level->getSharedSpawnPos();
    Vec3 at{static_cast<float>(spawn.x) + 0.5f, static_cast<float>(spawn.y) + 1.0f, static_cast<float>(spawn.z) + 0.5f};
    auto sp = SimulatedPlayer::create(full ? "abp_shutdown_probe_full" : "abp_shutdown_probe", at, 0);
    if (!sp) return output.error("could not spawn the probe player");
    auto bp = game::Storage::get().create(core::Tier::Iron);
    if (!bp) return output.error("no storage");
    auto& inv = sp->getInventory();
    inv.setItem(0, game::makeBackpackItem(core::Tier::Iron, bp->uuid()));
    if (full) {
        for (int i = 1; i < 36; ++i) inv.setItem(i, game::ItemConv::make("minecraft:dirt", 64));
    }
    if (!BackpackMenu::openCarried(*sp)) return output.error("could not open the backpack");
    sp->setPlayerUIItem(PlayerUISlot::CursorSelected, game::ItemConv::make("minecraft:diamond", 5), false);
    log().info("shutdown probe: ready player={} backpack={} full={}", sp->getRealName(), bp->uuid(), full);
    output.success("shutdown probe: ready backpack=" + bp->uuid());
}

void registerMenuDebug(ll::command::CommandHandle& cmd) {
    cmd.overload().text("menudebug").text("closeopen").execute([](CommandOrigin const& origin, CommandOutput& output) {
        auto* e = origin.getEntity();
        if (!e || !e->isPlayer()) return output.error("players only");
        if (!menuDebug()) return output.error("set debugPackets in config/menu.json first");
        auto& player = *static_cast<Player*>(e);
        auto  key    = player.getOrCreateUniqueID().rawID;
        if (!BackpackMenu::openCarried(player)) return output.error("no backpack carried");
        auto& mm = MenuManager::get();
        mm.runLater(40, [key, &mm] {
            auto* p = findByKey(key);
            if (!p) return;
            menuDebugLog("closeopen: server close");
            mm.close(*p);
            mm.runLater(4, [key] {
                if (auto* q = findByKey(key)) {
                    menuDebugLog("closeopen: open again");
                    BackpackMenu::openCarried(*q);
                }
            });
        });
        output.success("menudebug closeopen started");
    });

    for (bool full : {false, true}) {
        auto ov = cmd.overload();
        ov.text("menudebug").text("shutdownprobe");
        if (full) ov.text("full");
        ov.execute([full](CommandOrigin const& origin, CommandOutput& output) { shutdownProbe(origin, output, full); });
    }
}
} // namespace

} // namespace abp::ui

ABP_REGISTER_SUBCOMMAND(&abp::ui::registerMenuDebug);
