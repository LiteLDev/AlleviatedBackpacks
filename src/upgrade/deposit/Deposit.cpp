// Deposit upgrade (basic + advanced): using the backpack on a container moves the items the
// upgrade's filter lets through from the backpack into that container.

#include <algorithm>

#include "mc/world/Container.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "game/ItemConv.h"
#include "net/NativeScreens.h"
#include "selftest/SelfTest.h"
#include "ui/menu/Menu.h"
#include "upgrade/IUpgrade.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/deposit/ContainerTransfer.h"
#include "upgrade/deposit/TestSupport.h"
#include "upgrade/filter/FilterCommon.h"

namespace abp::upgrade::deposit {

transfer::Result run(UpgradeContext const& ctx, Container& container, int face, bool advanced) {
    // SB DepositUpgradeWrapper.depositToHandler: InventoryHelper.transfer(backpack -> filtered
    // container); every inserted chunk (at most a max stack) counts as one stack.
    transfer::Result out;
    auto&            inv    = ctx.backpack().inventory();
    auto             filter = transfer::loadFilter(ctx, advanced);
    // INVENTORY: the container's unique stacks, captured once before the transfer.
    auto reference = filter.filterByInventory ? transfer::distinctItems(container) : std::vector<core::ItemStackData>{};
    core::AccessContext const access{core::Source::Upgrade, ctx.player()};
    core::AccessContext const internal{core::Source::Internal, ctx.player()};
    for (int i = 0; i < inv.size(); ++i) {
        auto stack = inv.at(i);
        if (stack.empty() || !filter.matchesContents(stack, reference, &filtering::matcher())) continue;
        int maxStack = std::max(1, stack.maxStack);
        int left     = stack.count;
        while (left > 0) {
            int chunk = std::min(maxStack, left);
            int fits  = transfer::insertIntoContainer(container, face, stack, chunk, true);
            if (fits <= 0) break;
            auto taken = inv.extract(i, fits, access);
            if (taken.empty()) break;
            int put = transfer::insertIntoContainer(container, face, taken, taken.count, false);
            if (put < taken.count) {
                // Should not happen (simulated first); never lose items.
                auto rest = taken.withCount(taken.count - put);
                auto r    = inv.insertInto(i, rest, internal);
                if (!r.remainder.empty()) (void)inv.insert(r.remainder, internal);
            }
            if (put <= 0) break;
            ++out.stacks;
            out.items += put;
            left      -= put;
        }
    }
    return out;
}

namespace {

class DepositUpgrade : public IUpgrade {
public:
    explicit DepositUpgrade(bool advanced = false) : mAdvanced(advanced) {
        mInfo = UpgradeInfo{
            .id           = advanced ? "advanced_deposit" : "deposit",
            .itemId       = advanced ? "alleviatedbackpacks:advanced_deposit_upgrade" : "alleviatedbackpacks:deposit_upgrade",
            .nameKey      = advanced ? "abp.upgrade.advanced_deposit.name" : "abp.upgrade.deposit.name",
            .nameFallback = advanced ? "Advanced Deposit Upgrade" : "Deposit Upgrade",
            .tier         = advanced ? UpgradeTier::Advanced : UpgradeTier::Basic,
            .group        = "deposit",
            .hasSettings  = true,
        };
    }
    [[nodiscard]] UpgradeInfo const& info() const override { return mInfo; }
    [[nodiscard]] std::shared_ptr<ui::Menu> createSettingsMenu(Player& player, UpgradeContext const& ctx) override {
        return transfer::settingsMenu(player, ctx, mAdvanced, true);
    }
    [[nodiscard]] std::vector<std::string> describe(Player& player, UpgradeContext const& ctx) const override {
        return transfer::describe(player, ctx, mAdvanced, true);
    }

private:
    bool        mAdvanced;
    UpgradeInfo mInfo;
};

class AdvancedDepositUpgrade : public DepositUpgrade {
public:
    AdvancedDepositUpgrade() : DepositUpgrade(true) {}
};

} // namespace
} // namespace abp::upgrade::deposit

ABP_REGISTER_UPGRADE(abp::upgrade::deposit::DepositUpgrade);
ABP_REGISTER_UPGRADE(abp::upgrade::deposit::AdvancedDepositUpgrade);

// ---- native tab (client: FilterTab "filter" at +3,+24) ------------------------------------------

namespace abp::upgrade::deposit {
namespace {
[[maybe_unused]] bool const kTabRegistered = (filtering::registerStandardFilterTab("deposit"), true);
} // namespace
} // namespace abp::upgrade::deposit


// ---- selftests -----------------------------------------------------------------------------------

namespace abp::upgrade::deposit {
namespace {

using selftest::Context;
using namespace transfer::test;

ABP_REGISTER_SELFTEST("deposit.basic_filter", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    co_await settle(ctx, player);
    auto  bp     = ctx.createBackpack(core::Tier::Leather);
    int   slot   = ctx.installUpgrade(bp, "deposit");
    UpgradeContext uc(bp, slot, &player);
    PlacedBlock    chest(ctx, player, "minecraft:chest");
    auto&          c = chest.container();

    fill(*bp, "minecraft:iron_ingot", 40);
    fill(*bp, "minecraft:dirt", 20);
    fill(*bp, "minecraft:cobblestone", 64);

    // Allow list: iron only.
    setFilter(uc, false, {"minecraft:iron_ingot"}, core::FilterMode::Allow);
    auto r = run(uc, c, kUp, false);
    ctx.check(r.stacks == 1 && r.items == 40, "allow list moved 1 stack / 40 items, got " + describeResult(r));
    ctx.check(countIn(c, "minecraft:iron_ingot") == 40, "iron in chest");
    ctx.check(countIn(*bp, "minecraft:iron_ingot") == 0, "iron left the backpack");
    ctx.check(countIn(*bp, "minecraft:dirt") == 20, "dirt stays");

    // Deny list: everything except dirt.
    setFilter(uc, false, {"minecraft:dirt"}, core::FilterMode::Deny);
    r = run(uc, c, kUp, false);
    ctx.check(r.stacks == 1 && countIn(c, "minecraft:cobblestone") == 64, "deny list moved cobblestone");
    ctx.check(countIn(*bp, "minecraft:dirt") == 20 && countIn(c, "minecraft:dirt") == 0, "denied dirt stays");

    // Empty allow list (SC): nothing.
    setFilter(uc, false, {}, core::FilterMode::Allow);
    r = run(uc, c, kUp, false);
    ctx.check(r.stacks == 0 && countIn(*bp, "minecraft:dirt") == 20, "empty allow list deposits nothing");
    // Empty block list (a new filter): everything.
    setFilter(uc, false, {}, core::FilterMode::Deny);
    r = run(uc, c, kUp, false);
    ctx.check(r.stacks == 1 && countIn(c, "minecraft:dirt") == 20 && bp->inventory().isEmpty(), "empty block list deposits all");

    // Basic upgrades ignore advanced match options stored in the settings.
    auto f    = core::Filter::fromJson(uc.settings()["filter"], 9);
    f.mode    = core::FilterMode::Allow;
    f.matchBy = core::MatchBy::Namespace;
    f.setGhost(0, itemData("minecraft:stone", 1));
    uc.settings()["filter"] = f.toJson();
    fill(*bp, "minecraft:gold_ingot", 3);
    r = run(uc, c, kUp, false);
    ctx.check(r.stacks == 0 && countIn(*bp, "minecraft:gold_ingot") == 3, "basic: no namespace match");
    // The basic type button also has "Match Inventory Contents" (SB DepositFilterLogicControl.Basic).
    c.setItem(0, game::ItemConv::make("minecraft:gold_ingot", 1));
    setContents(uc, false, true);
    r = run(uc, c, kUp, false);
    ctx.check(r.stacks == 1 && countIn(c, "minecraft:gold_ingot") == 4, "basic: match inventory contents");
});

ABP_REGISTER_SELFTEST("deposit.capacity_and_merge", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    co_await settle(ctx, player);
    auto  bp     = ctx.createBackpack(core::Tier::Leather);
    int   slot   = ctx.installUpgrade(bp, "deposit");
    UpgradeContext uc(bp, slot, &player);
    PlacedBlock    hopper(ctx, player, "minecraft:hopper"); // 5 slots
    auto&          c = hopper.container();

    c.setItem(0, game::ItemConv::make("minecraft:stone", 60));
    c.setItem(1, game::ItemConv::make("minecraft:dirt", 64));
    // An oversized backpack slot (as with stack upgrades): 300 stone.
    bp->inventory().set(0, itemData("minecraft:stone", 1).withCount(300));
    fill(*bp, "minecraft:ender_pearl", 20); // max stack 16
    long long before = countIn(*bp, "minecraft:stone") + countIn(c, "minecraft:stone");

    auto r = run(uc, c, kUp, false);
    // stone: 4 merged into slot 0 + 64 into slots 2, 3, 4 = 196; no room for pearls.
    ctx.check(countIn(c, "minecraft:stone") == 60 + 4 + 64 * 3, "stone filled the hopper: " + std::to_string(countIn(c, "minecraft:stone")));
    ctx.check(countIn(*bp, "minecraft:stone") + countIn(c, "minecraft:stone") == before, "no stone lost or duplicated");
    ctx.check(countIn(*bp, "minecraft:ender_pearl") == 20, "pearls stay (no room)");
    // SC InventoryHelper.transfer counts every inserted chunk (<= 64): 64 + 64 + 64 + 4.
    ctx.check(r.stacks == 4 && r.items == 196, "result " + describeResult(r));
    ctx.check(c.getItem(2).mCount == 64, "stacks are capped at 64");

    // Respect the item's own max stack size.
    c.removeAllItems();
    r = run(uc, c, kUp, false);
    ctx.check(countIn(c, "minecraft:ender_pearl") == 20 && c.getItem(4).isNull(), "pearls split in stacks of 16");
    int pearlSlots = 0;
    for (int i = 0; i < c.getContainerSize(); ++i) {
        auto const& it = c.getItem(i);
        if (!it.isNull() && it.getTypeName() == "minecraft:ender_pearl") {
            ++pearlSlots;
            ctx.check(it.mCount <= 16, "pearl stack <= 16");
        }
    }
    ctx.check(pearlSlots == 2, "pearls in 2 stacks");
});

ABP_REGISTER_SELFTEST("deposit.advanced_contents_mode", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    co_await settle(ctx, player);
    auto  bp     = ctx.createBackpack(core::Tier::Leather);
    int   slot   = ctx.installUpgrade(bp, "advanced_deposit");
    UpgradeContext uc(bp, slot, &player);
    PlacedBlock    chest(ctx, player, "minecraft:chest");
    auto&          c = chest.container();

    c.setItem(5, game::ItemConv::make("minecraft:dirt", 1));
    fill(*bp, "minecraft:dirt", 30);
    fill(*bp, "minecraft:stone", 30);
    setFilter(uc, true, {"minecraft:stone"}, core::FilterMode::Allow);
    setContents(uc, true, true);
    auto r = run(uc, c, kUp, true);
    ctx.check(r.stacks == 1 && countIn(c, "minecraft:dirt") == 31, "only items already in the chest (dirt)");
    ctx.check(countIn(*bp, "minecraft:stone") == 30, "stone stays although it's in the filter list");

    // Contents mode with an empty container deposits nothing.
    c.removeAllItems();
    fill(*bp, "minecraft:dirt", 5);
    r = run(uc, c, kUp, true);
    ctx.check(r.stacks == 0, "empty container: nothing deposited");

    // Advanced: namespace matching and 16 filter slots.
    setContents(uc, true, false);
    auto f = core::Filter::fromJson(uc.settings()["filter"], 16);
    ctx.check(f.ghosts.size() == 16, "16 filter slots");
    f.matchBy = core::MatchBy::Namespace;
    for (auto& g : f.ghosts) g.clear();
    f.setGhost(15, itemData("minecraft:apple", 1));
    uc.settings()["filter"] = f.toJson();
    r = run(uc, c, kUp, true);
    ctx.check(r.stacks == 2 && bp->inventory().isEmpty(), "namespace match deposits every minecraft item");
});

ABP_REGISTER_SELFTEST("deposit.settings_menu", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await settingsMenuTest(ctx, "advanced_deposit");
});

} // namespace
} // namespace abp::upgrade::deposit
