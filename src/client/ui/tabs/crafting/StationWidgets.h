#pragma once

// Widgets shared by the station tabs (crafting, stonecutter, cooking): real item slots whose
// items live on the server (clicks become tab actions), SC's 26x26 result slot frame, small
// buttons, progress bars and original pixel-art icons drawn after SC's GuiHelper / gui_controls
// dimensions (no SB textures are copied).

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "client/ui/screen/Tabs.h"
#include "client/ui/widgets/Widgets.h"

class ItemStack;

namespace abp::client::ui::station {

// ---- colours of SC's gui_controls (sampled for dimensions / palette only) --------------------------
namespace color {
inline constexpr Color LIST_BG     = Color::rgb(0x51493A); ///< dark result list background
inline constexpr Color LIST_EDGE   = Color::rgb(0x6B614C);
inline constexpr Color RECIPE      = Color::rgb(0xA09172); ///< recipe button
inline constexpr Color RECIPE_LITE = Color::rgb(0xE0CA9F);
inline constexpr Color RECIPE_DARK = Color::rgb(0x544C3B);
inline constexpr Color SELECTED    = Color::rgb(0x51493A); ///< selected recipe (pressed)
inline constexpr Color SELECTED_DK = Color::rgb(0x211D17);
inline constexpr Color HOVERED     = Color::rgb(0xB688AE); ///< hovered recipe
inline constexpr Color HOVERED_LT  = Color::rgb(0xFAB9EF);
inline constexpr Color RECENT      = Color::rgb(0xB8C4CF); ///< recently taken result
inline constexpr Color RECENT_LT   = Color::rgb(0xD3DEE8);
} // namespace color

/// The item on the local player's cursor (nullptr when empty / no player).
[[nodiscard]] ItemStack const* cursorItem();
/// Item stack from tab JSON (null / missing -> empty stack).
[[nodiscard]] ItemStack stackFromJson(nlohmann::json const& j);
/// SB count overlay (vanilla style; abbreviated above 99).
void drawCount(Painter& p, long long count, float x, float y);

/// One real item slot (18x18 frame, item at +1,+1). The item comes from the tab's server data;
/// a click calls `onClick(button, shift)` (send it to the server as a tab action).
class ItemSlot : public Widget {
public:
    std::function<ItemStack const*()>          item;
    std::function<void(int button, bool shift)> onClick;
    bool                                       frame = true; ///< draw the slot frame

    void render(Painter& p, float mx, float my, double time) override;
    bool mouseDown(MouseButton button, float x, float y) override;
    void tooltip(float x, float y, std::vector<TooltipLine>& out) const override;
};

/// A bevelled box like SC's gui_controls pieces: `topLeft` on the top row and left column, `bottomRight`
/// on the bottom row and right column (the two odd corners take `fill`).
void bevel(Painter& p, Rect const& r, Color topLeft, Color fill, Color bottomRight, Painter::Layer layer = Painter::Layer::Background);

/// SC recipe button backgrounds (16x18).
enum class RecipeBg { Normal, Selected, Hovered, Recent };
void recipeBackground(Painter& p, float x, float y, RecipeBg which);
/// SC list background (dark), scroll bar background and slider (12x15).
void listBackground(Painter& p, Rect const& r);
void scrollBackground(Painter& p, Rect const& r);
void slider(Painter& p, float x, float y, bool enabled);

/// SC GuiHelper.CRAFTING_RESULT_SLOT: the big 26x26 result slot frame at (x, y).
void resultSlotFrame(Painter& p, float x, float y);

/// A button with SC's small (8x12 / 16x12) or default (18x18 / 14x14) backgrounds.
class StationButton : public Widget {
public:
    IconFn                                    icon;
    std::vector<TooltipLine>                  tooltipLines;
    std::function<std::vector<TooltipLine>()> dynamicTooltip;
    std::function<void(int button)>           onClick;
    /// Text drawn on the button (e.g. the result count of "Select Result").
    std::function<std::string()>              label;

    void render(Painter& p, float mx, float my, double time) override;
    bool mouseDown(MouseButton button, float x, float y) override;
    void tooltip(float x, float y, std::vector<TooltipLine>& out) const override;
};

/// Splits SB's two-line button texts ("...\n...") into tooltip lines.
[[nodiscard]] std::vector<TooltipLine> lines(std::string const& text, std::uint32_t rgb = 0xFFFFFF);

// ---- icons (original pixel art) -------------------------------------------------------------------
enum class Icon {
    IntoBackpack,      ///< 16x16 shift-click result into the backpack
    IntoInventory,     ///< 16x16 ... into the player's inventory
    RefillGrid,        ///< 16x16 refill crafting grid
    NoRefillGrid,      ///< 16x16 do not refill crafting grid
    RefillInput,       ///< 16x16 refill input
    NoRefillInput,     ///< 16x16 do not refill input
    Previous,          ///< 8x12 chevron
    Next,              ///< 8x12 chevron
    Browse,            ///< 12x12 expand brackets
    Search,            ///< 16x16 magnifying glass
    FlameEmpty,        ///< 14x14 unlit flame (furnace background)
    FlameLit,          ///< 14x14 lit flame (burn progress)
    ArrowEmpty,        ///< 22x16 empty cook arrow
    ArrowFull,         ///< 22x16 white cook arrow (cook progress)
    CraftArrow,        ///< 15x8 crafting tab arrow
    Allow,             ///< 16x16 filter allow list
    Block,             ///< 16x16 filter deny list
    MatchItem,         ///< 16x16
    MatchMod,          ///< 16x16
    MatchTags,         ///< 16x16
    MatchDurability,   ///< 16x16
    IgnoreDurability,  ///< 16x16
    AddTag,            ///< 16x16
    RemoveTag,         ///< 16x16
    MatchAnyTag,       ///< 16x16
    MatchAllTags,      ///< 16x16
};
void icon(Painter& p, Icon which, float x, float y);
/// Draws the top `fraction` (0..1) rows of an icon cut off: SC ProgressBar BOTTOM_UP shows the
/// rows below `height * progress`.
void iconBottomUp(Painter& p, Icon which, float x, float y, float progress, float height);
/// SC ProgressBar LEFT_RIGHT: the first `width * progress` columns.
void iconLeftRight(Painter& p, Icon which, float x, float y, float progress, float width);

// ---- open-tab reporting ----------------------------------------------------------------------------

/// Tells the server that this station tab is open (shift-clicks go into it first) and, when its
/// widgets are cleared (the tab closes or rebuilds), that it closed again. Add it in `build()`.
void reportTabOpen(TabContext const& ctx, WidgetGroup& widgets);

} // namespace abp::client::ui::station
