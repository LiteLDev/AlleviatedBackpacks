// Tank upgrade: a fluid tank inside the backpack (SB "Tank Upgrade").
//
// Settings JSON (travels with the upgrade item):
//   "tank": {"fluid": "<fluid id>", "amount": <mB>}
//   "in":   ItemStackData (the input slot: what is left after pouring filled containers in)
//   "out":  ItemStackData (the output slot: containers filled from the tank)

#include "upgrade/tank/Tank.h"

#include <algorithm>
#include <cmath>

#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "core/Format.h"
#include "selftest/SelfTest.h"
#include "game/ItemConv.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/tank/MenuBits.h"

namespace abp::upgrade::tank {

namespace fl = core::fluid;

/// `config/tank.json`
struct TankConfig {
    int version = 1;
    /// Tank capacity in mB per 9 storage slots of the backpack (multiplied by stack upgrades).
    int capacityPerSlotRow = fl::kPerSlotRow;
};

// ---- tank access --------------------------------------------------------------------------------

int stackMultiplier(game::Backpack& backpack) {
    auto& inv = backpack.inventory();
    if (inv.size() <= 0) return 1;
    auto probe = core::ItemStackData::of("minecraft:stone", 1, 64);
    return std::max(1, inv.slotLimit(0, probe) / 64);
}

int capacity(game::Backpack& backpack) {
    return fl::tankCapacity(backpack.inventory().size(), stackMultiplier(backpack), config::get<TankConfig>().capacityPerSlotRow);
}

bool isTank(UpgradeContext const& ctx) {
    auto const& slots = ctx.backpack().data().upgrades;
    if (ctx.slot() < 0 || ctx.slot() >= static_cast<int>(slots.size())) return false;
    return slots[static_cast<std::size_t>(ctx.slot())].item.id == kItemId;
}

std::vector<UpgradeContext> tanks(std::shared_ptr<game::Backpack> const& backpack) {
    std::vector<UpgradeContext> out;
    if (!backpack) return out;
    auto const& slots = backpack->data().upgrades;
    for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
        if (slots[static_cast<std::size_t>(i)].item.id == kItemId) out.emplace_back(backpack, i);
    }
    return out;
}

fl::Tank read(UpgradeContext const& ctx) {
    if (!isTank(ctx)) return {};
    auto const& s = ctx.settings();
    return fl::Tank::fromJson(s.is_object() ? s.value("tank", nlohmann::json::object()) : nlohmann::json::object(), capacity(ctx.backpack()));
}

void write(UpgradeContext const& ctx, fl::Tank const& tank) {
    if (!isTank(ctx)) return;
    auto& s = ctx.settings();
    if (!s.is_object()) s = nlohmann::json::object();
    s["tank"] = tank.toJson();
    ctx.saveSettings();
}

int fill(std::shared_ptr<game::Backpack> const& backpack, FluidStack const& stack, bool simulate, bool exact) {
    if (stack.empty()) return 0;
    auto all = tanks(backpack);
    if (exact && !simulate && fill(backpack, stack, true, false) < stack.amount) return 0;
    int left = stack.amount;
    // Pass 1: tanks already holding this fluid; pass 2: empty tanks.
    for (int pass = 0; pass < 2 && left > 0; ++pass) {
        for (auto const& ctx : all) {
            if (left <= 0) break;
            auto t = read(ctx);
            if ((pass == 0) == t.empty()) continue;
            int n = t.fill({stack.id, left}, simulate);
            if (n <= 0) continue;
            left -= n;
            if (!simulate) write(ctx, t);
        }
    }
    int done = stack.amount - left;
    if (exact && done < stack.amount) return 0;
    return done;
}

FluidStack drain(std::shared_ptr<game::Backpack> const& backpack, std::string_view fluid, int max, bool simulate, bool exact) {
    if (max <= 0) return {};
    auto        all = tanks(backpack);
    std::string id(fluid);
    if (id.empty()) {
        for (auto const& ctx : all) {
            auto t = read(ctx);
            if (!t.empty()) {
                id = t.fluid();
                break;
            }
        }
        if (id.empty()) return {};
    }
    if (exact && !simulate && drain(backpack, id, max, true, false).amount < max) return {};
    int left = max;
    for (auto const& ctx : all) {
        if (left <= 0) break;
        auto t   = read(ctx);
        auto got = t.drain(id, left, simulate);
        if (got.empty()) continue;
        left -= got.amount;
        if (!simulate) write(ctx, t);
    }
    int done = max - left;
    if (done <= 0 || (exact && done < max)) return {};
    return {id, done};
}

long long amountOf(std::shared_ptr<game::Backpack> const& backpack, std::string_view fluid) {
    long long n = 0;
    for (auto const& ctx : tanks(backpack)) {
        auto t = read(ctx);
        if (!t.empty() && t.fluid() == fluid) n += t.amount();
    }
    return n;
}

std::vector<std::string> fluids(std::shared_ptr<game::Backpack> const& backpack) {
    std::vector<std::string> out;
    for (auto const& ctx : tanks(backpack)) {
        auto t = read(ctx);
        if (!t.empty() && std::find(out.begin(), out.end(), t.fluid()) == out.end()) out.push_back(t.fluid());
    }
    return out;
}

// ---- containers ---------------------------------------------------------------------------------

namespace {
core::ItemStackData itemData(std::string_view id, int aux) {
    auto stack = game::ItemConv::make(id, 1, aux);
    if (stack.isNull()) return {};
    auto d  = game::ItemConv::toData(stack);
    d.count = 1;
    return d;
}
} // namespace

std::optional<core::ItemStackData> emptyContainer(core::ItemStackData const& filled, FillFn const& fill, bool simulate) {
    if (filled.empty() || !fill) return std::nullopt;
    auto contents = fl::contentsOf(filled.id, filled.aux);
    if (!contents) return std::nullopt;
    auto empty = itemData(contents->emptyId, 0);
    if (empty.empty()) return std::nullopt;
    if (fill(contents->fluid, true) < contents->fluid.amount) return std::nullopt;
    if (!simulate && fill(contents->fluid, false) < contents->fluid.amount) return std::nullopt;
    return empty;
}

std::optional<core::ItemStackData>
fillContainer(core::ItemStackData const& empty, std::string_view fluid, DrainFn const& drain, bool simulate) {
    if (empty.empty() || fluid.empty() || !drain) return std::nullopt;
    auto filled = fl::fill(empty.id, fluid);
    if (!filled) return std::nullopt;
    auto item = itemData(filled->item.id, filled->item.aux);
    if (item.empty()) return std::nullopt;
    if (drain(fluid, filled->amount, true).amount < filled->amount) return std::nullopt;
    if (!simulate && drain(fluid, filled->amount, false).amount < filled->amount) return std::nullopt;
    return item;
}

FillFn fillerOf(UpgradeContext const& ctx) {
    return [ctx](FluidStack const& s, bool simulate) {
        auto t = read(ctx);
        if (!isTank(ctx) || t.fill(s, true, true) < s.amount) return 0;
        if (simulate) return s.amount;
        int n = t.fill(s, false, true);
        write(ctx, t);
        return n;
    };
}

DrainFn drainerOf(UpgradeContext const& ctx) {
    return [ctx](std::string_view fluid, int amount, bool simulate) -> FluidStack {
        auto t = read(ctx);
        if (!isTank(ctx)) return {};
        auto got = t.drain(fluid, amount, simulate, true);
        if (!simulate && !got.empty()) write(ctx, t);
        return got;
    };
}

FillFn fillerOf(std::shared_ptr<game::Backpack> const& backpack) {
    return [backpack](FluidStack const& s, bool simulate) { return fill(backpack, s, simulate, true); };
}

DrainFn drainerOf(std::shared_ptr<game::Backpack> const& backpack) {
    return [backpack](std::string_view fluid, int amount, bool simulate) { return drain(backpack, fluid, amount, simulate, true); };
}

// ---- display ------------------------------------------------------------------------------------

namespace {
std::string withSeparators(long long n) {
    auto s     = std::to_string(n < 0 ? -n : n);
    int  first = static_cast<int>(s.size()) % 3;
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (i != 0 && (static_cast<int>(i) - first) % 3 == 0) out += ',';
        out += s[i];
    }
    return n < 0 ? "-" + out : out;
}

template <class... Args>
std::string text(Player const* p, std::string_view key, std::string_view fallback, Args const&... args) {
    return p ? trFor(*p, key, fallback, args...) : tr(key, fallback, args...);
}

std::string_view gaugePane(std::string_view fluid) {
    auto fam = fl::family(fluid);
    if (fam == fl::kWater) return "minecraft:blue_stained_glass_pane";
    if (fam == fl::kLava) return "minecraft:orange_stained_glass_pane";
    if (fam == fl::kMilk) return "minecraft:white_stained_glass_pane";
    if (fam == fl::kPowderSnow) return "minecraft:light_blue_stained_glass_pane";
    if (fam == fl::kHoney) return "minecraft:yellow_stained_glass_pane";
    if (fam == fl::kExperience) return "minecraft:lime_stained_glass_pane";
    return "minecraft:magenta_stained_glass_pane"; // potions
}
} // namespace

std::string fluidName(Player const* player, std::string_view fluid) {
    auto fam = fl::family(fluid);
    if (fam == "potion") return text(player, "abp.tank.fluid.potion", "Potion #{}", fl::potionAux(fluid));
    if (fam == "splash_potion") return text(player, "abp.tank.fluid.splash_potion", "Splash Potion #{}", fl::potionAux(fluid));
    if (fam == "lingering_potion") {
        return text(player, "abp.tank.fluid.lingering_potion", "Lingering Potion #{}", fl::potionAux(fluid));
    }
    if (fluid == fl::kWater) return text(player, "abp.tank.fluid.water", "Water");
    if (fluid == fl::kLava) return text(player, "abp.tank.fluid.lava", "Lava");
    if (fluid == fl::kMilk) return text(player, "abp.tank.fluid.milk", "Milk");
    if (fluid == fl::kPowderSnow) return text(player, "abp.tank.fluid.powder_snow", "Powder Snow");
    if (fluid == fl::kHoney) return text(player, "abp.tank.fluid.honey", "Honey");
    if (fluid == fl::kExperience) return text(player, "abp.tank.fluid.experience", "Experience");
    return std::string(fluid);
}

std::string describe(Player const* player, fl::Tank const& tank) {
    if (tank.empty()) return text(player, "abp.tank.empty", "§7Empty §8({} mB)", withSeparators(tank.capacity()));
    return text(
        player,
        "abp.tank.contents",
        "§b{}: §f{} §7/ {} mB",
        fluidName(player, tank.fluid()),
        withSeparators(tank.amount()),
        withSeparators(tank.capacity())
    );
}

ItemStack fluidIcon(std::string_view fluid, std::string const& name, std::vector<std::string> const& lore) {
    ItemStack stack;
    if (fluid.empty()) stack = game::ItemConv::make("minecraft:barrier", 1, 0);
    else if (fluid == fl::kExperience) stack = game::ItemConv::make("minecraft:experience_bottle", 1, 0);
    else if (auto item = fl::displayItem(fluid)) stack = game::ItemConv::make(item->id, 1, item->aux);
    if (stack.isNull()) stack = game::ItemConv::make("minecraft:bucket", 1, 0);
    game::ItemConv::setDisplay(stack, name.empty() ? std::string{} : "§r" + name, lore);
    return stack;
}

// ---- menu ---------------------------------------------------------------------------------------

namespace {

/// The tank's input (pour filled containers in) or output (fill empty containers) slot. The
/// result stays in the slot until the player takes it, as in SB.
class ContainerSlot : public ui::Slot {
public:
    ContainerSlot(UpgradeContext ctx, bool input) : mCtx(std::move(ctx)), mInput(input) {}

    [[nodiscard]] ItemStack display(Player&) const override {
        auto c = content();
        return c.empty() ? ItemStack{} : game::ItemConv::toStack(c, std::min(c.count, 255));
    }

    ItemStack take(Player&, int amount) override {
        auto c = content();
        if (c.empty() || amount <= 0) return {};
        int n = std::min({amount, c.count, 255});
        auto out = game::ItemConv::toStack(c, n);
        if (!mTaken.empty() && mTaken.sameItem(c)) mTaken.count += n;
        else mTaken = c.withCount(n);
        c.count -= n;
        store(c);
        return out;
    }

    int accept(Player&, ItemStack const& stack) override { return offer(game::ItemConv::toData(stack)); }

    /// Processes (or merges back) `offered`; returns how many were accepted.
    int offer(core::ItemStackData offered) {
        if (offered.empty() || !isTank(mCtx)) return 0;
        auto c = content();
        // The framework handing back what it took (a rest that didn't fit, a failed swap): put it
        // back unprocessed. Only what was taken out can come back, so the slots never become extra
        // storage.
        if (!mTaken.empty() && mTaken.sameItem(offered) && (c.empty() || c.sameItem(offered))) {
            int room = std::max(0, std::max(1, offered.maxStack) - (c.empty() ? 0 : c.count));
            int n    = std::min({offered.count, mTaken.count, room});
            if (n > 0) {
                mTaken.count -= n;
                if (mTaken.count <= 0) mTaken.clear();
                store(c.empty() ? offered.withCount(n) : c.withCount(c.count + n));
                return n;
            }
        }
        int done = 0;
        for (; done < offered.count; ++done) {
            auto one    = offered.withCount(1);
            auto result = process(one, true);
            if (!result) break;
            if (!c.empty() && (!c.sameItem(*result) || c.count >= c.maxStack)) break;
            result = process(one, false);
            if (!result) break;
            if (c.empty()) c = *result;
            else c.count += 1;
        }
        if (done > 0) store(c);
        return done;
    }

    [[nodiscard]] core::ItemStackData content() const {
        if (!isTank(mCtx)) return {};
        auto const& s = mCtx.settings();
        if (!s.is_object() || !s.contains(key())) return {};
        return core::ItemStackData::fromJson(s.at(key()));
    }

private:
    [[nodiscard]] char const* key() const { return mInput ? "in" : "out"; }

    std::optional<core::ItemStackData> process(core::ItemStackData const& one, bool simulate) const {
        if (mInput) return emptyContainer(one, fillerOf(mCtx), simulate);
        auto t = read(mCtx);
        if (t.empty()) return std::nullopt;
        return fillContainer(one, t.fluid(), drainerOf(mCtx), simulate);
    }

    void store(core::ItemStackData const& c) const {
        auto& s = mCtx.settings();
        if (!s.is_object()) s = nlohmann::json::object();
        if (c.empty()) s.erase(key());
        else s[key()] = c.toJson();
        mCtx.saveSettings();
    }

    UpgradeContext mCtx;
    bool           mInput;
    core::ItemStackData mTaken; ///< items taken out that may be handed back unprocessed
};

class TankMenu : public ui::Menu {
public:
    static constexpr int kInputSlot  = 11;
    static constexpr int kOutputSlot = 15;
    static constexpr int kInfoSlot   = 13;

    TankMenu(UpgradeContext ctx, std::string title) : Menu(std::move(title), Rows::Three), mCtx(std::move(ctx)) {}

    void build(Player& player) override {
        for (int i = 0; i < size(); ++i) setSlot(i, fluidui::filler());
        // Row 0: a gauge of 9 panes.
        for (int i = 0; i < 9; ++i) {
            setSlot(i, fluidui::live([this, i](Player& p) {
                auto const& t = current();
                int  filled = 0;
                if (!t.empty() && t.capacity() > 0) {
                    filled = static_cast<int>(std::ceil(9.0 * std::min(t.amount(), t.capacity()) / t.capacity()));
                }
                auto id = i < filled ? gaugePane(t.fluid()) : std::string_view("minecraft:black_stained_glass_pane");
                return ui::icon(id, describe(&p, t));
            }));
        }
        setSlot(
            kInputSlot - 1,
            std::make_shared<ui::StaticSlot>(ui::icon(
                "minecraft:hopper",
                trFor(player, "abp.tank.input", "Fill the tank ▶"),
                {trFor(player, "abp.tank.input.help1", "§7Put a filled bucket or bottle here:"),
                 trFor(player, "abp.tank.input.help2", "§7it is poured into the tank and the"),
                 trFor(player, "abp.tank.input.help3", "§7empty container stays in the slot.")}
            ))
        );
        setSlot(kInputSlot, std::make_shared<ContainerSlot>(mCtx, true));
        setSlot(kInfoSlot, fluidui::live([this](Player& p) {
            auto const& t = current();
            return fluidIcon(t.fluid(), t.empty() ? trFor(p, "abp.tank.name", "Tank Upgrade") : fluidName(&p, t.fluid()), {describe(&p, t)});
        }));
        setSlot(
            kOutputSlot - 1,
            std::make_shared<ui::StaticSlot>(ui::icon(
                "minecraft:bucket",
                trFor(player, "abp.tank.output", "Empty the tank ▶"),
                {trFor(player, "abp.tank.output.help1", "§7Put an empty bucket or bottle here:"),
                 trFor(player, "abp.tank.output.help2", "§7it is filled from the tank.")}
            ))
        );
        setSlot(kOutputSlot, std::make_shared<ContainerSlot>(mCtx, false));
        setSlot(18, fluidui::backButton(player, mCtx.backpackPtr()));
    }

    void onOpen(Player& player) override { fluidui::lockBackpackItem(*this, player, mCtx.backpack()); }

    void onTick(Player& player) override {
        if (!isTank(mCtx)) ui::MenuManager::get().close(player);
    }

    /// Everything shown depends on the backpack only (tank settings, capacity): skip the per-tick
    /// diff while it is unchanged.
    std::optional<std::uint64_t> contentVersion() const override { return mCtx.backpack().revision(); }

    int quickMoveIn(Player&, ItemStack const& stack) override {
        if (stack.isNull()) return 0;
        auto d = game::ItemConv::toData(stack);
        if (fl::contentsOf(d.id, d.aux)) return static_cast<ContainerSlot*>(slot(kInputSlot))->offer(d);
        if (fl::isEmptyContainer(d.id)) return static_cast<ContainerSlot*>(slot(kOutputSlot))->offer(d);
        return 0;
    }

private:
    /// The tank, read once per backpack revision (the gauge shows it in 10 slots).
    fl::Tank const& current() {
        auto rev = mCtx.backpack().revision();
        if (!mCached || mCachedRevision != rev) {
            mCached         = read(mCtx);
            mCachedRevision = rev;
        }
        return *mCached;
    }

    UpgradeContext          mCtx;
    std::optional<fl::Tank> mCached;
    std::uint64_t           mCachedRevision = 0;
};

// ---- the upgrade --------------------------------------------------------------------------------

class TankUpgrade : public IUpgrade {
public:
    [[nodiscard]] UpgradeInfo const& info() const override {
        static UpgradeInfo const kInfo{
            .id             = std::string(kId),
            .itemId         = std::string(kItemId),
            .nameKey        = "abp.tank.name",
            .nameFallback   = "Tank Upgrade",
            .group          = "tank",
            .maxPerBackpack = 2,
            .hasSettings    = true,
            .toggleable     = false,
        };
        return kInfo;
    }

    std::shared_ptr<ui::Menu> createSettingsMenu(Player& player, UpgradeContext const& ctx) override {
        return std::make_shared<TankMenu>(ctx, displayName(*this, &player));
    }

    [[nodiscard]] std::vector<std::string> describe(Player& player, UpgradeContext const& ctx) const override {
        return {tank::describe(&player, read(ctx))};
    }
};

// ---- selftests ----------------------------------------------------------------------------------

using selftest::Context;
using Where  = ui::MenuManager::SlotAddress::Where;
using Action = ui::MenuManager::ActionType;

core::ItemStackData itemOf(std::string_view id, int aux = 0) {
    auto d = game::ItemConv::toData(game::ItemConv::make(id, 1, aux));
    return d;
}

ItemStack cursorOf(Player& p) { return p.mPlayerUIContainer->getItem(0); }

int countInInventory(Player& p, std::string_view id) {
    int n = 0;
    for (int i = 0; i < p.getInventory().getContainerSize(); ++i) {
        auto const& it = p.getInventory().getItem(i);
        if (!it.isNull() && it.getTypeName() == id) n += it.mCount;
    }
    if (!cursorOf(p).isNull() && cursorOf(p).getTypeName() == id) n += cursorOf(p).mCount;
    return n;
}

ABP_REGISTER_SELFTEST("tank.capacity_and_limit", [](Context& ctx) {
    auto big = ctx.createBackpack(core::Tier::Diamond); // 108 slots = 12 rows
    ctx.installUpgrade(big, "tank");
    ctx.check(capacity(*big) == 48000, "diamond tank capacity = " + std::to_string(capacity(*big)));
    ctx.installUpgrade(big, "tank");
    ctx.check(tanks(big).size() == 2, "two tanks installed");
    ctx.check(!upgrade::canInstall(big, 2, itemOf(kItemId), nullptr), "a third tank is refused (max 2)");
    auto small = ctx.createBackpack(core::Tier::Leather);
    ctx.installUpgrade(small, "tank");
    ctx.check(capacity(*small) == 12000, "leather tank capacity = " + std::to_string(capacity(*small)));
    auto t = read(tanks(small)[0]);
    ctx.check(t.empty() && t.capacity() == 12000, "a new tank is empty");
});

ABP_REGISTER_SELFTEST("tank.containers", [](Context& ctx) {
    auto bp = ctx.createBackpack(core::Tier::Leather);
    UpgradeContext t(bp, ctx.installUpgrade(bp, "tank"));
    auto           in  = fillerOf(t);
    auto           out = drainerOf(t);

    auto empty = emptyContainer(itemOf("minecraft:water_bucket"), in);
    ctx.require(empty && empty->id == "minecraft:bucket", "water bucket -> empty bucket");
    ctx.check(read(t).contents() == FluidStack{"water", 1000}, "1000 mB water");
    auto bottle = emptyContainer(itemOf("minecraft:potion", 0), in);
    ctx.check(bottle && bottle->id == "minecraft:glass_bottle", "water bottle -> glass bottle");
    ctx.check(read(t).amount() == 1250, "+250 mB from a bottle");
    ctx.check(!emptyContainer(itemOf("minecraft:lava_bucket"), in), "lava refused while the tank holds water");
    ctx.check(read(t).amount() == 1250, "refusal changes nothing");

    auto filledBottle = fillContainer(itemOf("minecraft:glass_bottle"), "water", out);
    ctx.check(filledBottle && filledBottle->id == "minecraft:potion" && filledBottle->aux == 0, "glass bottle -> water bottle");
    auto filledBucket = fillContainer(itemOf("minecraft:bucket"), "water", out);
    ctx.check(filledBucket && filledBucket->id == "minecraft:water_bucket", "bucket -> water bucket");
    ctx.check(read(t).empty(), "tank empty again");
    ctx.check(!fillContainer(itemOf("minecraft:bucket"), "water", out), "nothing to fill from an empty tank");

    // Potions keep their type.
    auto potion = itemOf("minecraft:potion", 7);
    ctx.require(potion.aux == 7, "potion aux survives conversion (aux=" + std::to_string(potion.aux) + ")");
    ctx.check(emptyContainer(potion, in).has_value(), "potion poured in");
    ctx.check(read(t).fluid() == "potion:7", "fluid is potion:7 (got " + read(t).fluid() + ")");
    auto back = fillContainer(itemOf("minecraft:glass_bottle"), read(t).fluid(), out);
    ctx.check(back && back->id == "minecraft:potion" && back->aux == 7, "the same potion comes back");
    // Honey bottles and milk buckets.
    ctx.check(emptyContainer(itemOf("minecraft:honey_bottle"), in).has_value(), "honey bottle poured in");
    ctx.check(read(t).contents() == FluidStack{"honey", 250}, "250 mB honey");
    ctx.check(!fillContainer(itemOf("minecraft:bucket"), "honey", out), "honey can't go into a bucket");
    (void)out("honey", 250, false);
    ctx.check(emptyContainer(itemOf("minecraft:milk_bucket"), in).has_value(), "milk poured in");
    ctx.check(fillContainer(itemOf("minecraft:bucket"), "milk", out)->id == "minecraft:milk_bucket", "milk bucket back");
});

ABP_REGISTER_SELFTEST("tank.backpack_api", [](Context& ctx) {
    auto bp = ctx.createBackpack(core::Tier::Iron); // 54 slots: 24000 mB per tank
    ctx.installUpgrade(bp, "tank");
    ctx.installUpgrade(bp, "tank");
    auto all = tanks(bp);
    ctx.require(all.size() == 2, "two tanks");
    ctx.check(fill(bp, {"water", 30000}) == 30000, "30000 mB fit into two tanks");
    ctx.check(read(all[0]).amount() == 24000 && read(all[1]).amount() == 6000, "first tank filled first");
    ctx.check(fill(bp, {"lava", 1000}) == 0, "no room for a second fluid");
    ctx.check(fill(bp, {"water", 20000}, false, true) == 0, "exact fill refuses a partial fill");
    ctx.check(amountOf(bp, "water") == 30000, "exact refusal changed nothing");
    auto got = drain(bp, "", 25000);
    ctx.check(got == FluidStack{"water", 25000}, "drain the first fluid found");
    ctx.check(amountOf(bp, "water") == 5000, "5000 mB left");
    ctx.check(drain(bp, "water", 6000, false, true).empty(), "exact drain refuses");
    ctx.check(fill(bp, {"lava", 1000}) == 1000, "lava goes into the now-empty tank");
    ctx.check(fluids(bp).size() == 2, "two fluids listed");
});

ABP_REGISTER_SELFTEST("tank.travels_with_item", [](Context& ctx) {
    auto a = ctx.createBackpack(core::Tier::Leather);
    UpgradeContext t(a, ctx.installUpgrade(a, "tank"));
    write(t, fl::Tank({"lava", 3000}, capacity(*a)));
    auto item = upgrade::remove(a, t.slot(), nullptr);
    ctx.require(item.has_value(), "tank removed");
    auto b = ctx.createBackpack(core::Tier::Copper);
    std::string reason;
    ctx.require(upgrade::install(b, 0, *item, nullptr, &reason), "reinstall: " + reason);
    ctx.check(read(UpgradeContext(b, 0)).contents() == FluidStack{"lava", 3000}, "contents kept in the upgrade item");
});

ABP_REGISTER_SELFTEST("tank.slots_are_not_storage", [](Context& ctx) {
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(core::Tier::Leather);
    UpgradeContext t(bp, ctx.installUpgrade(bp, "tank"));
    ContainerSlot  in(t, true);
    ctx.check(in.offer(itemOf("minecraft:water_bucket")) == 1, "water bucket processed");
    ctx.check(in.content().id == "minecraft:bucket", "empty bucket left in the slot");
    ctx.check(in.offer(itemOf("minecraft:bucket")) == 0, "unrelated empty buckets are not stored");
    ctx.check(in.offer(itemOf("minecraft:stone")) == 0, "other items are refused");
    auto taken = in.take(player, 1);
    ctx.check(game::ItemConv::idOf(taken) == "minecraft:bucket" && in.content().empty(), "bucket taken out");
    ctx.check(in.offer(itemOf("minecraft:stone")) == 0, "still no stone");
    ctx.check(in.offer(itemOf("minecraft:bucket")) == 1, "what was taken can be handed back (framework rollback)");
    ctx.check(in.offer(itemOf("minecraft:bucket")) == 0, "but not more than that");
    ContainerSlot out(t, false);
    ctx.check(out.offer(itemOf("minecraft:bucket")) == 1 && out.content().id == "minecraft:water_bucket", "output fills a bucket");
    ctx.check(out.offer(itemOf("minecraft:water_bucket")) == 0, "filled buckets are not stored in the output");
});

ABP_REGISTER_SELFTEST("tank.menu",[](Context& ctx) -> ll::coro::CoroTask<> {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(core::Tier::Leather);
    int   slot   = ctx.installUpgrade(bp, "tank");
    ctx.require(ctx.give(player, ctx.backpackItem(*bp), 0) == 0, "give backpack");
    ctx.require(ctx.give(player, game::ItemConv::make("minecraft:water_bucket"), 1) == 1, "give water bucket");
    ctx.require(ctx.give(player, game::ItemConv::make("minecraft:glass_bottle", 3), 2) == 2, "give bottles");
    auto* type = upgrade::byId(kId);
    ctx.require(type != nullptr, "tank upgrade registered");
    type->openSettings(player, UpgradeContext(bp, slot, &player));
    ctx.require(mm.current(player) != nullptr, "tank menu open");
    mm.forceShown(player);
    UpgradeContext t(bp, slot);

    // Pour the water bucket in through the input slot.
    ctx.check(mm.simulate(player, {Action::Take, {Where::Inventory, 1}, {Where::Cursor, 0}, 1}), "bucket to cursor");
    ctx.check(mm.simulate(player, {Action::Place, {Where::Cursor, 0}, {Where::Menu, TankMenu::kInputSlot}, 1}), "place into input");
    ctx.check(read(t).contents() == FluidStack{"water", 1000}, "tank holds 1000 mB water");
    co_await ctx.ticks(2);
    ctx.check(game::ItemConv::idOf(mm.shownItem(player, TankMenu::kInputSlot)) == "minecraft:bucket", "empty bucket shown in the input slot");
    ctx.check(cursorOf(player).isNull(), "cursor empty");
    // Fill 3 bottles in the output slot (one at a time: potions don't stack).
    ctx.check(mm.simulate(player, {Action::Take, {Where::Inventory, 2}, {Where::Cursor, 0}, 3}), "bottles to cursor");
    ctx.check(mm.simulate(player, {Action::Place, {Where::Cursor, 0}, {Where::Menu, TankMenu::kOutputSlot}, 3}), "place bottles");
    ctx.check(read(t).amount() == 750, "one bottle filled (750 mB left), got " + std::to_string(read(t).amount()));
    ctx.check(cursorOf(player).mCount == 2, "2 bottles stay on the cursor");
    // Take the water bottle out (shift-click) and fill the rest.
    ctx.check(mm.simulate(player, {Action::Take, {Where::Menu, TankMenu::kOutputSlot}, {Where::Inventory, 3}, 1}), "water bottle out");
    ctx.check(game::ItemConv::idOf(player.getInventory().getItem(3)) == "minecraft:potion", "water bottle in the inventory");
    ctx.check(mm.simulate(player, {Action::Place, {Where::Cursor, 0}, {Where::Menu, TankMenu::kOutputSlot}, 2}), "place again");
    ctx.check(read(t).amount() == 500, "500 mB left");
    // Shift-click the empty bucket from the input slot to the output: filled with 500 mB? no (needs 1000).
    ctx.check(mm.simulate(player, {Action::Take, {Where::Menu, TankMenu::kInputSlot}, {Where::Inventory, 4}, 1}), "bucket out");
    ctx.check(!mm.simulate(player, {Action::Place, {Where::Inventory, 4}, {Where::Menu, TankMenu::kOutputSlot}, 1}), "a bucket needs 1000 mB");
    // Shift-click the water bottle back in: it goes to the input slot.
    ctx.check(mm.simulate(player, {Action::Place, {Where::Inventory, 3}, {Where::Menu, 0}, 1}), "quick move bottle in");
    ctx.check(read(t).amount() == 750, "bottle poured back (750 mB)");
    mm.close(player);
    ctx.check(cursorOf(player).isNull(), "cursor returned on close");
    auto const& s = t.settings();
    int inSlot  = s.contains("in") ? core::ItemStackData::fromJson(s["in"]).count : 0;
    int outSlot = s.contains("out") ? core::ItemStackData::fromJson(s["out"]).count : 0;
    ctx.check(inSlot == 1 && outSlot == 1, "1 glass bottle in the input slot, 1 water bottle in the output slot");
    ctx.check(countInInventory(player, "minecraft:glass_bottle") == 1, "1 glass bottle left in the inventory");
    ctx.check(countInInventory(player, "minecraft:bucket") == 1, "the empty bucket is in the inventory");
});

} // namespace
} // namespace abp::upgrade::tank

ABP_REGISTER_CONFIG("tank", abp::upgrade::tank::TankConfig);
ABP_REGISTER_UPGRADE(abp::upgrade::tank::TankUpgrade);
