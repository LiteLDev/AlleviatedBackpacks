#include "feature/placed/PlacedBackpacks.h"

#include <cmath>
#include <exception>

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
constexpr std::int64_t     kActionCooldown = 8;    ///< ticks: ignore repeated use events after an action
constexpr float            kMaxUseDistSq   = 8.0f * 8.0f;
constexpr int              kUpdateAll      = 3;    ///< neighbours + clients
constexpr int              kMismatchChecks = 20;   ///< consecutive checks before a missing block counts

LevelStorage* levelStorage() {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    return &level->getLevelStorage();
}

BlockType const* backpackBlockType() { return BlockType::tryGetFromRegistry(HashedString(std::string(kBlockId))).as_ptr(); }

/// The placed-backpack block for `tier` facing `facing`.
Block const* blockFor(core::Tier tier, int facing) {
    auto const* type = backpackBlockType();
    if (!type) return nullptr;
    Block const* block = type->mDefaultState;
    if (!block) return nullptr;
    if (auto const* state = type->getBlockState(HashedString(std::string(kFacingState)))) {
        if (auto b = block->setState(*state, facing & 3)) block = b.as_ptr();
    }
    if (auto const* state = type->getBlockState(HashedString(std::string(kTierState)))) {
        if (auto b = block->setState(*state, static_cast<int>(tier))) block = b.as_ptr();
    }
    return block;
}

BlockPos posOf(BlockLocation const& l) { return BlockPos{l.x, l.y, l.z}; }

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

bool PlacedBackpacks::isBackpackBlock(Block const& block) { return block.getTypeName() == kBlockId; }

std::optional<int> PlacedBackpacks::stateOf(Block const& block, std::string_view state) {
    if (!isBackpackBlock(block)) return std::nullopt;
    auto const* s = block.getBlockType().getBlockState(HashedString(std::string(state)));
    if (!s) return std::nullopt;
    return block.getState<int>(*s);
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

void PlacedBackpacks::add(PlacedEntry entry) {
    if (auto old = mByUuid.find(entry.uuid); old != mByUuid.end() && !(old->second == entry.loc)) {
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
    if (auto u = mByUuid.find(it->second.uuid); u != mByUuid.end() && u->second == loc) mByUuid.erase(u);
    mEntries.erase(it);
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
    auto* block = blockFor(*tier, facing);
    if (!block) return PlaceResult::Failed;
    bool changed = false;
    auto bp      = storage.forItem(item, &changed); // binds a uuid on first use
    if (!bp) return PlaceResult::NoStorage;
    if (auto other = findUuid(bp->uuid())) {
        // An item bound to a backpack that is already placed (a copy): refuse instead of
        // ending up with two blocks for one backpack.
        log().warn("backpack {} is already placed at {} {} {}", bp->uuid(), other->loc.x, other->loc.y, other->loc.z);
        return PlaceResult::Failed;
    }
    {
        PlacedGuard guard(*this);
        BlockChangeContext ctx{};
        if (!region.setBlock(pos, *block, kUpdateAll, nullptr, ctx) || !isBackpackBlock(region.getBlock(pos))) {
            return PlaceResult::Failed;
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
    auto uuidBefore = game::backpackUuid(item);
    auto result     = place(region, target, item, facingTowards(player.getRotation().y));
    if (result != PlaceResult::Ok) {
        // place() may have bound a fresh uuid to the item before failing: keep that binding.
        if (game::backpackUuid(item) != uuidBefore) {
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
    // Always consumed (also in creative): the block now is the backpack.
    auto& inv = player.getInventory();
    inv.setItem(slot, ItemStack::EMPTY_ITEM());
    player.sendInventory(false);
    mCooldowns[player.getOrCreateUniqueID().rawID] = mNow;
    return true;
}

bool PlacedBackpacks::pickUp(Player& player, BlockPos const& pos) {
    if (!player.canUseAbility(AbilitiesIndex::Build)) return false;
    auto& region = player.getDimensionBlockSource();
    auto  loc    = locationOf(region, pos);
    if (!mEntries.contains(loc) || distSq(player, loc) > kMaxUseDistSq) return false;
    int slot = player.getSelectedItemSlot();
    if (!player.getSelectedItem().isNull()) return false;
    auto uuid  = at(loc)->uuid;
    auto stack = take(region, pos);
    if (!stack) return false;
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
        auto bp = storage.find(e.uuid);
        if (!bp) continue;
        upgrade::forEachInstalled(bp, true, [&](upgrade::IUpgrade& u, upgrade::UpgradeContext const& ctx) {
            try {
                u.onPlacedTick(upgrade::PlacedTickContext{ctx, region, e.loc.dim, e.loc.x, e.loc.y, e.loc.z, e.facing, interval});
            } catch (std::exception const& ex) {
                log().error("upgrade {} onPlacedTick failed: {}", u.info().id, ex.what());
            }
        });
    }
}

void PlacedBackpacks::tick() {
    ++mNow;
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

void PlacedBackpacks::install() {
    auto& bus = ll::event::EventBus::getInstance();
    mListeners.push_back(bus.emplaceListener<ll::event::ServerStartedEvent>([this](auto&) { load(); }));
    mListeners.push_back(bus.emplaceListener<ll::event::ServerStoppingEvent>([this](auto&) {
        save();
        mEntries.clear();
        mByUuid.clear();
        mOrphans.clear();
        mPendingDrops.clear();
        mMismatches.clear();
    }));
    mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([this](auto&) { tick(); }));
    mListeners.push_back(bus.emplaceListener<ll::event::BlockChangedEvent>([this](ll::event::BlockChangedEvent& ev) {
        if (mGuard > 0 || ev.layer() != 0) return;
        if (mEntries.empty() && !isBackpackBlock(ev.newBlock())) return; // fast path
        onBlockChanged(ev.blockSource(), ev.pos(), ev.previousBlock(), ev.newBlock());
    }));
    mListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>([this](ll::event::PlayerInteractBlockEvent& ev) {
        auto& player = ev.self();
        if (player.isClientSide()) return;
        auto&       region = player.getDimensionBlockSource();
        auto const& pos    = ev.blockPos();
        bool        ours   = isBackpackBlock(region.getBlock(pos)) && mEntries.contains(locationOf(region, pos));
        bool        sneak  = player.isSneaking();
        bool        holdingBackpack = game::isBackpack(player.getSelectedItem());
        bool        relevant        = ours || (sneak && holdingBackpack);
        if (!relevant) return;
        // Holding "use" repeats the event: one action per click.
        auto key = player.getOrCreateUniqueID().rawID;
        if (auto it = mCooldowns.find(key); it != mCooldowns.end() && mNow - it->second < kActionCooldown) {
            ev.cancel();
            return;
        }
        if (ours && !sneak) {
            ev.cancel();
            if (!ui::MenuManager::get().current(player)) open(player, pos);
            return;
        }
        if (ours && sneak && player.getSelectedItem().isNull()) {
            ev.cancel();
            pickUp(player, pos);
            return;
        }
        if (sneak && holdingBackpack) {
            ev.cancel();
            placeFromHand(player, pos, static_cast<int>(ev.face()));
            mCooldowns[key] = mNow; // also after a refused placement (no GUI right after)
        }
        // Sneaking with another item on a placed backpack: vanilla placement against it.
    }));
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
    auto& bus = ll::event::EventBus::getInstance();
    for (auto& l : mListeners) bus.removeListener(l);
    mListeners.clear();
    if (game::Storage::get().available()) save();
    mEntries.clear();
    mByUuid.clear();
    mOrphans.clear();
    mPendingDrops.clear();
    mMismatches.clear();
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
