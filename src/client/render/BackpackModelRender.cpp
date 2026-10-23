#ifdef ABP_CLIENT

// 3D backpack model on every entity surface (P2-11): Molang query + two-colour tint.
// See ModelState.h for the design.

#include "client/render/ModelState.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/client/model/models/DataDrivenModel.h"
#include "mc/client/renderer/ActorShaderManager.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec4.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/util/MolangQueryFunction.h"
#include "mc/util/MolangScriptArg.h"
#include "mc/util/molang/ExpressionNode.h"
#include "mc/util/molang/IComplexExpression.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/RenderParams.h"
#include "mc/world/actor/animation/AttachableSlotIndex.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemStackBase.h"

#include "client/net/ClientNet.h"
#include "core/Tier.h"
#include "feature/model/DebugPose.h"
#include "game/BackpackItem.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace abp::client::render {

namespace {

using Clock = std::chrono::steady_clock;

// ---- snapshot (any thread) ------------------------------------------------------------------------
std::shared_mutex                                gMutex;
std::unordered_map<std::string, ModelSnapshot>   gSnapshots; ///< by uuid
std::mutex                                       gWantedMutex;
std::unordered_map<std::string, Clock::time_point> gWanted;   ///< uuid -> last time it was rendered

std::thread::id gClientThread;

// Transforms of client-only display entities (setEntityXf).
std::shared_mutex                                gXfMutex;
std::unordered_map<Actor const*, EntityXf>       gXf;

// Accent colour by main colour (24 bit) of the backpacks rendered recently: the cloth tint
// (changeColor) comes from the item colour, which the server keeps equal to the main colour, so
// the draw call of a backpack can be recognised by it and given its accent (changeColor2) even
// when the game sets the constants outside our attachable hooks (deferred draws).
std::shared_mutex                               gAccentMutex;
std::unordered_map<std::uint32_t, std::uint32_t> gAccentByMain;
std::atomic<bool>                               gHasAccents{false};

std::uint32_t rgbOf(mce::Color const& c) {
    auto ch = [](float v) { return static_cast<std::uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    return (ch(c.r) << 16) | (ch(c.g) << 8) | ch(c.b);
}

void want(std::string const& uuid) {
    std::lock_guard lock(gWantedMutex);
    gWanted[uuid] = Clock::now();
}

/// Uuid of a backpack item, read without touching game state other than the item's user data.
std::optional<std::string> uuidOf(ItemStackBase const& item) {
    if (item.isNull()) return std::nullopt;
    if (!game::backpackTierOf(item.getTypeName())) return std::nullopt;
    return game::backpackUuid(static_cast<ItemStack const&>(item));
}

// ---- Molang ------------------------------------------------------------------------------------
MolangScriptArg const& ret(RenderParams& params, float v) {
    params.mScratchValue->mType       = MolangScriptArgType::Float;
    params.mScratchValue->mPOD._mData = 0;
    params.mScratchValue->mPOD.mFloat = v;
    return params.mScratchValue;
}

/// Constant or computed argument. (LeviLamina's ExpressionNode::evalGeneric throws on constants.)
float argOf(RenderParams& params, std::vector<ExpressionNode> const& args, std::size_t i) {
    if (i >= args.size()) return 0;
    auto const& impl = *args[i].mImpl;
    if (impl.index() == 1) return std::get<1>(impl);
    auto const& expr = std::get<0>(impl);
    if (!expr) return 0;
    auto const& a = expr->evalGeneric(params);
    return a.mType == MolangScriptArgType::Float ? a.mPOD.mFloat : 0.0f;
}

bool isDisplayEntity(Actor const& actor) { return actor.getTypeName() == kDisplayEntityId; }

/// Ticks (20 per second) since the client started, wrapped (at a multiple of every flipbook's
/// frames x ticks per frame) to keep the float exact.
double animationTicks() {
    static auto const start = Clock::now();
    double t = std::chrono::duration<double>(Clock::now() - start).count() * 20.0;
    return std::fmod(t, 20.0 * 32.0 * 16.0 * 10.0);
}

/// The item the attachable being rendered stands for.
ItemStack const* itemOf(RenderParams const& params) {
    if (params.mItemStackBasePtr && !params.mItemStackBasePtr->isNull()) return static_cast<ItemStack const*>(params.mItemStackBasePtr);
    auto* actor = params.mActor;
    if (!actor) return nullptr;
    switch (params.mSlotIndex) {
    case AttachableSlotIndex::Torso: return &actor->getArmor(SharedTypes::Legacy::ArmorSlot::Torso);
    case AttachableSlotIndex::CarriedItem: return &actor->getCarriedItem();
    case AttachableSlotIndex::OffhandItem: return &actor->getOffhandSlot();
    default:
        // The display entity's own render controllers / animations: the backpack it wears.
        return isDisplayEntity(*actor) ? &actor->getArmor(SharedTypes::Legacy::ArmorSlot::Torso) : nullptr;
    }
}

int contextOf(RenderParams const& params) {
    if (params.mActor && isDisplayEntity(*params.mActor)) return 4;
    switch (params.mSlotIndex) {
    case AttachableSlotIndex::Torso: return params.mActor && isDisplayEntity(*params.mActor) ? 4 : 1;
    case AttachableSlotIndex::CarriedItem: return 2;
    case AttachableSlotIndex::OffhandItem: return 3;
    default: return 0;
    }
}

float field(ModelSnapshot const* s, ModelField f, RenderParams const& params) {
    using core::render::kChargeSteps;
    using core::render::kFluidSteps;
    using core::render::ratioToStep;
    switch (f) {
    case ModelField::Known: return s ? 1.0f : 0.0f;
    case ModelField::Context: return static_cast<float>(contextOf(params));
    case ModelField::AnimTicks: return static_cast<float>(animationTicks());
    default: break;
    }
    if (!s) return 0;
    auto const& m = s->model;
    switch (f) {
    case ModelField::Battery: return m.battery ? 1.0f : 0.0f;
    case ModelField::LeftTank: return m.left ? 1.0f : 0.0f;
    case ModelField::RightTank: return m.right ? 1.0f : 0.0f;
    case ModelField::Charge: return m.battery ? static_cast<float>(ratioToStep(*m.battery, kChargeSteps)) : 0.0f;
    case ModelField::LeftLevel: return m.left ? static_cast<float>(ratioToStep(m.left->ratio, kFluidSteps)) : 0.0f;
    case ModelField::RightLevel: return m.right ? static_cast<float>(ratioToStep(m.right->ratio, kFluidSteps)) : 0.0f;
    case ModelField::LeftUp: return m.left && m.left->lighter ? 1.0f : 0.0f;
    case ModelField::RightUp: return m.right && m.right->lighter ? 1.0f : 0.0f;
    case ModelField::LeftLook: return m.left ? static_cast<float>(core::render::fluidLook(m.left->fluid)) : 0.0f;
    case ModelField::RightLook: return m.right ? static_cast<float>(core::render::fluidLook(m.right->fluid)) : 0.0f;
    case ModelField::HasDisplay: return m.display ? 1.0f : 0.0f;
    case ModelField::DisplayRot: return m.display ? static_cast<float>(m.display->rotation) : 0.0f;
    case ModelField::DisplayZ: return m.display ? static_cast<float>(m.display->zOffset) : 0.0f;
    default: return 0;
    }
}

MolangScriptArg const& modelQuery(RenderParams& params, std::vector<ExpressionNode> const& args) {
    int n = static_cast<int>(argOf(params, args, 0));
    if (n >= kEntityXfField && n < kEntityXfField + 8) {
        std::optional<EntityXf> xf;
        if (params.mActor) {
            std::shared_lock lock(gXfMutex);
            if (auto it = gXf.find(params.mActor); it != gXf.end()) xf = it->second;
        }
        if (!xf) return ret(params, 0.0f);
        float const v[8] = {1.0f, xf->px, xf->py, xf->pz, xf->rx, xf->ry, xf->rz, xf->scale};
        return ret(params, v[n - kEntityXfField]);
    }
    if (n >= 30) {
        auto& pose = model::debugPose((n - 30) / 8);
        int   k    = (n - 30) % 8;
        return ret(params, k == 0 ? (pose.on ? 1.0f : 0.0f) : pose.v[static_cast<std::size_t>(k - 1)].load());
    }
    auto f = static_cast<ModelField>(n);
    std::optional<ModelSnapshot> snap;
    if (auto const* item = itemOf(params)) snap = modelFor(*item);
    return ret(params, field(snap ? &*snap : nullptr, f, params));
}

// ---- tint ------------------------------------------------------------------------------------------
struct Tint {
    mce::Color main, accent;
};
thread_local std::vector<Tint> tTint; ///< colours of the backpack attachable(s) being set up / drawn

mce::Color toColor(std::uint32_t argb) {
    return mce::Color(((argb >> 16) & 0xFF) / 255.0f, ((argb >> 8) & 0xFF) / 255.0f, (argb & 0xFF) / 255.0f, 1.0f);
}

std::optional<Tint> tintFor(ItemStack const& item) {
    if (item.isNull() || !game::backpackTierOf(item.getTypeName())) return std::nullopt;
    auto snap = modelFor(item);
    std::uint32_t main   = snap ? snap->mainColor : core::render::kDefaultMainColor;
    std::uint32_t accent = snap ? snap->accentColor : core::render::kDefaultAccentColor;
    Tint t{toColor(main), toColor(accent)};
    return t;
}

struct TintScope {
    bool active = false;
    explicit TintScope(ItemStack const& item) {
        if (auto t = tintFor(item)) {
            tTint.push_back(*t);
            active = true;
        }
    }
    ~TintScope() {
        if (active) tTint.pop_back();
    }
};

} // namespace

void setEntityXf(Actor const* displayEntity, std::optional<EntityXf> xf) {
    std::unique_lock lock(gXfMutex);
    if (xf) gXf[displayEntity] = *xf;
    else gXf.erase(displayEntity);
}

std::optional<ModelSnapshot> modelFor(std::string const& uuid) {
    want(uuid);
    std::shared_lock lock(gMutex);
    auto it = gSnapshots.find(uuid);
    if (it == gSnapshots.end()) return std::nullopt;
    return it->second;
}

std::optional<ModelSnapshot> modelFor(ItemStackBase const& item) {
    auto uuid = uuidOf(item);
    if (!uuid) return std::nullopt;
    return modelFor(*uuid);
}

// ---- hooks -------------------------------------------------------------------------------------------

LL_TYPE_INSTANCE_HOOK(
    AbpSetupAttachableHook,
    ll::memory::HookPriority::Normal,
    DataDrivenModel,
    &DataDrivenModel::setupAttachable,
    void,
    ::ItemStack const&           item,
    ::AttachableSlotIndex const& slot,
    ::RenderParams&              params,
    ::Actor&                     actor
) {
    TintScope scope(item);
    origin(item, slot, params, actor);
}

LL_TYPE_INSTANCE_HOOK(
    AbpRenderAttachableHook,
    ll::memory::HookPriority::Normal,
    DataDrivenModel,
    &DataDrivenModel::renderAttachable,
    void,
    ::ItemStack const&           item,
    ::AttachableSlotIndex const& slot,
    ::RenderParams&              params,
    ::Actor&                     actor
) {
    TintScope scope(item);
    origin(item, slot, params, actor);
}

// Backpacks are chest equipment, which the game would only draw as an attachable when worn; SB
// draws the 3D model in the hand too. Only on the client thread (the integrated server's checks
// on its own thread stay vanilla).
LL_TYPE_INSTANCE_HOOK(AbpAttachableEquipmentHook, ll::memory::HookPriority::Normal, ItemStackBase, &ItemStackBase::isAttachableEquipment, bool) {
    if (std::this_thread::get_id() == gClientThread && !isNull() && game::backpackTierOf(getTypeName())) return false;
    return origin();
}

LL_STATIC_HOOK(
    AbpEntityConstantsHook,
    ll::memory::HookPriority::Normal,
    &ActorShaderManager::setEntityConstants,
    void,
    ::mce::ActorConstants& entityConstants,
    ::mce::RenderContext&  renderContext,
    ::mce::Color const&    tileLightColor,
    ::Vec2 const&          tileLightColorUV,
    ::glm::vec4 const&     blockLightColor,
    ::mce::Color const&    overlay,
    ::mce::Color const&    changeColor,
    ::mce::Color const&    changeColor2,
    ::mce::Color const&    glintColor,
    ::Vec2 const&          glintUVScale,
    ::Vec4 const&          uvAnim,
    float                  uvOffset1,
    float                  uvOffset2,
    float                  uvRot1,
    float                  uvRot2
) {
    if (!tTint.empty()) {
        // entity_alphatest_multicolor_tint: texture red = cloth (main colour), green = border
        // (accent colour), like SB's tint indexes 0 and 1.
        auto const& t = tTint.back();
        return origin(entityConstants, renderContext, tileLightColor, tileLightColorUV, blockLightColor, overlay, t.main, t.accent, glintColor, glintUVScale, uvAnim, uvOffset1, uvOffset2, uvRot1, uvRot2);
    }
    if (gHasAccents.load(std::memory_order_relaxed)) {
        std::optional<std::uint32_t> accent;
        {
            std::shared_lock lock(gAccentMutex);
            if (auto it = gAccentByMain.find(rgbOf(changeColor)); it != gAccentByMain.end()) accent = it->second;
        }
        if (accent) {
            auto a = toColor(*accent);
            return origin(entityConstants, renderContext, tileLightColor, tileLightColorUV, blockLightColor, overlay, changeColor, a, glintColor, glintUVScale, uvAnim, uvOffset1, uvOffset2, uvRot1, uvRot2);
        }
    }
    origin(entityConstants, renderContext, tileLightColor, tileLightColorUV, blockLightColor, overlay, changeColor, changeColor2, glintColor, glintUVScale, uvAnim, uvOffset1, uvOffset2, uvRot1, uvRot2);
}

/// Registers the query and hooks; refreshes the snapshot every client tick.
class BackpackModelRender {
public:
    void install() {
        gClientThread = std::this_thread::get_id();
        try {
            ExpressionNode::registerQueryFunction(
                "query.abp_model",
                modelQuery,
                "AlleviatedBackpacks: backpack model state (field)",
                MolangQueryFunctionReturnType::Float,
                HashedString("default"),
                1,
                1,
                {}
            );
        } catch (std::exception const& e) {
            log().error("could not register query.abp_model: {}", e.what());
        }
        AbpSetupAttachableHook::hook();
        AbpRenderAttachableHook::hook();
        AbpAttachableEquipmentHook::hook();
        AbpEntityConstantsHook::hook();
        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientLevelTickEvent>([](auto&) { tick(); });
    }
    void uninstall() {
        if (mTick) ll::event::EventBus::getInstance().removeListener(mTick);
        mTick.reset();
        AbpSetupAttachableHook::unhook();
        AbpRenderAttachableHook::unhook();
        AbpAttachableEquipmentHook::unhook();
        AbpEntityConstantsHook::unhook();
        std::unique_lock lock(gMutex);
        gSnapshots.clear();
    }

private:
    static void tick() {
        gClientThread = std::this_thread::get_id();
        std::vector<std::string> active;
        {
            std::lock_guard lock(gWantedMutex);
            auto now = Clock::now();
            for (auto it = gWanted.begin(); it != gWanted.end();) {
                if (now - it->second > std::chrono::seconds(10)) {
                    it = gWanted.erase(it);
                } else {
                    if (now - it->second < std::chrono::seconds(2)) active.push_back(it->first);
                    ++it;
                }
            }
        }
        for (auto const& uuid : active) {
            auto const* info = net::renderInfo(uuid);
            if (!info) continue;
            net::refreshRenderInfo(uuid);
            ModelSnapshot s;
            s.tier        = info->tier;
            s.mainColor   = info->mainColor;
            s.accentColor = info->accentColor;
            if (auto it = info->extra.find("model"); it != info->extra.end()) s.model = core::render::BackpackModel::fromJson(*it);
            std::unique_lock lock(gMutex);
            gSnapshots[uuid] = std::move(s);
        }
    // Accents by main colour (defaults included: unknown / fresh backpacks).
    std::unordered_map<std::uint32_t, std::uint32_t> accents;
    accents[core::render::kDefaultMainColor & 0xFFFFFF] = core::render::kDefaultAccentColor;
    {
        std::shared_lock lock(gMutex);
        for (auto const& [_, s] : gSnapshots) accents[s.mainColor & 0xFFFFFF] = s.accentColor;
    }
    {
        std::unique_lock lock(gAccentMutex);
        gAccentByMain.swap(accents);
    }
    gHasAccents = true;
    }

    ll::event::ListenerPtr mTick;
};

} // namespace abp::client::render

ABP_REGISTER_MODULE(abp::client::render::BackpackModelRender);

#endif // ABP_CLIENT
