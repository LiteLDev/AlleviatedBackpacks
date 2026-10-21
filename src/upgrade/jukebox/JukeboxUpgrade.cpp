// Jukebox upgrades (basic: 1 disc, play/stop; advanced: 12 discs, playlist with previous/next,
// shuffle and repeat) and their menu.

#include <algorithm>

#include "ll/api/service/Bedrock.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"
#include "mc/world/item/ItemStack.h"

#include "game/ItemConv.h"
#include "mod/I18n.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/jukebox/Jukebox.h"

namespace abp::upgrade::jukebox {

namespace {
constexpr std::string_view kBasicItem    = "alleviatedbackpacks:jukebox_upgrade";
constexpr std::string_view kAdvancedItem = "alleviatedbackpacks:advanced_jukebox_upgrade";

bool isAdvanced(UpgradeContext const& ctx) { return ctx.state().item.id == kAdvancedItem; }

nlohmann::json const* discArray(UpgradeContext const& ctx) {
    auto const& s  = ctx.settings();
    auto        it = s.find("discs");
    return it != s.end() && it->is_array() ? &*it : nullptr;
}
} // namespace

// ---- settings ------------------------------------------------------------------------------------

bool isJukebox(UpgradeContext const& ctx) {
    auto const& slots = ctx.backpack().data().upgrades;
    if (ctx.slot() < 0 || ctx.slot() >= static_cast<int>(slots.size())) return false;
    auto const& id = ctx.state().item.id;
    return !ctx.state().empty() && (id == kBasicItem || id == kAdvancedItem);
}

int discSlots(UpgradeContext const& ctx) {
    if (!isJukebox(ctx)) return 0;
    return isAdvanced(ctx) ? kAdvancedSlots : kBasicSlots;
}

core::ItemStackData disc(UpgradeContext const& ctx, int index) {
    if (index < 0 || index >= discSlots(ctx)) return {};
    auto const* arr = discArray(ctx);
    if (!arr || index >= static_cast<int>(arr->size())) return {};
    return core::ItemStackData::fromJson((*arr)[static_cast<std::size_t>(index)]);
}

void setDisc(UpgradeContext const& ctx, int index, core::ItemStackData const& item) {
    int n = discSlots(ctx);
    if (index < 0 || index >= n) return;
    auto& s = ctx.settings();
    if (!s.contains("discs") || !s["discs"].is_array()) s["discs"] = nlohmann::json::array();
    auto& arr = s["discs"];
    while (static_cast<int>(arr.size()) < n) arr.push_back(nullptr);
    arr[static_cast<std::size_t>(index)] = item.empty() ? nlohmann::json(nullptr) : item.withCount(1).toJson();
    ctx.saveSettings(false);
}

std::vector<bool> occupiedDiscs(UpgradeContext const& ctx) {
    int               n = discSlots(ctx);
    std::vector<bool> out(static_cast<std::size_t>(n), false);
    for (int i = 0; i < n; ++i) out[static_cast<std::size_t>(i)] = !disc(ctx, i).empty();
    return out;
}

bool shuffle(UpgradeContext const& ctx) {
    if (!isJukebox(ctx) || !isAdvanced(ctx)) return false;
    auto const& s = ctx.settings();
    return s.contains("shuffle") && s["shuffle"].is_boolean() && s["shuffle"].get<bool>();
}

core::RepeatMode repeatMode(UpgradeContext const& ctx) {
    // The basic jukebox plays its disc once, like a vanilla jukebox.
    if (!isJukebox(ctx) || !isAdvanced(ctx)) return core::RepeatMode::None;
    auto const& s = ctx.settings();
    if (s.contains("repeat") && s["repeat"].is_string()) {
        if (auto m = core::repeatModeFromId(s["repeat"].get<std::string>())) return *m;
    }
    return core::RepeatMode::All;
}

void setShuffle(UpgradeContext const& ctx, bool on) {
    if (!isJukebox(ctx)) return;
    ctx.settings()["shuffle"] = on;
    ctx.saveSettings(false);
    Service::get().settingsChanged(ctx);
}

void setRepeatMode(UpgradeContext const& ctx, core::RepeatMode mode) {
    if (!isJukebox(ctx)) return;
    ctx.settings()["repeat"] = std::string(core::repeatModeId(mode));
    ctx.saveSettings(false);
}

// ---- menu ----------------------------------------------------------------------------------------

namespace {

constexpr std::string_view kFiller = "minecraft:gray_stained_glass_pane";

/// Ticks between closing one chest screen and opening the next. Swapping screens in place (or
/// re-opening right after the close) left the real client without a screen.
constexpr int kSwitchDelay = 15;

/// Closes the player's menu, then runs `open` once the client screen is surely gone.
void switchMenu(Player& player, std::function<void(Player&)> open) {
    ui::MenuManager::get().closeThen(player, [open = std::move(open)](Player& p) {
        auto id = p.getOrCreateUniqueID();
        Service::get().runLater(kSwitchDelay, [id, open] {
            auto level = ll::service::getLevel();
            if (auto* pl = level ? level->getPlayer(id) : nullptr; pl && pl->isAlive()) open(*pl);
        });
    });
}

/// Menu slot of disc slot `i`: basic in the middle; advanced in two rows of six.
int discMenuSlot(int i, int count) {
    if (count <= 1) return 22;
    return (i < 6 ? 10 : 19) + i % 6;
}

class JukeboxMenu : public ui::Menu {
public:
    JukeboxMenu(UpgradeContext ctx, std::string title) : Menu(std::move(title), Rows::Six), mCtx(std::move(ctx)) {}

    void build(Player& player) override {
        for (int i = 0; i < size(); ++i) setSlot(i, std::make_shared<ui::StaticSlot>(ui::icon(kFiller, " ")));
        if (!isJukebox(mCtx)) return;
        int  n   = discSlots(mCtx);
        bool adv = n > 1;
        for (int i = 0; i < n; ++i) setSlot(discMenuSlot(i, n), discSlot(i));
        setSlot(4, statusSlot());

        auto& svc = Service::get();
        setSlot(45, std::make_shared<ui::ButtonSlot>(
            ui::icon("minecraft:smithing_table", trFor(player, "abp.jukebox.back", "Back to upgrades")),
            [bp = mCtx.backpackPtr()](ui::ClickContext const& c) {
                switchMenu(c.player, [bp](Player& p) {
                    ui::BackpackMenu::openBackpack(p, bp, {}, ui::BackpackMenu::View::Upgrades);
                });
            }
        ));
        setSlot(48, button(
            [](Player& p) { return ui::icon("minecraft:lime_dye", trFor(p, "abp.jukebox.play", "Play")); },
            [this, &svc](Player& p) {
                if (svc.playing(mCtx.backpack().uuid(), mCtx.slot())) return;
                std::string reason;
                if (!svc.play(mCtx.withPlayer(&p), &p, std::nullopt, &reason) && !reason.empty()) p.sendMessage(reason);
            }
        ));
        setSlot(49, button(
            [](Player& p) { return ui::icon("minecraft:red_dye", trFor(p, "abp.jukebox.stop", "Stop")); },
            [this, &svc](Player&) { svc.stop(mCtx.backpack().uuid(), mCtx.slot()); }
        ));
        if (!adv) return;
        setSlot(47, button(
            [](Player& p) { return ui::icon("minecraft:arrow", trFor(p, "abp.jukebox.previous", "Previous disc")); },
            [this, &svc](Player&) { svc.previous(mCtx); }
        ));
        setSlot(50, button(
            [](Player& p) { return ui::icon("minecraft:spectral_arrow", trFor(p, "abp.jukebox.next", "Next disc")); },
            [this, &svc](Player&) { svc.next(mCtx); }
        ));
        setSlot(52, button(
            [this](Player& p) {
                bool on = shuffle(mCtx);
                return ui::icon(
                    on ? "minecraft:ender_eye" : "minecraft:ender_pearl",
                    on ? trFor(p, "abp.jukebox.shuffle.on", "Shuffle: §aOn") : trFor(p, "abp.jukebox.shuffle.off", "Shuffle: §cOff"),
                    {trFor(p, "abp.jukebox.click_to_change", "§7Click to change")}
                );
            },
            [this](Player&) { setShuffle(mCtx, !shuffle(mCtx)); }
        ));
        setSlot(53, button(
            [this](Player& p) {
                switch (repeatMode(mCtx)) {
                case core::RepeatMode::One:
                    return ui::icon("minecraft:comparator", trFor(p, "abp.jukebox.repeat.one", "Repeat: one disc"), {trFor(p, "abp.jukebox.click_to_change", "§7Click to change")});
                case core::RepeatMode::None:
                    return ui::icon("minecraft:barrier", trFor(p, "abp.jukebox.repeat.none", "Repeat: off"), {trFor(p, "abp.jukebox.click_to_change", "§7Click to change")});
                case core::RepeatMode::All:
                default:
                    return ui::icon("minecraft:repeater", trFor(p, "abp.jukebox.repeat.all", "Repeat: all discs"), {trFor(p, "abp.jukebox.click_to_change", "§7Click to change")});
                }
            },
            [this](Player&) { setRepeatMode(mCtx, core::nextRepeatMode(repeatMode(mCtx))); }
        ));
    }

    void onTick(Player& player) override {
        // The upgrade was removed (or disabled) through another menu: nothing left to show.
        if (!isJukebox(mCtx) || !mCtx.state().enabled) ui::MenuManager::get().close(player);
    }

    /// Redraw only when the backpack (discs, settings) or the playback (disc, elapsed second) changes.
    [[nodiscard]] std::optional<std::uint64_t> contentVersion() const override {
        std::uint64_t v  = mCtx.backpack().revision() * 0x9E3779B97F4A7C15ull;
        auto          st = Service::get().status(mCtx.backpack().uuid(), mCtx.slot());
        if (st) v ^= (static_cast<std::uint64_t>(st->index + 1) << 48) ^ static_cast<std::uint64_t>(st->elapsedTicks / 20 + 1);
        return v;
    }

    int quickMoveIn(Player& player, ItemStack const& stack) override {
        if (!isJukebox(mCtx) || !discInfo(stack)) return 0;
        int n = discSlots(mCtx);
        for (int i = 0; i < n; ++i) {
            if (!disc(mCtx, i).empty()) continue;
            if (auto* s = slot(discMenuSlot(i, n)); s && s->accept(player, stack) > 0) return 1;
        }
        return 0;
    }

private:
    std::shared_ptr<ui::Slot> button(std::function<ItemStack(Player&)> display, std::function<void(Player&)> onClick) {
        auto s       = std::make_shared<ui::CallbackSlot>();
        s->onDisplay = std::move(display);
        s->onClick   = [fn = std::move(onClick)](ui::ClickContext const& c) {
            fn(c.player);
            return true;
        };
        return s;
    }

    std::shared_ptr<ui::Slot> discSlot(int i) {
        auto s       = std::make_shared<ui::CallbackSlot>();
        s->onDisplay = [this, i](Player& p) {
            auto d = disc(mCtx, i);
            if (d.empty()) return ItemStack{};
            auto stack  = game::ItemConv::toStack(d, 1);
            auto status = Service::get().status(mCtx.backpack().uuid(), mCtx.slot());
            if (status && status->index == i) {
                game::ItemConv::setDisplay(stack, "", {trFor(p, "abp.jukebox.now_playing", "§a▶ Playing")});
            }
            return stack;
        };
        s->onTake = [this, i](Player&, int) {
            if (!isJukebox(mCtx)) return ItemStack{};
            auto d = disc(mCtx, i);
            if (d.empty()) return ItemStack{};
            setDisc(mCtx, i, {});
            return game::ItemConv::toStack(d, 1);
        };
        s->onAccept = [this, i](Player&, ItemStack const& stack) {
            if (!isJukebox(mCtx) || !disc(mCtx, i).empty() || !discInfo(stack)) return 0;
            setDisc(mCtx, i, game::ItemConv::toData(stack).withCount(1));
            return 1;
        };
        return s;
    }

    std::shared_ptr<ui::Slot> statusSlot() {
        auto s       = std::make_shared<ui::CallbackSlot>();
        s->onDisplay = [this](Player& p) {
            auto status = Service::get().status(mCtx.backpack().uuid(), mCtx.slot());
            int  n      = discSlots(mCtx);
            auto occ    = occupiedDiscs(mCtx);
            int  discs  = static_cast<int>(std::count(occ.begin(), occ.end(), true));
            std::vector<std::string> lore;
            std::string              name;
            if (status) {
                auto info = discInfo(disc(mCtx, status->index));
                name      = trFor(p, "abp.jukebox.status.playing", "§aNow playing: §f{}", info ? info->title : std::string("?"));
                lore.push_back(trFor(p, "abp.jukebox.status.time", "§7{} / {}", formatTime(status->elapsedTicks), formatTime(status->durationTicks)));
            } else {
                name = trFor(p, "abp.jukebox.status.stopped", "§7Not playing");
            }
            lore.push_back(trFor(p, "abp.jukebox.discs", "§7Discs: {}/{}", discs, n));
            lore.push_back(trFor(p, "abp.jukebox.hint", "§7Put music discs into the slots below"));
            return ui::icon("minecraft:jukebox", name, lore);
        };
        return s;
    }

    UpgradeContext mCtx;
};

// ---- upgrades ------------------------------------------------------------------------------------

class JukeboxUpgradeBase : public IUpgrade {
public:
    void onRemove(UpgradeContext const& ctx) override { Service::get().stop(ctx.backpack().uuid(), ctx.slot()); }

    // Swapping one open chest screen for another in place left the real client without a screen;
    // close the backpack first, then open the jukebox menu.
    void openSettings(Player& player, UpgradeContext const& ctx) override {
        auto menu = createSettingsMenu(player, ctx);
        switchMenu(player, [menu](Player& p) { ui::MenuManager::get().open(p, menu); });
    }

    std::shared_ptr<ui::Menu> createSettingsMenu(Player& player, UpgradeContext const& ctx) override {
        return std::make_shared<JukeboxMenu>(ctx.withPlayer(nullptr), displayName(*this, &player));
    }

    std::vector<std::string> describe(Player& player, UpgradeContext const& ctx) const override {
        std::vector<std::string> out;
        auto                     occ = occupiedDiscs(ctx);
        out.push_back(trFor(player, "abp.jukebox.discs", "§7Discs: {}/{}", std::count(occ.begin(), occ.end(), true), occ.size()));
        if (auto status = Service::get().status(ctx.backpack().uuid(), ctx.slot())) {
            if (auto info = discInfo(disc(ctx, status->index))) {
                out.push_back(trFor(player, "abp.jukebox.status.playing", "§aNow playing: §f{}", info->title));
            }
        }
        return out;
    }
};

class JukeboxUpgrade : public JukeboxUpgradeBase {
public:
    [[nodiscard]] UpgradeInfo const& info() const override {
        static UpgradeInfo const i{
            .id             = std::string(kBasicId),
            .itemId         = std::string(kBasicItem),
            .nameKey        = "abp.upgrade.jukebox.name",
            .nameFallback   = "Jukebox Upgrade",
            .tier           = UpgradeTier::Basic,
            .group          = "jukebox",
            .maxPerBackpack = 1,
            .hasSettings    = true,
        };
        return i;
    }
};

class AdvancedJukeboxUpgrade : public JukeboxUpgradeBase {
public:
    [[nodiscard]] UpgradeInfo const& info() const override {
        static UpgradeInfo const i{
            .id             = std::string(kAdvancedId),
            .itemId         = std::string(kAdvancedItem),
            .nameKey        = "abp.upgrade.advanced_jukebox.name",
            .nameFallback   = "Advanced Jukebox Upgrade",
            .tier           = UpgradeTier::Advanced,
            .group          = "jukebox",
            .maxPerBackpack = 1,
            .hasSettings    = true,
        };
        return i;
    }
};

} // namespace

} // namespace abp::upgrade::jukebox

ABP_REGISTER_UPGRADE(abp::upgrade::jukebox::JukeboxUpgrade);
ABP_REGISTER_UPGRADE(abp::upgrade::jukebox::AdvancedJukeboxUpgrade);
