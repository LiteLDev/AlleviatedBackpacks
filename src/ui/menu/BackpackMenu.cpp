#include "ui/menu/BackpackMenu.h"

#include <algorithm>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerUseItemEvent.h"
#include "ll/api/form/SimpleForm.h"

#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/PlayerUISlot.h"
#include "mc/world/item/ItemStack.h"

#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/Storage.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"

namespace abp::ui {

namespace {
constexpr std::string_view kFiller = "minecraft:gray_stained_glass_pane";

std::shared_ptr<Slot> filler() { return std::make_shared<StaticSlot>(icon(kFiller, " ")); }

std::vector<BackpackButton const*> sortedButtons(BackpackMenuContext const& ctx) {
    std::vector<BackpackButton const*> out;
    for (auto const& b : BackpackButtonRegistry::entries()) {
        if (b.visible && !b.visible(ctx)) continue;
        out.push_back(&b);
    }
    std::stable_sort(out.begin(), out.end(), [](auto* a, auto* b) { return a->order < b->order; });
    return out;
}

std::string tierName(Player& player, core::Tier tier) {
    auto id = game::backpackItemId(tier);
    static constexpr std::string_view fallbacks[] =
        {"Leather Backpack", "Copper Backpack", "Iron Backpack", "Gold Backpack", "Diamond Backpack", "Netherite Backpack"};
    return trFor(player, "abp.backpack." + std::string(core::tierInfo(tier).id), fallbacks[static_cast<int>(tier)]);
}

/// One upgrade slot on the upgrades page.
class UpgradeSlotView : public Slot {
public:
    UpgradeSlotView(std::shared_ptr<game::Backpack> bp, int index) : mBackpack(std::move(bp)), mIndex(index) {}

    [[nodiscard]] ItemStack display(Player& player) const override {
        auto const& s = mBackpack->data().upgrades[static_cast<std::size_t>(mIndex)];
        if (s.empty()) return {};
        auto  stack = game::ItemConv::toStack(s.item, 1);
        auto* type  = upgrade::upgradeFor(s.item);
        std::vector<std::string> lore;
        lore.push_back(
            s.enabled ? trFor(player, "abp.menu.upgrade.enabled", "§aEnabled")
                      : trFor(player, "abp.menu.upgrade.disabled", "§cDisabled")
        );
        if (type && !upgrade::typeAllowed(*type)) {
            lore.push_back(trFor(player, "abp.admin.upgrade_disabled_lore", "§cDisabled on this server"));
        }
        if (type) {
            auto extra = type->describe(player, upgrade::UpgradeContext(mBackpack, mIndex, &player));
            lore.insert(lore.end(), extra.begin(), extra.end());
        }
        game::ItemConv::setDisplay(stack, type ? "§r" + upgrade::displayName(*type, &player) : std::string{}, lore);
        return stack;
    }

    /// Clicking an occupied upgrade slot with an item on the cursor: either the installed upgrade
    /// uses the item (`IUpgrade::useItemOn`, e.g. a conversion item) or the two are swapped in one
    /// step (`upgrade::replace`). Other interactions use take()/accept().
    bool click(ClickContext const& c) override {
        if ((c.type != ClickType::Swap && c.type != ClickType::Place) || c.offered.isNull()) return false;
        auto const& s = mBackpack->data().upgrades[static_cast<std::size_t>(mIndex)];
        if (s.empty()) return false;
        ItemStack cursor = c.player.mPlayerUIContainer->getItem(0);
        // Only when the offered stack is exactly the cursor (the usual chest-screen click).
        if (cursor.isNull() || !cursor.matchesItem(c.offered) || cursor.mCount != c.offered.mCount) return false;
        auto offered = game::ItemConv::toData(c.offered);
        auto setCursor = [&](ItemStack item) {
            c.player.setPlayerUIItem(PlayerUISlot::CursorSelected, item, false);
        };
        if (auto* type = upgrade::upgradeFor(s.item)) {
            auto used = type->useItemOn(c.player, upgrade::UpgradeContext(mBackpack, mIndex, &c.player), offered);
            if (used) {
                if (*used > 0) {
                    int left = std::max(0, static_cast<int>(cursor.mCount) - *used);
                    if (left == 0) cursor = ItemStack{};
                    else cursor.set(left);
                    setCursor(cursor);
                }
                return true;
            }
        }
        if (c.type != ClickType::Swap) return false;
        std::string reason;
        auto        old = upgrade::replace(mBackpack, mIndex, offered, &c.player, &reason);
        if (!old) {
            if (!reason.empty()) c.player.sendMessage(reason);
            return true;
        }
        auto oldStack = game::ItemConv::toStack(*old, 1);
        if (cursor.mCount <= 1) {
            setCursor(oldStack);
        } else {
            cursor.set(cursor.mCount - 1);
            setCursor(cursor);
            if (!c.player.add(oldStack)) c.player.drop(oldStack, false);
        }
        return true;
    }

    ItemStack take(Player& player, int) override {
        std::string reason;
        auto        item = upgrade::remove(mBackpack, mIndex, &player, &reason);
        if (!item) {
            if (!reason.empty()) player.sendMessage(reason);
            return {};
        }
        return game::ItemConv::toStack(*item, 1);
    }

    int accept(Player& player, ItemStack const& stack) override {
        if (stack.isNull()) return 0;
        std::string reason;
        if (!upgrade::install(mBackpack, mIndex, game::ItemConv::toData(stack).withCount(1), &player, &reason)) {
            if (!reason.empty()) player.sendMessage(reason);
            return 0;
        }
        return 1;
    }

private:
    std::shared_ptr<game::Backpack> mBackpack;
    int                             mIndex;
};
/// A storage slot whose display runs through the registered `BackpackSlotDecorator`s.
class DecoratedStorageSlot : public StorageSlot {
public:
    using Decorators = std::vector<BackpackSlotDecorator const*>;

    DecoratedStorageSlot(std::shared_ptr<game::Backpack> bp, int index, BackpackMenu* menu, std::shared_ptr<Decorators const> decorators)
    : StorageSlot(bp, index),
      mBackpack(std::move(bp)),
      mMenu(menu),
      mDecorators(std::move(decorators)) {}

    /// The registered decorators in `order` (computed once per page build).
    static std::shared_ptr<Decorators const> sorted() {
        auto out = std::make_shared<Decorators>();
        for (auto const& d : BackpackSlotDecoratorRegistry::entries()) {
            if (d.decorate) out->push_back(&d);
        }
        std::stable_sort(out->begin(), out->end(), [](auto* a, auto* b) { return a->order < b->order; });
        return out;
    }

    [[nodiscard]] ItemStack display(Player& player) const override {
        auto                shown = StorageSlot::display(player);
        BackpackMenuContext ctx{player, *mMenu, mBackpack};
        for (auto const* d : *mDecorators) d->decorate(ctx, index(), shown);
        return shown;
    }

private:
    std::shared_ptr<game::Backpack>   mBackpack;
    BackpackMenu*                     mMenu; ///< owns this slot
    std::shared_ptr<Decorators const> mDecorators;
};
} // namespace

BackpackMenu::BackpackMenu(std::shared_ptr<game::Backpack> backpack, std::optional<game::ItemLocation> source, std::string title)
: Menu(std::move(title), Rows::Six),
  mBackpack(std::move(backpack)),
  mSource(source) {
    if (mSource && mSource->kind == game::ItemLocation::Kind::Inventory) lockedInventorySlot = mSource->slot;
}

int BackpackMenu::pageCount() const {
    return std::max(1, (mBackpack->inventory().size() + kPageSlots - 1) / kPageSlots);
}

void BackpackMenu::showPage(int page) {
    mPage = std::clamp(page, 0, pageCount() - 1);
    mView = View::Storage;
    ++mLayout;
    invalidate();
}

void BackpackMenu::showView(View view) {
    mView = view;
    ++mLayout;
    invalidate();
}

std::optional<std::uint64_t> BackpackMenu::contentVersion() const { return (mBackpack->revision() << 12) ^ mLayout; }

void BackpackMenu::build(Player& player) {
    if (mView == View::Storage) buildStorage(player);
    else buildUpgrades(player);
    buildControls(player);
}

void BackpackMenu::buildStorage(Player&) {
    int  size       = mBackpack->inventory().size();
    auto decorators = DecoratedStorageSlot::sorted();
    bool decorated  = !decorators->empty();
    for (int i = 0; i < kPageSlots; ++i) {
        int index = mPage * kPageSlots + i;
        if (index < size && decorated) setSlot(i, std::make_shared<DecoratedStorageSlot>(mBackpack, index, this, decorators));
        else if (index < size) setSlot(i, std::make_shared<StorageSlot>(mBackpack, index));
        else setSlot(i, filler());
    }
}

void BackpackMenu::buildUpgrades(Player& player) {
    auto const& slots = mBackpack->data().upgrades;
    for (int i = 0; i < kPageSlots; ++i) setSlot(i, filler());
    for (int i = 0; i < 9; ++i) {
        if (i >= static_cast<int>(slots.size())) continue;
        setSlot(i, std::make_shared<UpgradeSlotView>(mBackpack, i));
        auto const& s    = slots[static_cast<std::size_t>(i)];
        auto*       type = upgrade::upgradeFor(s.item);
        if (!type) {
            setSlot(9 + i, std::make_shared<StaticSlot>(icon(kFiller, trFor(player, "abp.menu.upgrade.empty_slot", "Empty upgrade slot"))));
            continue;
        }
        if (type->info().toggleable) {
            bool on = s.enabled;
            setSlot(
                9 + i,
                std::make_shared<ButtonSlot>(
                    icon(
                        on ? "minecraft:lime_dye" : "minecraft:gray_dye",
                        on ? trFor(player, "abp.menu.upgrade.turn_off", "Enabled - click to turn off")
                           : trFor(player, "abp.menu.upgrade.turn_on", "Disabled - click to turn on")
                    ),
                    [bp = mBackpack, i, this](ClickContext const&) {
                        auto const& st = bp->data().upgrades[static_cast<std::size_t>(i)];
                        upgrade::setEnabled(bp, i, !st.enabled);
                        invalidate();
                    }
                )
            );
        }
        if (type->info().hasSettings) {
            setSlot(
                18 + i,
                std::make_shared<ButtonSlot>(
                    icon("minecraft:repeater", trFor(player, "abp.menu.upgrade.settings", "Upgrade settings")),
                    [bp = mBackpack, i, type](ClickContext const& c) {
                        type->openSettings(c.player, upgrade::UpgradeContext(bp, i, &c.player));
                    }
                )
            );
        }
    }
    setSlot(
        31,
        std::make_shared<StaticSlot>(icon(
            "minecraft:book",
            trFor(player, "abp.menu.upgrades.help_title", "Upgrades"),
            {trFor(player, "abp.menu.upgrades.help1", "§7Put upgrade items into the top row."),
             trFor(player, "abp.menu.upgrades.help2", "§7Take them out to remove them.")}
        ))
    );
}

void BackpackMenu::buildControls(Player& player) {
    for (int i = kPageSlots; i < size(); ++i) setSlot(i, filler());
    BackpackMenuContext ctx{player, *this, mBackpack};
    if (mView == View::Upgrades) {
        setSlot(45, std::make_shared<ButtonSlot>(icon("minecraft:chest", trFor(player, "abp.menu.back", "Back to storage")), [this](ClickContext const&) {
            showView(View::Storage);
        }));
    } else {
        int pages = pageCount();
        if (mPage > 0) {
            setSlot(45, std::make_shared<ButtonSlot>(icon("minecraft:arrow", trFor(player, "abp.menu.prev", "Previous page")), [this](ClickContext const&) {
                showPage(mPage - 1);
            }));
        }
        setSlot(46, std::make_shared<StaticSlot>(icon("minecraft:paper", trFor(player, "abp.menu.page", "Page {} of {}", mPage + 1, pages), {}, mPage + 1)));
        if (mPage + 1 < pages) {
            setSlot(47, std::make_shared<ButtonSlot>(icon("minecraft:arrow", trFor(player, "abp.menu.next", "Next page")), [this](ClickContext const&) {
                showPage(mPage + 1);
            }));
        }
        // Feature buttons: slots 48..51. With more than 4, show 3 per page + a "More" pager in 51.
        auto buttons  = sortedButtons(ctx);
        int  total    = static_cast<int>(buttons.size());
        bool paged    = total > 4;
        int  perPage  = paged ? 3 : 4;
        int  btnPages = std::max(1, (total + perPage - 1) / perPage);
        if (mButtonPage >= btnPages) mButtonPage = 0;
        int first = paged ? mButtonPage * perPage : 0;
        for (int i = 0; i < perPage && first + i < total; ++i) {
            auto const* b     = buttons[static_cast<std::size_t>(first + i)];
            auto        shown = b->icon ? b->icon(ctx) : icon("minecraft:stone_button", b->id);
            setSlot(48 + i, std::make_shared<ButtonSlot>(shown, [this, b, bp = mBackpack](ClickContext const& c) {
                if (b->onClick) b->onClick(BackpackMenuContext{c.player, *this, bp}, c);
            }));
        }
        if (paged) {
            std::vector<std::string> lore;
            for (int i = 0; i < total; ++i) {
                if (i >= first && i < first + perPage) continue;
                auto const* b = buttons[static_cast<std::size_t>(i)];
                std::string name;
                if (b->label) name = b->label(ctx);
                else if (b->icon) name = b->icon(ctx).getCustomName();
                lore.push_back("§7- " + (name.empty() ? b->id : name));
            }
            setSlot(
                51,
                std::make_shared<ButtonSlot>(
                    icon(
                        "minecraft:hopper_minecart",
                        trFor(player, "abp.gui.more_buttons", "More buttons ({}/{})", mButtonPage + 1, btnPages),
                        lore
                    ),
                    [this, btnPages](ClickContext const&) {
                        mButtonPage = (mButtonPage + 1) % btnPages;
                        ++mLayout;
                        invalidate();
                    }
                )
            );
        }
    }
    int installed = 0;
    for (auto const& s : mBackpack->data().upgrades) installed += s.empty() ? 0 : 1;
    setSlot(
        52,
        std::make_shared<ButtonSlot>(
            icon(
                "minecraft:smithing_table",
                trFor(player, "abp.menu.upgrades", "Upgrades ({}/{})", installed, mBackpack->data().upgrades.size())
            ),
            [this](ClickContext const&) { showView(mView == View::Upgrades ? View::Storage : View::Upgrades); }
        )
    );
    setSlot(53, std::make_shared<ButtonSlot>(icon("minecraft:comparator", trFor(player, "abp.menu.settings", "Backpack settings")), [this](ClickContext const& c) {
        openSettingsForm(c.player);
    }));
}

void BackpackMenu::openSettingsForm(Player& player) {
    auto bp     = mBackpack;
    auto source = mSource;
    MenuManager::get().closeThen(player, [bp, source](Player& p) {
        std::vector<BackpackSetting const*> entries;
        for (auto const& s : BackpackSettingRegistry::entries()) entries.push_back(&s);
        std::stable_sort(entries.begin(), entries.end(), [](auto* a, auto* b) { return a->order < b->order; });
        ll::form::SimpleForm form(trFor(p, "abp.settings.title", "Backpack settings"));
        if (entries.empty()) form.appendLabel(trFor(p, "abp.settings.none", "No settings available yet."));
        for (auto const* e : entries) {
            form.appendButton(e->label ? e->label(p, *bp) : e->id, [e, bp](Player& pl) {
                if (e->onSelect) e->onSelect(pl, bp);
            });
        }
        form.appendButton(trFor(p, "abp.settings.back", "Back to backpack"), [bp, source](Player& pl) {
            if (source) openFor(pl, *source);
            else openBackpack(pl, bp);
        });
        form.sendTo(p);
    });
}

void BackpackMenu::onTick(Player& player) {
    if (!mSource) return;
    auto const& item = game::itemAt(player, *mSource);
    auto        uuid = game::backpackUuid(item);
    if (!game::isBackpack(item) || !uuid || *uuid != mBackpack->uuid()) MenuManager::get().close(player);
}

void BackpackMenu::onDestroyed() { game::Storage::get().save(*mBackpack); }

int BackpackMenu::quickMoveIn(Player& player, ItemStack const& stack) {
    if (stack.isNull()) return 0;
    auto const& handlers = BackpackQuickMoveRegistry::entries();
    if (!handlers.empty()) {
        std::vector<BackpackQuickMoveHandler const*> sorted;
        for (auto const& h : handlers) {
            if (h.handle) sorted.push_back(&h);
        }
        std::stable_sort(sorted.begin(), sorted.end(), [](auto* a, auto* b) { return a->order < b->order; });
        BackpackMenuContext ctx{player, *this, mBackpack};
        for (auto const* h : sorted) {
            if (auto n = h->handle(ctx, stack)) return *n;
        }
    }
    return quickMoveDefault(player, stack);
}

int BackpackMenu::quickMoveDefault(Player& player, ItemStack const& stack) {
    return mView == View::Upgrades ? quickMoveToUpgrades(player, stack) : quickMoveToStorage(player, stack);
}

int BackpackMenu::quickMoveToUpgrades(Player& player, ItemStack const& stack) {
    if (stack.isNull()) return 0;
    auto data = game::ItemConv::toData(stack).withCount(1);
    for (int i = 0; i < static_cast<int>(mBackpack->data().upgrades.size()); ++i) {
        if (!mBackpack->data().upgrades[static_cast<std::size_t>(i)].empty()) continue;
        if (upgrade::canInstall(mBackpack, i, data, &player)) {
            std::string reason;
            if (upgrade::install(mBackpack, i, data, &player, &reason)) {
                invalidate();
                return 1;
            }
        }
    }
    return 0;
}

int BackpackMenu::quickMoveToStorage(Player& player, ItemStack const& stack) {
    if (stack.isNull()) return 0;
    auto r =mBackpack->inventory().insert(game::ItemConv::toData(stack), core::AccessContext{core::Source::Player, &player});
    return r.inserted + r.voided;
}

bool BackpackMenu::openBackpack(Player& player, std::shared_ptr<game::Backpack> backpack, std::string title, View view) {
    if (!backpack) return false;
    std::optional<game::ItemLocation> source;
    for (auto const& c : game::scanBackpacks(player)) {
        if (c.uuid && *c.uuid == backpack->uuid()) {
            source = c.where;
            if (title.empty()) title = game::itemAt(player, c.where).getCustomName();
            break;
        }
    }
    if (title.empty()) title = tierName(player, backpack->tier());
    auto menu = std::make_shared<BackpackMenu>(std::move(backpack), source, std::move(title));
    if (view != View::Storage) menu->showView(view);
    return MenuManager::get().open(player, std::move(menu));
}

bool BackpackMenu::openCarried(Player& player) {
    using Kind = game::ItemLocation::Kind;
    auto carried = game::scanBackpacks(player);
    auto rank    = [sel = game::selectedSlot(player)](game::ItemLocation const& w) {
        switch (w.kind) {
        case Kind::Armor:
            return 0;
        case Kind::Offhand:
            return 1;
        case Kind::Inventory:
            return w == sel ? 2 : 3;
        }
        return 4;
    };
    std::stable_sort(carried.begin(), carried.end(), [&](auto const& a, auto const& b) { return rank(a.where) < rank(b.where); });
    for (auto const& c : carried) {
        if (openFor(player, c.where)) return true;
    }
    return false;
}

bool BackpackMenu::openFor(Player& player, game::ItemLocation where) {
    auto& storage = game::Storage::get();
    if (!storage.available()) return false;
    ItemStack item = game::itemAt(player, where);
    if (!game::isBackpack(item)) return false;
    bool changed = false;
    auto bp      = storage.forItem(item, &changed);
    if (!bp) return false;
    if (changed) game::setItemAt(player, where, item);
    auto title = item.getCustomName();
    if (title.empty()) title = tierName(player, bp->tier());
    return MenuManager::get().open(player, std::make_shared<BackpackMenu>(bp, where, title));
}

/// Opens a backpack when the player uses (right-clicks) a backpack item.
class BackpackOpener {
public:
    void install() {
        mListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::PlayerUseItemEvent>(
            [](ll::event::PlayerUseItemEvent& ev) {
                auto& player = ev.self();
                if (player.isClientSide() || ev.isCancelled() || !game::isBackpack(ev.item())) return;
                ev.cancel();
                if (MenuManager::get().current(player)) return;
                BackpackMenu::openFor(player, game::selectedSlot(player));
            }
        );
    }
    void uninstall() { ll::event::EventBus::getInstance().removeListener(mListener); }

private:
    ll::event::ListenerPtr mListener;
};

} // namespace abp::ui

ABP_REGISTER_MODULE_ORDERED(abp::ui::BackpackOpener, abp::ModuleOrder::Menu + 1);
