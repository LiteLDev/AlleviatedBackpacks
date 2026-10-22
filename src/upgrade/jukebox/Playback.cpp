// Jukebox playback (SC JukeboxUpgradeWrapper + ServerStorageSoundHandler): which disc plays where,
// who gets the sound, when the next one starts and when the music stops.

#include <algorithm>
#include <cmath>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/Actor.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/network/packet/StopSoundPacket.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/dimension/Dimension.h"

#include "feature/placed/PlacedBackpacks.h"
#include "game/PlayerScan.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "net/Companion.h"
#include "upgrade/jukebox/Jukebox.h"
#include "upgrade/jukebox/JukeboxNet.h"

namespace abp::upgrade::jukebox {

namespace {
constexpr int kKeepAliveEvery = 5; ///< SC KEEP_ALIVE_SEND_INTERVAL

Player* findPlayer(std::int64_t key) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    ActorUniqueID id;
    id.rawID = key;
    return level->getPlayer(id);
}

std::int64_t keyOf(Player const& p) { return p.getOrCreateUniqueID().rawID; }

/// Any living entity (a mob wearing the backpack), or nullptr.
Actor* findEntity(std::int64_t key) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    ActorUniqueID id;
    id.rawID = key;
    auto* a  = level->fetchEntity(id, false);
    return a && a->isAlive() && !a->mRemoved ? a : nullptr;
}

bool carries(Player& player, std::string const& uuid) {
    for (auto const& [c, bp] : game::carriedBackpacks(player, false, true))
        if (bp && bp->uuid() == uuid) return true;
    return false;
}

float distance(Vec3 const& a, Vec3 const& b) {
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

std::string message(Player* p, std::string_view key, std::string_view fallback) {
    return p ? trFor(*p, key, fallback) : tr(key, fallback);
}

/// The block of a placed backpack (feature/placed), if it is placed.
std::optional<std::pair<int, Vec3>> placedAt(std::string const& uuid) {
    auto e = placed::PlacedBackpacks::get().findUuid(uuid);
    if (!e) return std::nullopt;
    return std::pair{
        e->loc.dim,
        Vec3{static_cast<float>(e->loc.x) + 0.5f, static_cast<float>(e->loc.y) + 0.5f, static_cast<float>(e->loc.z) + 0.5f}
    };
}
} // namespace

Service& Service::get() {
    static Service instance;
    return instance;
}

void Service::install() {
    mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>(
        [this](ll::event::ServerLevelTickEvent&) { tick(); }
    );
}

void Service::uninstall() {
    for (auto& [key, p] : mPlaybacks) stopSounds(p);
    mPlaybacks.clear();
    mReported.clear();
    mCarriers.clear();
    if (mTick) ll::event::EventBus::getInstance().removeListener(mTick);
    mTick = nullptr;
}

// ---- control -------------------------------------------------------------------------------------

bool Service::play(UpgradeContext const& ctx, Player* starter, std::optional<int> index, std::string* reason) {
    std::string dummy;
    auto&       why = reason ? *reason : dummy;
    if (!isJukebox(ctx) || !ctx.state().enabled) return false;
    auto const& uuid = ctx.backpack().uuid();
    if (mPlaybacks.contains(Key{uuid, ctx.slot()})) return true; // SC: play does nothing while playing
    auto occupied = occupiedDiscs(ctx);
    if (std::none_of(occupied.begin(), occupied.end(), [](bool b) { return b; })) {
        why = message(starter, "abp.jukebox.no_disc", "Put a music disc into the jukebox first.");
        return false;
    }
    Playback p;
    p.backpack    = ctx.backpackPtr();
    p.uuid        = uuid;
    p.slot        = ctx.slot();
    p.upgradeItem = ctx.state().item.id;
    p.phase       = static_cast<int>(std::hash<std::string>{}(uuid) % kKeepAliveEvery);
    p.queue       = core::JukeboxQueue(static_cast<std::uint32_t>(std::hash<std::string>{}(uuid) ^ static_cast<std::size_t>(mNow)));
    // Where does it play? A placed backpack plays at its block (SC: the block menu's position),
    // a carried one is attached to its carrier (SC: the menu's entity).
    if (auto at = placedAt(uuid)) {
        p.where = Emitter{std::nullopt, at->first, at->second};
    } else if (auto carrier = reportedCarrier(uuid); carrier && findEntity(*carrier)) {
        // Worn by a mob: attached to it (SB wrapper.play(livingEntity)).
        auto* a = findEntity(*carrier);
        p.where = Emitter{*carrier, static_cast<int>(a->getDimensionId()), a->getPosition()};
    } else if (auto r = reported(uuid)) {
        p.where = Emitter{std::nullopt, r->dim, r->pos};
    } else {
        Player* carrier = starter && carries(*starter, uuid) ? starter : nullptr;
        if (!carrier) {
            if (auto level = ll::service::getLevel()) {
                level->forEachPlayer([&](Player& pl) {
                    if (pl.isClientSide() || !carries(pl, uuid)) return true;
                    carrier = &pl;
                    return false;
                });
            }
        }
        if (!carrier) {
            why = message(starter, "abp.jukebox.not_carried", "Carry the backpack (or place it) to play music.");
            return false;
        }
        p.where = Emitter{keyOf(*carrier), static_cast<int>(carrier->getDimensionId()), carrier->getPosition()};
    }
    auto first = p.queue.start(occupied, shuffle(ctx), index);
    if (!first) return false;
    Key   key{uuid, ctx.slot()};
    auto& stored = mPlaybacks.emplace(key, std::move(p)).first->second;
    if (!startTrack(stored, *first, ctx)) {
        mPlaybacks.erase(key);
        why = message(starter, "abp.jukebox.no_disc", "Put a music disc into the jukebox first.");
        return false;
    }
    log().info("jukebox: playing {} in backpack {} (slot {})", stored.info.soundEvent, uuid, ctx.slot());
    return true;
}

void Service::stop(std::string const& uuid, int slot) {
    auto it = mPlaybacks.find(Key{uuid, slot});
    if (it == mPlaybacks.end()) return;
    stopSounds(it->second);
    mPlaybacks.erase(it);
    log().info("jukebox: stopped in backpack {} (slot {})", uuid, slot);
}

void Service::stopAll(std::string const& uuid) {
    for (auto it = mPlaybacks.begin(); it != mPlaybacks.end();) {
        if (it->first.first == uuid) {
            stopSounds(it->second);
            it = mPlaybacks.erase(it);
        } else {
            ++it;
        }
    }
    mReported.erase(uuid);
    mCarriers.erase(uuid);
}

bool Service::next(UpgradeContext const& ctx) {
    auto it = mPlaybacks.find(Key{ctx.backpack().uuid(), ctx.slot()});
    if (it == mPlaybacks.end() || !isJukebox(ctx)) return false;
    auto n = it->second.queue.skipNext(occupiedDiscs(ctx));
    if (!n) return false;
    if (!startTrack(it->second, *n, ctx)) {
        stop(ctx.backpack().uuid(), ctx.slot());
        return false;
    }
    return true;
}

bool Service::previous(UpgradeContext const& ctx) {
    auto it = mPlaybacks.find(Key{ctx.backpack().uuid(), ctx.slot()});
    if (it == mPlaybacks.end() || !isJukebox(ctx)) return false;
    auto n = it->second.queue.skipPrevious(occupiedDiscs(ctx));
    if (!n) return false; // SC: no history, nothing happens
    if (!startTrack(it->second, *n, ctx)) {
        stop(ctx.backpack().uuid(), ctx.slot());
        return false;
    }
    return true;
}

void Service::settingsChanged(UpgradeContext const& ctx) {
    auto it = mPlaybacks.find(Key{ctx.backpack().uuid(), ctx.slot()});
    if (it == mPlaybacks.end() || !isJukebox(ctx)) return;
    it->second.queue.setShuffle(occupiedDiscs(ctx), shuffle(ctx));
}

void Service::discChanged(UpgradeContext const& ctx, int index) {
    auto it = mPlaybacks.find(Key{ctx.backpack().uuid(), ctx.slot()});
    if (it != mPlaybacks.end() && it->second.index == index) stop(ctx.backpack().uuid(), ctx.slot());
}

std::optional<Service::Status> Service::status(std::string const& uuid, int slot) const {
    auto it = mPlaybacks.find(Key{uuid, slot});
    if (it == mPlaybacks.end()) return std::nullopt;
    auto const& p = it->second;
    return Status{p.index, static_cast<int>(mNow - p.start), static_cast<int>(p.end - p.start)};
}

std::vector<std::int64_t> Service::listeners(std::string const& uuid, int slot) const {
    std::vector<std::int64_t> out;
    auto                      it = mPlaybacks.find(Key{uuid, slot});
    if (it != mPlaybacks.end()) out.assign(it->second.heard.begin(), it->second.heard.end());
    return out;
}

std::optional<std::int64_t> Service::emitterEntity(std::string const& uuid, int slot) const {
    auto it = mPlaybacks.find(Key{uuid, slot});
    return it == mPlaybacks.end() ? std::nullopt : it->second.where.entity;
}

void Service::tickPlaced(std::shared_ptr<game::Backpack> const& backpack, int dim, Vec3 const& pos) {
    if (backpack) mReported[backpack->uuid()] = Reported{dim, pos, mNow};
}

void Service::tickCarrier(std::shared_ptr<game::Backpack> const& backpack, std::int64_t entity) {
    if (backpack) mCarriers[backpack->uuid()] = Carrier{entity, mNow};
}

std::optional<std::int64_t> Service::reportedCarrier(std::string const& uuid) const {
    auto it = mCarriers.find(uuid);
    if (it == mCarriers.end() || mNow - it->second.lastSeen > 40) return std::nullopt;
    return it->second.entity;
}

std::optional<Service::Reported> Service::reported(std::string const& uuid) const {
    auto it = mReported.find(uuid);
    if (it == mReported.end() || mNow - it->second.lastSeen > 40) return std::nullopt;
    return it->second;
}

// ---- playback ------------------------------------------------------------------------------------

bool Service::startTrack(Playback& p, int index, UpgradeContext const& ctx) {
    auto d    = disc(ctx, index);
    auto info = discInfo(d);
    if (!info) return false;
    auto at = locate(p, false);
    if (!at) return false;
    stopSounds(p);
    p.where = *at;
    p.index = index;
    p.disc  = d;
    p.info  = *info;
    p.sound = soundName(*info);
    p.start = mNow;
    p.end   = mNow + std::max(1, mDurationOverride.value_or(info->durationTicks));
    log().info("jukebox: track {} (disc slot {}) in backpack {}", p.sound, index, p.uuid);
    // SC ServerStorageSoundHandler.startPlayingDisc: PlayDiscPayload to players within 128 blocks.
    auto level = ll::service::getLevel();
    if (!level) return true;
    level->forEachPlayer([&](Player& pl) {
        if (pl.isClientSide() || static_cast<int>(pl.getDimensionId()) != p.where.dim) return true;
        if (distance(pl.getPosition(), p.where.pos) > kSoundRange) return true;
        auto id = keyOf(pl);
        if (net::hasCompanion(pl)) {
            nlohmann::json body{{"uuid", p.uuid}, {"sound", p.sound}};
            if (p.where.entity) body["entity"] = *p.where.entity;
            else body["pos"] = {p.where.pos.x, p.where.pos.y, p.where.pos.z};
            net::send(pl, JukeboxPlayPacket{std::move(body)});
            p.vanilla.erase(id);
        } else {
            // No client mod: the vanilla record sound at the spot (it can't follow the carrier).
            auto  pkt                   = MinecraftPackets::createPacket(MinecraftPacketIds::PlaySound);
            auto& s                     = static_cast<PlaySoundPacket&>(*pkt);
            s.mName                     = p.sound;
            s.mPos                      = BlockPos{
                static_cast<int>(std::floor(p.where.pos.x * 8.0f)),
                static_cast<int>(std::floor(p.where.pos.y * 8.0f)),
                static_cast<int>(std::floor(p.where.pos.z * 8.0f))
            };
            s.mVolume                   = 1.0f;
            s.mPitch                    = 1.0f;
            s.mLoopCount                = 0;
            s.mBypassListenerRangeCheck = false;
            s.mServerSoundHandle->reset();
            s.mPlaybackPositionSeconds->reset();
            if (!pl.isSimulatedPlayer()) s.sendTo(pl);
            p.vanilla.insert(id);
        }
        p.heard.insert(id);
        emit(SoundEvent{SoundEvent::Kind::Play, id, p.sound, net::hasCompanion(pl), p.where.entity, p.where.pos});
        return true;
    });
    return true;
}

void Service::stopSounds(Playback& p) {
    for (auto id : p.heard) {
        auto* pl = findPlayer(id);
        if (!pl) continue;
        bool vanilla = p.vanilla.contains(id);
        if (!vanilla) {
            net::send(*pl, JukeboxStopPacket{nlohmann::json{{"uuid", p.uuid}}});
        } else if (!pl->isSimulatedPlayer()) {
            auto  pkt          = MinecraftPackets::createPacket(MinecraftPacketIds::StopSound);
            auto& s            = static_cast<StopSoundPacket&>(*pkt);
            s.mName            = p.sound;
            s.mStopAll         = false;
            s.mStopMusicLegacy = false;
            s.sendTo(*pl);
        }
        emit(SoundEvent{SoundEvent::Kind::Stop, id, p.sound, !vanilla, std::nullopt, {}});
    }
    p.heard.clear();
    p.vanilla.clear();
}

void Service::spawnNotes(Playback const& p) const {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto at = p.where.pos;
    at.y   += p.where.entity ? 0.6f : 0.7f;
    if (auto dim = level->getDimension(p.where.dim).lock()) level->spawnParticleEffect("minecraft:note_particle", at, dim.get());
}

std::optional<Service::Emitter> Service::locate(Playback const& p, bool deep) const {
    if (p.where.entity) {
        if (auto* pl = findPlayer(*p.where.entity)) {
            if (!pl->isAlive()) return std::nullopt;
            if (deep && (!carries(*pl, p.uuid) || placedAt(p.uuid))) return std::nullopt;
            return Emitter{p.where.entity, static_cast<int>(pl->getDimensionId()), pl->getPosition()};
        }
        // A mob wearing it: alive and still reported as the wearer.
        auto* a = findEntity(*p.where.entity);
        if (!a || reportedCarrier(p.uuid) != p.where.entity || placedAt(p.uuid)) return std::nullopt;
        return Emitter{p.where.entity, static_cast<int>(a->getDimensionId()), a->getPosition()};
    }
    if (auto at = placedAt(p.uuid)) return Emitter{std::nullopt, at->first, at->second};
    if (auto r = reported(p.uuid)) return Emitter{std::nullopt, r->dim, r->pos};
    return std::nullopt;
}

std::optional<UpgradeContext> Service::validContext(Playback const& p) const {
    auto bp = p.backpack.lock();
    if (!bp) return std::nullopt;
    auto const& slots = bp->data().upgrades;
    if (p.slot < 0 || p.slot >= static_cast<int>(slots.size())) return std::nullopt;
    auto const& s = slots[static_cast<std::size_t>(p.slot)];
    if (s.empty() || !s.enabled || s.item.id != p.upgradeItem) return std::nullopt;
    return UpgradeContext(bp, p.slot);
}

void Service::tick() {
    ++mNow;
    if (mNow % 100 == 0) {
        std::erase_if(mReported, [&](auto const& e) { return mNow - e.second.lastSeen > 100; });
        std::erase_if(mCarriers, [&](auto const& e) { return mNow - e.second.lastSeen > 100; });
    }
    if (mPlaybacks.empty()) return;
    auto const& cfg = config::get<JukeboxConfig>();
    for (auto it = mPlaybacks.begin(); it != mPlaybacks.end();) {
        auto& p         = it->second;
        bool  keepAlive = (mNow + p.phase) % kKeepAliveEvery == 0;
        auto  ctx       = validContext(p);
        std::optional<Emitter> at;
        if (ctx) at = locate(p, keepAlive);
        bool alive = ctx && at && (!keepAlive || disc(*ctx, p.index).sameItem(p.disc));
        if (!alive) {
            log().info("jukebox: stopped in backpack {} (slot {}): no longer playable", p.uuid, p.slot);
            stopSounds(p);
            it = mPlaybacks.erase(it);
            continue;
        }
        p.where.pos = at->pos;
        p.where.dim = at->dim;
        if (mNow >= p.end) {
            // SC onDiscFinished: ONE replays, ALL plays on (starting over), NO plays the rest.
            auto next = p.queue.onTrackEnd(occupiedDiscs(*ctx), repeatMode(*ctx));
            if (!next || !startTrack(p, *next, *ctx)) {
                log().info("jukebox: finished in backpack {} (slot {})", p.uuid, p.slot);
                stopSounds(p);
                it = mPlaybacks.erase(it);
                continue;
            }
        }
        if (cfg.particles && (mNow + p.phase) % 20 == 0) spawnNotes(p);
        ++it;
    }
}

void Service::emit(SoundEvent const& ev) const {
    if (mObserver) mObserver(ev);
}

} // namespace abp::upgrade::jukebox

ABP_REGISTER_CONFIG("jukebox", abp::upgrade::jukebox::JukeboxConfig);
ABP_REGISTER_MODULE(abp::upgrade::jukebox::Service);
