// `/sbp template ...` (alias `/sophisticatedbackpacks`), SB TemplateCommand + DynamicCommand.
// Operators only (SB: permission level 2).
//
//   /sbp template list
//   /sbp template create <templateName> [override]
//   /sbp template delete <templateName>
//   /sbp template give <templateName> [targets]
//   /sbp template export <templateName> [delete]
//   /sbp template dynamic begin <templateName> <backpackItem|baseTemplateName>
//   /sbp template dynamic additem <templateName> [slot] <item> [count]
//   /sbp template dynamic addupgrade <templateName> <item>
//   /sbp template dynamic end <templateName>
//
// The `/sbp` root belongs to the commands unit; until it exists this file creates it on demand
// (`getOrCreateCommand` is idempotent, so both registrations share one command).
// (Bedrock matches command literals in lower case: SB's addItem / addUpgrade are typed as additem / addupgrade.)

#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/command/Optional.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/command/ServerCommandRegisterEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/server/commands/CommandItem.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/CommandSelector.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/dimension/Dimension.h"

#include "feature/templates/Templates.h"
#include "game/Backpack.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "game/Storage.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace abp::feature::templates {

// Command parameter types need external linkage.
struct TplNameParams {
    std::string templateName;
};
struct TplCreateParams {
    std::string                    templateName;
    ll::command::Optional<bool>    override;
};
struct TplGiveParams {
    std::string                                     templateName;
    ll::command::Optional<CommandSelector<Player>>  targets;
};
struct TplExportParams {
    std::string                 templateName;
    ll::command::Optional<bool> del;
};
struct TplBeginParams {
    std::string templateName;
    std::string backpackItemOrTemplate;
};
struct TplAddSlotParams {
    std::string                templateName;
    int                        slot = 0;
    CommandItem                item;
    ll::command::Optional<int> count;
};
struct TplAddParams {
    std::string                templateName;
    CommandItem                item;
    ll::command::Optional<int> count;
};
struct TplAddUpgradeParams {
    std::string templateName;
    CommandItem item;
};

namespace {

Player* playerOf(CommandOrigin const& origin) {
    auto* e = origin.getEntity();
    return (e && e->isPlayer()) ? static_cast<Player*>(e) : nullptr;
}

template <class... Args>
std::string say(CommandOrigin const& origin, std::string_view key, std::string_view fallback, Args const&... args) {
    if (auto* p = playerOf(origin)) return trFor(*p, key, fallback, args...);
    return tr(key, fallback, args...);
}

std::string say(CommandOrigin const& origin, DynamicResult const& r) {
    std::string text = std::string(translate(r.key, r.text, playerOf(origin) ? localeOf(*playerOf(origin)) : std::string{}));
    // Positional ({0}, {1}) and sequential ({}) placeholders with string arguments.
    std::string out;
    std::size_t next = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '{') {
            auto close = text.find('}', i);
            if (close != std::string::npos) {
                auto inner = text.substr(i + 1, close - i - 1);
                std::size_t idx = next;
                bool        ok  = inner.empty();
                if (!ok && inner.find_first_not_of("0123456789") == std::string::npos) {
                    idx = static_cast<std::size_t>(std::stoul(inner));
                    ok  = true;
                }
                if (ok) {
                    if (inner.empty()) ++next;
                    out += idx < r.args.size() ? r.args[idx] : std::string{};
                    i = close;
                    continue;
                }
            }
        }
        out += text[i];
    }
    return out;
}

bool isOp(CommandOrigin const& origin, CommandOutput& output) {
    if (origin.getPermissionsLevel() >= CommandPermissionLevel::GameDirectors) return true;
    output.error(say(origin, "abp.templates.err.permission", "You need operator permission."));
    return false;
}

/// SB giveBackpackToPlayer: into the inventory, else dropped at the player.
void giveTo(Player& p, ItemStack stack) {
    if (!p.add(stack)) p.drop(stack, false);
    p.sendInventory(false);
}

void dropAt(CommandOrigin const& origin, ItemStack const& stack) {
    auto* dim = origin.getDimension();
    if (!dim) return;
    auto& region = dim->getBlockSourceFromMainChunkSource();
    region.getLevel().getSpawner().spawnItem(region, stack, nullptr, origin.getWorldPosition(), 0);
}

bool validName(CommandOrigin const& origin, CommandOutput& output, std::string const& name) {
    if (auto bad = invalidTemplateNameCharacters(name); !bad.empty()) {
        output.error(say(origin, "abp.templates.export.failure.invalid_characters", "Template name contains invalid character(s): {}", bad));
        return false;
    }
    return true;
}

void list(CommandOrigin const& origin, CommandOutput& output) {
    output.success(say(origin, "abp.templates.list.header", "Template Name, Actions"));
    auto line = [&](std::string const& name, bool local) {
        std::string msg = name + ", §a" + say(origin, "abp.templates.list.give", "Give Backpack") + "§r";
        if (local) {
            msg += ", §b" + say(origin, "abp.templates.list.export", "Export Template") + "§r";
            msg += ", §c" + say(origin, "abp.templates.list.delete", "Delete Template") + "§r";
        }
        output.success(msg);
    };
    output.success("Datapack");
    for (auto const& n : packBackpackTemplateNames()) line(n, false);
    output.success("Local");
    for (auto const& n : backpackTemplateNames(false)) line(n, true);
}

void create(CommandOrigin const& origin, CommandOutput& output, std::string const& raw, bool override) {
    auto* player = playerOf(origin);
    if (!player) return;
    if (!validName(origin, output, raw)) return;
    auto  name = normalizeTemplateName(raw);
    auto& held = game::itemAt(*player, game::selectedSlot(*player));
    if (!game::isBackpack(held)) {
        output.error(say(origin, "abp.templates.create.nobackpack", "No backpack in main hand found"));
        return;
    }
    auto uuid = game::backpackUuid(held);
    auto bp   = uuid ? game::Storage::get().find(*uuid) : nullptr;
    bool none = !bp || (bp->inventory().isEmpty()
                        && std::all_of(bp->data().upgrades.begin(), bp->data().upgrades.end(), [](core::UpgradeSlot const& s) { return s.empty(); }));
    if (none) {
        output.error(say(origin, "abp.templates.backpackempty", "Backpack is empty"));
        return;
    }
    auto names = backpackTemplateNames(true);
    if (std::find(names.begin(), names.end(), name) != names.end() && !override) {
        output.error(say(origin, "abp.templates.create.alreadyexists", "Template '{}' already exists", name));
        return;
    }
    setBackpackTemplate(name, held.getTypeName(), backpackContents(*bp));
    log().info("templates: {} created template '{}' from backpack {}", player->getRealName(), name, bp->uuid());
    output.success(say(origin, "abp.templates.create.success", "Template '{}' created successfully", name));
}

void give(CommandOrigin const& origin, CommandOutput& output, std::string const& raw, std::vector<Player*> const& players) {
    auto name = normalizeTemplateName(raw);
    if (!backpackTemplate(name) || !makeTemplateBackpack(name)) {
        output.error(say(origin, "abp.templates.give.failure.notemplate", "The template '{}' does not exist", name));
        return;
    }
    for (auto* p : players) {
        if (auto stack = makeTemplateBackpack(name)) giveTo(*p, *stack);
    }
    log().info("templates: gave template backpack '{}' to {} player(s)", name, players.size());
    if (players.size() == 1) output.success(say(origin, "abp.templates.give.backpack.success", "Gave backpack to {}", players.front()->getRealName()));
    else output.success(say(origin, "abp.templates.give.backpack.success", "Gave backpack to {}", players.size()));
}

void exportCmd(CommandOrigin const& origin, CommandOutput& output, std::string const& raw, bool del) {
    auto name = normalizeTemplateName(raw);
    auto data = localBackpackTemplate(name);
    if (!data) {
        output.error(say(origin, "abp.templates.export.failure.notemplate", "The template '{}' does not exist", name));
        return;
    }
    if (!validName(origin, output, name)) return;
    auto path = exportBackpackTemplate(name, *data);
    if (!path) return;
    if (del) removeBackpackTemplate(name);
    log().info("templates: exported template '{}' to {}", name, *path);
    output.success(say(origin, "abp.templates.export.success", "Exported template to {}", *path));
}

std::optional<core::ItemStackData> itemOf(CommandOrigin const& origin, CommandOutput& output, CommandItem const& item, int count) {
    auto inst = item.createInstance(std::clamp(count, 1, 255), 0, output, false);
    if (!inst || inst->isNull()) {
        output.error(say(origin, "abp.templates.err.item", "Unknown item"));
        return std::nullopt;
    }
    auto stack = game::ItemConv::make(inst->getTypeName(), 1, inst->getAuxValue());
    if (stack.isNull()) return std::nullopt;
    return game::ItemConv::toData(stack).withCount(std::max(1, count));
}

void report(CommandOrigin const& origin, CommandOutput& output, DynamicResult const& r) {
    if (r.ok) output.success(say(origin, r));
    else output.error(say(origin, r));
}

void registerTemplateCommands() {
    if (!ll::service::getCommandRegistry()) return;
    auto& cmd = ll::command::CommandRegistrar::getServerInstance().getOrCreateCommand(
        "sbp",
        "Sophisticated Backpacks",
        CommandPermissionLevel::GameDirectors
    );
    cmd.alias("sophisticatedbackpacks");

    cmd.overload().text("template").text("list").execute([](CommandOrigin const& o, CommandOutput& out) {
        if (isOp(o, out)) list(o, out);
    });
    cmd.overload<TplCreateParams>().text("template").text("create").required("templateName").optional("override").execute(
        [](CommandOrigin const& o, CommandOutput& out, TplCreateParams const& p) {
            if (isOp(o, out)) create(o, out, p.templateName, p.override.has_value() && *p.override);
        }
    );
    cmd.overload<TplNameParams>().text("template").text("delete").required("templateName").execute(
        [](CommandOrigin const& o, CommandOutput& out, TplNameParams const& p) {
            if (!isOp(o, out)) return;
            auto name = normalizeTemplateName(p.templateName);
            removeBackpackTemplate(name);
            out.success(say(o, "abp.templates.delete.success", "Template '{}' deleted successfully", name));
        }
    );
    cmd.overload<TplGiveParams>().text("template").text("give").required("templateName").optional("targets").execute(
        [](CommandOrigin const& o, CommandOutput& out, TplGiveParams const& p) {
            if (!isOp(o, out)) return;
            if (!backpackTemplate(p.templateName)) {
                out.error(say(o, "abp.templates.give.failure.notemplate", "The template '{}' does not exist", normalizeTemplateName(p.templateName)));
                return;
            }
            std::vector<Player*> players;
            if (p.targets.has_value()) {
                for (auto* pl : p.targets->results(o)) players.push_back(pl);
            } else if (auto* self = playerOf(o)) {
                players.push_back(self);
            }
            if (players.empty()) {
                out.error(say(o, "abp.templates.err.no_targets", "No player was found"));
                return;
            }
            give(o, out, p.templateName, players);
        }
    );
    cmd.overload<TplExportParams>().text("template").text("export").required("templateName").optional("del").execute(
        [](CommandOrigin const& o, CommandOutput& out, TplExportParams const& p) {
            if (isOp(o, out)) exportCmd(o, out, p.templateName, p.del.has_value() && *p.del);
        }
    );

    // ---- dynamic -----------------------------------------------------------------------------------
    cmd.overload<TplBeginParams>().text("template").text("dynamic").text("begin").required("templateName").required("backpackItemOrTemplate").execute(
        [](CommandOrigin const& o, CommandOutput& out, TplBeginParams const& p) {
            if (!isOp(o, out)) return;
            auto arg  = p.backpackItemOrTemplate;
            auto tier = game::backpackTierOf(arg);
            if (!tier && arg.find(':') == std::string::npos) tier = game::backpackTierOf(std::string(game::kNamespace) + ":" + arg);
            report(o, out, tier ? dynamicBegin(p.templateName, *tier) : dynamicBeginFromTemplate(p.templateName, arg));
        }
    );
    cmd.overload<TplAddSlotParams>().text("template").text("dynamic").text("additem").required("templateName").required("slot").required("item").optional("count").execute(
        [](CommandOrigin const& o, CommandOutput& out, TplAddSlotParams const& p) {
            if (!isOp(o, out)) return;
            int count = p.count.has_value() ? *p.count : 1;
            if (auto item = itemOf(o, out, p.item, count)) report(o, out, dynamicAdd(p.templateName, p.slot, *item, false));
        }
    );
    cmd.overload<TplAddParams>().text("template").text("dynamic").text("additem").required("templateName").required("item").optional("count").execute(
        [](CommandOrigin const& o, CommandOutput& out, TplAddParams const& p) {
            if (!isOp(o, out)) return;
            int count = p.count.has_value() ? *p.count : 1;
            if (auto item = itemOf(o, out, p.item, count)) report(o, out, dynamicAdd(p.templateName, -1, *item, false));
        }
    );
    cmd.overload<TplAddUpgradeParams>().text("template").text("dynamic").text("addupgrade").required("templateName").required("item").execute(
        [](CommandOrigin const& o, CommandOutput& out, TplAddUpgradeParams const& p) {
            if (!isOp(o, out)) return;
            if (auto item = itemOf(o, out, p.item, 1)) report(o, out, dynamicAdd(p.templateName, -1, *item, true));
        }
    );
    cmd.overload<TplNameParams>().text("template").text("dynamic").text("end").required("templateName").execute(
        [](CommandOrigin const& o, CommandOutput& out, TplNameParams const& p) {
            if (!isOp(o, out)) return;
            auto end = dynamicEnd(p.templateName);
            for (auto const& l : end.leftovers) {
                out.error(say(o, "abp.templates.dynamic.end.addRemainingItemsFailed", "Not all items could be added: Remaining {}", l));
            }
            if (end.item) {
                if (auto* self = playerOf(o)) giveTo(*self, *end.item);
                else dropAt(o, *end.item);
                log().info("templates: dynamic template '{}' ended", p.templateName);
            }
            report(o, out, end.result);
        }
    );
}

class TemplateCommandsModule {
public:
    void install() {
        registerTemplateCommands();
        mListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::command::ServerCommandRegisterEvent>(
            [](auto&) { registerTemplateCommands(); }
        );
    }
    void uninstall() { ll::event::EventBus::getInstance().removeListener(mListener); }

private:
    ll::event::ListenerPtr mListener;
};
} // namespace

} // namespace abp::feature::templates

ABP_REGISTER_MODULE_ORDERED(abp::feature::templates::TemplateCommandsModule, abp::ModuleOrder::Commands + 1);
