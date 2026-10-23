// Smithing upgrade (SB `upgrades/smithing`): a smithing-table tab for the backpack (netherite
// upgrades, armor trims and any other smithing_table recipe of the game). Inputs live in the
// upgrade's settings.
//
// Native tab data (`upgrades[i].tab.smithing`):
//   {"template":item|null,"base":item|null,"addition":item|null,"result":item|null}
// Tab actions: {"click":"template|base|addition|result","button":0|1,"shift":b}

#include "upgrade/smithing/SmithingUpgrade.h"

#include "ll/api/service/Bedrock.h"

#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"

#include "mod/I18n.h"
#include "mod/Log.h"
#include "net/NativeScreens.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/anvil/StationNative.h"
#include "upgrade/crafting/Station.h"

namespace abp::upgrade::smithing {

namespace {
constexpr char const* kTemplate = "template";
constexpr char const* kBase     = "base";
constexpr char const* kAddition = "addition";

anvil::StationRef refFor(UpgradeContext const& ctx) { return anvil::StationRef(ctx.withPlayer(nullptr), "smithing"); }

std::shared_ptr<anvil::InputSlot> inputSlot(anvil::StationRef const& ref, char const* key, Ingredient which) {
    return std::make_shared<anvil::InputSlot>(ref, key, anvil::InputSlot::Changed{}, [which](ItemStack const& s) {
        return isIngredient(s, which);
    });
}
} // namespace

// ---- station ------------------------------------------------------------------------------------

SmithingOutcome stationOutcome(anvil::StationRef const& ref) {
    return compute(ref.stack(kTemplate), ref.stack(kBase), ref.stack(kAddition));
}

std::optional<anvil::OutputSlot::Crafted> craft(Player& player, anvil::StationRef const& ref) {
    auto o = stationOutcome(ref);
    if (!o.hasResult()) return std::nullopt;
    auto templ = ref.stack(kTemplate);
    auto base  = ref.stack(kBase);
    auto addit = ref.stack(kAddition);
    ref.consume(kTemplate, 1);
    ref.consume(kBase, 1);
    ref.consume(kAddition, 1);
    if (auto level = ll::service::getLevel()) {
        level->playSound(SharedTypes::Legacy::LevelSoundEvent::SmithingTableUse, player.getPosition(), 1.0f, 1.0f);
    }
    return anvil::OutputSlot::Crafted{o.result, [ref, templ, base, addit](Player&) {
                                          ref.setStack(kTemplate, templ);
                                          ref.setStack(kBase, base);
                                          ref.setStack(kAddition, addit);
                                      }};
}

std::shared_ptr<anvil::InputSlot> templateSlot(anvil::StationRef const& ref) { return inputSlot(ref, kTemplate, Ingredient::Template); }
std::shared_ptr<anvil::InputSlot> baseSlot(anvil::StationRef const& ref) { return inputSlot(ref, kBase, Ingredient::Base); }
std::shared_ptr<anvil::InputSlot> additionSlot(anvil::StationRef const& ref) { return inputSlot(ref, kAddition, Ingredient::Addition); }

std::shared_ptr<anvil::OutputSlot> resultSlot(anvil::StationRef const& ref, std::optional<anvil::PendingUndo>& pending) {
    return std::make_shared<anvil::OutputSlot>(
        ref,
        pending,
        [ref](Player&) { return stationOutcome(ref).result; },
        [ref](Player& p) { return craft(p, ref); }
    );
}

// ---- chest menu ---------------------------------------------------------------------------------

SmithingMenu::SmithingMenu(std::string title, anvil::StationRef ref) : StationMenu(std::move(title), std::move(ref)) {}

SmithingOutcome const& SmithingMenu::outcome() {
    auto rev = ref().ctx().backpack().revision();
    if (!mCache || mCacheRevision != rev) {
        mCache         = stationOutcome(ref());
        mCacheRevision = rev;
    }
    return *mCache;
}

void SmithingMenu::buildStation(Player& player) {
    setSlot(kTemplateSlot, templateSlot(ref()));
    setSlot(kBaseSlot, baseSlot(ref()));
    setSlot(kAdditionSlot, additionSlot(ref()));
    setSlot(5, filler("->"));
    setSlot(kResultSlot, resultSlot(ref(), pendingUndo));
    (void)player;
}

int SmithingMenu::quickMoveStation(Player& player, ItemStack const& stack) {
    // Vanilla SmithingMenu: the first input slot whose rules take the item.
    for (auto const& slot : {templateSlot(ref()), baseSlot(ref()), additionSlot(ref())}) {
        if (int n = slot->accept(player, stack); n > 0) return n;
    }
    return 0;
}

bool SmithingMenu::open(Player& player, UpgradeContext const& ctx) {
    return anvil::showMenu(player, std::make_shared<SmithingMenu>(trFor(player, "abp.smithing.title", "Smithing"), refFor(ctx)));
}

// ---- upgrade ------------------------------------------------------------------------------------

UpgradeInfo const& SmithingUpgrade::info() const {
    static UpgradeInfo const info{
        .id           = "smithing",
        .itemId       = "alleviatedbackpacks:smithing_upgrade",
        .nameKey      = "abp.upgrade.smithing.name",
        .nameFallback = "Smithing Upgrade",
        .maxPerBackpack = kUnlimitedPerBackpack, // SB: no limit
        .hasSettings  = true,
        .toggleable   = false,
    };
    return info;
}

void SmithingUpgrade::openSettings(Player& player, UpgradeContext const& ctx) { SmithingMenu::open(player, ctx); }

std::vector<std::string> SmithingUpgrade::describe(Player& player, UpgradeContext const& ctx) const {
    return anvil::describeContents(player, refFor(ctx), {kTemplate, kBase, kAddition});
}

// ---- native tab ---------------------------------------------------------------------------------

namespace {

nlohmann::json tabData(Player& player, UpgradeContext const& ctx) {
    auto ref = refFor(ctx);
    return nlohmann::json{
        {"smithing",
         {
             {"template", anvil::stackJson(templateSlot(ref)->display(player))},
             {"base", anvil::stackJson(baseSlot(ref)->display(player))},
             {"addition", anvil::stackJson(additionSlot(ref)->display(player))},
             {"result", anvil::stackJson(stationOutcome(ref).result)},
         }}
    };
}

bool tabAction(Player& player, UpgradeContext const& ctx, nlohmann::json const& data) {
    if (!data.is_object()) return false;
    if (crafting::handleTabOpen(player, ctx, data)) {
        log().info("station tab: smithing {} by {}", data.value("tabOpen", false) ? "open" : "closed", player.getRealName());
        return true;
    }
    auto ref = refFor(ctx);
    if (!ref.valid()) return false;
    auto which = data.value("click", std::string{});
    std::optional<anvil::PendingUndo> pending;
    std::shared_ptr<ui::Slot>         slot;
    if (which == kTemplate) slot = templateSlot(ref);
    else if (which == kBase) slot = baseSlot(ref);
    else if (which == kAddition) slot = additionSlot(ref);
    else if (which == "result") slot = resultSlot(ref, pending);
    if (!slot) return false;
    bool ok = anvil::clickStationSlot(player, slot, data.value("button", 0), data.value("shift", false), ctx.backpack());
    ui::MenuManager::get().resyncPlayer(player);
    return ok;
}

/// SC mergeStackToOpenUpgradeTab: template, base, addition (each with the vanilla slot rules).
int quickMoveIn(Player& player, UpgradeContext const& ctx, core::ItemStackData const& item) {
    auto ref = refFor(ctx);
    return anvil::mergeIntoStation(player, {templateSlot(ref), baseSlot(ref), additionSlot(ref)}, item);
}

} // namespace

} // namespace abp::upgrade::smithing

ABP_REGISTER_UPGRADE(abp::upgrade::smithing::SmithingUpgrade);

ABP_REGISTER_TAB_DATA(abp::net::TabDataProvider{.upgrade = "smithing", .data = abp::upgrade::smithing::tabData});
ABP_REGISTER_TAB_ACTION(abp::net::TabActionHandler{.upgrade = "smithing", .handle = abp::upgrade::smithing::tabAction});
ABP_REGISTER_STATION_TAB(abp::upgrade::crafting::StationTab{.upgradeId = "smithing", .quickMoveIn = abp::upgrade::smithing::quickMoveIn});

ABP_REGISTER_BACKPACK_BUTTON(abp::ui::BackpackButton{
    .id      = "smithing",
    .order   = 71,
    .visible = [](abp::ui::BackpackMenuContext const& c) { return abp::upgrade::hasActive(*c.backpack, "smithing"); },
    .icon    = [](abp::ui::BackpackMenuContext const& c) {
        return abp::ui::icon("minecraft:smithing_table", abp::trFor(c.player, "abp.smithing.open", "Smithing"));
    },
    .onClick = [](abp::ui::BackpackMenuContext const& c, abp::ui::ClickContext const&) {
        if (auto ctx = abp::upgrade::anvil::findInstalled(c.backpack, "smithing")) {
            abp::upgrade::smithing::SmithingMenu::open(c.player, *ctx);
        }
    },
});
