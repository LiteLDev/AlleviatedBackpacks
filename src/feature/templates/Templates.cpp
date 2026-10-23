// Settings templates, backpack templates, template packs and dynamic templates. See Templates.h.

#include "feature/templates/Templates.h"
#include "feature/templates/Snbt.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/server/ServerStoppingEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/platform/UUID.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/storage/LevelStorage.h"
#include "mc/world/level/storage/db_helpers/Category.h"

#include "core/BackpackData.h"
#include "game/Backpack.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/Storage.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "upgrade/UpgradeManager.h"

namespace abp::feature::templates {

namespace {
using nlohmann::json;
namespace fs = std::filesystem;

constexpr char const* kSettingsKey  = "abp:settings_templates";
constexpr char const* kBackpacksKey = "abp:backpack_templates";
constexpr char const* kSettingsDir  = "sophisticated_settingstemplates";
constexpr char const* kBackpacksDir = "sophisticatedbackpacks_templates";

// ---- world DB blobs ---------------------------------------------------------------------------------
LevelStorage* levelStorage() {
    auto level = ll::service::getLevel();
    return level ? &level->getLevelStorage() : nullptr;
}

struct WorldBlob {
    char const* key;
    bool        loaded = false;
    json        value  = json::object();

    json& get() {
        if (loaded) return value;
        loaded = true;
        value  = json::object();
        if (auto* db = levelStorage()) {
            std::string buffer;
            if (db->loadData(key, buffer, DBHelpers::Category::Uncategorized)) {
                auto parsed = json::parse(buffer, nullptr, false);
                if (parsed.is_object()) value = std::move(parsed);
                else log().warn("templates: {} unreadable; starting empty", key);
            }
        }
        return value;
    }
    void save() {
        if (auto* db = levelStorage()) db->saveData(key, value.dump(), DBHelpers::Category::Uncategorized);
    }
    void reset() {
        loaded = false;
        value  = json::object();
    }
};
WorldBlob gSettingsBlob{kSettingsKey};
WorldBlob gBackpacksBlob{kBackpacksKey};

std::string uuidOf(Player& p) { return p.getUuid().asString(); }

// ---- template packs -------------------------------------------------------------------------------------
PackTemplates                         gPackSettings;
std::map<std::string, json>           gPackBackpacks; // "ns:path" -> template

std::string capitalizeWords(std::string name) {
    std::replace(name.begin(), name.end(), '_', ' ');
    std::string out, word;
    std::istringstream in(name);
    while (in >> word) {
        word[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(word[0])));
        out += (out.empty() ? "" : " ") + word;
    }
    return out;
}

std::optional<json> readJsonFile(fs::path const& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::stringstream ss;
    ss << in.rdbuf();
    auto j = json::parse(ss.str(), nullptr, false, true);
    if (j.is_discarded() || !j.is_object()) return std::nullopt;
    return j;
}

/// A template file: SB's `.snbt` (TagParser), or the `.json` files of earlier exports.
std::optional<json> readTemplateFile(fs::path const& path) {
    if (path.extension() == ".json") return readJsonFile(path);
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::stringstream ss;
    ss << in.rdbuf();
    auto j = snbtToJson(ss.str());
    if (!j || !j->is_object()) return std::nullopt;
    return j;
}

bool isTemplateFile(fs::path const& path) { return path.extension() == ".snbt" || path.extension() == ".json"; }

/// A template as it comes back from its `.snbt` file (null members dropped, one type per list).
json asReadBack(json const& j) {
    auto tag = jsonToTag(j);
    return tag ? tagToJson(*tag) : j;
}

bool writeJsonFile(fs::path const& path, json const& j) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        log().error("templates: cannot create {}: {}", path.parent_path().string(), ec.message());
        return false;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        log().error("templates: cannot write {}", path.string());
        return false;
    }
    out << j.dump(4) << '\n';
    return static_cast<bool>(out);
}

/// SB `NbtUtils.structureToSnbt` + `Files.writeString`: the template as SNBT. An older `.json`
/// export of the same template is removed, so the new file is the only one.
bool writeSnbtFile(fs::path const& path, json const& j) {
    auto snbt = jsonToSnbt(j);
    if (!snbt) {
        log().error("templates: {} is not a compound", path.string());
        return false;
    }
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        log().error("templates: cannot create {}: {}", path.parent_path().string(), ec.message());
        return false;
    }
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            log().error("templates: cannot write {}", path.string());
            return false;
        }
        out << *snbt << '\n';
        if (!out) return false;
    }
    auto legacy = path;
    legacy.replace_extension(".json");
    fs::remove(legacy, ec);
    return true;
}

/// SB writes pack.mcmeta into a new data pack; the equivalent marks the folder as a template pack.
void writePackMeta(fs::path const& packRoot, std::string const& description) {
    auto meta = packRoot / "pack.json";
    if (fs::exists(meta)) return;
    (void)writeJsonFile(meta, json{{"pack", {{"description", description}}}});
}

std::string packRelative(fs::path const& p) {
    std::error_code ec;
    auto            rel = fs::relative(p, templatePacksRoot().parent_path(), ec);
    return (ec ? p : rel).generic_string();
}

void loadPacks() {
    gPackSettings.clear();
    gPackBackpacks.clear();
    auto            root = templatePacksRoot();
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return;
    int settings = 0, backpacks = 0;
    for (auto const& pack : fs::directory_iterator(root, ec)) {
        auto data = pack.path() / "data";
        if (!pack.is_directory() || !fs::is_directory(data, ec)) continue;
        for (auto const& ns : fs::directory_iterator(data, ec)) {
            if (!ns.is_directory()) continue;
            auto nsName = ns.path().filename().string();
            for (auto const* dir : {kSettingsDir, kBackpacksDir}) {
                auto base = ns.path() / dir;
                if (!fs::is_directory(base, ec)) continue;
                for (auto const& f : fs::recursive_directory_iterator(base, ec)) {
                    if (!f.is_regular_file() || !isTemplateFile(f.path())) continue;
                    auto j = readTemplateFile(f.path());
                    if (!j) {
                        log().error("templates: couldn't parse template file {}", f.path().string());
                        continue;
                    }
                    if (std::string_view(dir) == kSettingsDir) {
                        putPackSettingsTemplate(nsName, f.path().stem().string(), *j);
                        ++settings;
                    } else {
                        auto rel = fs::relative(f.path(), base, ec).replace_extension().generic_string();
                        gPackBackpacks[nsName + ":" + rel] = *j;
                        ++backpacks;
                    }
                }
            }
        }
    }
    log().info("templates: loaded {} settings and {} backpack templates from {}", settings, backpacks, root.string());
}

void setUserData(ItemStack& stack, std::function<void(CompoundTag&)> const& edit) {
    auto tag = stack.mUserData ? stack.mUserData->clone() : std::make_unique<CompoundTag>();
    edit(*tag);
    stack.setUserData(std::move(tag));
}

// ---- dynamic templates --------------------------------------------------------------------------------
struct Dynamic {
    core::BackpackData               data;
    std::vector<core::ItemStackData> forInventory;
    std::vector<core::ItemStackData> forUpgrades;
    std::int64_t                     lastAccess = 0;
};
std::unordered_map<std::string, Dynamic> gDynamic;
std::int64_t                             gClockOffset = 0;
constexpr std::int64_t                   kExpireSeconds = 5 * 60; // SB expireAfterAccess(5, MINUTES)

std::int64_t nowSeconds() {
    using namespace std::chrono;
    return duration_cast<seconds>(steady_clock::now().time_since_epoch()).count() + gClockOffset;
}

void expireDynamic() {
    auto now = nowSeconds();
    std::erase_if(gDynamic, [now](auto const& e) { return now - e.second.lastAccess >= kExpireSeconds; });
}

Dynamic* dynamicOf(std::string const& name) {
    expireDynamic();
    auto it = gDynamic.find(name);
    if (it == gDynamic.end()) return nullptr;
    it->second.lastAccess = nowSeconds();
    return &it->second;
}

DynamicResult result(bool ok, std::string key, std::string text, std::vector<std::string> args = {}) {
    return DynamicResult{ok, std::move(key), std::move(text), std::move(args)};
}

std::string stackName(core::ItemStackData const& item) {
    auto s = game::ItemConv::toStack(item, 1);
    return s.isNull() ? item.id : s.getName();
}

class TemplatesModule {
public:
    void install() {
        loadPacks();
        static bool reloadHooked = false;
        if (!reloadHooked) {
            reloadHooked = true;
            config::onReload([] { loadPacks(); });
        }
        mStopping = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerStoppingEvent>([](auto&) {
            gSettingsBlob.reset();
            gBackpacksBlob.reset();
            gDynamic.clear();
        });
        // SB fillFromTemplate: a template backpack gets its contents when its storage is created.
        mCreate = game::Storage::get().addCreateListener([](ItemStack& stack, std::shared_ptr<game::Backpack> const& bp) {
            auto name = pendingTemplate(stack);
            if (!name || !bp) return;
            auto tmpl = backpackTemplate(*name);
            if (!tmpl) return;
            auto it = tmpl->find("backpackContents");
            if (it == tmpl->end() || !applyBackpackContents(*bp, *it)) {
                log().warn("templates: template '{}' has no usable contents", *name);
                return;
            }
            setUserData(stack, [](CompoundTag& tag) { tag.remove(std::string(kTemplateKey)); });
            log().info("templates: filled backpack {} from template '{}'", bp->uuid(), *name);
        });
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mStopping);
        game::Storage::get().removeCreateListener(mCreate);
        gSettingsBlob.reset();
        gBackpacksBlob.reset();
        gDynamic.clear();
    }

private:
    ll::event::ListenerPtr mStopping;
    int                    mCreate = 0;
};
} // namespace

// ---- template packs ------------------------------------------------------------------------------------

std::filesystem::path templatePacksRoot() { return config::configDir() / "templates"; }

void reloadTemplatePacks() { loadPacks(); }

// ---- settings templates ---------------------------------------------------------------------------------

std::map<int, nlohmann::json> playerTemplates(Player& player) {
    std::map<int, json> out;
    auto& all = gSettingsBlob.get();
    auto  pt  = all.find("playerTemplates");
    if (pt == all.end() || !pt->is_object()) return out;
    auto mine = pt->find(uuidOf(player));
    if (mine == pt->end() || !mine->is_object()) return out;
    for (auto const& [k, v] : mine->items()) {
        try {
            out[std::stoi(k)] = v;
        } catch (...) {
        }
    }
    return out;
}

std::map<std::string, nlohmann::json> playerNamedTemplates(Player& player) {
    std::map<std::string, json> out;
    auto& all = gSettingsBlob.get();
    auto  pt  = all.find("playerNamedTemplates");
    if (pt == all.end() || !pt->is_object()) return out;
    auto mine = pt->find(uuidOf(player));
    if (mine == pt->end() || !mine->is_object()) return out;
    for (auto const& [k, v] : mine->items()) out[k] = v;
    return out;
}

void putPlayerTemplate(Player& player, int slot, nlohmann::json settings) {
    gSettingsBlob.get()["playerTemplates"][uuidOf(player)][std::to_string(slot)] = std::move(settings);
    gSettingsBlob.save();
}

void putPlayerNamedTemplate(Player& player, std::string const& name, nlohmann::json settings) {
    gSettingsBlob.get()["playerNamedTemplates"][uuidOf(player)][name] = std::move(settings);
    gSettingsBlob.save();
}

void clearPlayerTemplates(Player& player) {
    auto& all = gSettingsBlob.get();
    for (auto const* key : {"playerTemplates", "playerNamedTemplates"}) {
        if (auto it = all.find(key); it != all.end() && it->is_object()) it->erase(uuidOf(player));
    }
    gSettingsBlob.save();
}

PackTemplates const& packSettingsTemplates() { return gPackSettings; }

void putPackSettingsTemplate(std::string const& pack, std::string const& fileName, nlohmann::json settings) {
    gPackSettings[pack][capitalizeWords(fileName)] = std::move(settings);
}

std::string invalidFileNameCharacters(std::string const& name) {
    std::string bad;
    for (unsigned char c : name) {
        if (std::isalnum(c) || c == '_' || c == '-' || std::isspace(c)) continue;
        bad += static_cast<char>(c);
    }
    return bad;
}

std::optional<std::string> exportSettingsTemplate(Player& player, std::string const& fileName, nlohmann::json const& settings) {
    std::string folder = player.getRealName();
    std::transform(folder.begin(), folder.end(), folder.begin(), [](unsigned char c) {
        return std::isspace(c) ? '_' : static_cast<char>(std::tolower(c));
    });
    folder += "_soph_templates";
    auto packRoot = templatePacksRoot() / folder;
    auto file     = packRoot / "data" / folder / kSettingsDir / (fileName + ".snbt");
    writePackMeta(packRoot, "Sophisticated Settings Templates data pack");
    if (!writeSnbtFile(file, settings)) return std::nullopt;
    putPackSettingsTemplate(folder, fileName, asReadBack(settings)); // what a reload will see
    return packRelative(file);
}

// ---- backpack templates -----------------------------------------------------------------------------------

std::string normalizeTemplateName(std::string const& name) {
    return name.find(':') == std::string::npos ? "minecraft:" + name : name;
}

std::string invalidTemplateNameCharacters(std::string const& raw) {
    auto        name = normalizeTemplateName(raw);
    auto        colon = name.find(':');
    std::string bad;
    auto check = [&](std::string_view part, bool path) {
        for (unsigned char c : part) {
            bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || std::isspace(c) || (path && (c == '/' || c == '.'));
            if (!ok) bad += static_cast<char>(c);
        }
    };
    check(std::string_view(name).substr(0, colon), false);
    check(std::string_view(name).substr(colon + 1), true);
    return bad;
}

std::optional<nlohmann::json> localBackpackTemplate(std::string const& raw) {
    auto  name = normalizeTemplateName(raw);
    auto& all  = gBackpacksBlob.get();
    auto  list = all.find("backpackTemplates");
    if (list == all.end() || !list->is_object()) return std::nullopt;
    auto it = list->find(name);
    if (it == list->end() || !it->is_object()) return std::nullopt;
    return *it;
}

std::optional<nlohmann::json> backpackTemplate(std::string const& raw) {
    if (auto local = localBackpackTemplate(raw)) return local;
    auto it = gPackBackpacks.find(normalizeTemplateName(raw));
    if (it == gPackBackpacks.end()) return std::nullopt;
    return it->second;
}

std::vector<std::string> backpackTemplateNames(bool includePacks) {
    std::vector<std::string> out;
    auto& all  = gBackpacksBlob.get();
    if (auto list = all.find("backpackTemplates"); list != all.end() && list->is_object()) {
        for (auto const& [k, _] : list->items()) out.push_back(k);
    }
    if (includePacks) {
        for (auto const& [k, _] : gPackBackpacks) out.push_back(k);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::vector<std::string> packBackpackTemplateNames() {
    std::vector<std::string> out;
    for (auto const& [k, _] : gPackBackpacks) out.push_back(k);
    return out;
}

void setBackpackTemplate(std::string const& raw, std::string const& itemId, nlohmann::json contents) {
    gBackpacksBlob.get()["backpackTemplates"][normalizeTemplateName(raw)] =
        json{{"backpackItemRegistryName", itemId}, {"backpackContents", std::move(contents)}};
    gBackpacksBlob.save();
}

void removeBackpackTemplate(std::string const& raw) {
    auto& all = gBackpacksBlob.get();
    if (auto list = all.find("backpackTemplates"); list != all.end() && list->is_object()) list->erase(normalizeTemplateName(raw));
    gBackpacksBlob.save();
}

std::optional<std::string> exportBackpackTemplate(std::string const& raw, nlohmann::json const& data) {
    auto name  = normalizeTemplateName(raw);
    auto colon = name.find(':');
    auto ns    = name.substr(0, colon);
    auto path  = name.substr(colon + 1);
    auto root  = templatePacksRoot() / (ns + "_backpack_templates");
    auto file  = root / "data" / ns / kBackpacksDir / (path + ".snbt");
    writePackMeta(root, "Sophisticated Backpacks Templates data pack");
    if (!writeSnbtFile(file, data)) return std::nullopt;
    gPackBackpacks[name] = asReadBack(data); // what a reload will see
    return packRelative(file);
}

nlohmann::json backpackContents(game::Backpack const& backpack) {
    auto j = backpack.data().toJson();
    j.erase("uuid");
    return j;
}

bool applyBackpackContents(game::Backpack& backpack, nlohmann::json const& contents) {
    if (!contents.is_object()) return false;
    auto j    = contents;
    j["uuid"] = backpack.uuid();
    auto data = core::BackpackData::fromJson(j);
    if (!data) return false;
    auto& d = backpack.data();
    if (data->tier != d.tier) d.setTier(data->tier);
    d.inventory   = data->inventory; // copy-assign: keeps the backpack's hooks / listeners
    d.upgrades    = data->upgrades;
    d.settings    = data->settings;
    d.mainColor   = data->mainColor;
    d.accentColor = data->accentColor;
    backpack.clearTransient("gui");
    backpack.markDirty();
    backpack.rebuildHooks();
    return true;
}

std::optional<ItemStack> makeTemplateBackpack(std::string const& raw) {
    auto name = normalizeTemplateName(raw);
    auto tmpl = backpackTemplate(name);
    if (!tmpl) return std::nullopt;
    auto tier = game::backpackTierOf(tmpl->value("backpackItemRegistryName", std::string{}));
    if (!tier) return std::nullopt;
    auto stack = game::makeBackpackItem(*tier);
    if (stack.isNull()) return std::nullopt;
    setUserData(stack, [&](CompoundTag& tag) { tag.putString(std::string(kTemplateKey), name); });
    return stack;
}

std::optional<std::string> pendingTemplate(ItemStack const& stack) {
    if (stack.isNull() || !stack.mUserData) return std::nullopt;
    auto const& tag = *stack.mUserData;
    if (!tag.contains(std::string(kTemplateKey), Tag::Type::String)) return std::nullopt;
    std::string const& v = tag[std::string(kTemplateKey)];
    return v;
}

// ---- dynamic templates ----------------------------------------------------------------------------------------

bool dynamicInUse(std::string const& name) { return dynamicOf(name) != nullptr; }

void dynamicAdvanceClock(int seconds) { gClockOffset += seconds; }

DynamicResult dynamicBegin(std::string const& name, core::Tier tier) {
    if (dynamicOf(name)) return result(false, "abp.templates.dynamic.templateInUse", "The dynamic template name '{}' is currently in use", {name});
    Dynamic d;
    d.data       = core::BackpackData::create("", tier);
    d.lastAccess = nowSeconds();
    gDynamic[name] = std::move(d);
    return result(true, "abp.templates.dynamic.begin.success", "Successfully started a dynamic template '{}'", {name});
}

DynamicResult dynamicBeginFromTemplate(std::string const& name, std::string const& baseTemplate) {
    if (dynamicOf(name)) return result(false, "abp.templates.dynamic.templateInUse", "The dynamic template name '{}' is currently in use", {name});
    auto base = backpackTemplate(baseTemplate);
    auto tier = base ? game::backpackTierOf(base->value("backpackItemRegistryName", std::string{})) : std::nullopt;
    if (!base || !tier) {
        return result(false, "abp.templates.dynamic.begin.noBaseTemplate", "The template '{}' to base this dynamic template of does not exist", {normalizeTemplateName(baseTemplate)});
    }
    Dynamic d;
    d.data = core::BackpackData::create("", *tier);
    if (auto it = base->find("backpackContents"); it != base->end() && it->is_object()) {
        auto j    = *it;
        j["uuid"] = "";
        if (auto parsed = core::BackpackData::fromJson(j)) d.data = std::move(*parsed);
    }
    d.lastAccess   = nowSeconds();
    gDynamic[name] = std::move(d);
    return result(true, "abp.templates.dynamic.begin.success", "Successfully started a dynamic template '{}'", {name});
}

DynamicResult dynamicAdd(std::string const& name, int slot, core::ItemStackData const& item, bool upgrade) {
    auto* d = dynamicOf(name);
    if (!d) return result(false, "abp.templates.dynamic.templateNotInUse", "The dynamic template '{}' is currently not in use", {name});
    auto display = "[" + stackName(item) + "]";
    if (slot == -1) {
        (upgrade ? d->forUpgrades : d->forInventory).push_back(item);
    } else {
        bool free = false;
        if (upgrade) {
            free = slot >= 0 && slot < static_cast<int>(d->data.upgrades.size()) && d->data.upgrades[static_cast<std::size_t>(slot)].empty();
            if (free) d->data.upgrades[static_cast<std::size_t>(slot)].item = item.withCount(1);
        } else {
            free = slot >= 0 && slot < d->data.inventory.size() && d->data.inventory.at(slot).empty();
            if (free) d->data.inventory.set(slot, item);
        }
        if (free) return result(true, "abp.templates.dynamic.add.success", "Successfully added item '{1}' to '{0}'", {name, display});
        d->forInventory.push_back(item); // SB: an occupied slot defers the item to the inventory
    }
    return result(true, "abp.templates.dynamic.add.delayed", "The item '{1}' is marked to be added to '{0}' at the end", {name, display});
}

DynamicEnd dynamicEnd(std::string const& name) {
    DynamicEnd out;
    auto*      d = dynamicOf(name);
    if (!d) {
        out.result = result(false, "abp.templates.dynamic.templateNotInUse", "The dynamic template '{}' is currently not in use", {name});
        return out;
    }
    core::AccessContext ctx{core::Source::Internal, {}};
    for (auto const& item : d->forInventory) {
        auto r = d->data.inventory.insert(item, ctx);
        if (!r.remainder.empty()) out.leftovers.push_back({r.remainder.count, "[" + stackName(r.remainder) + "]"});
    }
    for (auto const& item : d->forUpgrades) {
        bool placed = false;
        if (upgrade::upgradeFor(item)) {
            for (auto& slot : d->data.upgrades) {
                if (!slot.empty()) continue;
                slot.item = item.withCount(1);
                placed    = true;
                break;
            }
        }
        if (!placed) out.leftovers.push_back({item.count, "[" + stackName(item) + "]"});
    }
    d->forInventory.clear();
    d->forUpgrades.clear();
    bool noUpgrades = std::all_of(d->data.upgrades.begin(), d->data.upgrades.end(), [](core::UpgradeSlot const& s) { return s.empty(); });
    if (d->data.inventory.isEmpty() && noUpgrades) {
        out.result = result(false, "abp.templates.backpackempty", "Backpack is empty");
        return out;
    }
    auto bp = game::Storage::get().create(d->data.tier);
    if (!bp) {
        out.result = result(false, "abp.templates.storage_unavailable", "Backpack storage is not available");
        return out;
    }
    auto contents = d->data.toJson();
    (void)applyBackpackContents(*bp, contents);
    game::Storage::get().save(*bp, true);
    out.item = game::makeBackpackItem(d->data.tier, bp->uuid());
    gDynamic.erase(name);
    out.result = result(true, "abp.templates.dynamic.end.success", "Successfully ended the dynamic template '{}'", {name});
    return out;
}

} // namespace abp::feature::templates

ABP_REGISTER_MODULE(abp::feature::templates::TemplatesModule);
