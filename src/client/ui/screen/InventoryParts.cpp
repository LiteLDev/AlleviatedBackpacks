#ifdef ABP_CLIENT

#include "client/ui/screen/InventoryParts.h"

#include <algorithm>
#include <unordered_map>

namespace abp::client::ui {

namespace {
struct Entry {
    InventoryPartFactory           factory;
    std::unique_ptr<InventoryPart> instance; ///< parts are stateless: one shared instance
};

std::unordered_map<std::string, Entry>& registry() {
    static std::unordered_map<std::string, Entry> r;
    return r;
}

InventoryPart* partFor(std::string const& id) {
    auto& r  = registry();
    auto  it = r.find(id);
    if (it == r.end() || !it->second.factory) return nullptr;
    if (!it->second.instance) it->second.instance = it->second.factory();
    return it->second.instance.get();
}

/// Calls `fn(part, ctx)` for every upgrade with an inventory part, 36 units apart.
template <class Fn>
bool forEachPart(core::screen::BackpackScreenModel const& model, Rect parts, Fn&& fn) {
    if (inventoryColumnsTaken(model) <= 0 || parts.h <= 0) return false;
    float x = parts.x;
    for (std::size_t i = 0; i < model.upgrades.size(); ++i) {
        auto const& u = model.upgrades[i];
        if (u.id.empty() || u.tab.value("inventoryColumns", 0) <= 0) continue;
        auto* part = partFor(u.id);
        if (!part) continue;
        InventoryPartContext ctx{model, static_cast<int>(i), u, Rect{x, parts.y, 36, parts.h}};
        if (fn(*part, ctx)) return true;
        x += 36;
    }
    return false;
}
} // namespace

void registerInventoryPart(std::string upgradeId, InventoryPartFactory factory) {
    registry()[std::move(upgradeId)] = Entry{std::move(factory), nullptr};
}

int inventoryColumnsTaken(core::screen::BackpackScreenModel const& model) {
    int taken = 0;
    for (auto const& u : model.upgrades) {
        if (u.tab.is_object()) taken = std::max(taken, u.tab.value("columnsTaken", 0));
    }
    return taken;
}

void renderInventoryParts(Painter& p, core::screen::BackpackScreenModel const& model, Rect parts, float mx, float my) {
    forEachPart(model, parts, [&](InventoryPart& part, InventoryPartContext const& ctx) {
        part.render(p, ctx, mx, my);
        return false;
    });
}

bool clickInventoryParts(core::screen::BackpackScreenModel const& model, Rect parts, int button, float mx, float my) {
    return forEachPart(model, parts, [&](InventoryPart& part, InventoryPartContext const& ctx) { return part.click(ctx, button, mx, my); });
}

bool inventoryPartsTooltip(core::screen::BackpackScreenModel const& model, Rect parts, float mx, float my, std::vector<TooltipLine>& out) {
    return forEachPart(model, parts, [&](InventoryPart& part, InventoryPartContext const& ctx) { return part.tooltip(ctx, mx, my, out); });
}

void inventoryPartsErrorOverlay(Painter& p, core::screen::BackpackScreenModel const& model, Rect parts) {
    forEachPart(model, parts, [&](InventoryPart& part, InventoryPartContext const& ctx) {
        if (std::find(model.errorUpgradeSlots.begin(), model.errorUpgradeSlots.end(), ctx.slot) != model.errorUpgradeSlots.end()) {
            part.errorOverlay(p, ctx);
        }
        return false;
    });
}

} // namespace abp::client::ui

#endif // ABP_CLIENT
