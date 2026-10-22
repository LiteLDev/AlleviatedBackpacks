// Refill settings menu (chest UI of simulated players; players get the native tab): ghost filter
// slots, per-slot target buttons (advanced), clear, back to the backpack's upgrades page.
//
// Basic (single chest):            Advanced (double chest):
//   row 0  ghosts 1-6                row 0  ghosts 1-9
//   row 1  filler                    row 1  targets of ghosts 1-9
//   row 2  controls                  row 2  ghosts 10-12 (centred)
//                                    row 3  targets of ghosts 10-12
//                                    row 4  filler
//                                    row 5  controls

#include <algorithm>
#include <memory>
#include <string>

#include "mc/world/actor/player/Player.h"

#include "mod/I18n.h"
#include "mod/Log.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/Menu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/refill/Refill.h"

namespace abp::upgrade::refill {

namespace {

std::string targetDescription(Player& player, Target t) {
    switch (t) {
    case Target::Any:
        return trFor(player, "abp.refill.target_slot.any.tooltip", "Any slot");
    case Target::MainHand:
        return trFor(player, "abp.refill.target_slot.main_hand.tooltip", "Main hand");
    case Target::Offhand:
        return trFor(player, "abp.refill.target_slot.off_hand.tooltip", "Off hand");
    default:
        return trFor(player, "abp.refill.target_slot.hotbar.tooltip", "Hotbar slot {}", static_cast<int>(t) - static_cast<int>(Target::Hotbar1) + 1);
    }
}

ItemStack targetIcon(Player& player, Target t) {
    auto name = trFor(player, "abp.refill.target_slot.tooltip", "Refill {}", "§2" + targetDescription(player, t));
    std::vector<std::string> lore{trFor(player, "abp.refill.target.click", "§7Click to change the target slot")};
    switch (t) {
    case Target::Any:
        return ui::icon("minecraft:chest", name, lore);
    case Target::MainHand:
        return ui::icon("minecraft:iron_sword", name, lore);
    case Target::Offhand:
        return ui::icon("minecraft:shield", name, lore);
    default:
        return ui::icon("minecraft:frame", name, lore, static_cast<int>(t) - static_cast<int>(Target::Hotbar1) + 1);
    }
}

class RefillMenu final : public ui::Menu {
public:
    RefillMenu(std::string title, UpgradeContext ctx, bool advanced)
    : Menu(std::move(title), advanced ? Rows::Six : Rows::Three),
      mCtx(std::move(ctx)),
      mAdvanced(advanced),
      // The phase-1 chest layout only fits SB's default counts (6 / 12).
      mSlots(std::min(slotsFor(advanced), advanced ? kAdvancedSlots : kBasicSlots)) {}

    void build(Player& player) override {
        auto filler = [](std::string_view id) { return std::make_shared<ui::StaticSlot>(ui::icon(id, " ")); };
        for (int i = 0; i < size(); ++i) setSlot(i, filler("minecraft:gray_stained_glass_pane"));
        if (!valid()) return;
        auto const current = load(mCtx, mSlots);

        for (int i = 0; i < mSlots; ++i) {
            setSlot(
                ghostPos(i),
                std::make_shared<ui::GhostSlot>(
                    [this, i] {
                        if (!valid()) return core::ItemStackData{};
                        return load(mCtx, mSlots).filter.ghosts[static_cast<std::size_t>(i)];
                    },
                    [this, i](core::ItemStackData const& d) {
                        if (!valid()) return;
                        (void)setGhost(mCtx, mAdvanced, i, d);
                        invalidate();
                    }
                )
            );
            if (mAdvanced) {
                auto t = current.targets[static_cast<std::size_t>(i)];
                setSlot(
                    targetPos(i),
                    std::make_shared<ui::ButtonSlot>(targetIcon(player, t), [this, i](ui::ClickContext const&) {
                        if (!valid()) return;
                        auto t = nextTarget(load(mCtx, mSlots).targets[static_cast<std::size_t>(i)]);
                        setTarget(mCtx, mAdvanced, i, t);
                        log().info("refill: filter slot {} target set to {}", i + 1, static_cast<int>(t));
                        invalidate();
                    })
                );
            }
        }

        int         control = size() - 9;
        for (int i = 0; i < 9; ++i) setSlot(control + i, filler("minecraft:black_stained_glass_pane"));
        setSlot(
            control + 7,
            std::make_shared<ui::ButtonSlot>(
                ui::icon("minecraft:barrier", trFor(player, "abp.filter.clear", "Clear filter")),
                [this](ui::ClickContext const&) {
                    if (!valid()) return;
                    auto s = load(mCtx, mSlots);
                    for (auto& g : s.filter.ghosts) g.clear();
                    for (auto& t : s.targets) t = Target::Any;
                    save(mCtx, s);
                    invalidate();
                }
            )
        );
        setSlot(
            control + 8,
            std::make_shared<ui::ButtonSlot>(
                ui::icon("minecraft:arrow", trFor(player, "abp.menu.back_generic", "Back")),
                [bp = mCtx.backpackPtr()](ui::ClickContext const& c) {
                    ui::BackpackMenu::openBackpack(c.player, bp, {}, ui::BackpackMenu::View::Upgrades);
                }
            )
        );
    }

    void onTick(Player& player) override {
        // The upgrade was removed (or swapped) while this menu was open.
        if (!valid()) ui::MenuManager::get().close(player);
    }

private:
    [[nodiscard]] bool valid() const {
        auto const& slots = mCtx.backpack().data().upgrades;
        if (mCtx.slot() < 0 || mCtx.slot() >= static_cast<int>(slots.size())) return false;
        auto* u = upgradeFor(mCtx.state().item);
        return u && u->info().id == (mAdvanced ? kAdvancedId : kBasicId);
    }
    [[nodiscard]] int ghostPos(int i) const {
        if (!mAdvanced || i < 9) return i;
        return 18 + 3 + (i - 9);
    }
    [[nodiscard]] int targetPos(int i) const { return ghostPos(i) + 9; }

    UpgradeContext mCtx;
    bool           mAdvanced;
    int            mSlots;
};

} // namespace

std::shared_ptr<ui::Menu> makeMenu(Player& player, UpgradeContext const& ctx, bool advanced) {
    auto* type  = upgradeFor(ctx.state().item);
    auto  title = type ? displayName(*type, &player) : trFor(player, "abp.refill.name", "Refill Upgrade");
    return std::make_shared<RefillMenu>(std::move(title), ctx, advanced);
}

} // namespace abp::upgrade::refill
