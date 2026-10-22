// E2E helpers of the filter unit (client scenario `filters.ps1`):
//   /backpack filters setup  a netherite backpack with the Pickup, Filter, Restock and Deposit
//                            upgrades (advanced ones first) plus a few items to put into filters.
//   /backpack filters dump   logs every carried backpack's filter settings
//                            ("filters dump: slot=<n> id=<id> filter=<json> direction=<d>").

#include "ll/api/command/CommandHandle.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "game/Backpack.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "game/Storage.h"
#include "mod/Commands.h"
#include "mod/Log.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/filter/FilterCommon.h"

namespace abp::upgrade::filtering {
namespace {

Player* playerOf(CommandOrigin const& origin) {
    auto* e = origin.getEntity();
    return e && e->isPlayer() ? static_cast<Player*>(e) : nullptr;
}

void setup(Player& player, CommandOutput& output) {
    if (!game::Storage::get().available()) return output.error("no storage");
    auto bp = game::Storage::get().create(core::Tier::Netherite);
    if (!bp) return output.error("could not create a backpack");
    int slot = 0;
    for (auto const* id :
         {"advanced_pickup", "advanced_filter", "advanced_deposit", "advanced_restock", "pickup", "filter", "deposit"}) {
        auto* type = byId(id);
        if (!type || slot >= static_cast<int>(bp->data().upgrades.size())) continue;
        auto item = game::ItemConv::toData(game::ItemConv::make(type->info().itemId, 1));
        if (install(bp, slot, item, &player)) ++slot;
    }
    auto& inv = bp->inventory();
    int   i   = 0;
    for (auto const* id : {"minecraft:cobblestone", "minecraft:oak_log", "minecraft:apple", "minecraft:torch"}) {
        inv.set(i++, game::ItemConv::toData(game::ItemConv::make(id, 32)));
    }
    bp->markDirty();
    game::Storage::get().save(*bp, true);
    auto stack = game::makeBackpackItem(core::Tier::Netherite, bp->uuid());
    if (!player.add(stack)) player.drop(stack, false);
    for (auto const* id : {"minecraft:apple", "minecraft:oak_log", "minecraft:iron_sword", "minecraft:torch"}) {
        auto s = game::ItemConv::make(id, 1);
        if (!player.add(s)) player.drop(s, false);
    }
    player.sendInventory(false);
    log().info("filters setup: gave netherite backpack {} with {} upgrades", bp->uuid(), slot);
    output.success("filters setup: " + bp->uuid());
}

void dump(Player& player, CommandOutput& output) {
    int n = 0;
    for (auto& [carried, bp] : game::carriedBackpacks(player, false, false)) {
        if (!bp) continue;
        auto const& slots = bp->data().upgrades;
        for (int s = 0; s < static_cast<int>(slots.size()); ++s) {
            auto const& st   = slots[static_cast<std::size_t>(s)];
            auto*       type = st.empty() ? nullptr : upgradeFor(st.item);
            if (!type || !type->info().hasSettings) continue;
            auto const& g = type->info().groupOrId();
            if (g != "pickup" && g != "filter" && g != "restock" && g != "deposit") continue;
            UpgradeContext ctx(bp, s, &player);
            auto           f = load(ctx, type->info().tier);
            log().info(
                "filters dump: slot={} id={} filter={} direction={}",
                s,
                type->info().id,
                tabJson(f, specFor(ctx, type->info().tier)).dump(),
                ctx.settings().value("direction", 0)
            );
            ++n;
        }
    }
    output.success("filters dump: " + std::to_string(n));
}

} // namespace
} // namespace abp::upgrade::filtering

ABP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    cmd.overload().text("filters").text("setup").execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (auto* p = abp::upgrade::filtering::playerOf(origin)) abp::upgrade::filtering::setup(*p, output);
        else output.error("players only");
    });
    cmd.overload().text("filters").text("dump").execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (auto* p = abp::upgrade::filtering::playerOf(origin)) abp::upgrade::filtering::dump(*p, output);
        else output.error("players only");
    });
});
