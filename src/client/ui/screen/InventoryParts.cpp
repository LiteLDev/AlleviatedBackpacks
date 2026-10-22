#ifdef ABP_CLIENT

#include "client/ui/screen/InventoryParts.h"

#include <unordered_map>

#include "client/ui/gfx/Painter.h"
#include "client/ui/widgets/SbStyle.h"

namespace abp::client::ui {

namespace {
std::unordered_map<std::string, InventoryPartFactory>& parts() {
    static std::unordered_map<std::string, InventoryPartFactory> map;
    return map;
}
} // namespace

void InventoryPart::renderErrorOverlay(Painter& p, Rect const& area) { p.fillRect(area, sb::color::ERROR_SLOT, Painter::Layer::Foreground); }

void registerInventoryPart(std::string upgradeId, InventoryPartFactory factory) {
    if (!factory) return;
    parts().try_emplace(std::move(upgradeId), std::move(factory));
}

bool hasInventoryPart(std::string const& upgradeId) { return parts().contains(upgradeId); }

std::unique_ptr<InventoryPart> createInventoryPart(std::string const& upgradeId) {
    auto it = parts().find(upgradeId);
    return it == parts().end() ? nullptr : it->second();
}

} // namespace abp::client::ui

#endif // ABP_CLIENT
