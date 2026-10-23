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

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include "ll/api/input/KeyRegistry.h"
#include "ll/api/memory/Hook.h"

#include "mc/client/input/KeyboardRemappingLayout.h"
#include "mc/client/input/Keymapping.h"
#include "mc/client/input/RemappingLayout.h"
#include "mc/client/input/VanillaClientInputMappingFactory.h"
#include "mc/locale/I18nImpl.h"
#include "mc/locale/Localization.h"
#include "mc/world/item/ItemStack.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/screens/SceneFactory.h"
#include "mc/client/gui/screens/interfaces/ISceneStack.h"

#include "client/Client.h"
#include "client/input/InputRouter.h"
#include "client/input/InputState.h"
#include "client/input/Keys.h"
#include "client/net/ClientNet.h"
#include "client/ui/UiRoot.h"
#include "client/ui/screen/BackpackScreen.h"
#include "game/BackpackItem.h"
#include "mod/I18n.h"
#include "mod/Main.h"
#include "mod/Log.h"

namespace abp::client::input {

namespace detail {
namespace {

struct KeyDef {
    std::string_view name;       ///< KeyRegistry name; the game calls it "key.AlleviatedBackpacks.<name>"
    std::vector<int> defaults;   ///< virtual-key codes (mouse buttons are negative: -97 = middle)
    KeyAction        action;
    bool             alt  = false; ///< fires only with Alt held (SB's KeyModifier.ALT defaults)
    bool             core = false; ///< SC's "Sophisticated Mods" category (else "Sophisticated Backpacks")
};

constexpr int VK_OEM_4_ = 0xDB; // [
constexpr int VK_OEM_6_ = 0xDD; // ]
constexpr int MOUSE_MIDDLE = -97;
constexpr int MOUSE_LEFT   = -99;
constexpr int MOUSE_RIGHT  = -98;
constexpr int MOUSE_X1     = -96;
constexpr int MOUSE_X2     = -95;

// SB KeybindHandler + SC ClientEventHandler. Toggle Upgrade 1/2 default to Alt+Z / Alt+X; 3-5 are
// unbound and have no modifier (SB registers them without KeyModifier).
std::vector<KeyDef> const& keyDefs() {
    static std::vector<KeyDef> const defs{
        {"open_backpack", {'B'}, KeyAction::OpenBackpack},
        {"inventory_interaction", {'C'}, KeyAction::InventoryInteraction},
        {"tool_swap", {}, KeyAction::ToolSwap},
        {"toggle_upgrade_1", {'Z'}, KeyAction::ToggleUpgrade1, true},
        {"toggle_upgrade_2", {'X'}, KeyAction::ToggleUpgrade2, true},
        {"toggle_upgrade_3", {}, KeyAction::ToggleUpgrade3},
        {"toggle_upgrade_4", {}, KeyAction::ToggleUpgrade4},
        {"toggle_upgrade_5", {}, KeyAction::ToggleUpgrade5},
        {"sort", {MOUSE_MIDDLE}, KeyAction::Sort, false, true},
        {"transfer_to_storage", {VK_OEM_4_}, KeyAction::TransferToStorage, false, true},
        {"transfer_to_inventory", {VK_OEM_6_}, KeyAction::TransferToInventory, false, true},
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
    std::string summary;
    for (std::size_t i = 0; i < defs.size(); ++i) {
        summary += " " + std::string{defs[i].name} + "=" + std::to_string(gKeys[i][0].load());
        if (int second = gKeys[i][1].load()) summary += "," + std::to_string(second);
    }
    static std::string logged;
    if (summary != logged) {
        logged = summary;
        log().info("keys:{}", summary);
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

/// The player's own inventory screen (SB: `containerMenu instanceof InventoryMenu`).
bool inventoryScreen(std::string const& name) { return name.find("inventory_screen") != std::string::npos; }

/// Sends `openAt` once the vanilla screen we asked to close is gone (SB: opening a backpack from
/// a chest replaces the chest's menu; going back closes everything). Gives up after ~1 s.
void sendWhenClosed(nlohmann::json body, int framesLeft) {
    auto& snap = snapshot();
    if (!snap.cursorScreen.load() || framesLeft <= 0) {
        net::sendKeyAction(std::move(body));
        return;
    }
    post([body = std::move(body), framesLeft]() mutable { sendWhenClosed(std::move(body), framesLeft - 1); });
}

std::optional<nlohmann::json> openTarget(HoveredSlot const& slot);

/// B over a backpack in one of the player's own slots of a vanilla container screen.
bool openHovered() {
    auto slot = hoveredVanillaSlot();
    if (!slot) return false;
    auto const& item = hoveredVanillaItem();
    if (item.isNull() || !game::isBackpack(item) || item.mCount != 1) return false;
    auto target = openTarget(*slot);
    if (!target) return false;
    (*target)["action"] = "openAt";
    auto* client        = currentClient();
    auto  screen        = client ? client->getScreenName() : std::string{};
    log().info("key: open backpack in {} #{} (screen {})", slot->collection, slot->index, screen);
    if (!inventoryScreen(screen) && client) {
        // Not the inventory: the container screen closes (the backpack replaces it).
        client->getSceneFactory().getCurrentSceneStack()->schedulePopScreen(1);
        sendWhenClosed(std::move(*target), 60);
        return true;
    }
    net::sendKeyAction(std::move(*target));
    return true;
}

/// Where a hovered vanilla slot is on the player, for Open Backpack over a backpack.
std::optional<nlohmann::json> openTarget(HoveredSlot const& slot) {
    auto const& c = slot.collection;
    int         i = slot.index;
    if (c == "hotbar_items" && i >= 0 && i < 9) return nlohmann::json{{"where", "inventory"}, {"slot", i}};
    if (c == "combined_hotbar_and_inventory_items" && i >= 0 && i < 36) return nlohmann::json{{"where", "inventory"}, {"slot", i}};
    // The main inventory grid counts from 0 (= player inventory slot 9).
    if (c == "inventory_items" && i >= 0 && i < 27) return nlohmann::json{{"where", "inventory"}, {"slot", i + 9}};
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
        // The backpack screen (a mouse-button binding the screen layer didn't see).
        if (ui::nativeScreenActive()) {
            ui::nativeScreenOpenBackpackKey();
            return;
        }
        // Any container screen: a backpack in the player's own slots under the cursor.
        openHovered();
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

// ---- saved bindings ----------------------------------------------------------------------------------
// The game reads options.txt before mod key bindings exist, so a rebinding saved there (from the
// controls screen or by hand) would be dropped and then overwritten with the defaults. We read our
// own "keyboard_type_0_key.AlleviatedBackpacks.<name>:<codes>" lines once and put them into the
// layout, which the game then shows and saves like any vanilla control.

std::filesystem::path gModDir; // set at load (registerHotkeys)
bool                  gSavedApplied = false;

std::optional<std::filesystem::path> optionsFile() {
    namespace fs = std::filesystem;
    std::vector<fs::path> roots;
    if (!gModDir.empty()) roots.push_back(gModDir.parent_path().parent_path() / "Minecraft Bedrock" / "Users"); // LeviLauncher
    if (auto const* appData = std::getenv("APPDATA")) roots.push_back(fs::path(appData) / "Minecraft Bedrock" / "Users");
    std::optional<fs::path>        best;
    fs::file_time_type             bestTime{};
    std::error_code                ec;
    for (auto const& root : roots) {
        if (!fs::is_directory(root, ec)) continue;
        for (auto const& user : fs::directory_iterator(root, ec)) {
            auto file = user.path() / "games" / "com.mojang" / "minecraftpe" / "options.txt";
            if (!fs::is_regular_file(file, ec)) continue;
            auto t = fs::last_write_time(file, ec);
            if (!best || t > bestTime) best = file, bestTime = t;
        }
        if (best) break; // the launcher's own data first
    }
    return best;
}

std::unordered_map<std::string, std::vector<int>> savedBindings() {
    std::unordered_map<std::string, std::vector<int>> out;
    auto file = optionsFile();
    if (!file) return out;
    std::ifstream in(*file);
    std::string   line;
    std::string const prefix = "keyboard_type_0_" + std::string{KEY_PREFIX};
    while (std::getline(in, line)) {
        if (!line.starts_with(prefix)) continue;
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        auto name = line.substr(prefix.size(), colon - prefix.size());
        std::vector<int> codes;
        std::stringstream list(line.substr(colon + 1));
        for (std::string part; std::getline(list, part, ',');) {
            try {
                codes.push_back(std::stoi(part));
            } catch (...) {
            }
        }
        if (!codes.empty()) out[name] = std::move(codes);
    }
    return out;
}

void applySavedBindings(RemappingLayout& layout) {
    if (gSavedApplied) return;
    auto const& defs  = keyDefs();
    bool        known = false;
    for (auto const& def : defs) {
        auto action = std::string{KEY_PREFIX} + std::string{def.name};
        if (*layout.getKeymappingByAction(action).mAction == action) known = true;
    }
    if (!known) return; // our bindings are not in this layout yet
    gSavedApplied = true;
    auto saved    = savedBindings();
    for (auto const& def : defs) {
        auto it = saved.find(std::string{def.name});
        if (it == saved.end()) continue;
        auto        action  = std::string{KEY_PREFIX} + std::string{def.name};
        auto const& mapping = layout.getKeymappingByAction(action);
        if (*mapping.mAction != action || *mapping.mKeys == it->second) continue;
        layout.setMapping(action, it->second);
        log().info("keys: saved binding {} restored", def.name);
    }
}
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
    if (auto layout = mKeyboardRemappingLayout.lock()) {
        applySavedBindings(*layout);
        readLayout(*layout);
    }
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
        std::string                localeCode  = locale ? locale->getFullLanguageCode() : std::string{};
        if (rest.ends_with(DESC_SUFFIX)) {
            // Bedrock's controls list has no categories: the description carries SB's category.
            rest.remove_suffix(DESC_SUFFIX.size());
            bool core = false;
            for (auto const& def : keyDefs()) {
                if (def.name == rest) core = def.core;
            }
            return core ? std::string(translate("abp.input.key.category.core", "Sophisticated Mods", localeCode))
                        : std::string(translate("abp.input.key.category.main", "Sophisticated Backpacks", localeCode));
        }
        if (rest.ends_with(NAME_SUFFIX)) rest.remove_suffix(NAME_SUFFIX.size());
        return std::string(translate("abp.input.key." + std::string{rest}, rest, localeCode));
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
        gModDir = abp::Mod::getInstance().getSelf().getModDir();
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

bool onHotkeyKeyDown(int vk) {
    if (!gActive.load() || vk <= 0) return false;
    auto& snap = snapshot();
    if (snap.vanillaTextFocus.load() || snap.textFocus.load()) return false;
    bool const alt     = altDown();
    bool const inLevel = snap.inLevel.load();
    bool const inGame  = inLevel && !snap.cursorScreen.load();
    bool       consume = false;
    auto const& defs   = keyDefs();
    for (std::size_t i = 0; i < defs.size(); ++i) {
        if (defs[i].alt && !alt) continue;
        if (!bound(i, vk)) continue;
        auto action = defs[i].action;
        // The key is ours when the binding acts here (SB's key conflict contexts): the game must
        // not also see it (B is the emote key in Bedrock).
        switch (action) {
        case KeyAction::OpenBackpack:
            consume |= inGame || (snap.cursorScreen.load() && snap.hoverBackpack.load());
            break;
        case KeyAction::InventoryInteraction:
        case KeyAction::ToolSwap:
            consume |= inGame;
            break;
        case KeyAction::ToggleUpgrade1:
        case KeyAction::ToggleUpgrade2:
        case KeyAction::ToggleUpgrade3:
        case KeyAction::ToggleUpgrade4:
        case KeyAction::ToggleUpgrade5:
            consume |= inLevel;
            break;
        default:
            break;
        }
        post([action] { run(action); });
    }
    return consume && snap.serverReady.load();
}

bool onHotkeyMouseDown(int code) {
    if (!gActive.load() || code == 0) return false;
    auto&       snap    = snapshot();
    bool        consume = false;
    bool const  alt     = altDown();
    auto const& defs    = keyDefs();
    for (std::size_t i = 0; i < defs.size(); ++i) {
        if ((defs[i].alt && !alt) || !bound(i, code)) continue;
        auto action = defs[i].action;
        // SB cancels the click when Open Backpack acted on the backpack under the cursor.
        if (action == KeyAction::OpenBackpack && snap.cursorScreen.load() && snap.hoverBackpack.load()) consume = true;
        post([action] { run(action); });
    }
    return consume;
}

int mouseButtonCode(ui::MouseButton button) { return mouseCode(button); }
int mouseExtraCode(int which) { return which == 1 ? MOUSE_X1 : MOUSE_X2; }

} // namespace detail

bool keyMatches(KeyAction action, int vk) { return vk > 0 && detail::bound(detail::indexOf(action), vk); }

std::string keyDisplayName(KeyAction action) {
    auto i = detail::indexOf(action);
    if (i >= detail::MAX_KEYS) return {};
    int code = detail::gKeys[i][0].load();
    switch (code) {
    case 0:
        return ctr("abp.input.key.not_bound", "Not Bound");
    case detail::MOUSE_LEFT:
        return ctr("abp.input.key.mouse_left", "Left Button");
    case detail::MOUSE_RIGHT:
        return ctr("abp.input.key.mouse_right", "Right Button");
    case detail::MOUSE_MIDDLE:
        return ctr("abp.input.key.mouse_middle", "Middle Button");
    case detail::MOUSE_X1:
        return ctr("abp.input.key.mouse_4", "Button 4");
    case detail::MOUSE_X2:
        return ctr("abp.input.key.mouse_5", "Button 5");
    case detail::VK_OEM_4_:
        return "[";
    case detail::VK_OEM_6_:
        return "]";
    default:
        break;
    }
    if ((code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9')) return std::string(1, static_cast<char>(code));
    return "#" + std::to_string(code);
}

bool mouseMatches(KeyAction action, ui::MouseButton button) {
    return detail::bound(detail::indexOf(action), detail::mouseCode(button));
}

} // namespace abp::client::input

#endif // ABP_CLIENT
