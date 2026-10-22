#ifdef ABP_CLIENT

#include "upgrade/smithing/ArmorStandPreview.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

#include "mc/world/item/ItemStack.h"

#include "client/ui/gfx/Painter.h"

namespace abp::upgrade::smithing::client {

namespace {

using ::abp::client::ui::Color;
using ::abp::client::ui::Painter;

struct V3 {
    float x, y, z;
};

/// Model boxes of an armor stand with arms and without base plate, in model pixels (x to the
/// stand's left, y down from the neck joint, z forward); the feet are at y = 24.
struct Box {
    float x, y, z, w, h, d;
};
constexpr std::array<Box, 9> kBoxes{{
    {-1, -7, -1, 2, 7, 2},      // neck stick (head)
    {-6, 0, -1.5f, 12, 3, 3},   // shoulder plank
    {-3, 3, -1, 2, 7, 2},       // right body stick
    {1, 3, -1, 2, 7, 2},        // left body stick
    {-4, 10, -1, 8, 2, 2},      // waist
    {-7, 0, -1, 2, 12, 2},      // right arm
    {5, 0, -1, 2, 12, 2},       // left arm
    {-2.9f, 12, -1, 2, 11, 2},  // right leg
    {0.9f, 12, -1, 2, 11, 2},   // left leg
}};

/// SB: body yaw 210 degrees (the stand faces the viewer, turned 30 degrees) and a 25 degree view
/// from above (SmithingScreen.ARMOR_STAND_ANGLE).
constexpr float kYaw   = 30.f * 3.14159265f / 180.f;
constexpr float kPitch = 25.f * 3.14159265f / 180.f;

/// Model pixel -> view space (x right, y up, z towards the viewer), relative to the feet.
V3 view(V3 m) {
    float X = m.x, Y = 24 - m.y, Z = m.z;
    float x1 = X * std::cos(kYaw) + Z * std::sin(kYaw);
    float z1 = -X * std::sin(kYaw) + Z * std::cos(kYaw);
    float y2 = Y * std::cos(kPitch) - z1 * std::sin(kPitch);
    float z2 = Y * std::sin(kPitch) + z1 * std::cos(kPitch);
    return {x1, y2, z2};
}

V3 viewDir(V3 n) {
    float x1 = n.x * std::cos(kYaw) + n.z * std::sin(kYaw);
    float z1 = -n.x * std::sin(kYaw) + n.z * std::cos(kYaw);
    float y2 = n.y * std::cos(kPitch) - z1 * std::sin(kPitch);
    float z2 = n.y * std::sin(kPitch) + z1 * std::cos(kPitch);
    return {x1, y2, z2};
}

struct Face {
    std::array<V3, 4> pts; ///< view space
    float             depth;
    float             light;
    bool              top;
};

/// Fills a convex polygon given in screen units, one half-unit row at a time.
void fillConvex(Painter& p, std::array<std::pair<float, float>, 4> const& pts, Color c) {
    float minY = pts[0].second, maxY = pts[0].second;
    for (auto const& [x, y] : pts) {
        minY = std::min(minY, y);
        maxY = std::max(maxY, y);
    }
    constexpr float kStep = 0.5f;
    for (float row = std::floor(minY / kStep) * kStep; row < maxY; row += kStep) {
        float yc = row + kStep / 2;
        float lo = 1e9f, hi = -1e9f;
        for (std::size_t i = 0; i < pts.size(); ++i) {
            auto [x1, y1] = pts[i];
            auto [x2, y2] = pts[(i + 1) % pts.size()];
            if ((yc < y1 && yc < y2) || (yc > y1 && yc > y2) || y1 == y2) continue;
            float x = x1 + (yc - y1) * (x2 - x1) / (y2 - y1);
            lo      = std::min(lo, x);
            hi      = std::max(hi, x);
        }
        if (hi > lo) p.fillRect({lo, row, hi - lo, kStep}, c);
    }
}

} // namespace

StandSlot standSlotFor(ItemStack const& stack) {
    auto id = stack.isNull() ? std::string{} : stack.getTypeName();
    auto ends = [&](std::string_view s) { return id.size() >= s.size() && id.compare(id.size() - s.size(), s.size(), s) == 0; };
    if (ends("_helmet") || ends("_skull") || ends("_head") || id == "minecraft:carved_pumpkin") return StandSlot::Head;
    if (ends("_chestplate") || id == "minecraft:elytra") return StandSlot::Chest;
    if (ends("_leggings")) return StandSlot::Legs;
    if (ends("_boots")) return StandSlot::Feet;
    return StandSlot::Offhand;
}

void drawArmorStand(Painter& p, float feetX, float feetY, float scale, ItemStack const* item) {
    float const px = scale / 16.f; // GUI units per model pixel
    V3 const    light = [] {
        V3    l{-0.35f, 0.8f, 0.6f};
        float n = std::sqrt(l.x * l.x + l.y * l.y + l.z * l.z);
        return V3{l.x / n, l.y / n, l.z / n};
    }();

    std::vector<Face> faces;
    for (auto const& b : kBoxes) {
        float x0 = b.x, x1 = b.x + b.w, y0 = b.y, y1 = b.y + b.h, z0 = b.z, z1 = b.z + b.d;
        struct Def {
            V3                n;
            std::array<V3, 4> c;
            bool              top;
        };
        std::array<Def, 6> defs{{
            {{0, 0, 1}, {{{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}}}, false},  // front
            {{0, 0, -1}, {{{x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}}}, false}, // back
            {{1, 0, 0}, {{{x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}}}, false},  // left side (+x)
            {{-1, 0, 0}, {{{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}}}, false}, // right side
            {{0, 1, 0}, {{{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}}}, true},   // top (y up)
            {{0, -1, 0}, {{{x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0}}}, false}, // bottom
        }};
        for (auto const& d : defs) {
            auto n = viewDir(d.n);
            if (n.z <= 0.01f) continue; // faces away from the viewer
            Face f;
            float depth = 0;
            for (std::size_t i = 0; i < 4; ++i) {
                f.pts[i]  = view(d.c[i]);
                depth    += f.pts[i].z;
            }
            f.depth = depth / 4;
            f.light = 0.5f + 0.5f * std::max(0.f, n.x * light.x + n.y * light.y + n.z * light.z);
            f.top   = d.top;
            faces.push_back(f);
        }
    }
    std::sort(faces.begin(), faces.end(), [](Face const& a, Face const& b) { return a.depth < b.depth; });

    // Oak wood of the armor stand texture (original palette).
    constexpr float kR = 0xB2 / 255.f, kG = 0x8C / 255.f, kB = 0x55 / 255.f;
    for (auto const& f : faces) {
        float l = f.light * (f.top ? 1.08f : 1.f);
        Color c{std::min(1.f, kR * l), std::min(1.f, kG * l), std::min(1.f, kB * l), 1.f};
        std::array<std::pair<float, float>, 4> s;
        for (std::size_t i = 0; i < 4; ++i) s[i] = {feetX + f.pts[i].x * px, feetY - f.pts[i].y * px};
        fillConvex(p, s, c);
    }

    if (!item || item->isNull()) return;
    V3    anchor;
    float size = 16;
    switch (standSlotFor(*item)) {
    case StandSlot::Head:
        anchor = {0, -6, 0};
        size   = 14;
        break;
    case StandSlot::Chest:
        anchor = {0, 4, 0};
        size   = 18;
        break;
    case StandSlot::Legs:
        anchor = {0, 14, 0};
        size   = 16;
        break;
    case StandSlot::Feet:
        anchor = {0, 21.5f, 0};
        size   = 14;
        break;
    case StandSlot::Offhand:
        anchor = {7, 10, 2};
        size   = 16;
        break;
    }
    auto  v = view(anchor);
    float k = size / 16.f * (scale / 25.f);
    p.item(*item, feetX + v.x * px - 8 * k, feetY - v.y * px - 8 * k, k);
}

} // namespace abp::upgrade::smithing::client

#endif // ABP_CLIENT
