#pragma once

// Linked storage (Sophisticated Core `linkedstorage/*`, SB `BackpackLinkedStorage*`) and the Ender
// Linker.
//
// A linked storage group shares one backpack storage between several endpoints: backpack items and
// placed backpacks. The backpack the group was made from is the PRIMARY endpoint; every endpoint
// added later is secondary. All endpoints carry the group's storage uuid (`abp_uuid`), so contents,
// upgrades, upgrade ticks, render info and the jukebox all resolve to the one shared storage, which
// is the primary's. Only the primary can tier-upgrade the storage (Storage tier growth gate).
//
// Endpoint item user data: `abp_link_group`, `abp_link_endpoint`, `abp_link_primary` (byte) and
// `abp_link_name` (group name, for the client tooltip). Ender Linker user data: `abp_linker_group`
// and `abp_linker_name` (a bound linker; none = unlinked). Groups are saved data in the world's
// LevelDB (key `abp:linked`).
//
// Linking (SC's LinkedStorageService):
//   unlinked linker + unlinked backpack  -> new group, the backpack becomes its primary, the linker
//                                           is bound to the group (a stack splits one off)
//   bound linker    + unlinked backpack  -> the backpack (no items, no upgrades) becomes a secondary
//                                           endpoint, the linker is used up
//   unlinked linker + linked endpoint    -> the linker is bound to the endpoint's group
//   anything else                        -> "Storage is already linked"
// Bedrock interactions: right click a backpack slot with the linker on the cursor (or the linker
// slot with a backpack on the cursor) in the backpack screen, or use the linker on a placed
// backpack.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/LinkedGroups.h"

class ItemStack;
class ItemStackBase;
class Player;
class BlockPos;

namespace abp::linked {

inline constexpr std::string_view kLinkerId = "alleviatedbackpacks:ender_linker";

inline constexpr std::string_view kGroupKey        = "abp_link_group";
inline constexpr std::string_view kEndpointKey     = "abp_link_endpoint";
inline constexpr std::string_view kPrimaryKey      = "abp_link_primary";
inline constexpr std::string_view kGroupNameKey    = "abp_link_name";
inline constexpr std::string_view kLinkerGroupKey  = "abp_linker_group";
inline constexpr std::string_view kLinkerNameKey   = "abp_linker_name";

enum class Role { Primary, Secondary };

struct EndpointData {
    std::string group;
    std::string endpoint;
    bool        primary = false;
    std::string name;
};

/// The endpoint data of a backpack item (nullopt = not linked).
[[nodiscard]] std::optional<EndpointData> endpointOf(ItemStackBase const& stack);
[[nodiscard]] bool                        isEndpoint(ItemStackBase const& stack);
/// PRIMARY / secondary as written in the item (SC's LinkedStorageStackLifecycle.getEndpointRole).
[[nodiscard]] std::optional<Role> roleOf(ItemStackBase const& stack);

[[nodiscard]] bool isLinker(ItemStackBase const& stack);
/// The group a bound linker targets (nullopt = unlinked linker / not a linker).
[[nodiscard]] std::optional<std::string> linkerGroup(ItemStackBase const& stack);
[[nodiscard]] std::string                linkerName(ItemStackBase const& stack);

enum class LinkResult { Success, AlreadyLinked, UnsupportedEndpoint, IncompatibleEndpoint, EndpointHasContents };
[[nodiscard]] std::string_view resultName(LinkResult r);

/// Server-side linked storage service (server thread).
class Service {
public:
    static Service& get();

    void install();
    void uninstall();

    /// Links `endpoint` (a backpack item) with `linker` (an Ender Linker stack of count 1) as
    /// described in the header. Both stacks are changed in place; on success with a bound linker +
    /// unlinked backpack the linker is used up (count 0). `owner` is the acting player's uuid.
    LinkResult link(std::string const& owner, ItemStack& linker, ItemStack& endpoint);
    /// (GX-2) The result `link` would have, without linking anything (SC canAddEndpoint /
    /// getAddEndpointResult, used by the crafting recipe).
    [[nodiscard]] LinkResult checkLink(ItemStack const& linker, ItemStack const& endpoint) const;

    /// SB's creative placement: a new secondary endpoint of `endpoint`'s group (nullopt when
    /// `endpoint` is not linked or its group is gone).
    [[nodiscard]] std::optional<ItemStack> createSecondaryEndpointCopy(ItemStack const& endpoint);

    /// Group queries.
    [[nodiscard]] core::LinkedGroup const* group(std::string const& id) const;
    [[nodiscard]] bool isPrimaryEndpoint(std::string const& group, std::string const& endpoint) const;
    [[nodiscard]] bool isEndpointMember(std::string const& group, std::string const& endpoint) const;
    /// Every group's shared storage uuid (protected from maintenance commands).
    [[nodiscard]] std::vector<std::string> storages() const;
    [[nodiscard]] std::size_t              groupCount() const { return mGroups.size(); }

    /// An endpoint was opened (SC records who opened which endpoint last).
    void recordEndpointOpened(std::string const& group, std::string const& endpoint, std::string const& player);

    void load();
    void save();
    void clear();

private:
    core::LinkedGroups mGroups;
    bool               mDirty  = false;
    bool               mLoaded = false;
};

/// Success / failure feedback like SC: success plays the enderman teleport sound (0.5, 1.15) at
/// the player or the block (and witch particles at a block); failure plays a bass note to the
/// player and shows the status message on the action bar.
void feedback(Player& player, LinkResult result, BlockPos const* block);

/// Links with SC's linker-stack handling: a stack of unlinked linkers splits one linker off (the
/// bound one goes to the player's inventory). `linker` and `endpoint` are changed in place; the
/// caller writes them back. Plays the feedback. Returns the result.
LinkResult linkWithFeedback(Player& player, ItemStack& linker, ItemStack& endpoint, BlockPos const* block);

} // namespace abp::linked
