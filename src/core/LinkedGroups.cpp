#include "core/LinkedGroups.h"

#include <algorithm>

namespace abp::core {

bool LinkedGroup::hasEndpoint(std::string const& endpoint) const {
    return std::any_of(endpoints.begin(), endpoints.end(), [&](auto const& e) { return e.id == endpoint; });
}

void LinkedGroup::addEndpoint(std::string const& endpoint) {
    if (endpoint.empty() || hasEndpoint(endpoint)) return;
    endpoints.push_back({endpoint, {}, -1});
    ++revision;
}

bool LinkedGroup::removeEndpoint(std::string const& endpoint) {
    if (endpoint == primary) return false;
    return detachLostEndpoint(endpoint);
}

bool LinkedGroup::detachLostEndpoint(std::string const& endpoint) {
    auto n = std::erase_if(endpoints, [&](auto const& e) { return e.id == endpoint; });
    if (n > 0) ++revision;
    return n > 0;
}

void LinkedGroup::recordOpened(std::string const& endpoint, std::string const& player, std::int64_t tick) {
    for (auto& e : endpoints) {
        if (e.id != endpoint) continue;
        e.lastOpenedBy = player;
        e.lastOpenedAt = tick;
    }
}

LinkedGroup&
LinkedGroups::create(std::string id, std::string owner, std::string primary, std::string storage, std::string name, std::string carrier) {
    LinkedGroup g;
    g.id      = id;
    g.owner   = std::move(owner);
    g.primary = primary;
    g.storage = std::move(storage);
    g.name    = std::move(name);
    g.carrier = std::move(carrier);
    g.endpoints.push_back({std::move(primary), {}, -1});
    auto& slot = mGroups[id];
    slot       = std::move(g);
    return slot;
}

LinkedGroup* LinkedGroups::find(std::string const& id) {
    auto it = mGroups.find(id);
    return it == mGroups.end() ? nullptr : &it->second;
}

LinkedGroup const* LinkedGroups::find(std::string const& id) const {
    auto it = mGroups.find(id);
    return it == mGroups.end() ? nullptr : &it->second;
}

LinkedGroup const* LinkedGroups::byStorage(std::string const& storage) const {
    for (auto const& [_, g] : mGroups) {
        if (g.storage == storage) return &g;
    }
    return nullptr;
}

bool LinkedGroups::erase(std::string const& id) { return mGroups.erase(id) > 0; }

std::vector<std::string> LinkedGroups::storages() const {
    std::vector<std::string> out;
    for (auto const& [_, g] : mGroups) out.push_back(g.storage);
    return out;
}

nlohmann::json LinkedGroups::toJson() const {
    auto groups = nlohmann::json::array();
    for (auto const& [_, g] : mGroups) {
        auto endpoints = nlohmann::json::array();
        for (auto const& e : g.endpoints) {
            endpoints.push_back({{"id", e.id}, {"lastOpenedBy", e.lastOpenedBy}, {"lastOpenedAt", e.lastOpenedAt}});
        }
        groups.push_back({
            {"id",        g.id       },
            {"owner",     g.owner    },
            {"primary",   g.primary  },
            {"storage",   g.storage  },
            {"name",      g.name     },
            {"carrier",   g.carrier  },
            {"revision",  g.revision },
            {"endpoints", endpoints  },
        });
    }
    return {{"version", 1}, {"groups", groups}};
}

LinkedGroups LinkedGroups::fromJson(nlohmann::json const& j) {
    LinkedGroups out;
    if (!j.is_object()) return out;
    auto it = j.find("groups");
    if (it == j.end() || !it->is_array()) return out;
    for (auto const& gj : *it) {
        if (!gj.is_object()) continue;
        try {
            LinkedGroup g;
            g.id       = gj.value("id", std::string{});
            g.owner    = gj.value("owner", std::string{});
            g.primary  = gj.value("primary", std::string{});
            g.storage  = gj.value("storage", std::string{});
            g.name     = gj.value("name", std::string{});
            g.carrier  = gj.value("carrier", std::string{});
            g.revision = gj.value("revision", std::int64_t{0});
            if (auto e = gj.find("endpoints"); e != gj.end() && e->is_array()) {
                for (auto const& ej : *e) {
                    if (!ej.is_object()) continue;
                    LinkedEndpointRecord r;
                    r.id           = ej.value("id", std::string{});
                    r.lastOpenedBy = ej.value("lastOpenedBy", std::string{});
                    r.lastOpenedAt = ej.value("lastOpenedAt", std::int64_t{-1});
                    if (!r.id.empty()) g.endpoints.push_back(std::move(r));
                }
            }
            if (g.id.empty() || g.storage.empty() || g.primary.empty()) continue;
            out.mGroups[g.id] = std::move(g);
        } catch (nlohmann::json::exception const&) {
            continue;
        }
    }
    return out;
}

} // namespace abp::core
