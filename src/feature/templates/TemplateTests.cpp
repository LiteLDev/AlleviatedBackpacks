// Selftests of backpack templates (SB BackpackTemplates / DynamicCommand) and template packs.

#include <filesystem>

#include "mc/world/item/ItemStack.h"

#include "feature/gui/Gui.h"
#include "feature/templates/Templates.h"
#include "game/Backpack.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/Storage.h"
#include "mod/I18n.h"
#include "selftest/SelfTest.h"

namespace abp::feature::templates {
namespace {
using selftest::Context;
using core::Tier;

core::ItemStackData item(std::string_view id, int count) { return game::ItemConv::toData(game::ItemConv::make(id, count)); }

/// Deletes backpacks created through Storage directly (not by ctx.createBackpack).
struct EraseGuard {
    std::vector<std::string> uuids;
    ~EraseGuard() {
        for (auto const& u : uuids) game::Storage::get().erase(u);
    }
};

ABP_REGISTER_SELFTEST("templates.create_give_fill", [](Context& ctx) {
    EraseGuard guard;
    auto       src = ctx.createBackpack(Tier::Gold);
    src->inventory().set(3, item("minecraft:emerald", 7));
    feature::gui::setNoSort(*src, 3, true);
    ctx.installUpgrade(src, "pickup");
    setBackpackTemplate("selftest:kit", game::backpackItemId(Tier::Gold), backpackContents(*src));
    ctx.check(localBackpackTemplate("selftest:kit").has_value(), "local template stored");
    auto names = backpackTemplateNames(false);
    ctx.check(std::find(names.begin(), names.end(), "selftest:kit") != names.end(), "listed");
    ctx.check(normalizeTemplateName("kit") == "minecraft:kit", "no namespace -> minecraft");
    ctx.check(invalidTemplateNameCharacters("selftest:Kit!") == "K!", "invalid characters reported");

    auto stack = makeTemplateBackpack("selftest:kit");
    ctx.require(stack.has_value(), "template backpack item");
    ctx.check(game::backpackTierOf(*stack) == Tier::Gold, "the template's backpack item");
    ctx.check(pendingTemplate(*stack) == std::optional<std::string>("selftest:kit"), "filled lazily (template name on the item)");
    ctx.check(!game::backpackUuid(*stack).has_value(), "no storage yet");

    bool changed = false;
    auto bp      = game::Storage::get().forItem(*stack, &changed);
    ctx.require(bp != nullptr, "first use creates the storage");
    guard.uuids.push_back(bp->uuid());
    ctx.check(changed && game::backpackUuid(*stack) == bp->uuid(), "uuid written");
    ctx.check(!pendingTemplate(*stack).has_value(), "template name removed after filling");
    ctx.check(bp->inventory().at(3).id == "minecraft:emerald" && bp->inventory().at(3).count == 7, "contents copied");
    ctx.check(feature::gui::noSort(*bp)[3], "settings copied");
    bool hasPickup = std::any_of(bp->data().upgrades.begin(), bp->data().upgrades.end(), [](core::UpgradeSlot const& s) {
        return s.item.id.find("pickup") != std::string::npos;
    });
    ctx.check(hasPickup, "upgrades copied");
    ctx.check(bp->uuid() != src->uuid(), "a new backpack, not the source");

    removeBackpackTemplate("selftest:kit");
    ctx.check(!localBackpackTemplate("selftest:kit").has_value(), "deleted");
    ctx.check(!makeTemplateBackpack("selftest:kit").has_value(), "unknown template");
});

ABP_REGISTER_SELFTEST("templates.export_pack", [](Context& ctx) {
    auto src = ctx.createBackpack(Tier::Leather);
    src->inventory().set(0, item("minecraft:apple", 3));
    setBackpackTemplate("selftesttpl:food/basic", game::backpackItemId(Tier::Leather), backpackContents(*src));
    auto path = exportBackpackTemplate("selftesttpl:food/basic", *localBackpackTemplate("selftesttpl:food/basic"));
    ctx.require(path.has_value(), "exported");
    ctx.check(path->find("selftesttpl_backpack_templates/data/selftesttpl/sophisticatedbackpacks_templates/food/basic.json") != std::string::npos,
              "SB data pack layout: " + *path);
    removeBackpackTemplate("selftesttpl:food/basic");
    reloadTemplatePacks();
    auto packs = packBackpackTemplateNames();
    ctx.check(std::find(packs.begin(), packs.end(), "selftesttpl:food/basic") != packs.end(), "read back from the template packs folder");
    ctx.check(backpackTemplate("selftesttpl:food/basic").has_value(), "pack templates resolve when there is no local one");
    std::error_code ec;
    std::filesystem::remove_all(templatePacksRoot() / "selftesttpl_backpack_templates", ec);
    reloadTemplatePacks();
    ctx.check(!backpackTemplate("selftesttpl:food/basic").has_value(), "gone after removing the pack");
});

ABP_REGISTER_SELFTEST("templates.dynamic", [](Context& ctx) {
    EraseGuard guard;
    ctx.check(dynamicBegin("st_dyn", Tier::Copper).ok, "begin");
    ctx.check(!dynamicBegin("st_dyn", Tier::Copper).ok, "name in use");
    ctx.check(dynamicAdd("st_dyn", 2, item("minecraft:stone", 10), false).key == "abp.templates.dynamic.add.success", "add into slot 2");
    ctx.check(dynamicAdd("st_dyn", 2, item("minecraft:dirt", 4), false).key == "abp.templates.dynamic.add.delayed", "occupied slot: delayed");
    ctx.check(dynamicAdd("st_dyn", -1, item("minecraft:apple", 64), false).key == "abp.templates.dynamic.add.delayed", "no slot: delayed");
    auto pickup = game::ItemConv::toData(game::ItemConv::make("alleviatedbackpacks:pickup_upgrade", 1));
    ctx.check(dynamicAdd("st_dyn", -1, pickup, true).ok, "upgrade queued");
    ctx.check(!dynamicAdd("nope", -1, item("minecraft:apple", 1), false).ok, "not in use");
    auto end = dynamicEnd("st_dyn");
    ctx.require(end.result.ok && end.item.has_value(), "end: " + end.result.text);
    auto uuid = game::backpackUuid(*end.item);
    ctx.require(uuid.has_value(), "a real backpack item");
    guard.uuids.push_back(*uuid);
    auto bp = game::Storage::get().find(*uuid);
    ctx.require(bp != nullptr, "stored");
    ctx.check(bp->inventory().at(2).id == "minecraft:stone", "slot item");
    ctx.check(bp->inventory().count([](core::ItemStackData const& d) { return d.id == "minecraft:dirt"; }) == 4, "delayed items inserted");
    ctx.check(!bp->data().upgrades[0].empty(), "upgrade installed");
    ctx.check(!dynamicInUse("st_dyn"), "ended templates leave the cache");

    // Leftovers carry SB's two message arguments (`Remaining %d x %s`).
    ctx.check(dynamicBegin("st_left", Tier::Leather).ok, "begin leftovers");
    (void)dynamicAdd("st_left", -1, pickup, true);
    (void)dynamicAdd("st_left", -1, game::ItemConv::toData(game::ItemConv::make("alleviatedbackpacks:magnet_upgrade", 1)), true);
    auto left = dynamicEnd("st_left");
    if (left.item) {
        if (auto u = game::backpackUuid(*left.item)) guard.uuids.push_back(*u);
    }
    ctx.require(left.leftovers.size() == 1, "one upgrade left over (one upgrade slot)");
    ctx.check(left.leftovers[0].count == 1 && left.leftovers[0].name.starts_with("[") && left.leftovers[0].name.ends_with("]"), "count and [name]");
    auto msg = tr("abp.templates.dynamic.end.addRemainingItemsFailed", "Not all items could be added: Remaining {0} x {1}", left.leftovers[0].count, left.leftovers[0].name);
    ctx.check(msg == "Not all items could be added: Remaining 1 x " + left.leftovers[0].name, "SB message: " + msg);

    // Empty templates are refused (and stay in the cache, as in SB).
    ctx.check(dynamicBegin("st_empty", Tier::Leather).ok, "begin empty");
    ctx.check(!dynamicEnd("st_empty").result.ok, "Backpack is empty");
    ctx.check(dynamicInUse("st_empty"), "still in use");
    // 5 minutes without access expire it.
    dynamicAdvanceClock(299);
    ctx.check(dynamicInUse("st_empty"), "accessed: still there after 299 s");
    dynamicAdvanceClock(300);
    ctx.check(!dynamicInUse("st_empty"), "expired after 5 minutes without access");
});

} // namespace
} // namespace abp::feature::templates
