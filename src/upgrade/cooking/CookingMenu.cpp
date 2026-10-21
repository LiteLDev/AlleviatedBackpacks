// The chest menu of the cooking upgrades: a single chest, like other upgrade settings menus
// (switching from the backpack's double chest to another double chest in place does not show the
// new screen on the client).
//
// Plain variants:          Auto variants:
//   . . I . . . . . K        F F F F F F F F F   <- input filter
//   . . f . P - - O .        G G G G G G G G G   <- fuel filter
//   . . U . . . . . B        K I f U P O m n B   (m/n: input/fuel filter allow/deny)
// I input, U fuel, f flame (burn), P progress, O output, K info (+ filter help), B back.
#include <algorithm>
#include <array>

#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "game/ItemConv.h"
#include "mod/I18n.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/Menu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/cooking/Cooking.h"

namespace abp::upgrade::cooking {

MenuLayout const& layout(bool automatic) {
    static MenuLayout const plain{.input = 2, .fuel = 20, .output = 16, .flame = 11, .progress = 13, .back = 26};
    static MenuLayout const autoLayout{
        .input            = 19,
        .fuel             = 21,
        .output           = 23,
        .flame            = 20,
        .progress         = 22,
        .back             = 26,
        .inputFilterStart = 0,
        .fuelFilterStart  = 9,
        .inputModeToggle  = 24,
        .fuelModeToggle   = 25,
    };
    return automatic ? autoLayout : plain;
}

namespace {

class CookingMenu : public ui::Menu {
public:
    CookingMenu(std::string title, CookingUpgrade& upgrade, UpgradeContext ctx)
    : Menu(std::move(title), Rows::Three),
      mUpgrade(upgrade),
      mCtx(std::move(ctx)) {}

    void build(Player& player) override;
    void onOpen(Player& player) override {
        // Results stored while the backpack was placed: their experience goes to whoever opens this.
        if (mUpgrade.automatic() && alive()) CookingUpgrade::claimPendingXp(mCtx, player);
    }
    void onTick(Player& player) override {
        // Put-backs are only rollbacks of moves within one request (see itemSlot()).
        for (auto& t : mTaken) t.clear();
        mTakenCook = 0;
        if (!mUpgrade.installedAt(mCtx)) ui::MenuManager::get().close(player);
    }

private:
    [[nodiscard]] bool alive() const { return mUpgrade.installedAt(mCtx); }

    std::shared_ptr<ui::Slot> itemSlot(int which); // 0 input, 1 fuel, 2 output
    std::shared_ptr<ui::Slot> flameSlot();
    std::shared_ptr<ui::Slot> progressSlot();
    std::shared_ptr<ui::Slot> filterGhost(bool fuel, int index);
    std::shared_ptr<ui::Slot> modeToggle(bool fuel);

    CookingUpgrade& mUpgrade;
    UpgradeContext  mCtx;
    /// Per item slot: what was taken out during the current request (a rolled-back move may put it
    /// back). Cleared every tick.
    std::array<core::ItemStackData, 3> mTaken;
    int                                mTakenCook = 0; ///< cook progress when the input was emptied
};

ItemStack filler(char const* id = "minecraft:gray_stained_glass_pane") { return ui::icon(id, " "); }

constexpr char const* kXpPaidKey = "xpPaid";

/// How many items of `offered` slot `which` takes (0 = refuse).
bool acceptsItem(CookingUpgrade const& u, int which, core::ItemStackData const& d) {
    if (d.empty()) return false;
    if (which == 0) return recipeResult(u.kind(), d).has_value();
    if (which == 1) return vanillaBurnTicks(u.kind(), d) > 0;
    return false;
}

std::shared_ptr<ui::Slot> CookingMenu::itemSlot(int which) {
    auto slot       = std::make_shared<ui::CallbackSlot>();
    auto field      = [which](CookState& s) -> core::ItemStackData& {
        return which == 0 ? s.input : which == 1 ? s.fuel : s.output;
    };
    slot->onDisplay = [this, field](Player&) -> ItemStack {
        if (!alive()) return {};
        auto st = CookingUpgrade::state(mCtx);
        auto d  = field(st);
        if (d.empty()) return {};
        return game::ItemConv::toStack(d, std::min({d.count, std::max(d.maxStack, 1), 255}));
    };
    slot->onTake = [this, field, which](Player& p, int amount) -> ItemStack {
        if (!alive() || amount <= 0) return {};
        auto  st = CookingUpgrade::state(mCtx);
        auto& d  = field(st);
        if (d.empty()) return {};
        int  n   = std::min({amount, d.count, std::max(d.maxStack, 1), 255});
        auto got = d.withCount(n);
        d        = d.withCount(d.count - n);
        if (which == 0 && d.empty()) {
            mTakenCook = st.cook;
            st.cook    = 0;
        }
        CookingUpgrade::setState(mCtx, st);
        auto& taken = mTaken[static_cast<std::size_t>(which)];
        if (!taken.empty() && taken.sameItem(got)) taken.count += n;
        else taken = got;
        // Like a furnace's result slot: taking the results awards their experience, once: results
        // put back by a rolled-back move are remembered in "xpPaid" (persisted).
        if (which == 2 && !mUpgrade.automatic()) {
            auto& s    = mCtx.settings();
            int   paid = std::min(n, std::max(0, s.value(kXpPaidKey, 0)));
            if (paid > 0) {
                s[kXpPaidKey] = s.value(kXpPaidKey, 0) - paid;
                mCtx.saveSettings(false);
            }
            if (int xp = xpFor(got, n - paid); xp > 0) p.addExperience(xp);
        }
        return game::ItemConv::toStack(got, n);
    };
    slot->onAccept = [this, field, which](Player&, ItemStack const& stack) -> int {
        if (!alive() || stack.isNull()) return 0;
        auto  offered = game::ItemConv::toData(stack);
        auto& taken   = mTaken[static_cast<std::size_t>(which)];
        // Items just taken from this slot always fit back: the menu framework rolls failed moves
        // back through accept(), and that must never lose items (results, the lava bucket).
        bool restoring = !taken.empty() && taken.sameItem(offered) && offered.count <= taken.count;
        if (!restoring && (which == 2 || !acceptsItem(mUpgrade, which, offered))) return 0;
        auto  st = CookingUpgrade::state(mCtx);
        auto& d  = field(st);
        if (!d.empty() && !d.sameItem(offered)) return 0;
        bool wasEmpty = d.empty();
        int  have     = wasEmpty ? 0 : d.count;
        int  room     = restoring ? offered.count : std::max(offered.maxStack, 1) - have;
        int  n        = std::min(room, offered.count);
        if (n <= 0) return 0;
        d = offered.withCount(have + n);
        if (which == 0 && wasEmpty) st.cook = restoring ? mTakenCook : 0;
        CookingUpgrade::setState(mCtx, st);
        if (restoring) {
            taken = taken.withCount(taken.count - n);
            if (which == 2 && !mUpgrade.automatic()) {
                mCtx.settings()[kXpPaidKey] = std::max(0, mCtx.settings().value(kXpPaidKey, 0)) + n;
                mCtx.saveSettings(false);
            }
        }
        return n;
    };
    return slot;
}

std::shared_ptr<ui::Slot> CookingMenu::flameSlot() {
    auto slot       = std::make_shared<ui::CallbackSlot>();
    slot->onClick   = [](ui::ClickContext const&) { return true; };
    slot->onDisplay = [this](Player& p) -> ItemStack {
        if (!alive()) return {};
        auto st = CookingUpgrade::state(mCtx);
        if (!st.burning()) {
            return ui::icon(
                "minecraft:gunpowder",
                trFor(p, "abp.cooking.flame.off", "Not burning"),
                {trFor(p, "abp.cooking.flame.hint", "§7Put fuel below the flame")}
            );
        }
        int pct = st.burnPercent();
        return ui::icon(
            "minecraft:blaze_powder",
            trFor(p, "abp.cooking.flame.on", "§6Burning: {}%", pct),
            {trFor(p, "abp.cooking.flame.left", "§7Fuel left: {}s", (st.burn + 19) / 20)},
            std::max(1, pct * 64 / 100) // count as a 64-step bar (the exact % is in the name)
        );
    };
    return slot;
}

std::shared_ptr<ui::Slot> CookingMenu::progressSlot() {
    auto slot       = std::make_shared<ui::CallbackSlot>();
    slot->onClick   = [](ui::ClickContext const&) { return true; };
    slot->onDisplay = [this](Player& p) -> ItemStack {
        if (!alive()) return {};
        auto st  = CookingUpgrade::state(mCtx);
        int  pct = st.cookPercent();
        std::vector<std::string> lore;
        if (st.cookTotal > 0) {
            lore.push_back(trFor(p, "abp.cooking.progress.time", "§7{}s per item", (st.cookTotal + 19) / 20));
        }
        if (!st.input.empty() && !recipeResult(mUpgrade.kind(), st.input.withCount(1))) {
            lore.push_back(trFor(p, "abp.cooking.progress.no_recipe", "§cThis can't be cooked here"));
        }
        return ui::icon(
            "minecraft:arrow",
            trFor(p, "abp.cooking.progress", "Cooking: {}%", pct),
            lore,
            std::max(1, pct * 64 / 100) // count as a 64-step bar (the exact % is in the name)
        );
    };
    return slot;
}

std::shared_ptr<ui::Slot> CookingMenu::filterGhost(bool fuel, int index) {
    return std::make_shared<ui::GhostSlot>(
        [this, fuel, index]() -> core::ItemStackData {
            if (!alive()) return {};
            auto f = fuel ? CookingUpgrade::fuelFilter(mCtx) : CookingUpgrade::inputFilter(mCtx);
            return index < static_cast<int>(f.ghosts.size()) ? f.ghosts[static_cast<std::size_t>(index)] : core::ItemStackData{};
        },
        [this, fuel, index](core::ItemStackData const& d) {
            if (!alive()) return;
            auto f = fuel ? CookingUpgrade::fuelFilter(mCtx) : CookingUpgrade::inputFilter(mCtx);
            if (index >= static_cast<int>(f.ghosts.size())) return;
            if (!d.empty() && f.hasGhost(d)) return;
            f.setGhost(index, d);
            if (fuel) CookingUpgrade::setFuelFilter(mCtx, f);
            else CookingUpgrade::setInputFilter(mCtx, f);
        }
    );
}

std::shared_ptr<ui::Slot> CookingMenu::modeToggle(bool fuel) {
    auto slot       = std::make_shared<ui::CallbackSlot>();
    slot->onDisplay = [this, fuel](Player& p) -> ItemStack {
        if (!alive()) return {};
        auto f     = fuel ? CookingUpgrade::fuelFilter(mCtx) : CookingUpgrade::inputFilter(mCtx);
        bool allow = f.mode == core::FilterMode::Allow;
        auto what  = fuel ? trFor(p, "abp.cooking.filter.fuel", "Fuel filter") : trFor(p, "abp.cooking.filter.input", "Input filter");
        auto mode  = allow ? trFor(p, "abp.filter.mode.allow", "Allow list") : trFor(p, "abp.filter.mode.deny", "Deny list");
        return ui::icon(
            allow ? "minecraft:white_wool" : "minecraft:black_wool",
            what + ": " + mode,
            {trFor(p, "abp.filter.click_to_toggle", "§7Click to toggle")}
        );
    };
    slot->onClick = [this, fuel](ui::ClickContext const&) {
        if (!alive()) return true;
        auto f = fuel ? CookingUpgrade::fuelFilter(mCtx) : CookingUpgrade::inputFilter(mCtx);
        f.mode = f.mode == core::FilterMode::Allow ? core::FilterMode::Deny : core::FilterMode::Allow;
        if (fuel) CookingUpgrade::setFuelFilter(mCtx, f);
        else CookingUpgrade::setInputFilter(mCtx, f);
        return true;
    };
    return slot;
}

void CookingMenu::build(Player& player) {
    clearSlots();
    auto const& L = layout(mUpgrade.automatic());
    for (int i = 0; i < size(); ++i) setSlot(i, std::make_shared<ui::StaticSlot>(filler()));

    // Info icon: the vanilla block this upgrade works like.
    int  infoSlot = mUpgrade.automatic() ? 18 : 8;
    auto speed    = burnInterval(mUpgrade.kind()) < 200 ? trFor(player, "abp.cooking.info.fast", "§7Twice as fast as a furnace")
                                                        : trFor(player, "abp.cooking.info.normal", "§7Works like a furnace");
    std::vector<std::string> lore{speed};
    if (mUpgrade.automatic()) {
        lore.push_back(trFor(player, "abp.cooking.info.auto", "§7Takes inputs and fuel from the backpack and stores the results"));
        lore.push_back(trFor(player, "abp.cooking.filter.label", "Top row: input filter · second row: fuel filter"));
        lore.push_back(trFor(player, "abp.cooking.filter.help", "§7Place items to match; empty = anything"));
        lore.push_back(trFor(player, "abp.cooking.filter.help_fuel", "§7Cookable inputs are burnt only if the fuel allow list names them"));
        lore.push_back(trFor(player, "abp.cooking.filter.help_products", "§7Its own results are not cooked again unless the input allow list names them"));
    } else {
        lore.push_back(trFor(player, "abp.cooking.info.manual", "§7Runs while you carry the backpack"));
    }
    setSlot(infoSlot, std::make_shared<ui::StaticSlot>(ui::icon(blockId(mUpgrade.kind()), displayName(mUpgrade, &player), lore)));

    setSlot(L.input, itemSlot(0));
    setSlot(L.fuel, itemSlot(1));
    setSlot(L.output, itemSlot(2));
    setSlot(L.flame, flameSlot());
    setSlot(L.progress, progressSlot());
    if (!mUpgrade.automatic()) {
        // Arrow-ish track between the flame and the output.
        setSlot(L.progress + 1, std::make_shared<ui::StaticSlot>(filler("minecraft:orange_stained_glass_pane")));
        setSlot(L.progress + 2, std::make_shared<ui::StaticSlot>(filler("minecraft:orange_stained_glass_pane")));
    }

    if (mUpgrade.automatic()) {
        int inSlots   = static_cast<int>(CookingUpgrade::inputFilter(mCtx).ghosts.size());
        int fuelSlots = static_cast<int>(CookingUpgrade::fuelFilter(mCtx).ghosts.size());
        for (int i = 0; i < 9; ++i) {
            if (i < inSlots) setSlot(L.inputFilterStart + i, filterGhost(false, i));
            else setSlot(L.inputFilterStart + i, std::make_shared<ui::StaticSlot>(filler("minecraft:black_stained_glass_pane")));
            if (i < fuelSlots) setSlot(L.fuelFilterStart + i, filterGhost(true, i));
            else setSlot(L.fuelFilterStart + i, std::make_shared<ui::StaticSlot>(filler("minecraft:black_stained_glass_pane")));
        }
        setSlot(L.inputModeToggle, modeToggle(false));
        setSlot(L.fuelModeToggle, modeToggle(true));
    }

    setSlot(
        L.back,
        std::make_shared<ui::ButtonSlot>(
            ui::icon("minecraft:arrow", trFor(player, "abp.menu.back_generic", "Back")),
            [bp = mCtx.backpackPtr()](ui::ClickContext const& c) {
                ui::BackpackMenu::openBackpack(c.player, bp, {}, ui::BackpackMenu::View::Upgrades);
            }
        )
    );
}

} // namespace

std::shared_ptr<ui::Menu> makeMenu(Player& player, CookingUpgrade& upgrade, UpgradeContext const& ctx) {
    auto menu = std::make_shared<CookingMenu>(displayName(upgrade, &player), upgrade, ctx.withPlayer(nullptr));
    // Keep the backpack item in place while its upgrade menu is open.
    for (auto const& [carried, bp] : game::carriedBackpacks(player)) {
        if (bp == ctx.backpackPtr() && carried.where.kind == game::ItemLocation::Kind::Inventory) {
            menu->lockedInventorySlot = carried.where.slot;
            break;
        }
    }
    return menu;
}

} // namespace abp::upgrade::cooking
