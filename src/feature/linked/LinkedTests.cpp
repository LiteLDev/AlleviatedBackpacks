// Selftests of linked storage: linked.*

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/platform/UUID.h"
#include "mc/world/inventory/CraftingContainer.h"
#include "mc/world/item/crafting/CraftingContext.h"
#include "mc/world/item/crafting/Recipe.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/level/ILevel.h"
#include "mc/world/level/Level.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/PlayerUISlot.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "feature/admin/Admin.h"
#include "feature/dedup/Dedup.h"
#include "feature/linked/Linked.h"
#include "feature/linked/LinkedCrafting.h"
#include "feature/placed/PlacedBackpacks.h"
#include "feature/placed/TestSite.h"
#include "game/Backpack.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "game/Storage.h"
#include "net/Companion.h"
#include "net/NativeScreens.h"
#include "selftest/SelfTest.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"

namespace abp::linked {
namespace {

using selftest::Context;
using core::Tier;

core::AccessContext const kInternal{core::Source::Internal};

ItemStack linker(int count = 1) { return game::ItemConv::make(std::string(kLinkerId), count); }
core::ItemStackData data(std::string_view id, int count) { return game::ItemConv::toData(game::ItemConv::make(id, count)); }

ABP_REGISTER_SELFTEST("linked.item_exists", [](Context& ctx) {
    auto l = linker();
    ctx.require(!l.isNull(), "the Ender Linker item is registered (behavior pack)");
    ctx.check(isLinker(l) && !linkerGroup(l), "a crafted linker is unlinked");
    ctx.check(l.getMaxStackSize() == 64, "stacks to 64");
});

ABP_REGISTER_SELFTEST("linked.create_and_add", [](Context& ctx) {
    auto& service = Service::get();
    auto  primary = ctx.createBackpack(Tier::Iron);
    primary->inventory().insert(data("minecraft:diamond", 5), kInternal);
    auto  primaryItem = ctx.backpackItem(*primary);
    auto  l           = linker();
    ctx.require(service.link("owner", l, primaryItem) == LinkResult::Success, "unlinked linker + backpack: new group");
    auto pd = endpointOf(primaryItem);
    ctx.require(pd && pd->primary, "the backpack is the PRIMARY endpoint");
    ctx.check(roleOf(primaryItem) == Role::Primary, "role primary");
    ctx.check(game::backpackUuid(primaryItem) == primary->uuid(), "the primary keeps its storage");
    ctx.check(linkerGroup(l) == pd->group, "the linker is bound to the group");
    ctx.check(service.isPrimaryEndpoint(pd->group, pd->endpoint), "group records the primary");

    // A second, empty backpack joins as a secondary endpoint and shows the same contents.
    auto secondary     = ctx.createBackpack(Tier::Leather);
    auto secondaryUuid = secondary->uuid();
    secondary.reset();
    auto secondaryItem = ctx.backpackItem(*game::Storage::get().find(secondaryUuid));
    ctx.require(service.link("owner", l, secondaryItem) == LinkResult::Success, "bound linker + empty backpack: secondary");
    ctx.check(l.isNull() || l.mCount == 0, "the bound linker is used up");
    auto sd = endpointOf(secondaryItem);
    ctx.require(sd && !sd->primary && sd->group == pd->group, "secondary endpoint of the same group");
    ctx.check(roleOf(secondaryItem) == Role::Secondary, "role secondary");
    ctx.check(game::backpackUuid(secondaryItem) == primary->uuid(), "the secondary shows the group's storage");
    ctx.check(game::Storage::get().find(secondaryUuid) == nullptr, "the secondary's own empty storage is gone");
    bool changed = false;
    auto shared  = game::Storage::get().forItem(secondaryItem, &changed);
    ctx.check(shared == primary && !changed, "opening the secondary opens the shared storage");
    ctx.check(shared->inventory().count([](core::ItemStackData const& s) { return s.id == "minecraft:diamond"; }) == 5, "shared contents");
});

ABP_REGISTER_SELFTEST("linked.rules", [](Context& ctx) {
    auto& service = Service::get();
    auto  a       = ctx.createBackpack(Tier::Gold);
    auto  aItem   = ctx.backpackItem(*a);
    auto  l       = linker();
    ctx.require(service.link("owner", l, aItem) == LinkResult::Success, "group");
    // A backpack with contents can't join.
    auto full = ctx.createBackpack(Tier::Leather);
    full->inventory().insert(data("minecraft:stone", 1), kInternal);
    auto fullItem = ctx.backpackItem(*full);
    auto bound    = l;
    ctx.check(service.link("owner", bound, fullItem) == LinkResult::EndpointHasContents, "Storage has contents");
    ctx.check(!endpointOf(fullItem), "nothing changed");
    // An endpoint can't be linked again with a bound linker.
    bound = l;
    ctx.check(service.link("owner", bound, aItem) == LinkResult::AlreadyLinked, "Storage is already linked");
    // An unlinked linker on an endpoint binds to its group.
    auto fresh = linker();
    ctx.check(service.link("owner", fresh, aItem) == LinkResult::Success, "bind existing endpoint");
    ctx.check(linkerGroup(fresh) == endpointOf(aItem)->group, "linker targets the endpoint's group");
    // Not a storage.
    auto stone = game::ItemConv::make("minecraft:stone");
    fresh      = linker();
    ctx.check(service.link("owner", fresh, stone) == LinkResult::UnsupportedEndpoint, "Storage cannot be linked");
    // Maintenance never deletes a group's storage.
    ctx.check(admin::removeNonPlayer(false).empty() || game::Storage::get().find(a->uuid()) != nullptr, "group storage protected");
});

ABP_REGISTER_SELFTEST("linked.only_primary_upgrades_tier", [](Context& ctx) {
    auto& service = Service::get();
    auto  primary = ctx.createBackpack(Tier::Copper);
    auto  pItem   = ctx.backpackItem(*primary);
    auto  l       = linker();
    ctx.require(service.link("owner", l, pItem) == LinkResult::Success, "group");
    auto other = game::makeBackpackItem(Tier::Diamond); // never opened: no storage, no contents
    ctx.require(service.link("owner", l, other) == LinkResult::Success, "secondary (diamond item)");
    bool changed = false;
    auto bp      = game::Storage::get().forItem(other, &changed);
    ctx.check(bp == primary && bp->tier() == Tier::Copper, "a higher-tier secondary doesn't upgrade the shared storage");
    // The primary upgraded (crafting swaps the item id): the storage grows.
    auto upgraded = game::makeBackpackItem(Tier::Gold);
    upgraded.setUserData(pItem.mUserData->clone());
    bp = game::Storage::get().forItem(upgraded, &changed);
    ctx.check(bp == primary && bp->tier() == Tier::Gold, "the primary upgrades the shared storage");
});

ABP_REGISTER_SELFTEST("linked.creative_copy_and_dedup", [](Context& ctx) {
    auto& service = Service::get();
    auto  primary = ctx.createBackpack(Tier::Leather);
    auto  pItem   = ctx.backpackItem(*primary);
    auto  l       = linker();
    ctx.require(service.link("owner", l, pItem) == LinkResult::Success, "group");
    auto copy = service.createSecondaryEndpointCopy(pItem);
    ctx.require(copy.has_value(), "creative placement copy");
    auto d = endpointOf(*copy);
    ctx.check(d && !d->primary && d->endpoint != endpointOf(pItem)->endpoint, "a new secondary endpoint");
    ctx.check(game::backpackUuid(*copy) == primary->uuid(), "same storage");
    // Endpoints share a uuid on purpose: the dedup logic leaves them alone.
    auto& player = ctx.player();
    ctx.require(ctx.give(player, pItem, 10) == 10 && ctx.give(player, *copy, 11) == 11, "give both");
    ctx.check(dedup::dedupePlayers(static_cast<int>(player.getDimensionId())) == 0, "no dedup between endpoints");
    ctx.check(game::backpackUuid(game::itemAt(player, {game::ItemLocation::Kind::Inventory, 11})) == primary->uuid(), "uuid kept");
});

ABP_REGISTER_SELFTEST("linked.placed_endpoints", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& sp = ctx.spawnPlayer();
    co_await placed::test::toGround(ctx, sp);
    placed::test::Site site(sp);
    auto& service = Service::get();
    auto  bp      = ctx.createBackpack(Tier::Leather);
    bp->inventory().insert(data("minecraft:emerald", 4), kInternal);
    auto primary = ctx.backpackItem(*bp);
    auto l       = linker();
    ctx.require(service.link("owner", l, primary) == LinkResult::Success, "group");
    auto copy = service.createSecondaryEndpointCopy(primary);
    ctx.require(copy.has_value(), "secondary endpoint");
    auto a = site.at(0, 1), b = site.at(2, 1);
    for (auto const& pos : {a, b}) {
        site.set(pos + BlockPos{0, -1, 0}, "minecraft:stone");
        site.set(pos, "minecraft:air");
        site.touch(pos);
    }
    auto& placedBackpacks = placed::PlacedBackpacks::get();
    ctx.require(placedBackpacks.place(site.region(), a, primary, 2) == placed::PlacedBackpacks::PlaceResult::Ok, "primary placed");
    ctx.require(placedBackpacks.place(site.region(), b, *copy, 2) == placed::PlacedBackpacks::PlaceResult::Ok, "a second endpoint of the group can stand too");
    ctx.check(placedBackpacks.backpackAt(site.region(), a) == bp && placedBackpacks.backpackAt(site.region(), b) == bp, "both show the shared storage");
    // A plain copy of an already placed backpack is still refused.
    auto plain = ctx.backpackItem(*ctx.createBackpack(Tier::Iron));
    auto plainCopy = plain;
    auto c = site.at(4, 1);
    site.set(c + BlockPos{0, -1, 0}, "minecraft:stone");
    site.set(c, "minecraft:air");
    site.touch(c);
    auto d = site.at(6, 1);
    site.set(d + BlockPos{0, -1, 0}, "minecraft:stone");
    site.set(d, "minecraft:air");
    site.touch(d);
    ctx.require(placedBackpacks.place(site.region(), c, plain, 2) == placed::PlacedBackpacks::PlaceResult::Ok, "plain placed");
    ctx.check(placedBackpacks.place(site.region(), d, plainCopy, 2) != placed::PlacedBackpacks::PlaceResult::Ok, "a copy of a placed plain backpack is refused");
    // Taking one endpoint away leaves the other one working.
    auto taken = placedBackpacks.take(site.region(), a);
    ctx.check(taken.has_value(), "primary picked up");
    ctx.check(placedBackpacks.backpackAt(site.region(), b) == bp, "the other endpoint still stands for the storage");
    ctx.check(placedBackpacks.findUuid(bp->uuid()).has_value(), "found by uuid");
});

ABP_REGISTER_SELFTEST("linked.screen_click", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    net::setCompanion(player, net::CompanionInfo{"test", {"native_screen"}});
    net::setTestSink([](Player&, std::string_view, nlohmann::json const&) {});
    struct Forget {
        Player& p;
        ~Forget() {
            net::setTestSink(nullptr);
            net::setCompanion(p, std::nullopt);
            ui::MenuManager::get().close(p);
        }
    } forget{player};
    auto opened = ctx.createBackpack(Tier::Iron);
    auto target = ctx.createBackpack(Tier::Leather);
    ctx.require(ctx.give(player, ctx.backpackItem(*opened), 0) == 0, "give the backpack to open");
    ctx.require(ctx.give(player, ctx.backpackItem(*target), 5) == 5, "give the backpack to link");
    ctx.require(ui::BackpackMenu::openFor(player, {game::ItemLocation::Kind::Inventory, 0}), "open");
    ui::MenuManager::get().forceShown(player);
    co_await ctx.ticks(2);
    auto session = net::nativeSession(player);
    ctx.require(session != 0, "native session");
    player.mPlayerUIContainer->setItem(0, linker(3)); // a stack of 3 unlinked linkers on the cursor
    bool ok = net::handleScreenAction(
        player,
        nlohmann::json{{"session", session}, {"action", "click"}, {"area", "inventory"}, {"index", 5}, {"button", 1}}
    );
    ctx.check(ok, "right click with the linker is handled");
    auto const& linked = game::itemAt(player, {game::ItemLocation::Kind::Inventory, 5});
    ctx.check(roleOf(linked) == Role::Primary, "the clicked backpack became a primary endpoint");
    auto cursor = player.mPlayerUIContainer->getItem(0);
    ctx.check(isLinker(cursor) && cursor.mCount == 2 && !linkerGroup(cursor), "one linker split off the cursor stack");
    bool gotBound = false;
    for (int i = 0; i < 36; ++i) gotBound = gotBound || linkerGroup(player.getInventory().getItem(i)).has_value();
    ctx.check(gotBound, "the bound linker went to the inventory");
    player.mPlayerUIContainer->setItem(0, ItemStack::EMPTY_ITEM());
    ui::MenuManager::get().close(player);
});

// ---- GX-2: SC's ender_linker_endpoint crafting recipe ------------------------------------------------

ABP_REGISTER_SELFTEST("linked.craft_recipe", [](Context& ctx) {
    auto level = ll::service::getLevel();
    ctx.require(level != nullptr, "level");
    auto const& all = level->getRecipes().mRecipes.get();
    auto        it  = all.find(HashedString("crafting_table"));
    ctx.require(it != all.end(), "crafting recipes");
    for (auto t : {"leather", "copper", "iron", "gold", "diamond", "netherite"}) {
        auto id = std::string("alleviatedbackpacks:ender_linker_endpoint_") + t;
        auto r  = it->second.find(id);
        ctx.require(r != it->second.end() && r->second, id + " registered");
        auto const& results = r->second->getResultItems();
        ctx.check(results.size() == 1 && results[0].getTypeName() == kLinkerId, id + ": crafts a linker");
        // The ingredients match a linker and the backpack in any grid position.
        CraftingContainer grid(3, 3);
        grid.setItem(4, linker());
        grid.setItem(7, game::ItemConv::make(std::string("alleviatedbackpacks:") + t + "_backpack"));
        CraftingContext cc(Bedrock::NotNullNonOwnerPtr<ILevel>(Bedrock::NonOwnerPointer<ILevel>(static_cast<ILevel const&>(*level))));
        ctx.check(r->second->matches(grid, cc), id + " matches linker + backpack");
    }

    // SC getPlan.
    auto& service = Service::get();
    auto  a       = ctx.createBackpack(Tier::Iron);
    auto  aItem   = ctx.backpackItem(*a);
    ctx.check(planFor(linker(), aItem) == CraftPlan::CreatePrimary, "unlinked + unlinked: CREATE_PRIMARY");
    auto bound = linker();
    ctx.require(service.link("owner", bound, aItem) == LinkResult::Success, "group");
    ctx.check(planFor(linker(), aItem) == CraftPlan::BindExisting, "unlinked + endpoint: BIND_EXISTING_ENDPOINT");
    auto empty     = ctx.createBackpack(Tier::Leather);
    auto emptyItem = ctx.backpackItem(*empty);
    ctx.check(planFor(bound, emptyItem) == CraftPlan::AddSecondary, "bound + unlinked: ADD_SECONDARY");
    ctx.check(!planFor(bound, aItem).has_value(), "bound + endpoint: no recipe");
    auto full = ctx.createBackpack(Tier::Leather);
    full->inventory().insert(data("minecraft:dirt", 3), kInternal);
    ctx.check(service.checkLink(bound, ctx.backpackItem(*full)) == LinkResult::EndpointHasContents, "ADD with contents refused (SC diagnostic)");
    ctx.check(service.checkLink(bound, emptyItem) == LinkResult::Success, "ADD of an empty backpack allowed");
});

ABP_REGISTER_SELFTEST("linked.craft_finalize", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    co_await ctx.ticks(1);
    player.getInventory().removeAllItems();
    player.mPlayerUIContainer->setItem(0, ItemStack::EMPTY_ITEM());
    auto countLinkers = [&](bool boundOnly) {
        int n = 0;
        for (int i = 0; i < 36; ++i) {
            auto const& s = player.getInventory().getItem(i);
            if (isLinker(s) && (!boundOnly || linkerGroup(s))) n += s.mCount;
        }
        auto const& c = player.mPlayerUIContainer->getItem(0);
        if (isLinker(c) && (!boundOnly || linkerGroup(c))) n += c.mCount;
        return n;
    };

    int const gridSlot = static_cast<int>(PlayerUISlot::Crafting3x3Input1) + 4;
    player.mPlayerUIContainer->setItem(gridSlot, ItemStack::EMPTY_ITEM());

    // CREATE_PRIMARY: the grid had a plain linker + the backpack; the crafted plain linker is on
    // the cursor. The backpack goes back into its grid slot as the PRIMARY endpoint.
    auto bp       = ctx.createBackpack(Tier::Iron);
    auto consumed = ctx.backpackItem(*bp);
    player.mPlayerUIContainer->setItem(0, linker());
    ctx.require(finalizeCraft(player, CraftPlan::CreatePrimary, linker(), consumed, gridSlot), "finalised");
    ItemStack primary = player.mPlayerUIContainer->getItem(gridSlot);
    ctx.check(roleOf(primary) == Role::Primary, "the backpack stays in the grid as the PRIMARY endpoint (SC remainder)");
    ctx.check(game::backpackUuid(primary) == bp->uuid(), "still the same backpack");
    auto cursor = player.mPlayerUIContainer->getItem(0);
    ctx.check(isLinker(cursor) && cursor.mCount == 1 && linkerGroup(cursor) == endpointOf(primary)->group, "the crafted linker is bound to the group");
    player.mPlayerUIContainer->setItem(gridSlot, ItemStack::EMPTY_ITEM());

    // ADD_SECONDARY: the bound linker was consumed; the crafted item becomes the backpack.
    ItemStack boundLinker = cursor;
    auto      second      = ctx.createBackpack(Tier::Leather);
    auto      secondItem  = ctx.backpackItem(*second);
    second.reset();
    player.mPlayerUIContainer->setItem(0, linker()); // what the recipe delivered
    ctx.require(finalizeCraft(player, CraftPlan::AddSecondary, boundLinker, secondItem, gridSlot), "finalised");
    auto sec = player.mPlayerUIContainer->getItem(0);
    ctx.check(roleOf(sec) == Role::Secondary && game::backpackUuid(sec) == bp->uuid(), "the crafted item is a secondary endpoint of the group");
    ctx.check(countLinkers(false) == 0, "no linker left (the bound one was used up)");
    ctx.check(player.mPlayerUIContainer->getItem(gridSlot).isNull(), "nothing stays in the grid");
    player.mPlayerUIContainer->setItem(0, ItemStack::EMPTY_ITEM());

    // BIND_EXISTING_ENDPOINT, the crafted linker shift-clicked onto a stack of plain linkers:
    // one is split off and bound.
    ctx.give(player, linker(4), 7);
    ctx.require(finalizeCraft(player, CraftPlan::BindExisting, linker(), primary, gridSlot), "finalised");
    ctx.check(countLinkers(true) == 1 && countLinkers(false) == 4, "one of the 4 linkers is bound");
    ctx.check(roleOf(player.mPlayerUIContainer->getItem(gridSlot)) == Role::Primary, "the endpoint stays in the grid, unchanged");
    player.mPlayerUIContainer->setItem(gridSlot, ItemStack::EMPTY_ITEM());
    player.getInventory().removeAllItems();
});

} // namespace
} // namespace abp::linked
