// Live backpack item lore + tint, refreshed while the backpack is carried and not open.

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"

#include "mc/deps/core/math/Color.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"

#include "core/Appearance.h"
#include "feature/admin/SbConfig.h"
#include "feature/tiers/Tiers.h"
#include "game/BackpackItem.h"
#include "game/PlayerScan.h"
#include "game/Storage.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"

namespace abp::tiers {

namespace {

constexpr int kRefreshTicks = 10;

constexpr std::string_view kTierFallbacks[] = {"Leather", "Copper", "Iron", "Gold", "Diamond", "Netherite"};
constexpr std::string_view kDyeFallbacks[]  = {
    "White", "Orange", "Magenta", "Light Blue", "Yellow", "Lime", "Pink",  "Gray",
    "Light Gray", "Cyan", "Purple", "Blue", "Brown", "Green", "Red", "Black",
};

std::uint32_t const kDefaultMain   = core::kDefaultMainColor;
std::uint32_t const kDefaultAccent = core::kDefaultAccentColor;

std::string tierLabel(Player const& player, core::Tier tier) {
    auto i = static_cast<std::size_t>(tier);
    return trFor(player, "abp.tiers.name." + std::string(core::tierInfo(tier).id), kTierFallbacks[i]);
}

/// The item's current tint (0xRRGGBB), or -1.
int currentTint(ItemStackBase const& stack) {
    auto* item = stack.mItem.get();
    if (!item || !item->hasCustomColor(stack)) return -1;
    auto c  = stack.getColor();
    auto ch = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    return (ch(c.r) << 16) | (ch(c.g) << 8) | ch(c.b);
}

std::optional<int> tintMarker(ItemStackBase const& stack) {
    if (!stack.mUserData || !stack.mUserData->contains(kTintKey, Tag::Type::Int)) return std::nullopt;
    int v = (*stack.mUserData)[kTintKey];
    return v;
}

void setTintMarker(ItemStackBase& stack, int value) {
    auto tag = stack.mUserData ? stack.mUserData->clone() : std::make_unique<CompoundTag>();
    tag->putInt(std::string(kTintKey), value);
    stack.setUserData(std::move(tag));
}

/// Keeps item colours, storage colours and the tint in sync. Returns true if the stack changed.
bool syncColors(ItemStack& stack, game::Backpack* bp) {
    bool changed  = false;
    auto stored   = itemColors(stack);
    auto defaults = defaultColors();
    auto colors   = colorsOf(stack, bp);
    int  current  = currentTint(stack);
    auto marker   = tintMarker(stack);
    // A tint that differs from what we last applied came from a cauldron: adopt it as the main
    // colour. Without a marker we never tinted this item: a tint on it is a cauldron dye unless
    // the backpack already has a main colour.
    if ((marker && *marker != current) || (!marker && current >= 0 && colors.main == defaults.main)) {
        // SB BackpackCauldronInteraction: washing resets both the main and the accent colour.
        if (current < 0) colors = defaults;
        else colors.main = 0xFF000000u | static_cast<std::uint32_t>(current);
        setItemColors(stack, colors);
        changed = true;
    } else if (!stored && colors != defaults) {
        setItemColors(stack, colors); // legacy item: its colours lived in the storage only
        changed = true;
    }
    if (bp && (bp->data().mainColor != colors.main || bp->data().accentColor != colors.accent)) {
        bp->data().mainColor   = colors.main;
        bp->data().accentColor = colors.accent;
        bp->markDirty();
    }
    changed |= applyTint(stack, colors);
    return changed;
}
} // namespace

BackpackColors defaultColors() { return {kDefaultMain, kDefaultAccent}; }

std::optional<BackpackColors> itemColors(ItemStackBase const& stack) {
    auto const& tag = stack.mUserData;
    if (!tag) return std::nullopt;
    bool hasMain = tag->contains(kMainColorKey, Tag::Type::Int), hasAccent = tag->contains(kAccentColorKey, Tag::Type::Int);
    if (!hasMain && !hasAccent) return std::nullopt;
    auto colors = defaultColors();
    if (hasMain) colors.main = 0xFF000000u | static_cast<std::uint32_t>(static_cast<int>((*tag)[kMainColorKey]));
    if (hasAccent) colors.accent = 0xFF000000u | static_cast<std::uint32_t>(static_cast<int>((*tag)[kAccentColorKey]));
    return colors;
}

BackpackColors colorsOf(ItemStackBase const& stack, game::Backpack const* backpack) {
    if (auto c = itemColors(stack)) return *c;
    if (backpack) return {backpack->data().mainColor, backpack->data().accentColor};
    return defaultColors();
}

void setItemColors(ItemStackBase& stack, BackpackColors colors) {
    auto tag = stack.mUserData ? stack.mUserData->clone() : std::make_unique<CompoundTag>();
    tag->putInt(std::string(kMainColorKey), static_cast<int>(colors.main | 0xFF000000u));
    tag->putInt(std::string(kAccentColorKey), static_cast<int>(colors.accent | 0xFF000000u));
    stack.setUserData(std::move(tag));
}

bool applyTint(ItemStackBase& stack, BackpackColors colors) {
    auto* item = stack.mItem.get();
    if (!item) return false;
    // Always tinted (also with the default colour): the item colour is the main colour the
    // backpack model's cloth is drawn with (P2-11; vanilla clients get it this way too).
    int  expected = static_cast<int>(colors.main & 0xFFFFFFu);
    bool changed  = false;
    if (expected != currentTint(stack)) {
        item->setColor(stack, mce::Color(static_cast<uint>(expected)));
        changed = true;
    }
    if (auto marker = tintMarker(stack); !marker || *marker != expected) {
        setTintMarker(stack, expected);
        changed = true;
    }
    return changed;
}

std::string colorLabel(Player const& player, std::uint32_t argb) {
    if (auto dye = core::dyeFromColor(argb)) {
        auto const& info = core::dyeInfo(*dye);
        return trFor(player, "abp.tiers.color." + std::string(info.id), kDyeFallbacks[static_cast<std::size_t>(*dye)]);
    }
    return core::colorHex(argb);
}

std::vector<std::string> buildLore(Player const& player, core::Tier tier, game::Backpack const* bp) {
    std::vector<std::string> lore;
    lore.push_back("§r" + trFor(player, "abp.tiers.lore.tier", "§7Tier: §f{}", tierLabel(player, tier)));
    core::FillStats stats;
    if (bp) {
        stats = core::fillStats(bp->data().inventory);
    } else {
        stats.totalSlots = admin::inventorySlotCount(tier); // SB inventorySlotCount (configurable)
    }
    lore.push_back(
        "§r"
        + trFor(player, "abp.tiers.lore.storage", "§7Storage: §f{}/{} slots §8({}% full)", stats.usedSlots, stats.totalSlots, stats.percent)
    );
    int upgradeSlots = bp ? static_cast<int>(bp->data().upgrades.size()) : admin::upgradeSlotCount(tier);
    std::vector<std::string> upgradeLines;
    int                      installed = 0;
    if (bp) {
        for (auto const& slot : bp->data().upgrades) {
            if (slot.empty()) continue;
            ++installed;
            auto* type = upgrade::upgradeFor(slot.item);
            auto  name = type ? upgrade::displayName(*type, &player) : slot.item.id;
            upgradeLines.push_back(
                "§r"
                + (slot.enabled ? trFor(player, "abp.tiers.lore.upgrade_on", "§8 - §a{}", name)
                                : trFor(player, "abp.tiers.lore.upgrade_off", "§8 - §7{} §8(off)", name))
            );
        }
    }
    lore.push_back("§r" + trFor(player, "abp.tiers.lore.upgrades", "§7Upgrades: §f{}/{}", installed, upgradeSlots));
    lore.insert(lore.end(), upgradeLines.begin(), upgradeLines.end());
    // The colours show on the 3D model and icon (SB has no colour lore).

    return lore;
}

bool refreshItem(Player const& /*player*/, ItemStack& stack, std::shared_ptr<game::Backpack> const& bp) {
    auto tier = game::backpackTierOf(stack);
    if (!tier) return false;
    bool changed = false;
    // Backpacks never wear out (Diamond backpacks made before GX-3 had durability as the
    // smithing-table base workaround; SmithingBase.cpp replaced it).
    if (stack.isDamageableItem() && stack.getDamageValue() > 0) {
        stack.setDamageValue(0);
        changed = true;
    }
    changed |= syncColors(stack, bp.get());
    // The storage tier is the truth once bound (a higher item tier is applied on next open).
    auto shownTier = bp && static_cast<int>(bp->tier()) > static_cast<int>(*tier) ? bp->tier() : *tier;
    // SB backpacks carry no lore (their tooltip is "Press <Left Shift> to View Contents" and the
    // Shift contents image, client side): the phase-1 lore (tier / storage / upgrades / colours,
    // every line starting with "§r") is removed from items that still have it.
    (void)shownTier;
    auto const& current = stack.getCustomLore();
    bool        ours    = !current.empty()
                  && std::all_of(current.begin(), current.end(), [](std::string const& l) { return l.starts_with("§r"); });
    if (ours) {
        stack.clearCustomLore();
        changed = true;
    }
    return changed;
}

int refreshPlayer(Player& player) {
    if (player.isClientSide() || !player.isAlive()) return 0;
    // Don't swap items under an open screen (ours or a vanilla container / the inventory): the
    // client may have item requests in flight. The lore catches up once it is closed.
    if (ui::MenuManager::get().current(player) || player.mContainerManager) return 0;
    auto& storage = game::Storage::get();
    if (!storage.available()) return 0;
    int rewritten = 0;
    for (auto const& c : game::scanBackpacks(player)) {
        std::shared_ptr<game::Backpack> bp;
        if (c.uuid) {
            bp = storage.find(*c.uuid);
            if (!bp) continue; // unknown / unreadable: leave the item alone
            // Apply a pending tier upgrade (item tier above stored tier) like opening would.
            if (static_cast<int>(c.tier) > static_cast<int>(bp->tier())) {
                ItemStack probe = game::itemAt(player, c.where);
                bp              = storage.forItem(probe);
                if (!bp) continue;
            }
        }
        ItemStack copy = game::itemAt(player, c.where);
        if (refreshItem(player, copy, bp)) {
            game::setItemAt(player, c.where, copy);
            ++rewritten;
        }
    }
    return rewritten;
}

/// Refreshes carried backpacks' lore every few ticks.
class LoreRefresher {
public:
    void install() {
        mListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>(
            [this](ll::event::ServerLevelTickEvent& ev) {
                if (++mTicks < kRefreshTicks) return;
                mTicks = 0;
                ev.level().forEachPlayer([](Player& player) {
                    try {
                        refreshPlayer(player);
                    } catch (std::exception const& e) {
                        log().error("backpack lore refresh failed: {}", e.what());
                    }
                    return true;
                });
            }
        );
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mListener);
    }

private:
    ll::event::ListenerPtr mListener;
    int                    mTicks = 0;
};

} // namespace abp::tiers

ABP_REGISTER_MODULE(abp::tiers::LoreRefresher);
