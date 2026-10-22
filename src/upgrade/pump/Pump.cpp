// Pump and Advanced Pump upgrades (SB "Pump Upgrade" / "Advanced Pump Upgrade"; Sophisticated
// Core `PumpUpgradeItem` / `PumpUpgradeWrapper` / `FluidFilterLogic`).
//
// Settings JSON: {"input": bool, "hand": bool, "world": bool, "handlers": bool,
//                 "filter": ["<phase-1 fluid id>" | "", ...]}
// Missing keys use the tier's defaults (SB data components fall back to the item's defaults).

#include "upgrade/pump/Pump.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <set>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDestroyBlockEvent.h"
#include "ll/api/event/player/PlayerPlaceBlockEvent.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/player/AbilitiesIndex.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/VanillaStates.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/states/BlockStateVariant.h"
#include "mc/world/level/dimension/Dimension.h"

#include "api/BackpackCapabilities.h"
#include "api/Service.h"
#include "core/Capacity.h"
#include "core/FluidRegistry.h"
#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "net/NativeScreens.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/tank/FastTick.h"
#include "upgrade/tank/MenuBits.h"
#include "upgrade/tank/Tank.h"

namespace abp::upgrade::pump {

/// `config/pump.json` (SB `pumpUpgrade` server config).
struct PumpConfig {
    int version = 2;
    /// Number of fluid filter slots (advanced pump).
    int filterSlots = 4;
    /// mB moved in / out per operation with tanks and pipes, per row of storage slots (times the
    /// stack multiplier, at least a bucket).
    int maxInputOutput = 20;
    /// Ratio applied to the inventory stack multiplier before it multiplies the max in / out.
    double stackMultiplierRatio = 1.0;
};

namespace {
constexpr int kDidNothingCooldown      = 40;
constexpr int kHandInteractionCooldown = 3;
constexpr int kWorldInteractionCooldown = 20;
constexpr int kFluidHandlerCooldown    = 20;
constexpr int kPlayerSearchRange       = 3;
constexpr int kPumpInWorldRangeSqr     = 4 * 4;

PumpConfig const& cfg() { return config::get<PumpConfig>(); }

bool isPump(UpgradeContext const& ctx) {
    auto const& slots = ctx.backpack().data().upgrades;
    if (ctx.slot() < 0 || ctx.slot() >= static_cast<int>(slots.size())) return false;
    auto const& id = slots[static_cast<std::size_t>(ctx.slot())].item.id;
    return id == "alleviatedbackpacks:pump_upgrade" || id == "alleviatedbackpacks:advanced_pump_upgrade";
}

int maxInOut(game::Backpack& bp) {
    return core::capacity::pumpMaxInOut(
        api::backpack::slotRows(bp),
        api::backpack::stackMultiplier(bp),
        std::clamp(cfg().maxInputOutput, 1, 1000),
        std::clamp(cfg().stackMultiplierRatio, 0.0, 5.0)
    );
}

// ---- world helpers ----------------------------------------------------------------------------------

/// Java fluidState.isSource(): a still water / lava source block.
bool isFluidSource(Block const& block) {
    auto const& name = block.getTypeName();
    if (name != "minecraft:water" && name != "minecraft:flowing_water" && name != "minecraft:lava" && name != "minecraft:flowing_lava") return false;
    auto depth = block.getState<int>(VanillaStates::LiquidDepth());
    return depth && *depth == 0;
}

bool isLiquid(Block const& block) {
    auto const& n = block.getTypeName();
    return n == "minecraft:water" || n == "minecraft:flowing_water" || n == "minecraft:lava" || n == "minecraft:flowing_lava";
}

/// SB isValidForFluidPlacement: air, or a flowing (non-source) liquid.
bool validForPlacement(BlockSource& bs, BlockPos const& pos) {
    auto const& b = bs.getBlock(pos);
    return b.isAir() || (isLiquid(b) && !isFluidSource(b));
}

/// SB WorldHelper.playerMayInteract: abilities plus LeviLamina's place / destroy events (protection
/// mods cancel those). No player (placed backpack): allowed.
bool mayInteract(Player* player, BlockPos const& pos, bool place) {
    if (!player) return true;
    if (!player->canUseAbility(AbilitiesIndex::Build) || !player->canUseAbility(AbilitiesIndex::Mine)) return false;
    auto& bus = ll::event::EventBus::getInstance();
    if (place) {
        auto ev = ll::event::PlayerPlacingBlockEvent(*player, pos, static_cast<uchar>(1));
        bus.publish(ev);
        return !ev.isCancelled();
    }
    auto ev = ll::event::PlayerDestroyBlockEvent(*player, pos);
    bus.publish(ev);
    return !ev.isCancelled();
}

constexpr int kDirs[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}}; // Direction.values()
constexpr api::Side kOpposite[6] = {api::Side::Up, api::Side::Down, api::Side::South, api::Side::North, api::Side::East, api::Side::West};

BlockPos offset(BlockPos const& p, int d) { return {p.x + kDirs[d][0], p.y + kDirs[d][1], p.z + kDirs[d][2]}; }
long long distSqr(BlockPos const& a, BlockPos const& b) {
    long long dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

/// A world fluid source as a drain-only handler (NeoForge BucketPickupHandlerWrapper: only whole
/// buckets).
class SourceBlockHandler final : public api::IFluidHandler {
public:
    SourceBlockHandler(BlockSource& bs, BlockPos pos) : mBs(bs), mPos(pos) {}
    [[nodiscard]] int             tanks() const override { return 1; }
    [[nodiscard]] api::FluidStack fluidInTank(int) const override { return api::service().pickupFluidSource(mBs, mPos, true); }
    [[nodiscard]] int             tankCapacity(int) const override { return api::kBucketVolume; }
    [[nodiscard]] bool            isFluidValid(int, api::FluidStack const&) const override { return false; }
    int                           fill(api::FluidStack const&, bool) override { return 0; }
    api::FluidStack               drain(api::FluidStack const& resource, bool simulate) override {
        auto there = api::service().pickupFluidSource(mBs, mPos, true);
        if (there.empty() || !there.isSameFluid(resource) || resource.amount < there.amount) return {};
        return simulate ? there : api::service().pickupFluidSource(mBs, mPos, false);
    }
    api::FluidStack drainAny(int maxDrain, bool simulate) override {
        auto there = api::service().pickupFluidSource(mBs, mPos, true);
        if (there.empty() || maxDrain < there.amount) return {};
        return simulate ? there : api::service().pickupFluidSource(mBs, mPos, false);
    }

private:
    BlockSource& mBs;
    BlockPos     mPos;
};

/// Places a fluid source when filled with a whole bucket (NeoForge's fluid block handler).
class PlaceBlockHandler final : public api::IFluidHandler {
public:
    PlaceBlockHandler(BlockSource& bs, BlockPos pos) : mBs(bs), mPos(pos) {}
    [[nodiscard]] int             tanks() const override { return 1; }
    [[nodiscard]] api::FluidStack fluidInTank(int) const override { return {}; }
    [[nodiscard]] int             tankCapacity(int) const override { return api::kBucketVolume; }
    [[nodiscard]] bool            isFluidValid(int, api::FluidStack const& s) const override { return !s.empty(); }
    int fill(api::FluidStack const& resource, bool simulate) override {
        if (resource.empty() || resource.amount < api::kBucketVolume) return 0;
        return api::service().placeFluidSource(mBs, mPos, resource.copyWithAmount(api::kBucketVolume), simulate);
    }
    api::FluidStack drain(api::FluidStack const&, bool) override { return {}; }
    api::FluidStack drainAny(int, bool) override { return {}; }

private:
    BlockSource& mBs;
    BlockPos     mPos;
};

// ---- SB pump logic ------------------------------------------------------------------------------------

struct PumpState {
    std::int64_t until          = 0;
    std::int64_t lastHandAction = -1000;
};

class Logic {
public:
    Logic(UpgradeContext ctx, Settings s, api::IFluidHandler& storage) : mCtx(std::move(ctx)), mS(std::move(s)), mStorage(storage) {}

    /// SB fillFromFluidHandler.
    bool fillFromFluidHandler(api::IFluidHandler& h, int maxDrain) {
        auto contained = h.drainAny(maxDrain, true);
        if (contained.empty() || !mS.matches(contained)) return false;
        return !tryFluidTransfer(mStorage, h, contained).empty();
    }

    /// SB fillFluidHandler.
    bool fillFluidHandler(api::IFluidHandler& h, int maxFill) {
        for (int t = 0; t < mStorage.tanks(); ++t) {
            auto tankFluid = mStorage.fluidInTank(t);
            if (!tankFluid.empty() && mS.matches(tankFluid) && !tryFluidTransfer(h, mStorage, tankFluid.copyWithAmount(maxFill)).empty()) return true;
        }
        return false;
    }

    /// SB handleFluidContainerInHand (main hand, then offhand).
    bool handleHands(Player& player, game::ItemLocation const* backpackItem) {
        for (auto loc : {game::selectedSlot(player), game::ItemLocation{game::ItemLocation::Kind::Offhand, 0}}) {
            if (backpackItem && *backpackItem == loc) continue; // the backpack itself
            auto const& stack = game::itemAt(player, loc);
            if (stack.isNull() || stack.mCount != 1) continue;
            auto h = api::service().itemFluid(stack);
            if (!h) continue;
            bool ok = mS.input ? fillFromFluidHandler(*h, api::kBucketVolume) : fillFluidHandler(*h, api::kBucketVolume);
            if (ok) {
                game::setItemAt(player, loc, h->container());
                return true;
            }
        }
        return false;
    }

    /// SB handleFluidContainersInHandsOfNearbyPlayers (placed backpacks).
    bool handleNearbyPlayers(BlockSource& region, BlockPos const& pos) {
        bool done = false;
        int  dim  = static_cast<int>(region.getDimensionId());
        region.getLevel().forEachPlayer([&](Player& p) {
            if (done || p.isClientSide() || static_cast<int>(p.getDimensionId()) != dim) return true;
            auto f = p.getFeetPos();
            bool in = f.x >= pos.x - kPlayerSearchRange && f.x < pos.x + 1 + kPlayerSearchRange && f.y >= pos.y - kPlayerSearchRange
                   && f.y < pos.y + 1 + kPlayerSearchRange && f.z >= pos.z - kPlayerSearchRange && f.z < pos.z + 1 + kPlayerSearchRange;
            if (in && handleHands(p, nullptr)) done = true;
            return !done;
        });
        return done;
    }

    /// SB interactWithWorld.
    std::optional<int> interactWithWorld(BlockSource& region, BlockPos const& pos, Player* player) {
        if (mS.input) return fillFromBlockInRange(region, pos, player);
        for (int d = 0; d < 6; ++d) {
            if (placeFluidInWorld(region, d, offset(pos, d), player)) return kWorldInteractionCooldown;
        }
        return std::nullopt;
    }

    /// SB interactWithAttachedFluidHandlers.
    std::optional<int> interactWithHandlers(BlockSource& region, BlockPos const& pos) {
        int max = maxInOut(mCtx.backpack());
        for (int d = 0; d < 6; ++d) {
            auto at = offset(pos, d);
            // Java asks block entities only, so cauldrons (no block entity there) are left alone;
            // Bedrock blocks with a fluid capability (placed backpacks, mod tanks) count.
            auto const& name = region.getBlock(at).getTypeName();
            if (name.find("cauldron") != std::string::npos) continue;
            auto h = api::service().blockFluid(region, at, kOpposite[d]);
            if (!h) continue;
            bool ok = mS.input ? fillFromFluidHandler(*h, max) : fillFluidHandler(*h, max);
            if (ok) return kFluidHandlerCooldown;
        }
        return std::nullopt;
    }

private:
    std::optional<int> fillFromBlockInRange(BlockSource& region, BlockPos const& base, Player* player) {
        std::deque<BlockPos>                       next{base};
        std::set<std::tuple<int, int, int>>        searched;
        while (!next.empty()) {
            auto pos = next.front();
            next.pop_front();
            if (fillFromBlock(region, pos, player)) {
                double dist = std::max(1.0, std::sqrt(static_cast<double>(distSqr(base, pos))));
                return static_cast<int>(dist * kWorldInteractionCooldown);
            }
            for (int d = 0; d < 6; ++d) {
                auto o = offset(pos, d);
                if (!searched.insert({o.x, o.y, o.z}).second) continue;
                if (distSqr(base, o) < kPumpInWorldRangeSqr) next.push_back(o);
            }
        }
        return std::nullopt;
    }

    bool fillFromBlock(BlockSource& region, BlockPos const& pos, Player* player) {
        if (pos.y < region.getMinHeight() || pos.y >= region.getMaxHeight()) return false;
        if (!isFluidSource(region.getBlock(pos))) return false;
        if (!mayInteract(player, pos, false)) return false;
        SourceBlockHandler h(region, pos);
        return fillFromFluidHandler(h, api::kBucketVolume);
    }

    bool placeFluidInWorld(BlockSource& region, int dir, BlockPos const& at, Player* player) {
        if (dir == 1) return false; // never above
        if (at.y < region.getMinHeight() || at.y >= region.getMaxHeight()) return false;
        for (int t = 0; t < mStorage.tanks(); ++t) {
            auto tankFluid = mStorage.fluidInTank(t);
            if (tankFluid.empty() || !mS.matches(tankFluid) || !mayInteract(player, at, true) || !validForPlacement(region, at)) continue;
            auto const* info = api::fluidRegistry().fluid(tankFluid.id);
            if (!info || info->sourceBlock.empty()) continue;
            // FluidUtil.tryPlaceFluid: water vaporizes in ultra-warm dimensions (the bucket's worth is used up).
            if (tankFluid.id == api::fluids::Water && region.getDimension().mUltraWarm) {
                if (!mStorage.drain(tankFluid, false).empty()) return true;
                continue;
            }
            PlaceBlockHandler place(region, at);
            if (!tryFluidTransfer(place, mStorage, tankFluid).empty()) return true;
        }
        return false;
    }

    UpgradeContext      mCtx;
    Settings            mS;
    api::IFluidHandler& mStorage;
};
} // namespace

api::FluidStack tryFluidTransfer(api::IFluidHandler& dest, api::IFluidHandler& source, api::FluidStack const& resource) {
    auto drainable = source.drain(resource, true);
    if (drainable.empty() || !drainable.isSameFluid(resource)) return {};
    int fillable = dest.fill(drainable, true);
    if (fillable <= 0) return {};
    auto drained = source.drain(drainable.copyWithAmount(fillable), false);
    if (drained.empty()) return {};
    return drained.copyWithAmount(dest.fill(drained, false));
}

// ---- settings -----------------------------------------------------------------------------------------

int filterSlots() { return std::clamp(cfg().filterSlots, 1, 20); }

Settings Settings::of(UpgradeContext const& ctx) {
    Settings s;
    s.advanced = ctx.state().item.id == "alleviatedbackpacks:advanced_pump_upgrade";
    s.hand     = s.advanced; // SB item defaults: basic (hand off), advanced (hand on)
    s.world    = false;
    s.handlers = true;
    auto const& j = ctx.settings();
    if (j.is_object()) {
        if (j.contains("input")) s.input = j.value("input", true);
        else if (j.contains("output")) s.input = !j.value("output", false); // phase-1 key
        s.hand     = j.value("hand", s.hand);
        s.world    = j.value("world", s.world);
        s.handlers = j.value("handlers", s.handlers);
        if (auto it = j.find("filter"); it != j.end() && it->is_array()) {
            for (auto const& f : *it) s.filter.push_back(f.is_string() ? f.get<std::string>() : std::string{});
        }
    }
    s.filter.resize(static_cast<std::size_t>(filterSlots()));
    return s;
}

void Settings::save(UpgradeContext const& ctx) const {
    auto& j = ctx.settings();
    if (!j.is_object()) j = nlohmann::json::object();
    j.erase("output");
    j["input"]    = input;
    j["hand"]     = hand;
    j["world"]    = world;
    j["handlers"] = handlers;
    j["filter"]   = filter;
    ctx.saveSettings();
}

bool Settings::matches(api::FluidStack const& fluid) const {
    bool any = false;
    auto id  = core::legacyIdOf(fluid);
    for (auto const& f : filter) {
        if (f.empty()) continue;
        any = true;
        if (f == id) return true;
    }
    return !any;
}

int pumpTick(UpgradeContext const& ctx, Player* carrier, game::ItemLocation const* backpackItem, BlockSource& region, BlockPos const& pos) {
    auto storage = api::backpack::fluidHandler(ctx.backpackPtr());
    if (!storage) return kDidNothingCooldown;
    auto  s     = Settings::of(ctx);
    auto& state = ctx.transient<PumpState>();
    Logic logic(ctx, s, *storage);
    auto  now   = fasttick::now();
    if (s.hand) {
        bool done = carrier ? logic.handleHands(*carrier, backpackItem) : logic.handleNearbyPlayers(region, pos);
        if (done) {
            state.lastHandAction = now;
            return kHandInteractionCooldown;
        }
    }
    if (s.world) {
        if (auto cd = logic.interactWithWorld(region, pos, carrier)) return *cd;
    }
    if (s.handlers) {
        if (auto cd = logic.interactWithHandlers(region, pos)) return *cd;
    }
    return state.lastHandAction + 10 * kHandInteractionCooldown > now ? kHandInteractionCooldown : kDidNothingCooldown;
}

namespace {

void runPump(UpgradeContext const& ctx, Player* carrier, game::ItemLocation const* where, BlockSource& region, BlockPos const& pos) {
    if (!isPump(ctx)) return;
    auto& state = ctx.transient<PumpState>();
    auto  now   = fasttick::now();
    if (now < state.until) return;
    state.until = now + pumpTick(ctx, carrier, where, region, pos);
}

class PumpUpgradeBase : public IUpgrade {
public:
    void onTick(TickContext const& tick) override {
        fasttick::keepAlive(tick, "pump", [](Player& player, UpgradeContext const& ctx, game::ItemLocation where, int depth) {
            auto feet = player.getFeetBlockPos();
            runPump(ctx, &player, depth == 0 ? &where : nullptr, player.getDimensionBlockSource(), feet);
        });
    }
    void onPlacedTick(PlacedTickContext const& t) override { runPump(t.ctx, nullptr, nullptr, t.region, BlockPos{t.x, t.y, t.z}); }
};

class PumpUpgrade : public PumpUpgradeBase {
public:
    [[nodiscard]] UpgradeInfo const& info() const override {
        static UpgradeInfo const kInfo{
            .id             = std::string(kBasicId),
            .itemId         = "alleviatedbackpacks:pump_upgrade",
            .nameKey        = "abp.pump.name",
            .nameFallback   = "Pump Upgrade",
            .group          = "pump",
            .maxPerBackpack = 1000, // SB: no limit
            .hasSettings    = true,
        };
        return kInfo;
    }
};

class AdvancedPumpUpgrade : public PumpUpgradeBase {
public:
    [[nodiscard]] UpgradeInfo const& info() const override {
        static UpgradeInfo const kInfo{
            .id             = std::string(kAdvancedId),
            .itemId         = "alleviatedbackpacks:advanced_pump_upgrade",
            .nameKey        = "abp.pump.advanced.name",
            .nameFallback   = "Advanced Pump Upgrade",
            .tier           = UpgradeTier::Advanced,
            .group          = "pump",
            .maxPerBackpack = 1000,
            .hasSettings    = true,
        };
        return kInfo;
    }
};

// ---- native tab ---------------------------------------------------------------------------------------

nlohmann::json pumpTabData(Player& player, UpgradeContext const& ctx) {
    auto           s      = Settings::of(ctx);
    nlohmann::json filter = nlohmann::json::array();
    for (auto const& f : s.filter) {
        if (f.empty()) {
            filter.push_back(nullptr);
            continue;
        }
        auto        api  = core::toApi({f, 1});
        auto const* info = api::fluidRegistry().fluid(api.id);
        filter.push_back({{"id", api.id}, {"nbt", api.nbt}, {"name", tank::fluidName(&player, f)}, {"color", info ? info->color : 0xFFFFFFFFu}});
    }
    return nlohmann::json{
        {"pump",
         {{"input", s.input}, {"hand", s.hand}, {"world", s.world}, {"handlers", s.handlers}, {"advanced", s.advanced}, {"filter", std::move(filter)}}},
        {"tabLabel", s.advanced ? trFor(player, "abp.pump.advanced.tab", "Adv. Pump") : trFor(player, "abp.pump.tab", "Pump")},
        {"tabTooltip", s.advanced ? trFor(player, "abp.pump.advanced.tab.tooltip", "Advanced Pump") : trFor(player, "abp.pump.tab.tooltip", "Pump")},
    };
}

bool pumpTabAction(Player& player, UpgradeContext const& ctx, nlohmann::json const& data) {
    auto op = data.value("op", std::string{});
    auto s  = Settings::of(ctx);
    if (op == "toggle") {
        auto what = data.value("what", std::string{});
        if (what == "input") s.input = !s.input;
        else if (what == "hand" && s.advanced) s.hand = !s.hand;
        else if (what == "world" && s.advanced) s.world = !s.world;
        else if (what == "handlers" && s.advanced) s.handlers = !s.handlers;
        else return false;
        s.save(ctx);
        return true;
    }
    if (op == "filter" && s.advanced) {
        // SB FluidFilterContainer.slotClick: an empty cursor clears, a fluid container sets.
        int i = data.value("index", -1);
        if (i < 0 || i >= static_cast<int>(s.filter.size())) return false;
        auto cursor = fluidui::cursorOf(player);
        if (cursor.isNull()) {
            s.filter[static_cast<std::size_t>(i)].clear();
        } else {
            auto h = api::service().itemFluid(cursor);
            if (!h) return true;
            auto contained = h->drainAny(api::kBucketVolume, true);
            if (contained.empty()) return true;
            s.filter[static_cast<std::size_t>(i)] = core::legacyIdOf(contained);
        }
        s.save(ctx);
        return true;
    }
    return false;
}

ABP_REGISTER_TAB_DATA(net::TabDataProvider{"pump", pumpTabData});
ABP_REGISTER_TAB_ACTION(net::TabActionHandler{"pump", pumpTabAction});

} // namespace
} // namespace abp::upgrade::pump

ABP_REGISTER_CONFIG("pump", abp::upgrade::pump::PumpConfig);
ABP_REGISTER_UPGRADE(abp::upgrade::pump::PumpUpgrade);
ABP_REGISTER_UPGRADE(abp::upgrade::pump::AdvancedPumpUpgrade);
