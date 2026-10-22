#ifdef ABP_CLIENT

#include "client/ui/gfx/Painter.h"

#include "mc/client/game/IClientInstance.h"
#include "mc/client/game/IMinecraftGame.h"
#include "mc/client/gui/CaretMeasureData.h"
#include "mc/client/gui/Font.h"
#include "mc/client/gui/FontHandle.h"
#include "mc/client/gui/GuiData.h"
#include "mc/client/gui/TextAlignment.h"
#include "mc/client/gui/TextMeasureData.h"
#include "mc/client/gui/controls/UIMaterialType.h"
#include "mc/client/gui/controls/renderers/InventoryItemRenderer.h"
#include "mc/client/gui/screens/BatchClippingState.h"
#include "mc/client/gui/screens/BatchKey.h"
#include "mc/client/gui/screens/ComponentRenderBatch.h"
#include "mc/client/gui/screens/UIBatchType.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/actor/ItemRenderChunkType.h"
#include "mc/client/renderer/actor/ItemRenderer.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/deps/core/file/PathView.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/input/RectangleArea.h"
#include "mc/deps/minecraft_renderer/renderer/BedrockTextureData.h"
#include "mc/deps/minecraft_renderer/renderer/TexturePtr.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include <unordered_map>
#include <variant>

namespace abp::client::ui {

namespace {
struct RectCmd {
    Rect  r;
    Color c;
};
struct ItemCmd {
    ItemStack const* stack;
    float            x, y, scale, alpha;
};
struct TextCmd {
    std::string s;
    float       x, y;
    Color       c;
    bool        shadow;
    float       scale;
    TextAlign   align;
    float       width;
};
struct TextureCmd {
    std::string path;
    Rect        r;
    float       alpha = 1;
    Color       tint;
};

mce::Color toMce(Color c) { return mce::Color{c.r, c.g, c.b, c.a}; }

RectangleArea toArea(Rect const& r) { return RectangleArea{r.x, r.right(), r.y, r.bottom()}; }

Font& uiFont(MinecraftUIRenderContext& ctx) { return ctx.mClient.getFontHandle().getFont(); }

HashedString const& fillMaterial() {
    static HashedString const m{"ui_fillColor"};
    return m;
}
HashedString const& imageMaterial() {
    static HashedString const m{"ui_flush"};
    return m;
}

// Render chunk types the vanilla slot renderer batches: block models on the terrain atlas and flat
// sprites on the item atlas. Anything else (and glinting stacks) goes through renderGuiItemNew.
constexpr int CHUNK_BLOCK = 0;
constexpr int CHUNK_FLAT  = 2;

/// A ComponentRenderBatch set up like the one a vanilla inventory slot uses for its icons.
class ItemMeshBatch {
public:
    ItemMeshBatch(int chunk, int depth, float alpha) {
        std::memset(mStorage, 0, sizeof(mStorage));
        auto& batch = get();
        alignas(BatchClippingState) std::byte clip[sizeof(BatchClippingState)]{};
        auto* key = ::new (std::addressof(batch.mBatchKey.get()))
            BatchKey(depth, alpha, *std::launder(reinterpret_cast<BatchClippingState*>(clip)));
        key->mBatchType      = UIBatchType::SharedMesh;
        key->mUIMaterialType = UIMaterialType::Item;
        auto& textures       = key->mResourceLocations.get();
        setLocation(textures[0], chunk == CHUNK_BLOCK ? "atlas.terrain" : "atlas.items");
        batch.mIsDirty                = true;
        batch.mRequiresPreRenderSetup = false;
        batch.mRenderPass             = 0;
        ::new (std::addressof(batch.mCustomRenderInstances.get())) std::vector<CustomRenderComponent*>();
        ::new (std::addressof(batch.mSpriteInstances.get())) std::vector<SpriteComponent*>();
        ::new (std::addressof(batch.mTextInstances.get())) std::vector<TextComponent*>();
    }
    ~ItemMeshBatch() { std::destroy_at(&get()); }
    ItemMeshBatch(ItemMeshBatch const&)            = delete;
    ItemMeshBatch& operator=(ItemMeshBatch const&) = delete;

    ComponentRenderBatch& get() { return *std::launder(reinterpret_cast<ComponentRenderBatch*>(mStorage)); }

private:
    static void setLocation(ResourceLocation& loc, char const* path) {
        std::destroy_at(&loc);
        ::new (&loc) ResourceLocation(Core::PathView(path));
    }
    alignas(ComponentRenderBatch) std::byte mStorage[sizeof(ComponentRenderBatch)];
};

float durabilityRatio(ItemStack const& stack) {
    if (!stack.mItem || !stack.isDamageableItem() || !stack.isDamaged()) return -1;
    short maxDamage = stack.mItem->getMaxDamage();
    if (maxDamage <= 0) return -1;
    float remaining = static_cast<float>(maxDamage - stack.getDamageValue());
    return std::clamp(remaining / static_cast<float>(maxDamage), 0.f, 1.f);
}

std::uint32_t paletteColor(PixelArt const& art, char ch, bool& found) {
    for (auto entry : art.palette) {
        if (entry.size() >= 3 && entry[0] == ch && entry[1] == '=') {
            found = true;
            return static_cast<std::uint32_t>(std::stoul(std::string(entry.substr(2)), nullptr, 16));
        }
    }
    found = false;
    return 0;
}
} // namespace

struct Painter::Cmd {
    Layer                                               layer;
    std::variant<RectCmd, ItemCmd, TextCmd, TextureCmd> v;
};

Painter::Painter(MinecraftUIRenderContext& ctx) : mCtx(ctx), mCmds(new std::vector<Cmd>) {}

Painter::~Painter() {
    try {
        flush();
    } catch (...) {
    }
    delete mCmds;
}

void Painter::fillRect(Rect const& r, Color c, Layer layer) {
    if (r.empty() || c.a <= 0) return;
    mCmds->push_back({layer, RectCmd{r, c}});
}

void Painter::gradientRect(Rect const& r, Color top, Color bottom, Layer layer) {
    if (r.empty()) return;
    int steps = std::max(1, static_cast<int>(r.h));
    for (int i = 0; i < steps; ++i) {
        float t  = steps == 1 ? 0.f : static_cast<float>(i) / static_cast<float>(steps - 1);
        Color c{top.r + (bottom.r - top.r) * t, top.g + (bottom.g - top.g) * t, top.b + (bottom.b - top.b) * t, top.a + (bottom.a - top.a) * t};
        float y0 = r.y + r.h * static_cast<float>(i) / static_cast<float>(steps);
        float y1 = r.y + r.h * static_cast<float>(i + 1) / static_cast<float>(steps);
        fillRect({r.x, y0, r.w, y1 - y0}, c, layer);
    }
}

void Painter::outlineRect(Rect const& r, Color c, float t, Layer layer) {
    fillRect({r.x, r.y, r.w, t}, c, layer);
    fillRect({r.x, r.bottom() - t, r.w, t}, c, layer);
    fillRect({r.x, r.y + t, t, r.h - 2 * t}, c, layer);
    fillRect({r.right() - t, r.y + t, t, r.h - 2 * t}, c, layer);
}

void Painter::pixels(PixelArt const& art, float x, float y, float scale, Layer layer, float alpha) {
    for (int row = 0; row < art.height(); ++row) {
        auto line = art.rows[static_cast<std::size_t>(row)];
        // Merge horizontal runs of one colour into one rectangle.
        for (int col = 0; col < static_cast<int>(line.size());) {
            char ch = line[static_cast<std::size_t>(col)];
            int  end = col + 1;
            while (end < static_cast<int>(line.size()) && line[static_cast<std::size_t>(end)] == ch) ++end;
            if (ch != '.' && ch != ' ') {
                bool found = false;
                auto rgb   = paletteColor(art, ch, found);
                if (found) {
                    fillRect(
                        {x + static_cast<float>(col) * scale, y + static_cast<float>(row) * scale, static_cast<float>(end - col) * scale, scale},
                        Color::rgb(rgb, alpha),
                        layer
                    );
                }
            }
            col = end;
        }
    }
}

void Painter::item(ItemStack const& stack, float x, float y, float scale, float alpha) {
    if (stack.isNull() || !stack.mItem) return;
    mCmds->push_back({Layer::Items, ItemCmd{&stack, x, y, scale, alpha}});
}

void Painter::text(std::string const& s, float x, float y, Color c, bool shadow, float scale, TextAlign align, float width) {
    if (s.empty()) return;
    mCmds->push_back({Layer::Text, TextCmd{s, x, y, c, shadow, scale, align, width}});
}

float Painter::textWidth(std::string const& s, float scale) const {
    return static_cast<float>(mCtx.getLineLength(uiFont(mCtx), s, scale, false));
}

void Painter::texture(std::string const& path, Rect const& r, float alpha, Layer layer, Color tint) {
    mCmds->push_back({layer, TextureCmd{path, r, alpha, tint}});
}

Rect Painter::screen() const {
    auto gui = mCtx.mClient.getGuiData();
    auto sz  = gui->mScreenSizeData->clientUIScreenSize;
    return {0, 0, sz->x, sz->y};
}

void Painter::flush() {
    if (mCmds->empty()) return;
    mce::Color const white{1.f, 1.f, 1.f, 1.f};

    std::unordered_map<std::string, mce::TexturePtr> textures;
    auto clientTexture = [&](std::string const& path) -> BedrockTextureData const* {
        auto it = textures.find(path);
        if (it == textures.end()) {
            it = textures.emplace(path, mCtx.getTexture(ResourceLocation(Core::PathView(path)), false)).first;
        }
        return it->second.mClientTexture.get();
    };

    auto quads = [&](Layer layer) {
        enum class Run { None, Fill, Image };
        Run   run      = Run::None;
        float runAlpha = 1;
        Color runTint;
        auto  endRun   = [&] {
            if (run == Run::Fill) mCtx.flushImages(white, 1.f, fillMaterial());
            if (run == Run::Image) mCtx.flushImages(toMce(runTint), runAlpha, imageMaterial());
            run = Run::None;
        };
        auto sameTint = [](Color a, Color b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; };
        for (auto& cmd : *mCmds) {
            if (cmd.layer != layer) continue;
            if (auto* rc = std::get_if<RectCmd>(&cmd.v)) {
                if (run != Run::Fill) endRun(), run = Run::Fill;
                mCtx.fillRectangle(toArea(rc->r), toMce(rc->c), rc->c.a);
                continue;
            }
            auto* tc = std::get_if<TextureCmd>(&cmd.v);
            if (!tc) continue;
            auto const* data = clientTexture(tc->path);
            if (!data) continue;
            if (run != Run::Image || runAlpha != tc->alpha || !sameTint(runTint, tc->tint)) {
                endRun();
                run      = Run::Image;
                runAlpha = tc->alpha;
                runTint  = tc->tint;
            }
            mCtx.drawImage(data->mClientTexture, {tc->r.x, tc->r.y}, {tc->r.w, tc->r.h}, {0, 0}, {1, 1}, false);
        }
        endRun();
    };

    quads(Layer::Background);

    std::vector<ItemCmd> items; // copies: recording below may reallocate mCmds
    for (auto& cmd : *mCmds) {
        if (auto* ic = std::get_if<ItemCmd>(&cmd.v)) items.push_back(*ic);
    }
    auto* renderer = mCtx.mClient.getItemRenderer();
    if (renderer && !items.empty()) {
        mCtx.fillRectangle(RectangleArea{-10000.f, -9999.f, -10000.f, -9999.f}, white, 1.f);
        mCtx.flushImages(white, 1.f, imageMaterial());

        BaseActorRenderContext rc(mCtx.mScreenContext, mCtx.mClient, mCtx.mClient.getMinecraftGame_DEPRECATED());
        auto direct = [&](ItemCmd const& c, bool foil) {
            renderer->renderGuiItemNew(rc, *c.stack, 0, c.x, c.y, foil, c.alpha, 1.f, c.scale, 0);
        };
        std::vector<int> chunks(items.size());
        for (std::size_t i = 0; i < items.size(); ++i) {
            auto const& st = *items[i].stack;
            chunks[i]      = st.mItem->isGlint(st) ? -1 : static_cast<int>(InventoryItemRenderer::getRenderTypeFromItem(st));
        }
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (chunks[i] != -1) continue;
            direct(items[i], false);
            direct(items[i], true);
        }
        std::vector<float> alphas;
        for (auto const& it : items) {
            if (std::find(alphas.begin(), alphas.end(), it.alpha) == alphas.end()) alphas.push_back(it.alpha);
        }
        for (int chunk : {CHUNK_BLOCK, CHUNK_FLAT}) {
            for (float alpha : alphas) {
                bool any = false;
                for (std::size_t i = 0; i < items.size() && !any; ++i) any = chunks[i] == chunk && items[i].alpha == alpha;
                if (!any) continue;
                ItemMeshBatch batch(chunk, 0, alpha);
                mCtx.beginSharedMeshBatch(batch.get());
                for (std::size_t i = 0; i < items.size(); ++i) {
                    if (chunks[i] != chunk || items[i].alpha != alpha) continue;
                    auto const& c = items[i];
                    renderer->renderGuiItemInChunk(
                        rc,
                        static_cast<ItemRenderChunkType>(chunk),
                        *c.stack,
                        c.x,
                        c.y,
                        1.f,
                        chunk == CHUNK_BLOCK ? 0.f : 1.f,
                        c.scale,
                        0,
                        false,
                        0,
                        std::nullopt
                    );
                }
                mCtx.endSharedMeshBatch(batch.get());
            }
        }
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (chunks[i] != -1 && chunks[i] != CHUNK_BLOCK && chunks[i] != CHUNK_FLAT) direct(items[i], false);
        }
        for (auto const& it : items) {
            float ratio = durabilityRatio(*it.stack);
            if (ratio < 0) continue;
            float x = it.x + 2 * it.scale, y = it.y + 13 * it.scale;
            fillRect({x, y, 13 * it.scale, 2 * it.scale}, Color::rgb(0x000000), Layer::Foreground);
            fillRect({x, y, 13 * it.scale * ratio, 1 * it.scale}, Color{1 - ratio, ratio, 0, 1}, Layer::Foreground);
        }
    }

    quads(Layer::Foreground);

    // Text drawn right after item icons (no quad flush in between) is hidden by the slot frames
    // drawn earlier (seen in the backpack screen's counts); an off-screen quad flush first, as
    // before the items, puts the text on top.
    if (std::any_of(mCmds->begin(), mCmds->end(), [](Cmd const& c) { return std::holds_alternative<TextCmd>(c.v); })) {
        mCtx.fillRectangle(RectangleArea{-10000.f, -9999.f, -10000.f, -9999.f}, white, 1.f);
        mCtx.flushImages(white, 1.f, fillMaterial());
    }

    bool  anyText = false;
    auto& font    = uiFont(mCtx);
    for (auto& cmd : *mCmds) {
        auto* tc = std::get_if<TextCmd>(&cmd.v);
        if (!tc) continue;
        float w  = tc->width > 0 ? tc->width : textWidth(tc->s, tc->scale) + 2;
        float x0 = tc->x;
        auto  al = ::ui::TextAlignment::Left;
        if (tc->align == TextAlign::Center) al = ::ui::TextAlignment::Center;
        if (tc->align == TextAlign::Right) al = ::ui::TextAlignment::Right;
        if (tc->width <= 0 && tc->align == TextAlign::Center) x0 -= w / 2;
        if (tc->width <= 0 && tc->align == TextAlign::Right) x0 -= w;
        RectangleArea    area{x0, x0 + w, tc->y, tc->y + lineHeight(tc->scale) + 2};
        TextMeasureData  measure{tc->scale, 0.f, tc->shadow, false, false, al};
        CaretMeasureData caret{-1, false};
        mCtx.drawText(font, area, std::string{tc->s}, toMce(tc->c), tc->c.a, al, measure, caret);
        anyText = true;
    }
    if (anyText) mCtx.flushText(0.f, std::nullopt);

    mCmds->clear();
}

} // namespace abp::client::ui

#endif // ABP_CLIENT
