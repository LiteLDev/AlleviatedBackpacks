#ifdef ABP_CLIENT

#include "client/ui/widgets/Widgets.h"

#include <algorithm>
#include <cmath>

#include "mc/world/item/ItemStack.h"

namespace abp::client::ui {

namespace {
int buttonIndex(MouseButton b) { return b == MouseButton::Left ? 0 : b == MouseButton::Right ? 1 : 2; }

std::function<void()>& clickSound() {
    static std::function<void()> fn;
    return fn;
}
} // namespace

void setButtonClickSound(std::function<void()> sound) { clickSound() = std::move(sound); }

void playButtonClick() {
    if (auto const& fn = clickSound()) fn();
}

// ---- Button ----------------------------------------------------------------------------------------

void Button::render(Painter& p, float mx, float my, double) {
    if (!visible) return;
    bool hov = hovered(mx, my);
    if (size == Size::Small) sb::smallButtonBackground(p, rect, hov);
    else sb::buttonBackground(p, rect, hov);
    if (icon) icon(p, rect.x, rect.y);
}

bool Button::mouseDown(MouseButton button, float x, float y) {
    if (!hovered(x, y)) return false;
    if (onClick) onClick(buttonIndex(button));
    playButtonClick();
    return true;
}

void Button::tooltip(float x, float y, std::vector<TooltipLine>& out) const {
    if (!hovered(x, y)) return;
    auto lines = dynamicTooltip ? dynamicTooltip() : tooltipLines;
    out.insert(out.end(), lines.begin(), lines.end());
}

// ---- ToggleButton -----------------------------------------------------------------------------------

void ToggleButton::render(Painter& p, float mx, float my, double) {
    if (!visible) return;
    bool hov = hovered(mx, my);
    if (size == Button::Size::Small) sb::smallButtonBackground(p, rect, hov);
    else sb::buttonBackground(p, rect, hov);
    int i = current ? current() : 0;
    if (i >= 0 && i < static_cast<int>(states.size()) && states[static_cast<std::size_t>(i)].icon) {
        states[static_cast<std::size_t>(i)].icon(p, rect.x, rect.y);
    }
}

bool ToggleButton::mouseDown(MouseButton button, float x, float y) {
    if (!hovered(x, y)) return false;
    if (onClick) onClick(buttonIndex(button));
    playButtonClick();
    return true;
}

void ToggleButton::tooltip(float x, float y, std::vector<TooltipLine>& out) const {
    if (!hovered(x, y)) return;
    int i = current ? current() : 0;
    if (i >= 0 && i < static_cast<int>(states.size())) {
        auto const& t = states[static_cast<std::size_t>(i)].tooltip;
        out.insert(out.end(), t.begin(), t.end());
    }
}

// ---- UpgradeSwitch ------------------------------------------------------------------------------------

void UpgradeSwitch::render(Painter& p, float mx, float my, double) {
    if (!visible) return;
    int s = state ? state() : 0;
    sb::upgradeSwitch(p, rect.x, rect.y, s, s >= 0 && hovered(mx, my));
}

bool UpgradeSwitch::mouseDown(MouseButton button, float x, float y) {
    if (!hovered(x, y)) return false;
    // SC: the switch's click handler ignores the mouse button, and an inactive switch still toggles.
    (void)button;
    if (onToggle) onToggle();
    playButtonClick();
    return true;
}

void UpgradeSwitch::tooltip(float x, float y, std::vector<TooltipLine>& out) const {
    if (!hovered(x, y)) return;
    int   s = state ? state() : 0;
    auto& t = s > 0 ? onTooltip : s == 0 ? offTooltip : inactiveTooltip;
    out.insert(out.end(), t.begin(), t.end());
}

// ---- ProgressBar ----------------------------------------------------------------------------------------

void ProgressBar::render(Painter& p, float, float, double) {
    if (!visible) return;
    if (framed) sb::controlBackground(p, {rect.x - 1, rect.y - 1, rect.w + 2, rect.h + 2});
    if (back.a > 0) p.fillRect(rect, back);
    float v = std::clamp(progress ? progress() : 0.f, 0.f, 1.f);
    if (v <= 0) return;
    if (direction == Direction::LeftRight) {
        p.fillRect({rect.x, rect.y, std::floor(rect.w * v), rect.h}, fill);
    } else {
        float h = std::floor(rect.h * v);
        p.fillRect({rect.x, rect.bottom() - h, rect.w, h}, fill);
    }
}

void ProgressBar::tooltip(float x, float y, std::vector<TooltipLine>& out) const {
    if (!hovered(x, y) || !tooltipFn) return;
    auto lines = tooltipFn();
    out.insert(out.end(), lines.begin(), lines.end());
}

// ---- SlotGrid ---------------------------------------------------------------------------------------------

int SlotGrid::slotAt(float x, float y) const {
    if (!hovered(x, y) || columns <= 0) return -1;
    int c = static_cast<int>((x - rect.x) / 18);
    int r = static_cast<int>((y - rect.y) / 18);
    int i = r * columns + c;
    return c >= 0 && c < columns && i >= 0 && i < count ? i : -1;
}

void SlotGrid::render(Painter& p, float mx, float my, double) {
    if (!visible) return;
    int hov = slotAt(mx, my);
    for (int i = 0; i < count; ++i) {
        float x = rect.x + 1 + static_cast<float>(i % columns) * 18;
        float y = rect.y + 1 + static_cast<float>(i / columns) * 18;
        sb::slotFrame(p, x, y);
        if (auto const* s = item ? item(i) : nullptr; s && !s->isNull()) p.item(*s, x, y, 1, ghost ? 0.6f : 1.f);
        if (i == hov) p.fillRect({x, y, 16, 16}, sb::color::HOVER, Painter::Layer::Foreground);
    }
}

bool SlotGrid::mouseDown(MouseButton button, float x, float y) {
    int i = slotAt(x, y);
    if (i < 0) return false;
    if (onClick) onClick(i, buttonIndex(button), false);
    return true;
}

void SlotGrid::tooltip(float x, float y, std::vector<TooltipLine>& out) const {
    int i = slotAt(x, y);
    if (i < 0 || !item) return;
    if (auto const* s = item(i); s && !s->isNull()) {
        auto lines = itemTooltipLines(*s);
        out.insert(out.end(), lines.begin(), lines.end());
    }
}

// ---- ScrollList ---------------------------------------------------------------------------------------------

void ScrollList::render(Painter& p, float mx, float my, double) {
    if (!visible) return;
    sb::controlBackground(p, rect);
    auto  list   = rows ? rows() : std::vector<std::string>{};
    int   fits   = std::max(1, static_cast<int>((rect.h - 2) / rowHeight));
    mFirst       = std::clamp(mFirst, 0, std::max(0, static_cast<int>(list.size()) - fits));
    for (int i = 0; i < fits && mFirst + i < static_cast<int>(list.size()); ++i) {
        int   row = mFirst + i;
        float y   = rect.y + 1 + static_cast<float>(i) * rowHeight;
        Rect  r{rect.x + 1, y, rect.w - 2, rowHeight};
        if (row == selected) p.fillRect(r, Color::argb(0x80FFFFFF));
        else if (r.contains(mx, my)) p.fillRect(r, Color::argb(0x40FFFFFF));
        p.text(list[static_cast<std::size_t>(row)], r.x + 2, r.y + 1, Color::rgb(0xFFFFFF), true);
    }
}

bool ScrollList::mouseDown(MouseButton, float x, float y) {
    if (!hovered(x, y)) return false;
    int row = mFirst + static_cast<int>((y - rect.y - 1) / rowHeight);
    if (rows && row >= 0 && row < static_cast<int>(rows().size())) {
        selected = row;
        if (onClick) onClick(row);
    }
    return true;
}

bool ScrollList::scroll(float x, float y, float delta) {
    if (!hovered(x, y)) return false;
    mFirst -= delta > 0 ? 1 : -1;
    if (mFirst < 0) mFirst = 0;
    return true;
}

// ---- ItemPreview / TextField / Label -------------------------------------------------------------------------

void ItemPreview::render(Painter& p, float, float, double time) {
    if (!visible) return;
    sb::controlBackground(p, rect);
    auto const* s = item ? item() : nullptr;
    if (!s || s->isNull()) return;
    float bob = static_cast<float>(std::sin(time * 2.0)) * 1.5f;
    float sz  = 16 * scale;
    p.item(*s, rect.x + (rect.w - sz) / 2, rect.y + (rect.h - sz) / 2 + bob, scale);
}

void TextField::render(Painter& p, float, float, double time) {
    if (!visible) return;
    box.rect = rect;
    box.render(p, time);
}

bool TextField::mouseDown(MouseButton button, float x, float y) {
    if (!visible) return false;
    return box.onMouseButton(button == MouseButton::Right, true, x, y);
}

bool TextField::key(int vk, bool down) { return box.onKey(vk, down); }

bool TextField::chr(std::string const& utf8) { return box.onChar(utf8); }

void Label::render(Painter& p, float, float, double) {
    if (!visible || text.empty()) return;
    p.text(text, rect.x, rect.y, color, shadow);
}

// ---- WidgetGroup ------------------------------------------------------------------------------------------------

void WidgetGroup::render(Painter& p, float mx, float my, double time) {
    for (auto& w : widgets) {
        if (w->visible) w->render(p, mx, my, time);
    }
}

bool WidgetGroup::mouseDown(MouseButton button, float x, float y) {
    bool handled = false;
    for (auto it = widgets.rbegin(); it != widgets.rend(); ++it) {
        if (!(*it)->visible) continue;
        // Text fields also need clicks elsewhere (to lose focus).
        if ((*it)->mouseDown(button, x, y)) {
            handled = true;
            break;
        }
    }
    return handled;
}

bool WidgetGroup::mouseUp(MouseButton button, float x, float y) {
    for (auto it = widgets.rbegin(); it != widgets.rend(); ++it) {
        if ((*it)->visible && (*it)->mouseUp(button, x, y)) return true;
    }
    return false;
}

bool WidgetGroup::mouseMove(float x, float y) {
    bool any = false;
    for (auto& w : widgets) any |= w->visible && w->mouseMove(x, y);
    return any;
}

bool WidgetGroup::scroll(float x, float y, float delta) {
    for (auto it = widgets.rbegin(); it != widgets.rend(); ++it) {
        if ((*it)->visible && (*it)->scroll(x, y, delta)) return true;
    }
    return false;
}

bool WidgetGroup::key(int vk, bool down) {
    for (auto& w : widgets) {
        if (w->visible && w->wantsText() && w->key(vk, down)) return true;
    }
    return false;
}

bool WidgetGroup::chr(std::string const& utf8) {
    for (auto& w : widgets) {
        if (w->visible && w->wantsText() && w->chr(utf8)) return true;
    }
    return false;
}

bool WidgetGroup::wantsText() const {
    return std::any_of(widgets.begin(), widgets.end(), [](auto const& w) { return w->visible && w->wantsText(); });
}

void WidgetGroup::tooltip(float x, float y, std::vector<TooltipLine>& out) const {
    for (auto const& w : widgets) {
        if (w->visible) w->tooltip(x, y, out);
    }
}

} // namespace abp::client::ui

#endif // ABP_CLIENT
