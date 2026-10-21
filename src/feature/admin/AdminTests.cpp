// Selftests for the admin unit: admin.*
// Commands are executed for real through the command registry (console origin, and a player origin
// for the permission check), so parsing, overload selection and permissions are covered too.

#include <filesystem>
#include <fstream>
#include <sstream>

#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/service/Bedrock.h"

#include "mc/server/ServerLevel.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/PlayerCommandOrigin.h"
#include "mc/server/commands/ServerCommandOrigin.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"

#include "feature/admin/Admin.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "game/Storage.h"
#include "mod/Config.h"
#include "mod/GeneralConfig.h"
#include "selftest/SelfTest.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"

namespace abp::admin {
namespace {

using selftest::Context;
using core::Tier;

core::AccessContext const kPlayerAccess{core::Source::Player};

core::ItemStackData data(std::string_view id, int count) { return game::ItemConv::toData(game::ItemConv::make(id, count)); }

struct CmdResult {
    bool        ok = false;
    std::string text; ///< every output message, one per line
};

CmdResult runCommand(std::string const& command, Player* as = nullptr) {
    CmdResult r;
    auto      level = ll::service::getLevel();
    if (!level) return r;
    auto&         registrar = ll::command::CommandRegistrar::getServerInstance();
    CommandOutput out       = [&] {
        if (as) {
            PlayerCommandOrigin origin(*level, as->getOrCreateUniqueID());
            return registrar.executeCommand(command, origin);
        }
        ServerCommandOrigin origin("abp-selftest", level->asServer(), CommandPermissionLevel::Owner, 0);
        return registrar.executeCommand(command, origin);
    }();
    r.ok = out.mSuccessCount > 0;
    for (auto const& m : out.mMessages) {
        r.text += m.mMessageId;
        for (auto const& p : m.mParams) r.text += " " + p;
        r.text += "\n";
    }
    return r;
}

bool contains(std::string const& hay, std::string const& needle) { return hay.find(needle) != std::string::npos; }

/// Restores a config value when the test ends (also on abort).
template <class T>
struct Restore {
    T& ref;
    T  saved;
    explicit Restore(T& r) : ref(r), saved(r) {}
    ~Restore() { ref = saved; }
};

// ---- rules -----------------------------------------------------------------------------------------

ABP_REGISTER_SELFTEST("admin.deny_list", [](Context& ctx) {
    auto&   cfg = config::mut<AdminConfig>();
    Restore keep(cfg.disallowedItems);
    auto    bp = ctx.createBackpack(Tier::Leather);
    cfg.disallowedItems = {"dirt", "minecraft:*_wool"};
    ctx.check(isDisallowed("minecraft:dirt"), "plain id defaults to the minecraft namespace");
    ctx.check(isDisallowed("minecraft:red_wool"), "glob matches");
    ctx.check(!isDisallowed("minecraft:stone"), "other items allowed");
    auto& inv = bp->inventory();
    ctx.check(inv.insert(data("minecraft:dirt", 5), kPlayerAccess).inserted == 0, "denied item refused (player)");
    ctx.check(
        inv.insert(data("minecraft:white_wool", 5), core::AccessContext{core::Source::Pickup}).inserted == 0,
        "denied item refused (pickup)"
    );
    ctx.check(inv.insert(data("minecraft:stone", 5), kPlayerAccess).inserted == 5, "allowed item accepted");
    ctx.check(
        inv.insert(data("minecraft:dirt", 1), core::AccessContext{core::Source::Internal}).inserted == 1,
        "internal moves bypass the deny-list"
    );
    cfg.disallowedItems.clear();
    ctx.check(inv.insert(data("minecraft:dirt", 5), kPlayerAccess).inserted == 5, "deny-list change applies live");
});

class AdminTestUpgrade : public upgrade::IUpgrade {
public:
    upgrade::UpgradeInfo mInfo{
        .id           = "admin_test_upgrade",
        .itemId       = "minecraft:structure_void",
        .nameKey      = "abp.admin.test_upgrade",
        .nameFallback = "Admin Test Upgrade",
        .group        = "admin_test_group",
    };
    int ticks = 0;
    [[nodiscard]] upgrade::UpgradeInfo const& info() const override { return mInfo; }
    void attach(upgrade::UpgradeContext const& ctx, core::BackpackInventory& inv) override {
        inv.addInsertFilter(ctx.hookOwner(), 0, [](core::InsertQuery const& q) {
            return q.stack.id == "minecraft:gravel" ? core::InsertVerdict::Deny : core::InsertVerdict::Allow;
        });
    }
    void onTick(upgrade::TickContext const&) override { ++ticks; }
};

ABP_REGISTER_SELFTEST("admin.disabled_upgrades", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto                   test = std::make_shared<AdminTestUpgrade>();
    upgrade::ScopedUpgrade scoped(test);
    auto&                  cfg = config::mut<AdminConfig>();
    Restore                keep(cfg.disabledUpgrades);
    auto&                  player = ctx.player();
    auto                   bp     = ctx.createBackpack(Tier::Iron);
    ctx.installUpgrade(bp, "admin_test_upgrade", 0);
    ctx.require(ctx.give(player, ctx.backpackItem(*bp), 4) == 4, "give backpack");
    ctx.check(bp->inventory().insert(data("minecraft:gravel", 1), kPlayerAccess).inserted == 0, "upgrade active");

    cfg.disabledUpgrades = {"admin_test_group"}; // by group
    applyRules();
    ctx.check(bp->inventory().insert(data("minecraft:gravel", 1), kPlayerAccess).inserted == 1, "filter gone when disabled");
    ctx.check(!upgrade::hasActive(*bp, "admin_test_upgrade"), "hasActive is false");
    auto        other = ctx.createBackpack(Tier::Iron);
    std::string reason;
    ctx.check(!upgrade::canInstall(other, 0, data("minecraft:structure_void", 1), nullptr, &reason), "can't install");
    ctx.check(!reason.empty(), "install refusal has a reason");
    int before = test->ticks;
    co_await ctx.ticks(30);
    ctx.check(test->ticks == before, "a disabled upgrade does not tick");

    cfg.disabledUpgrades = {};
    applyRules();
    co_await ctx.ticks(30);
    ctx.check(test->ticks > before, "re-enabled upgrade ticks again");
    ctx.check(bp->inventory().insert(data("minecraft:gravel", 1), kPlayerAccess).inserted == 0, "filter back");
    ctx.check(upgrade::canInstall(other, 0, data("minecraft:structure_void", 1), nullptr), "installable again");
});

ABP_REGISTER_SELFTEST("admin.nesting_depth", [](Context& ctx) {
    auto&   general = config::mut<GeneralConfig>();
    auto&   cfg     = config::mut<AdminConfig>();
    Restore keepNesting(general.allowNesting);
    Restore keepDepth(cfg.maxNestingDepth);
    general.allowNesting = true;
    cfg.maxNestingDepth  = 1;
    auto a = ctx.createBackpack(Tier::Leather);
    auto b = ctx.createBackpack(Tier::Leather);
    auto c = ctx.createBackpack(Tier::Leather);
    auto itemB = game::ItemConv::toData(ctx.backpackItem(*b));
    auto itemC = game::ItemConv::toData(ctx.backpackItem(*c));
    ctx.check(b->inventory().insert(itemC, kPlayerAccess).inserted == 1, "depth 1 allowed");
    ctx.check(nestedDepth(b->uuid(), 5) == 1, "B holds one level");
    ctx.check(a->inventory().insert(itemB, kPlayerAccess).inserted == 0, "depth 2 refused with max 1");
    cfg.maxNestingDepth = 2;
    ctx.check(a->inventory().insert(itemB, kPlayerAccess).inserted == 1, "depth 2 allowed with max 2");
    ctx.check(nestedDepth(a->uuid(), 5) == 2, "A holds two levels");
    // Inserting into a backpack that is itself nested counts the levels above it: A > B > C > D.
    auto d     = ctx.createBackpack(Tier::Leather);
    auto itemD = game::ItemConv::toData(ctx.backpackItem(*d));
    ctx.check(c->inventory().insert(itemD, kPlayerAccess).inserted == 0, "depth 3 via a nested target refused");
    cfg.maxNestingDepth = 3;
    ctx.check(c->inventory().insert(itemD, kPlayerAccess).inserted == 1, "depth 3 allowed with max 3");
    // Taken out again: the stale link no longer counts.
    a->inventory().set(0, {});
    cfg.maxNestingDepth = 2;
    auto e     = ctx.createBackpack(Tier::Leather);
    auto itemE = game::ItemConv::toData(ctx.backpackItem(*e));
    ctx.check(d->inventory().insert(itemE, kPlayerAccess).inserted == 0, "B > C > D > E is 3 levels");
    cfg.maxNestingDepth = 0;
    ctx.check(d->inventory().insert(itemE, kPlayerAccess).inserted == 1, "0 = unlimited");
});

ABP_REGISTER_SELFTEST("admin.config_names", [](Context& ctx) {
    auto&   cfg = config::mut<AdminConfig>();
    Restore keepItems(cfg.disallowedItems);
    Restore keepUps(cfg.disabledUpgrades);
    cfg.disabledUpgrades = {"Alleviatedbackpacks:Pickup_Upgrade"};
    ctx.check(isUpgradeDisabled("pickup", "pickup"), "item id style entry, any case");
    cfg.disabledUpgrades = {"MAGNET"};
    ctx.check(isUpgradeDisabled("magnet", "magnet") && !isUpgradeDisabled("pickup", "pickup"), "plain id");
    cfg.disallowedItems = {"Minecraft:Bundle"};
    ctx.check(isDisallowed("minecraft:bundle"), "deny-list ignores case");
});

// ---- holders / list / info / stats -------------------------------------------------------------------

ABP_REGISTER_SELFTEST("admin.track_and_list", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    auto  held   = ctx.createBackpack(Tier::Gold);
    auto  loose  = ctx.createBackpack(Tier::Copper);
    held->inventory().insert(data("minecraft:diamond", 7), core::AccessContext{core::Source::Internal});
    ctx.require(ctx.give(player, ctx.backpackItem(*held), 2) == 2, "give backpack");
    co_await ctx.ticks(config::get<AdminConfig>().holderScanInterval + 5);
    auto rec = holderOf(*held);
    ctx.require(rec.first && rec.last, "holder recorded");
    ctx.check(rec.last->name == player.getRealName(), "last holder is the player");
    ctx.check(rec.lastSeen > 0, "last seen set");
    ctx.check(playerAccessed(*held) && !playerAccessed(*loose), "access flags");

    auto all = runCommand("backpack list");
    ctx.check(all.ok, "list succeeds: " + all.text);
    ctx.check(contains(all.text, held->uuid()) && contains(all.text, loose->uuid()), "list shows both backpacks");
    ctx.check(contains(all.text, player.getRealName()), "list shows the holder");
    auto mine = runCommand("backpack list \"" + player.getRealName() + "\"");
    ctx.check(mine.ok && contains(mine.text, held->uuid()), "list <holder> shows the held backpack");
    ctx.check(!contains(mine.text, loose->uuid()), "list <holder> hides others");

    auto info = runCommand("backpack info " + held->uuid().substr(0, 13));
    ctx.check(info.ok, "info by uuid prefix: " + info.text);
    ctx.check(contains(info.text, "1/81") && contains(info.text, " 7 "), "info shows fill: " + info.text);

    auto st = runCommand("backpack stats");
    ctx.check(st.ok && contains(st.text, "gold="), "stats: " + st.text);
    auto s = stats();
    ctx.check(s.stored >= 2 && s.neverAccessed >= 1, "stats counts");
});

// ---- give / remove / openfor / permissions ---------------------------------------------------------------

ABP_REGISTER_SELFTEST("admin.give_by_uuid", [](Context& ctx) {
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(Tier::Diamond);
    auto  r      = runCommand("backpack give \"" + player.getRealName() + "\" " + bp->uuid());
    ctx.check(r.ok, "give succeeds: " + r.text);
    bool found = false;
    for (auto const& c : game::scanBackpacks(player)) {
        if (c.uuid == bp->uuid()) {
            found = true;
            ctx.check(c.tier == Tier::Diamond, "given item has the backpack's tier");
        }
    }
    ctx.check(found, "player received an item bound to the uuid");
    auto bad = runCommand("backpack give \"" + player.getRealName() + "\" 00000000-0000-4000-8000-000000000000");
    ctx.check(!bad.ok, "unknown uuid refused");
    auto shortId = runCommand("backpack give \"" + player.getRealName() + "\" ab");
    ctx.check(!shortId.ok, "too short uuid refused");
    (void)ctx.spawnPlayer();
    auto many = runCommand("backpack give @a " + bp->uuid());
    ctx.check(!many.ok, "several targets refused (copies share storage): " + many.text);
});

ABP_REGISTER_SELFTEST("admin.record_on_open", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(Tier::Leather);
    ctx.require(ui::BackpackMenu::openBackpack(player, bp), "open");
    co_await ctx.ticks(8);
    ctx.check(holderOf(*bp).first.has_value(), "opening a backpack records the player at once");
    ui::MenuManager::get().close(player);
});

ABP_REGISTER_SELFTEST("admin.remove", [](Context& ctx) {
    auto bp   = ctx.createBackpack(Tier::Leather);
    auto uuid = bp->uuid();
    bp.reset();
    auto r = runCommand("backpack remove " + uuid);
    ctx.check(r.ok, "remove succeeds: " + r.text);
    ctx.check(game::Storage::get().find(uuid) == nullptr, "storage gone");
    ctx.check(!runCommand("backpack remove " + uuid).ok, "second remove fails");
});

ABP_REGISTER_SELFTEST("admin.permissions", [](Context& ctx) {
    auto& player = ctx.player();
    auto& cfg    = config::mut<AdminConfig>();
    Restore keep(cfg.commandPermissionLevel);
    cfg.commandPermissionLevel = 4; // owner only: a simulated player never qualifies
    auto bp = ctx.createBackpack(Tier::Leather);
    for (std::string const& cmd : std::vector<std::string>{"backpack list", "backpack stats", "backpack removenonplayer", "backpack cleanup",
                            "backpack remove " + bp->uuid(), "backpack openfor " + bp->uuid()}) {
        auto r = runCommand(cmd, &player);
        ctx.check(!r.ok, cmd + " must be refused for non-operators: " + r.text);
    }
    ctx.check(game::Storage::get().find(bp->uuid()) != nullptr, "nothing removed by a refused command");
    ctx.check(runCommand("backpack stats").ok, "console may run admin commands");
});

ABP_REGISTER_SELFTEST("admin.openfor", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    auto& mm     = ui::MenuManager::get();
    auto& cfg    = config::mut<AdminConfig>();
    Restore keep(cfg.commandPermissionLevel);
    cfg.commandPermissionLevel = 0; // let the simulated player run it
    auto bp = ctx.createBackpack(Tier::Iron);
    bp->inventory().insert(data("minecraft:emerald", 3), core::AccessContext{core::Source::Internal});
    auto r = runCommand("backpack openfor " + bp->uuid(), &player);
    ctx.check(r.ok, "openfor succeeds: " + r.text);
    co_await ctx.ticks(12); // opened after a short delay (chat screen closing)
    auto menu = std::dynamic_pointer_cast<ui::BackpackMenu>(mm.current(player));
    ctx.require(menu != nullptr, "a backpack menu is open");
    ctx.check(menu->backpack() == bp, "the menu shows the requested backpack");
    co_await ctx.ticks(5);
    mm.forceShown(player);
    ctx.check(mm.shownItem(player, 0).getTypeName() == "minecraft:emerald", "contents visible to the admin");
    co_await ctx.ticks(8);
    ctx.check(!holderOf(*bp).first.has_value(), "inspecting does not make the admin a holder");
    mm.close(player);
    ctx.check(!runCommand("backpack openfor " + bp->uuid()).ok, "console can't open a GUI");
});

// ---- maintenance ---------------------------------------------------------------------------------------

ABP_REGISTER_SELFTEST("admin.removenonplayer", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player  = ctx.player();
    auto  held    = ctx.createBackpack(Tier::Leather);
    auto  mobLoot = ctx.createBackpack(Tier::Leather);
    mobLoot->inventory().insert(data("minecraft:bone", 4), core::AccessContext{core::Source::Internal});
    ctx.require(ctx.give(player, ctx.backpackItem(*held), 5) == 5, "give backpack");
    co_await ctx.ticks(config::get<AdminConfig>().holderScanInterval + 5);
    ctx.require(playerAccessed(*held), "held backpack recorded");
    // Taken out of the inventory again: still player-owned.
    game::setItemAt(player, {game::ItemLocation::Kind::Inventory, 5}, ItemStack::EMPTY_ITEM());
    auto heldId = held->uuid();
    auto mobId  = mobLoot->uuid();
    held.reset();
    mobLoot.reset();
    auto r = runCommand("backpack removenonplayer");
    ctx.check(r.ok, "removenonplayer succeeds: " + r.text);
    ctx.check(game::Storage::get().find(mobId) == nullptr, "never-carried backpack removed");
    ctx.check(game::Storage::get().find(heldId) != nullptr, "player backpack kept");
});

ABP_REGISTER_SELFTEST("admin.cleanup", [](Context& ctx) {
    auto empty = ctx.createBackpack(Tier::Leather);
    auto full  = ctx.createBackpack(Tier::Leather);
    auto inUse = ctx.createBackpack(Tier::Leather);
    full->inventory().insert(data("minecraft:stone", 1), core::AccessContext{core::Source::Internal});
    auto emptyId = empty->uuid();
    auto fullId  = full->uuid();
    empty.reset();
    full.reset();
    auto r = runCommand("backpack cleanup");
    ctx.check(r.ok, "cleanup succeeds: " + r.text);
    ctx.check(game::Storage::get().find(emptyId) == nullptr, "empty backpack removed");
    ctx.check(game::Storage::get().find(fullId) != nullptr, "backpack with items kept");
    ctx.check(game::Storage::get().find(inUse->uuid()) != nullptr, "backpack in use kept");
});

// ---- config reload -------------------------------------------------------------------------------------

ABP_REGISTER_SELFTEST("admin.reload", [](Context& ctx) {
    auto path = config::configDir() / "admin.json";
    ctx.require(std::filesystem::exists(path), "config/admin.json exists");
    std::string original;
    {
        std::ifstream in(path, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        original = ss.str();
    }
    auto j = nlohmann::json::parse(original, nullptr, false);
    ctx.require(j.is_object(), "admin.json is JSON");
    j["disallowedItems"] = {"minecraft:cobweb"};
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << j.dump(4);
    }
    auto r = runCommand("backpack reload");
    ctx.check(r.ok, "reload succeeds: " + r.text);
    ctx.check(isDisallowed("minecraft:cobweb"), "reloaded deny-list applies");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << original;
    }
    runCommand("backpack reload");
    ctx.check(!isDisallowed("minecraft:cobweb"), "restored config");
});

// ---- keep on death ---------------------------------------------------------------------------------------

ABP_REGISTER_SELFTEST("admin.keep_on_death_stash", [](Context& ctx) {
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(Tier::Copper);
    ctx.require(ctx.give(player, ctx.backpackItem(*bp), 6) == 6, "give backpack");
    ctx.check(stashOnDeath(player) == 0 || config::get<AdminConfig>().keepBackpacksOnDeath, "off by default");
    if (hasStash(player)) restoreStash(player);
    ctx.require(game::itemAt(player, {game::ItemLocation::Kind::Inventory, 6}).isNull() == false, "backpack still there");
    ctx.check(stashOnDeath(player, true) == 1, "stashed one backpack");
    ctx.check(game::itemAt(player, {game::ItemLocation::Kind::Inventory, 6}).isNull(), "removed from the inventory");
    ctx.check(hasStash(player), "stash recorded");
    ctx.check(stats().deathStashes >= 1, "stash persisted");
    ctx.check(restoreStash(player) == 1, "restored one backpack");
    auto const& back = game::itemAt(player, {game::ItemLocation::Kind::Inventory, 6});
    ctx.check(game::backpackUuid(back) == bp->uuid(), "same backpack back in the same slot");
    ctx.check(!hasStash(player), "stash cleared");
});

ABP_REGISTER_SELFTEST("admin.keep_on_death_real", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto&   cfg = config::mut<AdminConfig>();
    Restore keep(cfg.keepBackpacksOnDeath);
    cfg.keepBackpacksOnDeath = true;
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(Tier::Leather);
    ctx.require(ctx.give(player, ctx.backpackItem(*bp), 7) == 7, "give backpack");
    player.kill();
    co_await ctx.ticks(2);
    ctx.check(hasStash(player), "backpack kept on death");
    bool dropped = false;
    for (auto const& c : game::scanBackpacks(player)) dropped |= c.uuid == bp->uuid();
    ctx.check(!dropped, "backpack not left in the dead player's inventory");
    player.respawn();
    co_await ctx.ticks(20);
    bool back = false;
    for (auto const& c : game::scanBackpacks(player)) back |= c.uuid == bp->uuid();
    ctx.check(back, "backpack returned after respawn");
    ctx.check(!hasStash(player), "stash cleared after respawn");
});

} // namespace
} // namespace abp::admin
