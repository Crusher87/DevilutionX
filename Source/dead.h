/**
 * @file dead.h
 *
 * Interface of functions for placing dead monsters.
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/clx_sprite.hpp"
#include "engine/direction.hpp"
#include "engine/point.hpp"

namespace devilution {

struct CMonster;

static constexpr unsigned MaxCorpses = 31;

struct Corpse {
	OptionalClxSpriteListOrSheet sprites;
	int frame;
	uint16_t width;
	uint8_t translationPaletteIndex;

	/**
	 * @brief Returns the sprite list for a given direction.
	 *
	 * @param direction One of the 16 directions. Valid range: [0, 15].
	 * @return ClxSpriteList
	 */
	[[nodiscard]] ClxSpriteList spritesForDirection(Direction direction) const
	{
		return sprites->isSheet() ? sprites->sheet()[static_cast<size_t>(direction)] : sprites->list();
	}
};

extern Corpse Corpses[MaxCorpses];
extern int8_t stonendx;

void InitCorpses();
void AddCorpse(Point tilePosition, int8_t dv, Direction ddir);
/**
 * @brief POE2 Mashup: a corpse slot for a monster type brought onto the level after InitCorpses (the monster spawner).
 * @param translationPaletteIndex A unique monster's index + 1, for its colours, or 0.
 * @return The corpse id, or 0 when every slot is taken.
 */
int8_t AddCorpseKind(const CMonster &monsterType, uint8_t translationPaletteIndex);
void MoveLightsToCorpses();

} // namespace devilution
