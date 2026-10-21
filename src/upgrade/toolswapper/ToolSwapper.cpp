// Tool Swapper upgrade (basic + advanced): swaps the held item for the best tool / weapon stored in
// a carried backpack when the player starts breaking a block, attacks, or uses shears/axe/hoe/shovel
// targets. The swapped-out item goes into the backpack slot the tool came from.

#include "upgrade/toolswapper/ToolSwapper.h"

#include <algorithm>
#include <array>
#include <functional>

#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorFlags.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"

#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"

ABP_REGISTER_CONFIG("toolswapper", abp::upgrade::toolswapper::ToolSwapperConfig);

namespace abp::upgrade::toolswapper {

namespace {

ToolSwapperConfig const& cfg() { return config::get<ToolSwapperConfig>(); }

core::ItemStackData tagProbe(ItemStack const& stack) {
    core::ItemStackData d;
    if (stack.isNull()) return d;
    d.id    = stack.getTypeName();
    d.aux   = stack.getAuxValue();
    d.count = 1;
    return d;
}

bool hasAnyTag(ItemStack const& stack, std::initializer_list<std::string_view> tags) {
    auto probe = tagProbe(stack);
    if (probe.empty()) return false;
    auto const& provider = game::ItemConv::tags();
    return std::any_of(tags.begin(), tags.end(), [&](std::string_view t) { return provider.hasTag(probe, t); });
}

bool hasTag(ItemStack const& stack, std::string_view tag) { return hasAnyTag(stack, {tag}); }

/// Remaining uses as a tiny tie-breaker (prefer the most durable of equal tools).
double durabilityBonus(ItemStack const& stack) {
    auto* item = stack.mItem.get();
    if (!item || !stack.isDamageableItem()) return 0.0;
    return static_cast<double>(std::max(0, item->getMaxDamage() - stack.getDamageValue())) / 100000.0;
}

/// `j[key]` when it has the type of `def`, else `def` (settings may come from old or hand-edited data).
template <class T>
T field(nlohmann::json const& j, char const* key, T def) {
    auto it = j.find(key);
    if (it == j.end()) return def;
    if constexpr (std::is_same_v<T, bool>) {
        return it->is_boolean() ? it->template get<bool>() : def;
    } else {
        return it->is_number_integer() ? it->template get<T>() : def;
    }
}

} // namespace

// ---- settings -----------------------------------------------------------------------------------

bool isAdvanced(UpgradeContext const& ctx) {
    auto* type = upgradeFor(ctx.state().item);
    return type && type->info().id == kAdvancedId;
}

Settings settingsOf(UpgradeContext const& ctx) {
    Settings s;
    s.skipLowDurability = cfg().skipLowDurabilityByDefault;
    if (!isAdvanced(ctx)) return s;
    auto const& j = ctx.settings();
    if (!j.is_object()) return s;
    s.weapons           = field(j, "weapons", s.weapons);
    s.mode              = static_cast<ToolMode>(std::clamp(field(j, "mode", static_cast<int>(s.mode)), 0, 2));
    s.skipLowDurability = field(j, "durability", s.skipLowDurability);
    s.interact          = field(j, "interact", s.interact);
    if (auto it = j.find("filter"); it != j.end() && it->is_object()) {
        try {
            s.filter    = core::Filter::fromJson(*it, kFilterSlots);
            s.hasFilter = !s.filter.isEmpty();
        } catch (std::exception const&) {
            s.hasFilter = false; // unreadable filter: behave like no filter
        }
    }
    return s;
}

void saveSettings(UpgradeContext const& ctx, Settings const& s) {
    auto& j = ctx.settings();
    if (!j.is_object()) j = nlohmann::json::object();
    j["weapons"]    = s.weapons;
    j["mode"]       = static_cast<int>(s.mode);
    j["durability"] = s.skipLowDurability;
    j["interact"]   = s.interact;
    ctx.saveSettings(false);
}

// ---- classification -------------------------------------------------------------------------------

bool isWeapon(ItemStack const& stack) {
    if (stack.isNull()) return false;
    auto id = stack.getTypeName();
    if (id == "minecraft:mace" || id == "minecraft:trident") return true;
    return hasAnyTag(stack, {"minecraft:is_sword", "minecraft:is_axe", "minecraft:is_mace", "minecraft:is_trident"});
}

bool isTool(ItemStack const& stack) {
    if (stack.isNull()) return false;
    if (stack.getTypeName() == "minecraft:shears" || isWeapon(stack)) return true;
    return hasAnyTag(
        stack,
        {"minecraft:is_tool", "minecraft:digger", "minecraft:is_pickaxe", "minecraft:is_shovel", "minecraft:is_hoe"}
    );
}

float mineSpeed(ItemStack const& stack, Block const& block) {
    if (stack.isNull()) return 1.0f;
    auto* item = stack.mItem.get();
    if (!item) return 1.0f;
    float speed = item->getDestroySpeed(stack, block);
    if (speed > 1.0f) {
        int eff = EnchantUtils::getEnchantLevel(Enchant::Type::Efficiency, stack);
        if (eff > 0) speed += static_cast<float>(eff * eff + 1);
    }
    return speed;
}

bool canHarvest(ItemStack const& stack, Block const& block) {
    if (!block.getBlockType().mRequiresCorrectToolForDrops) return true;
    if (stack.isNull()) return false;
    auto* item = stack.mItem.get();
    return item && item->canDestroySpecial(block);
}

float attackDamage(ItemStack const& stack) {
    if (stack.isNull()) return 0.0f;
    auto* item = stack.mItem.get();
    if (!item) return 0.0f;
    float dmg   = static_cast<float>(item->getAttackDamage());
    int   sharp = EnchantUtils::getEnchantLevel(Enchant::Type::Sharpness, stack);
    if (sharp > 0) dmg += 1.25f * static_cast<float>(sharp);
    return dmg;
}

std::string useToolFor(Block const& block) {
    std::string_view name = block.getTypeName();
    // Axes strip logs/wood/nether stems and scrape or unwax copper.
    bool netherStem = name == "minecraft:crimson_stem" || name == "minecraft:warped_stem";
    if (name.ends_with("_log") || name.ends_with("_wood") || name.ends_with("_hyphae") || netherStem
        || name == "minecraft:bamboo_block") {
        return name.find("stripped_") == std::string_view::npos ? "minecraft:is_axe" : "";
    }
    if (name.find("copper") != std::string_view::npos
        && (name.starts_with("minecraft:waxed_") || name.starts_with("minecraft:exposed_")
            || name.starts_with("minecraft:weathered_") || name.starts_with("minecraft:oxidized_"))) {
        return "minecraft:is_axe";
    }
    // Hoes till dirt into farmland (and rooted dirt into dirt).
    if (name == "minecraft:grass_block" || name == "minecraft:dirt" || name == "minecraft:coarse_dirt"
        || name == "minecraft:dirt_with_roots") {
        return "minecraft:is_hoe";
    }
    // Shovels make paths and put out campfires.
    if (name == "minecraft:podzol" || name == "minecraft:mycelium" || name == "minecraft:campfire"
        || name == "minecraft:soul_campfire") {
        return "minecraft:is_shovel";
    }
    // Shears carve pumpkins and harvest honeycomb.
    if (name == "minecraft:pumpkin" || name == "minecraft:beehive" || name == "minecraft:bee_nest") {
        return "minecraft:shears";
    }
    return {};
}

bool isShearable(Actor const& actor) {
    auto const& type      = actor.getTypeName();
    bool        candidate = type == "minecraft:sheep" || type == "minecraft:mooshroom" || type == "minecraft:snow_golem"
                         || type == "minecraft:bogged";
    return candidate && !actor.getStatusFlag(ActorFlags::Sheared) && !actor.isBaby();
}

// ---- swap engine ----------------------------------------------------------------------------------

namespace {

enum class Purpose { Block, Attack, Use };

/// Score of a candidate (nullopt = not usable for this purpose). Higher is better.
using Scorer = std::function<std::optional<double>(ItemStack const&)>;

bool allowedBy(Settings const& s, Purpose purpose, ItemStack const& held) {
    switch (purpose) {
    case Purpose::Block:
        if (s.mode == ToolMode::Off) return false;
        break;
    case Purpose::Attack:
        if (!s.weapons) return false;
        break;
    case Purpose::Use:
        if (!s.interact) return false;
        break;
    }
    // "Only when holding a tool" is about block-breaking swaps (SB tool swap mode).
    if (purpose == Purpose::Block && s.mode == ToolMode::OnlyTools && !held.isNull() && !isTool(held)) return false;
    return true;
}

bool nearlyBroken(core::ItemStackData const& d) {
    if (d.maxDamage <= 0) return false;
    return d.maxDamage - d.damage <= std::max(0, cfg().lowDurabilityUses);
}

struct Best {
    std::shared_ptr<game::Backpack> backpack;
    int                             slot  = -1;
    double                          score = 0;
};

/// Puts the best-scoring candidate (score > `minScore`) from the carried backpacks into the main hand.
bool swapBest(Player& player, Purpose purpose, double minScore, Scorer const& score) {
    if (!cfg().enabled || player.isClientSide() || !player.isAlive() || player.isCreative()) return false;
    if (ui::MenuManager::get().current(player)) return false;
    auto            hand = game::selectedSlot(player);
    ItemStack const held = game::itemAt(player, hand);

    Best best;
    best.score = minScore;
    for (auto& [carried, bp] : game::carriedBackpacks(player)) {
        if (carried.where == hand) continue; // the held item is this backpack: it can't go into itself
        forEachInstalled(bp, true, [&](IUpgrade& type, UpgradeContext const& ctx) {
            if (type.info().groupOrId() != kGroup) return;
            auto settings = settingsOf(ctx);
            if (!allowedBy(settings, purpose, held)) return;
            auto& inv = bp->inventory();
            for (int i = 0; i < inv.size(); ++i) {
                auto const& d = inv.at(i);
                // Tools and weapons are unstackable and (in vanilla) damageable.
                if (d.empty() || d.maxStack != 1 || d.maxDamage <= 0) continue;
                if (settings.skipLowDurability && nearlyBroken(d)) continue;
                if (settings.hasFilter && !settings.filter.passes(d, &game::ItemConv::tags())) continue;
                auto s = score(game::ItemConv::toStack(d, 1));
                if (s && *s > best.score) best = Best{bp, i, *s};
            }
        });
    }
    if (!best.backpack) return false;

    auto&               inv = best.backpack->inventory();
    core::AccessContext access{core::Source::Upgrade, &player};
    if (!inv.canExtract(best.slot, 1, access)) return false;
    auto tool = inv.extract(best.slot, 1, access);
    if (tool.empty()) return false;
    auto heldData = game::ItemConv::toData(held);
    if (!heldData.empty()) {
        // Back into the tool's slot; anywhere else in that backpack if the slot refuses it. Only a
        // complete, non-voided insert counts: a Void filter must never eat the held item.
        auto kept   = [&](core::InsertResult const& r) { return r.remainder.empty() && r.voided == 0; };
        bool stored = kept(inv.insertInto(best.slot, heldData, access, true))
                   && kept(inv.insertInto(best.slot, heldData, access));
        if (!stored) stored = kept(inv.insert(heldData, access, true)) && kept(inv.insert(heldData, access));
        if (!stored) {
            // Undo: put the tool back exactly where it was.
            auto rest = inv.at(best.slot);
            if (rest.empty()) inv.set(best.slot, tool);
            else if (rest.sameItem(tool)) inv.set(best.slot, rest.withCount(rest.count + tool.count));
            else inv.insert(tool, core::AccessContext{core::Source::Internal, &player});
            return false;
        }
    }
    game::setItemAt(player, hand, game::ItemConv::toStack(tool, 1));
    log().info(
        "tool swapper: {} swapped {} for {}",
        player.getRealName(),
        heldData.empty() ? "<empty>" : heldData.id,
        tool.id
    );
    return true;
}

} // namespace

bool swapForBlock(Player& player, Block const& block) {
    if (block.isAir()) return false;
    auto const& held = game::itemAt(player, game::selectedSlot(player));
    float       hs   = mineSpeed(held, block);
    bool        hh   = canHarvest(held, block);
    if (hs > 1.0f && hh) return false; // never swap away a tool that already works
    auto blockScore = [&](float speed, bool harvest) { return (harvest ? 100000.0 : 0.0) + speed; };
    return swapBest(player, Purpose::Block, blockScore(hs, hh), [&](ItemStack const& s) -> std::optional<double> {
        float speed   = mineSpeed(s, block);
        bool  harvest = canHarvest(s, block);
        if (speed <= 1.0f && !(harvest && !hh)) return std::nullopt; // not better at this block than a hand
        return blockScore(speed, harvest);
    });
}

bool swapForAttack(Player& player) {
    auto const& held = game::itemAt(player, game::selectedSlot(player));
    if (isWeapon(held)) return false; // already holding a weapon
    double min = std::max(1.0, static_cast<double>(attackDamage(held)));
    return swapBest(player, Purpose::Attack, min, [](ItemStack const& s) -> std::optional<double> {
        if (!isWeapon(s)) return std::nullopt;
        return attackDamage(s);
    });
}

bool swapForEntityUse(Player& player, Actor const& actor) {
    if (!game::itemAt(player, game::selectedSlot(player)).isNull() || !isShearable(actor)) return false;
    return swapBest(player, Purpose::Use, 0.0, [](ItemStack const& s) -> std::optional<double> {
        if (s.getTypeName() != "minecraft:shears") return std::nullopt;
        return 1.0 + durabilityBonus(s); // the most durable pair
    });
}

bool swapForBlockUse(Player& player, Block const& block) {
    if (!game::itemAt(player, game::selectedSlot(player)).isNull()) return false;
    auto tool = useToolFor(block);
    if (tool.empty()) return false;
    return swapBest(player, Purpose::Use, 0.0, [&](ItemStack const& s) -> std::optional<double> {
        bool match = tool == "minecraft:shears" ? s.getTypeName() == "minecraft:shears" : hasTag(s, tool);
        if (!match) return std::nullopt;
        // Better tiers first (mining speed on the block), then durability.
        return 1.0 + mineSpeed(s, block) + durabilityBonus(s);
    });
}

} // namespace abp::upgrade::toolswapper
