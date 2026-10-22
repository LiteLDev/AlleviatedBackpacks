// Selftests of the companion protocol's server side (`/backpack selftest "companion.*"`), plus
// `/backpack companion setup` (gives a filled netherite backpack for the client e2e scenario).
//
// Simulated players are marked as companions with `net::setCompanion`; the packets the server
// would send them are captured through `net::setTestSink`.

#include <algorithm>

#include "ll/api/command/CommandHandle.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/PlayerUISlot.h"
#include "mc/world/item/ItemStack.h"

#include "feature/gui/Gui.h"
#include "feature/gui/SbSettings.h"
#include "game/Backpack.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/Storage.h"
#include "core/Appearance.h"
#include "mod/Commands.h"
#include "mod/Log.h"
#include "net/Companion.h"
#include "net/NativeScreens.h"
#include "net/Packets.h"
#include "selftest/SelfTest.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/IUpgrade.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/deposit/TestSupport.h"

namespace abp::net {
namespace {

using core::Tier;
using selftest::Context;
using nlohmann::json;

core::ItemStackData data(std::string_view id, int count) {
    return game::ItemConv::toData(game::ItemConv::make(id, std::min(count, 64))).withCount(count);
}

/// Marks the test player as a companion and captures what the server sends it.
struct CompanionScope {
    Player&                                         player;
    std::vector<std::pair<std::string, json>>       packets;

    explicit CompanionScope(Player& p) : player(p) {
        setCompanion(p, CompanionInfo{"test", {"native_screen"}});
        setTestSink([this](Player& to, std::string_view name, json const& body) {
            if (&to == &player) packets.emplace_back(std::string(name), body);
        });
    }
    ~CompanionScope() {
        setTestSink(nullptr);
        setCompanion(player, std::nullopt);
        ui::MenuManager::get().close(player);
    }

    [[nodiscard]] std::vector<json> named(std::string_view suffix) const {
        std::vector<json> out;
        for (auto const& [name, body] : packets) {
            if (name.ends_with(suffix)) out.push_back(body);
        }
        return out;
    }
};

json action(std::uint32_t session, std::string const& name, json extra = json::object()) {
    extra["session"] = session;
    extra["action"]  = name;
    return extra;
}

ItemStack cursor(Player& p) { return p.mPlayerUIContainer->getItem(0); }

/// Opens `bp` for the player as a shown menu; returns the native session (0 on failure).
std::uint32_t openNative(Context& ctx, Player& player, std::shared_ptr<game::Backpack> const& bp) {
    ctx.require(ui::BackpackMenu::openBackpack(player, bp), "open backpack menu");
    ui::MenuManager::get().forceShown(player);
    return nativeSession(player);
}

ABP_REGISTER_SELFTEST("companion.ui_mode", [](Context& ctx) {
    auto& p = ctx.player();
    ctx.check(!hasCompanion(p) && uiModeFor(p) != UiMode::Native, "simulated player has no companion");
    {
        CompanionScope scope(p);
        ctx.check(hasCompanion(p) && uiModeFor(p) == UiMode::Native, "companion -> native screens");
        ctx.check(companionInfo(p)->version == "test", "companion info kept");
    }
    ctx.check(!hasCompanion(p), "forgotten again");
});

ABP_REGISTER_SELFTEST("companion.screen_open", [](Context& ctx) {
    auto& p  = ctx.player();
    auto  bp = ctx.createBackpack(Tier::Netherite);
    bp->inventory().set(0, data("minecraft:stone", 300));
    bp->inventory().set(100, data("minecraft:diamond", 3));
    ctx.installUpgrade(bp, "pickup");
    CompanionScope scope(p);
    auto           session = openNative(ctx, p, bp);
    ctx.require(session != 0, "native session started");
    auto opens = scope.named("ScreenOpenPacket");
    ctx.require(opens.size() == 1, "one ScreenOpen sent, got " + std::to_string(opens.size()));
    auto m = core::screen::BackpackScreenModel::fromJson(opens[0]);
    ctx.check(m.session == session, "session id");
    ctx.check(m.containerId == ui::MenuManager::get().containerIdOf(p), "container id of the hidden chest");
    ctx.check(m.storageSlots == 120 && m.tier == static_cast<int>(Tier::Netherite), "netherite: 120 slots");
    ctx.check(m.slots[0].count == 300 && m.slots[100].id == "minecraft:diamond", "slot contents (also beyond the chest page)");
    ctx.check(m.upgrades.size() == 7 && m.upgrades[0].id == "pickup" && m.upgrades[0].toggleable, "upgrades + switch");
    ctx.check(m.slotLimits[0] >= 64, "slot limit");
    ui::MenuManager::get().close(p);
    ctx.check(scope.named("ScreenClosePacket").size() == 1, "ScreenClose on close");
    ctx.check(nativeSession(p) == 0, "session ended");
});

ABP_REGISTER_SELFTEST("companion.no_companion_no_packets", [](Context& ctx) {
    auto&                                      p  = ctx.player();
    auto                                       bp = ctx.createBackpack(Tier::Leather);
    std::vector<std::string>                   got;
    setTestSink([&](Player&, std::string_view n, json const&) { got.emplace_back(n); });
    ctx.require(ui::BackpackMenu::openBackpack(p, bp), "open");
    ui::MenuManager::get().forceShown(p);
    ctx.check(nativeSession(p) == 0 && got.empty(), "a vanilla client gets the chest only");
    ui::MenuManager::get().close(p);
    setTestSink(nullptr);
});

ABP_REGISTER_SELFTEST("companion.click_take_place", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p  = ctx.player();
    auto  bp = ctx.createBackpack(Tier::Netherite);
    bp->inventory().set(50, data("minecraft:stone", 100)); // second chest page
    CompanionScope scope(p);
    auto           s = openNative(ctx, p, bp);
    ctx.require(s != 0, "session");
    ctx.check(handleScreenAction(p, action(s, "click", {{"area", "storage"}, {"index", 50}, {"button", 0}})), "left click takes");
    ctx.check(cursor(p).mCount == 64 && bp->inventory().at(50).count == 36, "took one stack (64), 36 left");
    ctx.check(handleScreenAction(p, action(s, "click", {{"area", "inventory"}, {"index", 12}, {"button", 1}})), "right click places one");
    ctx.check(p.getInventory().getItem(12).mCount == 1 && cursor(p).mCount == 63, "one placed");
    ctx.check(handleScreenAction(p, action(s, "click", {{"area", "storage"}, {"index", 7}, {"button", 0}})), "left click places all");
    ctx.check(cursor(p).isNull() && bp->inventory().at(7).count == 63, "cursor emptied into slot 7");
    ctx.check(handleScreenAction(p, action(s, "click", {{"area", "storage"}, {"index", 50}, {"button", 1}})), "right click takes half");
    ctx.check(cursor(p).mCount == 18 && bp->inventory().at(50).count == 18, "half of 36");
    ctx.check(handleScreenAction(p, action(s, "double", {{"area", "storage"}, {"index", 50}})), "double click collects");
    ctx.check(cursor(p).mCount == 64, "cursor filled to a stack");
    co_await ctx.ticks(2);
    auto updates = scope.named("ScreenUpdatePacket");
    ctx.check(!updates.empty(), "storage changes are patched to the client");
    bool sawSlot = false;
    for (auto const& u : updates) sawSlot |= u["patch"].contains("slotsDiff");
    ctx.check(sawSlot, "patches carry slot diffs");
});

ABP_REGISTER_SELFTEST("companion.shift_click", [](Context& ctx) {
    auto& p  = ctx.player();
    auto  bp = ctx.createBackpack(Tier::Iron);
    bp->inventory().set(3, data("minecraft:apple", 20));
    ctx.give(p, game::ItemConv::make("minecraft:oak_log", 16), 20);
    CompanionScope scope(p);
    auto           s = openNative(ctx, p, bp);
    ctx.check(handleScreenAction(p, action(s, "click", {{"area", "storage"}, {"index", 3}, {"shift", true}})), "shift storage -> inventory");
    ctx.check(bp->inventory().at(3).empty(), "apples moved out");
    int apples = 0;
    for (int i = 0; i < 36; ++i) {
        auto const& it = p.getInventory().getItem(i);
        if (!it.isNull() && it.getTypeName() == "minecraft:apple") apples += it.mCount;
    }
    ctx.check(apples == 20, "20 apples in the inventory");
    ctx.check(handleScreenAction(p, action(s, "click", {{"area", "inventory"}, {"index", 20}, {"shift", true}})), "shift inventory -> storage");
    ctx.check(p.getInventory().getItem(20).isNull(), "logs left the inventory");
    ctx.check(bp->inventory().count([](auto const& d) { return d.id == "minecraft:oak_log"; }) == 16, "logs in the backpack");
});

ABP_REGISTER_SELFTEST("companion.authority", [](Context& ctx) {
    auto& p  = ctx.player();
    auto  bp = ctx.createBackpack(Tier::Leather);
    bp->inventory().set(0, data("minecraft:stone", 10));
    int locked = ctx.give(p, ctx.backpackItem(*bp), 4);
    ctx.require(locked == 4, "backpack in slot 4");
    CompanionScope scope(p);
    ctx.require(ui::BackpackMenu::openFor(p, game::ItemLocation{game::ItemLocation::Kind::Inventory, 4}), "open from the item");
    ui::MenuManager::get().forceShown(p);
    auto s = nativeSession(p);
    ctx.require(s != 0, "session");
    ctx.check(!handleScreenAction(p, action(s + 1000, "click", {{"area", "storage"}, {"index", 0}})), "wrong session refused");
    ctx.check(!handleScreenAction(p, action(s, "click", {{"area", "storage"}, {"index", 27}})), "index out of range refused");
    ctx.check(!handleScreenAction(p, action(s, "click", {{"area", "inventory"}, {"index", 4}})), "the open backpack's own slot is locked");
    ctx.check(game::isBackpack(p.getInventory().getItem(4)), "backpack item untouched");
    ctx.check(!handleScreenAction(p, action(s, "bogus")), "unknown action refused");
    ctx.check(bp->inventory().at(0).count == 10, "nothing moved");
});

ABP_REGISTER_SELFTEST("companion.upgrade_slots", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p  = ctx.player();
    auto  bp = ctx.createBackpack(Tier::Iron); // 2 upgrade slots
    CompanionScope scope(p);
    auto           s = openNative(ctx, p, bp);
    auto*          pickup = upgrade::byId("jukebox"); // max 1 per backpack (SB has no magnet limit)
    ctx.require(pickup != nullptr, "jukebox upgrade registered");
    p.setPlayerUIItem(PlayerUISlot::CursorSelected, game::ItemConv::make(pickup->info().itemId, 1), false);
    ctx.check(handleScreenAction(p, action(s, "click", {{"area", "upgrade"}, {"index", 0}})), "install from the cursor");
    ctx.check(!bp->data().upgrades[0].empty() && cursor(p).isNull(), "installed");
    // A second jukebox upgrade is refused with the red error overlay.
    p.setPlayerUIItem(PlayerUISlot::CursorSelected, game::ItemConv::make(pickup->info().itemId, 1), false);
    ctx.check(!handleScreenAction(p, action(s, "click", {{"area", "upgrade"}, {"index", 1}})), "duplicate refused");
    co_await ctx.ticks(1);
    auto const* m = sentModel(p);
    ctx.require(m != nullptr, "model");
    ctx.check(!m->errorMessage.empty() && m->errorUpgradeSlots == std::vector<int>{1}, "error overlay: " + m->errorMessage);
    ctx.check(handleScreenAction(p, action(s, "toggleUpgrade", {{"slot", 0}})), "switch off");
    ctx.check(!bp->data().upgrades[0].enabled, "disabled");
    p.setPlayerUIItem(PlayerUISlot::CursorSelected, ItemStack::EMPTY_ITEM(), false);
    ctx.check(handleScreenAction(p, action(s, "click", {{"area", "upgrade"}, {"index", 0}})), "take the upgrade out");
    ctx.check(bp->data().upgrades[0].empty() && !cursor(p).isNull(), "removed onto the cursor");
    p.setPlayerUIItem(PlayerUISlot::CursorSelected, ItemStack::EMPTY_ITEM(), false);
});

ABP_REGISTER_SELFTEST("companion.buttons", [](Context& ctx) {
    auto& p  = ctx.player();
    auto  bp = ctx.createBackpack(Tier::Leather);
    bp->inventory().set(5, data("minecraft:stone", 3));
    bp->inventory().set(1, data("minecraft:apple", 2));
    CompanionScope scope(p);
    auto           s = openNative(ctx, p, bp);
    ctx.check(handleScreenAction(p, action(s, "sort")), "sort");
    ctx.check(bp->inventory().at(0).id == "minecraft:apple" && bp->inventory().at(1).id == "minecraft:stone", "sorted by name");
    auto before = feature::gui::effective(&p, *bp).sortBy;
    ctx.check(handleScreenAction(p, action(s, "sortBy")), "cycle sort mode");
    ctx.check(feature::gui::effective(&p, *bp).sortBy == core::nextSortBy(before), "next mode");
    ctx.give(p, game::ItemConv::make("minecraft:apple", 5), 15);
    ctx.check(handleScreenAction(p, action(s, "transfer", {{"to", "storage"}, {"matching", true}})), "transfer matching");
    ctx.check(p.getInventory().getItem(15).isNull(), "apples moved into the backpack");
    ctx.check(handleScreenAction(p, action(s, "transfer", {{"to", "inventory"}, {"matching", false}})), "transfer all out");
    ctx.check(bp->inventory().isEmpty(), "backpack emptied");
    feature::gui::setBackpackOption(*bp, feature::gui::kSortBy, std::nullopt);
});

ABP_REGISTER_SELFTEST("companion.contents", [](Context& ctx) {
    auto& p  = ctx.player();
    auto  bp = ctx.createBackpack(Tier::Gold);
    bp->inventory().set(0, data("minecraft:dirt", 10));
    bp->inventory().set(1, data("minecraft:cobblestone", 64));
    bp->inventory().set(2, data("minecraft:dirt", 60));
    ctx.installUpgrade(bp, "magnet");
    auto m = buildContents(p, bp);
    ctx.check(m.known && m.uuid == bp->uuid(), "known backpack");
    ctx.require(m.items.size() == 2, "compacted to 2 kinds");
    ctx.check(m.items[0].id == "minecraft:dirt" && m.items[0].count == 70, "dirt first (70)");
    ctx.check(m.upgrades.size() == 1 && m.upgrades[0].toggleable, "magnet with a switch pip");
    auto r = buildRenderInfo(*bp);
    ctx.check(r.uuid == bp->uuid() && r.upgrades == std::vector<std::string>{"magnet"}, "render info");
});

ABP_REGISTER_SELFTEST("companion.key_actions", [](Context& ctx) {
    auto& p  = ctx.player();
    auto  bp = ctx.createBackpack(Tier::Copper);
    ctx.installUpgrade(bp, "magnet");
    ctx.require(ctx.give(p, ctx.backpackItem(*bp), 9) == 9, "carry the backpack");
    ctx.check(handleKeyAction(p, json{{"action", "toggleUpgrade"}, {"slot", 0}}), "toggle upgrade 1");
    ctx.check(!bp->data().upgrades[0].enabled, "magnet off");
    ctx.check(handleKeyAction(p, json{{"action", "toggleUpgrade"}, {"slot", 0}}), "toggle again");
    ctx.check(bp->data().upgrades[0].enabled, "magnet on");
    ctx.check(!handleKeyAction(p, json{{"action", "toggleUpgrade"}, {"slot", 3}}), "no upgrade in slot 4");
    ctx.check(handleKeyAction(p, json{{"action", "openAt"}, {"where", "inventory"}, {"slot", 9}}), "open the hovered backpack");
    auto menu = std::dynamic_pointer_cast<ui::BackpackMenu>(ui::MenuManager::get().current(p));
    ctx.check(menu && menu->backpack()->uuid() == bp->uuid(), "that backpack is open");
    ui::MenuManager::get().close(p);
    ctx.check(!handleKeyAction(p, json{{"action", "openAt"}, {"where", "inventory"}, {"slot", 10}}), "no backpack there");
    ctx.check(handleKeyAction(p, json{{"action", "open"}}), "Open Backpack opens the carried one");
    ui::MenuManager::get().close(p);
});

ABP_REGISTER_SELFTEST("companion.virtual_presentation", [](Context& ctx) {
    auto& p  = ctx.player();
    auto  bp = ctx.createBackpack(Tier::Leather);
    CompanionScope scope(p);
    auto           s = openNative(ctx, p, bp);
    ctx.check(s != 0, "native session");
    ctx.check(ui::MenuManager::get().isVirtual(p), "no fake chest for a companion");
    ctx.check(ui::MenuManager::get().containerIdOf(p) >= 1000, "virtual screen id");
    ctx.check(handleScreenAction(p, action(s, "close")), "close action");
    ctx.check(ui::MenuManager::get().current(p) == nullptr && nativeSession(p) == 0, "closed");
});

namespace {
class TestMenu : public ui::Menu {
public:
    int  clicks = 0;
    core::ItemStackData ghost;
    TestMenu() : Menu("Test menu", Rows::Three) {}
    void build(Player&) override {
        setSlot(0, std::make_shared<ui::ButtonSlot>(ui::icon("minecraft:emerald", "Button"), [this](ui::ClickContext const&) { ++clicks; }));
        setSlot(1, std::make_shared<ui::GhostSlot>([this] { return ghost; }, [this](core::ItemStackData const& d) { ghost = d; }));
        setSlot(2, std::make_shared<ui::StaticSlot>(ui::icon("minecraft:gray_stained_glass_pane", " ")));
    }
};
} // namespace

ABP_REGISTER_SELFTEST("companion.generic_menu", [](Context& ctx) {
    auto& p = ctx.player();
    CompanionScope scope(p);
    auto menu = std::make_shared<TestMenu>();
    ctx.require(ui::MenuManager::get().open(p, menu), "open");
    ui::MenuManager::get().forceShown(p);
    auto opens = scope.named("ScreenOpenPacket");
    ctx.require(opens.size() == 1, "ScreenOpen sent");
    auto m = core::screen::BackpackScreenModel::fromJson(opens[0]);
    ctx.check(m.kind == "menu" && m.menuRows == 3 && m.menuSlots.size() == 27, "generic menu model");
    ctx.check(m.menuSlots[0].id == "minecraft:emerald", "button icon");
    ctx.check(m.menuSlots[2].id == core::screen::kFillerId, "decoration pane sent as filler");
    auto s = nativeSession(p);
    ctx.check(handleScreenAction(p, action(s, "menuClick", {{"index", 0}})) || menu->clicks == 1, "button click");
    ctx.check(menu->clicks == 1, "button callback ran once");
    p.setPlayerUIItem(PlayerUISlot::CursorSelected, game::ItemConv::make("minecraft:apple", 3), false);
    (void)handleScreenAction(p, action(s, "menuClick", {{"index", 1}}));
    ctx.check(menu->ghost.id == "minecraft:apple" && cursor(p).mCount == 3, "ghost slot copies the cursor item");
    p.setPlayerUIItem(PlayerUISlot::CursorSelected, ItemStack::EMPTY_ITEM(), false);
});

ABP_REGISTER_SELFTEST("companion.settings_screen", [](Context& ctx) {
    auto& p  = ctx.player();
    auto  bp = ctx.createBackpack(Tier::Leather);
    bp->inventory().set(2, data("minecraft:dirt", 5));
    CompanionScope scope(p);
    auto           s = openNative(ctx, p, bp);
    ctx.check(!handleScreenAction(p, action(s, "setting", {{"what", "memory"}, {"op", "select"}, {"index", 3}})), "empty slot can't be memorized");
    ctx.check(handleScreenAction(p, action(s, "setting", {{"what", "memory"}, {"op", "select"}, {"index", 2}})), "memorize slot 2");
    ctx.check(feature::gui::memory(*bp)[2].id == "minecraft:dirt", "remembered dirt");
    ctx.check(handleScreenAction(p, action(s, "setting", {{"what", "noSort"}, {"op", "select"}, {"index", 4}})), "no-sort slot 4");
    ctx.check(feature::gui::noSort(*bp)[4], "slot 4 not sorted");
    ctx.check(handleScreenAction(p, action(s, "setting", {{"what", "noSortColor"}})), "next colour");
    ctx.check(handleScreenAction(p, action(s, "setting", {{"what", "option"}, {"name", "keepSearchPhrase"}})), "toggle keep search phrase");
    auto const* m = sentModel(p);
    ctx.require(m != nullptr, "model");
    auto settings = m->extra.value("settings", nlohmann::json::object());
    // SB: the context starts at Player and keepSearchPhrase defaults to on, so the toggle turns it off.
    ctx.check(!settings.value("keepSearchPhrase", true), "keepSearchPhrase toggled off (player context)");
    feature::gui::setPlayerSetting(p, feature::gui::MainSetting::KeepSearchPhrase, true);
    ctx.check(settings.value("noSortColor", -1) == static_cast<int>(core::DyeColor::Pink), "lime -> pink (dye order)");
    ctx.check(handleScreenAction(p, action(s, "setting", {{"what", "memory"}, {"op", "unselectAll"}})), "forget all");
    ctx.check(feature::gui::memoryCount(*bp) == 0, "memory cleared");
    feature::gui::setNoSortAll(*bp, false);

});

ABP_REGISTER_SELFTEST("companion.distribute", [](Context& ctx) {
    auto& p  = ctx.player();
    auto  bp = ctx.createBackpack(Tier::Leather);
    CompanionScope scope(p);
    auto           s = openNative(ctx, p, bp);
    p.setPlayerUIItem(PlayerUISlot::CursorSelected, game::ItemConv::make("minecraft:stone", 30), false);
    nlohmann::json slots = nlohmann::json::array({{{"area", "storage"}, {"index", 0}}, {{"area", "storage"}, {"index", 1}}, {{"area", "inventory"}, {"index", 20}}});
    ctx.check(handleScreenAction(p, action(s, "distribute", {{"button", 0}, {"slots", slots}})), "split evenly");
    ctx.check(bp->inventory().at(0).count == 10 && bp->inventory().at(1).count == 10, "10 + 10 in the backpack");
    ctx.check(p.getInventory().getItem(20).mCount == 10 && cursor(p).isNull(), "10 in the inventory, cursor empty");
});

ABP_REGISTER_SELFTEST("companion.tab_menu_bridge", [](Context& ctx) {
    auto& p  = ctx.player();
    auto  bp = ctx.createBackpack(Tier::Iron);
    int   slot = ctx.installUpgrade(bp, "pickup");
    CompanionScope scope(p);
    auto           s = openNative(ctx, p, bp);
    auto const*    m = sentModel(p);
    ctx.require(m != nullptr, "model");
    auto const& tab = m->upgrades[static_cast<std::size_t>(slot)].tab;
    ctx.require(tab.contains("menu"), "the pickup filter menu is bridged into its tab");
    ctx.check(tab["menu"].value("rows", 0) >= 1 && tab["menu"]["slots"].size() >= 9, "menu slots");
    // The first ghost slot of the filter takes the cursor item.
    int ghostIndex = -1;
    auto const& slotsJson = tab["menu"]["slots"];
    for (int i = 0; i < static_cast<int>(slotsJson.size()); ++i) {
        if (slotsJson[static_cast<std::size_t>(i)].is_null()) { ghostIndex = i; break; }
    }
    ctx.require(ghostIndex >= 0, "an empty filter slot");
    p.setPlayerUIItem(PlayerUISlot::CursorSelected, game::ItemConv::make("minecraft:cobblestone", 1), false);
    (void)handleScreenAction(p, action(s, "tab", {{"slot", slot}, {"data", {{"menuClick", ghostIndex}, {"button", 0}}}}));
    auto filter = bp->data().upgrades[static_cast<std::size_t>(slot)].settings.dump();
    ctx.check(filter.find("cobblestone") != std::string::npos, "filter got the item: " + filter.substr(0, 200));
    p.setPlayerUIItem(PlayerUISlot::CursorSelected, ItemStack::EMPTY_ITEM(), false);
});
} // namespace
} // namespace abp::net

// `/backpack companion setup`: a netherite backpack full of test items with a few upgrades (e2e).
ABP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    cmd.overload().text("companion").text("setup").execute([](CommandOrigin const& origin, CommandOutput& output) {
        using namespace abp;
        auto* e = origin.getEntity();
        if (!e || !e->isPlayer() || !game::Storage::get().available()) {
            output.error("players only");
            return;
        }
        auto& player = static_cast<Player&>(*e);
        auto  bp     = game::Storage::get().create(core::Tier::Netherite);
        if (!bp) {
            output.error("could not create a backpack");
            return;
        }
        static constexpr std::string_view kItems[] = {
            "minecraft:cobblestone", "minecraft:dirt", "minecraft:oak_log", "minecraft:diamond", "minecraft:iron_ingot",
            "minecraft:apple", "minecraft:redstone", "minecraft:oak_planks", "minecraft:torch", "minecraft:gold_ingot",
            "minecraft:stone", "minecraft:sand",
        };
        // Upgrades first: the stack upgrade raises the slot limits the fill below relies on.
        for (auto const* id : {"magnet", "deposit", "stack_tier_4", "tank"}) {
            auto* type = upgrade::byId(id);
            if (!type) continue;
            for (int s = 0; s < static_cast<int>(bp->data().upgrades.size()); ++s) {
                if (!bp->data().upgrades[static_cast<std::size_t>(s)].empty()) continue;
                auto item = game::ItemConv::toData(game::ItemConv::make(type->info().itemId, 1));
                (void)upgrade::install(bp, s, item, &player);
                break;
            }
        }
        auto& inv = bp->inventory();
        for (int i = 0; i < inv.size(); ++i) {
            auto id = kItems[static_cast<std::size_t>(i) % std::size(kItems)];
            if (i % 7 == 6) continue; // some gaps
            auto d = game::ItemConv::toData(game::ItemConv::make(id, 1));
            // Slot 0 shows the abbreviated count (stack upgrade installed above: limit > 64).
            inv.set(i, d.withCount(i == 0 ? std::min(1500, inv.slotLimit(0, d)) : 1 + (i * 13) % 64));
        }
        // The deposit upgrade puts cobblestone into containers (C on a chest).
        for (int s = 0; s < static_cast<int>(bp->data().upgrades.size()); ++s) {
            auto const& st = bp->data().upgrades[static_cast<std::size_t>(s)];
            if (auto* type = st.empty() ? nullptr : upgrade::upgradeFor(st.item); type && type->info().id == "deposit") {
                upgrade::transfer::test::setFilter(upgrade::UpgradeContext(bp, s, &player), false, {"minecraft:cobblestone"}, core::FilterMode::Allow);
            }
        }
        // Dyed (blue / yellow) so the client's render-hook tint is visible on this backpack.
        bp->data().mainColor   = 0xFF3C44AA;
        bp->data().accentColor = 0xFFFED83D;
        bp->markDirty();
        game::Storage::get().save(*bp, true);
        auto stack = game::makeBackpackItem(core::Tier::Netherite, bp->uuid());
        if (!player.add(stack)) player.drop(stack, false);
        player.sendInventory(false);
        abp::log().info("companion setup: gave netherite backpack {}", bp->uuid());
        output.success("companion setup: gave netherite backpack " + bp->uuid());
    });
});
