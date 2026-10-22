#pragma once

// e2e calibration of the backpack model placements (P2-11): `/backpack modeltest pose "<ctx> px py pz
// rx ry rz scale"` overrides one context's transform (local worlds: the command and the client
// share the process), read by `query.abp_model(30 + ctx * 8 + k)` (k 0 = on, 1..7 = values).
// Contexts: 1 worn, 2 held third person, 3 held first person, 4 placed, 5 display item.

#include <array>
#include <atomic>

namespace abp::model {

struct DebugPose {
    std::atomic<bool>                 on{false};
    std::array<std::atomic<float>, 7> v{};
};

inline DebugPose& debugPose(int ctx) {
    static std::array<DebugPose, 6> poses;
    return poses[static_cast<std::size_t>(ctx < 0 || ctx > 5 ? 0 : ctx)];
}

} // namespace abp::model