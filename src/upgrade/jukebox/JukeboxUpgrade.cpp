// Jukebox upgrades (SC JukeboxUpgradeItem / JukeboxUpgradeWrapper): basic = 1 disc, Stop / Play;
// advanced = 12 discs (config 1..16), Previous / Stop / Play / Next, Shuffle and Repeat. Their
// native tab is client/ui/tabs/jukebox; the server menu below backs the tab's disc slots (and is
// the chest UI of simulated players in selftests).

#include <algorithm>

#include "ll/api/service/Bedrock.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"

#include "game/ItemConv.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "net/NativeScreens.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/jukebox/Jukebox.h"

namespace abp::upgrade::jukebox {

namespace {
constexpr std::string_view kBasicItem    = "alleviatedbackpacks:jukebox_upgrade";
constexpr std::string_view kAdvancedItem = "alleviatedbackpacks:advanced_jukebox_upgrade";

nlohmann::json const* discArray(UpgradeContext const& ctx) {
    auto const& s  = ctx.settings();
    auto        it = s.find("discs");
    return it != s.end() && it->is_array() ? &*it : nullptr;
}
} // namespace

// ---- settings ------------------------------------------------------------------------------------

int advancedSlots() { return std::clamp(config::get<JukeboxConfig>().advancedJukeboxUpgrade.numberOfSlots, 1, kMaxAdvancedSlots); }

bool isAdvanced(UpgradeContext const& ctx) { return ctx.state().item.id == kAdvancedItem; }

int slotsInRow(UpgradeContext const& ctx) {
    return isAdvanced(ctx) ? std::clamp(config::get<JukeboxConfig>().advancedJukeboxUpgrade.slotsInRow, 1, 6) : 4;
}

bool isJukebox(UpgradeContext const& ctx) {
    auto const& slots = ctx.backpack().data().upgrades;
    if (ctx.slot() < 0 || ctx.slot() >= static_cast<int>(slots.size())) return false;
    auto const& id = ctx.state().item.id;
    return !ctx.state().empty() && (id == kBasicItem || id == kAdvancedItem);
}

int discSlots(UpgradeContext const& ctx) {
    if (!isJukebox(ctx)) return 0;
    return isAdvanced(ctx) ? advancedSlots() : kBasicSlots;
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
    // SC JukeboxUpgradeContainer: a change of the playing disc slot stops the playback.
    Service::get().discChanged(ctx, index);
}

std::vector<bool> occupiedDiscs(UpgradeContext const& ctx) {
    int               n = discSlots(ctx);
    std::vector<bool> out(static_cast<std::size_t>(n), false);
    for (int i = 0; i < n; ++i) out[static_cast<std::size_t>(i)] = !disc(ctx, i).empty();
    return out;
}

bool shuffle(UpgradeContext const& ctx) {
    if (!isJukebox(ctx)) return false;
    auto const& s = ctx.settings();
    return s.contains("shuffle") && s["shuffle"].is_boolean() && s["shuffle"].get<bool>();
}

core::RepeatMode repeatMode(UpgradeContext const& ctx) {
    if (!isJukebox(ctx)) return core::RepeatMode::None;
    auto const& s = ctx.settings();
    if (s.contains("repeat") && s["repeat"].is_string()) {
        if (auto m = core::repeatModeFromId(s["repeat"].get<std::string>())) return *m;
    }
    return core::RepeatMode::None; // SC default: RepeatMode.NO
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

/// Menu slot of disc slot `i`: basic in the middle; advanced in two rows (6 or 8 per row).
int discMenuSlot(int i, int count) {
    if (count <= 1) return 22;
    int perRow = count <= 12 ? 6 : 8;
    int col0   = count <= 12 ? 1 : 0;
    return (i < perRow ? 9 : 18) + col0 + i % perRow;
}

/// The jukebox's server menu: disc slots and buttons. Companion players see the native SB tab
/// (client/ui/tabs/jukebox), which clicks these disc slots through the tab menu bridge.
class JukeboxMenu : public ui::Menu {
public:
    JukeboxMenu(UpgradeContext ctx, std::string title) : Menu(std::move(title), Rows::Six), mCtx(std::move(ctx)) {}

    void build(Player& player) override {
        for (int i = 0; i < size(); ++i) setSlot(i, std::make_shared<ui::StaticSlot>(ui::icon(kFiller, " ")));
        if (!isJukebox(mCtx)) return;
        int  n   = discSlots(mCtx);
        bool adv = isAdvanced(mCtx);
        for (int i = 0; i < n; ++i) setSlot(discMenuSlot(i, n), discSlot(i));

        auto& svc = Service::get();
        setSlot(45, std::make_shared<ui::ButtonSlot>(
            ui::icon("minecraft:smithing_table", trFor(player, "abp.jukebox.back", "Back to upgrades")),
            [bp = mCtx.backpackPtr()](ui::ClickContext const& c) {
                ui::BackpackMenu::openBackpack(c.player, bp, {}, ui::BackpackMenu::View::Upgrades);
            }
        ));
        setSlot(48, button(
            [](Player& p) { return ui::icon("minecraft:red_dye", trFor(p, "abp.jukebox.stop", "Stop")); },
            [this, &svc](Player&) { svc.stop(mCtx.backpack().uuid(), mCtx.slot()); }
        ));
        setSlot(49, button(
            [](Player& p) { return ui::icon("minecraft:lime_dye", trFor(p, "abp.jukebox.play", "Play")); },
            [this, &svc](Player& p) { (void)svc.play(mCtx.withPlayer(&p), &p); }
        ));
        if (!adv) return;
        setSlot(47, button(
            [](Player& p) { return ui::icon("minecraft:arrow", trFor(p, "abp.jukebox.previous", "Previous")); },
            [this, &svc](Player&) { svc.previous(mCtx); }
        ));
        setSlot(50, button(
            [](Player& p) { return ui::icon("minecraft:arrow", trFor(p, "abp.jukebox.next", "Next")); },
            [this, &svc](Player&) { svc.next(mCtx); }
        ));
        setSlot(52, button(
            [this](Player& p) {
                bool on = shuffle(mCtx);
                return ui::icon(
                    on ? "minecraft:ender_eye" : "minecraft:ender_pearl",
                    on ? trFor(p, "abp.jukebox.shuffle_on", "Shuffle Enabled") : trFor(p, "abp.jukebox.shuffle_off", "Shuffle Disabled")
                );
            },
            [this](Player&) { setShuffle(mCtx, !shuffle(mCtx)); }
        ));
        setSlot(53, button(
            [this](Player& p) {
                switch (repeatMode(mCtx)) {
                case core::RepeatMode::All:
                    return ui::icon("minecraft:repeater", trFor(p, "abp.jukebox.repeat_all", "Repeat All"));
                case core::RepeatMode::One:
                    return ui::icon("minecraft:comparator", trFor(p, "abp.jukebox.repeat_one", "Repeat One"));
                case core::RepeatMode::None:
                default:
                    return ui::icon("minecraft:barrier", trFor(p, "abp.jukebox.no_repeat", "Repeat Disabled"));
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
        if (st) v ^= (static_cast<std::uint64_t>(st->index + 1) << 48);
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
        s->onDisplay = [this, i](Player&) {
            auto d = disc(mCtx, i);
            return d.empty() ? ItemStack{} : game::ItemConv::toStack(d, 1);
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

    UpgradeContext mCtx;
};

// ---- upgrades ------------------------------------------------------------------------------------

class JukeboxUpgradeBase : public IUpgrade {
public:
    void onRemove(UpgradeContext const& ctx) override { Service::get().stop(ctx.backpack().uuid(), ctx.slot()); }

    std::shared_ptr<ui::Menu> createSettingsMenu(Player& player, UpgradeContext const& ctx) override {
        return std::make_shared<JukeboxMenu>(ctx.withPlayer(nullptr), trFor(player, "abp.jukebox.tab", "Jukebox"));
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
            .maxPerBackpack = kUnlimitedPerBackpack, // SB maxUpgradesPerStorage jukebox_upgrades
            .hasSettings    = true,
        };
        return i;
    }
    std::vector<std::string> describe(Player& player, UpgradeContext const&) const override {
        return {trFor(player, "abp.jukebox.tooltip", "§7Portable Jukebox")};
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
            .maxPerBackpack = kUnlimitedPerBackpack, // SB maxUpgradesPerStorage jukebox_upgrades
            .hasSettings    = true,
        };
        return i;
    }
    std::vector<std::string> describe(Player& player, UpgradeContext const&) const override {
        return {
            trFor(player, "abp.jukebox.advanced_tooltip1", "§7Portable Jukebox with support for more music discs"),
            trFor(player, "abp.jukebox.advanced_tooltip2", "§7Also more playback options"),
        };
    }
};

// ---- native tab data / actions (client/ui/tabs/jukebox) ------------------------------------------

nlohmann::json tabData(Player& player, UpgradeContext const& ctx) {
    if (!isJukebox(ctx)) return nlohmann::json::object();
    int            n   = discSlots(ctx);
    bool           adv = isAdvanced(ctx);
    nlohmann::json discs    = nlohmann::json::array();
    nlohmann::json discMenu = nlohmann::json::array();
    for (int i = 0; i < n; ++i) {
        auto d = disc(ctx, i);
        discs.push_back(d.empty() ? nlohmann::json(nullptr) : d.toJson());
        discMenu.push_back(discMenuSlot(i, n));
    }
    nlohmann::json j{
        {"advanced", adv},
        {"slotsInRow", slotsInRow(ctx)},
        {"discs", std::move(discs)},
        {"discMenu", std::move(discMenu)},
        {"shuffle", shuffle(ctx)},
        {"repeat", std::string(core::repeatModeId(repeatMode(ctx)))},
        {"playing", -1},
        // SC tab label / closed tab tooltip.
        {"tabLabel", trFor(player, "abp.jukebox.tab", "Jukebox")},
        {"tabTooltip", adv ? trFor(player, "abp.jukebox.tab.advanced_tooltip", "Advanced Jukebox") : trFor(player, "abp.jukebox.tab.tooltip", "Jukebox")},
    };
    if (auto st = Service::get().status(ctx.backpack().uuid(), ctx.slot())) {
        j["playing"]  = st->index;
        j["elapsed"]  = st->elapsedTicks / 20 * 20; // whole seconds: one screen patch per second
        j["duration"] = st->durationTicks;
    }
    auto title   = j["tabLabel"];
    auto tooltip = j["tabTooltip"];
    return nlohmann::json{{"jukebox", std::move(j)}, {"tabLabel", std::move(title)}, {"tabTooltip", std::move(tooltip)}};
}

bool tabAction(Player& player, UpgradeContext const& ctx, nlohmann::json const& data) {
    if (!isJukebox(ctx) || !data.contains("jukebox")) return false;
    auto  op  = data.value("jukebox", std::string{});
    auto& svc = Service::get();
    auto  c   = ctx.withPlayer(&player);
    if (op == "play") (void)svc.play(c, &player);
    else if (op == "stop") svc.stop(ctx.backpack().uuid(), ctx.slot());
    else if (op == "next" && isAdvanced(ctx)) (void)svc.next(c);
    else if (op == "previous" && isAdvanced(ctx)) (void)svc.previous(c);
    else if (op == "shuffle" && isAdvanced(ctx)) setShuffle(c, !shuffle(c));
    else if (op == "repeat" && isAdvanced(ctx)) setRepeatMode(c, core::nextRepeatMode(repeatMode(c)));
    else return false;
    return true;
}

} // namespace

} // namespace abp::upgrade::jukebox

ABP_REGISTER_UPGRADE(abp::upgrade::jukebox::JukeboxUpgrade);
ABP_REGISTER_UPGRADE(abp::upgrade::jukebox::AdvancedJukeboxUpgrade);
ABP_REGISTER_TAB_DATA(abp::net::TabDataProvider{"jukebox", &abp::upgrade::jukebox::tabData});
ABP_REGISTER_TAB_ACTION(abp::net::TabActionHandler{"jukebox", &abp::upgrade::jukebox::tabAction});
