#include "core/Filter.h"

#include <algorithm>

namespace abp::core {

bool Filter::isEmpty() const {
    if (matchBy == MatchBy::Tag && !tags.empty()) return false;
    return std::all_of(ghosts.begin(), ghosts.end(), [](ItemStackData const& g) { return g.empty(); });
}

bool Filter::matches(ItemStackData const& item, ITagProvider const* tagProvider) const {
    if (item.empty()) return false;
    switch (matchBy) {
    case MatchBy::Item:
        for (auto const& g : ghosts) {
            if (g.empty() || g.id != item.id || g.aux != item.aux) continue;
            if (matchNbt && (g.nbtHash() != item.nbtHash() || g.nbt() != item.nbt())) continue;
            if (matchDurability && g.damage != item.damage) continue;
            return true;
        }
        return false;
    case MatchBy::Namespace:
        for (auto const& g : ghosts) {
            if (!g.empty() && g.itemNamespace() == item.itemNamespace()) return true;
        }
        return false;
    case MatchBy::Tag:
        if (!tagProvider) return false;
        if (!tags.empty()) {
            return std::any_of(tags.begin(), tags.end(), [&](std::string const& t) {
                return tagProvider->hasTag(item, t);
            });
        }
        for (auto const& g : ghosts) {
            if (g.empty()) continue;
            for (auto const& t : tagProvider->tagsOf(g)) {
                if (tagProvider->hasTag(item, t)) return true;
            }
        }
        return false;
    }
    return false;
}

bool Filter::passes(ItemStackData const& item, ITagProvider const* tagProvider) const {
    if (mode == FilterMode::Allow) {
        if (emptyAllowsAll && isEmpty()) return true;
        return matches(item, tagProvider);
    }
    return !matches(item, tagProvider);
}

void Filter::setGhost(int i, ItemStackData const& item) {
    if (i < 0) return;
    if (static_cast<std::size_t>(i) >= ghosts.size()) ghosts.resize(static_cast<std::size_t>(i) + 1);
    ghosts[static_cast<std::size_t>(i)] = item.empty() ? ItemStackData{} : item.withCount(1);
}

bool Filter::hasGhost(ItemStackData const& item) const {
    return std::any_of(ghosts.begin(), ghosts.end(), [&](ItemStackData const& g) {
        return !g.empty() && g.sameItem(item);
    });
}

nlohmann::json Filter::toJson() const {
    nlohmann::json g = nlohmann::json::array();
    for (auto const& s : ghosts) g.push_back(s.toJson());
    return nlohmann::json{
        {"mode",    static_cast<int>(mode)   },
        {"matchBy", static_cast<int>(matchBy)},
        {"nbt",     matchNbt                 },
        {"dur",     matchDurability          },
        {"emptyAll", emptyAllowsAll          },
        {"ghosts",  std::move(g)             },
        {"tags",    tags                     },
    };
}

Filter Filter::fromJson(nlohmann::json const& j, int slots) {
    Filter f;
    if (j.is_object()) {
        f.mode            = j.value("mode", 0) == 1 ? FilterMode::Deny : FilterMode::Allow;
        int mb            = j.value("matchBy", 0);
        f.matchBy         = (mb >= 0 && mb <= 2) ? static_cast<MatchBy>(mb) : MatchBy::Item;
        f.matchNbt        = j.value("nbt", false);
        f.matchDurability = j.value("dur", false);
        f.emptyAllowsAll  = j.value("emptyAll", true);
        if (auto it = j.find("ghosts"); it != j.end() && it->is_array()) {
            for (auto const& g : *it) f.ghosts.push_back(ItemStackData::fromJson(g));
        }
        if (auto it = j.find("tags"); it != j.end() && it->is_array()) {
            for (auto const& t : *it) {
                if (t.is_string()) f.tags.push_back(t.get<std::string>());
            }
        }
    }
    if (slots >= 0) f.ghosts.resize(static_cast<std::size_t>(slots));
    return f;
}

} // namespace abp::core
