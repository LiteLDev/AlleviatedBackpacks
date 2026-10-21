#include "ui/menu/MenuManager.h"

#include <algorithm>
#include <cmath>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDieEvent.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
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

// `ItemStackRequestId` has two virtual `clientInit` members the game doesn't export. Copying a
// request id (into our responses) emits its vtable here, so give them (never called) bodies.
template <>
void TypedClientNetId<ItemStackRequestIdTag, int, 0>::clientInit(TypedClientNetId const&) {}
template <>
void TypedClientNetId<ItemStackRequestIdTag, int, 0>::clientInit() {}

namespace abp::ui {

namespace {
constexpr int kOpenDelay   = 3; ///< ticks between the fake block packets and ContainerOpen
constexpr int kSwitchDelay = 5; ///< same, when replacing an open menu
constexpr int kAfterClose  = 5; ///< ticks before `closeThen` callbacks run
constexpr int kDiffEvery   = 2; ///< ticks between display diffs
constexpr float kMaxDistance = 8.0f;

std::int64_t keyOf(Player const& p) { return p.getOrCreateUniqueID().rawID; }

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
}

void sendBlockActor(Player& p, BlockPos const& pos, CompoundTag tag) {
    BlockActorDataPacket pkt;
    pkt.mPos  = pos;
    pkt.mData = std::move(tag);
    pkt.sendTo(p);
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
    std::int64_t                               key;
    std::shared_ptr<Menu>                      menu;
    int                                        id = 1;
    std::vector<BlockPos>                      fake;
    int                                        dim = 0;
    int                                        openIn = kOpenDelay;
    bool                                       shown = false;
    bool                                       needFull = true;
    std::optional<std::uint64_t>               lastVersion;
    int                                        diffIn = 0;
    std::vector<ItemStack>                     sent;
    std::vector<std::function<void(Player&)>> afterClose;
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

    auto key      = keyOf(player);
    bool replaced = mSessions.contains(key);
    if (replaced) finish(&player, key, true);

    auto s  = std::make_unique<Session>();
    s->key  = key;
    s->menu = std::move(menu);
    s->dim  = static_cast<int>(player.getDimensionId());
    // Next container id, the way vanilla counts (1..99), written back so vanilla won't reuse it.
    int next = static_cast<int>(static_cast<schar>(sp->mContainerCounter)) + 1;
    if (next < static_cast<int>(ContainerID::First) || next >= static_cast<int>(ContainerID::Last)) {
        next = static_cast<int>(ContainerID::First);
    }
    sp->mContainerCounter = static_cast<ContainerID>(next);
    s->id                 = next;
    s->openIn             = replaced ? kSwitchDelay : kOpenDelay;

    // Fake chest(s) two blocks above the head (below the feet if that's out of the world).
    auto& region = player.getDimensionBlockSource();
    auto  base   = player.getFeetBlockPos();
    base.y += 3;
    if (base.y >= region.getMaxHeight()) base.y = std::max<int>(player.getFeetBlockPos().y - 3, region.getMinHeight());
    s->fake.push_back(base);
    if (s->menu->rows() == Menu::Rows::Six) s->fake.push_back(BlockPos{base.x + 1, base.y, base.z});

    for (std::size_t i = 0; i < s->fake.size(); ++i) {
        auto const& pos = s->fake[i];
        sendBlock(player, pos, *chest);
        CompoundTag tag;
        tag["id"]         = std::string("Chest");
        tag["x"]          = pos.x;
        tag["y"]          = pos.y;
        tag["z"]          = pos.z;
        tag["isMovable"]  = static_cast<uchar>(0);
        tag["Findable"]   = static_cast<uchar>(0);
        tag["CustomName"] = s->menu->title();
        if (s->fake.size() == 2) {
            auto const& other = s->fake[1 - i];
            tag["pairx"]      = other.x;
            tag["pairz"]      = other.z;
            tag["pairlead"]   = static_cast<uchar>(i == 0 ? 1 : 0);
        }
        sendBlockActor(player, pos, std::move(tag));
    }

    auto& session = *s;
    mSessions[key] = std::move(s);
    session.menu->invalidate();
    try {
        session.menu->onOpen(player);
    } catch (std::exception const& e) {
        log().error("menu onOpen failed: {}", e.what());
    }
    return true;
}

void MenuManager::close(Player& player) {
    auto key = keyOf(player);
    if (!mSessions.contains(key)) return;
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
    return it != mSessions.end() && it->second->shown;
}

void MenuManager::after(int ticks, std::function<void()> fn) { mDelayed.push_back({ticks, std::move(fn)}); }

void MenuManager::finish(Player* player, std::int64_t key, bool sendClose) {
    auto it = mSessions.find(key);
    if (it == mSessions.end()) return;
    auto s = std::move(it->second);
    mSessions.erase(it);
    mClosedIds[key] = s->id;
    if (player) {
        if (sendClose && s->shown) {
            auto  pkt                = MinecraftPackets::createPacket(MinecraftPacketIds::ContainerClose);
            auto& c                  = static_cast<ContainerClosePacket&>(*pkt);
            c.mContainerId           = static_cast<ContainerID>(s->id);
            c.mContainerType         = SharedTypes::Legacy::ContainerType::Container;
            c.mServerInitiatedClose  = true;
            c.sendTo(*player);
        }
        // Restore what the client should see at the fake positions.
        auto& region = player->getDimensionBlockSource();
        for (auto const& pos : s->fake) {
            sendBlock(*player, pos, region.getBlock(pos));
            if (auto* be = region.getBlockEntity(pos)) {
                if (auto pkt = be->_getUpdatePacket(region)) pkt->sendTo(*player);
            }
        }
        // Give back whatever is still on the cursor.
        ItemStack cursor = player->mPlayerUIContainer->getItem(0);
        if (!cursor.isNull()) {
            player->setPlayerUIItem(PlayerUISlot::CursorSelected, ItemStack::EMPTY_ITEM(), false);
            if (!player->add(cursor) && !cursor.isNull() && cursor.mCount > 0) player->drop(cursor, false);
        }
        player->sendInventory(false);
        try {
            s->menu->onClose(*player);
        } catch (std::exception const& e) {
            log().error("menu onClose failed: {}", e.what());
        }
        for (auto& fn : s->afterClose) {
            after(kAfterClose, [key, fn = std::move(fn)] {
                if (auto* p = findPlayer(key)) fn(*p);
            });
        }
    }
    try {
        s->menu->onDestroyed();
    } catch (std::exception const& e) {
        log().error("menu onDestroyed failed: {}", e.what());
    }
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
    if (it == mSessions.end() || it->second->shown) return;
    it->second->openIn = 0;
    it->second->shown  = true;
    sync(player, *it->second, true);
}

void MenuManager::tick() {
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
        auto const& pos  = player->getPosition();
        auto const& fake = s.fake.front();
        float dx = pos.x - (fake.x + 0.5f), dy = pos.y - (fake.y + 0.5f), dz = pos.z - (fake.z + 0.5f);
        if (!player->isAlive() || static_cast<int>(player->getDimensionId()) != s.dim
            || std::sqrt(dx * dx + dy * dy + dz * dz) > kMaxDistance) {
            finish(player, key, true);
            continue;
        }
        if (!s.shown) {
            if (--s.openIn > 0) continue;
            auto  pkt         = MinecraftPackets::createPacket(MinecraftPacketIds::ContainerOpen);
            auto& o           = static_cast<ContainerOpenPacket&>(*pkt);
            o.mContainerId    = static_cast<ContainerID>(s.id);
            o.mType           = SharedTypes::Legacy::ContainerType::Container;
            o.mPos            = s.fake.front();
            o.mEntityUniqueID = ActorUniqueID::INVALID_ID();
            o.sendTo(*player);
            s.shown = true;
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
    ActionRunner(Player& player, Menu& menu) : mPlayer(player), mMenu(menu) {}

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
        auto* slot = mMenu.slot(r.index);
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
        auto* slot = mMenu.slot(m.index);
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
            auto taken = mMenu.slot(src.index)->take(mPlayer, n);
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
        auto* slot  = mMenu.slot(src.index);
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
            accepted = mMenu.slot(dst.index)->accept(mPlayer, offered);
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
        auto* from  = mMenu.slot(src.index);
        auto* to    = mMenu.slot(dst.index);
        if (!to) return false;
        auto  taken = from->take(mPlayer, n);
        if (taken.isNull()) return false;
        int acc = to->accept(mPlayer, taken);
        if (acc < taken.mCount) from->accept(mPlayer, withCount(taken, taken.mCount - acc));
        return acc > 0;
    }

    Player& mPlayer;
    Menu&   mMenu;
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

bool MenuManager::handleRequestPacket(Player& player, void const* raw) {
    auto it = mSessions.find(keyOf(player));
    if (it == mSessions.end() || !it->second->shown) return false;
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

bool MenuManager::handleClosePacket(Player& player, int containerId) {
    auto key = keyOf(player);
    auto it  = mSessions.find(key);
    bool ours = it != mSessions.end() && it->second->id == containerId;
    if (!ours) {
        // A late close for a menu we already closed server-side: swallow it.
        if (auto c = mClosedIds.find(key); c != mClosedIds.end() && c->second == containerId) {
            mClosedIds.erase(c);
            return true;
        }
        return false;
    }
    auto  pkt               = MinecraftPackets::createPacket(MinecraftPacketIds::ContainerClose);
    auto& c                 = static_cast<ContainerClosePacket&>(*pkt);
    c.mContainerId          = static_cast<ContainerID>(containerId);
    c.mContainerType        = SharedTypes::Legacy::ContainerType::Container;
    c.mServerInitiatedClose = false;
    c.sendTo(player);
    finish(&player, key, false);
    mClosedIds.erase(key);
    return true;
}

// ---------------------------------------------------------------------------------------------
// Hooks / module
// ---------------------------------------------------------------------------------------------

namespace {
LL_TYPE_INSTANCE_HOOK(
    MenuItemStackRequestHook,
    ll::memory::HookPriority::High,
    ServerNetworkHandler,
    &ServerNetworkHandler::$handle,
    void,
    NetworkIdentifier const&      source,
    ItemStackRequestPacket const& packet
) {
    if (auto* player = this->_getServerPlayer(source, packet.mSenderSubId)) {
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
    if (auto* player = this->_getServerPlayer(source, packet.mSenderSubId)) {
        if (MenuManager::get().handleClosePacket(*player, static_cast<int>(static_cast<schar>(packet.mContainerId)))) {
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
        mClosedIds.erase(key);
    }));
    mListeners.push_back(bus.emplaceListener<ll::event::PlayerDieEvent>([this](ll::event::PlayerDieEvent& ev) {
        close(ev.self());
    }));
    // Client: local worlds come and go (and reuse player ids); never carry sessions over.
    mListeners.push_back(bus.emplaceListener<ll::event::ServerStoppingEvent>([this](auto&) { closeAll(); }));
}

void MenuManager::closeAll() {
    std::vector<std::int64_t> keys;
    for (auto& [k, _] : mSessions) keys.push_back(k);
    for (auto k : keys) finish(findPlayer(k), k, true);
    mClosedIds.clear();
    mDelayed.clear();
    mDeferred.clear();
}

void MenuManager::uninstall() {
    closeAll();
    auto& bus = ll::event::EventBus::getInstance();
    for (auto& l : mListeners) bus.removeListener(l);
    mListeners.clear();
    MenuItemStackRequestHook::unhook();
    MenuContainerCloseHook::unhook();
}

} // namespace abp::ui

ABP_REGISTER_MODULE_ORDERED(abp::ui::MenuManager, abp::ModuleOrder::Menu);
