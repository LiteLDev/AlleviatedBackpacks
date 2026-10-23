#ifdef ABP_CLIENT

// GX-3: the crafting output preview of backpack recipes (client).
//
// SB's backpack recipes compute their result in `assemble` (BackpackUpgradeRecipe /
// BackpackDyeRecipe / SmithingBackpackUpgradeRecipe copy the source backpack's components and mix
// the dye colours), and the result slot shows that stack before anything is taken. Bedrock's
// client fills the preview from the recipe's plain result, and our server only makes the real
// result the same backpack once the craft request is handled (TierCrafting.cpp). So the screens'
// visual stack of the result slot is replaced here by what the craft will produce: the source
// backpack's user data (uuid -> contents tooltip, name, colours) and, for dyeing, the mixed
// colours and tint (Tiers.h `analyse` / `carryOver` rules, without the server storage).

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ll/api/memory/Hook.h"

#include "mc/client/gui/screens/controllers/ContainerScreenController.h"
#include "mc/client/gui/screens/controllers/CraftingScreenController.h"
#include "mc/common/Globals.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/CompoundTagVariant.h"
#include "mc/world/actor/player/PlayerUISlot.h"
#include "mc/world/containers/ContainerEnumName.h"
#include "mc/world/containers/ContainerFactory.h"
#include "mc/world/containers/FullContainerName.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemStackBase.h"

#include "core/Appearance.h"
#include "feature/tiers/Tiers.h"
#include "game/BackpackItem.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace abp::tiers {

namespace {

/// The UI collection name of a container (the game's map; the vanilla UI names when it is empty).
std::string collectionName(ContainerEnumName e) {
    auto const& names = ContainerCollectionNameMap();
    if (auto it = names.find(e); it != names.end()) return it->second;
    switch (e) {
    case ContainerEnumName::CraftingInputContainer:
        return "crafting_input_items";
    case ContainerEnumName::SmithingTableInputContainer:
        return "smithing_table_input_items";
    default:
        return {};
    }
}

/// The container a UI collection belongs to (ContainerFactory, cached).
bool isCollection(std::string const& collection, ContainerEnumName e) {
    static std::unordered_map<std::string, int> cache; // client thread
    auto it = cache.find(collection);
    if (it == cache.end()) {
        int v = -1;
        try {
            v = static_cast<int>(static_cast<ContainerEnumName>(ContainerFactory::getContainerEnumName(collection).mName));
        } catch (...) {
        }
        it = cache.emplace(collection, v).first;
    }
    return it->second == static_cast<int>(e);
}

ItemStack copyOf(ItemStackBase const& base) {
    ItemStack s;
    static_cast<ItemStackBase&>(s) = base;
    return s;
}

std::string fingerprint(ItemStackBase const& s) {
    if (s.isNull()) return "-";
    std::string fp = s.getTypeName() + "#" + std::to_string(s.getAuxValue()) + "x" + std::to_string(s.mCount);
    // What the preview depends on: the source backpack's uuid and colours (cheap: every frame).
    if (s.mUserData && game::backpackTierOf(s.getTypeName())) {
        auto const& tag = *s.mUserData;
        if (tag.contains(game::kUuidKey, Tag::Type::String)) fp += "@" + static_cast<std::string const&>(tag[game::kUuidKey]);
        if (auto c = itemColors(s)) fp += "/" + std::to_string(c->main) + "/" + std::to_string(c->accent);
    }
    return fp;
}

struct Preview {
    std::string key;
    ItemStack   stack;
};
Preview gCraftPreview;    // client thread
Preview gSmithingPreview; // client thread

/// The result `base` as the craft from `inputs` produces it, or nullptr when it is not a backpack
/// craft (or nothing changes).
ItemStackBase const* computePreview(Preview& cache, ItemStackBase const& base, std::vector<ConsumedInput> const& inputs, std::string key) {
    key += "=>" + fingerprint(base);
    if (cache.key == key) return cache.stack.isNull() ? nullptr : &cache.stack;
    cache.key   = key;
    cache.stack = ItemStack{};
    auto craft  = analyse(inputs);
    if (!craft || !craft->source) return nullptr;
    ItemStack out = copyOf(base);
    // carryOver without the server storage: the item's colours (else SB's defaults) are mixed.
    if (craft->source->mUserData) out.setUserData(craft->source->mUserData->clone());
    if (craft->dyes()) {
        auto current  = colorsOf(*craft->source, nullptr);
        auto defaults = defaultColors();
        BackpackColors colors{
            core::calculateColor(current.main, defaults.main, craft->mainDyes),
            core::calculateColor(current.accent, defaults.accent, craft->accentDyes),
        };
        setItemColors(out, colors);
        applyTint(out, colors);
    }
    if (!out.mUserData) return nullptr; // an unopened, undyed backpack: the plain result is right
    cache.stack = std::move(out);
    static std::unordered_set<std::string> logged;
    if (logged.size() < 32 && logged.insert(cache.stack.getTypeName() + (craft->dyes() ? "+dye" : "")).second) {
        auto c = itemColors(cache.stack);
        log().info(
            "craft preview: {} from the source backpack{} (main={} accent={})",
            cache.stack.getTypeName(),
            craft->dyes() ? " (dyed)" : "",
            c ? core::colorHex(c->main) : std::string("-"),
            c ? core::colorHex(c->accent) : std::string("-")
        );
    }
    return &cache.stack;
}

ItemStackBase const* craftingPreview(CraftingScreenController const& self, std::string const& collection, ItemStackBase const& base) {
    if (base.isNull() || !game::backpackTierOf(base.getTypeName())) return nullptr;
    if (!isCollection(collection, ContainerEnumName::CraftingOutputPreviewContainer)) return nullptr;
    bool const  workbench = static_cast<bool>(self.mWorkbench);
    int const   size      = workbench ? 9 : 4;
    int const   first     = static_cast<int>(workbench ? PlayerUISlot::Crafting3x3Input1 : PlayerUISlot::Crafting2x2Input1);
    auto const  input     = collectionName(ContainerEnumName::CraftingInputContainer);
    std::vector<ConsumedInput> inputs;
    std::string                key = workbench ? "3x3" : "2x2";
    for (int i = 0; i < size; ++i) {
        auto const& it = self._getItemStack(input, i);
        key += "|" + fingerprint(it);
        if (it.isNull()) continue;
        inputs.push_back(ConsumedInput{first + i, std::make_shared<ItemStack>(it), true});
    }
    return computePreview(gCraftPreview, base, inputs, std::move(key));
}

/// SB SmithingBackpackUpgradeRecipe: the netherite backpack keeps the diamond one's components.
ItemStackBase const* smithingPreview(ContainerScreenController const& self, std::string const& collection, ItemStackBase const& base) {
    if (base.isNull() || !game::backpackTierOf(base.getTypeName())) return nullptr;
    if (!isCollection(collection, ContainerEnumName::SmithingTableResultPreviewContainer)) return nullptr;
    auto const& it = self._getItemStack(collectionName(ContainerEnumName::SmithingTableInputContainer), 0);
    if (it.isNull() || !game::isBackpack(it)) return nullptr;
    std::vector<ConsumedInput> inputs{ConsumedInput{static_cast<int>(PlayerUISlot::SmithingTableInput), std::make_shared<ItemStack>(it), false}};
    return computePreview(gSmithingPreview, base, inputs, "smithing|" + fingerprint(it));
}
} // namespace

LL_TYPE_INSTANCE_HOOK(
    AbpCraftPreviewHook,
    ll::memory::HookPriority::Normal,
    CraftingScreenController,
    &CraftingScreenController::$_getVisualItemStackImpl,
    ItemStackBase const&,
    std::string const& collectionName,
    int                collectionIndex
) {
    auto const& base = origin(collectionName, collectionIndex);
    try {
        if (auto const* p = craftingPreview(*this, collectionName, base)) return *p;
    } catch (std::exception const& e) {
        log().error("craft preview failed: {}", e.what());
    }
    return base;
}

LL_TYPE_INSTANCE_HOOK(
    AbpSmithingPreviewHook,
    ll::memory::HookPriority::Normal,
    ContainerScreenController,
    &ContainerScreenController::$_getVisualItemStackImpl,
    ItemStackBase const&,
    std::string const& collectionName,
    int                collectionIndex
) {
    auto const& base = origin(collectionName, collectionIndex);
    try {
        if (auto const* p = smithingPreview(*this, collectionName, base)) return *p;
    } catch (std::exception const& e) {
        log().error("smithing preview failed: {}", e.what());
    }
    return base;
}

namespace {
class CraftPreviewModule {
public:
    void install() {
        AbpCraftPreviewHook::hook();
        AbpSmithingPreviewHook::hook();
    }
    void uninstall() {
        AbpSmithingPreviewHook::unhook();
        AbpCraftPreviewHook::unhook();
        gCraftPreview    = {};
        gSmithingPreview = {};
    }
};
} // namespace

} // namespace abp::tiers

ABP_REGISTER_MODULE(abp::tiers::CraftPreviewModule);

#endif // ABP_CLIENT
