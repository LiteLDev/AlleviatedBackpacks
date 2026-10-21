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
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
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
    type->openSettings(player, s.ctx().withPlayer(&player));
    auto& mm = ui::MenuManager::get();
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
        ctx.check(u->info().hasSettings, std::string(id) + " has a menu");
    }
    auto bp = ctx.createBackpack(Tier::Gold);
    ctx.installUpgrade(bp, std::string(kBasicId));
    ctx.check(
        !upgrade::canInstall(bp, 1, item("alleviatedbackpacks:advanced_jukebox_upgrade"), nullptr),
        "only one jukebox per backpack"
    );
    UpgradeContext uc(bp, 0);
    ctx.check(discSlots(uc) == kBasicSlots, "basic: 1 disc slot");
    ctx.check(repeatMode(uc) == core::RepeatMode::None && !shuffle(uc), "basic plays once, no shuffle");
    auto adv = ctx.createBackpack(Tier::Gold);
    ctx.installUpgrade(adv, std::string(kAdvancedId));
    ctx.check(discSlots(UpgradeContext(adv, 0)) == kAdvancedSlots, "advanced: 12 disc slots");
    ctx.check(repeatMode(UpgradeContext(adv, 0)) == core::RepeatMode::All, "advanced repeats all by default");
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
        ctx.check(!info->key.empty(), id + " has our 2D sound");
        ctx.check(info->durationTicks >= 60 * 20 && info->durationTicks <= 400 * 20, id + " duration " + std::to_string(info->durationTicks));
    }
    ctx.check(found >= 20, "vanilla discs found: " + std::to_string(found));
    auto cat = discInfo(game::ItemConv::make(kCat));
    ctx.require(cat.has_value(), "cat");
    ctx.check(std::abs(cat->durationTicks - 185 * 20) <= 3 * 20, "cat lasts ~3:05 (" + formatTime(cat->durationTicks) + ")");
    ctx.check(followSoundName(*cat) == "abp.jukebox.cat" && blockSoundName(*cat) == "abp.jukebox3d.cat", "sound names");
    ctx.check(!discInfo(game::ItemConv::make("minecraft:stone")), "stone is no disc");
    ctx.check(!discInfo(core::ItemStackData{}), "empty is no disc");
    ctx.check(formatTime(185 * 20) == "3:05", "formatTime");
});

// ---- menu ------------------------------------------------------------------------------------------

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
    ctx.check(occupiedDiscs(s.ctx()) == std::vector<bool>{true, false, false, false, false, false, false, false, false, false, false, false}, "one disc");

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

ABP_REGISTER_SELFTEST("jukebox.buttons", [](Context& ctx) {
    auto& player = ctx.player();
    auto  s      = carriedJukebox(ctx, kAdvancedId, {kCat});
    ctx.require(openMenu(ctx, player, s), "open");
    press(player, 52);
    ctx.check(shuffle(s.ctx()), "shuffle on");
    press(player, 52);
    ctx.check(!shuffle(s.ctx()), "shuffle off");
    press(player, 53);
    ctx.check(repeatMode(s.ctx()) == core::RepeatMode::One, "repeat one");
    press(player, 53);
    ctx.check(repeatMode(s.ctx()) == core::RepeatMode::None, "repeat none");
    press(player, 53);
    ctx.check(repeatMode(s.ctx()) == core::RepeatMode::All, "repeat all");
    press(player, 45);
    ctx.check(ui::MenuManager::get().current(player) != nullptr, "back opens the backpack");
    ui::MenuManager::get().close(player);
});

// ---- playback --------------------------------------------------------------------------------------

ABP_REGISTER_SELFTEST("jukebox.play_stop", [](Context& ctx) -> ll::coro::CoroTask<> {
    Recorder rec;
    auto&    svc    = Service::get();
    auto&    player = ctx.player();
    auto     s      = carriedJukebox(ctx, kBasicId, {kCat});
    ctx.require(openMenu(ctx, player, s), "open");
    press(player, 48);
    ctx.require(svc.playing(s.bp->uuid(), s.slot), "playing after Play");
    auto const* play = rec.last(SoundEvent::Kind::Play, idOf(player));
    ctx.require(play != nullptr, "the carrier got a PlaySound");
    ctx.check(play->sound == "abp.jukebox.cat", "2D record sound: " + play->sound);
    ctx.check(!play->positional && play->volume == 1.0f, "carrier hears it at full volume, in their ears");
    co_await ctx.ticks(25);
    auto st = svc.status(s.bp->uuid(), s.slot);
    ctx.require(st.has_value(), "still playing");
    ctx.check(st->elapsedTicks >= 20, "time advances");
    ctx.check(rec.count(SoundEvent::Kind::Play, idOf(player)) == 1, "no re-sends while playing");
    // (The simulated player may have fallen away from the fake chest meanwhile: reopen the menu.)
    ctx.require(openMenu(ctx, player, s), "reopen");
    press(player, 49);
    ctx.check(!svc.playing(s.bp->uuid(), s.slot), "stopped after Stop");
    auto const* stop = rec.last(SoundEvent::Kind::Stop, idOf(player));
    ctx.check(stop && stop->sound == "abp.jukebox.cat", "StopSound sent");
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
    ctx.require(svc.play(s.ctx(), &player), "play");
    ctx.check(index() == 0, "starts with the first disc");
    co_await ctx.ticks(25);
    ctx.check(index() == 1, "auto-advance to disc 2 (" + std::to_string(index()) + ")");
    co_await ctx.ticks(20);
    ctx.check(index() == 3, "skips the empty slot (" + std::to_string(index()) + ")");
    auto const* play = rec.last(SoundEvent::Kind::Play, idOf(player));
    ctx.check(play && play->sound == "abp.jukebox.far", "far is playing");
    ctx.check(rec.last(SoundEvent::Kind::Stop, idOf(player)) != nullptr, "previous track stopped");
    co_await ctx.ticks(20);
    ctx.check(index() == 0, "repeat all wraps (" + std::to_string(index()) + ")");

    ctx.check(svc.next(s.ctx()) && index() == 1, "next");
    ctx.check(svc.previous(s.ctx()) && index() == 0, "previous");
    ctx.check(svc.previous(s.ctx()) && index() == 3, "previous wraps");

    setRepeatMode(s.ctx(), core::RepeatMode::One);
    co_await ctx.ticks(25);
    ctx.check(index() == 3, "repeat one keeps the disc");

    setRepeatMode(s.ctx(), core::RepeatMode::None);
    co_await ctx.ticks(20);
    ctx.check(!svc.playing(uuid, s.slot), "repeat off stops after the last disc");

    // The playing disc taken out: the next one plays.
    setRepeatMode(s.ctx(), core::RepeatMode::All);
    svc.setDurationOverride(2000);
    ctx.require(svc.play(s.ctx(), &player, 1), "play from disc 2");
    ctx.check(index() == 1, "started at disc 2");
    setDisc(s.ctx(), 1, {});
    co_await ctx.ticks(12);
    ctx.check(index() == 3, "removing the playing disc moves on (" + std::to_string(index()) + ")");
    // Swapping the playing disc for another plays the new one.
    setDisc(s.ctx(), 3, item(kBlocks));
    co_await ctx.ticks(12);
    auto const* swapped = rec.last(SoundEvent::Kind::Play, idOf(player));
    ctx.check(index() == 3 && swapped && swapped->sound == "abp.jukebox.blocks", "swapped disc plays");
    // Repeat off: taking out the last disc of the pass stops.
    setRepeatMode(s.ctx(), core::RepeatMode::None);
    setDisc(s.ctx(), 3, {});
    co_await ctx.ticks(12);
    ctx.check(!svc.playing(uuid, s.slot), "repeat off: removing the last disc stops");
});

ABP_REGISTER_SELFTEST("jukebox.same_disc_two_backpacks", [](Context& ctx) -> ll::coro::CoroTask<> {
    // StopSound works by sound name: stopping one backpack must not leave a listener of another
    // backpack playing the same disc in silence.
    Recorder rec;
    auto&    svc    = Service::get();
    auto&    player = ctx.player();
    auto     a      = carriedJukebox(ctx, kBasicId, {kCat}, 0);
    auto     b      = carriedJukebox(ctx, kBasicId, {kCat}, 1);
    ctx.require(svc.play(a.ctx(), &player) && svc.play(b.ctx(), &player), "play both");
    ctx.check(rec.count(SoundEvent::Kind::Play, idOf(player)) == 2, "two sounds");
    co_await ctx.ticks(20);
    svc.stop(a.bp->uuid(), a.slot);
    co_await ctx.ticks(12);
    ctx.check(rec.count(SoundEvent::Kind::Play, idOf(player)) == 3, "the other backpack's sound is sent again");
    auto const* again = rec.last(SoundEvent::Kind::Play, idOf(player));
    ctx.check(again && again->offset > 0.25f, "resumed at its position");
    svc.stop(b.bp->uuid(), b.slot);
});

ABP_REGISTER_SELFTEST("jukebox.shuffle", [](Context& ctx) -> ll::coro::CoroTask<> {
    Recorder rec(10);
    auto&    svc    = Service::get();
    auto&    player = ctx.player();
    std::vector<std::string_view> discs;
    for (int i = 0; i < kAdvancedSlots; ++i) discs.push_back(vanillaDiscIds()[static_cast<std::size_t>(i)]);
    auto s = carriedJukebox(ctx, kAdvancedId, discs);
    setShuffle(s.ctx(), true);
    ctx.require(svc.play(s.ctx(), &player), "play");
    std::vector<int> seen{svc.status(s.bp->uuid(), s.slot)->index};
    // 12 tracks of 10 ticks: sample every tick for one full pass.
    for (int i = 0; i < 115; ++i) {
        co_await ctx.ticks(1);
        auto st = svc.status(s.bp->uuid(), s.slot);
        ctx.require(st.has_value(), "still playing");
        if (seen.back() != st->index) seen.push_back(st->index);
    }
    auto sorted = seen;
    std::sort(sorted.begin(), sorted.end());
    ctx.check(std::unique(sorted.begin(), sorted.end()) == sorted.end(), "no disc twice in one pass");
    ctx.check(seen.size() >= 10, "went through the discs (" + std::to_string(seen.size()) + ")");
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
    co_await ctx.ticks(15);
    ctx.check(!svc.playing(s.bp->uuid(), s.slot), "dropping the backpack stops the music");
    ctx.check(rec.last(SoundEvent::Kind::Stop, idOf(player)) != nullptr, "StopSound sent to the former carrier");
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

ABP_REGISTER_SELFTEST("jukebox.listeners", [](Context& ctx) -> ll::coro::CoroTask<> {
    Recorder rec;
    auto&    svc       = Service::get();
    struct RangeGuard {
        int saved = config::mut<JukeboxConfig>().range;
        RangeGuard() { config::mut<JukeboxConfig>().range = 16; }
        ~RangeGuard() { config::mut<JukeboxConfig>().range = saved; }
    } guard;
    auto& carrier = ctx.player();
    auto& other   = ctx.spawnPlayer();
    auto  s       = carriedJukebox(ctx, kBasicId, {kCat});
    // Simulated players may be falling (unresolved spawn height): teleport both together so they
    // fall side by side and keep their horizontal distance.
    auto base  = carrier.getFeetPos();
    auto place = [&](float dx) {
        carrier.teleport(base, carrier.getDimensionId());
        other.teleport(Vec3{base.x + dx, base.y, base.z}, carrier.getDimensionId());
    };
    place(6.0f);
    co_await ctx.ticks(1);
    ctx.require(svc.play(s.ctx(), &carrier), "play");
    auto heard = svc.listeners(s.bp->uuid(), s.slot);
    ctx.check(std::find(heard.begin(), heard.end(), idOf(other)) != heard.end(), "a nearby player hears it");
    auto const* play = rec.last(SoundEvent::Kind::Play, idOf(other));
    ctx.check(play && play->volume < 1.0f && play->volume >= 0.25f, "quieter for a player further away");

    place(40.0f);
    co_await ctx.ticks(12);
    heard = svc.listeners(s.bp->uuid(), s.slot);
    ctx.check(std::find(heard.begin(), heard.end(), idOf(other)) == heard.end(), "out of range: stops hearing it");
    ctx.check(rec.last(SoundEvent::Kind::Stop, idOf(other)) != nullptr, "StopSound when leaving the range");

    co_await ctx.ticks(20);
    place(3.0f);
    co_await ctx.ticks(12);
    play = rec.last(SoundEvent::Kind::Play, idOf(other));
    ctx.check(
        rec.count(SoundEvent::Kind::Play, idOf(other)) == 2 && play && play->offset > 1.0f,
        "back in range: joins mid-track (plays=" + std::to_string(rec.count(SoundEvent::Kind::Play, idOf(other)))
            + " offset=" + std::to_string(play ? play->offset : -1.0f) + ")"
    );
    ctx.check(rec.count(SoundEvent::Kind::Play, idOf(carrier)) == 1, "the carrier's sound is never restarted");
    svc.stop(s.bp->uuid(), s.slot);
});

ABP_REGISTER_SELFTEST("jukebox.placed", [](Context& ctx) -> ll::coro::CoroTask<> {
    Recorder rec;
    auto&    svc    = Service::get();
    auto&    player = ctx.player();
    auto     bp     = ctx.createBackpack(Tier::Leather);
    int      slot   = ctx.installUpgrade(bp, std::string(kBasicId));
    setDisc(UpgradeContext(bp, slot), 0, item(kCat));
    std::string reason;
    ctx.check(!svc.play(UpgradeContext(bp, slot), &player, std::nullopt, &reason), "not carried, not placed: refused");
    ctx.check(!reason.empty(), "with a reason");
    auto at  = player.getFeetPos();
    int  dim = static_cast<int>(player.getDimensionId());
    svc.tickPlaced(bp, dim, at);
    ctx.require(svc.play(UpgradeContext(bp, slot), &player), "a placed backpack plays");
    auto const* play = rec.last(SoundEvent::Kind::Play, idOf(player));
    ctx.require(play != nullptr, "a nearby player hears it");
    ctx.check(play->positional && play->sound == "abp.jukebox3d.cat", "3D sound at the block: " + play->sound);
    for (int i = 0; i < 10; ++i) {
        svc.tickPlaced(bp, dim, at);
        co_await ctx.ticks(5);
    }
    ctx.check(svc.playing(bp->uuid(), slot), "keeps playing while placed");
    co_await ctx.ticks(50);
    ctx.check(!svc.playing(bp->uuid(), slot), "stops once the block no longer reports");
});

} // namespace
} // namespace abp::upgrade::jukebox
