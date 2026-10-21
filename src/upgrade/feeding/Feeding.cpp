// Feeding upgrade (basic + advanced): eats food from a carried backpack when the carrier is
// hungry. Eating goes through the item's own vanilla "finished using" path, so nutrition,
// saturation, effects (golden apples, suspicious stew, honey, ...) and containers (bowls,
// bottles) behave exactly like eating by hand.

#include "upgrade/feeding/Feeding.h"

#include <map>

#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/attribute/BaseAttributeMap.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/components/IFoodItemComponent.h"
#include "mc/world/level/Level.h"

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

FeedingOptions optionsOf(UpgradeContext const& ctx, bool advanced) {
    if (!advanced) return {HungerMode::Full, false}; // basic: never waste nutrition
    auto const& s = ctx.settings();
    FeedingOptions o{kAdvancedDefaultMode, kAdvancedDefaultHurt};
    if (s.is_object()) {
        if (auto it = s.find(kHungerModeKey); it != s.end() && it->is_number_integer()) {
            o.mode = hungerModeFromInt(it->get<int>());
        }
        if (auto it = s.find(kEatWhenHurtKey); it != s.end() && it->is_boolean()) o.eatWhenHurt = it->get<bool>();
    }
    return o;
}

core::Filter filterOf(UpgradeContext const& ctx, bool advanced) {
    auto const& s     = ctx.settings();
    int         slots = advanced ? kAdvancedFilterSlots : kBasicFilterSlots;
    if (!s.is_object() || !s.contains(kFilterKey)) return core::Filter(slots);
    auto f = core::Filter::fromJson(s.at(kFilterKey), slots);
    if (!advanced) { // the basic filter only has the allow/deny switch
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
    if (AttributeInstance const* at = player.getAttribute(Player::HUNGER()).mPtr) {
        s.hunger    = static_cast<int>(at->mCurrentValue);
        s.maxHunger = static_cast<int>(at->mCurrentMaxValue);
    }
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

void setHealth(Player& player, float value) {
    (void)attributesOf(player).setCurrentValue(SharedAttributes::HEALTH(), value);
}

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

/// Puts a container left over by eating (bowl, bottle) into the backpack, else the player.
void returnContainer(Player& player, game::Backpack& bp, core::ItemStackData const& left) {
    auto r = bp.inventory().insert(left, core::AccessContext{core::Source::Upgrade, &player});
    if (r.remainder.empty()) return;
    ItemStack rest = game::ItemConv::toStack(r.remainder);
    if (!player.addAndRefresh(rest) && !rest.isNull() && rest.mCount > 0) player.drop(rest, false);
}

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
    // The vanilla "finished eating" path: nutrition, effects, sounds and container conversion.
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

bool canBeFed(Player& player) {
    return !player.isCreative() && !player.isSpectator() && player.isAlive() && eaterState(player).missing() > 0;
}

int tryFeed(Player& player, UpgradeContext const& ctx, FeedingOptions const& options, core::Filter const& filter) {
    if (!canBeFed(player)) return -1;
    auto state = eaterState(player);
    auto const& harmful = config::get<FeedingConfig>().harmfulFoods;
    auto&       bp      = ctx.backpack();
    auto const& inv     = bp.inventory();
    for (int i = 0; i < inv.size(); ++i) {
        auto const& item = inv.at(i);
        if (item.empty()) continue;
        auto nutrition = nutritionOf(item);
        if (!nutrition) continue;
        if (!filterAllows(filter, item, isHarmful(item.id, harmful), &game::ItemConv::tags())) continue;
        if (!shouldEat(state, *nutrition, options)) continue;
        if (eatFrom(player, bp, i)) return i;
    }
    return -1;
}

// ---- upgrade types ------------------------------------------------------------------------------

namespace {

std::string modeName(Player& p, HungerMode m) {
    switch (m) {
    case HungerMode::Always:
        return trFor(p, "abp.feeding.mode.always", "Whenever hungry");
    case HungerMode::Half:
        return trFor(p, "abp.feeding.mode.half", "Half of the food is used");
    case HungerMode::Full:
    default:
        return trFor(p, "abp.feeding.mode.full", "No nutrition is wasted");
    }
}

std::string onOff(Player& p, bool v) {
    return v ? trFor(p, "abp.filter.on", "§aOn") : trFor(p, "abp.filter.off", "§cOff");
}

/// Filter editor plus the advanced hunger settings in the free control-row slots.
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
        auto o       = optionsOf(mCtx, true);
        setSlot(
            control + 5,
            std::make_shared<ui::ButtonSlot>(
                ui::icon(
                    "minecraft:cooked_beef",
                    trFor(player, "abp.feeding.hunger", "Eat when: {}", modeName(player, o.mode)),
                    {trFor(player, "abp.filter.click_to_cycle", "§7Click to cycle")}
                ),
                [this](ui::ClickContext const&) {
                    auto cur  = optionsOf(mCtx, true);
                    int  next = (static_cast<int>(cur.mode) + 1) % 3;
                    write(kHungerModeKey, next);
                }
            )
        );
        setSlot(
            control + 6,
            std::make_shared<ui::ButtonSlot>(
                ui::icon(
                    "minecraft:golden_apple",
                    trFor(player, "abp.feeding.hurt", "Eat when health is below half: {}", onOff(player, o.eatWhenHurt)),
                    {trFor(player, "abp.filter.click_to_toggle", "§7Click to toggle")}
                ),
                [this](ui::ClickContext const&) { write(kEatWhenHurtKey, !optionsOf(mCtx, true).eatWhenHurt); }
            )
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
            i.id           = Advanced ? "advanced_feeding" : "feeding";
            i.itemId       = Advanced ? "alleviatedbackpacks:advanced_feeding_upgrade" : "alleviatedbackpacks:feeding_upgrade";
            i.nameKey      = Advanced ? "abp.feeding.advanced.name" : "abp.feeding.name";
            i.nameFallback = Advanced ? "Advanced Feeding Upgrade" : "Feeding Upgrade";
            i.tier         = Advanced ? UpgradeTier::Advanced : UpgradeTier::Basic;
            i.group        = "feeding";
            i.hasSettings  = true;
            return i;
        }();
        return kInfo;
    }

    void onTick(TickContext const& tick) override {
        if (!canBeFed(tick.player)) return; // cheap check before parsing the settings
        (void)tryFeed(tick.player, tick.ctx, optionsOf(tick.ctx, Advanced), filterOf(tick.ctx, Advanced));
    }

    [[nodiscard]] std::shared_ptr<ui::Menu> createSettingsMenu(Player& player, UpgradeContext const& ctx) override {
        ui::FilterMenu::Options options;
        options.slots           = Advanced ? kAdvancedFilterSlots : kBasicFilterSlots;
        options.allowMatchBy    = Advanced;
        options.allowNbt        = Advanced;
        options.allowDurability = Advanced;
        options.onBack          = [bp = ctx.backpackPtr()](Player& p) {
            ui::BackpackMenu::openBackpack(p, bp, {}, ui::BackpackMenu::View::Upgrades);
        };
        return std::make_shared<FeedingMenu>(displayName(*this, &player), ctx.withPlayer(nullptr), Advanced, std::move(options));
    }

    [[nodiscard]] std::vector<std::string> describe(Player& player, UpgradeContext const& ctx) const override {
        std::vector<std::string> lines;
        auto                     f = filterOf(ctx, Advanced);
        int                      n = 0;
        for (auto const& g : f.ghosts) n += g.empty() ? 0 : 1;
        if (n > 0) {
            lines.push_back(
                f.mode == core::FilterMode::Allow ? trFor(player, "abp.feeding.describe.allow", "§7Only eats: {} item(s)", n)
                                                  : trFor(player, "abp.feeding.describe.deny", "§7Never eats: {} item(s)", n)
            );
        }
        if (Advanced) {
            auto o = optionsOf(ctx, true);
            lines.push_back("§7" + trFor(player, "abp.feeding.hunger", "Eat when: {}", modeName(player, o.mode)));
            lines.push_back("§7" + trFor(player, "abp.feeding.hurt", "Eat when health is below half: {}", onOff(player, o.eatWhenHurt)));
        } else {
            lines.push_back(trFor(player, "abp.feeding.describe.basic", "§7Eats when no nutrition is wasted"));
        }
        return lines;
    }
};

using FeedingUpgrade         = FeedingUpgradeT<false>;
using AdvancedFeedingUpgrade = FeedingUpgradeT<true>;

} // namespace

} // namespace abp::upgrade::feeding

ABP_REGISTER_UPGRADE(abp::upgrade::feeding::FeedingUpgrade);
ABP_REGISTER_UPGRADE(abp::upgrade::feeding::AdvancedFeedingUpgrade);
