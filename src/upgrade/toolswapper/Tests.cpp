// Tool Swapper selftests: toolswapper.*  (`backpack selftest "toolswapper.*"`)

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"

#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "mod/Config.h"
#include "selftest/SelfTest.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/toolswapper/ToolSwapper.h"

namespace abp::upgrade::toolswapper {
namespace {

using selftest::Context;
using Where  = ui::MenuManager::SlotAddress::Where;
using Action = ui::MenuManager::ActionType;


ItemStack item(std::string_view id, int count = 1) { return game::ItemConv::make(id, count); }

Block const& block(Context& ctx, std::string_view name) {
    auto b = Block::tryGetFromRegistry(HashedString(std::string(name)));
    ctx.require(b.has_value(), std::string("block ") + std::string(name) + " exists");
    return *b;
}

std::string heldId(Player& p) {
    auto const& s = p.getInventory().getItem(0);
    return s.isNull() ? std::string("<empty>") : s.getTypeName();
}

void put(game::Backpack& bp, int slot, ItemStack const& stack) {
    bp.inventory().set(slot, game::ItemConv::toData(stack));
}

/// A backpack with a tool swapper, carried in inventory slot 8; the player's main hand is slot 0.
std::shared_ptr<game::Backpack> setup(Context& ctx, bool advanced = false, ItemStack const& held = {}) {
    auto& p  = ctx.player();
    auto  bp = ctx.createBackpack(core::Tier::Iron);
    ctx.installUpgrade(bp, std::string(advanced ? kAdvancedId : kBasicId));
    for (int i = 0; i < 36; ++i) p.getInventory().setItem(i, ItemStack::EMPTY_ITEM());
    ctx.require(ctx.give(p, ctx.backpackItem(*bp), 8) == 8, "give backpack");
    if (!held.isNull()) ctx.require(ctx.give(p, held, 0) == 0, "give held item");
    ctx.require(game::selectedSlot(p).slot == 0, "main hand is slot 0");
    return bp;
}

UpgradeContext upgradeCtx(std::shared_ptr<game::Backpack> const& bp) { return UpgradeContext(bp, 0); }

/// The first non-air block at or below the player's feet (air everywhere while the chunk is not loaded).
BlockPos groundBelow(Player& p) {
    auto        pos    = p.getFeetBlockPos();
    auto const& region = p.getDimensionBlockSourceConst();
    pos.y              = std::min(pos.y, 319);
    while (pos.y > -64 && region.getBlock(pos).isAir()) --pos.y;
    return pos;
}

/// Waits (up to 5 s) until the chunk under the player is loaded.
ll::coro::CoroTask<BlockPos> waitGround(Context& ctx, Player& p) {
    auto pos = groundBelow(p);
    for (int i = 0; i < 100 && p.getDimensionBlockSourceConst().getBlock(pos).isAir(); ++i) {
        co_await ctx.ticks(1);
        pos = groundBelow(p);
    }
    // Stand on it (the spawn point may be high up in the air): mobs far from players despawn.
    auto feet = p.getFeetBlockPos();
    if (feet.y > pos.y + 1) {
        ctx.info("moving the player from y=" + std::to_string(feet.y) + " to the ground at y=" + std::to_string(pos.y));
        p.teleportTo(
            Vec3{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y + 1), static_cast<float>(pos.z) + 0.5f},
            true,
            0,
            1,
            false
        );
        co_await ctx.ticks(2);
    }
    co_return pos;
}

ABP_REGISTER_SELFTEST("toolswapper.classify", [](Context& ctx) {
    auto pick  = item("minecraft:diamond_pickaxe");
    auto wpick = item("minecraft:wooden_pickaxe");
    auto sword = item("minecraft:diamond_sword");
    auto shov  = item("minecraft:diamond_shovel");
    auto stone = item("minecraft:cobblestone");
    ctx.check(isTool(pick) && isTool(shov) && isTool(item("minecraft:shears")), "pickaxe/shovel/shears are tools");
    ctx.check(isWeapon(sword) && isWeapon(item("minecraft:iron_axe")), "sword/axe are weapons");
    ctx.check(!isTool(stone) && !isWeapon(stone) && !isWeapon(pick), "cobblestone is no tool, pickaxe no weapon");
    auto const& stoneB = block(ctx, "minecraft:stone");
    auto const& dirtB  = block(ctx, "minecraft:dirt");
    auto const& obsB   = block(ctx, "minecraft:obsidian");
    ctx.info(
        "speeds stone: diamond_pickaxe=" + std::to_string(mineSpeed(pick, stoneB)) + " wooden_pickaxe="
        + std::to_string(mineSpeed(wpick, stoneB)) + " shovel=" + std::to_string(mineSpeed(shov, stoneB))
        + " hand=" + std::to_string(mineSpeed(ItemStack::EMPTY_ITEM(), stoneB))
    );
    ctx.check(mineSpeed(pick, stoneB) > mineSpeed(wpick, stoneB), "diamond pickaxe beats wooden on stone");
    ctx.check(mineSpeed(wpick, stoneB) > 1.0f, "wooden pickaxe works on stone");
    ctx.check(mineSpeed(stone, stoneB) <= 1.0f, "cobblestone is a bare hand");
    ctx.check(mineSpeed(shov, dirtB) > mineSpeed(pick, dirtB), "shovel beats pickaxe on dirt");
    ctx.check(canHarvest(pick, obsB), "diamond pickaxe harvests obsidian");
    ctx.check(!canHarvest(ItemStack::EMPTY_ITEM(), stoneB), "stone needs a pickaxe for drops");
    ctx.check(canHarvest(ItemStack::EMPTY_ITEM(), dirtB), "dirt drops by hand");
    ctx.info(
        "damage: sword=" + std::to_string(attackDamage(sword)) + " pick=" + std::to_string(attackDamage(pick))
        + " iron_sword=" + std::to_string(attackDamage(item("minecraft:iron_sword")))
    );
    ctx.check(attackDamage(sword) > attackDamage(item("minecraft:iron_sword")), "diamond sword beats iron sword");
    ctx.check(useToolFor(block(ctx, "minecraft:oak_log")) == "minecraft:is_axe", "logs need an axe");
    ctx.check(useToolFor(block(ctx, "minecraft:grass_block")) == "minecraft:is_hoe", "grass needs a hoe");
    ctx.check(useToolFor(block(ctx, "minecraft:podzol")) == "minecraft:is_shovel", "podzol needs a shovel");
    ctx.check(useToolFor(stoneB).empty(), "stone has no use tool");
});

ABP_REGISTER_SELFTEST("toolswapper.block_swap", [](Context& ctx) {
    auto& p  = ctx.player();
    auto  bp = setup(ctx, false, item("minecraft:cobblestone", 10));
    put(*bp, 3, item("minecraft:wooden_pickaxe"));
    put(*bp, 7, item("minecraft:diamond_pickaxe"));
    put(*bp, 9, item("minecraft:diamond_shovel"));
    ctx.require(swapForBlock(p, block(ctx, "minecraft:stone")), "swapped for stone");
    ctx.check(heldId(p) == "minecraft:diamond_pickaxe", "holding the diamond pickaxe, got " + heldId(p));
    ctx.check(
        bp->inventory().at(7).id == "minecraft:cobblestone" && bp->inventory().at(7).count == 10,
        "cobblestone went into the tool's slot"
    );
    ctx.check(!swapForBlock(p, block(ctx, "minecraft:stone")), "an effective tool is never swapped away");
    ctx.check(!swapForBlock(p, block(ctx, "minecraft:cobblestone")), "still effective on cobblestone");
    ctx.require(swapForBlock(p, block(ctx, "minecraft:dirt")), "swapped for dirt");
    ctx.check(heldId(p) == "minecraft:diamond_shovel", "holding the shovel, got " + heldId(p));
    ctx.check(bp->inventory().at(9).id == "minecraft:diamond_pickaxe", "pickaxe went into the shovel's slot");
    ctx.check(!swapForBlock(p, block(ctx, "minecraft:bedrock")), "nothing helps against bedrock");
});

ABP_REGISTER_SELFTEST("toolswapper.empty_hand_and_no_tool", [](Context& ctx) {
    auto& p  = ctx.player();
    auto  bp = setup(ctx);
    put(*bp, 0, item("minecraft:iron_pickaxe"));
    ctx.require(swapForBlock(p, block(ctx, "minecraft:stone")), "swap from an empty hand");
    ctx.check(heldId(p) == "minecraft:iron_pickaxe", "pickaxe in hand");
    ctx.check(bp->inventory().at(0).empty(), "slot emptied");
    // No suitable tool: nothing happens.
    ctx.check(!swapForBlock(p, block(ctx, "minecraft:dirt")), "no shovel, no swap");
    ctx.check(heldId(p) == "minecraft:iron_pickaxe", "pickaxe kept");
});

ABP_REGISTER_SELFTEST("toolswapper.attack_swap", [](Context& ctx) {
    auto& p  = ctx.player();
    auto  bp = setup(ctx, false, item("minecraft:bread", 5));
    put(*bp, 1, item("minecraft:iron_sword"));
    put(*bp, 2, item("minecraft:diamond_sword"));
    put(*bp, 4, item("minecraft:diamond_pickaxe"));
    ctx.require(swapForAttack(p), "swapped for attack");
    ctx.check(heldId(p) == "minecraft:diamond_sword", "best weapon in hand, got " + heldId(p));
    ctx.check(
        bp->inventory().at(2).id == "minecraft:bread" && bp->inventory().at(2).count == 5,
        "bread stored in the sword's slot"
    );
    ctx.check(!swapForAttack(p), "a held weapon stays");
});

ABP_REGISTER_SELFTEST("toolswapper.modes", [](Context& ctx) {
    auto& p  = ctx.player();
    auto  bp = setup(ctx, true, item("minecraft:cobblestone", 3));
    put(*bp, 0, item("minecraft:diamond_pickaxe"));
    put(*bp, 1, item("minecraft:diamond_sword"));
    auto uc = upgradeCtx(bp);
    auto s  = settingsOf(uc);
    s.mode  = ToolMode::OnlyTools;
    saveSettings(uc, s);
    ctx.check(!swapForBlock(p, block(ctx, "minecraft:stone")), "only-tools mode keeps a held block");
    ctx.check(swapForAttack(p), "only-tools mode is about blocks: weapons still swap");
    ctx.check(heldId(p) == "minecraft:diamond_sword", "sword in hand, got " + heldId(p));
    put(*bp, 1, item("minecraft:diamond_sword"));
    p.getInventory().setItem(0, item("minecraft:wooden_shovel"));
    ctx.check(swapForBlock(p, block(ctx, "minecraft:stone")), "only-tools mode swaps a held tool");
    ctx.check(heldId(p) == "minecraft:diamond_pickaxe", "pickaxe in hand");
    s.mode = ToolMode::Off;
    saveSettings(uc, s);
    p.getInventory().setItem(0, ItemStack::EMPTY_ITEM());
    ctx.check(!swapForBlock(p, block(ctx, "minecraft:dirt")), "tools off: no block swap");
    ctx.check(swapForAttack(p), "tools off: weapons still swap");
    ctx.check(heldId(p) == "minecraft:diamond_sword", "sword in hand");
    s.mode    = ToolMode::Any;
    s.weapons = false;
    saveSettings(uc, s);
    p.getInventory().setItem(0, ItemStack::EMPTY_ITEM());
    bp->inventory().set(5, game::ItemConv::toData(item("minecraft:iron_sword")));
    ctx.check(!swapForAttack(p), "weapons off: no attack swap");
    put(*bp, 6, item("minecraft:iron_pickaxe"));
    upgrade::setEnabled(bp, 0, false);
    ctx.check(!swapForBlock(p, block(ctx, "minecraft:stone")), "disabled upgrade does nothing");
});

ABP_REGISTER_SELFTEST("toolswapper.filter_and_durability", [](Context& ctx) {
    auto& p    = ctx.player();
    auto  bp   = setup(ctx, true);
    auto  worn = item("minecraft:diamond_pickaxe");
    worn.setDamageValue(static_cast<short>(worn.mItem.get()->getMaxDamage() - 2));
    put(*bp, 0, worn);
    put(*bp, 1, item("minecraft:iron_pickaxe"));
    put(*bp, 2, item("minecraft:netherite_pickaxe"));
    auto uc = upgradeCtx(bp);
    // Deny the netherite pickaxe.
    core::Filter f(kFilterSlots);
    f.mode = core::FilterMode::Deny;
    f.setGhost(0, game::ItemConv::toData(item("minecraft:netherite_pickaxe")));
    uc.settings()["filter"] = f.toJson();
    uc.saveSettings(true);
    ctx.require(swapForBlock(p, block(ctx, "minecraft:stone")), "swap");
    ctx.check(heldId(p) == "minecraft:iron_pickaxe", "filter + durability pick the iron pickaxe, got " + heldId(p));
    // Durability check off: the worn diamond pickaxe wins.
    auto s              = settingsOf(uc);
    s.skipLowDurability = false;
    saveSettings(uc, s);
    p.getInventory().setItem(0, ItemStack::EMPTY_ITEM());
    bp->inventory().set(1, {});
    ctx.require(swapForBlock(p, block(ctx, "minecraft:stone")), "swap 2");
    ctx.check(heldId(p) == "minecraft:diamond_pickaxe", "worn diamond pickaxe allowed now, got " + heldId(p));
});

ABP_REGISTER_SELFTEST("toolswapper.no_loss_when_full", [](Context& ctx) {
    auto& p     = ctx.player();
    auto  other = ctx.createBackpack(core::Tier::Leather);
    // Holding another backpack: it can't go into a backpack, so nothing may change.
    auto bp = setup(ctx, false, ctx.backpackItem(*other));
    put(*bp, 4, item("minecraft:diamond_pickaxe"));
    ctx.check(!swapForBlock(p, block(ctx, "minecraft:stone")), "no swap when the held item can't be stored");
    ctx.check(bp->inventory().at(4).id == "minecraft:diamond_pickaxe", "tool stays in its slot");
    ctx.check(game::isBackpack(p.getInventory().getItem(0)), "held backpack untouched");
});

/// Voids every cobblestone put into the backpack (stands in for a Void upgrade).
class VoidCobbleUpgrade : public IUpgrade {
public:
    UpgradeInfo mInfo{
        .id           = "selftest_void_cobble",
        .itemId       = "minecraft:structure_void",
        .nameKey      = "abp.selftest.upgrade",
        .nameFallback = "Selftest Upgrade",
    };
    [[nodiscard]] UpgradeInfo const& info() const override { return mInfo; }
    void                             attach(UpgradeContext const& ctx, core::BackpackInventory& inv) override {
        inv.addInsertFilter(ctx.hookOwner(), 0, [](core::InsertQuery const& q) {
            return q.stack.id == "minecraft:cobblestone" ? core::InsertVerdict::Void : core::InsertVerdict::Allow;
        });
    }
};

ABP_REGISTER_SELFTEST("toolswapper.never_voids_held_item", [](Context& ctx) {
    ScopedUpgrade scoped(std::make_shared<VoidCobbleUpgrade>());
    auto&         p  = ctx.player();
    auto          bp = setup(ctx, false, item("minecraft:cobblestone", 32));
    ctx.installUpgrade(bp, "selftest_void_cobble");
    put(*bp, 4, item("minecraft:diamond_pickaxe"));
    ctx.check(!swapForBlock(p, block(ctx, "minecraft:stone")), "no swap when the held item would be voided");
    ctx.check(heldId(p) == "minecraft:cobblestone" && p.getInventory().getItem(0).mCount == 32, "cobblestone kept");
    ctx.check(bp->inventory().at(4).id == "minecraft:diamond_pickaxe", "pickaxe back in its slot");
});

ABP_REGISTER_SELFTEST("toolswapper.hook_start_destroy", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p  = ctx.player();
    auto  bp = setup(ctx, false, item("minecraft:stick"));
    put(*bp, 6, item("minecraft:diamond_shovel"));
    auto        pos   = co_await waitGround(ctx, p);
    auto const& below = p.getDimensionBlockSourceConst().getBlock(pos);
    ctx.require(!below.isAir(), "found the ground");
    ctx.info("block below: " + below.getTypeName());
    auto* gm = p.mGameMode.get();
    ctx.require(gm != nullptr, "game mode");
    bool destroyed = false;
    gm->startDestroyBlock(pos, 1, destroyed);
    co_await ctx.ticks(2); // served on the next tick
    gm->stopDestroyBlock(pos);
    ctx.check(heldId(p) == "minecraft:diamond_shovel", "startDestroyBlock swapped in the shovel, got " + heldId(p));
    ctx.check(bp->inventory().at(6).id == "minecraft:stick", "stick stored");
});

Mob* spawnSheep(Context& ctx, Player& p, BlockPos const& ground) {
    auto pos  = p.getPosition();
    pos.x    += 1.5f;
    pos.y     = static_cast<float>(ground.y + 1);
    Mob* mob  = nullptr;
    for (bool surface : {false, true}) {
        mob = p.getLevel().getSpawner().spawnMob(
            p.getDimensionBlockSource(),
            ActorDefinitionIdentifier("minecraft:sheep"),
            nullptr,
            pos,
            false,
            surface,
            false
        );
        if (mob) break;
    }
    ctx.require(mob != nullptr, "spawn sheep");
    return mob;
}

ABP_REGISTER_SELFTEST("toolswapper.hook_attack", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p  = ctx.player();
    auto  bp = setup(ctx);
    put(*bp, 2, item("minecraft:stone_sword"));
    auto  ground = co_await waitGround(ctx, p);
    auto* sheep  = spawnSheep(ctx, p, ground);
    auto  id     = sheep->getOrCreateUniqueID();
    co_await ctx.ticks(2);
    auto* s = p.getLevel().fetchEntity(id, false);
    ctx.require(s != nullptr, "sheep present");
    p.mGameMode.get()->attack(*s, s->getPosition());
    // The swap is served at the start of the next tick.
    co_await ctx.ticks(2);
    ctx.check(heldId(p) == "minecraft:stone_sword", "attack swapped in the sword, got " + heldId(p));
    ctx.check(bp->inventory().at(2).empty(), "sword left its slot (empty hand: nothing to store)");
    if (auto* again = p.getLevel().fetchEntity(id, false)) again->despawn();
});

ABP_REGISTER_SELFTEST("toolswapper.shears_and_use", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p  = ctx.player();
    auto  bp = setup(ctx);
    put(*bp, 0, item("minecraft:shears"));
    put(*bp, 1, item("minecraft:stone_axe"));
    put(*bp, 2, item("minecraft:diamond_axe"));
    put(*bp, 3, item("minecraft:iron_hoe"));
    auto  ground = co_await waitGround(ctx, p);
    auto* sheep  = spawnSheep(ctx, p, ground);
    auto  id     = sheep->getOrCreateUniqueID();
    co_await ctx.ticks(2);
    auto* s = p.getLevel().fetchEntity(id, false);
    ctx.require(s != nullptr, "sheep present");
    ctx.check(isShearable(*s), "sheep is shearable");
    // Through the interaction hook: the swap is served on the next tick.
    p.mGameMode.get()->interact(*s, s->getPosition(), HandSlot::Mainhand);
    co_await ctx.ticks(2);
    ctx.check(heldId(p) == "minecraft:shears", "shears in hand, got " + heldId(p));
    if (auto* again = p.getLevel().fetchEntity(id, false)) again->despawn();
    // Holding something already: no interaction swap.
    ctx.check(!swapForBlockUse(p, block(ctx, "minecraft:oak_log")), "no use-swap with a full hand");
    p.getInventory().setItem(0, ItemStack::EMPTY_ITEM());
    ctx.check(swapForBlockUse(p, block(ctx, "minecraft:oak_log")), "log -> axe");
    ctx.check(heldId(p) == "minecraft:diamond_axe", "best axe in hand, got " + heldId(p));
    // Through the use-on-block hook: only while sneaking with an empty hand.
    p.getInventory().setItem(0, ItemStack::EMPTY_ITEM());
    auto const& groundBlock = p.getDimensionBlockSourceConst().getBlock(ground);
    ctx.require(
        groundBlock.getTypeName() == "minecraft:grass_block",
        "standing on grass, not " + groundBlock.getTypeName()
    );
    Vec3 hit{
        static_cast<float>(ground.x) + 0.5f,
        static_cast<float>(ground.y + 1),
        static_cast<float>(ground.z) + 0.5f
    };
    ItemStack empty;
    p.setSneaking(false);
    (void)p.mGameMode.get()->useItemOn(empty, ground, 1, hit, HandSlot::Mainhand, &groundBlock, true);
    co_await ctx.ticks(2);
    ctx.check(heldId(p) == "<empty>", "no swap without sneaking, got " + heldId(p));
    p.setSneaking(true);
    (void)p.mGameMode.get()->useItemOn(empty, ground, 1, hit, HandSlot::Mainhand, &groundBlock, true);
    co_await ctx.ticks(2);
    p.setSneaking(false);
    ctx.check(heldId(p) == "minecraft:iron_hoe", "sneak-use on grass -> hoe, got " + heldId(p));
});

ABP_REGISTER_SELFTEST("toolswapper.settings_menu", [](Context& ctx) {
    auto& mm   = ui::MenuManager::get();
    auto& p    = ctx.player();
    auto  bp   = setup(ctx, true);
    auto  uc   = upgradeCtx(bp);
    auto* type = upgrade::upgradeFor(uc.state().item);
    ctx.require(type && type->info().hasSettings, "advanced upgrade has settings");
    auto menu = type->createSettingsMenu(p, uc.withPlayer(&p));
    ctx.require(menu != nullptr, "settings menu");
    ctx.require(mm.open(p, menu), "open");
    mm.forceShown(p);
    (void)mm.simulate(
        p,
        {
            Action::Take,
            {Where::Menu,   10},
            {Where::Cursor, 0 },
            1
    }
    );
    ctx.check(!settingsOf(uc).weapons, "weapons toggled off");
    (void)mm.simulate(
        p,
        {
            Action::Take,
            {Where::Menu,   11},
            {Where::Cursor, 0 },
            1
    }
    );
    ctx.check(settingsOf(uc).mode == ToolMode::OnlyTools, "mode cycled to only-tools");
    (void)mm.simulate(
        p,
        {
            Action::Take,
            {Where::Menu,   12},
            {Where::Cursor, 0 },
            1
    }
    );
    ctx.check(
        settingsOf(uc).skipLowDurability != config::get<ToolSwapperConfig>().skipLowDurabilityByDefault,
        "durability toggled"
    );
    (void)mm.simulate(
        p,
        {
            Action::Take,
            {Where::Menu,   13},
            {Where::Cursor, 0 },
            1
    }
    );
    ctx.check(!settingsOf(uc).interact, "interaction swaps off");
    ctx.check(p.mPlayerUIContainer->getItem(0).isNull(), "buttons never give items");
    mm.close(p);
    // Settings travel with the item.
    auto removed = upgrade::remove(bp, 0, nullptr);
    ctx.require(removed.has_value(), "remove");
    ctx.require(upgrade::install(bp, 1, *removed, nullptr), "reinstall");
    ctx.check(!settingsOf(UpgradeContext(bp, 1)).weapons, "settings kept after reinstall");
});

} // namespace
} // namespace abp::upgrade::toolswapper
