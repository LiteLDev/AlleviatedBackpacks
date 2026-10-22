#ifdef ABP_CLIENT

#include "client/ui/screen/ScreenConfig.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/multiplayer/ClientLevel.h"
#include "mc/deps/audio/SoundPlayerInterface.h"
#include "mc/world/level/Level.h"

#include "client/Client.h"
#include "client/ui/widgets/Widgets.h"
#include "mod/Config.h"
#include "mod/Module.h"

ABP_REGISTER_CONFIG("client_gui", abp::client::ui::ClientGuiConfig);

namespace abp::client::ui {

ClientGuiConfig const& clientGuiConfig() { return config::get<ClientGuiConfig>(); }

core::screen::SortButtonsPosition sortButtonsPosition() {
    auto const& s = clientGuiConfig().sortButtonsPosition;
    if (s == "below_upgrades") return core::screen::SortButtonsPosition::BelowUpgrades;
    if (s == "below_upgrade_tabs") return core::screen::SortButtonsPosition::BelowUpgradeTabs;
    if (s == "hidden") return core::screen::SortButtonsPosition::Hidden;
    return core::screen::SortButtonsPosition::TitleLineRight;
}

void playButtonClickSound() {
    if (!clientGuiConfig().playButtonSound) return;
    auto* client = currentClient();
    auto* level  = client ? client->getLevel() : nullptr;
    if (!level) return;
    try {
        // SimpleSoundInstance.forUI(SoundEvents.UI_BUTTON_CLICK, 1.0F)
        level->getSoundPlayer()->playUI("random.click", 1.f, 1.f);
    } catch (...) {
    }
}

namespace {
/// Every SC button (ButtonBase.mouseClicked) plays the click sound.
class ButtonSound {
public:
    void install() { setButtonClickSound(playButtonClickSound); }
    void uninstall() { setButtonClickSound(nullptr); }
};
} // namespace

} // namespace abp::client::ui

ABP_REGISTER_MODULE(abp::client::ui::ButtonSound);

#endif // ABP_CLIENT
