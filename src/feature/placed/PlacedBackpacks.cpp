#include "feature/placed/PlacedBackpacks.h"
#include "feature/placed/ContainerIO.h"
#include "feature/placed/DisplayEntity.h"

#include <cmath>
#include <exception>
#include <unordered_set>

#include "ll/api/command/CommandHandle.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/player/PlayerUseItemEvent.h"
#include "ll/api/event/server/ServerStartedEvent.h"
#include "ll/api/event/server/ServerStoppingEvent.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/world/Facing.h"
#include "mc/world/actor/player/AbilitiesIndex.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/storage/LevelStorage.h"
#include "mc/world/level/storage/db_helpers/Category.h"
#include "mc/world/phys/AABB.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/level/Explosion.h"
#include "ll/api/memory/Hook.h"
#include "core/Fluid.h"
#include "core/render/BackpackModel.h"
#include "feature/model/RenderModel.h"
#include "core/Comparator.h"
#include "upgrade/tank/Tank.h"

#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/Storage.h"
#include "mod/Commands.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/inception/Inception.h"
#include "upgrade/jukebox/Jukebox.h"

ABP_REGISTER_CONFIG("placed", abp::placed::PlacedConfig);

namespace abp::placed {

/// Marks block changes we make ourselves (placing, picking up, removing orphans): the
/// block-changed handler ignores them.
class PlacedGuard {
public:
    explicit PlacedGuard(PlacedBackpacks& p) : mP(p) { ++mP.mGuard; }
    ~PlacedGuard() { --mP.mGuard; }
    PlacedGuard(PlacedGuard const&)            = delete;
    PlacedGuard& operator=(PlacedGuard const&) = delete;

private:
    PlacedBackpacks& mP;
};

namespace {
constexpr std::string_view kDbKey         = "abp:placed";
constexpr int              kDataVersion   = 1;
constexpr std::int64_t     kActionCooldown = 5;    ///< ticks: SB puts a 5 tick item cooldown on a picked up backpack; also debounces repeated use events
constexpr float            kMaxUseDistSq   = 8.0f * 8.0f;
constexpr int              kUpdateAll      = 3;    ///< neighbours + clients
constexpr int              kMismatchChecks = 20;   ///< consecutive checks before a missing block counts

LevelStorage* levelStorage() {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    return &level->getLevelStorage();
}

BlockType const* typeOf(std::string const& id) { return BlockType::tryGetFromRegistry(HashedString(id)).as_ptr(); }

Block const* withState(Block const* block, std::string_view name, int value) {
    if (!block) return nullptr;
    auto const* state = block->getBlockType().getBlockState(HashedString(std::string(name)));
    if (!state) return block;
    if (auto b = block->setState(*state, value)) return b.as_ptr();
    return block;
}
Block const* withState(Block const* block, std::string_view name, bool value) {
    if (!block) return nullptr;
    auto const* state = block->getBlockType().getBlockState(HashedString(std::string(name)));
    if (!state) return block;
    if (auto b = block->setState(*state, value)) return b.as_ptr();
    return block;
}

/// The placed-backpack block for `tier` facing `facing` with `parts`.
Block const* blockFor(core::Tier tier, int facing, BlockParts const& parts = {}) {
    auto const* type = typeOf(blockIdFor(tier));
    if (!type || !type->mDefaultState) return nullptr;
    Block const* block = type->mDefaultState;
    block              = withState(block, kFacingState, facing & 3);
    block              = withState(block, kLeftTankState, parts.leftTank);
    block              = withState(block, kRightTankState, parts.rightTank);
    block              = withState(block, kBatteryState, parts.battery);
    block              = withState(block, kOpenState, parts.open);
    return block;
}

/// Sound at a block for the players nearby (SB plays wool sounds with its own volume / pitch).
void playSoundAt(BlockSource& region, BlockPos const& pos, std::string const& name, float volume, float pitch) {
    auto  center = pos.center();
    int   dim    = static_cast<int>(region.getDimensionId());
    region.getLevel().forEachPlayer([&](Player& p) {
        if (static_cast<int>(p.getDimensionId()) != dim || p.getPosition().distanceToSqr(center) > 16.0f * 16.0f * volume * volume + 1) return true;
        auto  pkt = MinecraftPackets::createPacket(MinecraftPacketIds::PlaySound);
        auto& s   = static_cast<PlaySoundPacket&>(*pkt);
        s.mName   = name;
        s.mPos    = BlockPos{static_cast<int>(std::floor(center.x * 8.0f)), static_cast<int>(std::floor(center.y * 8.0f)), static_cast<int>(std::floor(center.z * 8.0f))};
        s.mVolume = volume;
        s.mPitch  = pitch;
        s.mLoopCount                = 0;
        s.mBypassListenerRangeCheck = false;
        s.mServerSoundHandle->reset();
        s.mPlaybackPositionSeconds->reset();
        s.sendTo(p);
        return true;
    });
}
// Java SoundType.WOOL: volume 1, pitch 1. SB plays (volume + 1) / 2 and pitch * 0.8.
constexpr float kWoolVolume = (1.0f + 1.0f) / 2.0f;
constexpr float kWoolPitch  = 1.0f * 0.8f;

/// Set while an explosion runs on this thread (Everlasting backpacks survive it).
thread_local int tExploding = 0;

bool isFluidContainer(ItemStack const& stack) {
    if (stack.isNull()) return false;
    auto data = game::ItemConv::toData(stack);
    return core::fluid::contentsOf(data.id, data.aux).has_value() || core::fluid::isEmptyContainer(data.id);
}

/// Buckets (empty or filled): the items Java gives a fluid-handler capability.
bool isBucket(ItemStack const& stack) {
    if (stack.isNull()) return false;
    auto id = std::string(stack.getTypeName());
    return id == "minecraft:bucket" || id.ends_with("_bucket");
}

bool hasEverlasting(game::Backpack const& bp) { return upgrade::hasActive(bp, "everlasting"); }

BlockPos posOf(BlockLocation const& l) { return BlockPos{l.x, l.y, l.z}; }

/// The held backpack has an active upgrade that acts on sneak + use on a container (Deposit /
/// Restock, unit U10): such a click must deposit / restock, not place the backpack.
bool holdsInteractionBackpack(Player const& player) {
    auto const& item = player.getSelectedItem();
    if (!game::isBackpack(item)) return false;
    auto uuid = game::backpackUuid(item);
    if (!uuid) return false;
    auto bp = game::Storage::get().find(*uuid);
    return bp && (upgrade::hasActive(*bp, "deposit") || upgrade::hasActive(*bp, "restock"));
}

bool sameDimension(Player const& p, BlockLocation const& l) { return static_cast<int>(p.getDimensionId()) == l.dim; }

float distSq(Player const& p, BlockLocation const& l) {
    auto const& pos = p.getPosition();
    float       dx  = pos.x - (static_cast<float>(l.x) + 0.5f);
    float       dy  = pos.y - (static_cast<float>(l.y) + 0.5f);
    float       dz  = pos.z - (static_cast<float>(l.z) + 0.5f);
    return dx * dx + dy * dy + dz * dz;
}

std::string titleFor(Player& player, PlacedEntry const& e, game::Backpack const& bp) {
    auto stack = game::ItemConv::toStack(e.item, 1);
    auto name  = stack.getCustomName();
    if (!name.empty()) return name;
    static constexpr std::string_view fallbacks[] =
        {"Leather Backpack", "Copper Backpack", "Iron Backpack", "Gold Backpack", "Diamond Backpack", "Netherite Backpack"};
    return trFor(
        player,
        "abp.backpack." + std::string(core::tierInfo(bp.tier()).id),
        fallbacks[static_cast<int>(bp.tier())]
    );
}

/// The backpack GUI of a placed backpack: closes when the block goes away or the player leaves.
class PlacedBackpackMenu : public ui::BackpackMenu {
public:
    PlacedBackpackMenu(std::shared_ptr<game::Backpack> bp, BlockLocation loc, std::string title)
    : BackpackMenu(std::move(bp), std::nullopt, std::move(title)),
      mLoc(loc) {}

    [[nodiscard]] BlockLocation const& loc() const { return mLoc; }

    [[nodiscard]] core::ItemStackData openedItem(Player&) const override {
        auto e = PlacedBackpacks::get().at(mLoc);
        return e ? e->item : core::ItemStackData{};
    }

    void onTick(Player& player) override {
        auto e = PlacedBackpacks::get().at(mLoc);
        if (!e || e->uuid != backpack()->uuid() || !sameDimension(player, mLoc) || distSq(player, mLoc) > kMaxUseDistSq) {
            ui::MenuManager::get().close(player);
            return;
        }
        BackpackMenu::onTick(player);
    }

private:
    BlockLocation mLoc;
};

nlohmann::json toJson(PlacedEntry const& e) {
    return nlohmann::json{
        {"dim",    e.loc.dim        },
        {"x",      e.loc.x          },
        {"y",      e.loc.y          },
        {"z",      e.loc.z          },
        {"uuid",   e.uuid           },
        {"facing", e.facing         },
        {"item",   e.item.toJson()  },
    };
}

std::optional<PlacedEntry> fromJson(nlohmann::json const& j) {
    if (!j.is_object() || !j.contains("uuid")) return std::nullopt;
    PlacedEntry e;
    e.loc    = {j.value("dim", 0), j.value("x", 0), j.value("y", 0), j.value("z", 0)};
    e.uuid   = j.value("uuid", std::string{});
    e.facing = j.value("facing", 2);
    if (j.contains("item")) e.item = core::ItemStackData::fromJson(j["item"]);
    if (e.uuid.empty()) return std::nullopt;
    return e;
}
} // namespace

BlockLocation locationOf(BlockSource const& region, BlockPos const& pos) {
    return {static_cast<int>(region.getDimensionId()), pos.x, pos.y, pos.z};
}

PlacedBackpacks& PlacedBackpacks::get() {
    static PlacedBackpacks instance;
    return instance;
}

// ---- queries ------------------------------------------------------------------------------------

std::optional<PlacedEntry> PlacedBackpacks::at(BlockLocation const& loc) const {
    if (auto it = mEntries.find(loc); it != mEntries.end()) return it->second;
    return std::nullopt;
}

std::optional<PlacedEntry> PlacedBackpacks::findUuid(std::string_view uuid) const {
    if (auto it = mByUuid.find(std::string(uuid)); it != mByUuid.end()) return at(it->second);
    return std::nullopt;
}

std::vector<PlacedEntry> PlacedBackpacks::entries() const {
    std::vector<PlacedEntry> out;
    out.reserve(mEntries.size());
    for (auto const& [_, e] : mEntries) out.push_back(e);
    return out;
}

std::shared_ptr<game::Backpack> PlacedBackpacks::backpackAt(BlockSource& region, BlockPos const& pos) {
    if (mEntries.empty()) return nullptr;
    auto it = mEntries.find(locationOf(region, pos));
    if (it == mEntries.end()) return nullptr;
    if (!isBackpackBlock(region.getBlock(pos))) return nullptr;
    return game::Storage::get().find(it->second.uuid);
}

std::string blockIdFor(core::Tier tier) { return "alleviatedbackpacks:placed_" + std::string(core::tierInfo(tier).id) + "_backpack"; }

std::vector<std::string> blockIds() {
    std::vector<std::string> out;
    for (auto const& t : core::kTiers) out.push_back(blockIdFor(t.tier));
    out.emplace_back(kBlockId);
    return out;
}

std::optional<core::Tier> PlacedBackpacks::tierOfBlock(Block const& block) {
    auto const& name = block.getTypeName();
    if (name == kBlockId) {
        if (auto v = stateOf(block, kTierState)) return core::tierFromInt(*v);
        return core::Tier::Leather;
    }
    constexpr std::string_view prefix = "alleviatedbackpacks:placed_";
    constexpr std::string_view suffix = "_backpack";
    if (!name.starts_with(prefix) || !name.ends_with(suffix) || name.size() <= prefix.size() + suffix.size()) return std::nullopt;
    return core::tierFromId(std::string_view(name).substr(prefix.size(), name.size() - prefix.size() - suffix.size()));
}

bool PlacedBackpacks::isBackpackBlock(Block const& block) { return tierOfBlock(block).has_value(); }

std::optional<int> PlacedBackpacks::stateOf(Block const& block, std::string_view state) {
    auto const& name = block.getTypeName();
    if (name != kBlockId && !name.starts_with("alleviatedbackpacks:placed_")) return std::nullopt;
    auto const* s = block.getBlockType().getBlockState(HashedString(std::string(state)));
    if (!s) return std::nullopt;
    return block.getState<int>(*s);
}

std::optional<bool> PlacedBackpacks::boolStateOf(Block const& block, std::string_view state) {
    if (!isBackpackBlock(block)) return std::nullopt;
    auto const* s = block.getBlockType().getBlockState(HashedString(std::string(state)));
    if (!s) return std::nullopt;
    return block.getState<bool>(*s);
}

int PlacedBackpacks::facingTowards(float yaw) {
    // Bedrock yaw: 0 = looking south (+z), 90 = west, 180 = north, 270 = east. The backpack's front
    // looks back at the player: the opposite of the player's look direction.
    int look = static_cast<int>(std::floor(yaw * 4.0f / 360.0f + 0.5f)) & 3; // 0 south, 1 west, 2 north, 3 east
    return (look + 2) & 3;
}

// ---- persistence --------------------------------------------------------------------------------

void PlacedBackpacks::load() {
    mEntries.clear();
    mSignals.clear();
    mRestore.clear();
    mByUuid.clear();
    mOrphans.clear();
    mPendingDrops.clear();
    mMismatches.clear();
    auto* db = levelStorage();
    if (!db) return;
    std::string buffer;
    if (!db->loadData(std::string(kDbKey), buffer, DBHelpers::Category::Uncategorized)) return;
    auto j = nlohmann::json::parse(buffer, nullptr, false);
    if (!j.is_object() || !j["entries"].is_array()) {
        log().error("placed backpacks: stored data is unreadable");
        return;
    }
    for (auto const& item : j["entries"]) {
        if (auto e = fromJson(item)) add(std::move(*e));
    }
    if (j.contains("pending") && j["pending"].is_array()) {
        for (auto const& item : j["pending"]) {
            if (auto e = fromJson(item)) mPendingDrops.push_back(std::move(*e));
        }
    }
    if (!mEntries.empty()) log().info("{} placed backpack(s) loaded", mEntries.size());
}

void PlacedBackpacks::save() const {
    auto* db = levelStorage();
    if (!db) return;
    auto list = nlohmann::json::array();
    for (auto const& [_, e] : mEntries) list.push_back(toJson(e));
    auto pending = nlohmann::json::array();
    for (auto const& e : mPendingDrops) pending.push_back(toJson(e));
    nlohmann::json j{
        {"version", kDataVersion},
        {"entries", std::move(list)},
        {"pending", std::move(pending)},
    };
    db->saveData(std::string(kDbKey), j.dump(), DBHelpers::Category::Uncategorized);
}

namespace {
PlacedBackpacks::PlacementHooks& placementHooks() {
    static PlacedBackpacks::PlacementHooks hooks;
    return hooks;
}
} // namespace

void PlacedBackpacks::setPlacementHooks(PlacementHooks hooks) { placementHooks() = std::move(hooks); }

bool PlacedBackpacks::sharesUuid(core::ItemStackData const& a, core::ItemStackData const& b) {
    auto const& may = placementHooks().mayShareUuid;
    return may && may(a, b);
}

void PlacedBackpacks::add(PlacedEntry entry) {
    auto shared = [&](BlockLocation const& other) {
        auto it = mEntries.find(other);
        return it != mEntries.end() && sharesUuid(it->second.item, entry.item);
    };
    if (auto old = mByUuid.find(entry.uuid); old != mByUuid.end() && !(old->second == entry.loc) && !shared(old->second)) {
        // The same backpack can't stand in two places: keep the newest.
        log().warn("placed backpack {} was registered at two positions; keeping the newest", entry.uuid);
        mEntries.erase(old->second);
    }
    mByUuid[entry.uuid] = entry.loc;
    mEntries[entry.loc] = std::move(entry);
}

void PlacedBackpacks::erase(BlockLocation const& loc) {
    auto it = mEntries.find(loc);
    if (it == mEntries.end()) return;
    auto uuid = it->second.uuid;
    mSignals.erase(loc);
    // The entity drawing it goes too.
    if (auto level = ll::service::getLevel()) {
        if (auto dimension = level->getDimension(loc.dim).lock()) {
            auto& region = dimension->getBlockSourceFromMainChunkSource();
            if (region.hasChunksAt(posOf(loc), 0, false)) display::remove(region, loc);
        }
    }    mEntries.erase(it);
    // Another endpoint of a linked group may stand for the same storage: it takes over.
    std::optional<BlockLocation> other;
    for (auto const& [l, e] : mEntries) {
        if (e.uuid == uuid) other = l;
    }
    if (auto u = mByUuid.find(uuid); u != mByUuid.end() && u->second == loc) {
        if (other) u->second = *other;
        else mByUuid.erase(u);
    }
    // Its jukeboxes stop playing at the block (picked up, broken, dropped).
    if (!other) upgrade::jukebox::Service::get().stopAll(uuid);
}

// ---- actions ------------------------------------------------------------------------------------

PlacedBackpacks::PlaceResult PlacedBackpacks::place(BlockSource& region, BlockPos const& pos, ItemStack& item, int facing) {
    auto tier = game::backpackTierOf(item);
    if (!tier) return PlaceResult::NotBackpack;
    auto& storage = game::Storage::get();
    if (!storage.available()) return PlaceResult::NoStorage;
    if (pos.y < region.getMinHeight() || pos.y >= region.getMaxHeight()) return PlaceResult::Blocked;
    auto const& current = region.getBlock(pos);
    if (!current.isAir() && !current.getBlockType().canBeBuiltOver(current, region, pos)) return PlaceResult::Blocked;
    if (mEntries.contains(locationOf(region, pos))) return PlaceResult::Blocked;
    if (!typeOf(blockIdFor(*tier))) return PlaceResult::Failed;
    // SB: waterlogged when placed into water (Bedrock: water in the block's liquid layer).
    bool waterlog = current.getTypeName() == "minecraft:water" || current.getTypeName() == "minecraft:flowing_water";
    bool changed = false;
    auto bp      = storage.forItem(item, &changed); // binds a uuid on first use
    if (!bp) return PlaceResult::NoStorage;
    if (auto other = findUuid(bp->uuid()); other && !sharesUuid(other->item, game::ItemConv::toData(item))) {
        // An item bound to a backpack that is already placed (a copy): refuse instead of
        // ending up with two blocks for one backpack. Linked-storage endpoints share their group's
        // storage on purpose (placement hooks).
        log().warn("backpack {} is already placed at {} {} {}", bp->uuid(), other->loc.x, other->loc.y, other->loc.z);
        return PlaceResult::Failed;
    }
    auto* block = blockFor(*tier, facing, partsFor(locationOf(region, pos), *bp));
    if (!block) return PlaceResult::Failed;
    {
        PlacedGuard guard(*this);
        BlockChangeContext ctx{};
        if (!region.setBlock(pos, *block, kUpdateAll, nullptr, ctx) || !isBackpackBlock(region.getBlock(pos))) {
            return PlaceResult::Failed;
        }
        if (waterlog) {
            if (auto const* water = typeOf("minecraft:water"); water && water->mDefaultState) region.setExtraBlock(pos, *water->mDefaultState, kUpdateAll);
        }
    }
    PlacedEntry e;
    e.loc    = locationOf(region, pos);
    e.uuid   = bp->uuid();
    e.item   = game::ItemConv::toData(item).withCount(1);
    e.facing = facing & 3;
    add(std::move(e));
    storage.save(*bp);
    save();
    log().debug("backpack {} placed at {} {} {}", bp->uuid(), pos.x, pos.y, pos.z);
    return PlaceResult::Ok;
}

std::optional<ItemStack> PlacedBackpacks::take(BlockSource& region, BlockPos const& pos) {
    auto loc = locationOf(region, pos);
    auto it  = mEntries.find(loc);
    if (it == mEntries.end()) return std::nullopt;
    auto entry = it->second;
    erase(loc);
    save();
    if (isBackpackBlock(region.getBlock(pos))) {
        PlacedGuard        guard(*this);
        BlockChangeContext ctx{};
        region.removeBlock(pos, ctx);
    }
    if (auto bp = game::Storage::get().find(entry.uuid)) game::Storage::get().save(*bp);
    return itemOf(entry);
}

ItemStack PlacedBackpacks::itemOf(PlacedEntry const& entry) {
    auto stack = game::ItemConv::toStack(entry.item, 1);
    if (stack.isNull() || !game::isBackpack(stack)) {
        // The stored item can't be rebuilt (item format change?): a fresh item bound to the uuid.
        auto bp = game::Storage::get().find(entry.uuid);
        stack   = game::makeBackpackItem(bp ? bp->tier() : core::Tier::Leather, entry.uuid);
    }
    return stack;
}

bool PlacedBackpacks::dropEntry(BlockSource& region, PlacedEntry const& entry) {
    auto stack = itemOf(entry);
    auto pos   = posOf(entry.loc);
    if (!region.getLevel().getSpawner().spawnItem(region, stack, nullptr, pos.center(), 10)) {
        // Never lose it: keep it (persisted) and try again later.
        log().warn("could not drop backpack {} at {} {} {}; retrying", entry.uuid, pos.x, pos.y, pos.z);
        mPendingDrops.push_back(entry);
        save();
        return false;
    }
    log().debug("placed backpack {} at {} {} {} dropped", entry.uuid, pos.x, pos.y, pos.z);
    return true;
}

bool PlacedBackpacks::placeFromHand(Player& player, BlockPos const& clicked, int face) {
    if (!config::get<PlacedConfig>().allowPlacing) return false;
    if (!player.canUseAbility(AbilitiesIndex::Build)) return false;
    int  slot = player.getSelectedItemSlot();
    auto item = ItemStack(player.getSelectedItem());
    if (!game::isBackpack(item)) return false;
    auto& region = player.getDimensionBlockSource();
    auto  target = clicked;
    auto const& clickedBlock = region.getBlock(clicked);
    bool replace = !isBackpackBlock(clickedBlock) && !clickedBlock.isAir()
                && clickedBlock.getBlockType().canBeBuiltOver(clickedBlock, region, clicked);
    if (!replace) {
        if (face < 0 || face > 5) return false;
        target = clicked + Facing::DIRECTION()[static_cast<std::size_t>(face)];
    }
    if (!region.isUnobstructedByEntities(AABB(target), nullptr)) {
        player.sendMessage(trFor(player, "abp.placed.blocked", "There is no room for the backpack here."));
        return false;
    }
    // Creative: a linked-storage endpoint is placed as a new secondary endpoint, the hand keeps it.
    bool keepInHand = false;
    if (player.isCreative() && placementHooks().creativeCopy) {
        if (auto copy = placementHooks().creativeCopy(player, item)) {
            item       = *copy;
            keepInHand = true;
        }
    }
    auto uuidBefore = game::backpackUuid(item);
    bool creative   = player.isCreative() && !keepInHand;
    ItemStack                placedItem = item;
    std::vector<std::string> nestedClones; ///< storages created for backpacks inside the clone
    if (creative) {
        // SB BackpackItem.tryPlace: in creative the block gets a clone (new uuid, same contents)
        // and the item stays in the hand.
        auto& storage = game::Storage::get();
        auto  src     = storage.forItem(item);
        if (!src) {
            player.sendMessage(trFor(player, "abp.placed.failed", "This backpack can't be placed."));
            return false;
        }
        auto clone = storage.create(src->tier());
        if (!clone) return false;
        auto data = src->data();
        data.uuid = clone->uuid();
        clone->data() = std::move(data);
        clone->rebuildHooks(); // the copied inventory still carries the original's hooks
        // SB cloneSubbackpacks: backpacks inside the clone get their own copies too (one level),
        // so the clone and the original never share a nested storage.
        for (int i = 0; i < clone->inventory().size(); ++i) {
            auto const& slotData = clone->inventory().at(i);
            if (slotData.empty()) continue;
            auto stack = game::ItemConv::toStack(slotData, std::max(1, std::min(slotData.count, 255)));
            if (!game::isBackpack(stack)) continue;
            auto subUuid = game::backpackUuid(stack);
            auto sub     = subUuid ? storage.find(*subUuid) : nullptr;
            if (!sub) continue;
            auto subClone = storage.create(sub->tier());
            if (!subClone) continue;
            auto subData       = sub->data();
            subData.uuid       = subClone->uuid();
            subClone->data()   = std::move(subData);
            subClone->rebuildHooks();
            subClone->markDirty();
            storage.save(*subClone);
            nestedClones.push_back(subClone->uuid());
            game::setBackpackUuid(stack, subClone->uuid());
            auto updated  = game::ItemConv::toData(stack);
            updated.count = slotData.count;
            clone->inventory().set(i, updated);
        }
        clone->rebuildHooks();
        clone->markDirty();
        storage.save(*clone);
        game::setBackpackUuid(placedItem, clone->uuid());
    }
    auto result = place(region, target, placedItem, facingTowards(player.getRotation().y));
    if (creative && result != PlaceResult::Ok) {
        if (auto u = game::backpackUuid(placedItem); u && u != game::backpackUuid(item)) {
            game::Storage::get().erase(*u);
        }
        for (auto const& u : nestedClones) game::Storage::get().erase(u);
    }
    if (result != PlaceResult::Ok) {
        // place() may have bound a fresh uuid to the item before failing: keep that binding.
        if (!keepInHand && game::backpackUuid(item) != uuidBefore) {
            player.getInventory().setItem(slot, item);
            player.sendInventory(false);
        }
        if (result == PlaceResult::Blocked) {
            player.sendMessage(trFor(player, "abp.placed.blocked", "There is no room for the backpack here."));
        } else if (result == PlaceResult::NoStorage || result == PlaceResult::Failed) {
            player.sendMessage(trFor(player, "abp.placed.failed", "This backpack can't be placed."));
        }
        return false;
    }
    // SB: the wool place sound at (volume + 1) / 2, pitch * 0.8.
    playSoundAt(region, target, "use.cloth", kWoolVolume, kWoolPitch);
    // Survival: consumed, the block now is the backpack. Creative: the item stays (SB places a
    // clone; a linked-storage endpoint places another endpoint of the same group).
    auto& inv = player.getInventory();
    if (creative) {
        if (game::backpackUuid(item) != uuidBefore) inv.setItem(slot, item); // bound just now
    } else if (!keepInHand) {
        inv.setItem(slot, ItemStack::EMPTY_ITEM());
    }
    player.sendInventory(false);    mCooldowns[player.getOrCreateUniqueID().rawID] = mNow;
    return true;
}

bool PlacedBackpacks::pickUp(Player& player, BlockPos const& pos) {
    if (!player.canUseAbility(AbilitiesIndex::Build)) return false;
    auto& region = player.getDimensionBlockSource();
    auto  loc    = locationOf(region, pos);
    if (!mEntries.contains(loc) || distSq(player, loc) > kMaxUseDistSq) return false;
    int slot = player.getSelectedItemSlot();
    if (!player.getSelectedItem().isNull()) return false;
    auto uuid = at(loc)->uuid;
    // Admin Infinity upgrades: the infinity unit refuses the gesture before it gets here.
    auto stack = take(region, pos);
    if (!stack) return false;
    // SB putInPlayersHandAndRemove: the wool break sound at (volume + 1) / 2, pitch * 0.8.
    playSoundAt(region, pos, "dig.cloth", kWoolVolume, kWoolPitch);
    closeViewers(&uuid);
    auto& inv = player.getInventory();
    if (inv.getItem(slot).isNull()) {
        inv.setItem(slot, *stack);
    } else if (!player.add(*stack) && !region.getLevel().getSpawner().spawnItem(region, *stack, nullptr, player.getPosition(), 0)) {
        PlacedEntry pending;
        pending.loc  = locationOf(region, BlockPos{static_cast<int>(std::floor(player.getFeetPos().x)), static_cast<int>(std::floor(player.getFeetPos().y)), static_cast<int>(std::floor(player.getFeetPos().z))});
        pending.uuid = game::backpackUuid(*stack).value_or(std::string{});
        pending.item = game::ItemConv::toData(*stack);
        mPendingDrops.push_back(std::move(pending));
        save();
    }
    player.sendInventory(false);
    mCooldowns[player.getOrCreateUniqueID().rawID] = mNow;
    return true;
}

bool PlacedBackpacks::open(Player& player, BlockPos const& pos, ui::BackpackMenu::View view) {
    if (!player.canUseAbility(AbilitiesIndex::OpenContainers)) return false;
    auto& region = player.getDimensionBlockSource();
    auto  loc    = locationOf(region, pos);
    auto  e      = at(loc);
    if (!e || distSq(player, loc) > kMaxUseDistSq) return false;
    auto bp = game::Storage::get().find(e->uuid);
    if (!bp) {
        player.sendMessage(trFor(player, "abp.placed.unreadable", "This backpack's data can't be loaded."));
        return false;
    }
    auto menu = std::make_shared<PlacedBackpackMenu>(bp, loc, titleFor(player, *e, *bp));
    if (view != ui::BackpackMenu::View::Storage) menu->showView(view);
    mCooldowns[player.getOrCreateUniqueID().rawID] = mNow;
    return ui::MenuManager::get().open(player, std::move(menu));
}

bool PlacedBackpacks::openBackpack(Player& player, std::shared_ptr<game::Backpack> const& backpack, ui::BackpackMenu::View view) {
    if (!backpack) return false;
    if (auto e = get().findUuid(backpack->uuid()); e && sameDimension(player, e->loc) && distSq(player, e->loc) <= kMaxUseDistSq) {
        return get().open(player, posOf(e->loc), view);
    }
    return ui::BackpackMenu::openBackpack(player, backpack, {}, view);
}

void PlacedBackpacks::closeViewers(std::string const* uuid) {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto& mm = ui::MenuManager::get();
    std::vector<Player*> toClose;
    level->forEachPlayer([&](Player& p) {
        auto menu = std::dynamic_pointer_cast<ui::BackpackMenu>(mm.current(p));
        if (!menu) return true;
        auto const& id = menu->backpack()->uuid();
        if (uuid) {
            if (id == *uuid) toClose.push_back(&p);
            return true;
        }
        // A plain backpack GUI (e.g. reopened from a settings page) of a placed backpack: same
        // reach rules as the placed GUI. Carried backpacks are bound to their item instead.
        if (std::dynamic_pointer_cast<PlacedBackpackMenu>(menu)) return true;
        auto e = findUuid(id);
        if (!e) return true;
        bool carried = false;
        for (auto const& c : game::scanBackpacks(p)) carried = carried || (c.uuid && *c.uuid == id);
        if (!carried && (!sameDimension(p, e->loc) || distSq(p, e->loc) > kMaxUseDistSq)) toClose.push_back(&p);
        return true;
    });
    for (auto* p : toClose) mm.close(*p);
}

void PlacedBackpacks::retryPendingDrops() {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto pending = std::move(mPendingDrops);
    mPendingDrops.clear();
    for (auto const& e : pending) {
        auto dimension = level->getDimension(e.loc.dim).lock();
        if (!dimension) {
            mPendingDrops.push_back(e);
            continue;
        }
        auto& region = dimension->getBlockSourceFromMainChunkSource();
        if (!region.areChunksFullyLoaded(posOf(e.loc), 0)) {
            mPendingDrops.push_back(e);
            continue;
        }
        dropEntry(region, e); // re-queues itself on failure
    }
    save();
}

// ---- SB block behaviour -------------------------------------------------------------------------------

int PlacedBackpacks::openers(BlockLocation const& loc) const {
    int  n     = 0;
    auto level = ll::service::getLevel();
    if (!level) return 0;
    auto& mm = ui::MenuManager::get();
    level->forEachPlayer([&](Player& p) {
        if (auto menu = std::dynamic_pointer_cast<PlacedBackpackMenu>(mm.current(p)); menu && menu->loc() == loc) ++n;
        return true;
    });
    return n;
}

BlockParts PlacedBackpacks::partsFor(BlockLocation const& loc, game::Backpack& backpack) const {
    auto       m = model::buildModel(backpack);
    BlockParts p;
    p.leftTank  = m.left.has_value();
    p.rightTank = m.right.has_value();
    p.battery   = m.battery.has_value();
    p.open      = openers(loc) > 0;
    return p;
}

int PlacedBackpacks::comparatorSignal(BlockSource& region, BlockPos const& pos) {
    auto bp = backpackAt(region, pos);
    if (!bp) return 0;
    // SB getInventoryForInputOutput: with Inception the backpacks inside count too.
    std::vector<core::BackpackInventory const*> invs;
    for (auto const& b : upgrade::inception::upgradeInventories(bp)) invs.push_back(&b->inventory());
    return core::comparatorSignal(invs);
}

bool PlacedBackpacks::useFluidContainer(Player& player, BlockPos const& pos) {
    auto& region = player.getDimensionBlockSource();
    auto  bp     = backpackAt(region, pos);
    if (!bp) return false;
    auto held = ItemStack(player.getSelectedItem());
    if (held.isNull()) return false;
    auto data    = game::ItemConv::toData(held);
    bool isFull  = core::fluid::contentsOf(data.id, data.aux).has_value();
    bool isEmpty = core::fluid::isEmptyContainer(data.id);
    if (!isFull && !isEmpty) return false;
    // SB useItemOn: a fluid container on a backpack without tanks does nothing (no GUI either).
    if (upgrade::tank::tanks(bp).empty()) return true;
    // SB BackpackBlock.useItemOn: empty the container into the tanks, else fill it from them.
    std::optional<core::ItemStackData> result;
    std::string                        fluidId;
    bool                               emptied = false;
    auto tanks = upgrade::tank::tanks(bp);
    if (isFull) {
        for (auto const& t : tanks) {
            if ((result = upgrade::tank::emptyContainer(data.withCount(1), upgrade::tank::fillerOf(t)))) break;
        }
        if (result) {
            emptied = true;
            fluidId = core::fluid::contentsOf(data.id, data.aux)->fluid.id;
        }
    }
    if (!result && isEmpty) {
        for (auto const& t : tanks) {
            auto fluid = upgrade::tank::read(t).fluid();
            if (fluid.empty()) continue;
            if ((result = upgrade::tank::fillContainer(data.withCount(1), fluid, upgrade::tank::drainerOf(t)))) {
                fluidId = fluid;
                break;
            }
        }
    }
    if (result && isBucket(held)) {
        // FluidUtil.tryEmpty/FillContainerAndStow: the fluid type's bucket sound at the player
        // (volume 1, pitch 1); lava has its own, every other fluid uses the water ones.
        bool lava = fluidId == "minecraft:lava";
        auto name = emptied ? (lava ? "bucket.empty.lava" : "bucket.empty.water") : (lava ? "bucket.fill.lava" : "bucket.fill.water");
        auto at   = player.getPosition();
        playSoundAt(region, BlockPos{static_cast<int>(std::floor(at.x)), static_cast<int>(std::floor(at.y)), static_cast<int>(std::floor(at.z))}, name, 1.0f, 1.0f);
    }    if (result) {
        auto  out  = game::ItemConv::toStack(*result, 1);
        auto& inv  = player.getInventory();
        int   slot = player.getSelectedItemSlot();
        if (player.isCreative()) {
            if (!player.add(out)) region.getLevel().getSpawner().spawnItem(region, out, nullptr, player.getPosition(), 0);
        } else if (held.mCount <= 1) {
            inv.setItem(slot, out);
        } else {
            held.remove(1);
            inv.setItem(slot, held);
            if (!player.add(out)) region.getLevel().getSpawner().spawnItem(region, out, nullptr, player.getPosition(), 0);
        }
        player.sendInventory(false);
        bp->markDirty();
    }
    return true; // a fluid container on a backpack with tanks: never opens the GUI
}

void PlacedBackpacks::maintain(BlockSource& region, PlacedEntry const& e, std::shared_ptr<game::Backpack> const& bp) {
    auto pos = posOf(e.loc);
    // Legacy single block / part and open states (SB refreshRenderState, ContainerOpenersCounter).
    auto const& cur   = region.getBlock(pos);
    auto        tier  = bp->tier();
    auto        parts = partsFor(e.loc, *bp);
    bool legacy = cur.getTypeName() == kBlockId;
    bool stale  = legacy || tierOfBlock(cur) != tier || boolStateOf(cur, kLeftTankState) != parts.leftTank
              || boolStateOf(cur, kRightTankState) != parts.rightTank || boolStateOf(cur, kBatteryState) != parts.battery
              || boolStateOf(cur, kOpenState) != parts.open || stateOf(cur, kFacingState) != (e.facing & 3);
    if (stale) {
        if (auto const* block = blockFor(tier, e.facing, parts)) {
            PlacedGuard        guard(*this);
            BlockChangeContext ctx{};
            region.setBlock(pos, *block, kUpdateAll, nullptr, ctx);
        }
    }
    // Comparators next to it re-read the fill level when it changes.
    int signal = comparatorSignal(region, pos); // with Inception: the backpacks inside too
    if (auto it = mSignals.find(e.loc); it == mSignals.end() || it->second != signal) {
        mSignals[e.loc] = signal;
        region.updateNeighborsAt(pos);
    }
    // The display entity wearing the backpack and holding the displayed item.
    auto                     item = itemOf(e);
    game::setBackpackTint(item, bp->data().mainColor);
    std::optional<ItemStack> shown;
    auto                     m = model::buildModel(*bp);
    if (m.display) shown = game::ItemConv::toStack(m.display->item, 1);
    display::sync(region, e, item, shown);
    // SB BackpackBlock.entityInside: items falling into the block go through the pickup upgrades.
    AABB box(Vec3{static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z)},
             Vec3{static_cast<float>(pos.x + 1), static_cast<float>(pos.y + 1), static_cast<float>(pos.z + 1)});
    for (auto* a : region.getEntities(box, 0.0f, ActorType::ItemEntity)) {
        if (!a || !a->isAlive()) continue;
        auto& ia = static_cast<ItemActor&>(*a);
        auto& stack = *ia.mItem;
        if (stack.isNull() || display::isCopy(stack)) continue;
        auto data   = game::ItemConv::toData(stack);
        int  before = data.count;
        upgrade::forEachInstalled(bp, true, [&](upgrade::IUpgrade& u, upgrade::UpgradeContext const& ctx) {
            if (data.empty()) return;
            try {
                u.onPickup(ctx, data);
            } catch (std::exception const& ex) {
                log().error("upgrade {} onPickup failed: {}", u.info().id, ex.what());
            }
        });
        if (data.count >= before) continue;
        if (data.count <= 0) {
            ia.remove();
        } else {
            stack.set(static_cast<uchar>(std::min(data.count, 255)));
        }
    }
}

void PlacedBackpacks::restoreProtected() {
    auto list = std::move(mRestore);
    mRestore.clear();
    auto level = ll::service::getLevel();
    if (!level) return;
    for (auto const& r : list) {
        auto dimension = level->getDimension(r.entry.loc.dim).lock();
        if (!dimension) continue;
        auto& region = dimension->getBlockSourceFromMainChunkSource();
        auto  pos    = posOf(r.entry.loc);
        if (!region.hasChunksAt(pos, 0, false)) continue;
        if (!isBackpackBlock(region.getBlock(pos))) {
            PlacedGuard        guard(*this);
            BlockChangeContext ctx{};
            region.setBlock(pos, *r.block, kUpdateAll, nullptr, ctx);
        }
        add(r.entry);
    }
    save();
}

// ---- events -------------------------------------------------------------------------------------

void PlacedBackpacks::onBlockChanged(BlockSource& region, BlockPos const& pos, Block const& previous, Block const& now) {
    bool wasOurs = isBackpackBlock(previous);
    bool isOurs  = isBackpackBlock(now);
    if (wasOurs == isOurs) return;
    auto loc = locationOf(region, pos);
    if (wasOurs) {
        // The block went away (player break, creative break, command, ...): drop the backpack.
        auto it = mEntries.find(loc);
        if (it == mEntries.end()) return;
        auto entry = it->second;
        // SB: Everlasting gives the block bedrock's blast resistance: explosions don't break it;
        // nor do the Wither and the Ender Dragon (canEntityDestroy, see EntityGriefing.cpp).
        if (tExploding > 0 || entityBreakingBlocks()) {
            if (auto bp = game::Storage::get().find(entry.uuid); bp && hasEverlasting(*bp)) {
                mRestore.push_back({entry, &previous});
                return;
            }
        }
        erase(loc);
        save();
        dropEntry(region, entry);
        closeViewers(&entry.uuid);
    } else if (!mEntries.contains(loc)) {
        // Our block appeared without a backpack (creative pick-block item, structure, ...): it
        // stands for nothing, remove it.
        mOrphans.push_back(loc);
    }
}

void PlacedBackpacks::tickPlaced(int interval) {
    auto level = ll::service::getLevel();
    if (!level || mEntries.empty()) return;
    auto&                      storage = game::Storage::get();
    std::vector<BlockLocation> keys;
    keys.reserve(mEntries.size());
    for (auto const& [loc, _] : mEntries) keys.push_back(loc);
    // Jukebox upgrades play at the block; their service wants a call at least once a second.
    mJukeboxTicks += interval;
    bool jukeboxDue = mJukeboxTicks >= 10;
    if (jukeboxDue) mJukeboxTicks = 0;
    std::unordered_set<std::string> ticked;
    for (auto const& loc : keys) {
        auto it = mEntries.find(loc); // upgrades / drops may change the map while we iterate
        if (it == mEntries.end()) continue;
        auto const e         = it->second;
        auto       dimension = level->getDimension(e.loc.dim).lock();
        if (!dimension) continue;
        auto& region = dimension->getBlockSourceFromMainChunkSource();
        auto  pos    = posOf(e.loc);
        if (!region.areChunksFullyLoaded(pos, 0)) continue;
        if (!isBackpackBlock(region.getBlock(pos))) {
            // The block vanished without a block-changed event (e.g. changed while the mod was
            // not loaded): once that is stable over a few checks, give the backpack back where it
            // stood.
            if (++mMismatches[e.loc] < kMismatchChecks) continue;
            mMismatches.erase(e.loc);
            log().warn("placed backpack {} at {} {} {} has no block; dropping it", e.uuid, pos.x, pos.y, pos.z);
            erase(e.loc);
            save();
            dropEntry(region, e);
            closeViewers(&e.uuid);
            continue;
        }
        mMismatches.erase(e.loc);
        // Linked-storage endpoints share one storage: its upgrades run once per pass.
        if (!ticked.insert(e.uuid).second) continue;
        auto bp = storage.find(e.uuid);
        if (!bp) continue;
        try {
            maintain(region, e, bp);
        } catch (std::exception const& ex) {
            log().error("placed backpack {} upkeep failed: {}", e.uuid, ex.what());
        }
        if (jukeboxDue && upgrade::hasActive(*bp, "jukebox")) {
            upgrade::jukebox::Service::get().tickPlaced(bp, e.loc.dim, pos.center());
        }
        upgrade::forEachInstalled(bp, true, [&](upgrade::IUpgrade& u, upgrade::UpgradeContext const& ctx) {
            try {
                u.onPlacedTick(upgrade::PlacedTickContext{ctx, region, e.loc.dim, e.loc.x, e.loc.y, e.loc.z, e.facing, interval});
            } catch (std::exception const& ex) {
                log().error("upgrade {} onPlacedTick failed: {}", u.info().id, ex.what());
            }
        });
    }
}

void PlacedBackpacks::runActions() {
    auto actions = std::move(mActions);
    mActions.clear();
    auto level = ll::service::getLevel();
    if (!level) return;
    for (auto const& a : actions) {
        ActorUniqueID id;
        id.rawID     = a.player;
        auto* player = level->getPlayer(id);
        if (!player || !player->isAlive()) continue;
        if (a.kind == PendingAction::Kind::Place) placeFromHand(*player, a.pos, a.face);
        else if (a.kind == PendingAction::Kind::PickUp) pickUp(*player, a.pos);
        else if (a.kind == PendingAction::Kind::Fluid) useFluidContainer(*player, a.pos);
        else if (!ui::MenuManager::get().current(*player)) open(*player, a.pos);
    }
}

void PlacedBackpacks::tick() {
    ++mNow;
    if (!mActions.empty()) runActions();
    if (!mRestore.empty()) restoreProtected();
    if (mNow - mLastPurge >= 100) {
        mLastPurge = mNow;
        if (auto level = ll::service::getLevel()) {
            level->forEachPlayer([](Player& p) {
                if (!p.isSimulatedPlayer()) display::purgeCopies(p);
                return true;
            });
        }
    }
    if (!mOrphans.empty()) {
        auto orphans = std::move(mOrphans);
        mOrphans.clear();
        if (auto level = ll::service::getLevel()) {
            for (auto const& loc : orphans) {
                if (mEntries.contains(loc)) continue;
                auto dimension = level->getDimension(loc.dim).lock();
                if (!dimension) continue;
                auto& region = dimension->getBlockSourceFromMainChunkSource();
                auto  pos    = posOf(loc);
                if (!region.hasChunksAt(pos, 0, false) || !isBackpackBlock(region.getBlock(pos))) continue;
                PlacedGuard        guard(*this);
                BlockChangeContext ctx{};
                region.removeBlock(pos, ctx);
            }
        }
    }
    int interval = std::max(1, config::get<PlacedConfig>().upgradeTickInterval);
    if (++mTicks < interval) return;
    mTicks = 0;
    tickPlaced(interval);
    if (mNow % 10 == 0 && !mEntries.empty()) closeViewers(nullptr);
    if (mNow % 20 == 0 && !mPendingDrops.empty()) retryPendingDrops();
    if (mNow % 200 == 0) std::erase_if(mCooldowns, [this](auto const& kv) { return mNow - kv.second > kActionCooldown; });
}

LL_TYPE_INSTANCE_HOOK(AbpPlacedExplosionHook, ll::memory::HookPriority::Normal, Explosion, &Explosion::explode, bool, ::IRandom& random) {
    ++tExploding;
    struct Done {
        ~Done() { --tExploding; }
    } done;
    return origin(random);
}

void PlacedBackpacks::install() {
    AbpPlacedExplosionHook::hook();
    auto& bus = ll::event::EventBus::getInstance();
    mListeners.push_back(bus.emplaceListener<ll::event::ServerStartedEvent>([this](auto&) { load(); }));
    mListeners.push_back(bus.emplaceListener<ll::event::ServerStoppingEvent>([this](auto&) {
        save();
        mEntries.clear();
        mSignals.clear();
        mRestore.clear();
        mByUuid.clear();
        mOrphans.clear();
        mPendingDrops.clear();
        mMismatches.clear();
        mActions.clear();
    }));
    mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([this](auto&) { tick(); }));
    mListeners.push_back(bus.emplaceListener<ll::event::BlockChangedEvent>([this](ll::event::BlockChangedEvent& ev) {
        if (mGuard > 0 || ev.layer() != 0) return;
        // Client process: the client level fires block changes too (network updates, prediction
        // reverts); only the server level is the truth.
        if (ev.blockSource().getLevel().isClientSide()) return;
        if (mEntries.empty() && !isBackpackBlock(ev.newBlock())) return; // fast path
        onBlockChanged(ev.blockSource(), ev.pos(), ev.previousBlock(), ev.newBlock());
    }));
    mListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>([this](ll::event::PlayerInteractBlockEvent& ev) {
        auto& player = ev.self();
        if (player.isClientSide() || ev.isCancelled()) return;
        auto&       region = player.getDimensionBlockSource();
        auto const& pos    = ev.blockPos();
        bool        ours   = isBackpackBlock(region.getBlock(pos)) && mEntries.contains(locationOf(region, pos));
        bool        sneak  = player.isSneaking();
        bool        holdingBackpack = game::isBackpack(player.getSelectedItem());
        bool        relevant        = ours || (sneak && holdingBackpack);
        if (!relevant) return;
        // Sneak + use of a backpack with a Deposit / Restock upgrade on a container is theirs.
        if (!ours && holdsInteractionBackpack(player) && containerAt(region, pos)) return;
        // Holding "use" repeats the event: one action per click.
        auto key = player.getOrCreateUniqueID().rawID;
        if (auto it = mCooldowns.find(key); it != mCooldowns.end() && mNow - it->second < kActionCooldown) {
            ev.cancel();
            return;
        }
        if (ours && !sneak && isFluidContainer(player.getSelectedItem())) {
            // SB useItemOn: a fluid container fills / drains the tanks (next tick, see below).
            // Buckets (Java's fluid-handler items) do nothing at all without tanks: no GUI, and
            // the bucket isn't emptied. Bottles only act on tanks; otherwise the GUI opens.
            auto bp = backpackAt(region, pos);
            if (isBucket(player.getSelectedItem()) || (bp && !upgrade::tank::tanks(bp).empty())) {
                ev.cancel();
                mActions.push_back({key, PendingAction::Kind::Fluid, pos, 0});
                mCooldowns[key] = mNow;
                return;
            }
        }
        if (ours && !sneak) {
            ev.cancel();
            if (!ui::MenuManager::get().current(player)) mActions.push_back({key, PendingAction::Kind::Open, pos, 0});
            mCooldowns[key] = mNow;
            return;
        }
        // Placing / picking up runs on the next tick, outside the item-use transaction: a
        // cancelled (= failed) use makes the game roll back what changed during it (the item in
        // the hand, the block), which would undo our work.
        if (ours && sneak && player.getSelectedItem().isNull()) {
            ev.cancel();
            mActions.push_back({key, PendingAction::Kind::PickUp, pos, 0});
            mCooldowns[key] = mNow;
            return;
        }
        if (sneak && holdingBackpack) {
            ev.cancel();
            mActions.push_back({key, PendingAction::Kind::Place, pos, static_cast<int>(ev.face())});
            mCooldowns[key] = mNow; // also after a refused placement (no GUI right after)
        }
        // Sneaking with another item on a placed backpack: vanilla placement against it.
    }, ll::event::EventPriority::High)); // before other sneak-use handlers (e.g. open the worn backpack)
    // A placement click must not also open the backpack that was just placed / is in the hand.
    mListeners.push_back(bus.emplaceListener<ll::event::PlayerUseItemEvent>(
        [this](ll::event::PlayerUseItemEvent& ev) {
            auto& player = ev.self();
            if (player.isClientSide()) return;
            auto key = player.getOrCreateUniqueID().rawID;
            if (auto it = mCooldowns.find(key); it != mCooldowns.end() && mNow - it->second < kActionCooldown) {
                if (game::isBackpack(ev.item()) || ev.item().isNull()) ev.cancel();
            }
        },
        ll::event::EventPriority::High
    ));
    if (game::Storage::get().available()) load(); // BDS: the world exists already
}

void PlacedBackpacks::uninstall() {
    AbpPlacedExplosionHook::unhook();
    auto& bus = ll::event::EventBus::getInstance();
    for (auto& l : mListeners) bus.removeListener(l);
    mListeners.clear();
    if (game::Storage::get().available()) save();
    mEntries.clear();
    mSignals.clear();
    mRestore.clear();
    mByUuid.clear();
    mOrphans.clear();
    mPendingDrops.clear();
    mMismatches.clear();
    mActions.clear();
    mCooldowns.clear();
}

} // namespace abp::placed

ABP_REGISTER_MODULE(abp::placed::PlacedBackpacks);

// `/backpack placed` (op / console): lists the placed backpacks of this world.
ABP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    cmd.overload().text("placed").execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors) {
            return output.error("You need operator permission.");
        }
        auto entries = abp::placed::PlacedBackpacks::get().entries();
        abp::log().info("placed backpacks: {}", entries.size());
        output.success("placed backpacks: " + std::to_string(entries.size()));
        for (auto const& e : entries) {
            auto line = "  dim " + std::to_string(e.loc.dim) + " at " + std::to_string(e.loc.x) + " "
                      + std::to_string(e.loc.y) + " " + std::to_string(e.loc.z) + ": " + e.uuid;
            abp::log().info("{}", line);
            output.success(line);
        }
    });
});
