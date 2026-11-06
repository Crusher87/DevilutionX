/**
 * @file dead.cpp
 *
 * Implementation of functions for placing dead monsters.
 */
#include "dead.h"

#include <cstdint>

#include "diablo.h"
#include "headless_mode.hpp"
#include "levels/gendung.h"
#include "lighting.h"
#include "monster.h"
#include "tables/misdat.h"

namespace devilution {

Corpse Corpses[MaxCorpses];
int8_t stonendx;

namespace {
/** @brief How many corpse slots are in use: InitCorpses's, and any added since (AddCorpseKind). */
int8_t CorpseKindCount = -1;

void InitDeadAnimationFromMonster(Corpse &corpse, const CMonster &mon)
{
	const AnimStruct &animData = mon.getAnimData(MonsterGraphic::Death);
	if (animData.sprites) {
		corpse.sprites.emplace(*animData.sprites);
	} else {
		corpse.sprites = std::nullopt;
	}
	corpse.frame = animData.frames - 1;
	corpse.width = animData.width;
}

void MoveLightToCorpse(Monster &monster)
{
	for (int dx = 0; dx < MAXDUNX; dx++) {
		for (int dy = 0; dy < MAXDUNY; dy++) {
			if ((dCorpse[dx][dy] & 0x1F) == monster.corpseId) {
				ChangeLightXY(monster.lightId, { dx, dy });
				return;
			}
		}
	}
	AddUnLight(monster.lightId);
}
} // namespace

void InitCorpses()
{
	int8_t mtypes[MaxMonsters] = {};

	int8_t nd = 0;

	for (size_t i = 0; i < LevelMonsterTypeCount; i++) {
		CMonster &monsterType = LevelMonsterTypes[i];
		if (mtypes[monsterType.type] != 0)
			continue;

		InitDeadAnimationFromMonster(Corpses[nd], monsterType);
		Corpses[nd].translationPaletteIndex = 0;
		nd++;

		monsterType.corpseId = nd;
		mtypes[monsterType.type] = nd;
	}

	nd++; // Unused blood spatter

	if (!HeadlessMode)
		Corpses[nd].sprites.emplace(*GetMissileSpriteData(MissileGraphicID::StoneCurseShatter).sprites);
	Corpses[nd].frame = 11;
	Corpses[nd].width = 128;
	Corpses[nd].translationPaletteIndex = 0;
	nd++;

	stonendx = nd;

	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		auto &monster = Monsters[ActiveMonsters[i]];
		if (monster.isUnique()) {
			InitDeadAnimationFromMonster(Corpses[nd], monster.type());
			Corpses[nd].translationPaletteIndex = ActiveMonsters[i] + 1;
			nd++;

			monster.corpseId = nd;
		}
	}

	assert(static_cast<unsigned>(nd) <= MaxCorpses);
	// POE2 Mashup: the slots after these hold no picture, not one left over from another level.
	CorpseKindCount = nd;
	for (unsigned i = static_cast<unsigned>(nd); i < MaxCorpses; i++)
		Corpses[i] = {};
}

int8_t AddCorpseKind(const CMonster &monsterType, uint8_t translationPaletteIndex)
{
	if (CorpseKindCount < 0 || static_cast<unsigned>(CorpseKindCount) >= MaxCorpses)
		return 0;
	Corpse &corpse = Corpses[CorpseKindCount];
	InitDeadAnimationFromMonster(corpse, monsterType);
	corpse.translationPaletteIndex = translationPaletteIndex;
	CorpseKindCount++;
	return CorpseKindCount;
}

void AddCorpse(Point tilePosition, int8_t dv, Direction ddir)
{
	// POE2 Mashup: a corpse with no picture loaded is left out rather than crash the drawing (Curtis 2026-11-06).
	if (!HeadlessMode && (dv & 0x1F) != 0 && !Corpses[(dv & 0x1F) - 1].sprites)
		return;
	dCorpse[tilePosition.x][tilePosition.y] = (dv & 0x1F) + (static_cast<int>(ddir) << 5);
}

void MoveLightsToCorpses()
{
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		auto &monster = Monsters[ActiveMonsters[i]];
		if (!monster.isUnique())
			continue;
		MoveLightToCorpse(monster);
	}
}

} // namespace devilution
