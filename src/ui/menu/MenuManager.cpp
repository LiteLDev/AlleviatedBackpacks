#include "ui/menu/MenuManager.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDieEvent.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/event/player/PlayerJoinEvent.h"
#include "ll/api/event/server/ServerStoppingEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/shared_types/legacy/ContainerType.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/NetEventCallback.h"
#include "mc/network/ServerNetworkHandler.h"
#include "mc/network/packet/BlockActorDataPacket.h"
#include "mc/network/packet/ContainerClosePacket.h"
#include "mc/network/packet/ContainerOpenPacket.h"
#include "mc/network/packet/InventoryContentPacket.h"
#include "mc/network/packet/InventoryContentPacketPayload.h"
#include "mc/network/packet/InventorySlotPacket.h"
#include "mc/network/packet/InventorySlotPacketPayload.h"
#include "mc/network/packet/ItemStackRequestPacket.h"
#include "mc/network/packet/ItemStackResponseContainerInfo.h"
#include "mc/network/packet/ItemStackResponseInfo.h"
#include "mc/network/packet/ItemStackResponsePacket.h"
#include "mc/network/packet/ItemStackResponseSlotInfo.h"
#include "mc/network/packet/UpdateBlockPacket.h"
#include "mc/network/packet/cerealize/types/item_stack_request_cereal/DestroyActionData.h"
#include "mc/network/packet/cerealize/types/item_stack_request_cereal/DropActionData.h"
#include "mc/network/packet/cerealize/types/item_stack_request_cereal/PlaceActionData.h"
#include "mc/network/packet/cerealize/types/item_stack_request_cereal/SlotInfoData.h"
#include "mc/network/packet/cerealize/types/item_stack_request_cereal/SwapActionData.h"
#include "mc/network/packet/cerealize/types/item_stack_request_cereal/TakeActionData.h"
#include "mc/network/packet/item_stack_request_packet_data/RequestData.h"
#include "mc/safety/RedactableString.h"
#include "mc/server/ServerPlayer.h"
#include "mc/world/ContainerID.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/PlayerUISlot.h"
#include "mc/world/containers/ContainerEnumName.h"
#include "mc/world/item/registry/ItemRegistryManager.h"
#include "mc/world/item/registry/ItemRegistryRef.h"
#include "mc/world/containers/FullContainerName.h"
#include "mc/world/inventory/network/ItemStackNetResult.h"
#include "mc/world/inventory/network/TypedClientNetId.h"
#include "mc/world/inventory/network/TypedServerNetId.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActor.h"

#include "mod/Log.h"
#include "mod/Module.h"
#include "ui/menu/MenuDebug.h"

ABP_REGISTER_CONFIG("menu", abp::ui::MenuConfig);

// `ItemStackRequestId` has two virtual `clientInit` members the game doesn't export. Copying a
// request id (into our responses) emits its vtable here, so give them (never called) bodies.
template <>
void TypedClientNetId<ItemStackRequestIdTag, int, 0>::clientInit(TypedClientNetId const&) {}
template <>
void TypedClientNetId<ItemStackRequestIdTag, int, 0>::clientInit() {}

namespace abp::ui {

namespace {
constexpr int kOpenDelay = 3; ///< ticks between the fake block packets and ContainerOpen
/// After a server-initiated close: max ticks to wait for the client's answering ContainerClose
/// before the next menu is placed anyway (a ContainerOpen that arrives while the client is still
/// closing the previous screen is dropped by the client).
constexpr int kAwaitCloseMax = 20;
/// A server-closed id is still acknowledged if the client's close comes this late (short: ids
/// are shared with vanilla containers).
constexpr int kForgetClose = 200;
/// Ticks a fake-chest position stays unused after its menu went away.
constexpr int kFakeReuseCooldown = 100;
constexpr int kAfterClose = 5; ///< ticks before `closeThen` callbacks run (no close to wait for)
constexpr int kAfterAck   = 2; ///< ticks after the client's close before `closeThen` callbacks run
/// A client that has another screen up (e.g. chat, right after a chat command) rejects a
/// ContainerOpen with a ContainerClose for id -1 at once; the open is then sent again this much
/// later, at most kMaxOpenRetries times.
constexpr int kRejectWindow   = 10;
constexpr int kRetryDelay     = 10;
constexpr int kMaxOpenRetries = 3;
constexpr int kDiffEvery   = 2; ///< ticks between display diffs
constexpr float kMaxDistance = 8.0f;

std::int64_t keyOf(Player const& p) { return p.getOrCreateUniqueID().rawID; }

/// Puts up to `count` of `stack` into the player's inventory slots (vanilla Inventory.add order:
/// matching stacks first, then empty slots) through the container only: no entity, no events.
/// Returns how many did not fit.
int addToInventorySlots(Player& player, ItemStack const& stack, int count) {
    if (stack.isNull() || count <= 0) return count;
    int   max = std::max<int>(stack.getMaxStackSize(), 1);
    auto& inv = player.getInventory();
    int   n   = std::min(inv.getContainerSize(), 36);
    for (int pass = 0; pass < 2 && count > 0; ++pass) {
        for (int i = 0; i < n && count > 0; ++i) {
            auto const& it    = inv.getItem(i);
            bool        empty = it.isNull() || it.mCount == 0;
            if (pass == 0 ? (empty || !it.isStackable(stack) || it.mCount >= max) : !empty) continue;
            int       have = pass == 0 ? static_cast<int>(it.mCount) : 0;
            int       add  = std::min(count, max - have);
            ItemStack copy = pass == 0 ? it : stack;
            copy.set(have + add);
            inv.setItem(i, copy);
            count -= add;
        }
    }
    return count;
}

/// Gives back whatever is on the cursor: inventory, else dropped at the player's feet.
void returnCursor(Player& player) {
    ItemStack cursor = player.mPlayerUIContainer->getItem(0);
    if (cursor.isNull()) return;
    player.setPlayerUIItem(PlayerUISlot::CursorSelected, ItemStack::EMPTY_ITEM(), false);
    if (!player.add(cursor) && !cursor.isNull() && cursor.mCount > 0) player.drop(cursor, false);
}

/// The world is stopping: the cursor item goes into the inventory, else into the menu (the
/// backpack's storage), and whatever still does not fit stays on the cursor, which is saved with the
/// player (PlayerUIItems) and given back by `returnCursor` on the next join. Never dropped: there is
/// no world left to drop it into. Needs the level's item registry on this thread (see closeAll).
void returnCursorOnShutdown(Player& player, Menu& menu) {
    ItemStack cursor = player.mPlayerUIContainer->getItem(0);
    if (cursor.isNull() || cursor.mCount == 0) return;
    int total = cursor.mCount;
    int left  = addToInventorySlots(player, cursor, total);
    int toInv = total - left;
    int toBp  = 0;
    if (left > 0) {
        ItemStack rest = cursor;
        rest.set(left);
        try {
            toBp = std::clamp(menu.quickMoveIn(player, rest), 0, left);
        } catch (std::exception const& e) {
            log().error("menu shutdown: moving the cursor item into the menu failed: {}", e.what());
        }
        left -= toBp;
    }
    ItemStack keep = cursor;
    keep.set(left);
    player.setPlayerUIItem(PlayerUISlot::CursorSelected, left > 0 ? keep : ItemStack::EMPTY_ITEM(), false);
    log().info(
        "menu shutdown: cursor {} x{} of {}: inventory={} menu={} cursor={}",
        cursor.getTypeName(),
        total,
        player.getRealName(),
        toInv,
        toBp,
        left
    );
}

Player* findPlayer(std::int64_t key) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    ActorUniqueID id;
    id.rawID = key;
    return level->getPlayer(id);
}

uint runtimeIdOf(Level& level, Block const& b) {
    return level.blockNetworkIdsAreHashes() ? b.mSerializationIdHashForNetwork : b.mNetworkId;
}

void sendBlock(Player& p, BlockPos const& pos, Block const& block) {
    auto  pkt          = MinecraftPackets::createPacket(MinecraftPacketIds::UpdateBlock);
    auto& u            = static_cast<UpdateBlockPacket&>(*pkt);
    u.mPos             = pos;
    u.mLayer           = 0u;
    u.mUpdateFlags     = static_cast<uchar>(3);
    u.mRuntimeId       = runtimeIdOf(p.getLevel(), block);
    u.sendTo(p);
    menuDebugLog("-> UpdateBlock ({},{},{}) {}", pos.x, pos.y, pos.z, block.getTypeName());
}

void sendBlockActor(Player& p, BlockPos const& pos, CompoundTag tag) {
    BlockActorDataPacket pkt;
    pkt.mPos  = pos;
    pkt.mData = std::move(tag);
    pkt.sendTo(p);
    menuDebugLog("-> BlockActorData ({},{},{})", pos.x, pos.y, pos.z);
}

FullContainerName containerName(ContainerEnumName name) { return FullContainerName{name, std::optional<uint>{}}; }

int netIdOf(ItemStack const& s) {
    if (s.isNull()) return 0;
    auto const& v = s.mNetIdVariant.get().mVariant.get();
    if (auto const* id = std::get_if<ItemStackNetId>(&v)) return id->mRawId;
    return 0;
}

bool sameDisplay(ItemStack const& a, ItemStack const& b) {
    if (a.isNull() || b.isNull()) return a.isNull() == b.isNull();
    return a.mCount == b.mCount && a.matchesItem(b) && a.hasSameUserData(b);
}

ItemStack clampForDisplay(ItemStack s) {
    if (s.isNull()) return ItemStack{};
    if (s.mCount == 0) s.set(1);
    return s;
}

Block const* chestBlock() {
    static Block const* cached = nullptr;
    if (!cached) {
        Block::BlockStatesType states{
            {"minecraft:cardinal_direction", std::string("north")}
        };
        if (auto b = Block::tryGetFromRegistry(HashedString("minecraft:chest"), states)) cached = b.as_ptr();
        else if (auto b2 = Block::tryGetFromRegistry(HashedString("minecraft:chest"))) cached = b2.as_ptr();
    }
    return cached;
}
} // namespace

struct MenuManager::Session {
    /// AwaitClose: the client is still closing an earlier screen (no fake block sent yet).
    /// Placed: the fake chest was sent; ContainerOpen follows after `wait` ticks. Shown: open.
    enum class Phase { AwaitClose, Placed, Shown };

    std::int64_t                               key;
    std::shared_ptr<Menu>                      menu;
    int                                        id = 1;
    std::vector<BlockPos>                      fake;
    int                                        dim = 0;
    Phase                                      phase = Phase::AwaitClose;
    int                                        wait  = 0;
    bool                                       clientOpened = false; ///< ContainerOpen sent (not just forceShown)
    bool                                       virtualScreen = false; ///< Presentation::Virtual: no chest, no packets
    std::int64_t                               openedAt = 0; ///< tick of the last ContainerOpen
    int                                        retries  = 0; ///< ContainerOpens re-sent after a rejection
    bool                                       needFull = true;
    std::optional<std::uint64_t>               lastVersion;
    int                                        diffIn = 0;
    std::vector<ItemStack>                     sent;
    std::vector<std::function<void(Player&)>> afterClose;

    [[nodiscard]] bool shown() const { return phase == Phase::Shown; }
};

MenuManager& MenuManager::get() {
    static MenuManager instance;
    return instance;
}

// ---------------------------------------------------------------------------------------------
// Opening / closing
// ---------------------------------------------------------------------------------------------

bool MenuManager::open(Player& player, std::shared_ptr<Menu> menu) {
    if (!menu) return false;
    if (mBusy) {
        auto key = keyOf(player);
        mDeferred.push_back([this, key, menu] {
            if (auto* p = findPlayer(key)) open(*p, menu);
        });
        return true;
    }
    // Game classes have no usable RTTI: a non-client-side Player is a ServerPlayer.
    if (player.isClientSide() || !player.isAlive()) return false;
    auto* sp = static_cast<ServerPlayer*>(&player);
    auto* chest = chestBlock();
    if (!chest) {
        log().error("menu: minecraft:chest block not found");
        return false;
    }

    auto presentation = mGate ? mGate(player, *menu) : Presentation::Chest;
    if (presentation == Presentation::Refuse) {
        mRefused[keyOf(player)] = true;
        return false;
    }
    mRefused.erase(keyOf(player));

    auto key = keyOf(player);
    // Replacing a menu: close it first (a shown one gets a server-initiated ContainerClose that
    // the client answers; see `handleClosePacket`).
    if (mSessions.contains(key)) finish(&player, key, true);

    auto s  = std::make_unique<Session>();
    s->key  = key;
    s->menu = std::move(menu);
    s->dim  = static_cast<int>(player.getDimensionId());
    if (presentation == Presentation::Virtual) {
        // No fake chest, no container screen: shown on the next tick through the observers.
        s->virtualScreen = true;
        s->id            = mNextVirtualId++;
        if (mNextVirtualId > 1'000'000'000) mNextVirtualId = 1000;
        s->phase = Session::Phase::Placed;
        s->wait  = 1;
        auto& session  = *s;
        mSessions[key] = std::move(s);
        session.menu->invalidate();
        try {
            session.menu->onOpen(player);
        } catch (std::exception const& e) {
            log().error("menu onOpen failed: {}", e.what());
        }
        return true;
    }
    // Next container id, the way vanilla counts (1..99), written back so vanilla won't reuse it.
    int next = static_cast<int>(static_cast<schar>(sp->mContainerCounter)) + 1;
    if (next < static_cast<int>(ContainerID::First) || next >= static_cast<int>(ContainerID::Last)) {
        next = static_cast<int>(ContainerID::First);
    }
    sp->mContainerCounter = static_cast<ContainerID>(next);
    s->id                 = next;

    auto& session = *s;
    mSessions[key] = std::move(s);
    // While the client is still closing an earlier screen it drops a ContainerOpen, so the fake
    // chest is only placed once that close is acknowledged (or timed out), see tick().
    if (awaitingClose(key)) {
        session.phase = Session::Phase::AwaitClose;
        menuDebugLog("open id={} waits for the client to close the previous screen", session.id);
    } else {
        place(player, session);
    }
    session.menu->invalidate();
    try {
        session.menu->onOpen(player);
    } catch (std::exception const& e) {
        log().error("menu onOpen failed: {}", e.what());
    }
    return true;
}

bool MenuManager::awaitingClose(std::int64_t key) const {
    auto it = mPendingCloses.find(key);
    if (it == mPendingCloses.end()) return false;
    return std::any_of(it->second.begin(), it->second.end(), [](PendingClose const& c) { return c.age < kAwaitCloseMax; });
}

std::vector<BlockPos> MenuManager::pickFakePositions(Player& player, bool twoBlocks) {
    auto& region = player.getDimensionBlockSource();
    auto  feet   = player.getFeetBlockPos();
    auto& recent = mRecentFake[keyOf(player)];
    std::erase_if(recent, [this](RecentFake const& r) { return r.until <= mTick; });
    auto inWorld = [&](BlockPos const& p) { return p.y >= region.getMinHeight() && p.y < region.getMaxHeight(); };
    // Tick until which a position stays blocked (0 = free).
    auto blockedUntil = [&](BlockPos const& p) {
        std::int64_t until = 0;
        for (auto const& r : recent) {
            if (r.pos == p) until = std::max(until, r.until);
        }
        return until;
    };
    auto isRecent = [&](BlockPos const& p) { return blockedUntil(p) != 0; };
    // Candidates: 3-4 blocks above the feet (clear of the player's head even when jumping, well
    // inside the client's container reach), then the same below the feet for the world top.
    static constexpr int kOffsets[][3] = {
        {0,  3, 0 },
        {0,  4, 0 },
        {0,  3, 1 },
        {0,  4, 1 },
        {-1, 3, 0 },
        {-1, 4, 0 },
        {0,  3, -1},
        {0,  4, -1},
        {0,  -2, 0},
        {0,  -3, 0},
        {0,  -2, 1},
        {0,  -3, 1},
    };
    // If every candidate was used a moment ago: the one that was freed first.
    std::vector<BlockPos> fallback;
    std::int64_t          fallbackUntil = 0;
    for (int pass = 0; pass < 2; ++pass) {
        for (auto const& o : kOffsets) {
            std::vector<BlockPos> ps{BlockPos{feet.x + o[0], feet.y + o[1], feet.z + o[2]}};
            if (twoBlocks) ps.push_back(BlockPos{ps[0].x + 1, ps[0].y, ps[0].z});
            if (!std::all_of(ps.begin(), ps.end(), inWorld)) continue;
            std::int64_t until = 0;
            for (auto const& p : ps) until = std::max(until, blockedUntil(p));
            if (fallback.empty() || until < fallbackUntil) {
                fallback      = ps;
                fallbackUntil = until;
            }
            // Never reuse a position a fake chest had a moment ago: the client may still hold
            // that chest's state (a double chest's pairing) and then fails to show the new one.
            if (std::any_of(ps.begin(), ps.end(), isRecent)) continue;
            // First pass: only air, so nothing real (a chest, a block entity) is masked.
            if (pass == 0 && !std::all_of(ps.begin(), ps.end(), [&](BlockPos const& p) { return region.getBlock(p).isAir(); })) {
                continue;
            }
            return ps;
        }
    }
    if (fallback.empty()) fallback.push_back(feet); // no room in the world at all (can't happen)
    return fallback;
}

void MenuManager::place(Player& player, Session& s) {
    auto* chest = chestBlock();
    s.fake      = pickFakePositions(player, s.menu->rows() == Menu::Rows::Six);
    // Both blocks first, then their block entities: a double chest's pairing refers to the other
    // half, which must already be a chest on the client.
    for (auto const& pos : s.fake) sendBlock(player, pos, *chest);
    for (std::size_t i = 0; i < s.fake.size(); ++i) {
        auto const& pos = s.fake[i];
        CompoundTag tag;
        tag["id"]         = std::string("Chest");
        tag["x"]          = pos.x;
        tag["y"]          = pos.y;
        tag["z"]          = pos.z;
        tag["isMovable"]  = static_cast<uchar>(0);
        tag["Findable"]   = static_cast<uchar>(0);
        tag["CustomName"] = s.menu->title();
        if (s.fake.size() == 2) {
            auto const& other = s.fake[1 - i];
            tag["pairx"]      = other.x;
            tag["pairz"]      = other.z;
            tag["pairlead"]   = static_cast<uchar>(i == 0 ? 1 : 0);
        }
        sendBlockActor(player, pos, std::move(tag));
    }
    s.phase = Session::Phase::Placed;
    s.wait  = kOpenDelay;
}

MenuManager::NavigationGuard::NavigationGuard(Player const& player) : mKey(keyOf(player)) { ++MenuManager::get().mGuarded[mKey]; }

MenuManager::NavigationGuard::~NavigationGuard() {
    auto& g = MenuManager::get().mGuarded;
    if (auto it = g.find(mKey); it != g.end() && --it->second <= 0) g.erase(it);
}

void MenuManager::close(Player& player) {
    auto key = keyOf(player);
    if (!mSessions.contains(key)) return;
    if (mGuarded.contains(key)) return; // a tab-hosted menu wanted to close itself
    if (mBusy) {
        mDeferred.push_back([this, key] {
            if (auto* p = findPlayer(key)) close(*p);
        });
        return;
    }
    finish(&player, key, true);
}

void MenuManager::reopen(Player& player) {
    if (auto m = current(player)) open(player, m);
}

void MenuManager::closeThen(Player& player, std::function<void(Player&)> fn) {
    auto key = keyOf(player);
    if (mGuarded.contains(key)) {
        if (fn) {
            after(kAfterClose, [key, fn = std::move(fn)] {
                if (auto* p = findPlayer(key)) fn(*p);
            });
        }
        return;
    }
    auto it  = mSessions.find(key);
    if (it == mSessions.end()) {
        if (fn) fn(player);
        return;
    }
    if (fn) it->second->afterClose.push_back(std::move(fn));
    close(player);
}

std::shared_ptr<Menu> MenuManager::current(Player const& player) const {
    auto it = mSessions.find(keyOf(player));
    return it == mSessions.end() ? nullptr : it->second->menu;
}

bool MenuManager::isShown(Player const& player) const {
    auto it = mSessions.find(keyOf(player));
    return it != mSessions.end() && it->second->shown();
}

void MenuManager::after(int ticks, std::function<void()> fn) { mDelayed.push_back({ticks, std::move(fn)}); }

void MenuManager::finish(Player* player, std::int64_t key, bool sendClose, bool shutdown) {
    auto it = mSessions.find(key);
    if (it == mSessions.end()) return;
    auto s = std::move(it->second);
    mSessions.erase(it);
    if (player) {
        bool awaitAnswer = false;
        if (shutdown) {
            // The world is stopping (or the mod going away): no packets, no block reads, no
            // entities; the players are about to be saved and disconnected.
            returnCursorOnShutdown(*player, *s->menu);
        } else if (sendClose && s->shown() && !s->virtualScreen) {
            auto  pkt                = MinecraftPackets::createPacket(MinecraftPacketIds::ContainerClose);
            auto& c                  = static_cast<ContainerClosePacket&>(*pkt);
            c.mContainerId           = static_cast<ContainerID>(s->id);
            c.mContainerType         = SharedTypes::Legacy::ContainerType::Container;
            c.mServerInitiatedClose  = true;
            c.sendTo(*player);
            menuDebugLog("-> ContainerClose id={} serverInitiated", s->id);
            // The client answers with its own ContainerClose and expects ours back; until then
            // it ignores ContainerOpen (and can't open chat). Track it, see handleClosePacket.
            // (Only for a screen the client really opened: not forceShown / simulated players.)
            if (s->clientOpened && !player->isSimulatedPlayer()) {
                mPendingCloses[key].push_back(PendingClose{s->id, 0, {}});
                awaitAnswer = true;
            }
        }
        if (!shutdown) {
            // Restore what the client should see at the fake positions.
            auto& region = player->getDimensionBlockSource();
            for (auto const& pos : s->fake) {
                sendBlock(*player, pos, region.getBlock(pos));
                if (auto* be = region.getBlockEntity(pos)) {
                    if (auto pkt = be->_getUpdatePacket(region)) pkt->sendTo(*player);
                }
                mRecentFake[key].push_back(RecentFake{pos, mTick + kFakeReuseCooldown});
            }
            returnCursor(*player);
            player->sendInventory(false);
        }
        try {
            s->menu->onClose(*player);
        } catch (std::exception const& e) {
            log().error("menu onClose failed: {}", e.what());
        }
        // `closeThen` callbacks (usually a form) run once the client has finished closing.
        if (!s->afterClose.empty()) {
            if (awaitAnswer) {
                auto& then = mPendingCloses[key].back().then;
                for (auto& fn : s->afterClose) then.push_back(std::move(fn));
            } else {
                for (auto& fn : s->afterClose) {
                    after(kAfterClose, [key, fn = std::move(fn)] {
                        if (auto* p = findPlayer(key)) fn(*p);
                    });
                }
            }
        }
    }
    for (auto const& [id, o] : std::vector(mObservers)) {
        if (!o.closed) continue;
        try {
            o.closed(player, *s->menu);
        } catch (std::exception const& e) {
            log().error("menu observer (closed) failed: {}", e.what());
        }
    }
    try {
        s->menu->onDestroyed();
    } catch (std::exception const& e) {
        log().error("menu onDestroyed failed: {}", e.what());
    }
}

void MenuManager::notifyBeforeShow(Player& player, Session& s) {
    auto menu = s.menu; // observers must not open/close menus, but keep it alive anyway
    int  id   = s.id;
    for (auto const& [oid, o] : std::vector(mObservers)) {
        if (!o.beforeShow) continue;
        try {
            o.beforeShow(player, *menu, id);
        } catch (std::exception const& e) {
            log().error("menu observer (beforeShow) failed: {}", e.what());
        }
    }
}

int MenuManager::addObserver(Observer observer) {
    int id = mNextObserver++;
    mObservers.emplace_back(id, std::move(observer));
    return id;
}

void MenuManager::removeObserver(int id) {
    std::erase_if(mObservers, [id](auto const& e) { return e.first == id; });
}

bool MenuManager::isVirtual(Player const& player) const {
    auto it = mSessions.find(keyOf(player));
    return it != mSessions.end() && it->second->virtualScreen;
}

bool MenuManager::lastOpenRefused(Player const& player) const {
    auto it = mRefused.find(keyOf(player));
    return it != mRefused.end() && it->second;
}

int MenuManager::containerIdOf(Player const& player) const {
    auto it = mSessions.find(keyOf(player));
    return it == mSessions.end() ? -1 : it->second->id;
}

void MenuManager::runDeferred() {
    while (!mDeferred.empty()) {
        auto list = std::move(mDeferred);
        mDeferred.clear();
        for (auto& fn : list) fn();
    }
}

// ---------------------------------------------------------------------------------------------
// Sync
// ---------------------------------------------------------------------------------------------

void MenuManager::sync(Player& player, Session& s, bool full) {
    auto& menu = *s.menu;
    bool  rebuilt = menu.needsBuild();
    menu.ensureBuilt(player);
    auto version = menu.contentVersion();
    if (!full && !rebuilt && !s.needFull && version && s.lastVersion == version) return;
    s.lastVersion = version;

    int  n = menu.size();
    bool sendAll = full || s.needFull || static_cast<int>(s.sent.size()) != n;
    s.sent.resize(static_cast<std::size_t>(n));
    std::vector<int> changed;
    for (int i = 0; i < n; ++i) {
        auto* slot = menu.slot(i);
        auto  now  = clampForDisplay(slot ? slot->display(player) : ItemStack{});
        auto& old  = s.sent[static_cast<std::size_t>(i)];
        if (!sendAll && sameDisplay(now, old)) continue;
        if (!now.isNull()) now.serverInitNetId();
        old = std::move(now);
        changed.push_back(i);
    }
    s.needFull = false;
    if (menuDebug() && !changed.empty()) {
        std::string list;
        for (int i : changed) {
            auto const& it = s.sent[static_cast<std::size_t>(i)];
            list += std::format(" {}:{}", i, it.isNull() ? std::string("-") : it.getRawNameId());
        }
        menuDebugLog("sync id={}{}", s.id, list);
    }
    if (s.virtualScreen) return; // shown by an observer: no container on the client
    if (sendAll || changed.size() > 8) {
        InventoryContentPacket pkt{InventoryContentPacketPayload{
            static_cast<ContainerID>(s.id),
            s.sent,
            containerName(ContainerEnumName::LevelEntityContainer),
            ItemStack::EMPTY_ITEM()
        }};
        pkt.sendTo(player);
    } else {
        for (int i : changed) {
            InventorySlotPacket pkt{
                InventorySlotPacketPayload{static_cast<ContainerID>(s.id), i, s.sent[static_cast<std::size_t>(i)]}
            };
            pkt.sendTo(player);
        }
    }
}

ItemStack MenuManager::shownItem(Player const& player, int slot) const {
    auto it = mSessions.find(keyOf(player));
    if (it == mSessions.end() || slot < 0 || slot >= static_cast<int>(it->second->sent.size())) return {};
    return it->second->sent[static_cast<std::size_t>(slot)];
}

void MenuManager::forceShown(Player& player) {
    auto it = mSessions.find(keyOf(player));
    if (it == mSessions.end() || it->second->shown()) return;
    if (it->second->phase == Session::Phase::AwaitClose && !it->second->virtualScreen) place(player, *it->second);
    it->second->phase = Session::Phase::Shown;
    if (it->second->virtualScreen) it->second->menu->ensureBuilt(player);
    notifyBeforeShow(player, *it->second);
    sync(player, *it->second, true);
}

void MenuManager::tickPendingCloses() {
    for (auto it = mPendingCloses.begin(); it != mPendingCloses.end();) {
        auto key = it->first;
        std::vector<std::function<void(Player&)>> due;
        for (auto& c : it->second) {
            // The client never answered in time: go on with what waited for it.
            if (++c.age == kAwaitCloseMax) {
                menuDebugLog("close id={} not answered by the client in time", c.id);
                for (auto& fn : c.then) due.push_back(std::move(fn));
                c.then.clear();
            }
        }
        std::erase_if(it->second, [](PendingClose const& c) { return c.age >= kForgetClose; });
        it = it->second.empty() ? mPendingCloses.erase(it) : std::next(it);
        if (!due.empty()) {
            after(1, [key, due = std::move(due)] {
                if (auto* p = findPlayer(key)) {
                    for (auto const& fn : due) fn(*p);
                }
            });
        }
    }
}

void MenuManager::tick() {
    ++mTick;
    tickPendingCloses();
    // Collect due tasks first: running them may schedule new ones (push_back into mDelayed).
    std::vector<std::function<void()>> due;
    for (auto it = mDelayed.begin(); it != mDelayed.end();) {
        if (--it->ticks <= 0) {
            due.push_back(std::move(it->fn));
            it = mDelayed.erase(it);
        } else {
            ++it;
        }
    }
    for (auto& fn : due) fn();
    std::vector<std::int64_t> keys;
    keys.reserve(mSessions.size());
    for (auto& [k, _] : mSessions) keys.push_back(k);
    for (auto key : keys) {
        auto it = mSessions.find(key);
        if (it == mSessions.end()) continue;
        auto& s      = *it->second;
        auto* player = findPlayer(key);
        if (!player) {
            finish(nullptr, key, false);
            continue;
        }
        // Validity: alive, same dimension, still near the fake chest.
        // (Only for real clients: the reach limit is the client's. Selftest players have no screen
        // and may still be falling from an unresolved spawn height.)
        bool tooFar = false;
        if (!s.fake.empty() && !player->isSimulatedPlayer()) {
            auto const& pos  = player->getPosition();
            auto const& fake = s.fake.front();
            float dx = pos.x - (fake.x + 0.5f), dy = pos.y - (fake.y + 0.5f), dz = pos.z - (fake.z + 0.5f);
            tooFar   = std::sqrt(dx * dx + dy * dy + dz * dz) > kMaxDistance;
        }
        if (!player->isAlive() || static_cast<int>(player->getDimensionId()) != s.dim || tooFar) {
            menuDebugLog("menu id={} closed: alive={} tooFar={}", s.id, player->isAlive(), tooFar);
            finish(player, key, true);
            continue;
        }
        if (s.phase == Session::Phase::AwaitClose) {
            if (!awaitingClose(key)) place(*player, s);
            continue;
        }
        if (s.phase == Session::Phase::Placed) {
            if (--s.wait > 0) continue;
            s.phase = Session::Phase::Shown;
            if (s.virtualScreen) {
                s.menu->ensureBuilt(*player);
                notifyBeforeShow(*player, s);
                // notifyBeforeShow must not close/replace menus; look the session up anyway.
                if (auto again = mSessions.find(key); again != mSessions.end() && again->second.get() == &s) sync(*player, s, true);
                continue;
            }
            notifyBeforeShow(*player, s);
            auto  pkt         = MinecraftPackets::createPacket(MinecraftPacketIds::ContainerOpen);
            auto& o           = static_cast<ContainerOpenPacket&>(*pkt);
            o.mContainerId    = static_cast<ContainerID>(s.id);
            o.mType           = SharedTypes::Legacy::ContainerType::Container;
            o.mPos            = s.fake.front();
            o.mEntityUniqueID = ActorUniqueID::INVALID_ID();
            o.sendTo(*player);
            menuDebugLog("-> ContainerOpen id={} pos=({},{},{})", s.id, s.fake.front().x, s.fake.front().y, s.fake.front().z);
            s.clientOpened = true;
            s.openedAt     = mTick;
            log().info("menu shown: {} ({} slots)", s.menu->title(), s.menu->size());
            sync(*player, s, true);
            continue;
        }
        auto menuRef = s.menu; // onTick may close the menu
        try {
            menuRef->onTick(*player);
        } catch (std::exception const& e) {
            log().error("menu onTick failed: {}", e.what());
        }
        // onTick may have closed the menu or replaced it (then `s` is gone): look it up again.
        auto again = mSessions.find(key);
        if (again == mSessions.end() || again->second->menu != menuRef) continue;
        auto& cur = *again->second;
        if (--cur.diffIn <= 0 || cur.menu->needsBuild()) {
            cur.diffIn = kDiffEvery;
            sync(*player, cur, false);
        }
        if (!mObservers.empty()) {
            auto shownMenu = cur.menu;
            for (auto const& [oid, o] : std::vector(mObservers)) {
                if (!o.afterTick) continue;
                try {
                    o.afterTick(*player, *shownMenu);
                } catch (std::exception const& e) {
                    log().error("menu observer (afterTick) failed: {}", e.what());
                }
            }
        }
    }
    runDeferred();
}

// ---------------------------------------------------------------------------------------------
// Item stack requests
// ---------------------------------------------------------------------------------------------

namespace {

using Where = MenuManager::SlotAddress::Where;

struct Ref {
    Where             where = Where::Menu;
    int               index = 0;
    bool              valid = false;
    ContainerEnumName name  = ContainerEnumName::LevelEntityContainer;
    std::optional<uint> dynamicId;
    uchar             requested = 0;

    [[nodiscard]] bool isMenu() const { return where == Where::Menu; }
};

Ref resolve(ItemStackRequestCereal::SlotInfoData const& info, Menu const& menu) {
    Ref r;
    auto const& name = info.mFullContainerName.get();
    r.name           = name.mName;
    r.dynamicId      = name.mDynamicId.get();
    r.requested      = info.mSlot;
    r.index          = info.mSlot;
    switch (r.name) {
    case ContainerEnumName::HotbarContainer:
    case ContainerEnumName::InventoryContainer:
    case ContainerEnumName::CombinedHotbarAndInventoryContainer:
        r.where = Where::Inventory;
        r.valid = r.index >= 0 && r.index < 36;
        break;
    case ContainerEnumName::CursorContainer:
        r.where = Where::Cursor;
        r.index = 0;
        r.valid = true;
        break;
    case ContainerEnumName::OffhandContainer:
        r.where = Where::Offhand;
        r.index = 0;
        r.valid = true;
        break;
    case ContainerEnumName::ArmorContainer:
        r.where = Where::Armor;
        r.valid = r.index >= 0 && r.index < 4;
        break;
    case ContainerEnumName::LevelEntityContainer:
    case ContainerEnumName::BarrelContainer:
    case ContainerEnumName::DynamicContainer:
        r.where = Where::Menu;
        r.valid = r.index >= 0 && r.index < menu.size();
        break;
    default:
        r.valid = false;
        break;
    }
    return r;
}

Ref fromAddress(MenuManager::SlotAddress a) {
    Ref r;
    r.where = a.where;
    r.index = a.index;
    r.valid = true;
    switch (a.where) {
    case Where::Menu:
        r.name = ContainerEnumName::LevelEntityContainer;
        break;
    case Where::Inventory:
        r.name  = a.index < 9 ? ContainerEnumName::HotbarContainer : ContainerEnumName::InventoryContainer;
        r.valid = a.index >= 0 && a.index < 36;
        break;
    case Where::Cursor:
        r.name = ContainerEnumName::CursorContainer;
        break;
    case Where::Offhand:
        r.name = ContainerEnumName::OffhandContainer;
        break;
    case Where::Armor:
        r.name  = ContainerEnumName::ArmorContainer;
        r.valid = a.index >= 0 && a.index < 4;
        break;
    }
    r.requested = static_cast<uchar>(r.index);
    return r;
}

ItemStack getItem(Player& p, Ref const& r) {
    switch (r.where) {
    case Where::Inventory:
        return p.getInventory().getItem(r.index);
    case Where::Cursor:
        return p.mPlayerUIContainer->getItem(0);
    case Where::Offhand:
        return p.getOffhandSlot();
    case Where::Armor:
        return p.getArmor(static_cast<SharedTypes::Legacy::ArmorSlot>(r.index));
    case Where::Menu:
        break;
    }
    return {};
}

void setItem(Player& p, Ref const& r, ItemStack item) {
    if (item.isNull() || item.mCount == 0) item = ItemStack{};
    else item.serverInitNetId();
    switch (r.where) {
    case Where::Inventory:
        p.getInventory().setItem(r.index, item);
        break;
    case Where::Cursor:
        p.setPlayerUIItem(PlayerUISlot::CursorSelected, item, false);
        break;
    case Where::Offhand:
        p.setOffhandSlot(item);
        break;
    case Where::Armor:
        p.setArmor(static_cast<SharedTypes::Legacy::ArmorSlot>(r.index), item);
        break;
    case Where::Menu:
        break;
    }
}

ItemStack withCount(ItemStack s, int n) {
    if (n <= 0) return ItemStack{};
    s.set(n);
    return s;
}

/// Executes actions against a menu + the player's own containers.
class ActionRunner {
public:
    ActionRunner(Player& player, Menu& menu, std::shared_ptr<Slot> overrideSlot = nullptr)
    : mPlayer(player),
      mMenu(menu),
      mOverride(std::move(overrideSlot)) {}

    std::vector<Ref> touched;
    bool             menuChanged = false;

    bool locked(Ref const& r) const {
        return r.where == Where::Inventory && mMenu.lockedInventorySlot && *mMenu.lockedInventorySlot == r.index;
    }

    void touch(Ref const& r) {
        auto same = [&](Ref const& t) {
            return t.where == r.where && t.index == r.index && t.name == r.name;
        };
        if (std::none_of(touched.begin(), touched.end(), same)) touched.push_back(r);
        if (r.isMenu()) menuChanged = true;
    }

    bool click(Ref const& r, ClickType type, int amount, ItemStack const& offered) {
        auto* slot = slotAt(r.index);
        if (!slot) return true; // nothing there: treat as handled (refuse)
        ClickContext ctx{mPlayer, mMenu, r.index, type, amount, offered};
        bool         handled = slot->click(ctx);
        if (handled) menuChanged = true;
        return handled;
    }

    bool transfer(Ref const& src, Ref const& dst, int n) {
        if (!src.valid || !dst.valid || n <= 0 || locked(src) || locked(dst)) return false;
        touch(src);
        touch(dst);
        if (!src.isMenu() && !dst.isMenu()) return playerToPlayer(src, dst, n);
        if (src.isMenu() && !dst.isMenu()) return menuToPlayer(src, dst, n);
        if (!src.isMenu() && dst.isMenu()) return playerToMenu(src, dst, n);
        return menuToMenu(src, dst, n);
    }

    bool swap(Ref const& a, Ref const& b) {
        if (!a.valid || !b.valid || locked(a) || locked(b)) return false;
        touch(a);
        touch(b);
        if (!a.isMenu() && !b.isMenu()) {
            auto ia = getItem(mPlayer, a), ib = getItem(mPlayer, b);
            setItem(mPlayer, a, ib);
            setItem(mPlayer, b, ia);
            return true;
        }
        if (a.isMenu() && b.isMenu()) return false;
        auto const& m = a.isMenu() ? a : b;
        auto const& p = a.isMenu() ? b : a;
        auto        pItem = getItem(mPlayer, p);
        if (click(m, ClickType::Swap, pItem.mCount, pItem)) return false;
        auto* slot = slotAt(m.index);
        auto  taken = slot->take(mPlayer, 255);
        if (!pItem.isNull()) {
            int acc = slot->accept(mPlayer, pItem);
            if (acc < pItem.mCount) {
                // Roll back: take back what went in, return what came out.
                if (acc > 0) (void)slot->take(mPlayer, acc);
                if (!taken.isNull()) slot->accept(mPlayer, taken);
                return false;
            }
        }
        setItem(mPlayer, p, taken);
        return true;
    }

    bool drop(Ref const& src, int n, bool randomly) {
        if (!src.valid || n <= 0 || locked(src)) return false;
        touch(src);
        if (src.isMenu()) {
            if (click(src, ClickType::Drop, n, ItemStack::EMPTY_ITEM())) return false;
            auto taken = slotAt(src.index)->take(mPlayer, n);
            if (taken.isNull()) return false;
            mPlayer.drop(taken, randomly);
            return true;
        }
        auto s = getItem(mPlayer, src);
        if (s.isNull()) return false;
        n = std::min<int>(n, s.mCount);
        mPlayer.drop(withCount(s, n), randomly);
        setItem(mPlayer, src, withCount(s, s.mCount - n));
        return true;
    }

    bool destroy(Ref const& src, int n) {
        if (!src.valid || src.isMenu() || n <= 0 || locked(src) || !mPlayer.isCreative()) return false;
        touch(src);
        auto s = getItem(mPlayer, src);
        if (s.isNull()) return false;
        setItem(mPlayer, src, withCount(s, s.mCount - std::min<int>(n, s.mCount)));
        return true;
    }

private:
    bool playerToPlayer(Ref const& src, Ref const& dst, int n) {
        auto s = getItem(mPlayer, src);
        if (s.isNull() || s.mCount < n) return false;
        auto d = getItem(mPlayer, dst);
        if (d.isNull()) {
            d = withCount(s, n);
        } else if (d.matchesItem(s) && d.mCount + n <= d.getMaxStackSize()) {
            d.set(d.mCount + n);
        } else {
            return false;
        }
        setItem(mPlayer, src, withCount(s, s.mCount - n));
        setItem(mPlayer, dst, d);
        return true;
    }

    bool menuToPlayer(Ref const& src, Ref const& dst, int n) {
        auto type = dst.where == Where::Cursor ? ClickType::Take : ClickType::QuickMove;
        if (click(src, type, n, ItemStack::EMPTY_ITEM())) return false;
        auto* slot  = slotAt(src.index);
        auto  taken = slot->take(mPlayer, n);
        if (taken.isNull()) return false;
        auto d     = getItem(mPlayer, dst);
        int  moved = 0;
        if (d.isNull()) {
            d     = taken;
            moved = taken.mCount;
        } else if (d.matchesItem(taken)) {
            moved = std::min<int>(taken.mCount, std::max(0, d.getMaxStackSize() - d.mCount));
            d.set(d.mCount + moved);
        }
        if (moved > 0) setItem(mPlayer, dst, d);
        int left = taken.mCount - moved;
        if (left > 0) {
            auto rest = withCount(taken, left);
            if (type == ClickType::QuickMove) {
                mPlayer.add(rest); // anywhere else in the inventory
                touch(fromAddress({Where::Inventory, 0}));
            }
            if (!rest.isNull() && rest.mCount > 0) slot->accept(mPlayer, rest);
        }
        return moved > 0 || left < taken.mCount;
    }

    bool playerToMenu(Ref const& src, Ref const& dst, int n) {
        auto s = getItem(mPlayer, src);
        if (s.isNull()) return false;
        n            = std::min<int>(n, s.mCount);
        auto offered = withCount(s, n);
        int  accepted = 0;
        if (src.where == Where::Cursor) {
            if (click(dst, ClickType::Place, n, offered)) return false;
            accepted = slotAt(dst.index)->accept(mPlayer, offered);
        } else {
            accepted = mMenu.quickMoveIn(mPlayer, offered);
            menuChanged = true;
        }
        if (accepted <= 0) return false;
        setItem(mPlayer, src, withCount(s, s.mCount - accepted));
        return true;
    }

    bool menuToMenu(Ref const& src, Ref const& dst, int n) {
        if (src.index == dst.index) return false;
        if (click(src, ClickType::Take, n, ItemStack::EMPTY_ITEM())) return false;
        auto* from  = slotAt(src.index);
        auto* to    = slotAt(dst.index);
        if (!to) return false;
        auto  taken = from->take(mPlayer, n);
        if (taken.isNull()) return false;
        int acc = to->accept(mPlayer, taken);
        if (acc < taken.mCount) from->accept(mPlayer, withCount(taken, taken.mCount - acc));
        return acc > 0;
    }

    /// The slot a menu address refers to (simulateOn replaces the menu's own slots).
    [[nodiscard]] Slot* slotAt(int index) const { return mOverride ? mOverride.get() : mMenu.slot(index); }

    Player&               mPlayer;
    Menu&                 mMenu;
    std::shared_ptr<Slot> mOverride;
};

ItemStackResponseSlotInfo slotInfo(Ref const& r, ItemStack const& item) {
    return ItemStackResponseSlotInfo{
        r.requested,
        r.requested,
        static_cast<uchar>(item.isNull() ? 0 : item.mCount),
        ItemStackNetId(netIdOf(item)),
        Bedrock::Safety::RedactableString(item.isNull() ? std::string{} : item.getCustomName()),
        static_cast<short>(0)
    };
}

} // namespace

bool MenuManager::simulate(Player& player, SimAction const& action) {
    auto it = mSessions.find(keyOf(player));
    if (it == mSessions.end()) return false;
    auto& s = *it->second;
    s.menu->ensureBuilt(player);
    ActionRunner run(player, *s.menu);
    auto         src = fromAddress(action.src);
    auto         dst = fromAddress(action.dst);
    mBusy            = true;
    bool ok          = false;
    switch (action.type) {
    case ActionType::Take:
    case ActionType::Place:
        ok = run.transfer(src, dst, action.amount);
        break;
    case ActionType::Swap:
        ok = run.swap(src, dst);
        break;
    case ActionType::Drop:
        ok = run.drop(src, action.amount, false);
        break;
    case ActionType::Destroy:
        ok = run.destroy(src, action.amount);
        break;
    }
    mBusy = false;
    if (auto again = mSessions.find(keyOf(player)); again != mSessions.end()) sync(player, *again->second, false);
    runDeferred();
    return ok;
}

bool MenuManager::simulateOn(Player& player, SimAction const& action, std::shared_ptr<Slot> menuSlot) {
    auto it = mSessions.find(keyOf(player));
    if (it == mSessions.end() || !menuSlot) return false;
    auto menu = it->second->menu; // keep alive even if a callback replaces it
    menu->ensureBuilt(player);
    ActionRunner run(player, *menu, std::move(menuSlot));
    auto         src = fromAddress(action.src);
    auto         dst = fromAddress(action.dst);
    mBusy            = true;
    bool ok          = false;
    switch (action.type) {
    case ActionType::Take:
    case ActionType::Place:
        ok = run.transfer(src, dst, action.amount);
        break;
    case ActionType::Swap:
        ok = run.swap(src, dst);
        break;
    case ActionType::Drop:
        ok = run.drop(src, action.amount, false);
        break;
    case ActionType::Destroy:
        ok = run.destroy(src, action.amount);
        break;
    }
    mBusy = false;
    if (auto again = mSessions.find(keyOf(player)); again != mSessions.end() && again->second->menu == menu) {
        sync(player, *again->second, false);
    }
    runDeferred();
    return ok;
}

void MenuManager::resyncPlayer(Player& player) {
    player.sendInventory(false);
    InventorySlotPacket cursorPkt{
        InventorySlotPacketPayload{ContainerID::PlayerOnlyUi, 0, player.mPlayerUIContainer->getItem(0)}
    };
    cursorPkt.sendTo(player);
}

bool MenuManager::handleRequestPacket(Player& player, void const* raw) {
    auto it = mSessions.find(keyOf(player));
    if (it == mSessions.end() || !it->second->shown() || it->second->virtualScreen) return false;
    auto& packet  = *static_cast<ItemStackRequestPacket const*>(raw);
    auto& session = *it->second;
    auto  menu    = session.menu; // keep alive even if a callback replaces it
    menu->ensureBuilt(player);

    auto  respPkt  = MinecraftPackets::createPacket(MinecraftPacketIds::ItemStackResponse);
    auto& response = static_cast<ItemStackResponsePacket&>(*respPkt);

    mBusy = true;
    for (auto const& request : packet.mRequests.get()) {
        ActionRunner run(player, *menu);
        bool         ok = !request.mActions->empty();
        for (auto const& action : request.mActions.get()) {
            if (!ok) break;
            using namespace ItemStackRequestCereal;
            if (auto const* take = std::get_if<TakeActionData>(&action)) {
                ok = run.transfer(resolve(take->mSource, *menu), resolve(take->mDestination, *menu), take->mAmount);
            } else if (auto const* place = std::get_if<PlaceActionData>(&action)) {
                ok = run.transfer(resolve(place->mSource, *menu), resolve(place->mDestination, *menu), place->mAmount);
            } else if (auto const* swap = std::get_if<SwapActionData>(&action)) {
                ok = run.swap(resolve(swap->mSource, *menu), resolve(swap->mDestination, *menu));
            } else if (auto const* drop = std::get_if<DropActionData>(&action)) {
                ok = run.drop(resolve(drop->mSource, *menu), drop->mAmount, drop->mRandomly);
            } else if (auto const* destroy = std::get_if<DestroyActionData>(&action)) {
                ok = run.destroy(resolve(destroy->mSource, *menu), destroy->mAmount);
            } else {
                ok = false; // crafting etc. can't happen in a chest screen
            }
        }
        // Bring the displays up to date so the response carries the server truth.
        if (auto cur = mSessions.find(keyOf(player)); cur != mSessions.end() && cur->second->menu == menu) {
            sync(player, *cur->second, false);
        }
        std::vector<ItemStackResponseContainerInfo> containers;
        if (ok) {
            for (auto const& r : run.touched) {
                auto item = r.isMenu() ? shownItem(player, r.index) : getItem(player, r);
                auto info = slotInfo(r, item);
                auto existing = std::find_if(containers.begin(), containers.end(), [&](auto const& c) {
                    return c.mFullContainerName->mName == r.name && c.mFullContainerName->mDynamicId.get() == r.dynamicId;
                });
                if (existing == containers.end()) {
                    std::vector<ItemStackResponseSlotInfo> slots;
                    slots.push_back(std::move(info));
                    containers.push_back(ItemStackResponseContainerInfo{
                        FullContainerName{r.name, r.dynamicId},
                        std::move(slots)
                    });
                } else {
                    existing->mSlots->push_back(std::move(info));
                }
            }
        }
        response.mResponses->push_back(ItemStackResponseInfo{
            ok ? ItemStackNetResult::Success : ItemStackNetResult::Error,
            request.mClientRequestId.get(),
            std::move(containers)
        });
    }
    mBusy = false;

    response.sendTo(player);
    // Authoritative re-sync of everything the client may have predicted differently.
    player.sendInventory(false);
    InventorySlotPacket cursorPkt{
        InventorySlotPacketPayload{ContainerID::PlayerOnlyUi, 0, player.mPlayerUIContainer->getItem(0)}
    };
    cursorPkt.sendTo(player);
    if (auto cur = mSessions.find(keyOf(player)); cur != mSessions.end() && cur->second->menu == menu) {
        sync(player, *cur->second, true);
    }
    runDeferred();
    return true;
}

bool MenuManager::handleClosePacket(Player& player, int containerId, int containerType) {
    auto key  = keyOf(player);
    auto it   = mSessions.find(key);
    bool ours = it != mSessions.end() && it->second->id == containerId && it->second->shown() && !it->second->virtualScreen;
    // Confirms a client's close. The client waits for this before it opens any other screen
    // (another chest, but also chat), so every close of one of our screens is answered: the
    // player's own close, and the close the client sends back after a server-initiated one.
    auto confirm = [&] {
        auto  pkt               = MinecraftPackets::createPacket(MinecraftPacketIds::ContainerClose);
        auto& c                 = static_cast<ContainerClosePacket&>(*pkt);
        c.mContainerId          = static_cast<ContainerID>(containerId);
        c.mContainerType        = containerType >= 0 ? static_cast<SharedTypes::Legacy::ContainerType>(containerType)
                                                     : SharedTypes::Legacy::ContainerType::Container;
        c.mServerInitiatedClose = false;
        c.sendTo(player);
        menuDebugLog("-> ContainerClose id={} (ack)", containerId);
    };
    menuDebugLog("<- ContainerClose id={} ours={}", containerId, ours);

    // A close for a screen we closed server-side (menu replaced / closed by the server).
    bool                                       pending = false;
    std::vector<std::function<void(Player&)>> then;
    if (auto p = mPendingCloses.find(key); p != mPendingCloses.end()) {
        auto& list = p->second;
        auto  c    = std::find_if(list.begin(), list.end(), [&](PendingClose const& x) { return x.id == containerId; });
        if (c != list.end()) {
            pending = true;
            then    = std::move(c->then);
            list.erase(c);
            if (list.empty()) mPendingCloses.erase(p);
        }
    }
    // The client rejected our ContainerOpen because another screen was still up (chat after a
    // command): vanilla answers this close as usual; we send the open again a bit later.
    if (containerId == -1 && it != mSessions.end() && it->second->shown() && it->second->clientOpened && !it->second->virtualScreen
        && mTick - it->second->openedAt <= kRejectWindow && it->second->retries < kMaxOpenRetries) {
        auto& s = *it->second;
        menuDebugLog("client rejected ContainerOpen id={}; opening again in {} ticks", s.id, kRetryDelay);
        ++s.retries;
        s.phase        = Session::Phase::Placed;
        s.wait         = kRetryDelay;
        s.clientOpened = false;
        s.needFull     = true;
        return false;
    }
    if (!ours && !pending) return false; // not ours: vanilla handles it
    confirm();
    if (ours) finish(&player, key, false);
    // A menu waiting in AwaitClose is placed on the next tick (see tick()).
    if (!then.empty()) {
        after(kAfterAck, [key, then = std::move(then)] {
            if (auto* p = findPlayer(key)) {
                for (auto const& fn : then) fn(*p);
            }
        });
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// Hooks / module
// ---------------------------------------------------------------------------------------------

namespace {
// $handle overrides a NetEventCallback virtual: the hooked `this` is that base subobject, so use
// thisFor<NetEventCallback>() (like LeviLamina does) to reach ServerNetworkHandler members.
LL_TYPE_INSTANCE_HOOK(
    MenuItemStackRequestHook,
    ll::memory::HookPriority::High,
    ServerNetworkHandler,
    &ServerNetworkHandler::$handle,
    void,
    NetworkIdentifier const&      source,
    ItemStackRequestPacket const& packet
) {
    if (auto* player = thisFor<NetEventCallback>()->_getServerPlayer(source, packet.mSenderSubId)) {
        if (MenuManager::get().handleRequestPacket(*player, &packet)) return;
    }
    origin(source, packet);
}

LL_TYPE_INSTANCE_HOOK(
    MenuContainerCloseHook,
    ll::memory::HookPriority::High,
    ServerNetworkHandler,
    &ServerNetworkHandler::$handle,
    void,
    NetworkIdentifier const&    source,
    ContainerClosePacket const& packet
) {
    if (auto* player = thisFor<NetEventCallback>()->_getServerPlayer(source, packet.mSenderSubId)) {
        if (MenuManager::get().handleClosePacket(
                *player,
                static_cast<int>(static_cast<schar>(packet.mContainerId)),
                static_cast<int>(packet.mContainerType)
            )) {
            return;
        }
    }
    origin(source, packet);
}
} // namespace

void MenuManager::install() {
    MenuItemStackRequestHook::hook();
    MenuContainerCloseHook::hook();
    auto& bus = ll::event::EventBus::getInstance();
    mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([this](auto&) { tick(); }));
    mListeners.push_back(bus.emplaceListener<ll::event::PlayerDisconnectEvent>([this](ll::event::PlayerDisconnectEvent& ev) {
        auto key = keyOf(ev.self());
        if (mSessions.contains(key)) finish(&ev.self(), key, false);
        mPendingCloses.erase(key);
        mRecentFake.erase(key);
    }));
    mListeners.push_back(bus.emplaceListener<ll::event::PlayerDieEvent>([this](ll::event::PlayerDieEvent& ev) {
        close(ev.self());
    }));
    // Client: local worlds come and go (and reuse player ids); never carry sessions over.
    mListeners.push_back(bus.emplaceListener<ll::event::ServerStoppingEvent>([this](auto&) { closeAll(true); }));
    // A cursor item kept through a shutdown (see returnCursorOnShutdown) comes back on the next join.
    mListeners.push_back(bus.emplaceListener<ll::event::PlayerJoinEvent>([this](ll::event::PlayerJoinEvent& ev) {
        auto& player = ev.self();
        if (player.isSimulatedPlayer() || mSessions.contains(keyOf(player))) return;
        if (player.mPlayerUIContainer->getItem(0).isNull()) return;
        log().info("menu: giving {} back the cursor item kept at the last shutdown", player.getRealName());
        returnCursor(player);
        player.sendInventory(false);
    }));
}

void MenuManager::closeAll() { closeAll(false); }

void MenuManager::closeAll(bool shutdown) {
    std::vector<std::int64_t> keys;
    for (auto& [k, _] : mSessions) keys.push_back(k);
    // Shutdown runs on whatever thread stops the world (BDS: the main thread, in
    // ServerInstance::leaveGameSync). The item registry is thread-local and only set on the server
    // thread: copying an ItemStack without it hits the game's 0xDEADC0DE assertion. Lend this thread
    // the level's registry while the sessions close (restored by the scope's destructor).
    std::optional<ItemRegistryManager::ScopedItemRegistry> registryScope;
    if (shutdown && !keys.empty()) {
        if (auto level = ll::service::getLevel()) registryScope.emplace(std::weak_ptr<ItemRegistry>(level->getItemRegistry().mWeakRegistry));
    }
    for (auto k : keys) finish(findPlayer(k), k, !shutdown, shutdown);
    mPendingCloses.clear();
    mRecentFake.clear();
    mDelayed.clear();
    mDeferred.clear();
}

void MenuManager::uninstall() {
    closeAll(true);
    mRefused.clear();
    auto& bus = ll::event::EventBus::getInstance();
    for (auto& l : mListeners) bus.removeListener(l);
    mListeners.clear();
    MenuItemStackRequestHook::unhook();
    MenuContainerCloseHook::unhook();
}

} // namespace abp::ui

ABP_REGISTER_MODULE_ORDERED(abp::ui::MenuManager, abp::ModuleOrder::Menu);
