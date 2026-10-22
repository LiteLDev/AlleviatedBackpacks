// Jukebox selftests: `backpack selftest "jukebox.*"`.

#include <algorithm>

#include "ll/api/service/Bedrock.h"

#include "mc/server/SimulatedPlayer.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"

#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "mod/Config.h"
#include "selftest/SelfTest.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "feature/placed/TestSite.h"
#include "net/Companion.h"
#include "net/NativeScreens.h"
#include "upgrade/jukebox/Jukebox.h"

namespace abp::upgrade::jukebox {
namespace {

using selftest::Context;
using core::Tier;
using Where  = ui::MenuManager::SlotAddress::Where;
using Action = ui::MenuManager::ActionType;

constexpr std::string_view kCat    = "minecraft:music_disc_cat";
constexpr std::string_view kBlocks = "minecraft:music_disc_blocks";
constexpr std::string_view kFar    = "minecraft:music_disc_far";

core::ItemStackData item(std::string_view id) { return game::ItemConv::toData(game::ItemConv::make(id, 1)); }

std::int64_t idOf(Player const& p) { return p.getOrCreateUniqueID().rawID; }

/// Records the service's sound packets; restores the service's test hooks on destruction.
struct Recorder {
    std::vector<SoundEvent> events;
    explicit Recorder(std::optional<int> duration = std::nullopt) {
        Service::get().setObserver([this](SoundEvent const& e) { events.push_back(e); });
        Service::get().setDurationOverride(duration);
    }
    ~Recorder() {
        Service::get().setObserver(nullptr);
        Service::get().setDurationOverride(std::nullopt);
    }
    Recorder(Recorder const&)            = delete;
    Recorder& operator=(Recorder const&) = delete;

    [[nodiscard]] SoundEvent const* last(SoundEvent::Kind kind, std::int64_t listener) const {
        for (auto it = events.rbegin(); it != events.rend(); ++it)
            if (it->kind == kind && it->listener == listener) return &*it;
        return nullptr;
    }
    [[nodiscard]] int count(SoundEvent::Kind kind, std::int64_t listener) const {
        return static_cast<int>(std::count_if(events.begin(), events.end(), [&](SoundEvent const& e) {
            return e.kind == kind && e.listener == listener;
        }));
    }
};

/// A backpack with a jukebox carried by the test player, discs already inserted.
struct Setup {
    std::shared_ptr<game::Backpack> bp;
    int                             slot;
    UpgradeContext                  ctx() const { return UpgradeContext(bp, slot); }
};

Setup carriedJukebox(Context& ctx, std::string_view upgradeId, std::vector<std::string_view> discs, int invSlot = 0) {
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(Tier::Gold);
    int   slot   = ctx.installUpgrade(bp, std::string(upgradeId));
    UpgradeContext uc(bp, slot);
    for (int i = 0; i < static_cast<int>(discs.size()); ++i) {
        if (!discs[static_cast<std::size_t>(i)].empty()) setDisc(uc, i, item(discs[static_cast<std::size_t>(i)]));
    }
    ctx.require(ctx.give(player, ctx.backpackItem(*bp), invSlot) == invSlot, "give backpack");
    return {bp, slot};
}

bool press(Player& player, int menuSlot) {
    // Buttons swallow the click (the simulated move "fails" by design).
    (void)ui::MenuManager::get().simulate(player, {Action::Take, {Where::Menu, menuSlot}, {Where::Cursor, 0}, 1});
    return true;
}

bool openMenu(Context& ctx, Player& player, Setup const& s) {
    auto* type = upgradeFor(s.ctx().state().item);
    ctx.require(type != nullptr, "jukebox type");
    auto& mm = ui::MenuManager::get();
    // (openSettings() closes first and opens a few ticks later; open directly here.)
    mm.open(player, type->createSettingsMenu(player, s.ctx().withPlayer(&player)));
    if (!mm.current(player)) return false;
    mm.forceShown(player);
    return true;
}

// ---- items / discs ---------------------------------------------------------------------------------

ABP_REGISTER_SELFTEST("jukebox.items", [](Context& ctx) {
    for (auto id : {kBasicId, kAdvancedId}) {
        auto* u = byId(id);
        ctx.require(u != nullptr, std::string(id) + " registered");
        ctx.check(!game::ItemConv::make(u->info().itemId).isNull(), u->info().itemId + " exists in the behavior pack");
        ctx.check(u->info().hasSettings, std::string(id) + " has a tab");
    }
    auto bp = ctx.createBackpack(Tier::Gold);
    ctx.installUpgrade(bp, std::string(kBasicId));
    ctx.check(
        !upgrade::canInstall(bp, 1, item("alleviatedbackpacks:advanced_jukebox_upgrade"), nullptr),
        "only one jukebox per backpack (group jukebox_upgrades)"
    );
    UpgradeContext uc(bp, 0);
    ctx.check(discSlots(uc) == kBasicSlots, "basic: 1 disc slot");
    ctx.check(repeatMode(uc) == core::RepeatMode::None && !shuffle(uc), "SC defaults: no repeat, no shuffle");
    auto adv = ctx.createBackpack(Tier::Gold);
    ctx.installUpgrade(adv, std::string(kAdvancedId));
    ctx.check(discSlots(UpgradeContext(adv, 0)) == 12, "advanced: 12 disc slots by default");
    ctx.check(slotsInRow(UpgradeContext(adv, 0)) == 4, "advanced: 4 per row");
    ctx.check(repeatMode(UpgradeContext(adv, 0)) == core::RepeatMode::None, "advanced: Repeat Disabled by default");
    struct SlotsGuard {
        int saved = config::get<JukeboxConfig>().advancedSlots;
        ~SlotsGuard() { config::mut<JukeboxConfig>().advancedSlots = saved; }
    } guard;
    config::mut<JukeboxConfig>().advancedSlots = 99;
    ctx.check(discSlots(UpgradeContext(adv, 0)) == 16, "configurable up to 16");
});

ABP_REGISTER_SELFTEST("jukebox.disc_info", [](Context& ctx) {
    int found = 0;
    for (auto const& id : vanillaDiscIds()) {
        auto stack = game::ItemConv::make(id);
        if (stack.isNull()) {
            ctx.info(id + " is not an item in this version");
            continue;
        }
        ++found;
        auto info = discInfo(stack);
        ctx.require(info.has_value(), id + " is a disc");
        ctx.check(info->soundEvent == "record." + id.substr(std::string("minecraft:music_disc_").size()), id + " sound " + info->soundEvent);
        ctx.check(!info->key.empty(), id + " has our positional copy");
        ctx.check(info->durationTicks >= 60 * 20 && info->durationTicks <= 400 * 20, id + " duration " + std::to_string(info->durationTicks));
    }
    ctx.check(found >= 20, "vanilla discs found: " + std::to_string(found));
    auto cat = discInfo(game::ItemConv::make(kCat));
    ctx.require(cat.has_value(), "cat");
    ctx.check(std::abs(cat->durationTicks - 185 * 20) <= 3 * 20, "cat lasts ~3:05 (" + formatTime(cat->durationTicks) + ")");
    ctx.check(soundName(*cat) == "abp.jukebox3d.cat", "sound name");
    ctx.check(!discInfo(game::ItemConv::make("minecraft:stone")), "stone is no disc");
    ctx.check(!discInfo(core::ItemStackData{}), "empty is no disc");
    ctx.check(formatTime(185 * 20) == "3:05", "formatTime");
});

// ---- menu (the server side of the native tab's disc slots) -------------------------------------------

ABP_REGISTER_SELFTEST("jukebox.menu_discs", [](Context& ctx) {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto  s      = carriedJukebox(ctx, kAdvancedId, {});
    ctx.require(ctx.give(player, game::ItemConv::make(kCat), 3) == 3, "give cat");
    ctx.require(ctx.give(player, game::ItemConv::make(kBlocks), 4) == 4, "give blocks");
    ctx.require(ctx.give(player, game::ItemConv::make("minecraft:stone", 5), 5) == 5, "give stone");
    ctx.require(openMenu(ctx, player, s), "open jukebox menu");
    ctx.check(mm.current(player)->size() == 54, "double chest");

    (void)mm.simulate(player, {Action::Place, {Where::Inventory, 3}, {Where::Menu, 10}, 1});
    ctx.check(disc(s.ctx(), 0).id == kCat, "shift-click puts the disc into the first slot");
    ctx.check(player.getInventory().getItem(3).isNull(), "disc left the inventory");
    (void)mm.simulate(player, {Action::Place, {Where::Inventory, 5}, {Where::Menu, 11}, 5});
    ctx.check(player.getInventory().getItem(5).mCount == 5, "non-discs are refused");
    auto occ = occupiedDiscs(s.ctx());
    ctx.check(std::count(occ.begin(), occ.end(), true) == 1 && occ[0], "one disc");

    ctx.check(mm.simulate(player, {Action::Take, {Where::Inventory, 4}, {Where::Cursor, 0}, 1}), "blocks to cursor");
    ctx.check(!mm.simulate(player, {Action::Place, {Where::Cursor, 0}, {Where::Menu, 10}, 1}), "occupied disc slot refuses");
    ctx.check(mm.simulate(player, {Action::Place, {Where::Cursor, 0}, {Where::Menu, 20}, 1}), "place into the 8th slot");
    ctx.check(disc(s.ctx(), 7).id == kBlocks, "blocks in slot 8 (menu 20)");
    ctx.check(player.mPlayerUIContainer->getItem(0).isNull(), "cursor empty");

    ctx.check(mm.simulate(player, {Action::Take, {Where::Menu, 10}, {Where::Cursor, 0}, 1}), "take cat");
    ctx.check(disc(s.ctx(), 0).empty(), "slot emptied");
    mm.close(player);
    int cats = 0;
    for (int i = 0; i < 36; ++i) {
        auto const& it = player.getInventory().getItem(i);
        if (!it.isNull() && it.getTypeName() == kCat) cats += it.mCount;
    }
    ctx.check(cats == 1, "cat returned to the inventory (" + std::to_string(cats) + ")");
});

// The native tab's buttons (TabAction "jukebox": play / stop / next / previous / shuffle / repeat).
ABP_REGISTER_SELFTEST("jukebox.tab_actions", [](Context& ctx) {
    Recorder rec(2000);
    auto&    svc    = Service::get();
    auto&    player = ctx.player();
    auto     s      = carriedJukebox(ctx, kAdvancedId, {kCat, kBlocks});
    auto     act    = [&](std::string const& op) {
        for (auto const& h : net::TabActionRegistry::entries()) {
            if (h.upgrade == "jukebox") return h.handle(player, s.ctx(), nlohmann::json{{"jukebox", op}});
        }
        return false;
    };
    ctx.check(act("shuffle") && shuffle(s.ctx()), "Shuffle Enabled");
    ctx.check(act("shuffle") && !shuffle(s.ctx()), "Shuffle Disabled");
    ctx.check(act("repeat") && repeatMode(s.ctx()) == core::RepeatMode::All, "Repeat Disabled -> Repeat All");
    ctx.check(act("repeat") && repeatMode(s.ctx()) == core::RepeatMode::One, "-> Repeat One");
    ctx.check(act("repeat") && repeatMode(s.ctx()) == core::RepeatMode::None, "-> Repeat Disabled");
    ctx.check(act("next") && !svc.playing(s.bp->uuid(), s.slot), "Next does nothing while stopped");
    ctx.check(act("play") && svc.playing(s.bp->uuid(), s.slot), "Play");
    ctx.check(svc.status(s.bp->uuid(), s.slot)->index == 0, "first disc");
    ctx.check(act("next") && svc.status(s.bp->uuid(), s.slot)->index == 1, "Next");
    ctx.check(act("previous") && svc.status(s.bp->uuid(), s.slot)->index == 0, "Previous");
    ctx.check(act("stop") && !svc.playing(s.bp->uuid(), s.slot), "Stop");
    // Tab data the native tab draws from.
    nlohmann::json data;
    for (auto const& p : net::TabDataRegistry::entries())
        if (p.upgrade == "jukebox") data = p.data(player, s.ctx());
    auto const& j = data["jukebox"];
    ctx.check(j.value("advanced", false) && j.value("slotsInRow", 0) == 4, "tab data: advanced, 4 per row");
    ctx.check(j["discs"].size() == 12 && j["discMenu"].size() == 12, "tab data: 12 disc slots");
    ctx.check(j.value("tabLabel", std::string{}) == "Jukebox", "tab title");
});

// ---- playback --------------------------------------------------------------------------------------

ABP_REGISTER_SELFTEST("jukebox.play_stop", [](Context& ctx) -> ll::coro::CoroTask<> {
    Recorder rec;
    auto&    svc    = Service::get();
    auto&    player = ctx.player();
    auto     s      = carriedJukebox(ctx, kBasicId, {kCat});
    ctx.require(openMenu(ctx, player, s), "open");
    press(player, 49);
    ctx.require(svc.playing(s.bp->uuid(), s.slot), "playing after Play");
    auto const* play = rec.last(SoundEvent::Kind::Play, idOf(player));
    ctx.require(play != nullptr, "the carrier got the sound");
    ctx.check(play->sound == "abp.jukebox3d.cat", "record sound: " + play->sound);
    ctx.check(play->entity == idOf(player), "attached to the carrier");
    ctx.check(svc.emitterEntity(s.bp->uuid(), s.slot) == idOf(player), "emitter: the carrier");
    co_await ctx.ticks(25);
    auto st = svc.status(s.bp->uuid(), s.slot);
    ctx.require(st.has_value(), "still playing");
    ctx.check(st->elapsedTicks >= 20, "time advances");
    ctx.check(rec.count(SoundEvent::Kind::Play, idOf(player)) == 1, "no re-sends while playing");
    ctx.check(svc.play(s.ctx(), &player) && rec.count(SoundEvent::Kind::Play, idOf(player)) == 1, "Play while playing does nothing");
    ctx.require(openMenu(ctx, player, s), "reopen");
    press(player, 48);
    ctx.check(!svc.playing(s.bp->uuid(), s.slot), "stopped after Stop");
    ctx.check(rec.last(SoundEvent::Kind::Stop, idOf(player)) != nullptr, "stop sent");
    ui::MenuManager::get().close(player);
});

ABP_REGISTER_SELFTEST("jukebox.basic_plays_once", [](Context& ctx) -> ll::coro::CoroTask<> {
    Recorder rec(20);
    auto&    svc    = Service::get();
    auto&    player = ctx.player();
    auto     s      = carriedJukebox(ctx, kBasicId, {kCat});
    ctx.require(svc.play(s.ctx(), &player), "play");
    co_await ctx.ticks(30);
    ctx.check(!svc.playing(s.bp->uuid(), s.slot), "basic jukebox stops at the end of the disc");
    ctx.check(rec.count(SoundEvent::Kind::Play, idOf(player)) == 1, "played once");
});

ABP_REGISTER_SELFTEST("jukebox.playlist", [](Context& ctx) -> ll::coro::CoroTask<> {
    Recorder rec(20);
    auto&    svc    = Service::get();
    auto&    player = ctx.player();
    auto     s      = carriedJukebox(ctx, kAdvancedId, {kCat, kBlocks, "", kFar});
    auto     uuid   = s.bp->uuid();
    auto     index  = [&] {
        auto st = svc.status(uuid, s.slot);
        return st ? st->index : -1;
    };
    setRepeatMode(s.ctx(), core::RepeatMode::All);
    ctx.require(svc.play(s.ctx(), &player), "play");
    ctx.check(index() == 0, "starts with the first disc");
    co_await ctx.ticks(25);
    ctx.check(index() == 1, "auto-advance to disc 2 (" + std::to_string(index()) + ")");
    co_await ctx.ticks(20);
    ctx.check(index() == 3, "skips the empty slot (" + std::to_string(index()) + ")");
    auto const* play = rec.last(SoundEvent::Kind::Play, idOf(player));
    ctx.check(play && play->sound == "abp.jukebox3d.far", "far is playing");
    ctx.check(rec.last(SoundEvent::Kind::Stop, idOf(player)) != nullptr, "previous track stopped");
    co_await ctx.ticks(20);
    ctx.check(index() == 0, "repeat all starts over (" + std::to_string(index()) + ")");

    ctx.check(svc.next(s.ctx()) && index() == 1, "next");
    ctx.check(svc.previous(s.ctx()) && index() == 0, "previous plays the history");

    setRepeatMode(s.ctx(), core::RepeatMode::One);
    co_await ctx.ticks(25);
    ctx.check(index() == 0, "repeat one keeps the disc");

    setRepeatMode(s.ctx(), core::RepeatMode::None);
    co_await ctx.ticks(60);
    ctx.check(!svc.playing(uuid, s.slot), "repeat disabled stops at the end of the playlist");

    // SC: taking out (or swapping) the playing disc stops the playback.
    svc.setDurationOverride(2000);
    ctx.require(svc.play(s.ctx(), &player, 1), "play from disc 2");
    ctx.check(index() == 1, "started at disc 2");
    setDisc(s.ctx(), 1, {});
    ctx.check(!svc.playing(uuid, s.slot), "removing the playing disc stops");
    ctx.require(svc.play(s.ctx(), &player, 3), "play from disc 4");
    setDisc(s.ctx(), 0, {});
    ctx.check(svc.playing(uuid, s.slot), "another disc slot changing keeps playing");
    setDisc(s.ctx(), 3, {});
    setDisc(s.ctx(), 3, item(kBlocks));
    ctx.check(!svc.playing(uuid, s.slot), "swapping the playing disc stops");
});

ABP_REGISTER_SELFTEST("jukebox.shuffle", [](Context& ctx) -> ll::coro::CoroTask<> {
    Recorder rec(10);
    auto&    svc    = Service::get();
    auto&    player = ctx.player();
    std::vector<std::string_view> discs;
    for (int i = 0; i < 12; ++i) discs.push_back(vanillaDiscIds()[static_cast<std::size_t>(i)]);
    auto s = carriedJukebox(ctx, kAdvancedId, discs);
    setShuffle(s.ctx(), true);
    ctx.require(svc.play(s.ctx(), &player), "play");
    std::vector<int> seen{svc.status(s.bp->uuid(), s.slot)->index};
    // 12 tracks of 10 ticks (repeat disabled: one pass, then it stops).
    for (int i = 0; i < 125 && svc.playing(s.bp->uuid(), s.slot); ++i) {
        co_await ctx.ticks(1);
        auto st = svc.status(s.bp->uuid(), s.slot);
        if (st && seen.back() != st->index) seen.push_back(st->index);
    }
    auto sorted = seen;
    std::sort(sorted.begin(), sorted.end());
    ctx.check(std::unique(sorted.begin(), sorted.end()) == sorted.end(), "no disc twice in one pass");
    ctx.check(seen.size() == 12, "went through all discs (" + std::to_string(seen.size()) + ")");
    ctx.check(!std::is_sorted(seen.begin(), seen.end()), "order is shuffled");
    svc.stop(s.bp->uuid(), s.slot);
});

ABP_REGISTER_SELFTEST("jukebox.stops_when_dropped", [](Context& ctx) -> ll::coro::CoroTask<> {
    Recorder rec;
    auto&    svc    = Service::get();
    auto&    player = ctx.player();
    // Hold the backpack in the selected hotbar slot, play, then drop it.
    auto     sel    = game::selectedSlot(player);
    auto     s      = carriedJukebox(ctx, kBasicId, {kCat}, sel.slot);
    ctx.require(svc.play(s.ctx(), &player), "play");
    ctx.require(static_cast<SimulatedPlayer&>(player).simulateDropSelectedItem(), "drop the backpack");
    co_await ctx.ticks(10); // SC: keep-alive every 5 ticks, dropped after 10 without one
    ctx.check(!svc.playing(s.bp->uuid(), s.slot), "dropping the backpack stops the music within 10 ticks");
    ctx.check(rec.last(SoundEvent::Kind::Stop, idOf(player)) != nullptr, "stop sent to the former carrier");
});

ABP_REGISTER_SELFTEST("jukebox.stops_on_disable_and_remove", [](Context& ctx) -> ll::coro::CoroTask<> {
    Recorder rec;
    auto&    svc    = Service::get();
    auto&    player = ctx.player();
    auto     s      = carriedJukebox(ctx, kAdvancedId, {kCat, kBlocks});
    ctx.require(svc.play(s.ctx(), &player), "play");
    upgrade::setEnabled(s.bp, s.slot, false);
    co_await ctx.ticks(2);
    ctx.check(!svc.playing(s.bp->uuid(), s.slot), "disabling the upgrade stops it");
    ctx.check(!svc.play(s.ctx(), &player), "a disabled jukebox doesn't play");
    upgrade::setEnabled(s.bp, s.slot, true);
    ctx.require(svc.play(s.ctx(), &player), "play again");
    auto removed = upgrade::remove(s.bp, s.slot, &player);
    ctx.require(removed.has_value(), "remove");
    ctx.check(!svc.playing(s.bp->uuid(), s.slot), "removing the upgrade stops it");
    // The discs travel with the upgrade item.
    auto other = ctx.createBackpack(Tier::Iron);
    ctx.require(upgrade::install(other, 1, *removed, &player), "install into another backpack");
    UpgradeContext moved(other, 1);
    ctx.check(disc(moved, 0).id == kCat && disc(moved, 1).id == kBlocks, "discs kept in the upgrade");
});

ABP_REGISTER_SELFTEST("jukebox.carrier_leaves", [](Context& ctx) -> ll::coro::CoroTask<> {
    Recorder rec;
    auto&    svc   = Service::get();
    auto     level = ll::service::getLevel();
    ctx.require(static_cast<bool>(level), "level");
    auto spawn = level->getSharedSpawnPos();
    auto sp    = SimulatedPlayer::create("abp_jukebox_leaver", Vec3{spawn.x + 0.5f, spawn.y + 1.0f, spawn.z + 0.5f}, 0);
    ctx.require(static_cast<bool>(sp), "spawn");
    SimulatedPlayer* leaver = sp.as_ptr();
    auto             bp     = ctx.createBackpack(Tier::Leather);
    int              slot   = ctx.installUpgrade(bp, std::string(kBasicId));
    setDisc(UpgradeContext(bp, slot), 0, item(kCat));
    ctx.give(*leaver, ctx.backpackItem(*bp), 0);
    bool ok = svc.play(UpgradeContext(bp, slot), leaver);
    if (!ok) leaver->simulateDisconnect();
    ctx.require(ok, "play");
    leaver->simulateDisconnect();
    co_await ctx.ticks(5);
    ctx.check(!svc.playing(bp->uuid(), slot), "the carrier leaving stops the music");
});

// SC: the play payload goes to every player within 128 blocks, once, when the disc starts; the
// companion gets the entity-attached sound, others the vanilla positional fallback.
ABP_REGISTER_SELFTEST("jukebox.listeners", [](Context& ctx) -> ll::coro::CoroTask<> {
    Recorder rec;
    auto&    svc     = Service::get();
    auto&    carrier = ctx.player();
    auto&    near    = ctx.spawnPlayer();
    auto&    far     = ctx.spawnPlayer();
    auto     s       = carriedJukebox(ctx, kBasicId, {kCat});
    struct CompanionGuard {
        Player& p;
        explicit CompanionGuard(Player& pl) : p(pl) { net::setCompanion(p, net::CompanionInfo{"test", {"native_screen"}}); }
        ~CompanionGuard() { net::setCompanion(p, std::nullopt); }
    } companion(near);
    auto base  = carrier.getFeetPos();
    auto place = [&] {
        carrier.teleport(base, carrier.getDimensionId());
        near.teleport(Vec3{base.x + 6.0f, base.y, base.z}, carrier.getDimensionId());
        far.teleport(Vec3{base.x + 140.0f, base.y, base.z}, carrier.getDimensionId());
    };
    place();
    co_await ctx.ticks(1);
    ctx.require(svc.play(s.ctx(), &carrier), "play");
    auto heard = svc.listeners(s.bp->uuid(), s.slot);
    ctx.check(std::find(heard.begin(), heard.end(), idOf(near)) != heard.end(), "a player 6 blocks away gets it");
    ctx.check(std::find(heard.begin(), heard.end(), idOf(far)) == heard.end(), "a player 140 blocks away doesn't");
    auto const* play = rec.last(SoundEvent::Kind::Play, idOf(near));
    ctx.check(play && play->companion && play->entity == idOf(carrier), "companion: sound attached to the carrier");
    auto const* own = rec.last(SoundEvent::Kind::Play, idOf(carrier));
    ctx.check(own && !own->companion, "no client mod: vanilla positional fallback");
    svc.stop(s.bp->uuid(), s.slot);
    auto const* stop = rec.last(SoundEvent::Kind::Stop, idOf(near));
    ctx.check(stop && stop->companion, "companion: JukeboxStop");
});

// A placed backpack plays at its block; placing a carried one / picking a placed one up stops it.
ABP_REGISTER_SELFTEST("jukebox.placed", [](Context& ctx) -> ll::coro::CoroTask<> {
    Recorder rec;
    auto&    svc = Service::get();
    auto&    sp  = ctx.spawnPlayer();
    co_await placed::test::toGround(ctx, sp);
    placed::test::Site site(sp);
    auto pos = site.at(0, 1);
    site.set(pos, "minecraft:air");
    auto bp   = ctx.createBackpack(Tier::Leather);
    int  slot = ctx.installUpgrade(bp, std::string(kBasicId));
    setDisc(UpgradeContext(bp, slot), 0, item(kCat));
    ctx.require(ctx.give(sp, ctx.backpackItem(*bp), 0) == 0, "carry it");
    ctx.require(svc.play(UpgradeContext(bp, slot), &sp), "play carried");
    ctx.check(svc.emitterEntity(bp->uuid(), slot).has_value(), "attached to the carrier");
    // Place it (the item leaves the inventory).
    sp.getInventory().removeItem(0, 1);
    site.touch(pos);
    auto stack = ctx.backpackItem(*bp);
    ctx.require(placed::PlacedBackpacks::get().place(site.region(), pos, stack, 2) == placed::PlacedBackpacks::PlaceResult::Ok, "place");
    co_await ctx.ticks(6);
    ctx.check(!svc.playing(bp->uuid(), slot), "placing the backpack stops the disc");
    ctx.require(svc.play(UpgradeContext(bp, slot), &sp), "play at the placed backpack");
    ctx.check(!svc.emitterEntity(bp->uuid(), slot).has_value(), "plays at the block");
    auto const* play = rec.last(SoundEvent::Kind::Play, idOf(sp));
    ctx.require(play != nullptr, "a nearby player gets it");
    ctx.check(!play->entity && std::abs(play->pos.x - (static_cast<float>(pos.x) + 0.5f)) < 0.01f, "at the block centre");
    co_await ctx.ticks(30);
    ctx.check(svc.playing(bp->uuid(), slot), "keeps playing while placed");
    (void)placed::PlacedBackpacks::get().take(site.region(), pos);
    ctx.check(!svc.playing(bp->uuid(), slot), "picking it up stops the disc");
});

} // namespace
} // namespace abp::upgrade::jukebox
