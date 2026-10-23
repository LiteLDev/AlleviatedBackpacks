// Selftests for the bundle rule (SB `canBePlacedInContainerItems`): bundle.*
// The engine's own storage item validation (`DynamicContainerValidation`, made by the
// `ContainerValidatorFactory` exactly as an item stack request makes it) is asked directly.

#include <filesystem>
#include <fstream>

#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/service/Bedrock.h"

#include "mc/server/ServerLevel.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/ServerCommandOrigin.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/containers/ContainerEnumName.h"
#include "mc/world/containers/FullContainerName.h"
#include "mc/world/inventory/network/ContainerScreenContext.h"
#include "mc/world/inventory/simulation/ContainerValidationCaller.h"
#include "mc/world/inventory/simulation/ContainerValidatorFactory.h"
#include "mc/world/inventory/simulation/validation/ContainerValidationBase.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/world/containers/IContainerRegistryAccess.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/components/StorageItemComponent.h"
#include "mc/world/item/components/StorageWeightLimitItemComponent.h"
#include "mc/world/item/components/storage_item_utility/StorageItemWeightData.h"
#include "mc/world/item/components/storage_item_utility/StorageItemUtility.h"
#include "mc/world/level/Level.h"

#include "nlohmann/json.hpp"

#include "core/Tier.h"
#include "feature/admin/SbConfig.h"
#include "feature/bundle/BundleRule.h"
#include "game/ItemConv.h"
#include "mod/Config.h"
#include "net/Companion.h"
#include "selftest/SelfTest.h"

namespace abp::bundle {
namespace {

using selftest::Context;

template <class T>
struct Restore {
    T& ref;
    T  saved;
    explicit Restore(T& r) : ref(r), saved(r) {}
    ~Restore() { ref = saved; }
};

/// A real, empty bundle container in the level's container registry (what the engine creates
/// when a bundle is first opened), made from the vanilla bundle's own components.
std::optional<FullContainerName> bundleContainer() {
    static std::optional<FullContainerName> made;
    if (made) return made;
    auto level  = ll::service::getLevel();
    auto bundle = game::ItemConv::make("minecraft:bundle");
    auto* item  = bundle.mItem.get();
    if (!level || !item) return std::nullopt;
    auto* storage = static_cast<StorageItemComponent*>(item->getComponent(HashedString("minecraft:storage_item")));
    auto* limit   = static_cast<StorageWeightLimitItemComponent*>(item->getComponent(HashedString("minecraft:storage_weight_limit")));
    if (!storage || !limit) return std::nullopt;
    FullContainerName name;
    name.mName      = ContainerEnumName::DynamicContainer;
    name.mDynamicId = 0x7ABB0001u;
    level->getContainerRegistryAccess()->tryCreateEntry(storage, limit, name);
    made = name;
    return made;
}

/// What the engine's bundle validation says about putting `item` into an empty bundle.
bool engineAllows(Player& player, ItemStack const& item) {
    ContainerScreenContext sc;
    sc.mPlayer              = &player;
    sc.mScreenContainerType = SharedTypes::Legacy::ContainerType::Inventory;
    auto bundle             = bundleContainer();
    if (!bundle) return false;
    auto const& name        = *bundle;
    auto validator  = ContainerValidatorFactory::createContainerValidator(name, sc, ContainerValidationCaller::Server);
    return validator && validator->isItemAllowedInSlot(sc, 0, item, 1, false);
}

bool runConsole(std::string const& command) {
    auto level = ll::service::getLevel();
    if (!level) return false;
    ServerCommandOrigin origin("abp-selftest", level->asServer(), CommandPermissionLevel::Owner, 0);
    return ll::command::CommandRegistrar::getServerInstance().executeCommand(command, origin).mSuccessCount > 0;
}

std::filesystem::path serverConfigFile() { return config::configDir() / "sophisticatedbackpacks-server.json"; }

/// Writes `canBePlacedInContainerItems` into the config file only (the running value changes on
/// the next `/backpack reload`).
void writeFileValue(bool allowed) {
    auto           path = serverConfigFile();
    nlohmann::json j    = nlohmann::json::object();
    if (std::ifstream in(path); in) j = nlohmann::json::parse(in, nullptr, false);
    if (!j.is_object()) j = nlohmann::json::object();
    j["canBePlacedInContainerItems"] = allowed;
    std::ofstream(path) << j.dump(4);
}

} // namespace

ABP_REGISTER_SELFTEST("bundle.default_refuses", [](Context& ctx) {
    ctx.check(!admin::SbServerConfig{}.canBePlacedInContainerItems, "SB default is canBePlacedInContainerItems = false");
    auto&   cfg = config::mut<admin::SbServerConfig>();
    Restore keep(cfg.canBePlacedInContainerItems);
    cfg.canBePlacedInContainerItems = false;
    auto backpack = ctx.backpackItem(*ctx.createBackpack(core::Tier::Leather));
    auto sword    = game::ItemConv::make("minecraft:diamond_sword");
    ctx.check(!fitsBundles(backpack, false), "the rule refuses the backpack");
    ctx.check(fitsBundles(sword, false) && fitsBundles(game::ItemConv::make("minecraft:dirt", 5), false), "other items fit");
    ctx.check(!engineAllows(ctx.player(), backpack), "the bundle validation refuses the backpack");
    ctx.require(bundleContainer().has_value(), "made a bundle container");
    ctx.check(engineAllows(ctx.player(), sword), "the bundle validation accepts a diamond sword");
    // Every tier.
    for (auto const& t : core::kTiers) {
        ctx.check(!engineAllows(ctx.player(), ctx.backpackItem(*ctx.createBackpack(t.tier))), "refused: " + std::string(t.id));
    }
});

ABP_REGISTER_SELFTEST("bundle.config_true_allows", [](Context& ctx) {
    auto&   cfg = config::mut<admin::SbServerConfig>();
    Restore keep(cfg.canBePlacedInContainerItems);
    cfg.canBePlacedInContainerItems = true;
    auto backpack = ctx.backpackItem(*ctx.createBackpack(core::Tier::Diamond));
    auto sword    = game::ItemConv::make("minecraft:diamond_sword");
    ctx.check(fitsBundles(backpack, false), "the rule allows the backpack");
    // A backpack is then just another unstackable item: what the bundle says about a sword.
    bool swordOk = engineAllows(ctx.player(), sword);
    ctx.check(engineAllows(ctx.player(), backpack) == swordOk, "the bundle validation treats the backpack like a sword");
    ctx.check(swordOk, "the bundle validation accepts an unstackable item");
    // Java: BundleContents weight 1/maxStackSize = a whole bundle; Bedrock: 64 of 64.
    int w = StorageItemUtility::getItemWeightInStorageItem(backpack, uchar{1});
    ctx.check(w == 64, "a backpack weighs a whole bundle (64), got " + std::to_string(w));
    ctx.check(w == StorageItemUtility::getItemWeightInStorageItem(sword, uchar{1}), "same weight as a sword");
});

ABP_REGISTER_SELFTEST("bundle.reload", [](Context& ctx) {
    auto&   cfg = config::mut<admin::SbServerConfig>();
    Restore keep(cfg.canBePlacedInContainerItems);
    auto    path = serverConfigFile();
    std::string original;
    if (std::ifstream in(path, std::ios::binary); in) original.assign(std::istreambuf_iterator<char>(in), {});
    auto backpack = ctx.backpackItem(*ctx.createBackpack(core::Tier::Iron));
    writeFileValue(true);
    ctx.check(runConsole("backpack reload"), "/backpack reload succeeds");
    ctx.check(serverAllowsBackpacks(), "reloaded true");
    ctx.check(engineAllows(ctx.player(), backpack) == engineAllows(ctx.player(), game::ItemConv::make("minecraft:iron_sword")), "allowed after reload");
    writeFileValue(false);
    ctx.check(runConsole("backpack reload"), "/backpack reload succeeds again");
    ctx.check(!serverAllowsBackpacks(), "reloaded false");
    ctx.check(!engineAllows(ctx.player(), backpack), "refused after reload");
    if (!original.empty()) std::ofstream(path, std::ios::binary) << original;
    (void)config::loadAll();
});

ABP_REGISTER_SELFTEST("bundle.client_rule", [](Context& ctx) {
    auto saved    = clientRule();
    auto backpack = ctx.backpackItem(*ctx.createBackpack(core::Tier::Gold));
    setClientRule(std::nullopt);
    ctx.check(!fitsBundles(backpack, true), "unknown rule: refused (SB default)");
    setClientRule(true);
    ctx.check(fitsBundles(backpack, true), "announced true: allowed");
    setClientRule(false);
    ctx.check(!fitsBundles(backpack, true), "announced false: refused");
    ctx.check(fitsBundles(game::ItemConv::make("minecraft:stone", 3), true), "other items always fit");
    setClientRule(saved);
});

// The server announces the rule to companions, and again after it changed.
ABP_REGISTER_SELFTEST("bundle.announce", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto&   cfg = config::mut<admin::SbServerConfig>();
    Restore keep(cfg.canBePlacedInContainerItems);
    cfg.canBePlacedInContainerItems = false;
    auto&              player       = ctx.player();
    std::vector<bool>  got;
    net::setCompanion(player, net::CompanionInfo{"test", {"native_screen"}});
    net::setTestSink([&](Player& to, std::string_view name, nlohmann::json const& body) {
        if (&to == &player && name.find("BundleRulePacket") != std::string_view::npos) got.push_back(body.value("allowed", true));
    });
    co_await ctx.ticks(25);
    ctx.check(got.size() == 1 && !got[0], "announced false once, got " + std::to_string(got.size()));
    cfg.canBePlacedInContainerItems = true;
    co_await ctx.ticks(25);
    ctx.check(got.size() == 2 && got[1], "announced the change to true");
    co_await ctx.ticks(25);
    ctx.check(got.size() == 2, "no repeat while unchanged");
    net::setTestSink(nullptr);
    net::setCompanion(player, std::nullopt);
});

} // namespace abp::bundle
