// Selftests: inception.*

#include <algorithm>

#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

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

void setMode(std::shared_ptr<game::Backpack> const& bp, int slot, NestedUpgrades mode) {
    UpgradeContext(bp, slot).settings()["nestedUpgrades"] = std::string(toString(mode));
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
    // Cycle: outer can't go into inner even if inner gets Inception too.
    ctx.installUpgrade(inner, std::string(kId));
    ctx.check(store(ctx, *inner, *outer) < 0, "cycles refused");
    // Removal is blocked while backpacks are inside.
    std::string reason;
    ctx.check(!upgrade::remove(outer, slot, nullptr, &reason).has_value(), "removal refused while not empty");
    ctx.check(!reason.empty(), "removal reason given");
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
    int                          islot  = ctx.installUpgrade(outer, std::string(kId));
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

    // Non-storage only: our test upgrade counts as storage related.
    config::mut<InceptionConfig>().storageUpgrades.push_back("selftest_inception_tick");
    setMode(outer, islot, NestedUpgrades::NonStorage);
    c = findCarried(player, inner->uuid());
    ctx.require(c.has_value(), "still reported in non-storage mode");
    ctx.check(!c->upgradeActive("selftest_inception_tick", "selftest_inception_tick"), "storage upgrade gated");
    ctx.check(c->upgradeActive("feeding_like_other", "other"), "other upgrades pass");
    int before = test->ticks;
    co_await ctx.ticks(25);
    ctx.check(test->ticks == before, "gated upgrade does not tick");
    apples = data("minecraft:apple", 4);
    ctx.check(upgrade::dispatchPickup(player, apples) == 0, "gated upgrade does not pick up");

    // None: nested backpacks are just storage.
    setMode(outer, islot, NestedUpgrades::None);
    ctx.check(!findCarried(player, inner->uuid()).has_value(), "mode none: not reported");

    // Server switch.
    setMode(outer, islot, NestedUpgrades::All);
    ctx.check(findCarried(player, inner->uuid()).has_value(), "mode all again");
    config::mut<InceptionConfig>().nestedUpgradesWork = false;
    ctx.check(!findCarried(player, inner->uuid()).has_value(), "config switch off");
});

ABP_REGISTER_SELFTEST("inception.depth_and_requirements", [](Context& ctx) {
    ConfigGuard<InceptionConfig> guard;
    ConfigGuard<GeneralConfig>   general;
    auto&                        player = ctx.player();
    auto                         a      = ctx.createBackpack(Tier::Iron);
    auto                         b      = ctx.createBackpack(Tier::Iron);
    auto                         c      = ctx.createBackpack(Tier::Leather);
    ctx.installUpgrade(a, std::string(kId));
    ctx.installUpgrade(b, std::string(kId));
    ctx.require(store(ctx, *b, *c) >= 0, "c into b");
    ctx.require(store(ctx, *a, *b) >= 0, "b into a");
    ctx.require(ctx.give(player, ctx.backpackItem(*a), 4) == 4, "give a");
    config::mut<InceptionConfig>().maxDepth = 2;
    auto cc                                 = findCarried(player, c->uuid());
    ctx.check(cc && cc->depth == 2 && cc->parent == b->uuid(), "depth 2 found");
    config::mut<InceptionConfig>().maxDepth = 1;
    ctx.check(findCarried(player, b->uuid()).has_value(), "depth 1 found with maxDepth 1");
    ctx.check(!findCarried(player, c->uuid()).has_value(), "depth 2 hidden with maxDepth 1");
    config::mut<InceptionConfig>().maxDepth = 3;

    // Nesting allowed by the server config, but without Inception: inner upgrades stay inactive.
    config::mut<GeneralConfig>().allowNesting = true;
    auto plain                                = ctx.createBackpack(Tier::Leather);
    auto d                                    = ctx.createBackpack(Tier::Leather);
    ctx.require(store(ctx, *plain, *d) >= 0, "nesting via general config");
    ctx.require(ctx.give(player, ctx.backpackItem(*plain), 5) == 5, "give plain");
    ctx.check(!findCarried(player, d->uuid()).has_value(), "no Inception: nested upgrades inactive");
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
    auto  a      = ctx.createBackpack(Tier::Iron);
    auto  b      = ctx.createBackpack(Tier::Iron);
    auto  c      = ctx.createBackpack(Tier::Leather);
    ctx.installUpgrade(a, std::string(kId));
    int bInc = ctx.installUpgrade(b, std::string(kId));
    ctx.require(store(ctx, *b, *c) >= 0, "c into b");
    int bAt = store(ctx, *a, *b);
    ctx.require(bAt >= 0 && bAt < ui::BackpackMenu::kPageSlots, "b into a (first page)");
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
    auto nested = std::dynamic_pointer_cast<NestedBackpackMenu>(mm.current(player));
    ctx.require(nested != nullptr, "nested menu open");
    ctx.check(nested->backpack()->uuid() == b->uuid(), "showing b");
    ctx.check(nested->ancestors() == std::vector<std::string>{a->uuid()}, "chain = [a]");
    ctx.check(nested->rootSource() == game::ItemLocation{game::ItemLocation::Kind::Inventory, 0}, "root = slot 0");
    ctx.check(nested->lockedInventorySlot == 0, "root item locked");
    ctx.check(backpackSlots(*a).size() == 1, "b was not dropped");

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
    co_await ctx.ticks(2);
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
    co_await ctx.ticks(2);
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
    co_await ctx.ticks(2);
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
    co_await ctx.ticks(2);
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
    ctx.require(std::dynamic_pointer_cast<NestedBackpackMenu>(mm.current(player)) != nullptr, "b open again");
    mm.forceShown(player);
    auto removed = a->inventory().extract(bAt, 1, kPlayer);
    ctx.require(!removed.empty(), "b taken out of a");
    co_await ctx.ticks(12);
    ctx.check(mm.current(player) == nullptr, "nested menu closed when the chain broke");
    mm.close(player);
});

ABP_REGISTER_SELFTEST("inception.settings_menu", [](Context& ctx) {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(Tier::Leather);
    int   slot   = ctx.installUpgrade(bp, std::string(kId));
    ctx.check(nestedUpgradesOf(*bp) == NestedUpgrades::All, "default: all");
    auto* type = upgrade::byId(kId);
    ctx.require(type && type->info().hasSettings, "has settings");
    UpgradeContext uctx(bp, slot, &player);
    auto           menu = type->createSettingsMenu(player, uctx);
    ctx.require(menu != nullptr, "settings menu");
    ctx.require(mm.open(player, menu), "open settings");
    mm.forceShown(player);
    (void)mm.simulate(
        player,
        {
            Action::Take,
            {Where::Menu,   13},
            {Where::Cursor, 0 },
            1
    }
    );
    ctx.check(nestedUpgradesOf(*bp) == NestedUpgrades::NonStorage, "non-storage selected");
    (void)mm.simulate(
        player,
        {
            Action::Take,
            {Where::Menu,   15},
            {Where::Cursor, 0 },
            1
    }
    );
    ctx.check(nestedUpgradesOf(*bp) == NestedUpgrades::None, "none selected");
    ctx.check(
        bp->data().upgrades[static_cast<std::size_t>(slot)].settings.value("nestedUpgrades", "") == "none",
        "persisted in settings"
    );
    auto lore = type->describe(player, uctx);
    ctx.check(lore.size() == 2, "describe lines");
    // Settings travel with the item.
    auto item = upgrade::remove(bp, slot, &player);
    ctx.require(item.has_value(), "removed (empty backpack)");
    ctx.require(upgrade::install(bp, slot, *item, &player), "reinstalled");
    ctx.check(nestedUpgradesOf(*bp) == NestedUpgrades::None, "setting kept");
    mm.close(player);
});

} // namespace
} // namespace abp::upgrade::inception
