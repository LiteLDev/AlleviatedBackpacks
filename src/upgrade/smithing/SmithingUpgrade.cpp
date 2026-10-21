// Smithing upgrade: a smithing-table tab for the backpack (netherite upgrades, armor trims and any
// other smithing_table recipe of the game). Inputs live in the upgrade's settings.

#include "upgrade/smithing/SmithingUpgrade.h"

#include "ll/api/service/Bedrock.h"

#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"

#include "mod/I18n.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"

namespace abp::upgrade::smithing {

namespace {
constexpr char const* kTemplate = "template";
constexpr char const* kBase     = "base";
constexpr char const* kAddition = "addition";

anvil::StationRef refFor(UpgradeContext const& ctx) { return anvil::StationRef(ctx.withPlayer(nullptr), "smithing"); }
} // namespace

SmithingMenu::SmithingMenu(std::string title, anvil::StationRef ref) : StationMenu(std::move(title), std::move(ref)) {}

SmithingOutcome const& SmithingMenu::outcome() {
    auto rev = ref().ctx().backpack().revision();
    if (!mCache || mCacheRevision != rev) {
        mCache         = compute(ref().stack(kTemplate), ref().stack(kBase), ref().stack(kAddition));
        mCacheRevision = rev;
    }
    return *mCache;
}

std::optional<anvil::OutputSlot::Crafted> SmithingMenu::craft(Player& player) {
    auto o = outcome(); // copy: consuming the inputs invalidates the cache
    if (!o.hasResult()) return std::nullopt;
    auto const& r     = ref();
    auto        templ = r.stack(kTemplate);
    auto        base  = r.stack(kBase);
    auto        addit = r.stack(kAddition);
    r.consume(kTemplate, 1);
    r.consume(kBase, 1);
    r.consume(kAddition, 1);
    if (auto level = ll::service::getLevel()) {
        level->playSound(SharedTypes::Legacy::LevelSoundEvent::SmithingTableUse, player.getPosition(), 1.0f, 1.0f);
    }
    return anvil::OutputSlot::Crafted{o.result, [r, templ, base, addit](Player&) {
                                          r.setStack(kTemplate, templ);
                                          r.setStack(kBase, base);
                                          r.setStack(kAddition, addit);
                                      }};
}

void SmithingMenu::buildStation(Player& player) {
    setSlot(kTemplateSlot, std::make_shared<anvil::InputSlot>(ref(), kTemplate));
    setSlot(kBaseSlot, std::make_shared<anvil::InputSlot>(ref(), kBase));
    setSlot(kAdditionSlot, std::make_shared<anvil::InputSlot>(ref(), kAddition));
    setSlot(5, filler("->"));
    setSlot(
        kResultSlot,
        std::make_shared<anvil::OutputSlot>(
            *this,
            [this](Player&) { return outcome().result; },
            [this](Player& p) { return craft(p); }
        )
    );
    setSlot(
        13,
        std::make_shared<ui::StaticSlot>(ui::icon(
            "minecraft:smithing_table",
            trFor(player, "abp.smithing.help_title", "Smithing"),
            {trFor(player, "abp.smithing.help1", "§7Template, base item and material"),
             trFor(player, "abp.smithing.help2", "§7Upgrade gear to netherite or add armor trims")}
        ))
    );
}

int SmithingMenu::quickMoveStation(Player& player, ItemStack const& stack) {
    auto tryKey = [&](char const* key) {
        anvil::InputSlot slot(ref(), key);
        return slot.accept(player, stack);
    };
    if (isTemplate(stack)) return tryKey(kTemplate);
    // Gear (tools, armor) is the base; materials (ingots, gems, ...) are the addition.
    if (stack.isDamageableItem()) return ref().item(kBase).empty() ? tryKey(kBase) : 0;
    return tryKey(kAddition);
}

bool SmithingMenu::open(Player& player, UpgradeContext const& ctx) {
    return anvil::showMenu(player, std::make_shared<SmithingMenu>(trFor(player, "abp.smithing.title", "Smithing"), refFor(ctx)));
}

UpgradeInfo const& SmithingUpgrade::info() const {
    static UpgradeInfo const info{
        .id           = "smithing",
        .itemId       = "alleviatedbackpacks:smithing_upgrade",
        .nameKey      = "abp.upgrade.smithing.name",
        .nameFallback = "Smithing Upgrade",
        .hasSettings  = true,
        .toggleable   = false,
    };
    return info;
}

void SmithingUpgrade::openSettings(Player& player, UpgradeContext const& ctx) { SmithingMenu::open(player, ctx); }

std::vector<std::string> SmithingUpgrade::describe(Player& player, UpgradeContext const& ctx) const {
    return anvil::describeContents(player, refFor(ctx), {kTemplate, kBase, kAddition});
}

} // namespace abp::upgrade::smithing

ABP_REGISTER_UPGRADE(abp::upgrade::smithing::SmithingUpgrade);

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
