#ifdef ABP_CLIENT

// Client side of the bundle rule (feature/bundle/BundleRule.h): remembers what the server
// announced, and the bundle UI only sends moves of a backpack into a bundle the server accepts
// (vanilla's client never asks the storage item validation before sending the request).

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/network/NetEventCallback.h"
#include "mc/network/ServerNetworkHandler.h"
#include "mc/world/containers/FullContainerName.h"
#include "mc/world/containers/SlotData.h"
#include "mc/world/containers/managers/controllers/ContainerManagerController.h"
#include "mc/world/containers/models/ContainerModel.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/components/storage_item_utility/StorageItemWeightData.h"
#include "mc/world/item/components/storage_item_utility/StorageItemUtility.h"

#include "feature/bundle/BundleRule.h"
#include "mod/Module.h"

namespace abp::client::bundle {
namespace {

/// A server-to-client packet that really reached the client (not this process's server).
bool fromServer(NetEventCallback& callback) {
    auto handler = ll::service::getServerNetworkHandler();
    return !handler || static_cast<NetEventCallback*>(handler.as_ptr()) != &callback;
}

} // namespace

class BundleRuleHandler : public ll::network::PacketHandlerBase<BundleRuleHandler, abp::bundle::BundleRulePacket> {
public:
    void handlePacket(NetworkIdentifier const&, NetEventCallback& cb, abp::bundle::BundleRulePacket const& p) const {
        if (!fromServer(cb) || !p.valid()) return;
        auto it = p.body.find("allowed");
        if (it == p.body.end() || !it->is_boolean()) return;
        abp::bundle::setClientRule(it->get<bool>());
    }
};

namespace {

/// The client refuses to move `item` into a bundle (logged).
bool refused(ItemStackBase const& item) {
    if (item.isNull() || abp::bundle::fitsBundles(item, true)) return false;
    abp::bundle::noteDecision(true, false, item);
    return true;
}

} // namespace
} // namespace abp::client::bundle

// A slot's item into a bundle (cursor item clicked onto a bundle, bundle clicked onto an item).
LL_TYPE_INSTANCE_HOOK(
    BundleMoveIntoHook,
    ll::memory::HookPriority::Normal,
    ContainerManagerController,
    &ContainerManagerController::handleMoveItemFromSlotIntoStorageItem,
    bool,
    SlotData const&       srcSlot,
    ItemStack const&      dstStorageItem,
    std::optional<uchar>  requestedAmount
) {
    if (abp::client::bundle::refused(getItemStack(srcSlot.mCollectionName, srcSlot.mCollectionIndex))) return false;
    return origin(srcSlot, dstStorageItem, requestedAmount);
}

// The selected item of one bundle into another bundle.
LL_TYPE_INSTANCE_HOOK(
    BundleMoveBetweenHook,
    ll::memory::HookPriority::Normal,
    ContainerManagerController,
    &ContainerManagerController::handleMoveSelectedItemFromStorageItemToStorageItem,
    bool,
    ItemStack const& srcStorageItem,
    int              selectedItemIdx,
    ItemStack const& dstStorageItem
) {
    if (auto name = StorageItemUtility::getStorageItemID(srcStorageItem)) {
        if (auto model = getDynamicContainerModel(*name)) {
            if (abp::client::bundle::refused(model->getItemStack(selectedItemIdx))) return false;
        }
    }
    return origin(srcStorageItem, selectedItemIdx, dstStorageItem);
}

namespace abp::client::bundle {
namespace {

class BundleRuleClientModule {
public:
    void install() {
        BundleMoveIntoHook::hook();
        BundleMoveBetweenHook::hook();
        mExit = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientExitLevelEvent>([](auto&) {
            abp::bundle::setClientRule(std::nullopt);
        });
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mExit);
        BundleMoveBetweenHook::unhook();
        BundleMoveIntoHook::unhook();
        abp::bundle::setClientRule(std::nullopt);
    }

private:
    ll::event::ListenerPtr mExit;
};

} // namespace
} // namespace abp::client::bundle

ABP_REGISTER_MODULE(abp::client::bundle::BundleRuleClientModule);

#endif // ABP_CLIENT
