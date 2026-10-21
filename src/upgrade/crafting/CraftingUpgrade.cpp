#include "upgrade/crafting/CraftingUpgrade.h"

#include <algorithm>
#include <array>

#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "game/ItemConv.h"
#include "mod/I18n.h"
#include "ui/menu/MenuManager.h"

namespace abp::upgrade::crafting {

// ---- model ------------------------------------------------------------------------------------

namespace {

constexpr int kGridSize = 9;
/// Safety bound for shift-click crafting (a full grid of 64-stacks).
constexpr int kMaxCrafts = 64 * kGridSize;

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

/// The grid without counts: recipes depend on the kinds of items only, so count changes (every
/// craft) keep the cached match.
nlohmann::json gridKinds(UpgradeContext const& ctx) {
    auto kinds = gridJson(ctx);
    for (auto& e : kinds) {
        if (e.is_object()) e.erase("count");
    }
    return kinds;
}

struct CraftCache {
    bool                      valid = false;
    nlohmann::json            kinds;
    std::optional<CraftMatch> match;
};

Grid gridStacks(UpgradeContext const& ctx) {
    Grid grid;
    for (int i = 0; i < kGridSize; ++i) {
        auto d = gridAt(ctx, i);
        if (!d.empty()) grid[static_cast<std::size_t>(i)] = game::ItemConv::toStack(d, 1);
    }
    return grid;
}

} // namespace

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

bool refillEnabled(UpgradeContext const& ctx) {
    auto const& s  = ctx.settings();
    auto        it = s.find(kRefillKey);
    return it == s.end() || !it->is_boolean() || it->get<bool>();
}

std::optional<CraftMatch> currentMatch(UpgradeContext const& ctx) {
    auto& cache = ctx.transient<CraftCache>();
    auto  kinds = gridKinds(ctx);
    if (cache.valid && cache.kinds == kinds) return cache.match;
    cache.match = matchCrafting(gridStacks(ctx));
    cache.kinds = std::move(kinds);
    cache.valid = true;
    return cache.match;
}

ItemStack craftOnce(UpgradeContext const& ctx, Player& player) {
    auto match = currentMatch(ctx);
    if (!match || match->result.isNull()) return {};
    std::array<core::ItemStackData, kGridSize> before;
    std::vector<core::ItemStackData>           leftovers;
    for (int i = 0; i < kGridSize; ++i) {
        auto item                           = gridAt(ctx, i);
        before[static_cast<std::size_t>(i)] = item;
        if (item.empty()) continue;
        auto remainder  = remainderOf(game::ItemConv::toStack(item, 1));
        item.count     -= 1;
        if (item.count <= 0) {
            item = remainder.isNull() ? core::ItemStackData{} : game::ItemConv::toData(remainder);
        } else if (!remainder.isNull()) {
            leftovers.push_back(game::ItemConv::toData(remainder));
        }
        putGrid(ctx, i, item);
    }
    if (refillEnabled(ctx)) {
        auto& inv = ctx.backpack().inventory();
        for (int i = 0; i < kGridSize; ++i) {
            auto const& was = before[static_cast<std::size_t>(i)];
            if (was.empty() || !gridAt(ctx, i).empty()) continue;
            auto got = inv.extractMatching(
                [&](core::ItemStackData const& s) { return s.sameItem(was); },
                1,
                core::AccessContext{core::Source::Upgrade, &player}
            );
            if (!got.empty()) putGrid(ctx, i, got);
        }
    }
    ctx.saveSettings();
    for (auto& rest : leftovers) (void)giveBack(&player, ctx.backpack(), rest); // with a player: never lost
    return match->result;
}

int craftMax(UpgradeContext const& ctx, Player& player, bool intoBackpack) {
    std::optional<ItemStack> first;
    int                      crafts = 0;
    for (; crafts < kMaxCrafts; ++crafts) {
        auto match = currentMatch(ctx);
        if (!match || match->result.isNull()) break;
        auto const& result = match->result;
        if (first && (!first->isStackable(result) || first->mCount != result.mCount)) break;
        if (!deliver(player, ctx.backpack(), result, intoBackpack, true)) break;
        auto out = craftOnce(ctx, player);
        if (out.isNull()) break;
        if (!first) first = out;
        deliver(player, ctx.backpack(), out, intoBackpack, false);
    }
    return crafts;
}

void clearGrid(UpgradeContext const& ctx, Player& player) {
    for (int i = 0; i < kGridSize; ++i) {
        auto item = gridAt(ctx, i);
        if (item.empty()) continue;
        putGrid(ctx, i, stash(player, ctx.backpack(), item));
    }
    ctx.saveSettings();
}

// ---- menu -------------------------------------------------------------------------------------

namespace {

class CraftingMenu : public TabMenu {
public:
    CraftingMenu(std::string title, UpgradeContext const& ctx)
    : TabMenu(std::move(title), ctx, std::string(kCraftingId)) {}

protected:
    void buildTab(Player& player) override {
        for (int i = 0; i < kGridSize; ++i) {
            setSlot(
                kGridMenuSlots[i],
                std::make_shared<StackSlot>(
                    *this,
                    [this, i] { return gridAt(mCtx, i); },
                    [this, i](core::ItemStackData const& d) { setGridAt(mCtx, i, d); }
                )
            );
        }
        setSlot(
            kResultSlot,
            std::make_shared<ResultSlot>(
                *this,
                ResultSlot::Ops{
                    .preview =
                        [this] {
                            auto m = currentMatch(mCtx);
                            return m ? m->result : ItemStack{};
                        },
                    .craftOnce = [this](Player& p) { return craftOnce(mCtx, p); },
                    .craftMax  = [this](Player& p, bool intoBackpack) { return craftMax(mCtx, p, intoBackpack); },
                }
            )
        );

        auto info       = std::make_shared<ui::CallbackSlot>();
        info->onDisplay = [this](Player& p) {
            std::vector<std::string> lore{
                trFor(p, "abp.crafting.help_click", "§7Click the result to craft one"),
                trFor(p, "abp.crafting.help_shift", "§7Shift-click the result to craft as many as possible"),
            };
            if (valid()) {
                if (auto m = currentMatch(mCtx))
                    lore.push_back(trFor(p, "abp.crafting.recipe", "§8Recipe: {}", m->recipeId));
            }
            return ui::icon("minecraft:crafting_table", trFor(p, "abp.crafting.title", "Crafting"), lore);
        };
        info->onClick = [](ui::ClickContext const&) { return true; };
        setSlot(kInfoSlot, info);

        bool refill = refillEnabled(mCtx);
        setSlot(
            kRefillSlot,
            std::make_shared<ui::ButtonSlot>(
                ui::icon(
                    refill ? "minecraft:hopper" : "minecraft:barrier",
                    refill ? trFor(player, "abp.crafting.refill_on", "Refill grid from backpack: on")
                           : trFor(player, "abp.crafting.refill_off", "Refill grid from backpack: off"),
                    {trFor(player, "abp.crafting.refill_hint", "§7Used-up ingredients are refilled from the backpack"),
                     trFor(player, "abp.crafting.click_to_toggle", "§7Click to change")}
                ),
                [this](ui::ClickContext const&) {
                    if (!valid()) return;
                    mCtx.settings()[std::string(kRefillKey)] = !refillEnabled(mCtx);
                    mCtx.saveSettings();
                    invalidate();
                }
            )
        );
        setSlot(
            kClearSlot,
            std::make_shared<ui::ButtonSlot>(
                ui::icon(
                    "minecraft:bucket",
                    trFor(player, "abp.crafting.clear", "Clear grid"),
                    {trFor(player, "abp.crafting.clear_hint", "§7Moves the grid into the backpack")}
                ),
                [this](ui::ClickContext const& c) {
                    if (valid()) clearGrid(mCtx, c.player);
                }
            )
        );
    }
};

} // namespace

std::shared_ptr<TabMenu> makeCraftingMenu(Player& player, UpgradeContext const& ctx) {
    return std::make_shared<CraftingMenu>(trFor(player, "abp.crafting.title", "Crafting"), ctx);
}

// ---- upgrade ----------------------------------------------------------------------------------

namespace {

class CraftingUpgrade : public IUpgrade {
public:
    [[nodiscard]] UpgradeInfo const& info() const override {
        static UpgradeInfo const kInfo{
            .id           = std::string(kCraftingId),
            .itemId       = "alleviatedbackpacks:crafting_upgrade",
            .nameKey      = "abp.crafting.name",
            .nameFallback = "Crafting Upgrade",
            .hasSettings  = true,
            .toggleable   = false,
        };
        return kInfo;
    }

    void onRemove(UpgradeContext const& ctx) override {
        // Grid contents go back into the backpack (or to the player); they never travel with the
        // upgrade item. Without a player, what doesn't fit stays in the settings (and so travels
        // with the item) instead of being lost.
        bool kept = false;
        for (int i = 0; i < kGridSize; ++i) {
            auto item = gridAt(ctx, i);
            if (item.empty()) continue;
            auto rest = giveBack(ctx.player(), ctx.backpack(), item);
            putGrid(ctx, i, rest);
            kept = kept || !rest.empty();
        }
        if (!kept) ctx.settings().erase(std::string(kGridKey));
        ctx.saveSettings();
    }

    std::shared_ptr<ui::Menu> createSettingsMenu(Player& player, UpgradeContext const& ctx) override {
        return makeCraftingMenu(player, ctx);
    }

    [[nodiscard]] std::vector<std::string> describe(Player& player, UpgradeContext const& ctx) const override {
        int used = 0;
        for (int i = 0; i < kGridSize; ++i) used += gridAt(ctx, i).empty() ? 0 : 1;
        return {
            trFor(player, "abp.crafting.describe_grid", "§7Grid: {}/9 slots used", used),
            refillEnabled(ctx) ? trFor(player, "abp.crafting.describe_refill_on", "§7Refill from backpack: on")
                               : trFor(player, "abp.crafting.describe_refill_off", "§7Refill from backpack: off"),
        };
    }
};

} // namespace

} // namespace abp::upgrade::crafting

ABP_REGISTER_UPGRADE(abp::upgrade::crafting::CraftingUpgrade);

ABP_REGISTER_CRAFTING_TAB(
    abp::upgrade::crafting::TabEntry{
        .upgradeId     = std::string(abp::upgrade::crafting::kCraftingId),
        .order         = 0,
        .iconItem      = "minecraft:crafting_table",
        .labelKey      = "abp.crafting.button",
        .labelFallback = "Crafting",
        .make          = &abp::upgrade::crafting::makeCraftingMenu,
    }
);
