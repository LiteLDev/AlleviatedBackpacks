#include "upgrade/stonecutter/StonecutterUpgrade.h"

#include <algorithm>

#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"

#include "game/ItemConv.h"
#include "mod/I18n.h"
#include "net/NativeScreens.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/crafting/Station.h"
#include "upgrade/stonecutter/RecentResults.h"

namespace abp::upgrade::stonecutter {

using crafting::StonecutterOption;

namespace {

/// Safety bound for one shift-click without refill (SC loops while the result stays the same).
constexpr int kMaxShiftCrafts = 64 * 4;

struct OptionCache {
    bool                           valid = false;
    nlohmann::json                 kind; ///< the input without its count
    std::vector<StonecutterOption> options;
};

nlohmann::json inputKind(UpgradeContext const& ctx) {
    auto const& s = ctx.settings();
    if (!s.is_object()) return nullptr;
    auto it = s.find(kInputKey);
    if (it == s.end() || !it->is_object()) return nullptr;
    auto kind = *it;
    kind.erase("count");
    return kind;
}

bool flag(UpgradeContext const& ctx, std::string_view key, bool def) {
    auto const& s = ctx.settings();
    if (!s.is_object()) return def;
    auto it = s.find(key);
    return it != s.end() && it->is_boolean() ? it->get<bool>() : def;
}

void setFlag(UpgradeContext const& ctx, std::string_view key, bool value) {
    auto& s = ctx.settings();
    if (!s.is_object()) s = nlohmann::json::object();
    s[std::string(key)] = value;
    ctx.saveSettings();
}

bool installedHere(UpgradeContext const& ctx) {
    auto const& slots = ctx.backpack().data().upgrades;
    if (ctx.slot() < 0 || ctx.slot() >= static_cast<int>(slots.size())) return false;
    auto const& s = slots[static_cast<std::size_t>(ctx.slot())];
    if (s.empty()) return false;
    auto* u = upgradeFor(s.item);
    return u && u->info().id == crafting::kStonecutterId;
}

} // namespace

// ---- model --------------------------------------------------------------------------------------

core::ItemStackData inputOf(UpgradeContext const& ctx) {
    auto const& s = ctx.settings();
    if (!s.is_object()) return {};
    auto it = s.find(kInputKey);
    if (it == s.end() || it->is_null()) return {};
    return core::ItemStackData::fromJson(*it);
}

void setInput(UpgradeContext const& ctx, core::ItemStackData const& item) {
    auto& s = ctx.settings();
    if (!s.is_object()) s = nlohmann::json::object();
    if (item.empty()) s.erase(std::string(kInputKey));
    else s[std::string(kInputKey)] = item.toJson();
    ctx.saveSettings();
}

bool shiftIntoStorage(UpgradeContext const& ctx) { return flag(ctx, kShiftIntoStorageKey, true); }
void setShiftIntoStorage(UpgradeContext const& ctx, bool value) { setFlag(ctx, kShiftIntoStorageKey, value); }
bool refillInput(UpgradeContext const& ctx) { return flag(ctx, kRefillKey, false); }
void setRefillInput(UpgradeContext const& ctx, bool value) { setFlag(ctx, kRefillKey, value); }

std::vector<StonecutterOption> const& options(UpgradeContext const& ctx) {
    auto& cache = ctx.transient<OptionCache>();
    auto  kind  = inputKind(ctx);
    if (cache.valid && cache.kind == kind) return cache.options;
    auto input    = inputOf(ctx);
    cache.options = input.empty() ? std::vector<StonecutterOption>{} : crafting::stonecutterOptions(game::ItemConv::toStack(input, 1));
    cache.kind    = std::move(kind);
    cache.valid   = true;
    return cache.options;
}

std::string selectedId(UpgradeContext const& ctx) {
    auto const& s = ctx.settings();
    if (!s.is_object()) return {};
    auto it = s.find(kSelectedKey);
    return it != s.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

int selectedIndex(UpgradeContext const& ctx) {
    auto id = selectedId(ctx);
    if (id.empty() || inputOf(ctx).empty()) return -1;
    auto const& list = options(ctx);
    for (int i = 0; i < static_cast<int>(list.size()); ++i) {
        if (list[static_cast<std::size_t>(i)].recipeId == id) return i;
    }
    return -1;
}

bool selectRecipe(UpgradeContext const& ctx, int index) {
    auto const& list = options(ctx);
    if (index < 0 || index >= static_cast<int>(list.size())) return false;
    auto& s = ctx.settings();
    if (!s.is_object()) s = nlohmann::json::object();
    s[std::string(kSelectedKey)] = list[static_cast<std::size_t>(index)].recipeId;
    ctx.saveSettings();
    return true;
}

std::optional<StonecutterOption> selectedOption(UpgradeContext const& ctx) {
    int i = selectedIndex(ctx);
    if (i < 0) return std::nullopt;
    return options(ctx)[static_cast<std::size_t>(i)];
}

namespace {
/// SC ResultSlot.onTake; `sound`: SC plays the take sound once per game tick.
ItemStack cut(UpgradeContext const& ctx, Player& player, bool sound) {
    auto option = selectedOption(ctx);
    auto input  = inputOf(ctx);
    if (!option || input.empty() || option->result.isNull()) return {};
    recordResult(player, kRecentScope, input.id, option->result.getTypeName());
    setInput(ctx, input.withCount(input.count - 1));
    // SC tryRefillInput: one more of the same input from the backpack storage.
    if (refillInput(ctx)) {
        auto  same = [&](core::ItemStackData const& d) { return d.sameItem(input); };
        auto& inv  = ctx.backpack().inventory();
        core::AccessContext access{core::Source::Upgrade, &player};
        if (inv.extractMatching(same, 1, access, true).count == 1) {
            auto got = inv.extractMatching(same, 1, access, false);
            auto cur = inputOf(ctx);
            setInput(ctx, cur.empty() ? got : cur.withCount(cur.count + got.count));
        }
    }
    if (sound) player.getLevel().playSound(SharedTypes::Legacy::LevelSoundEvent::UIStonecutterUse, player.getPosition(), 1.0f, 1.0f);
    return option->result;
}
} // namespace

ItemStack craftOnce(UpgradeContext const& ctx, Player& player) { return cut(ctx, player, true); }

int craftShift(UpgradeContext const& ctx, Player& player) {
    auto first = selectedOption(ctx);
    if (!first || first->result.isNull()) return 0;
    int  limit  = refillInput(ctx) ? std::max<int>(first->result.getMaxStackSize(), 1) : kMaxShiftCrafts;
    bool toBp   = shiftIntoStorage(ctx);
    auto target = first->result.getTypeName();
    int  crafts = 0;
    for (; crafts < limit; ++crafts) {
        auto option = selectedOption(ctx);
        if (!option || inputOf(ctx).empty() || option->result.getTypeName() != target) break;
        auto result = game::ItemConv::toData(option->result);
        auto r      = ctx.backpack().inventory().insert(result, core::AccessContext{core::Source::Player, &player}, true);
        if (r.inserted + r.voided + crafting::playerRoomFor(player, option->result) <= 0) break;
        auto out = cut(ctx, player, crafts == 0);
        if (out.isNull()) break;
        crafting::dropAt(player, crafting::moveOut(player, ctx.backpack(), game::ItemConv::toData(out), toBp));
    }
    return crafts;
}

bool shiftInput(UpgradeContext const& ctx, Player& player) {
    auto item = inputOf(ctx);
    if (item.empty()) return false;
    auto left = crafting::moveOut(player, ctx.backpack(), item, true);
    if (left.count == item.count) return false;
    setInput(ctx, left);
    return true;
}

int quickMoveIntoInput(UpgradeContext const& ctx, core::ItemStackData const& item) {
    if (item.empty() || !crafting::allowedInUpgradeSlot(ctx.backpack(), item)) return 0;
    auto cur = inputOf(ctx);
    int  max = std::max(item.maxStack, 1);
    int  n   = std::min(item.count, max);
    if (cur.empty()) {
        setInput(ctx, item.withCount(n));
        return n;
    }
    if (!cur.sameItem(item)) return 0;
    n = std::min(n, max - cur.count);
    if (n <= 0) return 0;
    setInput(ctx, cur.withCount(cur.count + n));
    return n;
}

std::vector<int> recentOrder(Player& player, UpgradeContext const& ctx) {
    auto const&      list = options(ctx);
    std::vector<int> out(list.size(), -1);
    auto             input = inputOf(ctx);
    if (input.empty()) return out;
    auto recent = recentResults(player, kRecentScope, input.id);
    for (std::size_t i = 0; i < list.size(); ++i) {
        auto id = list[i].result.getTypeName();
        auto it = std::find(recent.begin(), recent.end(), id);
        if (it != recent.end()) out[i] = static_cast<int>(it - recent.begin());
    }
    return out;
}

// ---- native tab ---------------------------------------------------------------------------------

namespace {

std::shared_ptr<crafting::StateSlot> inputSlot(UpgradeContext const& ctx) {
    return std::make_shared<crafting::StateSlot>(crafting::StateSlot::Ops{
        .get      = [ctx] { return inputOf(ctx); },
        .set      = [ctx](core::ItemStackData const& d) { setInput(ctx, d); },
        .mayPlace = [ctx](core::ItemStackData const& d) { return crafting::allowedInUpgradeSlot(ctx.backpack(), d); },
        .maxCount = {},
        .valid    = [ctx] { return installedHere(ctx); },
    });
}

std::shared_ptr<crafting::ResultSlot> outputSlot(UpgradeContext const& ctx) {
    return std::make_shared<crafting::ResultSlot>(
        ctx.backpack(),
        crafting::ResultSlot::Ops{
            .preview =
                [ctx] {
                    auto o = selectedOption(ctx);
                    return o ? o->result : ItemStack{};
                },
            .craftOnce = [ctx](Player& p) { return craftOnce(ctx, p); },
            .valid     = [ctx] { return installedHere(ctx); },
        }
    );
}

nlohmann::json itemJson(core::ItemStackData const& d) { return d.empty() ? nlohmann::json(nullptr) : d.toJson(); }

nlohmann::json tabData(Player& player, UpgradeContext const& ctx) {
    nlohmann::json recipes = nlohmann::json::array();
    for (auto const& o : options(ctx)) recipes.push_back(itemJson(game::ItemConv::toData(o.result)));
    auto output = selectedOption(ctx);
    return nlohmann::json{
        {"input",            itemJson(inputOf(ctx))                                                },
        {"output",           output ? itemJson(game::ItemConv::toData(output->result)) : nullptr},
        {"recipes",          std::move(recipes)                                                    },
        {"selected",         selectedIndex(ctx)                                                    },
        {"recent",           recentOrder(player, ctx)                                              },
        {"shiftIntoStorage", shiftIntoStorage(ctx)                                                 },
        {"refill",           refillInput(ctx)                                                      },
        {"tabLabel",         trFor(player, "abp.stonecutter.tab", "Stonecutter")                   },
        {"tabTooltip",       trFor(player, "abp.stonecutter.tab.tooltip", "Stonecutter")           },
    };
}

bool tabAction(Player& player, UpgradeContext const& ctx, nlohmann::json const& data) {
    if (crafting::handleTabOpen(player, ctx, data)) return true;
    int  button = data.value("button", 0);
    bool shift  = data.value("shift", false);
    if (data.contains("input")) {
        if (shift) {
            shiftInput(ctx, player);
            ui::MenuManager::get().resyncPlayer(player);
        } else {
            crafting::clickSlot(player, inputSlot(ctx), button);
        }
        return true;
    }
    if (data.contains("output")) {
        if (shift) {
            craftShift(ctx, player);
            ui::MenuManager::get().resyncPlayer(player);
        } else {
            crafting::clickSlot(player, outputSlot(ctx), button, true);
        }
        return true;
    }
    if (auto it = data.find("recipe"); it != data.end() && it->is_number_integer()) {
        return selectRecipe(ctx, it->get<int>());
    }
    auto toggle = data.value("toggle", std::string{});
    if (toggle == "shiftTarget") {
        setShiftIntoStorage(ctx, !shiftIntoStorage(ctx));
        return true;
    }
    if (toggle == "refill") {
        setRefillInput(ctx, !refillInput(ctx));
        return true;
    }
    return false;
}

// ---- upgrade ------------------------------------------------------------------------------------

class StonecutterUpgrade : public IUpgrade {
public:
    [[nodiscard]] UpgradeInfo const& info() const override {
        static UpgradeInfo const kInfo{
            .id           = std::string(crafting::kStonecutterId),
            .itemId       = "alleviatedbackpacks:stonecutter_upgrade",
            .nameKey      = "abp.stonecutter.name",
            .nameFallback = "Stonecutter Upgrade",
            .maxPerBackpack = kUnlimitedPerBackpack, // SB: no limit
            .hasSettings  = true,
            .toggleable   = false, // SC canBeDisabled() == false
        };
        return kInfo;
    }

    [[nodiscard]] std::vector<std::string> describe(Player& player, UpgradeContext const&) const override {
        return {trFor(player, "abp.stonecutter.tooltip", "§7Stonecutter in an upgrade tab")};
    }
};

} // namespace

} // namespace abp::upgrade::stonecutter

ABP_REGISTER_UPGRADE(abp::upgrade::stonecutter::StonecutterUpgrade);

ABP_REGISTER_TAB_DATA(abp::net::TabDataProvider{std::string(abp::upgrade::crafting::kStonecutterId), &abp::upgrade::stonecutter::tabData});
ABP_REGISTER_TAB_ACTION(abp::net::TabActionHandler{std::string(abp::upgrade::crafting::kStonecutterId), &abp::upgrade::stonecutter::tabAction});

ABP_REGISTER_STATION_TAB(abp::upgrade::crafting::StationTab{
    .upgradeId   = std::string(abp::upgrade::crafting::kStonecutterId),
    .quickMoveIn = [](Player&, abp::upgrade::UpgradeContext const& ctx, abp::core::ItemStackData const& item) {
        return abp::upgrade::stonecutter::quickMoveIntoInput(ctx, item);
    },
});
