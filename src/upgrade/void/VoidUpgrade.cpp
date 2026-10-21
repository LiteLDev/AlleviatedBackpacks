// Void upgrade (basic + advanced): destroys matching items instead of storing them.
//
// - Basic: 9 filter slots (allow/deny list, match by item).
// - Advanced: 16 filter slots, match by item/namespace/tag, NBT, durability, and an "overflow"
//   mode that only voids what doesn't fit next to the stack already stored.
// - Both: "work in GUI" toggle (whether items a player puts in by hand are voided too).
//
// Voiding happens on every insert path (pickup, automation, other upgrades, and player inserts
// when "work in GUI" is on) through an insert filter, and on item pickup from the ground through
// `onPickup` (even without a Pickup upgrade, like the original mod).

#include "upgrade/void/VoidUpgrade.h"

#include <algorithm>

#include "mc/world/actor/player/Player.h"

#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "mod/I18n.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/FilterMenu.h"
#include "upgrade/UpgradeManager.h"

namespace abp::upgrade::voiding {

// ---- logic ------------------------------------------------------------------------------------

VoidSettings VoidSettings::read(UpgradeContext const& ctx) {
    VoidSettings s;
    auto const&  j = ctx.settings();
    auto*        type = upgradeFor(ctx.state().item);
    s.advanced        = type && type->info().id == kAdvancedId;
    int slots         = s.advanced ? kAdvancedSlots : kBasicSlots;
    auto filterJson   = j.is_object() ? j.value(std::string(kFilterKey), nlohmann::json::object()) : nlohmann::json::object();
    s.filter          = core::Filter::fromJson(filterJson, slots);
    if (!s.advanced) {
        // The basic upgrade only matches by item.
        s.filter.matchBy         = core::MatchBy::Item;
        s.filter.matchNbt        = false;
        s.filter.matchDurability = false;
    }
    // An empty filter voids nothing (see matches()).
    s.filter.emptyAllowsAll = false;
    auto flag               = [&](std::string_view key) {
        if (!j.is_object()) return false;
        auto it = j.find(std::string(key));
        return it != j.end() && it->is_boolean() && it->get<bool>();
    };
    s.overflow  = s.advanced && flag(kOverflowKey);
    s.workInGui = flag(kWorkInGuiKey);
    return s;
}

bool isProtected(core::ItemStackData const& item) { return game::isBackpack(item) || upgradeFor(item) != nullptr; }

bool matches(VoidSettings const& s, core::ItemStackData const& item) {
    if (item.empty() || isProtected(item)) return false;
    // An empty filter never voids, in either mode: a fresh upgrade toggled to "deny" before any
    // item was listed must not start destroying everything.
    if (s.filter.isEmpty()) return false;
    return s.filter.passes(item, &game::ItemConv::tags());
}

core::InsertVerdict verdict(VoidSettings const& s, core::InsertQuery const& q) {
    using V = core::InsertVerdict;
    switch (q.ctx.source) {
    case core::Source::Internal:
        return V::Allow;
    case core::Source::Player:
        if (!s.workInGui) return V::Allow;
        break;
    default:
        break;
    }
    if (!matches(s, q.stack)) return V::Allow;
    if (!s.overflow) return V::Void;

    // Overflow mode: keep one stack (slot) of each matching item; whatever doesn't fit there is
    // voided instead of starting new stacks (or bouncing back when the backpack is full).
    if (q.slot < 0) return V::VoidOverflow; // whole stack: insert what fits, void the rest
    auto const& inv = q.inventory;
    if (!inv.at(q.slot).empty()) return V::Allow; // merging into a stack (or a different item: no fit anyway)
    for (int i = 0; i < inv.size(); ++i) {
        if (i == q.slot) continue;
        auto const& cur = inv.at(i);
        if (!cur.empty() && cur.sameItem(q.stack)) return V::Void; // would start a second stack
    }
    return V::Allow;
}

namespace {

/// True while `ctx` still points at the same kind of Void upgrade (a settings menu can outlive
/// it, and another tier could be installed into the slot meanwhile).
bool alive(UpgradeContext const& ctx, bool advanced) {
    auto const& slots = ctx.backpack().data().upgrades;
    if (ctx.slot() < 0 || ctx.slot() >= static_cast<int>(slots.size())) return false;
    auto* type = upgradeFor(slots[static_cast<std::size_t>(ctx.slot())].item);
    return type && type->info().id == (advanced ? kAdvancedId : kBasicId);
}

/// Settings parsed by the last `attach()` (hooks are rebuilt on every settings change).
struct SettingsCache {
    bool         valid = false;
    VoidSettings settings;
};

/// How many of `item` the backpack can still take: free room in stacks of the same item, or
/// (none stored yet) one empty slot. Respects every insert filter.
int room(core::BackpackInventory const& inv, core::ItemStackData const& item, core::AccessContext const& ac) {
    long long fits   = 0;
    bool      stored = false;
    int       empty  = -1;
    for (int i = 0; i < inv.size(); ++i) {
        auto const& cur = inv.at(i);
        if (cur.empty()) {
            if (empty < 0 && inv.checkInsert(i, item, ac) == core::InsertVerdict::Allow) empty = i;
            continue;
        }
        if (!cur.sameItem(item)) continue;
        stored = true;
        if (inv.checkInsert(i, item, ac) == core::InsertVerdict::Allow) fits += std::max(0, inv.slotLimit(i, cur) - cur.count);
    }
    if (!stored && empty >= 0) fits = inv.slotLimit(empty, item);
    return static_cast<int>(std::min<long long>(fits, item.count));
}

// ---- settings menu ------------------------------------------------------------------------------

/// The filter editor plus two toggles in the free control-row slots: overflow mode (advanced)
/// and "work in GUI".
class VoidMenu : public ui::FilterMenu {
public:
    VoidMenu(std::string title, UpgradeContext ctx, bool advanced, Options options)
    : FilterMenu(
          std::move(title),
          [ctx, advanced] {
              int slots = advanced ? kAdvancedSlots : kBasicSlots;
              if (!alive(ctx, advanced) || !ctx.settings().is_object()) return core::Filter(slots);
              return core::Filter::fromJson(ctx.settings().value(std::string(kFilterKey), nlohmann::json::object()), slots);
          },
          [ctx, advanced](core::Filter const& f) {
              if (!alive(ctx, advanced)) return;
              if (!ctx.settings().is_object()) ctx.settings() = nlohmann::json::object();
              ctx.settings()[std::string(kFilterKey)] = f.toJson();
              ctx.saveSettings(true);
          },
          std::move(options)
      ),
      mCtx(std::move(ctx)),
      mAdvanced(advanced) {}

    void build(Player& player) override {
        FilterMenu::build(player);
        int  control = size() - 9;
        auto onOff   = [&](bool v) {
            return v ? trFor(player, "abp.filter.on", "§aOn") : trFor(player, "abp.filter.off", "§cOff");
        };
        if (!alive(mCtx, mAdvanced)) return;
        auto s = VoidSettings::read(mCtx);
        if (mAdvanced) {
            setSlot(
                control + 5,
                std::make_shared<ui::ButtonSlot>(
                    ui::icon(
                        s.overflow ? "minecraft:hopper" : "minecraft:lava_bucket",
                        s.overflow ? trFor(player, "abp.void.mode.overflow", "Void: only overflow")
                                   : trFor(player, "abp.void.mode.all", "Void: all matching items"),
                        {s.overflow ? trFor(player, "abp.void.mode.overflow.desc", "§7Keeps one stack, voids what doesn't fit")
                                    : trFor(player, "abp.void.mode.all.desc", "§7Every matching item is destroyed"),
                         trFor(player, "abp.filter.click_to_toggle", "§7Click to toggle")}
                    ),
                    [this](ui::ClickContext const&) { toggle(kOverflowKey); }
                )
            );
        }
        setSlot(
            control + 6,
            std::make_shared<ui::ButtonSlot>(
                ui::icon(
                    "minecraft:chest",
                    trFor(player, "abp.void.work_in_gui", "Work in GUI: {}", onOff(s.workInGui)),
                    {trFor(player, "abp.void.work_in_gui.desc", "§7Also void items you put in by hand"),
                     trFor(player, "abp.filter.click_to_toggle", "§7Click to toggle")}
                ),
                [this](ui::ClickContext const&) { toggle(kWorkInGuiKey); }
            )
        );
    }

private:
    void toggle(std::string_view key) {
        if (!alive(mCtx, mAdvanced)) return;
        auto& j = mCtx.settings();
        if (!j.is_object()) j = nlohmann::json::object();
        auto it             = j.find(std::string(key));
        bool on             = it != j.end() && it->is_boolean() && it->get<bool>();
        j[std::string(key)] = !on;
        mCtx.saveSettings(true);
        invalidate();
    }

    UpgradeContext mCtx;
    bool           mAdvanced;
};

// ---- upgrades ---------------------------------------------------------------------------------

class VoidUpgradeBase : public IUpgrade {
public:
    void attach(UpgradeContext const& ctx, core::BackpackInventory& inventory) override {
        auto s                         = VoidSettings::read(ctx);
        ctx.transient<SettingsCache>() = SettingsCache{true, s};
        inventory.addInsertFilter(ctx.hookOwner(), kInsertPriority, [s = std::move(s)](core::InsertQuery const& q) {
            return verdict(s, q);
        });
    }

    /// Items picked up from the ground are voided even without a Pickup upgrade. The decision
    /// goes through the inventory's insert filters, so a Deny from the foundation rules (e.g.
    /// shulker boxes) or another upgrade wins over voiding.
    void onPickup(UpgradeContext const& ctx, core::ItemStackData& item) override {
        auto&       cache = ctx.transient<SettingsCache>();
        auto const& s     = cache.valid ? cache.settings : (cache = SettingsCache{true, VoidSettings::read(ctx)}).settings;
        if (!matches(s, item)) return;
        auto const&         inv = ctx.backpack().inventory();
        core::AccessContext ac{core::Source::Pickup, ctx.player()};
        switch (inv.checkInsert(-1, item, ac)) {
        case core::InsertVerdict::Void:
            item.count = 0;
            return;
        case core::InsertVerdict::VoidOverflow:
            // Overflow: void only what the backpack couldn't take; what fits is left for a Pickup
            // upgrade (or the player's inventory).
            item.count = room(inv, item, ac);
            return;
        default:
            return;
        }
    }

    std::shared_ptr<ui::Menu> createSettingsMenu(Player& player, UpgradeContext const& ctx) override {
        bool                     advanced = info().id == kAdvancedId;
        ui::FilterMenu::Options  options;
        options.allowMatchBy    = advanced;
        options.allowNbt        = advanced;
        options.allowDurability = advanced;
        options.onBack          = [bp = ctx.backpackPtr()](Player& p) {
            ui::BackpackMenu::openBackpack(p, bp, {}, ui::BackpackMenu::View::Upgrades);
        };
        options.slots = advanced ? kAdvancedSlots : kBasicSlots;
        return std::make_shared<VoidMenu>(displayName(*this, &player), ctx.withPlayer(nullptr), advanced, std::move(options));
    }

    [[nodiscard]] std::vector<std::string> describe(Player& player, UpgradeContext const& ctx) const override {
        auto s     = VoidSettings::read(ctx);
        int  count = static_cast<int>(std::count_if(s.filter.ghosts.begin(), s.filter.ghosts.end(), [](auto const& g) {
            return !g.empty();
        }));
        if (s.filter.matchBy == core::MatchBy::Tag) count += static_cast<int>(s.filter.tags.size());
        std::vector<std::string> lore;
        lore.push_back(
            s.filter.mode == core::FilterMode::Allow
                ? trFor(player, "abp.void.describe.allow", "§7Voids {} listed item(s)", count)
                : trFor(player, "abp.void.describe.deny", "§7Voids everything except {} listed item(s)", count)
        );
        if (s.overflow) lore.push_back(trFor(player, "abp.void.describe.overflow", "§7Only overflow"));
        if (s.workInGui) lore.push_back(trFor(player, "abp.void.describe.gui", "§7Also in the GUI"));
        return lore;
    }
};

class VoidUpgrade final : public VoidUpgradeBase {
public:
    [[nodiscard]] UpgradeInfo const& info() const override {
        static UpgradeInfo const i{
            .id           = std::string(kBasicId),
            .itemId       = "alleviatedbackpacks:void_upgrade",
            .nameKey      = "abp.void.name",
            .nameFallback = "Void Upgrade",
            .tier         = UpgradeTier::Basic,
            .group        = "void",
            .hasSettings  = true,
        };
        return i;
    }
};

class AdvancedVoidUpgrade final : public VoidUpgradeBase {
public:
    [[nodiscard]] UpgradeInfo const& info() const override {
        static UpgradeInfo const i{
            .id           = std::string(kAdvancedId),
            .itemId       = "alleviatedbackpacks:advanced_void_upgrade",
            .nameKey      = "abp.void.advanced_name",
            .nameFallback = "Advanced Void Upgrade",
            .tier         = UpgradeTier::Advanced,
            .group        = "void",
            .hasSettings  = true,
        };
        return i;
    }
};

} // namespace
} // namespace abp::upgrade::voiding

ABP_REGISTER_UPGRADE(abp::upgrade::voiding::VoidUpgrade);
ABP_REGISTER_UPGRADE(abp::upgrade::voiding::AdvancedVoidUpgrade);
