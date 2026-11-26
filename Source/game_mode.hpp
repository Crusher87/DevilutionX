#pragma once

#include <cstdint>
#include <string>

#include "levels/gendung_defs.hpp"
#include "utils/attributes.h"

namespace devilution {

struct GameData {
	int32_t size;
	/**
	 * Whether this is the shareware (spawn) edition. Part of the join-compatibility identity
	 * (edition + version + mod config), unlike `programid` which is now cosmetic branding.
	 */
	uint8_t isSpawn;
	uint8_t reserved[3];
	/** Branding id (e.g. "DRTL"/"HRTL"/"DXMD") for display only. A mod may change it via its manifest. */
	uint32_t programid;
	uint8_t versionMajor;
	uint8_t versionMinor;
	uint8_t versionPatch;
	_difficulty nDifficulty;
	uint8_t nTickRate;
	uint8_t bRunInTown;
	uint8_t bTheoQuest;
	uint8_t bCowQuest;
	uint8_t bFriendlyFire;
	uint8_t fullQuests;
	/** Used to initialise the seed table for dungeon levels so players in multiplayer games generate the same layout */
	uint32_t gameSeed[4];

	void swapLE();
};

extern DVL_API_FOR_TEST GameData sgGameInitInfo;

/* Are we in-game? If false, we're in the main menu. */
extern DVL_API_FOR_TEST bool gbRunGame;
/** Indicate if we are in a multiplayer game */
extern DVL_API_FOR_TEST bool gbIsMultiplayer;
/** Indicate if we only have access to demo data */
extern DVL_API_FOR_TEST bool gbIsSpawn;
/** Indicate if we have loaded the Hellfire expansion data */
extern DVL_API_FOR_TEST bool gbIsHellfire;
/** Indicate if the POE2 Mashup mod is active */
extern DVL_API_FOR_TEST bool gbIsPoe2;
/** Indicate if the Sanctuary Reforged mod is active: the HD look and the comfort features on plain Diablo and Hellfire rules. Off whenever POE2 Mashup is on. */
extern DVL_API_FOR_TEST bool gbIsReforged;
/** The POE2 Mashup mod's version, set by the mod and shown in the game menu. */
extern std::string Poe2ModVersion;
/** Indicate if we want vanilla savefiles */
extern DVL_API_FOR_TEST bool gbVanilla;
/** Whether the Hellfire mode is required (forced). */
extern bool forceHellfire;

/**
 * @brief Whether the look and comfort features are on (HD art and light, weather, atmosphere, effects, the corner map and
 * the like): with POE2 Mashup, which has always had them, or with Sanctuary Reforged, which is those alone.
 *
 * Game rules never ask this: items, monsters, levels, experience, saves and net messages only ask gbIsPoe2.
 */
inline bool HasEnhancements()
{
	return gbIsPoe2 || gbIsReforged;
}

/** @brief The game was started with --admin: the admin tools are there in every mode. */
extern DVL_API_FOR_TEST bool gbAdminSwitch;

/**
 * @brief Whether the admin tools (the game menu's Admin Tools, the item and monster spawner on Insert) are there:
 * always in POE2 Mashup, and in the other modes only when the game was started with --admin.
 */
inline bool AdminToolsOn()
{
	return gbIsPoe2 || gbAdminSwitch;
}

} // namespace devilution
