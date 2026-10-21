#include "game/Storage.h"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/server/ServerStartedEvent.h"
#include "ll/api/event/server/ServerStoppingEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/storage/LevelStorage.h"
#include "mc/world/level/storage/db_helpers/Category.h"

#include "core/Codec.h"
#include "game/BackpackItem.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace abp::game {

namespace {
constexpr int          kAutosaveTicks   = 20 * 30;
constexpr std::int64_t kEvictAfterTicks = 20 * 60 * 5; ///< unused for 5 minutes

std::string keyOf(std::string const& uuid) { return "abp:backpack:" + uuid; }

LevelStorage* levelStorage() {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    return &level->getLevelStorage();
}
} // namespace

Storage& Storage::get() {
    static Storage instance;
    return instance;
}

bool Storage::available() const { return levelStorage() != nullptr; }

std::shared_ptr<Backpack> Storage::find(std::string const& uuid, LoadStatus* status) {
    auto result = [&](LoadStatus s, std::shared_ptr<Backpack> bp = nullptr) {
        if (status) *status = s;
        return bp;
    };
    if (uuid.empty()) return result(LoadStatus::Missing);
    if (auto it = mCache.find(uuid); it != mCache.end()) {
        it->second.lastAccess = mNow;
        return result(LoadStatus::Ok, it->second.backpack);
    }
    auto* db = levelStorage();
    if (!db) return result(LoadStatus::NoLevel);
    std::string buffer;
    if (!db->loadData(keyOf(uuid), buffer, DBHelpers::Category::Uncategorized)) return result(LoadStatus::Missing);
    std::optional<core::BackpackData> data;
    try {
        data = core::BackpackData::deserialize(buffer);
    } catch (std::exception const& e) {
        log().error("backpack {}: {}", uuid, e.what());
    }
    if (!data) {
        log().error("backpack {}: stored data is unreadable (kept untouched in the world DB)", uuid);
        return result(LoadStatus::Unreadable);
    }
    data->uuid = uuid;
    auto bp    = std::make_shared<Backpack>(std::move(*data));
    bp->rebuildHooks();
    bp->clearDirty();
    mCache.emplace(uuid, Entry{bp, mNow});
    return result(LoadStatus::Ok, bp);
}

std::shared_ptr<Backpack> Storage::create(core::Tier tier) {
    if (!available()) return nullptr;
    auto bp = std::make_shared<Backpack>(core::BackpackData::create(core::newUuid(), tier));
    bp->rebuildHooks();
    mCache.emplace(bp->uuid(), Entry{bp, mNow});
    save(*bp, true);
    log().debug("backpack {} created ({})", bp->uuid(), core::tierInfo(tier).id);
    return bp;
}

std::shared_ptr<Backpack> Storage::forItem(ItemStack& stack, bool* itemChanged) {
    if (itemChanged) *itemChanged = false;
    auto tier = backpackTierOf(stack);
    if (!tier) return nullptr;
    if (auto uuid = backpackUuid(stack)) {
        LoadStatus status = LoadStatus::Missing;
        if (auto bp = find(*uuid, &status)) {
            // The item was upgraded to a higher tier (tier upgrades swap the item id): grow the
            // storage. The stored tier is the truth otherwise (a lower-tier item id never shrinks it).
            if (static_cast<int>(*tier) > static_cast<int>(bp->tier())) {
                bp->data().setTier(*tier);
                bp->markDirty();
                bp->rebuildHooks();
            }
            return bp;
        }
        if (status != LoadStatus::Missing) return nullptr; // unreadable / no level: don't orphan the data
        log().warn("backpack item references unknown uuid {}; creating a new backpack", *uuid);
    }
    auto bp = create(*tier);
    if (!bp) return nullptr;
    setBackpackUuid(stack, bp->uuid());
    if (itemChanged) *itemChanged = true;
    return bp;
}

void Storage::save(Backpack& bp, bool force) {
    if (!force && !bp.dirty()) return;
    auto* db = levelStorage();
    if (!db) return;
    db->saveData(keyOf(bp.uuid()), bp.data().serialize(), DBHelpers::Category::Uncategorized);
    bp.clearDirty();
}

void Storage::saveDirty() {
    for (auto& [_, e] : mCache) save(*e.backpack);
}

void Storage::erase(std::string const& uuid) {
    mCache.erase(uuid);
    if (auto* db = levelStorage()) db->deleteData(keyOf(uuid), DBHelpers::Category::Uncategorized);
}

void Storage::unload(std::string const& uuid) {
    auto it = mCache.find(uuid);
    if (it == mCache.end()) return;
    save(*it->second.backpack);
    mCache.erase(it);
}

void Storage::evictUnused() {
    for (auto it = mCache.begin(); it != mCache.end();) {
        auto& e = it->second;
        if (e.backpack.use_count() == 1 && mNow - e.lastAccess > kEvictAfterTicks) {
            save(*e.backpack);
            it = mCache.erase(it);
        } else {
            ++it;
        }
    }
}
void Storage::clear(bool doSave) {
    if (doSave) saveDirty();
    mCache.clear();
}

void Storage::install() {
    auto& bus = ll::event::EventBus::getInstance();
    // A new world (client: every local world load) starts with an empty cache.
    mListeners.push_back(bus.emplaceListener<ll::event::ServerStartedEvent>([this](auto&) {
        clear(false);
        log().info("AlleviatedBackpacks world ready");
    }));
    // BDS enables mods after the server started: the world is already there.
    if (available()) log().info("AlleviatedBackpacks world ready");
    mListeners.push_back(bus.emplaceListener<ll::event::ServerStoppingEvent>([this](auto&) {
        clear(true);
        log().debug("backpacks saved (world stopping)");
    }));
    mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([this](auto&) {
        ++mNow;
        if (++mTicks < kAutosaveTicks) return;
        mTicks = 0;
        saveDirty();
        evictUnused();
    }));
}

void Storage::uninstall() {
    auto& bus = ll::event::EventBus::getInstance();
    for (auto& l : mListeners) bus.removeListener(l);
    mListeners.clear();
    clear(available());
}

} // namespace abp::game

ABP_REGISTER_MODULE_ORDERED(abp::game::Storage, abp::ModuleOrder::Storage);
