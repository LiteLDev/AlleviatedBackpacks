// Refill settings menu: ghost filter slots, per-slot target buttons (advanced), NBT toggle, clear,
// back to the backpack's upgrades page.
//
// Basic (single chest):            Advanced (double chest):
//   row 0  ghosts 1-9                row 0  ghosts 1-9
//   row 1  filler                    row 1  targets of ghosts 1-9
//   row 2  controls                  row 2  ghosts 10-12 (centred)
//                                    row 3  targets of ghosts 10-12
//                                    row 4  filler
//                                    row 5  controls

#include <memory>
#include <string>

#include "ll/api/chrono/GameChrono.h"
#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/thread/ServerThreadExecutor.h"

#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"

#include "mod/I18n.h"
#include "mod/Log.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/Menu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/refill/Refill.h"

namespace abp::upgrade::refill {

namespace {

constexpr int kReopenDelayTicks = 15;

ItemStack targetIcon(Player& player, Target t) {
    std::vector<std::string> lore{trFor(player, "abp.refill.target.click", "§7Click to change the target slot")};
    switch (t) {
    case Target::Any:
        return ui::icon("minecraft:chest", trFor(player, "abp.refill.target.any", "Target: any slot"), lore);
    case Target::MainHand:
        return ui::icon(
            "minecraft:iron_sword",
            trFor(player, "abp.refill.target.main_hand", "Target: main hand"),
            lore
        );
    case Target::Offhand:
        return ui::icon("minecraft:shield", trFor(player, "abp.refill.target.offhand", "Target: offhand"), lore);
    default: {
        int k = static_cast<int>(t) - static_cast<int>(Target::Hotbar1) + 1;
        return ui::icon(
            "minecraft:frame",
            trFor(player, "abp.refill.target.hotbar", "Target: hotbar slot {}", k),
            lore,
            k
        );
    }
    }
}

class RefillMenu final : public ui::Menu {
public:
    RefillMenu(std::string title, UpgradeContext ctx, bool advanced)
    : Menu(std::move(title), advanced ? Rows::Six : Rows::Three),
      mCtx(std::move(ctx)),
      mAdvanced(advanced),
      mSlots(slotsFor(advanced)) {}

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
                        auto s = load(mCtx, mSlots);
                        if (!d.empty() && s.filter.hasGhost(d)) return; // no duplicates
                        s.filter.setGhost(i, d);
                        save(mCtx, s);
                    }
                )
            );
            if (mAdvanced) {
                auto t = current.targets[static_cast<std::size_t>(i)];
                setSlot(
                    targetPos(i),
                    std::make_shared<ui::ButtonSlot>(targetIcon(player, t), [this, i](ui::ClickContext const&) {
                        if (!valid()) return;
                        auto  s = load(mCtx, mSlots);
                        auto& t = s.targets[static_cast<std::size_t>(i)];
                        t       = nextTarget(t);
                        save(mCtx, s);
                        log().info("refill: filter slot {} target set to {}", i + 1, static_cast<int>(t));
                        invalidate();
                    })
                );
            }
        }

        int         control = size() - 9;
        auto const& s       = current;
        for (int i = 0; i < 9; ++i) setSlot(control + i, filler("minecraft:black_stained_glass_pane"));
        std::vector<std::string> help{
            trFor(player, "abp.refill.help1", "§7Put items into the slots above."),
            trFor(player, "abp.refill.help2", "§7When you run low on them, they are refilled from the backpack."),
        };
        if (mAdvanced) {
            help.push_back(
                trFor(player, "abp.refill.help3", "§7The button below each slot picks where the item goes.")
            );
            help.push_back(
                trFor(player, "abp.refill.help4", "§7Pick block (middle click) also takes blocks from the backpack.")
            );
        }
        setSlot(
            control,
            std::make_shared<ui::StaticSlot>(
                ui::icon("minecraft:book", trFor(player, "abp.refill.help_title", "Refill"), help)
            )
        );
        auto onOff =
            s.filter.matchNbt ? trFor(player, "abp.filter.on", "§aOn") : trFor(player, "abp.filter.off", "§cOff");
        setSlot(
            control + 3,
            std::make_shared<ui::ButtonSlot>(
                ui::icon("minecraft:enchanted_book", trFor(player, "abp.filter.nbt", "Match NBT: {}", onOff)),
                [this](ui::ClickContext const&) {
                    if (!valid()) return;
                    auto s            = load(mCtx, mSlots);
                    s.filter.matchNbt = !s.filter.matchNbt;
                    save(mCtx, s);
                    invalidate();
                }
            )
        );
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
                    switchMenu(c.player, [bp](Player& p) {
                        ui::BackpackMenu::openBackpack(p, bp, {}, ui::BackpackMenu::View::Upgrades);
                    });
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

void switchMenu(Player& player, std::function<void(Player&)> open) {
    auto& mm = ui::MenuManager::get();
    if (!mm.current(player)) {
        open(player);
        return;
    }
    // Swapping one chest menu for another right away doesn't show the new one on a real client:
    // close the current one, give the client time to finish closing the screen, then open.
    mm.closeThen(player, [open = std::move(open)](Player& p) {
        auto id = p.getOrCreateUniqueID();
        ll::coro::keepThis([id, open]() -> ll::coro::CoroTask<> {
            co_await ll::chrono::ticks(kReopenDelayTicks);
            auto level = ll::service::getLevel();
            if (!level) co_return;
            if (auto* pl = level->getPlayer(id); pl && !ui::MenuManager::get().current(*pl)) open(*pl);
        }).launch(ll::thread::ServerThreadExecutor::getDefault());
    });
}

void openMenu(Player& player, std::shared_ptr<ui::Menu> menu) {
    switchMenu(player, [menu = std::move(menu)](Player& p) { ui::MenuManager::get().open(p, menu); });
}

std::shared_ptr<ui::Menu> makeMenu(Player& player, UpgradeContext const& ctx, bool advanced) {
    auto* type  = upgradeFor(ctx.state().item);
    auto  title = type ? displayName(*type, &player) : trFor(player, "abp.refill.name", "Refill Upgrade");
    return std::make_shared<RefillMenu>(std::move(title), ctx, advanced);
}

} // namespace abp::upgrade::refill
