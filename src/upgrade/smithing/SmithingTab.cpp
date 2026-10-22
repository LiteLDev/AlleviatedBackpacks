// Client: the smithing upgrade's native tab (SB SmithingUpgradeTab), 103x100.
//
//   slots at y + 25: template (+4), base (+22), addition (+40), result (+width - 23)
//   an arrow between the addition and the result, a red cross over it when the three inputs make
//   nothing ("Item can't be upgraded this way" on hover)
//   empty input slots cycle through what fits there (CyclingSlotBackground) and explain it on
//   hover (SmithingScreen onboarding tooltips)
//   the armor stand preview wearing / holding the result, feet at (+width/2, +height-10), scale 25

#ifdef ABP_CLIENT

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "client/Client.h"
#include "client/ui/screen/Tabs.h"
#include "client/ui/tabs/crafting/StationWidgets.h"
#include "client/ui/widgets/Widgets.h"
#include "upgrade/anvil/StationTab.h"
#include "upgrade/smithing/ArmorStandPreview.h"
#include "upgrade/smithing/SlotSilhouettes.h"

namespace abp::upgrade::smithing::client {
namespace {

using ::abp::client::ctr;
using ::abp::upgrade::anvil::client::StationSlot;
using ::abp::upgrade::anvil::client::stackFrom;
namespace ui = ::abp::client::ui;

constexpr float kWidth  = 103;
constexpr float kHeight = 100;

enum class TemplateKind { None, Netherite, Trim, Other };

TemplateKind kindOf(ItemStack const& s) {
    if (s.isNull()) return TemplateKind::None;
    auto id = s.getTypeName();
    if (id == "minecraft:netherite_upgrade_smithing_template") return TemplateKind::Netherite;
    if (id.ends_with("_armor_trim_smithing_template")) return TemplateKind::Trim;
    return TemplateKind::Other;
}

class SmithingTab final : public ui::NativeTab {
public:
    Size build(ui::TabContext const& ctx, ui::Rect area, ui::WidgetGroup& widgets) override {
        mSend = ctx.sendTabAction;
        read(ctx);
        float y     = area.y + 24;
        auto addSlot = [&](float itemX, ItemStack const* stack, char const* name) -> StationSlot& {
            auto& s   = widgets.add(std::make_unique<StationSlot>());
            s.rect    = {itemX - 1, y, 18, 18};
            s.stack   = [stack] { return stack; };
            s.onClick = [this, name](int button, bool shift) {
                if (mSend) mSend(nlohmann::json{{"click", name}, {"button", button}, {"shift", shift}});
            };
            return s;
        };
        auto& templ        = addSlot(area.x + 4, &mTemplate, "template");
        templ.emptyIcon    = [](ui::Painter& p, float x, float yy, double t) { silhouette::drawCycle(p, silhouette::kTemplateCycle, x, yy, t); };
        templ.emptyTooltip = [] {
            return std::vector<ui::TooltipLine>{{ctr("abp.smithing.missing_template", "Add Smithing Template"), 0xFFFFFF}};
        };
        auto& base        = addSlot(area.x + 22, &mBase, "base");
        base.emptyIcon    = [this](ui::Painter& p, float x, float yy, double t) { silhouette::drawCycle(p, baseCycle(), x, yy, t); };
        base.emptyTooltip = [this] {
            switch (kindOf(mTemplate)) {
            case TemplateKind::Netherite:
                return std::vector<ui::TooltipLine>{{ctr("abp.smithing.netherite.base", "Add diamond armor, weapon, or tool"), 0xFFFFFF}};
            case TemplateKind::Trim:
                return std::vector<ui::TooltipLine>{{ctr("abp.smithing.trim.base", "Add a piece of armor"), 0xFFFFFF}};
            default:
                return std::vector<ui::TooltipLine>{};
            }
        };
        auto& addition        = addSlot(area.x + 40, &mAddition, "addition");
        addition.emptyIcon    = [this](ui::Painter& p, float x, float yy, double t) { silhouette::drawCycle(p, additionCycle(), x, yy, t); };
        addition.emptyTooltip = [this] {
            switch (kindOf(mTemplate)) {
            case TemplateKind::Netherite:
                return std::vector<ui::TooltipLine>{{ctr("abp.smithing.netherite.addition", "Add Netherite Ingot"), 0xFFFFFF}};
            case TemplateKind::Trim:
                return std::vector<ui::TooltipLine>{{ctr("abp.smithing.trim.addition", "Add ingot or crystal"), 0xFFFFFF}};
            default:
                return std::vector<ui::TooltipLine>{};
            }
        };
        addSlot(area.x + kWidth - 2 - 3 - 18, &mResult, "result");
        mArea = area;
        // SC: shift-clicks go into the open tab (crafting/Station.h).
        ::abp::client::ui::station::reportTabOpen(ctx, widgets);
        return {kWidth, kHeight};
    }

    void update(ui::TabContext const& ctx) override {
        mSend = ctx.sendTabAction;
        read(ctx);
    }

    void render(ui::Painter& p, ui::TabContext const&, ui::Rect area, float mx, float my) override {
        mArea          = area;
        float slotY    = area.y + 25;
        float addX     = area.x + 40;
        float resultX  = area.x + kWidth - 2 - 3 - 18;
        float arrowX   = addX + 18 + std::floor((resultX - (addX + 18)) / 2) - 7 - 1;
        float arrowY   = slotY + 1;
        p.pixels(::abp::upgrade::anvil::client::arrow(), arrowX, arrowY);
        if (recipeError()) {
            p.pixels(::abp::upgrade::anvil::client::redCross(), arrowX, arrowY, 1, ui::Painter::Layer::Foreground);
        }
        drawArmorStand(p, area.x + kWidth / 2, area.y + kHeight - 10, 25, mResult.isNull() ? nullptr : &mResult);
        // SB renderOnboardingTooltips: the red cross explains itself.
        if (recipeError() && mx >= arrowX && mx < arrowX + 15 && my >= arrowY && my < arrowY + 15
            && ::abp::upgrade::anvil::client::cursorEmpty()) {
            ui::drawTooltip(p, {{ctr("abp.smithing.error", "Item can't be upgraded this way"), 0xFFFFFF}}, mx, my);
        }
    }

private:
    void read(ui::TabContext const& ctx) {
        auto it = ctx.upgrade.tab.find("smithing");
        if (it == ctx.upgrade.tab.end() || !it->is_object()) return;
        auto const& s = *it;
        mTemplate     = stackFrom(s.value("template", nlohmann::json()));
        mBase         = stackFrom(s.value("base", nlohmann::json()));
        mAddition     = stackFrom(s.value("addition", nlohmann::json()));
        mResult       = stackFrom(s.value("result", nlohmann::json()));
    }

    /// SmithingTemplateItem.getBaseSlotEmptyIcons / getAdditionalSlotEmptyIcons (none without a template).
    [[nodiscard]] std::vector<silhouette::Art const*> const& baseCycle() const {
        static std::vector<silhouette::Art const*> const none;
        switch (kindOf(mTemplate)) {
        case TemplateKind::Netherite:
            return silhouette::kNetheriteBaseCycle;
        case TemplateKind::Trim:
            return silhouette::kTrimBaseCycle;
        default:
            return none;
        }
    }
    [[nodiscard]] std::vector<silhouette::Art const*> const& additionCycle() const {
        static std::vector<silhouette::Art const*> const none;
        switch (kindOf(mTemplate)) {
        case TemplateKind::Netherite:
            return silhouette::kNetheriteAdditionCycle;
        case TemplateKind::Trim:
            return silhouette::kTrimAdditionCycle;
        default:
            return none;
        }
    }

    /// SB hasRecipeError: all three inputs, no result.
    [[nodiscard]] bool recipeError() const {
        return !mTemplate.isNull() && !mBase.isNull() && !mAddition.isNull() && mResult.isNull();
    }

    std::function<void(nlohmann::json)> mSend;
    ui::Rect                            mArea;
    ItemStack                           mTemplate, mBase, mAddition, mResult;
};

} // namespace
} // namespace abp::upgrade::smithing::client

ABP_REGISTER_NATIVE_TAB("smithing", [] { return std::make_unique<abp::upgrade::smithing::client::SmithingTab>(); });

#endif // ABP_CLIENT
