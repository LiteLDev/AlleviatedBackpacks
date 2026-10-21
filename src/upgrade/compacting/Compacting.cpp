// Compacting upgrade (basic: 2x2 recipes; advanced: 3x3 and 2x2).
//
// Whenever items enter the backpack (any source: GUI, pickup, automation, other upgrades), the
// upgrade looks up the game's own crafting recipes for "n x n copies of this item" and turns full
// sets into the crafted result (iron ingots -> iron block, nuggets -> ingot -> block, wheat -> hay
// bale, ...). By default only recipes that can be crafted back are used (as in Sophisticated
// Backpacks). Results that don't fit are never forced in: the inputs stay as they were.
//
// The inventory change listener only records which item kinds grew; the compaction itself runs at
// the end of the server tick, outside of any inventory operation (no re-entrant inventory edits
// while other listeners are still being notified, and one pass per item kind per tick).

#include <memory>
#include <string>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "core/Filter.h"
#include "game/Backpack.h"
#include "game/ItemConv.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "selftest/SelfTest.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/FilterMenu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/IUpgrade.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/compacting/CompactingRecipes.h"

namespace abp::upgrade::compacting {

namespace {

constexpr int              kBasicFilterSlots    = 9;
constexpr int              kAdvancedFilterSlots = 16;
constexpr std::string_view kFilterKey           = "filter";
constexpr std::string_view kOnlyReversibleKey   = "onlyUncraftable";
/// Upper bound of compaction steps per drain (chains like nugget -> ingot -> block).
constexpr int kMaxSteps = 64;

core::AccessContext const kUpgradeAccess{core::Source::Upgrade};
core::AccessContext const kInternalAccess{core::Source::Internal};

bool onlyReversible(nlohmann::json const& settings) {
    if (!settings.is_object()) return true;
    auto it = settings.find(kOnlyReversibleKey);
    return it == settings.end() || !it->is_boolean() || it->get<bool>();
}

core::Filter readFilter(nlohmann::json const& settings, bool advanced) {
    nlohmann::json j = nlohmann::json::object();
    if (settings.is_object()) {
        if (auto it = settings.find(kFilterKey); it != settings.end()) j = *it;
    }
    auto f = core::Filter::fromJson(j, advanced ? kAdvancedFilterSlots : kBasicFilterSlots);
    if (!advanced) f.matchBy = core::MatchBy::Item; // basic filters only match by item
    return f;
}

// ---- compaction engine ---------------------------------------------------------------------------

using Kinds = std::vector<core::ItemStackData>; ///< distinct item kinds (count 1)

void addKind(Kinds& kinds, core::ItemStackData const& item) {
    if (item.empty()) return;
    for (auto const& k : kinds)
        if (k.sameItem(item)) return;
    kinds.push_back(item.withCount(1));
}

/// Runtime state of one installed compacting upgrade. Owned by its inventory change listener, so
/// it dies with the hooks (rebuild, disable, removal); the scheduler only keeps weak references.
struct Engine : std::enable_shared_from_this<Engine> {
    std::weak_ptr<game::Backpack> backpack;
    core::Filter                  filter;
    bool                          onlyReversible = true;
    std::vector<int>              grids; ///< grid sizes to try, biggest first

    std::vector<core::ItemStackData> snapshots; ///< last seen slot contents (tells growth from removal)
    Kinds                            pending;   ///< kinds to look at on the next drain
    Kinds                            blocked;   ///< kinds that had full sets but no room for the result
    bool                             busy      = false;
    bool                             scheduled = false;

    void snapshotAll(core::BackpackInventory const& inv) {
        snapshots.assign(static_cast<std::size_t>(inv.size()), {});
        for (int i = 0; i < inv.size(); ++i) snapshots[static_cast<std::size_t>(i)] = inv.at(i);
    }

    /// Change listener: remember what grew (or retry blocked kinds when something shrank).
    void onChange(core::BackpackInventory const& inv, int slot);
    /// Runs the pending compactions (end of tick).
    void drain(core::BackpackInventory& inv);
    void schedule();

    void compact(core::BackpackInventory& inv, core::ItemStackData const& item, Kinds& next);
    static int  craftAll(core::BackpackInventory& inv, core::ItemStackData const& item, Compaction const& c);
    static bool craft(core::BackpackInventory& inv, core::ItemStackData const& item, Compaction const& c, int times);
    static void putBack(core::BackpackInventory& inv, std::vector<std::pair<int, core::ItemStackData>> const& taken);
};

/// Engines with pending work, drained at the end of the server tick.
std::vector<std::weak_ptr<Engine>> gScheduled;

void Engine::schedule() {
    if (scheduled || pending.empty()) return;
    scheduled = true;
    gScheduled.push_back(weak_from_this());
}

void Engine::onChange(core::BackpackInventory const& inv, int slot) {
    if (slot < 0 || slot >= inv.size()) {
        snapshotAll(inv);
        return;
    }
    if (static_cast<std::size_t>(slot) >= snapshots.size()) snapshots.resize(static_cast<std::size_t>(slot) + 1);
    auto&       prev = snapshots[static_cast<std::size_t>(slot)];
    auto const& cur  = inv.at(slot);
    bool        grew = !cur.empty() && (prev.empty() || !prev.sameItem(cur) || cur.count > prev.count);
    bool shrank = !prev.empty() && (cur.empty() || !prev.sameItem(cur) || cur.count < prev.count);
    prev        = cur;
    if (busy) return; // our own crafting
    if (grew) addKind(pending, cur);
    if (shrank && !blocked.empty()) {
        for (auto const& k : blocked) addKind(pending, k);
        blocked.clear();
    }
    schedule();
}

void Engine::drain(core::BackpackInventory& inv) {
    busy = true;
    try {
        Kinds work = std::move(pending);
        pending.clear();
        for (int steps = 0; !work.empty() && steps < kMaxSteps; ++steps) {
            Kinds next;
            for (auto const& item : work) compact(inv, item, next);
            work = std::move(next);
        }
    } catch (std::exception const& e) {
        log().error("compacting failed: {}", e.what());
    }
    busy = false;
}

/// Compacts every full set of `item` with its biggest usable grid size; adds produced results to
/// `next` so that chains continue (nuggets -> ingots -> blocks).
void Engine::compact(core::BackpackInventory& inv, core::ItemStackData const& item, Kinds& next) {
    if (!filter.passes(item, &game::ItemConv::tags())) return;
    for (int grid : grids) {
        auto c = findCompaction(item, grid);
        if (!c || (onlyReversible && !c->reversible)) continue;
        // Like SB: the biggest usable shape wins (iron ingots make blocks, never trapdoors).
        auto total = inv.count([&](core::ItemStackData const& s) { return s.sameItem(item); });
        if (total < c->inputs) return;
        if (craftAll(inv, item, *c) > 0) addKind(next, c->result);
        if (inv.count([&](core::ItemStackData const& s) { return s.sameItem(item); }) >= c->inputs) {
            addKind(blocked, item); // no room for (all) results: retry when space frees up
        }
        return;
    }
}

/// Crafts as many sets as possible. Returns the number of crafts done.
int Engine::craftAll(core::BackpackInventory& inv, core::ItemStackData const& item, Compaction const& c) {
    auto total  = inv.count([&](core::ItemStackData const& s) { return s.sameItem(item); });
    int  crafts = static_cast<int>(std::min<long long>(total / c.inputs, 1'000'000));
    if (crafts <= 0) return 0;
    if (craft(inv, item, c, crafts)) return crafts;
    // Not everything fits: one craft at a time, as far as it goes.
    int done = 0;
    while (done < crafts && craft(inv, item, c, 1)) ++done;
    return done;
}

/// Consumes `times` sets of `item` and inserts the results. All or nothing: when the inputs
/// can't be taken or the results don't fit (or would be voided), everything is put back.
bool Engine::craft(core::BackpackInventory& inv, core::ItemStackData const& item, Compaction const& c, int times) {
    int                                               need = times * c.inputs;
    std::vector<std::pair<int, core::ItemStackData>> taken;
    int                                               got = 0;
    for (int i = inv.size() - 1; i >= 0 && got < need; --i) {
        auto const& cur = inv.at(i);
        if (cur.empty() || !cur.sameItem(item)) continue;
        auto e = inv.extract(i, need - got, kUpgradeAccess);
        if (e.empty()) continue;
        got += e.count;
        taken.emplace_back(i, std::move(e));
    }
    auto results = c.result.withCount(times * c.result.count);
    // Simulated after taking the inputs, so the space they free up counts.
    auto sim = got < need ? core::InsertResult{} : inv.insert(results, kUpgradeAccess, true);
    if (got < need || !sim.remainder.empty() || sim.voided > 0 || sim.inserted != results.count) {
        putBack(inv, taken);
        return false;
    }
    auto r = inv.insert(results, kUpgradeAccess);
    if (!r.remainder.empty()) {
        // Should not happen (the simulation said it fits): keep the inputs whole rather than the results.
        auto back = inv.extractMatching([&](core::ItemStackData const& s) { return s.sameItem(c.result); }, r.inserted, kInternalAccess);
        if (back.count != r.inserted) {
            log().error("compacting: could not undo {} x{}; keeping them", c.result.id, r.inserted - back.count);
            (void)inv.insert(back, kInternalAccess);
            return true;
        }
        putBack(inv, taken);
        return false;
    }
    return true;
}

void Engine::putBack(core::BackpackInventory& inv, std::vector<std::pair<int, core::ItemStackData>> const& taken) {
    for (auto it = taken.rbegin(); it != taken.rend(); ++it) {
        auto const& [slot, stack] = *it;
        auto rest                 = inv.insertInto(slot, stack, kInternalAccess).remainder;
        if (!rest.empty()) rest = inv.insert(rest, kInternalAccess).remainder;
        if (rest.empty()) continue;
        // Last resort (filters refused even internal moves): an empty slot, else the original slot
        // even beyond its limit. Never drop items.
        int target = -1;
        for (int i = 0; i < inv.size() && target < 0; ++i)
            if (inv.at(i).empty()) target = i;
        if (target >= 0) {
            inv.set(target, rest);
        } else if (inv.at(slot).sameItem(rest)) {
            inv.set(slot, inv.at(slot).withCount(inv.at(slot).count + rest.count));
        } else {
            log().error("compacting: could not put back {} x{}", rest.id, rest.count);
        }
    }
}

/// Drains scheduled engines at the end of every server tick.
class CompactingScheduler {
public:
    void install() {
        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>(
            [](ll::event::ServerLevelTickEvent&) { drainAll(); },
            ll::event::EventPriority::Low
        );
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mTick);
        gScheduled.clear();
    }

    static void drainAll() {
        if (gScheduled.empty()) return;
        auto list = std::move(gScheduled);
        gScheduled.clear();
        for (auto const& weak : list) {
            auto engine = weak.lock();
            if (!engine) continue; // hooks were rebuilt / upgrade disabled or removed
            engine->scheduled = false;
            if (auto bp = engine->backpack.lock()) engine->drain(bp->inventory());
        }
    }

private:
    ll::event::ListenerPtr mTick;
};


// ---- settings menu -------------------------------------------------------------------------------

/// The filter editor plus an "only compact uncraftable items" toggle and a size hint.
class CompactingMenu : public ui::FilterMenu {
public:
    CompactingMenu(std::string title, UpgradeContext ctx, bool advanced, Options options)
    : FilterMenu(
          std::move(title),
          [ctx, advanced] { return readFilter(ctx.settings(), advanced); },
          [ctx](core::Filter const& f) {
              ctx.settings()[std::string(kFilterKey)] = f.toJson();
              ctx.saveSettings(true);
          },
          std::move(options)
      ),
      mCtx(std::move(ctx)),
      mAdvanced(advanced) {}

    static constexpr int kToggleOffset = 5; ///< control-row slot of the toggle
    static constexpr int kInfoOffset   = 6;

    void build(Player& player) override {
        FilterMenu::build(player);
        int  control = size() - 9;
        bool only    = onlyReversible(mCtx.settings());
        auto state   = only ? trFor(player, "abp.filter.on", "§aOn") : trFor(player, "abp.filter.off", "§cOff");
        setSlot(
            control + kToggleOffset,
            std::make_shared<ui::ButtonSlot>(
                ui::icon(
                    only ? "minecraft:crafting_table" : "minecraft:piston",
                    trFor(player, "abp.compacting.only_uncraftable", "Only uncraftable: {}", state),
                    {trFor(player, "abp.compacting.only_uncraftable.help1", "§7Only compact items that can be"),
                     trFor(player, "abp.compacting.only_uncraftable.help2", "§7crafted back into the original items"),
                     trFor(player, "abp.filter.click_to_toggle", "§7Click to toggle")}
                ),
                [this](ui::ClickContext const&) {
                    auto& s                            = mCtx.settings();
                    bool  next                         = !onlyReversible(s);
                    s[std::string(kOnlyReversibleKey)] = next;
                    mCtx.saveSettings(true);
                    invalidate();
                }
            )
        );
        setSlot(
            control + kInfoOffset,
            std::make_shared<ui::StaticSlot>(ui::icon(
                mAdvanced ? "minecraft:iron_block" : "minecraft:quartz_block",
                mAdvanced ? trFor(player, "abp.compacting.grid.advanced", "Compacts 3x3 and 2x2 recipes")
                          : trFor(player, "abp.compacting.grid.basic", "Compacts 2x2 recipes")
            ))
        );
    }

private:
    UpgradeContext mCtx;
    bool           mAdvanced;
};

// ---- upgrades ------------------------------------------------------------------------------------

class CompactingUpgradeBase : public IUpgrade {
public:
    [[nodiscard]] virtual bool advanced() const = 0;

    void attach(UpgradeContext const& ctx, core::BackpackInventory& inventory) override {
        auto engine            = std::make_shared<Engine>();
        engine->backpack       = ctx.backpackPtr();
        engine->filter         = readFilter(ctx.settings(), advanced());
        engine->onlyReversible = onlyReversible(ctx.settings());
        engine->grids          = advanced() ? std::vector<int>{3, 2} : std::vector<int>{2};
        engine->snapshotAll(inventory);
        // Look at what is already stored too (new install, changed settings, loaded backpack).
        for (int i = 0; i < inventory.size(); ++i) addKind(engine->pending, inventory.at(i));
        engine->schedule();
        inventory.addChangeListener(ctx.hookOwner(), [engine](int slot) {
            auto bp = engine->backpack.lock();
            if (!bp) return;
            engine->onChange(bp->inventory(), slot);
        });
    }

    std::shared_ptr<ui::Menu> createSettingsMenu(Player& player, UpgradeContext const& ctx) override {
        ui::FilterMenu::Options options;
        options.slots        = advanced() ? kAdvancedFilterSlots : kBasicFilterSlots;
        options.allowMatchBy = advanced();
        // Items with custom data or damage never compact, so NBT / durability matching is moot.
        options.allowNbt        = false;
        options.allowDurability = false;
        options.onBack       = [bp = ctx.backpackPtr()](Player& p) {
            ui::BackpackMenu::openBackpack(p, bp, {}, ui::BackpackMenu::View::Upgrades);
        };
        return std::make_shared<CompactingMenu>(displayName(*this, &player), ctx, advanced(), std::move(options));
    }

    [[nodiscard]] std::vector<std::string> describe(Player& player, UpgradeContext const& ctx) const override {
        std::vector<std::string> lines;
        lines.push_back(
            advanced() ? trFor(player, "abp.compacting.grid.advanced", "Compacts 3x3 and 2x2 recipes")
                       : trFor(player, "abp.compacting.grid.basic", "Compacts 2x2 recipes")
        );
        auto state = onlyReversible(ctx.settings()) ? trFor(player, "abp.filter.on", "§aOn")
                                                    : trFor(player, "abp.filter.off", "§cOff");
        lines.push_back(trFor(player, "abp.compacting.only_uncraftable", "Only uncraftable: {}", state));
        auto filter = readFilter(ctx.settings(), advanced());
        int  used   = 0;
        for (auto const& g : filter.ghosts) used += g.empty() ? 0 : 1;
        if (used > 0) {
            lines.push_back(
                filter.mode == core::FilterMode::Allow
                    ? trFor(player, "abp.compacting.filter.allow", "§7Allow list: {} items", used)
                    : trFor(player, "abp.compacting.filter.deny", "§7Deny list: {} items", used)
            );
        }
        return lines;
    }
};

class CompactingUpgrade final : public CompactingUpgradeBase {
public:
    [[nodiscard]] UpgradeInfo const& info() const override {
        static UpgradeInfo const i{
            .id           = "compacting",
            .itemId       = "alleviatedbackpacks:compacting_upgrade",
            .nameKey      = "abp.compacting.name",
            .nameFallback = "Compacting Upgrade",
            .tier         = UpgradeTier::Basic,
            .group        = "compacting",
            .hasSettings  = true,
        };
        return i;
    }
    [[nodiscard]] bool advanced() const override { return false; }
};

class AdvancedCompactingUpgrade final : public CompactingUpgradeBase {
public:
    [[nodiscard]] UpgradeInfo const& info() const override {
        static UpgradeInfo const i{
            .id           = "advanced_compacting",
            .itemId       = "alleviatedbackpacks:advanced_compacting_upgrade",
            .nameKey      = "abp.compacting.advanced_name",
            .nameFallback = "Advanced Compacting Upgrade",
            .tier         = UpgradeTier::Advanced,
            .group        = "compacting",
            .hasSettings  = true,
        };
        return i;
    }
    [[nodiscard]] bool advanced() const override { return true; }
};

} // namespace
} // namespace abp::upgrade::compacting

ABP_REGISTER_UPGRADE(abp::upgrade::compacting::CompactingUpgrade);
ABP_REGISTER_UPGRADE(abp::upgrade::compacting::AdvancedCompactingUpgrade);
ABP_REGISTER_MODULE(abp::upgrade::compacting::CompactingScheduler);

// ---- selftests -----------------------------------------------------------------------------------

namespace abp::upgrade::compacting {
namespace {

using selftest::Context;
using Where  = ui::MenuManager::SlotAddress::Where;
using Action = ui::MenuManager::ActionType;

core::ItemStackData data(std::string_view id, int count) {
    return game::ItemConv::toData(game::ItemConv::make(id, count));
}

int countOf(game::Backpack& bp, std::string_view id) {
    return static_cast<int>(bp.inventory().count([&](core::ItemStackData const& s) { return s.id == id; }));
}

std::string counts(game::Backpack& bp, std::initializer_list<std::string_view> ids) {
    std::string out;
    for (auto id : ids) out += std::string(id) + "=" + std::to_string(countOf(bp, id)) + " ";
    return out;
}

void setOnlyReversible(std::shared_ptr<game::Backpack> const& bp, int slot, bool value) {
    UpgradeContext ctx(bp, slot);
    ctx.settings()[std::string(kOnlyReversibleKey)] = value;
    ctx.saveSettings(true);
}



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
        {"minecraft:slime_ball",  3, "minecraft:slime",          true },
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
        ctx.check(found->result.sameItem(data(c.result, 1)), name + ": result merges with normal " + std::string(c.result));
        ctx.check(found->result.count == 1, name + ": result count " + std::to_string(found->result.count));
        ctx.check(found->inputs == c.grid * c.grid, name + ": inputs");
        ctx.check(found->reversible == c.reversible, name + ": reversible flag");
    }
    // 4 iron ingots in a 2x2 grid are an iron trapdoor: found, but not uncraftable.
    auto trapdoor = findCompaction(data("minecraft:iron_ingot", 1), 2);
    ctx.check(trapdoor && trapdoor->result.id == "minecraft:iron_trapdoor" && !trapdoor->reversible,
              "2x2 iron ingots -> iron trapdoor (not reversible)");
    ctx.check(!findCompaction(data("minecraft:dirt", 1), 3).has_value(), "no 3x3 recipe for dirt");
    ctx.check(!findCompaction(data("minecraft:iron_block", 1), 3).has_value(), "iron blocks don't compact further");
    // Named items are never compacted (their custom data would be lost).
    auto named = game::ItemConv::make("minecraft:iron_ingot", 1);
    game::ItemConv::setDisplay(named, "Special");
    ctx.check(!findCompaction(game::ItemConv::toData(named), 3).has_value(), "named items are skipped");
});

ABP_REGISTER_SELFTEST("compacting.advanced_3x3", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  bp   = ctx.createBackpack(core::Tier::Iron);
    int   slot = ctx.installUpgrade(bp, "advanced_compacting");
    auto& inv  = bp->inventory();
    core::AccessContext player{core::Source::Player};

    auto r = inv.insert(data("minecraft:iron_ingot", 64), player);
    ctx.check(r.inserted == 64 && r.remainder.empty(), "insert reports all 64 taken");
    co_await ctx.ticks(2);
    ctx.check(countOf(*bp, "minecraft:iron_block") == 7 && countOf(*bp, "minecraft:iron_ingot") == 1,
              "64 ingots -> 7 blocks + 1 ingot: " + counts(*bp, {"minecraft:iron_block", "minecraft:iron_ingot"}));
    (void)inv.insert(data("minecraft:iron_ingot", 8), player);
    co_await ctx.ticks(2);
    ctx.check(countOf(*bp, "minecraft:iron_block") == 8 && countOf(*bp, "minecraft:iron_ingot") == 0,
              "8 more ingots complete another block: " + counts(*bp, {"minecraft:iron_block", "minecraft:iron_ingot"}));

    // Chains: 81 nuggets -> 9 ingots -> 1 block (in one go).
    (void)inv.insert(data("minecraft:gold_nugget", 64), player);
    (void)inv.insert(data("minecraft:gold_nugget", 17), core::AccessContext{core::Source::Pickup});
    co_await ctx.ticks(2);
    ctx.check(countOf(*bp, "minecraft:gold_block") == 1 && countOf(*bp, "minecraft:gold_nugget") == 0
                  && countOf(*bp, "minecraft:gold_ingot") == 0,
              "nuggets chain into a block: " + counts(*bp, {"minecraft:gold_nugget", "minecraft:gold_ingot", "minecraft:gold_block"}));

    // Placing into a specific slot (GUI) compacts too.
    (void)inv.insertInto(40, data("minecraft:wheat", 18), player);
    co_await ctx.ticks(2);
    ctx.check(countOf(*bp, "minecraft:hay_block") == 2 && countOf(*bp, "minecraft:wheat") == 0, "wheat -> hay bales");

    // 2x2 recipes are used by the advanced upgrade as well (once "only uncraftable" is off), and a
    // settings change compacts what is already stored.
    (void)inv.insert(data("minecraft:quartz", 5), player);
    co_await ctx.ticks(2);
    ctx.check(countOf(*bp, "minecraft:quartz") == 5, "quartz kept: its block can't be uncrafted");
    setOnlyReversible(bp, slot, false);
    co_await ctx.ticks(2);
    ctx.check(countOf(*bp, "minecraft:quartz_block") == 1 && countOf(*bp, "minecraft:quartz") == 1,
              "stored quartz compacted once only-uncraftable is off: " + counts(*bp, {"minecraft:quartz", "minecraft:quartz_block"}));

    // Items with a 3x3 shape only use it: 4 leftover ingots don't become an iron trapdoor (2x2).
    (void)inv.insert(data("minecraft:iron_ingot", 13), player);
    co_await ctx.ticks(2);
    ctx.check(countOf(*bp, "minecraft:iron_block") == 9 && countOf(*bp, "minecraft:iron_ingot") == 4
                  && countOf(*bp, "minecraft:iron_trapdoor") == 0,
              "3x3 shape wins: " + counts(*bp, {"minecraft:iron_ingot", "minecraft:iron_block", "minecraft:iron_trapdoor"}));

    // Disabled upgrades do nothing.
    upgrade::setEnabled(bp, slot, false);
    (void)inv.insert(data("minecraft:iron_ingot", 9), player);
    co_await ctx.ticks(2);
    ctx.check(countOf(*bp, "minecraft:iron_ingot") == 13, "disabled upgrade leaves ingots alone");
});

ABP_REGISTER_SELFTEST("compacting.basic_2x2_only", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  bp   = ctx.createBackpack(core::Tier::Leather);
    int   slot = ctx.installUpgrade(bp, "compacting");
    auto& inv  = bp->inventory();
    core::AccessContext player{core::Source::Player};
    (void)inv.insert(data("minecraft:iron_ingot", 27), player);
    co_await ctx.ticks(2);
    ctx.check(countOf(*bp, "minecraft:iron_ingot") == 27, "basic compacting ignores 3x3 recipes");
    (void)inv.insert(data("minecraft:quartz", 8), player);
    co_await ctx.ticks(2);
    ctx.check(countOf(*bp, "minecraft:quartz") == 8, "default: only uncraftable recipes");
    // Only uncraftable off: quartz -> quartz blocks, and the stored ingots -> iron trapdoors (2x2).
    setOnlyReversible(bp, slot, false);
    (void)inv.insert(data("minecraft:quartz", 1), player);
    co_await ctx.ticks(2);
    ctx.check(countOf(*bp, "minecraft:quartz_block") == 2 && countOf(*bp, "minecraft:quartz") == 1,
              "2x2 compaction: " + counts(*bp, {"minecraft:quartz", "minecraft:quartz_block"}));
    ctx.check(countOf(*bp, "minecraft:iron_trapdoor") == 6 && countOf(*bp, "minecraft:iron_ingot") == 3,
              "2x2 iron: " + counts(*bp, {"minecraft:iron_ingot", "minecraft:iron_trapdoor"}));

    auto iron = ctx.createBackpack(core::Tier::Iron); // 2 upgrade slots
    ctx.installUpgrade(iron, "compacting", 0);
    auto advancedItem = data("alleviatedbackpacks:advanced_compacting_upgrade", 1);
    ctx.require(!advancedItem.empty(), "advanced compacting item is registered");
    ctx.check(!upgrade::canInstall(iron, 1, advancedItem, nullptr), "only one compacting upgrade per backpack");
});

ABP_REGISTER_SELFTEST("compacting.filter", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  bp   = ctx.createBackpack(core::Tier::Iron);
    int   slot = ctx.installUpgrade(bp, "advanced_compacting");
    auto& inv  = bp->inventory();
    core::AccessContext player{core::Source::Player};
    UpgradeContext      uctx(bp, slot);
    core::Filter        f(kAdvancedFilterSlots);
    f.setGhost(0, data("minecraft:wheat", 1));
    uctx.settings()[std::string(kFilterKey)] = f.toJson();
    uctx.saveSettings(true);
    (void)inv.insert(data("minecraft:iron_ingot", 9), player);
    (void)inv.insert(data("minecraft:wheat", 9), player);
    co_await ctx.ticks(2);
    ctx.check(countOf(*bp, "minecraft:iron_ingot") == 9, "allow list: ingots not listed, not compacted");
    ctx.check(countOf(*bp, "minecraft:hay_block") == 1, "allow list: wheat compacted");

    f.mode = core::FilterMode::Deny;
    uctx.settings()[std::string(kFilterKey)] = f.toJson();
    uctx.saveSettings(true);
    (void)inv.insert(data("minecraft:wheat", 9), player);
    co_await ctx.ticks(2);
    ctx.check(countOf(*bp, "minecraft:wheat") == 9, "deny list: wheat kept");
    ctx.check(countOf(*bp, "minecraft:iron_block") == 1 && countOf(*bp, "minecraft:iron_ingot") == 0,
              "deny list: stored ingots compacted: " + counts(*bp, {"minecraft:iron_ingot", "minecraft:iron_block"}));
});

ABP_REGISTER_SELFTEST("compacting.no_loss_when_full", [](Context& ctx) -> ll::coro::CoroTask<> {
    core::AccessContext player{core::Source::Player};
    long long           total = 0;
    auto                all   = [](core::ItemStackData const& s) { return !s.empty(); };
    {
        // 25 slots of dirt, 13 ingots, 1 more slot of dirt: crafting a block would not free a slot.
        auto  bp  = ctx.createBackpack(core::Tier::Leather); // 27 slots
        auto& inv = bp->inventory();
        for (int i = 0; i < 27; ++i) inv.set(i, data("minecraft:dirt", 64));
        inv.set(25, data("minecraft:iron_ingot", 13));
        ctx.installUpgrade(bp, "advanced_compacting");
        co_await ctx.ticks(2);
        ctx.check(countOf(*bp, "minecraft:iron_ingot") == 13 && countOf(*bp, "minecraft:iron_block") == 0,
                  "no room for a block: ingots kept: " + counts(*bp, {"minecraft:iron_ingot", "minecraft:iron_block"}));
        ctx.check(countOf(*bp, "minecraft:dirt") == 26 * 64, "dirt untouched");
        // Space frees up (the player takes dirt out): the waiting ingots compact.
        auto taken = inv.extract(3, 64, player);
        ctx.check(taken.count == 64, "took dirt out");
        co_await ctx.ticks(2);
        ctx.check(countOf(*bp, "minecraft:iron_block") == 1 && countOf(*bp, "minecraft:iron_ingot") == 4,
                  "freed space is used: " + counts(*bp, {"minecraft:iron_ingot", "minecraft:iron_block"}));
        total = inv.count(all);
        ctx.check(total == 25 * 64 + 1 + 4, "nothing lost or duplicated (" + std::to_string(total) + ")");
    }
    {
        // 26 slots of dirt, 8 ingots in the last one: the 9th ingot completes a set whose inputs
        // free the very slot the block then goes into.
        auto  bp  = ctx.createBackpack(core::Tier::Leather);
        auto& inv = bp->inventory();
        for (int i = 0; i < 26; ++i) inv.set(i, data("minecraft:dirt", 64));
        inv.set(26, data("minecraft:iron_ingot", 8));
        ctx.installUpgrade(bp, "advanced_compacting");
        auto r = inv.insert(data("minecraft:iron_ingot", 1), player);
        ctx.check(r.inserted == 1, "the ingot fits");
        co_await ctx.ticks(2);
        ctx.check(countOf(*bp, "minecraft:iron_block") == 1 && countOf(*bp, "minecraft:iron_ingot") == 0,
                  "inputs freed the slot for the block: " + counts(*bp, {"minecraft:iron_ingot", "minecraft:iron_block"}));
        total = inv.count(all);
        ctx.check(total == 26 * 64 + 1, "nothing lost or duplicated (" + std::to_string(total) + ")");
    }
});

/// Voids iron blocks (stand-in for a Void upgrade): compacting must not turn ingots into voided blocks.
class VoidBlocksUpgrade : public IUpgrade {
public:
    UpgradeInfo mInfo{
        .id           = "selftest_compacting_void",
        .itemId       = "minecraft:structure_void",
        .nameKey      = "abp.selftest.upgrade",
        .nameFallback = "Selftest Upgrade",
    };
    [[nodiscard]] UpgradeInfo const& info() const override { return mInfo; }
    void attach(UpgradeContext const& ctx, core::BackpackInventory& inv) override {
        inv.addInsertFilter(ctx.hookOwner(), 0, [](core::InsertQuery const& q) {
            return q.stack.id == "minecraft:iron_block" ? core::InsertVerdict::Void : core::InsertVerdict::Allow;
        });
    }
};

ABP_REGISTER_SELFTEST("compacting.never_voids_inputs", [](Context& ctx) -> ll::coro::CoroTask<> {
    ScopedUpgrade scoped(std::make_shared<VoidBlocksUpgrade>());
    auto          bp = ctx.createBackpack(core::Tier::Iron);
    ctx.installUpgrade(bp, "advanced_compacting");
    ctx.installUpgrade(bp, "selftest_compacting_void");
    (void)bp->inventory().insert(data("minecraft:iron_ingot", 18), core::AccessContext{core::Source::Player});
    co_await ctx.ticks(2);
    ctx.check(countOf(*bp, "minecraft:iron_ingot") == 18, "ingots kept when the result would be voided: "
                                                              + counts(*bp, {"minecraft:iron_ingot", "minecraft:iron_block"}));
});

ABP_REGISTER_SELFTEST("compacting.settings_menu", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(core::Tier::Iron);
    int   slot   = ctx.installUpgrade(bp, "advanced_compacting");
    auto* type   = upgrade::byId("advanced_compacting");
    ctx.require(type != nullptr, "upgrade registered");
    ctx.require(type->info().hasSettings, "has settings");
    UpgradeContext uctx(bp, slot, &player);
    auto           menu = type->createSettingsMenu(player, uctx);
    ctx.require(menu != nullptr, "settings menu");
    ctx.require(mm.open(player, menu), "open settings");
    mm.forceShown(player);
    co_await ctx.ticks(2);
    auto settings = [&]() -> nlohmann::json const& { return bp->data().upgrades[static_cast<std::size_t>(slot)].settings; };
    int  toggle   = menu->size() - 9 + CompactingMenu::kToggleOffset;
    ctx.check(mm.shownItem(player, toggle).getTypeName() == "minecraft:crafting_table", "toggle shown (on)");
    ctx.check(onlyReversible(settings()), "default on");
    (void)mm.simulate(player, {Action::Take, {Where::Menu, toggle}, {Where::Cursor, 0}, 1});
    ctx.check(!onlyReversible(settings()), "toggled off");
    // A ghost item in the 16th filter slot.
    ctx.require(ctx.give(player, game::ItemConv::make("minecraft:wheat", 3), 4) == 4, "give wheat");
    (void)mm.simulate(player, {Action::Take, {Where::Inventory, 4}, {Where::Cursor, 0}, 3});
    (void)mm.simulate(player, {Action::Place, {Where::Cursor, 0}, {Where::Menu, 15}, 3});
    auto f = readFilter(settings(), true);
    ctx.check(f.ghosts.size() == kAdvancedFilterSlots && f.ghosts[15].id == "minecraft:wheat", "16th filter slot set");
    co_await ctx.ticks(2);
    ctx.check(mm.shownItem(player, toggle).getTypeName() == "minecraft:piston", "toggle icon follows the setting");
    mm.close(player);
    auto lore = type->describe(player, uctx);
    ctx.check(lore.size() == 3, "describe: grid, toggle, filter (" + std::to_string(lore.size()) + " lines)");
    // The new settings apply right away: the filter only allows wheat.
    (void)bp->inventory().insert(data("minecraft:quartz", 4), core::AccessContext{core::Source::Player});
    (void)bp->inventory().insert(data("minecraft:wheat", 9), core::AccessContext{core::Source::Player});
    co_await ctx.ticks(2);
    ctx.check(countOf(*bp, "minecraft:quartz") == 4, "quartz not in the filter");
    ctx.check(countOf(*bp, "minecraft:hay_block") == 1, "wheat compacted");
});

} // namespace
} // namespace abp::upgrade::compacting