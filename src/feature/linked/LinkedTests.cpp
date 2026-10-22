// Selftests of linked storage: linked.*

#include "mc/platform/UUID.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "feature/admin/Admin.h"
#include "feature/dedup/Dedup.h"
#include "feature/linked/Linked.h"
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

} // namespace
} // namespace abp::linked
