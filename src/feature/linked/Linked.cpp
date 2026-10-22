// Linked storage service: groups (saved data), linking rules, endpoint item data, and the gates
// that make the rest of the mod treat endpoints right (tier growth only through the primary,
// creative placement of an endpoint, shared uuids of placed endpoints, maintenance).

#include "feature/linked/Linked.h"

#include <cmath>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/server/ServerStartedEvent.h"
#include "ll/api/event/server/ServerStoppingEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/platform/UUID.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/storage/LevelStorage.h"
#include "mc/world/level/storage/db_helpers/Category.h"

#include "core/Codec.h"
#include "feature/admin/Admin.h"
#include "feature/placed/PlacedBackpacks.h"
#include "game/Backpack.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/Storage.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "upgrade/deposit/ContainerTransfer.h"

namespace abp::linked {

namespace {

constexpr std::string_view kDbKey = "abp:linked";

std::string stringTag(ItemStackBase const& stack, std::string_view key) {
    if (stack.isNull() || !stack.mUserData) return {};
    auto const& tag = *stack.mUserData;
    if (!tag.contains(key, Tag::Type::String)) return {};
    std::string const& s = tag[key];
    return s;
}

void editUserData(ItemStack& stack, std::function<void(CompoundTag&)> const& edit) {
    auto tag = stack.mUserData ? stack.mUserData->clone() : std::make_unique<CompoundTag>();
    edit(*tag);
    stack.setUserData(std::move(tag));
}

void bindEndpoint(ItemStack& stack, core::LinkedGroup const& group, std::string const& endpoint, bool primary) {
    game::setBackpackUuid(stack, group.storage);
    editUserData(stack, [&](CompoundTag& tag) {
        tag.putString(std::string(kGroupKey), group.id);
        tag.putString(std::string(kEndpointKey), endpoint);
        tag.putByte(std::string(kPrimaryKey), primary ? 1 : 0);
        if (group.name.empty()) tag.remove(kGroupNameKey);
        else tag.putString(std::string(kGroupNameKey), group.name);
    });
}

void bindLinker(ItemStack& linker, core::LinkedGroup const& group) {
    editUserData(linker, [&](CompoundTag& tag) {
        tag.putString(std::string(kLinkerGroupKey), group.id);
        if (group.name.empty()) tag.remove(kLinkerNameKey);
        else tag.putString(std::string(kLinkerNameKey), group.name);
    });
}

LevelStorage* db() {
    auto level = ll::service::getLevel();
    return level ? &level->getLevelStorage() : nullptr;
}

/// SB's BackpackLinkedStorageEndpointAdapter.hasIncompatibleBackpackData: items or upgrades.
bool hasContents(ItemStack& endpoint) {
    auto uuid = game::backpackUuid(endpoint);
    if (!uuid) return false;
    auto bp = game::Storage::get().find(*uuid);
    if (!bp) return false;
    if (!bp->inventory().isEmpty()) return true;
    for (auto const& u : bp->data().upgrades) {
        if (!u.empty()) return true;
    }
    return false;
}

} // namespace

// ---- item data ---------------------------------------------------------------------------------------

std::optional<EndpointData> endpointOf(ItemStackBase const& stack) {
    if (stack.isNull() || !stack.mUserData || !game::backpackTierOf(stack.getTypeName())) return std::nullopt;
    EndpointData d;
    d.group    = stringTag(stack, kGroupKey);
    d.endpoint = stringTag(stack, kEndpointKey);
    if (d.group.empty() || d.endpoint.empty()) return std::nullopt;
    auto const& tag = *stack.mUserData;
    d.primary       = tag.contains(kPrimaryKey, Tag::Type::Byte) && static_cast<int>(tag[kPrimaryKey]) != 0;
    d.name          = stringTag(stack, kGroupNameKey);
    return d;
}

bool isEndpoint(ItemStackBase const& stack) { return endpointOf(stack).has_value(); }

std::optional<Role> roleOf(ItemStackBase const& stack) {
    auto d = endpointOf(stack);
    if (!d) return std::nullopt;
    return d->primary ? Role::Primary : Role::Secondary;
}

bool isLinker(ItemStackBase const& stack) { return !stack.isNull() && stack.getTypeName() == kLinkerId; }

std::optional<std::string> linkerGroup(ItemStackBase const& stack) {
    if (!isLinker(stack)) return std::nullopt;
    auto g = stringTag(stack, kLinkerGroupKey);
    if (g.empty()) return std::nullopt;
    return g;
}

std::string linkerName(ItemStackBase const& stack) { return stringTag(stack, kLinkerNameKey); }

std::string_view resultName(LinkResult r) {
    switch (r) {
    case LinkResult::Success:
        return "success";
    case LinkResult::AlreadyLinked:
        return "already_linked";
    case LinkResult::UnsupportedEndpoint:
        return "unsupported_endpoint";
    case LinkResult::IncompatibleEndpoint:
        return "incompatible_endpoint";
    case LinkResult::EndpointHasContents:
        return "endpoint_has_contents";
    }
    return "unknown";
}

// ---- service -----------------------------------------------------------------------------------------

Service& Service::get() {
    static Service instance;
    return instance;
}

LinkResult Service::link(std::string const& owner, ItemStack& linker, ItemStack& endpoint) {
    if (!isLinker(linker) || endpoint.isNull()) return LinkResult::UnsupportedEndpoint;
    if (!game::backpackTierOf(endpoint)) return LinkResult::UnsupportedEndpoint;
    auto target   = linkerGroup(linker);
    auto existing = endpointOf(endpoint);
    // A bound linker whose group is gone (world edits) behaves like an unlinked one.
    if (target && !mGroups.find(*target)) target.reset();

    if (!target && !existing) {
        // CREATE_PRIMARY: the backpack's storage becomes the group's.
        bool changed = false;
        auto bp      = game::Storage::get().forItem(endpoint, &changed);
        if (!bp) return LinkResult::UnsupportedEndpoint;
        auto  groupId    = core::newUuid();
        auto  endpointId = core::newUuid();
        auto& group =
            mGroups.create(groupId, owner, endpointId, bp->uuid(), endpoint.getCustomName(), std::string(endpoint.getTypeName()));
        bindEndpoint(endpoint, group, endpointId, true);
        bindLinker(linker, group);
        mDirty = true;
        save();
        log().info("linked storage: group {} created from backpack {}", groupId, bp->uuid());
        return LinkResult::Success;
    }
    if (target && !existing) {
        // ADD_SECONDARY: an empty backpack joins the group.
        auto* group = mGroups.find(*target);
        if (hasContents(endpoint)) return LinkResult::EndpointHasContents;
        auto own = game::backpackUuid(endpoint);
        if (own && *own != group->storage && !mGroups.byStorage(*own)) {
            // Its own (empty) storage is dropped: from now on it shows the group's.
            game::Storage::get().erase(*own);
        }
        auto endpointId = core::newUuid();
        group->addEndpoint(endpointId);
        bindEndpoint(endpoint, *group, endpointId, false);
        linker.remove(1);
        mDirty = true;
        save();
        log().info("linked storage: secondary endpoint {} added to group {}", endpointId, group->id);
        return LinkResult::Success;
    }
    if (!target && existing) {
        // BIND_EXISTING_ENDPOINT: the linker targets the endpoint's group.
        auto* group = mGroups.find(existing->group);
        if (!group || !group->hasEndpoint(existing->endpoint)) return LinkResult::UnsupportedEndpoint;
        bindLinker(linker, *group);
        return LinkResult::Success;
    }
    return LinkResult::AlreadyLinked;
}

std::optional<ItemStack> Service::createSecondaryEndpointCopy(ItemStack const& endpoint) {
    auto d = endpointOf(endpoint);
    if (!d) return std::nullopt;
    auto* group = mGroups.find(d->group);
    if (!group || !group->hasEndpoint(d->endpoint)) return std::nullopt;
    auto endpointId = core::newUuid();
    group->addEndpoint(endpointId);
    ItemStack copy = endpoint;
    copy.set(1);
    bindEndpoint(copy, *group, endpointId, false);
    mDirty = true;
    save();
    log().info("linked storage: creative copy {} of group {}", endpointId, group->id);
    return copy;
}

core::LinkedGroup const* Service::group(std::string const& id) const { return mGroups.find(id); }

bool Service::isPrimaryEndpoint(std::string const& group, std::string const& endpoint) const {
    auto const* g = mGroups.find(group);
    return g && g->isPrimary(endpoint);
}

bool Service::isEndpointMember(std::string const& group, std::string const& endpoint) const {
    auto const* g = mGroups.find(group);
    return g && g->hasEndpoint(endpoint);
}

std::vector<std::string> Service::storages() const { return mGroups.storages(); }

void Service::recordEndpointOpened(std::string const& group, std::string const& endpoint, std::string const& player) {
    auto* g = mGroups.find(group);
    if (!g) return;
    auto level = ll::service::getLevel();
    g->recordOpened(endpoint, player, level ? static_cast<std::int64_t>(level->getCurrentTick().tickID) : 0);
    mDirty = true;
}

void Service::load() {
    mGroups = {};
    mDirty  = false;
    mLoaded = false;
    auto* d = db();
    if (!d) return;
    mLoaded = true;
    std::string buffer;
    if (!d->loadData(kDbKey, buffer, DBHelpers::Category::Uncategorized)) return;
    auto j = nlohmann::json::parse(buffer, nullptr, false);
    if (!j.is_object()) {
        log().error("linked storage: stored groups are unreadable");
        return;
    }
    mGroups = core::LinkedGroups::fromJson(j);
}

void Service::save() {
    if (!mDirty || !mLoaded) return;
    auto* d = db();
    if (!d) return;
    d->saveData(std::string(kDbKey), mGroups.toJson().dump(), DBHelpers::Category::Uncategorized);
    mDirty = false;
}

void Service::clear() {
    mGroups = {};
    mDirty  = false;
    mLoaded = false;
}

namespace {

/// Only the primary can grow the shared storage (SC: completePrimaryTierUpgrade). A secondary of a
/// higher tier shows the group's storage as it is.
bool tierGrowthAllowed(ItemStack const& stack, game::Backpack const&) {
    auto d = endpointOf(stack);
    if (!d) return true;
    return Service::get().isPrimaryEndpoint(d->group, d->endpoint);
}

std::vector<std::string> protectedStorages() { return Service::get().storages(); }

} // namespace

void Service::install() {
    game::Storage::get().setTierGrowthGate(&tierGrowthAllowed);
    placed::PlacedBackpacks::setPlacementHooks({
        .creativeCopy = [](Player&, ItemStack const& hand) { return Service::get().createSecondaryEndpointCopy(hand); },
        .mayShareUuid =
            [](core::ItemStackData const& a, core::ItemStackData const& b) {
                auto ea = endpointOf(game::ItemConv::toStack(a, 1));
                auto eb = endpointOf(game::ItemConv::toStack(b, 1));
                return ea && eb && ea->group == eb->group && ea->endpoint != eb->endpoint;
            },
    });
    static bool registered = false;
    if (!registered) {
        registered = true;
        admin::addProtectedUuidProvider(&protectedStorages);
    }
    if (game::Storage::get().available()) load();
}

void Service::uninstall() {
    save();
    game::Storage::get().setTierGrowthGate(nullptr);
    placed::PlacedBackpacks::setPlacementHooks({});
}

// ---- feedback ------------------------------------------------------------------------------------------

namespace {

void playSound(Player& to, std::string const& name, Vec3 const& at, float volume, float pitch) {
    auto  pkt = MinecraftPackets::createPacket(MinecraftPacketIds::PlaySound);
    auto& s   = static_cast<PlaySoundPacket&>(*pkt);
    s.mName   = name;
    s.mPos    = BlockPos{
        static_cast<int>(std::floor(at.x * 8.0f)),
        static_cast<int>(std::floor(at.y * 8.0f)),
        static_cast<int>(std::floor(at.z * 8.0f))
    };
    s.mVolume                   = volume;
    s.mPitch                    = pitch;
    s.mLoopCount                = 0;
    s.mBypassListenerRangeCheck = false;
    s.mServerSoundHandle->reset();
    s.mPlaybackPositionSeconds->reset();
    s.sendTo(to);
}

/// Plays `name` for every player near `at` in `player`'s dimension (SC: level.playSound(null, ...)).
void playSoundAround(Player& player, std::string const& name, Vec3 const& at, float volume, float pitch) {
    auto dim = player.getDimensionId();
    player.getLevel().forEachPlayer([&](Player& p) {
        if (p.getDimensionId() == dim && p.getPosition().distanceTo(at) <= 16.0f) playSound(p, name, at, volume, pitch);
        return true;
    });
}

} // namespace

void feedback(Player& player, LinkResult result, BlockPos const* block) {
    if (result == LinkResult::Success) {
        Vec3 at = block ? block->center() : player.getFeetPos();
        playSoundAround(player, "mob.endermen.portal", at, 0.5f, 1.15f);
        if (block) player.getLevel().spawnParticleEffect("minecraft:portal_reverse_particle", at, &player.getDimension());
        return;
    }
    playSound(player, "note.bass", player.getPosition(), 1.0f, 0.7f);
    std::string text;
    switch (result) {
    case LinkResult::AlreadyLinked:
        text = trFor(player, "abp.linked.status.already_linked", "Storage is already linked");
        break;
    case LinkResult::IncompatibleEndpoint:
        text = trFor(player, "abp.linked.status.incompatible_endpoint", "Storage is incompatible with this linked storage group");
        break;
    case LinkResult::EndpointHasContents:
        text = trFor(player, "abp.linked.status.endpoint_has_contents", "Storage has contents");
        break;
    default:
        text = trFor(player, "abp.linked.status.unsupported_endpoint", "Storage cannot be linked");
        break;
    }
    upgrade::transfer::sendTip(player, text);
}

LinkResult linkWithFeedback(Player& player, ItemStack& linker, ItemStack& endpoint, BlockPos const* block) {
    auto  owner   = player.getUuid().asString();
    auto& service = Service::get();
    LinkResult result;
    if (!linkerGroup(linker) && linker.mCount > 1) {
        // SC: a stack of unlinked linkers binds one linker and keeps the rest.
        ItemStack bound = linker;
        bound.set(1);
        result = service.link(owner, bound, endpoint);
        if (result == LinkResult::Success) {
            linker.remove(1);
            if (!bound.isNull() && !player.addAndRefresh(bound)) player.drop(bound, false);
        }
    } else {
        result = service.link(owner, linker, endpoint);
    }
    feedback(player, result, block);
    log().info("linked storage: {} linked with result {}", player.getRealName(), resultName(result));
    return result;
}

namespace {

class LinkedModule {
public:
    void install() {
        Service::get().install();
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::ServerStartedEvent>([](auto&) { Service::get().load(); }));
        mListeners.push_back(bus.emplaceListener<ll::event::ServerStoppingEvent>([](auto&) { Service::get().save(); }));
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([this](auto&) {
            if (++mTicks % 600 == 0) Service::get().save();
        }));
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        Service::get().uninstall();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
    int                                 mTicks = 0;
};

} // namespace
} // namespace abp::linked

ABP_REGISTER_MODULE(abp::linked::LinkedModule);
