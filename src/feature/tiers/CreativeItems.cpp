// Creative inventory entries of SB's `BackpackItem.addCreativeTabItems`: right after the plain
// Backpack (Leather tier only, like SB) come 16 dyed ones (main = accent = the dye colour) and one
// with main = yellow + lime and accent = blue + black (SC `calculateColor` from the defaults).
//
// Bedrock builds the creative item list once per world (CreativeItemInitializer); we add our
// stacks next to the plain backpack when the initializer adds it. The client builds its list from
// the server's CreativeContentPacket, so this runs on the (integrated) server only.

#include <vector>

#include "ll/api/memory/Hook.h"

#include "mc/world/item/CreativeItemInitializer.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/registry/CreativeGroupInfo.h"

#include "core/Appearance.h"
#include "core/Tier.h"
#include "feature/tiers/Tiers.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace abp::tiers {

std::vector<BackpackColors> creativeColorVariants() {
    std::vector<BackpackColors> out;
    for (auto const& dye : core::kDyes) out.push_back({dye.argb | 0xFF000000u, dye.argb | 0xFF000000u});
    auto        defaults = defaultColors();
    auto        argb     = [](core::DyeColor c) { return core::dyeInfo(c).argb; };
    std::uint32_t const mainDyes[]   = {argb(core::DyeColor::Yellow), argb(core::DyeColor::Lime)};
    std::uint32_t const accentDyes[] = {argb(core::DyeColor::Blue), argb(core::DyeColor::Black)};
    out.push_back({core::calculateColor(defaults.main, defaults.main, mainDyes), core::calculateColor(defaults.accent, defaults.accent, accentDyes)});
    return out;
}

std::vector<ItemStack> creativeBackpackVariants() {
    std::vector<ItemStack> out;
    for (auto colors : creativeColorVariants()) {
        auto stack = game::makeBackpackItem(core::Tier::Leather);
        if (stack.isNull()) break;
        setItemColors(stack, colors);
        applyTint(stack, colors);
        out.push_back(std::move(stack));
    }
    return out;
}

namespace {

bool gAdding = false; ///< our own calls back into the initializer

bool isPlainLeatherBackpack(ItemInstance const& item) {
    return !item.isNull() && !item.mUserData && item.getTypeName() == game::backpackItemId(core::Tier::Leather);
}

} // namespace

LL_TYPE_INSTANCE_HOOK(
    CreativeAddServerHook,
    ll::memory::HookPriority::Normal,
    CreativeItemInitializer,
    &CreativeItemInitializer::_addCreativeItemServer,
    void,
    CreativeGroupInfo&  group,
    ItemInstance const& item
) {
    origin(group, item);
    if (gAdding || !isPlainLeatherBackpack(item)) return;
    gAdding = true;
    try {
        for (auto const& stack : creativeBackpackVariants()) origin(group, ItemInstance(stack));
    } catch (std::exception const& e) {
        log().error("creative backpack variants failed: {}", e.what());
    }
    gAdding = false;
}

LL_TYPE_INSTANCE_HOOK(
    CreativeAddLooseHook,
    ll::memory::HookPriority::Normal,
    CreativeItemInitializer,
    &CreativeItemInitializer::_addLooseCreativeItem,
    void,
    ItemInstance const& item
) {
    bool outer = !gAdding;
    if (outer) gAdding = true; // `_addCreativeItemServer` reached from here is handled below
    origin(item);
    if (!outer) return;
    if (isPlainLeatherBackpack(item)) {
        try {
            for (auto const& stack : creativeBackpackVariants()) origin(ItemInstance(stack));
        } catch (std::exception const& e) {
            log().error("creative backpack variants failed: {}", e.what());
        }
    }
    gAdding = false;
}

/// Installed at mod load: the creative list is built when the world is created (before mods are
/// enabled on BDS).
class CreativeItems {
public:
    void load() { hook(); }
    void install() { hook(); }
    void uninstall() {
        if (!mHooked) return;
        CreativeAddServerHook::unhook();
        CreativeAddLooseHook::unhook();
        mHooked = false;
    }

private:
    void hook() {
        if (mHooked) return;
        CreativeAddServerHook::hook();
        CreativeAddLooseHook::hook();
        mHooked = true;
    }
    bool mHooked = false;
};

} // namespace abp::tiers

ABP_REGISTER_MODULE(abp::tiers::CreativeItems);
