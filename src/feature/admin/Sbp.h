#pragma once

// `/sbp` (alias `/sophisticatedbackpacks`): Sophisticated Backpacks' admin command, operators only
// (SB: op level 2). Built-in: `list [playerName]`, `give <targets> <backpackUuid>`,
// `removeNonPlayer <onlyWithEmptyInventory>`. Other units add sub-commands (e.g. `template`) with
// `ABP_REGISTER_SBP_SUBCOMMAND`; the same overloads are also reachable as `/backpack ...`.

#include <functional>

#include "mod/Registry.h"

namespace ll::command {
class CommandHandle;
}

namespace abp::admin {

/// Adds overloads to `/sbp`. Runs every time the command is (re)registered (BDS start, every client
/// local world). Gate nothing yourself: the root already requires operator permission.
using SbpSubcommandFn       = std::function<void(ll::command::CommandHandle&)>;
using SbpSubcommandRegistry = StaticRegistry<SbpSubcommandFn>;

namespace detail {
struct SbpSubcommandRegistrar {
    explicit SbpSubcommandRegistrar(SbpSubcommandFn fn) { SbpSubcommandRegistry::add(std::move(fn)); }
};
} // namespace detail

/// Refreshes the `/sbp` argument suggestions (player names and backpack uuids from the access log).
void refreshSbpSuggestions();

} // namespace abp::admin

#define ABP_REGISTER_SBP_SUBCOMMAND(...)                                                                               \
    static ::abp::admin::detail::SbpSubcommandRegistrar ABP_UNIQUE(abp_sbp_subcommand_)(__VA_ARGS__)
