#include "core/render/BackpackModel.h"

#include <algorithm>
#include <cmath>

namespace abp::core::render {

namespace {
nlohmann::json tankJson(TankModel const& t) {
    return nlohmann::json{{"fluid", t.fluid}, {"ratio", t.ratio}, {"color", t.color}, {"lighter", t.lighter}};
}

std::optional<TankModel> tankFrom(nlohmann::json const& j, char const* key) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_object()) return std::nullopt;
    TankModel t;
    t.fluid   = it->value("fluid", std::string{});
    t.ratio   = std::clamp(it->value("ratio", 0.0f), 0.0f, 1.0f);
    t.color   = it->value("color", 0xFFFFFFFFu);
    t.lighter = it->value("lighter", false);
    return t;
}
} // namespace

nlohmann::json BackpackModel::toJson() const {
    nlohmann::json j = nlohmann::json::object();
    if (left) j["left"] = tankJson(*left);
    if (right) j["right"] = tankJson(*right);
    if (battery) j["battery"] = *battery;
    if (display) {
        j["display"] = {
            {"item",     display->item.toJson()},
            {"rotation", display->rotation     },
            {"zOffset",  display->zOffset      },
            {"side",     display->side         },
        };
    }
    if (!activity.empty()) j["activity"] = activity;
    return j;
}

BackpackModel BackpackModel::fromJson(nlohmann::json const& j) {
    BackpackModel m;
    if (!j.is_object()) return m;
    m.left  = tankFrom(j, "left");
    m.right = tankFrom(j, "right");
    if (auto it = j.find("battery"); it != j.end() && it->is_number()) m.battery = std::clamp(it->get<float>(), 0.0f, 1.0f);
    if (auto it = j.find("display"); it != j.end() && it->is_object() && it->contains("item")) {
        DisplayModel d;
        d.item     = ItemStackData::fromJson((*it)["item"]);
        d.rotation = it->value("rotation", 0);
        d.zOffset  = std::clamp(it->value("zOffset", 0), -16, 16);
        d.side     = it->value("side", std::string("front"));
        if (d.side != "left" && d.side != "right") d.side = "front";
        if (!d.item.empty()) m.display = std::move(d);
    }
    if (auto it = j.find("activity"); it != j.end() && it->is_array()) {
        for (auto const& a : *it) {
            if (a.is_string()) m.activity.push_back(a.get<std::string>());
        }
    }
    return m;
}

std::vector<TankSide> tankSides(std::vector<int> const& tankSlots, int upgradeSlots) {
    std::vector<TankSide> out;
    int  minRightSlot = upgradeSlots / 2;
    bool singleRight  = false;
    for (int slot : tankSlots) {
        if (slot >= minRightSlot) singleRight = true;
    }
    TankSide current = tankSlots.size() == 1 && singleRight ? TankSide::Right : TankSide::Left;
    for (std::size_t i = 0; i < tankSlots.size(); ++i) {
        out.push_back(current);
        current = TankSide::Right;
    }
    return out;
}

int ratioToStep(float ratio, int steps) {
    if (steps <= 0) return 0;
    ratio    = std::clamp(ratio, 0.0f, 1.0f);
    int step = static_cast<int>(std::floor(ratio * static_cast<float>(steps) + 1e-6f));
    return std::clamp(step, 0, steps);
}

FluidLook fluidLook(std::string_view fluid) {
    if (fluid == "lava" || fluid == "minecraft:lava") return FluidLook::Lava;
    if (fluid == "milk" || fluid == "minecraft:milk") return FluidLook::Milk;
    if (fluid == "powder_snow" || fluid == "minecraft:powder_snow") return FluidLook::PowderSnow;
    if (fluid == "honey" || fluid == "minecraft:honey") return FluidLook::Honey;
    if (fluid == "experience" || fluid == "abp:experience") return FluidLook::Experience;
    if (fluid.find("potion") != std::string_view::npos) return FluidLook::Potion;
    return FluidLook::Water;
}

float displayDepth(int zOffset) { return kDisplayDepthOffset - static_cast<float>(zOffset) * kDisplayScale / kDisplayPixelDivisor; }

} // namespace abp::core::render
