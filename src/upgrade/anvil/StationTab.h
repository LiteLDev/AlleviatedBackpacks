#pragma once

// Client side of the workstation tabs (anvil, smithing): a slot widget for the tab's item slots
// and the small GUI marks SB draws between them (plus sign, arrow, red cross). Original pixel art
// sized after SC's GUI_CONTROLS sprites (plus 13x13, arrow 14x15, red cross 15x15).

#ifdef ABP_CLIENT

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "mc/world/PlayerUIContainer.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/item/ItemStack.h"

#include "client/Client.h"
#include "client/input/InputRouter.h"
#include "client/ui/widgets/Widgets.h"
#include "game/ItemConv.h"

namespace abp::upgrade::anvil::client {

namespace ui = ::abp::client::ui;

/// A stack sent as tab data (`ItemStackData` JSON or null), with its real count.
inline ItemStack stackFrom(nlohmann::json const& j) {
    if (!j.is_object()) return {};
    try {
        auto d = core::ItemStackData::fromJson(j);
        if (d.empty()) return {};
        return game::ItemConv::toStack(d, std::clamp(d.count, 1, 255));
    } catch (...) {
        return {};
    }
}

/// Vanilla slot count overlay (white with shadow, bottom right) for counts above 1.
inline void drawSlotCount(ui::Painter& p, ItemStack const& s, float x, float y) {
    if (s.isNull() || s.mCount <= 1) return;
    auto text = std::to_string(static_cast<int>(s.mCount));
    p.text(text, x + 17 - p.textWidth(text), y + 9, ui::Color::rgb(0xFFFFFF), true);
}

/// True while the local player holds nothing on the cursor (SC only shows slot tooltips then).
inline bool cursorEmpty() {
    auto* player = ::abp::client::currentPlayer();
    return !player || player->mPlayerUIContainer->getItem(0).isNull();
}

/// One item slot of a station tab: an 18x18 SC slot frame (`rect`), the item, the hover highlight
/// and the item tooltip. Clicks are sent to the server (`onClick(button, shift)`).
class StationSlot final : public ui::Widget {
public:
    std::function<ItemStack const*()>                       stack;
    std::function<void(int button, bool shift)>             onClick;
    /// Drawn in an empty slot (SC CyclingSlotBackground); optional.
    std::function<void(ui::Painter&, float x, float y, double time)> emptyIcon;
    /// Tooltip of the empty slot (SB smithing onboarding tooltips); optional.
    std::function<std::vector<ui::TooltipLine>()>          emptyTooltip;

    void render(ui::Painter& p, float mx, float my, double time) override {
        if (!visible) return;
        float x = rect.x + 1, y = rect.y + 1;
        ui::sb::slotFrame(p, x, y);
        auto const* s = stack ? stack() : nullptr;
        if (s && !s->isNull()) {
            p.item(*s, x, y);
            drawSlotCount(p, *s, x, y);
        } else if (emptyIcon) {
            emptyIcon(p, x, y, time);
        }
        if (hovered(mx, my)) p.fillRect({x, y, 16, 16}, ui::sb::color::HOVER, ui::Painter::Layer::Foreground);
    }
    bool mouseDown(ui::MouseButton button, float x, float y) override {
        if (!hovered(x, y)) return false;
        if (button == ui::MouseButton::Middle) return true;
        if (onClick) onClick(button == ui::MouseButton::Right ? 1 : 0, ::abp::client::input::shiftDown());
        return true;
    }
    void tooltip(float x, float y, std::vector<ui::TooltipLine>& out) const override {
        if (!hovered(x, y)) return;
        auto const* s = stack ? stack() : nullptr;
        if (s && !s->isNull()) {
            if (!cursorEmpty()) return; // vanilla item tooltips need an empty cursor
            auto lines = ui::itemTooltipLines(*s);
            out.insert(out.end(), lines.begin(), lines.end());
        } else if (emptyTooltip) {
            auto lines = emptyTooltip();
            out.insert(out.end(), lines.begin(), lines.end());
        }
    }

    [[nodiscard]] bool hovered(float x, float y) const { return visible && ui::Rect{rect.x + 1, rect.y + 1, 16, 16}.contains(x, y); }
};

// ---- marks between the slots (original pixel art) --------------------------------------------------

inline ui::PixelArt const& plusSign() {
    static ui::PixelArt const art{
        {
            ".....DDD.....",
            ".....DLD.....",
            ".....DLD.....",
            ".....DLD.....",
            ".....DLD.....",
            "DDDDDDLDDDDDD",
            "DLLLLLLLLLLLD",
            "DDDDDDLDDDDDD",
            ".....DLD.....",
            ".....DLD.....",
            ".....DLD.....",
            ".....DLD.....",
            ".....DDD.....",
        },
        {"D=8B8B8B", "L=E8E8E8"},
    };
    return art;
}

inline ui::PixelArt const& arrow() {
    static ui::PixelArt const art{
        {
            "..............",
            "........D.....",
            "........DD....",
            "........DGD...",
            "........DGGD..",
            "DDDDDDDDDGGGD.",
            "DGGGGGGGGGGGGD",
            "DGGGGGGGGGGGGD",
            "DGGGGGGGGGGGGD",
            "DDDDDDDDDGGGD.",
            "........DGGD..",
            "........DGD...",
            "........DD....",
            "........D.....",
            "..............",
        },
        {"D=5A5A5A", "G=8B8B8B"},
    };
    return art;
}

inline ui::PixelArt const& redCross() {
    static ui::PixelArt const art{
        {
            "OO...........OO",
            "ORO.........ORO",
            ".ORO.......ORO.",
            "..ORO.....ORO..",
            "...ORO...ORO...",
            "....ORO.ORO....",
            ".....ORORO.....",
            "......ORO......",
            ".....ORORO.....",
            "....ORO.ORO....",
            "...ORO...ORO...",
            "..ORO.....ORO..",
            ".ORO.......ORO.",
            "ORO.........ORO",
            "OO...........OO",
        },
        {"O=5C0000", "R=FF3B3B"},
    };
    return art;
}

} // namespace abp::upgrade::anvil::client

#endif // ABP_CLIENT
