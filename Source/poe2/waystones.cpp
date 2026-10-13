#include "poe2/waystones.hpp"

#include <algorithm>

#include "game_mode.hpp"
#include "interfac.h"
#include "items.h"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "player.h"
#include "plrmsg.h"
#include "utils/language.h"
#include "utils/str_cat.hpp"

namespace devilution {

bool IsWaystone(const Item &item)
{
	return !item.isEmpty() && item._iMiscId == IMISC_WAYSTONE;
}

int WaystoneTierForLevel(int itemLevel)
{
	return std::clamp((itemLevel / 5) - 2, 1, MaxWaystoneTier);
}

int WaystoneTier(const Item &item)
{
	return WaystoneTierForLevel(item._iCreateInfo & CF_LEVEL);
}

int MapLevelForTier(int tier, uint32_t seed)
{
	// Levels 15, 16, 20 and 24 hold bosses and quests, so maps use the levels around them.
	if (tier >= 5 && gbIsHellfire)
		return 21 + static_cast<int>(seed % 3);
	return 13 + static_cast<int>(seed % 2);
}

int ActiveMapTier()
{
	if (!gbIsPoe2 || MyPlayer == nullptr || setlevel)
		return 0;
	const Poe2PlayerData &data = MyPlayer->poe2;
	if (data.mapTier == 0 || currlevel != data.mapLevel)
		return 0;
	return data.mapTier;
}

int PrepareMap(Player &player, int tier, uint32_t seed)
{
	tier = std::clamp(tier, 1, MaxWaystoneTier);
	const int level = MapLevelForTier(tier, seed);
	DungeonSeeds[level] = seed;
	LevelSeeds[level] = std::nullopt;
	player._pLvlVisited[level] = false;
	player.poe2.mapLevel = static_cast<uint8_t>(level);
	player.poe2.mapTier = static_cast<uint8_t>(tier);
	return level;
}

bool UseWaystone(Player &player, Item &waystone)
{
	if (!IsWaystone(waystone) || &player != MyPlayer)
		return false;
	if (gbIsMultiplayer) {
		EventPlrMsg(_("Maps aren't available in multiplayer yet."), UiFlags::ColorRed);
		return false;
	}
	if (!player.isOnLevel(0)) {
		EventPlrMsg(_("Use waystones in town to open a map."), UiFlags::ColorRed);
		return false;
	}
	const int tier = WaystoneTier(waystone);
	const int level = PrepareMap(player, tier, waystone._iSeed);
	EventPlrMsg(StrCat(_("Map tier "), tier, _(": monsters are tougher and more of them are magic or rare.")), UiFlags::ColorWhitegold);
	// Level 21 is the Hellfire crypt, which is entered from town.
	StartNewLvl(player, level != 21 ? WM_DIABNEXTLVL : WM_DIABTOWNWARP, level);
	return true;
}

void UpdateActiveMap(Player &player)
{
	if (player.poe2.mapTier == 0 || leveltype == DTYPE_TOWN)
		return;
	if (setlevel || currlevel != player.poe2.mapLevel) {
		player.poe2.mapTier = 0;
		player.poe2.mapLevel = 0;
	}
}

} // namespace devilution
