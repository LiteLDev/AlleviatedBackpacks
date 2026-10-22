#ifdef ABP_CLIENT

#include "client/render/WorldRender.h"

#include <utility>
#include <vector>

#include "ll/api/memory/Hook.h"

#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"

#include "mod/Log.h"
#include "mod/Module.h"

namespace abp::client::render {

namespace {
std::vector<std::pair<std::string, WorldPass>>& passes() {
    static std::vector<std::pair<std::string, WorldPass>> v;
    return v;
}
bool gLogged = false;
} // namespace

void addWorldPass(std::string id, WorldPass pass) {
    removeWorldPass(id);
    passes().emplace_back(std::move(id), std::move(pass));
}

void removeWorldPass(std::string const& id) {
    std::erase_if(passes(), [&](auto const& e) { return e.first == id; });
}

// SB renders its capture effects at RenderLevelStageEvent.Stage.AFTER_BLOCK_ENTITIES. Bedrock's
// player level renderer draws the entity effects every frame right after the entities and block
// entities, with the world pass's actor render context (its block entity pass is skipped when no
// block entity is in view, so it can't carry these); the callbacks run after it.
LL_TYPE_INSTANCE_HOOK(
    AbpRenderEntityEffectsHook,
    ll::memory::HookPriority::Normal,
    LevelRendererPlayer,
    &LevelRendererPlayer::$renderEntityEffects,
    void,
    ::BaseActorRenderContext& renderContext
) {
    origin(renderContext);
    if (passes().empty()) return;
    if (!gLogged) {
        gLogged = true;
        log().info("world render: entity effects pass reached ({} callbacks)", passes().size());
    }
    // Copy: a callback may add or remove passes.
    auto list = passes();
    for (auto const& [id, pass] : list) {
        try {
            if (pass) pass(renderContext);
        } catch (std::exception const& e) {
            log().error("world pass {} failed: {}", id, e.what());
        }
    }
}

class WorldRenderModule {
public:
    void install() { AbpRenderEntityEffectsHook::hook(); }
    void uninstall() { AbpRenderEntityEffectsHook::unhook(); }
};

} // namespace abp::client::render

ABP_REGISTER_MODULE(abp::client::render::WorldRenderModule);

#endif // ABP_CLIENT
