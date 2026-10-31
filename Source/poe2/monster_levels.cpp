#include "poe2/monster_levels.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <magic_enum/magic_enum.hpp>

#include "data/file.hpp"
#include "data/record_reader.hpp"
#include "game_mode.hpp"
#include "monster.h"
#include "player.h"
#include "poe2/area_levels.hpp"
#include "poe2/endgame.hpp"
#include "poe2/minion_gems.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/monster_resistance.hpp"
#include "poe2/spirit.hpp"
#include "poe2/spirit_skills.hpp"
#include "poe2/waystones.hpp"
#include "utils/log.hpp"

namespace devilution {

namespace {

struct LevelRow {
	int life = 0;
	int damage10 = 0;
	int accuracy = 0;
};

/** Index = monster level - 1. */
std::vector<LevelRow> LevelRows;
std::unordered_map<int, MonsterTypeLevelRow> TypeRows;
std::unordered_map<std::string, MonsterBossLevelRow> UniqueRows;
std::unordered_map<int, MonsterBossLevelRow> TypeBossRows;
/** Uniques not listed by name (a mod's). */
MonsterBossLevelRow OtherUniqueRow { 450, 150 };

const MonsterTypeLevelRow NoTypeRow {};

constexpr int64_t MaxMonsterLifeRaw = std::numeric_limits<int32_t>::max() / 4;

void LoadLevelRows()
{
	constexpr std::string_view Filename = "txtdata\\poe2\\monster_levels.tsv";
	auto dataFile = DataFile::load(Filename);
	if (!dataFile.has_value()) {
		// Monsters keep Diablo's life and damage without the table.
		LogError("Couldn't load the monster levels: {}", Filename);
		return;
	}
	dataFile->skipHeaderOrDie(Filename);
	for (DataFileRecord record : *dataFile) {
		RecordReader reader { record, Filename };
		int level = 0;
		LevelRow row;
		reader.readInt("level", level);
		reader.readInt("life", row.life);
		reader.readInt("damage10", row.damage10);
		reader.readInt("accuracy", row.accuracy);
		if (level < 1 || level > 255 || row.life < 1) {
			LogError("Bad monster level row in {}: level {}", Filename, level);
			continue;
		}
		if (LevelRows.size() < static_cast<size_t>(level))
			LevelRows.resize(static_cast<size_t>(level), row);
		LevelRows[static_cast<size_t>(level) - 1] = row;
	}
}

void LoadTypeRows()
{
	constexpr std::string_view Filename = "txtdata\\poe2\\monster_types.tsv";
	auto dataFile = DataFile::load(Filename);
	if (!dataFile.has_value()) {
		LogError("Couldn't load the monster types: {}", Filename);
		return;
	}
	dataFile->skipHeaderOrDie(Filename);
	for (DataFileRecord record : *dataFile) {
		RecordReader reader { record, Filename };
		std::string name;
		MonsterTypeLevelRow row;
		reader.readString("type", name);
		reader.readInt("lifeRef10", row.lifeRef10);
		reader.readInt("damageRef10", row.damageRef10);
		reader.readInt("toHitRef10", row.toHitRef10);
		reader.readInt("lifePercent", row.lifePercent);
		reader.readInt("damagePercent", row.damagePercent);
		reader.readInt("xpPercent", row.xpPercent);
		const std::optional<_monster_id> type = magic_enum::enum_cast<_monster_id>(name);
		if (!type) {
			LogError("Unknown monster type {} in {}", name, Filename);
			continue;
		}
		row.damagePercent = std::clamp(row.damagePercent, MinMonsterTypePercent, MaxMonsterTypePercent);
		row.xpPercent = std::clamp(row.xpPercent, MinMonsterTypePercent, MaxMonsterTypePercent);
		TypeRows[static_cast<int>(*type)] = row;
	}
}

void LoadBossRows()
{
	constexpr std::string_view Filename = "txtdata\\poe2\\monster_bosses.tsv";
	auto dataFile = DataFile::load(Filename);
	if (!dataFile.has_value()) {
		LogError("Couldn't load the monster bosses: {}", Filename);
		return;
	}
	dataFile->skipHeaderOrDie(Filename);
	for (DataFileRecord record : *dataFile) {
		RecordReader reader { record, Filename };
		std::string boss;
		MonsterBossLevelRow row;
		reader.readString("boss", boss);
		reader.readInt("lifePercent", row.lifePercent);
		reader.readInt("damagePercent", row.damagePercent);
		row.lifePercent = std::max(row.lifePercent, 1);
		row.damagePercent = std::max(row.damagePercent, 0);
		if (boss == "*") {
			OtherUniqueRow = row;
		} else if (const std::optional<_monster_id> type = magic_enum::enum_cast<_monster_id>(boss); type) {
			TypeBossRows[static_cast<int>(*type)] = row;
		} else {
			UniqueRows[boss] = row;
		}
	}
}

const LevelRow *RowForLevel(int level)
{
	if (LevelRows.empty())
		return nullptr;
	const int last = static_cast<int>(LevelRows.size());
	return &LevelRows[static_cast<size_t>(std::clamp(level, 1, last) - 1)];
}

int64_t ClampLifeRaw(int64_t raw)
{
	return std::clamp<int64_t>(raw, 64, MaxMonsterLifeRaw);
}

/** Life in 1/64ths of a monster of this kind at an area level on the tables: a boss's multiplier, else its roll. */
int64_t TableLifeRaw(const Monster &monster, int areaLevel, int roll)
{
	const int64_t white = static_cast<int64_t>(MonsterLevelLife(areaLevel)) * 64;
	if (const MonsterBossLevelRow *boss = MonsterBossRow(monster); boss != nullptr)
		return ClampLifeRaw(white * boss->lifePercent / 100);
	return ClampLifeRaw(white * MonsterLifeRatioPermille(monster.type().type, roll) / 1000);
}

/** The life Diablo's rules give a monster of this kind with this roll (InitMonster, PrepareUniqueMonst), in 1/64ths. */
int64_t DiabloLifeRaw(int hitPoints)
{
	int64_t raw = static_cast<int64_t>(hitPoints) * 64;
	if (!gbIsMultiplayer)
		raw = std::max<int64_t>(raw / 2, 64);
	if (sgGameInitInfo.nDifficulty == DIFF_NIGHTMARE)
		raw = (3 * raw) + (64 * static_cast<int64_t>(gbIsHellfire && !gbIsMultiplayer ? 50 : 100));
	else if (sgGameInitInfo.nDifficulty == DIFF_HELL)
		raw = (4 * raw) + (64 * static_cast<int64_t>(gbIsHellfire && !gbIsMultiplayer ? 100 : 200));
	return raw;
}

/** Sum of a monster's own data damage (the unique's for uniques): what its damage bytes hold on the tables. */
int OwnDataDamageSum(const Monster &monster)
{
	if (monster.isUnique()) {
		const auto index = static_cast<size_t>(monster.uniqueType);
		if (index < UniqueMonstersData.size())
			return UniqueMonstersData[index].mMinDamage + UniqueMonstersData[index].mMaxDamage;
	}
	return monster.data().minDamage + monster.data().maxDamage;
}

/** The table part of the damage scale, in per mille: hit at the level x ratio / own data average. */
int64_t TableDamagePermille(const Monster &monster)
{
	if ((monster.flags & MFLAG_POE2_TABLES) == 0 || !HasMonsterLevelTable())
		return 1000;
	const int level = Poe2MonsterLevel(monster, sgGameInitInfo.nDifficulty);
	if (level <= 0)
		return 1000;
	const int sum = OwnDataDamageSum(monster);
	// Lachdanan's 0-0 (B3): nothing to scale, and never a division by zero.
	if (sum <= 0)
		return 1000;
	const MonsterBossLevelRow *boss = MonsterBossRow(monster);
	const int ratio = boss != nullptr ? boss->damagePercent : MonsterTypeRow(monster.type().type).damagePercent;
	// 1000 x (hit10 / 10) x (ratio / 100) / (sum / 2), then the area's monster damage (70% in Act 1, plan step 14).
	return static_cast<int64_t>(MonsterLevelDamage10(level)) * ratio * 2 / sum * CurrentAreaMonsterDamagePercent() / 100;
}

/**
 * A player's minion's scale (plan v2 step 11): its level's table hit times its own damage over its type's ref, over its
 * own average; then its owner's minion damage, which used to go into the bytes and their 255 cap.
 */
int64_t MinionDamagePermille(const Monster &minion)
{
	const int level = HasMonsterLevelTable() ? Poe2MinionLevel(minion) : 0;
	const int ownerPercent = static_cast<size_t>(minion.goalVar3) < Players.size() ? MinionDamagePercent(Players[static_cast<size_t>(minion.goalVar3)]) : 100;
	return MinionDamageScalePermille(minion.type().type, level, minion.minDamage + minion.maxDamage, ownerPercent);
}

int64_t DamagePermille(const Monster &monster)
{
	if (!gbIsPoe2)
		return 1000;
	if (monster.isPlayerMinion())
		return MinionDamagePermille(monster);
	int64_t permille = TableDamagePermille(monster);
	// Hard Hitting and the waystone's monster damage, the map boss and the encounter leaders: once in the bytes.
	permille = permille * MonsterModsDamagePercent(monster) / 100;
	permille = permille * MapBossHitPercent(monster) / 100;
	permille = permille * EncounterLeaderDamagePercent(monster) / 100;
	return std::clamp<int64_t>(permille, 0, 100000000);
}

} // namespace

void LoadMonsterLevels()
{
	LevelRows.clear();
	TypeRows.clear();
	UniqueRows.clear();
	TypeBossRows.clear();
	OtherUniqueRow = { 450, 150 };
	LoadMonsterChaosResistances();
	LoadMonsterColdResistances();
	if (!gbIsPoe2)
		return;
	LoadLevelRows();
	LoadTypeRows();
	LoadBossRows();
}

bool HasMonsterLevelTable()
{
	return !LevelRows.empty();
}

int MonsterLevelLife(int level)
{
	const LevelRow *row = RowForLevel(level);
	return row != nullptr ? row->life : 0;
}

int MonsterLevelDamage10(int level)
{
	const LevelRow *row = RowForLevel(level);
	return row != nullptr ? row->damage10 : 0;
}

int MonsterLevelAccuracy(int level)
{
	const LevelRow *row = RowForLevel(level);
	return row != nullptr ? row->accuracy : 0;
}

const MonsterTypeLevelRow &MonsterTypeRow(_monster_id type)
{
	if (const auto it = TypeRows.find(static_cast<int>(type)); it != TypeRows.end())
		return it->second;
	return NoTypeRow;
}

const MonsterBossLevelRow *MonsterBossRow(const Monster &monster)
{
	if (LevelRows.empty())
		return nullptr;
	if (monster.isUnique()) {
		const auto index = static_cast<size_t>(monster.uniqueType);
		if (index < UniqueMonstersData.size()) {
			if (const auto it = UniqueRows.find(UniqueMonstersData[index].mName); it != UniqueRows.end())
				return &it->second;
		}
		return &OtherUniqueRow;
	}
	if (const auto it = TypeBossRows.find(static_cast<int>(monster.type().type)); it != TypeBossRows.end())
		return &it->second;
	return nullptr;
}

int MonsterLifeRatioPermille(_monster_id type, int roll)
{
	const int ref = MonsterTypeRow(type).lifeRef10;
	if (ref <= 0)
		return 1000;
	const int64_t permille = static_cast<int64_t>(roll) * 10000 / ref;
	return static_cast<int>(std::clamp<int64_t>(permille, MinMonsterTypePercent * 10, MaxMonsterTypePercent * 10));
}

bool ApplyPoe2MonsterLife(Monster &monster, int roll)
{
	if (!gbIsPoe2 || !HasMonsterLevelTable())
		return false;
	const int areaLevel = CurrentAreaLevel();
	if (areaLevel <= 0)
		return false;
	monster.maxHitPoints = Fixed26_6::fromRaw(static_cast<int32_t>(TableLifeRaw(monster, areaLevel, roll)));
	monster.hitPoints = monster.maxHitPoints;
	monster.flags |= MFLAG_POE2_TABLES;
	return true;
}

int Poe2MinionLevel(const Monster &minion)
{
	if (!gbIsPoe2)
		return 0;
	if (static_cast<size_t>(minion.goalVar3) >= Players.size())
		return 0;
	const Player &owner = Players[static_cast<size_t>(minion.goalVar3)];
	// The skill that keeps it, as support_table's MinionSkillOf finds it.
	PersistentSkill skill = PersistentSkill::SummonGolem;
	if (minion.poe2.revived) {
		skill = PersistentSkill::Revive;
	} else if (const MinionGemData *gem = FindMinionGemByType(minion.type().type); gem != nullptr) {
		skill = gem->skill;
	} else if (const PersistentSkill golem = ActiveGolemSkill(owner); golem != PersistentSkill::None) {
		skill = golem;
	}
	// A sceptre's granted Skeleton Warriors fight at the level it grants.
	return MinionLevelForGemLevel(std::max(owner.poe2.effectiveLevel(skill), 1));
}

int MinionHit10(_monster_id type, int level, int damageSum)
{
	const int ref = MonsterTypeRow(type).damageRef10;
	int64_t ratio = 1000;
	// (sum / 2) x 10 / ref10, in per mille.
	if (ref > 0)
		ratio = std::clamp<int64_t>(static_cast<int64_t>(std::max(damageSum, 0)) * 5000 / ref, MinMonsterTypePercent * 10, MaxMonsterTypePercent * 10);
	return static_cast<int>(static_cast<int64_t>(MonsterLevelDamage10(level)) * ratio / 1000);
}

int64_t MinionDamageScalePermille(_monster_id type, int level, int damageSum, int ownerMinionDamagePercent)
{
	int64_t permille = 1000;
	if (HasMonsterLevelTable() && level > 0 && damageSum > 0) {
		// 1000 x (hit10 / 10) / (sum / 2)
		permille = static_cast<int64_t>(MinionHit10(type, level, damageSum)) * 200 / damageSum;
	}
	permille = permille * ownerMinionDamagePercent / 100;
	return std::clamp<int64_t>(permille, 0, 100000000);
}

int32_t MinionLifeRaw(int level, int percent)
{
	return static_cast<int32_t>(ClampLifeRaw(static_cast<int64_t>(MonsterLevelLife(level)) * 64 * percent / 100));
}

int Poe2MonsterDamagePercent(const Monster &monster)
{
	return static_cast<int>((DamagePermille(monster) + 5) / 10);
}

int ScaleMonsterHit(const Monster &monster, int rolled)
{
	const int64_t permille = DamagePermille(monster);
	if (permille == 1000)
		return rolled;
	constexpr int64_t Limit = std::numeric_limits<int32_t>::max() / 4;
	return static_cast<int>(std::clamp<int64_t>(static_cast<int64_t>(rolled) * permille / 1000, -Limit, Limit));
}

void RescaleLoadedMonster(Monster &monster)
{
	if (!gbIsPoe2 || !HasMonsterLevelTable())
		return;
	if ((monster.flags & (MFLAG_POE2_TABLES | MFLAG_GOLEM | MFLAG_BERSERK)) != 0)
		return;
	const int areaLevel = CurrentAreaLevel();
	if (areaLevel <= 0)
		return;
	const bool unique = monster.isUnique() && static_cast<size_t>(monster.uniqueType) < UniqueMonstersData.size();
	const UniqueMonsterData *uniqueData = unique ? &UniqueMonstersData[static_cast<size_t>(monster.uniqueType)] : nullptr;
	const MonsterData &data = monster.data();
	const int midpoint = uniqueData != nullptr ? uniqueData->mmaxhp : (data.hitPointsMinimum + data.hitPointsMaximum) / 2;
	// The saved life is Diablo's x its roll and any multipliers (rarity, the map, a unique's pack); move the Diablo
	// part onto the table and keep the rest.
	const int64_t diablo = DiabloLifeRaw(midpoint);
	const int64_t table = TableLifeRaw(monster, areaLevel, midpoint);
	const int64_t oldMax = std::max<int64_t>(monster.maxHitPoints.raw(), 1);
	const int64_t newMax = ClampLifeRaw(oldMax * table / std::max<int64_t>(diablo, 1));
	const int64_t current = std::clamp<int64_t>(static_cast<int64_t>(monster.hitPoints.raw()) * newMax / oldMax, 0, MaxMonsterLifeRaw);
	monster.maxHitPoints = Fixed26_6::fromRaw(static_cast<int32_t>(newMax));
	monster.hitPoints = Fixed26_6::fromRaw(static_cast<int32_t>(current));

	// Damage is a percent of the data now; Nightmare/Hell, rarity and map boss multipliers left the bytes.
	if (uniqueData != nullptr) {
		monster.minDamage = uniqueData->mMinDamage;
		monster.maxDamage = uniqueData->mMaxDamage;
		monster.minDamageSpecial = uniqueData->mMinDamage;
		monster.maxDamageSpecial = uniqueData->mMaxDamage;
	} else {
		monster.minDamage = data.minDamage;
		monster.maxDamage = data.maxDamage;
		monster.minDamageSpecial = data.minDamageSpecial;
		monster.maxDamageSpecial = data.maxDamageSpecial;
	}
	if (sgGameInitInfo.nDifficulty == DIFF_NIGHTMARE)
		monster.armorClass = static_cast<uint8_t>(std::max(monster.armorClass - NightmareAcBonus, 0));
	else if (sgGameInitInfo.nDifficulty == DIFF_HELL)
		monster.armorClass = static_cast<uint8_t>(std::max(monster.armorClass - HellAcBonus, 0));
	monster.flags |= MFLAG_POE2_TABLES;
}

void Poe2TypeLifeRange(_monster_id type, int areaLevel, int &minLife, int &maxLife)
{
	const MonsterData &data = MonstersData[static_cast<size_t>(type)];
	const int64_t white = MonsterLevelLife(areaLevel);
	if (const auto it = TypeBossRows.find(static_cast<int>(type)); it != TypeBossRows.end()) {
		minLife = maxLife = static_cast<int>(std::max<int64_t>(white * it->second.lifePercent / 100, 1));
		return;
	}
	minLife = static_cast<int>(std::max<int64_t>(white * MonsterLifeRatioPermille(type, data.hitPointsMinimum) / 1000, 1));
	maxLife = static_cast<int>(std::max<int64_t>(white * MonsterLifeRatioPermille(type, data.hitPointsMaximum) / 1000, 1));
}

} // namespace devilution
