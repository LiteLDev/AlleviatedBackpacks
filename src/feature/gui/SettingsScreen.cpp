// Server side of the native settings screen (SB BackpackSettingsScreen / SC SettingsContainerMenu):
// the screen's data (`m.extra["settings"]`) and its actions. Everything runs on the server thread.
//
// Actions (`{"action":"setting", ...}` on the open backpack's native session):
//   {"what":"context"}                                     toggle Player <-> Backpack context
//   {"what":"main","name":"<SB setting>"}                  toggle a main setting in the context
//   {"what":"memory","op":"selectAll|unselectAll|select|unselect","index":n}
//   {"what":"memoryIgnoreNbt","value":b}                   (no value: toggle)
//   {"what":"noSort","op":"selectAll|unselectAll|select|unselect","index":n}
//   {"what":"noSortColor","dye":n} | {"what":"noSortColor","dir":1|-1}
//   {"what":"itemDisplay","op":"select|unselect|rotate|zOffset|color","index":n,"clockwise":b,"change":n,"dye":n}
//   {"what":"template","op":"scrollSave|scrollLoad","next":b} | {"op":"save","name":s} | {"op":"load"}
//                                                          | {"op":"export","fileName":s}
//   {"what":"searchPhrase","value":s}                      the storage screen's search box
// G2 names still work: {"what":"option","name":..}, {"what":"memoryMatchNbt"}.

#include <algorithm>
#include <cctype>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/event/server/ServerStoppingEvent.h"

#include "mc/world/actor/player/Player.h"

#include "core/Appearance.h"
#include "feature/gui/Gui.h"
#include "feature/gui/SbSettings.h"
#include "feature/templates/Templates.h"
#include "game/Backpack.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace abp::feature::gui {

namespace {
using nlohmann::json;

constexpr int kTotalOrdinalSaveSlots = 10; // SC TOTAL_ORDINAL_SAVE_SLOTS

struct PersistSlot {
    enum class Kind { Ordinal, Named, Pack, EditName } kind = Kind::Ordinal;
    int         ordinal = 0;
    std::string name; ///< named / pack template name / edit-name slot's typed name
    std::string pack;

    [[nodiscard]] std::string slotName() const { return kind == Kind::Ordinal ? std::to_string(ordinal) : name; }
};

struct ScreenState {
    bool                     storageContext = false; ///< SC Context: PLAYER (false) / STORAGE
    bool                     opened         = false; ///< the settings screen was opened this session
    std::vector<PersistSlot> saveSlots;
    std::vector<PersistSlot> loadSlots;
    int                      saveIndex = 0;
    int                      loadIndex = -1;
};

std::unordered_map<std::int64_t, ScreenState> gStates; // server thread

std::int64_t keyOf(Player const& p) { return p.getOrCreateUniqueID().rawID; }
ScreenState& stateOf(Player const& p) { return gStates[keyOf(p)]; }

/// SC TemplatePersistanceContainer.initSlots.
void initSlots(Player& player, ScreenState& s) {
    auto ordinals = templates::playerTemplates(player);
    auto named    = templates::playerNamedTemplates(player);
    s.saveSlots.clear();
    int n = static_cast<int>(ordinals.size());
    int i = 0;
    for (; i < n; ++i) s.saveSlots.push_back({PersistSlot::Kind::Ordinal, i + 1, {}, {}});
    if (i < kTotalOrdinalSaveSlots - 1) s.saveSlots.push_back({PersistSlot::Kind::Ordinal, i + 1, {}, {}});
    for (auto const& [name, _] : named) s.saveSlots.push_back({PersistSlot::Kind::Named, 0, name, {}});
    s.saveSlots.push_back({PersistSlot::Kind::EditName, 0, {}, {}});

    s.loadSlots.clear();
    for (i = 0; i < n; ++i) s.loadSlots.push_back({PersistSlot::Kind::Ordinal, i + 1, {}, {}});
    for (auto const& [name, _] : named) s.loadSlots.push_back({PersistSlot::Kind::Named, 0, name, {}});
    for (auto const& [pack, list] : templates::packSettingsTemplates()) {
        for (auto const& [name, _] : list) s.loadSlots.push_back({PersistSlot::Kind::Pack, 0, name, pack});
    }
    if (s.loadIndex == -1 && !s.loadSlots.empty()) s.loadIndex = 0;
    else if (s.loadIndex != -1 && s.loadSlots.empty()) s.loadIndex = -1;
    if (s.loadIndex >= static_cast<int>(s.loadSlots.size())) s.loadIndex = static_cast<int>(s.loadSlots.size()) - 1;
    s.saveIndex = std::clamp(s.saveIndex, 0, static_cast<int>(s.saveSlots.size()) - 1);
}

json settingsOf(Player& player, PersistSlot const& slot) {
    switch (slot.kind) {
    case PersistSlot::Kind::Ordinal: {
        auto all = templates::playerTemplates(player);
        auto it  = all.find(slot.ordinal);
        return it != all.end() ? it->second : json::object();
    }
    case PersistSlot::Kind::Named:
    case PersistSlot::Kind::EditName: {
        auto all = templates::playerNamedTemplates(player);
        auto it  = all.find(slot.name);
        return it != all.end() ? it->second : json::object();
    }
    case PersistSlot::Kind::Pack: {
        auto const& packs = templates::packSettingsTemplates();
        auto        p     = packs.find(slot.pack);
        if (p == packs.end()) return json::object();
        auto t = p->second.find(slot.name);
        return t != p->second.end() ? t->second : json::object();
    }
    }
    return json::object();
}

PersistSlot const* loadSlot(ScreenState const& s) {
    if (s.loadIndex < 0 || s.loadIndex >= static_cast<int>(s.loadSlots.size())) return nullptr;
    return &s.loadSlots[static_cast<std::size_t>(s.loadIndex)];
}

void message(Player& player, std::string const& text) { player.sendMessage(text); }

bool templateAction(Player& player, game::Backpack& bp, ScreenState& s, json const& body) {
    auto op = body.value("op", std::string{});
    if (s.saveSlots.empty()) initSlots(player, s);
    if (op == "scrollSave") {
        int n = static_cast<int>(s.saveSlots.size());
        s.saveIndex += body.value("next", true) ? 1 : -1;
        if (s.saveIndex < 0) s.saveIndex = n - 1;
        if (s.saveIndex >= n) s.saveIndex = 0;
        return true;
    }
    if (op == "scrollLoad") {
        if (s.loadSlots.empty()) {
            s.loadIndex = -1;
            return true;
        }
        int n = static_cast<int>(s.loadSlots.size());
        s.loadIndex += body.value("next", true) ? 1 : -1;
        if (s.loadIndex < 0) s.loadIndex = n - 1;
        if (s.loadIndex >= n) s.loadIndex = 0;
        return true;
    }
    if (op == "save") {
        auto slot = s.saveSlots[static_cast<std::size_t>(s.saveIndex)];
        if (slot.kind == PersistSlot::Kind::EditName) {
            slot.name = body.value("name", std::string{});
            if (slot.name.empty() || slot.name.size() > 64) return false; // a blank name would make an unnamed slot
        }
        auto settings = settingsTemplateOf(bp);
        if (slot.kind == PersistSlot::Kind::Ordinal) templates::putPlayerTemplate(player, slot.ordinal, settings);
        else if (slot.kind != PersistSlot::Kind::Pack) templates::putPlayerNamedTemplate(player, slot.name, settings);
        initSlots(player, s);
        // SC moveSaveSlotIndexTo(slot name): the first save slot with that name.
        for (int i = 0; i < static_cast<int>(s.saveSlots.size()); ++i) {
            if (s.saveSlots[static_cast<std::size_t>(i)].slotName() == slot.slotName()) {
                s.saveIndex = i;
                break;
            }
        }
        message(player, trFor(player, "abp.sbsettings.message.save_template", "Saved Settings to slot {}", slot.slotName()));
        log().info("settings: {} saved template slot '{}'", player.getRealName(), slot.slotName());
        return true;
    }
    if (op == "load") {
        auto const* slot = loadSlot(s);
        if (!slot) return false;
        loadSettingsTemplate(bp, settingsOf(player, *slot));
        message(player, trFor(player, "abp.sbsettings.message.load_template", "Loaded Settings from slot {}", slot->slotName()));
        log().info("settings: {} loaded template slot '{}'", player.getRealName(), slot->slotName());
        return true;
    }
    if (op == "export") {
        auto fileName = body.value("fileName", std::string{});
        if (fileName.empty()) {
            message(player, "§c" + trFor(player, "abp.sbsettings.message.export_template.empty_name", "File name can't be empty"));
            return false;
        }
        if (auto bad = templates::invalidFileNameCharacters(fileName); !bad.empty()) {
            message(player, "§c" + trFor(player, "abp.sbsettings.message.export_template.invalid_characters", "File name contains invalid character(s): {}", bad));
            return false;
        }
        std::replace(fileName.begin(), fileName.end(), ' ', '_');
        std::transform(fileName.begin(), fileName.end(), fileName.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        auto path = templates::exportSettingsTemplate(player, fileName, settingsTemplateOf(bp));
        if (!path) return false;
        initSlots(player, s);
        message(player, trFor(player, "abp.sbsettings.message.export_template", "Exported Settings to {}", *path));
        log().info("settings: {} exported template to {}", player.getRealName(), *path);
        return true;
    }
    return false;
}

bool slotAction(Player&, game::Backpack& bp, std::string const& what, json const& body) {
    auto  op  = body.value("op", std::string{});
    int   idx = body.value("index", -1);
    auto& inv = bp.inventory();
    bool  memoryCat = what == "memory";
    if (op == "selectAll") {
        if (memoryCat) memorizeAll(bp);
        else setNoSortAll(bp, true);
        return true;
    }
    if (op == "unselectAll") {
        if (memoryCat) clearMemory(bp);
        else setNoSortAll(bp, false);
        return true;
    }
    if (idx < 0 || idx >= inv.size()) return false;
    if (memoryCat) {
        auto const& mem = memory(bp);
        bool        selected = !mem[static_cast<std::size_t>(idx)].empty();
        if (op == "select") {
            if (selected || inv.at(idx).empty()) return false; // SC: only slots with an item
            setMemory(bp, idx, inv.at(idx));
            return true;
        }
        if (op == "unselect") {
            if (!selected) return false;
            setMemory(bp, idx, {});
            return true;
        }
        return false;
    }
    bool selected = noSort(bp)[static_cast<std::size_t>(idx)];
    if (op == "select") {
        if (selected) return false;
        setNoSort(bp, idx, true);
        return true;
    }
    if (op == "unselect") {
        if (!selected) return false;
        setNoSort(bp, idx, false);
        return true;
    }
    return false;
}

bool itemDisplayAction(game::Backpack& bp, json const& body) {
    if (config::get<GuiConfig>().itemDisplayDisabled) return false;
    auto op  = body.value("op", std::string{});
    int  idx = body.value("index", -1);
    if (op == "select") return selectItemDisplaySlot(bp, idx);
    if (op == "unselect") return unselectItemDisplaySlot(bp, idx);
    if (op == "rotate") {
        rotateItemDisplay(bp, idx, body.value("clockwise", true));
        return true;
    }
    if (op == "zOffset") {
        changeItemDisplayZOffset(bp, idx, std::clamp(body.value("change", 1), -32, 32));
        return true;
    }
    if (op == "color") {
        setItemDisplayColor(bp, body.value("dye", -1));
        return true;
    }
    return false;
}

class SettingsScreenModule {
public:
    void install() {
        mDisconnect = ll::event::EventBus::getInstance().emplaceListener<ll::event::PlayerDisconnectEvent>(
            [](ll::event::PlayerDisconnectEvent& ev) { gStates.erase(keyOf(ev.self())); }
        );
        mStopping = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerStoppingEvent>([](auto&) { gStates.clear(); });
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mDisconnect);
        ll::event::EventBus::getInstance().removeListener(mStopping);
        gStates.clear();
    }

private:
    ll::event::ListenerPtr mDisconnect;
    ll::event::ListenerPtr mStopping;
};
} // namespace

void settingsScreenOpened(Player& player, game::Backpack&) {
    auto& s          = stateOf(player);
    s                = {};
    s.opened         = true;
    s.storageContext = false;
    initSlots(player, s);
}

nlohmann::json settingsScreenJson(Player& player, game::Backpack& backpack) {
    auto const& cfg = config::get<GuiConfig>();
    json        j   = json::object();
    auto        it  = gStates.find(keyOf(player));
    bool        storageCtx = it != gStates.end() && it->second.storageContext;
    j["context"]           = storageCtx ? "storage" : "player";
    json main              = json::object();
    for (auto const& m : mainSettings()) {
        main[m.key] = storageCtx ? settingValue(player, backpack, m.setting) : playerSettingOrDefault(player, m.setting);
    }
    j["main"] = std::move(main);
    // Effective values for the storage screen.
    j["shiftClickOpenTab"]       = settingValue(player, backpack, MainSetting::ShiftClickOpenTab);
    j["keepTabOpen"]             = settingValue(player, backpack, MainSetting::KeepTabOpen);
    j["keepSearchPhrase"]        = settingValue(player, backpack, MainSetting::KeepSearchPhrase);
    j["searchPhrase"]            = searchPhrase(player, backpack);
    j["showAnotherPlayerCanOpen"] = cfg.allowOpeningOtherPlayerBackpacks;
    j["itemDisplayEnabled"]      = !cfg.itemDisplayDisabled;
    j["noSortColor"]             = noSortColor(backpack);
    j["memoryIgnoreNbt"]         = memoryIgnoresNbt(backpack);
    auto d                       = itemDisplay(backpack);
    j["itemDisplay"] = json{{"slots", d.slots}, {"rotations", d.rotations}, {"zOffsets", d.zOffsets}, {"color", d.color}};
    if (it != gStates.end() && it->second.opened) {
        auto& s = it->second;
        if (s.saveSlots.empty()) initSlots(player, s);
        auto const& save = s.saveSlots[static_cast<std::size_t>(s.saveIndex)];
        json        t    = json::object();
        t["save"]        = json{{"name", save.slotName()}, {"custom", save.kind == PersistSlot::Kind::EditName}};
        json load        = json{{"index", s.loadIndex}, {"count", s.loadSlots.size()}};
        if (auto const* slot = loadSlot(s)) {
            auto settings     = settingsOf(player, *slot);
            load["name"]      = slot->slotName();
            load["source"]    = slot->kind == PersistSlot::Kind::Pack ? slot->pack : std::string{};
            load["tooLarge"]  = settingsTemplateTooLarge(settings, backpack.inventory().size());
            load["preview"]   = settingsTemplatePreview(settings);
        }
        t["load"]      = std::move(load);
        j["templates"] = std::move(t);
    }
    return j;
}

bool handleSettingsScreenAction(Player& player, game::Backpack& bp, nlohmann::json const& body) {
    auto  what = body.value("what", std::string{});
    auto& s    = stateOf(player);
    if (what == "closed") {
        // The client left the settings view: stop sending the template block.
        s.opened = false;
        return true;
    }
    if (what == "context") {
        s.storageContext = !s.storageContext;
        return true;
    }
    if (what == "main" || what == "option") {
        auto setting = mainSettingByKey(body.value("name", std::string{}));
        if (!setting) return false;
        if (*setting == MainSetting::AnotherPlayerCanOpen && !config::get<GuiConfig>().allowOpeningOtherPlayerBackpacks) return false;
        // SC toggleBooleanSetting.
        if (!s.storageContext) setPlayerSetting(player, *setting, !playerSettingOrDefault(player, *setting));
        else setStorageSetting(player, bp, *setting, !settingValue(player, bp, *setting));
        if (*setting == MainSetting::KeepSearchPhrase) {
            // SC clearSearchPhraseIfOff.
            bool keep = s.storageContext ? settingValue(player, bp, *setting) : playerSettingOrDefault(player, *setting);
            if (!keep && !searchPhrase(player, bp).empty()) {
                if (!s.storageContext) setPlayerOption(player, "searchPhrase", std::nullopt);
                else detail::guiSection(bp).erase("searchPhrase"), bp.markDirty();
            }
        }
        return true;
    }
    if (what == "memory" || what == "noSort") return slotAction(player, bp, what, body);
    if (what == "memoryIgnoreNbt" || what == "memoryMatchNbt") {
        bool ignore = body.contains("value") && body["value"].is_boolean() ? body["value"].get<bool>() : !memoryIgnoresNbt(bp);
        setMemoryIgnoreNbt(bp, ignore);
        return true;
    }
    if (what == "noSortColor") {
        int n = static_cast<int>(core::kDyes.size());
        if (body.contains("dye")) setNoSortColor(bp, body.value("dye", -1));
        else setNoSortColor(bp, (noSortColor(bp) + (body.value("dir", 1) >= 0 ? 1 : n - 1)) % n);
        return true;
    }
    if (what == "itemDisplay") return itemDisplayAction(bp, body);
    if (what == "template") return templateAction(player, bp, s, body);
    if (what == "searchPhrase") {
        auto v = body.value("value", std::string{});
        if (v.size() > 256) return false;
        setSearchPhrase(player, bp, v);
        return true;
    }
    return false;
}

} // namespace abp::feature::gui

ABP_REGISTER_MODULE(abp::feature::gui::SettingsScreenModule);
