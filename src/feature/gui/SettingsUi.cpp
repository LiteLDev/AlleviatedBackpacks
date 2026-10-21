// Settings form entries (backpack options, memory / no-sort pickers, the player's global options)
// and the slot picker chest menu.

#include <algorithm>

#include "ll/api/form/CustomForm.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "feature/gui/Gui.h"
#include "game/Backpack.h"
#include "game/ItemConv.h"
#include "mod/I18n.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/Menu.h"
#include "ui/menu/MenuManager.h"

namespace abp::feature::gui {

namespace {
using ui::ClickContext;
using ui::ClickType;

// ---- slot picker --------------------------------------------------------------------------------

class SlotPickerMenu;

/// One backpack slot in the picker: shows the slot (and its memory / no-sort state); clicks toggle.
class PickSlot : public ui::Slot {
public:
    PickSlot(std::shared_ptr<game::Backpack> bp, int index, PickMode mode)
    : mBackpack(std::move(bp)),
      mIndex(index),
      mMode(mode) {}

    [[nodiscard]] ItemStack display(Player& player) const override {
        auto&       bp   = *mBackpack;
        auto const& item = bp.inventory().at(mIndex);
        if (mMode == PickMode::Memory) {
            auto const& m = memory(bp)[static_cast<std::size_t>(mIndex)];
            if (!m.empty()) {
                auto s = game::ItemConv::toStack(item.empty() ? m : item, 1);
                game::ItemConv::setDisplay(
                    s,
                    {},
                    {trFor(player, "abp.gui.memory.set", "§bRemembered"),
                     trFor(player, "abp.gui.memory.click_forget", "§7Click to forget")}
                );
                return s;
            }
            if (item.empty()) return {}; // drop an item from the cursor here to remember it
            auto s = game::ItemConv::toStack(item, 1);
            game::ItemConv::setDisplay(s, {}, {trFor(player, "abp.gui.memory.click_set", "§7Click to remember this item")});
            return s;
        }
        bool on = noSort(bp)[static_cast<std::size_t>(mIndex)];
        if (item.empty()) {
            return ui::icon(
                on ? "minecraft:yellow_stained_glass_pane" : "minecraft:light_gray_stained_glass_pane",
                on ? trFor(player, "abp.gui.nosort.on", "§eNo-sort slot") : trFor(player, "abp.gui.nosort.off", "Sorted slot"),
                {on ? trFor(player, "abp.gui.nosort.click_off", "§7Click to sort this slot again")
                    : trFor(player, "abp.gui.nosort.click_on", "§7Click to exclude from sorting")}
            );
        }
        auto s = game::ItemConv::toStack(item, 1);
        game::ItemConv::setDisplay(
            s,
            {},
            {on ? trFor(player, "abp.gui.nosort.on", "§eNo-sort slot") : trFor(player, "abp.gui.nosort.off", "Sorted slot"),
             on ? trFor(player, "abp.gui.nosort.click_off", "§7Click to sort this slot again")
                : trFor(player, "abp.gui.nosort.click_on", "§7Click to exclude from sorting")}
        );
        return s;
    }

    bool click(ClickContext const& ctx) override {
        auto& bp = *mBackpack;
        if (mMode == PickMode::Memory) {
            auto const& m = memory(bp)[static_cast<std::size_t>(mIndex)];
            if (!ctx.offered.isNull() && (ctx.type == ClickType::Place || ctx.type == ClickType::Swap)) {
                setMemory(bp, mIndex, game::ItemConv::toData(ctx.offered)); // remember the cursor item
            } else if (!m.empty()) {
                setMemory(bp, mIndex, {});
            } else if (!bp.inventory().at(mIndex).empty()) {
                setMemory(bp, mIndex, bp.inventory().at(mIndex));
            }
        } else {
            setNoSort(bp, mIndex, !noSort(bp)[static_cast<std::size_t>(mIndex)]);
        }
        return true;
    }

private:
    std::shared_ptr<game::Backpack> mBackpack;
    int                             mIndex;
    PickMode                        mMode;
};

class SlotPickerMenu : public ui::Menu {
public:
    SlotPickerMenu(std::string title, std::shared_ptr<game::Backpack> bp, PickMode mode, int page)
    : Menu(std::move(title), Rows::Six),
      mBackpack(std::move(bp)),
      mMode(mode),
      mPage(page) {}

    [[nodiscard]] int pageCount() const {
        return std::max(1, (mBackpack->inventory().size() + kPage - 1) / kPage);
    }

    void build(Player& player) override {
        clearSlots();
        mPage    = std::clamp(mPage, 0, pageCount() - 1);
        int size = mBackpack->inventory().size();
        for (int i = 0; i < kPage; ++i) {
            int index = mPage * kPage + i;
            if (index < size) setSlot(i, std::make_shared<PickSlot>(mBackpack, index, mMode));
            else setSlot(i, filler());
        }
        for (int i = kPage; i < 54; ++i) setSlot(i, filler());
        if (mPage > 0) {
            setSlot(45, button("minecraft:arrow", trFor(player, "abp.menu.prev", "Previous page"), [this](ClickContext const&) {
                --mPage;
                invalidate();
            }));
        }
        setSlot(46, std::make_shared<ui::StaticSlot>(ui::icon("minecraft:paper", trFor(player, "abp.menu.page", "Page {} of {}", mPage + 1, pageCount()), {}, mPage + 1)));
        if (mPage + 1 < pageCount()) {
            setSlot(47, button("minecraft:arrow", trFor(player, "abp.menu.next", "Next page"), [this](ClickContext const&) {
                ++mPage;
                invalidate();
            }));
        }
        bool memoryMode = mMode == PickMode::Memory;
        setSlot(
            48,
            button(
                "minecraft:lime_dye",
                memoryMode ? trFor(player, "abp.gui.memory.all", "Remember all current items")
                           : trFor(player, "abp.gui.nosort.all", "Exclude all slots from sorting"),
                [this, memoryMode](ClickContext const&) {
                    if (memoryMode) memorizeAll(*mBackpack);
                    else setNoSortAll(*mBackpack, true);
                    invalidate();
                }
            )
        );
        setSlot(
            49,
            button(
                "minecraft:red_dye",
                memoryMode ? trFor(player, "abp.gui.memory.clear", "Forget all") : trFor(player, "abp.gui.nosort.clear", "Sort all slots"),
                [this, memoryMode](ClickContext const&) {
                    if (memoryMode) clearMemory(*mBackpack);
                    else setNoSortAll(*mBackpack, false);
                    invalidate();
                }
            )
        );
        setSlot(
            50,
            std::make_shared<ui::StaticSlot>(ui::icon(
                "minecraft:book",
                memoryMode ? trFor(player, "abp.gui.memory.help_title", "Memory slots")
                           : trFor(player, "abp.gui.nosort.help_title", "No-sort slots"),
                memoryMode ? std::vector<std::string>{trFor(player, "abp.gui.memory.help1", "§7A memory slot only accepts its item."),
                                                      trFor(player, "abp.gui.memory.help2", "§7Click a slot to remember / forget its item,"),
                                                      trFor(player, "abp.gui.memory.help3", "§7or drop an item from the cursor onto it.")}
                           : std::vector<std::string>{trFor(player, "abp.gui.nosort.help1", "§7Sorting never touches no-sort slots."),
                                                      trFor(player, "abp.gui.nosort.help2", "§7Click a slot to toggle it.")}
            ))
        );
        setSlot(53, button("minecraft:chest", trFor(player, "abp.menu.back", "Back to storage"), [this](ClickContext const& c) {
            ui::BackpackMenu::openBackpack(c.player, mBackpack);
        }));
    }

    [[nodiscard]] PickMode mode() const { return mMode; }

private:
    static constexpr int kPage = 45;

    static std::shared_ptr<ui::Slot> filler() {
        return std::make_shared<ui::StaticSlot>(ui::icon("minecraft:gray_stained_glass_pane", " "));
    }
    static std::shared_ptr<ui::Slot> button(std::string_view id, std::string const& name, ui::ButtonSlot::Callback fn) {
        return std::make_shared<ui::ButtonSlot>(ui::icon(id, name), std::move(fn));
    }

    std::shared_ptr<game::Backpack> mBackpack;
    PickMode                        mMode;
    int                             mPage;
};

// ---- forms ----------------------------------------------------------------------------------------

/// Reopens the backpack (bound to the carried item when the player carries it).
void back(Player& player, std::shared_ptr<game::Backpack> const& bp) { ui::BackpackMenu::openBackpack(player, bp); }

std::vector<std::string> sortOptions(Player& p) {
    std::vector<std::string> out;
    for (int i = 0; i < core::kSortByCount; ++i) out.push_back(sortByLabel(p, static_cast<core::SortBy>(i)));
    return out;
}

std::string onOff(Player& p, bool on) {
    return on ? trFor(p, "abp.gui.on", "On") : trFor(p, "abp.gui.off", "Off");
}

/// Index of `label` in `options`, or -1.
int indexOf(std::vector<std::string> const& options, ll::form::CustomFormElementResult const& r) {
    auto const* s = std::get_if<std::string>(&r);
    if (!s) return -1;
    auto it = std::find(options.begin(), options.end(), *s);
    return it == options.end() ? -1 : static_cast<int>(it - options.begin());
}

/// The per-backpack options form: each option is "use my global setting" or an explicit value.
void showBackpackOptions(Player& player, std::shared_ptr<game::Backpack> bp) {
    auto global    = playerOptions(player);
    auto overrides = backpackOverrides(*bp);
    auto mine      = effective(&player, *bp);
    auto useGlobal = [&](std::string const& current) {
        return trFor(player, "abp.gui.use_global", "My default ({})", current);
    };
    // Dropdown 0 = global, 1.. = explicit values.
    std::vector<std::string> sortOpts{useGlobal(sortByLabel(player, global.sortBy))};
    for (auto& s : sortOptions(player)) sortOpts.push_back(s);
    auto boolOpts = [&](bool g) {
        return std::vector<std::string>{useGlobal(onOff(player, g)), onOff(player, true), onOff(player, false)};
    };
    auto sortIdx = overrides.contains(kSortBy) ? 1 + static_cast<std::size_t>(mine.sortBy) : 0;
    auto boolIdx = [&](char const* key, bool v) -> std::size_t { return overrides.contains(key) ? (v ? 1 : 2) : 0; };

    auto tabOpts   = boolOpts(global.openTabFirst);
    auto notifOpts = boolOpts(global.notifications);
    auto hotOpts   = boolOpts(global.keepHotbar);

    ll::form::CustomForm form(trFor(player, "abp.gui.options.title", "Backpack options"));
    form.appendLabel(trFor(player, "abp.gui.options.help", "These apply to this backpack only."));
    form.appendDropdown(kSortBy, trFor(player, "abp.gui.opt.sort_by", "Sort by"), sortOpts, sortIdx);
    form.appendDropdown(kOpenTabFirst, trFor(player, "abp.gui.opt.open_tab_first", "Shift-click on the upgrades page goes into the upgrade slots first"), tabOpts, boolIdx(kOpenTabFirst, mine.openTabFirst));
    form.appendDropdown(kNotifications, trFor(player, "abp.gui.opt.notifications", "Show notifications and slot hints"), notifOpts, boolIdx(kNotifications, mine.notifications));
    form.appendDropdown(kKeepHotbar, trFor(player, "abp.gui.opt.keep_hotbar", "\"Move all\" buttons keep the hotbar"), hotOpts, boolIdx(kKeepHotbar, mine.keepHotbar));
    form.sendTo(player, [bp, sortOpts, tabOpts, notifOpts, hotOpts](Player& p, ll::form::CustomFormResult const& result, ll::form::FormCancelReason) {
        if (result) {
            auto get = [&](char const* key, std::vector<std::string> const& opts) {
                auto it = result->find(key);
                return it == result->end() ? -1 : indexOf(opts, it->second);
            };
            if (int i = get(kSortBy, sortOpts); i == 0) setBackpackOption(*bp, kSortBy, std::nullopt);
            else if (i > 0) setBackpackOption(*bp, kSortBy, std::string(core::toString(static_cast<core::SortBy>(i - 1))));
            auto setBool = [&](char const* key, std::vector<std::string> const& opts) {
                int i = get(key, opts);
                if (i == 0) setBackpackOption(*bp, key, std::nullopt);
                else if (i > 0) setBackpackOption(*bp, key, i == 1);
            };
            setBool(kOpenTabFirst, tabOpts);
            setBool(kNotifications, notifOpts);
            setBool(kKeepHotbar, hotOpts);
        }
        back(p, bp);
    });
}

/// The player's global options (all backpacks unless a backpack overrides them).
void showGlobalOptions(Player& player, std::shared_ptr<game::Backpack> bp) {
    auto g        = playerOptions(player);
    auto sortOpts = sortOptions(player);
    ll::form::CustomForm form(trFor(player, "abp.gui.global.title", "My backpack settings"));
    form.appendLabel(trFor(player, "abp.gui.global.help", "Defaults for all your backpacks. A backpack's own options win."));
    form.appendDropdown(kSortBy, trFor(player, "abp.gui.opt.sort_by", "Sort by"), sortOpts, static_cast<std::size_t>(g.sortBy));
    form.appendToggle(kOpenTabFirst, trFor(player, "abp.gui.opt.open_tab_first", "Shift-click on the upgrades page goes into the upgrade slots first"), g.openTabFirst);
    form.appendToggle(kNotifications, trFor(player, "abp.gui.opt.notifications", "Show notifications and slot hints"), g.notifications);
    form.appendToggle(kKeepHotbar, trFor(player, "abp.gui.opt.keep_hotbar", "\"Move all\" buttons keep the hotbar"), g.keepHotbar);
    form.sendTo(player, [bp, sortOpts](Player& p, ll::form::CustomFormResult const& result, ll::form::FormCancelReason) {
        if (result) {
            if (auto it = result->find(kSortBy); it != result->end()) {
                if (int i = indexOf(sortOpts, it->second); i >= 0) {
                    setPlayerOption(p, kSortBy, std::string(core::toString(static_cast<core::SortBy>(i))));
                }
            }
            for (auto const* key : {kOpenTabFirst, kNotifications, kKeepHotbar}) {
                auto it = result->find(key);
                if (it == result->end()) continue;
                if (auto const* v = std::get_if<uint64>(&it->second)) setPlayerOption(p, key, *v != 0);
            }
        }
        back(p, bp);
    });
}
} // namespace

bool openSlotPicker(Player& player, std::shared_ptr<game::Backpack> backpack, PickMode mode, int page) {
    if (!backpack) return false;
    auto title = mode == PickMode::Memory ? trFor(player, "abp.gui.memory.title", "Memory slots")
                                          : trFor(player, "abp.gui.nosort.title", "No-sort slots");
    return ui::MenuManager::get().open(player, std::make_shared<SlotPickerMenu>(title, std::move(backpack), mode, page));
}

} // namespace abp::feature::gui

namespace {
using namespace abp;
using namespace abp::feature::gui;

ABP_REGISTER_BACKPACK_SETTING(ui::BackpackSetting{
    .id    = "gui.options",
    .order = 10,
    .label =
        [](Player& p, game::Backpack const& bp) {
            return trFor(p, "abp.gui.options.button", "Backpack options\n§8Sort by {}", sortByLabel(p, effective(&p, bp).sortBy));
        },
    .onSelect = [](Player& p, std::shared_ptr<game::Backpack> bp) { showBackpackOptions(p, std::move(bp)); },
});

ABP_REGISTER_BACKPACK_SETTING(ui::BackpackSetting{
    .id    = "gui.memory",
    .order = 20,
    .label =
        [](Player& p, game::Backpack const& bp) {
            return trFor(p, "abp.gui.memory.button", "Memory slots\n§8{} set", memoryCount(const_cast<game::Backpack&>(bp)));
        },
    .onSelect = [](Player& p, std::shared_ptr<game::Backpack> bp) { openSlotPicker(p, std::move(bp), PickMode::Memory); },
});

ABP_REGISTER_BACKPACK_SETTING(ui::BackpackSetting{
    .id    = "gui.nosort",
    .order = 30,
    .label =
        [](Player& p, game::Backpack const& bp) {
            return trFor(p, "abp.gui.nosort.button", "No-sort slots\n§8{} set", noSortCount(const_cast<game::Backpack&>(bp)));
        },
    .onSelect = [](Player& p, std::shared_ptr<game::Backpack> bp) { openSlotPicker(p, std::move(bp), PickMode::NoSort); },
});

ABP_REGISTER_BACKPACK_SETTING(ui::BackpackSetting{
    .id    = "gui.global",
    .order = 1000,
    .label = [](Player& p, game::Backpack const&) { return trFor(p, "abp.gui.global.button", "My settings (all backpacks)"); },
    .onSelect = [](Player& p, std::shared_ptr<game::Backpack> bp) { showGlobalOptions(p, std::move(bp)); },
});
} // namespace
