#ifdef ABP_CLIENT

#include "client/tooltip/ContentsTooltip.h"

#include <algorithm>
#include <optional>

#include "ll/api/memory/Hook.h"

#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/controls/UIControl.h"
#include "mc/client/gui/controls/renderers/HoverTextRenderer.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/safety/RedactableString.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/components/ComponentItem.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"

#include "client/Client.h"
#include "client/input/InputRouter.h"
#include "client/net/ClientNet.h"
#include "client/render/RenderHooks.h"
#include "client/ui/UiRoot.h"
#include "client/ui/widgets/SbStyle.h"
#include "core/screen/CountFormat.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace abp::client::tooltip {

namespace {
constexpr int   kMaxStacksOnLine = 9;
constexpr float kStackWidth      = 18;
constexpr float kCountPadding    = 2;

/// Item stacks built from the last contents received for one backpack.
struct Built {
    std::string            uuid;
    double                 receivedAt = -1;
    bool                   known      = false;
    std::vector<std::string> lines;
    std::vector<ItemStack> upgrades;
    std::vector<bool>      toggleable, enabled;
    std::vector<ItemStack> items;
    std::vector<long long> counts;
};
Built gBuilt; // client thread

Built const& build(std::string const& uuid) {
    auto const* cached = net::contents(uuid);
    double      at     = cached ? cached->receivedAt : 0;
    if (gBuilt.uuid == uuid && gBuilt.receivedAt == at) return gBuilt;
    gBuilt            = {};
    gBuilt.uuid       = uuid;
    gBuilt.receivedAt = at;
    if (!cached) return gBuilt;
    auto const& m = cached->model;
    gBuilt.known  = m.known;
    gBuilt.lines  = m.lines;
    for (auto const& u : m.upgrades) {
        gBuilt.upgrades.push_back(game::ItemConv::toStack(u.item, 1));
        gBuilt.toggleable.push_back(u.toggleable);
        gBuilt.enabled.push_back(u.enabled);
    }
    for (auto const& i : m.items) {
        gBuilt.items.push_back(game::ItemConv::toStack(i, 1));
        gBuilt.counts.push_back(i.count);
    }
    return gBuilt;
}

bool cursorHasItem() {
    auto* player = currentPlayer();
    if (!player) return false;
    return !player->mPlayerUIContainer->getItem(0).isNull();
}

std::string yellow(std::string s) { return "§e" + s; }

/// SC's contents image: lines, "Upgrades" + pips/icons, "Inventory" + compacted grid.
ui::TooltipImage contentsImage(ui::Painter& p, Built const& b) {
    std::vector<std::string> lines = b.lines;
    bool empty = b.upgrades.empty() && b.items.empty();
    if (empty) lines.push_back(yellow(ctr("abp.companion.tooltip.empty", "No Upgrades or Inventory Contents")));

    float upgradesWidth = 0;
    for (std::size_t i = 0; i < b.upgrades.size(); ++i) upgradesWidth += (b.toggleable[i] ? 4 : 0) + kStackWidth;
    auto countWidth = [&](long long n) { return p.textWidth(core::screen::abbreviateCount(n)) + kCountPadding; };
    float contentsWidth = 0;
    for (std::size_t i = 0; i < b.items.size() && i < kMaxStacksOnLine; ++i) contentsWidth += std::max(countWidth(b.counts[i]), kStackWidth);
    float linesWidth = 0;
    for (auto const& l : lines) linesWidth = std::max(linesWidth, p.textWidth(l));
    float upgradesTitle  = b.upgrades.empty() ? 0 : p.textWidth(ctr("abp.companion.tooltip.upgrades", "Upgrades"));
    float inventoryTitle = b.items.empty() ? 0 : p.textWidth(ctr("abp.companion.tooltip.inventory", "Inventory"));

    ui::TooltipImage img;
    img.width  = std::max({upgradesWidth, contentsWidth, linesWidth, upgradesTitle, inventoryTitle});
    float upgradesHeight  = b.upgrades.empty() ? 0 : 32;
    float inventoryHeight = b.items.empty() ? 0 : 12 + static_cast<float>(1 + (static_cast<int>(b.items.size()) - 1) / kMaxStacksOnLine) * 20;
    float total           = upgradesHeight + inventoryHeight + static_cast<float>(lines.size()) * 10;
    img.height            = total > 0 ? total : 12;

    img.draw = [&b, lines, countWidth](ui::Painter& painter, float left, float top) {
        float y = top;
        for (auto const& l : lines) {
            painter.text(l, left, y, ui::Color::rgb(0xFFFFFF), true);
            y += 10;
        }
        if (!b.upgrades.empty()) {
            painter.text(yellow(ctr("abp.companion.tooltip.upgrades", "Upgrades")), left, y, ui::Color::rgb(0xFFFFFF), true);
            y += 10;
            float x = left;
            for (std::size_t i = 0; i < b.upgrades.size(); ++i) {
                if (b.toggleable[i]) {
                    ui::sb::switchPip(painter, x, y + 3, b.enabled[i]);
                    x += 4;
                }
                painter.item(b.upgrades[i], x, y);
                x += kStackWidth;
            }
            y += 20;
        }
        if (!b.items.empty()) {
            painter.text(yellow(ctr("abp.companion.tooltip.inventory", "Inventory")), left, y, ui::Color::rgb(0xFFFFFF), true);
            y += 10;
            float x = left;
            for (std::size_t i = 0; i < b.items.size(); ++i) {
                float rowY = y + static_cast<float>(i / kMaxStacksOnLine) * 20;
                if (i % kMaxStacksOnLine == 0) x = left;
                float w       = std::max(countWidth(b.counts[i]), kStackWidth);
                float xOffset = w - kStackWidth;
                painter.item(b.items[i], x + xOffset, rowY);
                render::decorateItemIcon(painter, b.items[i], x + xOffset, rowY);
                auto text = core::screen::abbreviateCount(b.counts[i]);
                // renderItemDecorations: count right-aligned at the icon's bottom-right corner.
                painter.text(text, x + xOffset + 17 - painter.textWidth(text), rowY + 9, ui::Color::rgb(0xFFFFFF), true);
                x += w;
            }
        }
    };
    return img;
}
} // namespace

bool contentsWanted() { return input::shiftDown() || cursorHasItem(); }

std::string pressForContentsLine() {
    return ctr(
        "abp.companion.tooltip.press_for_contents",
        "§7Press <{}§7> to View Contents",
        "§b" + ctr("abp.companion.tooltip.shift", "Left Shift")
    );
}

bool drawBackpackTooltip(ui::Painter& p, ItemStack const& stack, float x, float y, std::vector<ui::TooltipLine> extra) {
    if (stack.isNull() || !game::isBackpack(stack)) return false;
    auto lines = ui::itemTooltipLines(stack);
    lines.insert(lines.end(), extra.begin(), extra.end());
    if (!contentsWanted()) {
        ui::drawTooltip(p, lines, x, y);
        return true;
    }
    auto  uuid  = game::backpackUuid(stack).value_or(std::string{});
    auto& built = uuid.empty() ? (gBuilt = Built{}, gBuilt) : build(uuid);
    (void)net::renderInfo(uuid);
    auto img = contentsImage(p, built);
    ui::drawTooltip(p, lines, x, y, &img);
    return true;
}

// ---- hooks ----------------------------------------------------------------------------------------------

namespace {
void appendPressLine(::ItemStackBase const& stack, ::Level& level, ::Bedrock::Safety::RedactableString& hovertext) {
    if (!level.isClientSide() || !net::serverReady() || input::shiftDown()) return;
    if (!game::backpackTierOf(stack.getTypeName())) return;
    auto  line = pressForContentsLine();
    auto& s    = hovertext.mUnredactedString;
    if (s.find(line) != std::string::npos) return; // both hooks ran (ComponentItem calls Item)
    if (!s.empty() && s.back() != '\n') s += '\n';
    s += line;
}
} // namespace

// "Press <Left Shift> to View Contents" on backpack items (client side only). Backpacks are
// data-driven (ComponentItem); the Item hook covers other item classes calling the base.
LL_TYPE_INSTANCE_HOOK(
    AbpBackpackHoverTextHook,
    ll::memory::HookPriority::Normal,
    Item,
    &Item::$appendFormattedHovertext,
    void,
    ::ItemStackBase const&               stack,
    ::Level&                             level,
    ::Bedrock::Safety::RedactableString& hovertext,
    bool                                 showCategory
) {
    origin(stack, level, hovertext, showCategory);
    appendPressLine(stack, level, hovertext);
}

LL_TYPE_INSTANCE_HOOK(
    AbpBackpackComponentHoverTextHook,
    ll::memory::HookPriority::Normal,
    ComponentItem,
    &ComponentItem::$appendFormattedHovertext,
    void,
    ::ItemStackBase const&               stack,
    ::Level&                             level,
    ::Bedrock::Safety::RedactableString& hovertext,
    bool                                 showCategory
) {
    origin(stack, level, hovertext, showCategory);
    appendPressLine(stack, level, hovertext);
}

// Vanilla screens: with Shift (or a cursor item) the hover text over a backpack slot becomes the
// contents tooltip.
LL_TYPE_INSTANCE_HOOK(
    AbpBackpackHoverRenderHook,
    ll::memory::HookPriority::Normal,
    HoverTextRenderer,
    &HoverTextRenderer::$render,
    void,
    ::MinecraftUIRenderContext& ctx,
    ::IClientInstance&          client,
    ::UIControl&                owner,
    int                         pass
) {
    if (net::serverReady() && input::hoveredVanillaSlot() && contentsWanted()) {
        auto const& item = input::hoveredVanillaItem();
        if (!item.isNull() && game::isBackpack(item)) {
            try {
                ui::Painter painter(ctx);
                auto const& frame = ui::UiRoot::get().lastFrame();
                drawBackpackTooltip(painter, item, frame.mouseX, frame.mouseY);
                return;
            } catch (std::exception const& e) {
                log().error("contents tooltip failed: {}", e.what());
            }
        }
    }
    origin(ctx, client, owner, pass);
}

class ContentsTooltipModule {
public:
    void install() {
        AbpBackpackHoverTextHook::hook();
        AbpBackpackComponentHoverTextHook::hook();
        AbpBackpackHoverRenderHook::hook();
    }
    void uninstall() {
        AbpBackpackHoverRenderHook::unhook();
        AbpBackpackComponentHoverTextHook::unhook();
        AbpBackpackHoverTextHook::unhook();
        gBuilt = {};
    }
};

} // namespace abp::client::tooltip

ABP_REGISTER_MODULE(abp::client::tooltip::ContentsTooltipModule);

#endif // ABP_CLIENT
