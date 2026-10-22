// `/backpack` (alias `/abp`):
//   /backpack open                 open the first carried backpack (chest, offhand, inventory)
//   /backpack give <tier>          (op) give yourself a backpack
//   /backpack selftest [glob]      (op / console) run selftests
//   /backpack reload               (op / console) reload configs and translations
//   /backpack info                 show the held backpack's uuid / tier / upgrades
//
// Feature units add sub-commands through `abp::commands::registerSubcommands` (see Commands.h).

#include "mod/Commands.h"

#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/command/ServerCommandRegisterEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"

#include "game/BackpackItem.h"
#include "game/PlayerScan.h"
#include "game/Storage.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "mod/Module.h"
#include "selftest/SelfTest.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"

namespace abp::commands {

// Command parameter types need external linkage (reflection), so no anonymous namespace here.
enum class TierName { leather, copper, iron, gold, diamond, netherite };

struct SelftestParams {
    std::string glob;
};
struct GiveParams {
    TierName tier;
};

namespace {

Player* playerOf(CommandOrigin const& origin) {
    auto* e = origin.getEntity();
    return (e && e->isPlayer()) ? static_cast<Player*>(e) : nullptr;
}

bool isAdmin(CommandOrigin const& origin) {
    return origin.getPermissionsLevel() >= CommandPermissionLevel::GameDirectors;
}

void registerAll() {
    if (!ll::service::getCommandRegistry()) return;
    auto& cmd = ll::command::CommandRegistrar::getServerInstance().getOrCreateCommand(
        "backpack",
        "AlleviatedBackpacks",
        CommandPermissionLevel::Any
    );
    cmd.alias("abp");

    cmd.overload().text("open").execute([](CommandOrigin const& origin, CommandOutput& output) {
        auto* player = playerOf(origin);
        if (!player) return output.error("Only players can open backpacks.");
        // First carried backpack: chest slot, offhand, then inventory (the Java mod's keybind).
        if (!ui::BackpackMenu::openCarried(*player)) {
            // Refused (no client mod): the presentation gate already told the player why.
            if (ui::MenuManager::get().lastOpenRefused(*player)) return;
            return output.error(trFor(*player, "abp.gui.open.none", "You are not carrying a backpack."));
        }
        output.success(trFor(*player, "abp.cmd.open.ok", "Backpack opened."));
    });

    cmd.overload<GiveParams>().text("give").required("tier").execute(
        [](CommandOrigin const& origin, CommandOutput& output, GiveParams const& params) {
            auto* player = playerOf(origin);
            if (!player) return output.error("Only players can receive items.");
            if (!isAdmin(origin)) return output.error("You need operator permission.");
            auto item = game::makeBackpackItem(static_cast<core::Tier>(params.tier));
            if (item.isNull()) return output.error("Backpack items are not registered (behavior pack missing?).");
            if (!player->addAndRefresh(item)) player->drop(item, false);
            output.success(trFor(*player, "abp.cmd.give.ok", "Gave a backpack."));
        }
    );

    auto selftest = [](CommandOrigin const& origin, CommandOutput& output, std::string const& glob) {
        if (!isAdmin(origin)) return output.error("You need operator permission.");
        std::int64_t key = 0;
        if (auto* p = playerOf(origin)) key = p->getOrCreateUniqueID().rawID;
        selftest::runAll(glob, [key](std::string const& line) {
            if (!key) return; // console: the log already prints every line
            auto level = ll::service::getLevel();
            if (!level) return;
            ActorUniqueID id;
            id.rawID = key;
            if (auto* p = level->getPlayer(id)) p->sendMessage(line);
        });
        output.success("SELFTEST started");
    };
    cmd.overload().text("selftest").execute([selftest](CommandOrigin const& origin, CommandOutput& output) {
        selftest(origin, output, "*");
    });
    cmd.overload<SelftestParams>().text("selftest").required("glob").execute(
        [selftest](CommandOrigin const& origin, CommandOutput& output, SelftestParams const& params) {
            selftest(origin, output, params.glob);
        }
    );

    cmd.overload().text("reload").execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (!isAdmin(origin)) return output.error("You need operator permission.");
        bool ok = config::loadAll();
        loadTranslations(Mod::getInstance().getSelf().getModDir() / "lang");
        if (ok) output.success("AlleviatedBackpacks configuration reloaded.");
        else output.error("Some config files failed to load; see the log.");
    });

    cmd.overload().text("info").execute([](CommandOrigin const& origin, CommandOutput& output) {
        auto* player = playerOf(origin);
        if (!player) return output.error("Only players can use this.");
        auto const& item = game::itemAt(*player, game::selectedSlot(*player));
        auto        uuid = game::backpackUuid(item);
        if (!game::isBackpack(item)) return output.error("Hold a backpack in your main hand.");
        if (!uuid) return output.success("This backpack has not been opened yet (no uuid).");
        auto bp = game::Storage::get().find(*uuid);
        if (!bp) return output.error("Unknown backpack " + *uuid);
        std::string ups;
        upgrade::forEachInstalled(bp, false, [&](upgrade::IUpgrade& u, upgrade::UpgradeContext const& ctx) {
            ups += (ups.empty() ? "" : ", ") + u.info().id + (ctx.state().enabled ? "" : " (off)");
        });
        auto items = bp->inventory().count([](core::ItemStackData const&) { return true; });
        auto line  = "uuid=" + *uuid + " tier=" + std::string(core::tierInfo(bp->tier()).id)
                  + " slots=" + std::to_string(bp->inventory().size()) + " items=" + std::to_string(items)
                  + " upgrades=[" + ups + "]";
        log().info("backpack info: {}", line);
        output.success(line);
    });

    for (auto const& fn : SubcommandRegistry::entries()) fn(cmd);
}
} // namespace

class CommandModule {
public:
    void install() {
        registerAll(); // BDS: the registry already exists when mods are enabled
        mListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::command::ServerCommandRegisterEvent>(
            [](auto&) { registerAll(); } // client: every local world
        );
    }
    void uninstall() { ll::event::EventBus::getInstance().removeListener(mListener); }

private:
    ll::event::ListenerPtr mListener;
};

} // namespace abp::commands

ABP_REGISTER_MODULE_ORDERED(abp::commands::CommandModule, abp::ModuleOrder::Commands);
