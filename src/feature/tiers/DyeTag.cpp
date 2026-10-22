// The item tag "alleviatedbackpacks:dye" = the 16 dyes (Java's `c:dyes`), used by the backpack dye
// recipes (assets/pack/bp/recipes/dye/). Bedrock has no dye item tag and behavior packs can't tag
// vanilla items, so the mod adds it when the game registers its hard-coded item tags, before
// recipes are loaded (recipe loading checks that the tag exists). This runs in both builds: the
// client matches recipes itself to show the crafting output and the recipe book.

#include <string>

#include "ll/api/memory/Hook.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/VanillaItems.h"
#include "mc/world/item/registry/ItemRegistryRef.h"
#include "mc/world/item/registry/TagUpdateToken.h"

#include "core/Appearance.h"
#include "feature/tiers/Tiers.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace abp::tiers {

// Registers the tag for real: the 16 dye items get it and join the registry's tag map, right where
// the game registers its hard-coded item tags (before recipes are loaded, on both sides).
LL_STATIC_HOOK(
    DyeTagRegisterHook,
    ll::memory::HookPriority::Normal,
    &VanillaItems::registerHardCodedItemTags,
    void,
    ItemRegistryRef itemRegistry
) {
    origin(itemRegistry);
    try {
        auto token = itemRegistry.allowTagUpdate();
        // The game keeps pointers to HashedStrings it compared against (`mLastMatch`): the tag must
        // outlive the registry.
        static HashedString const tag{std::string(kDyeTag)};
        for (auto const& d : core::kDyes) {
            auto item = itemRegistry.lookupByName(HashedString("minecraft:" + std::string(d.id) + "_dye"));
            if (!item) continue;
            if (!item->hasTag(tag)) item->addTag(tag);
            itemRegistry.addItemToTagMap(*item);
        }
    } catch (std::exception const& e) {
        log().error("registering the dye item tag failed: {}", e.what());
    }
}

/// Installed at mod load: recipes are loaded (and validated) when the world is created, which on
/// BDS happens before mods are enabled.
class DyeTag {
public:
    void load() { hook(); }
    void install() { hook(); }
    void uninstall() {
        if (!mHooked) return;
        DyeTagRegisterHook::unhook();
        mHooked = false;
    }

private:
    void hook() {
        if (mHooked) return;
        DyeTagRegisterHook::hook();
        mHooked = true;
    }
    bool mHooked = false;
};

} // namespace abp::tiers

ABP_REGISTER_MODULE(abp::tiers::DyeTag);
