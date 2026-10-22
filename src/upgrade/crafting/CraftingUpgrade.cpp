#include "upgrade/crafting/CraftingUpgrade.h"

#include <algorithm>
#include <array>

#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "game/ItemConv.h"
#include "mod/I18n.h"
#include "net/NativeScreens.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/crafting/Station.h"

namespace abp::upgrade::crafting {

namespace {

constexpr int kGridSize = 9;
/// Safety bound for one shift-click without refill (SC loops while the result stays the same).
constexpr int kMaxShiftCrafts = 64 * kGridSize;

nlohmann::json& gridJson(UpgradeContext const& ctx) {
    auto& settings = ctx.settings();
    if (!settings.is_object()) settings = nlohmann::json::object();
    auto& g = settings[std::string(kGridKey)];
    if (!g.is_array() || g.size() != kGridSize) {
        auto fixed = nlohmann::json::array();
        for (int i = 0; i < kGridSize; ++i) {
            fixed.push_back(g.is_array() && i < static_cast<int>(g.size()) ? g[static_cast<std::size_t>(i)] : nullptr);
        }
        g = std::move(fixed);
    }
    return g;
}

void putGrid(UpgradeContext const& ctx, int index, core::ItemStackData const& item) {
    gridJson(ctx)[static_cast<std::size_t>(index)] = item.empty() ? nlohmann::json(nullptr) : item.toJson();
}

/// The grid without counts: recipes depend on the kinds of items only.
nlohmann::json gridKinds(UpgradeContext const& ctx) {
    auto kinds = gridJson(ctx);
    for (auto& e : kinds) {
        if (e.is_object()) e.erase("count");
    }
    return kinds;
}

Grid gridStacks(UpgradeContext const& ctx) {
    Grid grid;
    for (int i = 0; i < kGridSize; ++i) {
        auto d = gridAt(ctx, i);
        if (!d.empty()) grid[static_cast<std::size_t>(i)] = game::ItemConv::toStack(d, 1);
    }
    return grid;
}

struct CraftCache {
    bool           valid = false;
    nlohmann::json kinds;
    CraftResults   results;
    std::string    lastRecipe; ///< SC lastRecipe: kept selected while it still matches
};

bool flag(UpgradeContext const& ctx, std::string_view key, bool def) {
    auto const& s  = ctx.settings();
    if (!s.is_object()) return def;
    auto        it = s.find(key);
    return it != s.end() && it->is_boolean() ? it->get<bool>() : def;
}

void setFlag(UpgradeContext const& ctx, std::string_view key, bool value) {
    auto& s = ctx.settings();
    if (!s.is_object()) s = nlohmann::json::object();
    s[std::string(key)] = value;
    ctx.saveSettings();
}

bool installedHere(UpgradeContext const& ctx) {
    auto const& slots = ctx.backpack().data().upgrades;
    if (ctx.slot() < 0 || ctx.slot() >= static_cast<int>(slots.size())) return false;
    auto const& s = slots[static_cast<std::size_t>(ctx.slot())];
    if (s.empty()) return false;
    auto* u = upgradeFor(s.item);
    return u && u->info().id == kCraftingId;
}

} // namespace

// ---- model --------------------------------------------------------------------------------------

core::ItemStackData gridAt(UpgradeContext const& ctx, int index) {
    if (index < 0 || index >= kGridSize) return {};
    auto const& j = gridJson(ctx)[static_cast<std::size_t>(index)];
    if (j.is_null()) return {};
    return core::ItemStackData::fromJson(j);
}

void setGridAt(UpgradeContext const& ctx, int index, core::ItemStackData const& item) {
    if (index < 0 || index >= kGridSize) return;
    putGrid(ctx, index, item);
    ctx.saveSettings();
}

bool shiftIntoStorage(UpgradeContext const& ctx) { return flag(ctx, kShiftIntoStorageKey, true); }
void setShiftIntoStorage(UpgradeContext const& ctx, bool value) { setFlag(ctx, kShiftIntoStorageKey, value); }
bool refillEnabled(UpgradeContext const& ctx) { return flag(ctx, kRefillKey, false); }
void setRefill(UpgradeContext const& ctx, bool value) { setFlag(ctx, kRefillKey, value); }

CraftResults const& results(UpgradeContext const& ctx) {
    auto& cache = ctx.transient<CraftCache>();
    auto  kinds = gridKinds(ctx);
    if (cache.valid && cache.kinds == kinds) return cache.results;
    cache.results.matches  = matchAllCrafting(gridStacks(ctx));
    cache.results.selected = 0;
    for (int i = 0; i < static_cast<int>(cache.results.matches.size()); ++i) {
        if (!cache.lastRecipe.empty() && cache.results.matches[static_cast<std::size_t>(i)].recipeId == cache.lastRecipe) {
            cache.results.selected = i;
            break;
        }
    }
    if (!cache.results.matches.empty()) {
        cache.lastRecipe = cache.results.matches[static_cast<std::size_t>(cache.results.selected)].recipeId;
    }
    cache.kinds = std::move(kinds);
    cache.valid = true;
    return cache.results;
}

std::optional<CraftMatch> currentMatch(UpgradeContext const& ctx) {
    auto const& r = results(ctx);
    if (r.matches.empty()) return std::nullopt;
    return r.matches[static_cast<std::size_t>(std::clamp(r.selected, 0, static_cast<int>(r.matches.size()) - 1))];
}

void selectResult(UpgradeContext const& ctx, int index) {
    (void)results(ctx);
    auto& cache = ctx.transient<CraftCache>();
    if (index < 0 || index >= static_cast<int>(cache.results.matches.size())) return;
    cache.results.selected = index;
    cache.lastRecipe       = cache.results.matches[static_cast<std::size_t>(index)].recipeId;
    ctx.backpack().markDirty(); // the screen shows the new result
}

void injectResultsForTest(UpgradeContext const& ctx, std::vector<CraftMatch> matches) {
    (void)results(ctx);
    auto& cache            = ctx.transient<CraftCache>();
    cache.results.matches  = std::move(matches);
    cache.results.selected = 0;
    cache.lastRecipe       = cache.results.matches.empty() ? std::string{} : cache.results.matches.front().recipeId;
}

void selectNextResult(UpgradeContext const& ctx) {
    auto const& r = results(ctx);
    int         n = static_cast<int>(r.matches.size());
    if (n > 1) selectResult(ctx, (r.selected + 1) % n);
}

void selectPreviousResult(UpgradeContext const& ctx) {
    auto const& r = results(ctx);
    int         n = static_cast<int>(r.matches.size());
    if (n > 1) selectResult(ctx, (r.selected + n - 1) % n);
}

ItemStack craftOnce(UpgradeContext const& ctx, Player& player) {
    auto match = currentMatch(ctx);
    if (!match || match->result.isNull()) return {};
    bool refill = refillEnabled(ctx);
    for (int i = 0; i < kGridSize; ++i) {
        auto item = gridAt(ctx, i);
        if (item.empty()) continue;
        auto remainder = game::ItemConv::toData(remainderOf(game::ItemConv::toStack(item, 1)));
        if (remainder.empty() && refill && extractFromStorageOrPlayer(player, ctx.backpack(), item)) {
            continue; // the cell keeps its count: the ingredient came from the backpack / player
        }
        item = item.withCount(item.count - 1);
        if (!remainder.empty()) {
            if (item.empty()) {
                item = remainder;
            } else if (item.sameItem(remainder)) {
                item.count += remainder.count;
            } else {
                // SC: into the player's inventory, else dropped.
                int left = addToPlayer(player, game::ItemConv::toStack(remainder, 1), remainder.count);
                dropAt(player, remainder.withCount(left));
            }
        }
        putGrid(ctx, i, item);
    }
    ctx.saveSettings();
    return match->result;
}

int craftShift(UpgradeContext const& ctx, Player& player) {
    auto first = currentMatch(ctx);
    if (!first || first->result.isNull()) return 0;
    int  limit  = refillEnabled(ctx) ? std::max<int>(first->result.getMaxStackSize(), 1) : kMaxShiftCrafts;
    bool toBp   = shiftIntoStorage(ctx);
    auto target = game::ItemConv::toData(first->result);
    int  crafts = 0;
    for (; crafts < limit; ++crafts) {
        auto match = currentMatch(ctx);
        if (!match || match->result.isNull()) break;
        auto result = game::ItemConv::toData(match->result);
        if (result.id != target.id) break; // SC: while the result is still the same item
        // SC mergeSlotStack: nothing fits anywhere -> nothing is crafted.
        auto r    = ctx.backpack().inventory().insert(result, core::AccessContext{core::Source::Player, &player}, true);
        int  room = r.inserted + r.voided + playerRoomFor(player, match->result);
        if (room <= 0) break;
        auto out = craftOnce(ctx, player);
        if (out.isNull()) break;
        auto left = moveOut(player, ctx.backpack(), game::ItemConv::toData(out), toBp);
        dropAt(player, left); // SC drops what didn't fit
    }
    return crafts;
}

bool shiftGridCell(UpgradeContext const& ctx, Player& player, int index) {
    auto item = gridAt(ctx, index);
    if (item.empty()) return false;
    auto left = moveOut(player, ctx.backpack(), item, true);
    if (left.count == item.count) return false;
    setGridAt(ctx, index, left);
    return true;
}

int quickMoveIntoGrid(UpgradeContext const& ctx, core::ItemStackData const& item) {
    if (item.empty() || !allowedInUpgradeSlot(ctx.backpack(), item)) return 0;
    int toTransfer = std::min(item.count, std::max(item.maxStack, 1));
    int moved      = 0;
    int max        = std::max(item.maxStack, 1);
    for (int i = 0; i < kGridSize && toTransfer > 0; ++i) {
        auto cur = gridAt(ctx, i);
        if (cur.empty() || !cur.sameItem(item) || cur.count >= max) continue;
        int n = std::min(toTransfer, max - cur.count);
        putGrid(ctx, i, cur.withCount(cur.count + n));
        toTransfer -= n;
        moved      += n;
    }
    for (int i = 0; i < kGridSize && toTransfer > 0; ++i) {
        if (!gridAt(ctx, i).empty()) continue;
        putGrid(ctx, i, item.withCount(toTransfer));
        moved      += toTransfer;
        toTransfer  = 0;
    }
    if (moved > 0) ctx.saveSettings();
    return moved;
}

// ---- native tab ---------------------------------------------------------------------------------

namespace {

std::shared_ptr<StateSlot> gridSlot(UpgradeContext const& ctx, int index) {
    return std::make_shared<StateSlot>(StateSlot::Ops{
        .get      = [ctx, index] { return gridAt(ctx, index); },
        .set      = [ctx, index](core::ItemStackData const& d) { setGridAt(ctx, index, d); },
        .mayPlace = [ctx](core::ItemStackData const& d) { return allowedInUpgradeSlot(ctx.backpack(), d); },
        .maxCount = {},
        .valid    = [ctx] { return installedHere(ctx); },
    });
}

std::shared_ptr<ResultSlot> resultSlot(UpgradeContext const& ctx) {
    return std::make_shared<ResultSlot>(
        ctx.backpack(),
        ResultSlot::Ops{
            .preview =
                [ctx] {
                    auto m = currentMatch(ctx);
                    return m ? m->result : ItemStack{};
                },
            .craftOnce = [ctx](Player& p) { return craftOnce(ctx, p); },
            .valid     = [ctx] { return installedHere(ctx); },
        }
    );
}

nlohmann::json itemJson(core::ItemStackData const& d) { return d.empty() ? nlohmann::json(nullptr) : d.toJson(); }

nlohmann::json tabData(Player& player, UpgradeContext const& ctx) {
    nlohmann::json grid = nlohmann::json::array();
    for (int i = 0; i < kGridSize; ++i) grid.push_back(itemJson(gridAt(ctx, i)));
    auto const&    r    = results(ctx);
    nlohmann::json list = nlohmann::json::array();
    for (auto const& m : r.matches) list.push_back(itemJson(game::ItemConv::toData(m.result)));
    auto match = currentMatch(ctx);
    return nlohmann::json{
        {"grid",             std::move(grid)                                                      },
        {"result",           match ? itemJson(game::ItemConv::toData(match->result)) : nullptr},
        {"results",          std::move(list)                                                      },
        {"selected",         r.selected                                                           },
        {"shiftIntoStorage", shiftIntoStorage(ctx)                                                },
        {"refill",           refillEnabled(ctx)                                                   },
        {"title",            trFor(player, "abp.crafting.tab", "Craft")                           },
        {"tooltip",          trFor(player, "abp.crafting.tab.tooltip", "Crafting")                },
    };
}

bool tabAction(Player& player, UpgradeContext const& ctx, nlohmann::json const& data) {
    if (handleTabOpen(player, ctx, data)) return true;
    int  button = data.value("button", 0);
    bool shift  = data.value("shift", false);
    if (auto it = data.find("grid"); it != data.end() && it->is_number_integer()) {
        int i = it->get<int>();
        if (i < 0 || i >= kGridSize) return false;
        if (shift) {
            shiftGridCell(ctx, player, i);
            ui::MenuManager::get().resyncPlayer(player);
            return true;
        }
        clickSlot(player, gridSlot(ctx, i), button);
        return true;
    }
    if (data.contains("result")) {
        if (shift) {
            craftShift(ctx, player);
            ui::MenuManager::get().resyncPlayer(player);
            return true;
        }
        clickSlot(player, resultSlot(ctx), button, true);
        return true;
    }
    if (auto it = data.find("select"); it != data.end() && it->is_number_integer()) {
        selectResult(ctx, it->get<int>());
        return true;
    }
    if (data.value("next", false)) {
        selectNextResult(ctx);
        return true;
    }
    if (data.value("previous", false)) {
        selectPreviousResult(ctx);
        return true;
    }
    auto toggle = data.value("toggle", std::string{});
    if (toggle == "shiftTarget") {
        setShiftIntoStorage(ctx, !shiftIntoStorage(ctx));
        return true;
    }
    if (toggle == "refill") {
        setRefill(ctx, !refillEnabled(ctx));
        return true;
    }
    return false;
}

// ---- upgrade ------------------------------------------------------------------------------------

class CraftingUpgrade : public IUpgrade {
public:
    [[nodiscard]] UpgradeInfo const& info() const override {
        static UpgradeInfo const kInfo{
            .id           = std::string(kCraftingId),
            .itemId       = "alleviatedbackpacks:crafting_upgrade",
            .nameKey      = "abp.crafting.name",
            .nameFallback = "Crafting Upgrade",
            .hasSettings  = true,
            .toggleable   = false, // SC canBeDisabled() == false
        };
        return kInfo;
    }

    [[nodiscard]] std::vector<std::string> describe(Player& player, UpgradeContext const&) const override {
        return {trFor(player, "abp.crafting.tooltip", "§7Crafting table in an upgrade tab")};
    }
};

} // namespace

} // namespace abp::upgrade::crafting

ABP_REGISTER_UPGRADE(abp::upgrade::crafting::CraftingUpgrade);

ABP_REGISTER_TAB_DATA(abp::net::TabDataProvider{std::string(abp::upgrade::crafting::kCraftingId), &abp::upgrade::crafting::tabData});
ABP_REGISTER_TAB_ACTION(abp::net::TabActionHandler{std::string(abp::upgrade::crafting::kCraftingId), &abp::upgrade::crafting::tabAction});

ABP_REGISTER_STATION_TAB(abp::upgrade::crafting::StationTab{
    .upgradeId   = std::string(abp::upgrade::crafting::kCraftingId),
    .quickMoveIn = [](Player&, abp::upgrade::UpgradeContext const& ctx, abp::core::ItemStackData const& item) {
        return abp::upgrade::crafting::quickMoveIntoGrid(ctx, item);
    },
});
