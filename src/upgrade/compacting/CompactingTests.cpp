// Compacting upgrade selftests: `/backpack selftest "compacting.*"`.

#include <memory>
#include <string>

#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/PlayerUISlot.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "game/ItemConv.h"
#include "mod/Config.h"
#include "net/NativeScreens.h"
#include "selftest/SelfTest.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/compacting/Compacting.h"

namespace abp::upgrade::compacting {
namespace {

using selftest::Context;
using Where  = ui::MenuManager::SlotAddress::Where;
using Action = ui::MenuManager::ActionType;

core::AccessContext const kPickup{core::Source::Pickup};
core::AccessContext const kPlayer{core::Source::Player};

core::ItemStackData data(std::string_view id, int count) { return game::ItemConv::toData(game::ItemConv::make(id, count)); }

int countOf(game::Backpack& bp, std::string_view id) {
    return static_cast<int>(bp.inventory().count([&](core::ItemStackData const& s) { return s.id == id; }));
}

std::string counts(game::Backpack& bp, std::initializer_list<std::string_view> ids) {
    std::string out;
    for (auto id : ids) out += std::string(id) + "=" + std::to_string(countOf(bp, id)) + " ";
    return out;
}

void setSetting(std::shared_ptr<game::Backpack> const& bp, int slot, std::string_view key, nlohmann::json value) {
    UpgradeContext ctx(bp, slot);
    ctx.settings()[std::string(key)] = std::move(value);
    ctx.saveSettings(true);
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

ABP_REGISTER_SELFTEST("compacting.recipe_lookup", [](Context& ctx) {
    invalidateCache();
    struct Case {
        std::string_view item;
        int              grid;
        std::string_view result;
        bool             reversible;
    };
    Case const cases[] = {
        {"minecraft:iron_ingot",  3, "minecraft:iron_block",     true },
        {"minecraft:iron_nugget", 3, "minecraft:iron_ingot",     true },
        {"minecraft:gold_nugget", 3, "minecraft:gold_ingot",     true },
        {"minecraft:wheat",       3, "minecraft:hay_block",      true },
        {"minecraft:redstone",    3, "minecraft:redstone_block", true },
        {"minecraft:quartz",      2, "minecraft:quartz_block",   false},
    };
    for (auto const& c : cases) {
        std::string name(c.item);
        auto        found = findCompaction(data(c.item, 1), c.grid);
        if (!found) {
            ctx.check(false, name + ": no compaction found");
            continue;
        }
        ctx.check(found->result.id == c.result, name + " -> " + found->result.id);
        ctx.check(found->inputs == c.grid * c.grid, name + ": inputs");
        ctx.check(found->reversible == c.reversible, name + ": reversible flag");
    }
    auto shapes = compactingShapes(data("minecraft:iron_ingot", 1));
    ctx.check(shapes.threeByThree && shapes.threeByThreeUncraftable, "iron ingot: 3x3 uncraftable");
    ctx.check(shapes.twoByTwo && !shapes.twoByTwoUncraftable, "iron ingot: 2x2 (trapdoor) craftable only");
    ctx.check(!findCompaction(data("minecraft:dirt", 1), 3).has_value(), "no 3x3 recipe for dirt");
    auto named = game::ItemConv::make("minecraft:iron_ingot", 1);
    game::ItemConv::setDisplay(named, "Special");
    ctx.check(!findCompaction(game::ItemConv::toData(named), 3).has_value(), "named items are skipped");
    ctx.check(Shape::parse("111/101/111").has_value() && Shape::parse("111/101/111")->count == 8, "default additional shape parses");
    auto const& cfg = config::get<CompactingConfig>();
    ctx.check(cfg.compactingUpgrade.additionalCompactingShapes == std::vector<std::string>{"111/101/111"}, "SB default shapes");
    ctx.check(cfg.compactingUpgrade.filterSlots == 9 && cfg.advancedCompactingUpgrade.filterSlots == 16, "SB filter slots");
});

// GX-2: SC returns the recipe's remaining items (honey bottles -> glass bottles).
ABP_REGISTER_SELFTEST("compacting.recipe_remainders", [](Context& ctx) {
    invalidateCache();
    auto honey = findCompaction(data("minecraft:honey_bottle", 1), 2);
    ctx.require(honey.has_value(), "4 honey bottles craft a honey block (2x2)");
    ctx.check(honey->result.id == "minecraft:honey_block", "-> " + honey->result.id);
    int bottles = 0;
    for (auto const& r : honey->remaining) {
        ctx.info("remaining: " + r.id + " x" + std::to_string(r.count));
        if (r.id == "minecraft:glass_bottle") bottles += r.count;
    }
    ctx.check(bottles == 4, "one craft leaves 4 glass bottles (got " + std::to_string(bottles) + ")");
    auto iron = findCompaction(data("minecraft:iron_ingot", 1), 3);
    ctx.check(iron && iron->remaining.empty(), "iron block: no remainders");

    // In a backpack (honey blocks don't uncraft into bottles: Compact Anything).
    auto bp   = ctx.createBackpack(core::Tier::Iron);
    int  slot = ctx.installUpgrade(bp, "compacting");
    setSetting(bp, slot, kCompactAnythingKey, true);
    (void)bp->inventory().insert(data("minecraft:honey_bottle", 9), kPickup);
    ctx.check(countOf(*bp, "minecraft:honey_block") == 2 && countOf(*bp, "minecraft:honey_bottle") == 1
                  && countOf(*bp, "minecraft:glass_bottle") == 8,
              "2 blocks, 1 bottle left, 8 glass bottles back: "
                  + counts(*bp, {"minecraft:honey_block", "minecraft:honey_bottle", "minecraft:glass_bottle"}));

    // No room for the remainders: nothing is compacted (SC fitsResultAndRemainingItems).
    auto full = ctx.createBackpack(core::Tier::Leather); // 27 slots
    int  s2   = ctx.installUpgrade(full, "compacting");
    setSetting(full, s2, kCompactAnythingKey, true);
    for (int i = 0; i < 26; ++i) full->inventory().set(i, data("minecraft:dirt", 64).withCount(64));
    full->inventory().set(26, data("minecraft:honey_bottle", 3));
    (void)full->inventory().insert(data("minecraft:honey_bottle", 1), kPickup);
    ctx.check(countOf(*full, "minecraft:honey_block") == 0 && countOf(*full, "minecraft:honey_bottle") == 4,
              "no room for the glass bottles: not compacted: " + counts(*full, {"minecraft:honey_block", "minecraft:honey_bottle"}));
});

ABP_REGISTER_SELFTEST("compacting.advanced_after_insert", [](Context& ctx) {
    auto  bp   = ctx.createBackpack(core::Tier::Iron);
    int   slot = ctx.installUpgrade(bp, "advanced_compacting");
    auto& inv  = bp->inventory();
    auto  r    = inv.insert(data("minecraft:iron_ingot", 64), kPickup);
    ctx.check(r.inserted == 64 && r.remainder.empty(), "insert reports all 64 taken");
    ctx.check(countOf(*bp, "minecraft:iron_block") == 7 && countOf(*bp, "minecraft:iron_ingot") == 1,
              "compacted right after the insert: " + counts(*bp, {"minecraft:iron_block", "minecraft:iron_ingot"}));
    (void)inv.insert(data("minecraft:gold_nugget", 64), kPickup);
    (void)inv.insert(data("minecraft:gold_nugget", 17), kPickup);
    ctx.check(countOf(*bp, "minecraft:gold_block") == 1 && countOf(*bp, "minecraft:gold_nugget") == 0
                  && countOf(*bp, "minecraft:gold_ingot") == 0,
              "nuggets chain into a block: " + counts(*bp, {"minecraft:gold_nugget", "minecraft:gold_ingot", "minecraft:gold_block"}));
    (void)inv.insert(data("minecraft:quartz", 5), kPickup);
    ctx.check(countOf(*bp, "minecraft:quartz") == 5, "quartz kept: its block can't be uncrafted");
    setSetting(bp, slot, kCompactAnythingKey, true);
    (void)inv.insert(data("minecraft:quartz", 3), kPickup);
    ctx.check(countOf(*bp, "minecraft:quartz_block") == 2 && countOf(*bp, "minecraft:quartz") == 0,
              "Compact Anything: " + counts(*bp, {"minecraft:quartz", "minecraft:quartz_block"}));
    (void)inv.insert(data("minecraft:iron_ingot", 12), kPickup);
    // SC picks one shape per item: ingots always use their 3x3 recipe, the 4 left over don't
    // become an iron trapdoor (2x2).
    ctx.check(countOf(*bp, "minecraft:iron_block") == 8 && countOf(*bp, "minecraft:iron_ingot") == 4
                  && countOf(*bp, "minecraft:iron_trapdoor") == 0,
              "3x3 shape wins: " + counts(*bp, {"minecraft:iron_ingot", "minecraft:iron_block", "minecraft:iron_trapdoor"}));
    upgrade::setEnabled(bp, slot, false);
    (void)inv.insert(data("minecraft:wheat", 9), kPickup);
    ctx.check(countOf(*bp, "minecraft:wheat") == 9, "disabled upgrade leaves wheat alone");
});

ABP_REGISTER_SELFTEST("compacting.basic_2x2_only", [](Context& ctx) {
    auto  bp   = ctx.createBackpack(core::Tier::Iron);
    int   slot = ctx.installUpgrade(bp, "compacting");
    auto& inv  = bp->inventory();
    (void)inv.insert(data("minecraft:iron_ingot", 27), kPickup);
    ctx.check(countOf(*bp, "minecraft:iron_ingot") == 27, "basic ignores 3x3; trapdoors aren't uncraftable");
    setSetting(bp, slot, kCompactAnythingKey, true);
    (void)inv.insert(data("minecraft:iron_ingot", 1), kPickup);
    ctx.check(countOf(*bp, "minecraft:iron_trapdoor") == 7 && countOf(*bp, "minecraft:iron_ingot") == 0,
              "Compact Anything 2x2: " + counts(*bp, {"minecraft:iron_ingot", "minecraft:iron_trapdoor"}));
    ctx.check(canInstall(bp, 1, data("alleviatedbackpacks:advanced_compacting_upgrade", 1), nullptr), "SB: no compacting limit");
});

ABP_REGISTER_SELFTEST("compacting.gui_only_with_work_in_gui", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  bp   = ctx.createBackpack(core::Tier::Iron);
    int   slot = ctx.installUpgrade(bp, "advanced_compacting");
    auto& inv  = bp->inventory();
    (void)inv.insertInto(4, data("minecraft:iron_ingot", 18), kPlayer);
    co_await ctx.ticks(2);
    ctx.check(countOf(*bp, "minecraft:iron_ingot") == 18, "GUI insert not compacted by default");
    setSetting(bp, slot, kWorkInGuiKey, true);
    (void)inv.insertInto(4, data("minecraft:iron_ingot", 1), kPlayer);
    ctx.check(countOf(*bp, "minecraft:iron_ingot") == 19, "not right away");
    co_await ctx.ticks(2);
    ctx.check(countOf(*bp, "minecraft:iron_block") == 2 && countOf(*bp, "minecraft:iron_ingot") == 1,
              "Works in GUI: compacted on the next tick: " + counts(*bp, {"minecraft:iron_ingot", "minecraft:iron_block"}));
});

ABP_REGISTER_SELFTEST("compacting.full_slot_retried_after_extract", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  bp  = ctx.createBackpack(core::Tier::Leather); // 27 slots
    auto& inv = bp->inventory();
    for (int i = 0; i < 25; ++i) inv.set(i, data("minecraft:dirt", 64));
    inv.set(25, data("minecraft:iron_ingot", 64));
    inv.set(26, data("minecraft:iron_block", 64));
    ctx.installUpgrade(bp, "advanced_compacting");
    ctx.check(countOf(*bp, "minecraft:iron_ingot") == 64, "no room for blocks: nothing happens");
    auto taken = inv.extract(26, 1, kPlayer);
    ctx.check(taken.count == 1, "a block taken out");
    co_await ctx.ticks(2);
    ctx.check(countOf(*bp, "minecraft:iron_block") == 64 && countOf(*bp, "minecraft:iron_ingot") == 55,
              "the full ingot slot compacts into the freed room: " + counts(*bp, {"minecraft:iron_ingot", "minecraft:iron_block"}));
});

ABP_REGISTER_SELFTEST("compacting.filter", [](Context& ctx) {
    auto  bp   = ctx.createBackpack(core::Tier::Iron);
    int   slot = ctx.installUpgrade(bp, "advanced_compacting");
    auto& inv  = bp->inventory();
    core::Filter f(16);
    f.mode = core::FilterMode::Allow;
    f.setGhost(0, data("minecraft:wheat", 1));
    setSetting(bp, slot, kFilterKey, f.toJson());
    (void)inv.insert(data("minecraft:iron_ingot", 9), kPickup);
    (void)inv.insert(data("minecraft:wheat", 9), kPickup);
    ctx.check(countOf(*bp, "minecraft:iron_ingot") == 9, "allow list: ingots not listed, not compacted");
    ctx.check(countOf(*bp, "minecraft:hay_block") == 1, "allow list: wheat compacted");
    f.mode = core::FilterMode::Deny;
    setSetting(bp, slot, kFilterKey, f.toJson());
    (void)inv.insert(data("minecraft:wheat", 9), kPickup);
    ctx.check(countOf(*bp, "minecraft:wheat") == 9, "block list: wheat kept");
});

ABP_REGISTER_SELFTEST("compacting.settings_menu", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(core::Tier::Iron);
    int   slot   = ctx.installUpgrade(bp, "advanced_compacting");
    auto* type   = upgrade::byId("advanced_compacting");
    ctx.require(type && type->info().hasSettings, "has settings");
    UpgradeContext uctx(bp, slot, &player);
    auto           menu = type->createSettingsMenu(player, uctx);
    ctx.require(menu && mm.open(player, menu), "open settings");
    mm.forceShown(player);
    co_await ctx.ticks(2);
    auto settings = [&] { return CompactingSettings::read(bp->data().upgrades[static_cast<std::size_t>(slot)].settings, true); };
    int  control  = menu->size() - 9;
    ctx.check(!settings().compactAnything && !settings().workInGui, "defaults: only uncraftable, not in GUI");
    (void)mm.simulate(player, {Action::Take, {Where::Menu, control + 5}, {Where::Cursor, 0}, 1});
    ctx.check(settings().compactAnything, "Compact Anything");
    (void)mm.simulate(player, {Action::Take, {Where::Menu, control + 6}, {Where::Cursor, 0}, 1});
    ctx.check(settings().workInGui, "Works in GUI as well");
    mm.close(player);
});

ABP_REGISTER_SELFTEST("compacting.native_tab", [](Context& ctx) {
    auto& p  = ctx.player();
    auto  bp = ctx.createBackpack(core::Tier::Iron);
    int   b  = ctx.installUpgrade(bp, "compacting");
    int   a  = ctx.installUpgrade(bp, "advanced_compacting");
    UpgradeContext basic(bp, b, &p), adv(bp, a, &p);
    auto           t = tabData(p, basic, "compacting");
    ctx.check(t.value("title", "") == "Compa..." && t.value("tooltip", "") == "Compacting Settings", "SB tab texts: " + t.value("title", ""));
    ctx.check(tabData(p, adv, "compacting").value("title", "") == "Adv. Compacting", "advanced title");
    ctx.check(!t.value("compactAnything", true) && !t.value("workInGui", true), "defaults");
    ctx.check(tabAction(p, basic, "compacting", {{"op", "compactAnything"}}), "toggle");
    ctx.check(CompactingSettings::read(basic.settings(), false).compactAnything, "Compact Anything stored");
    ctx.check(tabAction(p, basic, "compacting", {{"op", "workInGui"}}) && CompactingSettings::read(basic.settings(), false).workInGui, "work in GUI");
    setCursor(p, game::ItemConv::make("minecraft:dirt", 1));
    tabAction(p, adv, "compacting", {{"filter", "filter"}, {"op", "slot"}, {"index", 0}});
    ctx.check(CompactingSettings::read(adv.settings(), true).filter.ghosts[0].empty(), "items that can't compact are refused");
    setCursor(p, game::ItemConv::make("minecraft:iron_ingot", 1));
    tabAction(p, adv, "compacting", {{"filter", "filter"}, {"op", "slot"}, {"index", 0}});
    setCursor(p, ItemStack::EMPTY_ITEM());
    ctx.check(CompactingSettings::read(adv.settings(), true).filter.ghosts[0].id == "minecraft:iron_ingot", "compactable item accepted");
});

} // namespace
} // namespace abp::upgrade::compacting
