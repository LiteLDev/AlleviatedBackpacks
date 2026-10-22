// Ender Linker interactions on Bedrock (Sophisticated Core's EnderLinkerItem):
//
// - In the backpack screen: right click a backpack slot with the linker on the cursor, or the
//   linker's slot with a backpack on the cursor (SC's overrideStackedOnOther /
//   overrideOtherStackedOnMe). Works on the player inventory and on backpack storage slots.
// - In the world: use the linker on a placed backpack (SC's onItemUseFirst on a block endpoint).
//
// SC's third way, crafting the linker with a storage, needs NBT-dependent recipe results, which
// Bedrock's data-driven recipes can't express; the screen click covers the same cases.

#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/platform/UUID.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/dimension/Dimension.h"

#include "feature/linked/Linked.h"
#include "feature/placed/PlacedBackpacks.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "net/NativeScreens.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"

namespace abp::linked {

namespace {

constexpr int kUseCooldownTicks = 5;

/// Right click in the native backpack screen (see the file comment). True when handled.
bool screenClick(Player& player, ui::BackpackMenu* menu, std::string_view area, int index) {
    ItemStack cursor = player.mPlayerUIContainer->getItem(0);
    ItemStack target;
    std::shared_ptr<game::Backpack> bp;
    if (area == "inventory") {
        if (index < 0 || index >= 36) return false;
        target = player.getInventory().getItem(index);
    } else {
        if (!menu) return false;
        bp = menu->backpack();
        if (!bp || index < 0 || index >= bp->inventory().size()) return false;
        auto const& d = bp->inventory().at(index);
        if (d.empty()) return false;
        target = game::ItemConv::toStack(d);
    }
    bool linkerOnCursor   = isLinker(cursor) && game::isBackpack(target) && target.mCount == 1;
    bool backpackOnCursor = game::isBackpack(cursor) && cursor.mCount == 1 && isLinker(target);
    if (!linkerOnCursor && !backpackOnCursor) return false;

    std::optional<std::string> before = game::backpackUuid(linkerOnCursor ? target : cursor);
    auto                       result = linkerOnCursor ? linkWithFeedback(player, cursor, target, nullptr)
                                                       : linkWithFeedback(player, target, cursor, nullptr);
    if (result != LinkResult::Success) return true;

    player.mPlayerUIContainer->setItem(0, cursor);
    if (area == "inventory") {
        game::setItemAt(player, {game::ItemLocation::Kind::Inventory, index}, target);
    } else {
        bp->inventory().set(index, target.isNull() ? core::ItemStackData{} : game::ItemConv::toData(target));
    }
    // SC closes a screen that shows the endpoint that was just linked (its storage changed).
    if (menu && before && menu->backpack() && menu->backpack()->uuid() == *before) {
        auto after = game::backpackUuid(linkerOnCursor ? target : cursor);
        if (after != before) ui::MenuManager::get().close(player);
    }
    return true;
}

struct PendingUse {
    std::int64_t player;
    int          dim;
    BlockPos     pos;
};

class EnderLinkerUse {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>(
            [this](ll::event::PlayerInteractBlockEvent& ev) { onInteract(ev); },
            ll::event::EventPriority::Highest // before the placed backpack opens its GUI
        ));
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([this](ll::event::ServerLevelTickEvent& ev) {
            ++mNow;
            runPending(ev.level());
        }));
        ui::MenuManager::Observer o;
        o.beforeShow = [](Player& player, ui::Menu& menu, int) {
            auto* bm = dynamic_cast<ui::BackpackMenu*>(&menu);
            if (!bm || !bm->source()) return;
            auto d = endpointOf(game::itemAt(player, *bm->source()));
            if (d) Service::get().recordEndpointOpened(d->group, d->endpoint, player.getUuid().asString());
        };
        mObserver = ui::MenuManager::get().addObserver(std::move(o));
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        ui::MenuManager::get().removeObserver(mObserver);
        mPending.clear();
    }

private:
    void onInteract(ll::event::PlayerInteractBlockEvent& ev) {
        auto& player = ev.self();
        if (player.isClientSide() || ev.isCancelled() || !isLinker(player.getSelectedItem())) return;
        auto& region = player.getDimensionBlockSource();
        auto  loc    = placed::locationOf(region, ev.blockPos());
        if (!placed::PlacedBackpacks::isBackpackBlock(region.getBlock(ev.blockPos()))) return;
        if (!placed::PlacedBackpacks::get().at(loc)) return;
        ev.cancel();
        auto key = player.getOrCreateUniqueID().rawID;
        if (auto it = mCooldown.find(key); it != mCooldown.end() && mNow - it->second < kUseCooldownTicks) return;
        mCooldown[key] = mNow;
        // Next tick, outside the item-use transaction (a cancelled use rolls back its changes).
        mPending.push_back({key, static_cast<int>(player.getDimensionId()), ev.blockPos()});
    }

    void runPending(Level& level) {
        if (mPending.empty()) return;
        auto pending = std::move(mPending);
        mPending.clear();
        for (auto const& p : pending) {
            ActorUniqueID id;
            id.rawID     = p.player;
            auto* player = level.getPlayer(id);
            if (!player || static_cast<int>(player->getDimensionId()) != p.dim) continue;
            linkPlaced(*player, p.pos);
        }
        if (mCooldown.size() > 256) std::erase_if(mCooldown, [&](auto const& e) { return mNow - e.second > 100; });
    }

    static void linkPlaced(Player& player, BlockPos const& pos) {
        int  slot   = player.getSelectedItemSlot();
        auto linker = ItemStack(player.getSelectedItem());
        if (!isLinker(linker)) return;
        auto& region = player.getDimensionBlockSource();
        auto& placed = placed::PlacedBackpacks::get();
        auto  entry  = placed.at(placed::locationOf(region, pos));
        if (!entry) return;
        auto item = placed.take(region, pos);
        if (!item) return;
        auto result = linkWithFeedback(player, linker, *item, &pos);
        if (placed.place(region, pos, *item, entry->facing) != placed::PlacedBackpacks::PlaceResult::Ok) {
            // Never lose the backpack: drop it where it stood.
            region.getLevel().getSpawner().spawnItem(region, *item, nullptr, pos.center(), 10);
            log().warn("linked storage: could not put the placed backpack back at {} {} {}; dropped it", pos.x, pos.y, pos.z);
        }
        if (result == LinkResult::Success) {
            player.getInventory().setItem(slot, linker);
            player.sendInventory(false);
        }
    }

    std::vector<ll::event::ListenerPtr>      mListeners;
    std::vector<PendingUse>                  mPending;
    std::unordered_map<std::int64_t, std::int64_t> mCooldown;
    std::int64_t                             mNow      = 0;
    int                                      mObserver = 0;
};

} // namespace
} // namespace abp::linked

ABP_REGISTER_SCREEN_CLICK(abp::net::ScreenClickInterceptor{"linked:ender_linker", &abp::linked::screenClick});
ABP_REGISTER_MODULE(abp::linked::EnderLinkerUse);
