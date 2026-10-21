// Pump and Advanced Pump upgrades (SB "Pump Upgrade" / "Advanced Pump Upgrade").
//
// Settings JSON: {"output": bool, "hand": bool, "world": bool, "filter": ["water", "", ...]}
// Runtime: a cooldown per pump (transient) so a pump moves at most one bucket per cycle.

#include "upgrade/pump/Pump.h"

#include <algorithm>
#include <chrono>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDestroyBlockEvent.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/player/PlayerPlaceBlockEvent.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/world/actor/player/AbilitiesIndex.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/CauldronBlockActor.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/CauldronLiquidType.h"
#include "mc/world/level/block/VanillaStates.h"
#include "mc/world/level/block/states/BlockStateVariant.h"
#include "mc/world/level/dimension/Dimension.h"

#include "core/Fluid.h"
#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "selftest/SelfTest.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/tank/MenuBits.h"
#include "upgrade/tank/Tank.h"

namespace abp::upgrade::pump {

namespace fl = core::fluid;

/// `config/pump.json`
struct PumpConfig {
    int version = 1;
    /// Game ticks between two pump operations (one bucket each) of a basic pump.
    int basicIntervalTicks = 20;
    /// Same for the advanced pump.
    int advancedIntervalTicks = 10;
};

namespace {
constexpr std::string_view kBucketId = "minecraft:bucket";
/// Fluids a pump can take from / put into the world (as source blocks or full cauldrons).
constexpr std::string_view kWorldFluids[] = {fl::kWater, fl::kLava, fl::kPowderSnow};

bool isBucket(std::string_view id) { return id == kBucketId || id.ends_with("_bucket"); }

/// The default block state of `name`, or nullopt.
std::optional<Block const*> blockByName(std::string_view name) {
    auto b = Block::tryGetFromRegistry(HashedString(name));
    if (!b) return std::nullopt;
    return b.as_ptr();
}

bool setBlock(BlockSource& bs, BlockPos const& pos, Block const& block) {
    return bs.setBlock(pos, block, 3, nullptr, BlockChangeContext{});
}

bool setBlock(BlockSource& bs, BlockPos const& pos, std::string_view name) {
    auto b = blockByName(name);
    return b && setBlock(bs, pos, **b);
}

bool isLiquid(Block const& block) {
    auto const& n = block.getTypeName();
    return n == "minecraft:water" || n == "minecraft:flowing_water" || n == "minecraft:lava" || n == "minecraft:flowing_lava";
}

/// The fluid id of a fluid source block (water / lava with liquid_depth 0), or "".
std::string_view sourceFluid(Block const& block) {
    auto const& name = block.getTypeName();
    std::string_view fluid;
    if (name == "minecraft:water" || name == "minecraft:flowing_water") fluid = fl::kWater;
    else if (name == "minecraft:lava" || name == "minecraft:flowing_lava") fluid = fl::kLava;
    else return {};
    auto depth = block.getState<int>(VanillaStates::LiquidDepth());
    return depth && *depth == 0 ? fluid : std::string_view{};
}

bool isCauldron(Block const& block) {
    auto const& name = block.getTypeName();
    return name == "minecraft:cauldron" || name == "minecraft:lava_cauldron";
}

/// Fluid of a full cauldron ("" when not full / empty).
std::string_view fullCauldronFluid(Block const& block) {
    auto level = block.getState<int>(VanillaStates::FillLevel());
    if (!level || *level < 6) return {};
    auto liquid = block.getState<int>(VanillaStates::CauldronLiquid());
    switch (liquid.value_or(0)) {
    case 1:
        return fl::kLava;
    case 2:
        return fl::kPowderSnow;
    default:
        return block.getTypeName() == "minecraft:lava_cauldron" ? fl::kLava : fl::kWater;
    }
}

/// Makes `pos` an empty cauldron (`fluid` empty) or a full one of water / lava / powder snow.
bool setCauldron(BlockSource& bs, BlockPos const& pos, std::string_view fluid) {
    auto base = blockByName(fluid == fl::kLava ? "minecraft:lava_cauldron" : "minecraft:cauldron");
    if (!base && fluid == fl::kLava) base = blockByName("minecraft:cauldron");
    if (!base) return false;
    auto liquid = fluid == fl::kLava ? CauldronLiquidType::Lava
                : fluid == fl::kPowderSnow ? CauldronLiquidType::PowderSnow
                                           : CauldronLiquidType::Water;
    auto withLiquid = (*base)->setState<int>(VanillaStates::CauldronLiquid(), static_cast<int>(liquid));
    if (!withLiquid) return false;
    auto full = withLiquid->setState<int>(VanillaStates::FillLevel(), fluid.empty() ? 0 : 6);
    if (!full) return false;
    return setBlock(bs, pos, *full);
}

/// Potion or dyed-water cauldrons keep their contents in the block actor: the pump leaves them alone.
bool cauldronHasExtras(BlockSource& bs, BlockPos const& pos) {
    auto* be = bs.getBlockEntity(pos);
    if (!be || be->getType() != BlockActorType::Cauldron) return false;
    auto const& cauldron = *static_cast<CauldronBlockActor const*>(be);
    return cauldron.mHasCustomColor || cauldron.mPotionId > 0;
}

/// Whether `player` may change the block at `pos` like a bucket would: needs the build and mine
/// abilities (no adventure / spectator) and the LeviLamina place / destroy events must not be
/// cancelled (protection mods hook those).
bool mayEdit(Player& player, BlockPos const& pos, bool place, int face = 1) {
    if (!player.canUseAbility(AbilitiesIndex::Build) || !player.canUseAbility(AbilitiesIndex::Mine)) return false;
    auto& bus = ll::event::EventBus::getInstance();
    if (place) {
        auto f  = static_cast<uchar>(face);
        auto ev = ll::event::PlayerPlacingBlockEvent(player, pos, f);
        bus.publish(ev);
        return !ev.isCancelled();
    }
    auto ev = ll::event::PlayerDestroyBlockEvent(player, pos);
    bus.publish(ev);
    return !ev.isCancelled();
}

/// Blocks with their own use action (buttons, doors, ...): sneak + use keeps its vanilla meaning.
bool hasOwnUseAction(Block const& block) {
    static constexpr std::string_view kWords[] =
        {"button", "lever", "door", "fence_gate", "bell", "repeater", "comparator", "noteblock", "daylight_detector"};
    auto const& name = block.getTypeName();
    return std::any_of(std::begin(kWords), std::end(kWords), [&](std::string_view w) {
        return name.find(w) != std::string::npos;
    });
}

bool ultraWarm(BlockSource& bs) { return bs.getDimension().mUltraWarm; }

/// The first world-placeable fluid in the tanks with at least a bucket, allowed by `settings`.
std::string firstPlaceable(std::shared_ptr<game::Backpack> const& bp, Settings const& settings, bool allowWater) {
    for (auto const& f : tank::fluids(bp)) {
        if (std::find(std::begin(kWorldFluids), std::end(kWorldFluids), f) == std::end(kWorldFluids)) continue;
        if (!settings.allows(f) || (!allowWater && f == fl::kWater)) continue;
        if (tank::amountOf(bp, f) >= fl::kBucket) return f;
    }
    return {};
}

int intervalFor(Settings const& s) {
    auto const& cfg = config::get<PumpConfig>();
    return std::max(1, s.advanced ? cfg.advancedIntervalTicks : cfg.basicIntervalTicks);
}

struct Cooldown {
    int ticks = 0;
};
} // namespace

// ---- settings -----------------------------------------------------------------------------------

Settings Settings::of(UpgradeContext const& ctx) {
    Settings    s;
    auto const& j = ctx.settings();
    s.advanced    = ctx.state().item.id == "alleviatedbackpacks:advanced_pump_upgrade";
    if (j.is_object()) {
        s.output = j.value("output", false);
        s.hand   = j.value("hand", true);
        s.world  = j.value("world", true);
        if (auto it = j.find("filter"); it != j.end() && it->is_array()) {
            for (auto const& f : *it) s.filter.push_back(f.is_string() ? f.get<std::string>() : std::string{});
        }
    }
    s.filter.resize(kFilterSlots);
    if (!s.advanced) s.world = false;
    return s;
}

void Settings::save(UpgradeContext const& ctx) const {
    auto& j = ctx.settings();
    if (!j.is_object()) j = nlohmann::json::object();
    j["output"] = output;
    j["hand"]   = hand;
    if (advanced) {
        j["world"]  = world;
        j["filter"] = filter;
    }
    ctx.saveSettings();
}

bool Settings::allows(std::string_view fluid) const {
    if (!advanced) return true;
    bool any = false;
    for (auto const& f : filter) {
        if (f.empty()) continue;
        any = true;
        if (f == fluid) return true;
    }
    return !any;
}

std::optional<UpgradeContext> activePump(std::shared_ptr<game::Backpack> const& backpack) {
    std::optional<UpgradeContext> out;
    forEachInstalled(backpack, true, [&](IUpgrade& u, UpgradeContext const& ctx) {
        if (!out && (u.info().id == kBasicId || u.info().id == kAdvancedId)) out = ctx;
    });
    return out;
}

// ---- operations ---------------------------------------------------------------------------------

bool interactHand(Player& player, UpgradeContext const& pump, Settings const& settings) {
    auto bp = pump.backpackPtr();
    for (auto loc : {game::selectedSlot(player), game::ItemLocation{game::ItemLocation::Kind::Offhand, 0}}) {
        auto const& stack = game::itemAt(player, loc);
        if (stack.isNull() || stack.mCount != 1) continue;
        auto d = game::ItemConv::toData(stack);
        if (!isBucket(d.id)) continue;
        if (!settings.output) {
            auto contents = fl::contentsOf(d.id, d.aux);
            if (!contents || !settings.allows(contents->fluid.id)) continue;
            if (auto empty = tank::emptyContainer(d, tank::fillerOf(bp))) {
                game::setItemAt(player, loc, game::ItemConv::toStack(*empty, 1));
                return true;
            }
        } else if (d.id == kBucketId) {
            for (auto const& f : tank::fluids(bp)) {
                if (!settings.allows(f)) continue;
                if (auto filled = tank::fillContainer(d, f, tank::drainerOf(bp))) {
                    game::setItemAt(player, loc, game::ItemConv::toStack(*filled, 1));
                    return true;
                }
            }
        }
    }
    return false;
}

bool pumpFromWorld(Player& player, BlockPos const& pos, UpgradeContext const& pump, Settings const& settings) {
    auto& bs    = player.getDimensionBlockSource();
    auto  fluid = sourceFluid(bs.getBlock(pos));
    if (fluid.empty() || !settings.allows(fluid)) return false;
    auto bp = pump.backpackPtr();
    if (tank::fill(bp, {std::string(fluid), fl::kBucket}, true, true) < fl::kBucket) return false;
    if (!mayEdit(player, pos, false) || !setBlock(bs, pos, "minecraft:air")) return false;
    tank::fill(bp, {std::string(fluid), fl::kBucket}, false, true);
    return true;
}

bool useOnBlock(Player& player, BlockPos const& pos, int face, UpgradeContext const& pump, Settings const& settings) {
    if (!settings.advanced || !settings.world) return false;
    auto&        bs    = player.getDimensionBlockSource();
    Block const& block = bs.getBlock(pos);
    auto         bp    = pump.backpackPtr();
    if (isCauldron(block)) {
        if (cauldronHasExtras(bs, pos)) return false;
        if (!settings.output) {
            auto fluid = fullCauldronFluid(block);
            if (fluid.empty() || !settings.allows(fluid)) return false;
            if (tank::fill(bp, {std::string(fluid), fl::kBucket}, true, true) < fl::kBucket) return false;
            if (!mayEdit(player, pos, false) || !setCauldron(bs, pos, {})) return false;
            tank::fill(bp, {std::string(fluid), fl::kBucket}, false, true);
            return true;
        }
        auto level = block.getState<int>(VanillaStates::FillLevel());
        if (level.value_or(0) != 0) return false;
        auto fluid = firstPlaceable(bp, settings, true);
        if (fluid.empty()) return false;
        if (tank::drain(bp, fluid, fl::kBucket, true, true).amount < fl::kBucket) return false;
        if (!mayEdit(player, pos, true, face) || !setCauldron(bs, pos, fluid)) return false;
        tank::drain(bp, fluid, fl::kBucket, false, true);
        return true;
    }
    static constexpr int kOffsets[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
    if (face < 0 || face > 5) return false;
    BlockPos target{pos.x + kOffsets[face][0], pos.y + kOffsets[face][1], pos.z + kOffsets[face][2]};
    if (!settings.output) return pumpFromWorld(player, target, pump, settings);
    Block const& there = bs.getBlock(target);
    // Air, or a flowing (non-source) liquid like a bucket would replace.
    bool free = there.isAir() || (isLiquid(there) && sourceFluid(there).empty());
    if (!free) return false;
    auto fluid = firstPlaceable(bp, settings, !ultraWarm(bs));
    if (fluid.empty()) return false;
    if (tank::drain(bp, fluid, fl::kBucket, true, true).amount < fl::kBucket) return false;
    if (!mayEdit(player, target, true, face) || !setBlock(bs, target, "minecraft:" + fluid)) return false;
    tank::drain(bp, fluid, fl::kBucket, false, true);
    return true;
}

bool pumpCycle(Player& player, UpgradeContext const& pump) {
    auto settings = Settings::of(pump);
    if (tank::tanks(pump.backpackPtr()).empty()) return false;
    if (settings.hand && interactHand(player, pump, settings)) return true;
    if (settings.advanced && settings.world && !settings.output) {
        auto feet = player.getFeetBlockPos();
        for (auto const& pos : {feet, BlockPos{feet.x, feet.y + 1, feet.z}}) {
            if (pumpFromWorld(player, pos, pump, settings)) return true;
        }
    }
    return false;
}

// ---- menu ---------------------------------------------------------------------------------------

namespace {

class PumpMenu : public ui::Menu {
public:
    PumpMenu(UpgradeContext ctx, std::string title) : Menu(std::move(title), Rows::Six), mCtx(std::move(ctx)) {}

    void build(Player& player) override {
        for (int i = 0; i < size(); ++i) setSlot(i, fluidui::filler());
        auto s = Settings::of(mCtx);
        setSlot(
            10,
            button(
                s.output ? "minecraft:dispenser" : "minecraft:hopper",
                s.output ? trFor(player, "abp.pump.mode.output", "Mode: empty the tanks")
                         : trFor(player, "abp.pump.mode.input", "Mode: fill the tanks"),
                {s.output ? trFor(player, "abp.pump.mode.output.help", "§7Fluid goes from the tanks into buckets / the world.")
                          : trFor(player, "abp.pump.mode.input.help", "§7Fluid goes from buckets / the world into the tanks.")},
                [](Settings& x) { x.output = !x.output; }
            )
        );
        setSlot(
            12,
            button(
                s.hand ? "minecraft:water_bucket" : "minecraft:bucket",
                trFor(player, "abp.pump.hand", "Interact with held bucket: {}", fluidui::onOff(player, s.hand)),
                {trFor(player, "abp.pump.hand.help", "§7Empties / fills a single bucket held in your hand.")},
                [](Settings& x) { x.hand = !x.hand; }
            )
        );
        if (s.advanced) {
            setSlot(
                14,
                button(
                    s.world ? "minecraft:grass_block" : "minecraft:dirt",
                    trFor(player, "abp.pump.world", "Interact with the world: {}", fluidui::onOff(player, s.world)),
                    {trFor(player, "abp.pump.world.help1", "§7Input: pumps fluid sources you stand in."),
                     trFor(player, "abp.pump.world.help2", "§7Sneak + use with an empty hand on a block:"),
                     trFor(player, "abp.pump.world.help3", "§7pump / place fluid, drain / fill cauldrons.")},
                    [](Settings& x) { x.world = !x.world; }
                )
            );
            setSlot(
                20,
                std::make_shared<ui::StaticSlot>(ui::icon(
                    "minecraft:paper",
                    trFor(player, "abp.pump.filter", "Fluid filter ▶"),
                    {trFor(player, "abp.pump.filter.help1", "§7Put a filled bucket or bottle to allow its fluid."),
                     trFor(player, "abp.pump.filter.help2", "§7Empty filter: every fluid.")}
                ))
            );
            for (int i = 0; i < kFilterSlots; ++i) {
                setSlot(
                    21 + i,
                    std::make_shared<ui::GhostSlot>(
                        [ctx = mCtx, i]() -> core::ItemStackData {
                            auto f = Settings::of(ctx).filter[static_cast<std::size_t>(i)];
                            if (f.empty()) return {};
                            auto item = fl::displayItem(f);
                            if (!item) return {};
                            auto d  = game::ItemConv::toData(game::ItemConv::make(item->id, 1, item->aux));
                            d.count = 1;
                            return d;
                        },
                        [ctx = mCtx, i](core::ItemStackData const& d) {
                            auto x = Settings::of(ctx);
                            if (d.empty()) {
                                x.filter[static_cast<std::size_t>(i)].clear();
                            } else {
                                auto contents = fl::contentsOf(d.id, d.aux);
                                if (!contents) return;
                                x.filter[static_cast<std::size_t>(i)] = contents->fluid.id;
                            }
                            x.save(ctx);
                        }
                    )
                );
            }
        }
        setSlot(4, fluidui::live([ctx = mCtx](Player& p) {
            std::vector<std::string> lore;
            for (auto const& t : tank::tanks(ctx.backpackPtr())) lore.push_back(tank::describe(&p, tank::read(t)));
            if (lore.empty()) lore.push_back(trFor(p, "abp.pump.no_tank", "§cInstall a Tank Upgrade to store fluids."));
            auto* type = upgradeFor(ctx.state().item);
            return ui::icon("minecraft:piston", type ? displayName(*type, &p) : std::string("Pump"), lore);
        }));
        setSlot(18, fluidui::backButton(player, mCtx.backpackPtr()));
    }

    void onOpen(Player& player) override { fluidui::lockBackpackItem(*this, player, mCtx.backpack()); }

    void onTick(Player& player) override {
        auto* type = upgradeFor(mCtx.state().item);
        if (!type || (type->info().id != kBasicId && type->info().id != kAdvancedId)) ui::MenuManager::get().close(player);
    }

private:
    std::shared_ptr<ui::Slot>
    button(std::string_view id, std::string const& name, std::vector<std::string> lore, std::function<void(Settings&)> change) {
        return std::make_shared<ui::ButtonSlot>(ui::icon(id, name, lore), [this, change = std::move(change)](ui::ClickContext const&) {
            auto s = Settings::of(mCtx);
            change(s);
            s.save(mCtx);
            invalidate();
        });
    }

    UpgradeContext mCtx;
};

// ---- upgrades -----------------------------------------------------------------------------------

class PumpUpgradeBase : public IUpgrade {
public:
    void onTick(TickContext const& tick) override {
        auto& cd = tick.ctx.transient<Cooldown>();
        cd.ticks += tick.interval;
        auto settings = Settings::of(tick.ctx);
        if (cd.ticks < intervalFor(settings)) return;
        cd.ticks = 0;
        pumpCycle(tick.player, tick.ctx.withPlayer(&tick.player));
    }

    void openSettings(Player& player, UpgradeContext const& ctx) override {
        fluidui::openFresh(player, createSettingsMenu(player, ctx));
    }

    std::shared_ptr<ui::Menu> createSettingsMenu(Player& player, UpgradeContext const& ctx) override {
        return std::make_shared<PumpMenu>(ctx, displayName(*this, &player));
    }

    [[nodiscard]] std::vector<std::string> describe(Player& player, UpgradeContext const& ctx) const override {
        auto                     s = Settings::of(ctx);
        std::vector<std::string> lore{
            s.output ? trFor(player, "abp.pump.mode.output", "Mode: empty the tanks")
                     : trFor(player, "abp.pump.mode.input", "Mode: fill the tanks"),
            trFor(player, "abp.pump.hand", "Interact with held bucket: {}", fluidui::onOff(player, s.hand)),
        };
        if (s.advanced) {
            lore.push_back(trFor(player, "abp.pump.world", "Interact with the world: {}", fluidui::onOff(player, s.world)));
            std::string list;
            for (auto const& f : s.filter) {
                if (f.empty()) continue;
                if (!list.empty()) list += ", ";
                list += tank::fluidName(&player, f);
            }
            if (!list.empty()) lore.push_back(trFor(player, "abp.pump.filter.list", "§7Filter: §f{}", list));
        }
        if (tank::tanks(ctx.backpackPtr()).empty()) lore.push_back(trFor(player, "abp.pump.no_tank", "§cInstall a Tank Upgrade to store fluids."));
        return lore;
    }
};

class PumpUpgrade : public PumpUpgradeBase {
public:
    [[nodiscard]] UpgradeInfo const& info() const override {
        static UpgradeInfo const kInfo{
            .id           = std::string(kBasicId),
            .itemId       = "alleviatedbackpacks:pump_upgrade",
            .nameKey      = "abp.pump.name",
            .nameFallback = "Pump Upgrade",
            .group        = "pump",
            .hasSettings  = true,
        };
        return kInfo;
    }
};

class AdvancedPumpUpgrade : public PumpUpgradeBase {
public:
    [[nodiscard]] UpgradeInfo const& info() const override {
        static UpgradeInfo const kInfo{
            .id           = std::string(kAdvancedId),
            .itemId       = "alleviatedbackpacks:advanced_pump_upgrade",
            .nameKey      = "abp.pump.advanced.name",
            .nameFallback = "Advanced Pump Upgrade",
            .tier         = UpgradeTier::Advanced,
            .group        = "pump",
            .hasSettings  = true,
        };
        return kInfo;
    }
};

} // namespace

// ---- sneak + use on blocks ----------------------------------------------------------------------

/// Sneak + use with an empty main hand on a block: the carried advanced pump (world interaction
/// on) pumps / places fluid or drains / fills a cauldron.
class PumpInteractModule {
public:
    void install() {
        mListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::PlayerInteractBlockEvent>(
            [this](ll::event::PlayerInteractBlockEvent& ev) {
                auto& player = ev.self();
                if (player.isClientSide() || !player.isSneaking() || !ev.item().isNull()) return;
                if (auto block = ev.block(); block && hasOwnUseAction(*block)) return;
                auto now = std::chrono::steady_clock::now();
                auto id  = player.getOrCreateUniqueID().rawID;
                for (auto& [carried, bp] : game::carriedBackpacks(player)) {
                    auto pump = activePump(bp);
                    if (!pump) continue;
                    auto settings = Settings::of(*pump);
                    if (!settings.advanced || !settings.world) continue;
                    // The client repeats the use action while the button is held: one action per 250 ms.
                    if (auto it = mLast.find(id); it != mLast.end() && now - it->second < kRepeatDelay) {
                        ev.cancel();
                        return;
                    }
                    if (useOnBlock(player, ev.blockPos(), static_cast<int>(ev.face()), pump->withPlayer(&player), settings)) {
                        std::erase_if(mLast, [&](auto const& e) { return now - e.second >= kRepeatDelay; });
                        mLast[id] = now;
                        ev.cancel();
                        return;
                    }
                }
            }
        );
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mListener);
        mLast.clear();
    }

private:
    static constexpr auto kRepeatDelay = std::chrono::milliseconds(250);

    ll::event::ListenerPtr                                                  mListener;
    std::unordered_map<std::int64_t, std::chrono::steady_clock::time_point> mLast;
};

// ---- selftests ----------------------------------------------------------------------------------

namespace {
using selftest::Context;

ItemStack const& held(Player& p) { return game::itemAt(p, game::selectedSlot(p)); }

void setHeld(Player& p, std::string_view id) {
    game::setItemAt(p, game::selectedSlot(p), id.empty() ? ItemStack{} : game::ItemConv::make(id));
}

ABP_REGISTER_SELFTEST("pump.hand", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(core::Tier::Iron);
    ctx.installUpgrade(bp, "tank");
    UpgradeContext pump(bp, ctx.installUpgrade(bp, "pump"));
    ctx.require(ctx.give(player, ctx.backpackItem(*bp), 8) == 8, "give backpack");
    setHeld(player, "minecraft:water_bucket");
    co_await ctx.ticks(50);
    ctx.check(game::ItemConv::idOf(held(player)) == "minecraft:bucket", "held water bucket emptied by the pump");
    ctx.check(tank::amountOf(bp, "water") == 1000, "1000 mB water in the tank");

    auto s   = Settings::of(pump);
    s.output = true;
    s.save(pump);
    co_await ctx.ticks(50);
    ctx.check(game::ItemConv::idOf(held(player)) == "minecraft:water_bucket", "held bucket filled in output mode");
    ctx.check(tank::amountOf(bp, "water") == 0, "tank empty");

    s.hand   = false;
    s.output = false;
    s.save(pump);
    co_await ctx.ticks(50);
    ctx.check(game::ItemConv::idOf(held(player)) == "minecraft:water_bucket", "hand interaction off: bucket untouched");
    setHeld(player, "");
});

ABP_REGISTER_SELFTEST("pump.filter", [](Context& ctx) {
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(core::Tier::Gold);
    ctx.installUpgrade(bp, "tank");
    UpgradeContext pump(bp, ctx.installUpgrade(bp, "advanced_pump"));
    auto           s = Settings::of(pump);
    ctx.require(s.advanced && s.world && s.hand, "advanced pump defaults");
    s.filter[1] = "lava";
    s.save(pump);
    s = Settings::of(pump);
    ctx.check(s.allows("lava") && !s.allows("water"), "filter allows only lava");
    setHeld(player, "minecraft:water_bucket");
    ctx.check(!interactHand(player, pump, s), "water refused by the filter");
    ctx.check(game::ItemConv::idOf(held(player)) == "minecraft:water_bucket", "water bucket kept");
    setHeld(player, "minecraft:lava_bucket");
    ctx.check(interactHand(player, pump, s), "lava pumped");
    ctx.check(tank::amountOf(bp, "lava") == 1000, "1000 mB lava");
    setHeld(player, "");
    // A basic pump has no filter.
    Settings basic;
    basic.filter = {"lava", "", "", ""};
    ctx.check(basic.allows("water"), "basic pumps ignore filters");
});

/// The first free (air) block above the ground in the player's column, or nullopt (the simulated
/// player spawns high above the world spawn, so its own position is no use).
std::optional<BlockPos> groundAbove(Player& player) {
    auto& bs   = player.getDimensionBlockSource();
    auto  feet = player.getFeetBlockPos();
    for (int y = 250; y > bs.getMinHeight(); --y) {
        if (!bs.getBlock(BlockPos{feet.x, y, feet.z}).isAir()) return BlockPos{feet.x, y + 1, feet.z};
    }
    return std::nullopt;
}

ABP_REGISTER_SELFTEST("pump.world", [](Context& ctx) {
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(core::Tier::Gold);
    ctx.installUpgrade(bp, "tank");
    UpgradeContext pump(bp, ctx.installUpgrade(bp, "advanced_pump"));
    auto&          bs   = player.getDimensionBlockSource();
    auto           base = groundAbove(player);
    ctx.require(base.has_value(), "found the ground");
    BlockPos spot{base->x + 2, base->y, base->z};
    BlockPos ground{base->x + 2, base->y - 1, base->z};
    BlockPos cauldron{base->x + 3, base->y, base->z + 1};
    auto     cleanup = [&] {
        setBlock(bs, spot, "minecraft:air");
        setBlock(bs, cauldron, "minecraft:air");
    };
    cleanup();
    ctx.require(setBlock(bs, spot, "minecraft:water"), "place a water source");
    ctx.require(sourceFluid(bs.getBlock(spot)) == fl::kWater, "it's a source");
    auto s = Settings::of(pump);
    ctx.check(pumpFromWorld(player, spot, pump, s), "pump the source");
    ctx.check(bs.getBlock(spot).isAir(), "source removed");
    ctx.check(tank::amountOf(bp, "water") == 1000, "1000 mB water");

    s.output = true;
    s.save(pump);
    ctx.check(useOnBlock(player, ground, static_cast<int>(FacingID::Up), pump, s), "place water on top of the ground");
    ctx.check(sourceFluid(bs.getBlock(spot)) == fl::kWater, "water source placed");
    ctx.check(tank::amountOf(bp, "water") == 0, "tank emptied");
    ctx.check(!useOnBlock(player, ground, static_cast<int>(FacingID::Up), pump, s), "nothing left to place");

    s.output = false;
    ctx.check(useOnBlock(player, ground, static_cast<int>(FacingID::Up), pump, s), "sneak-use pumps it back");
    ctx.check(bs.getBlock(spot).isAir() && tank::amountOf(bp, "water") == 1000, "pumped back");

    // Cauldrons.
    ctx.require(setCauldron(bs, cauldron, {}), "place an empty cauldron");
    s.output = true;
    ctx.check(useOnBlock(player, cauldron, static_cast<int>(FacingID::Up), pump, s), "fill the cauldron");
    ctx.check(fullCauldronFluid(bs.getBlock(cauldron)) == fl::kWater, "cauldron full of water");
    ctx.check(tank::amountOf(bp, "water") == 0, "tank emptied into the cauldron");
    ctx.check(!useOnBlock(player, cauldron, static_cast<int>(FacingID::Up), pump, s), "a full cauldron takes nothing");
    s.output = false;
    ctx.check(useOnBlock(player, cauldron, static_cast<int>(FacingID::Up), pump, s), "drain the cauldron");
    ctx.check(fullCauldronFluid(bs.getBlock(cauldron)).empty(), "cauldron empty");
    ctx.check(tank::amountOf(bp, "water") == 1000, "water back in the tank");

    // Lava goes into a lava cauldron.
    ctx.check(tank::drain(bp, "water", 1000).amount == 1000 && tank::fill(bp, {"lava", 1000}) == 1000, "swap to lava");
    s.output = true;
    ctx.check(useOnBlock(player, cauldron, static_cast<int>(FacingID::Up), pump, s), "fill the cauldron with lava");
    ctx.check(fullCauldronFluid(bs.getBlock(cauldron)) == fl::kLava, "lava cauldron (" + bs.getBlock(cauldron).getTypeName() + ")");
    s.output = false;
    ctx.check(useOnBlock(player, cauldron, static_cast<int>(FacingID::Up), pump, s), "drain the lava cauldron");
    ctx.check(tank::amountOf(bp, "lava") == 1000, "lava back in the tank");

    // World interaction off: nothing happens.
    s.world = false;
    ctx.check(!useOnBlock(player, cauldron, static_cast<int>(FacingID::Up), pump, s), "world interaction off");
    s.world = true;

    // Protection mods cancel LeviLamina's destroy / place events: the pump respects that.
    (void)tank::drain(bp, "lava", 1000);
    auto& bus  = ll::event::EventBus::getInstance();
    auto  deny = bus.emplaceListener<ll::event::PlayerDestroyBlockEvent>([](ll::event::PlayerDestroyBlockEvent& ev) { ev.cancel(); });
    ctx.require(setBlock(bs, spot, "minecraft:water"), "place a water source again");
    ctx.check(!pumpFromWorld(player, spot, pump, s), "a cancelled destroy event blocks pumping");
    ctx.check(sourceFluid(bs.getBlock(spot)) == fl::kWater && tank::amountOf(bp, "water") == 0, "source and tank unchanged");
    bus.removeListener(deny);
    ctx.check(pumpFromWorld(player, spot, pump, s), "allowed again without the listener");
    cleanup();
});

ABP_REGISTER_SELFTEST("pump.world_tick", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(core::Tier::Gold);
    ctx.installUpgrade(bp, "tank");
    ctx.installUpgrade(bp, "advanced_pump");
    ctx.require(ctx.give(player, ctx.backpackItem(*bp), 8) == 8, "give backpack");
    auto& bs   = player.getDimensionBlockSource();
    auto  base = groundAbove(player);
    ctx.require(base.has_value(), "found the ground");
    BlockPos spot{base->x + 5, base->y, base->z + 5};
    player.teleport(Vec3{static_cast<float>(spot.x) + 0.5f, static_cast<float>(spot.y), static_cast<float>(spot.z) + 0.5f}, player.getDimensionId());
    co_await ctx.ticks(5);
    auto feet = player.getFeetBlockPos();
    ctx.require(feet == spot, "player stands at the test spot");
    ctx.require(setBlock(bs, feet, "minecraft:water"), "water at the player's feet");
    co_await ctx.ticks(40);
    ctx.check(tank::amountOf(bp, "water") >= 1000, "the carried advanced pump pumped the source");
    ctx.check(sourceFluid(bs.getBlock(feet)).empty(), "no source left at the feet");
    setBlock(bs, feet, "minecraft:air");
});} // namespace

} // namespace abp::upgrade::pump

ABP_REGISTER_CONFIG("pump", abp::upgrade::pump::PumpConfig);
ABP_REGISTER_UPGRADE(abp::upgrade::pump::PumpUpgrade);
ABP_REGISTER_UPGRADE(abp::upgrade::pump::AdvancedPumpUpgrade);
ABP_REGISTER_MODULE(abp::upgrade::pump::PumpInteractModule);
