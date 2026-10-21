// Inception upgrade: backpacks can be stored inside this backpack, the upgrades of those nested
// backpacks work as if they were carried (configurable: all / non-storage only / none), and a
// nested backpack can be opened from the GUI (Q on its slot, or the "Nested backpacks" button)
// with a Back button to the containing backpack.

#include "upgrade/inception/Inception.h"

#include <algorithm>
#include <unordered_set>

#include "ll/api/chrono/GameChrono.h"
#include "ll/api/coro/CoroTask.h"
#include "ll/api/form/SimpleForm.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/thread/ServerThreadExecutor.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"

#include "feature/admin/Admin.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/Storage.h"
#include "mod/Config.h"
#include "mod/GeneralConfig.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/IUpgrade.h"
#include "upgrade/UpgradeManager.h"

ABP_REGISTER_CONFIG("inception", abp::upgrade::inception::InceptionConfig);

namespace abp::upgrade::inception {

namespace {
constexpr std::string_view kSettingKey       = "nestedUpgrades";
constexpr std::string_view kFiller           = "minecraft:gray_stained_glass_pane";
constexpr int              kReopenDelayTicks = 10; ///< extra ticks between closing one chest menu and opening the next

std::string tierName(Player& player, core::Tier tier) {
    static constexpr std::string_view fallbacks[] = {
        "Leather Backpack",
        "Copper Backpack",
        "Iron Backpack",
        "Gold Backpack",
        "Diamond Backpack",
        "Netherite Backpack"
    };
    return trFor(player, "abp.backpack." + std::string(core::tierInfo(tier).id), fallbacks[static_cast<int>(tier)]);
}

/// Display name of a backpack item stored in a backpack (custom name or tier name).
std::string storedName(Player& player, core::ItemStackData const& item) {
    auto name = game::ItemConv::toStack(item, 1).getCustomName();
    if (!name.empty()) return name;
    auto tier = game::backpackTierOf(item.id);
    return tier ? tierName(player, *tier) : item.id;
}

/// Slot of `parent` holding the backpack item bound to `uuid`, or -1.
int slotOf(game::Backpack& parent, std::string const& uuid) {
    auto const& inv = parent.inventory();
    for (int i = 0; i < inv.size(); ++i) {
        auto const& item = inv.at(i);
        if (item.empty() || !game::isBackpack(item)) continue;
        if (auto u = game::backpackUuid(item); u && *u == uuid) return i;
    }
    return -1;
}

std::string modeName(Player* player, NestedUpgrades mode) {
    auto t = [&](std::string_view key, std::string_view fb) { return player ? trFor(*player, key, fb) : tr(key, fb); };
    switch (mode) {
    case NestedUpgrades::All:
        return t("abp.inception.mode.all", "All upgrades");
    case NestedUpgrades::NonStorage:
        return t("abp.inception.mode.non_storage", "Only non-storage upgrades");
    case NestedUpgrades::None:
        return t("abp.inception.mode.none", "No upgrades");
    }
    return {};
}

/// The chain from the carried backpack down to `parent` as seen from the menu showing `parent`.
struct Chain {
    std::optional<game::ItemLocation> root;
    std::vector<std::string>          ancestors; ///< outermost .. parent
};

Chain chainFor(ui::Menu const* menu, game::Backpack const& parent) {
    Chain c;
    if (auto const* nested = dynamic_cast<NestedBackpackMenu const*>(menu);
        nested && nested->backpack()->uuid() == parent.uuid()) {
        c.root      = nested->rootSource();
        c.ancestors = nested->ancestors();
    } else if (
        auto const* bm = dynamic_cast<ui::BackpackMenu const*>(menu); bm && bm->backpack()->uuid() == parent.uuid()
    ) {
        c.root = bm->source();
    }
    c.ancestors.push_back(parent.uuid());
    return c;
}

/// Runs `open` once the current chest menu (if any) is gone. Replacing a shown chest menu with
/// another one directly doesn't reliably show the new screen on a real client (the old screen's
/// close races the new open), so close first, like the forms do.
void switchMenu(Player& player, std::function<void(Player&)> open) {
    auto& mm = ui::MenuManager::get();
    if (!mm.current(player)) {
        open(player);
        return;
    }
    mm.closeThen(player, [open = std::move(open)](Player& p) {
        // Give the client time to tear the old chest screen down before the next ContainerOpen.
        ll::coro::keepThis([id = p.getOrCreateUniqueID(), open]() -> ll::coro::CoroTask<> {
            co_await ll::chrono::ticks(kReopenDelayTicks);
            auto level = ll::service::getLevel();
            if (!level) co_return;
            if (auto* pl = level->getPlayer(id); pl && pl->isAlive() && !ui::MenuManager::get().current(*pl)) open(*pl);
        }).launch(ll::thread::ServerThreadExecutor::getDefault());
    });
}

void reopenNow(
    Player&                                player,
    Chain const&                           chain,
    std::shared_ptr<game::Backpack> const& bp,
    ui::BackpackMenu::View                 view
);

/// Opens `bp` again the way it was shown (`chain.ancestors.back()` is `bp`).
void reopen(
    Player&                                player,
    Chain const&                           chain,
    std::shared_ptr<game::Backpack> const& bp,
    ui::BackpackMenu::View                 view = ui::BackpackMenu::View::Storage
) {
    switchMenu(player, [chain, bp, view](Player& p) { reopenNow(p, chain, bp, view); });
}

void reopenNow(
    Player&                                player,
    Chain const&                           chain,
    std::shared_ptr<game::Backpack> const& bp,
    ui::BackpackMenu::View                 view
) {
    if (chain.ancestors.size() <= 1) {
        if (view == ui::BackpackMenu::View::Storage && chain.root && ui::BackpackMenu::openFor(player, *chain.root))
            return;
        ui::BackpackMenu::openBackpack(player, bp, {}, view);
        return;
    }
    std::vector<std::string> up(chain.ancestors.begin(), chain.ancestors.end() - 1);
    std::string              title = tierName(player, bp->tier());
    if (auto parent = game::Storage::get().find(up.back())) {
        if (int s = slotOf(*parent, bp->uuid()); s >= 0) title = storedName(player, parent->inventory().at(s));
    }
    auto menu = std::make_shared<NestedBackpackMenu>(bp, chain.root, std::move(up), std::move(title));
    if (view != ui::BackpackMenu::View::Storage) menu->showView(view);
    ui::MenuManager::get().open(player, std::move(menu));
}

bool openVia(Player& player, Chain chain, std::shared_ptr<game::Backpack> const& parent, int index) {
    auto child = resolveNested(parent, index);
    if (!child) return false;
    // Never open a backpack that is already part of the chain (defensive; cycles are refused on insert).
    if (std::find(chain.ancestors.begin(), chain.ancestors.end(), child->uuid()) != chain.ancestors.end()) return false;
    auto title = storedName(player, parent->inventory().at(index));
    auto menu  = std::make_shared<NestedBackpackMenu>(
        std::move(child),
        chain.root,
        std::move(chain.ancestors),
        std::move(title)
    );
    switchMenu(player, [menu](Player& p) { ui::MenuManager::get().open(p, menu); });
    return true;
}
} // namespace

// ---- settings / helpers -----------------------------------------------------------------------

std::string_view toString(NestedUpgrades mode) {
    switch (mode) {
    case NestedUpgrades::All:
        return "all";
    case NestedUpgrades::NonStorage:
        return "non_storage";
    case NestedUpgrades::None:
        return "none";
    }
    return "all";
}

NestedUpgrades nestedUpgradesFromString(std::string_view s) {
    if (s == "non_storage") return NestedUpgrades::NonStorage;
    if (s == "none") return NestedUpgrades::None;
    return NestedUpgrades::All;
}

std::optional<NestedUpgrades> nestedUpgradesOf(game::Backpack const& backpack) {
    for (auto const& s : backpack.data().upgrades) {
        if (s.empty() || !s.enabled) continue;
        auto* u = upgradeFor(s.item);
        if (!u || u->info().id != kId || !typeAllowed(*u)) continue;
        auto it = s.settings.find(kSettingKey);
        if (it == s.settings.end() || !it->is_string()) return NestedUpgrades::All;
        return nestedUpgradesFromString(it->get<std::string>());
    }
    return std::nullopt;
}

bool isStorageRelated(std::string_view upgradeId, std::string_view group) {
    auto const& list = config::get<InceptionConfig>().storageUpgrades;
    return std::any_of(list.begin(), list.end(), [&](std::string const& s) { return s == upgradeId || s == group; });
}

std::vector<int> backpackSlots(game::Backpack& backpack) {
    std::vector<int> out;
    auto const&      inv = backpack.inventory();
    for (int i = 0; i < inv.size(); ++i) {
        auto const& item = inv.at(i);
        if (!item.empty() && game::isBackpack(item)) out.push_back(i);
    }
    return out;
}

std::shared_ptr<game::Backpack> resolveNested(std::shared_ptr<game::Backpack> const& parent, int index) {
    auto& storage = game::Storage::get();
    if (!parent || !storage.available()) return nullptr;
    auto& inv = parent->inventory();
    if (index < 0 || index >= inv.size()) return nullptr;
    auto item = inv.at(index);
    if (item.empty() || !game::isBackpack(item)) return nullptr;
    if (auto uuid = game::backpackUuid(item)) {
        if (*uuid == parent->uuid()) return nullptr;
        if (auto bp = storage.find(*uuid)) return bp;
    }
    // Never opened before (or unknown in this world): bind it now and write the uuid back.
    ItemStack stack   = game::ItemConv::toStack(item, 1);
    bool      changed = false;
    auto      bp      = storage.forItem(stack, &changed);
    if (!bp) return nullptr;
    if (changed) {
        auto bound  = game::ItemConv::toData(stack);
        bound.count = item.count;
        inv.set(index, bound);
    }
    return bp;
}

// ---- nested upgrades (PlayerScan provider) ----------------------------------------------------

namespace {
/// Scan depth for nested upgrades: `inception.maxDepth`, never deeper than the server's nesting
/// limit (`admin.maxNestingDepth`, 0 = unlimited), since deeper backpacks can't exist anyway.
int effectiveMaxDepth() {
    int depth = config::get<InceptionConfig>().maxDepth;
    int limit = config::get<admin::AdminConfig>().maxNestingDepth;
    return limit > 0 ? std::min(depth, limit) : depth;
}

void walk(
    game::CarriedBackpack const&                                                    parent,
    std::shared_ptr<game::Backpack> const&                                          parentBp,
    int                                                                             depth,
    std::unordered_set<std::string>&                                                seen,
    std::vector<std::pair<game::CarriedBackpack, std::shared_ptr<game::Backpack>>>& list
) {
    if (depth > effectiveMaxDepth()) return;
    if (!parent.upgradeActive(kId, kId)) return;
    auto mode = nestedUpgradesOf(*parentBp);
    if (!mode || *mode == NestedUpgrades::None) return;
    auto gate = parent.upgradeGate;
    if (*mode == NestedUpgrades::NonStorage) {
        gate = [outer = std::move(gate)](std::string_view id, std::string_view group) {
            return !isStorageRelated(id, group) && (!outer || outer(id, group));
        };
    }
    auto& storage = game::Storage::get();
    auto  slots   = backpackSlots(*parentBp);
    for (int i : slots) {
        auto uuid = game::backpackUuid(parentBp->inventory().at(i));
        if (!uuid || seen.contains(*uuid)) continue;
        auto bp = storage.find(*uuid);
        if (!bp) continue;
        seen.insert(*uuid);
        game::CarriedBackpack c{parent.where, bp->tier(), *uuid};
        c.depth       = depth;
        c.parent      = parentBp->uuid();
        c.upgradeGate = gate;
        list.emplace_back(c, bp);
        walk(c, bp, depth + 1, seen, list);
    }
}
} // namespace

void appendNested(Player&, std::vector<std::pair<game::CarriedBackpack, std::shared_ptr<game::Backpack>>>& list) {
    auto const& cfg = config::get<InceptionConfig>();
    if (!cfg.nestedUpgradesWork || effectiveMaxDepth() <= 0) return;
    std::unordered_set<std::string> seen;
    for (auto const& [c, bp] : list) seen.insert(bp->uuid());
    // walk() appends to `list`: iterate the directly carried entries by index (copies, since the
    // vector may reallocate).
    auto const top = list.size();
    for (std::size_t i = 0; i < top; ++i) {
        if (list[i].first.depth != 0) continue;
        auto carried = list[i].first;
        auto bp      = list[i].second;
        walk(carried, bp, 1, seen, list);
    }
}

// ---- nested backpack menu ---------------------------------------------------------------------

NestedBackpackMenu::NestedBackpackMenu(
    std::shared_ptr<game::Backpack>   backpack,
    std::optional<game::ItemLocation> rootSource,
    std::vector<std::string>          ancestors,
    std::string                       title
)
: BackpackMenu(std::move(backpack), std::nullopt, std::move(title)),
  mRootSource(rootSource),
  mAncestors(std::move(ancestors)) {
    if (mRootSource && mRootSource->kind == game::ItemLocation::Kind::Inventory)
        lockedInventorySlot = mRootSource->slot;
}

void NestedBackpackMenu::build(Player& player) {
    BackpackMenu::build(player);
    std::string parentName;
    if (auto parent = game::Storage::get().find(mAncestors.back())) {
        parentName = tierName(player, parent->tier());
        if (mAncestors.size() >= 2) {
            if (auto grand = game::Storage::get().find(mAncestors[mAncestors.size() - 2])) {
                if (int s = slotOf(*grand, parent->uuid()); s >= 0)
                    parentName = storedName(player, grand->inventory().at(s));
            }
        } else if (mRootSource) {
            auto const& item = game::itemAt(player, *mRootSource);
            if (auto custom = item.getCustomName(); !custom.empty()) parentName = custom;
        }
    }
    std::vector<std::string> lore{trFor(player, "abp.inception.back_lore", "§7Return to §f{}", parentName)};
    if (view() == View::Storage) {
        lore.push_back(trFor(player, "abp.menu.page", "Page {} of {}", page() + 1, pageCount()));
    }
    setSlot(
        46,
        std::make_shared<ui::ButtonSlot>(
            ui::icon("minecraft:wooden_door", trFor(player, "abp.inception.back", "Back to the outer backpack"), lore),
            [this](ui::ClickContext const& c) { goBack(c.player); }
        )
    );
}

bool NestedBackpackMenu::chainValid(Player& player) const {
    auto& storage = game::Storage::get();
    if (mAncestors.empty() || !storage.available()) return false;
    if (mRootSource) {
        auto const& item = game::itemAt(player, *mRootSource);
        auto        uuid = game::backpackUuid(item);
        if (!game::isBackpack(item) || !uuid || *uuid != mAncestors.front()) return false;
    }
    for (std::size_t i = 0; i < mAncestors.size(); ++i) {
        auto bp = storage.find(mAncestors[i]);
        if (!bp) return false;
        auto const& next = i + 1 < mAncestors.size() ? mAncestors[i + 1] : backpack()->uuid();
        if (slotOf(*bp, next) < 0) return false;
    }
    return true;
}

void NestedBackpackMenu::onTick(Player& player) {
    // Every tick, like BackpackMenu's own item check: the outer backpack must not leave the player
    // while the inner one is open.
    if (!chainValid(player)) ui::MenuManager::get().close(player);
}

void NestedBackpackMenu::goBack(Player& player) {
    auto& storage = game::Storage::get();
    auto  parent  = storage.find(mAncestors.back());
    if (!parent) {
        ui::MenuManager::get().close(player);
        return;
    }
    reopen(player, Chain{mRootSource, mAncestors}, parent);
}

bool openNested(Player& player, ui::Menu const* menu, std::shared_ptr<game::Backpack> const& parent, int index) {
    if (!parent) return false;
    return openVia(player, chainFor(menu, *parent), parent, index);
}

// ---- settings menu ----------------------------------------------------------------------------

namespace {
class InceptionSettingsMenu : public ui::Menu {
public:
    /// `from`: how the backpack was shown when the settings were opened (nested chain), if known.
    InceptionSettingsMenu(UpgradeContext ctx, std::string title, std::optional<Chain> from)
    : Menu(std::move(title), Rows::Three),
      mCtx(std::move(ctx)),
      mFrom(std::move(from)) {}

    void build(Player& player) override {
        for (int i = 0; i < size(); ++i) setSlot(i, std::make_shared<ui::StaticSlot>(ui::icon(kFiller, " ")));
        auto current = nestedUpgradesOf(mCtx.backpack()).value_or(NestedUpgrades::All);
        setSlot(
            4,
            std::make_shared<ui::StaticSlot>(ui::icon(
                "minecraft:book",
                trFor(player, "abp.inception.settings.title", "Upgrades of nested backpacks"),
                {trFor(player, "abp.inception.settings.help1", "§7Backpacks stored in this backpack run their"),
                 trFor(player, "abp.inception.settings.help2", "§7upgrades as if you carried them.")}
            ))
        );
        struct Option {
            int              slot;
            NestedUpgrades   mode;
            std::string_view itemId;
            std::string_view loreKey;
            std::string_view loreFallback;
        };
        static constexpr Option options[] = {
            {11,
             NestedUpgrades::All,
             "minecraft:nether_star", "abp.inception.mode.all_lore",
             "§7Every upgrade of nested backpacks works."                                     },
            {13,
             NestedUpgrades::NonStorage,
             "minecraft:hopper",      "abp.inception.mode.non_storage_lore",
             "§7Upgrades that move items (pickup, magnet,\n§7void, compacting, ...) stay off."},
            {15,
             NestedUpgrades::None,
             "minecraft:barrier",     "abp.inception.mode.none_lore",
             "§7Nested backpacks are just storage."                                           },
        };
        for (auto const& o : options) {
            bool                     selected = o.mode == current;
            std::vector<std::string> lore;
            auto                     text = trFor(player, o.loreKey, o.loreFallback);
            for (std::size_t start = 0;;) {
                auto nl = text.find('\n', start);
                lore.push_back(text.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
                if (nl == std::string::npos) break;
                start = nl + 1;
            }
            lore.push_back(
                selected ? trFor(player, "abp.inception.selected", "§aSelected")
                         : trFor(player, "abp.inception.click_select", "§eClick to select")
            );
            auto name = (selected ? "§a> " : "§f") + modeName(&player, o.mode);
            setSlot(
                o.slot,
                std::make_shared<ui::ButtonSlot>(
                    ui::icon(o.itemId, name, lore),
                    [this, mode = o.mode](ui::ClickContext const&) {
                        mCtx.settings()[std::string(kSettingKey)] = std::string(toString(mode));
                        mCtx.saveSettings(false);
                        invalidate();
                    }
                )
            );
        }
        setSlot(
            22,
            std::make_shared<ui::ButtonSlot>(
                ui::icon("minecraft:arrow", trFor(player, "abp.menu.back_generic", "Back")),
                [bp = mCtx.backpackPtr(), from = mFrom](ui::ClickContext const& c) {
                    if (from) reopen(c.player, *from, bp, ui::BackpackMenu::View::Upgrades);
                    else
                        switchMenu(c.player, [bp](Player& p) {
                            ui::BackpackMenu::openBackpack(p, bp, {}, ui::BackpackMenu::View::Upgrades);
                        });
                }
            )
        );
    }

private:
    UpgradeContext       mCtx;
    std::optional<Chain> mFrom;
};

std::string generic(Player* player, std::string_view key, std::string_view fallback) {
    return player ? trFor(*player, key, fallback) : tr(key, fallback);
}
} // namespace

// ---- the upgrade --------------------------------------------------------------------------------

class InceptionUpgrade : public IUpgrade {
public:
    [[nodiscard]] UpgradeInfo const& info() const override {
        static UpgradeInfo const info{
            .id           = std::string(kId),
            .itemId       = "alleviatedbackpacks:inception_upgrade",
            .nameKey      = "abp.inception.name",
            .nameFallback = "Inception Upgrade",
            .hasSettings  = true,
            .toggleable   = false,
        };
        return info;
    }

    void attach(UpgradeContext const& ctx, core::BackpackInventory&) override {
        ctx.backpack().addCapability("allow_nesting");
    }

    bool canRemove(UpgradeContext const& ctx, std::string& reason) const override {
        if (config::get<GeneralConfig>().allowNesting) return true;
        if (backpackSlots(ctx.backpack()).empty()) return true;
        reason = generic(
            ctx.player(),
            "abp.inception.error.not_empty",
            "Take the backpacks out of this backpack before removing the Inception Upgrade."
        );
        return false;
    }

    std::shared_ptr<ui::Menu> createSettingsMenu(Player& player, UpgradeContext const& ctx) override {
        // Opened from a nested backpack's upgrades page: remember the chain so Back returns there.
        std::optional<Chain> from;
        auto                 current = ui::MenuManager::get().current(player);
        if (auto const* nested = dynamic_cast<NestedBackpackMenu const*>(current.get());
            nested && nested->backpack() == ctx.backpackPtr()) {
            from = chainFor(nested, ctx.backpack());
        }
        return std::make_shared<InceptionSettingsMenu>(
            ctx,
            trFor(player, "abp.inception.name", "Inception Upgrade"),
            std::move(from)
        );
    }

    [[nodiscard]] std::vector<std::string> describe(Player& player, UpgradeContext const& ctx) const override {
        auto mode = nestedUpgradesOf(ctx.backpack()).value_or(NestedUpgrades::All);
        return {
            trFor(player, "abp.inception.describe.mode", "§7Nested upgrades: §f{}", modeName(&player, mode)),
            trFor(
                player,
                "abp.inception.describe.count",
                "§7Backpacks inside: §f{}",
                backpackSlots(ctx.backpack()).size()
            ),
        };
    }
};

ABP_REGISTER_UPGRADE(InceptionUpgrade);

// ---- GUI entry points ---------------------------------------------------------------------------

// Q (drop) on a backpack stored in a backpack opens it instead of dropping it.
ABP_REGISTER_STORAGE_CLICK([](ui::ClickContext const& c, std::shared_ptr<game::Backpack> const& bp, int index) {
    if (c.type != ui::ClickType::Drop || !bp || !hasActive(*bp, kId)) return false;
    auto const& inv = bp->inventory();
    if (index < 0 || index >= inv.size() || inv.at(index).empty() || !game::isBackpack(inv.at(index))) return false;
    if (!openNested(c.player, &c.menu, bp, index)) {
        c.player.sendMessage(trFor(c.player, "abp.inception.error.open", "That backpack can't be opened."));
    }
    return true;
});

// "Nested backpacks" button: opens the only nested backpack directly, or offers a list.
ABP_REGISTER_BACKPACK_BUTTON(
    ui::BackpackButton{
        .id    = "inception.open",
        .order = 5, // first: only shown with an active Inception upgrade
        .visible =
            [](ui::BackpackMenuContext const& c) {
                // Visible whenever Inception is active: the control row is only rebuilt on page / view
                // changes, so the button must not depend on the (changing) contents.
                return hasActive(*c.backpack, kId);
            },
        .icon =
            [](ui::BackpackMenuContext const& c) {
                auto slots = backpackSlots(*c.backpack);
                auto tier =
                    slots.empty() ? std::nullopt : game::backpackTierOf(c.backpack->inventory().at(slots.front()).id);
                return ui::icon(
                    game::backpackItemId(tier.value_or(core::Tier::Leather)),
                    trFor(c.player, "abp.inception.open", "Nested backpacks ({})", slots.size()),
                    {trFor(c.player, "abp.inception.open_lore1", "§7Click to open a backpack stored here."),
                     trFor(c.player, "abp.inception.open_lore2", "§7Tip: press Q on a stored backpack to open it.")}
                );
            },
        .onClick =
            [](ui::BackpackMenuContext const& c, ui::ClickContext const&) {
                auto slots = backpackSlots(*c.backpack);
                if (slots.empty()) {
                    c.player.sendMessage(
                        trFor(c.player, "abp.inception.none_inside", "There are no backpacks in this backpack.")
                    );
                    return;
                }
                if (slots.size() == 1) {
                    (void)openNested(c.player, &c.menu, c.backpack, slots.front());
                    return;
                }
                auto chain = chainFor(&c.menu, *c.backpack);
                ui::MenuManager::get().closeThen(c.player, [chain, bp = c.backpack](Player& p) {
                    ll::form::SimpleForm form(trFor(p, "abp.inception.choose", "Open a nested backpack"));
                    for (int slot : backpackSlots(*bp)) {
                        auto const& item = bp->inventory().at(slot);
                        auto        uuid = game::backpackUuid(item).value_or("");
                        form.appendButton(storedName(p, item), [chain, bp, slot, uuid](Player& pl) {
                            // The slot may have changed while the form was open: re-check the binding.
                            if (slot >= bp->inventory().size()) return;
                            auto const& now = bp->inventory().at(slot);
                            if (now.empty() || !game::isBackpack(now) || game::backpackUuid(now).value_or("") != uuid)
                                return;
                            (void)openVia(pl, chain, bp, slot);
                        });
                    }
                    form.appendButton(trFor(p, "abp.settings.back", "Back to backpack"), [chain, bp](Player& pl) {
                        reopen(pl, chain, bp);
                    });
                    form.sendTo(p);
                });
            },
        .label =
            [](ui::BackpackMenuContext const& c) {
                return trFor(c.player, "abp.inception.open_label", "Nested backpacks");
            },
    }
);

// ---- module -------------------------------------------------------------------------------------

class InceptionModule {
public:
    void install() { game::setNestedBackpackProvider(&appendNested); }
    void uninstall() { game::setNestedBackpackProvider(nullptr); }
};

} // namespace abp::upgrade::inception

ABP_REGISTER_MODULE(abp::upgrade::inception::InceptionModule);
