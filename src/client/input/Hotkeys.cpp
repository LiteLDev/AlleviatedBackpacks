#ifdef ABP_CLIENT

// Sophisticated Backpacks' key bindings (see Keys.h). Registration and detection follow BEI's
// approach: the keys are registered with LeviLamina's KeyRegistry (listed and remappable in the
// controls menu, saved with the game's options) but bound in no input mapping; the currently
// mapped key codes are read from the keyboard layout and raw input events are matched against them
// (a key that already has a vanilla binding never reaches a second binding otherwise).

#include <array>
#include <atomic>
#include <string>
#include <string_view>
#include <vector>

#include "ll/api/input/KeyRegistry.h"
#include "ll/api/memory/Hook.h"

#include "mc/client/input/KeyboardRemappingLayout.h"
#include "mc/client/input/Keymapping.h"
#include "mc/client/input/RemappingLayout.h"
#include "mc/client/input/VanillaClientInputMappingFactory.h"
#include "mc/locale/I18nImpl.h"
#include "mc/locale/Localization.h"
#include "mc/world/item/ItemStack.h"

#include "client/input/InputRouter.h"
#include "client/input/InputState.h"
#include "client/input/Keys.h"
#include "client/net/ClientNet.h"
#include "client/ui/UiRoot.h"
#include "game/BackpackItem.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace abp::client::input {

namespace detail {
namespace {

struct KeyDef {
    std::string_view name;       ///< KeyRegistry name; the game calls it "key.AlleviatedBackpacks.<name>"
    std::vector<int> defaults;   ///< virtual-key codes (mouse buttons are negative: -97 = middle)
    KeyAction        action;
    bool             alt = false; ///< fires only with Alt held
};

constexpr int VK_OEM_4_ = 0xDB; // [
constexpr int VK_OEM_6_ = 0xDD; // ]
constexpr int MOUSE_MIDDLE = -97;
constexpr int MOUSE_LEFT   = -99;
constexpr int MOUSE_RIGHT  = -98;

std::vector<KeyDef> const& keyDefs() {
    static std::vector<KeyDef> const defs{
        {"open_backpack", {'B'}, KeyAction::OpenBackpack},
        {"inventory_interaction", {'C'}, KeyAction::InventoryInteraction},
        {"tool_swap", {}, KeyAction::ToolSwap},
        {"toggle_upgrade_1", {'Z'}, KeyAction::ToggleUpgrade1, true},
        {"toggle_upgrade_2", {'X'}, KeyAction::ToggleUpgrade2, true},
        {"toggle_upgrade_3", {}, KeyAction::ToggleUpgrade3, true},
        {"toggle_upgrade_4", {}, KeyAction::ToggleUpgrade4, true},
        {"toggle_upgrade_5", {}, KeyAction::ToggleUpgrade5, true},
        {"sort", {MOUSE_MIDDLE}, KeyAction::Sort},
        {"transfer_to_storage", {VK_OEM_4_}, KeyAction::TransferToStorage},
        {"transfer_to_inventory", {VK_OEM_6_}, KeyAction::TransferToInventory},
    };
    return defs;
}

constexpr std::string_view KEY_PREFIX = "key.AlleviatedBackpacks.";
constexpr std::size_t      KEYS_PER   = 2;
constexpr std::size_t      MAX_KEYS   = 16;

/// Up to two codes per binding, as currently mapped (0 = none). Client thread writes, any reads.
std::array<std::array<std::atomic<int>, KEYS_PER>, MAX_KEYS> gKeys{};
std::atomic<bool>                                            gActive{false};

void resetToDefaults() {
    auto const& defs = keyDefs();
    for (std::size_t i = 0; i < defs.size(); ++i) {
        for (std::size_t k = 0; k < KEYS_PER; ++k) gKeys[i][k] = k < defs[i].defaults.size() ? defs[i].defaults[k] : 0;
    }
}

std::size_t indexOf(KeyAction action) {
    auto const& defs = keyDefs();
    for (std::size_t i = 0; i < defs.size(); ++i) {
        if (defs[i].action == action) return i;
    }
    return MAX_KEYS;
}

bool bound(std::size_t i, int code) {
    if (i >= MAX_KEYS || code == 0) return false;
    for (auto const& k : gKeys[i]) {
        if (k.load() == code) return true;
    }
    return false;
}

/// Client thread: refresh gKeys from the (possibly remapped) keyboard layout.
void readLayout(RemappingLayout const& layout) {
    auto const& defs = keyDefs();
    for (std::size_t i = 0; i < defs.size(); ++i) {
        auto        action  = std::string{KEY_PREFIX} + std::string{defs[i].name};
        auto const& mapping = layout.getKeymappingByAction(action);
        if (*mapping.mAction != action) continue; // not in the layout: keep defaults
        std::array<int, KEYS_PER> keys{};
        std::size_t               n = 0;
        if (mapping.isAssigned()) {
            for (int key : *mapping.mKeys) {
                if (key != 0 && n < KEYS_PER) keys[n++] = key;
            }
        }
        for (std::size_t k = 0; k < KEYS_PER; ++k) gKeys[i][k] = keys[k];
    }
}

int mouseCode(ui::MouseButton b) {
    switch (b) {
    case ui::MouseButton::Left:
        return MOUSE_LEFT;
    case ui::MouseButton::Right:
        return MOUSE_RIGHT;
    case ui::MouseButton::Middle:
        return MOUSE_MIDDLE;
    }
    return 0;
}

/// Where a hovered vanilla slot is on the player, for Open Backpack over a backpack.
std::optional<nlohmann::json> openTarget(HoveredSlot const& slot) {
    auto const& c = slot.collection;
    int         i = slot.index;
    if (c == "hotbar_items" && i >= 0 && i < 9) return nlohmann::json{{"where", "inventory"}, {"slot", i}};
    if ((c == "inventory_items" || c == "combined_hotbar_and_inventory_items") && i >= 0 && i < 36) {
        return nlohmann::json{{"where", "inventory"}, {"slot", i}};
    }
    if (c == "armor_items" && i == 1) return nlohmann::json{{"where", "armor"}};
    if (c == "offhand_items") return nlohmann::json{{"where", "offhand"}};
    return std::nullopt;
}

/// Client thread: run a binding.
void run(KeyAction action) {
    if (!gActive.load() || !net::serverReady()) return;
    auto& snap   = snapshot();
    bool  inGame = snap.inLevel.load() && !snap.cursorScreen.load();
    switch (action) {
    case KeyAction::OpenBackpack: {
        if (inGame) {
            net::sendKeyAction(nlohmann::json{{"action", "open"}});
            return;
        }
        // Any inventory screen: a backpack in the player's own slots under the cursor.
        auto slot = hoveredVanillaSlot();
        if (!slot) return;
        auto const& item = hoveredVanillaItem();
        if (item.isNull() || !game::isBackpack(item) || item.mCount != 1) return;
        auto target = openTarget(*slot);
        if (!target) return;
        (*target)["action"] = "openAt";
        log().info("key: open backpack in {} #{}", slot->collection, slot->index);
        net::sendKeyAction(std::move(*target));
        return;
    }
    case KeyAction::InventoryInteraction:
        if (inGame) net::sendKeyAction(nlohmann::json{{"action", "interact"}});
        return;
    case KeyAction::ToolSwap:
        if (inGame) net::sendKeyAction(nlohmann::json{{"action", "toolSwap"}});
        return;
    case KeyAction::ToggleUpgrade1:
    case KeyAction::ToggleUpgrade2:
    case KeyAction::ToggleUpgrade3:
    case KeyAction::ToggleUpgrade4:
    case KeyAction::ToggleUpgrade5: {
        if (!snap.inLevel.load()) return;
        int slot = static_cast<int>(action) - static_cast<int>(KeyAction::ToggleUpgrade1);
        net::sendKeyAction(nlohmann::json{{"action", "toggleUpgrade"}, {"slot", slot}});
        return;
    }
    default:
        return; // screen-only bindings: handled by the backpack screen
    }
}

} // namespace

// Mark our default keymappings as shared keys before the game de-duplicates its layout (B, C, Z,
// X and the middle mouse button also have vanilla bindings or may get them).
LL_TYPE_INSTANCE_HOOK(
    AbpKeySharedDefaultsHook,
    ll::memory::HookPriority::Low,
    RemappingLayout,
    &RemappingLayout::assignDefaultMapping,
    void,
    std::vector<::Keymapping>&& newDefaultMapping
) {
    for (auto& mapping : newDefaultMapping) {
        if (std::string_view{*mapping.mAction}.starts_with(KEY_PREFIX)) mapping.mIsSharedKey = true;
    }
    origin(std::move(newDefaultMapping));
}

LL_TYPE_INSTANCE_HOOK(
    AbpKeyLayoutHook,
    ll::memory::HookPriority::Normal,
    VanillaClientInputMappingFactory,
    &VanillaClientInputMappingFactory::$_updateKeyboardAndMouseControls,
    void,
    IOptionRegistry& options
) {
    origin(options);
    if (auto layout = mKeyboardRemappingLayout.lock()) readLayout(*layout);
}

// The controls menu localizes "key.AlleviatedBackpacks.<name>"; answer from our translations.
LL_TYPE_INSTANCE_HOOK(
    AbpKeyNameI18nHook,
    ll::memory::HookPriority::Normal,
    I18nImpl,
    &I18nImpl::$get,
    std::string,
    std::string const&                          id,
    std::vector<std::string> const&             params,
    std::shared_ptr<::Localization const> const locale
) {
    if (id.size() > KEY_PREFIX.size() && id.starts_with(KEY_PREFIX)) {
        constexpr std::string_view NAME_SUFFIX = ".name";
        constexpr std::string_view DESC_SUFFIX = ".description";
        auto                       rest        = std::string_view{id}.substr(KEY_PREFIX.size());
        if (rest.ends_with(DESC_SUFFIX)) return std::string(" ");
        if (rest.ends_with(NAME_SUFFIX)) rest.remove_suffix(NAME_SUFFIX.size());
        std::string localeCode = locale ? locale->getFullLanguageCode() : std::string{};
        return std::string(translate("abp.key." + std::string{rest}, rest, localeCode));
    }
    return origin(id, params, locale);
}

namespace {
bool gLayoutHooked = false;
}

void registerHotkeys() {
    if (!gLayoutHooked) {
        AbpKeySharedDefaultsHook::hook();
        AbpKeyLayoutHook::hook();
        gLayoutHooked = true;
    }
    static bool const registered = [] {
        resetToDefaults();
        auto& registry = ll::input::KeyRegistry::getInstance();
        for (auto const& def : keyDefs()) {
            auto& handle = registry.getOrCreateKey(def.name, def.defaults, true);
            handle.setInputMappingStack({}); // listed and remappable, bound in no mapping
        }
        return true;
    }();
    (void)registered;
}

void installHotkeys() {
    registerHotkeys();
    AbpKeyNameI18nHook::hook();
    gActive = true;
}

void uninstallHotkeys() {
    gActive = false;
    AbpKeyNameI18nHook::unhook();
    if (gLayoutHooked) {
        AbpKeyLayoutHook::unhook();
        AbpKeySharedDefaultsHook::unhook();
        gLayoutHooked = false;
    }
}

void onHotkeyKeyDown(int vk) {
    if (!gActive.load() || vk <= 0) return;
    auto& snap = snapshot();
    if (snap.vanillaTextFocus.load() || snap.textFocus.load()) return;
    bool const alt  = altDown();
    auto const& defs = keyDefs();
    for (std::size_t i = 0; i < defs.size(); ++i) {
        if (defs[i].alt && !alt) continue;
        if (!bound(i, vk)) continue;
        auto action = defs[i].action;
        post([action] { run(action); });
    }
}

void onHotkeyMouseDown(ui::MouseButton button) {
    if (!gActive.load()) return;
    int         code = mouseCode(button);
    auto const& defs = keyDefs();
    for (std::size_t i = 0; i < defs.size(); ++i) {
        if (defs[i].alt || !bound(i, code)) continue;
        auto action = defs[i].action;
        post([action] { run(action); });
    }
}

} // namespace detail

bool keyMatches(KeyAction action, int vk) { return vk > 0 && detail::bound(detail::indexOf(action), vk); }

bool mouseMatches(KeyAction action, ui::MouseButton button) {
    return detail::bound(detail::indexOf(action), detail::mouseCode(button));
}

} // namespace abp::client::input

#endif // ABP_CLIENT
