#pragma once

#include <functional>

#include "mod/Registry.h"

namespace ll::command {
class CommandHandle;
}

namespace abp::commands {

/// Adds overloads to the `/backpack` command. The function runs every time the command is
/// (re)registered (BDS start, every client local world):
///
///     ABP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
///         cmd.overload().text("sort").execute([](CommandOrigin const& o, CommandOutput& out) { ... });
///     });
using SubcommandFn       = std::function<void(ll::command::CommandHandle&)>;
using SubcommandRegistry = StaticRegistry<SubcommandFn>;

namespace detail {
struct SubcommandRegistrar {
    explicit SubcommandRegistrar(SubcommandFn fn) { SubcommandRegistry::add(std::move(fn)); }
};
} // namespace detail

} // namespace abp::commands

#define ABP_REGISTER_SUBCOMMAND(...)                                                                                   \
    static ::abp::commands::detail::SubcommandRegistrar ABP_UNIQUE(abp_subcommand_)(__VA_ARGS__)
