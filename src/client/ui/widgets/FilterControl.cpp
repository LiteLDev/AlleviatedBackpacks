#ifdef ABP_CLIENT

#include "client/ui/widgets/FilterControl.h"

#include <algorithm>

#include "mc/client/player/LocalPlayer.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/item/ItemStack.h"

#include "client/Client.h"
#include "client/ui/widgets/SbStyle.h"
#include "game/ItemConv.h"

namespace abp::client::ui {

namespace {
/// SC FilterLogicControlBase colours / sizes.
constexpr std::uint32_t kTagFontColor      = 16383998; // 0xF9FFFE
constexpr std::uint32_t kMoreTagsFontColor = 13882323; // 0xD3D3D3
constexpr float         kMaxTagNameWidth   = 68;

int buttonIndex(MouseButton b) { return b == MouseButton::Left ? 0 : b == MouseButton::Right ? 1 : 2; }

ItemStack cursorStack() {
    if (auto* player = currentPlayer()) return player->mPlayerUIContainer->getItem(0);
    return ItemStack{};
}

FilterControl::Type typeOf(std::string const& s) {
    if (s == "allowList") return FilterControl::Type::AllowList;
    if (s == "deposit") return FilterControl::Type::Deposit;
    if (s == "none") return FilterControl::Type::None;
    return FilterControl::Type::Contents;
}

std::string tr(std::string_view key, std::string_view fallback) { return ctr(key, fallback); }
} // namespace

FilterControl::FilterControl(std::string key, Send send) : mKey(std::move(key)), mSend(std::move(send)) {}

void FilterControl::setPosition(float x, float y) {
    mX   = x;
    mY   = y;
    rect = {x, y, width(), height()};
}

void FilterControl::setState(nlohmann::json const& s) {
    if (!s.is_object()) return;
    try {
        mHasState      = true;
        mSlots         = std::clamp(s.value("slots", 9), 0, 64);
        mPerRow        = std::clamp(s.value("perRow", 3), 1, 16);
        mAdvanced      = s.value("advanced", false);
        mType          = typeOf(s.value("type", std::string("contents")));
        mTypeState     = s.value("typeState", 1);
        mPrimary       = std::clamp(s.value("primary", 0), 0, 2);
        mDurability    = s.value("dur", false);
        mNbt           = s.value("nbt", false);
        mAnyTag        = s.value("anyTag", true);
        mSlotsEnabled  = s.value("slotsEnabled", true);
        mButtonsVisible = mType != Type::None || mAdvanced;
        std::vector<std::string> tags;
        if (auto it = s.find("tags"); it != s.end() && it->is_array()) {
            for (auto const& t : *it) {
                if (t.is_string()) tags.push_back(t.get<std::string>());
            }
        }
        bool tagsChanged = tags != mTags;
        mTags            = std::move(tags);
        mItems.assign(static_cast<std::size_t>(mSlots), ItemStack{});
        if (auto it = s.find("items"); it != s.end() && it->is_array()) {
            for (int i = 0; i < mSlots && i < static_cast<int>(it->size()); ++i) {
                auto const& j = (*it)[static_cast<std::size_t>(i)];
                if (j.is_null()) continue;
                auto d = core::ItemStackData::fromJson(j);
                if (!d.empty()) mItems[static_cast<std::size_t>(i)] = game::ItemConv::toStack(d, 1);
            }
        }
        if (tagsChanged) refreshTagsToAdd();
        mSelectedToRemove = std::clamp(mSelectedToRemove, 0, std::max(0, static_cast<int>(mTags.size()) - 1));
    } catch (...) {
    }
    rect = {mX, mY, width(), height()};
    if (mLastHeight >= 0 && mLastHeight != rect.h) mSizeChanged = true;
    mLastHeight = rect.h;
}

bool FilterControl::consumeSizeChange() {
    bool c       = mSizeChanged;
    mSizeChanged = false;
    return c;
}

int FilterControl::totalRows() const {
    int full  = mSlots / mPerRow;
    int extra = mSlots % mPerRow;
    return full + (extra > 0 ? 1 : 0);
}

float FilterControl::width() const {
    // SC: max(slotsPerRow * 18, widest button extent); advanced controls reach x + 72.
    float buttons = mAdvanced ? 72.f : (mButtonsVisible ? 18.f : 0.f);
    return std::max(static_cast<float>(mPerRow) * 18, buttons);
}

float FilterControl::height() const {
    if (tagsMode()) return slotsTop() + tagListHeight() + 18;
    return static_cast<float>(totalRows()) * 18 + slotsTop();
}

Rect FilterControl::buttonRect(int column, float dy) const { return {mX + static_cast<float>(column) * 18, mY + dy, 18, 18}; }

void FilterControl::send(nlohmann::json data) const {
    data["filter"] = mKey;
    if (mSend) mSend(std::move(data));
}

void FilterControl::refreshTagsToAdd() {
    mTagsToAdd.clear();
    if (!mTagItem.isNull()) {
        auto data = game::ItemConv::toData(mTagItem);
        auto all  = game::ItemConv::tags().tagsOf(data);
        std::sort(all.begin(), all.end());
        all.erase(std::unique(all.begin(), all.end()), all.end());
        for (auto& t : all) {
            if (!t.empty() && std::find(mTags.begin(), mTags.end(), t) == mTags.end()) mTagsToAdd.push_back(std::move(t));
        }
    }
    mSelectedToAdd = std::clamp(mSelectedToAdd, 0, std::max(0, static_cast<int>(mTagsToAdd.size()) - 1));
}

void FilterControl::setTagSelection(ItemStack const& stack) {
    // SC TagSelectionSlot.mayPlace: empty, or an item with at least one tag.
    if (!stack.isNull()) {
        auto tags = game::ItemConv::tags().tagsOf(game::ItemConv::toData(stack));
        if (tags.empty()) return;
    }
    mTagItem = stack.isNull() ? ItemStack{} : stack;
    if (!mTagItem.isNull()) mTagItem.set(1);
    mSelectedToAdd = 0;
    refreshTagsToAdd();
}

FilterControl::HitResult FilterControl::hitTest(float x, float y) const {
    if (!mHasState || !visible) return {};
    if (mType != Type::None && buttonRect(0).contains(x, y)) return {Hit::Type};
    if (mAdvanced) {
        if (buttonRect(1).contains(x, y)) return {Hit::Primary};
        if (!tagsMode()) {
            if (buttonRect(2).contains(x, y)) return {Hit::Durability};
            if (buttonRect(3).contains(x, y)) return {Hit::Nbt};
        }
    }
    if (tagsMode()) {
        float ty = tagButtonsY();
        if (Rect{mX + 1, mY + ty + 1, 16, 16}.contains(x, y)) return {Hit::TagSlot};
        if (buttonRect(1, ty).contains(x, y)) return {Hit::AddTag};
        if (buttonRect(2, ty).contains(x, y)) return {Hit::RemoveTag};
        if (buttonRect(3, ty).contains(x, y)) return {Hit::AnyTag};
        Rect list{mX, mY + slotsTop(), static_cast<float>(mPerRow) * 18, tagListHeight()};
        if (list.contains(x, y)) return {Hit::TagList};
        return {};
    }
    for (int i = 0; i < mSlots; ++i) {
        float sx = mX + 1 + static_cast<float>(i % mPerRow) * 18;
        float sy = mY + slotsTop() + 1 + static_cast<float>(i / mPerRow) * 18;
        if (Rect{sx, sy, 16, 16}.contains(x, y)) return {Hit::Slot, i};
    }
    return {};
}

void FilterControl::drawButton(Painter& p, Rect const& r, bool hovered, std::function<void(Painter&, float, float)> const& icon) const {
    sb::buttonBackground(p, r, hovered);
    if (icon) icon(p, r.x, r.y);
}

void FilterControl::render(Painter& p, float mx, float my, double) {
    if (!visible || !mHasState) return;
    using filter_icons::Icon;
    auto hit  = hitTest(mx, my);
    auto icon = [](Icon i) { return [i](Painter& pp, float x, float y) { filter_icons::draw(pp, i, x, y); }; };

    // Background (SC renderBg): slots, or the tag list and the tag selection slot.
    if (!tagsMode()) {
        sb::slotsBackground(p, mX, mY + slotsTop(), mPerRow, mSlots / mPerRow, mSlots % mPerRow);
    } else {
        sb::slotsBackground(p, mX, mY + tagButtonsY(), 1, 1, 0);
        sb::controlBackground(p, {mX, mY + slotsTop(), static_cast<float>(mPerRow) * 18, tagListHeight()});
    }

    // Type button.
    if (mType != Type::None) {
        Icon i = Icon::Block;
        switch (mType) {
        case Type::AllowList:
            i = mTypeState == 0 ? Icon::Allow : Icon::Block;
            break;
        case Type::Contents:
            i = mTypeState == 0 ? Icon::Allow : mTypeState == 1 ? Icon::Block : Icon::MatchStorage;
            break;
        case Type::Deposit:
            i = mTypeState == 0 ? Icon::Allow : mTypeState == 1 ? Icon::Block : Icon::MatchInventory;
            break;
        default:
            break;
        }
        drawButton(p, buttonRect(0), hit.hit == Hit::Type, icon(i));
    }
    if (mAdvanced) {
        drawButton(p, buttonRect(1), hit.hit == Hit::Primary, icon(mPrimary == 0 ? Icon::MatchItem : mPrimary == 1 ? Icon::MatchMod : Icon::MatchTags));
        if (!tagsMode()) {
            drawButton(p, buttonRect(2), hit.hit == Hit::Durability, icon(mDurability ? Icon::MatchDurability : Icon::IgnoreDurability));
            drawButton(p, buttonRect(3), hit.hit == Hit::Nbt, icon(mNbt ? Icon::MatchNbt : Icon::IgnoreNbt));
        }
    }

    if (!tagsMode()) {
        for (int i = 0; i < mSlots; ++i) {
            float sx = mX + 1 + static_cast<float>(i % mPerRow) * 18;
            float sy = mY + slotsTop() + 1 + static_cast<float>(i / mPerRow) * 18;
            // Disabled slots (STORAGE / INVENTORY type) are not drawn (SC Slot.isActive).
            if (!mSlotsEnabled) continue;
            auto const& it = mItems[static_cast<std::size_t>(i)];
            if (!it.isNull()) p.item(it, sx, sy);
            if (hit.hit == Hit::Slot && hit.index == i) p.fillRect({sx, sy, 16, 16}, sb::color::HOVER, Painter::Layer::Foreground);
        }
        return;
    }

    // TAGS mode: tag selection slot + Add / Remove / Match Any buttons + the tag names.
    float ty = tagButtonsY();
    if (!mTagItem.isNull()) p.item(mTagItem, mX + 1, mY + ty + 1);
    if (hit.hit == Hit::TagSlot) p.fillRect({mX + 1, mY + ty + 1, 16, 16}, sb::color::HOVER, Painter::Layer::Foreground);
    drawButton(p, buttonRect(1, ty), hit.hit == Hit::AddTag, icon(Icon::AddTag));
    drawButton(p, buttonRect(2, ty), hit.hit == Hit::RemoveTag, icon(Icon::RemoveTag));
    drawButton(p, buttonRect(3, ty), hit.hit == Hit::AnyTag, icon(mAnyTag ? Icon::MatchAnyTag : Icon::MatchAllTags));

    int   maxLines = static_cast<int>(tagListHeight() / 10);
    int   count    = 0;
    float dots     = p.textWidth("...");
    int   n        = static_cast<int>(mTags.size());
    for (auto const& name : mTags) {
        float ly = mY + 23 + static_cast<float>(count) * 10;
        if (n > maxLines && count == maxLines - 1) {
            p.text(ctr("abp.filter_control.tag_list.tag_overflow", "+ {} more", n - (maxLines - 1)), mX + 2, ly, Color::rgb(kMoreTagsFontColor));
            break;
        }
        std::string shown = name;
        if (p.textWidth(name) > kMaxTagNameWidth) {
            // SC plainSubstrByWidth(name, max - "...", tail): keep the end of the name.
            std::size_t start = 0;
            while (start < name.size() && p.textWidth(name.substr(start)) > kMaxTagNameWidth - dots) ++start;
            if (start > 0) shown = "..." + name.substr(start);
        }
        p.text(shown, mX + 2, ly, Color::rgb(kTagFontColor));
        ++count;
        if (count >= maxLines) break;
    }
}

bool FilterControl::mouseDown(MouseButton button, float x, float y) {
    auto hit = hitTest(x, y);
    int  b   = buttonIndex(button);
    switch (hit.hit) {
    case Hit::Type:
        send({{"op", "type"}, {"button", b}});
        return true;
    case Hit::Primary: {
        send({{"op", "primary"}, {"button", b}});
        return true;
    }
    case Hit::Durability:
        send({{"op", "dur"}});
        return true;
    case Hit::Nbt:
        send({{"op", "nbt"}});
        return true;
    case Hit::Slot:
        if (mSlotsEnabled) send({{"op", "slot"}, {"index", hit.index}, {"button", b}});
        return true;
    case Hit::TagSlot:
        setTagSelection(cursorStack());
        return true;
    case Hit::AddTag:
        if (mSelectedToAdd >= 0 && mSelectedToAdd < static_cast<int>(mTagsToAdd.size())) {
            auto tag = mTagsToAdd[static_cast<std::size_t>(mSelectedToAdd)];
            send({{"op", "addTag"}, {"tag", tag}});
            mSelectedToRemove = 0;
            mTagsToAdd.erase(mTagsToAdd.begin() + mSelectedToAdd);
            mSelectedToAdd = std::max(0, mSelectedToAdd - 1);
        }
        return true;
    case Hit::RemoveTag:
        if (mSelectedToRemove >= 0 && mSelectedToRemove < static_cast<int>(mTags.size())) {
            auto tag = mTags[static_cast<std::size_t>(mSelectedToRemove)];
            send({{"op", "removeTag"}, {"tag", tag}});
            if (!mTagItem.isNull() && game::ItemConv::tags().hasTag(game::ItemConv::toData(mTagItem), tag)
                && std::find(mTagsToAdd.begin(), mTagsToAdd.end(), tag) == mTagsToAdd.end()) {
                mTagsToAdd.insert(std::lower_bound(mTagsToAdd.begin(), mTagsToAdd.end(), tag), tag);
            }
            mSelectedToRemove = std::max(0, mSelectedToRemove - 1);
        }
        return true;
    case Hit::AnyTag:
        send({{"op", "anyTag"}});
        return true;
    case Hit::TagList:
        return true;
    case Hit::None:
    default:
        return hovered(x, y);
    }
}

bool FilterControl::scroll(float x, float y, float delta) {
    auto hit = hitTest(x, y);
    // SC TagButton.mouseScrolled: down (delta < 0) = next, up = previous.
    auto step = [&](int& selected, int size) {
        if (size <= 0) return;
        if (delta < 0) selected = selected + 1 >= size ? 0 : selected + 1;
        else selected = selected == 0 ? size - 1 : selected - 1;
    };
    if (hit.hit == Hit::AddTag) {
        step(mSelectedToAdd, static_cast<int>(mTagsToAdd.size()));
        return true;
    }
    if (hit.hit == Hit::RemoveTag) {
        step(mSelectedToRemove, static_cast<int>(mTags.size()));
        return true;
    }
    return false;
}

std::vector<TooltipLine> FilterControl::addTagTooltip() const {
    std::vector<TooltipLine> out{{tr("abp.filter_control.add_tag", "Add Tag")}};
    if (mTagItem.isNull()) {
        out.push_back({"§8§o" + tr("abp.filter_control.add_tag.no_item", "<- Click an Item into slot to start adding its tags")});
        return out;
    }
    for (int i = 0; i < static_cast<int>(mTagsToAdd.size()); ++i) {
        auto const& t = mTagsToAdd[static_cast<std::size_t>(i)];
        out.push_back({i == mSelectedToAdd ? "§a-> " + t : "§7> " + t});
    }
    if (mTagsToAdd.empty()) out.push_back({"§e§o" + tr("abp.filter_control.add_tag.no_additional_tags", "All Item's tags are already added")});
    else out.push_back({"§8§o" + tr("abp.filter_control.add_tag.controls", "Scroll to Select, Click to Add")});
    return out;
}

std::vector<TooltipLine> FilterControl::removeTagTooltip() const {
    std::vector<TooltipLine> out{{tr("abp.filter_control.remove_tag", "Remove Tag")}};
    if (mTags.empty()) {
        out.push_back({"§c" + tr("abp.filter_control.remove_tag.empty", "No tags to remove")});
        return out;
    }
    for (int i = 0; i < static_cast<int>(mTags.size()); ++i) {
        auto const& t = mTags[static_cast<std::size_t>(i)];
        out.push_back({i == mSelectedToRemove ? "§c-> " + t : "§7> " + t});
    }
    out.push_back({"§8§o" + tr("abp.filter_control.remove_tag.controls", "Scroll to Select, Click to Remove")});
    return out;
}

std::vector<TooltipLine> FilterControl::tagListTooltip() const {
    std::vector<TooltipLine> out{{tr("abp.filter_control.tag_list.title", "Tags")}};
    if (mTags.empty()) {
        out.push_back({"§8" + tr("abp.filter_control.tag_list.empty", "No tags selected")});
        return out;
    }
    for (auto const& t : mTags) out.push_back({"§7> " + t});
    return out;
}

void FilterControl::tooltip(float x, float y, std::vector<TooltipLine>& out) const {
    auto hit = hitTest(x, y);
    switch (hit.hit) {
    case Hit::Type: {
        std::string text;
        if (mTypeState == 0) text = tr("abp.filter_control.allow", "Allow");
        else if (mTypeState == 1 || mType == Type::AllowList) text = tr("abp.filter_control.block", "Block");
        else if (mType == Type::Deposit) text = tr("abp.filter_control.deposit_filter_type_inventory", "Match Inventory Contents");
        else text = tr("abp.filter_control.match_backpack_contents", "Match Backpack Contents");
        out.push_back({text});
        break;
    }
    case Hit::Primary:
        out.push_back({mPrimary == 0   ? tr("abp.filter_control.match_item", "Match Item")
                       : mPrimary == 1 ? tr("abp.filter_control.match_mod", "Match Mod")
                                       : tr("abp.filter_control.match_tags", "Match Tags")});
        break;
    case Hit::Durability:
        out.push_back({mDurability ? tr("abp.filter_control.match_durability", "Match Durability")
                                   : tr("abp.filter_control.ignore_durability", "Ignore Durability")});
        break;
    case Hit::Nbt:
        out.push_back({mNbt ? tr("abp.filter_control.match_nbt", "Match NBT") : tr("abp.filter_control.ignore_nbt", "Ignore NBT")});
        break;
    case Hit::AnyTag:
        out.push_back({mAnyTag ? tr("abp.filter_control.match_any_tag", "Match Any Tag")
                               : tr("abp.filter_control.match_all_tags", "Match All Tags")});
        break;
    case Hit::AddTag: {
        auto l = addTagTooltip();
        out.insert(out.end(), l.begin(), l.end());
        break;
    }
    case Hit::RemoveTag: {
        auto l = removeTagTooltip();
        out.insert(out.end(), l.begin(), l.end());
        break;
    }
    case Hit::TagList: {
        auto l = tagListTooltip();
        out.insert(out.end(), l.begin(), l.end());
        break;
    }
    case Hit::Slot:
        if (mSlotsEnabled && !mItems[static_cast<std::size_t>(hit.index)].isNull()) {
            auto l = itemTooltipLines(mItems[static_cast<std::size_t>(hit.index)]);
            out.insert(out.end(), l.begin(), l.end());
        }
        break;
    case Hit::TagSlot:
        if (!mTagItem.isNull()) {
            auto l = itemTooltipLines(mTagItem);
            out.insert(out.end(), l.begin(), l.end());
        }
        break;
    default:
        break;
    }
}

// ---- FilterTab ---------------------------------------------------------------------------------------

FilterTab::FilterTab(std::string key, float controlY, Extra extra)
: mKey(std::move(key)),
  mControlY(controlY),
  mExtra(std::move(extra)) {}

NativeTab::Size FilterTab::build(TabContext const& ctx, Rect area, WidgetGroup& widgets) {
    if (mExtra) mExtra(ctx, area, widgets);
    auto& control = widgets.add(std::make_unique<FilterControl>(mKey, ctx.sendTabAction));
    mControl      = &control;
    control.setPosition(area.x + 3, area.y + mControlY);
    auto it = ctx.upgrade.tab.find(mKey);
    if (it != ctx.upgrade.tab.end()) control.setState(*it);
    (void)control.consumeSizeChange();
    // SC SettingsTabBase: widget extent + 6 right / bottom border.
    return {3 + control.width() + 6, mControlY + control.height() + 6};
}

void FilterTab::update(TabContext const& ctx) {
    if (!mControl) return;
    auto it = ctx.upgrade.tab.find(mKey);
    if (it != ctx.upgrade.tab.end()) mControl->setState(*it);
}

bool FilterTab::needsRebuild() { return mControl && mControl->consumeSizeChange(); }

} // namespace abp::client::ui

#endif // ABP_CLIENT
