#include "ui/menu/FilterMenu.h"

#include <algorithm>

#include "ll/api/form/CustomForm.h"

#include "mc/world/actor/player/Player.h"

#include "mod/I18n.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"

namespace abp::ui {

namespace {
Menu::Rows rowsFor(int slots) { return slots + 9 > 27 ? Menu::Rows::Six : Menu::Rows::Three; }

std::string joinTags(std::vector<std::string> const& tags) {
    std::string out;
    for (auto const& t : tags) {
        if (!out.empty()) out += ", ";
        out += t;
    }
    return out;
}

std::vector<std::string> splitTags(std::string const& text) {
    std::vector<std::string> out;
    std::string              cur;
    auto                     flush = [&] {
        auto b = cur.find_first_not_of(" \t");
        auto e = cur.find_last_not_of(" \t");
        if (b != std::string::npos) {
            auto t = cur.substr(b, e - b + 1);
            if (t.find(':') == std::string::npos) t = "minecraft:" + t;
            out.push_back(t);
        }
        cur.clear();
    };
    for (char c : text) {
        if (c == ',' || c == ';' || c == ' ') flush();
        else cur += c;
    }
    flush();
    return out;
}
} // namespace

FilterMenu::FilterMenu(std::string title, Getter get, Setter set, Options options)
: Menu(std::move(title), rowsFor(std::clamp(options.slots, 1, 45))),
  mGet(std::move(get)),
  mSet(std::move(set)),
  mOptions(std::move(options)) {
    mOptions.slots = std::clamp(mOptions.slots, 1, 45);
}

std::shared_ptr<FilterMenu>
FilterMenu::forUpgrade(Player& player, upgrade::UpgradeContext const& ctx, std::string key, int slots, Options options) {
    auto* type  = upgrade::upgradeFor(ctx.state().item);
    auto  title = type ? upgrade::displayName(*type, &player) : trFor(player, "abp.filter.title", "Filter");
    if (!options.onBack) {
        options.onBack = [bp = ctx.backpackPtr()](Player& p) {
            BackpackMenu::openBackpack(p, bp, {}, BackpackMenu::View::Upgrades);
        };
    }
    options.slots = slots;
    return std::make_shared<FilterMenu>(
        std::move(title),
        [ctx, key, slots] { return core::Filter::fromJson(ctx.settings().value(key, nlohmann::json::object()), slots); },
        [ctx, key](core::Filter const& f) {
            ctx.settings()[key] = f.toJson();
            ctx.saveSettings(true);
        },
        std::move(options)
    );
}

void FilterMenu::build(Player& player) {
    int ghosts  = mOptions.slots;
    int control = size() - 9;
    for (int i = 0; i < control; ++i) {
        if (i < ghosts) {
            setSlot(
                i,
                std::make_shared<GhostSlot>(
                    [this, i] {
                        auto f = mGet();
                        return i < static_cast<int>(f.ghosts.size()) ? f.ghosts[static_cast<std::size_t>(i)] : core::ItemStackData{};
                    },
                    [this, i](core::ItemStackData const& d) {
                        auto f = mGet();
                        if (!d.empty() && f.hasGhost(d)) return; // no duplicates
                        f.setGhost(i, d);
                        mSet(f);
                    }
                )
            );
        } else {
            setSlot(i, std::make_shared<StaticSlot>(icon("minecraft:gray_stained_glass_pane", " ")));
        }
    }
    auto f    = mGet();
    auto onOff = [&](bool v) {
        return v ? trFor(player, "abp.filter.on", "§aOn") : trFor(player, "abp.filter.off", "§cOff");
    };
    auto button = [&](int slot, ItemStack shown, std::function<void(core::Filter&)> change) {
        setSlot(control + slot, std::make_shared<ButtonSlot>(std::move(shown), [this, change](ClickContext const&) {
            auto filter = mGet();
            change(filter);
            mSet(filter);
            invalidate();
        }));
    };
    for (int i = 0; i < 9; ++i) setSlot(control + i, std::make_shared<StaticSlot>(icon("minecraft:black_stained_glass_pane", " ")));

    if (mOptions.allowModeToggle) {
        bool allow = f.mode == core::FilterMode::Allow;
        button(
            0,
            icon(
                allow ? "minecraft:white_wool" : "minecraft:black_wool",
                allow ? trFor(player, "abp.filter.mode.allow", "Allow list") : trFor(player, "abp.filter.mode.deny", "Deny list"),
                {trFor(player, "abp.filter.click_to_toggle", "§7Click to toggle")}
            ),
            [](core::Filter& x) {
                x.mode = x.mode == core::FilterMode::Allow ? core::FilterMode::Deny : core::FilterMode::Allow;
            }
        );
    }
    if (mOptions.allowMatchBy) {
        std::string_view id;
        std::string      label;
        switch (f.matchBy) {
        case core::MatchBy::Item:
            id    = "minecraft:iron_ingot";
            label = trFor(player, "abp.filter.match.item", "Match: item");
            break;
        case core::MatchBy::Namespace:
            id    = "minecraft:name_tag";
            label = trFor(player, "abp.filter.match.namespace", "Match: namespace (mod)");
            break;
        case core::MatchBy::Tag:
            id    = "minecraft:paper";
            label = trFor(player, "abp.filter.match.tag", "Match: tag");
            break;
        }
        button(1, icon(id, label, {trFor(player, "abp.filter.click_to_cycle", "§7Click to cycle")}), [](core::Filter& x) {
            x.matchBy = static_cast<core::MatchBy>((static_cast<int>(x.matchBy) + 1) % 3);
        });
        if (f.matchBy == core::MatchBy::Tag) {
            setSlot(
                control + 2,
                std::make_shared<ButtonSlot>(
                    icon(
                        "minecraft:writable_book",
                        trFor(player, "abp.filter.tags", "Tags"),
                        {f.tags.empty() ? trFor(player, "abp.filter.tags.from_items", "§7Tags of the items above")
                                        : "§7" + joinTags(f.tags),
                         trFor(player, "abp.filter.tags.edit", "§eClick to edit")}
                    ),
                    [this](ClickContext const& c) { editTags(c.player); }
                )
            );
        }
    }
    if (mOptions.allowNbt) {
        button(
            3,
            icon("minecraft:enchanted_book", trFor(player, "abp.filter.nbt", "Match NBT: {}", onOff(f.matchNbt))),
            [](core::Filter& x) { x.matchNbt = !x.matchNbt; }
        );
    }
    if (mOptions.allowDurability) {
        button(
            4,
            icon("minecraft:iron_pickaxe", trFor(player, "abp.filter.durability", "Match durability: {}", onOff(f.matchDurability))),
            [](core::Filter& x) { x.matchDurability = !x.matchDurability; }
        );
    }
    button(7, icon("minecraft:barrier", trFor(player, "abp.filter.clear", "Clear filter")), [](core::Filter& x) {
        for (auto& g : x.ghosts) g.clear();
        x.tags.clear();
    });
    setSlot(
        control + 8,
        std::make_shared<ButtonSlot>(icon("minecraft:arrow", trFor(player, "abp.menu.back_generic", "Back")), [this](ClickContext const& c) {
            if (mOptions.onBack) {
                auto back = mOptions.onBack;
                back(c.player);
            } else {
                MenuManager::get().close(c.player);
            }
        })
    );
}

void FilterMenu::editTags(Player& player) {
    auto self = std::static_pointer_cast<FilterMenu>(shared_from_this());
    MenuManager::get().closeThen(player, [self](Player& p) {
        auto                f = self->mGet();
        ll::form::CustomForm form(trFor(p, "abp.filter.tags.title", "Filter tags"));
        form.appendLabel(trFor(p, "abp.filter.tags.help", "Comma separated item tags, e.g. minecraft:logs, minecraft:planks"));
        form.appendInput("tags", trFor(p, "abp.filter.tags", "Tags"), "minecraft:logs", joinTags(f.tags));
        form.sendTo(p, [self](Player& pl, ll::form::CustomFormResult const& result, ll::form::FormCancelReason) {
            if (result) {
                if (auto it = result->find("tags"); it != result->end()) {
                    if (auto const* text = std::get_if<std::string>(&it->second)) {
                        auto filter = self->mGet();
                        filter.tags = splitTags(*text);
                        self->mSet(filter);
                    }
                }
            }
            self->invalidate();
            MenuManager::get().open(pl, self);
        });
    });
}

} // namespace abp::ui
