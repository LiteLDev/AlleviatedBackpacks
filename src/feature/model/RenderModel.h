#pragma once

// P2-11: server side of the backpack model render state (RenderInfo::extra["model"]).

#include <cstdint>
#include <string_view>

#include "core/render/BackpackModel.h"

namespace abp::game {
class Backpack;
}

namespace abp::model {

/// Tank parts, battery charge, item display and upgrade activity of `backpack`.
[[nodiscard]] core::render::BackpackModel buildModel(game::Backpack& backpack);

/// e2e: forced model parts from `settings["abp_model_test"]` (ModelCommands.cpp).
void applyTestOverrides(game::Backpack const& backpack, core::render::BackpackModel& model);

/// ARGB tint of a fluid (core fluid ids; SB uses the fluid type's tint colour).
[[nodiscard]] std::uint32_t fluidColor(std::string_view fluid);

} // namespace abp::model
