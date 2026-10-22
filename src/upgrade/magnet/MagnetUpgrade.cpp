// Magnet upgrade (basic + advanced): pulls nearby item entities into the backpack and turns XP orbs
// into liquid experience in the backpack's tanks. See Magnet.h.

#include "upgrade/magnet/Magnet.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <random>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/server/ServerStartedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/item/ExperienceOrb.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Tick.h"
#include "mc/world/phys/AABB.h"

#include "api/BackpackCapabilities.h"
#include "api/abp/Xp.h"
#include "feature/gui/Gui.h"
#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/FilterMenu.h"
#include "ui/menu/MenuManager.h"
#include "net/NativeScreens.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/filter/FilterCommon.h"
#include "upgrade/magnet/MagnetInternal.h"

ABP_REGISTER_CONFIG("magnet", abp::upgrade::magnet::MagnetConfig);

namespace abp::upgrade::magnet {

// ---- config / settings -----------------------------------------------------------------------

MagnetTierConfig tierConfig(bool advanced) {
    auto const& cfg = config::get<MagnetConfig>();
    auto        c   = advanced ? cfg.advancedMagnetUpgrade : cfg.magnetUpgrade;
    c.filterSlots   = std::clamp(c.filterSlots, 1, 20);
    c.slotsInRow    = std::clamp(c.slotsInRow, 1, 6);
    c.magnetRange   = std::clamp(c.magnetRange, 1, 20);
    return c;
}

Settings Settings::read(nlohmann::json const& settings, int filterSlots, bool advanced) {
    Settings s;
    // SC ContentsFilterLogic: a block list by default (an empty one matches everything).
    s.filter = filtering::read(settings, filterSlots, {}, "magnet");
    filtering::normalize(s.filter, advanced ? UpgradeTier::Advanced : UpgradeTier::Basic, filterSlots);
    if (!settings.is_object()) return s;
    if (auto it = settings.find("pickupItems"); it != settings.end() && it->is_boolean()) s.pickupItems = it->get<bool>();
    if (auto it = settings.find("pickupXp"); it != settings.end() && it->is_boolean()) s.pickupXp = it->get<bool>();
    return s;
}

filtering::FilterSpec filterSpec(bool advanced) {
    auto tc = tierConfig(advanced);
    return filtering::FilterSpec{tc.filterSlots, tc.slotsInRow, advanced, filtering::TypeButton::Contents};
}

bool matchesFilter(Settings const& s, game::Backpack& backpack, core::ItemStackData const& item) {
    return filtering::matchesContents(s.filter, backpack, item);
}

// ---- XP consumers (other units) ----------------------------------------------------------------

namespace {
struct ConsumerEntry {
    std::string owner;
    int         priority;
    XpConsumer  fn;
};
std::vector<ConsumerEntry>& consumers() {
    static std::vector<ConsumerEntry> v;
    return v;
}
} // namespace

void addXpConsumer(std::string owner, int priority, XpConsumer fn) {
    auto& v  = consumers();
    auto  it = std::upper_bound(v.begin(), v.end(), priority, [](int p, ConsumerEntry const& e) { return p < e.priority; });
    v.insert(it, ConsumerEntry{std::move(owner), priority, std::move(fn)});
}

void removeXpConsumers(std::string const& owner) {
    std::erase_if(consumers(), [&](ConsumerEntry const& e) { return e.owner == owner; });
}

int offerXp(UpgradeContext const& magnet, Player* carrier, int amount) {
    auto list = consumers(); // a consumer may (un)register consumers
    for (auto const& c : list) {
        if (amount <= 0) break;
        try {
            amount -= std::clamp(c.fn(magnet, carrier, amount), 0, amount);
        } catch (std::exception const& e) {
            log().error("magnet XP consumer {} failed: {}", c.owner, e.what());
        }
    }
    return amount;
}

// ---- world helpers ---------------------------------------------------------------------------

int orbValue(ExperienceOrb const& orb) {
    CompoundTag tag;
    orb.addAdditionalSaveData(tag);
    for (auto key : {"experience value", "experienceValue", "Value"}) {
        if (tag.contains(key) && tag[key].is_number_integer()) return static_cast<int>(tag[key]);
    }
    return 0;
}

AABB rangeBox(BlockPos const& pos, int range) {
    auto r = static_cast<float>(range);
    return AABB{
        Vec3{static_cast<float>(pos.x) - r, static_cast<float>(pos.y) - r, static_cast<float>(pos.z) - r},
        Vec3{static_cast<float>(pos.x) + 1 + r, static_cast<float>(pos.y) + 1 + r, static_cast<float>(pos.z) + 1 + r}
    };
}

BlockPos carrierPos(Player& player) {
    auto f = player.getFeetPos();
    return BlockPos{static_cast<int>(std::floor(f.x)), static_cast<int>(std::floor(f.y)), static_cast<int>(std::floor(f.z))};
}

bool isBlockedByTags(Actor& entity, bool carried) {
    bool prevent = entity.hasTag(kPreventRemoteMovement);
    return carried ? prevent : prevent && !entity.hasTag(kAllowMachineMovement);
}

bool isItemPullable(ItemActor& item, bool carried) {
    if (item.mRemoved || !item.isAlive() || item.item().isNull()) return false;
    // Java's ItemEntity.INFINITE_PICKUP_DELAY (32767); Bedrock has no such value, so treat the
    // same magnitude as "never".
    if (item.pickupDelay() >= 32767) return false;
    return !isBlockedByTags(item, carried);
}

bool canFillStorageWithXp(std::shared_ptr<game::Backpack> const& backpack) {
    auto handler = api::backpack::fluidHandler(backpack);
    return handler && handler->fill(api::FluidStack{std::string(api::fluids::Experience), 1, {}}, true) > 0;
}

namespace {

std::minstd_rand& rng() {
    static std::minstd_rand r{std::random_device{}()};
    return r;
}
float nextFloat() { return std::uniform_real_distribution<float>(0.f, 1.f)(rng()); }

/// `level.playSound(null, player.x/y/z, sound, PLAYERS, volume, pitch)`: heard by the players
/// around the carrier.
void playSound(Player& player, char const* sound, float volume, float pitch) {
    auto at  = player.getPosition();
    auto dim = player.getDimensionId();
    player.getLevel().forEachPlayer([&](Player& p) {
        if (p.getDimensionId() != dim) return true;
        auto d = p.getPosition() - at;
        if (d.x * d.x + d.y * d.y + d.z * d.z > 16.f * 16.f) return true;
        auto  pkt = MinecraftPackets::createPacket(MinecraftPacketIds::PlaySound);
        auto& s   = static_cast<PlaySoundPacket&>(*pkt);
        s.mName   = sound;
        s.mPos    = BlockPos{
            static_cast<int>(std::floor(at.x * 8.0f)),
            static_cast<int>(std::floor(at.y * 8.0f)),
            static_cast<int>(std::floor(at.z * 8.0f))
        };
        s.mVolume                   = volume;
        s.mPitch                    = pitch;
        s.mLoopCount                = 0;
        s.mBypassListenerRangeCheck = false;
        s.mServerSoundHandle->reset();
        s.mPlaybackPositionSeconds->reset();
        s.sendTo(p);
        return true;
    });
}

void playItemPickupSound(Player& player) {
    playSound(player, "random.pop", 0.2f, (nextFloat() - nextFloat()) * 1.4f + 2.0f);
}
void playXpPickupSound(Player& player) {
    playSound(player, "random.orb", 0.1f, (nextFloat() - nextFloat()) * 0.35f + 0.9f);
}

/// SC `tryToInsertItem`. True if (part of) the stack went in.
bool tryToInsertItem(game::Backpack& bp, Player* player, ItemActor& entity) {
    auto& stack = entity.item();
    auto  data  = game::ItemConv::toData(stack);
    if (data.empty()) return false;
    auto r     = bp.inventory().insert(data, core::AccessContext{core::Source::Pickup, player});
    int  taken = r.inserted + r.voided;
    if (taken <= 0) return false;
    if (taken >= data.count) {
        entity.remove(); // setItem(EMPTY) discards the entity
    } else {
        stack.set(data.count - taken);
    }
    return true;
}

int pickupItems(UpgradeContext const& ctx, Settings const& s, Player* player, BlockSource& region, BlockPos const& pos, int range) {
    auto actors = region.getEntities(rangeBox(pos, range), 2.0f, ActorType::ItemEntity);
    std::vector<ItemActor*> items;
    for (auto* a : actors) {
        if (a && a->isType(ActorType::ItemEntity)) items.push_back(static_cast<ItemActor*>(a));
    }
    if (items.empty()) return kCooldownTicks;
    auto& bp       = ctx.backpack();
    int   cooldown = kFullCooldownTicks;
    for (auto* item : items) {
        if (!isItemPullable(*item, player != nullptr)) continue;
        if (!matchesFilter(s, bp, game::ItemConv::toData(item->item()))) continue;
        if (tryToInsertItem(bp, player, *item)) {
            if (player) playItemPickupSound(*player);
            cooldown = kCooldownTicks;
        }
    }
    return cooldown;
}

/// SC `tryToFillTank`.
bool tryToFillTank(UpgradeContext const& ctx, ExperienceOrb& orb, Player* player, BlockSource& region) {
    auto handler = api::backpack::fluidHandler(ctx.backpackPtr());
    if (!handler) return false;
    int amount = api::xp::experienceToLiquid(static_cast<float>(orbValue(orb)));
    if (amount <= 0) return false;
    int added = handler->fill(api::FluidStack{std::string(api::fluids::Experience), amount, {}}, false);
    if (added <= 0) return false;
    auto pos = orb.getPosition();
    orb.remove();
    if (player) playXpPickupSound(*player);
    if (amount > added) {
        int rest = static_cast<int>(api::xp::liquidToExperience(amount - added));
        if (rest > 0) ExperienceOrb::_spawnOrb(region, region.getLevel(), pos, rest);
    }
    return true;
}

int pickupXpOrbs(UpgradeContext const& ctx, Player* player, BlockSource& region, BlockPos const& pos, int range) {
    auto actors = region.getEntities(rangeBox(pos, range), 2.0f, ActorType::Experience);
    std::vector<ExperienceOrb*> orbs;
    for (auto* a : actors) {
        if (a && a->isType(ActorType::Experience)) orbs.push_back(static_cast<ExperienceOrb*>(a));
    }
    if (orbs.empty()) return kCooldownTicks;
    int cooldown = kCooldownTicks;
    for (auto* orb : orbs) {
        if (orb->mRemoved || !orb->isAlive() || isBlockedByTags(*orb, player != nullptr)) continue;
        if (!tryToFillTank(ctx, *orb, player, region)) {
            cooldown = kFullCooldownTicks;
            break;
        }
    }
    return cooldown;
}

} // namespace

int runMagnet(UpgradeContext const& ctx, Player* player, BlockSource& region, BlockPos const& pos) {
    auto* type = upgradeFor(ctx.state().item);
    bool  adv  = type && type->info().id == kAdvancedId;
    auto  tc   = tierConfig(adv);
    auto  s    = Settings::read(ctx.settings(), tc.filterSlots, adv);
    int   cooldown = s.pickupItems ? pickupItems(ctx, s, player, region, pos, tc.magnetRange) : kFullCooldownTicks;
    if (s.pickupXp && canFillStorageWithXp(ctx.backpackPtr())) {
        cooldown = std::min(cooldown, pickupXpOrbs(ctx, player, region, pos, tc.magnetRange));
    }
    return cooldown;
}

// ---- shared clock (SC MagnetUpgradeWrapper.globalPostTick) ---------------------------------------

namespace {
std::int64_t gNextTickTime = std::numeric_limits<std::int64_t>::min();
} // namespace

bool advanceSharedClock(std::uint64_t gameTime) {
    auto now = static_cast<std::int64_t>(gameTime);
    if (now > gNextTickTime) gNextTickTime = now + kCooldownTicks;
    return gNextTickTime <= now; // isInCooldown: nextTickTime > gameTime
}

void resetSharedClock() { gNextTickTime = std::numeric_limits<std::int64_t>::min(); }

namespace {

bool isMagnet(IUpgrade const& u) { return u.info().id == kBasicId || u.info().id == kAdvancedId; }

/// Runs the magnets of every carried backpack when the shared clock fires.
class MagnetClock {
public:
    void install() {
        resetSharedClock();
        auto& bus = ll::event::EventBus::getInstance();
        mTick     = bus.emplaceListener<ll::event::ServerLevelTickEvent>([](ll::event::ServerLevelTickEvent& ev) { tick(ev.level()); });
        mStarted  = bus.emplaceListener<ll::event::ServerStartedEvent>([](auto&) { resetSharedClock(); });
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        bus.removeListener(mTick);
        bus.removeListener(mStarted);
    }

    static void tick(Level& level) {
        if (!advanceSharedClock(static_cast<std::uint64_t>(level.getCurrentServerTick().tickID))) return;
        level.forEachPlayer([&](Player& player) {
            if (player.isClientSide() || !player.isAlive()) return true;
            for (auto& [carried, bp] : game::carriedBackpacks(player, false, true)) {
                forEachInstalled(bp, true, [&](IUpgrade& u, UpgradeContext const& ctx) {
                    if (!isMagnet(u) || !carried.upgradeActive(u.info().id, u.info().groupOrId())) return;
                    try {
                        (void)runMagnet(ctx.withPlayer(&player), &player, player.getDimensionBlockSource(), carrierPos(player));
                    } catch (std::exception const& e) {
                        log().error("magnet failed: {}", e.what());
                    }
                });
            }
            return true;
        });
    }

private:
    ll::event::ListenerPtr mTick;
    ll::event::ListenerPtr mStarted;
};

class MagnetUpgrade : public IUpgrade {
public:
    explicit MagnetUpgrade(bool advanced) : mAdvanced(advanced) {
        mInfo.id             = std::string(advanced ? kAdvancedId : kBasicId);
        mInfo.itemId         = "alleviatedbackpacks:" + mInfo.id + "_upgrade";
        mInfo.nameKey        = "abp.magnet." + mInfo.id + ".name";
        mInfo.nameFallback   = advanced ? "Advanced Magnet Upgrade" : "Magnet Upgrade";
        mInfo.tier           = advanced ? UpgradeTier::Advanced : UpgradeTier::Basic;
        mInfo.group          = "magnet";
        mInfo.maxPerBackpack = 9; // SB: no limit
        mInfo.hasSettings    = true;
        mInfo.toggleable     = true;
    }

    [[nodiscard]] UpgradeInfo const& info() const override { return mInfo; }
    [[nodiscard]] Settings settings(UpgradeContext const& ctx) const {
        return Settings::read(ctx.settings(), tierConfig(mAdvanced).filterSlots, mAdvanced);
    }

    // SC IPickupResponseUpgrade.pickup: walking over items works like a Pickup upgrade.
    void onPickup(UpgradeContext const& ctx, core::ItemStackData& item) override {
        auto s = settings(ctx);
        if (!s.pickupItems || !matchesFilter(s, ctx.backpack(), item)) return;
        auto r      = ctx.backpack().inventory().insert(item, core::AccessContext{core::Source::Pickup, ctx.player()});
        item.count -= r.inserted + r.voided;
    }

    // Carried magnets run on the shared clock (MagnetClock), not on the upgrade ticker.

    void onPlacedTick(PlacedTickContext const& tick) override {
        auto& state = tick.ctx.transient<PlacedState>();
        auto  now   = static_cast<std::int64_t>(tick.region.getLevel().getCurrentServerTick().tickID);
        if (state.cooldownUntil > now) return; // UpgradeWrapperBase.isInCooldown
        int cooldown        = runMagnet(tick.ctx, nullptr, tick.region, BlockPos{tick.x, tick.y, tick.z});
        state.cooldownUntil = now + cooldown;
    }

    std::shared_ptr<ui::Menu> createSettingsMenu(Player& player, UpgradeContext const& ctx) override {
        return std::make_shared<MagnetMenu>(player, ctx, tierConfig(mAdvanced).filterSlots, mAdvanced);
    }

    [[nodiscard]] std::vector<std::string> describe(Player& player, UpgradeContext const& ctx) const override {
        auto s = settings(ctx);
        return {
            s.pickupItems ? trFor(player, "abp.magnet.button.pickup_items", "Pickup Items")
                          : trFor(player, "abp.magnet.button.do_not_pickup_items", "Do Not Pickup Items"),
            s.pickupXp ? trFor(player, "abp.magnet.button.pickup_xp", "Pickup Experience")
                       : trFor(player, "abp.magnet.button.do_not_pickup_xp", "Do Not Pickup Experience"),
        };
    }

private:
    struct PlacedState {
        std::int64_t cooldownUntil = 0;
    };

    UpgradeInfo mInfo;
    bool        mAdvanced;
};

} // namespace

// ---- settings menu (chest fallback) ------------------------------------------------------------

namespace {
core::Filter menuFilter(UpgradeContext const& ctx, int slots, bool advanced) {
    return Settings::read(ctx.settings(), slots, advanced).filter;
}
} // namespace

MagnetMenu::MagnetMenu(Player& player, UpgradeContext const& ctx, int slots, bool advanced)
: ui::FilterMenu(
      [&] {
          auto* type = upgradeFor(ctx.state().item);
          return type ? displayName(*type, &player) : trFor(player, "abp.filter.title", "Filter");
      }(),
      [ctx, slots, advanced] { return menuFilter(ctx, slots, advanced); },
      [ctx](core::Filter const& f) {
          auto& j = ctx.settings();
          if (!j.is_object()) j = nlohmann::json::object();
          j["filter"] = f.toJson();
          ctx.saveSettings(true);
      },
      ui::FilterMenuOptions{
          .slots           = slots,
          .allowModeToggle = true,
          .allowMatchBy    = advanced,
          .allowNbt        = advanced,
          .allowDurability = advanced,
          .onBack =
              [bp = ctx.backpackPtr()](Player& p) {
                  ui::BackpackMenu::openBackpack(p, bp, {}, ui::BackpackMenu::View::Upgrades);
              },
      }
  ),
  mCtx(ctx) {}

void MagnetMenu::build(Player& player) {
    FilterMenu::build(player);
    int  control = size() - 9;
    auto s       = Settings::read(mCtx.settings(), 1, false);
    auto toggle  = [this](char const* key) {
        return [this, key](ui::ClickContext const&) {
            auto& j = mCtx.settings();
            if (!j.is_object()) j = nlohmann::json::object();
            bool v = j.contains(key) && j[key].is_boolean() ? j[key].get<bool>() : true;
            j[key] = !v;
            mCtx.saveSettings();
            invalidate();
        };
    };
    setSlot(
        control + kItemsButton,
        std::make_shared<ui::ButtonSlot>(
            ui::icon(
                "minecraft:hopper",
                s.pickupItems ? trFor(player, "abp.magnet.button.pickup_items", "Pickup Items")
                              : trFor(player, "abp.magnet.button.do_not_pickup_items", "Do Not Pickup Items")
            ),
            toggle("pickupItems")
        )
    );
    setSlot(
        control + kXpButton,
        std::make_shared<ui::ButtonSlot>(
            ui::icon(
                "minecraft:experience_bottle",
                s.pickupXp ? trFor(player, "abp.magnet.button.pickup_xp", "Pickup Experience")
                           : trFor(player, "abp.magnet.button.do_not_pickup_xp", "Do Not Pickup Experience"),
                s.pickupXp ? std::vector<std::string>{"§8§o" + trFor(player, "abp.magnet.button.pickup_xp.detail", "Requires Tank Upgrade")}
                           : std::vector<std::string>{}
            ),
            toggle("pickupXp")
        )
    );
}

// ---- native tab (client: client/ui/tabs/magnet) -----------------------------------------------

namespace {

bool isAdvanced(UpgradeContext const& ctx) {
    auto* type = upgradeFor(ctx.state().item);
    return type && type->info().id == kAdvancedId;
}

void toggle(UpgradeContext const& ctx, char const* key) {
    auto& j = ctx.settings();
    if (!j.is_object()) j = nlohmann::json::object();
    bool v = j.contains(key) && j[key].is_boolean() ? j[key].get<bool>() : true;
    j[key] = !v;
    ctx.saveSettings();
}

ABP_REGISTER_TAB_DATA(net::TabDataProvider{
    "magnet",
    [](Player& player, UpgradeContext const& ctx) {
        bool adv = isAdvanced(ctx);
        auto s   = Settings::read(ctx.settings(), tierConfig(adv).filterSlots, adv);
        return nlohmann::json{
            {"title",       adv ? trFor(player, "abp.magnet.tab.advanced", "Adv. Magnet") : trFor(player, "abp.magnet.tab", "Magnet")},
            {"tooltip",     adv ? trFor(player, "abp.magnet.tab.advanced.tooltip", "Advanced Magnet Settings")
                                : trFor(player, "abp.magnet.tab.tooltip", "Magnet Settings")},
            {"pickupItems", s.pickupItems},
            {"pickupXp",    s.pickupXp},
            {"filter",      filtering::tabJson(s.filter, filterSpec(adv))},
        };
    },
});

ABP_REGISTER_TAB_ACTION(net::TabActionHandler{
    "magnet",
    [](Player& player, UpgradeContext const& ctx, nlohmann::json const& data) {
        if (!data.is_object()) return false;
        bool adv = isAdvanced(ctx);
        if (data.value("filter", std::string{}) == "filter") {
            auto spec = filterSpec(adv);
            auto s    = Settings::read(ctx.settings(), spec.slots, adv);
            if (filtering::applyAction(s.filter, spec, data, filtering::cursorOf(player))) {
                filtering::normalize(s.filter, adv ? UpgradeTier::Advanced : UpgradeTier::Basic, spec.slots);
                ctx.settings()["filter"] = s.filter.toJson();
                ctx.saveSettings(true);
            }
            return true;
        }
        auto op = data.value("op", std::string{});
        if (op == "pickupItems" || op == "pickupXp") {
            toggle(ctx, op == "pickupItems" ? "pickupItems" : "pickupXp");
            return true;
        }
        return false;
    },
});

} // namespace

namespace {
struct BasicMagnet : MagnetUpgrade {
    BasicMagnet() : MagnetUpgrade(false) {}
};
struct AdvancedMagnet : MagnetUpgrade {
    AdvancedMagnet() : MagnetUpgrade(true) {}
};
} // namespace

} // namespace abp::upgrade::magnet

ABP_REGISTER_UPGRADE(abp::upgrade::magnet::BasicMagnet);
ABP_REGISTER_UPGRADE(abp::upgrade::magnet::AdvancedMagnet);
ABP_REGISTER_MODULE(abp::upgrade::magnet::MagnetClock);
