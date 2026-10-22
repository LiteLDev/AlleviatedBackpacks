// P2-11: server side of the backpack model. Fills `RenderInfo::extra["model"]` (see
// core/render/BackpackModel.h) with what SB's RenderInfo carries for rendering: tank parts (side,
// fluid, fill ratio, tint), the battery part (charge ratio), the item display and upgrade
// activity (cooking / jukebox particles).

#include "feature/model/RenderModel.h"

#include <algorithm>
#include <cmath>

#include "api/abp/Capabilities.h"
#include "core/Fluid.h"
#include "core/render/BackpackModel.h"
#include "game/Backpack.h"
#include "net/NativeScreens.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/cooking/Cooking.h"
#include "upgrade/jukebox/Jukebox.h"
#include "upgrade/tank/Tank.h"

namespace abp::model {

namespace {

// Bedrock potion colours by aux value (effect colours), used for the fluid tint.
std::uint32_t potionColor(int aux) {
    switch (aux) {
    case 5: case 6: return 0xFF1F1FA1;              // night vision
    case 7: case 8: return 0xFF7F8392;              // invisibility
    case 9: case 10: case 11: return 0xFF22FF4C;    // leaping
    case 12: case 13: return 0xFFE49A3A;            // fire resistance
    case 14: case 15: case 16: return 0xFF7CAFC6;   // swiftness
    case 17: case 18: case 42: return 0xFF5A6C81;   // slowness
    case 19: case 20: return 0xFF2E5299;            // water breathing
    case 21: case 22: return 0xFFF82423;            // healing
    case 23: case 24: return 0xFF430A09;            // harming
    case 25: case 26: case 27: return 0xFF4E9331;   // poison
    case 28: case 29: case 30: return 0xFFCD5CAB;   // regeneration
    case 31: case 32: case 33: return 0xFF932423;   // strength
    case 34: case 35: return 0xFF484D48;            // weakness
    case 36: return 0xFF352A27;                     // decay
    case 37: case 38: case 39: return 0xFF755B78;   // turtle master
    case 40: case 41: return 0xFFFFEFD1;            // slow falling
    default: return 0xFF385DC6;                     // water bottle / mundane / awkward / thick
    }
}

} // namespace

std::uint32_t fluidColor(std::string_view fluid) {
    using namespace core::fluid;
    if (fluid == kWater) return 0xFF3F76E4;
    if (fluid == kLava) return 0xFFFF7A1E;
    if (fluid == kMilk) return 0xFFF4F4F4;
    if (fluid == kPowderSnow) return 0xFFF0F8FF;
    if (fluid == kHoney) return 0xFFFFB838;
    if (fluid == kExperience) return 0xFF8CE13A;
    if (family(fluid) != fluid) return potionColor(potionAux(fluid));
    return 0xFFFFFFFF;
}

core::render::BackpackModel buildModel(game::Backpack& backpack) {
    core::render::BackpackModel m;
    auto bp = backpack.shared_from_this();

    // Tanks: SC UpgradeHandler.initTankRenderInfoCallbacks.
    auto tanks = upgrade::tank::tanks(bp);
    std::vector<int> slots;
    for (auto const& t : tanks) slots.push_back(t.slot());
    auto sides = core::render::tankSides(slots, static_cast<int>(backpack.data().upgrades.size()));
    for (std::size_t i = 0; i < tanks.size(); ++i) {
        auto tank = upgrade::tank::read(tanks[i]);
        core::render::TankModel t;
        if (!tank.empty()) {
            t.fluid = tank.fluid();
            t.ratio = tank.capacity() > 0 ? std::clamp(static_cast<float>(tank.amount()) / static_cast<float>(tank.capacity()), 0.0f, 1.0f) : 0.0f;
            t.color = fluidColor(tank.fluid());
        }
        (sides[i] == core::render::TankSide::Left ? m.left : m.right) = t;
    }

    // Battery: any installed "battery" upgrade; charge from the energy capability (G3).
    bool battery = false;
    for (auto const& s : backpack.data().upgrades) {
        if (s.empty()) continue;
        if (auto* type = upgrade::upgradeFor(s.item); type && type->info().groupOrId() == "battery") battery = true;
    }
    if (battery) {
        float ratio = 0;
        if (auto caps = api::capabilities()) {
            if (auto energy = caps->backpackEnergy(backpack.uuid()); energy && energy->capacity() > 0) {
                ratio = std::clamp(static_cast<float>(energy->stored()) / static_cast<float>(energy->capacity()), 0.0f, 1.0f);
            }
        }
        m.battery = ratio;
    }

    // Item display (SC ItemDisplaySettingsCategory "item_display": first selected slot).
    if (auto it = backpack.data().settings.find("item_display"); it != backpack.data().settings.end() && it->is_object()) {
        auto const& s    = *it;
        int         slot = -1;
        if (auto sl = s.find("slots"); sl != s.end() && sl->is_array() && !sl->empty() && (*sl)[0].is_number_integer()) slot = (*sl)[0].get<int>();
        else if (auto one = s.find("slot"); one != s.end() && one->is_number_integer()) slot = one->get<int>();
        auto const& inv = backpack.data().inventory;
        if (slot >= 0 && slot < inv.size() && !inv.at(slot).empty()) {
            core::render::DisplayModel d;
            d.item = inv.at(slot).withCount(1);
            auto key = std::to_string(slot);
            auto num = [&](char const* many, char const* one) {
                if (auto r = s.find(many); r != s.end() && r->is_object() && r->contains(key) && (*r)[key].is_number_integer()) return (*r)[key].get<int>();
                if (auto r = s.find(one); r != s.end() && r->is_number_integer()) return r->get<int>();
                return 0;
            };
            d.rotation = num("rotations", "rotation");
            d.zOffset  = std::clamp(num("zOffsets", "zOffset"), -16, 16);
            d.side     = s.value("displaySide", std::string("front"));
            if (d.side != "left" && d.side != "right") d.side = "front";
            m.display = std::move(d);
        }
    }

    // Upgrade activity (SC upgrade render data): cooking upgrades burning, jukebox playing.
    upgrade::forEachInstalled(bp, true, [&](upgrade::IUpgrade& u, upgrade::UpgradeContext const& ctx) {
        auto const& id = u.info().id;
        if (id.find("smelting") != std::string::npos || id.find("smoking") != std::string::npos || id.find("blasting") != std::string::npos) {
            if (upgrade::cooking::CookingUpgrade::state(ctx).burning() && std::find(m.activity.begin(), m.activity.end(), "cooking") == m.activity.end()) {
                m.activity.emplace_back("cooking");
            }
        } else if (u.info().groupOrId() == "jukebox") {
            if (upgrade::jukebox::Service::get().playing(backpack.uuid(), ctx.slot())
                && std::find(m.activity.begin(), m.activity.end(), "jukebox") == m.activity.end()) {
                m.activity.emplace_back("jukebox");
            }
        }
    });
    return m;
}

} // namespace abp::model

ABP_REGISTER_RENDER_INFO([](abp::game::Backpack& backpack, abp::core::screen::RenderInfo& info) {
    info.extra["model"] = abp::model::buildModel(backpack).toJson();
});
