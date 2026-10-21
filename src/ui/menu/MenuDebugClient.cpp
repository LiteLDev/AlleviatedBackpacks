// Client build only: logs what the game client receives for our fake chests (ContainerOpen /
// ContainerClose / InventoryContent), the client-side block at the chest position and the top
// screen, when `menu.json` `debugPackets` is on. Diagnostics only; installs nothing otherwise.
#ifdef ABP_CLIENT

#include "ll/api/memory/Hook.h"
#include "ll/api/service/TargetedBedrock.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/network/LegacyClientNetworkHandler.h"
#include "mc/network/packet/ContainerClosePacket.h"
#include "mc/network/packet/ContainerOpenPacket.h"
#include "mc/network/packet/InventoryContentPacket.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "mod/Module.h"
#include "ui/menu/MenuDebug.h"

namespace abp::ui {

namespace {
std::string topScreen() {
    auto ci = ll::service::getClientInstance();
    if (!ci) return "?";
    return ci->getTopScreenName();
}

std::string describeBlock(BlockPos const& pos) {
    auto ci = ll::service::getClientInstance();
    if (!ci) return "?";
    auto* region = ci->getRegion();
    if (!region) return "no-region";
    auto const& b   = region->getBlock(pos);
    std::string out = b.getTypeName();
    out += region->getBlockEntity(pos) ? " +be" : " -be";
    if (auto* p = ci->getLocalPlayer()) {
        auto const& v = p->getPosition();
        out += std::format(" player=({:.1f},{:.1f},{:.1f})", v.x, v.y, v.z);
    }
    return out;
}

LL_TYPE_INSTANCE_HOOK(
    MenuDbgClientOpenHook,
    ll::memory::HookPriority::Normal,
    LegacyClientNetworkHandler,
    &LegacyClientNetworkHandler::$handle,
    void,
    NetworkIdentifier const&   source,
    ContainerOpenPacket const& packet
) {
    BlockPos pos = packet.mPos;
    menuDebugLog(
        "client <- ContainerOpen id={} type={} pos=({},{},{}) block={} screen={}",
        static_cast<int>(static_cast<schar>(packet.mContainerId)),
        static_cast<int>(packet.mType),
        pos.x,
        pos.y,
        pos.z,
        describeBlock(pos),
        topScreen()
    );
    origin(source, packet);
    menuDebugLog("client    ContainerOpen handled, screen={}", topScreen());
}

LL_TYPE_INSTANCE_HOOK(
    MenuDbgClientCloseHook,
    ll::memory::HookPriority::Normal,
    LegacyClientNetworkHandler,
    &LegacyClientNetworkHandler::$handle,
    void,
    NetworkIdentifier const&    source,
    ContainerClosePacket const& packet
) {
    menuDebugLog(
        "client <- ContainerClose id={} type={} serverInitiated={} screen={}",
        static_cast<int>(static_cast<schar>(packet.mContainerId)),
        static_cast<int>(packet.mContainerType),
        static_cast<bool>(packet.mServerInitiatedClose),
        topScreen()
    );
    origin(source, packet);
    menuDebugLog("client    ContainerClose handled, screen={}", topScreen());
}

LL_TYPE_INSTANCE_HOOK(
    MenuDbgClientContentHook,
    ll::memory::HookPriority::Normal,
    LegacyClientNetworkHandler,
    &LegacyClientNetworkHandler::$handle,
    void,
    NetworkIdentifier const&      source,
    InventoryContentPacket const& packet
) {
    int id = static_cast<int>(static_cast<schar>(packet.mInventoryId));
    if (id > 0 && id < 100) menuDebugLog("client <- InventoryContent id={} screen={}", id, topScreen());
    origin(source, packet);
}
} // namespace

class MenuClientDebug {
public:
    void install() {
        if (!menuDebug()) return;
        MenuDbgClientOpenHook::hook();
        MenuDbgClientCloseHook::hook();
        MenuDbgClientContentHook::hook();
        mHooked = true;
    }
    void uninstall() {
        if (!mHooked) return;
        MenuDbgClientOpenHook::unhook();
        MenuDbgClientCloseHook::unhook();
        MenuDbgClientContentHook::unhook();
        mHooked = false;
    }

private:
    bool mHooked = false;
};

} // namespace abp::ui

ABP_REGISTER_MODULE_ORDERED(abp::ui::MenuClientDebug, abp::ModuleOrder::Menu + 2);

#endif
