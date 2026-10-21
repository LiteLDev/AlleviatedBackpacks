// Magnet upgrade (basic + advanced): pulls nearby item entities into the backpack and XP orbs to
// the carrier. See Magnet.h.

#include "upgrade/magnet/Magnet.h"

#include <algorithm>
#include <exception>
#include <vector>

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/TakeItemActorPacket.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/item/ExperienceOrb.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/phys/AABB.h"

#include "game/ItemConv.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/FilterMenu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/magnet/MagnetInternal.h"

ABP_REGISTER_CONFIG("magnet", abp::upgrade::magnet::MagnetConfig);

namespace abp::upgrade::magnet {

// ---- settings --------------------------------------------------------------------------------

Settings Settings::read(nlohmann::json const& settings, int filterSlots) {
    Settings s;
    s.filter = core::Filter(filterSlots);
    if (!settings.is_object()) return s;
    if (auto it = settings.find("pickupItems"); it != settings.end() && it->is_boolean()) s.pickupItems = it->get<bool>();
    if (auto it = settings.find("pickupXp"); it != settings.end() && it->is_boolean()) s.pickupXp = it->get<bool>();
    if (auto it = settings.find("filter"); it != settings.end() && it->is_object()) {
        s.filter = core::Filter::fromJson(*it, filterSlots);
    }
    return s;
}

// ---- XP consumers ----------------------------------------------------------------------------

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
    // Copy: a consumer may (un)register consumers.
    auto list = consumers();
    for (auto const& c : list) {
        if (amount <= 0) break;
        try {
            int took = std::clamp(c.fn(magnet, carrier, amount), 0, amount);
            amount  -= took;
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

AABB rangeBox(Player& player, int range) {
    auto const& b = player.getAABB();
    auto        r = static_cast<float>(range);
    return AABB{Vec3{b.min.x - r, b.min.y - r, b.min.z - r}, Vec3{b.max.x + r, b.max.y + r, b.max.z + r}};
}

bool isItemPullable(ItemActor& item, Player& carrier, MagnetConfig const& cfg) {
    if (item.mRemoved || !item.isAlive() || item.isInItemFrame()) return false;
    if (item.item().isNull()) return false;
    if (item.pickupDelay() > 0) return false;
    if (cfg.ownDropDelayTicks > 0 && item.age() < cfg.ownDropDelayTicks) {
        if (item.getSourceUniqueID() == carrier.getOrCreateUniqueID()) return false;
    }
    return true;
}

namespace {

/// Shows the vanilla "item flies into the player" animation and plays the pickup pop.
void showPickup(Player& player, Actor& item) {
    auto  pkt = MinecraftPackets::createPacket(MinecraftPacketIds::TakeItemActor);
    auto& t   = static_cast<TakeItemActorPacket&>(*pkt);
    t.mItemId = item.getRuntimeID();
    t.mActorId = player.getRuntimeID();
    t.sendTo(item);
    player.getLevel().playSound(SharedTypes::Legacy::LevelSoundEvent::Pop, player.getPosition(), 0.2f, 1.6f);
}

class MagnetUpgrade : public IUpgrade {
public:
    explicit MagnetUpgrade(bool advanced) : mAdvanced(advanced) {
        mInfo.id           = std::string(advanced ? kAdvancedId : kBasicId);
        mInfo.itemId       = "alleviatedbackpacks:" + mInfo.id + "_upgrade";
        mInfo.nameKey      = "abp.magnet." + mInfo.id + ".name";
        mInfo.nameFallback = advanced ? "Advanced Magnet Upgrade" : "Magnet Upgrade";
        mInfo.tier         = advanced ? UpgradeTier::Advanced : UpgradeTier::Basic;
        mInfo.group        = "magnet";
        mInfo.maxPerBackpack = 1;
        mInfo.hasSettings  = true;
        mInfo.toggleable   = true;
    }

    [[nodiscard]] UpgradeInfo const& info() const override { return mInfo; }
    [[nodiscard]] int                slots() const { return mAdvanced ? kAdvancedSlots : kBasicSlots; }
    [[nodiscard]] int                range() const {
        auto const& cfg = config::get<MagnetConfig>();
        return std::max(0, mAdvanced ? cfg.advancedRange : cfg.basicRange);
    }
    [[nodiscard]] Settings settings(UpgradeContext const& ctx) const { return Settings::read(ctx.settings(), slots()); }

    void onInstall(UpgradeContext const& ctx) override {
        auto& s = ctx.settings();
        if (!s.is_object()) s = nlohmann::json::object();
        if (!s.contains("pickupItems")) s["pickupItems"] = true;
        if (!s.contains("pickupXp")) s["pickupXp"] = true;
        ctx.saveSettings();
    }

    // Walking over items: the magnet works like a Pickup upgrade with the same filter.
    void onPickup(UpgradeContext const& ctx, core::ItemStackData& item) override {
        auto s = settings(ctx);
        if (!s.pickupItems || !s.filter.passes(item, &game::ItemConv::tags())) return;
        auto r      = ctx.backpack().inventory().insert(item, core::AccessContext{core::Source::Pickup, ctx.player()});
        item.count -= r.inserted + r.voided;
    }

    void onTick(TickContext const& tick) override {
        auto& player = tick.player;
        if (player.getPlayerGameType() == GameType::Spectator) return;
        auto& state  = tick.ctx.transient<TickState>();
        state.cooldown -= std::max(1, tick.interval);
        if (state.cooldown > 0) return;
        auto const& cfg = config::get<MagnetConfig>();
        auto        s   = settings(tick.ctx);
        int         next = cfg.cooldownTicks;
        if (s.pickupItems && pullItems(tick.ctx, player, s, range(), cfg) == PullResult::Blocked) {
            next = cfg.fullCooldownTicks;
        }
        if (s.pickupXp) pullXp(tick.ctx, player, range());
        state.cooldown = next;
    }

    std::shared_ptr<ui::Menu> createSettingsMenu(Player& player, UpgradeContext const& ctx) override {
        return std::make_shared<MagnetMenu>(player, ctx, slots(), mAdvanced);
    }

    [[nodiscard]] std::vector<std::string> describe(Player& player, UpgradeContext const& ctx) const override {
        auto s       = settings(ctx);
        auto onOff   = [&](bool v) { return v ? trFor(player, "abp.filter.on", "§aOn") : trFor(player, "abp.filter.off", "§cOff"); };
        int  ghosts  = static_cast<int>(std::count_if(s.filter.ghosts.begin(), s.filter.ghosts.end(), [](auto const& g) { return !g.empty(); }));
        return {
            trFor(player, "abp.magnet.lore.range", "§7Range: §f{} blocks", range()),
            trFor(player, "abp.magnet.lore.items", "§7Pick up items: {}", onOff(s.pickupItems)),
            trFor(player, "abp.magnet.lore.xp", "§7Pick up XP: {}", onOff(s.pickupXp)),
            trFor(player, "abp.magnet.lore.filter", "§7Filter: §f{} item(s)", ghosts),
        };
    }

private:
    struct TickState {
        int cooldown = 0;
    };

    UpgradeInfo mInfo;
    bool        mAdvanced;
};

} // namespace

PullResult pullItems(UpgradeContext const& ctx, Player& player, Settings const& s, int range, MagnetConfig const& cfg) {
    auto& region = player.getDimensionBlockSource();
    auto  actors = region.getEntities(rangeBox(player, range), 2.0f, ActorType::ItemEntity);
    auto& inv    = ctx.backpack().inventory();
    auto  access = core::AccessContext{core::Source::Pickup, &player};
    auto  result = PullResult::Nothing;
    for (auto* actor : actors) {
        if (!actor || !actor->isType(ActorType::ItemEntity)) continue;
        auto& item = static_cast<ItemActor&>(*actor);
        if (!isItemPullable(item, player, cfg)) continue;
        auto const& stack = item.item();
        auto        data  = game::ItemConv::toData(stack);
        if (data.empty() || !s.filter.passes(data, &game::ItemConv::tags())) continue;
        // Items the backpack refuses outright (nesting rules, other upgrades' filters) are not
        // "no room": they don't slow the magnet down.
        if (inv.checkInsert(-1, data, access) == core::InsertVerdict::Deny) continue;
        auto r     = inv.insert(data, access);
        int  taken = r.inserted + r.voided;
        if (taken <= 0) {
            result = PullResult::Blocked;
            continue;
        }
        if (result == PullResult::Nothing) result = PullResult::Pulled;
        if (taken < data.count) {
            // Only part fit. Clients don't see count changes of an existing item entity, so
            // replace it with a fresh one holding the rest.
            ItemStack rest = stack;
            rest.set(data.count - taken);
            if (auto* fresh = player.getLevel().getSpawner().spawnItem(region, rest, nullptr, item.getPosition(), 0)) {
                fresh->pickupDelay() = 0;
            }
            result = PullResult::Blocked;
        }
        showPickup(player, item);
        item.remove();
    }
    return result;
}

void pullXp(UpgradeContext const& ctx, Player& player, int range) {
    auto& region   = player.getDimensionBlockSource();
    auto  actors   = region.getEntities(rangeBox(player, range), 2.0f, ActorType::Experience);
    auto  feet     = player.getFeetPos();
    bool  hasHooks = !consumers().empty();
    for (auto* actor : actors) {
        if (!actor || !actor->isType(ActorType::Experience) || actor->mRemoved || !actor->isAlive()) continue;
        auto& orb = static_cast<ExperienceOrb&>(*actor);
        if (hasHooks) {
            int value = orbValue(orb);
            int left  = value > 0 ? offerXp(ctx.withPlayer(&player), &player, value) : value;
            if (left < value) {
                // A consumer took (part of) it. The rest becomes a new orb at the player, so the
                // vanilla pickup (and mending) still applies to it.
                orb.remove();
                if (left > 0) ExperienceOrb::_spawnOrb(region, player.getLevel(), feet, left);
                continue;
            }
        }
        // Nobody else wants it: bring the orb to the player and let the vanilla pickup run (XP,
        // mending, sound, the player's orb pickup cooldown). Orbs already there stay put.
        auto d = orb.getPosition() - feet;
        if (d.x * d.x + d.y * d.y + d.z * d.z > 1.0f) orb.teleport(feet, player.getDimensionId());
    }
}

// ---- settings menu -----------------------------------------------------------------------------

MagnetMenu::MagnetMenu(Player& player, UpgradeContext const& ctx, int slots, bool advanced)
: ui::FilterMenu(
      [&] {
          auto* type = upgradeFor(ctx.state().item);
          return type ? displayName(*type, &player) : trFor(player, "abp.filter.title", "Filter");
      }(),
      [ctx, slots] { return core::Filter::fromJson(ctx.settings().value("filter", nlohmann::json::object()), slots); },
      [ctx](core::Filter const& f) {
          ctx.settings()["filter"] = f.toJson();
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
    auto s       = Settings::read(mCtx.settings(), 1);
    auto onOff   = [&](bool v) { return v ? trFor(player, "abp.filter.on", "§aOn") : trFor(player, "abp.filter.off", "§cOff"); };
    auto toggle  = [this](char const* key) {
        return [this, key](ui::ClickContext const&) {
            auto& j = mCtx.settings();
            bool  v = j.is_object() && j.contains(key) && j[key].is_boolean() ? j[key].get<bool>() : true;
            j[key]  = !v;
            mCtx.saveSettings();
            invalidate();
        };
    };
    setSlot(
        control + kItemsButton,
        std::make_shared<ui::ButtonSlot>(
            ui::icon(
                "minecraft:hopper",
                trFor(player, "abp.magnet.items", "Pick up items: {}", onOff(s.pickupItems)),
                {trFor(player, "abp.filter.click_to_toggle", "§7Click to toggle")}
            ),
            toggle("pickupItems")
        )
    );
    setSlot(
        control + kXpButton,
        std::make_shared<ui::ButtonSlot>(
            ui::icon(
                "minecraft:experience_bottle",
                trFor(player, "abp.magnet.xp", "Pick up XP: {}", onOff(s.pickupXp)),
                {trFor(player, "abp.filter.click_to_toggle", "§7Click to toggle")}
            ),
            toggle("pickupXp")
        )
    );
}

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
