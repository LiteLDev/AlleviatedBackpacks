#pragma once

#include <algorithm>

namespace abp::client::ui {

/// Axis-aligned rectangle in GUI units (the game's scaled UI coordinate space).
struct Rect {
    float x = 0, y = 0, w = 0, h = 0;

    [[nodiscard]] float right() const noexcept { return x + w; }
    [[nodiscard]] float bottom() const noexcept { return y + h; }
    [[nodiscard]] bool  contains(float px, float py) const noexcept { return px >= x && py >= y && px < x + w && py < y + h; }
    [[nodiscard]] bool  empty() const noexcept { return !(w > 0) || !(h > 0); }
    [[nodiscard]] Rect  offset(float dx, float dy) const noexcept { return {x + dx, y + dy, w, h}; }
    [[nodiscard]] Rect  inset(float d) const noexcept {
        float nw = std::max(0.f, w - 2 * d);
        float nh = std::max(0.f, h - 2 * d);
        return {x + (w - nw) / 2, y + (h - nh) / 2, nw, nh};
    }

    friend bool operator==(Rect const&, Rect const&) = default;
};

} // namespace abp::client::ui
