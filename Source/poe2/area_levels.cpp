#include "poe2/area_levels.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "data/file.hpp"
#include "data/record_reader.hpp"
#include "game_mode.hpp"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "monster.h"
#include "poe2/monster_levels.hpp"
#include "poe2/waystones.hpp"
#include "tables/monstdat.h"
#include "tables/questdat.hpp"
#include "utils/log.hpp"

namespace devilution {

namespace {

/** @brief Normal area level by raw dungeon level; 0 where the table has no row. */
std::array<uint8_t, NUMLEVELS> AreaByFloor {};
/** @brief Monster damage percent by raw dungeon level (txtdata/poe2/area_levels.tsv monsterDamagePercent). */
std::array<uint16_t, NUMLEVELS> DamagePercentByFloor {};

} // namespace

void LoadAreaLevels()
{
	AreaByFloor.fill(0);
	DamagePercentByFloor.fill(DefaultAreaMonsterDamagePercent);
	// The monster tables that go with the area levels; cleared outside POE2 mode too.
	LoadMonsterLevels();
	if (!gbIsPoe2)
		return;

	constexpr std::string_view Filename = "txtdata\\poe2\\area_levels.tsv";
	auto dataFile = DataFile::load(Filename);
	if (!dataFile.has_value()) {
		// Monsters keep Diablo's levels without the table, as in Diablo mode.
		LogError("Couldn't load the area levels: {}", Filename);
		return;
	}
	dataFile->skipHeaderOrDie(Filename);
	for (DataFileRecord record : *dataFile) {
		RecordReader reader { record, Filename };
		int floor = 0;
		int areaLevel = 0;
		int damagePercent = DefaultAreaMonsterDamagePercent;
		reader.readInt("floor", floor);
		reader.readInt("areaLevel", areaLevel);
		// The reader goes column by column, so the act and name columns are read past.
		int act = 0;
		reader.readInt("act", act);
		std::string name;
		reader.readString("name", name);
		reader.readInt("monsterDamagePercent", damagePercent);
		if (floor < 1 || floor >= NUMLEVELS || areaLevel < 1 || areaLevel > UINT8_MAX) {
			LogError("Bad area level row in {}: floor {}, area level {}", Filename, floor, areaLevel);
			continue;
		}
		AreaByFloor[static_cast<size_t>(floor)] = static_cast<uint8_t>(areaLevel);
		DamagePercentByFloor[static_cast<size_t>(floor)] = static_cast<uint16_t>(std::clamp(damagePercent, 1, 1000));
	}
}

int AreaLevelForFloor(int floor)
{
	if (floor < 1 || floor >= NUMLEVELS)
		return 0;
	return AreaByFloor[static_cast<size_t>(floor)];
}

int AreaMonsterDamagePercentForFloor(int floor)
{
	if (floor < 1 || floor >= NUMLEVELS)
		return DefaultAreaMonsterDamagePercent;
	return DamagePercentByFloor[static_cast<size_t>(floor)];
}

int AreaFloor()
{
	if (setlevel) {
		switch (setlvlnum) {
		case SL_SKELKING:
			return Quests[Q_SKELKING]._qlevel;
		case SL_BONECHAMB:
			return Quests[Q_SCHAMB]._qlevel;
		case SL_POISONWATER:
			return Quests[Q_PWATER]._qlevel;
		case SL_VILEBETRAYER:
			return Quests[Q_BETRAYER]._qlevel;
		default:
			// On other quest levels currlevel holds the quest level's number, not a floor.
			return 1;
		}
	}
	return currlevel;
}

int CurrentAreaLevel(int difficulty)
{
	// ActiveMapTier() is 0 on quest levels and in multiplayer, so peers always agree.
	if (const int tier = ActiveMapTier(); tier > 0)
		return MapMonsterAreaLevel(tier);
	if (!setlevel && leveltype == DTYPE_TOWN)
		return 1;
	const int areaLevel = AreaLevelForFloor(AreaFloor());
	if (areaLevel == 0)
		return 0;
	return areaLevel + (AreaLevelsPerDifficulty * difficulty);
}

int CurrentAreaLevel()
{
	return CurrentAreaLevel(sgGameInitInfo.nDifficulty);
}

int CurrentAreaMonsterDamagePercent()
{
	if (!gbIsPoe2 || sgGameInitInfo.nDifficulty != DIFF_NORMAL || ActiveMapTier() > 0)
		return DefaultAreaMonsterDamagePercent;
	if (!setlevel && leveltype == DTYPE_TOWN)
		return DefaultAreaMonsterDamagePercent;
	return AreaMonsterDamagePercentForFloor(AreaFloor());
}

int Poe2MonsterLevel(const Monster &monster, int difficulty)
{
	// The player's golems and minions (and golems gone berserk) get their levels elsewhere.
	if (!gbIsPoe2 || (monster.flags & MFLAG_GOLEM) != 0)
		return 0;
	const int areaLevel = CurrentAreaLevel(difficulty);
	if (areaLevel <= 0)
		return 0;
	if (monster.uniqueType == UniqueMonsterType::Butcher)
		return areaLevel + 5;
	// Diablo is a monster type, not a unique, and Na-Krul can be placed either way: both count as bosses.
	const _monster_id type = monster.type().type;
	if (monster.isUnique() || type == MT_DIABLO || type == MT_NAKRUL)
		return areaLevel + 2;
	return areaLevel;
}

} // namespace devilution
