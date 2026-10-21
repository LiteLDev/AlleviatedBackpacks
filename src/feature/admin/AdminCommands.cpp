// Admin sub-commands of `/backpack` (alias `/abp`). Permission: `admin.commandPermissionLevel`
// (operators by default; the server console always qualifies).
//
//   /backpack list [holder]               stored backpacks: uuid, tier, fill, upgrades, holders
//   /backpack info <uuid>                 details of one stored backpack
//   /backpack stats                       storage statistics
//   /backpack give <player> <uuid>        give a backpack item bound to an existing backpack
//   /backpack remove <uuid>               delete a backpack's storage
//   /backpack removenonplayer             delete backpacks no player ever carried (mob loot)
//   /backpack cleanup                     delete empty, unused backpack storage (orphans)
//   /backpack openfor <uuid>              open any backpack's GUI (players only)
//   /backpack reload                      (foundation) reloads configs; rules apply at once
//
// Uuids may be abbreviated to a unique prefix of at least 4 characters.

#include <ctime>

#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/CommandSelector.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "feature/admin/Admin.h"
#include "game/BackpackItem.h"
#include "game/Storage.h"
#include "mod/Commands.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "ui/menu/BackpackMenu.h"
#include "upgrade/UpgradeManager.h"

namespace abp::admin {

// Command parameter types need external linkage.
struct AdminUuidParams {
    std::string uuid;
};
struct AdminListParams {
    std::string holder;
};
struct AdminGiveParams {
    CommandSelector<Player> player;
    std::string             uuid;
};

namespace {

constexpr std::size_t kMaxListLines   = 100;
constexpr int         kOpenDelayTicks = 10; ///< let the chat screen close before the GUI opens

Player* playerOf(CommandOrigin const& origin) {
    auto* e = origin.getEntity();
    return (e && e->isPlayer()) ? static_cast<Player*>(e) : nullptr;
}

template <class... Args>
std::string say(CommandOrigin const& origin, std::string_view key, std::string_view fallback, Args const&... args) {
    if (auto* p = playerOf(origin)) return trFor(*p, key, fallback, args...);
    return tr(key, fallback, args...);
}

bool allowed(CommandOrigin const& origin, CommandOutput& output) {
    if (static_cast<int>(origin.getPermissionsLevel()) >= config::get<AdminConfig>().commandPermissionLevel) return true;
    output.error(say(origin, "abp.admin.err.permission", "You need operator permission."));
    return false;
}

std::optional<std::string> resolve(CommandOrigin const& origin, CommandOutput& output, std::string const& input) {
    std::string key, fallback;
    auto        uuid = resolveUuid(input, &key, &fallback);
    if (!uuid) output.error(say(origin, key, fallback, input));
    return uuid;
}

std::string formatTime(std::int64_t t) {
    if (t <= 0) return "-";
    std::time_t tt = static_cast<std::time_t>(t);
    std::tm     tm{};
    gmtime_s(&tm, &tt);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M UTC", &tm);
    return buf;
}

std::string holderText(CommandOrigin const& origin, Summary const& s) {
    if (s.holders.last) {
        auto text = s.holders.last->name;
        if (s.holders.first && s.holders.first->uuid != s.holders.last->uuid) {
            text += say(origin, "abp.admin.list.first", " (first: {})", s.holders.first->name);
        }
        return text;
    }
    return s.legacy ? say(origin, "abp.admin.list.legacy", "(before tracking)")
                    : say(origin, "abp.admin.list.nobody", "(never carried by a player)");
}

std::string summaryLine(CommandOrigin const& origin, Summary const& s) {
    return say(
        origin,
        "abp.admin.list.line",
        "{} §7{}§r {}/{} slots, {} items, {} upgrades, holder: {}, seen: {}",
        s.uuid,
        core::tierInfo(s.tier).id,
        s.usedSlots,
        s.slots,
        s.items,
        s.upgrades,
        holderText(origin, s),
        formatTime(s.holders.lastSeen)
    );
}

void list(CommandOrigin const& origin, CommandOutput& output, std::string const& holder) {
    if (!allowed(origin, output)) return;
    auto all = listBackpacks(holder);
    output.success(
        holder.empty() ? say(origin, "abp.admin.list.header", "{} stored backpacks:", all.size())
                       : say(origin, "abp.admin.list.header_holder", "{} backpacks of {}:", all.size(), holder)
    );
    for (std::size_t i = 0; i < all.size() && i < kMaxListLines; ++i) output.success(summaryLine(origin, all[i]));
    if (all.size() > kMaxListLines) {
        output.success(say(origin, "abp.admin.list.more", "... and {} more", all.size() - kMaxListLines));
    }
    log().info("admin: list{} -> {} backpacks", holder.empty() ? "" : " " + holder, all.size());
}

void registerCommands(ll::command::CommandHandle& cmd) {
    cmd.overload().text("list").execute([](CommandOrigin const& origin, CommandOutput& output) {
        list(origin, output, {});
    });
    cmd.overload<AdminListParams>().text("list").required("holder").execute(
        [](CommandOrigin const& origin, CommandOutput& output, AdminListParams const& params) {
            list(origin, output, params.holder);
        }
    );

    cmd.overload<AdminUuidParams>().text("info").required("uuid").execute(
        [](CommandOrigin const& origin, CommandOutput& output, AdminUuidParams const& params) {
            if (!allowed(origin, output)) return;
            auto uuid = resolve(origin, output, params.uuid);
            if (!uuid) return;
            auto bp = game::Storage::get().find(*uuid);
            if (!bp) return output.error(say(origin, "abp.admin.err.uuid_unknown", "No backpack with uuid {}.", *uuid));
            auto        s = summarize(bp);
            std::string ups;
            upgrade::forEachInstalled(bp, false, [&](upgrade::IUpgrade& u, upgrade::UpgradeContext const& ctx) {
                ups += (ups.empty() ? "" : ", ") + u.info().id;
                if (!ctx.state().enabled) ups += " (off)";
                else if (!upgrade::typeAllowed(u)) ups += " (disabled by server)";
            });
            output.success(summaryLine(origin, s));
            output.success(say(origin, "abp.admin.info.upgrades", "Upgrades: [{}]", ups));
            if (s.holders.first) {
                output.success(say(
                    origin,
                    "abp.admin.info.holders",
                    "First holder: {} ({}), last holder: {} ({})",
                    s.holders.first->name,
                    s.holders.first->uuid,
                    s.holders.last ? s.holders.last->name : "-",
                    s.holders.last ? s.holders.last->uuid : "-"
                ));
            }
            log().info("admin: info {} -> {}/{} slots, {} items", s.uuid, s.usedSlots, s.slots, s.items);
        }
    );

    cmd.overload().text("stats").execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (!allowed(origin, output)) return;
        auto        s = stats();
        std::string tiers;
        for (auto const& t : core::kTiers) {
            tiers += (tiers.empty() ? "" : ", ") + std::string(t.id) + "="
                   + std::to_string(s.perTier[static_cast<std::size_t>(t.tier)]);
        }
        output.success(say(
            origin,
            "abp.admin.stats.line",
            "Backpacks: {} stored ({} loaded), {} items; empty: {}, never carried: {}, unreadable: {}, kept on death: {}",
            s.stored,
            s.loaded,
            s.items,
            s.empty,
            s.neverAccessed,
            s.unreadable,
            s.deathStashes
        ));
        output.success(say(origin, "abp.admin.stats.tiers", "Per tier: {}", tiers));
        log().info("admin: stats stored={} loaded={} empty={} never_carried={}", s.stored, s.loaded, s.empty, s.neverAccessed);
    });

    cmd.overload<AdminGiveParams>().text("give").required("player").required("uuid").execute(
        [](CommandOrigin const& origin, CommandOutput& output, AdminGiveParams const& params) {
            if (!allowed(origin, output)) return;
            auto uuid = resolve(origin, output, params.uuid);
            if (!uuid) return;
            auto bp = game::Storage::get().find(*uuid);
            if (!bp) return output.error(say(origin, "abp.admin.err.uuid_unknown", "No backpack with uuid {}.", *uuid));
            // One target only: every copy shares the same storage, so handing it to a crowd by
            // accident (`@a`) would make the contents reachable from several places at once.
            auto    targets = params.player.results(origin);
            Player* target  = nullptr;
            int     matched = 0;
            for (auto* p : targets) {
                if (!p) continue;
                target = p;
                ++matched;
            }
            if (matched == 0) return output.error(say(origin, "abp.admin.err.no_target", "No player matched."));
            if (matched > 1) {
                return output.error(
                    say(origin, "abp.admin.err.one_target", "Select exactly one player (the copy shares the storage).")
                );
            }
            auto item = game::makeBackpackItem(bp->tier(), *uuid);
            if (item.isNull()) return output.error("Backpack items are not registered (behavior pack missing?).");
            if (!target->addAndRefresh(item)) target->drop(item, false);
            output.success(say(origin, "abp.admin.give.ok", "Gave backpack {} to {}.", *uuid, target->getRealName()));
            log().info("admin: gave backpack {} to {}", *uuid, target->getRealName());
        }
    );

    cmd.overload<AdminUuidParams>().text("remove").required("uuid").execute(
        [](CommandOrigin const& origin, CommandOutput& output, AdminUuidParams const& params) {
            if (!allowed(origin, output)) return;
            auto uuid = resolve(origin, output, params.uuid);
            if (!uuid) return;
            game::Storage::get().erase(*uuid);
            output.success(say(origin, "abp.admin.remove.ok", "Removed backpack {}.", *uuid));
            log().info("admin: removed backpack {}", *uuid);
        }
    );

    cmd.overload().text("removenonplayer").execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (!allowed(origin, output)) return;
        auto removed = removeNonPlayer();
        output.success(say(origin, "abp.admin.removenonplayer.ok", "Removed {} backpacks no player ever carried.", removed.size()));
        log().info("admin: removenonplayer -> {}", removed.size());
    });

    cmd.overload().text("cleanup").execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (!allowed(origin, output)) return;
        auto removed = cleanupEmpty();
        output.success(say(origin, "abp.admin.cleanup.ok", "Removed {} empty, unused backpacks.", removed.size()));
        log().info("admin: cleanup -> {}", removed.size());
    });

    cmd.overload<AdminUuidParams>().text("openfor").required("uuid").execute(
        [](CommandOrigin const& origin, CommandOutput& output, AdminUuidParams const& params) {
            if (!allowed(origin, output)) return;
            auto* player = playerOf(origin);
            if (!player) return output.error("Only players can open backpacks.");
            auto uuid = resolve(origin, output, params.uuid);
            if (!uuid) return;
            auto bp = game::Storage::get().find(*uuid);
            if (!bp) return output.error(say(origin, "abp.admin.err.uuid_unknown", "No backpack with uuid {}.", *uuid));
            markInspected(*uuid);
            auto title = trFor(*player, "abp.admin.openfor.title", "Backpack {}", uuid->substr(0, 8));
            openLater(*player, bp, title, kOpenDelayTicks);
            output.success(trFor(*player, "abp.admin.openfor.ok", "Opened backpack {}.", *uuid));
            log().info("admin: {} opened backpack {}", player->getRealName(), *uuid);
        }
    );
}

} // namespace

} // namespace abp::admin

ABP_REGISTER_SUBCOMMAND(&abp::admin::registerCommands);
