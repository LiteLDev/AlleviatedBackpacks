#pragma once

// Alchemy & Advanced Alchemy upgrades (Sophisticated Core `upgrades/alchemy`).
//
// Every 5 ticks the upgrade checks its filter slots against the wearer (a carried backpack) or the
// living entities within 3 blocks (a placed backpack). A filter whose condition holds and whose
// effects are missing on the target makes the upgrade use one matching item from the backpack:
// it waits the item's use time (splash potions: 1 tick), plays the use sounds, applies the item
// and puts the container (glass bottle, bucket) back into the backpack.
//
// Settings (upgrade settings JSON, see `Settings` in AlchemyRules.h) travel with the item. The
// settings UI is the native tab in src/client/ui/tabs/alchemy (tab data / actions below).

#include <memory>
#include <string>
#include <vector>

#include "mc/deps/core/math/Vec3.h"

#include "upgrade/IUpgrade.h"
#include "upgrade/alchemy/AlchemyRules.h"

class Actor;

namespace abp::upgrade::alchemy {

/// `config/alchemy.json` (SB `alchemyUpgrade.filterSlots` / `advancedAlchemyUpgrade.filterSlots`).
struct AlchemyConfig {
    int version                    = 1;
    int alchemyFilterSlots         = 4; ///< 1..20
    int advancedAlchemyFilterSlots = 8; ///< 1..20
};

inline constexpr char const* kBasicId      = "alchemy";
inline constexpr char const* kAdvancedId   = "advanced_alchemy";
inline constexpr char const* kGroup        = "alchemy";
inline constexpr char const* kBasicItem    = "alleviatedbackpacks:alchemy_upgrade";
inline constexpr char const* kAdvancedItem = "alleviatedbackpacks:advanced_alchemy_upgrade";

[[nodiscard]] int  filterSlots(bool advanced);
[[nodiscard]] bool isAdvanced(UpgradeContext const& ctx);
[[nodiscard]] bool isAlchemy(IUpgrade const& type);

[[nodiscard]] Settings settingsOf(UpgradeContext const& ctx);
void                   writeSettings(UpgradeContext const& ctx, Settings const& settings);

/// Item effects of a potion / splash potion (Bedrock potion data) or an effect food.
[[nodiscard]] std::vector<Effect> itemEffects(core::ItemStackData const& item);
/// SC `isValidAlchemyItem` for a filter slot: an alchemy item that is not a filter yet.
[[nodiscard]] bool canBeFilter(Settings const& settings, core::ItemStackData const& item);
/// Puts `item` (count 1) into filter slot `slot` (empty = clear). Returns false if refused.
bool setFilter(UpgradeContext const& ctx, int slot, core::ItemStackData const& item);

/// Condition inputs of a living entity.
[[nodiscard]] EntityState stateOf(Actor& entity);

/// The server clock of the alchemy ticker (ticks).
[[nodiscard]] long long now();

/// One SB check of the upgrade against `targets` (in order; the first that gets an item wins).
/// Does nothing while an application is running or before the next check time. Returns true if
/// an application started.
bool check(UpgradeContext const& ctx, std::vector<Actor*> const& targets);
/// Like `check`, ignoring the check interval (selftests).
bool checkNow(UpgradeContext const& ctx, std::vector<Actor*> const& targets);
/// True while the upgrade waits for an item's use time.
[[nodiscard]] bool applying(UpgradeContext const& ctx);
/// Advances every running application by one tick (the ticker does it every server tick).
void tickApplications();
/// Ends every running application, putting the items back into their backpacks (world stops).
void abortApplications();

/// One entity a splash reaches, with its effect strength (ThrownPotion.applySplash).
struct SplashHit {
    Actor* actor = nullptr;
    float  scale = 1.f;
};
/// The splash of an alchemy splash potion hitting `hit` at `impact` (vanilla's `applySplash`):
/// of the `candidates` (entity, feet position) those closer than 4 blocks, with strength 1 for the
/// entity hit and 1 - distance / 4 for the others.
[[nodiscard]] std::vector<SplashHit>
splashHits(Vec3 const& impact, std::vector<std::pair<Actor*, Vec3>> const& candidates, Actor const* hit);
/// Where the potion of an alchemy splash on `target` breaks (SC: x + w/2, eye y, z + w/2).
[[nodiscard]] Vec3 splashImpact(Actor& target);

} // namespace abp::upgrade::alchemy
