#include "upgrade/stonecutter/RecentResults.h"

#include <algorithm>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/server/ServerStoppingEvent.h"
#include "ll/api/service/Bedrock.h"

#include "nlohmann/json.hpp"

#include "mc/platform/UUID.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/storage/LevelStorage.h"
#include "mc/world/level/storage/db_helpers/Category.h"

#include "mod/Log.h"
#include "mod/Module.h"

namespace abp::upgrade::stonecutter {

namespace {

std::unordered_map<std::string, nlohmann::json>& cache() {
    static std::unordered_map<std::string, nlohmann::json> c;
    return c;
}

std::string keyOf(Player& player) { return "abp:player:" + player.getUuid().asString() + ":recent_results"; }

LevelStorage* db() {
    auto level = ll::service::getLevel();
    return level ? &level->getLevelStorage() : nullptr;
}

nlohmann::json& load(Player& player) {
    auto key = keyOf(player);
    if (auto it = cache().find(key); it != cache().end()) return it->second;
    nlohmann::json j = nlohmann::json::object();
    if (auto* d = db()) {
        std::string buffer;
        if (d->loadData(key, buffer, DBHelpers::Category::Uncategorized)) {
            auto parsed = nlohmann::json::parse(buffer, nullptr, false);
            if (parsed.is_object()) j = std::move(parsed);
            else log().warn("recent results {} unreadable; starting empty", key);
        }
    }
    if (cache().size() > 256) cache().clear();
    return cache().emplace(key, std::move(j)).first->second;
}

void save(Player& player) {
    if (auto* d = db()) d->saveData(keyOf(player), load(player).dump(), DBHelpers::Category::Uncategorized);
}

/// The client loads many worlds per game session: forget everything when one stops.
class RecentResultsModule {
public:
    void install() {
        mListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerStoppingEvent>(
            [](ll::event::ServerStoppingEvent&) { cache().clear(); }
        );
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mListener);
        cache().clear();
    }

private:
    ll::event::ListenerPtr mListener;
};

} // namespace

std::vector<std::string> recentResults(Player& player, std::string_view scope, std::string_view ingredient) {
    std::vector<std::string> out;
    auto const&              j = load(player);
    auto                     s = j.find(scope);
    if (s == j.end() || !s->is_object()) return out;
    auto i = s->find(ingredient);
    if (i == s->end() || !i->is_array()) return out;
    for (auto const& r : *i) {
        if (r.is_string() && static_cast<int>(out.size()) < kMaxRecentResults) out.push_back(r.get<std::string>());
    }
    return out;
}

bool recordResult(Player& player, std::string_view scope, std::string_view ingredient, std::string_view result) {
    auto list = recentResults(player, scope, ingredient);
    if (!list.empty() && list.front() == result) return false;
    std::erase(list, std::string(result));
    list.insert(list.begin(), std::string(result));
    if (static_cast<int>(list.size()) > kMaxRecentResults) list.resize(kMaxRecentResults);
    auto& j = load(player);
    auto& s = j[std::string(scope)];
    if (!s.is_object()) s = nlohmann::json::object();
    s[std::string(ingredient)] = list;
    save(player);
    return true;
}

} // namespace abp::upgrade::stonecutter

ABP_REGISTER_MODULE(abp::upgrade::stonecutter::RecentResultsModule);
