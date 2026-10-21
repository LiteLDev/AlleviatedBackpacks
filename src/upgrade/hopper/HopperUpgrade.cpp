#include "upgrade/hopper/HopperUpgrade.h"

#include <algorithm>

#include "mc/world/Container.h"
#include "mc/world/Facing.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"

#include "core/Filter.h"
#include "feature/placed/ContainerIO.h"
#include "feature/placed/PlacedBackpacks.h"
#include "game/ItemConv.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "ui/menu/FilterMenu.h"
#include "ui/menu/Menu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"

ABP_REGISTER_CONFIG("hopper", abp::upgrade::hopper::HopperConfig);

namespace abp::upgrade::hopper {

namespace {
constexpr int kUp   = 1;
constexpr int kDown = 0;

struct Cooldown {
    int ticks = 0;
};

std::string dirName(Player& p, int face) {
    static constexpr std::string_view keys[]      = {"down", "up", "north", "south", "west", "east"};
    static constexpr std::string_view fallbacks[] = {"Down", "Up", "North", "South", "West", "East"};
    return trFor(p, "abp.hopper.dir." + std::string(keys[face]), fallbacks[face]);
}

Directions readDirections(nlohmann::json const& settings, std::string const& key, int def) {
    Directions out{};
    auto       it = settings.find(key);
    if (it == settings.end() || !it->is_array()) {
        out[static_cast<std::size_t>(def)] = true;
        return out;
    }
    for (std::size_t i = 0; i < out.size() && i < it->size(); ++i) out[i] = (*it)[i].is_boolean() && (*it)[i].get<bool>();
    return out;
}

std::string dirList(Player& p, Directions const& d) {
    std::string out;
    for (int i = 0; i < 6; ++i) {
        if (!d[static_cast<std::size_t>(i)]) continue;
        if (!out.empty()) out += ", ";
        out += dirName(p, i);
    }
    return out.empty() ? trFor(p, "abp.hopper.dir.none", "none") : out;
}

/// The hopper upgrade's settings page: input / output filters and (advanced) directions.
class HopperSettingsMenu : public ui::Menu {
public:
    HopperSettingsMenu(HopperUpgradeBase const& type, UpgradeContext ctx, std::string title)
    : Menu(std::move(title), Rows::Three),
      mType(type),
      mCtx(std::move(ctx)) {}

    void build(Player& player) override {
        for (int i = 0; i < size(); ++i) setSlot(i, std::make_shared<ui::StaticSlot>(ui::icon("minecraft:gray_stained_glass_pane", " ")));
        int  slots = mType.filterSlots();
        auto ctx   = mCtx;
        auto filterButton = [&](int slot, std::string_view item, std::string const& key, std::string name, std::string lore) {
            setSlot(slot, std::make_shared<ui::ButtonSlot>(ui::icon(item, name, {lore}), [ctx, key, slots, type = &mType, title = title()](ui::ClickContext const& c) {
                ui::FilterMenuOptions opts;
                opts.onBack = [ctx, type, title](Player& p) {
                    ui::MenuManager::get().open(p, std::make_shared<HopperSettingsMenu>(*type, ctx, title));
                };
                ui::MenuManager::get().open(c.player, ui::FilterMenu::forUpgrade(c.player, ctx, key, slots, std::move(opts)));
            }));
        };
        filterButton(
            0,
            "minecraft:hopper",
            "input",
            trFor(player, "abp.hopper.input_filter", "Input filter"),
            trFor(player, "abp.hopper.input_filter.lore", "§7Items pulled into the backpack")
        );
        filterButton(
            9,
            "minecraft:dropper",
            "output",
            trFor(player, "abp.hopper.output_filter", "Output filter"),
            trFor(player, "abp.hopper.output_filter.lore", "§7Items pushed out of the backpack")
        );
        if (mType.advanced()) {
            auto pull = mType.pullDirections(mCtx);
            auto push = mType.pushDirections(mCtx);
            for (int d = 0; d < 6; ++d) {
                toggle(player, 2 + d, "pull", d, pull[static_cast<std::size_t>(d)]);
                toggle(player, 11 + d, "push", d, push[static_cast<std::size_t>(d)]);
            }
        } else {
            setSlot(
                4,
                std::make_shared<ui::StaticSlot>(ui::icon(
                    "minecraft:book",
                    trFor(player, "abp.hopper.basic_info", "Pulls from above, pushes below"),
                    {trFor(player, "abp.hopper.placed_only", "§7Works while the backpack is placed")}
                ))
            );
        }
        setSlot(18, std::make_shared<ui::ButtonSlot>(ui::icon("minecraft:chest", trFor(player, "abp.menu.back_generic", "Back")), [bp = mCtx.backpackPtr()](ui::ClickContext const& c) {
            placed::PlacedBackpacks::openBackpack(c.player, bp, ui::BackpackMenu::View::Upgrades);
        }));
    }

    void onTick(Player& player) override {
        if (!stillInstalled()) ui::MenuManager::get().close(player);
    }

private:
    /// The upgrade slot still holds this upgrade type (it may have been removed meanwhile).
    [[nodiscard]] bool stillInstalled() const {
        auto const& slots = mCtx.backpack().data().upgrades;
        if (mCtx.slot() < 0 || mCtx.slot() >= static_cast<int>(slots.size())) return false;
        return upgradeFor(mCtx.state().item) == &mType;
    }

    void toggle(Player& player, int slot, std::string const& key, int face, bool on) {
        auto name = key == "pull" ? trFor(player, "abp.hopper.pull_from", "Pull from {}: {}", dirName(player, face), on ? "§aOn" : "§cOff")
                                  : trFor(player, "abp.hopper.push_to", "Push to {}: {}", dirName(player, face), on ? "§aOn" : "§cOff");
        setSlot(
            slot,
            std::make_shared<ui::ButtonSlot>(
                ui::icon(on ? "minecraft:lime_dye" : "minecraft:gray_dye", name, {trFor(player, "abp.filter.click_to_toggle", "§7Click to toggle")}),
                [this, key, face, on](ui::ClickContext const& c) {
                    if (!stillInstalled()) return ui::MenuManager::get().close(c.player);
                    mType.setDirection(mCtx, key, face, !on);
                    invalidate();
                }
            )
        );
    }

    HopperUpgradeBase const& mType;
    UpgradeContext           mCtx;
};
} // namespace

HopperUpgradeBase::HopperUpgradeBase(bool advanced) : mAdvanced(advanced) {
    mInfo.id             = advanced ? "advanced_hopper" : "hopper";
    mInfo.itemId         = advanced ? "alleviatedbackpacks:advanced_hopper_upgrade" : "alleviatedbackpacks:hopper_upgrade";
    mInfo.nameKey        = advanced ? "abp.upgrade.advanced_hopper.name" : "abp.upgrade.hopper.name";
    mInfo.nameFallback   = advanced ? "Advanced Hopper Upgrade" : "Hopper Upgrade";
    mInfo.tier           = advanced ? UpgradeTier::Advanced : UpgradeTier::Basic;
    mInfo.maxPerBackpack = 9;
    mInfo.hasSettings    = true;
    mInfo.toggleable     = true;
}

int HopperUpgradeBase::filterSlots() const {
    auto const& cfg = config::get<HopperConfig>();
    return std::clamp(mAdvanced ? cfg.advancedFilterSlots : cfg.basicFilterSlots, 1, 18);
}

Directions HopperUpgradeBase::pullDirections(UpgradeContext const& ctx) const {
    if (!mAdvanced) return {false, true, false, false, false, false};
    return readDirections(ctx.settings(), "pull", kUp);
}

Directions HopperUpgradeBase::pushDirections(UpgradeContext const& ctx) const {
    if (!mAdvanced) return {true, false, false, false, false, false};
    return readDirections(ctx.settings(), "push", kDown);
}

void HopperUpgradeBase::setDirection(UpgradeContext const& ctx, std::string const& key, int face, bool on) const {
    if (!mAdvanced || face < 0 || face > 5) return;
    auto dirs = key == "pull" ? pullDirections(ctx) : pushDirections(ctx);
    dirs[static_cast<std::size_t>(face)] = on;
    auto arr = nlohmann::json::array();
    for (bool b : dirs) arr.push_back(b);
    ctx.settings()[key] = std::move(arr);
    ctx.saveSettings(false);
}

void HopperUpgradeBase::onPlacedTick(PlacedTickContext const& tick) {
    auto const& cfg   = config::get<HopperConfig>();
    auto&       cd    = tick.ctx.transient<Cooldown>();
    cd.ticks         -= tick.interval;
    if (cd.ticks > 0) return;
    cd.ticks   = std::max(1, mAdvanced ? cfg.advancedTransferTicks : cfg.basicTransferTicks);
    int amount = std::max(1, mAdvanced ? cfg.advancedTransferAmount : cfg.basicTransferAmount);

    auto&       inv      = tick.ctx.backpack().inventory();
    auto const& settings = tick.ctx.settings();
    int         slots    = filterSlots();
    auto filterOf = [&](char const* key) {
        auto it = settings.is_object() ? settings.find(key) : settings.end();
        return core::Filter::fromJson(it != settings.end() ? *it : nlohmann::json::object(), slots);
    };
    auto input  = filterOf("input");
    auto output = filterOf("output");
    placed::ItemPredicate acceptIn  = [&](core::ItemStackData const& d) { return input.passes(d, &game::ItemConv::tags()); };
    placed::ItemPredicate acceptOut = [&](core::ItemStackData const& d) { return output.passes(d, &game::ItemConv::tags()); };
    core::AccessContext const self{core::Source::Upgrade};
    core::AccessContext const other{core::Source::Automation};

    auto& placedBackpacks = placed::PlacedBackpacks::get();
    BlockPos const pos{tick.x, tick.y, tick.z};
    auto pull = pullDirections(tick.ctx);
    auto push = pushDirections(tick.ctx);
    for (int d = 0; d < 6; ++d) {
        if (!pull[static_cast<std::size_t>(d)]) continue;
        auto nb = placed::neighbor(pos, d);
        if (auto bp = placedBackpacks.backpackAt(tick.region, nb)) {
            placed::moveBetween(bp->inventory(), inv, amount, other, self, acceptIn);
        } else if (auto* c = placed::containerAt(tick.region, nb)) {
            placed::pullFromContainer(*c, inv, amount, placed::oppositeFace(d), self, acceptIn);
        }
    }
    for (int d = 0; d < 6; ++d) {
        if (!push[static_cast<std::size_t>(d)]) continue;
        auto nb = placed::neighbor(pos, d);
        if (auto bp = placedBackpacks.backpackAt(tick.region, nb)) {
            placed::moveBetween(inv, bp->inventory(), amount, self, other, acceptOut);
        } else if (auto* c = placed::containerAt(tick.region, nb)) {
            placed::pushToContainer(inv, *c, amount, d, self, acceptOut);
        }
    }
}

std::shared_ptr<ui::Menu> HopperUpgradeBase::createSettingsMenu(Player& player, UpgradeContext const& ctx) {
    return std::make_shared<HopperSettingsMenu>(*this, ctx, displayName(*this, &player));
}

std::vector<std::string> HopperUpgradeBase::describe(Player& player, UpgradeContext const& ctx) const {
    return {
        trFor(player, "abp.hopper.describe.pull", "§7Pulls from: §f{}", dirList(player, pullDirections(ctx))),
        trFor(player, "abp.hopper.describe.push", "§7Pushes to: §f{}", dirList(player, pushDirections(ctx))),
        trFor(player, "abp.hopper.placed_only", "§7Works while the backpack is placed"),
    };
}

} // namespace abp::upgrade::hopper

ABP_REGISTER_UPGRADE(abp::upgrade::hopper::HopperUpgrade);
ABP_REGISTER_UPGRADE(abp::upgrade::hopper::AdvancedHopperUpgrade);
