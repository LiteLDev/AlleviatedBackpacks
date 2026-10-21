#pragma once

// Reusable widgets matching Sophisticated Core's controls (client/ui/widgets/SbStyle.h draws them).
// Used by the backpack screen and by native upgrade tabs (client/ui/screen/Tabs.h).
//
// Coordinates are absolute GUI units. Input callbacks run on the client thread; a widget returns
// true when it consumed the event.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "client/ui/UiLayer.h"
#include "client/ui/gfx/Painter.h"
#include "client/ui/widgets/SbStyle.h"
#include "client/ui/widgets/TextBox.h"
#include "client/ui/widgets/Tooltip.h"

class ItemStack;

namespace abp::client::ui {

class Widget {
public:
    Rect rect;
    bool visible = true;

    virtual ~Widget() = default;
    virtual void render(Painter& p, float mouseX, float mouseY, double time) = 0;
    virtual bool mouseDown(MouseButton /*button*/, float /*x*/, float /*y*/) { return false; }
    virtual bool mouseUp(MouseButton /*button*/, float /*x*/, float /*y*/) { return false; }
    virtual bool mouseMove(float /*x*/, float /*y*/) { return false; }
    virtual bool scroll(float /*x*/, float /*y*/, float /*delta*/) { return false; }
    virtual bool key(int /*vk*/, bool /*down*/) { return false; }
    virtual bool chr(std::string const& /*utf8*/) { return false; }
    [[nodiscard]] virtual bool wantsText() const { return false; }
    /// Tooltip lines when hovered at (x, y).
    virtual void tooltip(float /*x*/, float /*y*/, std::vector<TooltipLine>& /*out*/) const {}

    [[nodiscard]] bool hovered(float x, float y) const { return visible && rect.contains(x, y); }
};

/// Icon drawer for buttons: draws at the button's top-left.
using IconFn = std::function<void(Painter&, float x, float y)>;

/// SC `Button`: 18x18 (`Size::Normal`) or 12x12 (`Size::Small`) with a background, a hovered
/// background and a foreground icon. `onClick(button)` gets 0 = left, 1 = right, 2 = middle.
class Button : public Widget {
public:
    enum class Size { Normal, Small };
    Size                              size = Size::Small;
    IconFn                            icon;
    std::vector<TooltipLine>          tooltipLines;
    std::function<void(int button)>   onClick;
    /// Dynamic tooltip / icon (e.g. depending on Shift).
    std::function<std::vector<TooltipLine>()> dynamicTooltip;

    void render(Painter& p, float mx, float my, double time) override;
    bool mouseDown(MouseButton button, float x, float y) override;
    void tooltip(float x, float y, std::vector<TooltipLine>& out) const override;
};

/// SC `ToggleButton<T>`: a button showing the icon/tooltip of the current state; clicking cycles
/// (left: next, right: previous) through `stateCount` states.
class ToggleButton : public Widget {
public:
    struct State {
        IconFn                   icon;
        std::vector<TooltipLine> tooltip;
    };
    Button::Size                        size = Button::Size::Normal;
    std::vector<State>                  states;
    std::function<int()>                current;  ///< index into `states`
    std::function<void(int button)>     onClick;

    void render(Painter& p, float mx, float my, double time) override;
    bool mouseDown(MouseButton button, float x, float y) override;
    void tooltip(float x, float y, std::vector<TooltipLine>& out) const override;
};

/// SC upgrade switch (6x12): on / off / inactive.
class UpgradeSwitch : public Widget {
public:
    std::function<int()>     state; ///< 1 on, 0 off, -1 inactive
    std::function<void()>    onToggle;
    std::vector<TooltipLine> onTooltip, offTooltip, inactiveTooltip;

    void render(Painter& p, float mx, float my, double time) override;
    bool mouseDown(MouseButton button, float x, float y) override;
    void tooltip(float x, float y, std::vector<TooltipLine>& out) const override;
};

/// SC `ProgressBar` (and the tank / energy bars of later units): fills `rect` by `progress()`
/// in [0,1], left-to-right or bottom-up.
class ProgressBar : public Widget {
public:
    enum class Direction { LeftRight, BottomUp };
    Direction                         direction = Direction::LeftRight;
    Color                             fill      = Color::rgb(0xFFFFFF);
    Color                             back      = Color::argb(0x00000000);
    bool                              framed    = false; ///< draw SC's bar frame (tank/energy)
    std::function<float()>            progress;
    std::function<std::vector<TooltipLine>()> tooltipFn;

    void render(Painter& p, float mx, float my, double time) override;
    void tooltip(float x, float y, std::vector<TooltipLine>& out) const override;
};

/// A grid of 18x18 item slots (filter / ghost slots, upgrade inventories). The owner provides the
/// items and handles clicks (send them to the server).
class SlotGrid : public Widget {
public:
    int                                                     columns = 9;
    int                                                     count   = 9;
    std::function<ItemStack const*(int index)>              item;  ///< nullptr = empty
    std::function<void(int index, int button, bool shift)> onClick;
    bool                                                    ghost = false; ///< draw items faded

    void render(Painter& p, float mx, float my, double time) override;
    bool mouseDown(MouseButton button, float x, float y) override;
    void tooltip(float x, float y, std::vector<TooltipLine>& out) const override;
    [[nodiscard]] int slotAt(float x, float y) const;
};

/// A vertically scrollable list of text rows (`rows()`), `rowHeight` each.
class ScrollList : public Widget {
public:
    float                                     rowHeight = 10;
    std::function<std::vector<std::string>()> rows;
    std::function<void(int row)>              onClick;
    int                                       selected = -1;

    void render(Painter& p, float mx, float my, double time) override;
    bool mouseDown(MouseButton button, float x, float y) override;
    bool scroll(float x, float y, float delta) override;

private:
    int mFirst = 0;
};

/// A slowly rotating / pulsing preview of an item (SC `RotatablePreviewWidget` analogue; Bedrock
/// draws GUI items as icons, so it shows the icon scaled up).
class ItemPreview : public Widget {
public:
    std::function<ItemStack const*()> item;
    float                             scale = 2;

    void render(Painter& p, float mx, float my, double time) override;
};

/// SC `TextBox` widget wrapping the ported text box.
class TextField : public Widget {
public:
    TextBox box;

    void render(Painter& p, float mx, float my, double time) override;
    bool mouseDown(MouseButton button, float x, float y) override;
    bool key(int vk, bool down) override;
    bool chr(std::string const& utf8) override;
    [[nodiscard]] bool wantsText() const override { return box.focused(); }
};

/// SC `Label`: text (truncated to `rect.w` when set).
class Label : public Widget {
public:
    std::string text;
    Color       color  = Color::rgb(0x404040);
    bool        shadow = false;

    void render(Painter& p, float mx, float my, double time) override;
};

/// A set of widgets with input routing in reverse draw order.
class WidgetGroup {
public:
    std::vector<std::unique_ptr<Widget>> widgets;

    template <class W>
    W& add(std::unique_ptr<W> w) {
        auto& ref = *w;
        widgets.push_back(std::move(w));
        return ref;
    }
    void render(Painter& p, float mx, float my, double time);
    bool mouseDown(MouseButton button, float x, float y);
    bool mouseUp(MouseButton button, float x, float y);
    bool mouseMove(float x, float y);
    bool scroll(float x, float y, float delta);
    bool key(int vk, bool down);
    bool chr(std::string const& utf8);
    [[nodiscard]] bool wantsText() const;
    void               tooltip(float x, float y, std::vector<TooltipLine>& out) const;
};

} // namespace abp::client::ui
