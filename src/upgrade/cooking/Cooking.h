#pragma once

// Smelting / Smoking / Blasting upgrades and their Auto variants (Sophisticated Core
// `upgrades/cooking`).
//
// Each upgrade owns a small furnace (input 0, fuel 1, output 2) kept in its settings JSON, so the
// contents, the stored experience and the cooking state travel with the upgrade item. The furnace
// runs while the backpack is carried or placed, on the game's furnace / smoker / blast furnace
// recipes and fuel values, with SB's per-upgrade speed and fuel efficiency multipliers. The Auto
// variants push results into the backpack, pull fuel and inputs (through SB's input and fuel
// filters) and pour stored experience into the backpack's tanks as liquid experience (G3 API).
// One furnace upgrade per backpack (SB group "cooking_upgrades", "Furnace Upgrades"). Disabling
// the upgrade pauses it. The native SB tab is client/ui/tabs/cooking.

#include <optional>
#include <string_view>

#include "core/Filter.h"
#include "upgrade/IUpgrade.h"
#include "upgrade/cooking/CookingLogic.h"

class Player;

namespace abp::upgrade::cooking {

enum class Kind { Smelting, Smoking, Blasting };

/// SB/SC CookingUpgradeConfig (per upgrade).
struct CookingUpgradeConfig {
    /// Smelting speed multiplier (1.0 equals speed at which vanilla furnace smelts items), 0.25..4.
    double smeltingSpeedMultiplier  = 1.0;
    /// Fuel efficiency multiplier (1.0 equals speed at which it's used in vanilla furnace), 0.25..4.
    double fuelEfficiencyMultiplier = 1.0;
};

/// SB/SC AutoCookingUpgradeConfig (per Auto upgrade).
struct AutoCookingUpgradeConfig {
    double smeltingSpeedMultiplier  = 1.0;
    double fuelEfficiencyMultiplier = 1.0;
    /// Number of input filter slots (1..20).
    int    inputFilterSlots         = 8;
    /// Number of input filter slots displayed in a row (1..6).
    int    inputFilterSlotsInRow    = 4;
    /// Number of fuel filter slots (1..20).
    int    fuelFilterSlots          = 4;
    /// Number of fuel filter slots displayed in a row (1..6).
    int    fuelFilterSlotsInRow     = 4;
};

/// `config/cooking.json` (SB's server config sections of the six upgrades).
struct CookingConfig {
    int                      version = 2;
    CookingUpgradeConfig     smeltingUpgrade;
    CookingUpgradeConfig     smokingUpgrade;
    CookingUpgradeConfig     blastingUpgrade;
    AutoCookingUpgradeConfig autoSmeltingUpgrade;
    AutoCookingUpgradeConfig autoSmokingUpgrade;
    AutoCookingUpgradeConfig autoBlastingUpgrade;
};

/// SB upgrade group of the six cooking upgrades (max one per backpack).
inline constexpr std::string_view kGroup = "furnace_upgrades";

/// Recipe tag of the vanilla block ("furnace", "smoker", "blast_furnace").
[[nodiscard]] std::string_view recipeTag(Kind kind);
/// SC recipe cooking time: smelting 200, smoking / blasting 100 ticks.
[[nodiscard]] int recipeTime(Kind kind);
/// SC burnTimeModifier: 1 for smelting, 0.5 for smoking / blasting.
[[nodiscard]] float burnTimeModifier(Kind kind);

/// The result of cooking one `input` item in `kind`, or nullopt.
[[nodiscard]] std::optional<core::ItemStackData> recipeResult(Kind kind, core::ItemStackData const& input);
/// Furnace burn ticks of one `fuel` item (0 = not a fuel).
[[nodiscard]] int furnaceBurnTicks(core::ItemStackData const& fuel);
/// Experience of one smelted `result` item (the recipe's experience).
[[nodiscard]] float smeltingExperience(core::ItemStackData const& result);

class CookingUpgrade : public IUpgrade {
public:
    CookingUpgrade(Kind kind, bool automatic);

    [[nodiscard]] UpgradeInfo const& info() const override { return mInfo; }
    [[nodiscard]] Kind               kind() const { return mKind; }
    [[nodiscard]] bool               automatic() const { return mAuto; }

    void onTick(TickContext const& tick) override;
    void onPlacedTick(PlacedTickContext const& tick) override;
    /// SC onBeforeRemoved -> pause: the burning fuel and the cooking progress are lost; the items
    /// and the stored experience stay with the upgrade item.
    void onRemove(UpgradeContext const& ctx) override;
    [[nodiscard]] std::vector<std::string> describe(Player& player, UpgradeContext const& ctx) const override;

    /// Speed and fuel multipliers of this upgrade (clamped to SB's 0.25..4).
    [[nodiscard]] double speedMultiplier() const;
    [[nodiscard]] double fuelMultiplier() const;
    /// Auto variants: filter slots and slots per row (clamped to SB's ranges).
    [[nodiscard]] int inputFilterSlots() const;
    [[nodiscard]] int inputFilterSlotsInRow() const;
    [[nodiscard]] int fuelFilterSlots() const;
    [[nodiscard]] int fuelFilterSlotsInRow() const;

    /// The game-backed environment for this upgrade at `ctx` (the Auto variants' filters included).
    [[nodiscard]] CookEnv env(UpgradeContext const& ctx) const;
    /// Runs the furnace for `ticks` game ticks. Returns true if the state changed.
    bool advance(UpgradeContext const& ctx, int ticks) const;

    // ---- state access (settings JSON) ---------------------------------------------------------
    [[nodiscard]] static CookState state(UpgradeContext const& ctx);
    static void                    setState(UpgradeContext const& ctx, CookState const& state);
    /// SC input filter (allow list by default; an empty allow list matches nothing).
    [[nodiscard]] core::Filter inputFilter(UpgradeContext const& ctx) const;
    /// SC fuel filter (allow list by default; an empty allow list matches everything).
    [[nodiscard]] core::Filter fuelFilter(UpgradeContext const& ctx) const;
    static void                setInputFilter(UpgradeContext const& ctx, core::Filter const& filter);
    static void                setFuelFilter(UpgradeContext const& ctx, core::Filter const& filter);

    /// SC awardUsedRecipesAndPopExperience: spawns the stored experience (whole points, the
    /// fraction by chance) as orbs at the player. Returns the experience spawned.
    static int popExperience(UpgradeContext const& ctx, Player& player);
    /// SC tryPushingXpToTanks: pours stored experience into the backpack's tanks.
    static void pushExperienceToTanks(UpgradeContext const& ctx, CookState& state);

    /// True while `ctx` still refers to an installed upgrade of this type.
    [[nodiscard]] bool installedAt(UpgradeContext const& ctx) const;

private:
    Kind        mKind;
    bool        mAuto;
    UpgradeInfo mInfo;
};

/// The registered upgrade for `kind` / `automatic`.
[[nodiscard]] CookingUpgrade& upgradeOf(Kind kind, bool automatic);
/// The cooking upgrade type installed at `ctx` (nullptr if none).
[[nodiscard]] CookingUpgrade* cookingAt(UpgradeContext const& ctx);

} // namespace abp::upgrade::cooking
