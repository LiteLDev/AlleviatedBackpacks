#pragma once

// P2-2 - Settings templates (SC SettingsTemplateStorage / DatapackSettingsTemplateManager) and
// backpack templates (SB BackpackTemplates / BackpackTemplateStorage / DatapackBackpackTemplateManager
// / DynamicCommand).
//
// Persistence:
// - Player settings templates and local backpack templates live in the world's LevelDB (keys
//   `abp:settings_templates` and `abp:backpack_templates`), like SB's SavedData in the overworld.
// - Bedrock has no data packs. Their equivalent is the "template packs" folder
//   `<mod>/config/templates/`, laid out exactly like SB's data packs:
//     <pack>/data/<namespace>/sophisticated_settingstemplates/<name>.json   settings templates
//     <pack>/data/<namespace>/sophisticatedbackpacks_templates/<path>.json  backpack templates
//   (SNBT files become JSON). It is read when the mod enables and on `/backpack reload`; exports
//   (settings screen "Export", `/sbp template export`) are written there and registered at once.

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "mc/world/item/ItemStack.h"

#include "core/ItemStackData.h"
#include "core/Tier.h"

class Player;

namespace abp::game {
class Backpack;
}

namespace abp::feature::templates {

// ---- template packs folder ------------------------------------------------------------------------
[[nodiscard]] std::filesystem::path templatePacksRoot();
/// Re-reads every template pack (settings and backpack templates).
void reloadTemplatePacks();

// ---- settings templates ------------------------------------------------------------------------------
[[nodiscard]] std::map<int, nlohmann::json>         playerTemplates(Player& player);
[[nodiscard]] std::map<std::string, nlohmann::json> playerNamedTemplates(Player& player);
void putPlayerTemplate(Player& player, int slot, nlohmann::json settings);
void putPlayerNamedTemplate(Player& player, std::string const& name, nlohmann::json settings);
void clearPlayerTemplates(Player& player);

/// pack name -> template name ("Capitalized Words") -> settings, both sorted.
using PackTemplates = std::map<std::string, std::map<std::string, nlohmann::json>>;
[[nodiscard]] PackTemplates const& packSettingsTemplates();
/// SC DatapackSettingsTemplateManager.putTemplate ('_' -> ' ', words capitalized).
void putPackSettingsTemplate(std::string const& pack, std::string const& fileName, nlohmann::json settings);

/// SC TemplatePersistanceContainer.exportTemplate after the name checks: writes
/// `<root>/<player>_soph_templates/data/<player>_soph_templates/sophisticated_settingstemplates/<file>.json`
/// and registers it. Returns the written path relative to the packs root, nullopt on I/O errors.
[[nodiscard]] std::optional<std::string> exportSettingsTemplate(Player& player, std::string const& fileName, nlohmann::json const& settings);

/// SC EXPORT_FILE_NAME_PATTERN `[A-Za-z0-9_\-\s]+`: the characters of `name` that don't match.
[[nodiscard]] std::string invalidFileNameCharacters(std::string const& name);

// ---- backpack templates -------------------------------------------------------------------------------
/// "namespace:path" (ResourceLocation; "minecraft" when there is no namespace).
[[nodiscard]] std::string normalizeTemplateName(std::string const& name);
/// SB EXPORT_TEMPLATE_NAMESPACE_PATTERN / _PATH_PATTERN: invalid characters ("" = valid).
[[nodiscard]] std::string invalidTemplateNameCharacters(std::string const& name);

/// {"backpackItemRegistryName": "<item id>", "backpackContents": <backpack data json>}
[[nodiscard]] std::optional<nlohmann::json> backpackTemplate(std::string const& name); ///< local, then packs
[[nodiscard]] std::optional<nlohmann::json> localBackpackTemplate(std::string const& name);
[[nodiscard]] std::vector<std::string>      backpackTemplateNames(bool includePacks);
[[nodiscard]] std::vector<std::string>      packBackpackTemplateNames();
void setBackpackTemplate(std::string const& name, std::string const& itemId, nlohmann::json contents);
void removeBackpackTemplate(std::string const& name);
/// Writes `<root>/<ns>_backpack_templates/data/<ns>/sophisticatedbackpacks_templates/<path>.json`.
[[nodiscard]] std::optional<std::string> exportBackpackTemplate(std::string const& name, nlohmann::json const& data);

/// The contents of `backpack` as a template's `backpackContents` (inventory, upgrades, settings,
/// colours; no uuid).
[[nodiscard]] nlohmann::json backpackContents(game::Backpack const& backpack);
/// Replaces `backpack`'s contents with a template's `backpackContents` (keeps the uuid).
bool applyBackpackContents(game::Backpack& backpack, nlohmann::json const& contents);

/// User-data key of a backpack item that gets a template's contents on first use (SB TEMPLATE_NAME).
inline constexpr std::string_view kTemplateKey = "abp_template";
/// A new backpack item of the template's item that is filled from the template on first use.
[[nodiscard]] std::optional<ItemStack> makeTemplateBackpack(std::string const& name);
[[nodiscard]] std::optional<std::string> pendingTemplate(ItemStack const& stack);

// ---- dynamic templates (SB DynamicCommand, 5 minute cache) ----------------------------------------------
struct DynamicResult {
    bool        ok = false;
    std::string key;   ///< translation key of the message
    std::string text;  ///< English fallback
    std::vector<std::string> args;
};
DynamicResult dynamicBegin(std::string const& name, core::Tier tier);
DynamicResult dynamicBeginFromTemplate(std::string const& name, std::string const& baseTemplate);
/// `slot` -1 = add at the end; `upgrade` = upgrade slots.
DynamicResult dynamicAdd(std::string const& name, int slot, core::ItemStackData const& item, bool upgrade);
/// An item a dynamic template couldn't take (SB's `%d x %s` message arguments).
struct DynamicLeftover {
    int         count = 0;
    std::string name; ///< "[Display Name]" (SB ItemStack.getDisplayName)
};
struct DynamicEnd {
    DynamicResult                result;
    std::vector<DynamicLeftover> leftovers;
    std::optional<ItemStack> item;
};
/// Ends the template: returns the backpack item (+ messages for items that didn't fit).
DynamicEnd dynamicEnd(std::string const& name);
[[nodiscard]] bool dynamicInUse(std::string const& name);
/// Test hook: pretend `seconds` passed for the cache (expiry after 300 s without access).
void dynamicAdvanceClock(int seconds);

} // namespace abp::feature::templates
