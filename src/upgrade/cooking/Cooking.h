#pragma once

// Smelting / Smoking / Blasting upgrades and their Auto variants (unit U13 "cooking").
//
// Each upgrade owns a small furnace (input, fuel and output slots) kept in its settings JSON, so
// the contents travel with the upgrade item. While the backpack is carried the furnace runs at
// vanilla speed (smoker / blast furnace twice as fast) on the game's own furnace recipes and
// fuel values. The Auto variants feed it from the backpack storage through an input filter and a
// fuel filter and put the results back into the storage.

#include <memory>
#include <optional>
#include <string_view>

#include "core/Filter.h"
#include "upgrade/IUpgrade.h"
#include "upgrade/cooking/CookingLogic.h"

class Player;

namespace abp::upgrade::cooking {

enum class Kind { Smelting, Smoking, Blasting };

/// `config/cooking.json`.
struct CookingConfig {
    int    version                  = 1;
    /// Cooking speed factor (2.0 = twice as fast as the vanilla block).
    double cookingSpeedMultiplier   = 1.0;
    /// Fuel burn time factor (2.0 = fuel lasts twice as long).
    double fuelEfficiencyMultiplier = 1.0;
    /// Ghost slots of the Auto variants' input filter (1..9).
    int    autoInputFilterSlots     = 9;
    /// Ghost slots of the Auto variants' fuel filter (1..9).
    int    autoFuelFilterSlots      = 9;
};

/// Recipe tag of the vanilla block ("furnace", "smoker", "blast_furnace").
[[nodiscard]] std::string_view recipeTag(Kind kind);
/// Vanilla ticks per item (furnace 200, smoker / blast furnace 100).
[[nodiscard]] int burnInterval(Kind kind);
/// Block item used as the icon of this kind ("minecraft:furnace", ...).
[[nodiscard]] std::string_view blockId(Kind kind);

/// The game-backed environment (recipes, fuel values, config multipliers) for `kind`.
[[nodiscard]] CookEnv makeEnv(Kind kind);
/// The result of cooking one `input` item in `kind`, or nullopt.
[[nodiscard]] std::optional<core::ItemStackData> recipeResult(Kind kind, core::ItemStackData const& input);
/// Burn ticks of one `fuel` item in `kind` (0 = not a fuel), without config multipliers.
[[nodiscard]] int vanillaBurnTicks(Kind kind, core::ItemStackData const& fuel);
/// XP for `count` items of smelting result `output` (random rounding like vanilla).
[[nodiscard]] int xpFor(core::ItemStackData const& output, int count);

class CookingUpgrade : public IUpgrade {
public:
    CookingUpgrade(Kind kind, bool automatic);

    [[nodiscard]] UpgradeInfo const& info() const override { return mInfo; }
    [[nodiscard]] Kind               kind() const { return mKind; }
    [[nodiscard]] bool               automatic() const { return mAuto; }

    void onTick(TickContext const& tick) override;
    /// Keeps cooking while the backpack is placed as a block (no carrier: the Auto variants keep the
    /// experience of stored results until a player claims it, see claimPendingXp).
    void onPlacedTick(PlacedTickContext const& tick) override;

    [[nodiscard]] std::shared_ptr<ui::Menu> createSettingsMenu(Player& player, UpgradeContext const& ctx) override;
    [[nodiscard]] std::vector<std::string>  describe(Player& player, UpgradeContext const& ctx) const override;

    /// Runs the furnace for `ticks` game ticks (Auto variants exchange with the storage).
    /// `carrier` receives the XP of automatically stored results. Returns true if anything changed.
    bool advance(UpgradeContext const& ctx, Player* carrier, int ticks) const;

    // ---- state access (settings JSON) ---------------------------------------------------------
    [[nodiscard]] static CookState    state(UpgradeContext const& ctx);
    static void                       setState(UpgradeContext const& ctx, CookState const& state);
    [[nodiscard]] static core::Filter inputFilter(UpgradeContext const& ctx);
    [[nodiscard]] static core::Filter fuelFilter(UpgradeContext const& ctx);
    static void                       setInputFilter(UpgradeContext const& ctx, core::Filter const& filter);
    static void                       setFuelFilter(UpgradeContext const& ctx, core::Filter const& filter);

    /// Auto variants: gives `player` the experience of results stored while nobody carried the
    /// backpack (placed). Returns the XP awarded.
    static int claimPendingXp(UpgradeContext const& ctx, Player& player);

    /// True while `ctx` still refers to an installed upgrade of this type (menus check it).
    [[nodiscard]] bool installedAt(UpgradeContext const& ctx) const;

private:
    Kind        mKind;
    bool        mAuto;
    UpgradeInfo mInfo;
};

/// The registered upgrade for `kind` / `automatic`.
[[nodiscard]] CookingUpgrade& upgradeOf(Kind kind, bool automatic);

/// The chest menu of a cooking upgrade (input/fuel/output slots, burn and progress indicators;
/// Auto variants also show their input and fuel filters).
[[nodiscard]] std::shared_ptr<ui::Menu> makeMenu(Player& player, CookingUpgrade& upgrade, UpgradeContext const& ctx);

/// Menu slot layout (used by selftests and the client scenario).
struct MenuLayout {
    int input, fuel, output, flame, progress, back;
    int inputFilterStart = -1, fuelFilterStart = -1, inputModeToggle = -1, fuelModeToggle = -1;
};
[[nodiscard]] MenuLayout const& layout(bool automatic);

} // namespace abp::upgrade::cooking
