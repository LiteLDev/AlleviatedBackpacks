#ifdef ABP_CLIENT

#include "client/tooltip/ContentsTooltip.h"

#include <algorithm>
#include <atomic>
#include <optional>
#include <unordered_set>

#include "ll/api/memory/Hook.h"

#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/controls/UIControl.h"
#include "mc/client/gui/controls/renderers/HoverTextRenderer.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/locale/I18n.h"
#include "mc/locale/Localization.h"
#include "mc/safety/RedactableString.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/components/ComponentItem.h"
#include "mc/world/level/Level.h"

#include "client/Client.h"
#include "client/input/InputRouter.h"
#include "client/input/Keys.h"
#include "client/input/Stash.h"
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
// SC ClientStorageContentsTooltipBase constants.
constexpr int   kMaxStacksOnLine = 9;
constexpr float kStackWidth      = 18;
constexpr float kCountPadding    = 2;
// SB ClientLinkedStorageTooltip / ClientBackpackContentsTooltip.
constexpr std::uint32_t kRoleTextColor   = 0xAE8BC7;
constexpr float         kRoleHeight      = 16;
constexpr float         kRoleHeaderHeight = 18;

constexpr char const* kYellow = "\xC2\xA7" "e";
constexpr char const* kGray   = "\xC2\xA7" "7";
constexpr char const* kAqua   = "\xC2\xA7" "b";
constexpr char const* kDarkGray = "\xC2\xA7" "8";

LinkedEndpointProvider gLinkedProvider;

/// Item stacks built from the last contents received for one backpack.
struct Built {
    std::string              uuid;
    double                   receivedAt = -1;
    bool                     known      = false;
    std::vector<std::string> lines;
    std::vector<ItemStack>   upgrades;
    std::vector<bool>        toggleable, enabled;
    std::vector<ItemStack>   items;
    std::vector<long long>   counts;
};
Built gBuilt; // client thread

Built const& build(std::string const& uuid) {
    if (uuid.empty()) {
        // SC: a storage without contents uuid has nothing to request (only the "empty" line).
        static Built const none;
        return none;
    }
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

std::optional<LinkedEndpoint> linkedOf(ItemStack const& stack) {
    if (!gLinkedProvider) return std::nullopt;
    try {
        return gLinkedProvider(stack);
    } catch (std::exception const& e) {
        log().error("linked storage tooltip provider failed: {}", e.what());
        return std::nullopt;
    }
}

/// SB ClientLinkedStorageTooltip.renderRole: icon at x - 2, description at (x + 16, y + 4).
void drawRole(ui::Painter& p, LinkedEndpoint const& role, float x, float y) {
    if (role.drawIcon) role.drawIcon(p, x - 2, y);
    p.text(role.description, x + 16, y + 4, ui::Color::rgb(kRoleTextColor), false);
}

/// SC's contents image (+ SB's role header for linked endpoints).
ui::TooltipImage contentsImage(ui::Painter& p, Built const& b, std::optional<LinkedEndpoint> const& role) {
    std::vector<std::string> lines = b.lines;
    if (b.upgrades.empty() && b.items.empty()) {
        lines.push_back(kYellow + ctr("abp.input.tooltip.empty", "No Upgrades or Inventory Contents"));
    }
    auto countWidth = [&p](long long n) { return p.textWidth(core::screen::abbreviateCount(n)) + kCountPadding; };

    // calculateWidth: max(upgrades, contents (first row), lines); the section titles don't count.
    float upgradesWidth = 0;
    for (std::size_t i = 0; i < b.upgrades.size(); ++i) upgradesWidth += (b.toggleable[i] ? 4 : 0) + kStackWidth;
    float contentsWidth = 0;
    for (std::size_t i = 0; i < b.items.size() && i < kMaxStacksOnLine; ++i) {
        contentsWidth += std::max(countWidth(b.counts[i]), kStackWidth);
    }
    float linesWidth = 0;
    for (auto const& l : lines) linesWidth = std::max(linesWidth, p.textWidth(l));
    // calculateHeight.
    float upgradesHeight  = b.upgrades.empty() ? 0 : 32;
    float inventoryHeight = b.items.empty() ? 0 : 12 + static_cast<float>(1 + (static_cast<int>(b.items.size()) - 1) / kMaxStacksOnLine) * 20;
    float total           = upgradesHeight + inventoryHeight + static_cast<float>(lines.size()) * 10;

    float roleH = role ? kRoleHeaderHeight : 0;
    ui::TooltipImage img;
    img.width  = std::max({upgradesWidth, contentsWidth, linesWidth, role ? 16 + p.textWidth(role->description) : 0.f});
    img.height = (total > 0 ? total : 12) + roleH;
    img.draw   = [&b, lines, countWidth, role, roleH](ui::Painter& painter, float left, float top) {
        if (role) drawRole(painter, *role, left, top);
        float y = top + roleH;
        for (auto const& l : lines) {
            painter.text(l, left, y, ui::Color::rgb(0xFFFFFF), true);
            y += 10;
        }
        if (!b.upgrades.empty()) {
            painter.text(kYellow + ctr("abp.input.tooltip.upgrades", "Upgrades"), left, y, ui::Color::rgb(0xFFFFFF), true);
            y += 10;
            float x = left;
            for (std::size_t i = 0; i < b.upgrades.size(); ++i) {
                if (b.toggleable[i]) {
                    ui::sb::switchPip(painter, x, y + 3, b.enabled[i]); // UPGRADE_ON / UPGRADE_OFF, 4x10
                    x += 4;
                }
                painter.item(b.upgrades[i], x, y);
                x += kStackWidth;
            }
            y += 20;
        }
        if (!b.items.empty()) {
            painter.text(kYellow + ctr("abp.input.tooltip.inventory", "Inventory"), left, y, ui::Color::rgb(0xFFFFFF), true);
            y += 10;
            float x = left;
            for (std::size_t i = 0; i < b.items.size(); ++i) {
                float rowY = y + static_cast<float>(i / kMaxStacksOnLine) * 20;
                if (i % kMaxStacksOnLine == 0) x = left;
                float w       = std::max(countWidth(b.counts[i]), kStackWidth);
                float xOffset = w - kStackWidth;
                painter.item(b.items[i], x + xOffset, rowY);
                render::decorateItemIcon(painter, b.items[i], x + xOffset, rowY);
                // renderItemDecorations with the abbreviated count as text (always drawn).
                auto text = core::screen::abbreviateCount(b.counts[i]);
                painter.text(text, x + xOffset + 19 - 2 - painter.textWidth(text), rowY + 6 + 3, ui::Color::rgb(0xFFFFFF), true);
                x += w;
            }
        }
    };
    return img;
}

/// Role header only (SB getTooltipImage for a linked endpoint without Shift).
ui::TooltipImage roleImage(ui::Painter& p, LinkedEndpoint const& role) {
    ui::TooltipImage img;
    img.width  = 16 + p.textWidth(role.description);
    img.height = kRoleHeight;
    img.draw   = [role](ui::Painter& painter, float left, float top) { drawRole(painter, role, left, top); };
    return img;
}

std::string contentsUuidOf(ItemStack const& stack, std::optional<LinkedEndpoint> const& role) {
    if (role && !role->contentsUuid.empty()) return role->contentsUuid;
    return game::backpackUuid(stack).value_or(std::string{});
}
} // namespace

bool contentsWanted() { return input::shiftDown() || cursorHasItem(); }

namespace {
std::atomic<bool> gAdvanced{false};
}
bool advancedTooltips() { return gAdvanced.load(); }
void setAdvancedTooltips(bool on) {
    gAdvanced = on;
    log().info("tooltip: advanced tooltips {}", on ? "on" : "off");
}

std::string pressForContentsLine() {
    // translatable(press_for_contents, translatable(shift).withStyle(AQUA)).withStyle(GRAY)
    return kGray
         + ctr("abp.input.tooltip.press_for_contents",
               "Press <{}> to View Contents",
               kAqua + ctr("abp.input.tooltip.shift", "Left Shift") + kGray);
}

bool drawBackpackTooltip(ui::Painter& p, ItemStack const& stack, float x, float y, std::vector<ui::TooltipLine> extra) {
    if (stack.isNull() || !game::isBackpack(stack)) return false;
    auto lines = ui::itemTooltipLines(stack);
    lines.insert(lines.end(), extra.begin(), extra.end());
    auto role = linkedOf(stack);
    if (role && !input::shiftDown() && !cursorHasItem()) {
        auto img = roleImage(p, *role);
        ui::drawTooltip(p, lines, x, y, &img);
        return true;
    }
    if (!contentsWanted()) {
        ui::drawTooltip(p, lines, x, y);
        return true;
    }
    auto const& built = build(contentsUuidOf(stack, role));
    (void)net::renderInfo(game::backpackUuid(stack).value_or(std::string{}));
    auto img = contentsImage(p, built, role);
    ui::drawTooltip(p, lines, x, y, &img);
    return true;
}

void drawRightClickToAdd(ui::Painter& p, ItemStack const& backpack, float x, float y) {
    if (backpack.isNull()) return;
    auto        role  = linkedOf(backpack);
    auto const& built = build(contentsUuidOf(backpack, role));
    auto        img   = contentsImage(p, built, role);
    ui::drawTooltip(p, {{ctr("abp.input.tooltip.right_click_to_add_to_storage", "Right Click To Add"), 0xFFFFFF}}, x, y, &img);
}

void setLinkedEndpointProvider(LinkedEndpointProvider provider) { gLinkedProvider = std::move(provider); }

// ---- hover text --------------------------------------------------------------------------------------------

namespace {
void appendLine(std::string& s, std::string const& line) {
    if (!s.empty() && s.back() != '\n') s += '\n';
    s += line;
}

/// The game's own translation of `key` (resource pack lang), nullopt if there is none.
std::optional<std::string> packText(std::string const& key) {
    try {
        std::shared_ptr<::Localization const> loc = getI18n().getCurrentLanguage();
        auto text = getI18n().get(key, std::vector<std::string>{}, loc);
        if (text.empty() || text == key) return std::nullopt;
        return text;
    } catch (...) {
        return std::nullopt;
    }
}

/// SC UpgradeItemBase: `<descriptionId>.tooltip`, split into lines (`\n`), dark gray.
void appendUpgradeLines(::ItemStackBase const& stack, std::string& s) {
    auto const* item = stack.mItem.get();
    if (!item) return;
    auto const& id = stack.getTypeName();
    if (!id.starts_with("alleviatedbackpacks:") || game::backpackTierOf(id)) return;
    // Bedrock's description id already ends in ".name" (the lang key of the name).
    auto key = item->getDescriptionId();
    if (key.ends_with(".name")) key.resize(key.size() - 5);
    key += ".tooltip";
    auto text = packText(key);
    static std::unordered_set<std::string> logged;
    if (logged.size() < 64 && logged.insert(id).second) log().info("tooltip: {} -> {} ({})", id, key, text ? "found" : "missing");
    if (!text) return;
    // SB ToolSwapperUpgradeItem: the advanced tooltip names the Tool Swap key.
    if (auto at = text->find("%s"); at != std::string::npos) text->replace(at, 2, input::keyDisplayName(input::KeyAction::ToolSwap));
    std::vector<std::string> lines;
    std::size_t              start = 0;
    while (start <= text->size()) {
        auto nl  = text->find('\n', start);
        auto esc = text->find("\\n", start);
        auto end = std::min(nl, esc);
        lines.push_back(text->substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) break;
        start = end + (end == esc ? 2 : 1);
    }
    if (lines.empty() || s.find(kDarkGray + lines.front()) != std::string::npos) return; // both hooks ran
    for (auto const& l : lines) appendLine(s, kDarkGray + l);
}

void appendBackpackLines(::ItemStackBase const& stack, bool advanced, std::string& s) {
    if (!game::backpackTierOf(stack.getTypeName())) return;
    std::vector<std::string> lines;
    if (advanced) {
        ItemStack copy;
        static_cast<ItemStackBase&>(copy) = stack;
        if (auto role = linkedOf(copy)) {
            lines.push_back(kDarkGray + ctr("abp.input.tooltip.linked_storage_group", "Linked Storage Group: {}", role->groupId));
            lines.push_back(kDarkGray + ctr("abp.input.tooltip.linked_storage_endpoint", "Linked Storage Endpoint: {}", role->endpointId));
        } else if (auto uuid = game::backpackUuid(copy)) {
            lines.push_back(kDarkGray + ctr("abp.input.tooltip.uuid", "UUID: {}", *uuid));
        }
    }
    if (!input::shiftDown()) lines.push_back(pressForContentsLine());
    // Both hooks run for data-driven items (ComponentItem calls Item): add the lines once.
    if (lines.empty() || s.find(lines.front()) != std::string::npos) return;
    for (auto const& l : lines) appendLine(s, l);
}

void appendLines(::ItemStackBase const& stack, ::Level& level, ::Bedrock::Safety::RedactableString& hovertext, bool advanced) {
    if (!level.isClientSide() || !net::serverReady() || stack.isNull()) return;
    auto& s = hovertext.mUnredactedString;
    appendUpgradeLines(stack, s);
    // Bedrock passes `true` for every inventory tooltip (it is the "show category" flag there), and
    // has no advanced-tooltips toggle (F3+H): the UUID / linked endpoint lines follow
    // `abp::client::tooltip::advancedTooltips` instead (off by default, like Java).
    (void)advanced;
    appendBackpackLines(stack, advancedTooltips(), s);
}
} // namespace

// Item hover text (client side only). Backpacks and upgrades are data-driven (ComponentItem); the
// Item hook covers other item classes calling the base. The last parameter is Bedrock's
// "advanced tooltips" flag (named so in FireworksItem).
LL_TYPE_INSTANCE_HOOK(
    AbpBackpackHoverTextHook,
    ll::memory::HookPriority::Normal,
    Item,
    &Item::$appendFormattedHovertext,
    void,
    ::ItemStackBase const&               stack,
    ::Level&                             level,
    ::Bedrock::Safety::RedactableString& hovertext,
    bool                                 advanced
) {
    origin(stack, level, hovertext, advanced);
    appendLines(stack, level, hovertext, advanced);
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
    bool                                 advanced
) {
    origin(stack, level, hovertext, advanced);
    appendLines(stack, level, hovertext, advanced);
}

// Vanilla screens: the hover text over a backpack slot becomes SB's tooltip (contents image with
// Shift, role header for linked endpoints); with an item on the cursor only the right-click
// stashing tooltip is shown (drawn by the stash overlay layer).
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
    if (net::serverReady() && input::hoveredVanillaSlot()) {
        if (input::stash::vanillaTooltipShown()) return;
        auto const& item = input::hoveredVanillaItem();
        if (!item.isNull() && game::isBackpack(item) && (contentsWanted() || linkedOf(item))) {
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
