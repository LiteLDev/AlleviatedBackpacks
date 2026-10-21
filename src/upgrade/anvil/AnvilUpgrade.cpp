// Anvil upgrade: an anvil tab for the backpack (repair, combine, enchanted books, rename) that never
// gets damaged. Inputs live in the upgrade's settings.

#include "upgrade/anvil/AnvilUpgrade.h"

#include <algorithm>

#include "ll/api/form/CustomForm.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"

#include "game/ItemConv.h"
#include "mod/I18n.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/anvil/AnvilMath.h"

namespace abp::upgrade::anvil {

namespace {
constexpr char const* kLeft  = "left";
constexpr char const* kRight = "right";
constexpr char const* kName  = "name";

StationRef refFor(UpgradeContext const& ctx) { return StationRef(ctx.withPlayer(nullptr), "anvil"); }
} // namespace

// ---- menu ---------------------------------------------------------------------------------------

AnvilMenu::AnvilMenu(std::string title, StationRef ref) : StationMenu(std::move(title), std::move(ref)) {}

std::optional<std::string> AnvilMenu::pendingName(StationRef const& ref) { return ref.text(kName); }

void AnvilMenu::setPendingName(StationRef const& ref, std::optional<std::string> const& name) { ref.setText(kName, name); }

AnvilOutcome const& AnvilMenu::outcome(Player& player) {
    auto rev      = ref().ctx().backpack().revision();
    bool creative = player.isCreative();
    if (!mCache || mCacheRevision != rev || mCacheCreative != creative) {
        mCache         = compute(ref().stack(kLeft), ref().stack(kRight), pendingName(ref()), creative);
        mCacheRevision = rev;
        mCacheCreative = creative;
    }
    return *mCache;
}

std::optional<OutputSlot::Crafted> AnvilMenu::craft(Player& player) {
    auto o = outcome(player); // copy: consuming the inputs invalidates the cache
    if (!o.hasResult()) return std::nullopt;
    if (!canAfford(player, o)) {
        player.sendMessage(
            o.tooExpensive ? trFor(player, "abp.anvil.too_expensive", "§cToo Expensive!")
                           : trFor(player, "abp.anvil.not_enough_levels", "§cYou need {} levels.", o.cost)
        );
        return std::nullopt;
    }
    auto const& r     = ref();
    auto        left  = r.stack(kLeft);
    auto        right = r.stack(kRight);
    auto        name  = pendingName(r);
    int         paid  = player.isCreative() ? 0 : o.cost;
    r.setItem(kLeft, {});
    r.consume(kRight, o.rightUsed);
    setPendingName(r, std::nullopt);
    if (paid > 0) player.addLevels(-paid);
    // The upgrade's anvil never takes damage (unlike the block).
    if (auto level = ll::service::getLevel()) {
        level->playSound(SharedTypes::Legacy::LevelSoundEvent::AnvilUse, player.getPosition(), 1.0f, 1.0f);
    }
    return OutputSlot::Crafted{o.result, [r, left, right, name, paid](Player& p) {
                                   r.setStack(kLeft, left);
                                   r.setStack(kRight, right);
                                   setPendingName(r, name);
                                   if (paid > 0) p.addLevels(paid);
                               }};
}

void AnvilMenu::buildStation(Player& player) {
    auto clearName = [r = ref()](Player&) { setPendingName(r, std::nullopt); };
    setSlot(kLeftSlot, std::make_shared<InputSlot>(ref(), kLeft, clearName));
    setSlot(3, filler("+"));
    setSlot(kRightSlot, std::make_shared<InputSlot>(ref(), kRight));

    auto cost       = std::make_shared<ui::CallbackSlot>();
    cost->onDisplay = [this](Player& p) -> ItemStack {
        auto const& o = outcome(p);
        if (o.hasResult() && o.tooExpensive) {
            return ui::icon("minecraft:barrier", trFor(p, "abp.anvil.too_expensive", "§cToo Expensive!"));
        }
        if (!o.hasResult()) return ui::icon("minecraft:gray_stained_glass_pane", trFor(p, "abp.anvil.no_result", "§7Anvil"));
        bool ok = canAfford(p, o);
        return ui::icon(
            "minecraft:experience_bottle",
            ok ? trFor(p, "abp.anvil.cost_ok", "§aCost: {} levels", o.cost)
               : trFor(p, "abp.anvil.cost_missing", "§cCost: {} levels", o.cost),
            {trFor(p, "abp.anvil.your_level", "§7Your level: {}", playerLevel(p))},
            std::clamp(o.cost, 1, 64)
        );
    };
    cost->onClick = [](ui::ClickContext const&) { return true; };
    setSlot(kCostSlot, cost);

    setSlot(
        kResultSlot,
        std::make_shared<OutputSlot>(
            *this,
            [this](Player& p) -> ItemStack {
                auto const& o = outcome(p);
                if (!o.hasResult() || o.tooExpensive) return {};
                ItemStack shown = o.result;
                auto      lore  = shown.getCustomLore();
                lore.push_back(
                    canAfford(p, o) ? trFor(p, "abp.anvil.cost_ok", "§aCost: {} levels", o.cost)
                                    : trFor(p, "abp.anvil.cost_missing", "§cCost: {} levels", o.cost)
                );
                shown.setCustomLore(lore);
                return shown;
            },
            [this](Player& p) { return craft(p); }
        )
    );

    mBuiltName = pendingName(ref());
    std::vector<std::string> lore{trFor(player, "abp.anvil.rename_hint", "§7Click to type a new name")};
    if (mBuiltName) lore.push_back(trFor(player, "abp.anvil.rename_current", "§7New name: §f{}", *mBuiltName));
    setSlot(
        kRenameSlot,
        std::make_shared<ui::ButtonSlot>(
            ui::icon("minecraft:name_tag", trFor(player, "abp.anvil.rename", "Rename"), lore),
            [r = ref()](ui::ClickContext const& c) { AnvilMenu::openRenameForm(c.player, r); }
        )
    );
    setSlot(
        13,
        std::make_shared<ui::StaticSlot>(ui::icon(
            "minecraft:anvil",
            trFor(player, "abp.anvil.help_title", "Anvil"),
            {trFor(player, "abp.anvil.help1", "§7Left: item to repair, enchant or rename"),
             trFor(player, "abp.anvil.help2", "§7Right: material, same item or enchanted book"),
             trFor(player, "abp.anvil.help3", "§7This anvil never breaks")}
        ))
    );
}

void AnvilMenu::onTick(Player& player) {
    StationMenu::onTick(player);
    if (pendingName(ref()) != mBuiltName) invalidate(); // refresh the rename button
}

int AnvilMenu::quickMoveStation(Player& player, ItemStack const& stack) {
    auto left = ref().stack(kLeft);
    if (left.isNull()) {
        if (!isAnvilSubject(stack)) return 0;
        InputSlot slot(ref(), kLeft, [r = ref()](Player&) { setPendingName(r, std::nullopt); });
        return slot.accept(player, stack);
    }
    // Only what the anvil can use with the left item (material, same item, book).
    if (!compute(left, stack, std::nullopt, true).hasResult()) return 0;
    InputSlot slot(ref(), kRight);
    return slot.accept(player, stack);
}

void AnvilMenu::openRenameForm(Player& player, StationRef ref) {
    if (ref.stack(kLeft).isNull()) {
        player.sendMessage(trFor(player, "abp.anvil.rename_empty", "§cPut an item into the left slot first."));
        return;
    }
    ui::MenuManager::get().closeThen(player, [ref](Player& p) {
        auto left = ref.stack(kLeft);
        if (left.isNull()) {
            if (ref.valid()) open(p, ref.ctx());
            return;
        }
        auto current = left.getCustomName();
        ll::form::CustomForm form(trFor(p, "abp.anvil.rename_title", "Rename item"));
        form.appendInput(
            "name",
            trFor(p, "abp.anvil.rename_label", "New name (leave empty to remove a custom name)"),
            current.empty() ? left.getName() : current,
            pendingName(ref).value_or(current)
        );
        form.sendTo(p, [ref](Player& pl, ll::form::CustomFormResult const& result, ll::form::FormCancelReason) {
            if (result && ref.valid() && !ref.stack(kLeft).isNull()) {
                auto it = result->find("name");
                if (it != result->end()) {
                    if (auto const* text = std::get_if<std::string>(&it->second)) {
                        setPendingName(ref, math::truncateUtf8(*text, kMaxNameLength));
                    }
                }
            }
            if (ref.valid()) open(pl, ref.ctx());
        });
    });
}

bool AnvilMenu::open(Player& player, UpgradeContext const& ctx) {
    return showMenu(player, std::make_shared<AnvilMenu>(trFor(player, "abp.anvil.title", "Anvil"), refFor(ctx)));
}

// ---- upgrade ------------------------------------------------------------------------------------

UpgradeInfo const& AnvilUpgrade::info() const {
    static UpgradeInfo const info{
        .id           = "anvil",
        .itemId       = "alleviatedbackpacks:anvil_upgrade",
        .nameKey      = "abp.upgrade.anvil.name",
        .nameFallback = "Anvil Upgrade",
        .hasSettings  = true,
        .toggleable   = false,
    };
    return info;
}

void AnvilUpgrade::openSettings(Player& player, UpgradeContext const& ctx) { AnvilMenu::open(player, ctx); }

std::vector<std::string> AnvilUpgrade::describe(Player& player, UpgradeContext const& ctx) const {
    return describeContents(player, refFor(ctx), {kLeft, kRight});
}

} // namespace abp::upgrade::anvil

ABP_REGISTER_UPGRADE(abp::upgrade::anvil::AnvilUpgrade);

ABP_REGISTER_BACKPACK_BUTTON(abp::ui::BackpackButton{
    .id      = "anvil",
    .order   = 70,
    .visible = [](abp::ui::BackpackMenuContext const& c) { return abp::upgrade::hasActive(*c.backpack, "anvil"); },
    .icon    = [](abp::ui::BackpackMenuContext const& c) {
        return abp::ui::icon("minecraft:anvil", abp::trFor(c.player, "abp.anvil.open", "Anvil"));
    },
    .onClick = [](abp::ui::BackpackMenuContext const& c, abp::ui::ClickContext const&) {
        if (auto ctx = abp::upgrade::anvil::findInstalled(c.backpack, "anvil")) {
            abp::upgrade::anvil::AnvilMenu::open(c.player, *ctx);
        }
    },
});
