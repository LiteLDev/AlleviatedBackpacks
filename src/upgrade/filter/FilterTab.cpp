#include "upgrade/filter/FilterTab.h"

#include <algorithm>

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/item/ItemStack.h"

#include "game/ItemConv.h"
#include "net/NativeScreens.h"

namespace abp::upgrade::filtering {

namespace {
/// Longest tag name accepted from a client.
constexpr std::size_t kMaxTagLength = 256;
/// Most tags a filter keeps.
constexpr std::size_t kMaxTags = 64;

std::string_view typeName(TypeButton t) {
    switch (t) {
    case TypeButton::AllowList:
        return "allowList";
    case TypeButton::Contents:
        return "contents";
    case TypeButton::Deposit:
        return "deposit";
    case TypeButton::None:
    default:
        return "none";
    }
}

int typeState(core::Filter const& f, TypeButton t) {
    switch (t) {
    case TypeButton::Contents:
        return static_cast<int>(f.contentsType());
    case TypeButton::Deposit:
        return static_cast<int>(f.depositType());
    case TypeButton::AllowList:
    case TypeButton::None:
    default:
        return f.mode == core::FilterMode::Allow ? 0 : 1;
    }
}

bool slotsEnabled(core::Filter const& f, TypeButton t) {
    if (t == TypeButton::Contents) return !f.filterByStorage;
    if (t == TypeButton::Deposit) return !f.filterByInventory;
    return true;
}

/// Removes the durability from an item's saved tag (Bedrock keeps it in tag.Damage).
std::string withoutDamage(core::ItemStackData const& item) {
    if (item.nbt().empty()) return {};
    auto tag = CompoundTag::fromBinaryNbt(item.nbt());
    if (!tag) return item.nbt();
    if (tag->contains("tag") && tag->at("tag").is_object()) {
        auto& inner = tag->at("tag").get<CompoundTag>();
        inner.erase("Damage");
        if (inner.empty()) tag->erase("tag");
    }
    tag->erase("Damage");
    return tag->toBinaryNbt();
}

class Matcher final : public core::ITagProvider {
public:
    bool hasTag(core::ItemStackData const& item, std::string_view tag) const override {
        return game::ItemConv::tags().hasTag(item, tag);
    }
    std::vector<std::string> tagsOf(core::ItemStackData const& item) const override {
        return game::ItemConv::tags().tagsOf(item);
    }
    bool componentsEqualIgnoringDamage(core::ItemStackData const& a, core::ItemStackData const& b) const override {
        if (a.nbtHash() == b.nbtHash() && a.nbt() == b.nbt()) return true;
        if (a.damage == b.damage) return false; // differ in something other than the durability
        return withoutDamage(a) == withoutDamage(b);
    }
};
} // namespace

core::ITagProvider const& matcher() {
    static Matcher m;
    return m;
}

nlohmann::json tabJson(core::Filter const& filter, FilterSpec const& spec) {
    nlohmann::json items = nlohmann::json::array();
    for (int i = 0; i < spec.slots; ++i) {
        auto const* g = i < static_cast<int>(filter.ghosts.size()) ? &filter.ghosts[static_cast<std::size_t>(i)] : nullptr;
        items.push_back(g && !g->empty() ? g->toJson() : nlohmann::json());
    }
    return nlohmann::json{
        {"slots",        spec.slots                                  },
        {"perRow",       spec.slotsPerRow                            },
        {"advanced",     spec.advanced                               },
        {"type",         typeName(spec.type)                         },
        {"typeState",    typeState(filter, spec.type)                },
        {"primary",      static_cast<int>(filter.matchBy)            },
        {"dur",          filter.matchDurability                      },
        {"nbt",          filter.matchNbt                             },
        {"anyTag",       filter.matchAnyTag                          },
        {"tags",         filter.tags                                 },
        {"items",        std::move(items)                            },
        {"slotsEnabled", slotsEnabled(filter, spec.type)             },
    };
}

bool applyAction(core::Filter& f, FilterSpec const& spec, nlohmann::json const& data, core::ItemStackData const& cursor) {
    if (!data.is_object()) return false;
    auto op = data.value("op", std::string{});
    if (op == "slot") {
        int i = data.value("index", -1);
        if (i < 0 || i >= spec.slots) return false;
        if (!slotsEnabled(f, spec.type)) return false; // disabled slots (STORAGE / INVENTORY)
        if (f.matchBy == core::MatchBy::Tag && spec.advanced) return false; // slots hidden in TAGS mode
        if (!f.canSetGhost(cursor)) return false;                           // no duplicates
        if (static_cast<int>(f.ghosts.size()) < spec.slots) f.ghosts.resize(static_cast<std::size_t>(spec.slots));
        auto before = f.ghosts[static_cast<std::size_t>(i)];
        f.setGhost(i, cursor);
        return !(before.empty() && cursor.empty());
    }
    if (op == "type") {
        switch (spec.type) {
        case TypeButton::AllowList:
            f.mode = f.mode == core::FilterMode::Allow ? core::FilterMode::Deny : core::FilterMode::Allow;
            return true;
        case TypeButton::Contents:
            f.setContentsType(f.nextContentsType());
            return true;
        case TypeButton::Deposit:
            f.setDepositType(static_cast<core::DepositFilterType>((static_cast<int>(f.depositType()) + 1) % 3));
            return true;
        case TypeButton::None:
        default:
            return false;
        }
    }
    if (!spec.advanced) return false; // the remaining buttons only exist on advanced controls
    if (op == "primary") {
        f.cyclePrimaryMatch(spec.type == TypeButton::Contents);
        return true;
    }
    if (op == "dur") {
        if (f.matchBy == core::MatchBy::Tag) return false;
        f.matchDurability = !f.matchDurability;
        return true;
    }
    if (op == "nbt") {
        if (f.matchBy == core::MatchBy::Tag) return false;
        f.matchNbt = !f.matchNbt;
        return true;
    }
    if (op == "anyTag") {
        if (f.matchBy != core::MatchBy::Tag) return false;
        f.matchAnyTag = !f.matchAnyTag;
        return true;
    }
    if (op == "addTag" || op == "removeTag") {
        if (f.matchBy != core::MatchBy::Tag) return false;
        auto tag = data.value("tag", std::string{});
        if (tag.empty() || tag.size() > kMaxTagLength) return false;
        if (op == "removeTag") return f.removeTag(tag);
        if (f.tags.size() >= kMaxTags) return false;
        return f.addTag(tag);
    }
    return false;
}

core::ItemStackData cursorOf(Player& player) {
    auto data = game::ItemConv::toData(player.mPlayerUIContainer->getItem(0));
    return data.empty() ? core::ItemStackData{} : data.withCount(1);
}

void registerSettingsFilterTab(SettingsFilterTab tab) {
    auto shared = std::make_shared<SettingsFilterTab>(std::move(tab));
    net::TabDataRegistry::add(net::TabDataProvider{
        shared->upgrade,
        [shared](Player&, UpgradeContext const& ctx) {
            return nlohmann::json{
                {shared->key, tabJson(shared->load(ctx), shared->spec(ctx))}
            };
        },
    });
    net::TabActionRegistry::add(net::TabActionHandler{
        shared->upgrade,
        [shared](Player& player, UpgradeContext const& ctx, nlohmann::json const& data) {
            if (!data.is_object() || data.value("filter", std::string{}) != shared->key) return false;
            auto f = shared->load(ctx);
            if (applyAction(f, shared->spec(ctx), data, cursorOf(player))) shared->store(ctx, f);
            return true; // handled (re-sync even when nothing changed)
        },
    });
}

FilterSpec specOf(FilteredUpgradeConfig const& config, bool advanced, TypeButton type) {
    return FilterSpec{
        .slots       = std::clamp(config.filterSlots, 1, 20),
        .slotsPerRow = std::clamp(config.slotsInRow, 1, 6),
        .advanced    = advanced,
        .type        = type,
    };
}

} // namespace abp::upgrade::filtering
