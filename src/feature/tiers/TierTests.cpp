// U1 selftests: tiers.*  (`backpack selftest "tiers.*"`)

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

#include "ll/api/service/Bedrock.h"

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
#include "mc/world/level/Level.h"

#include "core/Appearance.h"
#include "feature/tiers/Tiers.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "game/Storage.h"
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
    expect("alleviatedbackpacks:gold_backpack", "alleviatedbackpacks:gold_backpack", true);
    expect("alleviatedbackpacks:diamond_backpack", "alleviatedbackpacks:diamond_backpack", true);
    expect("alleviatedbackpacks:netherite_backpack", "alleviatedbackpacks:netherite_backpack", true);
    int dyes = 0;
    for (auto const& info : core::kTiers) {
        for (auto const& dye : core::kDyes) {
            auto id = "alleviatedbackpacks:" + std::string(info.id) + "_backpack_dye_" + std::string(dye.id);
            if (crafting->contains(id)) ++dyes;
        }
    }
    ctx.check(dyes == 96, "dye recipes: " + std::to_string(dyes) + "/96");
    auto smithing = recipesFor("smithing_table");
    ctx.require(smithing != nullptr, "no smithing_table recipes");
    auto it = smithing->find("alleviatedbackpacks:netherite_backpack_smithing");
    ctx.check(it != smithing->end() && it->second && resultId(*it->second) == "alleviatedbackpacks:netherite_backpack", "netherite smithing recipe");
});

ABP_REGISTER_SELFTEST("tiers.recipes_match", [](Context& ctx) {
    auto bp      = ctx.createBackpack(Tier::Leather);
    auto leather = ctx.backpackItem(*bp); // carries a uuid: recipes must still match
    auto copper  = game::ItemConv::make("minecraft:copper_ingot");
    std::array<ItemStack, 9> grid;
    grid.fill(copper);
    grid[4] = leather;
    ctx.check(craftResult(grid) == "alleviatedbackpacks:copper_backpack", "copper upgrade: " + craftResult(grid));

    struct Step {
        Tier             from;
        std::string_view material;
        std::string_view result;
    };
    for (auto const& s : {Step{Tier::Copper, "minecraft:iron_ingot", "alleviatedbackpacks:iron_backpack"},
                          Step{Tier::Iron, "minecraft:gold_ingot", "alleviatedbackpacks:gold_backpack"},
                          Step{Tier::Gold, "minecraft:diamond", "alleviatedbackpacks:diamond_backpack"}}) {
        grid.fill(game::ItemConv::make(s.material));
        grid[4] = game::makeBackpackItem(s.from, bp->uuid());
        ctx.check(craftResult(grid) == s.result, std::string(s.result) + ": " + craftResult(grid));
    }
    // Wrong material: no backpack recipe.
    grid.fill(game::ItemConv::make("minecraft:gold_ingot"));
    grid[4] = leather;
    ctx.check(!craftResult(grid).ends_with("_backpack"), "gold around leather must not craft a backpack");

    // Netherite in a crafting grid (shapeless).
    std::array<ItemStack, 9> shapeless{};
    shapeless[0] = game::ItemConv::make("minecraft:netherite_upgrade_smithing_template");
    shapeless[4] = game::makeBackpackItem(Tier::Diamond, bp->uuid());
    shapeless[8] = game::ItemConv::make("minecraft:netherite_ingot");
    ctx.check(craftResult(shapeless) == "alleviatedbackpacks:netherite_backpack", "netherite: " + craftResult(shapeless));

    // Dyeing: any tier + one dye -> same tier.
    std::array<ItemStack, 9> dye{};
    dye[3] = game::ItemConv::make("minecraft:red_dye");
    dye[4] = game::makeBackpackItem(Tier::Gold, bp->uuid());
    ctx.check(craftResult(dye) == "alleviatedbackpacks:gold_backpack", "dye gold: " + craftResult(dye));
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
    // Dyes: same row (left/right) = main, other rows (above/below) = accent; several are mixed.
    auto red  = game::ItemConv::make("minecraft:red_dye");
    auto blue = game::ItemConv::make("minecraft:blue_dye");
    auto c    = analyse({input(3, red), input(4, ctx.backpackItem(*a)), input(7, blue)});
    ctx.require(c.has_value(), "dye craft");
    ctx.check(c->mainColor == core::dyeInfo(core::DyeColor::Red).argb, "main = red");
    ctx.check(c->accentColor == core::dyeInfo(core::DyeColor::Blue).argb, "accent = blue");
    auto two  = analyse({input(0, red), input(1, blue), input(2, ctx.backpackItem(*a))});
    std::vector<std::uint32_t> mix{core::dyeInfo(core::DyeColor::Red).argb, core::dyeInfo(core::DyeColor::Blue).argb};
    ctx.check(two && two->mainColor == core::mixColors(mix) && !two->accentColor, "two dyes in the row -> mixed main colour");
    // 2x2 grid: dye in slot 2 (top right) beside a backpack in slot 1 (top left) -> main; a dye
    // in slot 3 (below the backpack) -> accent.
    auto inv = analyse(
        {{static_cast<int>(PlayerUISlot::Crafting2x2Input1), std::make_shared<ItemStack>(ctx.backpackItem(*a))},
         {static_cast<int>(PlayerUISlot::Crafting2x2Input2), std::make_shared<ItemStack>(red)},
         {static_cast<int>(PlayerUISlot::Crafting2x2Input3), std::make_shared<ItemStack>(blue)}}
    );
    ctx.check(inv && inv->mainColor == core::dyeInfo(core::DyeColor::Red).argb, "2x2: dye beside the backpack = main");
    ctx.check(inv && inv->accentColor == core::dyeInfo(core::DyeColor::Blue).argb, "2x2: dye below the backpack = accent");
    // Consumed from outside the grid (recipe book auto-craft from the inventory): main colour.
    ConsumedInput fromInv{static_cast<int>(PlayerUISlot::Crafting3x3Input2), std::make_shared<ItemStack>(blue), false};
    auto          outside = analyse({input(4, ctx.backpackItem(*a)), fromInv});
    ctx.check(outside && outside->mainColor && !outside->accentColor, "dye outside the grid = main");
});

ABP_REGISTER_SELFTEST("tiers.dye_colors", [](Context& ctx) {
    auto& storage = game::Storage::get();
    auto  bp      = ctx.createBackpack(Tier::Iron);
    auto  red     = game::ItemConv::make("minecraft:red_dye");
    auto  lime    = game::ItemConv::make("minecraft:lime_dye");
    auto  craft   = analyse({input(3, red), input(4, ctx.backpackItem(*bp))});
    ctx.require(craft.has_value(), "dye craft");
    auto result = game::makeBackpackItem(Tier::Iron);
    auto uuid   = carryOver(result, *craft);
    ctx.check(uuid == bp->uuid(), "uuid carried");
    ctx.require(commitColors(uuid, *craft), "commit");
    ctx.check(bp->data().mainColor == core::dyeInfo(core::DyeColor::Red).argb, "main red");
    ctx.check(bp->data().accentColor == core::BackpackData().accentColor, "accent unchanged");
    craft = analyse({input(4, result), input(7, lime)});
    ctx.require(craft && commitColors(uuid, *craft), "second dye");
    ctx.check(bp->data().mainColor == core::dyeInfo(core::DyeColor::Red).argb, "main still red");
    ctx.check(bp->data().accentColor == core::dyeInfo(core::DyeColor::Lime).argb, "accent lime");

    // A never-opened backpack gets a storage entry so the colour has somewhere to live.
    auto fresh = game::makeBackpackItem(Tier::Leather);
    auto c2    = analyse({input(0, red), input(1, fresh)});
    ctx.require(c2.has_value(), "fresh dye craft");
    auto r2 = game::makeBackpackItem(Tier::Leather);
    auto u2 = carryOver(r2, *c2);
    ctx.require(!u2.empty() && game::backpackUuid(r2) == u2, "fresh backpack bound on dyeing");
    ctx.check(carryOver(r2, *c2) == u2, "repeated carry-over reuses the entry");
    ctx.check(commitColors(u2, *c2), "commit fresh");
    auto stored = storage.find(u2);
    ctx.check(stored && stored->data().mainColor == core::dyeInfo(core::DyeColor::Red).argb, "fresh backpack dyed");
    storage.erase(u2);
});

// ---- presentation ------------------------------------------------------------------------------

ABP_REGISTER_SELFTEST("tiers.lore_and_tint", [](Context& ctx) {
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(Tier::Leather);
    bp->inventory().insert(data("minecraft:stone", 64), kInternal);
    bp->inventory().insert(data("minecraft:dirt", 10), kInternal);
    auto item = ctx.backpackItem(*bp);
    // SB has no backpack lore: the phase-1 lore is removed, other lore is kept.
    item.setCustomLore(buildLore(player, Tier::Leather, bp.get()));
    ctx.check(loreText(item).find("2/27") != std::string::npos, "legacy lore set up");
    ctx.check(refreshItem(player, item, bp), "first refresh changes the item");
    ctx.check(!refreshItem(player, item, bp), "second refresh is a no-op");
    ctx.check(loreText(item).empty(), "legacy lore removed: " + loreText(item));
    item.setCustomLore({"A custom line"});
    ctx.check(!refreshItem(player, item, bp) && loreText(item) == "A custom line\n", "foreign lore kept");
    item.clearCustomLore();
    ctx.check(!item.mItem->hasCustomColor(item), "default colour: no tint");

    // Colours: lore line + tint.
    bp->data().mainColor   = core::dyeInfo(core::DyeColor::Blue).argb;
    bp->data().accentColor = core::dyeInfo(core::DyeColor::Yellow).argb;
    bp->markDirty();
    ctx.check(refreshItem(player, item, bp), "colour change refreshes");
    ctx.check(loreText(item).empty(), "no colour lore (SB)");

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
    ctx.check(bp->data().mainColor == core::BackpackData().mainColor, "wash resets main colour");
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
    auto legacy  = ctx.backpackItem(*bp);
    legacy.setCustomLore(buildLore(player, Tier::Copper, bp.get()));
    ctx.require(ctx.give(player, legacy, 0) == 0, "give backpack with phase-1 lore");
    co_await ctx.ticks(25);
    auto text = loreText(player.getInventory().getItem(0));
    ctx.check(text.empty(), "carried backpack loses the phase-1 lore (SB has none): " + text);

    // While the backpack is open the item is left alone; it is cleaned after closing. (Driven
    // directly: a simulated player may drift away from the fake chest over many ticks.)
    ctx.require(ui::BackpackMenu::openFor(player, {game::ItemLocation::Kind::Inventory, 0}), "open");
    mm.forceShown(player);
    ItemStack again = player.getInventory().getItem(0);
    again.setCustomLore(buildLore(player, Tier::Copper, bp.get()));
    game::setItemAt(player, {game::ItemLocation::Kind::Inventory, 0}, again);
    ctx.check(refreshPlayer(player) == 0, "nothing rewritten while open");
    mm.close(player);
    ctx.check(refreshPlayer(player) == 1, "rewritten after close");
    ctx.check(loreText(player.getInventory().getItem(0)).empty(), "cleaned after close");
    ctx.check(refreshPlayer(player) == 0, "stable");
    ctx.check(game::backpackUuid(player.getInventory().getItem(0)) == bp->uuid(), "uuid intact");
});

} // namespace
} // namespace abp::tiers
