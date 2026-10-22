// Inception upgrade (SB InceptionUpgradeWrapper): backpacks can be stored inside a top-level
// backpack with this upgrade (only backpacks without Inception themselves), the upgrades work with
// the inventories of the backpacks inside (InventoryOrder: main first / incepted first), the
// upgrades of the backpacks inside stay functional and their tanks / batteries join the outer
// backpack's. A nested backpack can be opened from the GUI (native screen: right click / B; chest
// UI: Q on its slot or the "Nested backpacks" button) with a Back button to the containing one.

#include "upgrade/inception/Inception.h"

#include <algorithm>
#include <functional>
#include <unordered_set>

#include <climits>

#include "ll/api/form/SimpleForm.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "api/BackpackCapabilities.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/Storage.h"
#include "mod/Config.h"
#include "mod/GeneralConfig.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "net/NativeScreens.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/IUpgrade.h"
#include "upgrade/UpgradeManager.h"

ABP_REGISTER_CONFIG("inception", abp::upgrade::inception::InceptionConfig);

namespace abp::upgrade::inception {

namespace {
constexpr std::string_view kSettingKey = "inventoryOrder";
constexpr std::string_view kFiller     = "minecraft:gray_stained_glass_pane";

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

std::string orderName(Player& player, InventoryOrder order) {
    return order == InventoryOrder::MainFirst
             ? trFor(player, "abp.inception.inventory_order_main_first", "Upgrades work with inventory\nof main backpack first")
             : trFor(player, "abp.inception.inventory_order_incepted_first", "Upgrades work with inventories\nof backpacks in backpack first");
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

/// Opens the next menu. `MenuManager::open` replaces the shown chest menu and waits for the client
/// to finish closing the old screen before opening the new one. (Not `swap`: the title must change.)
void switchMenu(Player& player, std::function<void(Player&)> const& open) { open(player); }

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

std::string_view toString(InventoryOrder order) { return order == InventoryOrder::InceptedFirst ? "incepted_first" : "main_first"; }

InventoryOrder inventoryOrderFromString(std::string_view s) {
    return s == "incepted_first" ? InventoryOrder::InceptedFirst : InventoryOrder::MainFirst; // SB fromName: MAIN_FIRST
}

std::optional<InventoryOrder> inventoryOrderOf(game::Backpack const& backpack) {
    for (auto const& s : backpack.data().upgrades) {
        if (s.empty()) continue;
        auto* u = upgradeFor(s.item);
        if (!u || u->info().id != kId || !typeAllowed(*u)) continue;
        auto it = s.settings.find(kSettingKey);
        if (it == s.settings.end() || !it->is_string()) return InventoryOrder::MainFirst;
        return inventoryOrderFromString(it->get<std::string>());
    }
    return std::nullopt;
}

bool hasInception(game::Backpack const& backpack) {
    for (auto const& s : backpack.data().upgrades) {
        if (s.empty()) continue;
        if (auto* u = upgradeFor(s.item); u && u->info().id == kId) return true;
    }
    return false;
}

std::vector<std::shared_ptr<game::Backpack>> subBackpacks(std::shared_ptr<game::Backpack> const& backpack) {
    std::vector<std::shared_ptr<game::Backpack>> out;
    if (!backpack) return out;
    for (int i : backpackSlots(*backpack)) {
        auto bp = resolveNested(backpack, i);
        if (bp && bp != backpack && std::find(out.begin(), out.end(), bp) == out.end()) out.push_back(std::move(bp));
    }
    return out;
}

std::vector<std::shared_ptr<game::Backpack>> upgradeInventories(std::shared_ptr<game::Backpack> const& backpack) {
    std::vector<std::shared_ptr<game::Backpack>> out;
    if (!backpack) return out;
    auto order = config::get<InceptionConfig>().upgradesUseInventoriesOfBackpacksInBackpack ? inventoryOrderOf(*backpack) : std::nullopt;
    if (!order) {
        out.push_back(backpack);
        return out;
    }
    if (*order == InventoryOrder::MainFirst) out.push_back(backpack);
    auto subs = subBackpacks(backpack);
    out.insert(out.end(), subs.begin(), subs.end());
    if (*order == InventoryOrder::InceptedFirst) out.push_back(backpack);
    return out;
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

void appendNested(Player&, std::vector<std::pair<game::CarriedBackpack, std::shared_ptr<game::Backpack>>>& list) {
    // SB InceptionWrapperAccessor: the upgrades of the backpacks inside a carried backpack with an
    // Inception upgrade work too (one level: a backpack with Inception can't be stored in another).
    if (!config::get<InceptionConfig>().upgradesInContainedBackpacksAreFunctional) return;
    std::unordered_set<std::string> seen;
    for (auto const& [c, bp] : list) seen.insert(bp->uuid());
    auto const top = list.size();
    for (std::size_t i = 0; i < top; ++i) {
        if (list[i].first.depth != 0) continue;
        auto parent   = list[i].first;
        auto parentBp = list[i].second;
        if (!parent.upgradeActive(kId, kId) || !inventoryOrderOf(*parentBp)) continue;
        for (auto const& bp : subBackpacks(parentBp)) {
            if (seen.contains(bp->uuid())) continue;
            seen.insert(bp->uuid());
            game::CarriedBackpack c{parent.where, bp->tier(), bp->uuid()};
            c.depth       = 1;
            c.parent      = parentBp->uuid();
            c.upgradeGate = parent.upgradeGate;
            list.emplace_back(c, bp);
        }
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

// ---- settings ---------------------------------------------------------------------------------

namespace {
void setOrder(UpgradeContext const& ctx, InventoryOrder order) {
    ctx.settings()[std::string(kSettingKey)] = std::string(toString(order));
    ctx.saveSettings(false);
}

InventoryOrder orderOf(UpgradeContext const& ctx) {
    auto it = ctx.settings().find(kSettingKey);
    return it != ctx.settings().end() && it->is_string() ? inventoryOrderFromString(it->get<std::string>()) : InventoryOrder::MainFirst;
}

/// Chest UI of the setting (simulated players in selftests; players get the native tab): one
/// toggle button, like SB's tab.
class InceptionSettingsMenu : public ui::Menu {
public:
    /// `from`: how the backpack was shown when the settings were opened (nested chain), if known.
    InceptionSettingsMenu(UpgradeContext ctx, std::string title, std::optional<Chain> from)
    : Menu(std::move(title), Rows::Three),
      mCtx(std::move(ctx)),
      mFrom(std::move(from)) {}

    void build(Player& player) override {
        for (int i = 0; i < size(); ++i) setSlot(i, std::make_shared<ui::StaticSlot>(ui::icon(kFiller, " ")));
        auto order = orderOf(mCtx);
        auto text  = orderName(player, order);
        auto nl    = text.find('\n');
        setSlot(
            13,
            std::make_shared<ui::ButtonSlot>(
                ui::icon(
                    order == InventoryOrder::MainFirst ? "minecraft:chest" : "minecraft:ender_chest",
                    text.substr(0, nl),
                    nl == std::string::npos ? std::vector<std::string>{} : std::vector<std::string>{"§f" + text.substr(nl + 1)}
                ),
                [this](ui::ClickContext const&) {
                    setOrder(mCtx, orderOf(mCtx) == InventoryOrder::MainFirst ? InventoryOrder::InceptedFirst : InventoryOrder::MainFirst);
                    invalidate();
                }
            )
        );
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

/// True if `backpack` is stored inside another backpack as far as `player` can tell (SB: the
/// storage is not "first level": opened from inside another backpack).
bool storedInAnother(Player* player, game::Backpack const& backpack) {
    if (!player) return false;
    auto current = ui::MenuManager::get().current(*player);
    if (auto const* nested = dynamic_cast<NestedBackpackMenu const*>(current.get()); nested && nested->backpack()->uuid() == backpack.uuid()) {
        return true;
    }
    for (auto const& [c, bp] : game::carriedBackpacks(*player, false, true)) {
        if (c.depth > 0 && bp->uuid() == backpack.uuid()) return true;
    }
    return false;
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

    void attach(UpgradeContext const& ctx, core::BackpackInventory& inv) override {
        ctx.backpack().addCapability("allow_nesting");
        // SB BackpackInventoryHandler.isAllowed: a backpack with an Inception upgrade can't go in.
        inv.addInsertFilter(ctx.hookOwner(), 900, [](core::InsertQuery const& q) {
            if (!game::isBackpack(q.stack)) return core::InsertVerdict::Allow;
            auto uuid = game::backpackUuid(q.stack);
            if (!uuid || !game::Storage::get().available()) return core::InsertVerdict::Allow;
            auto inner = game::Storage::get().find(*uuid);
            return inner && hasInception(*inner) ? core::InsertVerdict::Deny : core::InsertVerdict::Allow;
        });
    }

    bool canInstall(UpgradeContext const& ctx, std::string& reason) const override {
        if (storedInAnother(ctx.player(), ctx.backpack())) {
            reason = generic(ctx.player(), "abp.inception.error.sub_backpack", "Inception upgrade can only be placed in top level backpack");
            return false;
        }
        return true;
    }

    bool canRemove(UpgradeContext const& ctx, std::string& reason) const override {
        if (config::get<GeneralConfig>().allowNesting) return true;
        if (backpackSlots(ctx.backpack()).empty()) return true;
        reason = generic(ctx.player(), "abp.inception.error.remove_sub_backpack", "There's one or more backpacks in the backpack");
        return false;
    }

    bool canReplaceWith(UpgradeContext const& ctx, core::ItemStackData const& replacement, std::string& reason) const override {
        if (replacement.id == info().itemId) return true; // SB: swapping for another Inception is fine
        return canRemove(ctx, reason);
    }

    std::shared_ptr<ui::Menu> createSettingsMenu(Player& player, UpgradeContext const& ctx) override {
        // Opened from a nested backpack's upgrades page: remember the chain so Back returns there.
        std::optional<Chain> from;
        auto                 current = ui::MenuManager::get().current(player);
        if (auto const* nested = dynamic_cast<NestedBackpackMenu const*>(current.get());
            nested && nested->backpack() == ctx.backpackPtr()) {
            from = chainFor(nested, ctx.backpack());
        }
        return std::make_shared<InceptionSettingsMenu>(ctx, trFor(player, "abp.inception.tab", "Inception"), std::move(from));
    }

    [[nodiscard]] std::vector<std::string> describe(Player& player, UpgradeContext const&) const override {
        return {trFor(player, "abp.inception.tooltip", "§7Makes it possible to put backpacks into the backpack")};
    }
};

// Native tab (client/ui/tabs/inception): SB InceptionUpgradeTab, hidden when
// upgradesUseInventoriesOfBackpacksInBackpack is off.
nlohmann::json tabData(Player& player, UpgradeContext const& ctx) {
    return nlohmann::json{
        {"inception", {{"order", std::string(toString(orderOf(ctx)))}}},
        {"title", trFor(player, "abp.inception.tab", "Inception")},
        {"tooltip", trFor(player, "abp.inception.tab.tooltip", "Inception Settings")},
        {"hidden", !config::get<InceptionConfig>().upgradesUseInventoriesOfBackpacksInBackpack},
    };
}

bool tabAction(Player& player, UpgradeContext const& ctx, nlohmann::json const& data) {
    if (data.value("inception", std::string{}) != "toggle") return false;
    auto next = orderOf(ctx) == InventoryOrder::MainFirst ? InventoryOrder::InceptedFirst : InventoryOrder::MainFirst;
    setOrder(ctx, next);
    log().info("inception: {} set the inventory order to {}", player.getRealName(), toString(next));
    return true;
}

// ---- tanks and batteries of the backpacks inside (SB InceptionFluidHandler / InceptionEnergyStorage)

namespace {
class CombinedEnergy final : public api::IEnergyStorage {
public:
    explicit CombinedEnergy(std::vector<std::shared_ptr<api::IEnergyStorage>> parts) : mParts(std::move(parts)) {}
    int receive(int maxReceive, bool simulate) override {
        int total = 0;
        for (auto const& p : mParts) {
            total += p->receive(maxReceive - total, simulate);
            if (total == maxReceive) break;
        }
        return total;
    }
    int extract(int maxExtract, bool simulate) override {
        int total = 0;
        for (auto const& p : mParts) {
            total += p->extract(maxExtract - total, simulate);
            if (total == maxExtract) break;
        }
        return total;
    }
    [[nodiscard]] int stored() const override {
        long long total = 0;
        for (auto const& p : mParts) total += p->stored();
        return static_cast<int>(std::min<long long>(total, INT_MAX));
    }
    [[nodiscard]] int capacity() const override {
        long long total = 0;
        for (auto const& p : mParts) total += p->capacity();
        return static_cast<int>(std::min<long long>(total, INT_MAX));
    }
    [[nodiscard]] bool canReceive() const override { return !mParts.empty(); }
    [[nodiscard]] bool canExtract() const override { return !mParts.empty(); }

private:
    std::vector<std::shared_ptr<api::IEnergyStorage>> mParts;
};

/// Main + sub-backpack handlers in inventory order (nullopt: no Inception / off).
template <class T, class Get>
std::optional<std::vector<std::shared_ptr<T>>> ordered(std::shared_ptr<game::Backpack> const& bp, std::shared_ptr<T> inner, Get get) {
    if (!bp || !config::get<InceptionConfig>().upgradesInContainedBackpacksAreFunctional) return std::nullopt;
    auto order = inventoryOrderOf(*bp);
    if (!order) return std::nullopt;
    std::vector<std::shared_ptr<T>> parts;
    if (inner && *order == InventoryOrder::MainFirst) parts.push_back(inner);
    for (auto const& sub : subBackpacks(bp))
        if (auto h = get(sub)) parts.push_back(std::move(h));
    if (inner && *order == InventoryOrder::InceptedFirst) parts.push_back(inner);
    return parts;
}

struct WrapperRegistrar {
    WrapperRegistrar() {
        api::backpack::addFluidWrapper([](std::shared_ptr<game::Backpack> const& bp, std::shared_ptr<api::IFluidHandler> inner) -> std::shared_ptr<api::IFluidHandler> {
            auto parts = ordered<api::IFluidHandler>(bp, inner, [](auto const& sub) { return api::backpack::fluidHandler(sub); });
            if (!parts) return inner;
            if (parts->empty()) return nullptr;
            return std::make_shared<api::CombinedFluidHandler>(std::move(*parts));
        });
        api::backpack::addEnergyWrapper([](std::shared_ptr<game::Backpack> const& bp, std::shared_ptr<api::IEnergyStorage> inner) -> std::shared_ptr<api::IEnergyStorage> {
            auto parts = ordered<api::IEnergyStorage>(bp, inner, [](auto const& sub) { return api::backpack::energyStorage(sub); });
            if (!parts) return inner;
            if (parts->empty()) return nullptr;
            return std::make_shared<CombinedEnergy>(std::move(*parts));
        });
    }
} const gWrappers;
} // namespace

ABP_REGISTER_UPGRADE(InceptionUpgrade);
ABP_REGISTER_TAB_DATA(net::TabDataProvider{std::string(kId), &tabData});
ABP_REGISTER_TAB_ACTION(net::TabActionHandler{std::string(kId), &tabAction});

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
