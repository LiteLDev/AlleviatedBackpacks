#include "upgrade/stonecutter/StonecutterUpgrade.h"

#include <algorithm>

#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "game/ItemConv.h"
#include "mod/I18n.h"
#include "ui/menu/MenuManager.h"

namespace abp::upgrade::stonecutter {

using crafting::StonecutterOption;
using crafting::TabMenu;

// ---- model ------------------------------------------------------------------------------------

namespace {

constexpr int kMaxCrafts   = 64 * 4;
constexpr int kRecipesPage = static_cast<int>(std::size(kRecipeSlots));

struct OptionCache {
    bool                           valid = false;
    nlohmann::json                 kind; ///< the input without its count
    std::vector<StonecutterOption> options;
};

nlohmann::json inputKind(UpgradeContext const& ctx) {
    auto const& s  = ctx.settings();
    auto        it = s.find(kInputKey);
    if (it == s.end() || !it->is_object()) return nullptr;
    auto kind = *it;
    kind.erase("count");
    return kind;
}

} // namespace

core::ItemStackData inputOf(UpgradeContext const& ctx) {
    auto const& s  = ctx.settings();
    auto        it = s.find(kInputKey);
    if (it == s.end() || it->is_null()) return {};
    return core::ItemStackData::fromJson(*it);
}

void setInput(UpgradeContext const& ctx, core::ItemStackData const& item) {
    auto& s = ctx.settings();
    if (!s.is_object()) s = nlohmann::json::object();
    if (item.empty()) s.erase(std::string(kInputKey));
    else s[std::string(kInputKey)] = item.toJson();
    ctx.saveSettings();
}

std::vector<StonecutterOption> const& options(UpgradeContext const& ctx) {
    auto& cache = ctx.transient<OptionCache>();
    auto  kind  = inputKind(ctx);
    if (cache.valid && cache.kind == kind) return cache.options;
    auto input    = inputOf(ctx);
    cache.options = input.empty() ? std::vector<StonecutterOption>{}
                                  : crafting::stonecutterOptions(game::ItemConv::toStack(input, 1));
    cache.kind    = std::move(kind);
    cache.valid   = true;
    return cache.options;
}

std::string selectedId(UpgradeContext const& ctx) {
    auto const& s  = ctx.settings();
    auto        it = s.find(kSelectedKey);
    return it != s.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

void select(UpgradeContext const& ctx, std::string const& recipeId) {
    auto& s = ctx.settings();
    if (!s.is_object()) s = nlohmann::json::object();
    if (recipeId.empty()) s.erase(std::string(kSelectedKey));
    else s[std::string(kSelectedKey)] = recipeId;
    ctx.saveSettings();
}

std::optional<StonecutterOption> selectedOption(UpgradeContext const& ctx) {
    auto id = selectedId(ctx);
    if (id.empty()) return std::nullopt;
    for (auto const& o : options(ctx)) {
        if (o.recipeId == id) return o;
    }
    return std::nullopt;
}

ItemStack craftOnce(UpgradeContext const& ctx) {
    auto option = selectedOption(ctx);
    auto input  = inputOf(ctx);
    if (!option || input.empty() || option->result.isNull()) return {};
    setInput(ctx, input.withCount(input.count - 1));
    return option->result;
}

int craftMax(UpgradeContext const& ctx, Player& player, bool intoBackpack) {
    int crafts = 0;
    for (; crafts < kMaxCrafts; ++crafts) {
        auto option = selectedOption(ctx);
        if (!option || inputOf(ctx).empty()) break;
        if (!crafting::deliver(player, ctx.backpack(), option->result, intoBackpack, true)) break;
        auto out = craftOnce(ctx);
        if (out.isNull()) break;
        crafting::deliver(player, ctx.backpack(), out, intoBackpack, false);
    }
    return crafts;
}

// ---- menu -------------------------------------------------------------------------------------

namespace {

/// One selectable output of the current input.
class RecipeButton : public ui::Slot {
public:
    RecipeButton(TabMenu& menu, int index, int const& page) : mMenu(menu), mIndex(index), mPage(page) {}

    [[nodiscard]] ItemStack display(Player& player) const override {
        auto const* option = current();
        if (!option) return {};
        auto stack    = option->result;
        bool selected = option->recipeId == selectedId(mMenu.context());
        game::ItemConv::setDisplay(
            stack,
            {},
            {selected ? trFor(player, "abp.stonecutter.selected", "§a» Selected")
                      : trFor(player, "abp.stonecutter.select", "§7Click to select")}
        );
        return stack;
    }

    bool click(ui::ClickContext const&) override {
        if (auto const* option = current()) select(mMenu.context(), option->recipeId);
        return true;
    }

private:
    [[nodiscard]] StonecutterOption const* current() const {
        if (!mMenu.valid()) return nullptr;
        auto const& list  = options(mMenu.context());
        int         index = mPage * kRecipesPage + mIndex;
        if (index < 0 || index >= static_cast<int>(list.size())) return nullptr;
        return &list[static_cast<std::size_t>(index)];
    }

    TabMenu&   mMenu;
    int        mIndex;
    int const& mPage;
};

class StonecutterMenu : public TabMenu {
public:
    StonecutterMenu(std::string title, UpgradeContext const& ctx)
    : TabMenu(std::move(title), ctx, std::string(crafting::kStonecutterId)) {}

    /// Shift-click from the inventory: into the input slot (empty or same item), the rest into
    /// the backpack.
    int quickMoveIn(Player& player, ItemStack const& stack) override {
        if (stack.isNull() || !valid()) return 0;
        auto in       = game::ItemConv::toData(stack);
        auto cur      = inputOf(mCtx);
        int  accepted = 0;
        if (crafting::allowedInUpgradeSlot(mCtx.backpack(), in) && (cur.empty() || cur.sameItem(in))) {
            int have = cur.empty() ? 0 : cur.count;
            int n    = std::clamp(std::max(in.maxStack, 1) - have, 0, in.count);
            if (n > 0) {
                setInput(mCtx, in.withCount(have + n));
                accepted = n;
            }
        }
        if (accepted < in.count) {
            ItemStack rest = stack;
            rest.set(in.count - accepted);
            accepted += TabMenu::quickMoveIn(player, rest);
        }
        return accepted;
    }

    void onTick(Player& player) override {
        TabMenu::onTick(player);
        if (!valid()) return;
        // Keep the outputs page in range and the page button in sync with the input.
        int pages = pageCount();
        if (mRecipePage >= pages) {
            mRecipePage = 0;
            invalidate();
        }
        if ((pages > 1) != mHasPageButton) invalidate();
    }

protected:
    void buildTab(Player& player) override {
        setSlot(
            kInputSlot,
            std::make_shared<crafting::StackSlot>(
                *this,
                [this] { return inputOf(mCtx); },
                [this](core::ItemStackData const& d) { setInput(mCtx, d); }
            )
        );
        setSlot(
            kResultSlot,
            std::make_shared<crafting::ResultSlot>(
                *this,
                crafting::ResultSlot::Ops{
                    .preview =
                        [this] {
                            if (inputOf(mCtx).empty()) return ItemStack{};
                            auto option = selectedOption(mCtx);
                            return option ? option->result : ItemStack{};
                        },
                    .craftOnce = [this](Player&) { return craftOnce(mCtx); },
                    .craftMax  = [this](Player& p, bool intoBackpack) { return craftMax(mCtx, p, intoBackpack); },
                }
            )
        );
        for (int i = 0; i < kRecipesPage; ++i) {
            setSlot(kRecipeSlots[i], std::make_shared<RecipeButton>(*this, i, mRecipePage));
        }

        auto info       = std::make_shared<ui::CallbackSlot>();
        info->onDisplay = [](Player& p) {
            return ui::icon(
                "minecraft:stonecutter_block",
                trFor(p, "abp.stonecutter.title", "Stonecutter"),
                {trFor(p, "abp.stonecutter.help_input", "§7Put blocks into the slot below"),
                 trFor(p, "abp.stonecutter.help_select", "§7Pick an output on the right"),
                 trFor(p, "abp.stonecutter.help_click", "§7Click the result to cut one"),
                 trFor(p, "abp.stonecutter.help_shift", "§7Shift-click the result to cut as many as possible")}
            );
        };
        info->onClick = [](ui::ClickContext const&) { return true; };
        setSlot(kInfoSlot, info);

        int pages      = pageCount();
        mHasPageButton = pages > 1;
        if (mHasPageButton) {
            setSlot(
                kRecipePageSlot,
                std::make_shared<ui::ButtonSlot>(
                    ui::icon(
                        "minecraft:arrow",
                        trFor(player, "abp.stonecutter.page", "Outputs page {} of {}", mRecipePage + 1, pages),
                        {trFor(player, "abp.stonecutter.next_page_hint", "§7Click for the next page")},
                        mRecipePage + 1
                    ),
                    [this](ui::ClickContext const&) {
                        mRecipePage = (mRecipePage + 1) % pageCount();
                        invalidate();
                    }
                )
            );
        }
    }

private:
    [[nodiscard]] int pageCount() const {
        int n = static_cast<int>(options(mCtx).size());
        return std::max(1, (n + kRecipesPage - 1) / kRecipesPage);
    }

    int  mRecipePage    = 0;
    bool mHasPageButton = false;
};

} // namespace

std::shared_ptr<TabMenu> makeStonecutterMenu(Player& player, UpgradeContext const& ctx) {
    return std::make_shared<StonecutterMenu>(trFor(player, "abp.stonecutter.title", "Stonecutter"), ctx);
}

// ---- upgrade ----------------------------------------------------------------------------------

namespace {

class StonecutterUpgrade : public IUpgrade {
public:
    [[nodiscard]] UpgradeInfo const& info() const override {
        static UpgradeInfo const kInfo{
            .id           = std::string(crafting::kStonecutterId),
            .itemId       = "alleviatedbackpacks:stonecutter_upgrade",
            .nameKey      = "abp.stonecutter.name",
            .nameFallback = "Stonecutter Upgrade",
            .hasSettings  = true,
            .toggleable   = false,
        };
        return kInfo;
    }

    void onRemove(UpgradeContext const& ctx) override {
        // The input goes back into the backpack (or to the player). It only stays with the
        // upgrade item when it fits nowhere and there is no player to give it to.
        auto item = inputOf(ctx);
        if (!item.empty()) setInput(ctx, crafting::giveBack(ctx.player(), ctx.backpack(), item));
    }

    void openSettings(Player& player, UpgradeContext const& ctx) override {
        crafting::switchMenu(player, makeStonecutterMenu(player, ctx.withPlayer(nullptr)));
    }

    [[nodiscard]] std::vector<std::string> describe(Player& player, UpgradeContext const& ctx) const override {
        if (auto option = selectedOption(ctx)) {
            return {trFor(player, "abp.stonecutter.describe_selected", "§7Output: {}", option->result.getName())};
        }
        return {};
    }
};

} // namespace

} // namespace abp::upgrade::stonecutter

ABP_REGISTER_UPGRADE(abp::upgrade::stonecutter::StonecutterUpgrade);

ABP_REGISTER_CRAFTING_TAB(
    abp::upgrade::crafting::TabEntry{
        .upgradeId     = std::string(abp::upgrade::crafting::kStonecutterId),
        .order         = 10,
        .iconItem      = "minecraft:stonecutter_block",
        .labelKey      = "abp.stonecutter.button",
        .labelFallback = "Stonecutter",
        .make          = &abp::upgrade::stonecutter::makeStonecutterMenu,
    }
);
