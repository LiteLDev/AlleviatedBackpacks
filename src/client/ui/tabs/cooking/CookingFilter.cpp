#ifdef ABP_CLIENT

#include "client/ui/tabs/cooking/CookingFilter.h"

#include <algorithm>

#include "client/Client.h"
#include "client/ui/tabs/crafting/StationWidgets.h"
#include "client/ui/widgets/SbStyle.h"
#include "core/ItemStackData.h"

namespace abp::client::ui::station {

namespace {

constexpr std::uint32_t kTagColor      = 16383998; ///< SC TAG_FONT_COLOR
constexpr std::uint32_t kMoreTagsColor = 13882323; ///< SC MORE_TAGS_FONT_COLOR
constexpr float         kMaxTagWidth   = 68;       ///< SC MAX_TAG_NAME_WIDTH

/// SC's tag list in "Match Tags" mode.
class TagList final : public Widget {
public:
    FilterControl const* f = nullptr;

    void render(Painter& p, float, float, double) override {
        if (!visible) return;
        listBackground(p, rect);
        int maxLines = static_cast<int>(rect.h) / 10;
        int n        = static_cast<int>(f->tags.size());
        for (int i = 0; i < n && i < maxLines; ++i) {
            if (n > maxLines && i == maxLines - 1) {
                p.text(ctr("abp.cooking.filter.tag_list.tag_overflow", "+ {} more", n - (maxLines - 1)), rect.x + 2, rect.y + 2 + static_cast<float>(i * 10), Color::rgb(kMoreTagsColor));
                break;
            }
            auto name = f->tags[static_cast<std::size_t>(i)];
            if (p.textWidth(name) > kMaxTagWidth) {
                // SC: "..." + the end of the name.
                std::string tail = name;
                while (!tail.empty() && p.textWidth("..." + tail) > kMaxTagWidth) tail.erase(tail.begin());
                name = "..." + tail;
            }
            p.text(name, rect.x + 2, rect.y + 2 + static_cast<float>(i * 10), Color::rgb(kTagColor));
        }
    }

    void tooltip(float x, float y, std::vector<TooltipLine>& out) const override {
        if (!hovered(x, y)) return;
        out.push_back({ctr("abp.cooking.filter.tag_list.title", "Tags"), 0xFFFFFF});
        if (f->tags.empty()) {
            out.push_back({ctr("abp.cooking.filter.tag_list.empty", "No tags selected"), 0x555555});
            return;
        }
        for (auto const& t : f->tags) out.push_back({"> " + t, 0xAAAAAA});
    }
};

/// SC TagButton: an 18x18 button that also scrolls through its selection.
class TagButton final : public StationButton {
public:
    std::function<void(float delta)> onScroll;

    void render(Painter& p, float mx, float my, double t) override {
        if (!visible) return;
        sb::buttonBackground(p, rect, hovered(mx, my));
        if (icon) icon(p, rect.x, rect.y);
        (void)t;
    }
    bool scroll(float x, float y, float delta) override {
        if (!hovered(x, y)) return false;
        if (onScroll) onScroll(delta);
        return true;
    }
};

int rowsOf(FilterControl const& f) { return f.slots / f.perRow + (f.slots % f.perRow > 0 ? 1 : 0); }

} // namespace

void FilterControl::setData(nlohmann::json const& j) {
    if (!j.is_object()) return;
    mode       = j.value("mode", 0);
    mMatchBy   = j.value("matchBy", 0);
    nbt        = j.value("nbt", false);
    durability = j.value("dur", false);
    slots      = std::max(1, j.value("slots", 4));
    perRow     = std::max(1, j.value("perRow", 4));
    ghosts.assign(static_cast<std::size_t>(slots), ItemStack{});
    if (auto g = j.find("ghosts"); g != j.end() && g->is_array()) {
        for (std::size_t i = 0; i < g->size() && i < ghosts.size(); ++i) ghosts[i] = stackFromJson((*g)[i]);
    }
    tags.clear();
    if (auto t = j.find("tags"); t != j.end() && t->is_array()) {
        for (auto const& e : *t) {
            if (e.is_string()) tags.push_back(e.get<std::string>());
        }
    }
    tagsToAdd.clear();
    if (auto t = j.find("tagsToAdd"); t != j.end() && t->is_array()) {
        for (auto const& e : *t) {
            if (e.is_string()) tagsToAdd.push_back(e.get<std::string>());
        }
    }
    tagItem        = stackFromJson(j.value("tagItem", nlohmann::json()));
    selectedAdd    = std::clamp(selectedAdd, 0, std::max(0, static_cast<int>(tagsToAdd.size()) - 1));
    selectedRemove = std::clamp(selectedRemove, 0, std::max(0, static_cast<int>(tags.size()) - 1));
}

float FilterControl::height() const {
    float top = mAdvanced ? 21.f : 0.f;
    return top + static_cast<float>(rowsOf(*this) * 18); // TAGS: top + (rows-1)*18 + 18, the same
}

float FilterControl::width() const { return std::max(static_cast<float>(perRow * 18), mAdvanced ? 72.f : 0.f); }

float FilterControl::build(WidgetGroup& widgets, float x, float y, std::function<void(nlohmann::json)> send) {
    mDur = mNbt = mGrid = mList = mTagSlot = mAdd = mRemove = nullptr;
    auto which = mWhich;
    float top   = mAdvanced ? 21.f : 0.f;
    auto  op    = [send, which](char const* name) {
        if (send) send({{"filter", which}, {"op", name}});
    };
    if (mAdvanced) {
        auto& allow   = widgets.add(std::make_unique<ToggleButton>());
        allow.size    = Button::Size::Normal;
        allow.rect    = {x, y, 18, 18};
        allow.states  = {
            {[](Painter& p, float bx, float by) { icon(p, Icon::Allow, bx + 1, by + 1); }, {{ctr("abp.cooking.filter.allow", "Allow"), 0xFFFFFF}}},
            {[](Painter& p, float bx, float by) { icon(p, Icon::Block, bx + 1, by + 1); }, {{ctr("abp.cooking.filter.block", "Block"), 0xFFFFFF}}},
        };
        allow.current = [this] { return mode == 1 ? 1 : 0; };
        allow.onClick = [this, op](int) {
            mode = mode == 1 ? 0 : 1;
            op("allowList");
        };
        auto& primary   = widgets.add(std::make_unique<ToggleButton>());
        primary.size    = Button::Size::Normal;
        primary.rect    = {x + 18, y, 18, 18};
        primary.states  = {
            {[](Painter& p, float bx, float by) { icon(p, Icon::MatchItem, bx + 1, by + 1); }, {{ctr("abp.cooking.filter.match_item", "Match Item"), 0xFFFFFF}}},
            {[](Painter& p, float bx, float by) { icon(p, Icon::MatchMod, bx + 1, by + 1); }, {{ctr("abp.cooking.filter.match_mod", "Match Mod"), 0xFFFFFF}}},
            {[](Painter& p, float bx, float by) { icon(p, Icon::MatchTags, bx + 1, by + 1); }, {{ctr("abp.cooking.filter.match_tags", "Match Tags"), 0xFFFFFF}}},
        };
        primary.current = [this] { return std::clamp(mMatchBy, 0, 2); };
        primary.onClick = [this, op](int) {
            mMatchBy = (mMatchBy + 1) % 3;
            op("primaryMatch");
        };
        auto& dur   = widgets.add(std::make_unique<ToggleButton>());
        dur.size    = Button::Size::Normal;
        dur.rect    = {x + 36, y, 18, 18};
        dur.states  = {
            {[](Painter& p, float bx, float by) { icon(p, Icon::MatchDurability, bx + 1, by + 1); },
             {{ctr("abp.cooking.filter.match_durability", "Match Durability"), 0xFFFFFF}}},
            {[](Painter& p, float bx, float by) { icon(p, Icon::IgnoreDurability, bx + 1, by + 1); },
             {{ctr("abp.cooking.filter.ignore_durability", "Ignore Durability"), 0xFFFFFF}}},
        };
        dur.current = [this] { return durability ? 0 : 1; };
        dur.onClick = [this, op](int) {
            durability = !durability;
            op("durability");
        };
        auto& nbtButton   = widgets.add(std::make_unique<ToggleButton>());
        nbtButton.size    = Button::Size::Normal;
        nbtButton.rect    = {x + 54, y, 18, 18};
        nbtButton.states  = {
            {[](Painter& p, float bx, float by) { sb::icon(p, sb::Icon::MatchNbt, bx + 1, by + 1); }, {{ctr("abp.cooking.filter.match_nbt", "Match NBT"), 0xFFFFFF}}},
            {[](Painter& p, float bx, float by) { sb::icon(p, sb::Icon::IgnoreNbt, bx + 1, by + 1); }, {{ctr("abp.cooking.filter.ignore_nbt", "Ignore NBT"), 0xFFFFFF}}},
        };
        nbtButton.current = [this] { return nbt ? 0 : 1; };
        nbtButton.onClick = [this, op](int) {
            nbt = !nbt;
            op("nbt");
        };
        mDur = &dur;
        mNbt = &nbtButton;
    }

    // Filter slots (hidden in "Match Tags" mode).
    auto& grid   = widgets.add(std::make_unique<SlotGrid>());
    grid.columns = perRow;
    grid.count   = slots;
    grid.rect    = {x, y + top, static_cast<float>(perRow * 18), static_cast<float>(rowsOf(*this) * 18)};
    grid.item    = [this](int i) { return i < static_cast<int>(ghosts.size()) ? &ghosts[static_cast<std::size_t>(i)] : nullptr; };
    grid.onClick = [send, which](int i, int button, bool) {
        if (send) send({{"filter", which}, {"ghost", i}, {"button", button}});
    };
    mGrid = &grid;

    if (mAdvanced) {
        float tagOff  = top + static_cast<float>((rowsOf(*this) - 1) * 18);
        auto& list    = widgets.add(std::make_unique<TagList>());
        list.f        = this;
        list.rect     = {x, y + top, static_cast<float>(perRow * 18), static_cast<float>((rowsOf(*this) - 1) * 18)};
        mList         = &list;
        auto& slot    = widgets.add(std::make_unique<ItemSlot>());
        slot.rect     = {x, y + tagOff, 18, 18};
        slot.item     = [this] { return &tagItem; };
        slot.onClick  = [send, which](int, bool) {
            if (send) send({{"filter", which}, {"tagItem", true}});
        };
        mTagSlot = &slot;
        auto& add    = widgets.add(std::make_unique<TagButton>());
        add.rect     = {x + 18, y + tagOff, 18, 18};
        add.icon     = [](Painter& p, float bx, float by) { icon(p, Icon::AddTag, bx + 1, by + 1); };
        add.onClick  = [this, send, which](int button) {
            if (button != 0 || tagsToAdd.empty() || !send) return;
            send({{"filter", which}, {"op", "addTag"}, {"tag", tagsToAdd[static_cast<std::size_t>(std::clamp(selectedAdd, 0, static_cast<int>(tagsToAdd.size()) - 1))]}});
        };
        add.onScroll = [this](float delta) {
            int n = static_cast<int>(tagsToAdd.size());
            if (n > 0) selectedAdd = (selectedAdd + (delta < 0 ? 1 : n - 1)) % n;
        };
        add.dynamicTooltip = [this] {
            std::vector<TooltipLine> out{{ctr("abp.cooking.filter.add_tag", "Add Tag"), 0xFFFFFF}};
            if (tagItem.isNull()) {
                out.push_back({ctr("abp.cooking.filter.add_tag.no_item", "<- Click an Item into slot to start adding its tags"), 0x555555});
                return out;
            }
            for (int i = 0; i < static_cast<int>(tagsToAdd.size()); ++i) {
                if (i == selectedAdd) out.push_back({"-> " + tagsToAdd[static_cast<std::size_t>(i)], 0x55FF55});
                else out.push_back({"> " + tagsToAdd[static_cast<std::size_t>(i)], 0xAAAAAA});
            }
            if (tagsToAdd.empty()) out.push_back({ctr("abp.cooking.filter.add_tag.no_additional_tags", "All Item's tags are already added"), 0xFFFF55});
            else out.push_back({ctr("abp.cooking.filter.add_tag.controls", "Scroll to Select, Click to Add"), 0x555555});
            return out;
        };
        mAdd = &add;
        auto& rem    = widgets.add(std::make_unique<TagButton>());
        rem.rect     = {x + 36, y + tagOff, 18, 18};
        rem.icon     = [](Painter& p, float bx, float by) { icon(p, Icon::RemoveTag, bx + 1, by + 1); };
        rem.onClick  = [this, send, which](int button) {
            if (button != 0 || tags.empty() || !send) return;
            send({{"filter", which}, {"op", "removeTag"}, {"tag", tags[static_cast<std::size_t>(std::clamp(selectedRemove, 0, static_cast<int>(tags.size()) - 1))]}});
        };
        rem.onScroll = [this](float delta) {
            int n = static_cast<int>(tags.size());
            if (n > 0) selectedRemove = (selectedRemove + (delta < 0 ? 1 : n - 1)) % n;
        };
        rem.dynamicTooltip = [this] {
            std::vector<TooltipLine> out{{ctr("abp.cooking.filter.remove_tag", "Remove Tag"), 0xFFFFFF}};
            if (tags.empty()) {
                out.push_back({ctr("abp.cooking.filter.remove_tag.empty", "No tags to remove"), 0xFF5555});
                return out;
            }
            for (int i = 0; i < static_cast<int>(tags.size()); ++i) {
                if (i == selectedRemove) out.push_back({"-> " + tags[static_cast<std::size_t>(i)], 0xFF5555});
                else out.push_back({"> " + tags[static_cast<std::size_t>(i)], 0xAAAAAA});
            }
            out.push_back({ctr("abp.cooking.filter.remove_tag.controls", "Scroll to Select, Click to Remove"), 0x555555});
            return out;
        };
        mRemove = &rem;
    }
    refresh();
    return height();
}

void FilterControl::refresh() {
    bool t = tagsMode();
    if (mGrid) mGrid->visible = !t;
    if (mDur) mDur->visible = !t;
    if (mNbt) mNbt->visible = !t;
    if (mList) mList->visible = t;
    if (mTagSlot) mTagSlot->visible = t;
    if (mAdd) mAdd->visible = t;
    if (mRemove) mRemove->visible = t;
}

} // namespace abp::client::ui::station

#endif // ABP_CLIENT
