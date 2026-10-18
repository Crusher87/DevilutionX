#include "panels/monster_banner.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
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
#include "poe2/endgame.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/monster_resistance.hpp"
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

	struct Tag {
		std::string_view text;
		UiFlags color;
	};
	std::vector<Tag> ailments;
	if (monster.poe2.igniteTicks > 0)
		ailments.push_back({ _("Ignited"), UiFlags::ColorOrange });
	if (monster.poe2.shockTicks > 0)
		ailments.push_back({ _("Shocked"), UiFlags::ColorYellow });
	if (monster.poe2.freezeTicks > 0)
		ailments.push_back({ _("Frozen"), UiFlags::ColorBlue });
	else if (monster.poe2.chillTicks > 0)
		ailments.push_back({ _("Chilled"), UiFlags::ColorBlue });
	if (monster.poe2.heavyStunTicks > 0)
		ailments.push_back({ _("Heavy Stunned"), UiFlags::ColorWhite });
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
	lines += (ailments.empty() && resistances.empty()) ? 0 : 1;
	const int height = 6 + NameHeight + barHeight + 10 + (lines * LineHeight);

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
	if (!ailments.empty() || !resistances.empty()) {
		// Ailments in their element's colour, then what the monster resists in grey.
		int lineWidth = 0;
		constexpr int Gap = 10;
		for (const Tag &tag : ailments)
			lineWidth += GetLineWidth(tag.text, GameFont12) + Gap;
		if (!resistances.empty())
			lineWidth += GetLineWidth(resistances, GameFont12);
		else
			lineWidth -= Gap;
		int tx = x + (width - lineWidth) / 2;
		for (const Tag &tag : ailments) {
			DrawShadowedText(out, tag.text, { { tx, y }, { width, LineHeight } }, tag.color | UiFlags::FontSize12);
			tx += GetLineWidth(tag.text, GameFont12) + Gap;
		}
		if (!resistances.empty())
			DrawShadowedText(out, resistances, { { tx, y }, { width, LineHeight } }, UiFlags::ColorButtonpushed | UiFlags::FontSize12);
	}
}

} // namespace devilution
