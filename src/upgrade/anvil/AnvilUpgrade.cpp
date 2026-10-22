// Anvil upgrade (SB `upgrades/anvil`): an anvil tab for the backpack (repair, combine, enchanted
// books, rename) that never gets damaged. Inputs and the item name live in the upgrade's settings,
// so they stay with the upgrade (and travel with the upgrade item when it is removed).

#include "upgrade/anvil/AnvilUpgrade.h"

#include <algorithm>

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
constexpr char const* kLeft        = "left";
constexpr char const* kRight       = "right";
constexpr char const* kName        = "name";
constexpr char const* kNameDefault = "nameDefault";

StationRef refFor(UpgradeContext const& ctx) { return StationRef(ctx.withPlayer(nullptr), "anvil"); }

/// The item's name as the server shows it (custom name, else the item name).
std::string hoverName(ItemStack const& s) {
    auto custom = s.getCustomName();
    return custom.empty() ? s.getName() : custom;
}

bool isBlank(std::string const& s) {
    return std::all_of(s.begin(), s.end(), [](unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; });
}

/// SB PersistableAnvilMenu input slot 0 `setChanged`: the name box follows the left slot (the
/// item's name when an item is put into the empty slot, "" when the slot becomes empty).
void syncNameWithLeft(StationRef const& ref) {
    auto left = ref.stack(kLeft);
    if (left.isNull()) {
        ref.setText(kName, std::nullopt);
        ref.setText(kNameDefault, std::nullopt);
    } else if (itemName(ref).empty()) {
        ref.setText(kName, math::truncateUtf8(hoverName(left), kMaxNameLength));
        ref.setText(kNameDefault, std::nullopt);
    }
}
} // namespace

// ---- station ------------------------------------------------------------------------------------

std::string itemName(StationRef const& ref) { return ref.text(kName).value_or(std::string{}); }

void setItemName(StationRef const& ref, std::string const& name, std::optional<std::string> const& clientDefault) {
    if (ref.stack(kLeft).isNull()) return; // SB: the box is only editable with an item in the left slot
    ref.setText(kName, math::truncateUtf8(name, kMaxNameLength));
    if (clientDefault) ref.setText(kNameDefault, math::truncateUtf8(*clientDefault, kMaxNameLength));
}

std::optional<std::string> requestedName(StationRef const& ref) {
    auto left = ref.stack(kLeft);
    auto name = ref.text(kName);
    if (left.isNull() || !name) return std::nullopt;
    // Vanilla AnvilMenu.createResult: a blank name removes the custom name, the item's own
    // (hover) name changes nothing, anything else renames.
    if (isBlank(*name)) return std::string{};
    auto hover = math::truncateUtf8(hoverName(left), kMaxNameLength);
    if (*name == hover || *name == ref.text(kNameDefault).value_or(hover)) return std::nullopt;
    return name;
}

AnvilOutcome stationOutcome(Player& player, StationRef const& ref) {
    return compute(ref.stack(kLeft), ref.stack(kRight), requestedName(ref), player.isCreative());
}

std::optional<OutputSlot::Crafted> craft(Player& player, StationRef const& ref) {
    auto o = stationOutcome(player, ref);
    if (!o.hasResult() || !canAfford(player, o)) return std::nullopt;
    auto left    = ref.stack(kLeft);
    auto right   = ref.stack(kRight);
    auto name    = ref.text(kName);
    auto nameDef = ref.text(kNameDefault);
    int  paid    = player.isCreative() ? 0 : o.cost;
    ref.setItem(kLeft, {});
    ref.consume(kRight, o.rightUsed);
    syncNameWithLeft(ref);
    if (paid > 0) player.addLevels(-paid);
    // The upgrade's anvil never takes damage (unlike the block).
    if (auto level = ll::service::getLevel()) {
        level->playSound(SharedTypes::Legacy::LevelSoundEvent::AnvilUse, player.getPosition(), 1.0f, 1.0f);
    }
    return OutputSlot::Crafted{o.result, [ref, left, right, name, nameDef, paid](Player& p) {
                                   ref.setStack(kLeft, left);
                                   ref.setStack(kRight, right);
                                   ref.setText(kName, name);
                                   ref.setText(kNameDefault, nameDef);
                                   if (paid > 0) p.addLevels(paid);
                               }};
}

std::shared_ptr<InputSlot> leftSlot(StationRef const& ref) {
    return std::make_shared<InputSlot>(ref, kLeft, [ref](Player&) { syncNameWithLeft(ref); });
}

std::shared_ptr<InputSlot> rightSlot(StationRef const& ref) { return std::make_shared<InputSlot>(ref, kRight); }

std::shared_ptr<OutputSlot> resultSlot(StationRef const& ref, std::optional<PendingUndo>& pending) {
    return std::make_shared<OutputSlot>(
        ref,
        pending,
        [ref](Player& p) -> ItemStack {
            auto o = stationOutcome(p, ref);
            return o.hasResult() && !o.tooExpensive ? o.result : ItemStack{};
        },
        [ref](Player& p) { return craft(p, ref); }
    );
}

// ---- chest menu ---------------------------------------------------------------------------------

AnvilMenu::AnvilMenu(std::string title, StationRef ref) : StationMenu(std::move(title), std::move(ref)) {}

AnvilOutcome const& AnvilMenu::outcome(Player& player) {
    auto rev      = ref().ctx().backpack().revision();
    bool creative = player.isCreative();
    if (!mCache || mCacheRevision != rev || mCacheCreative != creative) {
        mCache         = stationOutcome(player, ref());
        mCacheRevision = rev;
        mCacheCreative = creative;
    }
    return *mCache;
}

void AnvilMenu::buildStation(Player& player) {
    setSlot(kLeftSlot, leftSlot(ref()));
    setSlot(3, filler("+"));
    setSlot(kRightSlot, rightSlot(ref()));

    auto cost       = std::make_shared<ui::CallbackSlot>();
    cost->onDisplay = [this](Player& p) -> ItemStack {
        auto const& o = outcome(p);
        if (o.hasResult() && o.tooExpensive) {
            return ui::icon("minecraft:barrier", trFor(p, "abp.anvil.too_expensive", "Too Expensive!"));
        }
        if (!o.hasResult()) return ui::icon("minecraft:gray_stained_glass_pane", " ");
        return ui::icon(
            "minecraft:experience_bottle",
            trFor(p, "abp.anvil.cost", "Enchantment Cost: {}", o.cost),
            {},
            std::clamp(o.cost, 1, 64)
        );
    };
    cost->onClick = [](ui::ClickContext const&) { return true; };
    setSlot(kCostSlot, cost);
    setSlot(kResultSlot, resultSlot(ref(), pendingUndo));
    (void)player;
}

int AnvilMenu::quickMoveStation(Player& player, ItemStack const& stack) {
    auto left = ref().stack(kLeft);
    if (left.isNull()) {
        if (!isAnvilSubject(stack)) return 0;
        return leftSlot(ref())->accept(player, stack);
    }
    // Only what the anvil can use with the left item (material, same item, book).
    if (!compute(left, stack, std::nullopt, true).hasResult()) return 0;
    return rightSlot(ref())->accept(player, stack);
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
        .maxPerBackpack = kUnlimitedPerBackpack, // SB: no limit
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
