// Selftests of the Experience Pump upgrade: xppump.*
// Run with `/backpack selftest "xppump.*"`.

#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"

#include "api/abp/Xp.h"
#include "game/Backpack.h"
#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "net/NativeScreens.h"
#include "selftest/SelfTest.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/tank/Tank.h"
#include "upgrade/xppump/XpPump.h"

namespace abp::upgrade::xppump {
namespace {

using selftest::Context;
namespace fl = core::fluid;

long long xpInTanks(std::shared_ptr<game::Backpack> const& bp) { return tank::amountOf(bp, fl::kExperience); }

bool action(Player& p, UpgradeContext const& ctx, nlohmann::json const& data) {
    bool ok = false;
    for (auto const& h : net::TabActionRegistry::entries()) {
        if (h.upgrade == kId && h.handle) ok |= h.handle(p, ctx, data);
    }
    return ok;
}

ABP_REGISTER_SELFTEST("xppump.defaults", [](Context& ctx) {
    auto           bp = ctx.createBackpack(core::Tier::Leather);
    UpgradeContext x(bp, ctx.installUpgrade(bp, std::string(kId)));
    auto           s = Settings::of(x);
    ctx.check(s.direction == Direction::Input && s.level == 10 && s.levelsToStore == 1 && s.levelsToTake == 1 && s.mendItems, "SB defaults");
    // Phase-1 settings are read and rewritten in SB's format.
    x.settings() = {{"mode", "both"}, {"keep", 7}, {"mending", false}};
    s            = Settings::of(x);
    ctx.check(s.direction == Direction::Keep && s.level == 7 && !s.mendItems, "phase-1 settings migrate");
});

ABP_REGISTER_SELFTEST("xppump.player_xp", [](Context& ctx) {
    auto& player = ctx.player();
    setPlayerXp(player, 0);
    ctx.check(playerXp(player) == 0 && playerLevel(player) == 0, "starts at 0");
    setPlayerXp(player, api::xp::experienceForLevel(17) + 5);
    ctx.check(playerLevel(player) == 17 && playerXp(player) == api::xp::experienceForLevel(17) + 5, "level 17 + 5 points round trip");
    setPlayerXp(player, 0);
});

ABP_REGISTER_SELFTEST("xppump.needs_tank", [](Context& ctx) {
    auto&          player = ctx.player();
    auto           bp     = ctx.createBackpack(core::Tier::Leather);
    UpgradeContext x(bp, ctx.installUpgrade(bp, std::string(kId)), &player);
    setPlayerXp(player, api::xp::experienceForLevel(20));
    interactWithPlayer(player, x);
    manualAction(player, x, "storeAll");
    ctx.check(playerLevel(player) == 20, "without a tank nothing is stored");
    setPlayerXp(player, 0);
});

ABP_REGISTER_SELFTEST("xppump.auto_input", [](Context& ctx) {
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(core::Tier::Iron);
    ctx.installUpgrade(bp, "tank");
    UpgradeContext x(bp, ctx.installUpgrade(bp, std::string(kId)), &player);
    setPlayerXp(player, api::xp::experienceForLevel(20));
    interactWithPlayer(player, x);
    ctx.check(xpInTanks(bp) == 1000, "one operation: a bucket (50 points), got " + std::to_string(xpInTanks(bp)));
    for (int i = 0; i < 20; ++i) interactWithPlayer(player, x);
    ctx.check(playerXp(player) == api::xp::experienceForLevel(10), "stops at level 10, at " + std::to_string(playerXp(player)));
    ctx.check(xpInTanks(bp) == 20LL * (api::xp::experienceForLevel(20) - api::xp::experienceForLevel(10)), "the rest is in the tank");
    // Output mode gives it back up to the level.
    auto s      = Settings::of(x);
    s.direction = Direction::Output;
    s.level     = 15;
    s.save(x);
    for (int i = 0; i < 20; ++i) interactWithPlayer(player, x);
    ctx.check(playerXp(player) == api::xp::experienceForLevel(15), "output fills up to level 15");
    // Keep: both ways; Off: nothing.
    s.direction = Direction::Keep;
    s.level     = 12;
    s.save(x);
    for (int i = 0; i < 20; ++i) interactWithPlayer(player, x);
    ctx.check(playerXp(player) == api::xp::experienceForLevel(12), "keep: down to 12");
    s.direction = Direction::Off;
    s.save(x);
    setPlayerXp(player, 0);
    interactWithPlayer(player, x);
    ctx.check(playerXp(player) == 0, "off: nothing");
});

ABP_REGISTER_SELFTEST("xppump.manual", [](Context& ctx) {
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(core::Tier::Gold); // 1800 points of tank
    ctx.installUpgrade(bp, "tank");
    UpgradeContext x(bp, ctx.installUpgrade(bp, std::string(kId)), &player);
    setPlayerXp(player, api::xp::experienceForLevel(30));
    ctx.check(action(player, x, {{"op", "action"}, {"what", "store"}}), "store action");
    ctx.check(playerLevel(player) == 29, "store 1 level (no per-operation limit)");
    ctx.check(action(player, x, {{"op", "levelsToStore"}, {"delta", 1}}) && Settings::of(x).levelsToStore == 2, "scroll: store 2");
    action(player, x, {{"op", "action"}, {"what", "store"}});
    ctx.check(playerLevel(player) == 27, "store 2 levels");
    action(player, x, {{"op", "levelsToTake"}, {"delta", -1}});
    ctx.check(Settings::of(x).levelsToTake == 1, "levels to take can't go below 1");
    action(player, x, {{"op", "action"}, {"what", "take"}});
    ctx.check(playerLevel(player) == 28, "take 1 level");
    action(player, x, {{"op", "action"}, {"what", "storeAll"}});
    ctx.check(playerXp(player) == 0, "store all");
    action(player, x, {{"op", "action"}, {"what", "takeAll"}});
    ctx.check(playerXp(player) == api::xp::experienceForLevel(30), "take all");
    ctx.check(xpInTanks(bp) == 0, "the tank is empty again");
    action(player, x, {{"op", "level"}, {"delta", -1}});
    ctx.check(Settings::of(x).level == 9, "level selector -1");
    action(player, x, {{"op", "direction"}});
    ctx.check(Settings::of(x).direction == Direction::Output, "direction cycles input -> output");
    action(player, x, {{"op", "mend"}});
    ctx.check(!Settings::of(x).mendItems, "mending toggled off");
    setPlayerXp(player, 0);
});

ABP_REGISTER_SELFTEST("xppump.mending", [](Context& ctx) {
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(core::Tier::Iron);
    ctx.installUpgrade(bp, "tank");
    UpgradeContext x(bp, ctx.installUpgrade(bp, std::string(kId)), &player);
    auto           sword = game::ItemConv::make("minecraft:iron_sword");
    EnchantmentInstance mending;
    mending.mEnchantType = Enchant::Type::Mending;
    mending.mLevel       = 1;
    ctx.require(EnchantUtils::applyEnchant(sword, mending, false), "enchant with Mending");
    sword.setDamageValue(100);
    auto where = game::selectedSlot(player);
    game::setItemAt(player, where, sword);
    (void)tank::fill(bp, {std::string(fl::kExperience), 20 * 100});
    mendItems(player, x);
    ctx.check(game::itemAt(player, where).getDamageValue() == 90, "5 points (maxXpPointsPerMending) repair 10, damage now " + std::to_string(game::itemAt(player, where).getDamageValue()));
    ctx.check(xpInTanks(bp) == 20 * 95, "5 points used");
    auto s      = Settings::of(x);
    s.mendItems = false;
    s.save(x);
    mendItems(player, x);
    ctx.check(game::itemAt(player, where).getDamageValue() == 90, "mending off: no repair");
    game::setItemAt(player, where, ItemStack{});
});

ABP_REGISTER_SELFTEST("xppump.ticks", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(core::Tier::Iron);
    ctx.installUpgrade(bp, "tank");
    ctx.installUpgrade(bp, std::string(kId));
    ctx.require(ctx.give(player, ctx.backpackItem(*bp), 9) == 9, "carry the backpack");
    setPlayerXp(player, api::xp::experienceForLevel(12));
    co_await ctx.ticks(40);
    ctx.check(playerXp(player) == api::xp::experienceForLevel(10), "the carried pump keeps level 10, at " + std::to_string(playerXp(player)));
    setPlayerXp(player, 0);
});

} // namespace
} // namespace abp::upgrade::xppump
