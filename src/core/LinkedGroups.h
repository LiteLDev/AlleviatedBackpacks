#pragma once

// Sophisticated Core's linked storage groups, as data (LinkedStorageGroupsSavedData): a group
// shares one storage between several endpoints (backpack items or placed backpacks). One endpoint
// is the PRIMARY (the storage it was created from); the others are secondary. Upgrades, ticking,
// render info and the jukebox all live in the shared storage, so they resolve to the primary's.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace abp::core {

struct LinkedEndpointRecord {
    std::string  id;
    std::string  lastOpenedBy;     ///< player uuid ("" = never)
    std::int64_t lastOpenedAt = -1; ///< game tick
};

struct LinkedGroup {
    std::string                       id;
    std::string                       owner;   ///< player uuid that created it ("" = unknown)
    std::string                       primary; ///< primary endpoint id
    std::string                       storage; ///< backpack storage uuid shared by every endpoint
    std::string                       name;    ///< group name (the primary's custom name; "" = none)
    std::string                       carrier; ///< item id of the primary backpack (its tier)
    std::vector<LinkedEndpointRecord> endpoints;
    std::int64_t                      revision = 0;

    [[nodiscard]] bool hasEndpoint(std::string const& endpoint) const;
    [[nodiscard]] bool isPrimary(std::string const& endpoint) const { return primary == endpoint && hasEndpoint(endpoint); }
    void               addEndpoint(std::string const& endpoint);
    /// Removes a secondary endpoint (the primary can't be removed). True if removed.
    bool removeEndpoint(std::string const& endpoint);
    /// Removes any endpoint, the primary included (lost endpoints). True if removed.
    bool detachLostEndpoint(std::string const& endpoint);
    void recordOpened(std::string const& endpoint, std::string const& player, std::int64_t tick);
};

class LinkedGroups {
public:
    /// Creates a group with `primary` as its only endpoint.
    LinkedGroup& create(std::string id, std::string owner, std::string primary, std::string storage, std::string name, std::string carrier);
    [[nodiscard]] LinkedGroup*       find(std::string const& id);
    [[nodiscard]] LinkedGroup const* find(std::string const& id) const;
    /// The group whose shared storage is `storage`.
    [[nodiscard]] LinkedGroup const* byStorage(std::string const& storage) const;
    bool                             erase(std::string const& id);
    [[nodiscard]] std::size_t        size() const { return mGroups.size(); }
    [[nodiscard]] std::vector<std::string> storages() const;
    [[nodiscard]] std::map<std::string, LinkedGroup> const& all() const { return mGroups; }

    [[nodiscard]] nlohmann::json      toJson() const;
    [[nodiscard]] static LinkedGroups fromJson(nlohmann::json const& j);

private:
    std::map<std::string, LinkedGroup> mGroups;
};

} // namespace abp::core
