#ifdef ABP_CLIENT

#include "client/ui/tabs/stonecutter/LargeResultSelector.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

#include "client/Client.h"
#include "client/net/ClientNet.h"
#include "client/ui/UiRoot.h"
#include "client/ui/gfx/Painter.h"
#include "client/ui/tabs/crafting/StationWidgets.h"
#include "client/ui/widgets/SbStyle.h"
#include "client/ui/widgets/TextBox.h"
#include "client/ui/widgets/Tooltip.h"
#include "mod/Log.h"

namespace abp::client::ui::station {

namespace {

// SC LargeResultSelector geometry.
constexpr int kColumns          = 9;
constexpr int kRows             = 5;
constexpr int kCellW            = 16;
constexpr int kCellH            = 18;
constexpr int kSearchHeight     = 10;
constexpr int kSearchIcon       = 16;
constexpr int kPadding          = 8;
constexpr int kSearchToResults  = 6;
constexpr int kResultsTop       = kPadding + kSearchHeight + kSearchToResults;
constexpr int kResultsW         = kColumns * kCellW;
constexpr int kResultsH         = kRows * kCellH;
constexpr int kScrollbarW       = 14;
constexpr int kScrollbarSpacing = 4;
constexpr int kSearchBoxW       = kResultsW + kScrollbarSpacing + kScrollbarW - kSearchIcon;
constexpr int kWidth            = kPadding + kResultsW + kScrollbarSpacing + kScrollbarW + kPadding - 2;
constexpr int kHeight           = kResultsTop + kResultsH + kPadding;
constexpr int kSliderH          = 15;
constexpr int VK_ESCAPE_        = 0x1B;

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

class SelectorLayer final : public IUiLayer {
public:
    void open(std::vector<ResultEntry> results, int selected, std::function<void(int)> onSelect) {
        auto const* m = net::screenModel();
        if (!m) return;
        mSession  = m->session;
        mAll      = std::move(results);
        mSelected = selected;
        mOnSelect = std::move(onSelect);
        mNames.clear();
        for (auto const& e : mAll) mNames.push_back(e.stack.isNull() ? std::string{} : lower(e.stack.getHoverName()));
        mSearch                = TextBox{};
        mSearch.bordered       = false;
        mSearch.padding        = 1;
        mSearch.backgroundArgb = 0xFF777777;
        mSearch.maxLength      = 50;
        mSearch.onChange       = [this](std::string const& text) { updateFilter(text); };
        mSearch.setFocused(true); // SC: the search box has focus when the list opens
        mOpen        = true;
        mDragging    = false;
        mRealMouse   = {UiRoot::get().lastFrame().mouseX, UiRoot::get().lastFrame().mouseY};
        updateFilter({});
        log().info("companion: stonecutter expanded output list opened ({} results)", mAll.size());
    }

    void close() {
        if (!mOpen) return;
        mOpen = false;
        mSearch.setFocused(false);
        UiRoot::get().setMouse(mRealMouse.first, mRealMouse.second);
    }

    [[nodiscard]] bool isOpen() const { return mOpen; }

    // ---- IUiLayer -------------------------------------------------------------------------------
    [[nodiscard]] char const* name() const override { return "abp.station.large_result_selector"; }
    [[nodiscard]] int         zOrder() const override { return 1100; }
    [[nodiscard]] bool        visible(FrameInfo const& frame) const override {
        if (!mOpen) return false;
        auto const* m = net::screenModel();
        if (!m || m->session != mSession) {
            const_cast<SelectorLayer*>(this)->close(); // the screen went away
            return false;
        }
        // The backpack screen under the modal must not show hover tooltips (SC skips them while a
        // modal overlay is open): it renders first, so hide the cursor from it this frame.
        if (frame.mouseX > -1000) mRealMouse = {frame.mouseX, frame.mouseY};
        UiRoot::get().setMouse(-10000, -10000);
        return true;
    }
    [[nodiscard]] bool contains(float, float) const override { return mOpen; }
    [[nodiscard]] bool wantsTextInput() const override { return mOpen && mSearch.focused(); }
    [[nodiscard]] bool capturesKey(int) const override { return mOpen; }

    void render(Painter& p, FrameInfo const& f) override {
        float mx = mRealMouse.first, my = mRealMouse.second;
        mX       = std::floor((f.screen.w - kWidth) / 2);
        mY       = std::floor((f.screen.h - kHeight) / 2);
        p.flush();
        p.fillRect(f.screen, Color::argb(0x99000000));
        sb::panel(p, {mX, mY, static_cast<float>(kWidth), static_cast<float>(kHeight)});
        float rx = resultsX(), ry = resultsY();
        listBackground(p, {rx - 1, ry - 1, kResultsW + 2.f, kResultsH + 2.f});
        scrollBackground(p, {scrollX() - 1, ry - 1, static_cast<float>(kScrollbarW), kResultsH + 2.f});
        int maxIndex = std::min(mOffset + kColumns * kRows, static_cast<int>(mFiltered.size()));
        for (int i = mOffset; i < maxIndex; ++i) {
            int   v  = i - mOffset;
            float cx = rx + static_cast<float>(v % kColumns * kCellW);
            float cy = ry + static_cast<float>(v / kColumns * kCellH);
            auto const& e = mAll[static_cast<std::size_t>(mFiltered[static_cast<std::size_t>(i)])];
            RecipeBg    bg = RecipeBg::Normal;
            if (e.index == mSelected) bg = RecipeBg::Selected;
            else if (mx >= cx && my >= cy + 1 && mx < cx + 16 && my < cy + 19) bg = RecipeBg::Hovered;
            else if (e.recent) bg = RecipeBg::Recent;
            recipeBackground(p, cx, cy, bg);
        }
        for (int i = mOffset; i < maxIndex; ++i) {
            int   v  = i - mOffset;
            float cx = rx + static_cast<float>(v % kColumns * kCellW);
            float cy = ry + static_cast<float>(v / kColumns * kCellH) + 1;
            auto const& s = mAll[static_cast<std::size_t>(mFiltered[static_cast<std::size_t>(i)])].stack;
            if (!s.isNull()) {
                p.item(s, cx, cy);
                drawCount(p, s.mCount, cx, cy);
            }
        }
        float sliderY = ry + static_cast<float>(static_cast<int>((kResultsH - kSliderH) * mProgress));
        slider(p, scrollX(), sliderY, canScroll());
        p.flush();
        // Search box (SC ResultSearchBox: no border, #777777, grey text until focused).
        Rect box{searchX(), searchY(), static_cast<float>(kSearchBoxW), static_cast<float>(kSearchHeight)};
        mSearch.rect        = box;
        mSearch.textColor   = mSearch.focused() ? 0xFFFFFF : 0xBBBBBB;
        mSearch.placeholder = mSearch.focused() ? std::string{} : ctr("abp.stonecutter.search_hint", "Search");
        p.fillRect(box, sb::color::SEARCH_BG);
        mSearch.render(p, f.time);
        icon(p, Icon::Search, box.right(), box.y - static_cast<float>(kSearchIcon - kSearchHeight) / 2);
        p.flush();
        // Tooltips (SC renderTooltip).
        std::vector<TooltipLine> lines;
        Rect                     iconRect{box.right(), box.y - static_cast<float>(kSearchIcon - kSearchHeight) / 2, kSearchIcon, kSearchIcon};
        if (iconRect.contains(mx, my)) {
            lines.push_back({ctr("abp.stonecutter.search_icon", "Filter outputs by name"), 0xFFFFFF});
        } else if (int i = resultAt(mx, my); i >= 0) {
            lines = itemTooltipLines(mAll[static_cast<std::size_t>(mFiltered[static_cast<std::size_t>(i)])].stack);
        } else if (!mSearch.focused() && box.contains(mx, my)) {
            lines.push_back({ctr("abp.stonecutter.search_box", "Click to search"), 0xFFFFFF});
        }
        if (!lines.empty()) drawTooltip(p, lines, mx, my);
    }

    bool onMouseButton(MouseButton button, bool down, float x, float y) override {
        if (!mOpen) return false;
        mRealMouse = {x, y};
        if (!down) {
            mDragging = false;
            return true;
        }
        Rect box{searchX(), searchY(), static_cast<float>(kSearchBoxW), static_cast<float>(kSearchHeight)};
        if (box.contains(x, y)) {
            if (button == MouseButton::Left) mSearch.setFocused(true);
            else if (button == MouseButton::Right) mSearch.setText({}, true);
            return true;
        }
        if (int i = resultAt(x, y); i >= 0) {
            int index = mAll[static_cast<std::size_t>(mFiltered[static_cast<std::size_t>(i)])].index;
            auto fn   = mOnSelect;
            close();
            if (fn) fn(index);
            return true;
        }
        float sx = scrollX(), ry = resultsY();
        if (x >= sx && x < sx + kScrollbarW && y >= ry && y < ry + kResultsH) {
            mDragging = true;
            drag(y);
            return true;
        }
        Rect panel{mX, mY, static_cast<float>(kWidth), static_cast<float>(kHeight)};
        if (!panel.contains(x, y)) close(); // SC: a click outside closes the overlay
        return true;
    }

    bool onMouseMove(float x, float y) override {
        if (!mOpen) return false;
        mRealMouse = {x, y};
        if (mDragging) drag(y);
        return false; // others keep tracking the mouse (the real position is restored on close)
    }

    bool onScroll(float, float, float delta) override {
        if (!mOpen) return false;
        if (canScroll()) {
            int hidden = hiddenRows();
            mProgress  = std::clamp(mProgress - delta / static_cast<float>(hidden), 0.f, 1.f);
            mOffset    = static_cast<int>(mProgress * static_cast<float>(hidden) + 0.5f) * kColumns;
        }
        return true;
    }

    bool onKey(int vk, bool down) override {
        if (!mOpen) return false;
        if (vk == VK_ESCAPE_ && down) {
            close();
            return true;
        }
        if (mSearch.focused()) mSearch.onKey(vk, down);
        return true;
    }

    bool onChar(std::string const& utf8) override {
        if (!mOpen || !mSearch.focused()) return false;
        mSearch.onChar(utf8);
        return true;
    }

private:
    [[nodiscard]] float resultsX() const { return mX + kPadding; }
    [[nodiscard]] float resultsY() const { return mY + kResultsTop; }
    [[nodiscard]] float scrollX() const { return resultsX() + kResultsW + kScrollbarSpacing; }
    [[nodiscard]] float searchX() const { return mX + kPadding - 1; }
    [[nodiscard]] float searchY() const { return mY + kPadding; }
    [[nodiscard]] bool  canScroll() const { return static_cast<int>(mFiltered.size()) > kColumns * kRows; }
    [[nodiscard]] int   hiddenRows() const { return (static_cast<int>(mFiltered.size()) + kColumns - 1) / kColumns - kRows; }

    [[nodiscard]] int resultAt(float x, float y) const {
        int maxIndex = std::min(mOffset + kColumns * kRows, static_cast<int>(mFiltered.size()));
        for (int i = mOffset; i < maxIndex; ++i) {
            int   v  = i - mOffset;
            float cx = resultsX() + static_cast<float>(v % kColumns * kCellW);
            float cy = resultsY() + static_cast<float>(v / kColumns * kCellH) + 1;
            if (x >= cx && y >= cy && x < cx + 16 && y < cy + 18) return i;
        }
        return -1;
    }

    void drag(float y) {
        if (!canScroll()) return;
        mProgress = std::clamp((y - resultsY() - kSliderH / 2.f) / static_cast<float>(kResultsH - kSliderH), 0.f, 1.f);
        mOffset   = static_cast<int>(mProgress * static_cast<float>(hiddenRows()) + 0.5f) * kColumns;
    }

    void updateFilter(std::string const& phrase) {
        auto needle = lower(phrase);
        while (!needle.empty() && std::isspace(static_cast<unsigned char>(needle.back()))) needle.pop_back();
        while (!needle.empty() && std::isspace(static_cast<unsigned char>(needle.front()))) needle.erase(needle.begin());
        mFiltered.clear();
        for (int i = 0; i < static_cast<int>(mAll.size()); ++i) {
            if (needle.empty() || mNames[static_cast<std::size_t>(i)].find(needle) != std::string::npos) mFiltered.push_back(i);
        }
        // SC scrollSelectedResultIntoView.
        int sel = -1;
        for (int i = 0; i < static_cast<int>(mFiltered.size()); ++i) {
            if (mAll[static_cast<std::size_t>(mFiltered[static_cast<std::size_t>(i)])].index == mSelected) sel = i;
        }
        int hidden = hiddenRows();
        if (sel < 0 || hidden <= 0) {
            mOffset   = 0;
            mProgress = 0;
            return;
        }
        int first = std::clamp(sel / kColumns - kRows / 2, 0, hidden);
        mOffset   = first * kColumns;
        mProgress = static_cast<float>(first) / static_cast<float>(hidden);
    }

    bool                         mOpen     = false;
    std::uint32_t                mSession  = 0;
    std::vector<ResultEntry>     mAll;
    std::vector<std::string>     mNames;
    std::vector<int>             mFiltered;
    int                          mSelected = -1;
    std::function<void(int)>     mOnSelect;
    TextBox                      mSearch;
    int                          mOffset   = 0;
    float                        mProgress = 0;
    bool                         mDragging = false;
    float                        mX = 0, mY = 0;
    mutable std::pair<float, float> mRealMouse{-1, -1};
};

SelectorLayer gSelector;
ABP_REGISTER_UI_LAYER(gSelector);

} // namespace

void openLargeResultSelector(std::vector<ResultEntry> results, int selectedIndex, std::function<void(int index)> onSelect) {
    gSelector.open(std::move(results), selectedIndex, std::move(onSelect));
}

void closeLargeResultSelector() { gSelector.close(); }

bool largeResultSelectorOpen() { return gSelector.isOpen(); }

} // namespace abp::client::ui::station

#endif // ABP_CLIENT
