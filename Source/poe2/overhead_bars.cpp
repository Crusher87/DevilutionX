#include "poe2/overhead_bars.hpp"

#include <algorithm>
#include <climits>
#include <vector>

#include "engine/palette.h"
#include "engine/render/primitive_render.hpp"
#include "engine/ui_scale.hpp"
#include "game_mode.hpp"
#include "monster.h"
#include "options.h"
#include "panels/spell_icons.hpp"
#include "player.h"
#include "poe2/monster_mods.hpp"
#include "poe2/shrines.hpp"
#include "utils/clx_decode.hpp"
#include "utils/ui_fwd.h"

namespace devilution {

namespace {

/** @brief No more than this many bars a frame; a screen rarely holds more monsters. */
constexpr size_t MaxBars = 200;

enum class BarOwner : uint8_t {
	Hero,
	Monster,
	HeroShrine,
};

struct QueuedBar {
	BarOwner owner;
	/** @brief The monster's index; unused for the hero. */
	size_t index;
	/** @brief The middle of the bar's bottom edge, in the world drawing's buffer. */
	Point anchor;
};

std::vector<QueuedBar> Queue;
const Player *QueuedHero;

void QueueBar(BarOwner owner, size_t index, Point anchor)
{
	// The world is drawn once a frame, but a second note of the same character only moves its bar.
	for (QueuedBar &bar : Queue) {
		if (bar.owner == owner && bar.index == index) {
			bar.anchor = anchor;
			return;
		}
	}
	if (Queue.size() < MaxBars)
		Queue.push_back({ owner, index, anchor });
}

bool IsBigMonster(const Monster &monster)
{
	return monster.isUnique() || GetMonsterAffixes(monster).rarity == MonsterRarity::Rare;
}

/** @brief The anchor over a sprite: the middle of its top, from the solid top of its standing picture. */
Point AnchorOver(Point spritePosition, ClxSprite sprite, int standHeight, int standTopRow)
{
	const int solidHeight = standHeight - standTopRow;
	return { spritePosition.x + (sprite.width() / 2), spritePosition.y - solidHeight - OverheadBarGap };
}

void FillRows(const Surface &out, Point topLeft, int width, int rows, uint8_t color)
{
	if (width > 0 && rows > 0)
		FillRect(out, topLeft.x, topLeft.y, width, rows, color);
}

struct Rgb {
	uint8_t r, g, b;
};

// Colours read off POE2's bar (Curtis's picture 2026-11-06), top row first: dark edges, bright middle.
constexpr Rgb FrameTarget { 8, 8, 9 };
constexpr Rgb EmptyTarget { 22, 22, 25 };
constexpr std::array<Rgb, HeroLifeRows> LifeTargets { { { 27, 82, 29 }, { 31, 140, 46 }, { 30, 120, 40 }, { 30, 74, 27 } } };
// The Energy Shield is a pale wash over the life, palest at its edges.
constexpr std::array<Rgb, HeroLifeRows> EnergyShieldTargets { { { 141, 203, 182 }, { 79, 166, 97 }, { 74, 140, 89 }, { 131, 191, 173 } } };
// Where the life has run out, the wash lies over the dark empty bar.
constexpr std::array<Rgb, HeroLifeRows> EnergyShieldOverEmptyTargets { { { 110, 150, 145 }, { 55, 78, 76 }, { 50, 70, 68 }, { 100, 138, 132 } } };
constexpr std::array<Rgb, HeroManaRows> ManaTargets { { { 22, 62, 142 }, { 30, 79, 180 }, { 18, 45, 100 } } };

/**
 * @brief Fills a block in full colour where the UI layer allows it (the palette has no true green), in the palette's
 * nearest colour elsewhere.
 */
void FillTrueColour(const Surface &out, Point topLeft, int width, int rows, uint8_t index, Rgb colour)
{
	for (int y = 0; y < rows; y++) {
		for (int x = 0; x < width; x++)
			DrawUiTrueColourPixel(out, topLeft + Displacement { x, y }, index, colour.r, colour.g, colour.b);
	}
}

/** @brief Fills @p width pixels of each row with that row's shade. */
template <size_t Rows>
void FillShadedRows(const Surface &out, Point topLeft, int width, const std::array<uint8_t, Rows> &shades, const std::array<Rgb, Rows> &colours)
{
	for (size_t row = 0; row < Rows; row++)
		FillTrueColour(out, topLeft + Displacement { 0, static_cast<int>(row) }, width, 1, shades[row], colours[row]);
}

/** @brief A bar's frame, filled inside with the empty colour. */
void DrawFrame(const Surface &out, Point topLeft, int width, int height, uint8_t frameColor)
{
	FillRows(out, topLeft, width, height, frameColor);
	FillTrueColour(out, topLeft + Displacement { 1, 1 }, width - 2, height - 2, GetOverheadBarShades().empty, EmptyTarget);
}

/** @brief The hero's frame: near black, like POE2's. */
void DrawHeroFrame(const Surface &out, Point topLeft)
{
	const OverheadBarShades &shades = GetOverheadBarShades();
	FillTrueColour(out, topLeft, HeroBarWidth, HeroBarHeight, shades.frame, FrameTarget);
	FillTrueColour(out, topLeft + Displacement { 1, 1 }, HeroBarWidth - 2, HeroBarHeight - 2, shades.empty, EmptyTarget);
}

uint8_t NearestPaletteIndex(Rgb colour)
{
	// Not 0, nor 1-31, which some levels cycle for lava and water.
	int best = PAL16_GRAY;
	int bestDistance = INT_MAX;
	for (int i = 32; i < 256; i++) {
		const SDL_Color &candidate = logical_palette[i];
		const int dr = candidate.r - colour.r;
		const int dg = candidate.g - colour.g;
		const int db = candidate.b - colour.b;
		const int distance = (2 * dr * dr) + (4 * dg * dg) + (3 * db * db);
		if (distance < bestDistance) {
			bestDistance = distance;
			best = i;
		}
	}
	return static_cast<uint8_t>(best);
}

} // namespace

int ClxSolidTopRow(ClxSprite sprite)
{
	const uint8_t *src = sprite.pixelData();
	const uint8_t *end = src + sprite.pixelDataSize();
	const int width = sprite.width();
	const int height = sprite.height();
	if (width == 0)
		return height;
	// Rows are stored from the bottom up and a run can go on to the next row, so count pixels from the start.
	int position = 0;
	int highestRow = -1;
	while (src < end) {
		const uint8_t control = *src++;
		if (!IsClxOpaque(control)) {
			position += control;
			continue;
		}
		int run;
		if (IsClxOpaqueFill(control)) {
			run = GetClxOpaqueFillWidth(control);
			++src;
		} else {
			run = GetClxOpaquePixelsWidth(control);
			src += run;
		}
		highestRow = std::max(highestRow, (position + run - 1) / width);
		position += run;
	}
	if (highestRow < 0)
		return height;
	return std::max(0, height - 1 - highestRow);
}

namespace {

/** @brief The newest shrine buff's icon at half size, its bottom middle at @p bottom; it blinks for the last seconds. */
void DrawOverheadShrineIcon(const Surface &out, Point bottom, const Player &player)
{
	const ShrineBuff *newest = NewestShrineBuff(player.poe2.shrines);
	if (newest == nullptr)
		return;
	if (newest->ticksLeft <= ShrineBlinkTicks && (newest->ticksLeft / 5) % 2 == 1)
		return;
	OwnedSurface icon(37, 38);
	DrawSmallSpellIcon(icon, { 0, 37 }, static_cast<SpellIcon>(GetShrineData(newest->kind).icon));
	const Point topLeft = bottom - Displacement { OverheadShrineIconWidth / 2, OverheadShrineIconHeight };
	for (int y = 0; y < OverheadShrineIconHeight; y++) {
		for (int x = 0; x < OverheadShrineIconWidth; x++) {
			const Point target = topLeft + Displacement { x, y };
			if (out.InBounds(target))
				out.SetPixel(target, *icon.at(2 * x, 2 * y));
		}
	}
}

} // namespace

int OverheadBarFill(int64_t current, int64_t maximum, int width)
{
	if (maximum <= 0 || current <= 0 || width <= 0)
		return 0;
	if (current >= maximum)
		return width;
	return std::max(1, static_cast<int>((current * width) / maximum));
}

bool IsHurtEnemyForBar(const Monster &monster)
{
	if (monster.isInvalid || monster.isPlayerMinion() || (monster.flags & MFLAG_HIDDEN) != 0)
		return false;
	if (monster.mode == MonsterMode::Death || monster.hitPoints <= 0)
		return false;
	return monster.hitPoints < monster.maxHitPoints;
}

uint8_t MonsterBarFrameColor(const Monster &monster)
{
	if (monster.isUnique())
		return UniqueMonsterBarFrameColor;
	switch (GetMonsterAffixes(monster).rarity) {
	case MonsterRarity::Rare:
		return RareMonsterBarFrameColor;
	case MonsterRarity::Magic:
		return MagicMonsterBarFrameColor;
	default:
		return NormalMonsterBarFrameColor;
	}
}

void DrawMonsterBar(const Surface &out, Point topLeft, int width, const OverheadBarValues &values, uint8_t frameColor)
{
	DrawFrame(out, topLeft, width, MonsterBarHeight, frameColor);
	FillRows(out, topLeft + Displacement { 1, 1 }, OverheadBarFill(values.life, values.maxLife, width - 2), MonsterLifeRows, OverheadBarLifeColor);
}

const OverheadBarShades &GetOverheadBarShades()
{
	static OverheadBarShades shades {};
	static uint32_t matchedFor = 0;
	static bool matched = false;
	uint32_t checksum = 0;
	for (const SDL_Color &colour : logical_palette)
		checksum = (checksum * 31) + colour.r + (colour.g << 8) + (colour.b << 16);
	if (matched && checksum == matchedFor)
		return shades;
	shades.frame = NearestPaletteIndex(FrameTarget);
	shades.empty = NearestPaletteIndex(EmptyTarget);
	for (size_t i = 0; i < HeroLifeRows; i++) {
		shades.life[i] = NearestPaletteIndex(LifeTargets[i]);
		shades.energyShield[i] = NearestPaletteIndex(EnergyShieldTargets[i]);
		shades.energyShieldOverEmpty[i] = NearestPaletteIndex(EnergyShieldOverEmptyTargets[i]);
	}
	for (size_t i = 0; i < HeroManaRows; i++)
		shades.mana[i] = NearestPaletteIndex(ManaTargets[i]);
	matchedFor = checksum;
	matched = true;
	return shades;
}

std::vector<std::array<uint8_t, 3>> OverheadBarTargetColours()
{
	std::vector<std::array<uint8_t, 3>> colours;
	auto add = [&](Rgb colour) { colours.push_back({ colour.r, colour.g, colour.b }); };
	add(FrameTarget);
	add(EmptyTarget);
	for (const Rgb &colour : LifeTargets)
		add(colour);
	for (const Rgb &colour : EnergyShieldTargets)
		add(colour);
	for (const Rgb &colour : EnergyShieldOverEmptyTargets)
		add(colour);
	for (const Rgb &colour : ManaTargets)
		add(colour);
	return colours;
}

void DrawHeroBar(const Surface &out, Point topLeft, const OverheadBarValues &values, bool showEnergyShield)
{
	const OverheadBarShades &shades = GetOverheadBarShades();
	const int inner = HeroBarWidth - 2;
	DrawHeroFrame(out, topLeft);
	const Point life = topLeft + Displacement { 1, 1 };
	const int lifeWidth = OverheadBarFill(values.life, values.maxLife, inner);
	FillShadedRows(out, life, lifeWidth, shades.life, LifeTargets);
	// POE2 washes the Energy Shield over the whole height of the life bar, from the left.
	if (showEnergyShield && values.maxEnergyShield > 0) {
		const int shieldWidth = OverheadBarFill(values.energyShield, values.maxEnergyShield, inner);
		FillShadedRows(out, life, std::min(shieldWidth, lifeWidth), shades.energyShield, EnergyShieldTargets);
		if (shieldWidth > lifeWidth)
			FillShadedRows(out, life + Displacement { lifeWidth, 0 }, shieldWidth - lifeWidth, shades.energyShieldOverEmpty, EnergyShieldOverEmptyTargets);
	}
	const Point mana = life + Displacement { 0, HeroLifeRows + 1 };
	FillShadedRows(out, mana, OverheadBarFill(values.mana, values.maxMana, inner), shades.mana, ManaTargets);
}

void QueueHeroOverheadBar(const Player &player, Point spritePosition, ClxSprite sprite)
{
	if (!gbIsPoe2 || player.hasNoLife())
		return;
	const bool bar = *GetOptions().Gameplay.heroMiniBar;
	const bool shrine = NewestShrineBuff(player.poe2.shrines) != nullptr;
	if (!bar && !shrine)
		return;
	int standHeight = DefaultHeroHeight;
	int standTopRow = 0;
	const PlayerAnimationData &stand = player.AnimationData[static_cast<size_t>(player_graphic::Stand)];
	if (stand.sprites) {
		const ClxSprite standSprite = stand.spritesForDirection(Direction::South)[0];
		standHeight = standSprite.height();
		standTopRow = ClxSolidTopRow(standSprite);
	}
	QueuedHero = &player;
	const Point anchor = AnchorOver(spritePosition, sprite, standHeight, standTopRow);
	if (bar)
		QueueBar(BarOwner::Hero, 0, anchor);
	if (shrine)
		QueueBar(BarOwner::HeroShrine, 0, anchor);
}

void QueueMonsterOverheadBar(const Monster &monster, Point spritePosition, ClxSprite sprite)
{
	if (!gbIsPoe2 || !*GetOptions().Gameplay.hurtMonsterBars || !IsHurtEnemyForBar(monster))
		return;
	// Measured on the standing picture, so the bar doesn't bob as the monster swings.
	int standHeight = sprite.height();
	int standTopRow = ClxSolidTopRow(sprite);
	if (const OptionalClxSpriteList stand = monster.animDataOrWalk(MonsterGraphic::Stand).spritesForDirection(Direction::South); stand) {
		standHeight = (*stand)[0].height();
		standTopRow = ClxSolidTopRow((*stand)[0]);
	}
	QueueBar(BarOwner::Monster, monster.getId(), AnchorOver(spritePosition, sprite, standHeight, standTopRow));
}

void DrawOverheadBars(const Surface &out)
{
	if (Queue.empty())
		return;
	const Surface view = out.subregionY(0, gnViewportHeight);
	const bool zoomed = *GetOptions().Graphics.zoom;
	for (const QueuedBar &bar : Queue) {
		Point anchor = bar.anchor;
		if (zoomed)
			anchor *= 2;
		// From the full-size world to the scaled UI, as the item labels are.
		anchor = WorldToUi(anchor);
		if (bar.owner == BarOwner::Hero && QueuedHero != nullptr) {
			const Player &player = *QueuedHero;
			OverheadBarValues values;
			values.life = player._pHitPoints.raw();
			values.maxLife = player._pMaxHP.raw();
			values.energyShield = player.poe2.energyShield;
			values.maxEnergyShield = static_cast<int64_t>(player.poe2.maxEnergyShield) * 64;
			const bool noMana = HasAnyOf(player._pIFlags, ItemSpecialEffect::NoMana) || player.hasNoMana();
			values.mana = noMana ? 0 : player._pMana.raw();
			values.maxMana = noMana ? 0 : player._pMaxMana.raw();
			DrawHeroBar(view, anchor - Displacement { HeroBarWidth / 2, HeroBarHeight }, values, *GetOptions().Gameplay.heroMiniBarEnergyShield);
			continue;
		}
		if (bar.owner == BarOwner::HeroShrine && QueuedHero != nullptr) {
			// Above the mini bar when it's shown.
			const int lift = *GetOptions().Gameplay.heroMiniBar ? HeroBarHeight + 2 : 0;
			DrawOverheadShrineIcon(view, anchor - Displacement { 0, lift }, *QueuedHero);
			continue;
		}
		if (bar.owner != BarOwner::Monster || bar.index >= MaxMonsters)
			continue;
		const Monster &monster = Monsters[bar.index];
		if (!IsHurtEnemyForBar(monster))
			continue;
		OverheadBarValues values;
		values.life = monster.hitPoints.raw();
		values.maxLife = monster.maxHitPoints.raw();
		const int width = IsBigMonster(monster) ? BigMonsterBarWidth : MonsterBarWidth;
		DrawMonsterBar(view, anchor - Displacement { width / 2, MonsterBarHeight }, width, values, MonsterBarFrameColor(monster));
	}
	ClearOverheadBars();
}

void ClearOverheadBars()
{
	Queue.clear();
	QueuedHero = nullptr;
}

} // namespace devilution
