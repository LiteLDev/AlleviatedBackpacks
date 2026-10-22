// Selftests of right-click stashing (`/backpack selftest "stash.*"`) and of SB's key action rules
// (`input.*`).

#include "ll/api/command/CommandHandle.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/actor/player/PlayerUISlot.h"
#include "mc/world/containers/ContainerEnumName.h"

#include "feature/gui/Gui.h"
#include "feature/stash/Stash.h"
#include "game/Backpack.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "game/Storage.h"
#include "mod/Commands.h"
#include "mod/Log.h"
#include "net/Companion.h"
#include "net/NativeScreens.h"
#include "selftest/SelfTest.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/deposit/TestSupport.h"

namespace abp::feature::stash {
namespace {

using core::Tier;
using selftest::Context;
using Area = Location::Area;

core::ItemStackData data(std::string_view id, int count) {
    return game::ItemConv::toData(game::ItemConv::make(id, std::min(count, 64))).withCount(count);
}

void setCursor(Player& p, ItemStack const& item) { p.mPlayerUIContainer->setItem(0, item); }
ItemStack cursor(Player& p) { return p.mPlayerUIContainer->getItem(0); }

int countOf(game::Backpack& bp, std::string_view id) {
    int n   = 0;
    auto& inv = bp.inventory();
    for (int i = 0; i < inv.size(); ++i) {
        if (inv.at(i).id == id) n += inv.at(i).count;
    }
    return n;
}

struct CleanCursor {
    Player& p;
    ~CleanCursor() {
        p.mPlayerUIContainer->setItem(0, ItemStack{});
        ui::MenuManager::get().close(p);
    }
};

ABP_REGISTER_SELFTEST("stash.locations", [](Context& ctx) {
    auto loc = parseLocation(nlohmann::json{{"area", "inventory"}, {"slot", 5}});
    ctx.check(loc && loc->area == Area::Inventory && loc->slot == 5, "inventory:5");
    ctx.check(key(*loc) == "inventory:5", "key");
    ctx.check(parseLocation(nlohmann::json{{"area", "container"}, {"slot", 3}})->area == Area::Container, "container");
    ctx.check(parseLocation(nlohmann::json{{"area", "storage"}, {"slot", 100}})->slot == 100, "storage");
    ctx.check(!parseLocation(nlohmann::json{{"area", "nope"}, {"slot", 1}}), "unknown area");
    ctx.check(!parseLocation(nlohmann::json{{"area", "inventory"}, {"slot", -1}}), "negative slot");
    ctx.check(!parseLocation(nlohmann::json::array()), "not an object");
});

// SB overrideStackedOnOther: the backpack on the cursor takes the slot's items.
ABP_REGISTER_SELFTEST("stash.cursor_backpack_takes_slot", [](Context& ctx) {
    auto& p  = ctx.player();
    CleanCursor clean{p};
    auto  bp = ctx.createBackpack(Tier::Leather);
    setCursor(p, ctx.backpackItem(*bp));
    ctx.require(ctx.give(p, game::ItemConv::make("minecraft:cobblestone", 10), 5) == 5, "cobblestone in slot 5");
    ctx.require(ctx.give(p, game::ItemConv::make("minecraft:cobblestone", 3), 6) == 6, "more cobblestone in slot 6");
    Location five{Area::Inventory, 5}, six{Area::Inventory, 6};
    ctx.check(overlay(p, five) == Result::Space, "empty backpack: space (yellow)");
    ctx.check(rightClick(p, five) == 10, "10 stashed");
    ctx.check(p.getInventory().getItem(5).isNull(), "slot emptied");
    ctx.check(countOf(*bp, "minecraft:cobblestone") == 10, "backpack holds them");
    ctx.check(overlay(p, six) == Result::MatchAndSpace, "same item already inside: match (green)");
    auto q = query(p, nlohmann::json{{"slots", {{{"area", "inventory"}, {"slot", 6}}, {{"area", "inventory"}, {"slot", 7}}}}});
    ctx.check(q["results"].value("inventory:6", 0) == 2, "query: match for slot 6");
    ctx.check(!q["results"].contains("inventory:7"), "query: empty slot 7 omitted");
    ctx.check(game::isBackpack(cursor(p)), "backpack stays on the cursor");
});

// SB overrideOtherStackedOnMe: the backpack in the slot takes the cursor.
ABP_REGISTER_SELFTEST("stash.slot_backpack_takes_cursor", [](Context& ctx) {
    auto& p  = ctx.player();
    CleanCursor clean{p};
    auto  bp = ctx.createBackpack(Tier::Leather);
    ctx.require(ctx.give(p, ctx.backpackItem(*bp), 3) == 3, "backpack in slot 3");
    setCursor(p, game::ItemConv::make("minecraft:dirt", 20));
    Location three{Area::Inventory, 3};
    ctx.check(overlay(p, three) == Result::Space, "space");
    ctx.check(rightClick(p, three) == 20, "20 stashed");
    ctx.check(cursor(p).isNull(), "cursor emptied");
    ctx.check(countOf(*bp, "minecraft:dirt") == 20, "dirt in the backpack");
});

ABP_REGISTER_SELFTEST("stash.unopened_backpack_has_space", [](Context& ctx) {
    auto& p = ctx.player();
    CleanCursor clean{p};
    ctx.require(ctx.give(p, game::makeBackpackItem(Tier::Leather), 4) == 4, "a never-opened backpack");
    setCursor(p, game::ItemConv::make("minecraft:stone", 5));
    Location four{Area::Inventory, 4};
    ctx.check(overlay(p, four) == Result::Space, "SB: no contents uuid -> space");
    ctx.check(rightClick(p, four) == 5, "stashed (the backpack gets its storage)");
    auto uuid = game::backpackUuid(p.getInventory().getItem(4));
    ctx.require(uuid.has_value(), "uuid assigned and written back");
    auto bp = game::Storage::get().find(*uuid);
    ctx.check(bp && countOf(*bp, "minecraft:stone") == 5, "stone inside");
    if (bp) game::Storage::get().erase(*uuid);
});

ABP_REGISTER_SELFTEST("stash.no_space_and_memory", [](Context& ctx) {
    auto& p  = ctx.player();
    CleanCursor clean{p};
    auto  bp = ctx.createBackpack(Tier::Leather);
    auto& inv = bp->inventory();
    for (int i = 0; i < inv.size(); ++i) inv.set(i, data("minecraft:oak_log", 64));
    setCursor(p, ctx.backpackItem(*bp));
    ctx.require(ctx.give(p, game::ItemConv::make("minecraft:diamond", 2), 8) == 8, "diamonds");
    Location eight{Area::Inventory, 8};
    ctx.check(overlay(p, eight) == Result::NoSpace, "full backpack: no sign");
    ctx.check(rightClick(p, eight) == 0, "nothing stashed");
    ctx.check(p.getInventory().getItem(8).mCount == 2, "diamonds untouched");
    inv.set(0, {});
    feature::gui::setMemory(*bp, 1, data("minecraft:diamond", 1));
    ctx.check(stashable(&p, *bp, data("minecraft:diamond", 2)) == Result::MatchAndSpace, "memory slot remembers diamonds: match");
    ctx.check(stashable(&p, *bp, data("minecraft:emerald", 2)) == Result::Space, "other item: space only");
    feature::gui::setMemory(*bp, 1, {});
});

// The backpack screen: storage items into the backpack on the cursor; the open backpack's own
// inventory slot is locked.
ABP_REGISTER_SELFTEST("stash.storage_and_locked_slot", [](Context& ctx) {
    auto& p     = ctx.player();
    CleanCursor clean{p};
    auto  outer = ctx.createBackpack(Tier::Iron);
    auto  inner = ctx.createBackpack(Tier::Leather);
    outer->inventory().set(2, data("minecraft:iron_ingot", 100));
    ctx.require(ctx.give(p, ctx.backpackItem(*outer), 9) == 9, "outer backpack in slot 9");
    ctx.require(ui::BackpackMenu::openFor(p, {game::ItemLocation::Kind::Inventory, 9}), "open it");
    ui::MenuManager::get().forceShown(p);
    setCursor(p, ctx.backpackItem(*inner));
    Location storage{Area::Storage, 2};
    ctx.check(itemAt(p, storage).count == 100, "storage slot seen");
    ctx.check(overlay(p, storage) == Result::Space, "storage item: space");
    int moved = rightClick(p, storage);
    ctx.check(moved > 0, "moved " + std::to_string(moved));
    ctx.check(countOf(*inner, "minecraft:iron_ingot") == moved, "inner backpack got them");
    ctx.check(countOf(*outer, "minecraft:iron_ingot") == 100 - moved, "outer lost them");
    ctx.check(overlay(p, Location{Area::Inventory, 9}) == Result::NoSpace, "the open backpack's slot is locked");
});

// SB key actions: first backpack in SB order (chest slot, offhand, inventory 0..35); toggles only
// for upgrades that can be disabled.
ABP_REGISTER_SELFTEST("input.key_order_and_toggle", [](Context& ctx) {
    auto& p   = ctx.player();
    auto  inv = ctx.createBackpack(Tier::Copper);
    auto  worn = ctx.createBackpack(Tier::Iron);
    ctx.installUpgrade(inv, "magnet");
    ctx.installUpgrade(worn, "magnet");
    ctx.require(ctx.give(p, ctx.backpackItem(*inv), 0) == 0, "backpack in the selected hotbar slot");
    game::setItemAt(p, {game::ItemLocation::Kind::Armor, 1}, ctx.backpackItem(*worn));
    ctx.check(net::handleKeyAction(p, nlohmann::json{{"action", "toggleUpgrade"}, {"slot", 0}}), "toggle upgrade 1");
    ctx.check(!worn->data().upgrades[0].enabled && inv->data().upgrades[0].enabled, "the worn backpack comes first (SB order)");
    ctx.check(net::handleKeyAction(p, nlohmann::json{{"action", "toggleUpgrade"}, {"slot", 0}}), "toggle back");
    ctx.check(net::handleKeyAction(p, nlohmann::json{{"action", "open"}}), "open");
    auto menu = std::dynamic_pointer_cast<ui::BackpackMenu>(ui::MenuManager::get().current(p));
    ctx.check(menu && menu->backpack()->uuid() == worn->uuid(), "Open Backpack opens the worn backpack first");
    ui::MenuManager::get().close(p);
    game::setItemAt(p, {game::ItemLocation::Kind::Armor, 1}, ItemStack{});
    ctx.check(net::handleKeyAction(p, nlohmann::json{{"action", "open"}}), "open again");
    menu = std::dynamic_pointer_cast<ui::BackpackMenu>(ui::MenuManager::get().current(p));
    ctx.check(menu && menu->backpack()->uuid() == inv->uuid(), "then the inventory one");
    ui::MenuManager::get().close(p);
    ctx.check(!net::handleKeyAction(p, nlohmann::json{{"action", "toggleUpgrade"}, {"slot", 1}}), "empty upgrade slot: nothing");
});

ABP_REGISTER_SELFTEST("input.stash_key_actions", [](Context& ctx) {
    auto& p  = ctx.player();
    CleanCursor clean{p};
    auto  bp = ctx.createBackpack(Tier::Leather);
    setCursor(p, ctx.backpackItem(*bp));
    ctx.require(ctx.give(p, game::ItemConv::make("minecraft:sand", 7), 12) == 12, "sand");
    ctx.check(net::handleKeyAction(p, nlohmann::json{{"action", "stash"}, {"area", "inventory"}, {"slot", 12}}), "stash action");
    ctx.check(countOf(*bp, "minecraft:sand") == 7, "sand stashed");
    ctx.check(!net::handleKeyAction(p, nlohmann::json{{"action", "stash"}, {"area", "inventory"}, {"slot", 12}}), "nothing left");
    ctx.check(!net::handleKeyAction(p, nlohmann::json{{"action", "stash"}, {"area", "bogus"}, {"slot", 1}}), "bad location");
});

// GX-3: SB stashes in any container slot: the crafting grid (UI container slots) here; the client
// e2e covers the crafting table and furnace screens.
ABP_REGISTER_SELFTEST("stash.vanilla_station_slots", [](Context& ctx) {
    auto& p = ctx.player();
    CleanCursor clean{p};
    auto num     = [](ContainerEnumName e) { return std::to_string(static_cast<int>(e)); };
    auto grid    = num(ContainerEnumName::CraftingInputContainer);
    auto preview = num(ContainerEnumName::CraftingOutputPreviewContainer);
    ctx.check(stashableContainer(static_cast<int>(ContainerEnumName::CraftingInputContainer)), "crafting grid slots stash");
    ctx.check(stashableContainer(static_cast<int>(ContainerEnumName::FurnaceResultContainer)), "furnace result slots stash");
    ctx.check(stashableContainer(static_cast<int>(ContainerEnumName::SmithingTableInputContainer)), "smithing input slots stash");
    ctx.check(!stashableContainer(static_cast<int>(ContainerEnumName::CraftingOutputPreviewContainer)), "the crafting output preview does not (not a real slot)");
    ctx.check(!stashableContainer(static_cast<int>(ContainerEnumName::CursorContainer)), "the cursor does not");
    ctx.check(!stashableContainer(-1) && !stashableContainer(9999), "out of range");
    ctx.check(!parseLocation(nlohmann::json{{"area", "vanilla.13x"}, {"slot", 0}}).has_value(), "malformed area refused");

    auto bp       = ctx.createBackpack(Tier::Leather);
    int  gridSlot = static_cast<int>(PlayerUISlot::Crafting2x2Input1) + 1;
    p.mPlayerUIContainer->setItem(gridSlot, game::ItemConv::make("minecraft:oak_planks", 3));
    setCursor(p, ctx.backpackItem(*bp));
    nlohmann::json at{{"area", "vanilla." + grid}, {"slot", 1}};
    auto           loc = parseLocation(at);
    ctx.require(loc && loc->area == Area::Vanilla && std::to_string(loc->container) == grid, "parsed");
    ctx.check(key(*loc) == "vanilla." + grid + ":1", "key: " + key(*loc));
    ctx.check(itemAt(p, *loc).id == "minecraft:oak_planks", "the 2x2 grid slot (inventory screen)");
    ctx.check(overlay(p, *loc) == Result::Space, "'-' sign: the backpack on the cursor has space");
    auto q = query(p, nlohmann::json{{"slots", nlohmann::json::array({at})}});
    ctx.check(q["results"].value("vanilla." + grid + ":1", 0) == 1, "query answers for the grid slot: " + q.dump());
    at["action"] = "stash";
    ctx.check(net::handleKeyAction(p, at), "right click stashes from the crafting grid");
    ctx.check(countOf(*bp, "minecraft:oak_planks") == 3, "planks in the backpack");
    ctx.check(p.mPlayerUIContainer->getItem(gridSlot).isNull(), "grid slot emptied");

    // A backpack in the grid takes the cursor (overrideOtherStackedOnMe).
    auto other = ctx.createBackpack(Tier::Leather);
    p.mPlayerUIContainer->setItem(gridSlot, ctx.backpackItem(*other));
    setCursor(p, game::ItemConv::make("minecraft:stick", 5));
    ctx.check(net::handleKeyAction(p, nlohmann::json{{"action", "stash"}, {"area", "vanilla." + grid}, {"slot", 1}}), "cursor into the grid backpack");
    ctx.check(countOf(*other, "minecraft:stick") == 5 && cursor(p).isNull(), "sticks stashed");
    ctx.check(inOpenContainer(p, other->uuid()), "a backpack in the grid may show its contents");
    p.mPlayerUIContainer->setItem(gridSlot, ItemStack{});
    ctx.check(!net::handleKeyAction(p, nlohmann::json{{"action", "stash"}, {"area", "vanilla." + preview}, {"slot", 0}}), "previews are refused");
});

} // namespace
} // namespace abp::feature::stash

// `/backpack input setup` (client e2e `input.ps1`): two filled backpacks, loose items and two
// upgrade items in the hotbar.
//   slot 0: gold backpack A (magnet, deposit [cobblestone], inception; storage: leather backpack C,
//           40 cobblestone, 30 dirt, 12 apples)
//   slot 1: copper backpack B (magnet; storage: 20 cobblestone)
//   slot 2: 32 cobblestone, slot 3: 16 dirt, slot 4: 5 diamonds, slot 5: pickup upgrade,
//   slot 6: advanced tool swapper upgrade
ABP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    cmd.overload().text("input").text("setup").execute([](CommandOrigin const& origin, CommandOutput& output) {
        using namespace abp;
        auto* e = origin.getEntity();
        if (!e || !e->isPlayer() || !game::Storage::get().available()) {
            output.error("players only");
            return;
        }
        auto& player  = static_cast<Player&>(*e);
        auto& storage = game::Storage::get();
        auto  a       = storage.create(core::Tier::Gold);
        auto  b       = storage.create(core::Tier::Copper);
        auto  c       = storage.create(core::Tier::Leather);
        if (!a || !b || !c) {
            output.error("could not create backpacks");
            return;
        }
        auto installAll = [&](std::shared_ptr<game::Backpack> const& bp, std::initializer_list<char const*> ids) {
            for (auto const* id : ids) {
                auto* type = upgrade::byId(id);
                if (!type) continue;
                for (int s = 0; s < static_cast<int>(bp->data().upgrades.size()); ++s) {
                    if (!bp->data().upgrades[static_cast<std::size_t>(s)].empty()) continue;
                    (void)upgrade::install(bp, s, game::ItemConv::toData(game::ItemConv::make(type->info().itemId, 1)), &player);
                    break;
                }
            }
        };
        installAll(a, {"magnet", "deposit", "inception"});
        installAll(b, {"magnet"});
        for (int s = 0; s < static_cast<int>(a->data().upgrades.size()); ++s) {
            auto const& st = a->data().upgrades[static_cast<std::size_t>(s)];
            if (auto* type = st.empty() ? nullptr : upgrade::upgradeFor(st.item); type && type->info().id == "deposit") {
                upgrade::transfer::test::setFilter(upgrade::UpgradeContext(a, s, &player), false, {"minecraft:cobblestone"}, core::FilterMode::Allow);
            }
        }
        auto put = [](game::Backpack& bp, int slot, std::string_view id, int count) {
            bp.inventory().set(slot, game::ItemConv::toData(game::ItemConv::make(id, 1)).withCount(count));
        };
        a->inventory().set(0, game::ItemConv::toData(game::makeBackpackItem(core::Tier::Leather, c->uuid())));
        put(*a, 1, "minecraft:cobblestone", 40);
        put(*a, 2, "minecraft:dirt", 30);
        put(*a, 3, "minecraft:apple", 12);
        put(*b, 0, "minecraft:cobblestone", 20);
        for (auto const& bp : {a, b, c}) {
            bp->markDirty();
            storage.save(*bp, true);
        }
        auto give = [&](int slot, ItemStack const& item) { game::setItemAt(player, {game::ItemLocation::Kind::Inventory, slot}, item); };
        give(0, game::makeBackpackItem(core::Tier::Gold, a->uuid()));
        give(1, game::makeBackpackItem(core::Tier::Copper, b->uuid()));
        give(2, game::ItemConv::make("minecraft:cobblestone", 32));
        give(3, game::ItemConv::make("minecraft:dirt", 16));
        give(4, game::ItemConv::make("minecraft:diamond", 5));
        if (auto* t = upgrade::byId("pickup")) give(5, game::ItemConv::make(t->info().itemId, 1));
        if (auto* t = upgrade::byId("advanced_tool_swapper")) give(6, game::ItemConv::make(t->info().itemId, 1));
        player.sendInventory(false);
        abp::log().info("input setup: A={} B={} C={}", a->uuid(), b->uuid(), c->uuid());
        output.success("input setup: A=" + a->uuid() + " B=" + b->uuid() + " C=" + c->uuid());
    });
});
