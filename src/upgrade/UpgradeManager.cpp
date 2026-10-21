#include "upgrade/UpgradeManager.h"

#include <algorithm>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerPickUpItemEvent.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/Storage.h"
#include "mod/Config.h"
#include "mod/GeneralConfig.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "ui/menu/MenuManager.h"

namespace abp::upgrade {

namespace {
constexpr std::string_view kSettingsKey = "abp_upgrade_settings";

std::string message(Player* player, std::string_view key, std::string_view fallback, std::string const& arg = {}) {
    return player ? trFor(*player, key, fallback, arg) : tr(key, fallback, arg);
}

/// Splits embedded settings out of an upgrade item: returns the clean item and the settings.
std::pair<core::ItemStackData, nlohmann::json> extractSettings(core::ItemStackData const& item) {
    auto settings = nlohmann::json::object();
    if (item.nbt().empty()) return {item, settings};
    auto stack = game::ItemConv::toStack(item, 1);
    if (!stack.mUserData || !stack.mUserData->contains(kSettingsKey, Tag::Type::String)) return {item, settings};
    std::string const& text = (*stack.mUserData)[kSettingsKey];
    auto               j    = nlohmann::json::parse(text, nullptr, false);
    if (j.is_object()) settings = std::move(j);
    auto tag = stack.mUserData->clone();
    tag->erase(kSettingsKey);
    if (tag->empty()) stack.setUserData(nullptr);
    else stack.setUserData(std::move(tag));
    auto clean  = game::ItemConv::toData(stack);
    clean.count = 1;
    return {clean, settings};
}

core::ItemStackData embedSettings(core::ItemStackData const& item, nlohmann::json const& settings) {
    if (!settings.is_object() || settings.empty()) return item;
    auto stack = game::ItemConv::toStack(item, 1);
    auto tag   = stack.mUserData ? stack.mUserData->clone() : std::make_unique<CompoundTag>();
    tag->putString(std::string(kSettingsKey), settings.dump());
    stack.setUserData(std::move(tag));
    auto out  = game::ItemConv::toData(stack);
    out.count = 1;
    return out;
}

bool sameGroupOrId(IUpgrade const& u, std::string_view name) { return u.info().id == name || u.info().groupOrId() == name; }

/// True if backpack `from` contains (at any depth) a backpack item bound to `target`.
bool reaches(std::string const& from, std::string const& target, int depth = 0) {
    if (depth > 16) return true; // absurdly deep: treat as a cycle
    auto bp = game::Storage::get().find(from);
    if (!bp) return false;
    auto const& inv = bp->inventory();
    for (int i = 0; i < inv.size(); ++i) {
        auto const& item = inv.at(i);
        if (item.empty() || !game::isBackpack(item)) continue;
        if (auto uuid = game::backpackUuid(item); uuid && (*uuid == target || reaches(*uuid, target, depth + 1))) {
            return true;
        }
    }
    return false;
}

/// Rebuilds the runtime hooks of a backpack: foundation rules + enabled upgrades' attach().
void buildHooks(game::Backpack& bp) {
    auto  self = bp.shared_from_this();
    auto& inv  = bp.inventory();
    // Foundation rule: no backpack-in-itself, no nesting / shulker boxes unless allowed.
    inv.addInsertFilter("abp:core", 1000, [weak = std::weak_ptr<game::Backpack>(self)](core::InsertQuery const& q) {
        auto backpack = weak.lock();
        if (!backpack) return core::InsertVerdict::Allow;
        auto const& cfg = config::get<GeneralConfig>();
        if (game::isBackpack(q.stack)) {
            if (!cfg.allowNesting && !backpack->hasCapability("allow_nesting")) return core::InsertVerdict::Deny;
            // Never itself, never a backpack that (indirectly) contains this one.
            if (auto uuid = game::backpackUuid(q.stack); uuid && (*uuid == backpack->uuid() || reaches(*uuid, backpack->uuid()))) {
                return core::InsertVerdict::Deny;
            }
        }
        if (game::isShulkerBox(q.stack.id) && !cfg.allowShulkerBoxes
            && !backpack->hasCapability("allow_shulker_boxes")) {
            return core::InsertVerdict::Deny;
        }
        return core::InsertVerdict::Allow;
    });
    auto const& slots = bp.data().upgrades;
    for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
        auto const& s = slots[static_cast<std::size_t>(i)];
        if (s.empty() || !s.enabled) continue;
        if (auto* u = byItemId(s.item.id)) {
            try {
                u->attach(UpgradeContext(self, i), inv);
            } catch (std::exception const& e) {
                log().error("upgrade {} attach failed: {}", u->info().id, e.what());
            }
        }
    }
}
} // namespace

// ---- registry ---------------------------------------------------------------------------------

std::vector<std::shared_ptr<IUpgrade>> const& all() { return UpgradeRegistry::entries(); }

IUpgrade* byId(std::string_view id) {
    for (auto const& u : all())
        if (u->info().id == id) return u.get();
    return nullptr;
}

IUpgrade* byItemId(std::string_view itemId) {
    for (auto const& u : all())
        if (u->info().itemId == itemId) return u.get();
    return nullptr;
}

ScopedUpgrade::ScopedUpgrade(std::shared_ptr<IUpgrade> upgrade) : mUpgrade(std::move(upgrade)) {
    UpgradeRegistry::add(mUpgrade);
}

ScopedUpgrade::~ScopedUpgrade() { std::erase(UpgradeRegistry::entries(), mUpgrade); }

void IUpgrade::openSettings(Player& player, UpgradeContext const& ctx) {
    if (auto menu = createSettingsMenu(player, ctx)) ui::MenuManager::get().open(player, std::move(menu));
}

// ---- manager -----------------------------------------------------------------------------------

IUpgrade* upgradeFor(core::ItemStackData const& item) { return item.empty() ? nullptr : byItemId(item.id); }

std::string displayName(IUpgrade const& upgrade, Player const* player) {
    auto const& i = upgrade.info();
    return player ? trFor(*player, i.nameKey, i.nameFallback) : tr(i.nameKey, i.nameFallback);
}

bool canInstall(
    std::shared_ptr<game::Backpack> const& backpack,
    int                                    slot,
    core::ItemStackData const&             item,
    Player*                                player,
    std::string*                           reason
) {
    std::string dummy;
    auto&       why   = reason ? *reason : dummy;
    auto&       slots = backpack->data().upgrades;
    if (slot < 0 || slot >= static_cast<int>(slots.size())) return false;
    if (!slots[static_cast<std::size_t>(slot)].empty()) {
        why = message(player, "abp.upgrade.error.slot_taken", "That upgrade slot is already used.");
        return false;
    }
    auto* type = upgradeFor(item);
    if (!type) {
        why = message(player, "abp.upgrade.error.not_upgrade", "Only upgrades can go into upgrade slots.");
        return false;
    }
    auto const& info  = type->info();
    int         count = 0;
    for (auto const& s : slots) {
        if (s.empty()) continue;
        auto* other = upgradeFor(s.item);
        if (!other) continue;
        if (other->info().groupOrId() == info.groupOrId()) ++count;
        bool conflict = std::any_of(info.conflicts.begin(), info.conflicts.end(), [&](std::string const& c) {
            return sameGroupOrId(*other, c);
        }) || std::any_of(other->info().conflicts.begin(), other->info().conflicts.end(), [&](std::string const& c) {
            return sameGroupOrId(*type, c);
        });
        if (conflict) {
            why = message(
                player,
                "abp.upgrade.error.conflict",
                "Can't be combined with {}.",
                displayName(*other, player)
            );
            return false;
        }
    }
    if (count >= info.maxPerBackpack) {
        why = message(player, "abp.upgrade.error.max", "This backpack already has the maximum of this upgrade.");
        return false;
    }
    return type->canInstall(UpgradeContext(backpack, slot, player), why);
}

namespace {
/// Puts `item` into the (free) slot and runs onInstall. No checks, no hook rebuild.
void installUnchecked(std::shared_ptr<game::Backpack> const& backpack, int slot, core::ItemStackData const& item, Player* player) {
    auto [clean, settings] = extractSettings(item.withCount(1));
    auto& s                = backpack->data().upgrades[static_cast<std::size_t>(slot)];
    s.item                 = clean;
    s.enabled              = true;
    s.settings             = std::move(settings);
    backpack->markDirty();
    UpgradeContext ctx(backpack, slot, player);
    auto*          type = upgradeFor(clean);
    try {
        type->onInstall(ctx);
    } catch (std::exception const& e) {
        log().error("upgrade {} onInstall failed: {}", type->info().id, e.what());
    }
}

/// Runs onRemove and empties the (occupied) slot. Returns the item with its settings embedded.
/// No checks, no hook rebuild.
core::ItemStackData removeUnchecked(std::shared_ptr<game::Backpack> const& backpack, int slot, Player* player) {
    auto&          s = backpack->data().upgrades[static_cast<std::size_t>(slot)];
    UpgradeContext ctx(backpack, slot, player);
    if (auto* type = upgradeFor(s.item)) {
        try {
            type->onRemove(ctx);
        } catch (std::exception const& e) {
            log().error("upgrade {} onRemove failed: {}", type->info().id, e.what());
        }
    }
    auto out = embedSettings(s.item, s.settings);
    s        = core::UpgradeSlot{};
    backpack->clearTransient(ctx.hookOwner());
    backpack->markDirty();
    return out;
}
} // namespace

bool install(
    std::shared_ptr<game::Backpack> const& backpack,
    int                                    slot,
    core::ItemStackData const&             item,
    Player*                                player,
    std::string*                           reason
) {
    if (!canInstall(backpack, slot, item, player, reason)) return false;
    installUnchecked(backpack, slot, item, player);
    backpack->rebuildHooks();
    return true;
}

std::optional<core::ItemStackData>
remove(std::shared_ptr<game::Backpack> const& backpack, int slot, Player* player, std::string* reason) {
    auto& slots = backpack->data().upgrades;
    if (slot < 0 || slot >= static_cast<int>(slots.size())) return std::nullopt;
    auto& s = slots[static_cast<std::size_t>(slot)];
    if (s.empty()) return std::nullopt;
    if (auto* type = upgradeFor(s.item)) {
        std::string dummy;
        if (!type->canRemove(UpgradeContext(backpack, slot, player), reason ? *reason : dummy)) return std::nullopt;
    }
    auto out = removeUnchecked(backpack, slot, player);
    backpack->rebuildHooks();
    return out;
}

std::optional<core::ItemStackData> replace(
    std::shared_ptr<game::Backpack> const& backpack,
    int                                    slot,
    core::ItemStackData const&             item,
    Player*                                player,
    std::string*                           reason
) {
    auto& slots = backpack->data().upgrades;
    if (slot < 0 || slot >= static_cast<int>(slots.size())) return std::nullopt;
    auto& s = slots[static_cast<std::size_t>(slot)];
    if (s.empty()) return std::nullopt;
    auto newItem = item.withCount(1);
    if (auto* type = upgradeFor(s.item)) {
        std::string dummy;
        if (!type->canReplaceWith(UpgradeContext(backpack, slot, player), newItem, reason ? *reason : dummy)) {
            return std::nullopt;
        }
    }
    // Check the new one as if the slot were free; put the old state back untouched on failure.
    auto saved = std::move(s);
    s          = core::UpgradeSlot{};
    bool ok    = canInstall(backpack, slot, newItem, player, reason);
    s          = std::move(saved);
    if (!ok) return std::nullopt;
    auto out = removeUnchecked(backpack, slot, player);
    installUnchecked(backpack, slot, newItem, player);
    backpack->rebuildHooks();
    return out;
}

void setEnabled(std::shared_ptr<game::Backpack> const& backpack, int slot, bool enabled) {
    auto& slots = backpack->data().upgrades;
    if (slot < 0 || slot >= static_cast<int>(slots.size())) return;
    auto& s = slots[static_cast<std::size_t>(slot)];
    if (s.empty() || s.enabled == enabled) return;
    s.enabled = enabled;
    backpack->markDirty();
    backpack->rebuildHooks();
}

void forEachInstalled(
    std::shared_ptr<game::Backpack> const&                       backpack,
    bool                                                         activeOnly,
    std::function<void(IUpgrade&, UpgradeContext const&)> const& fn
) {
    auto const& slots = backpack->data().upgrades;
    for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
        auto const& s = slots[static_cast<std::size_t>(i)];
        if (s.empty() || (activeOnly && !s.enabled)) continue;
        if (auto* u = upgradeFor(s.item)) fn(*u, UpgradeContext(backpack, i));
    }
}

bool hasActive(game::Backpack const& backpack, std::string_view idOrGroup) {
    for (auto const& s : backpack.data().upgrades) {
        if (s.empty() || !s.enabled) continue;
        if (auto* u = upgradeFor(s.item); u && sameGroupOrId(*u, idOrGroup)) return true;
    }
    return false;
}

int dispatchPickup(Player& player, core::ItemStackData& item) {
    int before = item.count;
    for (auto& [carried, bp] : game::carriedBackpacks(player, false, true)) {
        if (item.empty()) break;
        forEachInstalled(bp, true, [&](IUpgrade& u, UpgradeContext const& ctx) {
            if (item.empty() || !carried.upgradeActive(u.info().id, u.info().groupOrId())) return;
            try {
                u.onPickup(ctx.withPlayer(&player), item);
            } catch (std::exception const& e) {
                log().error("upgrade {} onPickup failed: {}", u.info().id, e.what());
            }
        });
    }
    if (item.count < 0) item.count = 0;
    return before - item.count;
}

// ---- module ------------------------------------------------------------------------------------

/// Installs the hook builder and the pickup dispatch.
class UpgradeModule {
public:
    void install() {
        game::Backpack::setHookBuilder(&buildHooks);
        mPickup = ll::event::EventBus::getInstance().emplaceListener<ll::event::PlayerPickUpItemEvent>(
            [](ll::event::PlayerPickUpItemEvent& ev) {
                auto& player = ev.self();
                if (player.isClientSide() || all().empty()) return;
                auto& stack = ev.itemActor().item();
                if (stack.isNull()) return;
                auto data  = game::ItemConv::toData(stack);
                int  taken = dispatchPickup(player, data);
                if (taken <= 0) return;
                if (data.empty()) {
                    ev.cancel();
                    ev.itemActor().remove();
                } else {
                    stack.set(data.count);
                }
            }
        );
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mPickup);
        game::Backpack::setHookBuilder(nullptr);
    }

private:
    ll::event::ListenerPtr mPickup;
};

} // namespace abp::upgrade

ABP_REGISTER_MODULE_ORDERED(abp::upgrade::UpgradeModule, abp::ModuleOrder::Upgrades);
