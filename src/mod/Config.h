#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include "ll/api/Config.h"

#include "mod/Registry.h"

namespace abp::config {

/// Per-section JSON configs under `mods/AlleviatedBackpacks/config/<section>.json`.
///
/// Declare a reflectable aggregate with an integral `version` member and register it once:
///
///     struct PickupConfig { int version = 1; int range = 3; bool enabled = true; };
///     ABP_REGISTER_CONFIG("pickup", PickupConfig);       // in a .cpp file
///
///     int r = abp::config::get<PickupConfig>().range;    // anywhere
///
/// Files are created with defaults on first load, merged with new defaults when `version`
/// changes, and reloaded by `/backpack reload`. Configs are global (not per world).
template <class T>
T& mut() {
    static T value{};
    return value;
}

template <class T>
T const& get() {
    return mut<T>();
}

struct ConfigEntry {
    std::string                                       section;
    std::function<bool(std::filesystem::path const&)> load; ///< path of the .json file
    std::function<bool(std::filesystem::path const&)> save;
};
using ConfigRegistry = StaticRegistry<ConfigEntry>;

/// Directory holding the config files (valid after mod load).
[[nodiscard]] std::filesystem::path const& configDir();
/// Loads every registered section (creates missing files). Returns false if any failed.
bool loadAll();
/// Writes every registered section.
bool saveAll();
/// Callbacks run after `loadAll()` (e.g. to apply a changed tick interval).
void onReload(std::function<void()> fn);

namespace detail {
void setConfigDir(std::filesystem::path dir);

template <ll::config::IsConfig T>
struct ConfigRegistrar {
    explicit ConfigRegistrar(std::string section) {
        ConfigRegistry::add(ConfigEntry{
            std::move(section),
            [](std::filesystem::path const& p) {
                T    fresh{};
                // false = rewritten (missing / version change). A section that renamed keys
                // declares `template <class J> static void migrate(J& data)`: it runs on the old
                // file's JSON before the default merge.
                bool ok = ll::config::loadConfig(fresh, p, [](T& cfg, nlohmann::ordered_json& data) {
                    if constexpr (requires { T::migrate(data); }) T::migrate(data);
                    return ll::config::defaultConfigUpdater<T, nlohmann::ordered_json>(cfg, data);
                });
                if (!ok) ll::config::saveConfig(fresh, p);
                mut<T>() = std::move(fresh);
                return true;
            },
            [](std::filesystem::path const& p) { return ll::config::saveConfig(mut<T>(), p); },
        });
    }
};
} // namespace detail

} // namespace abp::config

/// Registers config struct `T` under `<configDir>/<section>.json`. `section` is a string literal.
#define ABP_REGISTER_CONFIG(section, T)                                                                                \
    static ::abp::config::detail::ConfigRegistrar<T> ABP_UNIQUE(abp_config_registrar_)(section)
