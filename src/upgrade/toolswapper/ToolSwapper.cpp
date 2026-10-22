// Tool Swapper upgrade logic (SB ToolSwapperUpgradeWrapper): settings, classification, the block
// click / attack swaps and the tool swap key.

#include "upgrade/toolswapper/ToolSwapper.h"

#include <algorithm>
#include <deque>
#include <unordered_set>

#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorFlags.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/phys/HitResult.h"

#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/deposit/ContainerTransfer.h"
#include "upgrade/inception/Inception.h"

ABP_REGISTER_CONFIG("toolswapper", abp::upgrade::toolswapper::ToolSwapperConfig);

namespace abp::upgrade::toolswapper {

namespace {

constexpr float kReach = 5.5f;

ToolSwapperConfig const& cfg() { return config::get<ToolSwapperConfig>(); }

core::ItemStackData tagProbe(ItemStack const& stack) {
    core::ItemStackData d;
    if (stack.isNull()) return d;
    d.id    = stack.getTypeName();
    d.aux   = stack.getAuxValue();
    d.count = 1;
    return d;
}

bool hasTag(ItemStack const& stack, std::string_view tag) {
    auto probe = tagProbe(stack);
    return !probe.empty() && game::ItemConv::tags().hasTag(probe, tag);
}

/// Runtime state of one installed Tool Swapper (SB wrapper fields).
struct State {
    std::string                     lastMinedBlock;
    std::string                     toolCacheFor;
    std::deque<core::ItemStackData> toolCache;
};

core::AccessContext access(Player& player) { return core::AccessContext{core::Source::Upgrade, &player}; }

bool passes(Settings const& s, core::ItemStackData const& d) { return s.filter.passes(d, &game::ItemConv::tags()); }
bool passes(Settings const& s, ItemStack const& stack) { return stack.isNull() ? passes(s, core::ItemStackData{}) : passes(s, game::ItemConv::toData(stack)); }

/// Puts `item` into the inventories (in order); whatever doesn't fit goes to the player (or is
/// dropped at their feet) so nothing is lost.
void storeOrGive(std::vector<std::shared_ptr<game::Backpack>> const& invs, core::ItemStackData item, Player& player) {
    for (auto const& bp : invs) {
        if (item.empty()) return;
        auto r = bp->inventory().insert(item, access(player));
        item   = r.remainder;
    }
    if (item.empty()) return;
    auto stack = game::ItemConv::toStack(item, item.count);
    if (!player.add(stack)) player.drop(stack, false);
}

/// Every installed Tool Swapper of the backpacks `player` carries (inception: contained ones too).
template <class Fn>
bool forEachSwapper(Player& player, Fn&& fn) {
    auto hand = game::selectedSlot(player);
    for (auto& [carried, bp] : game::carriedBackpacks(player, false, true)) {
        if (carried.depth == 0 && carried.where == hand) continue; // the held backpack itself
        bool done = false;
        forEachInstalled(bp, true, [&](IUpgrade& type, UpgradeContext const& ctx) {
            if (done || type.info().groupOrId() != kGroup || !carried.upgradeActive(type.info().id, kGroup)) return;
            done = fn(ctx.withPlayer(&player));
        });
        if (done) return true;
    }
    return false;
}

} // namespace

// ---- settings -----------------------------------------------------------------------------------

std::string_view toString(ToolMode mode) {
    switch (mode) {
    case ToolMode::OnlyTools:
        return "onlyTools";
    case ToolMode::Off:
        return "noSwap";
    case ToolMode::Any:
    default:
        return "name"; // SB's serialized name of ANY
    }
}

ToolMode toolModeFromString(std::string_view s) {
    if (s == "onlyTools") return ToolMode::OnlyTools;
    if (s == "noSwap") return ToolMode::Off;
    return ToolMode::Any;
}

int filterSlots() { return std::clamp(config::get<ToolSwapperConfig>().toolSwapperUpgrade.filterSlots, 1, 20); }
int filterSlotsInRow() { return std::clamp(config::get<ToolSwapperConfig>().toolSwapperUpgrade.slotsInRow, 1, 6); }
filtering::FilterSpec filterSpec() {
    return filtering::specOf(config::get<ToolSwapperConfig>().toolSwapperUpgrade, true, filtering::TypeButton::AllowList);
}

core::Filter Settings::defaultFilter() {
    core::Filter f(filterSlots());
    f.mode            = core::FilterMode::Deny; // SC FilterLogic allowListDefault = false
    f.matchBy         = core::MatchBy::Item;
    f.matchNbt        = false;
    f.matchDurability = false;
    f.emptyAllowsAll  = false;
    return f;
}

bool isAdvanced(UpgradeContext const& ctx) {
    auto* type = upgradeFor(ctx.state().item);
    return type && type->info().id == kAdvancedId;
}

Settings settingsOf(UpgradeContext const& ctx) {
    Settings s;
    auto const& j = ctx.settings();
    if (!j.is_object()) return s;
    if (auto it = j.find("swapWeapon"); it != j.end() && it->is_boolean()) s.swapWeapon = it->get<bool>();
    else if (auto old = j.find("weapons"); old != j.end() && old->is_boolean()) s.swapWeapon = old->get<bool>(); // phase-1
    if (auto it = j.find("toolSwapMode"); it != j.end() && it->is_string()) s.mode = toolModeFromString(it->get<std::string>());
    else if (auto old = j.find("mode"); old != j.end() && old->is_number_integer()) s.mode = static_cast<ToolMode>(std::clamp(old->get<int>(), 0, 2));
    if (auto it = j.find("filter"); it != j.end() && it->is_object()) {
        try {
            s.filter                = core::Filter::fromJson(*it, filterSlots());
            s.filter.emptyAllowsAll = false; // SC: an empty allow list matches nothing here
            // Filters saved before the SB settings (phase 1, chest filter editor) default to an
            // allow list that ignored emptiness: an empty one keeps letting everything through.
            if (!j.contains("filterSb") && s.filter.isEmpty()) s.filter.mode = core::FilterMode::Deny;
        } catch (std::exception const&) {
            s.filter = Settings::defaultFilter(); // unreadable filter: behave like no filter
        }
    }
    return s;
}

void setSwapWeapon(UpgradeContext const& ctx, bool on) {
    ctx.settings()["swapWeapon"] = on;
    ctx.saveSettings(false);
}

void setToolMode(UpgradeContext const& ctx, ToolMode mode) {
    ctx.settings()["toolSwapMode"] = std::string(toString(mode));
    ctx.saveSettings(false);
}

void setFilter(UpgradeContext const& ctx, core::Filter const& filter) {
    ctx.settings()["filter"]   = filter.toJson();
    ctx.settings()["filterSb"] = true; // SB filter semantics (see settingsOf)
    ctx.saveSettings(false);
}

// ---- classification -------------------------------------------------------------------------------

bool isSword(ItemStack const& stack) { return !stack.isNull() && hasTag(stack, "minecraft:is_sword"); }

bool isAxe(ItemStack const& stack) { return !stack.isNull() && hasTag(stack, "minecraft:is_axe"); }

bool isTool(ItemStack const& stack) {
    if (stack.isNull()) return false;
    if (stack.getTypeName() == "minecraft:shears") return true;
    return hasTag(stack, "minecraft:is_axe") || hasTag(stack, "minecraft:is_hoe") || hasTag(stack, "minecraft:is_pickaxe")
        || hasTag(stack, "minecraft:is_shovel");
}

float destroySpeed(ItemStack const& stack, Block const& block) {
    if (stack.isNull()) return 1.0f;
    auto* item = stack.mItem.get();
    return item ? item->getDestroySpeed(stack, block) : 1.0f;
}

namespace {
bool correctTool(ItemStack const& stack, Block const& block) {
    if (!block.getBlockType().mRequiresCorrectToolForDrops) return true;
    if (stack.isNull()) return false;
    auto* item = stack.mItem.get();
    return item && item->canDestroySpecial(block);
}
} // namespace

float destroyProgress(ItemStack const& stack, Block const& block) {
    float hardness = block.getBlockType().getDestroySpeed();
    if (hardness < 0) return 0.0f;
    if (hardness == 0) return 1.0f;
    float speed = destroySpeed(stack, block);
    if (speed > 1.0f && !stack.isNull()) {
        int eff = EnchantUtils::getEnchantLevel(Enchant::Type::Efficiency, stack);
        if (eff > 0) speed += static_cast<float>(eff * eff + 1);
    }
    return speed / hardness / (correctTool(stack, block) ? 30.0f : 100.0f);
}

bool goodAtBreaking(ItemStack const& stack, Block const& block) {
    return correctTool(stack, block) && (destroySpeed(stack, block) > 1.5f || destroyProgress(stack, block) >= 1.0f);
}

float attackDamage(ItemStack const& stack) {
    if (stack.isNull()) return 0.0f;
    auto* item = stack.mItem.get();
    return item ? static_cast<float>(item->getAttackDamage()) : 0.0f;
}

bool isShearable(Actor const& actor) {
    auto const& type      = actor.getTypeName();
    bool        candidate = type == "minecraft:sheep" || type == "minecraft:mooshroom" || type == "minecraft:snow_golem"
                         || type == "minecraft:bogged";
    return candidate && !actor.getStatusFlag(ActorFlags::Sheared) && !actor.isBaby();
}

namespace {
bool isShears(ItemStack const& s) { return !s.isNull() && (s.getTypeName() == "minecraft:shears" || hasTag(s, "minecraft:is_shears")); }

/// SB ToolRegistry entity_tools.json: minecraft:cow -> bucket, {"type":"animal"} -> lead.
bool isAnimal(std::string_view type) {
    static std::unordered_set<std::string_view> const animals{
        "minecraft:cow",       "minecraft:mooshroom", "minecraft:pig",      "minecraft:sheep",    "minecraft:chicken",
        "minecraft:horse",     "minecraft:donkey",    "minecraft:mule",     "minecraft:llama",    "minecraft:trader_llama",
        "minecraft:cat",       "minecraft:wolf",      "minecraft:fox",      "minecraft:rabbit",   "minecraft:goat",
        "minecraft:frog",      "minecraft:camel",     "minecraft:sniffer",  "minecraft:armadillo", "minecraft:ocelot",
        "minecraft:panda",     "minecraft:polar_bear", "minecraft:turtle",  "minecraft:bee",      "minecraft:axolotl",
        "minecraft:hoglin",    "minecraft:strider",   "minecraft:parrot",   "minecraft:skeleton_horse", "minecraft:zombie_horse",
    };
    return animals.contains(type);
}
} // namespace

bool worksOnEntity(ItemStack const& stack, Actor const& actor) {
    if (stack.isNull()) return false;
    if (isShearable(actor) && isShears(stack)) return true;
    auto const& type = actor.getTypeName();
    auto        id   = stack.getTypeName();
    if (type == "minecraft:cow" && id == "minecraft:bucket") return true;
    return id == "minecraft:lead" && isAnimal(type);
}

bool worksOnBlock(ItemStack const& stack, Block const& block, Player& player, BlockPos const& pos) {
    if (stack.isNull()) return false;
    std::string_view name = block.getTypeName();
    if (isAxe(stack)) {
        // AXE_STRIP
        bool strippable = name.find("stripped_") == std::string_view::npos
                       && (name.ends_with("_log") || name.ends_with("_wood") || name.ends_with("_hyphae")
                           || name == "minecraft:crimson_stem" || name == "minecraft:warped_stem" || name == "minecraft:bamboo_block");
        if (strippable) return true;
        // AXE_SCRAPE (oxidized copper) / AXE_WAX_OFF (waxed copper)
        if (name.find("copper") != std::string_view::npos
            && (name.starts_with("minecraft:waxed_") || name.starts_with("minecraft:exposed_")
                || name.starts_with("minecraft:weathered_") || name.starts_with("minecraft:oxidized_"))) {
            return true;
        }
    }
    if (hasTag(stack, "minecraft:is_shovel")) {
        // SHOVEL_FLATTEN: dirt-like blocks with air above become a path.
        bool flattenable = name == "minecraft:grass_block" || name == "minecraft:dirt" || name == "minecraft:coarse_dirt"
                        || name == "minecraft:mycelium" || name == "minecraft:podzol" || name == "minecraft:dirt_with_roots";
        if (flattenable && player.getDimensionBlockSource().getBlock(BlockPos{pos.x, pos.y + 1, pos.z}).isAir()) return true;
    }
    if (isShears(stack)) {
        // SHEARS_CARVE (pumpkin) and the shear interaction blocks (beehives).
        if (name == "minecraft:pumpkin" || name == "minecraft:beehive" || name == "minecraft:bee_nest") return true;
    }
    return false; // ToolRegistry block_tools: SB ships no vanilla entries (wrenches are mod compat)
}

// ---- block click / attack (SB onBlockClick / onAttackEntity) ----------------------------------------

namespace {
bool blockClick(UpgradeContext const& ctx, Player& player, BlockPos const& pos) {
    auto s = settingsOf(ctx);
    if (player.isCreative() || player.isSpectator() || s.mode == ToolMode::Off) return false;
    auto&       region = player.getDimensionBlockSource();
    auto const& block  = region.getBlock(pos);
    if (block.isAir()) return false;
    auto      hand = game::selectedSlot(player);
    ItemStack held = game::itemAt(player, hand);
    if (game::isBackpack(held) || (s.mode == ToolMode::OnlyTools && isSword(held)) || (!isSword(held) && !isTool(held)) || !passes(s, held)) {
        return false;
    }
    auto& state = ctx.transient<State>();
    float mainSpeed = 0;
    if (goodAtBreaking(held, block)) {
        if (state.lastMinedBlock == block.getTypeName() || block.getBlockType().getDestroySpeed() == 0) return true;
        mainSpeed = destroySpeed(held, block);
    }
    state.lastMinedBlock = block.getTypeName();

    // SB tryToSwapTool: the backpack's own inventory (not the upgrade processing one).
    auto& inv        = ctx.backpack().inventory();
    int   selected   = -1;
    float best       = mainSpeed;
    bool  instant    = false;
    for (int i = 0; i < inv.size() && !instant; ++i) {
        auto const& d = inv.at(i);
        if (d.empty() || !passes(s, d)) continue;
        auto stack = game::ItemConv::toStack(d, 1);
        if (!goodAtBreaking(stack, block)) continue;
        if (destroyProgress(stack, block) >= 1.0f) {
            selected = i;
            instant  = true;
        } else if (float speed = destroySpeed(stack, block); best < speed) {
            best     = speed;
            selected = i;
        }
    }
    if (selected < 0) return false;
    auto heldData = game::ItemConv::toData(held);
    auto toolData = inv.at(selected);
    if (!inv.canExtract(selected, 1, access(player))) return false;
    // SB hasSpaceInBackpackOrCanPlaceInTheSlotOfSwappedTool.
    bool fits = heldData.empty();
    if (!fits) {
        auto r = inv.insert(heldData, access(player), true);
        fits   = r.remainder.empty() && r.voided == 0; // never let a Void upgrade eat the held item
    }
    if (!fits && toolData.count == 1) {
        auto tool = inv.extract(selected, 1, access(player));
        auto r    = inv.insertInto(selected, heldData, access(player), true);
        if (!tool.empty() && r.remainder.empty() && r.voided == 0) {
            (void)inv.insertInto(selected, heldData, access(player));
            game::setItemAt(player, hand, game::ItemConv::toStack(tool, 1));
            log().info("tool swapper: {} swapped {} for {}", player.getRealName(), heldData.id, tool.id);
            return true;
        }
        if (!tool.empty()) inv.set(selected, tool); // undo
        return false;
    }
    if (!fits) return false;
    auto tool = inv.extract(selected, 1, access(player));
    if (tool.empty()) return false;
    if (!heldData.empty()) storeOrGive({ctx.backpackPtr()}, heldData, player);
    game::setItemAt(player, hand, game::ItemConv::toStack(tool, 1));
    log().info("tool swapper: {} swapped {} for {}", player.getRealName(), heldData.empty() ? "<empty>" : heldData.id, tool.id);
    return true;
}

bool attack(UpgradeContext const& ctx, Player& player) {
    auto s = settingsOf(ctx);
    if (!s.swapWeapon || player.isCreative() || player.isSpectator()) return false;
    auto      hand = game::selectedSlot(player);
    ItemStack held = game::itemAt(player, hand);
    if (isSword(held)) return true;
    if (game::isBackpack(held) || !isTool(held) || !passes(s, held)) return false;
    // SB tryToSwapInWeapon: the best sword, else the best axe (the held item competes too).
    struct Candidate {
        std::shared_ptr<game::Backpack> bp;
        int                             slot = -1; ///< -1 = the held item
        float                           damage = 0;
    } bestSword, bestAxe;
    auto consider = [&](ItemStack const& stack, std::shared_ptr<game::Backpack> const& bp, int slot) {
        float dmg = attackDamage(stack);
        if (isAxe(stack)) {
            if (dmg > bestAxe.damage) bestAxe = {bp, slot, dmg};
        } else if (isSword(stack) && dmg > bestSword.damage) {
            bestSword = {bp, slot, dmg};
        }
    };
    consider(held, nullptr, -1);
    auto invs = inception::upgradeInventories(ctx.backpackPtr());
    for (auto const& bp : invs) {
        auto& inv = bp->inventory();
        for (int i = 0; i < inv.size(); ++i) {
            auto const& d = inv.at(i);
            if (!d.empty() && passes(s, d)) consider(game::ItemConv::toStack(d, 1), bp, i);
        }
    }
    auto const& pick = bestSword.damage > 0 ? bestSword : bestAxe;
    if (pick.damage <= 0) return false;
    if (pick.slot < 0) return true; // the held item already is the best weapon
    auto& inv = pick.bp->inventory();
    if (!inv.canExtract(pick.slot, 1, access(player))) return false;
    auto heldData = game::ItemConv::toData(held);
    auto weapon   = inv.extract(pick.slot, 1, access(player));
    if (weapon.empty()) return false;
    // SB swapWeapon: only when the held item fits into the backpack.
    core::ItemStackData left   = heldData;
    bool                voided = false;
    for (auto const& bp : invs) {
        if (left.empty()) break;
        auto r  = bp->inventory().insert(left, access(player), true);
        voided |= r.voided > 0; // never let a Void upgrade eat the held item
        left    = r.remainder;
    }
    if (!left.empty() || voided) {
        // Undo: the weapon goes back where it was (else anywhere; never lost).
        if (inv.at(pick.slot).empty()) inv.set(pick.slot, weapon);
        else if (auto rest = inv.insert(weapon, core::AccessContext{core::Source::Internal, &player}).remainder; !rest.empty()) storeOrGive({}, rest, player);
        return false;
    }
    storeOrGive(invs, heldData, player);
    game::setItemAt(player, hand, game::ItemConv::toStack(weapon, 1));
    log().info("tool swapper: {} swapped {} for weapon {}", player.getRealName(), heldData.id, weapon.id);
    return true;
}

// ---- key (SB tryToSwapTool with a validity predicate and the tool cache) ----------------------------

bool keySwap(UpgradeContext const& ctx, Player& player, std::function<bool(ItemStack const&)> const& valid, std::string const& target) {
    auto      hand = game::selectedSlot(player);
    ItemStack held = game::itemAt(player, hand);
    if (game::isBackpack(held)) return false;
    auto& state = ctx.transient<State>();
    if (state.toolCacheFor != target) {
        state.toolCache.clear();
        state.toolCacheFor = target;
    }
    auto heldData = game::ItemConv::toData(held);
    auto inCache  = [&](core::ItemStackData const& d, auto const& list) {
        return std::any_of(list.begin(), list.end(), [&](core::ItemStackData const& c) { return c.id == d.id && c.aux == d.aux; });
    };
    if (valid(held) && !inCache(heldData, state.toolCache)) state.toolCache.push_back(heldData);
    auto invs = inception::upgradeInventories(ctx.backpackPtr());
    // findToolToSwap: a valid tool not given before; else the oldest one given before.
    core::ItemStackData             found;
    std::vector<core::ItemStackData> givenBefore;
    for (auto const& bp : invs) {
        auto& inv = bp->inventory();
        for (int i = 0; i < inv.size() && found.empty(); ++i) {
            auto const& d = inv.at(i);
            if (d.empty()) continue;
            if (!inCache(d, state.toolCache)) {
                if (valid(game::ItemConv::toStack(d, 1))) found = d.withCount(1);
            } else {
                givenBefore.push_back(d);
            }
        }
        if (!found.empty()) break;
    }
    if (found.empty() && !givenBefore.empty()) {
        while (!state.toolCache.empty()) {
            auto c = state.toolCache.front();
            state.toolCache.pop_front();
            if (inCache(c, givenBefore)) {
                // The stack as it is in the backpack (the cached copy may differ in NBT).
                for (auto const& g : givenBefore) {
                    if (g.id == c.id && g.aux == c.aux) {
                        found = g.withCount(1);
                        break;
                    }
                }
                break;
            }
        }
    }
    if (found.empty()) return false;
    // Extract one matching item (SB InventoryHelper.extractFromInventory).
    core::ItemStackData tool;
    for (auto const& bp : invs) {
        tool = bp->inventory().extractMatching([&](core::ItemStackData const& d) { return d.sameItem(found); }, 1, access(player));
        if (!tool.empty()) break;
    }
    if (tool.empty()) return false;
    game::setItemAt(player, hand, game::ItemConv::toStack(tool, 1));
    if (!heldData.empty()) storeOrGive(invs, heldData, player);
    state.toolCache.push_back(tool);
    log().info("tool swapper: {} key-swapped {} for {}", player.getRealName(), heldData.empty() ? "<empty>" : heldData.id, tool.id);
    return true;
}
} // namespace

bool onBlockClick(Player& player, BlockPos const& pos) {
    if (!cfg().enabled || player.isClientSide() || !player.isAlive()) return false;
    return forEachSwapper(player, [&](UpgradeContext const& ctx) { return blockClick(ctx, player, pos); });
}

bool onAttack(Player& player) {
    if (!cfg().enabled || player.isClientSide() || !player.isAlive()) return false;
    return forEachSwapper(player, [&](UpgradeContext const& ctx) { return attack(ctx, player); });
}

KeyResult onSwapKey(Player& player) {
    auto tip = [&](std::string_view key, std::string_view fallback) { transfer::sendTip(player, trFor(player, key, fallback)); };
    if (game::isBackpack(game::itemAt(player, game::selectedSlot(player)))) {
        tip("abp.toolswapper.status.unable_to_swap_tool_for_backpack", "Unable to swap tool for backpack in hand. Switch to different item or empty hand.");
        return KeyResult::HoldingBackpack;
    }
    auto hit = player.traceRay(kReach, true, true);
    if (hit.mType != HitResultType::Tile && hit.mType != HitResultType::Entity) return KeyResult::NoTarget;
    // SB onBlockInteract / onEntityInteract (the key): no creative check, unlike the click and
    // attack swaps. Spectators can't touch their inventory on Bedrock, so they stay excluded.
    if (!cfg().enabled || player.isSpectator()) return KeyResult::NoTarget;
    bool anyUpgrade = false;
    bool swapped    = false;
    if (hit.mType == HitResultType::Tile) {
        BlockPos    pos   = hit.mBlock;
        auto const& block = player.getDimensionBlockSource().getBlock(pos);
        std::string target(block.getTypeName());
        swapped = forEachSwapper(player, [&](UpgradeContext const& ctx) {
            if (!isAdvanced(ctx)) return false; // SB: swapToolOnKeyPress only on the advanced one
            anyUpgrade = true;
            return keySwap(ctx, player, [&](ItemStack const& s) { return worksOnBlock(s, block, player, pos); }, target);
        });
    } else {
        auto* entity = hit.getEntity();
        if (!entity) return KeyResult::NoTarget;
        std::string target = entity->getTypeName();
        swapped            = forEachSwapper(player, [&](UpgradeContext const& ctx) {
            if (!isAdvanced(ctx)) return false;
            anyUpgrade = true;
            return keySwap(ctx, player, [&](ItemStack const& s) { return worksOnEntity(s, *entity); }, target);
        });
    }
    if (!anyUpgrade) {
        tip("abp.toolswapper.status.no_tool_swap_upgrade_present", "No upgrade present that can do tool swap on key press");
        return KeyResult::NoUpgrade;
    }
    if (swapped) {
        player.refreshInventory();
        return KeyResult::Swapped;
    }
    if (hit.mType == HitResultType::Tile) {
        tip("abp.toolswapper.status.no_tool_found_for_block", "No tool was found that works on block");
        return KeyResult::NoToolForBlock;
    }
    tip("abp.toolswapper.status.no_tool_found_for_entity", "No tool was found that works on entity");
    return KeyResult::NoToolForEntity;
}

} // namespace abp::upgrade::toolswapper
