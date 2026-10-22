// Live backpack item lore + tint, refreshed while the backpack is carried and not open.

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

std::uint32_t const kDefaultMain   = core::BackpackData().mainColor;

std::string tierLabel(Player const& player, core::Tier tier) {
    auto i = static_cast<std::size_t>(tier);
    return trFor(player, "abp.tiers.name." + std::string(core::tierInfo(tier).id), kTierFallbacks[i]);
}

/// The item's current tint (0xRRGGBB), or -1.
int currentTint(ItemStack const& stack) {
    auto* item = stack.mItem.get();
    if (!item || !item->hasCustomColor(stack)) return -1;
    auto c  = stack.getColor();
    auto ch = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    return (ch(c.r) << 16) | (ch(c.g) << 8) | ch(c.b);
}

std::optional<int> tintMarker(ItemStack const& stack) {
    if (!stack.mUserData || !stack.mUserData->contains(kTintKey, Tag::Type::Int)) return std::nullopt;
    int v = (*stack.mUserData)[kTintKey];
    return v;
}

void setTintMarker(ItemStack& stack, int value) {
    auto tag = stack.mUserData ? stack.mUserData->clone() : std::make_unique<CompoundTag>();
    tag->putInt(std::string(kTintKey), value);
    stack.setUserData(std::move(tag));
}

/// Keeps the item tint equal to the stored main colour. Returns true if the stack changed.
bool syncTint(ItemStack& stack, game::Backpack& bp) {
    auto* item = stack.mItem.get();
    if (!item) return false;
    int  current = currentTint(stack);
    auto marker  = tintMarker(stack);
    auto& data   = bp.data();
    // A tint that differs from what we last applied came from a cauldron: adopt it. Without a
    // marker we never tinted this item: a tint on it is a cauldron dye unless we have a colour.
    if ((marker && *marker != current) || (!marker && current >= 0 && data.mainColor == kDefaultMain)) {
        data.mainColor = current < 0 ? kDefaultMain : (0xFF000000u | static_cast<std::uint32_t>(current));
        bp.markDirty();
    }
    // Always tinted (also with the default colour): the item colour is the main colour the
    // backpack model's cloth is drawn with (P2-11; vanilla clients get it this way too).
    int expected = static_cast<int>(data.mainColor & 0xFFFFFFu);
    bool changed = false;
    if (expected != current) {
        if (expected < 0) item->clearColor(stack);
        else item->setColor(stack, mce::Color(static_cast<uint>(expected)));
        changed = true;
    }
    if (!marker || *marker != expected) {
        setTintMarker(stack, expected);
        changed = true;
    }
    return changed;
}

} // namespace

std::string colorLabel(Player const& player, std::uint32_t argb) {
    if (auto dye = core::dyeFromColor(argb)) {
        auto const& info = core::dyeInfo(*dye);
        return trFor(player, "abp.tiers.color." + std::string(info.id), kDyeFallbacks[static_cast<std::size_t>(*dye)]);
    }
    return core::colorHex(argb);
}

std::vector<std::string> buildLore(Player const& player, core::Tier tier, game::Backpack const* bp) {
    auto const&              info = core::tierInfo(tier);
    std::vector<std::string> lore;
    lore.push_back("§r" + trFor(player, "abp.tiers.lore.tier", "§7Tier: §f{}", tierLabel(player, tier)));
    core::FillStats stats;
    if (bp) {
        stats = core::fillStats(bp->data().inventory);
    } else {
        stats.totalSlots = info.slots;
    }
    lore.push_back(
        "§r"
        + trFor(player, "abp.tiers.lore.storage", "§7Storage: §f{}/{} slots §8({}% full)", stats.usedSlots, stats.totalSlots, stats.percent)
    );
    int upgradeSlots = bp ? static_cast<int>(bp->data().upgrades.size()) : info.upgradeSlots;
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

namespace {
/// Lore cache: rebuilding it (fill stats over every slot, translations) only when the backpack
/// changed keeps the periodic refresh cheap.
struct CachedLore {
    std::uint64_t            revision = 0;
    core::Tier               tier     = core::Tier::Leather;
    std::string              locale;
    std::vector<std::string> lore;
};
std::unordered_map<std::string, CachedLore> gLoreCache;
constexpr std::size_t                       kLoreCacheMax = 4096;

std::vector<std::string> const& loreFor(Player const& player, core::Tier tier, game::Backpack const* bp) {
    static std::vector<std::string> scratch;
    if (!bp) return scratch = buildLore(player, tier, nullptr);
    auto locale = localeOf(player);
    if (auto it = gLoreCache.find(bp->uuid()); it != gLoreCache.end()) {
        auto& c = it->second;
        if (c.revision == bp->revision() && c.tier == tier && c.locale == locale) return c.lore;
    }
    if (gLoreCache.size() >= kLoreCacheMax) gLoreCache.clear();
    auto& c    = gLoreCache[bp->uuid()];
    c.revision = bp->revision();
    c.tier     = tier;
    c.locale   = std::move(locale);
    c.lore     = buildLore(player, tier, bp);
    return c.lore;
}
} // namespace

bool refreshItem(Player const& player, ItemStack& stack, std::shared_ptr<game::Backpack> const& bp) {
    auto tier = game::backpackTierOf(stack);
    if (!tier) return false;
    bool changed = false;
    // The Diamond backpack has durability only so Bedrock accepts it as a smithing-table base
    // (netherite upgrade). Backpacks never wear out.
    if (stack.isDamageableItem() && stack.getDamageValue() > 0) {
        stack.setDamageValue(0);
        changed = true;
    }
    if (bp) changed |= syncTint(stack, *bp);
    // The storage tier is the truth once bound (a higher item tier is applied on next open).
    auto shownTier = bp && static_cast<int>(bp->tier()) > static_cast<int>(*tier) ? bp->tier() : *tier;
    auto const& lore = loreFor(player, shownTier, bp.get());
    if (stack.getCustomLore() != lore) {
        stack.setCustomLore(lore);
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
        gLoreCache.clear();
    }

private:
    ll::event::ListenerPtr mListener;
    int                    mTicks = 0;
};

} // namespace abp::tiers

ABP_REGISTER_MODULE(abp::tiers::LoreRefresher);
