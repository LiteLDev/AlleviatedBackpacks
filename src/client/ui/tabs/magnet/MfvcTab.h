#pragma once

// Shared by the native tabs of the Magnet, Feeding, Void and Compacting upgrades.

#ifdef ABP_CLIENT

#include "client/ui/screen/Tabs.h"
#include "mod/Log.h"

namespace abp::client::ui::tabs {

/// Logs where an open tab and its filter control are, in GUI units (the e2e scenario `mfvc.ps1`
/// clicks from these values), and returns `size`.
inline NativeTab::Size logTabBuilt(TabContext const& ctx, Rect area, NativeTab::Size size, float controlY) {
    log().info(
        "mfvc tab: built {} at {},{} size {}x{} control {},{}",
        ctx.upgrade.id,
        area.x,
        area.y,
        size.w,
        size.h,
        area.x + 3,
        area.y + controlY
    );
    return size;
}

} // namespace abp::client::ui::tabs

#endif // ABP_CLIENT
