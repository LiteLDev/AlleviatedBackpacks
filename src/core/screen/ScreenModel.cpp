#include "core/screen/ScreenModel.h"

#include <algorithm>
#include <unordered_map>

namespace abp::core::screen {

using nlohmann::json;

namespace {
json itemJson(ItemStackData const& item) { return item.empty() ? json(nullptr) : item.toJson(); }

ItemStackData itemFrom(json const& j) {
    if (j.is_null()) return {};
    try {
        return ItemStackData::fromJson(j);
    } catch (...) {
        return {};
    }
}

template <class T>
T value(json const& j, char const* key, T fallback) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return fallback;
    try {
        return it->get<T>();
    } catch (...) {
        return fallback;
    }
}

json itemsJson(std::vector<ItemStackData> const& items) {
    json a = json::array();
    for (auto const& i : items) a.push_back(itemJson(i));
    return a;
}

std::vector<ItemStackData> itemsFrom(json const& j) {
    std::vector<ItemStackData> out;
    if (!j.is_array()) return out;
    for (auto const& e : j) out.push_back(itemFrom(e));
    return out;
}

json upgradesJson(std::vector<UpgradeView> const& ups) {
    json a = json::array();
    for (auto const& u : ups) a.push_back(u.toJson());
    return a;
}

std::vector<UpgradeView> upgradesFrom(json const& j) {
    std::vector<UpgradeView> out;
    if (!j.is_array()) return out;
    for (auto const& e : j) out.push_back(UpgradeView::fromJson(e));
    return out;
}

json errorJson(BackpackScreenModel const& m) {
    return json{{"msg", m.errorMessage}, {"up", m.errorUpgradeSlots}, {"inv", m.errorStorageSlots}};
}

void errorFrom(BackpackScreenModel& m, json const& j) {
    m.errorMessage      = value<std::string>(j, "msg", {});
    m.errorUpgradeSlots = value<std::vector<int>>(j, "up", {});
    m.errorStorageSlots = value<std::vector<int>>(j, "inv", {});
}

bool sameItems(ItemStackData const& a, ItemStackData const& b) {
    if (a.empty() && b.empty()) return true;
    return a == b && a.maxStack == b.maxStack && a.damage == b.damage;
}
} // namespace

// ---- UpgradeView ---------------------------------------------------------------------------------

json UpgradeView::toJson() const {
    json j{
        {"item", itemJson(item)},
        {"id", id},
        {"name", name},
        {"on", enabled},
        {"tog", toggleable},
        {"run", runnable},
        {"set", hasSettings},
    };
    if (!lore.empty()) j["lore"] = lore;
    if (!tab.empty()) j["tab"] = tab;
    return j;
}

UpgradeView UpgradeView::fromJson(json const& j) {
    UpgradeView u;
    if (!j.is_object()) return u;
    u.item        = itemFrom(j.value("item", json(nullptr)));
    u.id          = value<std::string>(j, "id", {});
    u.name        = value<std::string>(j, "name", {});
    u.lore        = value<std::vector<std::string>>(j, "lore", {});
    u.enabled     = value<bool>(j, "on", true);
    u.toggleable  = value<bool>(j, "tog", false);
    u.runnable    = value<bool>(j, "run", true);
    u.hasSettings = value<bool>(j, "set", false);
    if (auto it = j.find("tab"); it != j.end() && it->is_object()) u.tab = *it;
    return u;
}

// ---- BackpackScreenModel -------------------------------------------------------------------------

json BackpackScreenModel::toJson() const {
    return json{
        {"kind", kind},
        {"menuRows", menuRows},
        {"menuSlots", itemsJson(menuSlots)},
        {"session", session},
        {"cid", containerId},
        {"uuid", uuid},
        {"title", title},
        {"tier", tier},
        {"size", storageSlots},
        {"slots", itemsJson(slots)},
        {"lim", slotLimits},
        {"baseLim", baseSlotLimit},
        {"inacc", inaccessible},
        {"upgrades", upgradesJson(upgrades)},
        {"memory", itemsJson(memory)},
        {"noSort", noSort},
        {"noSortColor", noSortColor},
        {"sortBy", sortBy},
        {"nested", nested},
        {"locked", lockedSlot},
        {"mult", stackMultiplier},
        {"main", mainColor},
        {"accent", accentColor},
        {"error", errorJson(*this)},
        {"extra", extra},
    };
}

BackpackScreenModel BackpackScreenModel::fromJson(json const& j) {
    BackpackScreenModel m;
    if (!j.is_object()) return m;
    m.kind         = value<std::string>(j, "kind", "backpack");
    m.menuRows     = std::max(0, value<int>(j, "menuRows", 0));
    m.menuSlots    = itemsFrom(j.value("menuSlots", json::array()));
    m.session      = value<std::uint32_t>(j, "session", 0);
    m.containerId  = value<int>(j, "cid", -1);
    m.uuid         = value<std::string>(j, "uuid", {});
    m.title        = value<std::string>(j, "title", {});
    m.tier         = value<int>(j, "tier", 0);
    m.storageSlots = std::max(0, value<int>(j, "size", 0));
    m.slots        = itemsFrom(j.value("slots", json::array()));
    m.slots.resize(static_cast<std::size_t>(m.storageSlots));
    m.slotLimits = value<std::vector<int>>(j, "lim", {});
    m.slotLimits.resize(static_cast<std::size_t>(m.storageSlots), 0);
    m.baseSlotLimit = value<int>(j, "baseLim", 64);
    m.inaccessible  = value<std::vector<bool>>(j, "inacc", {});
    m.upgrades    = upgradesFrom(j.value("upgrades", json::array()));
    m.memory      = itemsFrom(j.value("memory", json::array()));
    m.noSort      = value<std::vector<bool>>(j, "noSort", {});
    m.noSortColor = value<std::uint32_t>(j, "noSortColor", 0x80C71F);
    m.sortBy      = value<std::string>(j, "sortBy", "name");
    m.nested      = value<bool>(j, "nested", false);
    m.lockedSlot  = value<int>(j, "locked", -1);
    m.stackMultiplier = value<double>(j, "mult", 1.0);
    m.mainColor       = value<std::uint32_t>(j, "main", 0xFFA0703C);
    m.accentColor     = value<std::uint32_t>(j, "accent", 0xFF5A3C1E);
    errorFrom(m, j.value("error", json::object()));
    if (auto it = j.find("extra"); it != j.end() && it->is_object()) m.extra = *it;
    return m;
}

json makePatch(BackpackScreenModel const& a, BackpackScreenModel const& b) {
    json p = json::object();
    if (a.storageSlots != b.storageSlots) {
        // Resized (tier upgrade while open): send the storage as a whole.
        p["size"]  = b.storageSlots;
        p["slots"] = itemsJson(b.slots);
        p["lim"]   = b.slotLimits;
    } else {
        json slots = json::object();
        json lims  = json::object();
        for (std::size_t i = 0; i < b.slots.size(); ++i) {
            bool itemChanged = i >= a.slots.size() || !sameItems(a.slots[i], b.slots[i]);
            int  oldLim      = i < a.slotLimits.size() ? a.slotLimits[i] : 0;
            int  newLim      = i < b.slotLimits.size() ? b.slotLimits[i] : 0;
            if (itemChanged) slots[std::to_string(i)] = itemJson(b.slots[i]);
            if (itemChanged || oldLim != newLim) lims[std::to_string(i)] = newLim;
        }
        if (!slots.empty()) p["slotsDiff"] = std::move(slots);
        if (!lims.empty()) p["limDiff"] = std::move(lims);
    }
    if (a.kind != b.kind || a.menuRows != b.menuRows || a.menuSlots.size() != b.menuSlots.size()) {
        p["kind"]      = b.kind;
        p["menuRows"]  = b.menuRows;
        p["menuSlots"] = itemsJson(b.menuSlots);
    } else {
        json ms = json::object();
        for (std::size_t i = 0; i < b.menuSlots.size(); ++i) {
            if (!sameItems(a.menuSlots[i], b.menuSlots[i])) ms[std::to_string(i)] = itemJson(b.menuSlots[i]);
        }
        if (!ms.empty()) p["menuSlotsDiff"] = std::move(ms);
    }
    if (a.upgrades != b.upgrades) p["upgrades"] = upgradesJson(b.upgrades);
    {
        bool same = a.memory.size() == b.memory.size()
                 && std::equal(a.memory.begin(), a.memory.end(), b.memory.begin(), sameItems);
        if (!same) p["memory"] = itemsJson(b.memory);
    }
    if (a.noSort != b.noSort) p["noSort"] = b.noSort;
    if (a.baseSlotLimit != b.baseSlotLimit) p["baseLim"] = b.baseSlotLimit;
    if (a.inaccessible != b.inaccessible) p["inacc"] = b.inaccessible;
    if (a.noSortColor != b.noSortColor) p["noSortColor"] = b.noSortColor;
    if (a.sortBy != b.sortBy) p["sortBy"] = b.sortBy;
    if (a.title != b.title) p["title"] = b.title;
    if (a.tier != b.tier) p["tier"] = b.tier;
    if (a.nested != b.nested) p["nested"] = b.nested;
    if (a.lockedSlot != b.lockedSlot) p["locked"] = b.lockedSlot;
    if (a.stackMultiplier != b.stackMultiplier) p["mult"] = b.stackMultiplier;
    if (a.mainColor != b.mainColor) p["main"] = b.mainColor;
    if (a.accentColor != b.accentColor) p["accent"] = b.accentColor;
    if (a.errorMessage != b.errorMessage || a.errorUpgradeSlots != b.errorUpgradeSlots || a.errorStorageSlots != b.errorStorageSlots) {
        p["error"] = errorJson(b);
    }
    if (a.extra != b.extra) p["extra"] = b.extra;
    return p;
}

bool applyPatch(BackpackScreenModel& m, json const& p) {
    if (!p.is_object()) return false;
    try {
        if (auto it = p.find("size"); it != p.end()) {
            m.storageSlots = std::max(0, it->get<int>());
            m.slots        = itemsFrom(p.value("slots", json::array()));
            m.slots.resize(static_cast<std::size_t>(m.storageSlots));
            m.slotLimits = p.value("lim", std::vector<int>{});
            m.slotLimits.resize(static_cast<std::size_t>(m.storageSlots), 0);
        }
        if (auto it = p.find("slotsDiff"); it != p.end()) {
            for (auto const& [key, item] : it->items()) {
                auto i = static_cast<std::size_t>(std::stoul(key));
                if (i >= m.slots.size()) return false;
                m.slots[i] = itemFrom(item);
            }
        }
        if (auto it = p.find("limDiff"); it != p.end()) {
            for (auto const& [key, lim] : it->items()) {
                auto i = static_cast<std::size_t>(std::stoul(key));
                if (i >= m.slotLimits.size()) return false;
                m.slotLimits[i] = lim.get<int>();
            }
        }
        if (auto it = p.find("kind"); it != p.end()) m.kind = it->get<std::string>();
        if (auto it = p.find("menuRows"); it != p.end()) m.menuRows = it->get<int>();
        if (auto it = p.find("menuSlots"); it != p.end()) m.menuSlots = itemsFrom(*it);
        if (auto it = p.find("menuSlotsDiff"); it != p.end()) {
            for (auto const& [key, item] : it->items()) {
                auto i = static_cast<std::size_t>(std::stoul(key));
                if (i >= m.menuSlots.size()) return false;
                m.menuSlots[i] = itemFrom(item);
            }
        }
        if (auto it = p.find("upgrades"); it != p.end()) m.upgrades = upgradesFrom(*it);
        if (auto it = p.find("memory"); it != p.end()) m.memory = itemsFrom(*it);
        if (auto it = p.find("noSort"); it != p.end()) m.noSort = it->get<std::vector<bool>>();
        if (auto it = p.find("baseLim"); it != p.end()) m.baseSlotLimit = it->get<int>();
        if (auto it = p.find("inacc"); it != p.end()) m.inaccessible = it->get<std::vector<bool>>();
        if (auto it = p.find("noSortColor"); it != p.end()) m.noSortColor = it->get<std::uint32_t>();
        if (auto it = p.find("sortBy"); it != p.end()) m.sortBy = it->get<std::string>();
        if (auto it = p.find("title"); it != p.end()) m.title = it->get<std::string>();
        if (auto it = p.find("tier"); it != p.end()) m.tier = it->get<int>();
        if (auto it = p.find("nested"); it != p.end()) m.nested = it->get<bool>();
        if (auto it = p.find("locked"); it != p.end()) m.lockedSlot = it->get<int>();
        if (auto it = p.find("mult"); it != p.end()) m.stackMultiplier = it->get<double>();
        if (auto it = p.find("main"); it != p.end()) m.mainColor = it->get<std::uint32_t>();
        if (auto it = p.find("accent"); it != p.end()) m.accentColor = it->get<std::uint32_t>();
        if (auto it = p.find("error"); it != p.end()) errorFrom(m, *it);
        if (auto it = p.find("extra"); it != p.end() && it->is_object()) m.extra = *it;
    } catch (...) {
        return false;
    }
    return true;
}

// ---- ContentsModel -------------------------------------------------------------------------------

json ContentsModel::toJson() const {
    json ups = json::array();
    for (auto const& u : upgrades) ups.push_back(json{{"item", itemJson(u.item)}, {"tog", u.toggleable}, {"on", u.enabled}});
    return json{{"uuid", uuid}, {"known", known}, {"lines", lines}, {"upgrades", ups}, {"items", itemsJson(items)}};
}

ContentsModel ContentsModel::fromJson(json const& j) {
    ContentsModel m;
    if (!j.is_object()) return m;
    m.uuid  = value<std::string>(j, "uuid", {});
    m.known = value<bool>(j, "known", false);
    m.lines = value<std::vector<std::string>>(j, "lines", {});
    if (auto it = j.find("upgrades"); it != j.end() && it->is_array()) {
        for (auto const& e : *it) {
            Upgrade u;
            u.item       = itemFrom(e.value("item", json(nullptr)));
            u.toggleable = value<bool>(e, "tog", false);
            u.enabled    = value<bool>(e, "on", true);
            if (!u.item.empty()) m.upgrades.push_back(std::move(u));
        }
    }
    for (auto& i : itemsFrom(j.value("items", json::array()))) {
        if (!i.empty()) m.items.push_back(std::move(i));
    }
    return m;
}

// ---- RenderInfo ----------------------------------------------------------------------------------

json RenderInfo::toJson() const {
    return json{{"uuid", uuid}, {"tier", tier}, {"main", mainColor}, {"accent", accentColor}, {"upgrades", upgrades}, {"extra", extra}};
}

RenderInfo RenderInfo::fromJson(json const& j) {
    RenderInfo r;
    if (!j.is_object()) return r;
    r.uuid        = value<std::string>(j, "uuid", {});
    r.tier        = value<int>(j, "tier", 0);
    r.mainColor   = value<std::uint32_t>(j, "main", 0xFFA0703C);
    r.accentColor = value<std::uint32_t>(j, "accent", 0xFF5A3C1E);
    r.upgrades    = value<std::vector<std::string>>(j, "upgrades", {});
    if (auto it = j.find("extra"); it != j.end() && it->is_object()) r.extra = *it;
    return r;
}

// ---- compaction ----------------------------------------------------------------------------------

std::vector<ItemStackData> compactSortedByCount(BackpackInventory const& inventory) {
    std::vector<ItemStackData>                        out;
    std::unordered_map<ItemKey, std::size_t, ItemKeyHash> index;
    for (int i = 0; i < inventory.size(); ++i) {
        auto const& s = inventory.at(i);
        if (s.empty()) continue;
        auto key = s.key();
        if (auto it = index.find(key); it != index.end()) {
            auto& e = out[it->second];
            long long sum = static_cast<long long>(e.count) + s.count;
            e.count       = sum > INT32_MAX ? INT32_MAX : static_cast<int>(sum);
            continue;
        }
        index.emplace(std::move(key), out.size());
        out.push_back(s);
    }
    std::stable_sort(out.begin(), out.end(), [](ItemStackData const& a, ItemStackData const& b) { return a.count > b.count; });
    return out;
}

} // namespace abp::core::screen
