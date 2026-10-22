// Server-side item names. A dedicated server never loads resource-pack texts, so the names of our
// items (`item.alleviatedbackpacks:<id>.name`, from the RP's texts/<locale>.lang) resolved on the
// server stay raw keys, e.g. in command output that reaches remote clients as plain text. The mod
// ships the texts in its resource pack folder; a lookup the game can't answer for one of our
// item keys is answered from them (the locale's file, else en_US).

#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>

#include "ll/api/memory/Hook.h"

#include "mc/locale/Localization.h"

#include "mod/Log.h"
#include "mod/Main.h"
#include "mod/Module.h"

namespace abp::tiers {

namespace {

constexpr std::string_view kPrefix = "item.alleviatedbackpacks:";

/// locale ("en_US") -> key -> text
std::unordered_map<std::string, std::unordered_map<std::string, std::string>> gTexts;

void loadTexts() {
    gTexts.clear();
    auto dir = Mod::getInstance().getSelf().getModDir() / "resource_packs" / "AlleviatedBackpacks_RP" / "texts";
    std::error_code ec;
    for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.path().extension() != ".lang") continue;
        auto&         table = gTexts[entry.path().stem().string()];
        std::ifstream in(entry.path());
        std::string   line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.starts_with(kPrefix)) continue;
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            table.emplace(line.substr(0, eq), line.substr(eq + 1));
        }
    }
}

std::string const* lookup(std::string const& locale, std::string const& id) {
    if (auto t = gTexts.find(locale); t != gTexts.end()) {
        if (auto it = t->second.find(id); it != t->second.end()) return &it->second;
    }
    if (auto t = gTexts.find("en_US"); t != gTexts.end()) {
        if (auto it = t->second.find(id); it != t->second.end()) return &it->second;
    }
    return nullptr;
}

} // namespace

LL_TYPE_INSTANCE_HOOK(
    ItemNameLocalizationHook,
    ll::memory::HookPriority::Normal,
    Localization,
    &Localization::get,
    bool,
    std::string const&              id,
    std::string&                    out,
    std::vector<std::string> const& params
) {
    if (origin(id, out, params)) return true;
    if (!id.starts_with(kPrefix)) return false;
    auto const* text = lookup(*mCode, id);
    if (!text) return false;
    out = *text;
    return true;
}

/// Installed at mod load (texts are read from the mod folder; names can be asked for any time).
class ItemNames {
public:
    void load() {
        loadTexts();
        hook();
    }
    void install() { hook(); }
    void uninstall() {
        if (!mHooked) return;
        ItemNameLocalizationHook::unhook();
        mHooked = false;
    }

private:
    void hook() {
        if (mHooked) return;
        ItemNameLocalizationHook::hook();
        mHooked = true;
    }
    bool mHooked = false;
};

} // namespace abp::tiers

ABP_REGISTER_MODULE(abp::tiers::ItemNames);
