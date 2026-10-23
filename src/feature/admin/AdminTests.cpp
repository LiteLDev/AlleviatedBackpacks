// Selftests for the admin unit: admin.*
// Commands are executed for real through the command registry (console origin, and a player origin
// for the permission check), so parsing, overload selection and permissions are covered too.

#include <algorithm>
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
#include "mc/server/SimulatedPlayer.h"

#include "feature/admin/AccessLog.h"
#include "feature/admin/Admin.h"
#include "feature/admin/SbConfig.h"
#include "feature/admin/Sbp.h"
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
#include "upgrade/stack/StackUpgrade.h"

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

// ---- rules -----------------------------------------------------------------------------------------

ABP_REGISTER_SELFTEST("admin.deny_list", [](Context& ctx) {
    auto&   cfg = config::mut<SbServerConfig>();
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

ABP_REGISTER_SELFTEST("admin.container_items", [](Context& ctx) {
    auto&   cfg = config::mut<SbServerConfig>();
    Restore keepDisallowed(cfg.containerItemsDisallowed);
    Restore keepPlaced(cfg.canBePlacedInContainerItems);
    Restore keepShulkers(config::mut<GeneralConfig>().allowShulkerBoxes);
    config::mut<GeneralConfig>().allowShulkerBoxes = true;
    applyRules();
    auto bp = ctx.createBackpack(Tier::Leather);
    // SB default: container items (shulker boxes) fit in backpacks.
    cfg.containerItemsDisallowed = false;
    ctx.check(bp->inventory().insert(data("minecraft:red_shulker_box", 1), kPlayerAccess).inserted == 1, "shulker box allowed by default");
    cfg.containerItemsDisallowed = true;
    ctx.check(isDisallowed("minecraft:undyed_shulker_box"), "containerItemsDisallowed denies shulker boxes");
    ctx.check(bp->inventory().insert(data("minecraft:blue_shulker_box", 1), kPlayerAccess).inserted == 0, "shulker box refused");
    ctx.check(!isDisallowed(game::backpackItemId(Tier::Iron)), "backpacks are not container items");
    // canBePlacedInContainerItems (default false): backpacks don't go into shulker boxes.
    auto backpack = ctx.backpackItem(*bp);
    cfg.canBePlacedInContainerItems = false;
    ctx.check(!itemFitsContainerItems(backpack), "backpack refused by a shulker box");
    ctx.check(itemFitsContainerItems(game::ItemConv::make("minecraft:stone")), "stone still fits a shulker box");
    cfg.canBePlacedInContainerItems = true;
    ctx.check(itemFitsContainerItems(backpack), "backpack fits a shulker box when allowed");
});

ABP_REGISTER_SELFTEST("admin.slot_counts", [](Context& ctx) {
    auto&   cfg = config::mut<SbServerConfig>();
    Restore keep(cfg.ironBackpack.inventorySlotCount);
    Restore keepUp(cfg.ironBackpack.upgradeSlotCount);
    cfg.ironBackpack.inventorySlotCount = 60;
    cfg.ironBackpack.upgradeSlotCount   = 4;
    auto bp = ctx.createBackpack(Tier::Iron);
    ctx.check(bp->inventory().size() == 60, "configured slot count: " + std::to_string(bp->inventory().size()));
    ctx.check(bp->data().upgrades.size() == 4, "configured upgrade slot count");
    bp->inventory().set(58, data("minecraft:stone", 1));
    cfg.ironBackpack.inventorySlotCount = 54;
    cfg.ironBackpack.upgradeSlotCount   = 2;
    bp->rebuildHooks();
    ctx.check(bp->inventory().size() == 60, "never shrinks over items");
    ctx.check(bp->data().upgrades.size() == 2, "empty upgrade slots are cut");
    bp->inventory().set(58, {});
    bp->rebuildHooks();
    ctx.check(bp->inventory().size() == 54, "shrinks once the cut slots are empty");
    ctx.check(inventorySlotCount(Tier::Netherite) == 120 && upgradeSlotCount(Tier::Netherite) == 7, "SB defaults");
    cfg.ironBackpack.inventorySlotCount = 1000;
    ctx.check(inventorySlotCount(Tier::Iron) == 144, "clamped to SB's range");
});

ABP_REGISTER_SELFTEST("admin.max_upgrades", [](Context& ctx) {
    auto test                   = std::make_shared<AdminTestUpgrade>();
    test->mInfo.maxPerBackpack  = 10;
    upgrade::ScopedUpgrade scoped(test);
    auto&                  cfg = config::mut<SbServerConfig>();
    Restore                keep(cfg.maxUpgradesPerStorage);
    ctx.check(cfg.maxUpgradesPerStorage == std::vector<std::string>{"stack_upgrades|3", "furnace_upgrades|1", "jukebox_upgrades|1"}, "SB default");
    cfg.maxUpgradesPerStorage = {"admin_test_group|2"};
    auto bp = ctx.createBackpack(Tier::Netherite);
    ctx.check(ctx.installUpgrade(bp, "admin_test_upgrade", 0) == 0, "first");
    ctx.check(ctx.installUpgrade(bp, "admin_test_upgrade", 1) == 1, "second");
    std::string reason;
    ctx.check(!upgrade::canInstall(bp, 2, data("minecraft:structure_void", 1), nullptr, &reason), "third refused by group limit");
    ctx.check(contains(reason, "Only 2x"), "SC's message: " + reason);
    cfg.maxUpgradesPerStorage = {"minecraft:structure_void|1"};
    ctx.check(!upgrade::canInstall(bp, 2, data("minecraft:structure_void", 1), nullptr), "registry name limit");
    cfg.maxUpgradesPerStorage = {"structure_void|3"};
    ctx.check(upgrade::canInstall(bp, 2, data("minecraft:structure_void", 1), nullptr), "within the limit");
});

// SB's only per-backpack limits are `maxUpgradesPerStorage` (stack 3, furnace 1, jukebox 1) and the
// conflict rules: crafting, stonecutter, anvil, smithing and refill have none, and the three
// group limits follow the config.
ABP_REGISTER_SELFTEST("admin.sb_upgrade_limits", [](Context& ctx) {
    auto&   cfg = config::mut<SbServerConfig>();
    Restore keep(cfg.maxUpgradesPerStorage);
    for (auto const* id : {"crafting", "stonecutter", "anvil", "smithing", "refill"}) {
        auto bp = ctx.createBackpack(Tier::Netherite);
        ctx.check(ctx.installUpgrade(bp, id, 0) == 0 && ctx.installUpgrade(bp, id, 1) == 1, std::string(id) + ": two allowed");
        ctx.check(ctx.installUpgrade(bp, id, 2) == 2, std::string(id) + ": three allowed");
    }
    auto bp = ctx.createBackpack(Tier::Netherite);
    for (int i = 0; i < 3; ++i) ctx.check(ctx.installUpgrade(bp, "stack_starter", i) == i, "stack upgrade " + std::to_string(i));
    auto starter = data("alleviatedbackpacks:stack_starter_upgrade", 1);
    std::string reason;
    ctx.check(!upgrade::canInstall(bp, 3, starter, nullptr, &reason), "a 4th stack upgrade is refused");
    ctx.check(contains(reason, "Only 3x Stack Upgrades"), "SC group message: " + reason);
    auto hit = maxUpgradesHit(*bp, "alleviatedbackpacks:stack_starter_upgrade", "stack");
    ctx.check(hit && !hit->byItem && hit->slots == std::vector<int>{0, 1, 2}, "the three stack upgrades are the limiting slots");
    cfg.maxUpgradesPerStorage = {"stack_upgrades|4", "furnace_upgrades|2", "jukebox_upgrades|2"};
    ctx.check(upgrade::canInstall(bp, 3, starter, nullptr), "stack limit raised by config");
    ctx.check(ctx.installUpgrade(bp, "smelting", 3) == 3 && ctx.installUpgrade(bp, "auto_blasting", 4) == 4, "two furnace upgrades with furnace_upgrades|2");
    ctx.check(ctx.installUpgrade(bp, "jukebox", 5) == 5 && ctx.installUpgrade(bp, "advanced_jukebox", 6) == 6, "two jukeboxes with jukebox_upgrades|2");
    cfg.maxUpgradesPerStorage = {"furnace_upgrades|1"};
    auto furnace              = ctx.createBackpack(Tier::Iron);
    ctx.check(ctx.installUpgrade(furnace, "smelting", 0) == 0, "one furnace upgrade");
    ctx.check(!upgrade::canInstall(furnace, 1, data("alleviatedbackpacks:blasting_upgrade", 1), nullptr, &reason), "a second one is refused");
    ctx.check(contains(reason, "Only 1x Furnace Upgrades"), "SC group name (upgrade_group.cooking_upgrades): " + reason);
    cfg.maxUpgradesPerStorage = {"crafting_upgrade|0"};
    auto other = ctx.createBackpack(Tier::Iron);
    ctx.check(!upgrade::canInstall(other, 0, data("alleviatedbackpacks:crafting_upgrade", 1), nullptr, &reason), "|0 = not allowed");
    ctx.check(contains(reason, "can't be used in"), "SC upgrade_not_allowed: " + reason);
    cfg.maxUpgradesPerStorage = {};
    for (int i = 0; i < 2; ++i) ctx.check(ctx.installUpgrade(other, "smoking", i) == i, "no entries = no limit");
});

// SB BackpackStorage.load: storages without an access log record that hold nothing are dropped
// (once per world load); anything stored, an access record or pending mob loot keeps them.
ABP_REGISTER_SELFTEST("admin.drop_empty_on_load", [](Context& ctx) {
    auto uuidOf = [&](auto&& setup) {
        auto bp = ctx.createBackpack(Tier::Iron);
        setup(*bp);
        bp->markDirty();
        game::Storage::get().save(*bp);
        return bp->uuid();
    };
    auto empty    = uuidOf([](game::Backpack&) {});
    auto accessed = uuidOf([](game::Backpack& bp) {
        AccessLogRecord r;
        r.backpackItemRegistryName = "alleviatedbackpacks:iron_backpack";
        r.backpackUuid             = bp.uuid();
        r.playerName               = "selftest";
        AccessLog::get().put(r);
    });
    auto items    = uuidOf([](game::Backpack& bp) { bp.inventory().set(0, data("minecraft:dirt", 1)); });
    auto upgraded = ctx.createBackpack(Tier::Iron);
    ctx.require(ctx.installUpgrade(upgraded, "crafting") >= 0, "upgrade installed");
    auto upgradedUuid = upgraded->uuid();
    upgraded.reset();
    auto coloured = uuidOf([](game::Backpack& bp) { bp.data().mainColor = 0xFF112233u; });
    auto loot     = uuidOf([](game::Backpack& bp) { bp.data().settings[std::string(kPendingLootSetting)] = true; });
    auto removed  = dropEmptyStorages();
    auto gone     = [&](std::string const& u) { return std::find(removed.begin(), removed.end(), u) != removed.end(); };
    ctx.check(gone(empty) && !game::Storage::get().find(empty), "an empty, never accessed storage is dropped");
    ctx.check(!gone(accessed) && game::Storage::get().find(accessed), "an access log record keeps it");
    ctx.check(!gone(items), "items keep it");
    ctx.check(!gone(upgradedUuid), "upgrades keep it");
    ctx.check(!gone(coloured), "non-default data keeps it");
    ctx.check(!gone(loot), "an unopened mob backpack waiting for its loot keeps it");
    AccessLog::get().erase(accessed);
});

// config/stack.json v2 `maxPerBackpack` moves into SB's maxUpgradesPerStorage (stack_upgrades|N).
ABP_REGISTER_SELFTEST("admin.stack_limit_migrates", [](Context& ctx) {
    auto& sb         = config::mut<SbServerConfig>();
    auto  savedSb    = sb.maxUpgradesPerStorage;
    auto  savedStack = config::get<upgrade::stack::StackConfig>();
    auto  path       = std::filesystem::temp_directory_path() / "abp_stack_migrate.json";
    {
        std::ofstream out(path, std::ios::trunc);
        out << R"({"version":2,"maxPerBackpack":5,"tier1":2.0})";
    }
    for (auto const& e : config::ConfigRegistry::entries()) {
        if (e.section == "stack") (void)e.load(path);
    }
    ctx.check(upgrade::stack::StackConfig::legacyStackLimit() == 5, "the old limit is read");
    adoptLegacyStackLimit();
    ctx.check(!upgrade::stack::StackConfig::legacyStackLimit().has_value(), "handed over once");
    auto const& entries = sb.maxUpgradesPerStorage;
    ctx.check(std::count(entries.begin(), entries.end(), std::string("stack_upgrades|5")) == 1, "stack_upgrades|5");
    ctx.check(std::none_of(entries.begin(), entries.end(), [](std::string const& e) { return e == "stack_upgrades|3"; }), "SB default entry replaced");
    std::ifstream in(path);
    auto          written = nlohmann::json::parse(in, nullptr, false);
    in.close();
    ctx.check(written.value("version", 0) == 3 && !written.contains("maxPerBackpack"), "stack.json rewritten without the key");
    sb.maxUpgradesPerStorage                     = savedSb;
    config::mut<upgrade::stack::StackConfig>()   = savedStack;
    config::saveAll(); // the adoption saved the test values
    std::error_code ec;
    std::filesystem::remove(path, ec);
});

ABP_REGISTER_SELFTEST("admin.enabled_items", [](Context& ctx) {
    auto&   cfg = config::mut<ScCommonConfig>();
    Restore keep(cfg.enabledItems);
    bool listed = false;
    for (auto const& e : cfg.enabledItems) listed = listed || e == "alleviatedbackpacks:leather_backpack|true";
    ctx.check(listed, "enabledItems lists every item with |true");
    ctx.check(isItemEnabled("alleviatedbackpacks:pickup_upgrade"), "enabled by default");
    cfg.enabledItems = {"alleviatedbackpacks:pickup_upgrade|false"};
    ctx.check(!isItemEnabled("alleviatedbackpacks:pickup_upgrade"), "disabled");
    ctx.check(isItemEnabled("alleviatedbackpacks:magnet_upgrade"), "others unaffected");
});

ABP_REGISTER_SELFTEST("admin.block_lists", [](Context& ctx) {
    auto&   cfg = config::mut<SbServerConfig>();
    Restore keepA(cfg.noInteractionBlocks);
    Restore keepB(cfg.noConnectionBlocks);
    Restore keepC(cfg.allBlockConnectionsDisallowed);
    cfg.noInteractionBlocks = {"minecraft:barrel"};
    ctx.check(isBlockInteractionDisallowed("barrel") && !isBlockInteractionDisallowed("minecraft:chest"), "noInteractionBlocks");
    cfg.noConnectionBlocks = {"minecraft:hopper"};
    ctx.check(isBlockConnectionDisallowed("minecraft:hopper") && !isBlockConnectionDisallowed("minecraft:dropper"), "noConnectionBlocks");
    cfg.allBlockConnectionsDisallowed = true;
    ctx.check(isBlockConnectionDisallowed("minecraft:dropper"), "allBlockConnectionsDisallowed");
});
ABP_REGISTER_SELFTEST("admin.config_names", [](Context& ctx) {
    auto&   cfg = config::mut<AdminConfig>();
    Restore keepItems(config::mut<SbServerConfig>().disallowedItems);
    Restore keepUps(cfg.disabledUpgrades);
    cfg.disabledUpgrades = {"Alleviatedbackpacks:Pickup_Upgrade"};
    ctx.check(isUpgradeDisabled("pickup", "pickup"), "item id style entry, any case");
    cfg.disabledUpgrades = {"MAGNET"};
    ctx.check(isUpgradeDisabled("magnet", "magnet") && !isUpgradeDisabled("pickup", "pickup"), "plain id");
    config::mut<SbServerConfig>().disallowedItems = {"Minecraft:Bundle"};
    ctx.check(isDisallowed("minecraft:bundle"), "deny-list ignores case");
    // Extras SB doesn't have are off / neutral by default.
    AdminConfig defaults;
    ctx.check(!defaults.keepBackpacksOnDeath && defaults.maxNestingDepth == 0 && defaults.disabledUpgrades.empty(), "extras off by default");
});

// ---- access log / sbp ---------------------------------------------------------------------------------

ABP_REGISTER_SELFTEST("admin.access_log_on_open", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(Tier::Gold);
    bp->data().mainColor   = 0xFFFF5555;
    bp->data().accentColor = 0xFF5555FF;
    ctx.require(ctx.give(player, ctx.backpackItem(*bp), 3) == 3, "give backpack");
    ctx.require(ui::BackpackMenu::openFor(player, {game::ItemLocation::Kind::Inventory, 3}), "open");
    co_await ctx.ticks(5);
    auto rec = AccessLog::get().find(bp->uuid());
    ctx.require(rec.has_value(), "opening logs the access");
    ctx.check(rec->playerName == player.getRealName(), "player name");
    ctx.check(rec->backpackItemRegistryName == "alleviatedbackpacks:gold_backpack", "item id: " + rec->backpackItemRegistryName);
    ctx.check(rec->backpackName == "Gold Backpack", "display name: " + rec->backpackName);
    ctx.check(rec->clothColor == 0xFFFF5555 && rec->trimColor == 0xFF5555FF, "colours");
    ctx.check(rec->accessTime > 0 && epochMillis() - rec->accessTime < 60'000, "time");
    ctx.check(holderOf(*bp).first.has_value(), "opening also records the holder");
    ui::MenuManager::get().close(player);
});

ABP_REGISTER_SELFTEST("admin.access_log_cache", [](Context& ctx) {
    auto& log_ = AccessLog::get();
    AccessLogRecord r;
    r.backpackUuid             = "00000000-0000-4000-8000-00000000c0de";
    r.backpackItemRegistryName = "alleviatedbackpacks:leather_backpack";
    r.playerName               = "CacheTester";
    r.backpackName             = "Leather Backpack";
    r.accessTime               = epochMillis();
    (void)log_.playerNames(); // cache built now
    log_.put(r);
    ctx.check(!log_.playerNames().contains("CacheTester"), "player names are cached for 30 s (SB)");
    log_.invalidateCache();
    ctx.check(log_.playerNames().contains("CacheTester"), "visible after the cache refresh");
    ctx.check(log_.logsForPlayer("CacheTester").size() == 1, "logs for the player");
    auto back = AccessLogRecord::fromJson(r.toJson());
    ctx.check(back && *back == r, "record JSON round trip");
    log_.erase(r.backpackUuid);
});

ABP_REGISTER_SELFTEST("admin.sbp_list_give", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(Tier::Diamond);
    ctx.require(ui::BackpackMenu::openBackpack(player, bp), "open");
    co_await ctx.ticks(3);
    ui::MenuManager::get().close(player);
    ctx.require(AccessLog::get().contains(bp->uuid()), "logged");
    refreshSbpSuggestions();

    auto all = runCommand("sbp list");
    ctx.check(all.ok, "sbp list succeeds: " + all.text);
    ctx.check(contains(all.text, "Backpack Name, Colors, Last Open By, Last Open On"), "SB header: " + all.text);
    ctx.check(contains(all.text, "Diamond Backpack") && contains(all.text, player.getRealName()), "line: " + all.text);
    ctx.check(contains(all.text, "Cloth") && contains(all.text, "Trim"), "colour swatches");
    auto alias = runCommand("sophisticatedbackpacks list");
    ctx.check(alias.ok, "alias /sophisticatedbackpacks: " + alias.text);
    auto viaBackpack = runCommand("backpack list");
    ctx.check(viaBackpack.ok, "/backpack list still works: " + viaBackpack.text);

    AccessLog::get().invalidateCache();
    auto mine = runCommand("sbp list \"" + player.getRealName() + "\"");
    ctx.check(mine.ok && contains(mine.text, "Diamond Backpack"), "list <playerName>: " + mine.text);

    // Give to the player: an item bound to the logged uuid.
    for (int i = 0; i < 36; ++i) game::setItemAt(player, {game::ItemLocation::Kind::Inventory, i}, ItemStack::EMPTY_ITEM());
    auto r = runCommand("sbp give \"" + player.getRealName() + "\" " + bp->uuid());
    ctx.check(r.ok && contains(r.text, "Gave backpack to"), "give succeeds: " + r.text);
    int found = 0;
    for (auto const& c : game::scanBackpacks(player)) {
        if (c.uuid == bp->uuid()) {
            ++found;
            ctx.check(c.tier == Tier::Diamond, "given item has the logged item id");
        }
    }
    ctx.check(found == 1, "player received an item bound to the uuid");
    // Several targets: everybody gets one (SB), the dedup logic sorts the copies out later.
    auto& other = ctx.spawnPlayer();
    auto  many  = runCommand("sbp give @a " + bp->uuid());
    ctx.check(many.ok, "several targets: " + many.text);
    bool otherGot = false;
    for (auto const& c : game::scanBackpacks(other)) otherGot = otherGot || c.uuid == bp->uuid();
    ctx.check(otherGot, "second target received it");
    auto bad = runCommand("sbp give \"" + player.getRealName() + "\" 00000000-0000-4000-8000-000000000000");
    ctx.check(!contains(bad.text, "Gave backpack"), "unknown uuid gives nothing");
});

ABP_REGISTER_SELFTEST("admin.permissions", [](Context& ctx) {
    auto& player = ctx.player();
    auto& cfg    = config::mut<AdminConfig>();
    Restore keep(cfg.commandPermissionLevel);
    cfg.commandPermissionLevel = 4; // owner only: a simulated player never qualifies
    auto bp = ctx.createBackpack(Tier::Leather);
    for (std::string const& cmd : std::vector<std::string>{"backpack stats", "backpack cleanup", "backpack remove " + bp->uuid(),
                                                           "backpack openfor " + bp->uuid(), "backpack list",
                                                           "backpack removeNonPlayer false", "sbp list", "sbp removeNonPlayer false"}) {
        auto r = runCommand(cmd, &player);
        ctx.check(!r.ok, cmd + " must be refused for non-operators: " + r.text);
    }
    ctx.check(game::Storage::get().find(bp->uuid()) != nullptr, "nothing removed by a refused command");
    ctx.check(runCommand("backpack stats").ok, "console may run admin commands");
    ctx.check(runCommand("sbp list").ok, "console may run /sbp");
});

ABP_REGISTER_SELFTEST("admin.info_and_stats", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    auto  held   = ctx.createBackpack(Tier::Gold);
    auto  loose  = ctx.createBackpack(Tier::Copper);
    held->inventory().insert(data("minecraft:diamond", 7), core::AccessContext{core::Source::Internal});
    ctx.require(ctx.give(player, ctx.backpackItem(*held), 2) == 2, "give backpack");
    co_await ctx.ticks(config::get<AdminConfig>().holderScanInterval + 5);
    auto rec = holderOf(*held);
    ctx.require(rec.first && rec.last, "holder recorded");
    ctx.check(rec.last->name == player.getRealName(), "last holder is the player");
    ctx.check(playerAccessed(*held) && !playerAccessed(*loose), "access flags");
    auto info = runCommand("backpack info " + held->uuid().substr(0, 13));
    ctx.check(info.ok, "info by uuid prefix: " + info.text);
    ctx.check(contains(info.text, "1/81") && contains(info.text, " 7 "), "info shows fill: " + info.text);
    auto st = runCommand("backpack stats");
    ctx.check(st.ok && contains(st.text, "gold="), "stats: " + st.text);
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
    ctx.check(!AccessLog::get().contains(bp->uuid()), "nor an access log entry");
    mm.close(player);
    ctx.check(!runCommand("backpack openfor " + bp->uuid()).ok, "console can't open a GUI");
});

// ---- maintenance ---------------------------------------------------------------------------------------

ABP_REGISTER_SELFTEST("admin.remove_non_player", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player  = ctx.player();
    auto  opened  = ctx.createBackpack(Tier::Leather);
    auto  mobLoot = ctx.createBackpack(Tier::Leather);
    auto  empty   = ctx.createBackpack(Tier::Leather);
    mobLoot->inventory().insert(data("minecraft:bone", 4), core::AccessContext{core::Source::Internal});
    ctx.require(ui::BackpackMenu::openBackpack(player, opened), "open");
    co_await ctx.ticks(3);
    ui::MenuManager::get().close(player);
    auto openedId = opened->uuid();
    auto mobId    = mobLoot->uuid();
    auto emptyId  = empty->uuid();
    opened.reset();
    mobLoot.reset();
    empty.reset();
    auto r = runCommand("sbp removeNonPlayer true");
    ctx.check(r.ok && contains(r.text, "non player backpacks"), "removeNonPlayer true: " + r.text);
    ctx.check(game::Storage::get().find(emptyId) == nullptr, "empty never-opened backpack removed");
    ctx.check(game::Storage::get().find(mobId) != nullptr, "never-opened backpack with items kept (onlyWithEmptyInventory)");
    r = runCommand("sbp removeNonPlayer false");
    ctx.check(r.ok, "removeNonPlayer false: " + r.text);
    ctx.check(game::Storage::get().find(mobId) == nullptr, "never-opened backpack with items removed");
    ctx.check(game::Storage::get().find(openedId) != nullptr, "opened backpack kept");
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
    auto path = config::configDir() / "sophisticatedbackpacks-server.json";
    ctx.require(std::filesystem::exists(path), "config/sophisticatedbackpacks-server.json exists");
    std::string original;
    {
        std::ifstream in(path, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        original = ss.str();
    }
    auto j = nlohmann::json::parse(original, nullptr, false);
    ctx.require(j.is_object(), "the server config is JSON");
    ctx.check(j.contains("entityBackpackAdditions") && j.contains("nerfs") && j.contains("maxUpgradesPerStorage"), "SB sections present");
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
