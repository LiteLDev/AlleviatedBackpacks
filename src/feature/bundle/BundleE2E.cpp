// Client e2e support for the bundle rule (`tools/e2e/client/scenarios/bundle.ps1`), operators only:
//
//   /backpack bundle setup     inventory: 0 an empty bundle, 1 a full leather backpack, the rest empty
//   /backpack bundle status    logs "bundle status: weight=<contents weight of the fullest bundle>
//                              backpacks=<backpack items in the inventory and on the cursor>"
//   /backpack bundle allow|refuse
//                              writes `canBePlacedInContainerItems` into the config file only; it
//                              applies on the next `/backpack reload`

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "ll/api/command/CommandHandle.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/components/storage_item_utility/StorageItemWeightData.h"
#include "mc/world/item/components/storage_item_utility/StorageItemUtility.h"

#include "nlohmann/json.hpp"

#include "core/Tier.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "game/Storage.h"
#include "mod/Commands.h"
#include "mod/Config.h"
#include "mod/Log.h"

namespace {
using namespace abp;

Player* playerOf(CommandOrigin const& origin, CommandOutput& output) {
    if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors) {
        output.error("operators only");
        return nullptr;
    }
    auto* e = origin.getEntity();
    if (!e || !e->isPlayer() || !game::Storage::get().available()) {
        output.error("players only");
        return nullptr;
    }
    return static_cast<Player*>(e);
}

game::ItemLocation inv(int slot) { return {game::ItemLocation::Kind::Inventory, slot}; }

void writeFile(bool allowed, CommandOutput& output) {
    auto           path = config::configDir() / "sophisticatedbackpacks-server.json";
    nlohmann::json j    = nlohmann::json::object();
    if (std::ifstream in(path); in) j = nlohmann::json::parse(in, nullptr, false);
    if (!j.is_object()) j = nlohmann::json::object();
    j["canBePlacedInContainerItems"] = allowed;
    std::ofstream(path) << j.dump(4);
    log().info("bundle file: canBePlacedInContainerItems={}", allowed);
    output.success(std::string("bundle file: canBePlacedInContainerItems=") + (allowed ? "true" : "false"));
}
} // namespace

ABP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    cmd.overload().text("bundle").text("setup").execute([](CommandOrigin const& origin, CommandOutput& output) {
        auto* player = playerOf(origin, output);
        if (!player) return;
        auto bp = game::Storage::get().create(core::Tier::Leather);
        if (!bp) return output.error("could not create the backpack");
        // Full: right-click stashing has nothing to offer, so the click reaches the bundle.
        for (int i = 0; i < bp->inventory().size(); ++i) {
            bp->inventory().set(i, game::ItemConv::toData(game::ItemConv::make("minecraft:cobblestone", 64)));
        }
        bp->markDirty();
        game::Storage::get().save(*bp, true);
        for (int i = 0; i < 36; ++i) game::setItemAt(*player, inv(i), ItemStack{});
        game::setItemAt(*player, inv(0), game::ItemConv::make("minecraft:bundle"));
        game::setItemAt(*player, inv(1), game::makeBackpackItem(core::Tier::Leather, bp->uuid()));
        player->sendInventory(false);
        log().info("bundle setup: backpack={}", bp->uuid());
        output.success("bundle setup: backpack=" + bp->uuid());
    });
    cmd.overload().text("bundle").text("status").execute([](CommandOrigin const& origin, CommandOutput& output) {
        auto* player = playerOf(origin, output);
        if (!player) return;
        int weight = 0, backpacks = 0;
        auto count = [&](ItemStack const& s) {
            if (s.isNull()) return;
            if (game::isBackpack(s)) ++backpacks;
            if (auto w = StorageItemUtility::getStorageItemWeightDataServer(s)) weight = std::max(weight, w->mContentsWeight);
        };
        for (int i = 0; i < 36; ++i) count(game::itemAt(*player, inv(i)));
        count(player->mPlayerUIContainer->getItem(0)); // the cursor
        log().info("bundle status: weight={} backpacks={}", weight, backpacks);
        output.success("bundle status: weight=" + std::to_string(weight) + " backpacks=" + std::to_string(backpacks));
    });
    cmd.overload().text("bundle").text("allow").execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (playerOf(origin, output)) writeFile(true, output);
    });
    cmd.overload().text("bundle").text("refuse").execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (playerOf(origin, output)) writeFile(false, output);
    });
});
