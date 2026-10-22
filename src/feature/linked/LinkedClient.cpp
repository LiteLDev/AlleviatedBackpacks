#ifdef ABP_CLIENT

// Client side of linked storage: the item tooltip lines of SC's EnderLinkerItem.appendHoverText
// and the endpoint role header (ClientLinkedStorageTooltip: "Main Linked" / "Linked", with the
// group name). Bedrock hover text only knows the formatting colours, so SC's role colour 0xAE8BC7
// is drawn as the amethyst text colour (§u).

#include "ll/api/memory/Hook.h"

#include "mc/safety/RedactableString.h"
#include "mc/world/item/ItemStackBase.h"
#include "mc/world/item/components/ComponentItem.h"
#include "mc/world/level/Level.h"

#include "client/Client.h"
#include "feature/linked/Linked.h"
#include "mod/Module.h"

namespace abp::linked {

namespace {

constexpr std::string_view kRoleColor = "§u";

void appendLine(std::string& s, std::string const& line) {
    if (s.find(line) != std::string::npos) return;
    if (!s.empty() && s.back() != '\n') s += '\n';
    s += line;
}

void appendLinkedLines(ItemStackBase const& stack, Level& level, Bedrock::Safety::RedactableString& hovertext) {
    if (!level.isClientSide() || stack.isNull()) return;
    auto& s = hovertext.mUnredactedString;
    if (isLinker(stack)) {
        bool bound = linkerGroup(stack).has_value();
        appendLine(
            s,
            "§8"
                + (bound ? client::ctr("abp.linked.tooltip.bound", "Right-click or craft with an unlinked compatible storage to add it")
                         : client::ctr("abp.linked.tooltip.blank", "Right-click a compatible storage or craft with one"))
        );
        auto name = linkerName(stack);
        if (bound && !name.empty()) {
            appendLine(s, "§7" + client::ctr("abp.linked.tooltip.linker_target", "Linked to: {}", std::string(kRoleColor) + name + "§7"));
        }
        return;
    }
    auto d = endpointOf(stack);
    if (!d) return;
    auto role = d->primary ? client::ctr("abp.linked.tooltip.primary", "Main Linked") : client::ctr("abp.linked.tooltip.secondary", "Linked");
    auto text = d->name.empty() ? role
                                : client::ctr(
                                      d->primary ? "abp.linked.tooltip.primary_named" : "abp.linked.tooltip.secondary_named",
                                      "{} ({})",
                                      role,
                                      d->name
                                  );
    appendLine(s, std::string(kRoleColor) + text);
}

} // namespace

LL_TYPE_INSTANCE_HOOK(
    AbpLinkedHoverTextHook,
    ll::memory::HookPriority::Normal,
    ComponentItem,
    &ComponentItem::$appendFormattedHovertext,
    void,
    ::ItemStackBase const&               stack,
    ::Level&                             level,
    ::Bedrock::Safety::RedactableString& hovertext,
    bool                                 showCategory
) {
    origin(stack, level, hovertext, showCategory);
    appendLinkedLines(stack, level, hovertext);
}

namespace {
class LinkedClientModule {
public:
    void install() { AbpLinkedHoverTextHook::hook(); }
    void uninstall() { AbpLinkedHoverTextHook::unhook(); }
};
} // namespace

} // namespace abp::linked

ABP_REGISTER_MODULE(abp::linked::LinkedClientModule);

#endif // ABP_CLIENT
