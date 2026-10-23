// JSON <-> SNBT for template files. See Snbt.h.

#include "feature/templates/Snbt.h"

#include <cstdint>
#include <limits>
#include <vector>

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/CompoundTagVariant.h"

namespace abp::feature::templates {

namespace {
using nlohmann::json;

CompoundTagVariant toTag(json const& j);

CompoundTagVariant integerTag(std::int64_t v) {
    if (v >= std::numeric_limits<int>::min() && v <= std::numeric_limits<int>::max()) return IntTag{static_cast<int>(v)};
    return Int64Tag{v};
}

CompoundTagVariant listTag(json const& arr) {
    std::vector<CompoundTagVariant> items;
    items.reserve(arr.size());
    bool anyLong = false, anyFloat = false, allNumbers = true;
    for (auto const& e : arr) {
        items.push_back(e.is_null() ? CompoundTagVariant(CompoundTag{}) : toTag(e));
        auto id = items.back().index();
        if (id == Tag::Int64) anyLong = true;
        else if (id == Tag::Double) anyFloat = true;
        else if (id != Tag::Int) allNumbers = false;
    }
    bool same = true;
    for (auto const& t : items) same = same && t.index() == items.front().index();
    if (!same && allNumbers) {
        // One numeric type for the whole list.
        for (std::size_t i = 0; i < items.size(); ++i) {
            auto const& e = arr[i];
            if (anyFloat) items[i] = DoubleTag{e.get<double>()};
            else if (anyLong) items[i] = Int64Tag{e.get<std::int64_t>()};
        }
        same = true;
    }
    if (!same) {
        // A mixed list: every element wrapped in {"": element}.
        for (auto& t : items) {
            CompoundTag wrap;
            wrap[""] = std::move(t);
            t        = std::move(wrap);
        }
    }
    return ListTag(std::move(items));
}

CompoundTagVariant toTag(json const& j) {
    switch (j.type()) {
    case json::value_t::object: {
        CompoundTag c;
        for (auto const& [k, v] : j.items()) {
            if (v.is_null()) continue;
            c[k] = toTag(v);
        }
        return c;
    }
    case json::value_t::array:
        return listTag(j);
    case json::value_t::string:
        return StringTag{j.get<std::string>()};
    case json::value_t::boolean:
        return ByteTag{static_cast<schar>(j.get<bool>() ? 1 : 0)};
    case json::value_t::number_integer:
        return integerTag(j.get<std::int64_t>());
    case json::value_t::number_unsigned: {
        auto v = j.get<std::uint64_t>();
        return integerTag(v > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ? std::numeric_limits<std::int64_t>::max()
                                                                                                 : static_cast<std::int64_t>(v));
    }
    case json::value_t::number_float:
        return DoubleTag{j.get<double>()};
    default:
        return CompoundTag{};
    }
}

json toJson(CompoundTagVariant const& v, bool inList);

json listToJson(ListTag const& list) {
    json out = json::array();
    for (auto const& e : list) {
        if (!e) continue;
        out.push_back(toJson(CompoundTagVariant(e), true));
    }
    return out;
}

json toJson(CompoundTagVariant const& v, bool inList) {
    switch (v.index()) {
    case Tag::Byte: {
        auto b = v.get<ByteTag>().data;
        if (b == 0 || b == 1) return b == 1;
        return static_cast<int>(b);
    }
    case Tag::Short:
        return static_cast<int>(v.get<ShortTag>().data);
    case Tag::Int:
        return v.get<IntTag>().data;
    case Tag::Int64:
        return static_cast<std::int64_t>(v.get<Int64Tag>().data);
    case Tag::Float:
        return static_cast<double>(v.get<FloatTag>().data);
    case Tag::Double:
        return v.get<DoubleTag>().data;
    case Tag::String:
        return static_cast<std::string const&>(v.get<StringTag>());
    case Tag::ByteArray: {
        json out = json::array();
        for (auto b : v.get<ByteArrayTag>()) out.push_back(static_cast<int>(static_cast<schar>(b)));
        return out;
    }
    case Tag::IntArray: {
        json out = json::array();
        for (auto i : v.get<IntArrayTag>()) out.push_back(i);
        return out;
    }
    case Tag::List:
        return listToJson(v.get<ListTag>());
    case Tag::Compound: {
        auto const& c = v.get<CompoundTag>();
        // A wrapped element of a mixed list.
        if (inList && c.size() == 1 && c.mTags.begin()->first.empty()) return toJson(c.mTags.begin()->second, true);
        json out = json::object();
        for (auto const& [k, e] : c.mTags) out[k] = toJson(e, false);
        return out;
    }
    default:
        return nullptr;
    }
}
} // namespace

std::optional<CompoundTag> jsonToTag(nlohmann::json const& j) {
    if (!j.is_object()) return std::nullopt;
    auto tag = toTag(j);
    return std::move(tag.get<CompoundTag>());
}

nlohmann::json tagToJson(CompoundTag const& tag) { return toJson(CompoundTagVariant(tag), false); }

std::optional<std::string> jsonToSnbt(nlohmann::json const& j) {
    auto tag = jsonToTag(j);
    if (!tag) return std::nullopt;
    return tag->toSnbt(SnbtFormat::PrettyFilePrint, 4);
}

std::optional<nlohmann::json> snbtToJson(std::string_view snbt) {
    auto tag = CompoundTag::fromSnbt(snbt);
    if (!tag) return std::nullopt;
    return tagToJson(*tag);
}

} // namespace abp::feature::templates
