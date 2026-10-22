#pragma once

// Sophisticated Backpacks' BackpackAccessLogger: every time a player opens a backpack GUI, the
// backpack's item id, uuid, the player's name, the backpack's display name, both colours, the time
// and `columnsTaken` are recorded (one record per backpack, the latest open wins). `/sbp list`,
// `/sbp give` and `/sbp removeNonPlayer` work from these records. Stored in the world's LevelDB
// (key `abp:accesslog`), like SB keeps them in its BackpackStorage saved data.

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

class Player;

namespace abp::game {
class Backpack;
}

namespace abp::admin {

struct AccessLogRecord {
    std::string   backpackItemRegistryName; ///< "alleviatedbackpacks:iron_backpack"
    std::string   backpackUuid;
    std::string   playerName;
    std::string   backpackName; ///< display name (custom name or the item's name)
    std::uint32_t clothColor   = 0; ///< ARGB
    std::uint32_t trimColor    = 0; ///< ARGB
    std::int64_t  accessTime   = 0; ///< unix epoch milliseconds
    int           columnsTaken = 0;

    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] static std::optional<AccessLogRecord> fromJson(nlohmann::json const& j);
    [[nodiscard]] bool operator==(AccessLogRecord const&) const = default;
};

class AccessLog {
public:
    static AccessLog& get();

    /// Records `player` opening `backpack` (item id `itemId`, display name `name`). Server thread.
    void logPlayerAccess(
        Player&                  player,
        std::string const&       itemId,
        game::Backpack const&    backpack,
        std::string const&       name
    );
    /// Same, with an explicit record (selftests, migration).
    void put(AccessLogRecord record);

    /// Player names with records (cached, refreshed at most every 30 s like SB).
    [[nodiscard]] std::set<std::string> playerNames();
    /// Records of one player (cached, refreshed at most every 30 s).
    [[nodiscard]] std::vector<AccessLogRecord> logsForPlayer(std::string const& playerName);
    [[nodiscard]] std::vector<AccessLogRecord> all() const;
    [[nodiscard]] std::vector<std::string>     uuids() const;
    [[nodiscard]] std::optional<AccessLogRecord> find(std::string const& uuid) const;
    [[nodiscard]] bool                         contains(std::string const& uuid) const;
    void                                       erase(std::string const& uuid);
    [[nodiscard]] std::size_t                  size() const;

    /// Forces the next `playerNames` / `logsForPlayer` to rebuild the cache (selftests).
    void invalidateCache() { mCacheTime = 0; }

    void load(); ///< world start
    void save(); ///< if changed
    void clear(); ///< world gone

    /// Bumps on every change (command suggestions follow it).
    [[nodiscard]] std::uint64_t revision() const { return mRevision; }

private:
    struct State;
    State& state() const;

    std::int64_t  mCacheTime = 0;
    std::uint64_t mRevision  = 0;
};

/// Epoch milliseconds now.
[[nodiscard]] std::int64_t epochMillis();

} // namespace abp::admin
