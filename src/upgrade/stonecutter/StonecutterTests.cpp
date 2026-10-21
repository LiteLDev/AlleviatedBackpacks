// Selftests for the Stonecutter upgrade: stonecutter.*
// Run in-game with `/backpack selftest "stonecutter.*"`.

#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "game/ItemConv.h"
#include "selftest/SelfTest.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/stonecutter/StonecutterUpgrade.h"

namespace abp::upgrade::stonecutter {
namespace {

using core::Tier;
using crafting::TabMenu;
using selftest::Context;
using Where  = ui::MenuManager::SlotAddress::Where;
using Action = ui::MenuManager::ActionType;

core::ItemStackData data(std::string_view id, int count) {
    return game::ItemConv::toData(game::ItemConv::make(id, count));
}

int countIn(game::Backpack& bp, std::string_view id) {
    return static_cast<int>(bp.inventory().count([&](core::ItemStackData const& s) { return s.id == id; }));
}

ItemStack cursorOf(Player& p) { return p.mPlayerUIContainer->getItem(0); }

std::string idOf(ItemStack const& s) { return s.isNull() ? std::string("<empty>") : s.getTypeName(); }

UpgradeContext withStonecutter(Context& ctx) {
    auto bp   = ctx.createBackpack(Tier::Iron);
    int  slot = ctx.installUpgrade(bp, std::string(crafting::kStonecutterId));
    return UpgradeContext(bp, slot);
}

/// Selects the option producing `resultId`; returns false if there is none.
bool selectResult(UpgradeContext const& u, std::string_view resultId) {
    for (auto const& o : options(u)) {
        if (idOf(o.result) == resultId) {
            select(u, o.recipeId);
            return true;
        }
    }
    return false;
}

ABP_REGISTER_SELFTEST("stonecutter.options", [](Context& ctx) {
    auto u = withStonecutter(ctx);
    ctx.check(options(u).empty(), "no input, no options");
    setInput(u, data("minecraft:stone", 1));
    auto list = options(u);
    ctx.check(list.size() >= 3, "stone has several outputs, got " + std::to_string(list.size()));
    bool bricks = false, twoOf = false;
    for (auto const& o : list) {
        bricks = bricks || idOf(o.result) == "minecraft:stone_bricks";
        twoOf  = twoOf || o.result.mCount == 2; // slabs
    }
    ctx.check(bricks, "stone -> stone bricks offered");
    ctx.check(twoOf, "an output with 2 items (slab) offered");
    setInput(u, data("minecraft:dirt", 1));
    ctx.check(options(u).empty(), "dirt has no stonecutter outputs");
});

ABP_REGISTER_SELFTEST("stonecutter.selection_validity", [](Context& ctx) {
    auto u = withStonecutter(ctx);
    setInput(u, data("minecraft:stone", 4));
    ctx.require(selectResult(u, "minecraft:stone_bricks"), "select bricks");
    ctx.check(selectedOption(u).has_value(), "selection valid for stone");
    setInput(u, data("minecraft:oak_log", 4));
    ctx.check(!selectedOption(u).has_value(), "selection invalid for logs");
    ctx.check(craftOnce(u).isNull(), "nothing crafted with an invalid selection");
    ctx.check(inputOf(u).count == 4, "input untouched");
});

ABP_REGISTER_SELFTEST("stonecutter.click_crafts_to_cursor", [](Context& ctx) {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto  u      = withStonecutter(ctx);
    setInput(u, data("minecraft:stone", 3));
    ctx.require(crafting::openTab(player, u.backpackPtr(), crafting::kStonecutterId), "open stonecutter tab");
    mm.forceShown(player);
    ctx.check(mm.shownItem(player, kResultSlot).isNull(), "no result before selecting");
    // Click the button showing stone bricks.
    int button = -1;
    for (int s : kRecipeSlots) {
        if (idOf(mm.shownItem(player, s)) == "minecraft:stone_bricks") button = s;
    }
    ctx.require(button >= 0, "stone bricks button shown");
    (void)mm.simulate(
        player,
        {
            Action::Take,
            {Where::Menu,   button},
            {Where::Cursor, 0     },
            1
    }
    );
    ctx.check(cursorOf(player).isNull(), "recipe buttons never give items");
    ctx.require(selectedOption(u) && idOf(selectedOption(u)->result) == "minecraft:stone_bricks", "bricks selected");
    mm.forceShown(player);
    ctx.check(idOf(mm.shownItem(player, kResultSlot)) == "minecraft:stone_bricks", "result slot shows bricks");
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
        idOf(cursorOf(player)) == "minecraft:stone_bricks" && cursorOf(player).mCount == 1,
        "1 brick on the cursor"
    );
    ctx.check(inputOf(u).count == 2, "one stone consumed");
    mm.close(player);
});

ABP_REGISTER_SELFTEST("stonecutter.shift_click_max", [](Context& ctx) {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto  u      = withStonecutter(ctx);
    setInput(u, data("minecraft:stone", 10));
    ctx.require(selectResult(u, "minecraft:stone_bricks"), "select bricks");
    ctx.require(crafting::openTab(player, u.backpackPtr(), crafting::kStonecutterId), "open");
    mm.forceShown(player);
    (void)mm.simulate(
        player,
        {
            Action::Take,
            {Where::Menu,      kResultSlot},
            {Where::Inventory, 9          },
            1
    }
    );
    ctx.check(countIn(u.backpack(), "minecraft:stone_bricks") == 10, "10 bricks into the backpack");
    ctx.check(inputOf(u).empty(), "input used up");
    mm.close(player);
});

ABP_REGISTER_SELFTEST("stonecutter.quick_move_into_input", [](Context& ctx) {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto  u      = withStonecutter(ctx);
    ctx.require(crafting::openTab(player, u.backpackPtr(), crafting::kStonecutterId), "open");
    mm.forceShown(player);
    ctx.require(ctx.give(player, game::ItemConv::make("minecraft:cobblestone", 32), 3) == 3, "give");
    ctx.check(
        mm.simulate(
            player,
            {
                Action::Place,
                {Where::Inventory, 3         },
                {Where::Menu,      kInputSlot},
                32
    }
        ),
        "shift-click in"
    );
    ctx.check(inputOf(u).count == 32 && inputOf(u).id == "minecraft:cobblestone", "goes into the input slot");
    ctx.require(ctx.give(player, game::ItemConv::make("minecraft:dirt", 5), 4) == 4, "give dirt");
    ctx.check(
        mm.simulate(
            player,
            {
                Action::Place,
                {Where::Inventory, 4         },
                {Where::Menu,      kInputSlot},
                5
    }
        ),
        "shift-click other item"
    );
    ctx.check(countIn(u.backpack(), "minecraft:dirt") == 5, "other items go into the backpack");
    mm.close(player);
});

ABP_REGISTER_SELFTEST("stonecutter.remove_returns_input", [](Context& ctx) {
    auto& player = ctx.player();
    auto  u      = withStonecutter(ctx);
    auto  bp     = u.backpackPtr();
    setInput(u, data("minecraft:stone", 12));
    auto item = upgrade::remove(bp, u.slot(), &player);
    ctx.require(item.has_value(), "removed");
    ctx.check(countIn(*bp, "minecraft:stone") == 12, "input moved into the backpack");
    auto        other = ctx.createBackpack(Tier::Iron);
    std::string reason;
    ctx.require(upgrade::install(other, 0, *item, &player, &reason), "reinstall: " + reason);
    ctx.check(inputOf(UpgradeContext(other, 0)).empty(), "no input travels with the item");
});

} // namespace
} // namespace abp::upgrade::stonecutter
