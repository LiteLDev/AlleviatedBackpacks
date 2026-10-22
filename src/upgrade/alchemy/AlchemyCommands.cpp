// e2e helpers of the alchemy unit (op only):
//   /backpack alchemy setup   an iron backpack with an Alchemy (Fire Resistance / Water Breathing /
//                             Healing filters) and an Advanced Alchemy upgrade, plus potions
//   /backpack alchemy status  logs the player's effects, health and the carried potion counts

#include <string>

#include "ll/api/command/CommandHandle.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/item/ItemStack.h"

#include "game/Backpack.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "game/Storage.h"
#include "mod/Commands.h"
#include "mod/Log.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/alchemy/Alchemy.h"

namespace {

using namespace abp;
using namespace abp::upgrade::alchemy;

core::ItemStackData item(std::string_view id, int aux = 0) { return game::ItemConv::toData(game::ItemConv::make(id, 1, aux)); }

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

void setup(Player& player, CommandOutput& output) {
    auto bp = game::Storage::get().create(core::Tier::Iron);
    if (!bp) {
        output.error("could not create a backpack");
        return;
    }
    int slots[2] = {-1, -1};
    int n        = 0;
    for (auto const* id : {kBasicItem, kAdvancedItem}) {
        for (int s = 0; s < static_cast<int>(bp->data().upgrades.size()); ++s) {
            if (!bp->data().upgrades[static_cast<std::size_t>(s)].empty()) continue;
            if (upgrade::install(bp, s, item(id), &player)) slots[n] = s;
            break;
        }
        ++n;
    }
    if (slots[0] < 0 || slots[1] < 0) {
        output.error("could not install the alchemy upgrades");
        return;
    }
    upgrade::UpgradeContext basic(bp, slots[0], &player);
    (void)setFilter(basic, 0, item(kPotion, 12)); // fire resistance: When on Fire
    (void)setFilter(basic, 1, item(kPotion, 19)); // water breathing: When Under Water
    (void)setFilter(basic, 2, item(kPotion, 21)); // healing: When Hurt (below 75%)
    upgrade::UpgradeContext advanced(bp, slots[1], &player);
    (void)setFilter(advanced, 0, item(kMilkBucket)); // When Affected by Negative Effect
    auto& inv  = bp->inventory();
    int   slot = 0;
    auto  put  = [&](core::ItemStackData const& d, int count) {
        for (int i = 0; i < count && slot < inv.size(); ++i) inv.set(slot++, d.withCount(1));
    };
    put(item(kPotion, 12), 3);
    put(item(kPotion, 19), 3);
    put(item(kPotion, 21), 3);
    put(item(kPotion, 5), 2); // night vision: for dragging into a filter slot
    put(item(kMilkBucket), 1);
    bp->markDirty();
    game::Storage::get().save(*bp, true);
    auto stack = game::makeBackpackItem(core::Tier::Iron, bp->uuid());
    if (!player.add(stack)) player.drop(stack, false);
    player.sendInventory(false);
    log().info("alchemy setup: gave iron backpack {} (alchemy slot {}, advanced slot {})", bp->uuid(), slots[0], slots[1]);
    output.success("alchemy setup: gave iron backpack " + bp->uuid());
}

void status(Player& player, CommandOutput& output) {
    std::string effects;
    for (auto const& e : player.getAllEffects()) {
        if (!effects.empty()) effects += ",";
        effects += std::to_string(e.mId) + ":" + std::to_string(e.mAmplifier);
    }
    int fr = 0, wb = 0, heal = 0, bottles = 0;
    for (auto& [carried, bp] : game::carriedBackpacks(player)) {
        if (!bp) continue;
        auto const& inv = bp->inventory();
        for (int i = 0; i < inv.size(); ++i) {
            auto const& d = inv.at(i);
            if (d.id == kPotion && d.aux == 12) fr += d.count;
            if (d.id == kPotion && d.aux == 19) wb += d.count;
            if (d.id == kPotion && d.aux == 21) heal += d.count;
            if (d.id == kGlassBottle) bottles += d.count;
        }
    }
    auto line = "alchemy status: effects=[" + effects + "] health=" + std::to_string(player.getHealth()) + "/"
              + std::to_string(player.getMaxHealth()) + " fire_resistance=" + std::to_string(fr) + " water_breathing="
              + std::to_string(wb) + " healing=" + std::to_string(heal) + " bottles=" + std::to_string(bottles);
    log().info("{}", line);
    output.success(line);
}

} // namespace

ABP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    cmd.overload().text("alchemy").text("setup").execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (auto* p = playerOf(origin, output)) setup(*p, output);
    });
    cmd.overload().text("alchemy").text("status").execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (auto* p = playerOf(origin, output)) status(*p, output);
    });
});
