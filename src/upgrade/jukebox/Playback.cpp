// Jukebox playback: which disc plays where, who hears it, when the next one starts.

#include <algorithm>
#include <cmath>
#include <set>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/legacy/ActorUniqueID.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/network/packet/StopSoundPacket.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/Level.h"

#include "game/PlayerScan.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "upgrade/jukebox/Jukebox.h"

namespace abp::upgrade::jukebox {

namespace {
constexpr int kCheckEvery    = 10; ///< ticks between listener / carrier checks
constexpr int kPlacedTimeout = 40; ///< ticks without `tickPlaced` before a placed backpack stops

Player* findPlayer(std::int64_t key) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    ActorUniqueID id;
    id.rawID = key;
    return level->getPlayer(id);
}

std::int64_t keyOf(Player const& p) { return p.getOrCreateUniqueID().rawID; }

bool carries(Player& player, std::string const& uuid) {
    for (auto const& c : game::scanBackpacks(player))
        if (c.uuid && *c.uuid == uuid) return true;
    return false;
}

float distance(Vec3 const& a, Vec3 const& b) {
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

std::string message(Player* p, std::string_view key, std::string_view fallback) {
    return p ? trFor(*p, key, fallback) : tr(key, fallback);
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
    mPlaced.clear();
    mLater.clear();
    if (mTick) ll::event::EventBus::getInstance().removeListener(mTick);
    mTick = nullptr;
}

// ---- control -------------------------------------------------------------------------------------

bool Service::play(UpgradeContext const& ctx, Player* starter, std::optional<int> index, std::string* reason) {
    std::string dummy;
    auto&       why = reason ? *reason : dummy;
    if (!isJukebox(ctx) || !ctx.state().enabled) return false;
    auto occupied = occupiedDiscs(ctx);
    if (std::none_of(occupied.begin(), occupied.end(), [](bool b) { return b; })) {
        why = message(starter, "abp.jukebox.no_disc", "Put a music disc into the jukebox first.");
        return false;
    }
    auto const& uuid = ctx.backpack().uuid();
    Playback    p;
    p.backpack    = ctx.backpackPtr();
    p.uuid        = uuid;
    p.slot        = ctx.slot();
    p.upgradeItem = ctx.state().item.id;
    p.phase       = static_cast<int>(std::hash<std::string>{}(uuid) % kCheckEvery);
    p.queue       = core::JukeboxQueue(static_cast<std::uint32_t>(std::hash<std::string>{}(uuid) ^ static_cast<std::size_t>(mNow)));
    // Where does it play from?
    if (starter && carries(*starter, uuid)) {
        p.carrier = keyOf(*starter);
    } else if (auto it = mPlaced.find(uuid); it != mPlaced.end() && mNow - it->second.lastSeen <= kPlacedTimeout) {
        p.carried = false;
    } else {
        Player* holder = nullptr;
        if (auto level = ll::service::getLevel()) {
            level->forEachPlayer([&](Player& pl) {
                if (pl.isClientSide() || !carries(pl, uuid)) return true;
                holder = &pl;
                return false;
            });
        }
        if (!holder) {
            why = message(starter, "abp.jukebox.not_carried", "Carry the backpack (or place it) to play music.");
            return false;
        }
        p.carrier = keyOf(*holder);
    }
    auto first = p.queue.start(occupied, shuffle(ctx), index);
    if (!first) return false;
    Key key{uuid, ctx.slot()};
    if (auto it = mPlaybacks.find(key); it != mPlaybacks.end()) {
        stopSounds(it->second);
        mPlaybacks.erase(it);
    }
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
    mPlaced.erase(uuid);
}

bool Service::next(UpgradeContext const& ctx) {
    auto it = mPlaybacks.find(Key{ctx.backpack().uuid(), ctx.slot()});
    if (it == mPlaybacks.end() || !isJukebox(ctx)) return false;
    auto n = it->second.queue.skipNext(occupiedDiscs(ctx));
    if (!n || !startTrack(it->second, *n, ctx)) {
        stop(ctx.backpack().uuid(), ctx.slot());
        return false;
    }
    return true;
}

bool Service::previous(UpgradeContext const& ctx) {
    auto it = mPlaybacks.find(Key{ctx.backpack().uuid(), ctx.slot()});
    if (it == mPlaybacks.end() || !isJukebox(ctx)) return false;
    auto n = it->second.queue.skipPrevious(occupiedDiscs(ctx));
    if (!n || !startTrack(it->second, *n, ctx)) {
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

void Service::tickPlaced(std::shared_ptr<game::Backpack> const& backpack, int dim, Vec3 const& pos) {
    if (!backpack) return;
    mPlaced[backpack->uuid()] = Placed{dim, pos, mNow};
}

// ---- playback ------------------------------------------------------------------------------------

bool Service::startTrack(Playback& p, int index, UpgradeContext const& ctx) {
    auto d    = disc(ctx, index);
    auto info = discInfo(d);
    if (!info) return false;
    auto e = emitter(p, false);
    if (!e) return false;
    stopSounds(p);
    p.index = index;
    p.disc  = d;
    p.info  = *info;
    p.sound = p.carried ? followSoundName(*info) : blockSoundName(*info);
    p.start = mNow;
    p.end   = mNow + std::max(1, mDurationOverride.value_or(info->durationTicks));
    updateListeners(p, *e, true);
    return true;
}

void Service::stopSounds(Playback& p) {
    for (auto id : p.heard) {
        if (auto* pl = findPlayer(id)) sendStop(*pl, p.sound);
        forgetOthers(p, id);
    }
    p.heard.clear();
}

void Service::forgetOthers(Playback const& p, std::int64_t listener) {
    // StopSound works by name: it also silenced other playbacks of the same disc for this
    // listener. Forget them so the next listener update sends them again (at their position).
    for (auto& [key, other] : mPlaybacks) {
        if (&other != &p && other.sound == p.sound) other.heard.erase(listener);
    }
}

void Service::spawnNotes(Emitter const& e) {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto at = e.pos;
    at.y += e.carried ? 0.6f : 1.2f;
    if (e.carrier) {
        level->spawnParticleEffect("minecraft:note_particle", at, &e.carrier->getDimension());
    } else if (auto dim = level->getDimension(e.dim).lock()) {
        level->spawnParticleEffect("minecraft:note_particle", at, dim.get());
    }
}

std::optional<Service::Emitter> Service::emitter(Playback& p, bool deepCheck) {
    if (p.carried) {
        auto* pl = findPlayer(p.carrier);
        if (!pl || !pl->isAlive()) return std::nullopt;
        if (deepCheck && !carries(*pl, p.uuid)) return std::nullopt;
        return Emitter{true, static_cast<int>(pl->getDimensionId()), pl->getPosition(), pl};
    }
    auto it = mPlaced.find(p.uuid);
    if (it == mPlaced.end() || mNow - it->second.lastSeen > kPlacedTimeout) return std::nullopt;
    return Emitter{false, it->second.dim, it->second.pos, nullptr};
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
    if (!mLater.empty()) {
        std::vector<std::function<void()>> due;
        std::erase_if(mLater, [&](auto& e) {
            if (e.first > mNow) return false;
            due.push_back(std::move(e.second));
            return true;
        });
        for (auto& fn : due) fn();
    }
    if (mNow % 100 == 0) {
        std::erase_if(mPlaced, [&](auto const& e) { return mNow - e.second.lastSeen > 100; });
    }
    if (mPlaybacks.empty()) return;
    auto const& cfg = config::get<JukeboxConfig>();
    for (auto it = mPlaybacks.begin(); it != mPlaybacks.end();) {
        auto& p     = it->second;
        bool  check = (mNow + p.phase) % kCheckEvery == 0;
        auto  ctx   = validContext(p);
        auto  e     = ctx ? emitter(p, check) : std::nullopt;
        if (!ctx || !e) {
            log().info("jukebox: stopped in backpack {} (slot {}): no longer playable", p.uuid, p.slot);
            stopSounds(p);
            it = mPlaybacks.erase(it);
            continue;
        }
        std::optional<int> next;
        bool               change = false;
        if (check && !disc(*ctx, p.index).sameItem(p.disc)) {
            change   = true;
            auto occ = occupiedDiscs(*ctx);
            if (occ[static_cast<std::size_t>(p.index)]) {
                // Swapped for another disc: play the new one.
                next = p.index;
            } else {
                // Taken out: carry on as if the track had ended (repeat mode applies; "repeat one"
                // has nothing left to repeat and moves on).
                next = p.queue.onTrackEnd(occ, repeatMode(*ctx));
            }
        } else if (mNow >= p.end) {
            change = true;
            next   = p.queue.onTrackEnd(occupiedDiscs(*ctx), repeatMode(*ctx));
        }
        if (change) {
            if (!next || !startTrack(p, *next, *ctx)) {
                log().info("jukebox: finished in backpack {} (slot {})", p.uuid, p.slot);
                stopSounds(p);
                it = mPlaybacks.erase(it);
                continue;
            }
        } else if (check) {
            updateListeners(p, *e, false);
        }
        if (cfg.particles && (mNow + p.phase) % 20 == 0) spawnNotes(*e);
        ++it;
    }
}

void Service::updateListeners(Playback& p, Emitter const& e, bool force) {
    auto level = ll::service::getLevel();
    if (!level) return;
    float                  range = static_cast<float>(std::max(1, config::get<JukeboxConfig>().range));
    float                  offset = static_cast<float>(mNow - p.start) / 20.0f;
    std::set<std::int64_t> present;
    level->forEachPlayer([&](Player& pl) {
        if (pl.isClientSide()) return true;
        auto id = keyOf(pl);
        present.insert(id);
        bool  isCarrier = p.carried && id == p.carrier;
        float d         = distance(pl.getPosition(), e.pos);
        bool  inRange   = isCarrier || (static_cast<int>(pl.getDimensionId()) == e.dim && d <= range);
        bool  hearing   = p.heard.contains(id);
        if (inRange && (!hearing || force)) {
            // 2D copies can't fade with distance: scale the volume once, when the player starts hearing it.
            float volume = (isCarrier || !p.carried) ? 1.0f : std::clamp(1.0f - d / range, 0.25f, 1.0f);
            sendPlay(pl, p, e, volume, offset);
            p.heard.insert(id);
        } else if (!inRange && hearing) {
            sendStop(pl, p.sound);
            p.heard.erase(id);
            forgetOthers(p, id);
        }
        return true;
    });
    std::erase_if(p.heard, [&](std::int64_t id) { return !present.contains(id); });
}

void Service::sendPlay(Player& to, Playback const& p, Emitter const& e, float volume, float offset) {
    bool  positional = !p.carried || p.info.key.empty();
    Vec3  at         = p.carried ? to.getPosition() : e.pos;
    auto  pkt        = MinecraftPackets::createPacket(MinecraftPacketIds::PlaySound);
    auto& s          = static_cast<PlaySoundPacket&>(*pkt);
    s.mName          = p.sound;
    s.mPos           = BlockPos{
        static_cast<int>(std::floor(at.x * 8.0f)),
        static_cast<int>(std::floor(at.y * 8.0f)),
        static_cast<int>(std::floor(at.z * 8.0f))
    };
    s.mVolume                   = volume;
    s.mPitch                    = 1.0f;
    s.mLoopCount                = 0;
    s.mBypassListenerRangeCheck = !positional;
    s.mServerSoundHandle->reset();
    if (offset >= 0.25f) *s.mPlaybackPositionSeconds = offset;
    else s.mPlaybackPositionSeconds->reset();
    s.sendTo(to);
    emit(SoundEvent{SoundEvent::Kind::Play, keyOf(to), p.sound, volume, offset, positional});
}

void Service::sendStop(Player& to, std::string const& sound) {
    auto  pkt         = MinecraftPackets::createPacket(MinecraftPacketIds::StopSound);
    auto& s           = static_cast<StopSoundPacket&>(*pkt);
    s.mName           = sound;
    s.mStopAll        = false;
    s.mStopMusicLegacy = false;
    s.sendTo(to);
    emit(SoundEvent{SoundEvent::Kind::Stop, keyOf(to), sound});
}

void Service::emit(SoundEvent const& ev) const {
    if (mObserver) mObserver(ev);
}

} // namespace abp::upgrade::jukebox

ABP_REGISTER_CONFIG("jukebox", abp::upgrade::jukebox::JukeboxConfig);
ABP_REGISTER_MODULE(abp::upgrade::jukebox::Service);
