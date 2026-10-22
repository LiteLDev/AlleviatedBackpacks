#include "feature/admin/AccessLog.h"

#include <chrono>
#include <map>
#include <unordered_map>

#include "ll/api/service/Bedrock.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/storage/LevelStorage.h"
#include "mc/world/level/storage/db_helpers/Category.h"

#include "game/Backpack.h"
#include "mod/Log.h"

namespace abp::admin {

namespace {
constexpr std::string_view kDbKey             = "abp:accesslog";
constexpr std::int64_t     kRefreshIntervalMs = 30 * 1000; ///< SB: REFRESH_INTERVAL_SECONDS = 30

LevelStorage* db() {
    auto level = ll::service::getLevel();
    return level ? &level->getLevelStorage() : nullptr;
}
} // namespace

std::int64_t epochMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

nlohmann::json AccessLogRecord::toJson() const {
    return {
        {"backpackItemRegistryName", backpackItemRegistryName},
        {"backpackUuid",             backpackUuid            },
        {"playerName",               playerName              },
        {"backpackName",             backpackName            },
        {"clothColor",               clothColor              },
        {"trimColor",                trimColor               },
        {"accessTime",               accessTime              },
        {"columnsTaken",             columnsTaken            },
    };
}

std::optional<AccessLogRecord> AccessLogRecord::fromJson(nlohmann::json const& j) {
    if (!j.is_object()) return std::nullopt;
    try {
        AccessLogRecord r;
        r.backpackItemRegistryName = j.value("backpackItemRegistryName", std::string{});
        r.backpackUuid             = j.value("backpackUuid", std::string{});
        r.playerName               = j.value("playerName", std::string{});
        r.backpackName             = j.value("backpackName", std::string{});
        r.clothColor               = j.value("clothColor", 0u);
        r.trimColor                = j.value("trimColor", 0u);
        r.accessTime               = j.value("accessTime", std::int64_t{0});
        r.columnsTaken             = j.value("columnsTaken", 0);
        if (r.backpackUuid.empty()) return std::nullopt;
        return r;
    } catch (nlohmann::json::exception const&) {
        return std::nullopt;
    }
}

struct AccessLog::State {
    std::map<std::string, AccessLogRecord>                      records; ///< by backpack uuid
    std::unordered_map<std::string, std::vector<AccessLogRecord>> playerCache;
    bool                                                         dirty  = false;
    bool                                                         loaded = false;
};

AccessLog& AccessLog::get() {
    static AccessLog instance;
    return instance;
}

AccessLog::State& AccessLog::state() const {
    static State s;
    return s;
}

void AccessLog::logPlayerAccess(Player& player, std::string const& itemId, game::Backpack const& backpack, std::string const& name) {
    if (player.isClientSide()) return;
    AccessLogRecord r;
    r.backpackItemRegistryName = itemId;
    r.backpackUuid             = backpack.uuid();
    r.playerName               = player.getRealName();
    r.backpackName             = name;
    r.clothColor               = backpack.data().mainColor;
    r.trimColor                = backpack.data().accentColor;
    r.accessTime               = epochMillis();
    auto const& settings       = backpack.data().settings;
    if (settings.is_object()) {
        if (auto it = settings.find("columnsTaken"); it != settings.end() && it->is_number_integer()) {
            r.columnsTaken = it->get<int>();
        }
    }
    put(std::move(r));
}

void AccessLog::put(AccessLogRecord record) {
    auto& s = state();
    auto  it = s.records.find(record.backpackUuid);
    bool  isNew = it == s.records.end();
    s.records[record.backpackUuid] = std::move(record);
    s.dirty = true;
    if (isNew) ++mRevision; // new uuid / player name for the command suggestions
}

std::set<std::string> AccessLog::playerNames() {
    auto& s   = state();
    auto  now = epochMillis();
    if (mCacheTime + kRefreshIntervalMs < now) {
        mCacheTime = now;
        s.playerCache.clear();
        for (auto const& [_, r] : s.records) s.playerCache[r.playerName].push_back(r);
    }
    std::set<std::string> out;
    for (auto const& [name, _] : s.playerCache) out.insert(name);
    return out;
}

std::vector<AccessLogRecord> AccessLog::logsForPlayer(std::string const& playerName) {
    (void)playerNames(); // refreshes the cache when it is older than 30 s
    auto& s  = state();
    auto  it = s.playerCache.find(playerName);
    return it == s.playerCache.end() ? std::vector<AccessLogRecord>{} : it->second;
}

std::vector<AccessLogRecord> AccessLog::all() const {
    std::vector<AccessLogRecord> out;
    for (auto const& [_, r] : state().records) out.push_back(r);
    return out;
}

std::vector<std::string> AccessLog::uuids() const {
    std::vector<std::string> out;
    for (auto const& [uuid, _] : state().records) out.push_back(uuid);
    return out;
}

std::optional<AccessLogRecord> AccessLog::find(std::string const& uuid) const {
    auto const& s  = state();
    auto        it = s.records.find(uuid);
    if (it == s.records.end()) return std::nullopt;
    return it->second;
}

bool AccessLog::contains(std::string const& uuid) const { return state().records.contains(uuid); }

void AccessLog::erase(std::string const& uuid) {
    auto& s = state();
    if (s.records.erase(uuid) > 0) {
        s.dirty = true;
        ++mRevision;
    }
}

std::size_t AccessLog::size() const { return state().records.size(); }

void AccessLog::load() {
    auto& s = state();
    s       = {};
    mCacheTime = 0;
    ++mRevision;
    auto* d = db();
    if (!d) return;
    s.loaded = true;
    std::string buffer;
    if (!d->loadData(kDbKey, buffer, DBHelpers::Category::Uncategorized)) return;
    auto j = nlohmann::json::parse(buffer, nullptr, false);
    if (!j.is_object()) {
        log().error("access log: stored data is unreadable");
        return;
    }
    for (auto const& [_, v] : j.items()) {
        if (auto r = AccessLogRecord::fromJson(v)) s.records[r->backpackUuid] = std::move(*r);
    }
}

void AccessLog::save() {
    auto& s = state();
    if (!s.dirty || !s.loaded) return;
    auto* d = db();
    if (!d) return;
    nlohmann::json j = nlohmann::json::object();
    for (auto const& [uuid, r] : s.records) j[uuid] = r.toJson();
    d->saveData(std::string(kDbKey), j.dump(), DBHelpers::Category::Uncategorized);
    s.dirty = false;
}

void AccessLog::clear() {
    state()    = {};
    mCacheTime = 0;
    ++mRevision;
}

} // namespace abp::admin
