#ifdef ABP_CLIENT

#include "client/render/RenderHooks.h"

#include <algorithm>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"

#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/item/ItemStack.h"

#include "client/Client.h"
#include "client/net/ClientNet.h"
#include "client/ui/gfx/Painter.h"
#include "core/Tier.h"
#include "game/BackpackItem.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace abp::client::render {

namespace {
struct IconEntry {
    std::string id;
    int         order;
    IconHook    hook;
};
std::vector<IconEntry>&                         icons() { static std::vector<IconEntry> v; return v; }
std::vector<std::pair<std::string, CarrierHook>>& carriers() { static std::vector<std::pair<std::string, CarrierHook>> v; return v; }
std::vector<std::pair<std::string, PlacedHook>>&  placed() { static std::vector<std::pair<std::string, PlacedHook>> v; return v; }

} // namespace

core::screen::RenderInfo const* renderInfoFor(ItemStack const& stack) {
    if (stack.isNull() || !game::isBackpack(stack)) return nullptr;
    auto uuid = game::backpackUuid(stack);
    if (!uuid) return nullptr;
    return net::renderInfo(*uuid);
}

void addIconHook(std::string id, int order, IconHook hook) {
    removeHook(id);
    icons().push_back({std::move(id), order, std::move(hook)});
    std::stable_sort(icons().begin(), icons().end(), [](auto const& a, auto const& b) { return a.order < b.order; });
}

void addCarrierHook(std::string id, CarrierHook hook) {
    removeHook(id);
    carriers().emplace_back(std::move(id), std::move(hook));
}

void addPlacedHook(std::string id, PlacedHook hook) {
    removeHook(id);
    placed().emplace_back(std::move(id), std::move(hook));
}

void removeHook(std::string const& id) {
    std::erase_if(icons(), [&](auto const& e) { return e.id == id; });
    std::erase_if(carriers(), [&](auto const& e) { return e.first == id; });
    std::erase_if(placed(), [&](auto const& e) { return e.first == id; });
}

void decorateItemIcon(ui::Painter& painter, ItemStack const& stack, float x, float y, float scale) {
    if (icons().empty()) return;
    auto const* info = renderInfoFor(stack);
    if (!info) return;
    IconContext ctx{painter, stack, x, y, scale, *info};
    for (auto const& e : icons()) {
        try {
            if (e.hook && e.hook(ctx)) break;
        } catch (std::exception const& ex) {
            log().error("icon render hook {} failed: {}", e.id, ex.what());
        }
    }
}

void dispatchPlaced(PlacedContext const& ctx) {
    for (auto const& [id, hook] : placed()) {
        try {
            if (hook) hook(ctx);
        } catch (std::exception const& ex) {
            log().error("placed render hook {} failed: {}", id, ex.what());
        }
    }
}

/// Dispatches carrier hooks every client tick for the local player's worn / held backpacks.
class RenderHookModule {
public:
    void install() {
        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientLevelTickEvent>([](auto&) {
            if (carriers().empty()) return;
            auto* player = currentPlayer();
            if (!player) return;
            auto run = [&](ItemStack const& stack, bool worn) {
                auto const* info = renderInfoFor(stack);
                if (!info) return;
                CarrierContext ctx{*player, stack, *info, worn};
                for (auto const& [id, hook] : carriers()) {
                    try {
                        if (hook) hook(ctx);
                    } catch (std::exception const& ex) {
                        log().error("carrier render hook {} failed: {}", id, ex.what());
                    }
                }
            };
            run(player->getArmor(SharedTypes::Legacy::ArmorSlot::Torso), true);
            run(player->getSelectedItem(), false);
        });
    }
    void uninstall() {
        if (mTick) ll::event::EventBus::getInstance().removeListener(mTick);
        mTick.reset();
    }

private:
    ll::event::ListenerPtr mTick;
};

} // namespace abp::client::render

ABP_REGISTER_MODULE(abp::client::render::RenderHookModule);

#endif // ABP_CLIENT
