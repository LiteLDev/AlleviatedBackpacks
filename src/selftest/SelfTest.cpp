#include "selftest/SelfTest.h"

#include <algorithm>

#include "ll/api/chrono/GameChrono.h"
#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/thread/ServerThreadExecutor.h"

#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"

#include "core/Codec.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/Storage.h"
#include "mod/Log.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"

namespace abp::selftest {

namespace {
bool gRunning = false;
int  gPlayerCounter = 0;
} // namespace

Context::Context(std::string name) : mName(std::move(name)) {}

Context::~Context() {
    for (auto* p : mPlayers) {
        if (!p) continue;
        ui::MenuManager::get().close(*p);
        p->simulateDisconnect();
    }
    auto& storage = game::Storage::get();
    for (auto const& uuid : mBackpacks) storage.erase(uuid);
}

SimulatedPlayer& Context::spawnPlayer(std::string const& name) {
    auto level = ll::service::getLevel();
    if (!level) fail("no level loaded");
    auto spawn = level->getSharedSpawnPos();
    auto pos   = Vec3{static_cast<float>(spawn.x) + 0.5f, static_cast<float>(spawn.y) + 1.0f, static_cast<float>(spawn.z) + 0.5f};
    auto n     = name.empty() ? "abp_test_" + std::to_string(++gPlayerCounter) : name;
    auto sp    = SimulatedPlayer::create(n, pos, 0);
    if (!sp) fail("could not spawn a simulated player");
    mPlayers.push_back(sp.as_ptr());
    return *sp;
}

Player& Context::player() {
    if (mPlayers.empty()) return spawnPlayer();
    return *mPlayers.front();
}

std::shared_ptr<game::Backpack> Context::createBackpack(core::Tier tier) {
    auto bp = game::Storage::get().create(tier);
    if (!bp) fail("could not create a backpack (no level?)");
    mBackpacks.push_back(bp->uuid());
    return bp;
}

ItemStack Context::backpackItem(game::Backpack const& backpack) const {
    return game::makeBackpackItem(backpack.tier(), backpack.uuid());
}

int Context::give(Player& player, ItemStack const& item, int slot) {
    auto& inv = player.getInventory();
    if (slot < 0) {
        for (int i = 0; i < inv.getContainerSize(); ++i) {
            if (inv.getItem(i).isNull()) {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0 || slot >= inv.getContainerSize()) return -1;
    inv.setItem(slot, item);
    player.sendInventory(false);
    return slot;
}

int Context::installUpgrade(std::shared_ptr<game::Backpack> const& backpack, std::string const& upgradeId, int slot) {
    auto* type = upgrade::byId(upgradeId);
    require(type != nullptr, "unknown upgrade " + upgradeId);
    auto item = game::ItemConv::toData(game::ItemConv::make(type->info().itemId, 1));
    require(!item.empty(), "upgrade item " + type->info().itemId + " is not registered (behavior pack missing?)");
    auto& slots = backpack->data().upgrades;
    if (slot < 0) {
        for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
            if (slots[static_cast<std::size_t>(i)].empty()) {
                slot = i;
                break;
            }
        }
    }
    require(slot >= 0, "no free upgrade slot");
    std::string reason;
    require(upgrade::install(backpack, slot, item, nullptr, &reason), "install " + upgradeId + " failed: " + reason);
    return slot;
}

ll::coro::CoroTask<> Context::ticks(int n) { co_await ll::chrono::ticks(std::max(n, 1)); }

void Context::check(bool condition, std::string const& message) {
    if (!condition) mFailures.push_back(message);
}

void Context::require(bool condition, std::string const& message) {
    if (!condition) fail(message);
}

void Context::fail(std::string const& message) {
    mFailures.push_back(message);
    throw Abort(message);
}

void Context::info(std::string const& message) const { log().info("SELFTEST INFO {}: {}", mName, message); }

bool running() { return gRunning; }

void runAll(std::string const& glob, std::function<void(std::string const&)> out) {
    if (gRunning) {
        out("SELFTEST BUSY");
        return;
    }
    std::vector<TestEntry const*> tests;
    for (auto const& t : TestRegistry::entries()) {
        if (glob.empty() || core::globMatch(glob, t.name)) tests.push_back(&t);
    }
    std::sort(tests.begin(), tests.end(), [](auto* a, auto* b) { return a->name < b->name; });
    gRunning = true;
    auto emit = [out](std::string const& line) {
        log().info("{}", line);
        if (out) out(line);
    };
    emit("SELFTEST START count=" + std::to_string(tests.size()));
    ll::coro::keepThis([tests = std::move(tests), emit]() -> ll::coro::CoroTask<> {
        int pass = 0, fail = 0;
        for (auto const* t : tests) {
            std::string reason;
            {
                Context ctx(t->name);
                auto    result = co_await t->run(ctx).tryGet();
                if (!ctx.failures().empty()) {
                    for (auto const& f : ctx.failures()) reason += (reason.empty() ? "" : "; ") + f;
                } else if (!result) {
                    reason = "exception: " + result.error().message();
                }
            } // cleanup (players, backpacks) before the next test
            if (reason.empty()) {
                ++pass;
                emit("SELFTEST PASS " + t->name);
            } else {
                ++fail;
                emit("SELFTEST FAIL " + t->name + ": " + reason);
            }
            co_await ll::chrono::ticks(1);
        }
        emit("SELFTEST DONE pass=" + std::to_string(pass) + " fail=" + std::to_string(fail));
        gRunning = false;
    }).launch(ll::thread::ServerThreadExecutor::getDefault());
}

} // namespace abp::selftest
