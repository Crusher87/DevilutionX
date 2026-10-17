#include "poe2/autosave.hpp"

#include <cstdint>

#ifdef USE_SDL3
#include <SDL3/SDL_timer.h>
#else
#include <SDL.h>
#endif

#include "cursor.h"
#include "diablo.h"
#include "game_mode.hpp"
#include "levels/gendung.h"
#include "loadsave.h"
#include "monster.h"
#include "multi.h"
#include "player.h"
#include "poe2/hardcore.hpp"

namespace devilution {

namespace {

/** Time between quiet-moment saves, in milliseconds. */
constexpr uint32_t QuietSaveInterval = 3 * 60 * 1000;
/** No monster may be this close (in tiles) for a quiet-moment save. */
constexpr int QuietRadius = 12;

bool Pending;
uint32_t LastSave;

bool CanSaveNow()
{
	if (!AutosaveEnabled() || MyPlayer == nullptr)
		return false;
	const Player &player = *MyPlayer;
	if (player._pmode == PM_DEATH || MyPlayerIsDead || player.hasNoLife() || HardcoreHeroFell())
		return false;
	// The game's own save keeps only items in the inventory, not on the cursor.
	return pcurs == CURSOR_HAND && player.HoldItem.isEmpty();
}

bool MonsterNearby()
{
	if (leveltype == DTYPE_TOWN)
		return false;
	const Point hero = MyPlayer->position.tile;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		const Monster &monster = Monsters[ActiveMonsters[i]];
		if (monster.hitPoints <= 0 || monster.isPlayerMinion())
			continue;
		if (hero.ManhattanDistance(monster.position.tile) <= QuietRadius)
			return true;
	}
	return false;
}

void Save()
{
	SaveGame();
	Pending = false;
	LastSave = SDL_GetTicks();
}

} // namespace

bool AutosaveEnabled()
{
	return gbIsPoe2 && !gbIsMultiplayer;
}

void RequestAutosave()
{
	if (AutosaveEnabled())
		Pending = true;
}

void ProcessAutosave()
{
	if (!CanSaveNow())
		return;
	const uint32_t now = SDL_GetTicks();
	if (LastSave == 0)
		LastSave = now;
	if (Pending || (now - LastSave >= QuietSaveInterval && !MonsterNearby()))
		Save();
}

void AutosaveNow()
{
	if (CanSaveNow())
		Save();
}

} // namespace devilution
