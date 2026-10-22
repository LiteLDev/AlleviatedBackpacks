// Feeding upgrade (basic + advanced), as Sophisticated Core's FeedingUpgradeWrapper: feeds the
// carrier (or, for a placed backpack, players within 3 blocks) with food from the backpack.
// Eating goes through the item's own vanilla "finished using" path, so nutrition, saturation,
// effects (golden apples, suspicious stew, rotten flesh, chorus fruit, ...) and containers (bowls,
// bottles) behave exactly like eating by hand; leftovers go back into the backpack.

#include "upgrade/feeding/Feeding.h"

#include <cmath>
#include <cstdint>
#include <map>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/attribute/BaseAttributeMap.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/components/IFoodItemComponent.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Tick.h"
#include "mc/world/phys/AABB.h"

#include "game/ItemConv.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/FilterMenu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"

ABP_REGISTER_CONFIG("feeding", abp::upgrade::feeding::FeedingConfig);

namespace abp::upgrade::feeding {

// ---- settings -----------------------------------------------------------------------------------

FeedingTierConfig tierConfig(bool advanced) {
    auto const& cfg = config::get<FeedingConfig>();
    auto        c   = advanced ? cfg.advancedFeedingUpgrade : cfg.feedingUpgrade;
    c.filterSlots   = std::clamp(c.filterSlots, 1, 20);
    c.slotsInRow    = std::clamp(c.slotsInRow, 1, 6);
    return c;
}

FeedingOptions optionsOf(UpgradeContext const& ctx) {
    auto const&    s = ctx.settings();
    FeedingOptions o;
    if (!s.is_object()) return o;
    if (auto it = s.find(kHungerLevelKey); it != s.end() && it->is_string()) {
        o.level = hungerLevelFromName(it->get<std::string>());
    } else if (auto old = s.find("hungerMode"); old != s.end() && old->is_number_integer()) {
        // Phase-1 data: 0 always, 1 half, 2 full.
        int v   = old->get<int>();
        o.level = v == 0 ? HungerLevel::Any : v == 2 ? HungerLevel::Full : HungerLevel::Half;
    }
    if (auto it = s.find(kWhenHurtKey); it != s.end() && it->is_boolean()) o.feedImmediatelyWhenHurt = it->get<bool>();
    return o;
}

core::Filter filterOf(UpgradeContext const& ctx, bool advanced) {
    auto const&  s     = ctx.settings();
    int          slots = tierConfig(advanced).filterSlots;
    core::Filter f(slots);
    f.mode = core::FilterMode::Deny; // SC: block list by default
    if (s.is_object() && s.contains(kFilterKey)) f = core::Filter::fromJson(s.at(kFilterKey), slots);
    f.emptyAllowsAll = false; // SC: an empty allow list matches nothing
    if (!advanced) {
        f.matchBy         = core::MatchBy::Item;
        f.matchNbt        = false;
        f.matchDurability = false;
    }
    return f;
}

void writeSetting(UpgradeContext const& ctx, char const* key, nlohmann::json value) {
    if (!ctx.settings().is_object()) ctx.settings() = nlohmann::json::object();
    ctx.settings()[key] = std::move(value);
    ctx.saveSettings(false);
}

// ---- player state -------------------------------------------------------------------------------

EaterState eaterState(Player& player) {
    EaterState s;
    if (AttributeInstance const* at = player.getAttribute(Player::HUNGER()).mPtr) s.foodLevel = static_cast<int>(at->mCurrentValue);
    s.health    = static_cast<float>(player.getHealth());
    s.maxHealth = static_cast<float>(player.getMaxHealth());
    return s;
}

namespace {
BaseAttributeMap& attributesOf(Player& player) {
    // The non-const Actor::getAttributes() overload isn't picked through gsl::not_null; the map
    // itself belongs to the (non-const) player.
    return const_cast<BaseAttributeMap&>(*static_cast<Actor&>(player).getAttributes());
}
} // namespace

void setHunger(Player& player, float value) { (void)attributesOf(player).setCurrentValue(Player::HUNGER(), value); }

void setHealth(Player& player, float value) { (void)attributesOf(player).setCurrentValue(SharedAttributes::HEALTH(), value); }

// ---- food lookup --------------------------------------------------------------------------------

std::optional<int> nutritionOf(core::ItemStackData const& item) {
    if (item.empty()) return std::nullopt;
    // Nutrition depends only on the item type: cache by id/aux.
    static std::map<std::pair<std::string, int>, std::optional<int>> cache;
    auto key = std::make_pair(item.id, item.aux);
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    ItemStack   stack = game::ItemConv::toStack(item, 1);
    Item const* type  = stack.mItem.get();
    if (!type) return std::nullopt; // unresolved item: don't cache
    std::optional<int> result;
    if (IFoodItemComponent* food = type->getFood()) result = food->getNutrition();
    cache.emplace(std::move(key), result);
    return result;
}

namespace {

/// Food level and saturation (to tell whether eating did anything).
std::pair<float, float> foodValues(Player& player) {
    std::pair<float, float> v{0.f, 0.f};
    if (AttributeInstance const* at = player.getAttribute(Player::HUNGER()).mPtr) v.first = at->mCurrentValue;
    if (AttributeInstance const* at = player.getAttribute(Player::SATURATION()).mPtr) v.second = at->mCurrentValue;
    return v;
}

/// SC: the result of eating (bowl, bottle) goes into the backpack, else the player (or the ground).
void returnContainer(Player& player, game::Backpack& bp, core::ItemStackData const& left) {
    auto r = bp.inventory().insert(left, core::AccessContext{core::Source::Upgrade, &player});
    if (r.remainder.empty()) return;
    ItemStack rest = game::ItemConv::toStack(r.remainder);
    if (!player.addAndRefresh(rest) && !rest.isNull() && rest.mCount > 0) player.drop(rest, false);
}

/// Eats one item from `slot` through the vanilla use path. False if the item refused.
bool eatFrom(Player& player, game::Backpack& bp, int slot) {
    core::AccessContext ac{core::Source::Upgrade, &player};
    auto                taken = bp.inventory().extract(slot, 1, ac);
    if (taken.empty()) return false;
    ItemStack   food = game::ItemConv::toStack(taken, 1);
    Item const* type = food.mItem.get();
    if (!type) {
        (void)bp.inventory().insertInto(slot, taken, core::AccessContext{core::Source::Internal, &player});
        return false;
    }
    auto before = foodValues(player);
    (void)type->useTimeDepleted(food, &player.getLevel(), &player);
    bool unchanged = !food.isNull() && food.mCount > 0 && game::ItemConv::toData(food).sameItem(taken);
    if (unchanged && foodValues(player) == before) {
        // The item refused to be eaten (e.g. an add-on food with use conditions): put it back.
        (void)bp.inventory().insertInto(slot, taken, core::AccessContext{core::Source::Internal, &player});
        return false;
    }
    if (!unchanged && !food.isNull() && food.mCount > 0) returnContainer(player, bp, game::ItemConv::toData(food));
    bp.markDirty();
    return true;
}

} // namespace

bool feedPlayerAndGetHungry(Player& player, UpgradeContext const& ctx, bool advanced, int* fedSlot) {
    if (fedSlot) *fedSlot = -1;
    if (!player.isAlive()) return false;
    auto state = eaterState(player);
    if (state.hunger() == 0) return false;
    auto        options = optionsOf(ctx);
    auto        filter  = filterOf(ctx, advanced);
    auto&       bp      = ctx.backpack();
    auto const& inv     = bp.inventory();
    for (int i = 0; i < inv.size(); ++i) {
        auto const& item = inv.at(i);
        if (item.empty()) continue;
        auto nutrition = nutritionOf(item);
        if (!nutrition || !isEdible(item.id, *nutrition)) continue;
        if (!filter.passes(item, &game::ItemConv::tags())) continue;
        if (!shouldFeed(state, *nutrition, options)) continue;
        if (!eatFrom(player, bp, i)) continue;
        if (fedSlot) *fedSlot = i;
        return eaterState(player).foodLevel < 20;
    }
    return false;
}

// ---- upgrade types ------------------------------------------------------------------------------

namespace {

std::string hungerLevelText(Player& p, HungerLevel l) {
    switch (l) {
    case HungerLevel::Any:
        return trFor(p, "abp.feeding.button.hunger_level_any", "Feed as soon as player is tiny bit hungry\nwastes quite a few hunger points of the food");
    case HungerLevel::Full:
        return trFor(p, "abp.feeding.button.hunger_level_full", "Only feed when player is hungry enough\nto not waste any hunger points of the food at all");
    case HungerLevel::Half:
    default:
        return trFor(p, "abp.feeding.button.hunger_level_half", "Only feed when player is hungry enough\nto only waste half hunger points of the food at most");
    }
}

std::string hurtText(Player& p, bool v) {
    return v ? trFor(p, "abp.feeding.button.feed_immediately_when_hurt", "Feed player immediately when hurt\nIgnores hunger setting when player is not at max health")
             : trFor(p, "abp.feeding.button.do_not_consider_health", "Do not consider health\nIgnores player's health and only feeds based on hunger setting");
}

/// Splits "first\nrest" into the title and the lore lines.
std::pair<std::string, std::vector<std::string>> titleAndLore(std::string const& text) {
    std::vector<std::string> lines;
    std::size_t              start = 0;
    while (true) {
        auto nl = text.find('\n', start);
        lines.push_back(text.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    auto title = lines.front();
    lines.erase(lines.begin());
    for (auto& l : lines) l = "§7" + l;
    return {title, lines};
}

/// Chest-menu fallback: filter editor plus the advanced hunger settings in the control row.
class FeedingMenu : public ui::FilterMenu {
public:
    FeedingMenu(std::string title, UpgradeContext ctx, bool advanced, Options options)
    : FilterMenu(
          std::move(title),
          [ctx, advanced] { return filterOf(ctx, advanced); },
          [ctx](core::Filter const& f) { writeSetting(ctx, kFilterKey, f.toJson()); },
          std::move(options)
      ),
      mCtx(std::move(ctx)),
      mAdvanced(advanced) {}

    void build(Player& player) override {
        FilterMenu::build(player);
        if (!mAdvanced) return;
        int  control = size() - 9;
        auto o       = optionsOf(mCtx);
        auto level   = titleAndLore(hungerLevelText(player, o.level));
        setSlot(
            control + 5,
            std::make_shared<ui::ButtonSlot>(ui::icon("minecraft:cooked_beef", level.first, level.second), [this](ui::ClickContext const&) {
                write(kHungerLevelKey, std::string(toString(next(optionsOf(mCtx).level))));
            })
        );
        auto hurt = titleAndLore(hurtText(player, o.feedImmediatelyWhenHurt));
        setSlot(
            control + 6,
            std::make_shared<ui::ButtonSlot>(ui::icon("minecraft:golden_apple", hurt.first, hurt.second), [this](ui::ClickContext const&) {
                write(kWhenHurtKey, !optionsOf(mCtx).feedImmediatelyWhenHurt);
            })
        );
    }

private:
    void write(char const* key, nlohmann::json value) {
        writeSetting(mCtx, key, std::move(value));
        invalidate();
    }

    UpgradeContext mCtx;
    bool           mAdvanced;
};

template <bool Advanced>
class FeedingUpgradeT : public IUpgrade {
public:
    [[nodiscard]] UpgradeInfo const& info() const override {
        static UpgradeInfo const kInfo = [] {
            UpgradeInfo i;
            i.id             = std::string(Advanced ? kAdvancedId : kBasicId);
            i.itemId         = Advanced ? "alleviatedbackpacks:advanced_feeding_upgrade" : "alleviatedbackpacks:feeding_upgrade";
            i.nameKey        = Advanced ? "abp.feeding.advanced.name" : "abp.feeding.name";
            i.nameFallback   = Advanced ? "Advanced Feeding upgrade" : "Feeding Upgrade";
            i.tier           = Advanced ? UpgradeTier::Advanced : UpgradeTier::Basic;
            i.group          = "feeding";
            i.maxPerBackpack = 9; // SB: no limit
            i.hasSettings    = true;
            return i;
        }();
        return kInfo;
    }

    void onTick(TickContext const& tick) override {
        auto& state = tick.ctx.transient<Cooldown>();
        auto  now   = static_cast<std::int64_t>(tick.player.getLevel().getCurrentServerTick().tickID);
        if (state.until > now) return;
        bool hungry = feedPlayerAndGetHungry(tick.player, tick.ctx, Advanced);
        state.until = now + (hungry ? kStillHungryCooldown : kCooldown);
    }

    void onPlacedTick(PlacedTickContext const& tick) override {
        auto& state = tick.ctx.transient<Cooldown>();
        auto& level = tick.region.getLevel();
        auto  now   = static_cast<std::int64_t>(level.getCurrentServerTick().tickID);
        if (state.until > now) return;
        // SC: every player in AABB(pos).inflate(FEEDING_RANGE).
        float r = static_cast<float>(kFeedingRange);
        AABB  box{
            Vec3{static_cast<float>(tick.x) - r, static_cast<float>(tick.y) - r, static_cast<float>(tick.z) - r},
            Vec3{static_cast<float>(tick.x) + 1 + r, static_cast<float>(tick.y) + 1 + r, static_cast<float>(tick.z) + 1 + r}
        };
        bool hungry = false;
        level.forEachPlayer([&](Player& p) {
            if (p.isClientSide() || static_cast<int>(p.getDimensionId()) != tick.dimension) return true;
            auto const& b = p.getAABB();
            if (b.max.x <= box.min.x || b.min.x >= box.max.x || b.max.y <= box.min.y || b.min.y >= box.max.y || b.max.z <= box.min.z
                || b.min.z >= box.max.z) {
                return true;
            }
            hungry = feedPlayerAndGetHungry(p, tick.ctx.withPlayer(&p), Advanced) || hungry;
            return true;
        });
        state.until = now + (hungry ? kStillHungryCooldown : kCooldown);
    }

    [[nodiscard]] std::shared_ptr<ui::Menu> createSettingsMenu(Player& player, UpgradeContext const& ctx) override {
        ui::FilterMenu::Options options;
        options.slots           = tierConfig(Advanced).filterSlots;
        options.allowMatchBy    = Advanced;
        options.allowNbt        = Advanced;
        options.allowDurability = Advanced;
        options.onBack          = [bp = ctx.backpackPtr()](Player& p) {
            ui::BackpackMenu::openBackpack(p, bp, {}, ui::BackpackMenu::View::Upgrades);
        };
        return std::make_shared<FeedingMenu>(displayName(*this, &player), ctx.withPlayer(nullptr), Advanced, std::move(options));
    }

    [[nodiscard]] std::vector<std::string> describe(Player& player, UpgradeContext const& ctx) const override {
        if (!Advanced) return {};
        auto o = optionsOf(ctx);
        return {"§7" + titleAndLore(hungerLevelText(player, o.level)).first, "§7" + titleAndLore(hurtText(player, o.feedImmediatelyWhenHurt)).first};
    }

private:
    struct Cooldown {
        std::int64_t until = 0;
    };
};

using FeedingUpgrade         = FeedingUpgradeT<false>;
using AdvancedFeedingUpgrade = FeedingUpgradeT<true>;

} // namespace

} // namespace abp::upgrade::feeding

ABP_REGISTER_UPGRADE(abp::upgrade::feeding::FeedingUpgrade);
ABP_REGISTER_UPGRADE(abp::upgrade::feeding::AdvancedFeedingUpgrade);
