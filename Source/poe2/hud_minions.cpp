#include "poe2/hud_minions.hpp"

#include <algorithm>
#include <cstdint>
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
#include "poe2/skill_gems.hpp"
#include "poe2/shrines.hpp"
#include "poe2/spirit.hpp"
#include "poe2/spirit_buffs.hpp"
#include "poe2/spirit_skills.hpp"
#include "poe2/support_table.hpp"
#include "poe2/table_skills.hpp"
#include "poe2/weapon_skills.hpp"
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
constexpr uint8_t RingTrackColor = PAL16_GRAY + 13;
constexpr uint8_t RingColor = PAL16_YELLOW + 2;
constexpr uint8_t HarmfulRingColor = PAL16_RED + 4;
constexpr int GemBuffBlinkTicks = 2 * 20;

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
	PersistentSkill::Impurity,
};

void DrawBuffTooltip(const Surface &out, PersistentSkill skill)
{
	const std::vector<std::pair<std::string, UiFlags>> lines { { std::string(PersistentSkillDescription(skill)), UiFlags::ColorWhite } };
	poe2ui::DrawTooltip(out, { { 0, 0 }, { out.w(), out.h() } }, MousePosition, std::string(PersistentSkillName(skill)), UiFlags::ColorWhitegold, lines);
}

/** @brief The point @p step pixels clockwise round the rectangle's edge, from the middle of its top. */
Point RingPoint(const Rectangle &rect, int step)
{
	const int w = rect.size.width - 1;
	const int h = rect.size.height - 1;
	const int perimeter = 2 * (w + h);
	int s = ((step % perimeter) + perimeter + (w / 2)) % perimeter;
	const Point origin = rect.position;
	if (s < w)
		return origin + Displacement { s, 0 };
	s -= w;
	if (s < h)
		return origin + Displacement { w, s };
	s -= h;
	if (s < w)
		return origin + Displacement { w - s, h };
	s -= w;
	return origin + Displacement { 0, h - s };
}

/** @brief A two-pixel ring round the icon: the time left, draining clockwise from the top. */
void DrawDrainingRing(const Surface &out, const Rectangle &icon, int ticksLeft, int ticksTotal, uint8_t color)
{
	const Rectangle outer { icon.position - Displacement { 2, 2 }, { icon.size.width + 4, icon.size.height + 4 } };
	const Rectangle inner { icon.position - Displacement { 1, 1 }, { icon.size.width + 2, icon.size.height + 2 } };
	for (const Rectangle &ring : { outer, inner }) {
		const int perimeter = 2 * ((ring.size.width - 1) + (ring.size.height - 1));
		const int lit = ticksTotal > 0 ? static_cast<int>(static_cast<int64_t>(perimeter) * std::clamp(ticksLeft, 0, ticksTotal) / ticksTotal) : 0;
		for (int step = 0; step < perimeter; step++) {
			const Point point = RingPoint(ring, step);
			if (out.InBounds(point))
				out.SetPixel(point, step < lit ? color : RingTrackColor);
		}
	}
}

void DrawOneLineTooltip(const Surface &out, const std::string &text)
{
	const int width = GetLineWidth(text, GameFont12) + 16;
	constexpr int Height = 24;
	Point position = MousePosition + Displacement { 16, 8 };
	position.x = std::clamp(position.x, 2, std::max(out.w() - width - 2, 2));
	position.y = std::clamp(position.y, 2, std::max(out.h() - Height - 2, 2));
	const Rectangle box { position, { width, Height } };
	poe2ui::FillRectangle(out, box, poe2ui::TooltipColor);
	poe2ui::DrawFrame(out, box, poe2ui::BorderColor);
	DrawString(out, text, { position + Displacement { 8, 2 }, { width - 16, Height - 4 } }, { .flags = UiFlags::ColorWhite | UiFlags::VerticalCenter });
}

/** @brief A skill gem's buff on the hero, with the time it lasts once cast. */
void AddGemBuffIcon(WeaponSkill skill, int ticksLeft, int ticksTotal, int count, std::vector<TimedHudIcon> &icons)
{
	if (ticksLeft <= 0)
		return;
	const SkillSlot slot = SkillSlot::ForAttack(skill);
	const int seconds = (ticksLeft + 19) / 20;
	icons.push_back({ SpellIcon {}, StrCat(SkillName(slot), " (", seconds, "s)"), ticksLeft, std::max(ticksTotal, ticksLeft), false, slot, count });
}

/** @brief The time a table skill's buff lasts once cast, with its supports, as ExecuteTableSkill gives it; at most @p cap. */
int TableBuffTicks(const Player &player, WeaponSkill skill, int cap)
{
	const TableSkillData *data = FindTableSkill(skill);
	if (data == nullptr || data->duration <= 0)
		return 0;
	return std::clamp(SupportDurationTicks(player, SkillSlot::ForAttack(skill), data->duration), 1, cap);
}

/** @brief The war cries that empower the next hits, each with the hits it waits on (Poe2PlayerData::cryHits). */
constexpr std::pair<WeaponSkill, EmpoweringCry> EmpoweringCries[] = {
	{ WeaponSkill::HellfireRoar, EmpoweringCry::Hellfire },
	{ WeaponSkill::QuakingRoar, EmpoweringCry::Quaking },
	{ WeaponSkill::CryOfBulKathos, EmpoweringCry::BulKathos },
	{ WeaponSkill::BulwarkRoar, EmpoweringCry::Bulwark },
};

/**
 * @brief The skill gems' timed buffs on the hero. None of them has a cooldown, and the Spirit gems (auras, heralds,
 * Convalescence and the banners) have their portraits already.
 */
void AddGemBuffIcons(const Player &player, std::vector<TimedHudIcon> &icons)
{
	const Poe2PlayerData &data = player.poe2;
	AddGemBuffIcon(WeaponSkill::BattleCry, data.warcryTicks, BattleCryTicks, 0, icons);
	for (const auto &[skill, cry] : EmpoweringCries) {
		const auto index = static_cast<size_t>(cry);
		// A damage cry is spent with its last hit; Bulwark Roar's block and Guard last its whole time, waves or none.
		if (data.cryHits[index] == 0 && cry != EmpoweringCry::Bulwark)
			continue;
		AddGemBuffIcon(skill, data.cryTicks[index], TableBuffTicks(player, skill, UINT8_MAX), data.cryHits[index], icons);
	}
	AddGemBuffIcon(WeaponSkill::FrozenArmor, data.frozenArmorTicks, TableBuffTicks(player, WeaponSkill::FrozenArmor, UINT16_MAX), 0, icons);
	AddGemBuffIcon(WeaponSkill::HolyShield, data.holyShieldTicks, TableBuffTicks(player, WeaponSkill::HolyShield, UINT16_MAX), 0, icons);
	// Soul Harvest with no souls taken does nothing.
	if (data.soulStacks > 0)
		AddGemBuffIcon(WeaponSkill::SoulHarvest, data.soulTicks, TableBuffTicks(player, WeaponSkill::SoulHarvest, UINT16_MAX), data.soulStacks, icons);
}

} // namespace

std::vector<TimedHudIcon> TimedHudIcons(const Player &player)
{
	std::vector<TimedHudIcon> icons;
	if (!gbIsPoe2)
		return icons;
	const ShrineBuffState &state = player.poe2.shrines;
	for (const ShrineBuff &buff : state.buffs) {
		if (!buff.active())
			continue;
		const ShrineData &shrine = GetShrineData(buff.kind);
		icons.push_back({ static_cast<SpellIcon>(shrine.icon), StrCat(shrine.power, ": ", ShrineBuffTooltip(buff)), buff.ticksLeft, buff.ticksTotal, false });
	}
	if (state.curse != ShrineCurse::None && state.curseTicks > 0)
		icons.push_back({ SpellIcon::RedSkull, StrCat(ShrineCurseName(state.curse), ": ", ShrineCurseTooltip(state.curse, state.curseTicks)), state.curseTicks, state.curseTotal, true });
	AddGemBuffIcons(player, icons);
	return icons;
}

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
	// Shrine buffs and curses, and the skill gems' buffs: a draining ring, and a blink for the last seconds.
	std::optional<std::string> timedHover;
	const bool blinkOff = (SDL_GetTicks() / 250) % 2 == 1;
	for (const TimedHudIcon &timed : TimedHudIcons(player)) {
		const Rectangle portrait { position + Displacement { 2, 2 }, PortraitSize };
		DrawHalfTransparentRectTo(out, portrait.position.x, portrait.position.y, portrait.size.width, portrait.size.height);
		const Point bottomLeft { portrait.position.x, portrait.position.y + portrait.size.height - 1 };
		// A gem's buff lasts seconds, not minutes: it blinks for its last two.
		const int blinkTicks = timed.skill.isEmpty() ? ShrineBlinkTicks : GemBuffBlinkTicks;
		if (timed.ticksLeft > blinkTicks || !blinkOff) {
			if (timed.skill.isEmpty())
				DrawSmallSpellIcon(out, bottomLeft, timed.icon);
			else
				DrawSkillIcon(out, bottomLeft, timed.skill, true);
		}
		DrawDrainingRing(out, portrait, timed.ticksLeft, timed.ticksTotal, timed.harmful ? HarmfulRingColor : RingColor);
		const int seconds = (timed.ticksLeft + 19) / 20;
		DrawString(out, StrCat(seconds), { portrait.position + Displacement { 2, portrait.size.height - 14 }, { portrait.size.width - 4, 12 } },
		    { .flags = (timed.harmful ? UiFlags::ColorRed : UiFlags::ColorWhite) | UiFlags::AlignRight | UiFlags::Outlined | UiFlags::FontSize12, .spacing = 0 });
		// The hits or souls left, in the top corner.
		if (timed.count > 0) {
			DrawString(out, StrCat(timed.count), { portrait.position + Displacement { 2, 1 }, { portrait.size.width - 4, 12 } },
			    { .flags = UiFlags::ColorWhitegold | UiFlags::Outlined | UiFlags::FontSize12, .spacing = 0 });
		}
		if (portrait.contains(MousePosition))
			timedHover = timed.tooltip;
		position.x += portrait.size.width + PortraitGap + 4;
	}
	if (hover != nullptr)
		DrawMinionTooltip(out, *hover);
	else if (buffHover)
		DrawBuffTooltip(out, *buffHover);
	else if (timedHover)
		DrawOneLineTooltip(out, *timedHover);
}

} // namespace devilution
