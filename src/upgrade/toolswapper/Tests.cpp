// Tool Swapper selftests: toolswapper.*  (`backpack selftest "toolswapper.*"`)

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorFlags.h"
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
#include "mc/deps/core/math/Vec2.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/block/Block.h"

#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "mod/Config.h"
#include "net/NativeScreens.h"
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


/// Voids every wooden pickaxe put into the backpack (stands in for a Void upgrade).
class VoidPickaxeUpgrade : public IUpgrade {
public:
    UpgradeInfo mInfo{
        .id           = "selftest_void_pickaxe",
        .itemId       = "minecraft:structure_void",
        .nameKey      = "abp.selftest.upgrade",
        .nameFallback = "Selftest Upgrade",
    };
    [[nodiscard]] UpgradeInfo const& info() const override { return mInfo; }
    void                             attach(UpgradeContext const& ctx, core::BackpackInventory& inv) override {
        inv.addInsertFilter(ctx.hookOwner(), 0, [](core::InsertQuery const& q) {
            return q.stack.id == "minecraft:wooden_pickaxe" ? core::InsertVerdict::Void : core::InsertVerdict::Allow;
        });
    }
};

Mob* spawnMob(Context& ctx, Player& p, BlockPos const& ground, std::string const& id) {
    auto pos  = p.getPosition();
    pos.x    += 1.5f;
    pos.y     = static_cast<float>(ground.y + 1);
    Mob* mob  = nullptr;
    for (bool surface : {false, true}) {
        mob = p.getLevel().getSpawner().spawnMob(p.getDimensionBlockSource(), ActorDefinitionIdentifier(id), nullptr, pos, false, surface, false);
        if (mob) break;
    }
    ctx.require(mob != nullptr, "spawn " + id);
    return mob;
}

void lookDown(Player& p, BlockPos const& ground) {
    p.teleport(Vec3{static_cast<float>(ground.x) + 0.5f, static_cast<float>(ground.y + 1), static_cast<float>(ground.z) + 0.5f}, p.getDimensionId(), Vec2{90.0f, 0.0f});
}

// ---- registration / settings -----------------------------------------------------------------------

ABP_REGISTER_SELFTEST("toolswapper.items", [](Context& ctx) {
    auto* basic = upgrade::byId(kBasicId);
    auto* adv   = upgrade::byId(kAdvancedId);
    ctx.require(basic && adv, "registered");
    ctx.check(!basic->info().hasSettings, "SB: the basic tool swapper has no settings tab");
    ctx.check(adv->info().hasSettings, "advanced has a tab");
    auto bp = ctx.createBackpack(core::Tier::Iron);
    ctx.installUpgrade(bp, std::string(kBasicId));
    ctx.check(!upgrade::canInstall(bp, 1, game::ItemConv::toData(item("alleviatedbackpacks:advanced_tool_swapper_upgrade")), nullptr), "one tool swapper per backpack");
    auto s = settingsOf(UpgradeContext(bp, 0));
    ctx.check(s.swapWeapon && s.mode == ToolMode::Any, "SB defaults: Swap Weapon, Swap Tools");
    ctx.check(s.filter.ghosts.size() == 8 && s.filter.mode == core::FilterMode::Deny, "8 slot filter, empty deny list");
    ctx.check(toString(ToolMode::Any) == "name" && toolModeFromString("onlyTools") == ToolMode::OnlyTools && toolModeFromString("noSwap") == ToolMode::Off, "SB mode names");
});

ABP_REGISTER_SELFTEST("toolswapper.classify", [](Context& ctx) {
    ctx.check(isSword(item("minecraft:diamond_sword")) && !isSword(item("minecraft:diamond_axe")), "swords");
    ctx.check(isTool(item("minecraft:iron_pickaxe")) && isTool(item("minecraft:shears")) && isTool(item("minecraft:stone_hoe")), "tools");
    ctx.check(!isTool(item("minecraft:stick")) && !isTool(item("minecraft:diamond_sword")), "sticks and swords are no tools");
    ctx.check(isAxe(item("minecraft:golden_axe")), "axes");
    auto const& stone = block(ctx, "minecraft:stone");
    ctx.check(goodAtBreaking(item("minecraft:iron_pickaxe"), stone), "a pickaxe is good at stone");
    ctx.check(!goodAtBreaking(item("minecraft:iron_shovel"), stone), "a shovel is not");
    ctx.check(!goodAtBreaking(ItemStack{}, stone), "a hand is not");
    ctx.check(destroyProgress(ItemStack{}, block(ctx, "minecraft:short_grass")) >= 1.0f || destroyProgress(ItemStack{}, block(ctx, "minecraft:tallgrass")) >= 1.0f, "grass breaks instantly");
    ctx.check(destroySpeed(item("minecraft:diamond_pickaxe"), stone) > destroySpeed(item("minecraft:stone_pickaxe"), stone), "diamond is faster than stone");
    ctx.check(attackDamage(item("minecraft:diamond_sword")) > attackDamage(item("minecraft:wooden_sword")), "attack damage");
});

// ---- block click (SB onBlockClick) -----------------------------------------------------------------

ABP_REGISTER_SELFTEST("toolswapper.block_swap", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p      = ctx.player();
    auto  bp     = setup(ctx, false, item("minecraft:wooden_pickaxe"));
    auto  ground = co_await waitGround(ctx, p);
    auto  name   = p.getDimensionBlockSourceConst().getBlock(ground).getTypeName();
    ctx.info("ground: " + name);
    put(*bp, 3, item("minecraft:stone_shovel"));
    put(*bp, 5, item("minecraft:diamond_shovel"));
    ctx.check(onBlockClick(p, ground), "handled");
    ctx.check(heldId(p) == "minecraft:diamond_shovel", "the fastest tool for " + name + ", got " + heldId(p));
    ctx.check(bp->inventory().count([](auto const& d) { return d.id == "minecraft:wooden_pickaxe"; }) == 1, "the pickaxe went into the backpack");
    // The held tool is already good at this block: nothing changes.
    ctx.check(onBlockClick(p, ground) && heldId(p) == "minecraft:diamond_shovel", "kept");

    // SB: only when holding a tool or a weapon.
    auto bp2 = setup(ctx, false, item("minecraft:dirt", 5));
    put(*bp2, 1, item("minecraft:diamond_shovel"));
    ctx.check(!onBlockClick(p, ground) && heldId(p) == "minecraft:dirt", "holding a block: no swap");
    auto bp3 = setup(ctx, false);
    put(*bp3, 1, item("minecraft:diamond_shovel"));
    ctx.check(!onBlockClick(p, ground) && heldId(p) == "<empty>", "empty hand: no swap");
    // Creative: never.
    auto bp4 = setup(ctx, false, item("minecraft:wooden_pickaxe"));
    put(*bp4, 1, item("minecraft:diamond_shovel"));
    p.setPlayerGameType(GameType::Creative);
    ctx.check(!onBlockClick(p, ground) && heldId(p) == "minecraft:wooden_pickaxe", "creative: no swap");
    p.setPlayerGameType(GameType::Survival);
});

ABP_REGISTER_SELFTEST("toolswapper.modes_and_filter", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p      = ctx.player();
    auto  ground = co_await waitGround(ctx, p);
    // Swap Tools: a held sword is swapped too.
    auto bp = setup(ctx, true, item("minecraft:iron_sword"));
    put(*bp, 1, item("minecraft:diamond_shovel"));
    ctx.check(onBlockClick(p, ground) && heldId(p) == "minecraft:diamond_shovel", "Swap Tools: sword -> shovel");
    // Only Swap For Other Tools: not for a weapon.
    bp = setup(ctx, true, item("minecraft:iron_sword"));
    put(*bp, 1, item("minecraft:diamond_shovel"));
    setToolMode(upgradeCtx(bp), ToolMode::OnlyTools);
    ctx.check(!onBlockClick(p, ground) && heldId(p) == "minecraft:iron_sword", "Only Swap For Other Tools: keeps the sword");
    auto bpTools = setup(ctx, true, item("minecraft:wooden_pickaxe"));
    put(*bpTools, 1, item("minecraft:diamond_shovel"));
    setToolMode(upgradeCtx(bpTools), ToolMode::OnlyTools);
    ctx.check(onBlockClick(p, ground) && heldId(p) == "minecraft:diamond_shovel", "Only Swap For Other Tools: pickaxe -> shovel");
    // Do Not Swap Tools.
    bp = setup(ctx, true, item("minecraft:wooden_pickaxe"));
    put(*bp, 1, item("minecraft:diamond_shovel"));
    setToolMode(upgradeCtx(bp), ToolMode::Off);
    ctx.check(!onBlockClick(p, ground) && heldId(p) == "minecraft:wooden_pickaxe", "Do Not Swap Tools");
    // Filter (deny list): a denied tool is never taken.
    bp = setup(ctx, true, item("minecraft:wooden_pickaxe"));
    put(*bp, 1, item("minecraft:diamond_shovel"));
    put(*bp, 2, item("minecraft:stone_shovel"));
    auto f = Settings::defaultFilter();
    f.setGhost(0, game::ItemConv::toData(item("minecraft:diamond_shovel")));
    setFilter(upgradeCtx(bp), f);
    ctx.check(onBlockClick(p, ground) && heldId(p) == "minecraft:stone_shovel", "denied diamond shovel skipped, got " + heldId(p));
    // Allow list: only the listed tools (the held one must pass too).
    bp = setup(ctx, true, item("minecraft:wooden_pickaxe"));
    put(*bp, 1, item("minecraft:diamond_shovel"));
    f      = Settings::defaultFilter();
    f.mode = core::FilterMode::Allow;
    setFilter(upgradeCtx(bp), f);
    ctx.check(!onBlockClick(p, ground) && heldId(p) == "minecraft:wooden_pickaxe", "empty allow list: nothing passes");
});

ABP_REGISTER_SELFTEST("toolswapper.never_voids_held_item", [](Context& ctx) -> ll::coro::CoroTask<> {
    ScopedUpgrade scoped(std::make_shared<VoidPickaxeUpgrade>());
    auto&         p      = ctx.player();
    auto          ground = co_await waitGround(ctx, p);
    auto          bp     = setup(ctx, false, item("minecraft:wooden_pickaxe"));
    ctx.installUpgrade(bp, "selftest_void_pickaxe");
    put(*bp, 4, item("minecraft:diamond_shovel"));
    (void)onBlockClick(p, ground);
    ctx.check(heldId(p) == "minecraft:wooden_pickaxe", "no swap when the held item would be voided");
    ctx.check(bp->inventory().at(4).id == "minecraft:diamond_shovel", "shovel back in its slot");
});

// ---- attack (SB onAttackEntity) ------------------------------------------------------------------

ABP_REGISTER_SELFTEST("toolswapper.attack_swap", [](Context& ctx) {
    auto& p  = ctx.player();
    auto  bp = setup(ctx, false, item("minecraft:stone_pickaxe"));
    put(*bp, 1, item("minecraft:diamond_axe"));
    put(*bp, 2, item("minecraft:stone_sword"));
    put(*bp, 3, item("minecraft:iron_sword"));
    ctx.check(onAttack(p) && heldId(p) == "minecraft:iron_sword", "the best sword wins over an axe, got " + heldId(p));
    ctx.check(onAttack(p) && heldId(p) == "minecraft:iron_sword", "holding a sword: kept");
    bp = setup(ctx, false, item("minecraft:stone_pickaxe"));
    put(*bp, 1, item("minecraft:diamond_axe"));
    ctx.check(onAttack(p) && heldId(p) == "minecraft:diamond_axe", "no sword: the best axe");
    bp = setup(ctx, false);
    put(*bp, 1, item("minecraft:iron_sword"));
    ctx.check(!onAttack(p) && heldId(p) == "<empty>", "SB: empty hand is no tool: no swap");
    bp = setup(ctx, true, item("minecraft:stone_pickaxe"));
    put(*bp, 1, item("minecraft:iron_sword"));
    setSwapWeapon(upgradeCtx(bp), false);
    ctx.check(!onAttack(p) && heldId(p) == "minecraft:stone_pickaxe", "Do Not Swap Weapon");
});

ABP_REGISTER_SELFTEST("toolswapper.hook_start_destroy", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p  = ctx.player();
    auto  bp = setup(ctx, false, item("minecraft:wooden_pickaxe"));
    put(*bp, 6, item("minecraft:diamond_shovel"));
    auto pos = co_await waitGround(ctx, p);
    ctx.require(!p.getDimensionBlockSourceConst().getBlock(pos).isAir(), "found the ground");
    auto* gm = p.mGameMode.get();
    ctx.require(gm != nullptr, "game mode");
    bool destroyed = false;
    gm->startDestroyBlock(pos, 1, destroyed);
    co_await ctx.ticks(2); // served on the next tick
    gm->stopDestroyBlock(pos);
    ctx.check(heldId(p) == "minecraft:diamond_shovel", "startDestroyBlock swapped in the shovel, got " + heldId(p));
});

ABP_REGISTER_SELFTEST("toolswapper.hook_attack", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p  = ctx.player();
    auto  bp = setup(ctx, false, item("minecraft:wooden_pickaxe"));
    put(*bp, 2, item("minecraft:stone_sword"));
    auto  ground = co_await waitGround(ctx, p);
    auto* sheep  = spawnMob(ctx, p, ground, "minecraft:sheep");
    auto  id     = sheep->getOrCreateUniqueID();
    co_await ctx.ticks(2);
    auto* s = p.getLevel().fetchEntity(id, false);
    ctx.require(s != nullptr, "sheep present");
    p.mGameMode.get()->attack(*s, s->getPosition());
    co_await ctx.ticks(2); // served at the start of the next tick
    ctx.check(heldId(p) == "minecraft:stone_sword", "attack swapped in the sword, got " + heldId(p));
    if (auto* again = p.getLevel().fetchEntity(id, false)) again->despawn();
});

// ---- tool swap key (SB BlockToolSwapPayload / EntityToolSwapPayload) ----------------------------------

ABP_REGISTER_SELFTEST("toolswapper.key_block", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p      = ctx.player();
    auto  ground = co_await waitGround(ctx, p);
    auto  name   = p.getDimensionBlockSourceConst().getBlock(ground).getTypeName();
    lookDown(p, ground);
    co_await ctx.ticks(2);
    // Basic: no key swap.
    auto bp = setup(ctx, false, item("minecraft:stick"));
    put(*bp, 1, item("minecraft:wooden_shovel"));
    ctx.check(onSwapKey(p) == KeyResult::NoUpgrade, "basic tool swapper can't swap on key press");
    // Advanced: the shovel flattens the grass under the player; repeated presses cycle tools.
    bp = setup(ctx, true, item("minecraft:stick"));
    lookDown(p, ground);
    co_await ctx.ticks(2);
    if (name != "minecraft:grass_block" && name != "minecraft:dirt") {
        ctx.info("ground is " + name + ": flatten check skipped");
        co_return;
    }
    put(*bp, 1, item("minecraft:wooden_shovel"));
    put(*bp, 2, item("minecraft:stone_shovel"));
    put(*bp, 3, item("minecraft:diamond_pickaxe"));
    ctx.check(onSwapKey(p) == KeyResult::Swapped && heldId(p) == "minecraft:wooden_shovel", "first press: a shovel, got " + heldId(p));
    ctx.check(onSwapKey(p) == KeyResult::Swapped && heldId(p) == "minecraft:stone_shovel", "second press: the other shovel, got " + heldId(p));
    ctx.check(onSwapKey(p) == KeyResult::Swapped && heldId(p) == "minecraft:wooden_shovel", "third press: back to the first, got " + heldId(p));
    // No tool for the block.
    bp = setup(ctx, true, item("minecraft:stick"));
    lookDown(p, ground);
    put(*bp, 1, item("minecraft:diamond_pickaxe"));
    ctx.check(onSwapKey(p) == KeyResult::NoToolForBlock, "no tool works on the block");
    // Holding a backpack.
    auto other = ctx.createBackpack(core::Tier::Leather);
    bp         = setup(ctx, true, ctx.backpackItem(*other));
    ctx.check(onSwapKey(p) == KeyResult::HoldingBackpack, "holding a backpack");
});

ABP_REGISTER_SELFTEST("toolswapper.key_entity_tools", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p      = ctx.player();
    auto  ground = co_await waitGround(ctx, p);
    auto* sheep  = spawnMob(ctx, p, ground, "minecraft:sheep");
    auto* cow    = spawnMob(ctx, p, ground, "minecraft:cow");
    auto  sid    = sheep->getOrCreateUniqueID();
    auto  cid    = cow->getOrCreateUniqueID();
    co_await ctx.ticks(2);
    auto* s = p.getLevel().fetchEntity(sid, false);
    auto* c = p.getLevel().fetchEntity(cid, false);
    ctx.require(s && c, "mobs present");
    if (s->isBaby() || s->getStatusFlag(ActorFlags::Sheared)) {
        ctx.info("the spawned sheep is a lamb / sheared: shears check skipped");
    } else {
        ctx.check(worksOnEntity(item("minecraft:shears"), *s), "shears on an adult unsheared sheep (type " + s->getTypeName() + ")");
    }
    ctx.check(worksOnEntity(item("minecraft:lead"), *s), "lead on an animal");
    ctx.check(worksOnEntity(item("minecraft:bucket"), *c), "bucket on a cow");
    ctx.check(!worksOnEntity(item("minecraft:bucket"), *s), "no bucket on a sheep");
    ctx.check(!worksOnEntity(item("minecraft:diamond_pickaxe"), *c), "a pickaxe does nothing to a cow");
    ctx.check(worksOnBlock(item("minecraft:iron_axe"), block(ctx, "minecraft:oak_log"), p, BlockPos{0, 400, 0}), "axe strips logs");
    ctx.check(!worksOnBlock(item("minecraft:iron_axe"), block(ctx, "minecraft:stripped_oak_log"), p, BlockPos{0, 400, 0}), "not stripped logs");
    ctx.check(worksOnBlock(item("minecraft:shears"), block(ctx, "minecraft:pumpkin"), p, BlockPos{0, 400, 0}), "shears carve pumpkins");
    ctx.check(worksOnBlock(item("minecraft:iron_axe"), block(ctx, "minecraft:waxed_copper"), p, BlockPos{0, 400, 0}), "axe un-waxes copper");
    if (auto* a = p.getLevel().fetchEntity(sid, false)) a->despawn();
    if (auto* a = p.getLevel().fetchEntity(cid, false)) a->despawn();
});

// ---- settings: chest menu (selftests) and native tab actions ------------------------------------------

ABP_REGISTER_SELFTEST("toolswapper.settings", [](Context& ctx) {
    auto& mm   = ui::MenuManager::get();
    auto& p    = ctx.player();
    auto  bp   = setup(ctx, true);
    auto  uc   = upgradeCtx(bp);
    auto* type = upgrade::upgradeFor(uc.state().item);
    auto  menu = type->createSettingsMenu(p, uc.withPlayer(&p));
    ctx.require(menu != nullptr && mm.open(p, menu), "open settings menu");
    mm.forceShown(p);
    (void)mm.simulate(p, {Action::Take, {Where::Menu, 11}, {Where::Cursor, 0}, 1});
    ctx.check(!settingsOf(uc).swapWeapon, "Do Not Swap Weapon");
    (void)mm.simulate(p, {Action::Take, {Where::Menu, 13}, {Where::Cursor, 0}, 1});
    ctx.check(settingsOf(uc).mode == ToolMode::OnlyTools, "Only Swap For Other Tools");
    mm.close(p);
    auto act = [&](nlohmann::json d) {
        for (auto const& h : net::TabActionRegistry::entries())
            if (h.upgrade == kAdvancedId) return h.handle(p, uc.withPlayer(&p), d);
        return false;
    };
    ctx.check(act({{"toolswapper", "mode"}}) && settingsOf(uc).mode == ToolMode::Off, "tab: Do Not Swap Tools");
    ctx.check(act({{"toolswapper", "mode"}}) && settingsOf(uc).mode == ToolMode::Any, "tab: Swap Tools");
    ctx.check(act({{"toolswapper", "weapon"}}) && settingsOf(uc).swapWeapon, "tab: Swap Weapon");
    ctx.check(act({{"toolswapper", "allow"}}) && settingsOf(uc).filter.mode == core::FilterMode::Allow, "tab: Allow");
    ctx.check(act({{"toolswapper", "primary"}}) && settingsOf(uc).filter.matchBy == core::MatchBy::Namespace, "tab: Match Mod");
    p.mPlayerUIContainer->setItem(0, item("minecraft:diamond_pickaxe"));
    ctx.check(act({{"toolswapper", "slot"}, {"index", 2}}) && settingsOf(uc).filter.ghosts[2].id == "minecraft:diamond_pickaxe", "tab: filter slot from the cursor");
    ctx.check(p.mPlayerUIContainer->getItem(0).mCount == 1, "cursor kept");
    p.mPlayerUIContainer->setItem(0, ItemStack::EMPTY_ITEM());
    ctx.check(act({{"toolswapper", "slot"}, {"index", 2}}) && settingsOf(uc).filter.ghosts[2].empty(), "tab: cleared with an empty cursor");
    nlohmann::json data;
    for (auto const& pr : net::TabDataRegistry::entries())
        if (pr.upgrade == kAdvancedId) data = pr.data(p, uc);
    ctx.check(data.value("title", std::string{}) == "Tool Swap", "tab title");
    ctx.check(data["toolswapper"]["filter"]["ghosts"].size() == 8, "8 filter slots in the tab data");
    // Settings travel with the item.
    auto removed = upgrade::remove(bp, 0, nullptr);
    ctx.require(removed.has_value(), "remove");
    ctx.require(upgrade::install(bp, 1, *removed, nullptr), "reinstall");
    ctx.check(settingsOf(UpgradeContext(bp, 1)).filter.mode == core::FilterMode::Allow, "settings kept after reinstall");
});

} // namespace
} // namespace abp::upgrade::toolswapper
