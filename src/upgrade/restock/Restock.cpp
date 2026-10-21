// Restock upgrade (basic + advanced): using the backpack on a container moves the items the
// upgrade's filter lets through from that container into the backpack.

#include "mc/world/Container.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "game/ItemConv.h"
#include "selftest/SelfTest.h"
#include "ui/menu/Menu.h"
#include "upgrade/IUpgrade.h"
#include "upgrade/deposit/ContainerTransfer.h"
#include "upgrade/deposit/TestSupport.h"

namespace abp::upgrade::restock {

transfer::Result run(UpgradeContext const& ctx, Container& container, int face, bool advanced) {
    transfer::Result out;
    auto&            inv    = ctx.backpack().inventory();
    auto             filter = transfer::effectiveFilter(
        ctx,
        advanced,
        transfer::contentsMode(ctx, advanced) ? transfer::distinctItems(inv) : std::vector<core::ItemStackData>{}
    );
    core::AccessContext const access{core::Source::Upgrade, ctx.player()};
    int                       size = container.getContainerSize();
    for (int s = 0; s < size; ++s) {
        auto const& stack = container.getItem(s);
        if (stack.isNull() || container.isSlotDisabled(s) || !container.canPullOutItem(s, face, stack)) continue;
        auto data = game::ItemConv::toData(stack);
        if (data.empty() || !filter.passes(data, &game::ItemConv::tags())) continue;
        auto r     = inv.insert(data, access);
        int  moved = r.inserted + r.voided;
        if (moved <= 0) continue;
        container.removeItem(s, moved);
        ++out.stacks;
        out.items += moved;
    }
    return out;
}

namespace {

class RestockUpgrade : public IUpgrade {
public:
    explicit RestockUpgrade(bool advanced = false) : mAdvanced(advanced) {
        mInfo = UpgradeInfo{
            .id           = advanced ? "advanced_restock" : "restock",
            .itemId       = advanced ? "alleviatedbackpacks:advanced_restock_upgrade" : "alleviatedbackpacks:restock_upgrade",
            .nameKey      = advanced ? "abp.upgrade.advanced_restock.name" : "abp.upgrade.restock.name",
            .nameFallback = advanced ? "Advanced Restock Upgrade" : "Restock Upgrade",
            .tier         = advanced ? UpgradeTier::Advanced : UpgradeTier::Basic,
            .group        = "restock",
            .hasSettings  = true,
        };
    }
    [[nodiscard]] UpgradeInfo const& info() const override { return mInfo; }
    [[nodiscard]] std::shared_ptr<ui::Menu> createSettingsMenu(Player& player, UpgradeContext const& ctx) override {
        return transfer::settingsMenu(player, ctx, mAdvanced, false);
    }
    [[nodiscard]] std::vector<std::string> describe(Player& player, UpgradeContext const& ctx) const override {
        return transfer::describe(player, ctx, mAdvanced, false);
    }

private:
    bool        mAdvanced;
    UpgradeInfo mInfo;
};

class AdvancedRestockUpgrade : public RestockUpgrade {
public:
    AdvancedRestockUpgrade() : RestockUpgrade(true) {}
};

} // namespace
} // namespace abp::upgrade::restock

ABP_REGISTER_UPGRADE(abp::upgrade::restock::RestockUpgrade);
ABP_REGISTER_UPGRADE(abp::upgrade::restock::AdvancedRestockUpgrade);

// ---- selftests -----------------------------------------------------------------------------------

namespace abp::upgrade::restock {
namespace {

using selftest::Context;
using namespace transfer::test;

ABP_REGISTER_SELFTEST("restock.basic_filter", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    co_await settle(ctx, player);
    auto  bp     = ctx.createBackpack(core::Tier::Leather);
    int   slot   = ctx.installUpgrade(bp, "restock");
    UpgradeContext uc(bp, slot, &player);
    PlacedBlock    chest(ctx, player, "minecraft:chest");
    auto&          c = chest.container();

    c.setItem(0, game::ItemConv::make("minecraft:torch", 64));
    c.setItem(3, game::ItemConv::make("minecraft:torch", 10));
    c.setItem(7, game::ItemConv::make("minecraft:bread", 12));
    c.setItem(20, game::ItemConv::make("minecraft:dirt", 64));

    setFilter(uc, false, {"minecraft:torch"}, core::FilterMode::Allow);
    auto r = run(uc, c, kUp, false);
    ctx.check(r.stacks == 2 && r.items == 74, "allow list: 2 torch stacks, got " + describeResult(r));
    ctx.check(countIn(*bp, "minecraft:torch") == 74 && countIn(c, "minecraft:torch") == 0, "torches moved");
    ctx.check(countIn(c, "minecraft:bread") == 12, "bread stays");

    setFilter(uc, false, {"minecraft:dirt"}, core::FilterMode::Deny);
    r = run(uc, c, kUp, false);
    ctx.check(r.stacks == 1 && countIn(*bp, "minecraft:bread") == 12, "deny list: bread moved");
    ctx.check(countIn(c, "minecraft:dirt") == 64, "denied dirt stays");
});

ABP_REGISTER_SELFTEST("restock.backpack_full", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    co_await settle(ctx, player);
    auto  bp     = ctx.createBackpack(core::Tier::Leather); // 27 slots
    int   slot   = ctx.installUpgrade(bp, "restock");
    UpgradeContext uc(bp, slot, &player);
    PlacedBlock    chest(ctx, player, "minecraft:chest");
    auto&          c = chest.container();

    fill(*bp, "minecraft:cobblestone", 26 * 64);
    fill(*bp, "minecraft:stone", 50); // last slot: 14 more stone fit
    c.setItem(0, game::ItemConv::make("minecraft:stone", 64));
    c.setItem(1, game::ItemConv::make("minecraft:dirt", 64));
    long long before = countIn(*bp, "minecraft:stone") + countIn(c, "minecraft:stone");
    auto      r      = run(uc, c, kUp, false);
    ctx.check(r.stacks == 1 && r.items == 14, "only what fits: " + describeResult(r));
    ctx.check(c.getItem(0).mCount == 50, "50 stone left in the chest");
    ctx.check(countIn(*bp, "minecraft:stone") + countIn(c, "minecraft:stone") == before, "nothing lost or duplicated");
    ctx.check(countIn(c, "minecraft:dirt") == 64, "dirt doesn't fit");
});

ABP_REGISTER_SELFTEST("restock.advanced_contents_mode", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    co_await settle(ctx, player);
    auto  bp     = ctx.createBackpack(core::Tier::Leather);
    int   slot   = ctx.installUpgrade(bp, "advanced_restock");
    UpgradeContext uc(bp, slot, &player);
    PlacedBlock    barrel(ctx, player, "minecraft:barrel");
    auto&          c = barrel.container();

    fill(*bp, "minecraft:arrow", 1);
    c.setItem(0, game::ItemConv::make("minecraft:arrow", 64));
    c.setItem(1, game::ItemConv::make("minecraft:string", 64));
    uc.settings()["contents"] = true;
    auto r = run(uc, c, kUp, true);
    ctx.check(r.stacks == 1 && countIn(*bp, "minecraft:arrow") == 65, "only items already in the backpack: " + describeResult(r));
    ctx.check(countIn(c, "minecraft:string") == 64, "string stays");

    // Tag matching (advanced): every item tagged like the ghost's tags.
    uc.settings()["contents"] = false;
    core::Filter f(16);
    f.matchBy = core::MatchBy::Tag;
    f.tags    = {"minecraft:logs"};
    uc.settings()["filter"] = f.toJson();
    c.setItem(2, game::ItemConv::make("minecraft:oak_log", 5));
    c.setItem(3, game::ItemConv::make("minecraft:birch_log", 7));
    r = run(uc, c, kUp, true);
    ctx.check(countIn(*bp, "minecraft:oak_log") == 5 && countIn(*bp, "minecraft:birch_log") == 7, "logs by tag: " + describeResult(r));
    ctx.check(countIn(c, "minecraft:string") == 64, "string has no logs tag");
});

ABP_REGISTER_SELFTEST("restock.settings_menu", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await settingsMenuTest(ctx, "advanced_restock");
});

} // namespace
} // namespace abp::upgrade::restock
