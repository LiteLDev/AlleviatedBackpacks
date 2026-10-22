// Client: the anvil upgrade's native tab (SB AnvilUpgradeTab), 103x92.
//
//   title                                   (+20, +8, drawn by the host)
//   item name box 90x14 at (+5, +25)        borderless, white text, 50 characters, editable only
//                                           with an item in the first slot
//   slots at y + 43: first (+4), second (+width/2 - 9), result (+width - 23); a plus sign between
//   the inputs, an arrow before the result and a red cross over it when there is no result
//   cost line at (+3, +62)                  "Enchantment Cost: n" (green, red when it can't be
//                                           taken) / "Too Expensive!" (red), on a dark strip

#ifdef ABP_CLIENT

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "client/Client.h"
#include "client/ui/screen/Tabs.h"
#include "client/ui/widgets/Widgets.h"
#include "upgrade/anvil/StationTab.h"

namespace abp::upgrade::anvil::client {
namespace {

using ::abp::client::ctr;
using ui::Color;
using ui::Painter;
using ui::Rect;

constexpr float kWidth  = 103;
constexpr float kHeight = 92;

/// SB's item name box: the text box plus its background (EDIT_ITEM_NAME_BACKGROUND, or the
/// _DISABLED variant without an item), blitted 2 units around the box.
class NameField final : public ui::Widget {
public:
    ui::TextBox            box;
    std::function<bool()> editable;

    void render(Painter& p, float, float, double time) override {
        if (!visible) return;
        bool on = editable && editable();
        if (!on && box.focused()) box.setFocused(false);
        Rect bg{rect.x - 2, rect.y - 2, rect.w + 4, rect.h + 2};
        // Original art: a recessed bar, light when editable, dark when disabled.
        p.fillRect(bg, Color::rgb(on ? 0x6B5F4B : 0x3A342A));
        p.fillRect({bg.x + 1, bg.y + 1, bg.w - 2, bg.h - 2}, Color::rgb(on ? 0x9C8C70 : 0x4D4537));
        p.fillRect({bg.x + 1, bg.bottom() - 1, bg.w - 1, 1}, Color::rgb(on ? 0xC7B898 : 0x5E5544));
        p.fillRect({bg.right() - 1, bg.y + 1, 1, bg.h - 1}, Color::rgb(on ? 0xC7B898 : 0x5E5544));
        box.rect = rect;
        box.render(p, time);
    }
    bool mouseDown(ui::MouseButton button, float x, float y) override {
        if (!visible) return false;
        if (!(editable && editable())) {
            box.setFocused(false);
            return rect.contains(x, y);
        }
        return box.onMouseButton(button == ui::MouseButton::Right, true, x, y);
    }
    bool key(int vk, bool down) override { return box.onKey(vk, down); }
    bool chr(std::string const& utf8) override { return box.onChar(utf8); }
    [[nodiscard]] bool wantsText() const override { return box.focused(); }
};

/// Words of `text` wrapped to `maxWidth` (Font.split).
std::vector<std::string> wrap(Painter& p, std::string const& text, float maxWidth) {
    std::vector<std::string> lines;
    std::string              line;
    std::size_t              i = 0;
    while (i < text.size()) {
        auto        sp   = text.find(' ', i);
        std::string word = text.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
        i                = sp == std::string::npos ? text.size() : sp + 1;
        std::string next = line.empty() ? word : line + " " + word;
        if (!line.empty() && p.textWidth(next) > maxWidth) {
            lines.push_back(line);
            line = word;
        } else {
            line = next;
        }
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

class AnvilTab final : public ::abp::client::ui::NativeTab {
public:
    Size build(::abp::client::ui::TabContext const& ctx, Rect area, ui::WidgetGroup& widgets) override {
        mSend = ctx.sendTabAction;
        read(ctx);

        float slotY = area.y + 42;
        auto  addSlot = [&](float x, ItemStack const* stack, char const* name) {
            auto& s   = widgets.add(std::make_unique<StationSlot>());
            s.rect    = {x - 1, slotY, 18, 18};
            s.stack   = [stack] { return stack; };
            s.onClick = [this, name](int button, bool shift) {
                if (mSend) mSend(nlohmann::json{{"click", name}, {"button", button}, {"shift", shift}});
            };
        };
        addSlot(area.x + 4, &mLeft, "left");
        addSlot(area.x + std::floor(kWidth / 2) - 9, &mRight, "right");
        addSlot(area.x + kWidth - 2 - 3 - 18, &mResult, "result");

        // Added last: gets clicks first, so clicking elsewhere unfocuses it.
        auto& field         = widgets.add(std::make_unique<NameField>());
        mField              = &field;
        field.rect          = {area.x + 5, area.y + 25, 90, 14};
        field.box.bordered  = false;
        field.box.padding   = 0;
        field.box.backgroundArgb = 0x00000000;
        field.box.maxLength = 50;
        field.box.textColor = 0xFFFFFF;
        field.editable      = [this] { return !mLeft.isNull(); };
        field.box.setText(mName);
        field.box.onChange = [this](std::string const& text) { sendName(text); };
        mLeftKey           = leftKey();
        mDefaultName       = mLeft.isNull() ? std::string{} : mLeft.getHoverName();
        if (!mLeft.isNull() && mName.empty()) adoptLeftName();
        return {kWidth, kHeight};
    }

    void update(::abp::client::ui::TabContext const& ctx) override {
        mSend = ctx.sendTabAction;
        read(ctx);
        if (!mField) return;
        // SB nameChangeListener: the box follows the first slot (its item's name when an item
        // is put in, empty when it is taken out).
        auto key = leftKey();
        if (key != mLeftKey) {
            mLeftKey = key;
            if (mLeft.isNull()) {
                mDefaultName.clear();
                mField->box.setText({});
            } else {
                adoptLeftName();
            }
        }
    }

    void render(Painter& p, ::abp::client::ui::TabContext const&, Rect area, float, float) override {
        float slotY   = area.y + 43;
        float firstX  = area.x + 4;
        float secondX = area.x + std::floor(kWidth / 2) - 9;
        float resultX = area.x + kWidth - 2 - 3 - 18;
        float plusX   = firstX + 18 + std::floor((secondX - (firstX + 18)) / 2) - 6 - 1;
        p.pixels(plusSign(), plusX, slotY + 2);
        float arrowX = secondX + 18 + std::floor((resultX - (secondX + 18)) / 2) - 7 - 1;
        p.pixels(arrow(), arrowX, slotY + 1);
        if (!mLeft.isNull() && mResult.isNull()) p.pixels(redCross(), arrowX, slotY + 1, 1, Painter::Layer::Foreground);
        renderCost(p, area.x + 3, area.y + 62);
    }

private:
    void read(::abp::client::ui::TabContext const& ctx) {
        auto it = ctx.upgrade.tab.find("anvil");
        if (it == ctx.upgrade.tab.end() || !it->is_object()) return;
        auto const& a = *it;
        mLeft         = stackFrom(a.value("left", nlohmann::json()));
        mRight        = stackFrom(a.value("right", nlohmann::json()));
        mResult       = stackFrom(a.value("result", nlohmann::json()));
        mCost         = a.value("cost", 0);
        mTooExpensive = a.value("tooExpensive", false);
        mCanTake      = a.value("canTake", false);
        mName         = a.value("name", std::string{});
    }

    /// Identity of the first slot's item (kind + data, not the count).
    [[nodiscard]] std::string leftKey() const {
        if (mLeft.isNull()) return {};
        auto d = game::ItemConv::toData(mLeft);
        return d.id + ":" + std::to_string(d.aux) + ":" + std::to_string(d.nbtHash());
    }

    /// The first slot got a new item: the box shows its name as this client names it.
    void adoptLeftName() {
        mDefaultName = mLeft.getHoverName();
        mField->box.setText(mDefaultName);
        sendName(mField->box.text());
    }

    void sendName(std::string const& text) {
        if (!mSend || mLeft.isNull()) return;
        mSend(nlohmann::json{{"name", text}, {"default", mDefaultName}});
    }

    /// SB AnvilUpgradeTab.renderCost.
    void renderCost(Painter& p, float x, float y) {
        if (mCost <= 0) return;
        std::string text;
        std::uint32_t color = 8453920; // 0x80FF20
        if (mTooExpensive) {
            text  = ctr("abp.anvil.too_expensive", "Too Expensive!");
            color = 16736352; // 0xFF6060
        } else if (mResult.isNull()) {
            return;
        } else {
            text = ctr("abp.anvil.cost", "Enchantment Cost: {}", mCost);
            if (!mCanTake) color = 16736352;
        }
        float maxWidth = kWidth - 9;
        auto  lines    = wrap(p, text, maxWidth);
        p.fillRect({x, y, maxWidth, static_cast<float>(lines.size()) * 12}, Color::argb(1325400064u)); // 0x4F000000
        float yOffset = 0;
        for (auto const& line : lines) {
            float w = p.textWidth(line);
            p.text(line, x + 2 + (maxWidth - w) / 2, y + 2 + yOffset, Color::rgb(color), true);
            yOffset += 12;
        }
    }

    std::function<void(nlohmann::json)> mSend;
    NameField*                          mField = nullptr; ///< owned by the tab's widget group (valid while open)
    ItemStack                           mLeft, mRight, mResult;
    int                                 mCost         = 0;
    bool                                mTooExpensive = false;
    bool                                mCanTake      = false;
    std::string                         mName;
    std::string                         mDefaultName;
    std::string                         mLeftKey;
};

} // namespace
} // namespace abp::upgrade::anvil::client

ABP_REGISTER_NATIVE_TAB("anvil", [] { return std::make_unique<abp::upgrade::anvil::client::AnvilTab>(); });

#endif // ABP_CLIENT
