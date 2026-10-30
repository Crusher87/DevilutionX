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
#include "levels/gendung.h"
#include "monster.h"
#include "player.h"
#include "poe2/profane_roads.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/spirit.hpp"
#include "poe2/spirit_buffs.hpp"
#include "poe2/spirit_skills.hpp"
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

void DrawMinionTooltip(const Surface &out, const MinionPortrait &portrait)
{
	std::vector<std::pair<std::string, UiFlags>> lines;
	lines.emplace_back(FormatRuntime(_("{:d} of {:d} alive"), portrait.alive, portrait.target), UiFlags::ColorWhite);
	if (portrait.pending > 0) {
		const int seconds = (portrait.ticksLeft + 19) / 20;
		lines.emplace_back(FormatRuntime(ngettext("Next one rises in {:d} second", "Next one rises in {:d} seconds", seconds), seconds), UiFlags::ColorWhite);
		if (portrait.pending > 1)
			lines.emplace_back(FormatRuntime(_("{:d} waiting to rise"), portrait.pending), UiFlags::ColorWhite);
	}
	poe2ui::DrawTooltip(out, { { 0, 0 }, { out.w(), out.h() } }, MousePosition, portrait.name, UiFlags::ColorWhitegold, lines);
}

/** @brief The golem: its skill's, or the Golem spell's while one it cast is up. */
void AddGolemPortrait(const Player &player, std::vector<MinionPortrait> &portraits)
{
	const bool up = FindGolemForPlayer(player) != nullptr;
	const PersistentSkill skill = ActiveGolemSkill(player);
	if (skill == PersistentSkill::None) {
		if (up)
			portraits.push_back({ SkillSlot::ForSpell(SpellID::Golem), std::string(SkillName(SkillSlot::ForSpell(SpellID::Golem))), 1, 1, 0, 0, 1 });
		return;
	}
	if (leveltype == DTYPE_TOWN)
		return;
	// A fallen golem rises once golemCooldown runs out, which KeepGolemAlive holds at a quarter of GolemReviveTicks while it stands.
	const int reviveTicks = GolemReviveTicks / 4;
	const int ticksLeft = up ? 0 : std::min<int>(player.poe2.golemCooldown, reviveTicks);
	portraits.push_back({ SkillSlot::ForPersistent(skill), std::string(PersistentSkillName(skill)), up ? 1 : 0, 1, up ? 0 : 1, ticksLeft, reviveTicks });
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
	PersistentSkill::RagingSpirits,
	PersistentSkill::RavenousSwarm,
	PersistentSkill::Sacrifice,
	PersistentSkill::LingeringIllusion,
	PersistentSkill::Guardian,
};

void DrawBuffTooltip(const Surface &out, PersistentSkill skill)
{
	const std::vector<std::pair<std::string, UiFlags>> lines { { std::string(PersistentSkillDescription(skill)), UiFlags::ColorWhite } };
	poe2ui::DrawTooltip(out, { { 0, 0 }, { out.w(), out.h() } }, MousePosition, std::string(PersistentSkillName(skill)), UiFlags::ColorWhitegold, lines);
}

} // namespace

std::vector<MinionPortrait> MinionHudPortraits(const Player &player)
{
	std::vector<MinionPortrait> portraits;
	if (!gbIsPoe2)
		return portraits;
	// Every kind of minion has one: the golem was missing from the row (Curtis 2026-10-30).
	AddGolemPortrait(player, portraits);
	// The minion gems' and the sceptre's skeletons, and Wolf Pack.
	const int reviveTicks = MinionReviveTicks(player);
	for (const PersistentSkill skill : RevivingMinionSkills) {
		const int target = MinionTarget(player, skill);
		if (target <= 0)
			continue;
		int ticksLeft = 0;
		const int pending = PendingMinionRevives(player, skill, &ticksLeft);
		portraits.push_back({ SkillSlot::ForPersistent(skill), std::string(PersistentSkillName(skill)), std::min(AliveMinionCount(player, skill), target), target, pending, ticksLeft, reviveTicks });
	}
	// Revive's minions come from kills; they don't rise again.
	if (const int limit = ReviveMinionLimit(player); limit > 0 && leveltype != DTYPE_TOWN) {
		const int alive = static_cast<int>(CountRevivedMinions(player));
		portraits.push_back({ SkillSlot::ForPersistent(PersistentSkill::Revive), std::string(PersistentSkillName(PersistentSkill::Revive)), std::min(alive, limit), limit, 0, 0, 1 });
	}
	// Leoric's Road's King's dead, while they last.
	if (const int kingsDead = static_cast<int>(CountKingsDead(player)); kingsDead > 0)
		portraits.push_back({ SkillSlot::ForSpell(SpellID::Resurrect), std::string(_("The King's Dead")), kingsDead, kingsDead, 0, 0, 1 });
	return portraits;
}

void DrawMinionHud(const Surface &out)
{
	if (!gbIsPoe2 || MyPlayer == nullptr || IsLeftPanelOpen())
		return;
	const Player &player = *MyPlayer;
	Point position = FirstPortraitPosition();
	const std::vector<MinionPortrait> portraits = MinionHudPortraits(player);
	const MinionPortrait *hover = nullptr;
	for (const MinionPortrait &minions : portraits) {
		const Rectangle portrait { position, PortraitSize };

		DrawHalfTransparentRectTo(out, portrait.position.x, portrait.position.y, portrait.size.width, portrait.size.height);
		const Point bottomLeft { portrait.position.x, portrait.position.y + portrait.size.height - 1 };
		DrawSkillIcon(out, bottomLeft, minions.icon, minions.alive > 0);
		const bool hovered = portrait.contains(MousePosition);
		if (hovered)
			DrawSmallSpellIconBorder(out, bottomLeft);
		const UiFlags countColor = minions.alive < minions.target ? UiFlags::ColorRed : UiFlags::ColorWhite;
		DrawString(out, StrCat(minions.alive, "/", minions.target), { portrait.position + Displacement { 2, portrait.size.height - 14 }, { portrait.size.width - 4, 12 } },
		    { .flags = countColor | UiFlags::AlignRight | UiFlags::Outlined | UiFlags::FontSize12, .spacing = 0 });

		// The soonest revive fills the bar under the portrait.
		if (minions.pending > 0 && minions.reviveTicks > 0) {
			const Point bar { portrait.position.x, portrait.position.y + portrait.size.height + 2 };
			FillRect(out, bar.x, bar.y, portrait.size.width, BarHeight, BarBackColor);
			const int filled = portrait.size.width * (minions.reviveTicks - std::min(minions.ticksLeft, minions.reviveTicks)) / minions.reviveTicks;
			if (filled > 0)
				FillRect(out, bar.x, bar.y, filled, BarHeight, BarColor);
		}
		if (hovered)
			hover = &minions;
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
	if (hover != nullptr)
		DrawMinionTooltip(out, *hover);
	else if (buffHover)
		DrawBuffTooltip(out, *buffHover);
}

} // namespace devilution
