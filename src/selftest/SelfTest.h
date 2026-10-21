#pragma once

#include <concepts>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "ll/api/coro/CoroTask.h"

#include "core/Tier.h"
#include "mod/Registry.h"

class ItemStack;
class Player;
class SimulatedPlayer;

namespace abp::game {
class Backpack;
}

namespace abp::selftest {

/// Thrown by `require()` / `fail()` to abort the current test.
struct Abort : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/// Helpers and assertions for one selftest run. Everything runs on the server thread.
///
/// Players: `player()` returns a SimulatedPlayer spawned for this test (created lazily near the
/// world spawn, removed afterwards). Backpacks made with `createBackpack()` are deleted from the
/// world after the test.
class Context {
public:
    explicit Context(std::string name);
    ~Context();
    Context(Context const&)            = delete;
    Context& operator=(Context const&) = delete;

    [[nodiscard]] std::string const& name() const { return mName; }

    // ---- players ------------------------------------------------------------------------------
    /// The test's simulated player (spawned on first use). Aborts the test if it can't spawn.
    [[nodiscard]] Player& player();
    /// Spawns an extra simulated player (removed after the test).
    [[nodiscard]] SimulatedPlayer& spawnPlayer(std::string const& name = {});

    // ---- backpacks / items --------------------------------------------------------------------
    /// A new backpack (deleted from the world after the test).
    [[nodiscard]] std::shared_ptr<game::Backpack> createBackpack(core::Tier tier);
    /// A backpack item bound to `backpack`.
    [[nodiscard]] ItemStack backpackItem(game::Backpack const& backpack) const;
    /// Puts `item` into the player's inventory slot (or the first free slot when `slot < 0`).
    /// Returns the slot used, -1 on failure.
    int give(Player& player, ItemStack const& item, int slot = -1);
    /// Installs the upgrade with id `upgradeId` (creating its item) into the first free upgrade
    /// slot (or `slot`). Aborts the test on failure.
    int installUpgrade(std::shared_ptr<game::Backpack> const& backpack, std::string const& upgradeId, int slot = -1);

    // ---- waiting ------------------------------------------------------------------------------
    /// `co_await ctx.ticks(5);` (async tests only).
    [[nodiscard]] static ll::coro::CoroTask<> ticks(int n);

    // ---- assertions ---------------------------------------------------------------------------
    /// Records a failure and continues.
    void check(bool condition, std::string const& message);
    /// Records a failure and aborts the test.
    void require(bool condition, std::string const& message);
    [[noreturn]] void fail(std::string const& message);
    /// Logs a progress line ("SELFTEST INFO <name>: ...").
    void info(std::string const& message) const;

    [[nodiscard]] std::vector<std::string> const& failures() const { return mFailures; }

private:
    std::string                                  mName;
    std::vector<std::string>                     mFailures;
    std::vector<SimulatedPlayer*>                mPlayers;
    std::vector<std::string>                     mBackpacks;
};

using SyncTest  = std::function<void(Context&)>;
using AsyncTest = std::function<ll::coro::CoroTask<>(Context&)>;

struct TestEntry {
    std::string name; ///< "unit.case"
    AsyncTest   run;
};
using TestRegistry = StaticRegistry<TestEntry>;

/// Runs all tests whose name matches `glob` (`*`, `?`), one after another, printing
/// `SELFTEST PASS <name>` / `SELFTEST FAIL <name>: <reason>` and finally
/// `SELFTEST DONE pass=<N> fail=<M>`. `out` receives every line too (command output).
void runAll(std::string const& glob, std::function<void(std::string const&)> out);
[[nodiscard]] bool running();

namespace detail {
struct TestRegistrar {
    template <class F>
    TestRegistrar(std::string name, F fn) {
        if constexpr (std::is_invocable_r_v<ll::coro::CoroTask<>, F, Context&>) {
            TestRegistry::add({std::move(name), AsyncTest(std::move(fn))});
        } else {
            static_assert(std::is_invocable_v<F, Context&>, "selftest must take (abp::selftest::Context&)");
            TestRegistry::add({std::move(name), [fn = std::move(fn)](Context& ctx) -> ll::coro::CoroTask<> {
                fn(ctx);
                co_return;
            }});
        }
    }
};
} // namespace detail

} // namespace abp::selftest

/// Registers a selftest. `fn` is `void(abp::selftest::Context&)` or
/// `ll::coro::CoroTask<>(abp::selftest::Context&)` (to `co_await ctx.ticks(n)`).
///     ABP_REGISTER_SELFTEST("pickup.basic", [](abp::selftest::Context& ctx) { ... });
#define ABP_REGISTER_SELFTEST(name, ...)                                                                               \
    static ::abp::selftest::detail::TestRegistrar ABP_UNIQUE(abp_selftest_registrar_)(name, __VA_ARGS__)
