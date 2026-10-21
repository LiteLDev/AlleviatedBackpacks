#include "upgrade/anvil/AnvilLogic.h"

#include <algorithm>

#include "ll/api/service/Bedrock.h"

#include "mc/safety/RedactableString.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"
#include "mc/world/item/enchanting/ItemEnchants.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/storage/LevelData.h"

#include "upgrade/anvil/AnvilMath.h"

namespace abp::upgrade::anvil {

namespace {

bool isBlank(std::string const& s) {
    return std::all_of(s.begin(), s.end(), [](unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; });
}

bool hasEnchants(ItemStackBase const& s) { return !s.constructItemEnchantsFromUserData().getAllEnchants().empty(); }

bool validRepairItem(ItemStack const& left, ItemStack const& right) {
    auto* item  = left.mItem.get();
    auto  level = ll::service::getLevel();
    if (!item || !level) return false;
    return item->isValidRepairItem(left, right, level->getLevelData().getBaseGameVersion());
}

} // namespace

AnvilOutcome compute(ItemStack const& left, ItemStack const& right, std::optional<std::string> const& name, bool creative) {
    AnvilOutcome out;
    if (left.isNull()) return out;

    ItemStack result    = left;
    int       work      = 0;

    if (!right.isNull()) {
        bool  rightIsBook = right.isEnchantingBook();
        auto* item        = result.mItem.get();
        int   maxDamage   = item ? item->getMaxDamage() : 0;
        if (result.isDamageableItem() && maxDamage > 0 && right.mItem.get() != item && validRepairItem(left, right)) {
            // Repair with material (diamonds for a diamond sword, ...).
            auto r = math::repairWithMaterial(result.getDamageValue(), maxDamage, right.mCount);
            if (r.unitsUsed <= 0) return out;
            result.setDamageValue(static_cast<short>(r.newDamage));
            work          += r.unitsUsed;
            out.rightUsed  = r.unitsUsed;
        } else {
            // Combining: an enchanted book, or two items of the same kind.
            bool sameKind = result.isDamageableItem() && right.mItem.get() == item;
            if (!rightIsBook && !sameKind && !(left.isEnchantingBook() && rightIsBook)) return out;
            bool repaired = false;
            if (sameKind && !rightIsBook) {
                int dmg = math::combinedDamage(maxDamage, left.getDamageValue(), right.getDamageValue());
                if (dmg < result.getDamageValue()) {
                    result.setDamageValue(static_cast<short>(dmg));
                    work     += 2;
                    repaired  = true;
                }
            }
            ItemStack before    = result;
            int       enchCost  = EnchantUtils::combineEnchantedItems(before, right, result, rightIsBook);
            if (enchCost < 0) enchCost = 0;
            // Nothing of the right item's enchantments applies (incompatible or not enchantable)
            // and nothing was repaired: no result, like vanilla.
            if (enchCost == 0 && !repaired && (rightIsBook || hasEnchants(right))) return out;
            if (enchCost > 0 && left.mCount > 1) enchCost = math::kTooExpensive; // stacks can't be enchanted
            work          += enchCost;
            out.rightUsed  = (repaired || enchCost > 0) ? right.mCount : 0; // untouched when nothing applied
        }
    }

    // Prior work penalty: only inputs that take part (an untouched right item is not consumed).
    int rightCost = out.rightUsed > 0 ? right.getBaseRepairCost() : 0;
    int priorWork = left.getBaseRepairCost() + rightCost;

    int renameCost = 0;
    if (name) {
        auto current = left.getCustomName();
        if (isBlank(*name)) {
            if (!current.empty()) {
                renameCost = 1;
                result.resetHoverName();
            }
        } else if (*name != current) {
            renameCost = 1;
            result.setCustomName(Bedrock::Safety::RedactableString(math::truncateUtf8(*name, kMaxNameLength)));
        }
    }
    work += renameCost;
    if (work <= 0) return out;

    auto cost        = math::finalCost(priorWork, work, renameCost, creative);
    out.cost         = cost.levels;
    out.tooExpensive = cost.tooExpensive;
    out.renameOnly   = renameCost > 0 && renameCost == work;
    result.setRepairCost(math::nextRepairCost(
        left.getBaseRepairCost(),
        rightCost,
        out.renameOnly
    ));
    out.result = std::move(result);
    return out;
}

bool isAnvilSubject(ItemStack const& stack) {
    return !stack.isNull() && (stack.isDamageableItem() || stack.isEnchantingBook() || hasEnchants(stack));
}

int playerLevel(Player& player) {
    auto attr = player.getAttribute(Player::LEVEL());
    if (!attr.mPtr) return 0;
    return static_cast<int>(attr.mPtr->mCurrentValue);
}

bool canAfford(Player& player, AnvilOutcome const& outcome) {
    if (!outcome.hasResult()) return false;
    if (player.isCreative()) return true;
    return !outcome.tooExpensive && playerLevel(player) >= outcome.cost;
}

} // namespace abp::upgrade::anvil
