#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "client/ui/Geometry.h"

class MinecraftUIRenderContext;
class ItemStack;

namespace abp::client::ui {

/// RGBA color, components in [0,1].
struct Color {
    float r = 1, g = 1, b = 1, a = 1;

    static constexpr Color rgb(std::uint32_t hex, float alpha = 1.f) {
        return {((hex >> 16) & 0xFF) / 255.f, ((hex >> 8) & 0xFF) / 255.f, (hex & 0xFF) / 255.f, alpha};
    }
    /// Java-style 0xAARRGGBB.
    static constexpr Color argb(std::uint32_t hex) {
        return {((hex >> 16) & 0xFF) / 255.f, ((hex >> 8) & 0xFF) / 255.f, (hex & 0xFF) / 255.f, ((hex >> 24) & 0xFF) / 255.f};
    }
    [[nodiscard]] constexpr Color withAlpha(float alpha) const { return {r, g, b, alpha}; }
};

enum class TextAlign { Left, Center, Right };

/// Pixel art drawn with rectangles: `rows` of equal length, one character per pixel; '.' or ' '
/// is transparent, other characters index `palette` ("X=000000" style entries: char, '=', hex).
struct PixelArt {
    std::vector<std::string_view> rows;
    std::vector<std::string_view> palette;

    [[nodiscard]] int width() const { return rows.empty() ? 0 : static_cast<int>(rows.front().size()); }
    [[nodiscard]] int height() const { return static_cast<int>(rows.size()); }
};

/// Thin batching wrapper over MinecraftUIRenderContext (ported from BEI's Painter).
///
/// The game's UI renderer needs a strict order: rectangles -> flushImages, item icons,
/// text -> flushText. Callers may record in any order; flush() replays commands layer by
/// layer (Background, Items, Foreground, Text). Call flush() once per "z band": whatever is
/// recorded after a flush() appears above everything before it.
///
/// Coordinates are GUI units. Only valid inside a UI render callback; never keep a Painter
/// across frames. Item stacks passed in must stay alive until the next flush().
class Painter {
public:
    enum class Layer { Background, Items, Foreground, Text };

    explicit Painter(MinecraftUIRenderContext& ctx);
    ~Painter(); // flushes
    Painter(Painter const&)            = delete;
    Painter& operator=(Painter const&) = delete;

    void fillRect(Rect const& r, Color c, Layer layer = Layer::Background);
    /// Vertical gradient (Java `fillGradient`), drawn as 1-unit strips.
    void gradientRect(Rect const& r, Color top, Color bottom, Layer layer = Layer::Background);
    void outlineRect(Rect const& r, Color c, float thickness = 1, Layer layer = Layer::Background);
    void pixels(PixelArt const& art, float x, float y, float scale = 1, Layer layer = Layer::Background, float alpha = 1);

    /// Item icon (16x16 * scale) with its top-left at (x,y), plus the durability bar.
    void item(ItemStack const& stack, float x, float y, float scale = 1, float alpha = 1);

    void text(
        std::string const& s,
        float              x,
        float              y,
        Color              c      = Color::rgb(0x404040),
        bool               shadow = false,
        float              scale  = 1,
        TextAlign          align  = TextAlign::Left,
        float              width  = 0
    );
    [[nodiscard]] float textWidth(std::string const& s, float scale = 1) const;
    [[nodiscard]] float lineHeight(float scale = 1) const { return 9 * scale; }

    /// Textured quad from a resource-pack path such as "textures/ui/arrow", multiplied by `tint`.
    void texture(std::string const& path, Rect const& r, float alpha = 1, Layer layer = Layer::Foreground, Color tint = Color{});
    /// Part of a texture: `uv` is the source rectangle in texture fractions (0..1), e.g. one frame
    /// of a flipbook strip.
    void textureUV(std::string const& path, Rect const& r, Rect const& uv, float alpha = 1, Layer layer = Layer::Foreground, Color tint = Color{});

    /// Replay all recorded commands in layer order.
    void flush();

    /// Screen size in GUI units.
    [[nodiscard]] Rect                      screen() const;
    [[nodiscard]] MinecraftUIRenderContext& raw() const { return mCtx; }

private:
    struct Cmd;
    MinecraftUIRenderContext& mCtx;
    std::vector<Cmd>*         mCmds;
};

} // namespace abp::client::ui
