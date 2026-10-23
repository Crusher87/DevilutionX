#include "poe2/experience.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <magic_enum/magic_enum.hpp>

#include "data/file.hpp"
#include "data/record_reader.hpp"
#include "game_mode.hpp"
#include "levels/gendung.h"
#include "monster.h"
#include "player.h"
#include "poe2/monster_mods.hpp"
#include "tables/monstdat.h"
#include "tables/playerdat.hpp"
#include "utils/log.hpp"

namespace devilution {

namespace {

constexpr size_t GapColumns = 41;

/** Base experience of a white monster, index = monster level - 1. */
std::vector<uint32_t> BaseExperience;
/** Permille by character level (index = level - 1) and level gap 0-40. */
std::vector<std::array<uint16_t, GapColumns>> GapPermille;
/** Experience percent of named uniques. */
std::unordered_map<std::string, int> UniqueBossPercent;
/** Experience percent of boss monster types that aren't uniques (Diablo). */
std::vector<std::pair<_monster_id, int>> TypeBossPercent;
/** Experience percent of the uniques not listed by name. */
int OtherUniquePercent = 100;

/** The Diablo experience table (assets/txtdata/Experience.tsv), kept to move old heroes onto the POE2 table. */
constexpr std::array<uint32_t, 50> DiabloThresholds = {
	2000, 4620, 8040, 12489, 18258, 25712, 35309, 47622, 63364, 83419,
	108879, 141086, 181683, 231075, 313656, 424067, 571190, 766569, 1025154, 1366227,
	1814568, 2401895, 3168651, 4166200, 5459523, 7130496, 9281874, 12042092, 15571031, 20066900,
	25774405, 32994399, 42095202, 53525811, 67831218, 85670061, 107834823, 135274799, 169122009, 210720231,
	261657253, 323800420, 399335440, 490808349, 601170414, 733825617, 892680222, 1082908612, 1310707109, 1583495809
};

bool HasExperienceTable()
{
	return GetMaximumCharacterLevel() > 0;
}

/** Total experience to leave a level on the loaded table (0 for level 0). */
uint32_t Threshold(int level)
{
	if (level <= 0 || !HasExperienceTable())
		return 0;
	return GetNextExperienceThresholdForLevel(static_cast<unsigned>(std::min<int>(level, GetMaximumCharacterLevel())));
}

uint32_t TableTop()
{
	if (!HasExperienceTable())
		return std::numeric_limits<uint32_t>::max();
	return Threshold(GetMaximumCharacterLevel());
}

void LoadBaseExperience()
{
	constexpr std::string_view Filename = "txtdata\\poe2\\monster_experience.tsv";
	auto dataFile = DataFile::load(Filename);
	if (!dataFile.has_value()) {
		LogError("Couldn't load the monster experience: {}", Filename);
		return;
	}
	dataFile->skipHeaderOrDie(Filename);
	for (DataFileRecord record : *dataFile) {
		RecordReader reader { record, Filename };
		unsigned level = 0;
		uint32_t experience = 0;
		reader.readInt("level", level);
		reader.readInt("baseXp", experience);
		if (level == 0 || level > 255)
			continue;
		if (BaseExperience.size() < level)
			BaseExperience.resize(level, 0);
		BaseExperience[level - 1] = experience;
	}
}

void LoadGapPermille()
{
	constexpr std::string_view Filename = "txtdata\\poe2\\experience_level_gap.tsv";
	auto dataFile = DataFile::load(Filename);
	if (!dataFile.has_value()) {
		LogError("Couldn't load the experience level gap: {}", Filename);
		return;
	}
	dataFile->skipHeaderOrDie(Filename);
	for (DataFileRecord record : *dataFile) {
		RecordReader reader { record, Filename };
		unsigned level = 0;
		reader.readInt("level", level);
		std::array<uint16_t, GapColumns> row {};
		for (uint16_t &permille : row)
			reader.readInt("gap", permille);
		if (level == 0 || level > 255)
			continue;
		if (GapPermille.size() < level)
			GapPermille.resize(level, row);
		GapPermille[level - 1] = row;
	}
}

void LoadBossPercent()
{
	constexpr std::string_view Filename = "txtdata\\poe2\\experience_bosses.tsv";
	auto dataFile = DataFile::load(Filename);
	if (!dataFile.has_value()) {
		LogError("Couldn't load the boss experience: {}", Filename);
		return;
	}
	dataFile->skipHeaderOrDie(Filename);
	for (DataFileRecord record : *dataFile) {
		RecordReader reader { record, Filename };
		std::string boss;
		int percent = 100;
		reader.readString("boss", boss);
		reader.readInt("xpPercent", percent);
		if (boss == "*") {
			OtherUniquePercent = percent;
		} else if (const std::optional<_monster_id> type = magic_enum::enum_cast<_monster_id>(boss); type) {
			TypeBossPercent.emplace_back(*type, percent);
		} else {
			UniqueBossPercent[boss] = percent;
		}
	}
}

} // namespace

void LoadPoe2ExperienceData()
{
	BaseExperience.clear();
	GapPermille.clear();
	UniqueBossPercent.clear();
	TypeBossPercent.clear();
	OtherUniquePercent = 100;
	if (!gbIsPoe2)
		return;
	LoadBaseExperience();
	LoadGapPermille();
	LoadBossPercent();
}

uint32_t MonsterBaseExperience(int monsterLevel)
{
	if (BaseExperience.empty())
		return 0;
	const int last = static_cast<int>(BaseExperience.size());
	return BaseExperience[static_cast<size_t>(std::clamp(monsterLevel, 1, last) - 1)];
}

int MonsterTypeExperiencePercent(const Monster & /*monster*/)
{
	// The monster type table's xpMult (the average of its life and damage ratios, 30-250%) goes here once it exists;
	// the levelling model assumes an average type, 100%.
	return 100;
}

int MonsterBossExperiencePercent(const Monster &monster)
{
	if (monster.isUnique()) {
		const auto index = static_cast<size_t>(monster.uniqueType);
		if (index < UniqueMonstersData.size()) {
			if (const auto it = UniqueBossPercent.find(UniqueMonstersData[index].mName); it != UniqueBossPercent.end())
				return it->second;
		}
		return OtherUniquePercent;
	}
	for (const auto &[type, percent] : TypeBossPercent) {
		if (monster.type().type == type)
			return percent;
	}
	return 100;
}

uint32_t Poe2MonsterExperience(const Monster &monster)
{
	const int level = static_cast<int>(monster.level(sgGameInitInfo.nDifficulty));
	int64_t experience = MonsterBaseExperience(level);
	experience = experience * MonsterTypeExperiencePercent(monster) / 100;
	experience = experience * MonsterExperiencePercent(monster) / 100;
	experience = experience * MonsterBossExperiencePercent(monster) / 100;
	return static_cast<uint32_t>(std::clamp<int64_t>(experience, 0, TableTop()));
}

int ExperienceLevelGapPermille(int characterLevel, int monsterLevel)
{
	if (monsterLevel >= ExperienceTopMonsterLevel || GapPermille.empty())
		return 1000;
	const int last = static_cast<int>(GapPermille.size());
	const auto row = static_cast<size_t>(std::clamp(characterLevel, 1, last) - 1);
	const auto gap = static_cast<size_t>(std::min<int>(std::abs(monsterLevel - characterLevel), static_cast<int>(GapColumns) - 1));
	return GapPermille[row][gap];
}

uint32_t Poe2ExperienceGain(int characterLevel, uint32_t experience, int monsterLevel, bool multiplayer)
{
	int64_t gain = static_cast<int64_t>(experience) * ExperienceLevelGapPermille(characterLevel, monsterLevel) / 1000;
	// Multiplayer keeps the cap at a twentieth of the level's threshold against power levelling; the 200 x level cap
	// would cut an area-38 kill to about a third.
	if (multiplayer && HasExperienceTable())
		gain = std::min<int64_t>(gain, Threshold(characterLevel) / 20);
	return static_cast<uint32_t>(std::clamp<int64_t>(gain, 0, std::numeric_limits<uint32_t>::max()));
}

uint32_t Poe2ShrineExperience(int areaLevel)
{
	return static_cast<uint32_t>(std::min<int64_t>(static_cast<int64_t>(MonsterBaseExperience(areaLevel)) * SparklingShrineKills, TableTop()));
}

int CurrentFloorMonsterLevel()
{
	int level = 0;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		const Monster &monster = Monsters[ActiveMonsters[i]];
		if (monster.isPlayerMinion() || monster.isUnique())
			continue;
		level = std::max(level, static_cast<int>(monster.level(sgGameInitInfo.nDifficulty)));
	}
	return level > 0 ? level : std::max<int>(currlevel, 1);
}

uint32_t ExperienceAfterMapDeath(int characterLevel, uint32_t experience)
{
	if (!HasExperienceTable() || characterLevel >= GetMaximumCharacterLevel())
		return experience;
	const int64_t low = Threshold(characterLevel - 1);
	const int64_t high = Threshold(characterLevel);
	if (high <= low || experience <= low)
		return experience;
	const int64_t loss = (high - low) * MapDeathExperiencePercent / 100;
	return static_cast<uint32_t>(std::max<int64_t>(low, static_cast<int64_t>(experience) - loss));
}

uint32_t DiabloExperienceThreshold(int level)
{
	if (level <= 0)
		return 0;
	return DiabloThresholds[static_cast<size_t>(std::min<int>(level, static_cast<int>(DiabloThresholds.size())) - 1)];
}

bool CorrectPoe2Experience(Player &player)
{
	if (!gbIsPoe2 || !HasExperienceTable())
		return false;
	const int level = player.getCharacterLevel();
	const int64_t low = Threshold(level - 1);
	const int64_t high = Threshold(level);
	const int64_t experience = player._pExperience;
	if (high < low || (experience >= low && experience <= high))
		return false;
	// Never land on the next level's threshold, so the hero doesn't level from the move itself.
	const int64_t top = high > low ? high - 1 : high;
	int64_t corrected = std::clamp(experience, low, top);
	if (level <= static_cast<int>(DiabloThresholds.size())) {
		const int64_t oldLow = DiabloExperienceThreshold(level - 1);
		const int64_t oldHigh = DiabloExperienceThreshold(level);
		if (oldHigh > oldLow && experience >= oldLow && experience <= oldHigh)
			corrected = std::min(low + (experience - oldLow) * (high - low) / (oldHigh - oldLow), top);
	}
	player._pExperience = static_cast<uint32_t>(corrected);
	return true;
}

} // namespace devilution
