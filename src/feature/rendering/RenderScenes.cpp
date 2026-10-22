// `/backpack rendertest <scene>` (operators): scenes for the GX-1 rendering gaps, used by the client
// e2e scenario `rendering`. Like `/backpack modeltest`, everything is laid out south of the player
// (+z), who should look south.
//   frames      - item frames on a wall (and one on the floor) holding backpacks: plain, dyed with
//                 tanks, turned 45 / 90 degrees
//   cooking     - a burning Smelting Upgrade in a worn backpack (chest slot) and in a placed one
//   fluidbars   - a netherite backpack in the inventory: a Stack Upgrade (slot 0) and water /
//                 lava tanks (water filled past the base capacity); fluidbars2: milk / potion tanks
//   fluidstands - armor stands wearing backpacks with water / lava and milk / potion tanks
//   coldcow     - turns the cows within 8 blocks into the cold climate variant (actor property)
//   slowcapture - stretches the mob catcher's capture animation to 60 ticks for screenshots
//   fastcapture - back to SB's 5 ticks

#include <bitset>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "ll/api/command/CommandHandle.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/state/ActorPropertyUtils.h"
#include "mc/world/actor/state/PropertyComponent.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/VanillaStates.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/ItemFrameBlockActor.h"

#include "core/Tier.h"
#include "feature/placed/PlacedBackpacks.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/Storage.h"
#include "mod/Commands.h"
#include "mod/Log.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/cooking/Cooking.h"
#include "upgrade/mobcatcher/CaptureEffect.h"
#include "upgrade/tank/Tank.h"

namespace abp::rendering {

namespace {

using core::Tier;

std::shared_ptr<game::Backpack> makeBackpack(Tier tier, std::uint32_t main = 0, std::uint32_t accent = 0) {
    auto bp = game::Storage::get().create(tier);
    if (!bp) return nullptr;
    if (main) {
        bp->data().mainColor   = main;
        bp->data().accentColor = accent;
    }
    bp->markDirty();
    return bp;
}

void installById(std::shared_ptr<game::Backpack> const& bp, std::string const& id, int slot, Player* player) {
    auto* type = upgrade::byId(id);
    if (!type) return;
    auto item = game::ItemConv::toData(game::ItemConv::make(type->info().itemId, 1));
    (void)upgrade::install(bp, slot, item, player);
}

ItemStack itemOf(std::shared_ptr<game::Backpack> const& bp) {
    if (!bp) return ItemStack::EMPTY_ITEM();
    game::Storage::get().save(*bp, true);
    auto item = game::makeBackpackItem(bp->tier(), bp->uuid());
    game::setBackpackTint(item, bp->data().mainColor);
    return item;
}

BlockPos feetOf(Player& p) {
    auto f = p.getFeetPos();
    return BlockPos{static_cast<int>(std::floor(f.x)), static_cast<int>(std::floor(f.y)), static_cast<int>(std::floor(f.z))};
}

void setBlock(BlockSource& region, BlockPos const& pos, Block const& block) {
    BlockChangeContext ctx{};
    region.setBlock(pos, block, 3, nullptr, ctx);
}

bool setBlock(BlockSource& region, BlockPos const& pos, std::string const& id, int facing = -1) {
    auto b = Block::tryGetFromRegistry(HashedString(id));
    if (!b) return false;
    Block const* block = &*b;
    if (facing >= 0) {
        auto turned = block->setState<int>(VanillaStates::FacingDirection(), facing);
        if (!turned) return false;
        block = &*turned;
    }
    setBlock(region, pos, *block);
    return true;
}

/// A frame at `pos` facing `facing` holding `item`, turned by `rotation` degrees.
bool frame(BlockSource& region, BlockPos const& pos, int facing, ItemStack const& item, float rotation) {
    if (!setBlock(region, pos, "minecraft:frame", facing)) return false;
    auto* be = region.getBlockEntity(pos);
    if (!be || be->getType() != BlockActorType::ItemFrame) return false;
    auto& f     = static_cast<ItemFrameBlockActor&>(*be);
    f.mRotation = rotation;
    f.setItem(region, ItemInstance(item), nullptr);
    return true;
}

/// A tank half full of water (left) and one with lava (right), dyed red / yellow.
std::shared_ptr<game::Backpack> tankBackpack(Player* player) {
    auto bp = makeBackpack(Tier::Diamond, 0xFFB02E26, 0xFFFED83D);
    if (!bp) return nullptr;
    installById(bp, "tank", 0, player);
    installById(bp, "tank", 1, player);
    auto tanks = upgrade::tank::tanks(bp);
    if (tanks.size() == 2) {
        auto t0 = upgrade::tank::read(tanks[0]);
        t0.fill({"water", t0.capacity() / 2});
        upgrade::tank::write(tanks[0], t0);
        auto t1 = upgrade::tank::read(tanks[1]);
        t1.fill({"lava", t1.capacity() * 3 / 4});
        upgrade::tank::write(tanks[1], t1);
    }
    bp->markDirty();
    return bp;
}

/// Tanks with `fluids` (core ids) at `ratios` of their capacity, from upgrade slot `first`.
void fillTanks(std::shared_ptr<game::Backpack> const& bp, int first, std::vector<std::pair<std::string, float>> const& fluids, Player* player) {
    for (std::size_t i = 0; i < fluids.size(); ++i) installById(bp, "tank", first + static_cast<int>(i), player);
    auto tanks = upgrade::tank::tanks(bp);
    for (std::size_t i = 0; i < tanks.size() && i < fluids.size(); ++i) {
        auto t = upgrade::tank::read(tanks[i]);
        t.fill({fluids[i].first, static_cast<int>(static_cast<float>(t.capacity()) * fluids[i].second)});
        upgrade::tank::write(tanks[i], t);
    }
    bp->markDirty();
}

Mob* stand(BlockSource& region, Vec3 const& at, float yaw) {
    auto* mob = region.getLevel().getSpawner().spawnMob(region, ActorDefinitionIdentifier("minecraft:armor_stand"), nullptr, at, false, false, false);
    if (!mob) return nullptr;
    mob->setYBodyRotations(yaw, yaw);
    mob->setYHeadRot(yaw);
    return mob;
}

/// A backpack with a Smelting Upgrade that burns for a long time.
std::shared_ptr<game::Backpack> cookingBackpack(Player* player) {
    auto bp = makeBackpack(Tier::Iron);
    if (!bp) return nullptr;
    installById(bp, "smelting", 0, player);
    upgrade::forEachInstalled(bp, true, [](upgrade::IUpgrade& u, upgrade::UpgradeContext const& ctx) {
        if (u.info().id != "smelting") return;
        auto s       = upgrade::cooking::CookingUpgrade::state(ctx);
        s.fuel       = core::ItemStackData::of("minecraft:coal_block", 64);
        s.input      = core::ItemStackData::of("minecraft:raw_iron", 64);
        s.burnTotal  = 1000000;
        s.burnFinish = s.clock + 1000000;
        upgrade::cooking::CookingUpgrade::setState(ctx, s);
    });
    bp->markDirty();
    return bp;
}

} // namespace

} // namespace abp::rendering

ABP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    using namespace abp;
    auto run = [](CommandOrigin const& origin, CommandOutput& output, std::string const& scene) {
        auto* e = origin.getEntity();
        if (!e || !e->isPlayer() || !game::Storage::get().available()) return output.error("players only");
        if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors) return output.error("You need operator permission.");
        auto& player = static_cast<Player&>(*e);
        auto& region = player.getDimensionBlockSource();
        auto  base   = rendering::feetOf(player);
        int   n      = 0;
        if (scene == "frames") {
            // A stone wall 4 blocks south, frames on its north face (facing north, towards the player).
            int z = base.z + 4;
            for (int x = base.x - 3; x <= base.x + 3; ++x) {
                for (int y = base.y; y <= base.y + 2; ++y) rendering::setBlock(region, BlockPos{x, y, z}, "minecraft:stone");
            }
            auto plain = rendering::makeBackpack(core::Tier::Leather);
            auto tanks = rendering::tankBackpack(&player);
            auto gold  = rendering::makeBackpack(core::Tier::Gold, 0xFF3C44AA, 0xFFF9FFFE);
            if (rendering::frame(region, BlockPos{base.x - 2, base.y + 1, z - 1}, 2, rendering::itemOf(plain), 0.f)) ++n;
            if (rendering::frame(region, BlockPos{base.x, base.y + 1, z - 1}, 2, rendering::itemOf(tanks), 0.f)) ++n;
            if (rendering::frame(region, BlockPos{base.x + 2, base.y + 1, z - 1}, 2, rendering::itemOf(gold), 45.f)) ++n;
            // On the floor (facing up), turned 90 degrees.
            auto floor = rendering::makeBackpack(core::Tier::Netherite);
            if (rendering::frame(region, BlockPos{base.x, base.y, base.z + 2}, 1, rendering::itemOf(floor), 90.f)) ++n;
        } else if (scene == "cooking") {
            auto worn = rendering::cookingBackpack(&player);
            if (worn) {
                player.setArmor(SharedTypes::Legacy::ArmorSlot::Torso, rendering::itemOf(worn));
                player.sendInventory(false);
                ++n;
            }
            auto placed = rendering::cookingBackpack(&player);
            BlockPos pos{base.x, base.y, base.z + 2};
            rendering::setBlock(region, pos, "minecraft:air");
            if (placed) {
                auto item = rendering::itemOf(placed);
                if (placed::PlacedBackpacks::get().place(region, pos, item, 2) == placed::PlacedBackpacks::PlaceResult::Ok) ++n;
            }
        } else if (scene == "fluidbars" || scene == "fluidbars2") {
            // SB allows two tanks per backpack: water / lava behind a Stack Upgrade, then milk / potion.
            auto first  = rendering::makeBackpack(core::Tier::Netherite);
            auto second = rendering::makeBackpack(core::Tier::Netherite);
            if (first && second) {
                rendering::installById(first, "stack_tier_1", 0, &player);
                rendering::fillTanks(first, 1, {{"water", 1.0f}, {"lava", 0.5f}}, &player);
                rendering::fillTanks(second, 0, {{"milk", 0.6f}, {"potion:21", 0.4f}}, &player);
                for (auto const& bp : {scene == "fluidbars" ? first : second}) {
                    auto item = rendering::itemOf(bp);
                    if (!player.add(item)) player.drop(item, false);
                    ++n;
                }
                player.sendInventory(false);
            }
        } else if (scene == "fluidstands") {
            std::vector<std::vector<std::pair<std::string, float>>> sets = {
                {{"water", 0.6f}, {"lava", 0.9f}},
                {{"milk", 0.7f}, {"potion:21", 0.5f}},
                {{"potion:5", 1.0f}, {"potion:31", 0.3f}},
            };
            for (std::size_t i = 0; i < sets.size(); ++i) {
                auto bp = rendering::makeBackpack(core::Tier::Diamond);
                if (!bp) continue;
                rendering::fillTanks(bp, 0, sets[i], &player);
                float x = static_cast<float>(base.x) + 0.5f + (static_cast<float>(i) - 1.f) * 1.5f;
                // Facing the player (north) and turned 30 degrees, so the side tanks show.
                if (auto* s = rendering::stand(region, Vec3{x, static_cast<float>(base.y), static_cast<float>(base.z) + 3.5f}, 150.0f)) {
                    s->setArmor(SharedTypes::Legacy::ArmorSlot::Torso, rendering::itemOf(bp));
                    s->sendArmor(std::bitset<5>{0b11111});
                    ++n;
                }
            }
        } else if (scene == "coldcow") {
            auto me = player.getPosition();
            for (auto* actor : player.getLevel().getRuntimeActorList()) {
                if (!actor || actor->getTypeName() != "minecraft:cow" || actor->getPosition().distanceTo(me) > 8.f) continue;
                auto props = actor->getEntityContext().tryGetComponent<PropertyComponent>();
                if (!props) continue;
                if (ActorPropertyUtils::tryQueueEnumIndexValueByString(*props, *actor, HashedString("minecraft:climate_variant").getHash(), "cold")) ++n;
            }
        } else if (scene == "slowcapture" || scene == "fastcapture") {
            upgrade::mobcatcher::setCaptureAnimationTicksForTests(scene == "slowcapture" ? 60 : 0);
            n = 1;
        } else {
            return output.error("scenes: frames, cooking, fluidbars, fluidbars2, fluidstands, coldcow, slowcapture, fastcapture");
        }
        log().info("render test {}: {} objects", scene, n);
        output.success("render test " + scene + ": " + std::to_string(n));
    };
    for (auto const* scene : {"frames", "cooking", "fluidbars", "fluidbars2", "fluidstands", "coldcow", "slowcapture", "fastcapture"}) {
        std::string s = scene;
        cmd.overload().text("rendertest").text(scene).execute([run, s](CommandOrigin const& origin, CommandOutput& output) { run(origin, output, s); });
    }
});
