// Jukebox sounds on the client (client build): SC StorageSoundHandler. The server sends
// JukeboxPlayPacket / JukeboxStopPacket (see JukeboxNet.h); the record is played as an attached
// sound whose position the engine reads from us every update:
//   - attached to an entity: the entity's eye position plus its look vector (SC's
//     EntityBoundSoundInstance for players), updated every client tick, so the music follows the
//     carrier; for the local player at the listener (SC PlayerDirectStorageSoundInstance: no
//     attenuation);
//   - a placed backpack: the block centre.
// The sound stops on JukeboxStopPacket, when the entity is gone (SC: EntityBoundSoundInstance
// stops when its entity is removed), when the engine finished it, and when leaving the world.

#ifdef ABP_CLIENT

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/network/packet/Packet.h"
#include "ll/api/service/Bedrock.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/sound/SoundEngine.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/sound/SoundInstanceProperties.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/network/NetEventCallback.h"
#include "mc/network/ServerNetworkHandler.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/level/Level.h"

#include "client/Client.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "upgrade/jukebox/JukeboxNet.h"

namespace abp::upgrade::jukebox {

namespace {

/// Position shared between the client tick (writer) and the sound engine's property callback
/// (reader; it may run on the audio thread).
struct SharedPos {
    std::mutex m;
    Vec3       pos{};
};

struct Active {
    std::uint64_t               handle = 0;
    std::optional<std::int64_t> entity;
    std::shared_ptr<SharedPos>  pos;
    int                         age = 0; ///< client ticks since it started
};

std::unordered_map<std::string, Active> gSounds;
bool                                    gLoggedLayout = false;

/// True when a server-to-client packet really reached the client (not this process's server).
bool fromServer(NetEventCallback& callback) {
    auto handler = ll::service::getServerNetworkHandler();
    return !handler || static_cast<NetEventCallback*>(handler.as_ptr()) != &callback;
}

ClientInstance* client() { return client::currentClient(); }

Actor* entityOf(std::int64_t raw) {
    auto* player = client::currentPlayer();
    if (!player) return nullptr;
    ActorUniqueID id;
    id.rawID = raw;
    return player->getLevel().fetchEntity(id, false);
}

/// SC: players are heard from in front of their eyes; the local player hears it at the listener.
std::optional<Vec3> entityPos(std::int64_t raw) {
    auto* a = entityOf(raw);
    if (!a || a->mRemoved) return std::nullopt;
    if (a->isPlayer()) {
        auto look = a->getViewVector(1.0f);
        auto eye  = a->getEyePos();
        if (a == client::currentPlayer()) return eye;
        return Vec3{eye.x + look.x, eye.y + look.y, eye.z + look.z};
    }
    return a->getPosition();
}

void stopSound(std::string const& uuid) {
    auto it = gSounds.find(uuid);
    if (it == gSounds.end()) return;
    if (auto* c = client()) c->getSoundEngine()->stop(it->second.handle);
    gSounds.erase(it);
}

void stopAll() {
    if (auto* c = client()) {
        for (auto& [uuid, s] : gSounds) c->getSoundEngine()->stop(s.handle);
    }
    gSounds.clear();
}

void playSound(std::string const& uuid, std::string const& sound, std::optional<std::int64_t> entity, Vec3 at) {
    stopSound(uuid);
    auto* c = client();
    if (!c || sound.empty()) return;
    if (entity) {
        auto p = entityPos(*entity);
        if (!p) return; // SC: an unknown entity plays nothing
        at = *p;
    }
    auto shared = std::make_shared<SharedPos>();
    shared->pos = at;
    // SoundInstanceProperties starts with the sound's world position (Vec3).
    std::uint64_t handle = c->getSoundEngine()->playAttached(sound, [shared](SoundInstanceProperties& props) {
        static std::atomic<bool> logged{false};
        if (!logged.exchange(true)) {
            auto const* v = reinterpret_cast<Vec3 const*>(&props);
            log().info("jukebox: first attached sound update (engine position {:.1f} {:.1f} {:.1f})", v->x, v->y, v->z);
        }
        std::lock_guard lock(shared->m);
        *reinterpret_cast<Vec3*>(&props) = shared->pos;
    });
    if (!handle) {
        log().warn("jukebox: the sound engine refused '{}'", sound);
        return;
    }
    gSounds[uuid] = Active{handle, entity, std::move(shared), 0};
    if (!gLoggedLayout) {
        gLoggedLayout = true;
        log().info("jukebox: attached sound playing ({} handle {})", sound, handle);
    }
    log().info(
        "jukebox: playing {} for backpack {} {}",
        sound,
        uuid,
        entity ? "attached to entity " + std::to_string(*entity) : std::string("at the block")
    );
}

void tick() {
    if (gSounds.empty()) return;
    auto* c = client();
    if (!c) return;
    for (auto it = gSounds.begin(); it != gSounds.end();) {
        auto& s = it->second;
        ++s.age;
        if (s.entity) {
            auto p = entityPos(*s.entity);
            if (!p) {
                log().info("jukebox: entity of backpack {} is gone; stopping its sound", it->first);
                c->getSoundEngine()->stop(s.handle);
                it = gSounds.erase(it);
                continue;
            }
            std::lock_guard lock(s.pos->m);
            s.pos->pos = *p;
        }
        if (s.age == 60) {
            auto pos = [&] {
                std::lock_guard lock(s.pos->m);
                return s.pos->pos;
            }();
            log().info(
                "jukebox: sound of backpack {} active={} at {:.1f} {:.1f} {:.1f}",
                it->first,
                c->getSoundEngine()->isPlayingSound(s.handle),
                pos.x,
                pos.y,
                pos.z
            );
        }
        // SC: finished sounds are dropped every 10 ticks (give streaming a moment to start).
        if (s.age > 40 && s.age % 10 == 0 && !c->getSoundEngine()->isPlayingSound(s.handle)) {
            it = gSounds.erase(it);
            continue;
        }
        ++it;
    }
}

class PlayHandler : public ll::network::PacketHandlerBase<PlayHandler, JukeboxPlayPacket> {
public:
    void handlePacket(NetworkIdentifier const&, NetEventCallback& cb, JukeboxPlayPacket const& p) const {
        if (!fromServer(cb) || !p.valid()) return;
        auto uuid  = p.body.value("uuid", std::string{});
        auto sound = p.body.value("sound", std::string{});
        if (uuid.empty()) return;
        std::optional<std::int64_t> entity;
        Vec3                        at{};
        if (p.body.contains("entity") && p.body["entity"].is_number_integer()) {
            entity = p.body["entity"].get<std::int64_t>();
        } else if (auto pos = p.body.find("pos"); pos != p.body.end() && pos->is_array() && pos->size() == 3) {
            at = Vec3{(*pos)[0].get<float>(), (*pos)[1].get<float>(), (*pos)[2].get<float>()};
        } else {
            return;
        }
        playSound(uuid, sound, entity, at);
    }
};

class StopHandler : public ll::network::PacketHandlerBase<StopHandler, JukeboxStopPacket> {
public:
    void handlePacket(NetworkIdentifier const&, NetEventCallback& cb, JukeboxStopPacket const& p) const {
        if (!fromServer(cb) || !p.valid()) return;
        auto uuid = p.body.value("uuid", std::string{});
        if (gSounds.contains(uuid)) log().info("jukebox: stopping the sound of backpack {}", uuid);
        stopSound(uuid);
    }
};

} // namespace

/// Client side of the jukebox sounds.
class JukeboxClientSounds {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::ClientLevelTickEvent>([](auto&) { tick(); }));
        mListeners.push_back(bus.emplaceListener<ll::event::ClientExitLevelEvent>([](auto&) { stopAll(); }));
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        stopAll();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace abp::upgrade::jukebox

ABP_REGISTER_MODULE(abp::upgrade::jukebox::JukeboxClientSounds);

#endif // ABP_CLIENT
