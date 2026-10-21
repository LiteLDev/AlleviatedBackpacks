#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "ll/api/event/ListenerBase.h"

#include "core/Tier.h"
#include "game/Backpack.h"

class ItemStack;

namespace abp::game {

/// Persistent backpack storage, per world, keyed by uuid.
///
/// Data lives in the world's own LevelDB (`LevelStorage`, key `abp:backpack:<uuid>`), so it
/// travels with the world folder on BDS and in client local worlds. Loaded backpacks are cached;
/// dirty ones are written every 30 s, on world save/stop and on mod disable.
///
/// All calls must happen on the server thread while a level is loaded (`available()`).
class Storage {
public:
    static Storage& get();

    void install();
    void uninstall();

    /// A level (and its DB) is loaded.
    [[nodiscard]] bool available() const;

    /// Loads (or returns the cached) backpack. nullptr if unknown or unreadable.
    [[nodiscard]] std::shared_ptr<Backpack> find(std::string const& uuid);
    /// Creates and persists a new empty backpack of `tier` with a fresh uuid.
    [[nodiscard]] std::shared_ptr<Backpack> create(core::Tier tier);
    /// The backpack bound to a backpack item. If the item has no uuid yet (or its uuid is unknown in
    /// this world), a new backpack is created and its uuid is written into `stack`; then
    /// `*itemChanged` is set to true and the caller must write the stack back to where it came from.
    /// nullptr if `stack` is not a backpack.
    [[nodiscard]] std::shared_ptr<Backpack> forItem(ItemStack& stack, bool* itemChanged = nullptr);

    /// Writes one backpack now (if dirty or `force`).
    void save(Backpack& backpack, bool force = false);
    /// Writes every dirty cached backpack.
    void saveDirty();
    /// Deletes a backpack from the world (and the cache).
    void erase(std::string const& uuid);
    /// Saves and drops one backpack from the cache (the next `find` reads it from the DB).
    /// Holders of the old shared_ptr keep a detached copy: only use it when nobody else does.
    void unload(std::string const& uuid);

    [[nodiscard]] std::size_t loadedCount() const { return mCache.size(); }

    /// Drops cached backpacks nobody else references (after saving them).
    void evictUnused();

private:
    void clear(bool save);

    std::unordered_map<std::string, std::shared_ptr<Backpack>> mCache;
    std::vector<ll::event::ListenerPtr>                        mListeners;
    int                                                        mTicks = 0;
};

} // namespace abp::game
