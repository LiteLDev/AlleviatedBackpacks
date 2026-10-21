#include "core/BackpackData.h"

#include <algorithm>

namespace abp::core {

BackpackData BackpackData::create(std::string uuid, Tier tier) {
    BackpackData d;
    d.uuid = std::move(uuid);
    d.tier = tier;
    d.inventory.resize(tierInfo(tier).slots);
    d.upgrades.resize(static_cast<std::size_t>(tierInfo(tier).upgradeSlots));
    return d;
}

void BackpackData::setTier(Tier t) {
    tier = t;
    if (inventory.size() < tierInfo(t).slots) inventory.resize(tierInfo(t).slots);
    if (upgrades.size() < static_cast<std::size_t>(tierInfo(t).upgradeSlots)) {
        upgrades.resize(static_cast<std::size_t>(tierInfo(t).upgradeSlots));
    }
}

nlohmann::json BackpackData::toJson() const {
    nlohmann::json ups = nlohmann::json::array();
    for (auto const& u : upgrades) {
        ups.push_back({
            {"item",     u.item.toJson()},
            {"enabled",  u.enabled      },
            {"settings", u.settings     },
        });
    }
    return nlohmann::json{
        {"v",         kVersion                },
        {"uuid",      uuid                    },
        {"tier",      tierInfo(tier).id       },
        {"main",      mainColor               },
        {"accent",    accentColor             },
        {"inventory", inventory.toJson()      },
        {"upgrades",  std::move(ups)          },
        {"settings",  settings                },
    };
}

std::optional<BackpackData> BackpackData::fromJson(nlohmann::json const& j) try {
    if (!j.is_object()) return std::nullopt;
    int version = j.value("v", 0);
    if (version < 1 || version > kVersion) return std::nullopt;
    // Migrations from older versions go here (version < kVersion).
    auto tier = tierFromId(j.value("tier", std::string{}));
    if (!tier) return std::nullopt;

    BackpackData d;
    d.uuid        = j.value("uuid", std::string{});
    d.tier        = *tier;
    d.mainColor   = j.value("main", d.mainColor);
    d.accentColor = j.value("accent", d.accentColor);
    if (auto it = j.find("inventory"); it != j.end()) d.inventory = BackpackInventory::fromJson(*it);
    if (auto it = j.find("upgrades"); it != j.end() && it->is_array()) {
        for (auto const& u : *it) {
            UpgradeSlot s;
            if (u.is_object()) {
                if (auto item = u.find("item"); item != u.end()) s.item = ItemStackData::fromJson(*item);
                s.enabled = u.value("enabled", true);
                if (auto st = u.find("settings"); st != u.end() && st->is_object()) s.settings = *st;
            }
            d.upgrades.push_back(std::move(s));
        }
    }
    if (auto it = j.find("settings"); it != j.end() && it->is_object()) d.settings = *it;
    d.setTier(d.tier); // ensure the minimum slot counts for the tier
    return d;
} catch (nlohmann::json::exception const&) {
    return std::nullopt; // wrong field types: unreadable
}

std::string BackpackData::serialize() const { return toJson().dump(); }

std::optional<BackpackData> BackpackData::deserialize(std::string_view text) {
    auto j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded()) return std::nullopt;
    return fromJson(j);
}

} // namespace abp::core
