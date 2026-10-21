// Refill selftests: `backpack selftest "refill.*"`.

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "selftest/SelfTest.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/refill/Refill.h"

namespace abp::upgrade::refill {
namespace {

using core::Tier;
using selftest::Context;
using Where  = ui::MenuManager::SlotAddress::Where;
using Action = ui::MenuManager::ActionType;

core::AccessContext const kInternal{core::Source::Internal};

core::ItemStackData data(std::string_view id, int count) {
    return game::ItemConv::toData(game::ItemConv::make(id, count));
}

int countIn(game::Backpack& bp, std::string_view id) {
    return static_cast<int>(bp.inventory().count([&](core::ItemStackData const& s) { return s.id == id; }));
}

std::string idAt(Player& p, int slot) {
    return p.getInventory().getItem(slot).isNull() ? "" : p.getInventory().getItem(slot).getTypeName();
}
int countAt(Player& p, int slot) {
    return p.getInventory().getItem(slot).isNull() ? 0 : p.getInventory().getItem(slot).mCount;
}

void setFilter(UpgradeContext const& ctx, int slots, std::vector<std::pair<std::string_view, Target>> const& entries) {
    auto s = load(ctx, slots);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        s.filter.setGhost(static_cast<int>(i), data(entries[i].first, 1));
        s.targets[i] = entries[i].second;
    }
    save(ctx, s);
}

void clearSlot(Player& p, int slot) {
    p.getInventory().setItem(slot, ItemStack::EMPTY_ITEM());
    p.sendInventory(false);
}

ABP_REGISTER_SELFTEST("refill.registration", [](Context& ctx) {
    for (auto id : {kBasicId, kAdvancedId}) {
        auto* u = upgrade::byId(id);
        ctx.require(u != nullptr, std::string(id) + " registered");
        ctx.check(
            !game::ItemConv::make(u->info().itemId, 1).isNull(),
            u->info().itemId + " item exists (behavior pack)"
        );
        ctx.check(u->info().hasSettings, "has settings");
    }
    auto bp = ctx.createBackpack(Tier::Gold);
    ctx.installUpgrade(bp, std::string(kBasicId), 0);
    auto adv = data("alleviatedbackpacks:advanced_refill_upgrade", 1);
    ctx.check(!upgrade::canInstall(bp, 1, adv, nullptr), "basic and advanced refill don't stack (same group)");
});

ABP_REGISTER_SELFTEST("refill.settings_roundtrip", [](Context& ctx) {
    auto           bp   = ctx.createBackpack(Tier::Iron);
    int            slot = ctx.installUpgrade(bp, std::string(kAdvancedId));
    UpgradeContext uc(bp, slot);
    setFilter(
        uc,
        kAdvancedSlots,
        {
            {"minecraft:torch", Target::Hotbar1},
            {"minecraft:bread", Target::Offhand}
    }
    );
    auto s = load(uc, kAdvancedSlots);
    ctx.check(static_cast<int>(s.filter.ghosts.size()) == kAdvancedSlots, "12 filter slots");
    ctx.check(s.filter.ghosts[0].id == "minecraft:torch" && s.targets[0] == Target::Hotbar1, "slot 0 kept");
    ctx.check(s.filter.ghosts[1].id == "minecraft:bread" && s.targets[1] == Target::Offhand, "slot 1 kept");
    ctx.check(s.targets[2] == Target::Any, "default target is any");
    auto t = Target::Any;
    for (int i = 0; i < kTargetCount; ++i) t = nextTarget(t);
    ctx.check(t == Target::Any, "target cycle wraps after 12 steps");
    // Settings travel with the item.
    auto removed = upgrade::remove(bp, slot, nullptr);
    ctx.require(removed.has_value(), "remove");
    ctx.require(upgrade::install(bp, 1, *removed, nullptr), "reinstall");
    ctx.check(
        load(UpgradeContext(bp, 1), kAdvancedSlots).filter.ghosts[0].id == "minecraft:torch",
        "filter travels with the item"
    );
    // Matching: NBT only when asked.
    auto named = game::ItemConv::make("minecraft:diamond_sword", 1);
    game::ItemConv::setDisplay(named, "Named");
    auto         namedData = game::ItemConv::toData(named);
    auto         plain     = data("minecraft:diamond_sword", 1);
    core::Filter f(1);
    ctx.check(matchesGhost(f, plain, namedData), "id match ignores NBT by default");
    f.matchNbt = true;
    ctx.check(!matchesGhost(f, plain, namedData), "match NBT distinguishes named items");
    ctx.check(!matchesGhost(f, plain, data("minecraft:iron_sword", 1)), "other items never match");
});

ABP_REGISTER_SELFTEST("refill.basic_tops_up", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(Tier::Leather);
    int   slot   = ctx.installUpgrade(bp, std::string(kBasicId));
    setFilter(
        UpgradeContext(bp, slot),
        kBasicSlots,
        {
            {"minecraft:cobblestone", Target::Hotbar1}
    }
    ); // basic ignores targets
    bp->inventory().insert(data("minecraft:cobblestone", 100), kInternal);
    bp->inventory().insert(data("minecraft:dirt", 50), kInternal);
    ctx.require(ctx.give(player, ctx.backpackItem(*bp), 8) == 8, "give backpack");
    ctx.require(ctx.give(player, game::ItemConv::make("minecraft:cobblestone", 10), 4) == 4, "give cobblestone");
    ctx.require(ctx.give(player, game::ItemConv::make("minecraft:dirt", 5), 5) == 5, "give dirt");
    co_await ctx.ticks(25);
    ctx.check(
        countAt(player, 4) == 64,
        "cobblestone topped up to a full stack (got " + std::to_string(countAt(player, 4)) + ")"
    );
    ctx.check(countIn(*bp, "minecraft:cobblestone") == 46, "54 taken from the backpack");
    ctx.check(
        countAt(player, 5) == 5 && countIn(*bp, "minecraft:dirt") == 50,
        "items outside the filter are untouched"
    );
    // The stack runs out (last block placed): it comes back into the same slot.
    clearSlot(player, 4);
    co_await ctx.ticks(25);
    ctx.check(
        idAt(player, 4) == "minecraft:cobblestone" && countAt(player, 4) == 46,
        "refilled into the slot it ran out in"
    );
    ctx.check(idAt(player, 0).empty(), "not put into the first free slot");
    ctx.check(countIn(*bp, "minecraft:cobblestone") == 0, "backpack emptied of cobblestone");
});

ABP_REGISTER_SELFTEST("refill.disabled_or_menu_open", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(Tier::Leather);
    int   slot   = ctx.installUpgrade(bp, std::string(kBasicId));
    setFilter(
        UpgradeContext(bp, slot),
        kBasicSlots,
        {
            {"minecraft:arrow", Target::Any}
    }
    );
    bp->inventory().insert(data("minecraft:arrow", 64), kInternal);
    upgrade::setEnabled(bp, slot, false);
    ctx.require(ctx.give(player, ctx.backpackItem(*bp), 8) == 8, "give backpack");
    co_await ctx.ticks(25);
    ctx.check(countIn(*bp, "minecraft:arrow") == 64, "disabled upgrade does nothing");
    upgrade::setEnabled(bp, slot, true);
    ctx.check(refillNow(player, UpgradeContext(bp, slot), false) == 64, "enabled: one pass refills");
    ctx.check(countIn(*bp, "minecraft:arrow") == 0, "arrows moved to the player");
    // No refill while a menu is open.
    bp->inventory().insert(data("minecraft:arrow", 64), kInternal);
    for (int i = 0; i < 8; ++i) clearSlot(player, i);
    ctx.require(ui::BackpackMenu::openBackpack(player, bp), "open menu");
    ctx.check(refillNow(player, UpgradeContext(bp, slot), false) == 0, "paused while a menu is open");
    ui::MenuManager::get().close(player);
});

ABP_REGISTER_SELFTEST("refill.advanced_targets", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(Tier::Gold);
    int   slot   = ctx.installUpgrade(bp, std::string(kAdvancedId));
    setFilter(
        UpgradeContext(bp, slot),
        kAdvancedSlots,
        {
            {"minecraft:torch",            static_cast<Target>(static_cast<int>(Target::Hotbar1) + 2)}, // hotbar 3
            {"minecraft:totem_of_undying", Target::Offhand                                           },
            {"minecraft:bread",            Target::MainHand                                          }
    }
    );
    bp->inventory().insert(data("minecraft:torch", 100), kInternal);
    bp->inventory().insert(data("minecraft:totem_of_undying", 1), kInternal);
    bp->inventory().insert(data("minecraft:totem_of_undying", 1), kInternal);
    bp->inventory().insert(data("minecraft:bread", 100), kInternal);
    player.setSelectedSlot(0);
    ctx.require(ctx.give(player, ctx.backpackItem(*bp), 8) == 8, "give backpack");
    ctx.require(ctx.give(player, game::ItemConv::make("minecraft:torch", 5), 5) == 5, "give torches");
    ctx.require(ctx.give(player, game::ItemConv::make("minecraft:bread", 1), 0) == 0, "bread in hand");
    co_await ctx.ticks(25);
    ctx.check(idAt(player, 2) == "minecraft:torch" && countAt(player, 2) == 64, "hotbar 3 filled with torches");
    ctx.check(countAt(player, 5) == 5, "other torch stacks are not the target");
    auto const& off = player.getOffhandSlot();
    ctx.check(!off.isNull() && off.getTypeName() == "minecraft:totem_of_undying", "totem in the offhand");
    ctx.check(countIn(*bp, "minecraft:totem_of_undying") == 1, "one totem left (max stack 1)");
    ctx.check(idAt(player, 0) == "minecraft:bread" && countAt(player, 0) == 64, "main hand bread topped up");
    // The bread runs out in the hand: refilled. Selecting another empty slot doesn't fill it.
    clearSlot(player, 0);
    co_await ctx.ticks(25);
    ctx.check(idAt(player, 0) == "minecraft:bread" && countAt(player, 0) == 37, "hand refilled after running out");
    player.setSelectedSlot(3);
    player.sendInventory(true);
    co_await ctx.ticks(25);
    ctx.check(idAt(player, 3).empty(), "an empty slot selected later is not filled");
    // A different item in the target slot is left alone.
    player.getInventory().setItem(2, game::ItemConv::make("minecraft:dirt", 1));
    player.sendInventory(false);
    co_await ctx.ticks(25);
    ctx.check(idAt(player, 2) == "minecraft:dirt", "occupied target slot untouched");
    ctx.check(countIn(*bp, "minecraft:torch") == 36, "no torches taken for an occupied slot");
});

ABP_REGISTER_SELFTEST("refill.pick_block", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    auto& inv    = player.getInventory();
    auto  bp     = ctx.createBackpack(Tier::Gold);
    int   slot   = ctx.installUpgrade(bp, std::string(kBasicId));
    bp->inventory().insert(data("minecraft:gold_block", 20), kInternal);
    ctx.require(ctx.give(player, ctx.backpackItem(*bp), 20) == 20, "give backpack");
    ctx.require(ctx.give(player, game::ItemConv::make("minecraft:stone", 1), 0) == 0, "stone in hand");
    player.setSelectedSlot(0);
    auto gold = data("minecraft:gold_block", 1);
    ctx.check(!pickFromBackpacks(player, gold), "basic refill doesn't pick blocks");

    (void)upgrade::remove(bp, slot, nullptr);
    ctx.installUpgrade(bp, std::string(kAdvancedId));
    ctx.check(pickFromBackpacks(player, gold), "advanced refill picks from the backpack");
    ctx.check(
        idAt(player, 1) == "minecraft:gold_block" && countAt(player, 1) == 20,
        "first free hotbar slot got the blocks"
    );
    ctx.check(player.getSelectedItemSlot() == 1, "and is selected");
    ctx.check(countIn(*bp, "minecraft:gold_block") == 0, "taken from the backpack");
    ctx.check(!pickFromBackpacks(player, gold), "already in the inventory: left to vanilla");

    // Full hotbar: the held item moves to the main inventory.
    bp->inventory().insert(data("minecraft:emerald_block", 3), kInternal);
    for (int i = 0; i < 9; ++i) inv.setItem(i, game::ItemConv::make("minecraft:dirt", 1 + i));
    player.setSelectedSlot(4);
    player.sendInventory(true);
    ctx.check(pickFromBackpacks(player, data("minecraft:emerald_block", 1)), "pick with a full hotbar");
    ctx.check(idAt(player, 4) == "minecraft:emerald_block" && countAt(player, 4) == 3, "picked into the selected slot");
    bool moved = false;
    for (int i = 9; i < 36; ++i) moved = moved || (idAt(player, i) == "minecraft:dirt" && countAt(player, i) == 5);
    ctx.check(moved, "held item moved to the main inventory");

    // The block-position path (BlockPickRequestPacket): a gold block placed next to the player.
    auto&    region = player.getDimensionBlockSource();
    BlockPos feet   = player.getFeetBlockPos();
    BlockPos pos(feet.x + 1, feet.y + 1, feet.z);
    auto     goldBlock = Block::tryGetFromRegistry(HashedString("minecraft:gold_block"));
    ctx.require(goldBlock.has_value(), "gold block registered");
    // Selftest players spawn above the build limit when the world has no spawn point yet: bring
    // the player into the world (the chunk may still be loading).
    if (feet.y > 300 || feet.y < -64) player.teleport(Vec3{0.5f, 0.0f, 0.5f}, player.getDimensionId());
    bool placed = false;
    for (int tries = 0; tries < 20 && !placed; ++tries) {
        co_await ctx.ticks(5);
        feet   = player.getFeetBlockPos();
        pos    = BlockPos(feet.x + 1, feet.y + 1, feet.z);
        placed = region.hasChunksAt(pos, 0, false) && region.setBlockAndRetainCompatibleBlockActor(pos, *goldBlock, 3);
    }
    ctx.require(
        placed,
        "place gold block at " + pos.toString() + " chunks=" + std::to_string(region.hasChunksAt(pos, 0, false))
            + " block=" + region.getBlock(pos).getTypeName()
    );
    Block const& previous = *Block::tryGetFromRegistry(HashedString("minecraft:air"));
    auto         wanted   = blockItem(player, pos, false);
    region.setBlockAndRetainCompatibleBlockActor(pos, previous, 3);
    ctx.require(!wanted.empty(), "pick item of block " + goldBlock->getTypeName());
    ctx.check(wanted.id == "minecraft:gold_block", "pick item id " + wanted.id);
    region.setBlockAndRetainCompatibleBlockActor(pos, *goldBlock, 3);
    std::string id = wanted.id;
    for (int i = 0; i < 36; ++i) {
        if (i != 20) inv.setItem(i, ItemStack::EMPTY_ITEM());
    }
    player.setSelectedSlot(0);
    player.sendInventory(true);
    bp->inventory().insert(game::ItemConv::toData(game::ItemConv::make(id, 7, wanted.aux)), kInternal);
    ctx.check(pickBlockAt(player, pos, false), "pick block at " + id);
    ctx.check(idAt(player, 0) == id && countAt(player, 0) == 7, "block item in hand");
    region.setBlockAndRetainCompatibleBlockActor(pos, previous, 3);
});

ABP_REGISTER_SELFTEST("refill.menu", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto&          mm     = ui::MenuManager::get();
    auto&          player = ctx.player();
    auto           bp     = ctx.createBackpack(Tier::Iron);
    int            slot   = ctx.installUpgrade(bp, std::string(kAdvancedId));
    UpgradeContext uc(bp, slot, &player);
    auto*          type = upgrade::byId(kAdvancedId);
    type->openSettings(player, uc);
    auto menu = mm.current(player);
    ctx.require(menu != nullptr && menu->size() == 54, "advanced settings menu is a double chest");
    mm.forceShown(player);
    ctx.require(ctx.give(player, game::ItemConv::make("minecraft:iron_ingot", 5), 4) == 4, "give");
    ctx.check(
        mm.simulate(
            player,
            {
                Action::Take,
                {Where::Inventory, 4},
                {Where::Cursor,    0},
                5
    }
        ),
        "to cursor"
    );
    (void)mm.simulate(
        player,
        {
            Action::Place,
            {Where::Cursor, 0},
            {Where::Menu,   0},
            5
    }
    );
    (void)mm.simulate(
        player,
        {
            Action::Place,
            {Where::Cursor, 0 },
            {Where::Menu,   21},
            5
    }
    ); // ghost 10
    auto s = load(uc, kAdvancedSlots);
    ctx.check(s.filter.ghosts[0].id == "minecraft:iron_ingot", "ghost 1 set");
    ctx.check(s.filter.ghosts[9].empty(), "duplicates refused");
    ctx.check(player.mPlayerUIContainer->getItem(0).mCount == 5, "ghosts don't consume items");
    (void)mm.simulate(
        player,
        {
            Action::Take,
            {Where::Menu,   9},
            {Where::Cursor, 0},
            1
    }
    ); // target button of ghost 1
    (void)mm.simulate(
        player,
        {
            Action::Take,
            {Where::Menu,   9},
            {Where::Cursor, 0},
            1
    }
    );
    ctx.check(
        load(uc, kAdvancedSlots).targets[0] == Target::Offhand,
        "target button cycles (any -> main hand -> offhand)"
    );
    (void)mm.simulate(
        player,
        {
            Action::Take,
            {Where::Menu,   48},
            {Where::Cursor, 0 },
            1
    }
    ); // NBT toggle
    ctx.check(load(uc, kAdvancedSlots).filter.matchNbt, "NBT toggle");
    co_await ctx.ticks(3);
    ctx.check(mm.shownItem(player, 0).getTypeName() == "minecraft:iron_ingot", "ghost displayed");
    ctx.check(mm.shownItem(player, 9).getTypeName() == "minecraft:shield", "offhand target icon");
    // Every target has a visible icon (an empty slot can't be clicked on a real client).
    for (int k = 0; k < kTargetCount; ++k) {
        // (No tick waits here: the selftest player is falling and would leave the fake chest.)
        (void)mm.simulate(
            player,
            {
                Action::Take,
                {Where::Menu,   9},
                {Where::Cursor, 0},
                1
        }
        );
        menu->ensureBuilt(player);
        auto t = load(uc, kAdvancedSlots).targets[0];
        ctx.check(
            menu->slot(9) && !menu->slot(9)->display(player).isNull(),
            "icon for target " + std::to_string(static_cast<int>(t))
        );
    }
    ctx.check(load(uc, kAdvancedSlots).targets[0] == Target::Offhand, "full cycle returns to offhand");
    (void)mm.simulate(
        player,
        {
            Action::Take,
            {Where::Menu,   52},
            {Where::Cursor, 0 },
            1
    }
    ); // clear
    ctx.check(load(uc, kAdvancedSlots).filter.isEmpty(), "clear");
    mm.close(player);
    ctx.check(player.mPlayerUIContainer->getItem(0).isNull(), "cursor returned");

    // Basic: single chest, no target buttons.
    auto bp2   = ctx.createBackpack(Tier::Leather);
    int  slot2 = ctx.installUpgrade(bp2, std::string(kBasicId));
    upgrade::byId(kBasicId)->openSettings(player, UpgradeContext(bp2, slot2, &player));
    auto basic = mm.current(player);
    ctx.check(basic != nullptr && basic->size() == 27, "basic settings menu is a single chest");
    mm.close(player);
});

} // namespace
} // namespace abp::upgrade::refill
