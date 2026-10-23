// Selftests of the GX-1 mob catcher additions: the capture effect packet (SB
// MobCatcherCaptureEffectPayload) and the captured mob's actor properties.

#include <cmath>
#include <string>
#include <vector>

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/state/ActorPropertyUtils.h"
#include "mc/world/actor/state/PropertyComponent.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"

#include "core/Codec.h"
#include "game/Backpack.h"
#include "net/Companion.h"
#include "selftest/SelfTest.h"
#include "upgrade/mobcatcher/CaptureEffect.h"
#include "upgrade/mobcatcher/MobCatcher.h"

namespace abp::upgrade::mobcatcher {
namespace {

using selftest::Context;

struct Sent {
    std::string    name;
    nlohmann::json body;
};

/// Marks the simulated player as a companion and records the packets sent to it.
struct CompanionSink {
    Player&                         player;
    std::shared_ptr<std::vector<Sent>> sent = std::make_shared<std::vector<Sent>>();
    explicit CompanionSink(Player& p) : player(p) {
        net::setCompanion(p, net::CompanionInfo{"test", {"native_screen"}});
        net::setTestSink([sent = sent](Player&, std::string_view name, nlohmann::json const& body) { sent->push_back({std::string(name), body}); });
    }
    CompanionSink(CompanionSink const&)            = delete;
    CompanionSink& operator=(CompanionSink const&) = delete;
    ~CompanionSink() {
        net::setTestSink(nullptr);
        net::setCompanion(player, std::nullopt);
    }
};

Mob* spawnCow(Context& ctx, float dx) {
    auto& player = ctx.player();
    auto  p      = player.getPosition();
    auto& bs     = player.getDimensionBlockSource();
    float x      = p.x + dx;
    auto  y      = bs.getAboveTopSolidBlock(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(p.z)), false, false);
    auto* mob = player.getLevel().getSpawner().spawnMob(bs, ActorDefinitionIdentifier("minecraft:cow"), nullptr, Vec3{x, static_cast<float>(y), p.z}, false, true, false);
    ctx.require(mob != nullptr, "could not spawn a cow");
    return mob;
}

/// Moves the test player onto the ground (test players may spawn above an unset world spawn).
ll::coro::CoroTask<> groundPlayer(Context& ctx) {
    auto&    p   = ctx.player();
    auto&    bs  = p.getDimensionBlockSource();
    auto     pos = p.getPosition();
    BlockPos column{static_cast<int>(std::floor(pos.x)), 0, static_cast<int>(std::floor(pos.z))};
    if (pos.y < bs.getMaxHeight() && bs.hasChunksAt(column, 2, false)) co_return;
    p.teleport(Vec3{pos.x, 0.0f, pos.z}, p.getDimensionId());
    for (int i = 0; i < 100 && !bs.hasChunksAt(column, 2, false); ++i) co_await ctx.ticks(2);
    ctx.require(bs.hasChunksAt(column, 2, false), "chunks around the test player did not load");
    auto y = bs.getAboveTopSolidBlock(column.x, column.z, false, false);
    p.teleport(Vec3{pos.x, static_cast<float>(y), pos.z}, p.getDimensionId());
    co_await ctx.ticks(2);
}

ABP_REGISTER_SELFTEST("mobcatcher.capture_effect", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await groundPlayer(ctx);
    auto&         p = ctx.player();
    CompanionSink sink(p);
    auto          bp = ctx.createBackpack(core::Tier::Iron);
    ctx.require(ctx.installUpgrade(bp, kBasicId) >= 0, "mob catcher installed");
    auto* cow = spawnCow(ctx, 2.f);
    // A cold cow: the climate variant is an actor property (client synced).
    auto props = cow->getEntityContext().tryGetComponent<PropertyComponent>();
    ctx.require(static_cast<bool>(props), "cows have actor properties");
    ctx.require(
        ActorPropertyUtils::tryQueueEnumIndexValueByString(*props, *cow, HashedString("minecraft:climate_variant").getHash(), "cold"),
        "climate variant queued"
    );
    auto id = cow->getOrCreateUniqueID();
    co_await ctx.ticks(2);
    cow = static_cast<Mob*>(p.getLevel().fetchEntity(id, false));
    ctx.require(cow != nullptr, "the cow is still there");

    auto r = capture(p, bp, *cow, false);
    ctx.require(r.success, "captured: " + r.message);
    auto mobs = capturedMobs(*bp);
    ctx.require(mobs.size() == 1, "one mob stored");
    auto props64 = mobs[0].look.value("properties", std::string{});
    ctx.check(!props64.empty(), "the captured mob's look carries its properties");
    if (auto bytes = core::base64Decode(props64)) {
        ctx.check(bytes->find("cold") != std::string::npos, "the properties hold the cold climate variant");
    } else {
        ctx.fail("properties are not base64");
    }

    Sent const* effect = nullptr;
    for (auto const& s : *sink.sent) {
        if (s.name.find("MobCaptureEffect") != std::string::npos) effect = &s;
    }
    ctx.require(effect != nullptr, "a capture effect packet was sent to the companion");
    auto const& b = effect->body;
    ctx.check(b.value("type", std::string{}) == "minecraft:cow", "effect names the mob type");
    ctx.check(b.value("ticks", 0) == kCaptureAnimationTicks, "SB's 5 animation ticks");
    ctx.check(b.contains("pos") && b["pos"].size() == 3 && b.contains("collapse") && b["collapse"].size() == 3, "position and collapse point");
    ctx.check(b.value("properties", std::string{}) == props64, "the effect copies the properties too");
    ctx.check(b.contains("data") && b["data"].is_array(), "synced data for the copy");
});

} // namespace
} // namespace abp::upgrade::mobcatcher
