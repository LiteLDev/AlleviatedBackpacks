// Mob Catcher / Advanced Mob Catcher upgrades (SB `upgrades/mobcatcher`): upgrade types, config,
// capture (sneak + right-click a mob with the backpack in hand), release (native screen click),
// slot blocking and the data the client overlay draws.

#include "upgrade/mobcatcher/MobCatcher.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <random>
#include <set>
#include <tuple>
#include <unordered_map>

#include "ll/api/command/CommandHandle.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/player/PlayerUseItemEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/ListTag.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/network/packet/TextPacket.h"
#include "mc/network/packet/TextPacketPayload.h"
#include "mc/network/packet/TextPacketType.h"
#include "mc/locale/I18n.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/ActorDataIDs.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/DataItem.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/SynchedActorData.h"
#include "mc/world/actor/SynchedActorDataEntityWrapper.h"
#include "mc/entity/components/SynchedActorDataComponent.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"
#include "mc/world/phys/HitResult.h"
#include "mc/world/phys/HitResultType.h"

#include "core/Capacity.h"
#include "core/Codec.h"
#include "game/Backpack.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "game/Storage.h"
#include "mod/Commands.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "net/Companion.h"
#include "net/NativeScreens.h"
#include "net/Packets.h"
#include "upgrade/IUpgrade.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/tank/Columns.h"

ABP_REGISTER_CONFIG("mobcatcher", abp::upgrade::mobcatcher::MobCatcherConfig);

namespace abp::upgrade::mobcatcher {

MobCatcherConfig const& config() { return abp::config::get<MobCatcherConfig>(); }

namespace {

constexpr char const* kGroup        = "mob_catcher";
constexpr char const* kBlockerOwner = "abp:mobcatcher";

int basicMax() { return std::clamp(config().basicMaxSlotCost, 1, 120); }
int advancedMax() { return std::clamp(config().advancedMaxSlotCost, 1, 120); }
double animalMultiplier() { return std::clamp(config().animalMultiplier, 0.1, 10.0); }
double hostileMultiplier() { return std::clamp(config().hostileMultiplier, 0.1, 10.0); }

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}
float randomFloat() { return std::uniform_real_distribution<float>(0.f, 1.f)(rng()); }

/// "ns:id" entries match the type, "#family" entries a Bedrock type family (Java entity tags).
bool matchesList(Actor const& actor, std::vector<std::string> const& list) {
    auto const& type = actor.getTypeName();
    for (auto const& e : list) {
        if (e.empty()) continue;
        if (e[0] == '#') {
            auto family = e.substr(1);
            if (auto colon = family.find(':'); colon != std::string::npos) family = family.substr(colon + 1);
            if (!family.empty() && actor.hasFamily(HashedString(family))) return true;
        } else if (e == type || (e.find(':') == std::string::npos && "minecraft:" + e == type)) {
            return true;
        }
    }
    return false;
}

std::string message(Player& player, std::string_view key, std::string_view fallback) { return trFor(player, key, fallback); }

/// Translation key of an entity type's name ("entity.cow.name"), resolved by the client.
std::string entityNameKey(std::string const& type) {
    constexpr std::string_view kVanilla = "minecraft:";
    if (type.starts_with(kVanilla)) return "entity." + type.substr(kVanilla.size()) + ".name";
    return "entity." + type + ".name";
}

/// The mob's name for messages (SB getCapturedMobDisplayName): its custom name, else the entity
/// type's name from the game's language files ("Cow"), else the id made readable.
std::string mobName(CapturedMob const& mob) {
    if (!mob.displayName.empty()) return mob.displayName;
    auto key = entityNameKey(mob.entityType);
    try {
        auto s = getI18n().get(key, nullptr);
        if (!s.empty() && s != key) return s;
    } catch (...) {
    }
    auto        id = mob.entityType.substr(mob.entityType.find(':') + 1);
    std::string out;
    bool        upper = true;
    for (char c : id) {
        if (c == '_') {
            out += ' ';
            upper = true;
        } else {
            out += upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
            upper = false;
        }
    }
    return out;
}

void playSound(Player& player, Vec3 const& at, std::string const& name, float volume, float pitch) {
    auto& level = player.getLevel();
    auto  dim   = player.getDimensionId();
    level.forEachPlayer([&](Player& other) {
        if (other.getDimensionId() != dim || other.getPosition().distanceTo(at) > 16.f * std::max(1.f, volume)) return true;
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
        s.sendTo(other);
        return true;
    });
}

/// SB `playMobCatcherSound`: base pitch +- 0.08.
void playMobCatcherSound(Player& player, std::string const& name, float volume, float basePitch) {
    playSound(player, player.getPosition(), name, volume, basePitch + (randomFloat() - 0.5f) * 0.16f);
}

CaptureResult fail(Player& player, std::string text) {
    playMobCatcherSound(player, "note.bass", 0.7f, 0.8f);
    return {false, std::move(text)};
}

// ---- entity data for the client preview -----------------------------------------------------------

/// Synched data ids that reference other entities or show text: not useful on a display copy.
bool skipDataId(int id) {
    switch (static_cast<ActorDataIDs>(id)) {
    case ActorDataIDs::Name:
    case ActorDataIDs::Owner:
    case ActorDataIDs::Target:
    case ActorDataIDs::LeashHolder:
    case ActorDataIDs::BlockTarget:
    case ActorDataIDs::TargetA:
    case ActorDataIDs::TargetB:
    case ActorDataIDs::TargetC:
    case ActorDataIDs::TradeTarget:
    case ActorDataIDs::CommandName:
    case ActorDataIDs::NametagAlwaysShow:
    case ActorDataIDs::NameAuthor:
    case ActorDataIDs::Score:
    case ActorDataIDs::InteractText:
    case ActorDataIDs::NameRawText:
    case ActorDataIDs::FilteredName:
        return true;
    default:
        return false;
    }
}

/// The entity's synched data (what an AddActorPacket carries: variant, colours, flags, ...), so the
/// client's display copy looks like the captured mob. [[id, type, value], ...]
nlohmann::json syncedData(Actor& actor) {
    nlohmann::json arr = nlohmann::json::array();
    try {
        auto& wrapper = *actor.mEntityData;
        auto  items   = (*wrapper.mData).mData->packAll(actor.getEntityContext());
        for (auto const& it : items) {
            if (!it) continue;
            int id = it->getId();
            if (skipDataId(id)) continue;
            auto           type = it->getType();
            nlohmann::json value;
            switch (type) {
            case DataItemType::Byte:
                if (auto v = it->getData<schar>()) value = static_cast<int>(*v);
                break;
            case DataItemType::Short:
                if (auto v = it->getData<short>()) value = static_cast<int>(*v);
                break;
            case DataItemType::Int:
                if (auto v = it->getData<int>()) value = *v;
                break;
            case DataItemType::Float:
                if (auto v = it->getData<float>()) value = *v;
                break;
            case DataItemType::String:
                if (auto v = it->getData<std::string>()) value = *v;
                break;
            case DataItemType::Int64:
                if (auto v = it->getData<int64>()) value = static_cast<std::int64_t>(*v);
                break;
            case DataItemType::Pos:
                if (auto v = it->getData<BlockPos>()) value = nlohmann::json::array({v->x, v->y, v->z});
                break;
            case DataItemType::Vec3:
                if (auto v = it->getData<Vec3>()) value = nlohmann::json::array({v->x, v->y, v->z});
                break;
            default:
                break;
            }
            if (!value.is_null()) arr.push_back(nlohmann::json::array({id, static_cast<int>(type), std::move(value)}));
        }
    } catch (...) {
        // Appearance only: a mob without readable data still renders with defaults.
    }
    return arr;
}

// ---- release position (SB getReleasePosition) --------------------------------------------------------

struct Dims {
    float width = 0.6f, height = 1.8f;
    bool  noGround = false; ///< flying / water mobs can be released without ground
};

bool collides(BlockSource& bs, AABB const& box) {
    std::vector<AABB> shapes;
    bs.fetchAABBs(shapes, box, false);
    constexpr float kEps = 1e-4f;
    return std::any_of(shapes.begin(), shapes.end(), [&](AABB const& s) {
        return s.max.x > box.min.x + kEps && s.min.x < box.max.x - kEps && s.max.y > box.min.y + kEps && s.min.y < box.max.y - kEps
            && s.max.z > box.min.z + kEps && s.min.z < box.max.z - kEps;
    });
}

std::optional<Vec3> releaseOnGround(BlockSource& bs, Dims const& d, BlockPos const& spawn) {
    if (d.noGround) return Vec3{spawn.x + 0.5f, static_cast<float>(spawn.y), spawn.z + 0.5f};
    BlockPos          ground{spawn.x, spawn.y - 1, spawn.z};
    std::vector<AABB> shapes;
    bs.fetchAABBs(shapes, AABB{Vec3{ground.x + 0.f, ground.y + 0.f, ground.z + 0.f}, Vec3{ground.x + 1.f, ground.y + 1.f, ground.z + 1.f}}, false);
    float top = -1e9f;
    for (auto const& s : shapes) {
        if (s.max.y <= ground.y || s.min.y >= ground.y + 1) continue;
        top = std::max(top, std::min(s.max.y, ground.y + 1.f));
    }
    if (top < -1e8f) return std::nullopt; // no collision shape below: nothing to stand on
    return Vec3{spawn.x + 0.5f, top, spawn.z + 0.5f};
}

std::optional<Vec3> validRelease(BlockSource& bs, Dims const& d, BlockPos const& spawn) {
    auto pos = releaseOnGround(bs, d, spawn);
    if (!pos) return std::nullopt;
    float half = d.width / 2.f;
    AABB  box{Vec3{pos->x - half, pos->y, pos->z - half}, Vec3{pos->x + half, pos->y + d.height, pos->z + half}};
    if (collides(bs, box)) return std::nullopt;
    return pos;
}

BlockPos offset(BlockPos const& p, int face) {
    switch (face) {
    case 0:
        return {p.x, p.y - 1, p.z};
    case 1:
        return {p.x, p.y + 1, p.z};
    case 2:
        return {p.x, p.y, p.z - 1};
    case 3:
        return {p.x, p.y, p.z + 1};
    case 4:
        return {p.x - 1, p.y, p.z};
    case 5:
        return {p.x + 1, p.y, p.z};
    default:
        return p;
    }
}

std::optional<Vec3> releasePosition(Player& player, Dims const& d) {
    auto&           bs  = player.getDimensionBlockSource();
    constexpr float kBlockInteractionRange = 4.5f; // Java Player.blockInteractionRange() default
    auto            hit = player.traceRay(kBlockInteractionRange, false, true);
    if (hit.mType == HitResultType::Tile) {
        auto ground = hit.mBlock;
        if (auto p = validRelease(bs, d, offset(ground, hit.mFacing))) return p;
        if (auto p = validRelease(bs, d, BlockPos{ground.x, ground.y + 1, ground.z})) return p;
    }
    auto look = player.getViewVector(1.f);
    Vec3 horizontal{look.x, 0.f, look.z};
    if (horizontal.x * horizontal.x + horizontal.z * horizontal.z < 1.0e-4f) {
        float yaw  = player.getRotation().y * 3.14159265f / 180.f;
        horizontal = Vec3{-std::sin(yaw), 0.f, std::cos(yaw)};
    }
    float len = std::sqrt(horizontal.x * horizontal.x + horizontal.z * horizontal.z);
    horizontal = Vec3{horizontal.x / len, 0.f, horizontal.z / len};
    auto feet  = player.getFeetPos();
    for (int distance = 1; distance <= 2; ++distance) {
        BlockPos cand{
            static_cast<int>(std::floor(feet.x + horizontal.x * distance)),
            static_cast<int>(std::floor(feet.y)),
            static_cast<int>(std::floor(feet.z + horizontal.z * distance))
        };
        if (auto p = validRelease(bs, d, cand)) return p;
    }
    return std::nullopt;
}

/// SB `getCaptureEffectCollapsePosition`: where the player's look ray enters the mob's box, else
/// the box centre.
Vec3 collapsePosition(Player& player, Actor& target) {
    auto const& box  = target.getAABB();
    Vec3        eye  = player.getHeadPos();
    Vec3        dir  = player.getViewVector(1.f);
    float       tMin = 0.f, tMax = 1.0e9f;
    float o[3]  = {eye.x, eye.y, eye.z}, v[3] = {dir.x, dir.y, dir.z};
    float lo[3] = {box.min.x, box.min.y, box.min.z}, hi[3] = {box.max.x, box.max.y, box.max.z};
    bool  hit   = true;
    for (int a = 0; a < 3 && hit; ++a) {
        if (std::abs(v[a]) < 1e-7f) {
            hit = o[a] >= lo[a] && o[a] <= hi[a];
            continue;
        }
        float t1 = (lo[a] - o[a]) / v[a], t2 = (hi[a] - o[a]) / v[a];
        if (t1 > t2) std::swap(t1, t2);
        tMin = std::max(tMin, t1);
        tMax = std::min(tMax, t2);
        hit  = tMin <= tMax;
    }
    if (hit) return Vec3{eye.x + dir.x * tMin, eye.y + dir.y * tMin, eye.z + dir.z * tMin};
    auto pos = target.getPosition();
    return Vec3{pos.x, box.min.y + (box.max.y - box.min.y) * 0.5f, pos.z};
}

/// SB's capture effect ends with poof + reverse portal particles and a pop.
void captureEffect(Player& player, Vec3 const& at) {
    auto& level = player.getLevel();
    auto& dim   = player.getDimension();
    level.spawnParticleEffect("minecraft:death_explosion_emitter", at, &dim);
    playSound(player, at, "random.pop", 0.7f, 0.7f + (randomFloat() - 0.5f) * 0.16f);
}

/// Pushes fresh render info (captured mob types for the ambient sounds) to a companion player.
void pushRenderInfo(Player& player, game::Backpack const& backpack) {
    net::send(player, net::RenderInfoPacket{net::buildRenderInfo(backpack).toJson()});
}

std::optional<std::string> eligibilityError(Player& player, Actor& target, bool advanced) {
    if (target.isPlayer()) return message(player, "abp.mobcatcher.status.players_blocked", "Players cannot be captured");
    if (target.isType(ActorType::Dragon) || target.isType(ActorType::WitherBoss)) {
        return message(player, "abp.mobcatcher.status.boss_blocked", "Bosses cannot be captured");
    }
    // NeoForge's `c:capturing_not_supported` entity tag -> a Bedrock type family of that name.
    if (target.hasFamily(HashedString("capturing_not_supported"))) {
        return message(player, "abp.mobcatcher.status.blocklisted", "That mob is blocked from capture");
    }
    if (target.getVehicle() != nullptr || target.hasPassenger()) {
        return message(player, "abp.mobcatcher.status.passengers_blocked", "Mobs with passengers or vehicles cannot be captured");
    }
    if (matchesList(target, config().entityBlockList)) {
        return message(player, "abp.mobcatcher.status.blocklisted", "That mob is blocked from capture");
    }
    auto owner = target.getOwnerId();
    if (owner != ActorUniqueID::INVALID_ID() && owner != player.getOrCreateUniqueID()) {
        return message(player, "abp.mobcatcher.status.not_owner", "Only the owner can capture that mob");
    }
    if (config().disallowInventoryEntities && target.hasComponent(HashedString("minecraft:inventory"))) {
        return message(player, "abp.mobcatcher.status.inventory_blocked", "Mobs with inventories cannot be captured");
    }
    if (!advanced && isHostile(target)) {
        return message(player, "abp.mobcatcher.status.hostile_needs_advanced", "Hostile mobs require Advanced Mob Catcher Upgrade");
    }
    return std::nullopt;
}

// ---- upgrade types ------------------------------------------------------------------------------------

class MobCatcherUpgrade : public IUpgrade {
public:
    explicit MobCatcherUpgrade(bool advanced) {
        mInfo.id           = advanced ? kAdvancedId : kBasicId;
        mInfo.itemId       = advanced ? "alleviatedbackpacks:advanced_mob_catcher_upgrade" : "alleviatedbackpacks:mob_catcher_upgrade";
        mInfo.nameKey      = advanced ? "abp.mobcatcher.advanced.name" : "abp.mobcatcher.name";
        mInfo.nameFallback = advanced ? "Advanced Mob Catcher Upgrade" : "Mob Catcher Upgrade";
        mInfo.tier         = advanced ? UpgradeTier::Advanced : UpgradeTier::Basic;
        mInfo.group        = kGroup;
        // SB allows one mob catcher per backpack, refused with its own message in canInstall (the
        // manager's generic "maximum" check would answer first with a limit of 1).
        mInfo.maxPerBackpack = 2;
        mInfo.hasSettings    = false; // SB hideSettingsTab()
        mInfo.toggleable     = false; // SB canBeDisabled() == false
    }

    [[nodiscard]] UpgradeInfo const& info() const override { return mInfo; }

    bool canInstall(UpgradeContext const& ctx, std::string& reason) const override {
        auto const& slots = ctx.backpack().data().upgrades;
        for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
            if (i == ctx.slot() || slots[static_cast<std::size_t>(i)].empty()) continue;
            auto* type = upgradeFor(slots[static_cast<std::size_t>(i)].item);
            if (type && type->info().groupOrId() == kGroup) {
                reason = translated(ctx, "abp.mobcatcher.status.only_one_allowed", "Only one Mob Catcher Upgrade is allowed per backpack");
                return false;
            }
        }
        return true;
    }

    bool canRemove(UpgradeContext const& ctx, std::string& reason) const override {
        if (capturedMobs(ctx.backpack()).empty()) return true;
        reason = translated(ctx, "abp.mobcatcher.status.contains_mobs", "Release captured mobs before removing Mob Catcher Upgrade");
        return false;
    }

    bool canReplaceWith(UpgradeContext const& ctx, core::ItemStackData const& replacement, std::string& reason) const override {
        auto* other = upgradeFor(replacement);
        if (!other || other->info().groupOrId() != kGroup) return canRemove(ctx, reason);
        if (other->info().id == kAdvancedId) return true; // SB: an advanced catcher fits everything
        if (canFitBasicTier(capturedMobs(ctx.backpack()), basicMax())) return true;
        reason = translated(ctx, "abp.mobcatcher.status.mobs_need_advanced", "Captured mobs require Advanced Mob Catcher Upgrade");
        return false;
    }

    [[nodiscard]] std::vector<int> refusalStorageSlots(UpgradeContext const& ctx) const override {
        auto& bp = ctx.backpack();
        return coveredSlots(capturedMobs(bp), columnsOf(bp), bp.inventory().size());
    }

private:
    static std::string translated(UpgradeContext const& ctx, std::string_view key, std::string_view fallback) {
        return ctx.player() ? trFor(*ctx.player(), key, fallback) : tr(key, fallback);
    }

    UpgradeInfo mInfo;
};

class BasicMobCatcher : public MobCatcherUpgrade {
public:
    BasicMobCatcher() : MobCatcherUpgrade(false) {}
};
class AdvancedMobCatcher : public MobCatcherUpgrade {
public:
    AdvancedMobCatcher() : MobCatcherUpgrade(true) {}
};

ABP_REGISTER_UPGRADE(BasicMobCatcher);
ABP_REGISTER_UPGRADE(AdvancedMobCatcher);

// ---- slot blocking (SB IInventorySlotBlocker) ----------------------------------------------------------

/// Captured mobs block their footprint whether or not a catcher is installed (or allowed by the
/// server), so stored mobs can never be overwritten by items.
ABP_REGISTER_BACKPACK_HOOKS([](game::Backpack& backpack) {
    auto mobs = capturedMobs(backpack);
    if (mobs.empty()) return;
    int  size    = backpack.inventory().size();
    int  columns = columnsOf(backpack);
    // The column count changes when a tier upgrade crosses 81 slots: keep each mob inside the grid
    // (SB getTargetSlot on its inventory layout refit).
    bool moved = false;
    for (std::size_t i = 0; i < mobs.size(); ++i) {
        auto& m      = mobs[i];
        int   stored = m.look.value("columns", columns);
        if (stored == columns || stored <= 0) continue;
        moved             = true;
        m.look["columns"] = columns;
        m.slot            = targetSlot(m, stored, columns, size);
        // The new footprint must stay inside the grid and must not cover items or other mobs:
        // otherwise move the mob to the first free rectangle (items are never moved).
        std::vector<CapturedMob> others;
        for (std::size_t j = 0; j < mobs.size(); ++j) {
            if (j != i) others.push_back(mobs[j]);
        }
        int  rows   = (size + columns - 1) / columns;
        auto cells  = coveredSlots({m}, columns, size);
        bool fits   = m.slot % columns + m.width <= columns && m.slot / columns + m.height <= rows
                 && static_cast<int>(cells.size()) == m.width * m.height;
        for (int s : cells) fits = fits && backpack.inventory().at(s).empty() && !isSlotCovered(others, s, columns);
        if (!fits) {
            if (auto free = findEmptyRectangle(backpack.inventory(), columns, {m.width, m.height}, others)) m.slot = *free;
        }
    }
    if (moved) {
        backpack.data().settings[kCapturedMobsKey] = mobsToJson(mobs);
        backpack.markDirty();
    }
    auto blocked = std::make_shared<std::vector<bool>>(static_cast<std::size_t>(size), false);
    for (int s : coveredSlots(mobs, columns, size)) (*blocked)[static_cast<std::size_t>(s)] = true;
    backpack.inventory().addSlotBlocker(kBlockerOwner, [blocked](int slot) {
        return slot >= 0 && slot < static_cast<int>(blocked->size()) && (*blocked)[static_cast<std::size_t>(slot)];
    });
});

// ---- native screen data / actions ---------------------------------------------------------------------

/// The client overlay's data, on the catcher's (hidden) tab: {"mobCatcher": {advanced, columns, mobs}}.
ABP_REGISTER_TAB_DATA(net::TabDataProvider{
    kGroup,
    [](Player&, UpgradeContext const& ctx) {
        auto& bp = ctx.backpack();
        return nlohmann::json{
            {"mobCatcher",
             {{"advanced", bestCatcher(bp).value_or(false)}, {"columns", columnsOf(bp)}, {"mobs", mobsToJson(capturedMobs(bp), false)}}}
        };
    }
});

/// {"action":"tab","slot":n,"data":{"release":"<mob id>"}}: SB MobCatcherReleasePayload.
ABP_REGISTER_TAB_ACTION(net::TabActionHandler{
    kGroup,
    [](Player& player, UpgradeContext const& ctx, nlohmann::json const& data) {
        auto id = data.value("release", std::string{});
        if (id.empty()) return false;
        auto result = release(player, ctx.backpackPtr(), id);
        if (!result.success && !result.message.empty()) {
            log().info("mob catcher: {} could not release a mob: {}", player.getRealName(), result.message);
        }
        showMessage(player, result);
        return true;
    }
});

/// Captured mob types of carried backpacks, for the client's muffled ambient sounds.
ABP_REGISTER_RENDER_INFO([](game::Backpack& backpack, nlohmann::json& extra) {
    auto mobs = capturedMobs(backpack);
    if (mobs.empty()) return;
    nlohmann::json arr = nlohmann::json::array();
    for (auto const& m : mobs) arr.push_back(nlohmann::json{{"id", m.id}, {"entityType", m.entityType}});
    extra["mobCatcher"] = std::move(arr);
});

// ---- capture by interaction ---------------------------------------------------------------------------

struct PendingCapture {
    std::int64_t player = 0;
    std::int64_t target = 0;
};
std::vector<PendingCapture>                      gPending;
std::set<std::pair<std::int64_t, std::int64_t>> gSeenThisTick;
bool                                             gInstalled = false;
/// Holding "use" repeats it: after a capture the next uses of the backpack are swallowed for a
/// moment (else the repeat would place / open the backpack where the mob stood).
constexpr int                                    kUseCooldownTicks = 10;
std::int64_t                                     gTick = 0;
std::unordered_map<std::int64_t, std::int64_t>   gLastUse; ///< player -> tick of the last capture use

/// The held backpack and its best catcher, if the player can capture with it.
std::shared_ptr<game::Backpack> heldCatcherBackpackImpl(Player& player, bool* advanced = nullptr) {
    auto const& held = player.getSelectedItem();
    if (held.isNull() || !game::isBackpack(held)) return nullptr;
    auto uuid = game::backpackUuid(held);
    if (!uuid || !game::Storage::get().available()) return nullptr;
    auto bp = game::Storage::get().find(*uuid);
    if (!bp) return nullptr;
    auto best = bestCatcher(*bp);
    if (!best) return nullptr;
    if (advanced) *advanced = *best;
    return bp;
}

/// SB CommonEventHandler.interactWithEntity: sneak + use on a living entity with a catcher
/// backpack in hand is always consumed (success or a refusal message); the capture itself runs at
/// the start of the next tick (the interaction packet is still being processed).
bool onInteract(Player& player, Actor& target) {
    if (!gInstalled || player.isClientSide() || !player.isSneaking()) return false;
    if (!Mob::tryGetFromEntity(target.getEntityContext(), false)) return false; // SB: LivingEntity only
    if (!heldCatcherBackpackImpl(player)) return false;
    gLastUse[player.getOrCreateUniqueID().rawID] = gTick;
    auto key = std::make_pair(player.getOrCreateUniqueID().rawID, target.getOrCreateUniqueID().rawID);
    if (gSeenThisTick.insert(key).second) gPending.push_back({key.first, key.second});
    return true;
}

/// Bedrock clients only send an entity interaction for mobs that have one (a cow does, a zombie
/// doesn't); for the others a sneak-use goes to the block behind the mob (or to the item, in the
/// air). Java fires the interaction for every living entity: a mob in the player's line of sight
/// within reach takes the use here instead (and the backpack is not placed / opened).
bool inUseCooldown(Player& player) {
    auto it = gLastUse.find(player.getOrCreateUniqueID().rawID);
    return it != gLastUse.end() && gTick - it->second < kUseCooldownTicks;
}

bool onUseTowardMob(Player& player) {
    if (gInstalled && !player.isClientSide() && player.isSneaking() && inUseCooldown(player) && heldCatcherBackpackImpl(player)) return true;
    if (!gInstalled || player.isClientSide() || !player.isSneaking() || !heldCatcherBackpackImpl(player)) return false;
    constexpr float kReach = 5.0f; // Bedrock's entity pick range (creative); survival picks up to 3
    auto            hit    = player.traceRay(kReach, true, true);
    if (hit.mType != HitResultType::Entity) {
        auto v = player.getViewVector();
        log().debug("mob catcher: {} sneak-used a catcher backpack with no mob in sight (hit {}, view {:.2f},{:.2f},{:.2f})", player.getRealName(), static_cast<int>(hit.mType), v.x, v.y, v.z);
        return false;
    }
    auto actor = hit.mEntity.tryUnwrap();
    if (!actor || actor.as_ptr() == &player) return false;
    float range = player.isCreative() ? 5.0f : 3.0f;
    if (player.getHeadPos().distanceTo(hit.mPos) > range + 0.01f) return false;
    return onInteract(player, *actor);
}

void serve(Level& level) {
    ++gTick;
    gSeenThisTick.clear();
    if (gPending.empty()) return;
    auto pending = std::move(gPending);
    gPending.clear();
    for (auto const& job : pending) {
        auto* player = level.getPlayer(ActorUniqueID{job.player});
        auto* target = level.fetchEntity(ActorUniqueID{job.target}, false);
        if (!player || !target) continue;
        bool advanced = false;
        auto bp       = heldCatcherBackpackImpl(*player, &advanced);
        if (!bp) continue;
        try {
            auto result = capture(*player, bp, *target, advanced);
            if (!result.success) log().info("mob catcher: {} could not capture {}: {}", player->getRealName(), target->getTypeName(), result.message);
            showMessage(*player, result);
        } catch (std::exception const& e) {
            log().error("mob catcher capture failed: {}", e.what());
        }
    }
}

// LL_TYPE_INSTANCE_HOOK declares an unused `_AutoHookCount` in manual-registration mode.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"

/// Every entity interaction ends here (GameMode::interact for real clients, simulated players'
/// interactions directly), before the entity's own interaction (milking, trading, leashing, ...).
LL_TYPE_INSTANCE_HOOK(
    McPlayerInteractHook,
    ll::memory::HookPriority::Normal,
    Player,
    &Player::interact,
    InteractionResult,
    Actor&      actor,
    Vec3 const& location
) {
    if (onInteract(*this, actor)) {
        InteractionResult consumed{};
        consumed.mSuccess = true;
        consumed.mSwing   = true;
        return consumed;
    }
    return origin(actor, location);
}

#pragma clang diagnostic pop

class MobCatcherModule {
public:
    void install() {
        McPlayerInteractHook::hook();
        auto& bus = ll::event::EventBus::getInstance();
        mTick     = bus.emplaceListener<ll::event::ServerLevelTickEvent>([](ll::event::ServerLevelTickEvent& ev) { serve(ev.level()); });
        // Before the placed-backpack (High) and open-on-use handlers.
        mUseOn = bus.emplaceListener<ll::event::PlayerInteractBlockEvent>(
            [](ll::event::PlayerInteractBlockEvent& ev) {
                if (!ev.isCancelled() && onUseTowardMob(ev.self())) ev.cancel();
            },
            ll::event::EventPriority::Highest
        );
        mUse = bus.emplaceListener<ll::event::PlayerUseItemEvent>(
            [](ll::event::PlayerUseItemEvent& ev) {
                if (!ev.isCancelled() && onUseTowardMob(ev.self())) ev.cancel();
            },
            ll::event::EventPriority::Highest
        );
        gInstalled = true;
    }
    void uninstall() {
        gInstalled = false;
        gPending.clear();
        gSeenThisTick.clear();
        gLastUse.clear();
        auto& bus = ll::event::EventBus::getInstance();
        for (auto* l : {&mTick, &mUseOn, &mUse}) {
            if (*l) bus.removeListener(*l);
            l->reset();
        }
        McPlayerInteractHook::unhook();
    }

private:
    ll::event::ListenerPtr mTick;
    ll::event::ListenerPtr mUseOn;
    ll::event::ListenerPtr mUse;
};

} // namespace

// ---- public API -----------------------------------------------------------------------------------------

std::shared_ptr<game::Backpack> heldCatcherBackpack(Player& player, bool* advanced) {
    return heldCatcherBackpackImpl(player, advanced);
}

std::vector<CapturedMob> capturedMobs(game::Backpack const& backpack) {
    auto const& settings = backpack.data().settings;
    auto        it       = settings.find(kCapturedMobsKey);
    return it == settings.end() ? std::vector<CapturedMob>{} : parseMobs(*it);
}

void saveCapturedMobs(game::Backpack& backpack, std::vector<CapturedMob> const& mobs) {
    auto& settings = backpack.data().settings;
    if (mobs.empty()) settings.erase(kCapturedMobsKey);
    else settings[kCapturedMobsKey] = mobsToJson(mobs);
    backpack.markDirty();
    backpack.rebuildHooks();
}

int columnsOf(game::Backpack const& backpack) {
    // SB getColumns: the layout's width (9 / 12) minus the columns taken by Tank / Battery parts.
    int taken = upgrade::columns::expected(backpack);
    int base  = core::capacity::baseSlotsFor(backpack.data().inventory.size(), taken);
    return std::max(1, columnsFor(base) - taken);
}

std::optional<bool> bestCatcher(game::Backpack const& backpack) {
    std::optional<bool> best;
    for (auto const& s : backpack.data().upgrades) {
        if (s.empty()) continue;
        auto* type = upgradeFor(s.item);
        if (!type || type->info().groupOrId() != kGroup || !typeAllowed(*type)) continue;
        bool advanced = type->info().id == kAdvancedId;
        if (!best || advanced) best = advanced;
    }
    return best;
}

bool isHostile(Actor const& actor) {
    if (matchesList(actor, config().passiveOverrides)) return false;
    return matchesList(actor, config().hostileOverrides) || actor.hasCategory(ActorCategory::Monster)
        || actor.hasFamily(HashedString("monster"));
}

CaptureResult capture(Player& player, std::shared_ptr<game::Backpack> const& backpack, Actor& target, bool advanced) {
    if (!backpack) return {};
    if (auto err = eligibilityError(player, target, advanced)) return fail(player, *err);
    if (!Mob::tryGetFromEntity(target.getEntityContext(), false) || !target.isAlive()) {
        return fail(player, message(player, "abp.mobcatcher.status.invalid_entity", "That mob cannot be captured"));
    }

    bool   hostile    = isHostile(target);
    double maxHealth  = effectiveMaxHealth(target.getMaxHealth(), target.getHealth());
    int    cost       = slotCost(target.getMaxHealth(), target.getHealth(), hostile ? hostileMultiplier() : animalMultiplier());
    int    maxCost    = advanced ? advancedMax() : basicMax();
    if (cost > maxCost) {
        return fail(player, trFor(player, "abp.mobcatcher.status.too_large", "Mob needs {} slots, more than this upgrade allows ({})", cost, maxCost));
    }

    auto const& box    = target.getAABB();
    float       width  = std::max(box.max.x - box.min.x, box.max.z - box.min.z);
    float       height = box.max.y - box.min.y;
    auto        fp     = footprintFor(width, height, cost);
    auto        mobs   = capturedMobs(*backpack);
    int         cols   = columnsOf(*backpack);
    auto        slot   = findEmptyRectangle(backpack->inventory(), cols, fp, mobs);
    if (!slot) {
        return fail(player, trFor(player, "abp.mobcatcher.status.no_space", "No empty {}x{} slot area available", fp.width, fp.height));
    }

    CompoundTag tag;
    if (!target.save(tag)) return fail(player, message(player, "abp.mobcatcher.status.invalid_entity", "That mob cannot be captured"));

    CapturedMob m;
    m.id            = core::newUuid();
    m.entityType    = target.getTypeName();
    m.nbt           = core::base64Encode(tag.toBinaryNbt());
    m.slot          = *slot;
    m.width         = fp.width;
    m.height        = fp.height;
    m.slotCost      = cost;
    m.hostile       = hostile;
    m.displayName   = target.getNameTag();
    m.currentHealth = static_cast<int>(std::ceil(std::max(0, target.getHealth())));
    m.maxHealth     = static_cast<int>(std::ceil(maxHealth));
    m.look          = nlohmann::json{
        {"bbWidth",  width                                                          },
        {"bbHeight", height                                                         },
        {"noGround", target.canFly() || target.hasCategory(ActorCategory::WaterAnimal)},
        {"columns",  cols                                                           },
        {"data",     syncedData(target)                                             },
    };
    auto effectAt = collapsePosition(player, target);
    mobs.push_back(m);
    saveCapturedMobs(*backpack, mobs);
    target.despawn();
    captureEffect(player, effectAt);
    pushRenderInfo(player, *backpack);
    log().info("mob catcher: {} captured {} into {} at slot {} ({}x{}, cost {})", player.getRealName(), m.entityType, backpack->uuid(), m.slot, m.width, m.height, cost);
    return {true, trFor(player, "abp.mobcatcher.status.captured", "Captured {}", mobName(m))};
}

CaptureResult release(Player& player, std::shared_ptr<game::Backpack> const& backpack, std::string const& id) {
    if (!backpack) return {};
    auto mobs = capturedMobs(*backpack);
    auto it   = std::find_if(mobs.begin(), mobs.end(), [&](CapturedMob const& m) { return m.id == id; });
    if (it == mobs.end()) return {};
    auto failed = [&] {
        return CaptureResult{false, message(player, "abp.mobcatcher.status.release_failed", "Could not release mob there")};
    };

    auto bytes = core::base64Decode(it->nbt);
    if (!bytes) return failed();
    auto tag = CompoundTag::fromBinaryNbt(*bytes);
    if (!tag) return failed();

    Dims d;
    d.width    = it->look.value("bbWidth", 0.6f);
    d.height   = it->look.value("bbHeight", 1.8f);
    d.noGround = it->look.value("noGround", false);
    auto pos   = releasePosition(player, d);
    if (!pos) {
        playMobCatcherSound(player, "note.bass", 0.7f, 0.8f);
        return {false, message(player, "abp.mobcatcher.status.no_release_space", "No valid release space there")};
    }

    float yaw            = player.getRotation().y;
    (*tag)["Pos"]        = ListTag{pos->x, pos->y, pos->z};
    (*tag)["Rotation"]   = ListTag{yaw, 0.0f};
    auto& bs             = player.getDimensionBlockSource();
    auto  spawned        = bs.spawnActor(*tag);
    if (!spawned) return failed();
    spawned->teleport(*pos, player.getDimensionId(), Vec2{0.f, yaw});

    CapturedMob released = *it;
    mobs.erase(it);
    saveCapturedMobs(*backpack, mobs);
    pushRenderInfo(player, *backpack);
    playMobCatcherSound(player, "random.pop", 0.7f, 1.2f);
    log().info("mob catcher: {} released {} from {}", player.getRealName(), released.entityType, backpack->uuid());
    return {true, trFor(player, "abp.mobcatcher.status.released", "Released {}", mobName(released))};
}

void showMessage(Player& player, CaptureResult const& result) {
    if (result.message.empty() || player.isSimulatedPlayer()) return;
    auto                           pkt = TextPacket{};
    TextPacketPayload::MessageOnly body;
    body.mType = TextPacketType::Tip;
    body.mMessage->assign(result.message);
    pkt.mBody = body;
    pkt.sendTo(player);
}

} // namespace abp::upgrade::mobcatcher

ABP_REGISTER_MODULE(abp::upgrade::mobcatcher::MobCatcherModule);

// ---- e2e helper -------------------------------------------------------------------------------------------

/// `/backpack mobcatcher setup` (netherite backpack, advanced catcher) and `/backpack mobcatcher
/// basic` (iron backpack, basic catcher): the backpack is put into the selected hotbar slot.
ABP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    auto give = [](CommandOrigin const& origin, CommandOutput& output, bool advanced) {
        using namespace abp;
        auto* e = origin.getEntity();
        if (!e || !e->isPlayer() || !game::Storage::get().available()) {
            output.error("players only");
            return;
        }
        auto& player = static_cast<Player&>(*e);
        auto  bp     = game::Storage::get().create(advanced ? core::Tier::Netherite : core::Tier::Iron);
        auto* type   = upgrade::byId(advanced ? upgrade::mobcatcher::kAdvancedId : upgrade::mobcatcher::kBasicId);
        if (!bp || !type) {
            output.error("could not create a backpack");
            return;
        }
        auto item = game::ItemConv::toData(game::ItemConv::make(type->info().itemId, 1));
        (void)upgrade::install(bp, 0, item, &player);
        auto stack = game::makeBackpackItem(bp->tier(), bp->uuid());
        game::setItemAt(player, game::selectedSlot(player), stack);
        log().info("mobcatcher setup: gave {} backpack {} with the {} upgrade", advanced ? "netherite" : "iron", bp->uuid(), type->info().id);
        output.success("ok");
    };
    // `/backpack mobcatcher capture{cow,zombie}`: spawns the mob next to the player and captures it
    // into the held backpack server-side (the same capture as sneak + use; e2e setup).
    auto spawnAndCapture = [](CommandOrigin const& origin, CommandOutput& output, std::string const& type) {
        using namespace abp;
        auto* e = origin.getEntity();
        if (!e || !e->isPlayer()) {
            output.error("players only");
            return;
        }
        auto& player   = static_cast<Player&>(*e);
        bool  advanced = false;
        auto  bp       = upgrade::mobcatcher::heldCatcherBackpack(player, &advanced);
        if (!bp) {
            output.error("hold a backpack with a mob catcher");
            return;
        }
        auto  pos = player.getPosition();
        auto* mob = player.getLevel().getSpawner().spawnMob(
            player.getDimensionBlockSource(), ActorDefinitionIdentifier(type), nullptr, Vec3{pos.x + 2.f, pos.y, pos.z}, false, true, false
        );
        if (!mob) {
            output.error("could not spawn " + type);
            return;
        }
        auto r = upgrade::mobcatcher::capture(player, bp, *mob, advanced);
        if (!r.success) {
            log().info("mob catcher: {} could not capture {}: {}", player.getRealName(), type, r.message);
            mob->despawn();
        }
        upgrade::mobcatcher::showMessage(player, r);
        output.success(r.message);
    };
    cmd.overload().text("mobcatcher").text("capturecow").execute([spawnAndCapture](CommandOrigin const& o, CommandOutput& out) {
        spawnAndCapture(o, out, "minecraft:cow");
    });
    cmd.overload().text("mobcatcher").text("capturezombie").execute([spawnAndCapture](CommandOrigin const& o, CommandOutput& out) {
        spawnAndCapture(o, out, "minecraft:zombie");
    });
    cmd.overload().text("mobcatcher").text("setup").execute([give](CommandOrigin const& o, CommandOutput& out) { give(o, out, true); });
    cmd.overload().text("mobcatcher").text("basic").execute([give](CommandOrigin const& o, CommandOutput& out) { give(o, out, false); });
});
