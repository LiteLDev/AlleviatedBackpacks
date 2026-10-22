// `/backpack modeltest <scene>` (operators): builds the scenes the client e2e scenario `models`
// takes screenshots of (P2-11). Every scene is laid out south of the player (+z), who should look
// south (yaw 0).
//   kit    - 12 backpacks in the inventory: every tier plain, then every tier dyed (red / yellow)
//   stands - armor stands wearing (row 1) and holding (row 2) every tier, dyed on odd tiers
//   parts  - armor stands wearing backpacks with tanks (water 60% / lava 30%), a battery (50%) and
//            a full milk tank, and an item display
//   place  - every tier placed as a block (dyed on odd tiers), one waterlogged, one with
//            tanks + battery + item display, and a comparator behind a half full one
//   drop   - every tier dropped on the ground

#include <cmath>

#include "ll/api/command/CommandHandle.h"

#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"

#include "core/Appearance.h"
#include "feature/model/RenderModel.h"
#include "feature/placed/PlacedBackpacks.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/Storage.h"
#include "mod/Commands.h"
#include "mod/Log.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/tank/Tank.h"

namespace abp::model {

namespace {

using core::Tier;

constexpr std::uint32_t kRed    = 0xFFB02E26; // red dye
constexpr std::uint32_t kYellow = 0xFFFED83D; // yellow dye

std::shared_ptr<game::Backpack> makeBackpack(Tier tier, bool dyed) {
    auto bp = game::Storage::get().create(tier);
    if (!bp) return nullptr;
    if (dyed) {
        bp->data().mainColor   = kRed;
        bp->data().accentColor = kYellow;
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
    return game::makeBackpackItem(bp->tier(), bp->uuid());
}

/// Tanks (water 60% left, lava 30% right), a forced battery charge, an item display.
std::shared_ptr<game::Backpack> partsBackpack(Tier tier, int variant, Player* player) {
    auto bp = makeBackpack(tier, variant % 2 == 1);
    if (!bp) return nullptr;
    int slots = static_cast<int>(bp->data().upgrades.size());
    if (variant == 0 && slots >= 2) {
        installById(bp, "tank", 0, player);
        installById(bp, "tank", 1, player);
        auto tanks = upgrade::tank::tanks(bp);
        if (tanks.size() == 2) {
            auto t0 = upgrade::tank::read(tanks[0]);
            t0.fill({"water", t0.capacity() * 6 / 10});
            upgrade::tank::write(tanks[0], t0);
            auto t1 = upgrade::tank::read(tanks[1]);
            t1.fill({"lava", t1.capacity() * 3 / 10});
            upgrade::tank::write(tanks[1], t1);
        }
    } else if (variant == 1) {
        // The battery part (no Battery upgrade item yet: forced for the screenshot) + a full milk tank.
        bp->data().settings["abp_model_test"] = {{"battery", 0.5}};
        installById(bp, "tank", 0, player);
        upgrade::tank::fill(bp, {"milk", 1000000});
    } else if (variant == 2) {
        bp->inventory().insert(core::ItemStackData::of("minecraft:diamond_sword", 1, 1), {core::Source::Internal});
        bp->data().settings["item_display"] = {{"slots", {0}}, {"rotations", {{"0", 45}}}};
    }
    bp->markDirty();
    return bp;
}

Mob* stand(BlockSource& region, Vec3 const& at, float yaw) {
    auto* mob = region.getLevel().getSpawner().spawnMob(region, ActorDefinitionIdentifier("minecraft:armor_stand"), nullptr, at, false, false, false);
    if (!mob) return nullptr;
    mob->setYBodyRotations(yaw, yaw);
    mob->setYHeadRot(yaw);
    return mob;
}

void sendArmor(Mob* mob) {
    if (mob) mob->sendArmor(std::bitset<5>{0b11111});
}

BlockPos feetOf(Player& p) {
    auto f = p.getFeetPos();
    return BlockPos{static_cast<int>(std::floor(f.x)), static_cast<int>(std::floor(f.y)), static_cast<int>(std::floor(f.z))};
}

void setBlock(BlockSource& region, BlockPos const& pos, std::string const& id) {
    if (auto b = Block::tryGetFromRegistry(HashedString(id))) {
        BlockChangeContext ctx{};
        region.setBlock(pos, *b, 3, nullptr, ctx);
    }
}

} // namespace

/// Model debug override for e2e screenshots (`settings["abp_model_test"]`).
void applyTestOverrides(game::Backpack const& backpack, core::render::BackpackModel& m) {
    auto it = backpack.data().settings.find("abp_model_test");
    if (it == backpack.data().settings.end() || !it->is_object()) return;
    if (auto b = it->find("battery"); b != it->end() && b->is_number()) m.battery = b->get<float>();
}

} // namespace abp::model

ABP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    using namespace abp;
    auto run = [](CommandOrigin const& origin, CommandOutput& output, std::string const& scene) {
        auto* e = origin.getEntity();
        if (!e || !e->isPlayer() || !game::Storage::get().available()) return output.error("players only");
        if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors) return output.error("You need operator permission.");
        auto& player = static_cast<Player&>(*e);
        auto& region = player.getDimensionBlockSource();
        auto  base   = model::feetOf(player);
        int   n      = 0;
        if (scene == "kit") {
            auto& inv = player.getInventory();
            for (int i = 0; i < 12; ++i) {
                auto bp = model::makeBackpack(static_cast<core::Tier>(i % 6), i >= 6);
                if (!bp) continue;
                inv.setItem(9 + i, model::itemOf(bp));
                ++n;
            }
            player.sendInventory(false);
        } else if (scene == "stands" || scene == "parts") {
            for (int i = 0; i < 6; ++i) {
                float x = static_cast<float>(base.x) + 0.5f + static_cast<float>(i - 3) * 1.5f + 0.75f;
                if (scene == "stands") {
                    auto worn = model::makeBackpack(static_cast<core::Tier>(i), i % 2 == 1);
                    auto held = model::makeBackpack(static_cast<core::Tier>(i), i % 2 == 1);
                    if (!worn || !held) continue;
                    // Row 1 shows the back (faces away), row 2 the front holding one.
                    if (auto* s = model::stand(region, Vec3{x, static_cast<float>(base.y), static_cast<float>(base.z) + 4.5f}, 0.0f)) {
                        s->setArmor(SharedTypes::Legacy::ArmorSlot::Torso, model::itemOf(worn));
                        model::sendArmor(s);
                        ++n;
                    }
                    if (auto* s = model::stand(region, Vec3{x, static_cast<float>(base.y), static_cast<float>(base.z) + 7.5f}, 180.0f)) {
                        s->setCarriedItem(model::itemOf(held));
                        ++n;
                    }
                } else if (i < 3) {
                    auto bp = model::partsBackpack(static_cast<core::Tier>(5 - i), i, &player);
                    if (!bp) continue;
                    if (auto* s = model::stand(region, Vec3{x + 1.5f, static_cast<float>(base.y), static_cast<float>(base.z) + 4.5f}, 0.0f)) {
                        s->setArmor(SharedTypes::Legacy::ArmorSlot::Torso, model::itemOf(bp));
                        model::sendArmor(s);
                        ++n;
                    }
                    if (auto* s = model::stand(region, Vec3{x + 1.5f, static_cast<float>(base.y), static_cast<float>(base.z) + 7.5f}, 180.0f)) {
                        s->setArmor(SharedTypes::Legacy::ArmorSlot::Torso, model::itemOf(bp));
                        model::sendArmor(s);
                        ++n;
                    }
                }
            }
        } else if (scene == "place") {
            auto& placed = placed::PlacedBackpacks::get();
            int   z      = base.z + 3;
            for (int i = 0; i < 6; ++i) {
                BlockPos pos{base.x - 5 + i * 2, base.y, z};
                model::setBlock(region, pos, "minecraft:air");
                auto bp   = model::makeBackpack(static_cast<core::Tier>(i), i % 2 == 1);
                if (!bp) continue;
                auto item = model::itemOf(bp);
                if (placed.place(region, pos, item, 2 /* faces north, towards the player */) == placed::PlacedBackpacks::PlaceResult::Ok) ++n;
            }
            // Waterlogged (in a water source) and one with tanks / battery / item display.
            BlockPos water{base.x - 3, base.y, z + 3};
            model::setBlock(region, water, "minecraft:water");
            if (auto bp = model::makeBackpack(core::Tier::Iron, false)) {
                auto item = model::itemOf(bp);
                if (placed.place(region, water, item, 2) == placed::PlacedBackpacks::PlaceResult::Ok) ++n;
            }
            for (int v = 0; v < 3; ++v) {
                BlockPos pos{base.x - 1 + v * 2, base.y, z + 3};
                model::setBlock(region, pos, "minecraft:air");
                auto bp   = model::partsBackpack(core::Tier::Netherite, v, &player);
                if (!bp) continue;
                auto item = model::itemOf(bp);
                if (placed.place(region, pos, item, 2) == placed::PlacedBackpacks::PlaceResult::Ok) ++n;
            }
            // Comparator behind a half full backpack (south of it), reading it.
            BlockPos cpos{base.x + 6, base.y, z + 3};
            model::setBlock(region, cpos, "minecraft:air");
            if (auto bp = model::makeBackpack(core::Tier::Leather, false)) {
                for (int s = 0; s < bp->inventory().size() / 2; ++s) bp->inventory().set(s, core::ItemStackData::of("minecraft:cobblestone", 64));
                auto item = model::itemOf(bp);
                if (placed.place(region, cpos, item, 2) == placed::PlacedBackpacks::PlaceResult::Ok) ++n;
                // The comparator (and dust) behind it are set by the e2e scenario (/setblock).
            }
        } else if (scene == "drop") {
            for (int i = 0; i < 12; ++i) {
                auto bp = model::makeBackpack(static_cast<core::Tier>(i % 6), i >= 6);
                if (!bp) continue;
                Vec3 at{static_cast<float>(base.x) - 3.5f + static_cast<float>(i % 6) * 1.2f, static_cast<float>(base.y) + 0.2f,
                        static_cast<float>(base.z) + 3.5f + static_cast<float>(i / 6) * 1.5f};
                if (region.getLevel().getSpawner().spawnItem(region, model::itemOf(bp), nullptr, at, 0)) ++n;
            }
        } else {
            return output.error("scenes: kit, stands, parts, place, drop");
        }
        log().info("model test {}: {} objects", scene, n);
        output.success("model test " + scene + ": " + std::to_string(n));
    };
    for (auto const* scene : {"kit", "stands", "parts", "place", "drop"}) {
        std::string s = scene;
        cmd.overload().text("modeltest").text(scene).execute([run, s](CommandOrigin const& origin, CommandOutput& output) { run(origin, output, s); });
    }
});
