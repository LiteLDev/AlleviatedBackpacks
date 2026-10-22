// `/sbp` (alias `/sophisticatedbackpacks`), Sophisticated Backpacks' admin command (SBPCommand):
//
//   /sbp list [playerName]                          access log, newest first
//   /sbp give <targets> <backpackUuid>              a backpack item bound to a logged backpack
//   /sbp removeNonPlayer <onlyWithEmptyInventory>   delete backpacks no player ever opened
//
// Operators only (SB: op level 2 = Bedrock operators). The same overloads also work as
// `/backpack list|give|removeNonPlayer`. Bedrock chat has no click / hover events: the uuid and
// player name parameters autocomplete from the access log instead (SB's argument suggestions).

#include "feature/admin/Sbp.h"

#include <algorithm>
#include <ctime>
#include <set>

#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/command/SoftEnum.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/command/ServerCommandRegisterEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/CommandSelector.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/safety/RedactableString.h"

#include "core/WorldRules.h"
#include "feature/admin/AccessLog.h"
#include "feature/admin/Admin.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/Storage.h"
#include "mod/Commands.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace abp::admin {

// Command parameter types need external linkage.
enum class SbpPlayerName {};
enum class SbpBackpackUuid {};

struct SbpListParams {
    ll::command::SoftEnum<SbpPlayerName> playerName;
};
struct SbpGiveParams {
    CommandSelector<Player>                targets;
    ll::command::SoftEnum<SbpBackpackUuid> backpackUuid;
};
struct SbpRemoveNonPlayerParams {
    bool onlyWithEmptyInventory = false;
};

namespace {

Player* playerOf(CommandOrigin const& origin) {
    auto* e = origin.getEntity();
    return (e && e->isPlayer()) ? static_cast<Player*>(e) : nullptr;
}

template <class... Args>
std::string say(CommandOrigin const& origin, std::string_view key, std::string_view fallback, Args const&... args) {
    if (auto* p = playerOf(origin)) return trFor(*p, key, fallback, args...);
    return tr(key, fallback, args...);
}

/// `/backpack` is open to everybody (per-overload checks); `/sbp` is registered for operators.
bool isOperator(CommandOrigin const& origin, CommandOutput& output) {
    if (origin.getPermissionsLevel() >= CommandPermissionLevel::GameDirectors) return true;
    output.error(say(origin, "abp.admin.err.permission", "You need operator permission."));
    return false;
}

std::string formatAccessTime(std::int64_t millis) {
    std::time_t t = static_cast<std::time_t>(millis / 1000);
    std::tm     tm{};
    localtime_s(&tm, &t);
    return core::shortDateTime(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
}

/// One `/sbp list` line: "§a<name>§r, <Cloth> <Trim>, <player>, <date>" with the colour words in
/// the nearest text colour of the backpack's colours.
std::string listLine(CommandOrigin const& origin, AccessLogRecord const& r) {
    auto cloth = say(origin, "abp.admin.sbp.list.cloth_color", "Cloth");
    auto trim  = say(origin, "abp.admin.sbp.list.trim_color", "Trim");
    std::string line = "§a" + r.backpackName + "§r, ";
    line += std::string("§") + core::nearestFormatCode(r.clothColor & 0xFFFFFF) + cloth + "§r ";
    line += std::string("§") + core::nearestFormatCode(r.trimColor & 0xFFFFFF) + trim + "§r, ";
    line += r.playerName + ", " + formatAccessTime(r.accessTime);
    return line;
}

void printBackpackList(CommandOrigin const& origin, CommandOutput& output, std::vector<AccessLogRecord> logs) {
    std::stable_sort(logs.begin(), logs.end(), [](auto const& a, auto const& b) { return a.accessTime > b.accessTime; });
    output.success(say(origin, "abp.admin.sbp.list.header", "Backpack Name, Colors, Last Open By, Last Open On"));
    for (auto const& r : logs) output.success(listLine(origin, r));
    log().info("sbp: list -> {} backpacks", logs.size());
}

void list(CommandOrigin const& origin, CommandOutput& output, std::optional<std::string> const& playerName) {
    auto& accessLog = AccessLog::get();
    printBackpackList(origin, output, playerName ? accessLog.logsForPlayer(*playerName) : accessLog.all());
}

void giveBackpackToPlayer(ItemStack const& backpack, Player& p) {
    ItemStack copy = backpack;
    if (!p.addAndRefresh(copy)) p.drop(copy, false);
}

void give(CommandOrigin const& origin, CommandOutput& output, CommandSelector<Player> const& targets, std::string const& uuid) {
    auto record = AccessLog::get().find(uuid);
    if (!record) {
        log().info("sbp: give {} -> no access log record", uuid);
        return; // SB: nothing happens for an unknown uuid
    }
    auto tier = game::backpackTierOf(record->backpackItemRegistryName);
    if (!tier) return;
    auto backpack = game::makeBackpackItem(*tier, uuid);
    if (backpack.isNull()) return output.error("Backpack items are not registered (behavior pack missing?).");
    if (record->backpackName != backpackDisplayName(*tier)) {
        backpack.setCustomName(Bedrock::Safety::RedactableString{record->backpackName});
    }
    // SB writes the logged colours and columns into the backpack it gives.
    if (auto bp = game::Storage::get().find(uuid)) {
        bp->data().mainColor   = record->clothColor;
        bp->data().accentColor = record->trimColor;
        if (record->columnsTaken > 0) {
            if (!bp->data().settings.is_object()) bp->data().settings = nlohmann::json::object();
            bp->data().settings["columnsTaken"] = record->columnsTaken;
        }
        bp->markDirty();
    }
    std::vector<Player*> players;
    for (auto* p : targets.results(origin)) {
        if (p) players.push_back(p);
    }
    for (auto* p : players) giveBackpackToPlayer(backpack, *p);
    if (players.size() == 1) {
        output.success(say(origin, "abp.admin.sbp.give.success", "Gave backpack to {}", players.front()->getRealName()));
    } else {
        output.success(say(origin, "abp.admin.sbp.give.success", "Gave backpack to {}", players.size()));
    }
    log().info("sbp: gave backpack {} to {} players", uuid, players.size());
}

void removeNonPlayerCommand(CommandOrigin const& origin, CommandOutput& output, bool onlyWithEmptyInventory) {
    auto removed = removeNonPlayer(onlyWithEmptyInventory);
    output.success(say(origin, "abp.admin.sbp.remove_non_player.success", "Removed {} non player backpacks", removed.size()));
    log().info("sbp: removeNonPlayer {} -> {}", onlyWithEmptyInventory, removed.size());
}

/// The built-in overloads. `gate` true: check operator permission per overload (`/backpack`).
void addOverloads(ll::command::CommandHandle& cmd, bool gate) {
    cmd.overload().text("list").execute([gate](CommandOrigin const& origin, CommandOutput& output) {
        if (gate && !isOperator(origin, output)) return;
        list(origin, output, std::nullopt);
    });
    cmd.overload<SbpListParams>().text("list").required("playerName").execute(
        [gate](CommandOrigin const& origin, CommandOutput& output, SbpListParams const& params) {
            if (gate && !isOperator(origin, output)) return;
            list(origin, output, std::string(params.playerName));
        }
    );
    cmd.overload<SbpGiveParams>().text("give").required("targets").required("backpackUuid").execute(
        [gate](CommandOrigin const& origin, CommandOutput& output, SbpGiveParams const& params) {
            if (gate && !isOperator(origin, output)) return;
            give(origin, output, params.targets, std::string(params.backpackUuid));
        }
    );
    // Bedrock matches literals lower-cased: SB's `removeNonPlayer` is typed as is and matches.
    cmd.overload<SbpRemoveNonPlayerParams>().text("removenonplayer").required("onlyWithEmptyInventory").execute(
        [gate](CommandOrigin const& origin, CommandOutput& output, SbpRemoveNonPlayerParams const& params) {
            if (gate && !isOperator(origin, output)) return;
            removeNonPlayerCommand(origin, output, params.onlyWithEmptyInventory);
        }
    );
}

std::uint64_t gSuggestionRevision = ~0ull;

void registerSbp() {
    if (!ll::service::getCommandRegistry()) return;
    auto& registrar = ll::command::CommandRegistrar::getServerInstance();
    auto& cmd       = registrar.getOrCreateCommand(
        "sbp",
        "Sophisticated Backpacks admin commands",
        CommandPermissionLevel::GameDirectors
    );
    cmd.alias("sophisticatedbackpacks");
    addOverloads(cmd, false);
    for (auto const& fn : SbpSubcommandRegistry::entries()) fn(cmd);
    gSuggestionRevision = ~0ull;
    refreshSbpSuggestions();
}

} // namespace

void refreshSbpSuggestions() {
    if (!ll::service::getCommandRegistry()) return;
    auto& accessLog = AccessLog::get();
    if (gSuggestionRevision == accessLog.revision()) return;
    gSuggestionRevision = accessLog.revision();
    auto& registrar     = ll::command::CommandRegistrar::getServerInstance();
    // Suggestions follow the log at once (the 30 s cache only applies to `list <playerName>`).
    std::set<std::string> names;
    for (auto const& r : accessLog.all()) names.insert(r.playerName);
    std::vector<std::string> nameList(names.begin(), names.end());
    if (registrar.hasSoftEnum(ll::command::enum_name_v<ll::command::SoftEnum<SbpPlayerName>>)) {
        registrar.setSoftEnumValues(ll::command::enum_name_v<ll::command::SoftEnum<SbpPlayerName>>, nameList);
    }
    if (registrar.hasSoftEnum(ll::command::enum_name_v<ll::command::SoftEnum<SbpBackpackUuid>>)) {
        registrar.setSoftEnumValues(ll::command::enum_name_v<ll::command::SoftEnum<SbpBackpackUuid>>, accessLog.uuids());
    }
}

namespace {

class SbpCommandModule {
public:
    void install() {
        registerSbp(); // BDS: the registry exists when mods are enabled
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::command::ServerCommandRegisterEvent>([](auto&) {
            registerSbp(); // client: every local world
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([this](auto&) {
            if (++mTicks % 20 == 0) refreshSbpSuggestions();
        }));
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
    int                                 mTicks = 0;
};

} // namespace
} // namespace abp::admin

ABP_REGISTER_MODULE_ORDERED(abp::admin::SbpCommandModule, abp::ModuleOrder::Commands + 1);
// `/backpack list|give|removeNonPlayer` keep working (operators only).
ABP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) { abp::admin::addOverloads(cmd, true); });
