#ifdef ABP_CLIENT

#include "client/ui/screen/StorageOverlay.h"

namespace abp::client::ui {

std::vector<std::unique_ptr<StorageOverlay>>& storageOverlays() {
    static std::vector<std::unique_ptr<StorageOverlay>> instances;
    static bool                                         created = false;
    if (!created) {
        created = true;
        for (auto const& factory : StorageOverlayRegistry::entries()) {
            if (auto o = factory ? factory() : nullptr) instances.push_back(std::move(o));
        }
    }
    return instances;
}

} // namespace abp::client::ui

#endif // ABP_CLIENT
