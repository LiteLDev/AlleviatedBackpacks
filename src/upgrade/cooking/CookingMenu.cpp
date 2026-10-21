// The chest menu of the cooking upgrades.
//
// Plain variants (3 rows):            Auto variants (6 rows):
//   . . I . . . . . K                   F F F F F F F F F   <- input filter
//   . . f . P - - O .                   . . . . L . . . .
//   . . U . . . . . B                   G G G G G G G G G   <- fuel filter
//                                       . . . . . . . . .
// I input, U fuel, f flame (burn),      K I f U P . O . .
// P progress, O output, K info,         m . n . . . . . B   (m/n: filter allow/deny)
// B back, F/G ghost filter slots.

#include <algorithm>

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
        .input            = 37,
        .fuel             = 39,
        .output           = 42,
        .flame            = 38,
        .progress         = 40,
        .back             = 53,
        .inputFilterStart = 0,
        .fuelFilterStart  = 18,
        .inputModeToggle  = 45,
        .fuelModeToggle   = 47,
    };
    return automatic ? autoLayout : plain;
}

namespace {

class CookingMenu : public ui::Menu {
public:
    CookingMenu(std::string title, CookingUpgrade& upgrade, UpgradeContext ctx)
    : Menu(std::move(title), upgrade.automatic() ? Rows::Six : Rows::Three),
      mUpgrade(upgrade),
      mCtx(std::move(ctx)) {}

    void build(Player& player) override;
    void onTick(Player& player) override {
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
};

ItemStack filler(char const* id = "minecraft:gray_stained_glass_pane") { return ui::icon(id, " "); }

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
        if (which == 0 && d.empty()) st.cook = 0;
        CookingUpgrade::setState(mCtx, st);
        // Like a furnace's result slot: taking the results awards their experience.
        if (which == 2 && !mUpgrade.automatic()) {
            if (int xp = xpFor(got, n); xp > 0) p.addExperience(xp);
        }
        return game::ItemConv::toStack(got, n);
    };
    slot->onAccept = [this, field, which](Player&, ItemStack const& stack) -> int {
        if (!alive() || stack.isNull() || which == 2) return 0;
        auto offered = game::ItemConv::toData(stack);
        if (!acceptsItem(mUpgrade, which, offered)) return 0;
        auto  st = CookingUpgrade::state(mCtx);
        auto& d  = field(st);
        if (!d.empty() && !d.sameItem(offered)) return 0;
        int room = std::max(offered.maxStack, 1) - (d.empty() ? 0 : d.count);
        int n    = std::min(room, offered.count);
        if (n <= 0) return 0;
        if (which == 0 && d.empty()) st.cook = 0;
        d = offered.withCount((d.empty() ? 0 : d.count) + n);
        CookingUpgrade::setState(mCtx, st);
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
            std::max(1, pct)
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
            pct > 0 ? "minecraft:spectral_arrow" : "minecraft:arrow",
            trFor(p, "abp.cooking.progress", "Cooking: {}%", pct),
            lore,
            std::max(1, pct)
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
    int  infoSlot = mUpgrade.automatic() ? 36 : 8;
    auto speed    = burnInterval(mUpgrade.kind()) < 200 ? trFor(player, "abp.cooking.info.fast", "§7Twice as fast as a furnace")
                                                        : trFor(player, "abp.cooking.info.normal", "§7Works like a furnace");
    std::vector<std::string> lore{speed};
    if (mUpgrade.automatic()) {
        lore.push_back(trFor(player, "abp.cooking.info.auto", "§7Takes inputs and fuel from the backpack and stores the results"));
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
        setSlot(
            13,
            std::make_shared<ui::StaticSlot>(ui::icon(
                "minecraft:paper",
                trFor(player, "abp.cooking.filter.label", "↑ Input filter · ↓ Fuel filter"),
                {trFor(player, "abp.cooking.filter.help", "§7Place items to match; empty = anything"),
                 trFor(player, "abp.cooking.filter.help_fuel", "§7With an empty fuel filter, cookable inputs are never burnt")}
            ))
        );
        for (int i = 45; i < 54; ++i) setSlot(i, std::make_shared<ui::StaticSlot>(filler("minecraft:black_stained_glass_pane")));
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
