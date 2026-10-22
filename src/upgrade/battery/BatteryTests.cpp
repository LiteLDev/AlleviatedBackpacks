// Selftests of the Battery upgrade: battery.*
// Run with `/backpack selftest "battery.*"`.

#include "mc/world/item/ItemStack.h"

#include "api/BackpackCapabilities.h"
#include "api/Service.h"
#include "game/Backpack.h"
#include "game/ItemConv.h"
#include "net/NativeScreens.h"
#include "selftest/SelfTest.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/battery/Battery.h"
#include "upgrade/tank/MenuBits.h"

namespace abp::upgrade::battery {
namespace {

using selftest::Context;

core::ItemStackData itemOf(std::string_view id) { return game::ItemConv::toData(game::ItemConv::make(id)); }

/// Echo shards become 10000 FE energy items for the test (receive 500 / extract 400 per call).
class TestEnergyItem {
public:
    TestEnergyItem() {
        auto& caps = api::service();
        mToken     = caps.registerItemEnergy("minecraft:echo_shard", [&caps](ItemStack& stack) {
            return caps.makeItemEnergyStorage(stack, 10000, 500, 400);
        });
    }
    ~TestEnergyItem() {
        if (mToken) api::service().unregister(mToken);
    }
    [[nodiscard]] bool ok() const { return mToken != 0; }
    static core::ItemStackData charged(int fe) {
        auto stack = game::ItemConv::make("minecraft:echo_shard");
        auto e     = api::service().itemEnergy(stack);
        int  left  = fe;
        for (int i = 0; i < 100 && left > 0 && e; ++i) left -= e->receive(left, false);
        return game::ItemConv::toData(stack);
    }
    static int energyOf(core::ItemStackData const& d) {
        if (d.empty()) return -1;
        auto stack = game::ItemConv::toStack(d, 1);
        auto e     = api::service().itemEnergy(stack);
        return e ? e->stored() : -1;
    }

private:
    api::Token mToken = 0;
};

ABP_REGISTER_SELFTEST("battery.capacity_and_limits", [](Context& ctx) {
    auto leather = ctx.createBackpack(core::Tier::Leather);
    UpgradeContext b(leather, ctx.installUpgrade(leather, "battery"));
    ctx.check(capacity(*leather) == 30000, "leather: 3 rows x 10000 FE, got " + std::to_string(capacity(*leather)));
    ctx.check(maxInOut(*leather) == 60, "leather: 20 x 3 FE per operation, got " + std::to_string(maxInOut(*leather)));
    ctx.check(leather->inventory().size() == 21 && leather->data().columnsTaken == 2, "the battery takes 2 columns");
    auto gold = ctx.createBackpack(core::Tier::Gold);
    ctx.installUpgrade(gold, "battery");
    std::string reason;
    ctx.check(!upgrade::canInstall(gold, 1, itemOf(kItemId), nullptr, &reason), "a second battery is refused");
    ctx.check(reason.find("already Battery Upgrade") != std::string::npos, "SB message: " + reason);
    // Energy travels with the item; a backpack too small for it refuses it.
    UpgradeContext g(gold, 0);
    setStored(g, 80000);
    auto item = upgrade::remove(gold, 0, nullptr);
    ctx.require(item.has_value(), "remove the charged battery");
    auto small = ctx.createBackpack(core::Tier::Copper); // 5 rows: 50000 FE
    reason.clear();
    ctx.check(!upgrade::canInstall(small, 0, *item, nullptr, &reason), "too small for 80000 FE");
    ctx.check(reason.find("at least 1.6x higher max energy") != std::string::npos, "SB message with 1.6x: " + reason);
    ctx.check(b.backpack().uuid() == leather->uuid(), "context");
});

ABP_REGISTER_SELFTEST("battery.receive_extract", [](Context& ctx) {
    auto           bp = ctx.createBackpack(core::Tier::Leather);
    UpgradeContext b(bp, ctx.installUpgrade(bp, "battery"));
    ctx.check(receive(b, 1000, false) == 60, "receive is limited to 60 FE per operation");
    ctx.check(stored(b) == 60, "60 FE stored");
    ctx.check(extract(b, 1000, true) == 60 && stored(b) == 60, "simulated extract");
    auto e = api::backpack::energyStorage(bp);
    ctx.require(e != nullptr, "the backpack exposes its battery");
    ctx.check(e->capacity() == 30000 && e->stored() == 60, "API view of the battery");
    ctx.check(e->extract(10, false) == 10 && stored(b) == 50, "API extract");
    auto& caps = api::service();
    ctx.check(caps.backpackEnergy(bp->uuid()) != nullptr, "backpackEnergy by uuid");
});

ABP_REGISTER_SELFTEST("battery.charge_items", [](Context& ctx) {
    TestEnergyItem test;
    ctx.require(test.ok(), "register a test energy item");
    auto           bp = ctx.createBackpack(core::Tier::Leather);
    UpgradeContext b(bp, ctx.installUpgrade(bp, "battery"));
    // Input: a charged item is discharged into the battery (60 FE per tick).
    fluidui::setSlotItem(b, "in", TestEnergyItem::charged(1000));
    tick(b);
    ctx.check(stored(b) == 60, "60 FE from the input item, got " + std::to_string(stored(b)));
    ctx.check(TestEnergyItem::energyOf(fluidui::slotItem(b, "in")) == 940, "the input item lost 60 FE");
    // Output: an empty item is charged from the battery.
    fluidui::setSlotItem(b, "out", TestEnergyItem::charged(0));
    tick(b);
    ctx.check(TestEnergyItem::energyOf(fluidui::slotItem(b, "out")) == 60, "the output item got 60 FE");
    // Slots accept energy items only (input: with charge; output: any that can receive).
    fluidui::setSlotItem(b, "in", {});
    fluidui::setSlotItem(b, "out", {});
    auto menu = upgrade::byId(kId)->createSettingsMenu(ctx.player(), b);
    ctx.require(menu != nullptr, "battery slots menu");
    menu->ensureBuilt(ctx.player());
    ctx.check(menu->slot(0)->accept(ctx.player(), game::ItemConv::make("minecraft:stone")) == 0, "stone refused");
    ctx.check(menu->slot(0)->accept(ctx.player(), game::ItemConv::toStack(TestEnergyItem::charged(0), 1)) == 0, "an empty item can't be an input");
    ctx.check(menu->slot(1)->accept(ctx.player(), game::ItemConv::toStack(TestEnergyItem::charged(0), 1)) == 1, "an empty item is a valid output");
    ctx.check(menu->slot(1)->accept(ctx.player(), game::ItemConv::toStack(TestEnergyItem::charged(0), 1)) == 0, "the slot holds one item");
});

ABP_REGISTER_SELFTEST("battery.contents_and_render", [](Context& ctx) {
    auto&          player = ctx.player();
    auto           bp     = ctx.createBackpack(core::Tier::Leather);
    UpgradeContext b(bp, ctx.installUpgrade(bp, "battery"));
    setStored(b, 15000);
    auto contents = net::buildContents(player, bp);
    bool found    = false;
    for (auto const& l : contents.lines) found |= l.find("FE") != std::string::npos && l.find("15") != std::string::npos;
    ctx.check(found, "the contents tooltip shows the energy");
    auto info = net::buildRenderInfo(*bp);
    ctx.check(info.extra.contains("battery") && info.extra["battery"].value("chargeRatio", 0.f) == 0.5f, "charge ratio 0.5");
});

} // namespace
} // namespace abp::upgrade::battery
