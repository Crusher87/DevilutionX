#include "poe2/overhead_bars.hpp"

#include <algorithm>
#include <vector>

#include "engine/palette.h"
#include "engine/render/primitive_render.hpp"
#include "engine/ui_scale.hpp"
#include "game_mode.hpp"
#include "monster.h"
#include "options.h"
#include "player.h"
#include "poe2/monster_mods.hpp"
#include "utils/clx_decode.hpp"
#include "utils/ui_fwd.h"

namespace devilution {

namespace {

/** @brief No more than this many bars a frame; a screen rarely holds more monsters. */
constexpr size_t MaxBars = 200;

enum class BarOwner : uint8_t {
	Hero,
	Monster,
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

/** @brief A bar's frame, filled inside with the empty colour. */
void DrawFrame(const Surface &out, Point topLeft, int width, int height, uint8_t frameColor)
{
	FillRows(out, topLeft, width, height, frameColor);
	FillRows(out, topLeft + Displacement { 1, 1 }, width - 2, height - 2, OverheadBarEmptyColor);
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

void DrawHeroBar(const Surface &out, Point topLeft, const OverheadBarValues &values, bool showEnergyShield)
{
	const int inner = HeroBarWidth - 2;
	DrawFrame(out, topLeft, HeroBarWidth, HeroBarHeight, HeroBarFrameColor);
	const Point life = topLeft + Displacement { 1, 1 };
	FillRows(out, life, OverheadBarFill(values.life, values.maxLife, inner), HeroLifeRows, OverheadBarLifeColor);
	// POE2 lays the Energy Shield over the top of the life bar.
	if (showEnergyShield && values.maxEnergyShield > 0)
		FillRows(out, life, OverheadBarFill(values.energyShield, values.maxEnergyShield, inner), HeroEnergyShieldRows, OverheadBarEnergyShieldColor);
	const Point mana = life + Displacement { 0, HeroLifeRows + 1 };
	FillRows(out, mana, OverheadBarFill(values.mana, values.maxMana, inner), HeroManaRows, OverheadBarManaColor);
}

void QueueHeroOverheadBar(const Player &player, Point spritePosition, ClxSprite sprite)
{
	if (!gbIsPoe2 || !*GetOptions().Gameplay.heroMiniBar || player.hasNoLife())
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
	QueueBar(BarOwner::Hero, 0, AnchorOver(spritePosition, sprite, standHeight, standTopRow));
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
