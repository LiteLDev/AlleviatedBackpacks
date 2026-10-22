#pragma once

// P2-2: Sophisticated Backpacks' settings screen (SB BackpackSettingsScreen / SC SettingsScreen),
// drawn natively by the backpack screen layer while its view is "settings".
//
// Layout (GUI units, SC values): the storage background (9 or 12 columns, 114 + rows * 18 high,
// scroll bar when the rows don't fit) with the backpack's slots (contents, memory ghosts, no-sort /
// item display stripes) and an empty player inventory area; tabs to the right at
// (left + width, top + 4), 25 apart: Back To Backpack, Backpack (main), Item Display (unless the
// server disables it), No Sort, Memory; template Save / Load / Export buttons at
// (left + inventoryLabelX - 29, top + inventoryLabelY + 29).
//
// Every change is a `{"action":"setting",...}` screen action (feature/gui/SettingsScreen.cpp); the
// screen shows what the server sends back in `model.extra["settings"]`.

#include <functional>
#include <memory>
#include <string>

#include "client/ui/UiLayer.h"
#include "core/screen/ScreenModel.h"

namespace abp::client::ui {
class Painter;
}

namespace abp::client::ui::settings {

class SettingsScreen {
public:
    SettingsScreen();
    ~SettingsScreen();
    SettingsScreen(SettingsScreen const&)            = delete;
    SettingsScreen& operator=(SettingsScreen const&) = delete;

    /// Called when the view switches to the settings screen (tells the server too).
    void open(core::screen::BackpackScreenModel const& model);
    /// Called when the view leaves the settings screen.
    void close();
    [[nodiscard]] bool isOpen() const;

    /// Back To Backpack tab / ESC: the owner switches back to the storage view.
    std::function<void()> onBack;

    void render(Painter& p, core::screen::BackpackScreenModel const& model, FrameInfo const& frame);
    bool mouseButton(core::screen::BackpackScreenModel const& model, MouseButton button, bool down, float x, float y);
    bool mouseMove(float x, float y);
    bool scroll(float x, float y, float delta);
    bool key(int vk, bool down);
    bool chr(std::string const& utf8);
    [[nodiscard]] bool wantsText() const;

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace abp::client::ui::settings
