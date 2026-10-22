// Void upgrade selftests: `/backpack selftest "void.*"`.

#include <memory>

#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/PlayerUISlot.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "api/BackpackCapabilities.h"
#include "game/ItemConv.h"
#include "mod/Config.h"
#include "net/NativeScreens.h"
#include "selftest/SelfTest.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/void/VoidUpgrade.h"

namespace abp::upgrade::voiding {
namespace {

using core::Tier;
using selftest::Context;
using Where  = ui::MenuManager::SlotAddress::Where;
using Action = ui::MenuManager::ActionType;

core::AccessContext const kPickup{core::Source::Pickup};
core::AccessContext const kAutomation{core::Source::Automation};
core::AccessContext const kPlayer{core::Source::Player};
core::AccessContext const kInternal{core::Source::Internal};

core::ItemStackData data(std::string_view id, int count) { return game::ItemConv::toData(game::ItemConv::make(id, count)); }

int countOf(game::Backpack& bp, std::string_view id) {
    return static_cast<int>(bp.inventory().count([&](core::ItemStackData const& s) { return s.id == id; }));
}

int slotsWith(game::Backpack& bp, std::string_view id) {
    int n = 0;
    for (int i = 0; i < bp.inventory().size(); ++i) n += bp.inventory().at(i).id == id ? 1 : 0;
    return n;
}

/// Installs `id` and configures it: filter mode + ghost items, void type, work in GUI.
UpgradeContext setup(
    Context&                               ctx,
    std::shared_ptr<game::Backpack> const& bp,
    std::string const&                     id,
    std::vector<std::string_view> const&   ghosts,
    core::FilterMode                       mode      = core::FilterMode::Allow,
    VoidType                               type      = VoidType::Always,
    bool                                   workInGui = false
) {
    int            slot = ctx.installUpgrade(bp, id);
    UpgradeContext uc(bp, slot);
    core::Filter   f(tierConfig(id == kAdvancedId).filterSlots);
    f.mode = mode;
    for (int i = 0; i < static_cast<int>(ghosts.size()); ++i) f.setGhost(i, data(ghosts[static_cast<std::size_t>(i)], 1));
    uc.settings()[std::string(kFilterKey)]    = f.toJson();
    uc.settings()[std::string(kVoidTypeKey)]  = std::string(toString(type));
    uc.settings()[std::string(kWorkInGuiKey)] = workInGui;
    uc.saveSettings(true);
    return uc;
}

/// The native tab data / actions registered for `group` (what the client tab gets and sends).
nlohmann::json tabData(Player& p, UpgradeContext const& ctx, std::string_view group) {
    auto out = nlohmann::json::object();
    for (auto const& e : net::TabDataRegistry::entries()) {
        if (e.upgrade == group && e.data) out.update(e.data(p, ctx));
    }
    return out;
}
bool tabAction(Player& p, UpgradeContext const& ctx, std::string_view group, nlohmann::json const& data) {
    for (auto const& h : net::TabActionRegistry::entries()) {
        if (h.upgrade == group && h.handle && h.handle(p, ctx, data)) return true;
    }
    return false;
}
void setCursor(Player& p, ItemStack const& item) { p.setPlayerUIItem(PlayerUISlot::CursorSelected, item, false); }

ABP_REGISTER_SELFTEST("void.defaults_and_config", [](Context& ctx) {
    auto const& c = config::get<VoidConfig>();
    ctx.check(c.voidUpgrade.filterSlots == 9 && c.voidUpgrade.slotsInRow == 3 && c.voidUpgrade.voidAlwaysEnabled, "basic config = SB");
    ctx.check(c.advancedVoidUpgrade.filterSlots == 16 && c.advancedVoidUpgrade.slotsInRow == 4, "advanced config = SB");
    auto bp   = ctx.createBackpack(Tier::Iron);
    int  slot = ctx.installUpgrade(bp, "void");
    auto s    = VoidSettings::read(UpgradeContext(bp, slot));
    ctx.check(s.filter.mode == core::FilterMode::Allow && s.type() == VoidType::Always && !s.workInGui, "SB defaults: allow list, ALWAYS, not in GUI");
    auto r = bp->inventory().insert(data("minecraft:stone", 10), kPickup);
    ctx.check(r.inserted == 10 && r.voided == 0, "empty allow list voids nothing");
    ctx.check(canInstall(bp, 1, data("alleviatedbackpacks:advanced_void_upgrade", 1), nullptr), "SB: no void limit");

    auto deny = ctx.createBackpack(Tier::Leather);
    setup(ctx, deny, "void", {}, core::FilterMode::Deny);
    r = deny->inventory().insert(data("minecraft:stone", 10), kPickup);
    ctx.check(r.voided == 10, "SC: an empty block list voids everything");
});

ABP_REGISTER_SELFTEST("void.always", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(Tier::Iron); // 2 upgrade slots
    setup(ctx, bp, "void", {"minecraft:cobblestone"});
    auto r = bp->inventory().insert(data("minecraft:cobblestone", 5), kAutomation);
    ctx.check(r.voided == 5 && r.inserted == 0, "automation insert voided");
    r = bp->inventory().insert(data("minecraft:cobblestone", 5), kPickup);
    ctx.check(r.voided == 5, "pickup insert voided");
    r = bp->inventory().insert(data("minecraft:cobblestone", 5), kInternal);
    ctx.check(r.inserted == 5 && r.voided == 0, "internal moves are never voided");
    r = bp->inventory().insert(data("minecraft:dirt", 7), kPickup);
    ctx.check(r.inserted == 7, "non-matching item stored");
    r = bp->inventory().insert(data("minecraft:cobblestone", 4), kPlayer);
    ctx.check(r.inserted == 4 && r.voided == 0, "GUI insert kept (work in GUI off)");

    // Without a Pickup upgrade nothing is picked up into the backpack (SB), so nothing is voided.
    ctx.require(ctx.give(player, ctx.backpackItem(*bp), 3) == 3, "give backpack");
    co_await ctx.ticks(2);
    auto cobble = data("minecraft:cobblestone", 20);
    ctx.check(upgrade::dispatchPickup(player, cobble) == 0 && cobble.count == 20, "no pickup without a Pickup upgrade");
    ctx.installUpgrade(bp, "pickup");
    ctx.check(upgrade::dispatchPickup(player, cobble) == 20, "with a Pickup upgrade the picked up items are voided");
    ctx.check(countOf(*bp, "minecraft:cobblestone") == 9, "voided items not stored");
});

ABP_REGISTER_SELFTEST("void.always_in_gui_next_tick", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto bp = ctx.createBackpack(Tier::Leather);
    setup(ctx, bp, "void", {"minecraft:cobblestone"}, core::FilterMode::Allow, VoidType::Always, true);
    auto r = bp->inventory().insertInto(3, data("minecraft:cobblestone", 12), kPlayer);
    ctx.check(r.inserted == 12, "placed in the slot first");
    co_await ctx.ticks(2);
    ctx.check(bp->inventory().at(3).empty(), "voided on the next tick");
    bp->inventory().insertInto(4, data("minecraft:dirt", 12), kPlayer);
    co_await ctx.ticks(2);
    ctx.check(countOf(*bp, "minecraft:dirt") == 12, "other items stay");
});

ABP_REGISTER_SELFTEST("void.slot_overflow", [](Context& ctx) {
    auto bp = ctx.createBackpack(Tier::Leather);
    setup(ctx, bp, "advanced_void", {"minecraft:cobblestone"}, core::FilterMode::Allow, VoidType::SlotOverflow);
    auto r = bp->inventory().insert(data("minecraft:cobblestone", 100), kPickup);
    ctx.check(r.inserted == 64 && r.voided == 36, "one slot filled, the overflow voided");
    ctx.check(slotsWith(*bp, "minecraft:cobblestone") == 1, "no second stack");
    r = bp->inventory().insert(data("minecraft:cobblestone", 10), kPickup);
    ctx.check(r.voided == 10, "full stack: everything voided");
    r = bp->inventory().insert(data("minecraft:stone", 100), kPickup);
    ctx.check(r.inserted == 100 && slotsWith(*bp, "minecraft:stone") == 2, "non-matching items fill normally");
    // A partial stack is topped up, the rest voided.
    auto bp2 = ctx.createBackpack(Tier::Leather);
    setup(ctx, bp2, "void", {"minecraft:cobblestone"}, core::FilterMode::Allow, VoidType::SlotOverflow);
    bp2->inventory().set(5, data("minecraft:cobblestone", 30));
    r = bp2->inventory().insert(data("minecraft:cobblestone", 50), kPickup);
    ctx.check(r.inserted == 34 && r.voided == 16 && slotsWith(*bp2, "minecraft:cobblestone") == 1, "topped up to 64, 16 voided");
});

ABP_REGISTER_SELFTEST("void.storage_overflow", [](Context& ctx) {
    auto bp = ctx.createBackpack(Tier::Leather);
    setup(ctx, bp, "void", {"minecraft:cobblestone"}, core::FilterMode::Allow, VoidType::StorageOverflow);
    auto r = bp->inventory().insert(data("minecraft:cobblestone", 100), kPickup);
    ctx.check(r.inserted == 100 && slotsWith(*bp, "minecraft:cobblestone") == 2, "storage fills normally");
    auto& inv = bp->inventory();
    for (int i = 0; i < inv.size(); ++i) {
        if (inv.at(i).empty()) inv.set(i, data("minecraft:dirt", 64));
    }
    r = inv.insert(data("minecraft:cobblestone", 50), kPickup);
    ctx.check(r.inserted == 28 && r.voided == 22, "full storage: the part that fits goes in, the rest is voided");
    r = inv.insert(data("minecraft:sand", 5), kPickup);
    ctx.check(r.inserted == 0 && r.voided == 0 && r.remainder.count == 5, "non-matching items bounce");
});

ABP_REGISTER_SELFTEST("void.gui_slot_overflow", [](Context& ctx) {
    auto bp = ctx.createBackpack(Tier::Leather);
    setup(ctx, bp, "void", {"minecraft:cobblestone"}, core::FilterMode::Allow, VoidType::SlotOverflow, true);
    auto& inv = bp->inventory();
    auto  r   = inv.insertInto(0, data("minecraft:cobblestone", 60), kPlayer);
    ctx.check(r.inserted == 60, "first stack placed by hand");
    r = inv.insertInto(0, data("minecraft:cobblestone", 10), kPlayer);
    ctx.check(r.inserted == 4 && r.voided == 6, "what overflows the clicked slot is voided");
    r = inv.insertInto(1, data("minecraft:cobblestone", 10), kPlayer);
    ctx.check(r.voided == 10 && inv.at(1).empty(), "no second stack by hand either");
    // Work in GUI off: the GUI behaves normally.
    UpgradeContext uc(bp, 0);
    uc.settings()[std::string(kWorkInGuiKey)] = false;
    uc.saveSettings(true);
    r = inv.insertInto(1, data("minecraft:cobblestone", 10), kPlayer);
    ctx.check(r.inserted == 10, "work in GUI off: kept");
});

ABP_REGISTER_SELFTEST("void.void_always_disabled", [](Context& ctx) {
    auto saved = config::get<VoidConfig>();
    config::mut<VoidConfig>().voidUpgrade.voidAlwaysEnabled = false;
    auto bp = ctx.createBackpack(Tier::Leather);
    auto uc = setup(ctx, bp, "void", {"minecraft:cobblestone"});
    auto s  = VoidSettings::read(uc);
    ctx.check(s.type() == VoidType::SlotOverflow, "ALWAYS acts as SLOT_OVERFLOW");
    ctx.check(next(VoidType::StorageOverflow, false) == VoidType::SlotOverflow, "cycling skips ALWAYS");
    auto r = bp->inventory().insert(data("minecraft:cobblestone", 70), kPickup);
    ctx.check(r.inserted == 64 && r.voided == 6, "slot overflow behaviour");
    config::mut<VoidConfig>() = saved;
});

ABP_REGISTER_SELFTEST("void.fluids", [](Context& ctx) {
    auto bp = ctx.createBackpack(Tier::Iron);
    setup(ctx, bp, "void", {"minecraft:water_bucket"});
    ctx.installUpgrade(bp, "tank");
    auto h = api::backpack::fluidHandler(bp);
    ctx.require(h != nullptr, "fluid handler");
    ctx.check(containedFluid(data("minecraft:water_bucket", 1)).id == "minecraft:water", "bucket stands for water");
    int filled = h->fill(api::FluidStack{"minecraft:water", 1000}, false);
    ctx.check(filled == 1000 && h->fluidInTank(0).empty(), "water voided (accepted, tank empty)");
    filled = h->fill(api::FluidStack{"minecraft:lava", 1000}, false);
    ctx.check(filled == 1000 && h->fluidInTank(0).id == "minecraft:lava", "lava stored");
    // Block list: everything but water.
    auto bp2 = ctx.createBackpack(Tier::Iron);
    setup(ctx, bp2, "void", {"minecraft:water_bucket"}, core::FilterMode::Deny);
    ctx.installUpgrade(bp2, "tank");
    auto h2 = api::backpack::fluidHandler(bp2);
    h2->fill(api::FluidStack{"minecraft:lava", 1000}, false);
    ctx.check(h2->fluidInTank(0).empty(), "block list voids lava");
    h2->fill(api::FluidStack{"minecraft:water", 1000}, false);
    ctx.check(h2->fluidInTank(0).id == "minecraft:water", "block list keeps water");
    // Storage overflow: fills the tank, voids what does not fit.
    auto bp3 = ctx.createBackpack(Tier::Iron);
    setup(ctx, bp3, "void", {"minecraft:water_bucket"}, core::FilterMode::Allow, VoidType::StorageOverflow);
    ctx.installUpgrade(bp3, "tank");
    auto h3  = api::backpack::fluidHandler(bp3);
    int  cap = h3->tankCapacity(0);
    filled   = h3->fill(api::FluidStack{"minecraft:water", cap + 500}, false);
    ctx.check(filled == cap + 500 && h3->fluidInTank(0).amount == cap, "tank full, overflow voided");
});

ABP_REGISTER_SELFTEST("void.settings_menu", [](Context& ctx) {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(Tier::Leather);
    int   slot   = ctx.installUpgrade(bp, std::string(kAdvancedId));
    UpgradeContext uc(bp, slot, &player);
    auto           menu = byId(kAdvancedId)->createSettingsMenu(player, uc);
    ctx.require(menu && mm.open(player, menu), "open");
    mm.forceShown(player);
    int control = menu->size() - 9;
    (void)mm.simulate(player, {Action::Take, {Where::Menu, control + 5}, {Where::Cursor, 0}, 1});
    ctx.check(VoidSettings::read(uc).workInGui, "work in GUI toggled");
    (void)mm.simulate(player, {Action::Take, {Where::Menu, control + 6}, {Where::Cursor, 0}, 1});
    ctx.check(VoidSettings::read(uc).type() == VoidType::SlotOverflow, "ALWAYS -> SLOT_OVERFLOW");
    (void)mm.simulate(player, {Action::Take, {Where::Menu, control + 6}, {Where::Cursor, 0}, 1});
    ctx.check(VoidSettings::read(uc).type() == VoidType::StorageOverflow, "-> STORAGE_OVERFLOW");
    (void)mm.simulate(player, {Action::Take, {Where::Menu, control + 6}, {Where::Cursor, 0}, 1});
    ctx.check(VoidSettings::read(uc).type() == VoidType::Always, "-> ALWAYS");
    mm.close(player);
});

ABP_REGISTER_SELFTEST("void.native_tab", [](Context& ctx) {
    auto& p  = ctx.player();
    auto  bp = ctx.createBackpack(Tier::Leather);
    int   b  = ctx.installUpgrade(bp, "void");
    UpgradeContext uc(bp, b, &p);
    auto           t = tabData(p, uc, "void");
    ctx.check(t.value("title", "") == "Void" && t.value("tooltip", "") == "Void Settings", "SB tab texts");
    ctx.check(!t.value("workInGui", true) && t.value("voidType", -1) == 0 && t.value("voidAlwaysEnabled", false), "defaults");
    ctx.check(t["filter"].value("type", "") == "allowList" && t["filter"].value("typeState", -1) == 0, "allow list by default");
    ctx.check(tabAction(p, uc, "void", {{"op", "workInGui"}}) && VoidSettings::read(uc).workInGui, "work in GUI");
    ctx.check(tabAction(p, uc, "void", {{"op", "voidType"}}) && VoidSettings::read(uc).type() == VoidType::SlotOverflow, "-> slot overflow");
    auto saved = config::get<VoidConfig>();
    config::mut<VoidConfig>().voidUpgrade.voidAlwaysEnabled = false;
    tabAction(p, uc, "void", {{"op", "voidType"}}); // -> storage overflow
    tabAction(p, uc, "void", {{"op", "voidType"}}); // skips ALWAYS
    ctx.check(VoidSettings::read(uc).type() == VoidType::SlotOverflow, "ALWAYS skipped when disabled");
    ctx.check(!tabData(p, uc, "void").value("voidAlwaysEnabled", true), "client told ALWAYS is disabled");
    config::mut<VoidConfig>() = saved;
});

} // namespace
} // namespace abp::upgrade::voiding
