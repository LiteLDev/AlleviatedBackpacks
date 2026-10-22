// Selftests: inception.*

#include <algorithm>

#include <cmath>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"

#include "api/BackpackCapabilities.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "game/Storage.h"
#include "mod/Config.h"
#include "mod/GeneralConfig.h"
#include "net/NativeScreens.h"
#include "selftest/SelfTest.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/inception/Inception.h"

namespace abp::upgrade::inception {
namespace {

using core::Tier;
using selftest::Context;
using Where  = ui::MenuManager::SlotAddress::Where;
using Action = ui::MenuManager::ActionType;

core::AccessContext const kPlayer{core::Source::Player};

core::ItemStackData data(std::string_view id, int count) {
    return game::ItemConv::toData(game::ItemConv::make(id, count));
}

/// Puts backpack `inner` into `outer` (through the filters). Returns the slot or -1.
int store(Context& ctx, game::Backpack& outer, game::Backpack const& inner) {
    auto item = game::ItemConv::toData(ctx.backpackItem(inner));
    if (outer.inventory().insert(item, kPlayer).inserted != 1) return -1;
    auto const& inv = outer.inventory();
    for (int i = 0; i < inv.size(); ++i) {
        if (!inv.at(i).empty() && game::backpackUuid(inv.at(i)) == inner.uuid()) return i;
    }
    return -1;
}

/// Restores a config value when the test ends (even when it aborts).
template <class T>
struct ConfigGuard {
    T saved = config::get<T>();
    ~ConfigGuard() { config::mut<T>() = saved; }
};

class TickUpgrade : public IUpgrade {
public:
    UpgradeInfo mInfo{
        .id           = "selftest_inception_tick",
        .itemId       = "minecraft:structure_void",
        .nameKey      = "abp.selftest.upgrade",
        .nameFallback = "Selftest Upgrade",
    };
    int                              ticks = 0;
    [[nodiscard]] UpgradeInfo const& info() const override { return mInfo; }
    void                             onTick(TickContext const&) override { ++ticks; }
    void                             onPickup(UpgradeContext const& ctx, core::ItemStackData& item) override {
        if (item.id != "minecraft:apple") return;
        auto r      = ctx.backpack().inventory().insert(item, core::AccessContext{core::Source::Pickup, ctx.player()});
        item.count -= r.inserted + r.voided;
    }
};

std::optional<game::CarriedBackpack> findCarried(Player& player, std::string const& uuid) {
    for (auto& [c, bp] : game::carriedBackpacks(player, false, true)) {
        if (bp->uuid() == uuid) return c;
    }
    return std::nullopt;
}

void setOrder(std::shared_ptr<game::Backpack> const& bp, int slot, InventoryOrder order) {
    UpgradeContext(bp, slot).settings()["inventoryOrder"] = std::string(toString(order));
    bp->markDirty();
}

// ---- tests ------------------------------------------------------------------------------------

ABP_REGISTER_SELFTEST("inception.allows_nesting", [](Context& ctx) {
    ConfigGuard<GeneralConfig> guard;
    config::mut<GeneralConfig>().allowNesting = false;
    auto outer                                = ctx.createBackpack(Tier::Iron);
    auto plain                                = ctx.createBackpack(Tier::Leather);
    auto inner                                = ctx.createBackpack(Tier::Leather);
    ctx.check(store(ctx, *plain, *inner) < 0, "no nesting without Inception");
    int slot = ctx.installUpgrade(outer, std::string(kId));
    ctx.check(outer->hasCapability("allow_nesting"), "allow_nesting capability");
    int at = store(ctx, *outer, *inner);
    ctx.require(at >= 0, "backpack stored in an Inception backpack");
    ctx.check(store(ctx, *outer, *outer) < 0, "never itself");
    // SB: a backpack with an Inception upgrade can't go into another backpack.
    auto incepted = ctx.createBackpack(Tier::Leather);
    ctx.installUpgrade(incepted, std::string(kId));
    ctx.check(store(ctx, *outer, *incepted) < 0, "a backpack with Inception is refused");
    // Removal is blocked while backpacks are inside (SB error text).
    std::string reason;
    ctx.check(!upgrade::remove(outer, slot, nullptr, &reason).has_value(), "removal refused while not empty");
    ctx.check(reason == "There's one or more backpacks in the backpack", "SB reason: " + reason);
    ctx.check(!outer->data().upgrades[static_cast<std::size_t>(slot)].empty(), "upgrade still installed");
    auto out = outer->inventory().extract(at, 1, kPlayer);
    ctx.check(!out.empty(), "nested backpack taken out");
    ctx.check(upgrade::remove(outer, slot, nullptr, &reason).has_value(), "removal allowed when empty: " + reason);
    ctx.check(!outer->hasCapability("allow_nesting"), "capability gone");
    ctx.check(!upgrade::byId(kId)->info().toggleable, "not toggleable");
});

ABP_REGISTER_SELFTEST("inception.nested_upgrades_work", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard<InceptionConfig> guard;
    auto                         test = std::make_shared<TickUpgrade>();
    ScopedUpgrade                scoped(test);
    auto&                        player = ctx.player();
    auto                         outer  = ctx.createBackpack(Tier::Iron);
    auto                         inner  = ctx.createBackpack(Tier::Leather);
    ctx.installUpgrade(outer, std::string(kId));
    ctx.installUpgrade(inner, "selftest_inception_tick");
    ctx.require(store(ctx, *outer, *inner) >= 0, "store inner");
    ctx.require(ctx.give(player, ctx.backpackItem(*outer), 2) == 2, "give outer");

    auto c = findCarried(player, inner->uuid());
    ctx.require(c.has_value(), "nested backpack reported by carriedBackpacks");
    ctx.check(c->depth == 1 && c->parent == outer->uuid(), "depth 1, parent = outer");
    ctx.check(c->where == game::ItemLocation{game::ItemLocation::Kind::Inventory, 2}, "where = outer item");
    bool onlyTop = true;
    for (auto& [cc, bp] : game::carriedBackpacks(player, false, false)) onlyTop = onlyTop && cc.depth == 0;
    ctx.check(onlyTop, "includeNested=false reports only carried backpacks");

    co_await ctx.ticks(25);
    ctx.check(test->ticks > 0, "nested upgrade ticks");
    auto apples = data("minecraft:apple", 4);
    ctx.check(upgrade::dispatchPickup(player, apples) == 4, "nested upgrade picks up");
    ctx.check(
        inner->inventory().count([](auto const& s) { return s.id == "minecraft:apple"; }) == 4,
        "apples in nested backpack"
    );

    // SB upgradesInContainedBackpacksAreFunctional = false: nested upgrades do nothing.
    config::mut<InceptionConfig>().upgradesInContainedBackpacksAreFunctional = false;
    ctx.check(!findCarried(player, inner->uuid()).has_value(), "config switch off");
    int before = test->ticks;
    co_await ctx.ticks(25);
    ctx.check(test->ticks == before, "no ticks while off");
});

// SC getInventoryForUpgradeProcessing: main + backpacks inside, in the Inception's inventory order.
ABP_REGISTER_SELFTEST("inception.upgrade_inventories", [](Context& ctx) {
    ConfigGuard<InceptionConfig> guard;
    auto                         outer = ctx.createBackpack(Tier::Iron);
    auto                         a     = ctx.createBackpack(Tier::Leather);
    auto                         b     = ctx.createBackpack(Tier::Leather);
    ctx.check(upgradeInventories(outer) == std::vector<std::shared_ptr<game::Backpack>>{outer}, "without Inception: just itself");
    int slot = ctx.installUpgrade(outer, std::string(kId));
    ctx.require(store(ctx, *outer, *a) >= 0 && store(ctx, *outer, *b) >= 0, "store two backpacks");
    ctx.check(inventoryOrderOf(*outer) == InventoryOrder::MainFirst, "SB default: main first");
    auto invs = upgradeInventories(outer);
    ctx.check(invs.size() == 3 && invs[0] == outer && invs[1] == a && invs[2] == b, "main first, then the backpacks inside");
    setOrder(outer, slot, InventoryOrder::InceptedFirst);
    invs = upgradeInventories(outer);
    ctx.check(invs.size() == 3 && invs[0] == a && invs[1] == b && invs[2] == outer, "incepted first");
    config::mut<InceptionConfig>().upgradesUseInventoriesOfBackpacksInBackpack = false;
    ctx.check(upgradeInventories(outer).size() == 1, "config off: only the main inventory");
});

// SB InceptionFluidHandler: the tanks of the backpacks inside join the outer's.
ABP_REGISTER_SELFTEST("inception.contained_tanks", [](Context& ctx) {
    ConfigGuard<InceptionConfig> guard;
    auto                         outer = ctx.createBackpack(Tier::Iron);
    auto                         inner = ctx.createBackpack(Tier::Iron);
    ctx.installUpgrade(outer, std::string(kId));
    ctx.installUpgrade(inner, "tank");
    ctx.require(store(ctx, *outer, *inner) >= 0, "store inner");
    auto innerTanks = api::backpack::fluidHandler(inner);
    ctx.require(innerTanks != nullptr, "inner has a tank");
    ctx.require(innerTanks->fill(api::FluidStack{"minecraft:water", 1000}, false) == 1000, "fill the inner tank");
    auto combined = api::backpack::fluidHandler(outer);
    ctx.require(combined != nullptr, "outer sees the inner tank");
    ctx.check(combined->tanks() == innerTanks->tanks(), "tank count");
    ctx.check(combined->drainAny(250, true).amount == 250, "water drainable through the outer backpack");
    config::mut<InceptionConfig>().upgradesInContainedBackpacksAreFunctional = false;
    ctx.check(api::backpack::fluidHandler(outer) == nullptr, "config off: no tanks");
});

ABP_REGISTER_SELFTEST("inception.assigns_uuid", [](Context& ctx) {
    auto outer = ctx.createBackpack(Tier::Leather);
    ctx.installUpgrade(outer, std::string(kId));
    auto fresh = game::ItemConv::toData(game::makeBackpackItem(Tier::Copper));
    ctx.require(outer->inventory().insert(fresh, kPlayer).inserted == 1, "fresh backpack stored");
    auto slots = backpackSlots(*outer);
    ctx.require(slots.size() == 1, "one nested backpack");
    auto bp = resolveNested(outer, slots.front());
    ctx.require(bp != nullptr, "resolved");
    auto uuid = game::backpackUuid(outer->inventory().at(slots.front()));
    ctx.check(uuid && *uuid == bp->uuid(), "uuid written into the stored item");
    ctx.check(bp->tier() == Tier::Copper, "tier kept");
    ctx.check(resolveNested(outer, slots.front()) == bp, "stable");
    game::Storage::get().erase(bp->uuid());
});

ABP_REGISTER_SELFTEST("inception.open_nested_gui", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    co_await ctx.ticks(10);
    {
        // Stand on the ground: menus close when the player moves away from the fake chest, and
        // the switches below take a few ticks.
        auto const& pos = player.getPosition();
        int         x = static_cast<int>(std::floor(pos.x)), z = static_cast<int>(std::floor(pos.z));
        auto        top = player.getDimensionBlockSource().getAboveTopSolidBlock(x, z, false, false);
        player.teleport(
            Vec3{static_cast<float>(x) + 0.5f, static_cast<float>(top), static_cast<float>(z) + 0.5f},
            player.getDimensionId()
        );
        co_await ctx.ticks(5);
    }
    auto a = ctx.createBackpack(Tier::Iron);
    auto b = ctx.createBackpack(Tier::Iron);
    auto c = ctx.createBackpack(Tier::Leather);
    ctx.installUpgrade(a, std::string(kId));
    // SB allows one level only (a backpack with Inception can't be stored): build the three-level
    // chain the GUI must still cope with through the server's allowNesting option, and give b its
    // Inception upgrade once it is inside a.
    int bAt = -1;
    {
        ConfigGuard<GeneralConfig> general;
        config::mut<GeneralConfig>().allowNesting = true;
        ctx.require(store(ctx, *b, *c) >= 0, "c into b");
        bAt = store(ctx, *a, *b);
    }
    ctx.require(bAt >= 0 && bAt < ui::BackpackMenu::kPageSlots, "b into a (first page)");
    int bInc = ctx.installUpgrade(b, std::string(kId));
    ctx.require(ctx.give(player, ctx.backpackItem(*a), 0) == 0, "give a");
    ctx.require(ui::BackpackMenu::openFor(player, {game::ItemLocation::Kind::Inventory, 0}), "open a");
    mm.forceShown(player);

    // Q on the stored backpack opens it.
    (void)mm.simulate(
        player,
        {
            Action::Drop,
            {Where::Menu, bAt},
            {Where::Menu, 0  },
            1
    }
    );
    co_await ctx.ticks(25);
    auto nested = std::dynamic_pointer_cast<NestedBackpackMenu>(mm.current(player));
    ctx.require(nested != nullptr, "nested menu open");
    ctx.check(nested->backpack()->uuid() == b->uuid(), "showing b");
    ctx.check(nested->ancestors() == std::vector<std::string>{a->uuid()}, "chain = [a]");
    ctx.check(nested->rootSource() == game::ItemLocation{game::ItemLocation::Kind::Inventory, 0}, "root = slot 0");
    ctx.check(nested->lockedInventorySlot == 0, "root item locked");
    ctx.check(backpackSlots(*a).size() == 1, "b was not dropped");
    mm.forceShown(player);
    co_await ctx.ticks(3);
    ctx.check(!mm.shownItem(player, 46).isNull(), "Back button has a visible icon");

    // One level deeper through the button.
    mm.forceShown(player);
    ui::BackpackButton const* button = nullptr;
    for (auto const& btn : ui::BackpackButtonRegistry::entries()) {
        if (btn.id == "inception.open") button = &btn;
    }
    ctx.require(button != nullptr, "button registered");
    ui::BackpackMenuContext bctx{player, *nested, b};
    ctx.check(button->visible(bctx), "button visible with a nested backpack");
    ui::ClickContext click{player, *nested, 48, ui::ClickType::Take, 1, ItemStack::EMPTY_ITEM()};
    button->onClick(bctx, click);
    co_await ctx.ticks(25); // menus switch through closeThen
    auto deeper = std::dynamic_pointer_cast<NestedBackpackMenu>(mm.current(player));
    ctx.require(deeper != nullptr && deeper->backpack()->uuid() == c->uuid(), "c open");
    ctx.check(deeper->ancestors() == (std::vector<std::string>{a->uuid(), b->uuid()}), "chain = [a, b]");
    ctx.check(deeper->chainValid(player), "chain valid");

    // Back -> b (nested), back -> a (plain backpack menu bound to slot 0).
    mm.forceShown(player);
    (void)mm.simulate(
        player,
        {
            Action::Take,
            {Where::Menu,   46},
            {Where::Cursor, 0 },
            1
    }
    );
    co_await ctx.ticks(25); // menus switch through closeThen
    auto back1 = std::dynamic_pointer_cast<NestedBackpackMenu>(mm.current(player));
    ctx.require(back1 != nullptr && back1->backpack()->uuid() == b->uuid(), "back to b");

    // b's Inception settings, opened from b's nested menu, return to b's nested upgrades page.
    upgrade::byId(kId)->openSettings(player, UpgradeContext(b, bInc, &player));
    ctx.require(std::dynamic_pointer_cast<ui::BackpackMenu>(mm.current(player)) == nullptr, "settings menu open");
    mm.forceShown(player);
    (void)mm.simulate(
        player,
        {
            Action::Take,
            {Where::Menu,   22},
            {Where::Cursor, 0 },
            1
    }
    );
    co_await ctx.ticks(25); // menus switch through closeThen
    back1 = std::dynamic_pointer_cast<NestedBackpackMenu>(mm.current(player));
    ctx.require(back1 != nullptr && back1->backpack()->uuid() == b->uuid(), "settings Back returns to nested b");
    ctx.check(back1->view() == ui::BackpackMenu::View::Upgrades, "on the upgrades page");
    ctx.check(back1->ancestors() == std::vector<std::string>{a->uuid()}, "chain kept");

    // Q on a stored backpack in a backpack without (active) Inception is a normal drop.
    {
        ConfigGuard<GeneralConfig> general;
        config::mut<GeneralConfig>().allowNesting = true;
        auto plain                                = ctx.createBackpack(Tier::Leather);
        auto d                                    = ctx.createBackpack(Tier::Leather);
        int  dAt                                  = store(ctx, *plain, *d);
        ctx.require(dAt >= 0, "d into plain");
        auto            click = ui::ClickContext{player, *back1, dAt, ui::ClickType::Drop, 1, ItemStack::EMPTY_ITEM()};
        ui::StorageSlot slotView(plain, dAt);
        ctx.check(!slotView.click(click), "drop not intercepted without Inception");
    }
    mm.forceShown(player);
    (void)mm.simulate(
        player,
        {
            Action::Take,
            {Where::Menu,   46},
            {Where::Cursor, 0 },
            1
    }
    );
    co_await ctx.ticks(25); // menus switch through closeThen
    auto top = std::dynamic_pointer_cast<ui::BackpackMenu>(mm.current(player));
    ctx.require(top != nullptr, "back to a");
    ctx.check(std::dynamic_pointer_cast<NestedBackpackMenu>(top) == nullptr, "a is the top-level menu");
    ctx.check(top->backpack()->uuid() == a->uuid() && top->source().has_value(), "a bound to its item");

    // Taking b out of a while b is open closes b's menu.
    (void)mm.simulate(
        player,
        {
            Action::Drop,
            {Where::Menu, bAt},
            {Where::Menu, 0  },
            1
    }
    );
    co_await ctx.ticks(25);
    ctx.require(std::dynamic_pointer_cast<NestedBackpackMenu>(mm.current(player)) != nullptr, "b open again");
    mm.forceShown(player);
    auto removed = a->inventory().extract(bAt, 1, kPlayer);
    ctx.require(!removed.empty(), "b taken out of a");
    co_await ctx.ticks(25);
    ctx.check(mm.current(player) == nullptr, "nested menu closed when the chain broke");
    mm.close(player);
});

ABP_REGISTER_SELFTEST("inception.settings_menu", [](Context& ctx) {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(Tier::Leather);
    int   slot   = ctx.installUpgrade(bp, std::string(kId));
    ctx.check(inventoryOrderOf(*bp) == InventoryOrder::MainFirst, "default: main first");
    auto* type = upgrade::byId(kId);
    ctx.require(type && type->info().hasSettings, "has settings");
    UpgradeContext uctx(bp, slot, &player);
    auto           menu = type->createSettingsMenu(player, uctx);
    ctx.require(menu != nullptr, "settings menu");
    ctx.require(mm.open(player, menu), "open settings");
    mm.forceShown(player);
    (void)mm.simulate(player, {Action::Take, {Where::Menu, 13}, {Where::Cursor, 0}, 1});
    ctx.check(inventoryOrderOf(*bp) == InventoryOrder::InceptedFirst, "toggled: incepted first");
    ctx.check(
        bp->data().upgrades[static_cast<std::size_t>(slot)].settings.value("inventoryOrder", "") == "incepted_first",
        "persisted in settings"
    );
    // Native tab action (the SB tab's toggle button).
    bool handled = false;
    for (auto const& h : net::TabActionRegistry::entries())
        if (h.upgrade == kId) handled = h.handle(player, uctx, nlohmann::json{{"inception", "toggle"}});
    ctx.check(handled && inventoryOrderOf(*bp) == InventoryOrder::MainFirst, "tab toggle: main first");
    auto lore = type->describe(player, uctx);
    ctx.check(lore.size() == 1, "describe: SB tooltip");
    (void)mm.simulate(player, {Action::Take, {Where::Menu, 13}, {Where::Cursor, 0}, 1});
    // Settings travel with the item.
    auto item = upgrade::remove(bp, slot, &player);
    ctx.require(item.has_value(), "removed (empty backpack)");
    ctx.require(upgrade::install(bp, slot, *item, &player), "reinstalled");
    ctx.check(inventoryOrderOf(*bp) == InventoryOrder::InceptedFirst, "setting kept");
    mm.close(player);
});

} // namespace
} // namespace abp::upgrade::inception
