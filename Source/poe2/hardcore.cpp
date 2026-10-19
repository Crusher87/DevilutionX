#include "poe2/hardcore.hpp"

#include "game_mode.hpp"
#include "gamemenu.h"
#include "loadsave.h"
#include "multi.h"
#include "pfile.h"
#include "player.h"
#include "plrmsg.h"
#include "poe2/telemetry.hpp"
#include "qol/stash.h"
#include "utils/language.h"

namespace devilution {

namespace {

bool Fallen;
bool PendingSave;

} // namespace

bool IsHardcoreHero(const Player &player)
{
	return gbIsPoe2 && !gbIsMultiplayer && player.poe2.hardcore;
}

bool HardcoreHeroFell()
{
	return Fallen;
}

void FallFromHardcore(Player &player)
{
	if (&player != MyPlayer || !IsHardcoreHero(player))
		return;
	// The hardcore stash is saved as it stands before the hero leaves the league; it stays for the other hardcore heroes.
	Stash.dirty = true;
	sfile_write_stash();
	player.poe2.hardcore = false;
	Fallen = true;
	TelemetryLog("hardcore", "hero died in Hardcore and moves to Softcore");
}

void ContinueInSoftcore(Player &player)
{
	if (!Fallen || &player != MyPlayer)
		return;
	Fallen = false;
	PendingSave = true;
	MyPlayerIsDead = false;
	gamemenu_off();
	RestartTownLvl(player);
}

void FinishHardcoreFall()
{
	if (!PendingSave || MyPlayer == nullptr)
		return;
	PendingSave = false;
	Player &player = *MyPlayer;
	SetPlayerHitPoints(player, player._pMaxHP);
	player._pMana = player._pMaxMana;
	player._pManaBase = player._pMaxManaBase;
	// The stash path follows the hero's league, so this loads the Softcore stash.
	const bool isHellfireSaveGame = gbIsHellfireSaveGame;
	gbIsHellfireSaveGame = gbIsHellfire;
	LoadStash();
	gbIsHellfireSaveGame = isHellfireSaveGame;
	SaveGame();
	EventPlrMsg(_("Your hero fell in Hardcore. They live on in Softcore with what they wore and carried."), UiFlags::ColorWhitegold);
}

} // namespace devilution
