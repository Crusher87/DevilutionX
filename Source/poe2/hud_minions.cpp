#include "poe2/hud_minions.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "automap.h"
#include "control/control.hpp"
#include "diablo.h"
#include "engine/render/primitive_render.hpp"
#include "engine/render/text_render.hpp"
#include "game_mode.hpp"
#include "options.h"
#include "panels/poe2_widgets.hpp"
#include "panels/skill_bar.hpp"
#include "panels/spell_icons.hpp"
#include "player.h"
#include "poe2/spirit.hpp"
#include "poe2/spirit_buffs.hpp"
#include "utils/format.hpp"
#include "utils/language.h"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

constexpr Size PortraitSize { 37, 38 };
constexpr int PortraitGap = 6;
constexpr int BarHeight = 3;
constexpr uint8_t BarBackColor = PAL16_GRAY + 14;
constexpr uint8_t BarColor = PAL16_YELLOW + 4;

Point FirstPortraitPosition()
{
	Point position { 8, 8 };
	// The FPS counter and the map's level line sit in the same corner.
	if (*GetOptions().Graphics.showFPS)
		position.y += 15;
	if (AutomapActive)
		position.y += 15;
	return position;
}

void DrawMinionTooltip(const Surface &out, PersistentSkill skill, int alive, int target, int pending, int ticksLeft)
{
	std::vector<std::pair<std::string, UiFlags>> lines;
	lines.emplace_back(FormatRuntime(_("{:d} of {:d} alive"), alive, target), UiFlags::ColorWhite);
	if (pending > 0) {
		const int seconds = (ticksLeft + 19) / 20;
		lines.emplace_back(FormatRuntime(ngettext("Next one rises in {:d} second", "Next one rises in {:d} seconds", seconds), seconds), UiFlags::ColorWhite);
		if (pending > 1)
			lines.emplace_back(FormatRuntime(_("{:d} waiting to rise"), pending), UiFlags::ColorWhite);
	}
	poe2ui::DrawTooltip(out, { { 0, 0 }, { out.w(), out.h() } }, MousePosition, std::string(PersistentSkillName(skill)), UiFlags::ColorWhitegold, lines);
}

/** Spirit gems with nothing on screen to show they're working get a portrait beside the minions (Curtis 2026-10-19). */
constexpr PersistentSkill BuffSkills[] = {
	PersistentSkill::HeraldOfAsh,
	PersistentSkill::HeraldOfThunder,
	PersistentSkill::HeraldOfFrost,
	PersistentSkill::HeraldOfPlague,
	PersistentSkill::PurityOfElements,
	PersistentSkill::WindDancer,
	PersistentSkill::GhostDance,
	PersistentSkill::Barkskin,
	PersistentSkill::ArcticArmour,
	PersistentSkill::CombatFrenzy,
	PersistentSkill::EternalRage,
	PersistentSkill::ElementalConflux,
	PersistentSkill::AlchemistsBoon,
	PersistentSkill::OverwhelmingPresence,
	PersistentSkill::TrailOfCaltrops,
	PersistentSkill::ManaRemnants,
	PersistentSkill::Attrition,
	PersistentSkill::Blink,
	PersistentSkill::IronWard,
	PersistentSkill::PlagueBearer,
	PersistentSkill::Convalescence,
	PersistentSkill::GrimFeast,
	PersistentSkill::WarBanner,
	PersistentSkill::DefianceBanner,
	PersistentSkill::DreadBanner,
	PersistentSkill::Briarpatch,
	PersistentSkill::MagmaBarrier,
	PersistentSkill::ScavengedPlating,
	PersistentSkill::TimeOfNeed,
	PersistentSkill::ChargeRegulation,
	PersistentSkill::WitheringPresence,
	PersistentSkill::Trinity,
	PersistentSkill::SiphonElements,
	PersistentSkill::HeraldOfBlood,
};

void DrawBuffTooltip(const Surface &out, PersistentSkill skill)
{
	const std::vector<std::pair<std::string, UiFlags>> lines { { std::string(PersistentSkillDescription(skill)), UiFlags::ColorWhite } };
	poe2ui::DrawTooltip(out, { { 0, 0 }, { out.w(), out.h() } }, MousePosition, std::string(PersistentSkillName(skill)), UiFlags::ColorWhitegold, lines);
}

} // namespace

void DrawMinionHud(const Surface &out)
{
	if (!gbIsPoe2 || MyPlayer == nullptr || IsLeftPanelOpen())
		return;
	const Player &player = *MyPlayer;
	Point position = FirstPortraitPosition();
	struct Hover {
		PersistentSkill skill;
		int alive;
		int target;
		int pending;
		int ticksLeft;
	};
	std::optional<Hover> hover;
	for (const PersistentSkill skill : MinionCountSkills) {
		const int target = MinionTarget(player, skill);
		if (target <= 0)
			continue;
		const int alive = std::min(AliveMinionCount(player, skill), target);
		int ticksLeft = 0;
		const int pending = PendingMinionRevives(player, skill, &ticksLeft);
		const Rectangle portrait { position, PortraitSize };

		DrawHalfTransparentRectTo(out, portrait.position.x, portrait.position.y, portrait.size.width, portrait.size.height);
		const Point bottomLeft { portrait.position.x, portrait.position.y + portrait.size.height - 1 };
		DrawSkillIcon(out, bottomLeft, SkillSlot::ForPersistent(skill), alive > 0);
		const bool hovered = portrait.contains(MousePosition);
		if (hovered)
			DrawSmallSpellIconBorder(out, bottomLeft);
		const UiFlags countColor = alive < target ? UiFlags::ColorRed : UiFlags::ColorWhite;
		DrawString(out, StrCat(alive, "/", target), { portrait.position + Displacement { 2, portrait.size.height - 14 }, { portrait.size.width - 4, 12 } },
		    { .flags = countColor | UiFlags::AlignRight | UiFlags::Outlined | UiFlags::FontSize12, .spacing = 0 });

		// The soonest revive fills the bar under the portrait.
		if (pending > 0) {
			const Point bar { portrait.position.x, portrait.position.y + portrait.size.height + 2 };
			FillRect(out, bar.x, bar.y, portrait.size.width, BarHeight, BarBackColor);
			const int filled = portrait.size.width * (SkeletonReviveTicks - std::min(ticksLeft, SkeletonReviveTicks)) / SkeletonReviveTicks;
			if (filled > 0)
				FillRect(out, bar.x, bar.y, filled, BarHeight, BarColor);
		}
		if (hovered)
			hover = Hover { skill, alive, target, pending, ticksLeft };
		position.x += portrait.size.width + PortraitGap;
	}
	std::optional<PersistentSkill> buffHover;
	for (const PersistentSkill skill : BuffSkills) {
		if (!IsPersistentSkillActive(player, skill))
			continue;
		const Rectangle portrait { position, PortraitSize };
		DrawHalfTransparentRectTo(out, portrait.position.x, portrait.position.y, portrait.size.width, portrait.size.height);
		const Point bottomLeft { portrait.position.x, portrait.position.y + portrait.size.height - 1 };
		DrawSkillIcon(out, bottomLeft, SkillSlot::ForPersistent(skill), true);
		// Stages, armour or the element, like the minions' count.
		if (const std::string text = SpiritBuffPortraitText(player, skill); !text.empty()) {
			DrawString(out, text, { portrait.position + Displacement { 2, portrait.size.height - 14 }, { portrait.size.width - 4, 12 } },
			    { .flags = UiFlags::ColorWhite | UiFlags::AlignRight | UiFlags::Outlined | UiFlags::FontSize12, .spacing = 0 });
		}
		if (portrait.contains(MousePosition)) {
			DrawSmallSpellIconBorder(out, bottomLeft);
			buffHover = skill;
		}
		position.x += portrait.size.width + PortraitGap;
	}
	if (hover)
		DrawMinionTooltip(out, hover->skill, hover->alive, hover->target, hover->pending, hover->ticksLeft);
	else if (buffHover)
		DrawBuffTooltip(out, *buffHover);
}

} // namespace devilution
