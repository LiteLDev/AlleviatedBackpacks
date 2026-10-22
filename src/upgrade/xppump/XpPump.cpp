// Experience Pump upgrade (SB "Experience Pump Upgrade"; Sophisticated Core `XpPumpUpgradeItem`,
// `XpPumpUpgradeWrapper`, `XpPumpUpgradeConfig`).

#include "upgrade/xppump/XpPump.h"

#include <algorithm>
#include <climits>
#include <random>

#include "mc/entity/components/AttributesComponent.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/attribute/BaseAttributeMap.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"

#include "api/BackpackCapabilities.h"
#include "api/Service.h"
#include "core/Experience.h"
#include "game/PlayerScan.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "net/NativeScreens.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/tank/FastTick.h"
#include "upgrade/tank/Tank.h"

namespace abp::upgrade::xppump {

/// `config/xppump.json` (SB `xpPumpUpgrade` server config).
struct XpPumpConfig {
    int version = 2;
    /// Whether the XP pump can mend items with Mending (false turns the feature off).
    bool mendingOn = true;
    /// Experience points used at most to mend an item per operation (every 5 ticks; 1 point
    /// repairs 2 durability).
    int maxXpPointsPerMending = 5;
};

namespace {
constexpr int kCooldown         = 5;
constexpr int kAllLevels        = 10000;
constexpr int kPlayerSearchRange = 3;

XpPumpConfig const& cfg() { return config::get<XpPumpConfig>(); }

bool isXpPump(UpgradeContext const& ctx) {
    auto const& slots = ctx.backpack().data().upgrades;
    return ctx.slot() >= 0 && ctx.slot() < static_cast<int>(slots.size()) && slots[static_cast<std::size_t>(ctx.slot())].item.id == kItemId;
}

constexpr std::string_view kDirectionNames[] = {"input", "output", "keep", "off"};

Direction directionFrom(std::string_view s) {
    for (int i = 0; i < 4; ++i) {
        if (kDirectionNames[i] == s) return static_cast<Direction>(i);
    }
    return Direction::Input; // SB AutomationDirection.fromName default
}
} // namespace

// ---- settings -----------------------------------------------------------------------------------------

Settings Settings::of(UpgradeContext const& ctx) {
    Settings    s;
    auto const& j = ctx.settings();
    if (!j.is_object()) return s;
    if (j.contains("direction")) {
        s.direction = directionFrom(j.value("direction", std::string("input")));
    } else if (j.contains("mode")) { // phase-1: off / store / give / both
        auto m      = j.value("mode", std::string("store"));
        s.direction = m == "off" ? Direction::Off : m == "give" ? Direction::Output : m == "both" ? Direction::Keep : Direction::Input;
    }
    s.level         = std::max(0, j.contains("level") ? j.value("level", 10) : j.value("keep", 10));
    s.levelsToStore = std::max(1, j.value("levelsToStore", 1));
    s.levelsToTake  = std::max(1, j.value("levelsToTake", 1));
    s.mendItems     = j.contains("mendItems") ? j.value("mendItems", true) : j.value("mending", true);
    return s;
}

void Settings::save(UpgradeContext const& ctx) const {
    auto& j = ctx.settings();
    if (!j.is_object()) j = nlohmann::json::object();
    for (auto k : {"mode", "keep", "mending"}) j.erase(k);
    j["direction"]     = std::string(kDirectionNames[static_cast<int>(direction)]);
    j["level"]         = level;
    j["levelsToStore"] = levelsToStore;
    j["levelsToTake"]  = levelsToTake;
    j["mendItems"]     = mendItems;
    ctx.saveSettings();
}

// ---- player experience ------------------------------------------------------------------------------

int playerLevel(Player& p) { return static_cast<int>(p.getAttribute(Player::LEVEL()).mPtr->mCurrentValue); }

int playerXp(Player& p) {
    int   level    = playerLevel(p);
    float progress = p.getAttribute(Player::EXPERIENCE()).mPtr->mCurrentValue;
    auto  need     = core::xp::pointsForNextLevel(level);
    // Rounded (not truncated like Java): Bedrock keeps the progress as a float fraction.
    auto points = static_cast<long long>(progress * static_cast<float>(need) + 0.5f);
    return static_cast<int>(std::min<long long>(INT_MAX, core::xp::toTotal(level, std::clamp(points, 0LL, need - 1))));
}

void setPlayerXp(Player& p, long long total) {
    auto target = core::xp::fromTotal(std::max(0LL, total));
    int  cur    = playerLevel(p);
    if (target.level != cur) p.addLevels(target.level - cur);
    if (auto attrs = p.getEntityContext().tryGetComponent<AttributesComponent>()) {
        auto need = static_cast<float>(core::xp::pointsForNextLevel(target.level));
        (void)attrs->mAttributes->setCurrentValue(Player::EXPERIENCE(), static_cast<float>(target.points) / need);
    }
}

namespace {
void giveExperiencePoints(Player& p, int points) { setPlayerXp(p, static_cast<long long>(playerXp(p)) + points); }

/// XpHelper.experienceToLiquid for whole points, saturating like Java's float -> int cast.
int toLiquid(long long points) { return static_cast<int>(std::clamp<long long>(points * api::xp::kLiquidPerPoint, 0, INT_MAX)); }

/// SB tryFillTankWithPlayerExperience.
void fillFromPlayer(Player& player, api::IFluidHandler& h, int stopAtLevel) {
    long long maxPoints = static_cast<long long>(playerXp(player)) - api::xp::experienceForLevel(stopAtLevel);
    if (maxPoints <= 0) return;
    auto& caps   = api::service();
    int   liquid = toLiquid(maxPoints);
    int   filled = api::fillTagged(caps, h, api::fluid_tags::Experience, liquid, {std::string(api::fluids::Experience), liquid}, false);
    if (filled > 0) giveExperiencePoints(player, -static_cast<int>(api::xp::liquidToExperience(filled)));
}

/// SB tryGivePlayerExperienceFromTank.
void giveFromTank(Player& player, api::IFluidHandler& h, int stopAtLevel) {
    long long maxPoints = static_cast<long long>(api::xp::experienceForLevel(stopAtLevel)) - playerXp(player);
    if (maxPoints <= 0) return;
    int  liquid  = toLiquid(maxPoints);
    auto drained = api::drainTagged(api::service(), h, api::fluid_tags::Experience, liquid, false);
    if (!drained.empty()) giveExperiencePoints(player, static_cast<int>(api::xp::liquidToExperience(drained.amount)));
}
} // namespace

void interactWithPlayer(Player& player, UpgradeContext const& ctx) {
    auto h = api::backpack::fluidHandler(ctx.backpackPtr());
    if (!h) return;
    auto s = Settings::of(ctx);
    if (s.direction == Direction::Off) return;
    int   level    = playerLevel(player);
    float progress = player.getAttribute(Player::EXPERIENCE()).mPtr->mCurrentValue;
    if ((s.direction == Direction::Input || s.direction == Direction::Keep) && (s.level < level || (s.level == level && progress > 0))) {
        fillFromPlayer(player, *h, s.level);
    } else if ((s.direction == Direction::Output || s.direction == Direction::Keep) && s.level > level) {
        giveFromTank(player, *h, s.level);
    }
}

void mendItems(Player& player, UpgradeContext const& ctx) {
    if (!cfg().mendingOn || !Settings::of(ctx).mendItems) return;
    // getRandomDamagedItemWithMending: hands and armor.
    using Loc = game::ItemLocation;
    std::vector<Loc> places{game::selectedSlot(player), Loc{Loc::Kind::Offhand, 0}};
    for (int a = 0; a < 4; ++a) places.push_back(Loc{Loc::Kind::Armor, a});
    std::vector<Loc> matching;
    for (auto const& loc : places) {
        auto const& stack = game::itemAt(player, loc);
        if (stack.isNull() || !stack.isDamageableItem() || stack.getDamageValue() <= 0) continue;
        if (EnchantUtils::hasEnchant(Enchant::Type::Mending, stack)) matching.push_back(loc);
    }
    if (matching.empty()) return;
    static std::mt19937 rng{std::random_device{}()};
    auto                loc   = matching[std::uniform_int_distribution<std::size_t>(0, matching.size() - 1)(rng)];
    auto const&         stack = game::itemAt(player, loc);
    float ratio  = static_cast<float>(core::xp::kDurabilityPerPoint);
    int   damage = stack.getDamageValue();
    float toTry  = std::min(static_cast<float>(std::clamp(cfg().maxXpPointsPerMending, 1, 20)), static_cast<float>(damage) / ratio);
    if (toTry <= 0) return;
    auto h = api::backpack::fluidHandler(ctx.backpackPtr());
    if (!h) return;
    auto  drained = api::drainTagged(api::service(), *h, api::fluid_tags::Experience, api::xp::experienceToLiquid(toTry), false);
    float xp      = api::xp::liquidToExperience(drained.empty() ? 0 : drained.amount);
    int   repair  = static_cast<int>(xp * ratio);
    if (repair <= 0) return;
    ItemStack copy = stack;
    copy.setDamageValue(static_cast<short>(std::max(0, damage - repair)));
    game::setItemAt(player, loc, copy);
}

void manualAction(Player& player, UpgradeContext const& ctx, std::string_view action) {
    auto h = tank::storageHandler(ctx.backpackPtr(), true); // manual actions ignore the in / out limit
    if (!h) return;
    auto s = Settings::of(ctx);
    if (action == "take") giveFromTank(player, *h, playerLevel(player) + s.levelsToTake);
    else if (action == "store") fillFromPlayer(player, *h, std::max(playerLevel(player) - s.levelsToStore, 0));
    else if (action == "takeAll") giveFromTank(player, *h, kAllLevels);
    else if (action == "storeAll") fillFromPlayer(player, *h, 0);
}

namespace {

struct Cooldown {
    std::int64_t until = 0;
};

/// Phase-1 XP pumps kept experience in the upgrade itself: move it into the tanks.
void migrateInternalXp(UpgradeContext const& ctx) {
    auto& j = ctx.settings();
    if (!j.is_object() || !j.contains("xp")) return;
    long long const before = j.value("xp", 0LL);
    long long       points = before;
    auto            h      = tank::storageHandler(ctx.backpackPtr(), true);
    if (points > 0 && h) {
        int liquid = toLiquid(points);
        int filled = api::fillTagged(api::service(), *h, api::fluid_tags::Experience, liquid, {std::string(api::fluids::Experience), liquid}, false);
        points -= static_cast<long long>(api::xp::liquidToExperience(filled));
    }
    if (points > 0 && points == before) return; // nothing moved: don't touch the settings
    if (points <= 0) j.erase("xp");
    else j["xp"] = points;
    ctx.saveSettings();
}

void tickFor(UpgradeContext const& ctx, std::function<void(std::function<void(Player&)> const&)> const& players) {
    if (!isXpPump(ctx)) return;
    auto& cd  = ctx.transient<Cooldown>();
    auto  now = fasttick::now();
    if (now < cd.until) return;
    migrateInternalXp(ctx);
    players([&](Player& p) {
        interactWithPlayer(p, ctx);
        mendItems(p, ctx);
    });
    cd.until = now + kCooldown;
}

class XpPumpUpgrade : public IUpgrade {
public:
    [[nodiscard]] UpgradeInfo const& info() const override {
        static UpgradeInfo const kInfo{
            .id             = std::string(kId),
            .itemId         = std::string(kItemId),
            .nameKey        = "abp.xppump.name",
            .nameFallback   = "Experience Pump Upgrade",
            .maxPerBackpack = 1000, // SB: no limit
            .hasSettings    = true,
        };
        return kInfo;
    }

    void onTick(TickContext const& tick) override {
        fasttick::keepAlive(tick, "xppump", [](Player& player, UpgradeContext const& ctx, game::ItemLocation, int) {
            tickFor(ctx, [&](auto const& fn) { fn(player); });
        });
    }

    void onPlacedTick(PlacedTickContext const& t) override {
        tickFor(t.ctx, [&](auto const& fn) {
            int dim = t.dimension;
            t.region.getLevel().forEachPlayer([&](Player& p) {
                if (p.isClientSide() || static_cast<int>(p.getDimensionId()) != dim) return true;
                auto f  = p.getFeetPos();
                bool in = f.x >= t.x - kPlayerSearchRange && f.x < t.x + 1 + kPlayerSearchRange && f.y >= t.y - kPlayerSearchRange
                       && f.y < t.y + 1 + kPlayerSearchRange && f.z >= t.z - kPlayerSearchRange && f.z < t.z + 1 + kPlayerSearchRange;
                if (in) fn(p);
                return true;
            });
        });
    }
};

// ---- native tab ---------------------------------------------------------------------------------------

ABP_REGISTER_TAB_DATA(net::TabDataProvider{
    std::string(kId),
    [](Player& player, UpgradeContext const& ctx) {
        auto s = Settings::of(ctx);
        return nlohmann::json{
            {"tabLabel", trFor(player, "abp.xppump.tab", "Exp. Pump")},
            {"tabTooltip", trFor(player, "abp.xppump.tab.tooltip", "Experience Pump")},
            {"xpPump",
             {{"direction", std::string(kDirectionNames[static_cast<int>(s.direction)])},
              {"level", s.level},
              {"levelsToStore", s.levelsToStore},
              {"levelsToTake", s.levelsToTake},
              {"mendItems", s.mendItems},
              {"mendingOn", cfg().mendingOn}}},
        };
    }
});

ABP_REGISTER_TAB_ACTION(net::TabActionHandler{
    std::string(kId),
    [](Player& player, UpgradeContext const& ctx, nlohmann::json const& data) {
        auto op = data.value("op", std::string{});
        auto s  = Settings::of(ctx);
        int  d  = data.value("delta", 0) > 0 ? 1 : -1;
        if (op == "direction") {
            s.direction = static_cast<Direction>((static_cast<int>(s.direction) + 1) % 4);
        } else if (op == "level") {
            if (s.level + d < 0) return true; // SB: levels below 0 are ignored
            s.level += d;
        } else if (op == "levelsToStore") {
            if (s.levelsToStore + d < 1) return true;
            s.levelsToStore += d;
        } else if (op == "levelsToTake") {
            if (s.levelsToTake + d < 1) return true;
            s.levelsToTake += d;
        } else if (op == "mend") {
            if (!cfg().mendingOn) return false;
            s.mendItems = !s.mendItems;
        } else if (op == "action") {
            auto what = data.value("what", std::string{});
            if (what != "take" && what != "store" && what != "takeAll" && what != "storeAll") return false;
            manualAction(player, ctx.withPlayer(&player), what);
            return true;
        } else {
            return false;
        }
        s.save(ctx);
        return true;
    }
});

} // namespace
} // namespace abp::upgrade::xppump

ABP_REGISTER_CONFIG("xppump", abp::upgrade::xppump::XpPumpConfig);
ABP_REGISTER_UPGRADE(abp::upgrade::xppump::XpPumpUpgrade);
