// U1 selftests: tiers.*  (`backpack selftest "tiers.*"`)

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

#include <format>

#include "ll/api/command/CommandHandle.h"
#include "ll/api/service/Bedrock.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"

#include "mc/deps/core/math/Color.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/PlayerUISlot.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/inventory/CraftingContainer.h"
#include "mc/world/item/crafting/CraftingContext.h"
#include "mc/world/item/crafting/Recipe.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/item/registry/CreativeItemEntry.h"
#include "mc/world/item/registry/CreativeItemRegistry.h"
#include "mc/world/item/registry/ItemRegistryRef.h"
#include "mc/world/level/Level.h"

#include "core/Appearance.h"
#include "feature/tiers/Tiers.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/Storage.h"
#include "mod/Commands.h"
#include "mod/Log.h"
#include "upgrade/UpgradeManager.h"
#include "selftest/SelfTest.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"

namespace abp::tiers {
namespace {

using core::Tier;
using selftest::Context;

core::AccessContext const kInternal{core::Source::Internal};

constexpr int kGrid0 = static_cast<int>(PlayerUISlot::Crafting3x3Input1);

core::ItemStackData data(std::string_view id, int count) { return game::ItemConv::toData(game::ItemConv::make(id, count)); }

ConsumedInput input(int gridIndex, ItemStack const& item) { return {kGrid0 + gridIndex, std::make_shared<ItemStack>(item)}; }

std::map<std::string, std::shared_ptr<Recipe>> const* recipesFor(std::string const& tag) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    auto const& all = level->getRecipes().mRecipes.get();
    auto        it  = all.find(HashedString(tag));
    return it == all.end() ? nullptr : &it->second;
}

std::string resultId(Recipe const& r) {
    auto const& results = r.getResultItems();
    return results.empty() ? std::string{} : results.front().getTypeName();
}

/// The result of the first `crafting_table` recipe matching a 3x3 grid (row-major ids, "" = empty).
std::string craftResult(std::array<ItemStack, 9> const& grid) {
    auto level   = ll::service::getLevel();
    auto recipes = recipesFor("crafting_table");
    if (!level || !recipes) return "<no recipes>";
    CraftingContainer container(3, 3);
    for (int i = 0; i < 9; ++i) container.setItem(i, grid[static_cast<std::size_t>(i)]);
    CraftingContext ctx(Bedrock::NotNullNonOwnerPtr<ILevel>(Bedrock::NonOwnerPointer<ILevel>(static_cast<ILevel const&>(*level))));
    for (auto const& [id, recipe] : *recipes) {
        if (recipe && recipe->matches(container, ctx)) return resultId(*recipe);
    }
    return {};
}

std::string loreText(ItemStack const& stack) {
    std::string all;
    for (auto const& l : stack.getCustomLore()) all += l + "\n";
    return all;
}

// ---- recipes ---------------------------------------------------------------------------------

ABP_REGISTER_SELFTEST("tiers.recipes_registered", [](Context& ctx) {
    auto crafting = recipesFor("crafting_table");
    ctx.require(crafting != nullptr, "no crafting_table recipes");
    auto expect = [&](std::string const& id, std::string const& result, bool unlockable) {
        auto it = crafting->find(id);
        if (it == crafting->end() || !it->second) {
            ctx.check(false, "recipe " + id + " missing");
            return;
        }
        ctx.check(resultId(*it->second) == result, id + " result = " + resultId(*it->second));
        if (unlockable) ctx.check(!it->second->mUnlockingRequirement->mIngredients->empty(), id + " has no unlock requirement");
    };
    expect("alleviatedbackpacks:leather_backpack", "alleviatedbackpacks:leather_backpack", true);
    expect("alleviatedbackpacks:copper_backpack", "alleviatedbackpacks:copper_backpack", true);
    expect("alleviatedbackpacks:iron_backpack", "alleviatedbackpacks:iron_backpack", true);
    expect("alleviatedbackpacks:iron_backpack_from_copper", "alleviatedbackpacks:iron_backpack", true);
    expect("alleviatedbackpacks:gold_backpack", "alleviatedbackpacks:gold_backpack", true);
    expect("alleviatedbackpacks:diamond_backpack", "alleviatedbackpacks:diamond_backpack", true);
    // SB: Netherite only at a smithing table.
    ctx.check(!crafting->contains("alleviatedbackpacks:netherite_backpack"), "no crafting-grid netherite recipe");
    int dyes = 0;
    for (auto const& info : core::kTiers) {
        for (int n = 1; n <= 8; ++n) {
            auto id = "alleviatedbackpacks:" + std::string(info.id) + "_backpack_dye_" + std::to_string(n);
            if (crafting->contains(id)) ++dyes;
        }
    }
    ctx.check(dyes == 48, "dye recipes: " + std::to_string(dyes) + "/48");
    // Recipes SB has that phase 1 missed.
    expect("alleviatedbackpacks:smoking_upgrade_from_smelting_upgrade", "alleviatedbackpacks:smoking_upgrade", true);
    expect("alleviatedbackpacks:blasting_upgrade_from_smelting_upgrade", "alleviatedbackpacks:blasting_upgrade", true);
    expect("alleviatedbackpacks:auto_smoking_upgrade_from_auto_smelting_upgrade", "alleviatedbackpacks:auto_smoking_upgrade", true);
    expect("alleviatedbackpacks:auto_blasting_upgrade_from_auto_smelting_upgrade", "alleviatedbackpacks:auto_blasting_upgrade", true);
    expect("alleviatedbackpacks:xp_pump_upgrade", "alleviatedbackpacks:xp_pump_upgrade", true);
    auto smithing = recipesFor("smithing_table");
    ctx.require(smithing != nullptr, "no smithing_table recipes");
    auto it = smithing->find("alleviatedbackpacks:netherite_backpack");
    ctx.check(it != smithing->end() && it->second && resultId(*it->second) == "alleviatedbackpacks:netherite_backpack", "netherite smithing recipe");
});

ABP_REGISTER_SELFTEST("tiers.recipes_match", [](Context& ctx) {
    auto bp      = ctx.createBackpack(Tier::Leather);
    auto leather = ctx.backpackItem(*bp); // carries a uuid: recipes must still match
    std::array<ItemStack, 9> grid;
    grid.fill(game::ItemConv::make("minecraft:copper_ingot"));
    grid[4] = leather;
    ctx.check(craftResult(grid) == "alleviatedbackpacks:copper_backpack", "copper upgrade: " + craftResult(grid));
    grid.fill(game::ItemConv::make("minecraft:iron_ingot"));
    grid[4] = leather;
    ctx.check(craftResult(grid) == "alleviatedbackpacks:iron_backpack", "iron around leather: " + craftResult(grid));

    struct Step {
        Tier             from;
        std::string_view material;
        std::string_view result;
    };
    for (auto const& s : {Step{Tier::Iron, "minecraft:gold_ingot", "alleviatedbackpacks:gold_backpack"},
                          Step{Tier::Gold, "minecraft:diamond", "alleviatedbackpacks:diamond_backpack"}}) {
        grid.fill(game::ItemConv::make(s.material));
        grid[4] = game::makeBackpackItem(s.from, bp->uuid());
        ctx.check(craftResult(grid) == s.result, std::string(s.result) + ": " + craftResult(grid));
    }
    // Iron from Copper: " I /IBI/ I ".
    std::array<ItemStack, 9> plus{};
    for (int i : {1, 3, 5, 7}) plus[static_cast<std::size_t>(i)] = game::ItemConv::make("minecraft:iron_ingot");
    plus[4] = game::makeBackpackItem(Tier::Copper, bp->uuid());
    ctx.check(craftResult(plus) == "alleviatedbackpacks:iron_backpack", "iron from copper: " + craftResult(plus));
    // Wrong material: no backpack recipe.
    grid.fill(game::ItemConv::make("minecraft:gold_ingot"));
    grid[4] = leather;
    ctx.check(!craftResult(grid).ends_with("_backpack"), "gold around leather must not craft a backpack");

    // Netherite: not in a crafting grid.
    std::array<ItemStack, 9> shapeless{};
    shapeless[0] = game::ItemConv::make("minecraft:netherite_upgrade_smithing_template");
    shapeless[4] = game::makeBackpackItem(Tier::Diamond, bp->uuid());
    shapeless[8] = game::ItemConv::make("minecraft:netherite_ingot");
    ctx.check(!craftResult(shapeless).ends_with("_backpack"), "netherite in a grid: " + craftResult(shapeless));

    // Dyeing: any tier + 1..8 dyes (the "alleviatedbackpacks:dye" tag) -> same tier.
    std::array<ItemStack, 9> dye{};
    dye[3] = game::ItemConv::make("minecraft:red_dye");
    dye[4] = game::makeBackpackItem(Tier::Gold, bp->uuid());
    ctx.check(craftResult(dye) == "alleviatedbackpacks:gold_backpack", "one dye: " + craftResult(dye));
    dye[0] = game::ItemConv::make("minecraft:lime_dye");
    dye[8] = game::ItemConv::make("minecraft:black_dye");
    ctx.check(craftResult(dye) == "alleviatedbackpacks:gold_backpack", "three dyes: " + craftResult(dye));
    std::array<ItemStack, 9> eight{};
    eight.fill(game::ItemConv::make("minecraft:white_dye"));
    eight[2] = game::makeBackpackItem(Tier::Netherite, bp->uuid());
    ctx.check(craftResult(eight) == "alleviatedbackpacks:netherite_backpack", "eight dyes: " + craftResult(eight));
    dye[8] = game::ItemConv::make("minecraft:stone");
    ctx.check(!craftResult(dye).ends_with("_backpack"), "a non-dye blocks dyeing: " + craftResult(dye));
    std::array<ItemStack, 9> noDye{};
    noDye[4] = game::makeBackpackItem(Tier::Gold, bp->uuid());
    noDye[5] = game::ItemConv::make("minecraft:ink_sac");
    ctx.check(!craftResult(noDye).ends_with("_backpack"), "ink sac is not one of the 16 dyes: " + craftResult(noDye));
});

ABP_REGISTER_SELFTEST("tiers.next_tier_upgrades", [](Context& ctx) {
    ctx.check(isNextTierUpgrade("alleviatedbackpacks:pickup_upgrade", "alleviatedbackpacks:advanced_pickup_upgrade"), "pickup -> advanced");
    ctx.check(isNextTierUpgrade("alleviatedbackpacks:pickup_upgrade", "alleviatedbackpacks:magnet_upgrade"), "pickup -> magnet");
    ctx.check(isNextTierUpgrade("alleviatedbackpacks:smelting_upgrade", "alleviatedbackpacks:auto_smelting_upgrade"), "smelting -> auto");
    ctx.check(!isNextTierUpgrade("alleviatedbackpacks:smelting_upgrade", "alleviatedbackpacks:blasting_upgrade"), "smelting -> blasting is a plain recipe");
    ctx.check(!isNextTierUpgrade("alleviatedbackpacks:advanced_pump_upgrade", "alleviatedbackpacks:xp_pump_upgrade"), "xp pump is a plain recipe");
    ctx.check(!isNextTierUpgrade("minecraft:pickup_upgrade", "alleviatedbackpacks:advanced_pickup_upgrade"), "namespace checked");
});
// ---- crafting hooks --------------------------------------------------------------------------

ABP_REGISTER_SELFTEST("tiers.upgrade_keeps_contents", [](Context& ctx) {
    auto& storage = game::Storage::get();
    auto  bp      = ctx.createBackpack(Tier::Leather);
    bp->inventory().insert(data("minecraft:diamond", 40), kInternal);
    bp->inventory().set(26, data("minecraft:oak_log", 7));
    bp->data().settings["tiers_test"] = 1;
    bp->markDirty();
    auto source = ctx.backpackItem(*bp);
    game::ItemConv::setDisplay(source, "My Pack");

    Tier                       tiers[] = {Tier::Copper, Tier::Iron, Tier::Gold, Tier::Diamond, Tier::Netherite};
    std::string_view           mats[]  = {"minecraft:copper_ingot", "minecraft:iron_ingot", "minecraft:gold_ingot", "minecraft:diamond", "minecraft:netherite_ingot"};
    for (int step = 0; step < 5; ++step) {
        std::vector<ConsumedInput> inputs;
        for (int i = 0; i < 9; ++i) inputs.push_back(input(i, i == 4 ? source : game::ItemConv::make(mats[step])));
        auto craft = analyse(inputs);
        ctx.require(craft.has_value(), "analyse found the backpack");
        ctx.check(!craft->dyes(), "metal is not a dye");
        auto result = game::makeBackpackItem(tiers[step]);
        auto uuid   = carryOver(result, *craft);
        ctx.check(uuid == bp->uuid(), "uuid carried");
        ctx.check(game::backpackUuid(result) == bp->uuid(), "result bound to the same backpack");
        ctx.check(result.getCustomName() == "My Pack", "custom name kept");
        bool changed = false;
        auto again   = storage.forItem(result, &changed);
        ctx.require(again == bp, "same storage entry");
        auto const& info = core::tierInfo(tiers[step]);
        ctx.check(bp->tier() == tiers[step], std::string(info.id) + ": tier");
        ctx.check(bp->inventory().size() == info.slots, std::string(info.id) + ": slots " + std::to_string(bp->inventory().size()));
        ctx.check(static_cast<int>(bp->data().upgrades.size()) == info.upgradeSlots, std::string(info.id) + ": upgrade slots");
        source = result;
    }
    ctx.check(bp->inventory().at(0).count == 40 && bp->inventory().at(0).id == "minecraft:diamond", "diamonds kept");
    ctx.check(bp->inventory().at(26).count == 7, "slot 26 kept");
    ctx.check(bp->data().settings.value("tiers_test", 0) == 1, "settings kept");
});

ABP_REGISTER_SELFTEST("tiers.analyse_rules", [](Context& ctx) {
    auto a = ctx.createBackpack(Tier::Leather);
    auto b = ctx.createBackpack(Tier::Leather);
    // Two backpacks: not a backpack craft.
    ctx.check(!analyse({input(0, ctx.backpackItem(*a)), input(1, ctx.backpackItem(*b))}), "two backpacks ignored");
    ctx.check(!analyse({input(0, game::ItemConv::make("minecraft:stone"))}), "no backpack ignored");
    // 2x2 inventory grid positions map onto the 3x3 reading order.
    ctx.check(craftGridIndex(static_cast<int>(PlayerUISlot::Crafting2x2Input1)) == 0, "2x2 slot 1");
    ctx.check(craftGridIndex(static_cast<int>(PlayerUISlot::Crafting2x2Input4)) == 4, "2x2 slot 4");
    ctx.check(craftGridIndex(static_cast<int>(PlayerUISlot::Crafting3x3Input9)) == 8, "3x3 slot 9");
    ctx.check(craftGridIndex(static_cast<int>(PlayerUISlot::SmithingTableInput)) == -1, "smithing slot");
    // SB columns: left of the backpack = main, right = accent, same column = both.
    auto red   = game::ItemConv::make("minecraft:red_dye");
    auto blue  = game::ItemConv::make("minecraft:blue_dye");
    auto lime  = game::ItemConv::make("minecraft:lime_dye");
    auto argb  = [](core::DyeColor d) { return core::dyeInfo(d).argb; };
    auto c     = analyse({input(3, red), input(4, ctx.backpackItem(*a)), input(5, blue), input(1, lime)});
    ctx.require(c.has_value(), "dye craft");
    ctx.check(c->mainDyes == std::vector<std::uint32_t>{argb(core::DyeColor::Red), argb(core::DyeColor::Lime)}, "main = red (left) + lime (same column)");
    ctx.check(c->accentDyes == std::vector<std::uint32_t>{argb(core::DyeColor::Blue), argb(core::DyeColor::Lime)}, "accent = blue (right) + lime (same column)");
    // Rows don't matter: a dye below-left of the backpack is still "left".
    auto low = analyse({input(1, ctx.backpackItem(*a)), input(6, red)});
    ctx.check(low && low->mainDyes.size() == 1 && low->accentDyes.empty(), "below-left = main");
    // 2x2 grid: backpack top right, dye bottom left -> main.
    auto inv = analyse(
        {{static_cast<int>(PlayerUISlot::Crafting2x2Input2), std::make_shared<ItemStack>(ctx.backpackItem(*a))},
         {static_cast<int>(PlayerUISlot::Crafting2x2Input3), std::make_shared<ItemStack>(red)}}
    );
    ctx.check(inv && inv->mainDyes.size() == 1 && inv->accentDyes.empty(), "2x2: dye left of the backpack = main");
    // Consumed from outside the grid (recipe book auto-craft from the inventory): both colours.
    ConsumedInput fromInv{static_cast<int>(PlayerUISlot::Crafting3x3Input2), std::make_shared<ItemStack>(blue), false};
    auto          outside = analyse({input(4, ctx.backpackItem(*a)), fromInv});
    ctx.check(outside && outside->mainDyes.size() == 1 && outside->accentDyes.size() == 1, "dye outside the grid = both");
});

ABP_REGISTER_SELFTEST("tiers.dye_colors", [](Context& ctx) {
    auto bp   = ctx.createBackpack(Tier::Iron);
    auto red  = game::ItemConv::make("minecraft:red_dye");
    auto lime = game::ItemConv::make("minecraft:lime_dye");
    auto blue = game::ItemConv::make("minecraft:blue_dye");
    auto argb = [](core::DyeColor d) { return core::dyeInfo(d).argb; };
    // Red left of the backpack: main red, accent unchanged (default).
    auto craft = analyse({input(3, red), input(4, ctx.backpackItem(*bp))});
    ctx.require(craft.has_value(), "dye craft");
    auto result = game::makeBackpackItem(Tier::Iron);
    auto uuid   = carryOver(result, *craft);
    ctx.check(uuid == bp->uuid(), "uuid carried");
    auto colors = itemColors(result);
    ctx.require(colors.has_value(), "colours stored on the item");
    ctx.check(colors->main == argb(core::DyeColor::Red), "item main red");
    ctx.check(colors->accent == core::kDefaultAccentColor, "item accent default");
    ctx.check(result.mItem->hasCustomColor(result), "result tinted");
    ctx.require(commitColors(uuid, *craft), "commit");
    ctx.check(bp->data().mainColor == argb(core::DyeColor::Red), "storage main red");
    ctx.check(bp->data().accentColor == core::kDefaultAccentColor, "storage accent unchanged");
    // Lime right of the (red) backpack: accent lime; main stays red.
    craft = analyse({input(4, result), input(5, lime)});
    ctx.require(craft.has_value(), "second dye");
    auto second = game::makeBackpackItem(Tier::Iron);
    carryOver(second, *craft);
    ctx.check(itemColors(second) == BackpackColors{argb(core::DyeColor::Red), argb(core::DyeColor::Lime)}, "red / lime");
    // Blue in the same column re-dyes both, mixed with the current (non-default) colours.
    craft = analyse({input(1, blue), input(4, second)});
    ctx.require(craft.has_value(), "third dye");
    auto third = game::makeBackpackItem(Tier::Iron);
    carryOver(third, *craft);
    std::uint32_t const blueOnly[] = {argb(core::DyeColor::Blue)};
    auto expected = BackpackColors{
        core::calculateColor(argb(core::DyeColor::Red), core::kDefaultMainColor, blueOnly),
        core::calculateColor(argb(core::DyeColor::Lime), core::kDefaultAccentColor, blueOnly),
    };
    ctx.check(itemColors(third) == expected, "mixed with the current colours");
    // Default colours are not mixed in (SC calculateColor): one dye on a fresh backpack = the dye.
    auto fresh = game::makeBackpackItem(Tier::Leather);
    auto c2    = analyse({input(0, red), input(1, fresh)});
    ctx.require(c2.has_value(), "fresh dye craft");
    auto r2 = game::makeBackpackItem(Tier::Leather);
    ctx.check(carryOver(r2, *c2).empty(), "never-opened backpack stays unbound");
    ctx.check(itemColors(r2) && itemColors(r2)->main == argb(core::DyeColor::Red), "fresh backpack dyed on the item");
    ctx.check(!commitColors("", *c2), "nothing to commit without a uuid");
    // The storage picks the item colours up once bound.
    bool changed = false;
    auto bound   = game::Storage::get().forItem(r2, &changed);
    ctx.require(bound != nullptr, "bind");
    refreshItem(ctx.player(), r2, bound);
    ctx.check(bound->data().mainColor == argb(core::DyeColor::Red), "storage mirrors the item colour");
    game::Storage::get().erase(bound->uuid());
});

ABP_REGISTER_SELFTEST("tiers.creative_variants", [](Context& ctx) {
    auto variants = creativeColorVariants();
    ctx.require(variants.size() == 17, "16 dyes + 1 mix");
    ctx.check(variants[14].main == core::dyeInfo(core::DyeColor::Red).argb && variants[14].accent == variants[14].main, "red variant");
    ctx.check(variants[16] == BackpackColors{0xFFD0E232u, 0xFF2C3065u}, "yellow+lime / blue+black");
    auto stacks = creativeBackpackVariants();
    ctx.require(stacks.size() == 17, "17 stacks");
    ctx.check(game::ItemConv::idOf(stacks[0]) == game::backpackItemId(Tier::Leather), "leather backpacks");
    ctx.check(itemColors(stacks[0]) == variants[0], "colours on the item");
    ctx.check(!game::backpackUuid(stacks[0]), "unbound");
    // In the world's creative list, right after the plain backpack.
    auto level = ll::service::getLevel();
    ctx.require(level.has_value(), "level");
    auto registry = level->getItemRegistry().getCreativeItemRegistry();
    ctx.require(registry, "creative registry");
    int  found = 0;
    for (auto const& entry : *registry->mCreativeItems) {
        auto const& item = *entry.mItemInstance;
        if (item.getTypeName() == game::backpackItemId(Tier::Leather) && itemColors(item)) ++found;
    }
    ctx.check(found == 17, "dyed backpacks in the creative list: " + std::to_string(found));
});
// ---- presentation ------------------------------------------------------------------------------

ABP_REGISTER_SELFTEST("tiers.lore_and_tint", [](Context& ctx) {
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(Tier::Leather);
    bp->inventory().insert(data("minecraft:stone", 64), kInternal);
    bp->inventory().insert(data("minecraft:dirt", 10), kInternal);
    auto item = ctx.backpackItem(*bp);
    ctx.check(refreshItem(player, item, bp), "first refresh changes the item");
    ctx.check(!refreshItem(player, item, bp), "second refresh is a no-op");
    auto text = loreText(item);
    ctx.check(text.find("2/27") != std::string::npos, "used/total slots in lore: " + text);
    ctx.check(text.find("4%") != std::string::npos, "fill % in lore (74/1728 -> 4%): " + text);
    ctx.check(text.find("0/1") != std::string::npos, "upgrade slots in lore");
    ctx.check(item.mItem->hasCustomColor(item), "default colour: tinted with SB's default main colour (model cloth)");

    // Colours: tint only (SB shows colours on the model, not in the tooltip).
    bp->data().mainColor   = core::dyeInfo(core::DyeColor::Blue).argb;
    bp->data().accentColor = core::dyeInfo(core::DyeColor::Yellow).argb;
    bp->markDirty();
    ctx.check(refreshItem(player, item, bp), "colour change refreshes");
    ctx.check(itemColors(item) == BackpackColors{core::dyeInfo(core::DyeColor::Blue).argb, core::dyeInfo(core::DyeColor::Yellow).argb}, "colours copied onto the item");
    text = loreText(item);
    ctx.check(text.find(colorLabel(player, core::dyeInfo(core::DyeColor::Blue).argb)) == std::string::npos, "no colour lore line");
    ctx.check(item.mItem->hasCustomColor(item), "tinted");
    auto c = item.getColor();
    ctx.check(std::abs(c.b - (0xAA / 255.0f)) < 0.01f && std::abs(c.r - (0x3C / 255.0f)) < 0.01f, "tint = blue");
    ctx.check(!refreshItem(player, item, bp), "stable after tint");

    // A cauldron dye on the item is adopted as the new main colour; a wash resets it.
    item.mItem->setColor(item, mce::Color(0x00FF00u));
    ctx.check(refreshItem(player, item, bp), "cauldron dye detected");
    ctx.check((bp->data().mainColor & 0xFFFFFF) == 0x00FF00u, "cauldron colour adopted");
    item.mItem->clearColor(item);
    refreshItem(player, item, bp);
    ctx.check(bp->data().mainColor == core::kDefaultMainColor, "wash resets main colour");
    ctx.check(itemColors(item) && itemColors(item)->main == core::kDefaultMainColor, "item main colour reset too");
    // Dyed in a cauldron before we ever touched the item: adopted too.
    auto bp2  = ctx.createBackpack(Tier::Leather);
    auto dyed = ctx.backpackItem(*bp2);
    dyed.mItem->setColor(dyed, mce::Color(0xFF0000u));
    refreshItem(player, dyed, bp2);
    ctx.check((bp2->data().mainColor & 0xFFFFFFu) == 0xFF0000u, "first-seen cauldron tint adopted");

    // A never-opened backpack shows its tier capacity.
    auto fresh = game::makeBackpackItem(Tier::Gold);
    ctx.check(refreshItem(player, fresh, nullptr), "fresh item gets lore");
    ctx.check(loreText(fresh).find("0/81") != std::string::npos, "gold capacity: " + loreText(fresh));
    ctx.check(!game::backpackUuid(fresh), "lore doesn't bind a uuid");

    // Diamond backpacks are damageable (smithing-table base) but never wear out.
    auto diamond = game::makeBackpackItem(Tier::Diamond);
    ctx.require(diamond.isDamageableItem(), "diamond backpack has durability (smithing base)");
    diamond.setDamageValue(5);
    refreshItem(player, diamond, nullptr);
    ctx.check(diamond.getDamageValue() == 0, "damage reset");
});

ABP_REGISTER_SELFTEST("tiers.lore_refresh_carried", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(Tier::Copper);
    ctx.require(ctx.give(player, ctx.backpackItem(*bp), 0) == 0, "give backpack");
    co_await ctx.ticks(25);
    auto text = loreText(player.getInventory().getItem(0));
    ctx.check(text.find("0/45") != std::string::npos, "carried backpack gets lore: " + text);

    // While the backpack is open the item is left alone; it refreshes after closing. (Driven
    // directly: a simulated player may drift away from the fake chest over many ticks.)
    ctx.require(ui::BackpackMenu::openFor(player, {game::ItemLocation::Kind::Inventory, 0}), "open");
    mm.forceShown(player);
    bp->inventory().insert(data("minecraft:emerald", 5), kInternal);
    ctx.check(refreshPlayer(player) == 0, "nothing rewritten while open");
    ctx.check(loreText(player.getInventory().getItem(0)).find("0/45") != std::string::npos, "lore unchanged while open");
    mm.close(player);
    ctx.check(refreshPlayer(player) == 1, "rewritten after close");
    text = loreText(player.getInventory().getItem(0));
    ctx.check(text.find("1/45") != std::string::npos, "refreshed after close: " + text);
    ctx.check(refreshPlayer(player) == 0, "stable");
    ctx.check(game::backpackUuid(player.getInventory().getItem(0)) == bp->uuid(), "uuid intact");

    // Changes while carried (e.g. pickup) refresh too.
    bp->inventory().insert(data("minecraft:cobblestone", 64), kInternal);
    co_await ctx.ticks(25);
    text = loreText(player.getInventory().getItem(0));
    ctx.check(text.find("2/45") != std::string::npos, "refreshed on change: " + text);
});

} // namespace
} // namespace abp::tiers

// e2e helpers (client scenario `recipes`):
// - `/backpack tiers setup`: a leather backpack holding 20 diamonds with a Stack Upgrade Starter
//   Tier installed, into the player's inventory.
// - `/backpack tiers info`: logs the selected backpack's uuid, tier, item count, colours and upgrades.
ABP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    cmd.overload().text("tiers").text("setup").execute([](CommandOrigin const& origin, CommandOutput& output) {
        using namespace abp;
        if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors) {
            output.error("operators only");
            return;
        }
        auto* e = origin.getEntity();
        if (!e || !e->isPlayer() || !game::Storage::get().available()) {
            output.error("players only");
            return;
        }
        auto& player = static_cast<Player&>(*e);
        auto  bp     = game::Storage::get().create(core::Tier::Leather);
        if (!bp) {
            output.error("could not create a backpack");
            return;
        }
        (void)upgrade::install(bp, 0, game::ItemConv::toData(game::ItemConv::make("alleviatedbackpacks:stack_starter_upgrade", 1)), &player);
        bp->inventory().set(0, game::ItemConv::toData(game::ItemConv::make("minecraft:diamond", 20)));
        bp->markDirty();
        game::Storage::get().save(*bp, true);
        auto stack = game::makeBackpackItem(core::Tier::Leather, bp->uuid());
        if (!player.add(stack)) player.drop(stack, false);
        player.sendInventory(false);
        log().info("tiers setup: gave leather backpack {}", bp->uuid());
        output.success("tiers setup: gave leather backpack " + bp->uuid());
    });
    cmd.overload().text("tiers").text("info").execute([](CommandOrigin const& origin, CommandOutput& output) {
        using namespace abp;
        auto* e = origin.getEntity();
        if (!e || !e->isPlayer()) {
            output.error("players only");
            return;
        }
        auto& player = static_cast<Player&>(*e);
        auto  item   = player.getSelectedItem();
        auto  tier   = game::backpackTierOf(item);
        if (!tier) {
            output.error("not holding a backpack");
            return;
        }
        auto uuid = game::backpackUuid(item);
        auto bp   = uuid ? game::Storage::get().find(*uuid) : nullptr;
        auto c    = tiers::colorsOf(item, bp.get());
        std::string ups;
        long long   items = 0;
        if (bp) {
            for (auto const& s : bp->data().upgrades) ups += (ups.empty() ? "" : ",") + (s.empty() ? std::string("-") : s.item.id);
            items = bp->inventory().count([](core::ItemStackData const&) { return true; });
        }
        auto line = std::format(
            "tiers info: uuid={} tier={} items={} main={} accent={} upgrades=[{}]",
            uuid.value_or("-"),
            core::tierInfo(*tier).id,
            items,
            core::colorHex(c.main),
            core::colorHex(c.accent),
            ups
        );
        log().info("{}", line);
        output.success(line);
    });
});
