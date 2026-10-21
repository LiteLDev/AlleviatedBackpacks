// `/backpack menudebug closeopen`: chest GUI diagnostics for real clients (only when
// `menu.json` `debugPackets` is on). Opens the carried backpack, closes it from the server two
// seconds later and opens it again a few ticks after that: the "server closes, then something opens
// a new chest right away" path (closeThen + open, tabs, forms) that the client must survive.

#include "ll/api/command/CommandHandle.h"
#include "ll/api/service/Bedrock.h"

#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/level/Level.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"

#include "mod/Commands.h"
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
}
} // namespace

} // namespace abp::ui

ABP_REGISTER_SUBCOMMAND(&abp::ui::registerMenuDebug);
