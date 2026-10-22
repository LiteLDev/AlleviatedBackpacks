// E2E helpers of the Magnet / Feeding / Void / Compacting unit (client scenario `mfvc.ps1`):
//   /backpack mfvc setup  a netherite backpack with the advanced Magnet, Feeding, Void and
//                         Compacting upgrades plus the basic Magnet, Feeding and Void (tabs 1..7),
//                         and a few items to put into filters (hotbar 1: apple, 2: stone).
//   /backpack mfvc dump   logs the settings of those upgrades in every carried backpack
//                         ("mfvc dump: slot=<n> id=<id> settings=<json>").

#include "ll/api/command/CommandHandle.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
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

namespace abp::upgrade::voiding {
namespace {

/// Test helpers: operators only (they hand out items).
Player* playerOf(CommandOrigin const& origin) {
    if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors) return nullptr;
    auto* e = origin.getEntity();
    return e && e->isPlayer() ? static_cast<Player*>(e) : nullptr;
}

bool isMfvc(std::string_view group) { return group == "magnet" || group == "feeding" || group == "void" || group == "compacting"; }

void setup(Player& player, CommandOutput& output) {
    if (!game::Storage::get().available()) return output.error("no storage");
    auto bp = game::Storage::get().create(core::Tier::Netherite);
    if (!bp) return output.error("could not create a backpack");
    int slot = 0;
    for (auto const* id : {"advanced_magnet", "advanced_feeding", "advanced_void", "advanced_compacting", "magnet", "feeding", "void"}) {
        auto* type = byId(id);
        if (!type || slot >= static_cast<int>(bp->data().upgrades.size())) continue;
        auto item = game::ItemConv::toData(game::ItemConv::make(type->info().itemId, 1));
        if (install(bp, slot, item, &player)) ++slot;
    }
    auto& inv = bp->inventory();
    int   i   = 0;
    for (auto const* id : {"minecraft:bread", "minecraft:iron_nugget", "minecraft:cobblestone"}) {
        inv.set(i++, game::ItemConv::toData(game::ItemConv::make(id, 8)));
    }
    bp->markDirty();
    game::Storage::get().save(*bp, true);
    auto stack = game::makeBackpackItem(core::Tier::Netherite, bp->uuid());
    if (!player.add(stack)) player.drop(stack, false);
    for (auto const* id : {"minecraft:apple", "minecraft:stone", "minecraft:iron_ingot", "minecraft:water_bucket"}) {
        auto s = game::ItemConv::make(id, 1);
        if (!player.add(s)) player.drop(s, false);
    }
    player.sendInventory(false);
    log().info("mfvc setup: gave netherite backpack {} with {} upgrades", bp->uuid(), slot);
    output.success("mfvc setup: " + bp->uuid());
}

void dump(Player& player, CommandOutput& output) {
    int n = 0;
    for (auto& [carried, bp] : game::carriedBackpacks(player)) {
        auto const& slots = bp->data().upgrades;
        for (int s = 0; s < static_cast<int>(slots.size()); ++s) {
            auto const& st   = slots[static_cast<std::size_t>(s)];
            auto*       type = st.empty() ? nullptr : upgradeFor(st.item);
            if (!type || !isMfvc(type->info().groupOrId())) continue;
            log().info("mfvc dump: slot={} id={} settings={}", s, type->info().id, st.settings.dump());
            ++n;
        }
    }
    output.success("mfvc dump: " + std::to_string(n) + " upgrades");
}

} // namespace
} // namespace abp::upgrade::voiding

ABP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    cmd.overload().text("mfvc").text("setup").execute([](CommandOrigin const& origin, CommandOutput& output) {
        auto* p = abp::upgrade::voiding::playerOf(origin);
        if (!p) return output.error("operators only");
        abp::upgrade::voiding::setup(*p, output);
    });
    cmd.overload().text("mfvc").text("dump").execute([](CommandOrigin const& origin, CommandOutput& output) {
        auto* p = abp::upgrade::voiding::playerOf(origin);
        if (!p) return output.error("operators only");
        abp::upgrade::voiding::dump(*p, output);
    });
});
