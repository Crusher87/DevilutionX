#include "panels/monster_banner.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "control/control.hpp"
#include "cursor.h"
#include "engine/palette.h"
#include "engine/render/primitive_render.hpp"
#include "engine/render/text_render.hpp"
#include "game_mode.hpp"
#include "levels/gendung.h"
#include "monster.h"
#include "panels/poe2_widgets.hpp"
#include "panels/spell_icons.hpp"
#include "poe2/endgame.hpp"
#include "poe2/map_play.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/monster_resistance.hpp"
#include "poe2/skill_resources.hpp"
#include "poe2/support_table.hpp"
#include "poe2/table_skills.hpp"
#include "poe2/unique_effects.hpp"
#include "tables/misdat.h"
#include "utils/language.h"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

using namespace poe2ui;

constexpr int MaxWidth = 420;
constexpr int Top = 8;
constexpr int NameHeight = 16;
constexpr int BarHeight = 12;
constexpr int UniqueBarHeight = 16;
constexpr int LineHeight = 13;
/** Frames the banner stays up after the cursor leaves the monster, so it doesn't flicker mid-fight. */
constexpr int LingerFrames = 45;

constexpr uint8_t FrameDark = 0;
constexpr uint8_t GoldLight = PAL16_YELLOW + 6;
constexpr uint8_t GoldDark = PAL16_YELLOW + 12;
constexpr uint8_t BarEmpty = PAL16_RED + 15;
constexpr uint8_t TrailColor = PAL16_ORANGE + 4;
constexpr uint8_t GemColor = PAL16_RED + 3;

int ShownMonster = -1;
int Linger = 0;
/** Life shown by the trailing "damage taken" chunk, in thousandths of the bar. */
int TrailPerMille = 1000;

/** The enemy debuff icons under the banner (Curtis 2026-10-30): Diablo's small spell icons, each with its strength under it. */
constexpr int IconWidth = 37;
constexpr int IconHeight = 38;
constexpr int IconGap = 4;
constexpr int IconNumberHeight = 13;
constexpr int TicksPerSecond = 20;

struct Debuff {
	SpellIcon icon;
	/** The icon's colours, as the spell book recolours its icons. */
	SpellType colours;
	/** The frame's colour: the element of an ailment, or the curse colour. */
	uint8_t frame;
	std::string number;
};

/** @brief A strength in percent, or in tenths of a percent below 10%, as POE2 shows it (2.9%, 17%). */
std::string PercentText(int perMille)
{
	const int sign = perMille < 0 ? -1 : 1;
	const int size = std::abs(perMille);
	if (size < 100 && size % 10 != 0)
		return StrCat(sign < 0 ? "-" : "", size / 10, ".", size % 10, "%");
	return StrCat(sign * (size / 10), "%");
}

/** @brief Time left for an effect that has no strength to show, such as a freeze or a stun. */
std::string SecondsText(int ticks)
{
	const int tenths = (std::max(ticks, 0) * 10 + TicksPerSecond - 1) / TicksPerSecond;
	return StrCat(tenths / 10, ".", tenths % 10, "s");
}

/** @brief Damage over time as the share of the monster's life it takes a second. */
std::string DamageOverTimeText(const Monster &monster, int32_t perTickRaw)
{
	const int64_t maxLife = std::max<int64_t>(monster.maxHitPoints.raw(), 1);
	const int64_t perMille = static_cast<int64_t>(std::max<int32_t>(perTickRaw, 0)) * TicksPerSecond * 1000 / maxLife;
	return PercentText(static_cast<int>(std::clamp<int64_t>(std::max<int64_t>(perMille, 1), 1, 9990)));
}

/** @brief Every ailment, curse and debuff on the monster, ailments first, with how strong each one is. */
std::vector<Debuff> MonsterDebuffs(const Monster &monster)
{
	constexpr uint8_t Fire = PAL16_ORANGE + 2;
	constexpr uint8_t Cold = PAL16_BLUE + 2;
	constexpr uint8_t Lightning = PAL16_YELLOW + 2;
	constexpr uint8_t Physical = PAL16_RED + 4;
	constexpr uint8_t Poison = PAL16_BEIGE + 6;
	constexpr uint8_t Curse = PAL16_RED + 10;
	constexpr uint8_t Other = PAL16_GRAY + 4;
	const Poe2MonsterState &state = monster.poe2;
	std::vector<Debuff> debuffs;
	const auto add = [&](SpellIcon icon, SpellType colours, uint8_t frame, std::string number) { debuffs.push_back({ icon, colours, frame, std::move(number) }); };
	// Diablo's icons, recoloured by element: orange for fire, blue for cold, gold for lightning, grey for the rest.
	// The red ones (bleeding, curses) are red already.
	if (state.igniteTicks > 0)
		add(SpellIcon::Inferno, SpellType::Charges, Fire, DamageOverTimeText(monster, state.igniteDamagePerTick));
	if (state.freezeTicks > 0)
		add(SpellIcon::StoneCurse, SpellType::Spell, Cold, SecondsText(state.freezeTicks));
	else if (state.chillTicks > 0)
		add(SpellIcon::Nova, SpellType::Spell, Cold, PercentText(state.chillPercent * 10));
	if (state.shockTicks > 0)
		add(SpellIcon::ChargedBolt, SpellType::Skill, Lightning, PercentText(ShockDamagePercent * MapExposurePercent() / 10));
	if (state.electrocuteTicks > 0)
		add(SpellIcon::Lightning, SpellType::Skill, Lightning, SecondsText(state.electrocuteTicks));
	if (state.poisonTicks > 0)
		add(SpellIcon::DoomSerpents, SpellType::Invalid, Poison, DamageOverTimeText(monster, state.poisonDamagePerTick));
	if (state.bleedTicks > 0)
		add(SpellIcon::BloodStar, SpellType::Skill, Physical, DamageOverTimeText(monster, state.bleedDamagePerTick));
	if (state.witherStacks > 0)
		add(SpellIcon::Apocalypse, SpellType::Invalid, Curse, PercentText(WitheredDamageTakenPercent(monster) * 10));
	if (state.heavyStunTicks > 0)
		add(SpellIcon::Flash, SpellType::Invalid, Other, SecondsText(state.heavyStunTicks));
	if (state.hinderTicks > 0)
		add(SpellIcon::Etherealize, SpellType::Invalid, Other, PercentText(MonsterHinderPercent(monster) * 10));
	if (state.armourBreakTicks > 0)
		add(SpellIcon::ItemRepair, SpellType::Invalid, Physical, PercentText(ArmourBreakDamagePercent * 10));
	if (state.exposureTicks > 0)
		add(SpellIcon::Elemental, SpellType::Spell, Curse, PercentText(-ExposurePercent * MapExposurePercent() / 100 * CurseEffectPercent(monster) / 10));
	// Curses.
	if (state.amplifyTicks > 0)
		add(SpellIcon::PentaStar, SpellType::Skill, Curse, PercentText(state.amplifyPercent * 10));
	if (state.lowerResistTicks > 0)
		add(SpellIcon::Pentagram, SpellType::Skill, Curse, PercentText(-LowerResistCursePercent * TableCurseStrength(monster, TableCurse::LowerResist) / 100 * CurseEffectPercent(monster) / 10));
	if (const int weaken = TableCurseShownPercent(monster, TableCurse::Weaken); weaken > 0)
		add(SpellIcon::LongHorn, SpellType::Skill, Curse, PercentText(-weaken * 10));
	if (const int decrepify = TableCurseShownPercent(monster, TableCurse::Decrepify); decrepify > 0)
		add(SpellIcon::RedSkull, SpellType::Skill, Curse, PercentText(decrepify * 10));
	if (const int deathMark = TableCurseShownPercent(monster, TableCurse::DeathMark); deathMark > 0)
		add(SpellIcon::BoneSpirit, SpellType::Skill, Curse, PercentText(deathMark * 10));
	if (const int ironMaiden = TableCurseShownPercent(monster, TableCurse::IronMaiden); ironMaiden > 0)
		add(SpellIcon::FireCloud, SpellType::Skill, Curse, PercentText(ironMaiden * 10));
	if (state.confuseTicks > 0)
		add(SpellIcon::Jester, SpellType::Skill, Curse, SecondsText(state.confuseTicks));
	return debuffs;
}

/** @brief The debuff icons in a row, centred on @p centerX, each framed in its colour with its strength in a box under it. */
void DrawDebuffIcons(const Surface &out, int centerX, int y, const std::vector<Debuff> &debuffs)
{
	if (debuffs.empty())
		return;
	const int count = static_cast<int>(debuffs.size());
	int x = centerX - ((count * IconWidth) + ((count - 1) * IconGap)) / 2;
	for (const Debuff &debuff : debuffs) {
		SetSpellTrans(debuff.colours);
		FillRectangle(out, { { x - 2, y - 2 }, { IconWidth + 4, IconHeight + IconNumberHeight + 5 } }, FrameDark);
		DrawSmallSpellIcon(out, { x, y + IconHeight - 1 }, debuff.icon);
		const Rectangle frame { { x - 1, y - 1 }, { IconWidth + 2, IconHeight + 2 } };
		DrawHorizontalLine(out, frame.position, frame.size.width, debuff.frame);
		DrawHorizontalLine(out, frame.position + Displacement { 0, frame.size.height - 1 }, frame.size.width, debuff.frame);
		DrawVerticalLine(out, frame.position, frame.size.height, debuff.frame);
		DrawVerticalLine(out, frame.position + Displacement { frame.size.width - 1, 0 }, frame.size.height, debuff.frame);
		DrawString(out, debuff.number, { { x - 2, y + IconHeight + 2 }, { IconWidth + 4, IconNumberHeight } }, { .flags = UiFlags::ColorWhite | UiFlags::FontSize12 | UiFlags::AlignCenter | UiFlags::VerticalCenter });
		x += IconWidth + IconGap;
	}
}

/** @brief A small gold diamond with a red gem, Diablo-style, capping each end of the bar. */
void DrawEndGem(const Surface &out, Point center, int radius)
{
	for (int y = -radius; y <= radius; y++) {
		const int half = radius - std::abs(y);
		DrawHorizontalLine(out, center + Displacement { -half, y }, (2 * half) + 1, y < 0 ? GoldLight : GoldDark);
	}
	const int inner = std::max(radius - 3, 1);
	for (int y = -inner; y <= inner; y++) {
		const int half = inner - std::abs(y);
		DrawHorizontalLine(out, center + Displacement { -half, y }, (2 * half) + 1, y < 0 ? GemColor : PAL16_RED + 8);
	}
}

void DrawShadowedText(const Surface &out, std::string_view text, const Rectangle &rect, UiFlags flags)
{
	DrawString(out, text, { rect.position + Displacement { 1, 1 }, rect.size }, { .flags = (flags & ~(UiFlags::ColorWhite | UiFlags::ColorBlue | UiFlags::ColorYellow | UiFlags::ColorWhitegold | UiFlags::ColorOrange | UiFlags::ColorRed | UiFlags::ColorButtonpushed)) | UiFlags::ColorBlack });
	DrawString(out, text, rect, { .flags = flags });
}

void DrawLifeBar(const Surface &out, const Rectangle &bar, const Monster &monster)
{
	// Outer dark rim, then a bevelled gold frame.
	FillRectangle(out, { bar.position - Displacement { 3, 3 }, { bar.size.width + 6, bar.size.height + 6 } }, FrameDark);
	DrawHorizontalLine(out, bar.position - Displacement { 2, 2 }, bar.size.width + 4, GoldLight);
	DrawVerticalLine(out, bar.position - Displacement { 2, 2 }, bar.size.height + 4, GoldLight);
	DrawHorizontalLine(out, bar.position + Displacement { -2, bar.size.height + 1 }, bar.size.width + 4, GoldDark);
	DrawVerticalLine(out, bar.position + Displacement { bar.size.width + 1, -2 }, bar.size.height + 4, GoldDark);
	FillRectangle(out, bar, BarEmpty);

	const int64_t maxLife = std::max<int64_t>(monster.maxHitPoints.raw(), 1);
	int64_t life = monster.hitPoints.raw();
	// Life-stealing monsters can go over their maximum; the overflow shows as a blue bar on top.
	const bool overflow = life > maxLife;
	if (overflow)
		life -= maxLife * (life / maxLife);
	const int perMille = static_cast<int>(std::clamp<int64_t>((life * 1000) / maxLife, 0, 1000));

	// The chunk just lost lingers in orange, then drains down to the new life.
	if (perMille > TrailPerMille)
		TrailPerMille = perMille;
	else
		TrailPerMille = std::max(perMille, TrailPerMille - std::max((TrailPerMille - perMille) / 12, 3));
	const int trailWidth = (bar.size.width * TrailPerMille) / 1000;
	if (trailWidth > 0)
		FillRectangle(out, { bar.position, { trailWidth, bar.size.height } }, TrailColor);

	const int width = (bar.size.width * perMille) / 1000;
	const uint8_t base = overflow ? PAL16_BLUE : PAL16_RED;
	for (int y = 0; y < bar.size.height; y++) {
		// Lighter at the top, darker at the bottom, like the life globe.
		const int shade = std::clamp(3 + ((y * 9) / std::max(bar.size.height - 1, 1)), 0, 15);
		if (width > 0)
			DrawHorizontalLine(out, bar.position + Displacement { 0, y }, width, static_cast<uint8_t>(base + shade));
	}
	if (width > 0)
		DrawHorizontalLine(out, bar.position, width, static_cast<uint8_t>(base + 1));

	// Tick marks every tenth of the bar make big hits easy to read.
	for (int tick = 1; tick < 10; tick++)
		DrawVerticalLine(out, bar.position + Displacement { (bar.size.width * tick) / 10, bar.size.height - 3 }, 3, FrameDark);

	const int radius = bar.size.height / 2 + 4;
	DrawEndGem(out, bar.position + Displacement { -radius - 1, bar.size.height / 2 }, radius);
	DrawEndGem(out, bar.position + Displacement { bar.size.width + radius, bar.size.height / 2 }, radius);
}

std::string ResistanceText(const Monster &monster)
{
	if (!monster.isUnique() && MonsterKillCounts[monster.type().type] < 15)
		return {};
	if (gbIsPoe2) {
		// POE2 Mashup: the resistances as they are now, curses and exposure included (Curtis 2026-10-18).
		constexpr struct {
			DamageType type;
			const char *name;
		} Poe2Elements[] = {
			{ DamageType::Fire, N_("Fire") },
			{ DamageType::Lightning, N_("Lightning") },
			{ DamageType::Magic, N_("Magic") },
			{ DamageType::Chaos, N_("Chaos") },
		};
		std::string text;
		for (const auto &element : Poe2Elements) {
			const int resistance = MonsterResistance(monster, element.type);
			if (resistance != 0)
				StrAppend(text, text.empty() ? "" : "   ", _(element.name), " ", resistance, "%");
		}
		return text;
	}
	constexpr struct {
		uint16_t resist;
		uint16_t immune;
		const char *name;
	} Elements[] = {
		{ RESIST_FIRE, IMMUNE_FIRE, N_("Fire") },
		{ RESIST_LIGHTNING, IMMUNE_LIGHTNING, N_("Lightning") },
		{ RESIST_MAGIC, IMMUNE_MAGIC, N_("Magic") },
	};
	std::string resists;
	std::string immunes;
	for (const auto &element : Elements) {
		if ((monster.shownResistance() & element.immune) != 0)
			StrAppend(immunes, immunes.empty() ? "" : ", ", _(element.name));
		else if ((monster.shownResistance() & element.resist) != 0)
			StrAppend(resists, resists.empty() ? "" : ", ", _(element.name));
	}
	std::string text;
	if (!resists.empty())
		StrAppend(text, _("Resists "), resists);
	if (!immunes.empty())
		StrAppend(text, text.empty() ? "" : "   ", _("Immune to "), immunes);
	return text;
}

} // namespace

void DrawMonsterBanner(const Surface &out)
{
	if (leveltype == DTYPE_TOWN) {
		ShownMonster = -1;
		return;
	}
	if (pcursmonst != -1) {
		if (pcursmonst != ShownMonster)
			TrailPerMille = 1000;
		ShownMonster = pcursmonst;
		Linger = LingerFrames;
	} else if (Linger > 0) {
		Linger--;
	} else {
		ShownMonster = -1;
	}
	if (ShownMonster < 0 || static_cast<size_t>(ShownMonster) >= MaxMonsters)
		return;
	const Monster &monster = Monsters[ShownMonster];
	if (monster.hasNoLife() || monster.isInvalid || monster.mode == MonsterMode::Death) {
		ShownMonster = -1;
		return;
	}

	const MonsterAffixes affixes = GetMonsterAffixes(monster);
	UiFlags nameColor = UiFlags::ColorWhite;
	std::string name { monster.name() };
	std::string subtitle;
	if (monster.isUnique()) {
		nameColor = UiFlags::ColorOrange;
	} else if (affixes.rarity == MonsterRarity::Rare) {
		nameColor = UiFlags::ColorYellow;
		name = RareMonsterName(monster);
		subtitle = monster.name();
	} else if (affixes.rarity == MonsterRarity::Magic) {
		nameColor = UiFlags::ColorBlue;
	}
	// The map boss and encounter leaders say what they are.
	if (const std::optional<std::string> title = MapMonsterTitle(monster); title) {
		subtitle = subtitle.empty() ? *title : StrCat(*title, " - ", subtitle);
		if (!monster.isUnique())
			nameColor = UiFlags::ColorOrange;
	}

	std::string mods;
	for (uint8_t mod = 0; mod < static_cast<uint8_t>(MonsterMod::Count); mod++) {
		if (affixes.has(static_cast<MonsterMod>(mod)))
			StrAppend(mods, mods.empty() ? "" : ", ", MonsterModName(static_cast<MonsterMod>(mod)));
	}

	const std::vector<Debuff> debuffs = MonsterDebuffs(monster);
	const std::string resistances = ResistanceText(monster);

	const int width = std::min(MaxWidth, out.w() - 60);
	int x = (out.w() - width) / 2;
	if (CanPanelsCoverView()) {
		if (IsRightPanelOpen())
			x -= SidePanelSize.width / 2;
		if (IsLeftPanelOpen())
			x += SidePanelSize.width / 2;
	}
	const int barHeight = monster.isUnique() ? UniqueBarHeight : BarHeight;
	int lines = 0;
	lines += subtitle.empty() ? 0 : 1;
	lines += mods.empty() ? 0 : 1;
	lines += resistances.empty() ? 0 : 1;
	const int iconsHeight = debuffs.empty() ? 0 : IconHeight + IconNumberHeight + 8;
	const int height = 6 + NameHeight + barHeight + 10 + (lines * LineHeight) + iconsHeight;

	// A soft dark backing so the text reads over any floor.
	DrawHalfTransparentRectTo(out, x - 24, Top - 4, width + 48, height);

	int y = Top;
	DrawShadowedText(out, name, { { x, y }, { width, NameHeight } }, nameColor | UiFlags::AlignCenter | UiFlags::VerticalCenter);
	y += NameHeight + 4;
	DrawLifeBar(out, { { x + 16, y }, { width - 32, barHeight } }, monster);
	y += barHeight + 6;
	if (!subtitle.empty()) {
		DrawShadowedText(out, subtitle, { { x, y }, { width, LineHeight } }, UiFlags::ColorWhite | UiFlags::FontSize12 | UiFlags::AlignCenter);
		y += LineHeight;
	}
	if (!mods.empty()) {
		DrawShadowedText(out, mods, { { x, y }, { width, LineHeight } }, (affixes.rarity == MonsterRarity::Rare ? UiFlags::ColorYellow : UiFlags::ColorBlue) | UiFlags::FontSize12 | UiFlags::AlignCenter);
		y += LineHeight;
	}
	if (!resistances.empty()) {
		DrawShadowedText(out, resistances, { { x, y }, { width, LineHeight } }, UiFlags::ColorButtonpushed | UiFlags::FontSize12 | UiFlags::AlignCenter);
		y += LineHeight;
	}
	DrawDebuffIcons(out, x + (width / 2), y + 4, debuffs);
}

} // namespace devilution
