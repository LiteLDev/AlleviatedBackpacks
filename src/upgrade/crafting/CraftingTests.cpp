// Selftests for the Crafting upgrade: crafting.*
// Run in-game with `/backpack selftest "crafting.*"`.

#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "game/ItemConv.h"
#include "game/Storage.h"
#include "selftest/SelfTest.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/crafting/CraftingUpgrade.h"

namespace abp::upgrade::crafting {
namespace {

using core::Tier;
using selftest::Context;
using Where  = ui::MenuManager::SlotAddress::Where;
using Action = ui::MenuManager::ActionType;

core::ItemStackData data(std::string_view id, int count) {
    return game::ItemConv::toData(game::ItemConv::make(id, count));
}

int countIn(game::Backpack& bp, std::string_view id) {
    return static_cast<int>(bp.inventory().count([&](core::ItemStackData const& s) { return s.id == id; }));
}

int countOnPlayer(Player& p, std::string_view id) {
    int n = 0;
    for (int i = 0; i < 36; ++i) {
        auto const& it = p.getInventory().getItem(i);
        if (!it.isNull() && it.getTypeName() == id) n += it.mCount;
    }
    return n;
}

ItemStack cursorOf(Player& p) { return p.mPlayerUIContainer->getItem(0); }

std::string idOf(ItemStack const& s) { return s.isNull() ? std::string("<empty>") : s.getTypeName(); }

/// A backpack with the crafting upgrade installed; returns its context.
UpgradeContext withCrafting(Context& ctx, Tier tier = Tier::Iron) {
    auto bp   = ctx.createBackpack(tier);
    int  slot = ctx.installUpgrade(bp, std::string(kCraftingId));
    return UpgradeContext(bp, slot);
}

void setGrid(UpgradeContext const& u, std::initializer_list<std::pair<int, core::ItemStackData>> items) {
    for (int i = 0; i < 9; ++i) setGridAt(u, i, {});
    for (auto const& [i, d] : items) setGridAt(u, i, d);
}

std::string resultId(UpgradeContext const& u) {
    auto m = currentMatch(u);
    return m ? idOf(m->result) : std::string("<none>");
}

ABP_REGISTER_SELFTEST("crafting.recipes_available", [](Context& ctx) {
    ctx.check(recipeCount(kCraftingTag) > 100, "crafting_table recipes: " + std::to_string(recipeCount(kCraftingTag)));
    ctx.check(
        recipeCount(kStonecutterTag) > 10,
        "stonecutter recipes: " + std::to_string(recipeCount(kStonecutterTag))
    );
    ctx.require(upgrade::byId(kCraftingId) != nullptr, "crafting upgrade registered");
    ctx.require(!game::ItemConv::make("alleviatedbackpacks:crafting_upgrade").isNull(), "crafting upgrade item exists");
});

ABP_REGISTER_SELFTEST("crafting.shaped_match", [](Context& ctx) {
    auto u = withCrafting(ctx);
    setGrid(
        u,
        {
            {0, data("minecraft:oak_planks", 1)},
            {1, data("minecraft:oak_planks", 1)},
            {3, data("minecraft:oak_planks", 1)},
            {4, data("minecraft:oak_planks", 1)}
    }
    );
    ctx.check(resultId(u) == "minecraft:crafting_table", "2x2 planks top-left -> crafting table, got " + resultId(u));
    setGrid(
        u,
        {
            {4, data("minecraft:oak_planks", 1)},
            {5, data("minecraft:oak_planks", 1)},
            {7, data("minecraft:oak_planks", 1)},
            {8, data("minecraft:oak_planks", 1)}
    }
    );
    ctx.check(
        resultId(u) == "minecraft:crafting_table",
        "2x2 planks bottom-right -> crafting table, got " + resultId(u)
    );
    setGrid(
        u,
        {
            {2, data("minecraft:oak_planks", 1)},
            {5, data("minecraft:oak_planks", 1)}
    }
    );
    auto m = currentMatch(u);
    ctx.check(
        m && idOf(m->result) == "minecraft:stick" && m->result.mCount == 4,
        "vertical planks -> 4 sticks, got " + resultId(u)
    );
    setGrid(
        u,
        {
            {0, data("minecraft:oak_planks", 1)},
            {1, data("minecraft:oak_planks", 1)}
    }
    );
    ctx.check(!currentMatch(u) || resultId(u) != "minecraft:stick", "horizontal planks are not sticks");
    setGrid(
        u,
        {
            {0, data("minecraft:dirt",    1)},
            {4, data("minecraft:diamond", 1)}
    }
    );
    ctx.check(!currentMatch(u), "nonsense grid has no result, got " + resultId(u));
});

ABP_REGISTER_SELFTEST("crafting.tag_variants", [](Context& ctx) {
    auto u = withCrafting(ctx);
    setGrid(
        u,
        {
            {0, data("minecraft:oak_planks",    1)},
            {1, data("minecraft:spruce_planks", 1)},
            {3, data("minecraft:birch_planks",  1)},
            {4, data("minecraft:jungle_planks", 1)}
    }
    );
    ctx.check(
        resultId(u) == "minecraft:crafting_table",
        "mixed planks (tag ingredient) -> crafting table, got " + resultId(u)
    );
});

ABP_REGISTER_SELFTEST("crafting.shapeless_match", [](Context& ctx) {
    auto u = withCrafting(ctx);
    setGrid(
        u,
        {
            {0, data("minecraft:paper",   1)},
            {4, data("minecraft:leather", 1)},
            {6, data("minecraft:paper",   1)},
            {8, data("minecraft:paper",   1)}
    }
    );
    ctx.check(resultId(u) == "minecraft:book", "scattered paper + leather -> book, got " + resultId(u));
    setGrid(
        u,
        {
            {8, data("minecraft:iron_ingot", 1)},
            {1, data("minecraft:flint",      1)}
    }
    );
    ctx.check(resultId(u) == "minecraft:flint_and_steel", "iron + flint -> flint and steel, got " + resultId(u));
});

ABP_REGISTER_SELFTEST("crafting.click_crafts_to_cursor", [](Context& ctx) {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto  u      = withCrafting(ctx);
    setGrid(
        u,
        {
            {0, data("minecraft:oak_planks", 2)},
            {1, data("minecraft:oak_planks", 2)},
            {3, data("minecraft:oak_planks", 2)},
            {4, data("minecraft:oak_planks", 2)}
    }
    );
    ctx.require(openTab(player, u.backpackPtr(), kCraftingId), "open crafting tab");
    mm.forceShown(player);
    ctx.require(std::dynamic_pointer_cast<TabMenu>(mm.current(player)) != nullptr, "crafting menu is open");
    ctx.check(
        idOf(mm.shownItem(player, kResultSlot)) == "minecraft:crafting_table",
        "result slot shows the crafting table"
    );
    ctx.check(idOf(mm.shownItem(player, kGridMenuSlots[0])) == "minecraft:oak_planks", "grid slot shows planks");
    (void)mm.simulate(
        player,
        {
            Action::Take,
            {Where::Menu,   kResultSlot},
            {Where::Cursor, 0          },
            1
    }
    );
    ctx.check(
        idOf(cursorOf(player)) == "minecraft:crafting_table" && cursorOf(player).mCount == 1,
        "cursor holds 1 crafting table"
    );
    for (int i : {0, 1, 3, 4})
        ctx.check(gridAt(u, i).count == 1, "one plank consumed from grid slot " + std::to_string(i));
    (void)mm.simulate(
        player,
        {
            Action::Take,
            {Where::Menu,   kResultSlot},
            {Where::Cursor, 0          },
            1
    }
    );
    ctx.check(cursorOf(player).mCount == 2, "second click stacks on the cursor");
    for (int i : {0, 1, 3, 4}) ctx.check(gridAt(u, i).empty(), "grid slot " + std::to_string(i) + " used up");
    (void)mm.simulate(
        player,
        {
            Action::Take,
            {Where::Menu,   kResultSlot},
            {Where::Cursor, 0          },
            1
    }
    );
    ctx.check(cursorOf(player).mCount == 2, "empty grid crafts nothing");
    mm.close(player);
    ctx.check(countOnPlayer(player, "minecraft:crafting_table") == 2, "closing returns the cursor items");
});

ABP_REGISTER_SELFTEST("crafting.cursor_mismatch_keeps_grid", [](Context& ctx) {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto  u      = withCrafting(ctx);
    setGrid(
        u,
        {
            {0, data("minecraft:oak_planks", 1)},
            {3, data("minecraft:oak_planks", 1)}
    }
    );
    ctx.require(openTab(player, u.backpackPtr(), kCraftingId), "open");
    mm.forceShown(player);
    ctx.require(ctx.give(player, game::ItemConv::make("minecraft:dirt", 5), 5) == 5, "give dirt");
    (void)mm.simulate(
        player,
        {
            Action::Take,
            {Where::Inventory, 5},
            {Where::Cursor,    0},
            5
    }
    );
    ctx.require(idOf(cursorOf(player)) == "minecraft:dirt", "dirt on cursor");
    (void)mm.simulate(
        player,
        {
            Action::Take,
            {Where::Menu,   kResultSlot},
            {Where::Cursor, 0          },
            1
    }
    );
    ctx.check(idOf(cursorOf(player)) == "minecraft:dirt" && cursorOf(player).mCount == 5, "cursor unchanged");
    ctx.check(gridAt(u, 0).count == 1 && gridAt(u, 3).count == 1, "nothing consumed");
    // Placing onto the result slot is refused too.
    (void)mm.simulate(
        player,
        {
            Action::Place,
            {Where::Cursor, 0          },
            {Where::Menu,   kResultSlot},
            5
    }
    );
    ctx.check(cursorOf(player).mCount == 5, "result slot refuses items");
    mm.close(player);
});

ABP_REGISTER_SELFTEST("crafting.grid_slots_hold_items", [](Context& ctx) {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto  u      = withCrafting(ctx);
    ctx.require(openTab(player, u.backpackPtr(), kCraftingId), "open");
    mm.forceShown(player);
    ctx.require(ctx.give(player, game::ItemConv::make("minecraft:cobblestone", 20), 2) == 2, "give cobblestone");
    ctx.check(
        mm.simulate(
            player,
            {
                Action::Take,
                {Where::Inventory, 2},
                {Where::Cursor,    0},
                20
    }
        ),
        "pick up"
    );
    ctx.check(
        mm.simulate(
            player,
            {
                Action::Place,
                {Where::Cursor, 0                },
                {Where::Menu,   kGridMenuSlots[4]},
                20
    }
        ),
        "place into grid centre"
    );
    ctx.check(gridAt(u, 4).count == 20 && gridAt(u, 4).id == "minecraft:cobblestone", "grid holds 20 cobblestone");
    ctx.check(cursorOf(player).isNull(), "cursor empty");
    ctx.check(
        mm.simulate(
            player,
            {
                Action::Take,
                {Where::Menu,   kGridMenuSlots[4]},
                {Where::Cursor, 0                },
                5
    }
        ),
        "take 5 back"
    );
    ctx.check(gridAt(u, 4).count == 15 && cursorOf(player).mCount == 5, "15 left in grid");
    // Shift-click a grid slot: into the backpack.
    (void)mm.simulate(
        player,
        {
            Action::Take,
            {Where::Menu,      kGridMenuSlots[4]},
            {Where::Inventory, 9                },
            15
    }
    );
    ctx.check(
        gridAt(u, 4).empty() && countIn(u.backpack(), "minecraft:cobblestone") == 15,
        "shift-click moves grid items into the backpack"
    );
    // Storage rows show the backpack.
    ctx.check(
        idOf(mm.shownItem(player, TabMenu::kStorageStart)) == "minecraft:cobblestone",
        "storage row shows backpack slot 0"
    );
    mm.close(player);
});

ABP_REGISTER_SELFTEST("crafting.grid_refuses_backpacks", [](Context& ctx) {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto  u      = withCrafting(ctx);
    auto  other  = ctx.createBackpack(Tier::Leather);
    ctx.require(openTab(player, u.backpackPtr(), kCraftingId), "open");
    mm.forceShown(player);
    ctx.require(ctx.give(player, ctx.backpackItem(*other), 4) == 4, "give a backpack");
    ctx.check(
        mm.simulate(
            player,
            {
                Action::Take,
                {Where::Inventory, 4},
                {Where::Cursor,    0},
                1
    }
        ),
        "pick up"
    );
    (void)mm.simulate(
        player,
        {
            Action::Place,
            {Where::Cursor, 0                },
            {Where::Menu,   kGridMenuSlots[0]},
            1
    }
    );
    ctx.check(gridAt(u, 0).empty(), "backpacks can't go into the grid (no nesting bypass)");
    ctx.check(!cursorOf(player).isNull(), "the backpack stays on the cursor");
    mm.close(player);
});

ABP_REGISTER_SELFTEST("crafting.container_items_remain", [](Context& ctx) {
    auto u = withCrafting(ctx);
    setGrid(
        u,
        {
            {0, data("minecraft:milk_bucket", 1)},
            {1, data("minecraft:milk_bucket", 1)},
            {2, data("minecraft:milk_bucket", 1)},
            {3, data("minecraft:sugar",       1)},
            {4, data("minecraft:egg",         1)},
            {5, data("minecraft:sugar",       1)},
            {6, data("minecraft:wheat",       1)},
            {7, data("minecraft:wheat",       1)},
            {8, data("minecraft:wheat",       1)}
    }
    );
    ctx.require(resultId(u) == "minecraft:cake", "cake recipe matches, got " + resultId(u));
    auto out = craftOnce(u, ctx.player());
    ctx.check(idOf(out) == "minecraft:cake", "crafted a cake");
    for (int i = 0; i < 3; ++i)
        ctx.check(gridAt(u, i).id == "minecraft:bucket", "empty bucket left in slot " + std::to_string(i));
    for (int i = 3; i < 9; ++i) ctx.check(gridAt(u, i).empty(), "slot " + std::to_string(i) + " consumed");
    ctx.check(!currentMatch(u), "buckets alone craft nothing");
});

ABP_REGISTER_SELFTEST("crafting.shift_click_into_backpack", [](Context& ctx) {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto  u      = withCrafting(ctx);
    setGrid(
        u,
        {
            {4, data("minecraft:oak_log", 10)}
    }
    );
    ctx.require(resultId(u) == "minecraft:oak_planks", "log -> planks, got " + resultId(u));
    ctx.require(openTab(player, u.backpackPtr(), kCraftingId), "open");
    mm.forceShown(player);
    (void)mm.simulate(
        player,
        {
            Action::Take,
            {Where::Menu,      kResultSlot},
            {Where::Inventory, 9          },
            4
    }
    );
    ctx.check(
        countIn(u.backpack(), "minecraft:oak_planks") == 40,
        "40 planks in the backpack, got " + std::to_string(countIn(u.backpack(), "minecraft:oak_planks"))
    );
    ctx.check(gridAt(u, 4).empty(), "all logs used");
    ctx.check(countOnPlayer(player, "minecraft:oak_planks") == 0, "nothing went to the player");
    mm.close(player);
});

ABP_REGISTER_SELFTEST("crafting.shift_click_into_inventory", [](Context& ctx) {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto  u      = withCrafting(ctx);
    setGrid(
        u,
        {
            {0, data("minecraft:oak_log", 3)}
    }
    );
    ctx.require(openTab(player, u.backpackPtr(), kCraftingId), "open");
    mm.forceShown(player);
    // Toggle the shift-click target button.
    (void)mm.simulate(
        player,
        {
            Action::Take,
            {Where::Menu,   TabMenu::kShiftSlot},
            {Where::Cursor, 0                  },
            1
    }
    );
    ctx.check(u.settings().value(std::string(kShiftIntoBackpackKey), true) == false, "target switched to inventory");
    (void)mm.simulate(
        player,
        {
            Action::Take,
            {Where::Menu,      kResultSlot},
            {Where::Inventory, 9          },
            4
    }
    );
    ctx.check(countOnPlayer(player, "minecraft:oak_planks") == 12, "12 planks in the player inventory");
    ctx.check(countIn(u.backpack(), "minecraft:oak_planks") == 0, "none in the backpack");
    ctx.check(cursorOf(player).isNull(), "buttons never give items");
    mm.close(player);
});

ABP_REGISTER_SELFTEST("crafting.refill_from_backpack", [](Context& ctx) {
    auto& player = ctx.player();
    auto  u      = withCrafting(ctx);
    u.backpack().inventory().insert(data("minecraft:oak_log", 5), core::AccessContext{core::Source::Internal});
    setGrid(
        u,
        {
            {0, data("minecraft:oak_log", 1)}
    }
    );
    ctx.require(refillEnabled(u), "refill is on by default");
    int crafts = craftMax(u, player, true);
    ctx.check(crafts == 6, "6 crafts with refill, got " + std::to_string(crafts));
    ctx.check(countIn(u.backpack(), "minecraft:oak_log") == 0, "backpack logs used up");
    ctx.check(countIn(u.backpack(), "minecraft:oak_planks") == 24, "24 planks");
    ctx.check(gridAt(u, 0).empty(), "grid empty at the end");

    u.settings()[std::string(kRefillKey)] = false;
    u.backpack().inventory().insert(data("minecraft:oak_log", 5), core::AccessContext{core::Source::Internal});
    setGrid(
        u,
        {
            {0, data("minecraft:oak_log", 1)}
    }
    );
    crafts = craftMax(u, player, true);
    ctx.check(crafts == 1, "without refill only the grid is used, got " + std::to_string(crafts));
    ctx.check(countIn(u.backpack(), "minecraft:oak_log") == 5, "backpack logs untouched");
});

ABP_REGISTER_SELFTEST("crafting.shift_click_stops_when_full", [](Context& ctx) {
    auto& player = ctx.player();
    auto  u      = withCrafting(ctx, Tier::Leather);
    // Fill the backpack and the player inventory completely.
    auto& inv = u.backpack().inventory();
    for (int i = 0; i < inv.size(); ++i) inv.set(i, data("minecraft:dirt", 64));
    for (int i = 0; i < 36; ++i) ctx.give(player, game::ItemConv::make("minecraft:dirt", 64), i);
    setGrid(
        u,
        {
            {0, data("minecraft:oak_log", 4)}
    }
    );
    int crafts = craftMax(u, player, true);
    ctx.check(crafts == 0, "no room: nothing crafted, got " + std::to_string(crafts));
    ctx.check(gridAt(u, 0).count == 4, "no ingredients consumed");
});

ABP_REGISTER_SELFTEST("crafting.grid_persists", [](Context& ctx) {
    auto& storage = game::Storage::get();
    auto  u       = withCrafting(ctx);
    auto  uuid    = u.backpack().uuid();
    int   slot    = u.slot();
    setGrid(
        u,
        {
            {2, data("minecraft:diamond", 7)}
    }
    );
    u = UpgradeContext(nullptr, 0); // drop our reference
    storage.unload(uuid);
    auto again = storage.find(uuid);
    ctx.require(again != nullptr, "reloaded");
    UpgradeContext u2(again, slot);
    ctx.check(gridAt(u2, 2).id == "minecraft:diamond" && gridAt(u2, 2).count == 7, "grid persisted with the backpack");
});

ABP_REGISTER_SELFTEST("crafting.remove_returns_grid", [](Context& ctx) {
    auto& player = ctx.player();
    auto  u      = withCrafting(ctx);
    auto  bp     = u.backpackPtr();
    setGrid(
        u,
        {
            {0, data("minecraft:iron_ingot", 5)},
            {8, data("minecraft:stick",      3)}
    }
    );
    auto item = upgrade::remove(bp, u.slot(), &player);
    ctx.require(item.has_value(), "upgrade removed");
    ctx.check(
        countIn(*bp, "minecraft:iron_ingot") == 5 && countIn(*bp, "minecraft:stick") == 3,
        "grid contents moved into the backpack"
    );
    auto        other = ctx.createBackpack(Tier::Iron);
    std::string reason;
    ctx.require(upgrade::install(other, 0, *item, &player, &reason), "reinstall: " + reason);
    UpgradeContext u2(other, 0);
    bool           empty = true;
    for (int i = 0; i < 9; ++i) empty = empty && gridAt(u2, i).empty();
    ctx.check(empty, "the upgrade item carries no grid items (no duplication)");
});

ABP_REGISTER_SELFTEST("crafting.backpack_button_opens_tab", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto  u      = withCrafting(ctx);
    ctx.require(ui::BackpackMenu::openBackpack(player, u.backpackPtr()), "open backpack");
    mm.forceShown(player);
    int button = -1;
    for (int i = 48; i < 52; ++i) {
        if (idOf(mm.shownItem(player, i)) == "minecraft:crafting_table") button = i;
    }
    ctx.require(button >= 0, "crafting button on the storage page");
    (void)mm.simulate(
        player,
        {
            Action::Take,
            {Where::Menu,   button},
            {Where::Cursor, 0     },
            1
    }
    );
    co_await ctx.ticks(10);
    auto tab = std::dynamic_pointer_cast<TabMenu>(mm.current(player));
    ctx.require(tab != nullptr, "crafting tab opened");
    mm.forceShown(player);
    ctx.check(tab->title() == "Crafting" || !tab->title().empty(), "titled");
    // Back to the backpack.
    (void)mm.simulate(
        player,
        {
            Action::Take,
            {Where::Menu,   TabMenu::kBackSlot},
            {Where::Cursor, 0                 },
            1
    }
    );
    co_await ctx.ticks(10);
    ctx.check(
        std::dynamic_pointer_cast<ui::BackpackMenu>(mm.current(player)) != nullptr,
        "back button reopens the backpack"
    );
    mm.close(player);
});

ABP_REGISTER_SELFTEST("crafting.closes_when_removed", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto  u      = withCrafting(ctx);
    ctx.require(openTab(player, u.backpackPtr(), kCraftingId), "open");
    mm.forceShown(player);
    (void)upgrade::remove(u.backpackPtr(), u.slot(), &player);
    co_await ctx.ticks(3);
    ctx.check(mm.current(player) == nullptr, "menu closes when the upgrade is removed");
});

} // namespace
} // namespace abp::upgrade::crafting
