#include "core/ItemStackData.h"

#include "core/Codec.h"

namespace abp::core {

ItemStackData ItemStackData::of(std::string id, int count, int maxStack, int aux) {
    ItemStackData s;
    s.id       = std::move(id);
    s.count    = count;
    s.maxStack = maxStack;
    s.aux      = aux;
    return s;
}

void ItemStackData::setNbt(std::string blob) {
    mNbtHash = blob.empty() ? 0 : fnv1a(blob);
    mNbt     = std::move(blob);
}

ItemStackData ItemStackData::withCount(int n) const {
    if (n <= 0) return {};
    ItemStackData c = *this;
    c.count         = n;
    return c;
}

std::string_view ItemStackData::itemNamespace() const {
    auto pos = id.find(':');
    if (pos == std::string::npos) return "minecraft";
    return std::string_view(id).substr(0, pos);
}

nlohmann::json ItemStackData::toJson() const {
    if (empty()) return nullptr;
    nlohmann::json j{
        {"id",    id      },
        {"count", count   },
        {"max",   maxStack},
    };
    if (aux != 0) j["aux"] = aux;
    if (damage != 0) j["dmg"] = damage;
    if (maxDamage != 0) j["maxDmg"] = maxDamage;
    if (!mNbt.empty()) j["nbt"] = base64Encode(mNbt);
    return j;
}

ItemStackData ItemStackData::fromJson(nlohmann::json const& j) {
    ItemStackData s;
    if (!j.is_object()) return s;
    s.id        = j.value("id", std::string{});
    s.count     = j.value("count", 0);
    s.maxStack  = j.value("max", 64);
    s.aux       = j.value("aux", 0);
    s.damage    = j.value("dmg", 0);
    s.maxDamage = j.value("maxDmg", 0);
    if (auto it = j.find("nbt"); it != j.end() && it->is_string()) {
        if (auto blob = base64Decode(it->get<std::string>())) s.setNbt(std::move(*blob));
    }
    if (s.maxStack <= 0) s.maxStack = 1;
    if (s.empty()) return {};
    return s;
}

} // namespace abp::core
