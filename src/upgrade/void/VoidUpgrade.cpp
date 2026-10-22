// Void upgrade (basic + advanced), as Sophisticated Core's VoidUpgradeWrapper:
//
// - Modes ALWAYS / SLOT_OVERFLOW / STORAGE_OVERFLOW (VoidRules.h); `voidAlwaysEnabled` (config)
//   turns ALWAYS into SLOT_OVERFLOW.
// - Allow list by default: an empty allow list voids nothing, an empty block list everything.
// - "Work in GUI" (default off): items put in by hand are voided too (ALWAYS: the slot is voided
//   on the next tick, SLOT_OVERFLOW: what overflows the clicked slot).
// - Fluids: tank fills are voided through the same filter; filled containers (buckets, bottles)
//   in the filter stand for their fluid.
// - Basic: 9 filter slots, allow/block list only. Advanced: 16 slots, item/mod/tag match,
//   durability and NBT.

#include "upgrade/void/VoidUpgrade.h"

#include <algorithm>
#include <memory>
#include <set>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "api/BackpackCapabilities.h"
#include "api/Service.h"
#include "game/ItemConv.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/FilterMenu.h"
#include "upgrade/UpgradeManager.h"

ABP_REGISTER_CONFIG("void", abp::upgrade::voiding::VoidConfig);

namespace abp::upgrade::voiding {

// ---- settings -----------------------------------------------------------------------------------

VoidTierConfig tierConfig(bool advanced) {
    auto const& cfg = config::get<VoidConfig>();
    auto        c   = advanced ? cfg.advancedVoidUpgrade : cfg.voidUpgrade;
    c.filterSlots   = std::clamp(c.filterSlots, 1, 20);
    c.slotsInRow    = std::clamp(c.slotsInRow, 1, 6);
    return c;
}

VoidSettings VoidSettings::read(UpgradeContext const& ctx) {
    VoidSettings s;
    auto const&  j    = ctx.settings();
    auto*        type = upgradeFor(ctx.state().item);
    s.advanced        = type && type->info().id == kAdvancedId;
    auto tc           = tierConfig(s.advanced);
    s.voidAlwaysEnabled = tc.voidAlwaysEnabled;
    s.filter            = core::Filter(tc.filterSlots);
    s.filter.mode       = core::FilterMode::Allow; // SC: setAllowByDefault(true)
    if (j.is_object()) {
        if (auto it = j.find(kFilterKey); it != j.end() && it->is_object()) s.filter = core::Filter::fromJson(*it, tc.filterSlots);
        if (auto it = j.find(kVoidTypeKey); it != j.end() && it->is_string()) {
            s.storedType = voidTypeFromName(it->get<std::string>());
        } else if (auto old = j.find("overflow"); old != j.end() && old->is_boolean()) {
            // Phase-1 "overflow" flag (like SC's legacy migration).
            s.storedType = old->get<bool>() ? VoidType::SlotOverflow : VoidType::Always;
        }
        if (auto it = j.find(kWorkInGuiKey); it != j.end() && it->is_boolean()) s.workInGui = it->get<bool>();
    }
    s.filter.emptyAllowsAll = false;
    if (!s.advanced) {
        s.filter.matchBy         = core::MatchBy::Item;
        s.filter.matchNbt        = false;
        s.filter.matchDurability = false;
    }
    return s;
}

bool VoidSettings::matches(core::ItemStackData const& item) const {
    return !item.empty() && filter.passes(item, &game::ItemConv::tags());
}

api::FluidStack containedFluid(core::ItemStackData const& item) {
    if (item.empty()) return {};
    auto stack   = game::ItemConv::toStack(item, 1);
    auto handler = api::service().itemFluid(stack);
    return handler ? handler->drainAny(api::kBucketVolume, true) : api::FluidStack{};
}

bool VoidSettings::voidsFluid(api::FluidStack const& fluid) const {
    if (fluid.empty()) return false;
    bool matched = false;
    for (auto const& g : filter.ghosts) {
        if (!g.empty() && containedFluid(g).isSameFluid(fluid)) {
            matched = true;
            break;
        }
    }
    return (filter.mode == core::FilterMode::Allow) == matched;
}

VoidRules VoidSettings::rules() const {
    VoidRules r;
    r.type            = type();
    r.workInGui       = workInGui;
    r.matchDurability = filter.matchDurability;
    r.matchNbt        = filter.matchNbt;
    r.matches         = [s = *this](core::ItemStackData const& item) { return s.matches(item); };
    return r;
}

namespace {

/// True while `ctx` still points at a Void upgrade (a settings menu can outlive it).
bool alive(UpgradeContext const& ctx) {
    auto const& slots = ctx.backpack().data().upgrades;
    if (ctx.slot() < 0 || ctx.slot() >= static_cast<int>(slots.size())) return false;
    auto* type = upgradeFor(slots[static_cast<std::size_t>(ctx.slot())].item);
    return type && (type->info().id == kBasicId || type->info().id == kAdvancedId);
}

void writeSetting(UpgradeContext const& ctx, std::string_view key, nlohmann::json value) {
    if (!alive(ctx)) return;
    auto& j = ctx.settings();
    if (!j.is_object()) j = nlohmann::json::object();
    j[std::string(key)] = std::move(value);
    ctx.saveSettings(true);
}

// ---- "work in GUI" with ALWAYS: void the slot on the next tick (SC onSlotChange + tick) -----------

struct GuiVoider : std::enable_shared_from_this<GuiVoider> {
    std::weak_ptr<game::Backpack> backpack;
    VoidSettings                  settings;
    std::set<int>                 slots;
    bool                          scheduled = false;

    void onSlotChange(core::BackpackInventory const& inv, int slot);
    void drain();
};

std::vector<std::weak_ptr<GuiVoider>> gScheduled;

void GuiVoider::onSlotChange(core::BackpackInventory const& inv, int slot) {
    if (!settings.workInGui || settings.shouldVoidOverflow()) return;
    if (slot < 0 || slot >= inv.size() || !settings.matches(inv.at(slot))) return;
    slots.insert(slot);
    if (!scheduled) {
        scheduled = true;
        gScheduled.push_back(weak_from_this());
    }
}

void GuiVoider::drain() {
    scheduled = false;
    auto bp   = backpack.lock();
    if (!bp || slots.empty()) return;
    auto& inv = bp->inventory();
    for (int slot : slots) {
        if (slot < inv.size() && !inv.at(slot).empty()) {
            (void)inv.extract(slot, inv.at(slot).count, core::AccessContext{core::Source::Upgrade});
        }
    }
    slots.clear();
}

class VoidScheduler {
public:
    void install() {
        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) {
            if (gScheduled.empty()) return;
            auto list = std::move(gScheduled);
            gScheduled.clear();
            for (auto const& w : list) {
                if (auto v = w.lock()) v->drain();
            }
        });
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mTick);
        gScheduled.clear();
    }

private:
    ll::event::ListenerPtr mTick;
};

// ---- fluids (SB BackpackFluidHandler.fill) -------------------------------------------------------

bool shouldVoidFluid(std::shared_ptr<game::Backpack> const& bp, api::FluidStack const& fluid, VoidType type) {
    bool voids = false;
    forEachInstalled(bp, true, [&](IUpgrade& u, UpgradeContext const& ctx) {
        if (voids || (u.info().id != kBasicId && u.info().id != kAdvancedId)) return;
        auto s = VoidSettings::read(ctx);
        voids  = s.type() == type && s.voidsFluid(fluid);
    });
    return voids;
}

class VoidFluidHandler final : public api::IFluidHandler {
public:
    VoidFluidHandler(std::shared_ptr<game::Backpack> bp, std::shared_ptr<api::IFluidHandler> inner)
    : mBackpack(std::move(bp)),
      mInner(std::move(inner)) {}

    [[nodiscard]] int             tanks() const override { return mInner->tanks(); }
    [[nodiscard]] api::FluidStack fluidInTank(int tank) const override { return mInner->fluidInTank(tank); }
    [[nodiscard]] int             tankCapacity(int tank) const override { return mInner->tankCapacity(tank); }
    [[nodiscard]] bool isFluidValid(int tank, api::FluidStack const& stack) const override { return mInner->isFluidValid(tank, stack); }

    int fill(api::FluidStack const& resource, bool simulate) override {
        if (resource.empty()) return 0;
        if (shouldVoidFluid(mBackpack, resource, VoidType::Always)) return resource.amount;
        if (shouldVoidFluid(mBackpack, resource, VoidType::SlotOverflow)) {
            (void)mInner->fill(resource.copyWithAmount(std::min(resource.amount, oneTankSpace(resource))), simulate);
            return resource.amount;
        }
        int filled = mInner->fill(resource, simulate);
        if (filled == resource.amount || !shouldVoidFluid(mBackpack, resource, VoidType::StorageOverflow)) return filled;
        return resource.amount;
    }
    api::FluidStack drain(api::FluidStack const& resource, bool simulate) override { return mInner->drain(resource, simulate); }
    api::FluidStack drainAny(int maxDrain, bool simulate) override { return mInner->drainAny(maxDrain, simulate); }

private:
    /// SB `fillOneTank`: the first tank with this fluid, else the first empty one.
    [[nodiscard]] int oneTankSpace(api::FluidStack const& resource) const {
        for (int pass = 0; pass < 2; ++pass) {
            for (int t = 0; t < mInner->tanks(); ++t) {
                auto f = mInner->fluidInTank(t);
                if (pass == 0 ? f.isSameFluid(resource) : f.empty()) return std::max(0, mInner->tankCapacity(t) - (f.empty() ? 0 : f.amount));
            }
        }
        return 0;
    }

    std::shared_ptr<game::Backpack>     mBackpack;
    std::shared_ptr<api::IFluidHandler> mInner;
};

struct FluidWrapperRegistrar {
    FluidWrapperRegistrar() {
        api::backpack::addFluidWrapper([](std::shared_ptr<game::Backpack> const& bp, std::shared_ptr<api::IFluidHandler> inner)
                                           -> std::shared_ptr<api::IFluidHandler> {
            if (!inner || !bp || (!hasActive(*bp, kBasicId) && !hasActive(*bp, kAdvancedId))) return inner;
            return std::make_shared<VoidFluidHandler>(bp, std::move(inner));
        });
    }
};
FluidWrapperRegistrar gFluidWrapper;

// ---- settings menu (chest fallback) ---------------------------------------------------------------

std::pair<std::string, std::vector<std::string>> typeText(Player& player, VoidType t, bool alwaysEnabled) {
    std::vector<std::string> lore;
    std::string              title;
    switch (t) {
    case VoidType::Always:
        title = trFor(player, "abp.void.button.void_always", "Void Always");
        break;
    case VoidType::SlotOverflow:
        title = trFor(player, "abp.void.button.void_slot_overflow", "Void Slot Overflow");
        lore  = {"§7" + trFor(player, "abp.void.button.void_slot_overflow.detail", "Allows single slot to be filled with the item\nand voids anything that overflows")};
        break;
    case VoidType::StorageOverflow:
        title = trFor(player, "abp.void.button.void_storage_overflow", "Void Storage Overflow");
        lore  = {"§7" + trFor(player, "abp.void.button.void_storage_overflow.detail", "Allows whole storage to be filled with the item\n and voids anything that overflows")};
        break;
    }
    if (!alwaysEnabled && t != VoidType::Always) {
        lore.push_back("§c" + trFor(player, "abp.void.button.void_always_disabled", "Void Always option is disabled"));
    }
    return {title, lore};
}

class VoidMenu : public ui::FilterMenu {
public:
    VoidMenu(std::string title, UpgradeContext ctx, bool advanced, Options options)
    : FilterMenu(
          std::move(title),
          [ctx] { return alive(ctx) ? VoidSettings::read(ctx).filter : core::Filter(9); },
          [ctx](core::Filter const& f) { writeSetting(ctx, kFilterKey, f.toJson()); },
          std::move(options)
      ),
      mCtx(std::move(ctx)) {
        (void)advanced;
    }

    void build(Player& player) override {
        FilterMenu::build(player);
        if (!alive(mCtx)) return;
        int  control = size() - 9;
        auto s       = VoidSettings::read(mCtx);
        setSlot(
            control + 5,
            std::make_shared<ui::ButtonSlot>(
                ui::icon(
                    "minecraft:crafting_table",
                    s.workInGui ? trFor(player, "abp.void.button.works_in_gui", "Works in GUI as well")
                                : trFor(player, "abp.void.button.only_automatic", "Only works with other upgrades/automation")
                ),
                [this](ui::ClickContext const&) {
                    writeSetting(mCtx, kWorkInGuiKey, !VoidSettings::read(mCtx).workInGui);
                    invalidate();
                }
            )
        );
        auto text = typeText(player, s.type(), s.voidAlwaysEnabled);
        setSlot(
            control + 6,
            std::make_shared<ui::ButtonSlot>(ui::icon("minecraft:lava_bucket", text.first, text.second), [this](ui::ClickContext const&) {
                auto cur = VoidSettings::read(mCtx);
                writeSetting(mCtx, kVoidTypeKey, std::string(toString(next(cur.type(), cur.voidAlwaysEnabled))));
                invalidate();
            })
        );
    }

private:
    UpgradeContext mCtx;
};

// ---- upgrades ---------------------------------------------------------------------------------

class VoidUpgradeBase : public IUpgrade {
public:
    void attach(UpgradeContext const& ctx, core::BackpackInventory& inventory) override {
        auto s = VoidSettings::read(ctx);
        inventory.addInsertFilter(ctx.hookOwner(), kInsertPriority, [r = s.rules()](core::InsertQuery const& q) { return verdict(r, q); });
        auto gui      = std::make_shared<GuiVoider>();
        gui->backpack = ctx.backpackPtr();
        gui->settings = s;
        inventory.addInsertListener(ctx.hookOwner(), [gui](int slot, core::AccessContext const& ac) {
            if (ac.source != core::Source::Player) return;
            if (auto bp = gui->backpack.lock()) gui->onSlotChange(bp->inventory(), slot);
        });
        inventory.addExtractListener(ctx.hookOwner(), [gui](int slot, core::ItemStackData const&, core::AccessContext const& ac) {
            if (ac.source != core::Source::Player) return;
            if (auto bp = gui->backpack.lock()) gui->onSlotChange(bp->inventory(), slot);
        });
    }

    std::shared_ptr<ui::Menu> createSettingsMenu(Player& player, UpgradeContext const& ctx) override {
        bool                    advanced = info().id == kAdvancedId;
        ui::FilterMenu::Options options;
        options.allowMatchBy    = advanced;
        options.allowNbt        = advanced;
        options.allowDurability = advanced;
        options.onBack          = [bp = ctx.backpackPtr()](Player& p) {
            ui::BackpackMenu::openBackpack(p, bp, {}, ui::BackpackMenu::View::Upgrades);
        };
        options.slots = tierConfig(advanced).filterSlots;
        return std::make_shared<VoidMenu>(displayName(*this, &player), ctx.withPlayer(nullptr), advanced, std::move(options));
    }

    [[nodiscard]] std::vector<std::string> describe(Player& player, UpgradeContext const& ctx) const override {
        auto s = VoidSettings::read(ctx);
        return {"§7" + typeText(player, s.type(), true).first};
    }
};

class VoidUpgrade final : public VoidUpgradeBase {
public:
    [[nodiscard]] UpgradeInfo const& info() const override {
        static UpgradeInfo const i{
            .id             = std::string(kBasicId),
            .itemId         = "alleviatedbackpacks:void_upgrade",
            .nameKey        = "abp.void.name",
            .nameFallback   = "Void Upgrade",
            .tier           = UpgradeTier::Basic,
            .group          = "void",
            .maxPerBackpack = 9, // SB: no limit
            .hasSettings    = true,
        };
        return i;
    }
};

class AdvancedVoidUpgrade final : public VoidUpgradeBase {
public:
    [[nodiscard]] UpgradeInfo const& info() const override {
        static UpgradeInfo const i{
            .id             = std::string(kAdvancedId),
            .itemId         = "alleviatedbackpacks:advanced_void_upgrade",
            .nameKey        = "abp.void.advanced_name",
            .nameFallback   = "Advanced Void Upgrade",
            .tier           = UpgradeTier::Advanced,
            .group          = "void",
            .maxPerBackpack = 9,
            .hasSettings    = true,
        };
        return i;
    }
};

} // namespace
} // namespace abp::upgrade::voiding

ABP_REGISTER_UPGRADE(abp::upgrade::voiding::VoidUpgrade);
ABP_REGISTER_UPGRADE(abp::upgrade::voiding::AdvancedVoidUpgrade);
ABP_REGISTER_MODULE(abp::upgrade::voiding::VoidScheduler);
