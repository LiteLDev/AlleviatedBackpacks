#ifdef ABP_CLIENT

// Client side of the bundle rule (feature/bundle/BundleRule.h): remembers what the server
// announced, so the bundle UI predicts exactly what the server will accept.

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/network/NetEventCallback.h"
#include "mc/network/ServerNetworkHandler.h"

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

class BundleRuleClientModule {
public:
    void install() {
        mExit = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientExitLevelEvent>([](auto&) {
            abp::bundle::setClientRule(std::nullopt);
        });
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mExit);
        abp::bundle::setClientRule(std::nullopt);
    }

private:
    ll::event::ListenerPtr mExit;
};

} // namespace
} // namespace abp::client::bundle

ABP_REGISTER_MODULE(abp::client::bundle::BundleRuleClientModule);

#endif // ABP_CLIENT
