// XP Pump upgrade (SB "Experience Pump Upgrade"): stores the carrier's experience in the backpack.
//
// Experience is kept as the fluid "experience" (20 mB per point) in the backpack's tanks when a
// Tank upgrade is installed, otherwise (or when the tanks are full) in the upgrade itself
// (settings "xp", capped by `XpPumpConfig::internalCapacity`).
//
// Settings JSON: {"mode": "off"|"store"|"give"|"both", "keep": <level>, "mending": bool, "xp": <points>}

#include <algorithm>
#include <climits>

#include "mc/entity/components/AttributesComponent.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/attribute/BaseAttributeMap.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"

#include "core/Experience.h"
#include "core/Fluid.h"
#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "selftest/SelfTest.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/tank/MenuBits.h"
#include "upgrade/tank/Tank.h"

namespace abp::upgrade::xppump {

namespace fl = core::fluid;

inline constexpr std::string_view kId     = "xp_pump";
inline constexpr std::string_view kItemId = "alleviatedbackpacks:xp_pump_upgrade";

/// `config/xppump.json`
struct XpPumpConfig {
    int version = 1;
    /// Experience points the upgrade itself can hold when there's no tank (default: level 0 -> 100).
    int internalCapacity = 30970;
    /// Highest "level to keep" that can be set in the menu.
    int maxKeepLevel = 100;
};

enum class Mode { Off, Store, Give, Both };

namespace {

// ---- player experience --------------------------------------------------------------------------

int playerLevel(Player& p) { return static_cast<int>(p.getAttribute(Player::LEVEL()).mPtr->mCurrentValue); }

long long playerXp(Player& p) {
    int   level    = playerLevel(p);
    float progress = p.getAttribute(Player::EXPERIENCE()).mPtr->mCurrentValue;
    auto  points   = static_cast<long long>(progress * static_cast<float>(core::xp::pointsForNextLevel(level)) + 0.5f);
    return core::xp::toTotal(level, std::clamp(points, 0LL, core::xp::pointsForNextLevel(level) - 1));
}

void setPlayerXp(Player& p, long long total) {
    auto target = core::xp::fromTotal(total);
    int  cur    = playerLevel(p);
    if (target.level != cur) p.addLevels(target.level - cur);
    if (auto attrs = p.getEntityContext().tryGetComponent<AttributesComponent>()) {
        auto need = static_cast<float>(core::xp::pointsForNextLevel(target.level));
        (void)attrs->mAttributes->setCurrentValue(Player::EXPERIENCE(), static_cast<float>(target.points) / need);
    }
}

// ---- settings -----------------------------------------------------------------------------------

struct Settings {
    Mode      mode    = Mode::Store;
    int       keep    = 10;
    bool      mending = true;
    long long xp      = 0;

    static Settings of(UpgradeContext const& ctx) {
        Settings    s;
        auto const& j = ctx.settings();
        if (!j.is_object()) return s;
        auto m = j.value("mode", std::string("store"));
        s.mode = m == "off" ? Mode::Off : m == "give" ? Mode::Give : m == "both" ? Mode::Both : Mode::Store;
        s.keep = std::max(0, j.value("keep", 10));
        s.mending = j.value("mending", true);
        if (auto it = j.find("xp"); it != j.end() && it->is_number_integer()) s.xp = std::max(0LL, it->get<long long>());
        return s;
    }
    void save(UpgradeContext const& ctx) const {
        auto& j = ctx.settings();
        if (!j.is_object()) j = nlohmann::json::object();
        static constexpr char const* kModes[] = {"off", "store", "give", "both"};
        j["mode"]    = kModes[static_cast<int>(mode)];
        j["keep"]    = keep;
        j["mending"] = mending;
        j["xp"]      = xp;
        ctx.saveSettings();
    }
};

bool isXpPump(UpgradeContext const& ctx) {
    auto const& slots = ctx.backpack().data().upgrades;
    return ctx.slot() >= 0 && ctx.slot() < static_cast<int>(slots.size())
        && slots[static_cast<std::size_t>(ctx.slot())].item.id == kItemId;
}

// ---- storage ------------------------------------------------------------------------------------

constexpr long long kMaxChunk = INT_MAX / fl::kXpPerPoint;

long long tankXp(UpgradeContext const& ctx) { return tank::amountOf(ctx.backpackPtr(), fl::kExperience) / fl::kXpPerPoint; }

} // namespace

/// Experience points stored by this XP pump (tanks + internal).
long long stored(UpgradeContext const& ctx) { return tankXp(ctx) + Settings::of(ctx).xp; }

/// Stores up to `points` (tanks first). Returns how many were stored.
long long store(UpgradeContext const& ctx, long long points) {
    if (points <= 0 || !isXpPump(ctx)) return 0;
    long long done = 0;
    auto      bp   = ctx.backpackPtr();
    while (done < points) {
        auto chunk = static_cast<int>(std::min(points - done, kMaxChunk));
        int  fits  = tank::fill(bp, {std::string(fl::kExperience), chunk * fl::kXpPerPoint}, true) / fl::kXpPerPoint;
        if (fits <= 0) break;
        tank::fill(bp, {std::string(fl::kExperience), fits * fl::kXpPerPoint}, false, true);
        done += fits;
        if (fits < chunk) break;
    }
    if (done < points) {
        auto s     = Settings::of(ctx);
        auto room  = std::max(0LL, static_cast<long long>(config::get<XpPumpConfig>().internalCapacity) - s.xp);
        auto extra = std::min(points - done, room);
        if (extra > 0) {
            s.xp += extra;
            s.save(ctx);
            done += extra;
        }
    }
    return done;
}

/// Takes up to `points` (internal store first, then tanks). Returns how many were taken.
long long take(UpgradeContext const& ctx, long long points) {
    if (points <= 0 || !isXpPump(ctx)) return 0;
    long long done = 0;
    auto      s    = Settings::of(ctx);
    if (s.xp > 0) {
        auto n = std::min(points, s.xp);
        s.xp -= n;
        s.save(ctx);
        done += n;
    }
    auto bp = ctx.backpackPtr();
    while (done < points) {
        auto chunk = static_cast<int>(std::min(points - done, kMaxChunk));
        auto got   = tank::drain(bp, fl::kExperience, chunk * fl::kXpPerPoint).amount / fl::kXpPerPoint;
        if (got <= 0) break;
        done += got;
        if (got < chunk) break;
    }
    return done;
}

/// Moves `points` from the player into the backpack (as much as fits). Returns the amount moved.
long long fromPlayer(Player& player, UpgradeContext const& ctx, long long points) {
    auto total = playerXp(player);
    points     = std::min(points, total);
    auto moved = store(ctx, points);
    if (moved > 0) setPlayerXp(player, total - moved);
    return moved;
}

/// Moves `points` from the backpack to the player. Returns the amount moved.
long long toPlayer(Player& player, UpgradeContext const& ctx, long long points) {
    auto moved = take(ctx, points);
    if (moved > 0) setPlayerXp(player, playerXp(player) + moved);
    return moved;
}

/// Mending: repairs damaged Mending items the player holds or wears with stored experience.
/// Returns the points used.
long long mend(Player& player, UpgradeContext const& ctx) {
    using Loc = game::ItemLocation;
    long long used = 0;
    std::vector<Loc> places{game::selectedSlot(player), Loc{Loc::Kind::Offhand, 0}};
    for (int a = 0; a < 4; ++a) places.push_back(Loc{Loc::Kind::Armor, a});
    for (auto const& loc : places) {
        auto const& stack = game::itemAt(player, loc);
        if (stack.isNull() || !stack.isDamageableItem() || stack.getDamageValue() <= 0) continue;
        if (!EnchantUtils::hasEnchant(Enchant::Type::Mending, stack)) continue;
        int  damage = stack.getDamageValue();
        auto points = take(ctx, core::xp::pointsToRepair(damage));
        if (points <= 0) break;
        ItemStack copy = stack;
        copy.setDamageValue(static_cast<short>(std::max(0LL, damage - points * core::xp::kDurabilityPerPoint)));
        game::setItemAt(player, loc, copy);
        used += points;
    }
    return used;
}

/// The automatic part of a pump cycle: keep the player at the configured level.
void autoCycle(Player& player, UpgradeContext const& ctx) {
    auto s = Settings::of(ctx);
    if (s.mending) mend(player, ctx);
    if (s.mode == Mode::Off) return;
    auto diff = core::xp::keepLevel(playerXp(player), s.keep);
    if (diff > 0 && (s.mode == Mode::Store || s.mode == Mode::Both)) fromPlayer(player, ctx, diff);
    else if (diff < 0 && (s.mode == Mode::Give || s.mode == Mode::Both)) toPlayer(player, ctx, -diff);
}

namespace {

// ---- menu ---------------------------------------------------------------------------------------

std::string modeName(Player& p, Mode m) {
    switch (m) {
    case Mode::Off:
        return trFor(p, "abp.xppump.mode.off", "Automatic: off");
    case Mode::Store:
        return trFor(p, "abp.xppump.mode.store", "Automatic: store levels above the target");
    case Mode::Give:
        return trFor(p, "abp.xppump.mode.give", "Automatic: refill up to the target");
    case Mode::Both:
        return trFor(p, "abp.xppump.mode.both", "Automatic: keep the target level");
    }
    return {};
}

std::string storedLine(Player& p, long long points) {
    auto lp = core::xp::fromTotal(points);
    return trFor(p, "abp.xppump.stored", "§aStored: §f{} XP §7(level {})", points, lp.level);
}

class XpPumpMenu : public ui::Menu {
public:
    XpPumpMenu(UpgradeContext ctx, std::string title) : Menu(std::move(title), Rows::Three), mCtx(std::move(ctx)) {}

    void build(Player& player) override {
        for (int i = 0; i < size(); ++i) setSlot(i, fluidui::filler());
        auto s = Settings::of(mCtx);
        setSlot(4, fluidui::live([ctx = mCtx](Player& p) {
            return ui::icon(
                "minecraft:experience_bottle",
                trFor(p, "abp.xppump.name", "XP Pump Upgrade"),
                {storedLine(p, stored(ctx)),
                 trFor(p, "abp.xppump.you", "§7You: level {} ({} XP)", playerLevel(p), playerXp(p))}
            );
        }));
        constexpr long long kAll = LLONG_MAX / 4;
        auto storeBtn = [&](int slot, std::string_view id, int count, std::string name, int levels) {
            setSlot(slot, std::make_shared<ui::ButtonSlot>(ui::icon(id, name, {}, count), [ctx = mCtx, levels](ui::ClickContext const& c) {
                fromPlayer(c.player, ctx, core::xp::storeLevels(playerXp(c.player), levels));
            }));
        };
        auto takeBtn = [&](int slot, std::string_view id, int count, std::string name, long long levels) {
            setSlot(slot, std::make_shared<ui::ButtonSlot>(ui::icon(id, name, {}, count), [ctx = mCtx, levels](ui::ClickContext const& c) {
                if (levels >= kAll) toPlayer(c.player, ctx, stored(ctx));
                else toPlayer(c.player, ctx, core::xp::takeLevels(playerXp(c.player), static_cast<int>(levels)));
            }));
        };
        storeBtn(9, "minecraft:chest", 1, trFor(player, "abp.xppump.store_all", "Store all levels"), INT_MAX);
        storeBtn(10, "minecraft:hopper", 10, trFor(player, "abp.xppump.store_n", "Store {} levels", 10), 10);
        storeBtn(11, "minecraft:hopper", 1, trFor(player, "abp.xppump.store_one", "Store 1 level"), 1);
        takeBtn(15, "minecraft:experience_bottle", 1, trFor(player, "abp.xppump.take_one", "Take 1 level"), 1);
        takeBtn(16, "minecraft:experience_bottle", 10, trFor(player, "abp.xppump.take_n", "Take {} levels", 10), 10);
        takeBtn(17, "minecraft:ender_chest", 1, trFor(player, "abp.xppump.take_all", "Take everything"), kAll);

        auto keepHelp = trFor(player, "abp.xppump.keep.help", "§7Click: ±1, shift-click: ±10");
        auto adjust   = [this](int delta) {
            return [this, delta](ui::ClickContext const& c) {
                int  step = c.type == ui::ClickType::QuickMove ? delta * 10 : delta;
                auto x    = Settings::of(mCtx);
                x.keep    = std::clamp(x.keep + step, 0, std::max(0, config::get<XpPumpConfig>().maxKeepLevel));
                x.save(mCtx);
                invalidate();
            };
        };
        setSlot(12, std::make_shared<ui::ButtonSlot>(ui::icon("minecraft:red_dye", trFor(player, "abp.xppump.keep.minus", "Lower target level"), {keepHelp}), adjust(-1)));
        setSlot(
            13,
            std::make_shared<ui::StaticSlot>(ui::icon(
                "minecraft:enchanting_table",
                trFor(player, "abp.xppump.keep", "Target level: {}", s.keep),
                {trFor(player, "abp.xppump.keep.info", "§7Used by the automatic mode.")},
                std::clamp(s.keep, 1, 64)
            ))
        );
        setSlot(14, std::make_shared<ui::ButtonSlot>(ui::icon("minecraft:lime_dye", trFor(player, "abp.xppump.keep.plus", "Raise target level"), {keepHelp}), adjust(1)));

        setSlot(
            20,
            std::make_shared<ui::ButtonSlot>(
                ui::icon(
                    s.mode == Mode::Off ? "minecraft:gray_dye" : "minecraft:repeater",
                    modeName(player, s.mode),
                    {trFor(player, "abp.xppump.mode.help", "§7Click to cycle")}
                ),
                [this](ui::ClickContext const&) {
                    auto x = Settings::of(mCtx);
                    x.mode = static_cast<Mode>((static_cast<int>(x.mode) + 1) % 4);
                    x.save(mCtx);
                    invalidate();
                }
            )
        );
        setSlot(
            22,
            std::make_shared<ui::ButtonSlot>(
                ui::icon(
                    "minecraft:anvil",
                    trFor(player, "abp.xppump.mending", "Mending: {}", fluidui::onOff(player, s.mending)),
                    {trFor(player, "abp.xppump.mending.help", "§7Repairs held / worn Mending items with stored XP.")}
                ),
                [this](ui::ClickContext const&) {
                    auto x    = Settings::of(mCtx);
                    x.mending = !x.mending;
                    x.save(mCtx);
                    invalidate();
                }
            )
        );
        setSlot(18, fluidui::backButton(player, mCtx.backpackPtr()));
    }

    void onOpen(Player& player) override { fluidui::lockBackpackItem(*this, player, mCtx.backpack()); }

    void onTick(Player& player) override {
        if (!isXpPump(mCtx)) ui::MenuManager::get().close(player);
    }

private:
    UpgradeContext mCtx;
};

// ---- upgrade ------------------------------------------------------------------------------------

class XpPumpUpgrade : public IUpgrade {
public:
    [[nodiscard]] UpgradeInfo const& info() const override {
        static UpgradeInfo const kInfo{
            .id           = std::string(kId),
            .itemId       = std::string(kItemId),
            .nameKey      = "abp.xppump.name",
            .nameFallback = "XP Pump Upgrade",
            .tier         = UpgradeTier::Advanced,
            .hasSettings  = true,
        };
        return kInfo;
    }

    void onTick(TickContext const& tick) override { autoCycle(tick.player, tick.ctx.withPlayer(&tick.player)); }

    std::shared_ptr<ui::Menu> createSettingsMenu(Player& player, UpgradeContext const& ctx) override {
        return std::make_shared<XpPumpMenu>(ctx, displayName(*this, &player));
    }

    [[nodiscard]] std::vector<std::string> describe(Player& player, UpgradeContext const& ctx) const override {
        auto s = Settings::of(ctx);
        return {
            storedLine(player, stored(ctx)),
            modeName(player, s.mode),
            trFor(player, "abp.xppump.keep", "Target level: {}", s.keep),
            trFor(player, "abp.xppump.mending", "Mending: {}", fluidui::onOff(player, s.mending)),
        };
    }
};

// ---- selftests ----------------------------------------------------------------------------------

using selftest::Context;
using Where  = ui::MenuManager::SlotAddress::Where;
using Action = ui::MenuManager::ActionType;

UpgradeContext setupPump(Context& ctx, std::shared_ptr<game::Backpack> const& bp, Mode mode, int keep = 10) {
    UpgradeContext pump(bp, ctx.installUpgrade(bp, std::string(kId)));
    auto           s = Settings::of(pump);
    s.mode           = mode;
    s.keep           = keep;
    s.save(pump);
    return pump;
}

ABP_REGISTER_SELFTEST("xppump.player_xp", [](Context& ctx) {
    auto& player = ctx.player();
    setPlayerXp(player, 0);
    ctx.check(playerXp(player) == 0 && playerLevel(player) == 0, "starts at 0");
    setPlayerXp(player, core::xp::toTotal(17, 5));
    ctx.check(playerLevel(player) == 17, "level 17, got " + std::to_string(playerLevel(player)));
    ctx.check(playerXp(player) == core::xp::toTotal(17, 5), "total round trip, got " + std::to_string(playerXp(player)));
    setPlayerXp(player, 3);
    ctx.check(playerLevel(player) == 0 && playerXp(player) == 3, "back down to 3 points");
    setPlayerXp(player, 0);
});

ABP_REGISTER_SELFTEST("xppump.store_take", [](Context& ctx) {
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(core::Tier::Leather);
    auto  pump   = setupPump(ctx, bp, Mode::Off);
    setPlayerXp(player, core::xp::totalForLevel(20));
    auto moved = fromPlayer(player, pump, core::xp::storeLevels(playerXp(player), 10));
    ctx.check(moved == core::xp::totalForLevel(20) - core::xp::totalForLevel(10), "stored 10 levels worth of XP");
    ctx.check(playerLevel(player) == 10, "player at level 10");
    ctx.check(stored(pump) == moved && Settings::of(pump).xp == moved, "kept in the upgrade (no tank)");
    toPlayer(player, pump, stored(pump));
    ctx.check(playerLevel(player) == 20 && stored(pump) == 0, "everything taken back");
    // The internal store is capped.
    setPlayerXp(player, core::xp::totalForLevel(200));
    fromPlayer(player, pump, playerXp(player));
    ctx.check(stored(pump) == config::get<XpPumpConfig>().internalCapacity, "internal store capped");
    ctx.check(playerXp(player) == core::xp::totalForLevel(200) - stored(pump), "the rest stays with the player");
    setPlayerXp(player, 0);
});

ABP_REGISTER_SELFTEST("xppump.tank", [](Context& ctx) {
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(core::Tier::Iron);
    ctx.installUpgrade(bp, "tank");
    auto pump = setupPump(ctx, bp, Mode::Off);
    setPlayerXp(player, 500);
    ctx.check(fromPlayer(player, pump, 500) == 500, "stored 500 points");
    ctx.check(tank::amountOf(bp, fl::kExperience) == 500 * fl::kXpPerPoint, "as 10000 mB of experience in the tank");
    ctx.check(Settings::of(pump).xp == 0, "nothing in the internal store");
    ctx.check(toPlayer(player, pump, 200) == 200 && playerXp(player) == 200, "200 points back");
    ctx.check(tank::amountOf(bp, fl::kExperience) == 300 * fl::kXpPerPoint, "6000 mB left");
    setPlayerXp(player, 0);
});

ABP_REGISTER_SELFTEST("xppump.auto", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(core::Tier::Leather);
    auto  pump   = setupPump(ctx, bp, Mode::Both, 5);
    ctx.require(ctx.give(player, ctx.backpackItem(*bp), 8) == 8, "give backpack");
    setPlayerXp(player, core::xp::toTotal(12, 4));
    co_await ctx.ticks(25);
    ctx.check(playerXp(player) == core::xp::totalForLevel(5), "excess above level 5 stored, now " + std::to_string(playerXp(player)));
    ctx.check(stored(pump) == core::xp::toTotal(12, 4) - core::xp::totalForLevel(5), "stored the excess");
    setPlayerXp(player, 0);
    co_await ctx.ticks(25);
    ctx.check(playerLevel(player) == 5, "refilled to level 5");
    auto s = Settings::of(pump);
    s.mode = Mode::Store;
    s.save(pump);
    setPlayerXp(player, 0);
    co_await ctx.ticks(25);
    ctx.check(playerXp(player) == 0, "store-only mode doesn't refill");
    setPlayerXp(player, 0);
});

ABP_REGISTER_SELFTEST("xppump.mending", [](Context& ctx) {
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(core::Tier::Leather);
    auto  pump   = setupPump(ctx, bp, Mode::Off);
    auto  sword  = game::ItemConv::make("minecraft:iron_sword");
    EnchantmentInstance mending;
    mending.mEnchantType = Enchant::Type::Mending;
    mending.mLevel       = 1;
    ctx.require(EnchantUtils::applyEnchant(sword, mending, false), "enchant with Mending");
    sword.setDamageValue(100);
    auto where = game::selectedSlot(player);
    game::setItemAt(player, where, sword);
    store(pump, 20);
    auto used = mend(player, pump);
    ctx.check(used == 20, "used all 20 points, got " + std::to_string(used));
    ctx.check(game::itemAt(player, where).getDamageValue() == 60, "repaired 40 durability");
    store(pump, 1000);
    mend(player, pump);
    ctx.check(game::itemAt(player, where).getDamageValue() == 0, "fully repaired");
    ctx.check(stored(pump) == 1000 - 30, "only what was needed was used");
    game::setItemAt(player, where, ItemStack{});
});

ABP_REGISTER_SELFTEST("xppump.menu", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(core::Tier::Leather);
    auto  pump   = setupPump(ctx, bp, Mode::Off);
    setPlayerXp(player, core::xp::totalForLevel(30));
    upgrade::byId(kId)->openSettings(player, pump.withPlayer(&player));
    ctx.require(mm.current(player) != nullptr, "menu open");
    mm.forceShown(player);
    auto click = [&](int slot, Where dst = Where::Cursor) {
        (void)mm.simulate(player, {Action::Take, {Where::Menu, slot}, {dst, 0}, 1});
    };
    click(11); // store 1 level
    ctx.check(playerLevel(player) == 29, "store 1 level -> 29");
    click(10); // store 10 levels
    ctx.check(playerLevel(player) == 19, "store 10 levels -> 19");
    click(15); // take 1 level
    ctx.check(playerLevel(player) == 20, "take 1 level -> 20");
    click(9); // store all
    ctx.check(playerXp(player) == 0 && stored(pump) == core::xp::totalForLevel(30), "store all");
    click(17); // take all
    ctx.check(playerLevel(player) == 30 && stored(pump) == 0, "take all");
    click(14); // keep +1
    ctx.check(Settings::of(pump).keep == 11, "target level +1");
    click(14, Where::Inventory); // shift-click: +10
    ctx.check(Settings::of(pump).keep == 21, "target level +10 on shift-click");
    click(20); // mode cycles off -> store
    ctx.check(Settings::of(pump).mode == Mode::Store, "mode cycled");
    click(22);
    ctx.check(!Settings::of(pump).mending, "mending toggled off");
    co_await ctx.ticks(3);
    ctx.check(mm.shownItem(player, 13).mCount == 21, "target level shown as the stack size");
    mm.close(player);
    setPlayerXp(player, 0);
});

} // namespace
} // namespace abp::upgrade::xppump

ABP_REGISTER_CONFIG("xppump", abp::upgrade::xppump::XpPumpConfig);
ABP_REGISTER_UPGRADE(abp::upgrade::xppump::XpPumpUpgrade);
