// Battery upgrade (SB "Battery Upgrade"; Sophisticated Core `BatteryUpgradeItem` /
// `BatteryUpgradeWrapper`).

#include "upgrade/battery/Battery.h"

#include <algorithm>
#include <climits>
#include <cmath>

#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "api/BackpackCapabilities.h"
#include "api/Service.h"
#include "core/Capacity.h"
#include "core/screen/CountFormat.h"
#include "game/ItemConv.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "net/NativeScreens.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/tank/Columns.h"
#include "upgrade/tank/FastTick.h"
#include "upgrade/tank/MenuBits.h"

namespace abp::upgrade::battery {

/// `config/battery.json` (SB `batteryUpgrade` server config).
struct BatteryConfig {
    int version = 1;
    /// Energy in FE the battery upgrade will have per row of storage slots.
    int energyPerSlotRow = 10000;
    /// Ratio applied to the inventory stack multiplier before it multiplies the max energy and the
    /// max in / out (0 = stack upgrades don't change the battery).
    double stackMultiplierRatio = 1.0;
    /// FE moved in / out per operation per row of storage slots (times the stack multiplier).
    int maxInputOutput = 20;
};

namespace {
constexpr char const* kEnergy = "energy";
constexpr char const* kIn     = "in";
constexpr char const* kOut    = "out";

BatteryConfig const& cfg() { return config::get<BatteryConfig>(); }
double               ratio() { return std::clamp(cfg().stackMultiplierRatio, 0.0, 5.0); }

bool isBattery(UpgradeContext const& ctx) {
    auto const& slots = ctx.backpack().data().upgrades;
    return ctx.slot() >= 0 && ctx.slot() < static_cast<int>(slots.size()) && slots[static_cast<std::size_t>(ctx.slot())].item.id == kItemId;
}
} // namespace

int baseCapacity(game::Backpack& backpack) {
    return core::capacity::batteryCapacity(api::backpack::slotRows(backpack), 1.0, std::clamp(cfg().energyPerSlotRow, 500, 50000), 0.0);
}

int capacity(game::Backpack& backpack) {
    return core::capacity::batteryCapacity(
        api::backpack::slotRows(backpack),
        api::backpack::stackMultiplier(backpack),
        std::clamp(cfg().energyPerSlotRow, 500, 50000),
        ratio()
    );
}

int maxInOut(game::Backpack& backpack) {
    return core::capacity::batteryMaxInOut(
        api::backpack::slotRows(backpack),
        api::backpack::stackMultiplier(backpack),
        std::clamp(cfg().maxInputOutput, 1, 1000),
        ratio()
    );
}

int stored(UpgradeContext const& ctx) {
    if (!isBattery(ctx)) return 0;
    auto const& s = ctx.settings();
    if (!s.is_object()) return 0;
    auto it = s.find(kEnergy);
    return it != s.end() && it->is_number_integer() ? std::max(0, it->get<int>()) : 0;
}

void setStored(UpgradeContext const& ctx, int energy) {
    if (!isBattery(ctx)) return;
    auto& s = ctx.settings();
    if (!s.is_object()) s = nlohmann::json::object();
    s[kEnergy] = std::max(0, energy);
    ctx.saveSettings();
}

int receive(UpgradeContext const& ctx, int maxReceive, bool simulate) {
    if (!isBattery(ctx) || maxReceive <= 0) return 0;
    int have = stored(ctx);
    int ret  = std::max(0, std::min(capacity(ctx.backpack()) - have, std::min(maxInOut(ctx.backpack()), maxReceive)));
    if (!simulate && ret > 0) setStored(ctx, have + ret);
    return ret;
}

int extract(UpgradeContext const& ctx, int maxExtract, bool simulate) {
    if (!isBattery(ctx) || maxExtract <= 0) return 0;
    int have = stored(ctx);
    int ret  = std::max(0, std::min(have, std::min(maxInOut(ctx.backpack()), maxExtract)));
    if (!simulate && ret > 0) setStored(ctx, have - ret);
    return ret;
}

namespace {
/// The battery as the backpack's energy storage (other mods, placed-backpack cables).
class BatteryStorage final : public api::IEnergyStorage {
public:
    explicit BatteryStorage(UpgradeContext ctx) : mCtx(std::move(ctx)) {}
    int                receive(int maxReceive, bool simulate) override { return battery::receive(mCtx, maxReceive, simulate); }
    int                extract(int maxExtract, bool simulate) override { return battery::extract(mCtx, maxExtract, simulate); }
    [[nodiscard]] int  stored() const override { return battery::stored(mCtx); }
    [[nodiscard]] int  capacity() const override { return isBattery(mCtx) ? battery::capacity(mCtx.backpack()) : 0; }
    [[nodiscard]] bool canReceive() const override { return true; }
    [[nodiscard]] bool canExtract() const override { return true; }

private:
    UpgradeContext mCtx;
};

/// SB isValidEnergyItem.
bool isValidEnergyItem(core::ItemStackData const& item, bool isOutput) {
    if (item.empty()) return false;
    auto stack  = game::ItemConv::toStack(item, 1);
    auto energy = api::service().itemEnergy(stack);
    if (!energy) return false;
    return isOutput ? energy->canReceive() : energy->canExtract() && energy->stored() > 0;
}

/// Runs `fn` on the energy storage of the item in settings slot `key` and writes the item back.
template <class Fn>
void withSlotEnergy(UpgradeContext const& ctx, char const* key, Fn&& fn) {
    auto item = fluidui::slotItem(ctx, key);
    if (item.empty()) return;
    auto stack  = game::ItemConv::toStack(item, std::min(item.count, 255));
    auto energy = api::service().itemEnergy(stack);
    if (!energy) return;
    if (fn(*energy)) fluidui::setSlotItem(ctx, key, game::ItemConv::toData(stack));
}
} // namespace

void tick(UpgradeContext const& ctx) {
    if (!isBattery(ctx)) return;
    auto& bp = ctx.backpack();
    if (stored(ctx) < capacity(bp)) {
        // receiveFromStorage
        withSlotEnergy(ctx, kIn, [&](api::IEnergyStorage& item) {
            int toReceive = receive(ctx, maxInOut(bp), true);
            if (toReceive <= 0) return false;
            toReceive = item.extract(toReceive, true);
            if (toReceive <= 0) return false;
            item.extract(toReceive, false);
            receive(ctx, toReceive, false);
            return true;
        });
    }
    if (stored(ctx) > 0) {
        // extractToStorage
        withSlotEnergy(ctx, kOut, [&](api::IEnergyStorage& item) {
            int toExtract = extract(ctx, maxInOut(bp), true);
            if (toExtract <= 0) return false;
            toExtract = item.receive(toExtract, true);
            if (toExtract <= 0) return false;
            item.receive(toExtract, false);
            extract(ctx, toExtract, false);
            return true;
        });
    }
}

namespace {

int otherBatteries(UpgradeContext const& ctx) {
    int         n     = 0;
    auto const& slots = ctx.backpack().data().upgrades;
    for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
        if (i != ctx.slot() && slots[static_cast<std::size_t>(i)].item.id == kItemId) ++n;
    }
    return n;
}

class BatteryUpgrade : public IUpgrade {
public:
    [[nodiscard]] UpgradeInfo const& info() const override {
        static UpgradeInfo const kInfo{
            .id             = std::string(kId),
            .itemId         = std::string(kItemId),
            .nameKey        = "abp.battery.name",
            .nameFallback   = "Battery Upgrade",
            .group          = "battery",
            .maxPerBackpack = 1000, // SB's limit (one) is checked in canInstallItem with its message
            .hasSettings    = true,
            .toggleable     = false,
        };
        return kInfo;
    }

    bool canInstallItem(UpgradeContext const& ctx, core::ItemStackData const& item, std::string& reason) const override {
        auto msg = [&](std::string_view key, std::string_view fallback, auto const&... args) {
            return ctx.player() ? trFor(*ctx.player(), key, fallback, args...) : tr(key, fallback, args...);
        };
        if (otherBatteries(ctx) > 0) {
            reason = msg("abp.battery.error.exists", "There's already Battery Upgrade in another upgrade slot");
            return false;
        }
        auto settings = settingsOf(item);
        int  energy   = 0;
        if (auto it = settings.find(kEnergy); it != settings.end() && it->is_number_integer()) energy = std::max(0, it->get<int>());
        double required = static_cast<double>(energy) / std::max(1, capacity(ctx.backpack()));
        if (required > 1) {
            reason = msg(
                "abp.battery.error.energy_high",
                "Backpack can't support this battery upgrade. Upgrade backpack or add stack multiplier to get at least {}x higher max energy",
                core::capacity::formatRequiredMultiplier(required)
            );
            return false;
        }
        return columns::canInstall(ctx, item, reason);
    }

    void onInstall(UpgradeContext const& ctx) override { columns::sync(ctx.backpack()); }
    void onRemove(UpgradeContext const& ctx) override { columns::sync(ctx.backpack(), ctx.slot()); }
    void attach(UpgradeContext const& ctx, core::BackpackInventory&) override {
        if (columns::expected(ctx.backpack()) != ctx.backpack().data().columnsTaken) columns::sync(ctx.backpack());
    }

    void onTick(TickContext const& t) override {
        fasttick::keepAlive(t, "battery", [](Player&, UpgradeContext const& ctx, game::ItemLocation, int) { battery::tick(ctx); });
    }
    void onPlacedTick(PlacedTickContext const& t) override {
        for (int i = 0; i < std::max(1, t.interval); ++i) battery::tick(t.ctx);
    }

    std::shared_ptr<ui::Menu> createSettingsMenu(Player& player, UpgradeContext const& ctx) override {
        auto alive   = [ctx] { return isBattery(ctx); };
        auto factory = [ctx, alive]() {
            std::vector<std::shared_ptr<ui::Slot>> slots;
            slots.push_back(std::make_shared<fluidui::SettingsItemSlot>(
                ctx, kIn, [ctx](core::ItemStackData const& d) { return !fluidui::isOwnBackpack(ctx, d) && isValidEnergyItem(d, false); }, 1, alive
            ));
            slots.push_back(std::make_shared<fluidui::SettingsItemSlot>(
                ctx, kOut, [ctx](core::ItemStackData const& d) { return !fluidui::isOwnBackpack(ctx, d) && isValidEnergyItem(d, true); }, 1, alive
            ));
            return slots;
        };
        return std::make_shared<fluidui::ItemSlotsMenu>(trFor(player, "abp.battery.tab", "Batt."), ctx, factory, alive);
    }
};

ABP_REGISTER_BACKPACK_BATTERY(std::string(kId), [](UpgradeContext const& ctx) -> std::shared_ptr<api::IEnergyStorage> {
    return std::make_shared<BatteryStorage>(ctx);
});

ABP_REGISTER_TAB_DATA(net::TabDataProvider{
    std::string(kId),
    [](Player& player, UpgradeContext const& ctx) {
        return nlohmann::json{
            {"energy",           {{"stored", stored(ctx)}, {"capacity", capacity(ctx.backpack())}}},
            {"inventoryColumns", 2                                                                 },
            {"columnsTaken",     ctx.backpack().data().columnsTaken                               },
            {"tabLabel",         trFor(player, "abp.battery.tab", "Batt.")                         },
            {"tabTooltip",       trFor(player, "abp.battery.tab.tooltip", "Battery")               },
        };
    }
});

/// SC ClientStorageContentsTooltipBase.addEnergyTooltip: "§f1.2k§c FE" after the tanks.
ABP_REGISTER_CONTENTS_LINES([](Player& player, game::Backpack& backpack, std::vector<std::string>& lines) {
    auto const& slots = backpack.data().upgrades;
    for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
        if (slots[static_cast<std::size_t>(i)].item.id != kItemId) continue;
        UpgradeContext ctx(backpack.shared_from_this(), i);
        lines.push_back("§c" + trFor(player, "abp.battery.tooltip.energy", "{} FE", "§f" + core::screen::abbreviateCount(stored(ctx)) + "§c"));
        return;
    }
});

/// SB BatteryRenderInfo: the charge ratio for the backpack model's battery part.
ABP_REGISTER_RENDER_INFO([](game::Backpack& backpack, nlohmann::json& extra) {
    auto const& slots = backpack.data().upgrades;
    for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
        if (slots[static_cast<std::size_t>(i)].item.id != kItemId) continue;
        UpgradeContext ctx(backpack.shared_from_this(), i);
        extra["battery"] = {{"chargeRatio", static_cast<float>(stored(ctx)) / static_cast<float>(std::max(1, capacity(backpack)))}};
        return;
    }
});

} // namespace
} // namespace abp::upgrade::battery

ABP_REGISTER_CONFIG("battery", abp::upgrade::battery::BatteryConfig);
ABP_REGISTER_UPGRADE(abp::upgrade::battery::BatteryUpgrade);
ABP_REGISTER_INVENTORY_COLUMNS("battery", 2);
