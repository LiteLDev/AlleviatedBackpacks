#include "api/BackpackCapabilities.h"

#include <algorithm>
#include <map>
#include <vector>

#include "core/Capacity.h"
#include "core/ItemStackData.h"
#include "game/Backpack.h"
#include "upgrade/UpgradeManager.h"

namespace abp::api::backpack {

namespace {
struct Registry {
    std::map<std::string, TankFactory, std::less<>>   tanks;
    std::map<std::string, EnergyFactory, std::less<>> energy;
    std::vector<FluidWrapper>                         fluidWrappers;
    std::vector<EnergyWrapper>                        energyWrappers;
};

Registry& registry() {
    static Registry r;
    return r;
}
} // namespace

void registerTankUpgrade(std::string upgradeId, TankFactory factory) {
    if (factory) registry().tanks[std::move(upgradeId)] = std::move(factory);
}

void registerEnergyUpgrade(std::string upgradeId, EnergyFactory factory) {
    if (factory) registry().energy[std::move(upgradeId)] = std::move(factory);
}

void addFluidWrapper(FluidWrapper wrapper) {
    if (wrapper) registry().fluidWrappers.push_back(std::move(wrapper));
}

void addEnergyWrapper(EnergyWrapper wrapper) {
    if (wrapper) registry().energyWrappers.push_back(std::move(wrapper));
}

std::shared_ptr<IFluidHandler> fluidHandler(std::shared_ptr<game::Backpack> const& backpack) {
    if (!backpack) return nullptr;
    auto&                                       r = registry();
    std::vector<std::shared_ptr<IFluidHandler>> parts;
    // Active upgrades only: an upgrade type the server switched off contributes nothing.
    upgrade::forEachInstalled(backpack, true, [&](upgrade::IUpgrade& u, upgrade::UpgradeContext const& ctx) {
        auto it = r.tanks.find(u.info().id);
        if (it == r.tanks.end()) return;
        if (auto h = it->second(ctx)) parts.push_back(std::move(h));
    });
    std::shared_ptr<IFluidHandler> out;
    if (!parts.empty()) out = std::make_shared<CombinedFluidHandler>(std::move(parts));
    for (auto const& w : r.fluidWrappers) out = w(backpack, std::move(out));
    return out;
}

std::shared_ptr<IEnergyStorage> energyStorage(std::shared_ptr<game::Backpack> const& backpack) {
    if (!backpack) return nullptr;
    auto&                           r = registry();
    std::shared_ptr<IEnergyStorage> out;
    upgrade::forEachInstalled(backpack, true, [&](upgrade::IUpgrade& u, upgrade::UpgradeContext const& ctx) {
        if (out) return;
        auto it = r.energy.find(u.info().id);
        if (it != r.energy.end()) out = it->second(ctx);
    });
    for (auto const& w : r.energyWrappers) out = w(backpack, std::move(out));
    return out;
}

std::shared_ptr<IFluidHandler> tankOf(upgrade::UpgradeContext const& ctx) {
    auto const& slots = ctx.backpack().data().upgrades;
    if (ctx.slot() < 0 || ctx.slot() >= static_cast<int>(slots.size())) return nullptr;
    auto* type = upgrade::upgradeFor(slots[static_cast<std::size_t>(ctx.slot())].item);
    if (!type) return nullptr;
    auto& r  = registry();
    auto  it = r.tanks.find(type->info().id);
    return it == r.tanks.end() ? nullptr : it->second(ctx);
}

std::shared_ptr<IEnergyStorage> energyOf(upgrade::UpgradeContext const& ctx) {
    auto const& slots = ctx.backpack().data().upgrades;
    if (ctx.slot() < 0 || ctx.slot() >= static_cast<int>(slots.size())) return nullptr;
    auto* type = upgrade::upgradeFor(slots[static_cast<std::size_t>(ctx.slot())].item);
    if (!type) return nullptr;
    auto& r  = registry();
    auto  it = r.energy.find(type->info().id);
    return it == r.energy.end() ? nullptr : it->second(ctx);
}

int slotRows(game::Backpack& backpack) { return core::capacity::slotRows(backpack.inventory().size()); }

double stackMultiplier(game::Backpack& backpack) {
    auto& inv = backpack.inventory();
    if (inv.size() <= 0) return 1.0;
    auto probe = core::ItemStackData::of("minecraft:stone", 1, 64);
    return std::max(1.0, static_cast<double>(inv.slotLimit(0, probe)) / 64.0);
}

} // namespace abp::api::backpack
