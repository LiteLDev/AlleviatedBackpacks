// Worn backpacks: backpack items are wearable in the chest armor slot (BP `minecraft:wearable`)
// and rendered on the back by RP attachables. The foundation already ticks upgrades of the chest
// slot (PlayerScan). This module adds ways to open the worn backpack, since Bedrock has no custom
// key bindings (Sophisticated Backpacks uses a key):
//   - sneak twice quickly (config `worn.doubleSneakToOpen`),
//   - `/backpack worn`.

#include <unordered_map>

#include "ll/api/command/CommandHandle.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"

#include "feature/wearable/Wearable.h"
#include "feature/wearable/WearableConfig.h"
#include "game/BackpackItem.h"
#include "mod/Commands.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Module.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"

namespace abp::wearable {

game::ItemLocation wornSlot() {
    return {game::ItemLocation::Kind::Armor, static_cast<int>(SharedTypes::Legacy::ArmorSlot::Torso)};
}

bool openWorn(Player& player) {
    if (!game::isBackpack(game::itemAt(player, wornSlot()))) return false;
    return ui::BackpackMenu::openFor(player, wornSlot());
}

namespace {

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable" // LL hook macros declare an unused counter

// Backpack items are wearables now, and the wearable component equips on use inside
// `GameMode::useItem` (before LeviLamina's PlayerUseItemEvent, which the foundation's opener
// cancels). Using a backpack must keep opening it, as in Sophisticated Backpacks: wearing is done
// by putting it into the chest slot.
LL_TYPE_INSTANCE_HOOK(
    BackpackUseHook,
    ll::memory::HookPriority::High,
    GameMode,
    &GameMode::$useItem,
    bool,
    ItemStack& item,
    HandSlot   handSlot
) {
    Player& player = mPlayer;
    if (player.isClientSide() || !game::isBackpack(item)) return origin(item, handSlot);
    if (!ui::MenuManager::get().current(player)) ui::BackpackMenu::openFor(player, game::selectedSlot(player));
    return false;
}

#pragma clang diagnostic pop

class UseOpens {
public:
    void install() { BackpackUseHook::hook(); }
    void uninstall() { BackpackUseHook::unhook(); }
};

/// Detects double sneak presses by polling the sneaking flag every tick (modern clients report
/// sneaking through PlayerAuthInputPacket, so there is no reliable sneak event).
class DoubleSneak {
public:
    void install() {
        mListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>(
            [this](ll::event::ServerLevelTickEvent& ev) { tick(ev.level()); }
        );
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mListener);
        mStates.clear();
    }

private:
    struct State {
        bool         sneaking  = false;
        std::int64_t lastPress = -1000;
        std::int64_t seen      = 0;
    };

    void tick(Level& level) {
        ++mNow;
        auto const& c = config::get<WearableConfig>().worn;
        if (!c.doubleSneakToOpen) {
            mStates.clear();
            return;
        }
        level.forEachPlayer([&](Player& player) {
            if (player.isClientSide()) return true;
            auto& st       = mStates[player.getOrCreateUniqueID().rawID];
            bool  sneaking = player.isSneaking();
            if (sneaking && !st.sneaking) {
                if (mNow - st.lastPress <= c.doubleSneakWindowTicks) {
                    st.lastPress = -1000;
                    if (player.isAlive() && !ui::MenuManager::get().current(player)) openWorn(player);
                } else {
                    st.lastPress = mNow;
                }
            }
            st.sneaking = sneaking;
            st.seen     = mNow;
            return true;
        });
        if (mNow % 200 == 0) std::erase_if(mStates, [&](auto const& e) { return mNow - e.second.seen > 200; });
    }

    ll::event::ListenerPtr                    mListener;
    std::unordered_map<std::int64_t, State>   mStates;
    std::int64_t                              mNow = 0;
};

} // namespace
} // namespace abp::wearable

ABP_REGISTER_MODULE(abp::wearable::DoubleSneak);
ABP_REGISTER_MODULE(abp::wearable::UseOpens);

ABP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    cmd.overload().text("worn").execute([](CommandOrigin const& origin, CommandOutput& output) {
        auto* e = origin.getEntity();
        if (!e || !e->isPlayer()) return output.error("Only players can open backpacks.");
        auto& player = *static_cast<Player*>(e);
        if (!abp::wearable::openWorn(player)) {
            return output.error(abp::trFor(player, "abp.wearable.no_worn", "You are not wearing a backpack."));
        }
        output.success(abp::trFor(player, "abp.wearable.opened", "Worn backpack opened."));
    });
});
