// P2-1 (main backpack screen): server side of the native storage screen's SC behaviour, plus
// `/backpack mainscreen setup <tier>` for the client e2e scenario `mainscreen`.
//
// Selftests `mainscreen.*` drive `net::handleScreenAction` with a simulated companion player.

#include <algorithm>

#include "ll/api/command/CommandHandle.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/PlayerUISlot.h"
#include "mc/world/item/ItemStack.h"

#include "core/Tier.h"
#include "feature/gui/Gui.h"
#include "game/Backpack.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/Storage.h"
#include "mod/Commands.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "net/Companion.h"
#include "net/NativeScreens.h"
#include "selftest/SelfTest.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/IUpgrade.h"
#include "upgrade/UpgradeManager.h"

/// `/backpack mainscreen setup <tier>` (external linkage: command enum).
enum class MainScreenTier { leather, copper, iron, gold, diamond, netherite };
struct MainScreenSetupParams {
    MainScreenTier tier;
};

namespace abp::feature::gui {
namespace {

using core::Tier;
using nlohmann::json;
using selftest::Context;

core::ItemStackData data(std::string_view id, int count) {
    return game::ItemConv::toData(game::ItemConv::make(id, std::min(count, 64))).withCount(count);
}

ItemStack cursorOf(Player& p) { return p.mPlayerUIContainer->getItem(0); }
void      setCursor(Player& p, ItemStack const& s) { p.setPlayerUIItem(PlayerUISlot::CursorSelected, s, false); }

/// Marks the simulated player as a companion (native screens) for one test.
struct Companion {
    Player& player;
    explicit Companion(Player& p) : player(p) {
        net::setCompanion(p, net::CompanionInfo{"test", {"native_screen"}});
        net::setTestSink([](Player&, std::string_view, json const&) {});
    }
    ~Companion() {
        setCursor(player, ItemStack::EMPTY_ITEM());
        ui::MenuManager::get().close(player);
        net::setTestSink(nullptr);
        net::setCompanion(player, std::nullopt);
    }
};

std::uint32_t open(Context& ctx, Player& p, std::shared_ptr<game::Backpack> const& bp) {
    ctx.require(ui::BackpackMenu::openBackpack(p, bp), "open backpack menu");
    ui::MenuManager::get().forceShown(p);
    auto s = net::nativeSession(p);
    ctx.require(s != 0, "native session");
    return s;
}

bool act(Player& p, std::uint32_t s, std::string const& name, json extra = json::object()) {
    extra["session"] = s;
    extra["action"]  = name;
    return net::handleScreenAction(p, extra);
}

int invCount(Player& p, std::string_view id) {
    int n = 0;
    for (int i = 0; i < 36; ++i) {
        auto const& it = p.getInventory().getItem(i);
        if (!it.isNull() && it.getTypeName() == id) n += it.mCount;
    }
    return n;
}

void clearInventory(Player& p) {
    for (int i = 0; i < 36; ++i) p.getInventory().setItem(i, ItemStack::EMPTY_ITEM());
}

ABP_REGISTER_SELFTEST("mainscreen.sort_by_order", [](Context& ctx) {
    auto& p  = ctx.player();
    auto  bp = ctx.createBackpack(Tier::Leather);
    Companion c(p);
    auto      s = open(ctx, p, bp);
    setBackpackOption(*bp, kSortBy, std::string("name"));
    std::vector<std::string> seen;
    for (int i = 0; i < 4; ++i) {
        ctx.check(act(p, s, "sortBy"), "sortBy accepted");
        seen.emplace_back(core::toString(effective(&p, *bp).sortBy));
    }
    ctx.check(seen == std::vector<std::string>{"mod", "count", "tag", "name"}, "SB order Name -> Mod -> Count -> Tags");
    setBackpackOption(*bp, kSortBy, std::nullopt);
});

ABP_REGISTER_SELFTEST("mainscreen.sort_by_display_name", [](Context& ctx) {
    auto  bp  = ctx.createBackpack(Tier::Leather);
    auto& inv = bp->inventory();
    inv.set(0, data("minecraft:oak_planks", 3));
    inv.set(1, data("minecraft:apple", 3));
    inv.set(2, data("minecraft:diamond", 3));
    ctx.info("server names: " + itemDisplayName(inv.at(0)) + ", " + itemDisplayName(inv.at(1)) + ", " + itemDisplayName(inv.at(2)));
    ctx.require(sortBackpack(*bp, core::SortBy::Name), "sort by name");
    std::vector<std::string> names;
    for (int i = 0; i < 3; ++i) names.push_back(itemDisplayName(inv.at(i)));
    auto lower = names;
    for (auto& n : lower) std::transform(n.begin(), n.end(), n.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    ctx.check(std::is_sorted(lower.begin(), lower.end()), "sorted by display name: " + names[0] + ", " + names[1] + ", " + names[2]);
});

ABP_REGISTER_SELFTEST("mainscreen.transfer_rules", [](Context& ctx) {
    auto& p  = ctx.player();
    auto  bp = ctx.createBackpack(Tier::Iron);
    clearInventory(p);
    bp->inventory().set(0, data("minecraft:apple", 5));
    ctx.give(p, game::ItemConv::make("minecraft:apple", 7), 2);  // hotbar
    ctx.give(p, game::ItemConv::make("minecraft:apple", 9), 20); // main inventory
    ctx.give(p, game::ItemConv::make("minecraft:stick", 4), 21);
    Companion c(p);
    auto      s = open(ctx, p, bp);
    // Matching into the backpack: main inventory only (the hotbar apples stay), sticks stay.
    ctx.check(act(p, s, "transfer", {{"to", "storage"}, {"matching", true}}), "transfer matching to storage");
    ctx.check(p.getInventory().getItem(2).mCount == 7 && p.getInventory().getItem(20).isNull(), "hotbar untouched, slot 20 moved");
    ctx.check(invCount(p, "minecraft:stick") == 4, "non-matching sticks stay");
    // Everything into the backpack: still never the hotbar.
    ctx.check(act(p, s, "transfer", {{"to", "storage"}, {"matching", false}}), "transfer all to storage");
    ctx.check(invCount(p, "minecraft:stick") == 0 && p.getInventory().getItem(2).mCount == 7, "sticks moved, hotbar kept");
    // Everything to the inventory: into slots 9..35 only.
    ctx.check(act(p, s, "transfer", {{"to", "inventory"}, {"matching", false}}), "transfer all to inventory");
    bool hotbarOnlyApples = true;
    for (int i = 0; i < 9; ++i) {
        auto const& it = p.getInventory().getItem(i);
        if (i != 2 && !it.isNull()) hotbarOnlyApples = false;
    }
    ctx.check(hotbarOnlyApples && p.getInventory().getItem(2).mCount == 7, "hotbar not filled by 'transfer all'");
    ctx.check(bp->inventory().isEmpty(), "backpack emptied");
    // Matching to the inventory: hotbar first (slot order), only kinds the player has.
    clearInventory(p);
    bp->inventory().set(0, data("minecraft:apple", 70));
    bp->inventory().set(1, data("minecraft:stick", 3));
    ctx.give(p, game::ItemConv::make("minecraft:apple", 1), 30);
    ctx.check(act(p, s, "transfer", {{"to", "inventory"}, {"matching", true}}), "transfer matching to inventory");
    ctx.check(p.getInventory().getItem(30).mCount == 64 && p.getInventory().getItem(0).mCount == 7, "merged, then the first empty slot (hotbar 0)");
    ctx.check(bp->inventory().at(1).count == 3, "sticks stay in the backpack");
    clearInventory(p);
});

ABP_REGISTER_SELFTEST("mainscreen.pickup_all", [](Context& ctx) {
    auto& p  = ctx.player();
    auto  bp = ctx.createBackpack(Tier::Iron);
    clearInventory(p);
    bp->inventory().set(0, data("minecraft:apple", 64)); // full: second pass
    bp->inventory().set(1, data("minecraft:apple", 10));
    ctx.give(p, game::ItemConv::make("minecraft:apple", 5), 15);
    Companion c(p);
    auto      s = open(ctx, p, bp);
    setCursor(p, game::ItemConv::make("minecraft:apple", 20));
    ctx.check(!act(p, s, "double", {{"area", "storage"}, {"index", 1}}), "a filled clicked slot gathers nothing");
    ctx.check(act(p, s, "double", {{"area", "storage"}, {"index", 5}}), "double click on an empty slot gathers");
    ctx.check(cursorOf(p).mCount == 64, "cursor full");
    ctx.check(bp->inventory().at(1).empty() && p.getInventory().getItem(15).isNull(), "partial stacks taken first");
    ctx.check(bp->inventory().at(0).count == 35, "then the full stack (64 - 29)");
});

ABP_REGISTER_SELFTEST("mainscreen.quick_move_all", [](Context& ctx) {
    auto& p  = ctx.player();
    auto  bp = ctx.createBackpack(Tier::Iron);
    clearInventory(p);
    bp->inventory().set(0, data("minecraft:apple", 10));
    bp->inventory().set(4, data("minecraft:apple", 12));
    bp->inventory().set(9, data("minecraft:stick", 2));
    Companion c(p);
    auto      s = open(ctx, p, bp);
    ctx.check(!act(p, s, "quickMoveAll", {{"area", "storage"}, {"index", 0}}), "nothing shift-clicked yet");
    ctx.check(act(p, s, "click", {{"area", "storage"}, {"index", 0}, {"shift", true}}), "shift click");
    ctx.check(bp->inventory().at(0).empty(), "slot 0 moved");
    bp->inventory().set(7, data("minecraft:apple", 3));
    ctx.check(act(p, s, "quickMoveAll", {{"area", "storage"}, {"index", 0}}), "shift + double click moves the other apples");
    ctx.check(bp->inventory().at(4).empty() && bp->inventory().at(7).empty() && bp->inventory().at(9).count == 2, "apples moved, sticks stay");
    clearInventory(p);
});

ABP_REGISTER_SELFTEST("mainscreen.transfer_full_slot", [](Context& ctx) {
    auto& p  = ctx.player();
    auto  bp = ctx.createBackpack(Tier::Iron);
    clearInventory(p);
    ctx.installUpgrade(bp, "stack_tier_1");
    int limit = bp->inventory().slotLimit(0, data("minecraft:cobblestone", 1));
    ctx.require(limit > 64, "stack upgrade raises the limit");
    bp->inventory().set(0, data("minecraft:cobblestone", std::min(limit, 150)));
    Companion c(p);
    auto      s = open(ctx, p, bp);
    ctx.check(act(p, s, "click", {{"area", "storage"}, {"index", 0}, {"shift", true}, {"ctrl", true}, {"button", 1}}), "ctrl+shift right click");
    ctx.check(bp->inventory().at(0).count == std::min(limit, 150) - 64, "right button: one stack only");
    ctx.check(act(p, s, "click", {{"area", "storage"}, {"index", 0}, {"shift", true}, {"ctrl", true}, {"button", 0}}), "ctrl+shift left click");
    ctx.check(bp->inventory().at(0).empty(), "left button: the whole slot");
    clearInventory(p);
});

ABP_REGISTER_SELFTEST("mainscreen.kept_search_and_tab", [](Context& ctx) {
    auto& p  = ctx.player();
    auto  bp = ctx.createBackpack(Tier::Iron);
    int   slot = ctx.installUpgrade(bp, "pickup");
    Companion c(p);
    auto      s = open(ctx, p, bp);
    ctx.check(act(p, s, "searchPhrase", {{"phrase", "dia @mine"}}), "search phrase");
    ctx.check(act(p, s, "openTab", {{"slot", slot}}), "open tab");
    ui::MenuManager::get().close(p);
    s = open(ctx, p, bp);
    auto const* m = net::sentModel(p);
    ctx.require(m != nullptr, "model");
    auto settings = m->extra.value("settings", json::object());
    ctx.check(settings.value("effectiveKeepSearchPhrase", false), "SC default: keep search phrase on");
    ctx.check(settings.value("searchPhrase", std::string{}) == "dia @mine", "phrase kept: " + settings.value("searchPhrase", std::string{}));
    ctx.check(settings.value("openTab", -1) == slot, "open tab kept");
    ctx.check(act(p, s, "openTab", {{"slot", -1}}), "close tab");
    ctx.check(act(p, s, "searchPhrase", {{"phrase", ""}}), "clear phrase");
    setPlayerOption(p, "searchPhrase", std::nullopt);
});

ABP_REGISTER_SELFTEST("mainscreen.error_overlay", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p  = ctx.player();
    auto  bp = ctx.createBackpack(Tier::Iron);
    ctx.installUpgrade(bp, "magnet", 1);
    Companion c(p);
    auto      s     = open(ctx, p, bp);
    auto*     magnet = upgrade::byId("magnet");
    ctx.require(magnet != nullptr, "magnet");
    setCursor(p, game::ItemConv::make(magnet->info().itemId, 1));
    ctx.check(!act(p, s, "click", {{"area", "upgrade"}, {"index", 0}}), "second magnet refused");
    co_await ctx.ticks(1);
    auto const* m = net::sentModel(p);
    ctx.require(m != nullptr, "model");
    ctx.check(m->errorUpgradeSlots == std::vector<int>{1}, "the existing magnet's slot is highlighted");
    ctx.check(m->errorMessage == "Only 1x Magnet Upgrade can be used in " + m->title, "SC message: " + m->errorMessage);
    setCursor(p, ItemStack::EMPTY_ITEM());
    bp->inventory().set(0, data("minecraft:apple", 2));
    (void)act(p, s, "click", {{"area", "storage"}, {"index", 0}});
    co_await ctx.ticks(1);
    ctx.check(!net::sentModel(p)->errorMessage.empty(), "a storage click does not clear it (SC)");
    co_await ctx.ticks(61);
    ctx.check(net::sentModel(p)->errorMessage.empty(), "expired after 60 ticks");
});

ABP_REGISTER_SELFTEST("mainscreen.model_fields", [](Context& ctx) {
    auto& p  = ctx.player();
    auto  bp = ctx.createBackpack(Tier::Iron);
    Companion c(p);
    (void)open(ctx, p, bp);
    ctx.check(net::sentModel(p)->baseSlotLimit == 64, "plain backpack: 64");
    ui::MenuManager::get().close(p);
    ctx.installUpgrade(bp, "stack_tier_1");
    (void)open(ctx, p, bp);
    auto const* m = net::sentModel(p);
    ctx.check(m->baseSlotLimit > 64, "stack upgrade: " + std::to_string(m->baseSlotLimit));
    ctx.check(m->upgrades[0].columnsTaken == 0, "no upgrade takes columns yet");
});

ABP_REGISTER_SELFTEST("mainscreen.locked_slot", [](Context& ctx) {
    auto& p  = ctx.player();
    auto  bp = ctx.createBackpack(Tier::Leather);
    bp->inventory().set(0, data("minecraft:apple", 2));
    clearInventory(p);
    ctx.require(ctx.give(p, ctx.backpackItem(*bp), 3) == 3, "backpack in hotbar 3");
    Companion c(p);
    ctx.require(ui::BackpackMenu::openFor(p, game::ItemLocation{game::ItemLocation::Kind::Inventory, 3}), "open from the item");
    ui::MenuManager::get().forceShown(p);
    auto s = net::nativeSession(p);
    ctx.check(!act(p, s, "hotbar", {{"area", "storage"}, {"index", 0}, {"hotbar", 3}}), "number key onto the open backpack refused");
    ctx.check(!act(p, s, "hotbar", {{"area", "inventory"}, {"index", 12}, {"hotbar", 3}}), "inventory swap with the open backpack refused");
    ctx.check(game::isBackpack(p.getInventory().getItem(3)) && bp->inventory().at(0).count == 2, "nothing moved");
});

// ---- e2e setup ------------------------------------------------------------------------------------

/// A backpack of `tier` filled for the client scenario: varied items and counts (incl. an
/// abbreviated count), gaps, a memory slot, two no-sort slots, a magnet upgrade (switch, error
/// test) and, from iron up, a stack upgrade. The player also gets a second magnet upgrade.
std::shared_ptr<game::Backpack> setupBackpack(Player& player, Tier tier) {
    auto bp = game::Storage::get().create(tier);
    if (!bp) return nullptr;
    auto install = [&](char const* id) {
        auto* type = upgrade::byId(id);
        if (!type) return;
        for (int s = 0; s < static_cast<int>(bp->data().upgrades.size()); ++s) {
            if (!bp->data().upgrades[static_cast<std::size_t>(s)].empty()) continue;
            (void)upgrade::install(bp, s, game::ItemConv::toData(game::ItemConv::make(type->info().itemId, 1)), &player);
            return;
        }
    };
    install("magnet");
    if (tier >= Tier::Iron) install("stack_tier_2");
    if (tier >= Tier::Gold) install("pickup");
    static constexpr std::string_view kItems[] = {
        "minecraft:cobblestone", "minecraft:dirt",    "minecraft:oak_log",  "minecraft:diamond", "minecraft:iron_ingot",
        "minecraft:apple",       "minecraft:redstone", "minecraft:oak_planks", "minecraft:torch", "minecraft:gold_ingot",
        "minecraft:ender_pearl", "minecraft:sand",
    };
    auto& inv = bp->inventory();
    for (int i = 0; i < inv.size(); ++i) {
        if (i % 7 == 6 || i == 1) continue; // gaps; slot 1 is the memory slot
        auto id = kItems[static_cast<std::size_t>(i) % std::size(kItems)];
        auto d  = game::ItemConv::toData(game::ItemConv::make(id, 1));
        int  n  = 1 + (i * 13) % std::max(1, d.maxStack);
        if (i == 0) n = std::min(1024, inv.slotLimit(0, d)); // abbreviated count ("1,024" / "64")
        if (i == 2) n = std::min(300, inv.slotLimit(2, d));
        inv.set(i, d.withCount(n));
    }
    setMemory(*bp, 1, game::ItemConv::toData(game::ItemConv::make("minecraft:diamond", 1)));
    setNoSort(*bp, 3, true);
    setNoSort(*bp, 4, true);
    bp->markDirty();
    game::Storage::get().save(*bp, true);
    return bp;
}
} // namespace
} // namespace abp::feature::gui

ABP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    cmd.overload<MainScreenSetupParams>().text("mainscreen").text("setup").required("tier").execute(
        [](CommandOrigin const& origin, CommandOutput& output, MainScreenSetupParams const& params) {
            using namespace abp;
            auto* e = origin.getEntity();
            if (!e || !e->isPlayer() || !game::Storage::get().available()) {
                output.error("players only");
                return;
            }
            auto& player = static_cast<Player&>(*e);
            auto  tier   = static_cast<core::Tier>(params.tier);
            auto  bp     = feature::gui::setupBackpack(player, tier);
            if (!bp) {
                output.error("could not create a backpack");
                return;
            }
            auto stack = game::makeBackpackItem(tier, bp->uuid());
            if (!player.add(stack)) player.drop(stack, false);
            if (auto* magnet = upgrade::byId("magnet")) {
                auto extra = game::ItemConv::make(magnet->info().itemId, 1);
                if (!player.add(extra)) player.drop(extra, false);
            }
            player.sendInventory(false);
            auto tierId = std::string(core::tierInfo(tier).id);
            log().info("mainscreen setup: gave {} backpack {}", tierId, bp->uuid());
            output.success(trFor(player, "abp.mainscreen.setup.done", "mainscreen setup: gave {0} backpack {1}", tierId, bp->uuid()));
        }
    );
});
