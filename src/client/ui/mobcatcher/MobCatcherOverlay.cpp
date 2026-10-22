#ifdef ABP_CLIENT

// Client side of the Mob Catcher upgrades (SB `MobCatcherInventoryControl`,
// `ClientMobCatcherHealthTooltip` and `MobCatcherUpgradeWrapper.clientTick`):
// - each captured mob is drawn over its slot footprint: SC's recessed control background with a
//   dark gradient, and the live entity (a display copy of the captured mob, idly looking around);
// - hovering shows the mob's name, "Click to release" (grey italic) and a row of hearts, plus the
//   release icon in the footprint's bottom-right corner; a left click releases the mob;
// - while carried, a backpack with captured mobs plays one of their ambient sounds, muffled,
//   every 220-480 ticks.

#include <chrono>
#include <cmath>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/sound/Sound.h"
#include "mc/client/sound/SoundEngine.h"
#include "mc/client/sound/SoundMapping.h"
#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/locale/I18n.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/item/ItemStack.h"

#include "client/Client.h"
#include "client/net/ClientNet.h"
#include "client/ui/gfx/Painter.h"
#include "client/ui/mobcatcher/EntityPreview.h"
#include "client/ui/screen/StorageOverlay.h"
#include "client/ui/widgets/SbStyle.h"
#include "client/ui/widgets/Tooltip.h"
#include "game/BackpackItem.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "upgrade/mobcatcher/MobCatcherLogic.h"

namespace abp::client::ui {

namespace {

namespace mc = abp::upgrade::mobcatcher;

constexpr std::uint32_t kBackgroundColor = 0xFF2B2B2B; ///< SB CAPTURED_MOB_BACKGROUND_COLOR
constexpr float         kBodyYawRange       = 50.f;
constexpr float         kHeadStaticYawRange = 24.f;
constexpr float         kHeadIdleYawAmplitude = 33.f;
constexpr float         kHeadStaticPitchRange = 6.f;
constexpr float         kHeadIdlePitchAmplitude = 10.f;

double seconds() {
    static auto const start = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}
float randomFloat() { return std::uniform_real_distribution<float>(0.f, 1.f)(rng()); }
int   randomInt(int bound) { return std::uniform_int_distribution<int>(0, std::max(0, bound - 1))(rng()); }

/// The localized name of an entity type ("entity.cow.name"), the client's own translation.
std::string entityName(std::string const& type) {
    constexpr std::string_view kVanilla = "minecraft:";
    std::string key = type.starts_with(kVanilla) ? "entity." + type.substr(kVanilla.size()) + ".name" : "entity." + type + ".name";
    try {
        auto s = getI18n().get(key, nullptr);
        if (!s.empty() && s != key) return s;
    } catch (...) {
    }
    return type;
}

// ---- display entities (shared by the overlay; released when leaving the world) -------------------

struct EntityCache {
    std::unordered_map<std::string, std::unique_ptr<DisplayEntity>> entities; ///< by captured mob id
    std::unordered_set<std::string>                                 failures;
    std::unordered_set<std::string>                                 logged;

    DisplayEntity* get(mc::CapturedMob const& mob) {
        if (failures.contains(mob.id)) return nullptr;
        if (auto it = entities.find(mob.id); it != entities.end()) return it->second.get();
        std::unique_ptr<DisplayEntity> e;
        try {
            e = createDisplayEntity(mob.entityType, mob.look.value("data", nlohmann::json::array()));
        } catch (...) {
        }
        if (!e) {
            failures.insert(mob.id);
            log().warn("mob catcher: unable to create render entity for captured mob {} ({})", mob.displayName, mob.entityType);
            return nullptr;
        }
        return entities.emplace(mob.id, std::move(e)).first->second.get();
    }

    /// Drops entities of mobs no longer listed.
    void keepOnly(std::vector<mc::CapturedMob> const& mobs) {
        std::erase_if(entities, [&](auto const& kv) {
            return std::none_of(mobs.begin(), mobs.end(), [&](mc::CapturedMob const& m) { return m.id == kv.first; });
        });
    }

    void clear() {
        entities.clear();
        failures.clear();
    }
};

EntityCache& cache() {
    static EntityCache c;
    return c;
}

// ---- the overlay -----------------------------------------------------------------------------------

class MobCatcherOverlay final : public StorageOverlay {
public:
    [[nodiscard]] bool replacesSlot(StorageOverlayContext const& ctx, int index) const override {
        auto const& d = data(ctx);
        return mc::isSlotCovered(d.mobs, index, d.columns);
    }

    void render(Painter& p, StorageOverlayContext const& ctx, float mx, float my) override {
        auto const& d = data(ctx);
        if (d.mobs.empty()) return;
        auto const* hovered = hoveredMob(ctx, mx, my);
        for (auto const& mob : d.mobs) {
            auto b = renderBounds(ctx, mob);
            if (!b) continue;
            Rect clip = clipRect(ctx);
            drawArea(p, *b, mob, clip);
            if (auto* e = cache().get(mob)) {
                double bbW   = mob.look.value("bbWidth", 0.6);
                double bbH   = mob.look.value("bbHeight", 1.8);
                int    scale = mc::previewScale(bbW, bbH, static_cast<int>(b->w), static_cast<int>(b->h));
                float  bottom = b->y + b->h / 2.f + static_cast<float>(bbH) * scale / 2.f;
                if (!renderEntity(p, e->actor(), b->x + b->w / 2.f, bottom, static_cast<float>(scale), poseOf(mob), &clip)) {
                    cache().failures.insert(mob.id);
                    cache().entities.erase(mob.id);
                } else if (cache().logged.insert(mob.id).second) {
                    log().info("mob catcher: preview of {} rendered ({}x{} slots at {:.0f},{:.0f}, scale {})", mob.entityType, mob.width, mob.height, b->x, b->y, scale);
                }
            }
            if (hovered == &mob && ctx.model.kind == "backpack") {
                // SB renderReleaseHint: the transfer-to-inventory arrow at the bottom-right corner.
                sb::icon(p, sb::Icon::TransferToInventory, b->x + b->w - 14, b->y + b->h - 14);
            }
        }
        p.flush();
    }

    bool mouseDown(StorageOverlayContext const& ctx, MouseButton button, float x, float y) override {
        if (button != MouseButton::Left || ctx.model.kind != "backpack") return false;
        auto const& d   = data(ctx);
        auto const* mob = hoveredMob(ctx, x, y);
        if (!mob || d.upgradeSlot < 0) return false;
        ctx.sendTabAction(d.upgradeSlot, nlohmann::json{{"release", mob->id}});
        log().info("mob catcher: release {} requested", mob->entityType);
        return true;
    }

    bool tooltip(Painter& p, StorageOverlayContext const& ctx, float mx, float my) override {
        auto const* mob = hoveredMob(ctx, mx, my);
        if (!mob) return false;
        std::vector<TooltipLine> lines;
        lines.push_back({mob->displayName.empty() ? entityName(mob->entityType) : mob->displayName, 0xFFFFFF});
        if (cache().failures.contains(mob->id)) {
            lines.push_back({ctr("abp.mobcatcher.entity_preview_failed", "Entity preview failed to render"), 0xFF5555});
        }
        if (ctx.model.kind == "backpack") lines.push_back({"§o" + ctr("abp.mobcatcher.click_to_release", "Click to release"), 0xAAAAAA});
        auto         row = mc::heartRow(mob->currentHealth, mob->maxHealth);
        TooltipImage image;
        image.height = static_cast<float>(mc::kHeartSize + 1);
        image.width  = heartsWidth(p, row);
        image.draw   = [row](Painter& painter, float left, float top) { drawHearts(painter, row, left, top); };
        drawTooltip(p, lines, mx, my, &image);
        return true;
    }

    void errorOverlay(Painter& p, StorageOverlayContext const& ctx, std::vector<int> const& errorSlots) override {
        auto const& d   = data(ctx);
        auto const  vis = ctx.visibleStorage;
        if (vis.empty()) return;
        for (auto const& mob : d.mobs) {
            auto b = renderBounds(ctx, mob);
            if (!b || std::none_of(errorSlots.begin(), errorSlots.end(), [&](int s) { return mob.occupiesSlot(s, d.columns); })) continue;
            // SB getInteriorVisibleBounds
            float visBottom = vis.bottom(), capBottom = b->bottom();
            float left   = std::max(b->x, vis.x);
            float top    = b->y < vis.y ? vis.y - 1 : b->y;
            float right  = std::min(b->x + b->w - 2, vis.right());
            float bottom = capBottom > visBottom ? visBottom + 1 : capBottom - 2;
            if (right <= left || bottom <= top) continue;
            p.fillRect({left, top, right - left, bottom - top}, sb::color::ERROR_SLOT, Painter::Layer::Foreground);
        }
    }

    void reset() override { mRevision = ~std::uint64_t{0}; }

private:
    struct Data {
        int                          upgradeSlot = -1;
        int                          columns     = 9;
        std::vector<mc::CapturedMob> mobs;
    };

    Data const& data(StorageOverlayContext const& ctx) const {
        auto rev = net::screenRevision();
        if (rev == mRevision && ctx.model.session == mSession) return mData;
        mRevision = rev;
        mSession  = ctx.model.session;
        mData     = {};
        if (ctx.model.kind != "backpack") return mData;
        for (std::size_t i = 0; i < ctx.model.upgrades.size(); ++i) {
            auto const& tab = ctx.model.upgrades[i].tab;
            auto        it  = tab.find("mobCatcher");
            if (it == tab.end() || !it->is_object()) continue;
            mData.upgradeSlot = static_cast<int>(i);
            mData.columns     = it->value("columns", mc::columnsFor(ctx.model.storageSlots));
            mData.mobs        = mc::parseMobs(it->value("mobs", nlohmann::json::array()));
            break;
        }
        cache().keepOnly(mData.mobs);
        return mData;
    }

    /// SB getCapturedMobRenderBounds: from the first shown slot of the footprint (item positions).
    std::optional<Rect> renderBounds(StorageOverlayContext const& ctx, mc::CapturedMob const& mob) const {
        auto const& d = data(ctx);
        for (int dy = 0; dy < mob.height; ++dy) {
            for (int dx = 0; dx < mob.width; ++dx) {
                int index = mob.slot + dy * d.columns + dx;
                if (index >= ctx.model.storageSlots) continue;
                if (auto r = ctx.slotItemRect(index)) {
                    return Rect{r->x - dx * 18.f, r->y - dy * 18.f, mob.width * 18.f, mob.height * 18.f};
                }
            }
        }
        return std::nullopt;
    }

    /// SB isMouseOverVisibleCapturedMob: over one of the footprint's shown 18x18 slot areas.
    mc::CapturedMob const* hoveredMob(StorageOverlayContext const& ctx, float mx, float my) const {
        auto const& d = data(ctx);
        for (auto const& mob : d.mobs) {
            for (int dy = 0; dy < mob.height; ++dy) {
                for (int dx = 0; dx < mob.width; ++dx) {
                    int index = mob.slot + dy * d.columns + dx;
                    if (index >= ctx.model.storageSlots) continue;
                    auto r = ctx.slotItemRect(index);
                    if (r && Rect{r->x - 1, r->y - 1, 18, 18}.contains(mx, my)) return &mob;
                }
            }
        }
        return nullptr;
    }

    /// The storage panel area (SC's scroll panel scissor): shown slots plus their frames.
    static Rect clipRect(StorageOverlayContext const& ctx) {
        auto v = ctx.visibleStorage;
        return v.empty() ? v : Rect{v.x - 1, v.y - 1, v.w + 2, v.h + 2};
    }

    static void fillClipped(Painter& p, Rect r, Rect const& clip, Color c) {
        float l = std::max(r.x, clip.x), t = std::max(r.y, clip.y);
        float rr = std::min(r.right(), clip.right()), b = std::min(r.bottom(), clip.bottom());
        if (rr > l && b > t) p.fillRect({l, t, rr - l, b - t}, c);
    }

    /// SB renderCapturedMobArea: SC's tiled control background (a recessed frame: dark top/left,
    /// white bottom/right) under a 0xFF2B2B2B fill with up to 5 lighter inset layers.
    static void drawArea(Painter& p, Rect const& b, mc::CapturedMob const& mob, Rect const& clip) {
        float bx = b.x - 1, by = b.y - 1;
        float w = mob.width * 18.f, h = mob.height * 18.f;
        fillClipped(p, {bx, by, w, h}, clip, sb::color::SLOT);
        fillClipped(p, {bx, by, w - 1, 1}, clip, sb::color::SLOT_DARK);
        fillClipped(p, {bx, by, 1, h - 1}, clip, sb::color::SLOT_DARK);
        fillClipped(p, {bx + 1, by + h - 1, w - 1, 1}, clip, sb::color::PANEL_LITE);
        fillClipped(p, {bx + w - 1, by + 1, 1, h - 1}, clip, sb::color::PANEL_LITE);
        float x = bx + 1, y = by + 1, iw = w - 2, ih = h - 2;
        fillClipped(p, {x, y, iw, ih}, clip, Color::argb(kBackgroundColor));
        int layers = std::max(1, std::min(5, static_cast<int>(std::min(iw, ih)) / 5));
        for (int layer = 0; layer < layers; ++layer) {
            float insetX = 1 + std::floor(layer * iw / (layers * 3));
            float insetY = 1 + std::floor(layer * ih / (layers * 3));
            int   alpha  = 24 + layer * 12;
            fillClipped(p, {x + insetX, y + insetY, iw - 2 * insetX, ih - 2 * insetY}, clip, Color::argb(static_cast<std::uint32_t>(alpha) << 24 | 0x4A4A4Au));
        }
    }

    /// SB prepareEntityForRender: a per-mob body turn and a head that looks around now and then.
    static EntityPose poseOf(mc::CapturedMob const& mob) {
        auto  hash  = mc::javaUuidHash(mob.id);
        float time  = static_cast<float>(seconds() * 20.0) + mc::uuidFloat(hash, 3) * 200.f;
        float body  = (mc::uuidFloat(hash, 0) - 0.5f) * kBodyYawRange;
        float head  = -kHeadStaticYawRange / 2.f + mc::uuidFloat(hash, 1) * kHeadStaticYawRange
                   + mc::idlePoseOffset(hash, time, 0, kHeadIdleYawAmplitude);
        float pitch = -kHeadStaticPitchRange / 2.f + mc::uuidFloat(hash, 2) * kHeadStaticPitchRange
                    + mc::idlePoseOffset(hash, time, 1, kHeadIdlePitchAmplitude);
        return {body, body + head, pitch};
    }

    // ---- hearts (SB ClientMobCatcherHealthTooltip) ----------------------------------------------------

    static void heart(Painter& p, float x, float y, char const* path) {
        p.texture(path, {x, y, static_cast<float>(mc::kHeartSize), static_cast<float>(mc::kHeartSize)}, 1.f, Painter::Layer::Foreground);
    }

    static float heartsWidth(Painter& p, mc::HeartRow const& r) {
        constexpr float H = mc::kHeartSize;
        if (!r.compact) return r.inlineHearts * H;
        float w = 0;
        if (r.full > 0) w += p.textWidth(std::to_string(r.full) + "x") + 2 + H + 3;
        if (r.half) w += H + 3;
        if (r.empty > 0) w += p.textWidth(std::to_string(r.empty) + "x") + 2 + H;
        return w;
    }

    static void drawHearts(Painter& p, mc::HeartRow const& r, float x, float y) {
        constexpr float H         = mc::kHeartSize;
        constexpr char const* kEmpty = "textures/ui/heart_background";
        constexpr char const* kFull  = "textures/ui/heart";
        constexpr char const* kHalf  = "textures/ui/heart_half";
        if (!r.compact) {
            for (int i = 0; i < r.inlineHearts; ++i) {
                heart(p, x + i * H, y, kEmpty);
                int kind = mc::inlineHeart(r, i);
                if (kind == 2) heart(p, x + i * H, y, kFull);
                else if (kind == 1) heart(p, x + i * H, y, kHalf);
            }
            return;
        }
        float next = x;
        if (r.full > 0) {
            auto s = std::to_string(r.full) + "x";
            p.text(s, next, y + 1, Color::rgb(0xFFFFFF), false);
            next += p.textWidth(s) + 2;
            heart(p, next, y, kFull);
            next += H + 3;
        }
        if (r.half) {
            heart(p, next, y, kEmpty);
            heart(p, next, y, kHalf);
            next += H + 3;
        }
        if (r.empty > 0) {
            auto s = std::to_string(r.empty) + "x";
            p.text(s, next, y + 1, Color::rgb(0xAAAAAA), false);
            next += p.textWidth(s) + 2;
            heart(p, next, y, kEmpty);
        }
    }

    mutable std::uint64_t mRevision = ~std::uint64_t{0};
    mutable std::uint32_t mSession  = 0;
    mutable Data          mData;
};

ABP_REGISTER_STORAGE_OVERLAY([] { return std::make_unique<MobCatcherOverlay>(); });

// ---- muffled ambient sounds of carried captured mobs ----------------------------------------------------

/// The ambient sound name of an entity type from the client's sound definitions (entity_sounds).
std::string ambientSoundOf(std::string const& type) {
    auto* client = currentClient();
    if (!client) return {};
    try {
        auto  engine = client->getSoundEngine();
        auto& map    = *engine->mSounds->mEntitySounds;
        std::string shortName = type.starts_with("minecraft:") ? type.substr(10) : type;
        for (auto const& key : {type, shortName}) {
            auto it = map.find(key);
            if (it == map.end()) continue;
            auto const& base = *it->second.mBaseSounds;
            auto        s    = base.find(SharedTypes::Legacy::LevelSoundEvent::Ambient);
            if (s != base.end() && !s->second.mName->empty()) return *s->second.mName;
        }
    } catch (...) {
    }
    return {};
}

class MobCatcherClientModule {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mTick     = bus.emplaceListener<ll::event::ClientLevelTickEvent>([this](auto&) { tick(); });
        mExit     = bus.emplaceListener<ll::event::ClientExitLevelEvent>([this](auto&) {
            cache().clear();
            mNext.clear();
        });
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        if (mTick) bus.removeListener(mTick);
        if (mExit) bus.removeListener(mExit);
        mTick.reset();
        mExit.reset();
        cache().clear();
    }

private:
    static constexpr std::int64_t kUnset = INT64_MIN;

    /// SB MobCatcherUpgradeWrapper.clientTick, for every backpack the local player carries.
    void tick() {
        ++mTicks;
        auto* player = currentPlayer();
        if (!player) return;
        std::unordered_set<std::string> seen;
        auto visit = [&](ItemStack const& stack) {
            if (stack.isNull() || !game::isBackpack(stack)) return;
            auto uuid = game::backpackUuid(stack);
            if (!uuid || !seen.insert(*uuid).second) return;
            auto const* info = net::renderInfo(*uuid);
            auto        mobs = info ? info->extra.find("mobCatcher") : nlohmann::json::const_iterator{};
            bool        any  = info && mobs != info->extra.end() && mobs->is_array() && !mobs->empty();
            auto&       next = mNext.try_emplace(*uuid, kUnset).first->second;
            if (!any) {
                next = kUnset;
                return;
            }
            if (next == kUnset) {
                next = mTicks + 40 + randomInt(120);
                return;
            }
            if (mTicks < next) return;
            next            = mTicks + mc::kMinAmbientSoundDelay + randomInt(mc::kAmbientSoundDelayVariation);
            auto const& mob = (*mobs)[static_cast<std::size_t>(randomInt(static_cast<int>(mobs->size())))];
            auto sound = ambientSoundOf(mob.value("entityType", std::string{}));
            if (sound.empty()) return;
            float volume = mc::kMuffledAmbientVolume + randomFloat() * mc::kMuffledAmbientVolumeVariation;
            float pitch  = mc::kMuffledAmbientPitch + (randomFloat() - 0.5f) * mc::kMuffledAmbientPitchVariation;
            currentClient()->playUI(sound, volume, pitch);
            if (!mLoggedAmbient) {
                mLoggedAmbient = true;
                log().info("mob catcher: playing muffled ambient sounds of captured mobs ({})", sound);
            }
        };
        auto& inv = player->getInventory();
        for (int i = 0; i < inv.getContainerSize(); ++i) visit(inv.getItem(i));
        visit(player->getOffhandSlot());
        visit(player->getArmor(SharedTypes::Legacy::ArmorSlot::Torso));
    }

    ll::event::ListenerPtr                        mTick;
    ll::event::ListenerPtr                        mExit;
    std::int64_t                                  mTicks = 0;
    bool                                          mLoggedAmbient = false;
    std::unordered_map<std::string, std::int64_t> mNext;
};

} // namespace

} // namespace abp::client::ui

ABP_REGISTER_MODULE(abp::client::ui::MobCatcherClientModule);

#endif // ABP_CLIENT
