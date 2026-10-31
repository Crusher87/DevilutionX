#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>

#include <magic_enum/magic_enum.hpp>

#include "data/file.hpp"
#include "data/record_reader.hpp"
#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/area_levels.hpp"
#include "poe2/map_mods.hpp"
#include "poe2/map_play.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/monster_armour.hpp"
#include "poe2/monster_levels.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/skill_resources.hpp"
#include "poe2/spirit_buffs.hpp"
#include "poe2/support_table.hpp"
#include "poe2/support_tiers.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/unique_effects.hpp"
#include "poe2/waystones.hpp"
#include "poe2/weapon_skills.hpp"
#include "spells.h"
#include "storm/storm_net.hpp"
#include "tables/monstdat.h"
#include "tables/playerdat.hpp"
#include "utils/file_util.h"
#include "utils/paths.h"

namespace devilution {
namespace {

std::string ReadLatestEvents()
{
	TelemetryFlush();
	const std::string folder = paths::PrefPath() + "poe2_logs" + DIRECTORY_SEPARATOR_STR;
	std::string latest;
	for (const std::string &file : ListFiles(folder.c_str())) {
		if (file.ends_with(".events") && file > latest)
			latest = file;
	}
	std::ifstream stream(folder + latest);
	std::stringstream contents;
	contents << stream.rdbuf();
	return contents.str();
}

/** @brief Monster armour (research/monster-armour/plan-v1.md and its audit). */
class Poe2MonsterArmourTest : public ::testing::Test {
public:
	static void SetUpTestSuite()
	{
		paths::SetPrefPath(::testing::TempDir() + "poe2_armour_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + DIRECTORY_SEPARATOR_STR);
		TelemetryInit();
	}

	void SetUp() override
	{
		Players.resize(1);
		MyPlayer = &Players[0];
		MyPlayerId = 0;
		*MyPlayer = {};
		MyPlayer->poe2.clear();
		gbIsSpawn = false;
		gbIsMultiplayer = false;
		gbIsHellfire = true;
		gbIsPoe2 = true;
		sgGameInitInfo.nDifficulty = DIFF_NORMAL;
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		LoadItemData();
		LoadSpellData();
		LoadPlayerDataFiles();
		LoadPassiveTreeData();
		LoadModifierData();
		LoadMissileData();
		LoadMonsterData();
		Missiles.clear();
		ActiveMonsterCount = 0;
		setlevel = false;
		setlvlnum = SL_NONE;
		currlevel = 8;
		leveltype = DTYPE_CATACOMBS;
		// Also loads the monster level tables and the armour table.
		LoadAreaLevels();
		SNetInitializeProvider(SELCONN_LOOPBACK, nullptr);

		Player &player = *MyPlayer;
		player._pClass = HeroClass::Warrior;
		player.setCharacterLevel(20);
		player._pBaseStr = player._pStrength = 100;
		player._pBaseMag = player._pMagic = 50;
		player._pBaseDex = player._pDexterity = 50;
		player._pBaseVit = player._pVitality = 50;
		player._pMaxHPBase = player._pHPBase = player._pMaxHP = player._pHitPoints = Fixed26_6::fromInt(200);
		player._pIMinDam = 20;
		player._pIMaxDam = 30;
		player.position.tile = { 40, 40 };
		player._pdir = Direction::East;
	}

	void TearDown() override
	{
		ClearSupportEffects();
		ActiveMonsterCount = 0;
		Missiles.clear();
		gbIsPoe2 = false;
		sgGameInitInfo.nDifficulty = DIFF_NORMAL;
		currlevel = 0;
		leveltype = DTYPE_TOWN;
		LoadAreaLevels();
		LoadModifierData();
		UnloadModArchives();
	}

	/** @brief A plain monster of the kind on the tables (or not), with plenty of life, standing next to the hero. */
	static Monster &PlaceMonster(size_t slot, _monster_id kind, bool onTables = true, UniqueMonsterType unique = UniqueMonsterType::None)
	{
		LevelMonsterTypes[slot].type = kind;
		Monster &monster = Monsters[slot];
		monster = {};
		monster.levelType = static_cast<uint8_t>(slot);
		monster.uniqueType = unique;
		monster.talkMsg = TEXT_NONE;
		for (uint32_t seed = 1; seed < 100000; seed++) {
			monster.rndItemSeed = seed;
			if (unique != UniqueMonsterType::None || GetMonsterAffixes(monster).rarity == MonsterRarity::Normal)
				break;
		}
		monster.flags = onTables ? MFLAG_POE2_TABLES : 0;
		monster.mode = MonsterMode::Petrified;
		monster.position.tile = { static_cast<WorldTileCoord>(41 + slot), 40 };
		monster.maxHitPoints = monster.hitPoints = Fixed26_6::fromInt(100000);
		ActiveMonsters[ActiveMonsterCount] = static_cast<unsigned>(slot);
		ActiveMonsterCount++;
		return monster;
	}

	/** @brief Sockets a skill in the first Skills panel row with one support of the given tier. */
	static SkillSlot Supported(SkillSlot skill, SupportGem gem = SupportGem::None, int tier = 1)
	{
		Player &player = *MyPlayer;
		player.poe2.skillSlots[0] = skill;
		player.poe2.slotLevels[0] = 1;
		player.poe2.slotSupports[0] = {};
		player.poe2.slotSupportTiers[0] = {};
		if (gem != SupportGem::None) {
			player.poe2.slotSupports[0][0] = gem;
			player.poe2.slotSupportTiers[0][0] = static_cast<uint8_t>(tier);
		}
		return skill;
	}

	static int Level(const Monster &monster)
	{
		return Poe2MonsterLevel(monster, sgGameInitInfo.nDifficulty);
	}

	static int32_t LifeLost(const Monster &monster)
	{
		return Fixed26_6::fromInt(100000).raw() - monster.hitPoints.raw();
	}
};

TEST_F(Poe2MonsterArmourTest, TableCoversEveryKind)
{
	// Every _monster_id of the Hellfire monster data has exactly one row.
	constexpr std::string_view Filename = "txtdata\\poe2\\monster_armour.tsv";
	auto dataFile = DataFile::load(Filename);
	ASSERT_TRUE(dataFile.has_value());
	dataFile->skipHeaderOrDie(Filename);
	std::set<std::string> kinds;
	for (DataFileRecord record : *dataFile) {
		RecordReader reader { record, Filename };
		std::string name;
		int percent = -1;
		reader.readString("type", name);
		reader.readInt("armourPercent", percent);
		EXPECT_TRUE(magic_enum::enum_cast<_monster_id>(name).has_value()) << name;
		EXPECT_TRUE(kinds.insert(name).second) << "twice: " << name;
		EXPECT_GE(percent, 0) << name;
		EXPECT_LE(percent, 100) << name;
	}
	EXPECT_EQ(kinds.size(), 138U);
	ASSERT_EQ(MonstersData.size(), 138U);
	for (size_t i = 0; i < MonstersData.size(); i++)
		EXPECT_TRUE(kinds.contains(std::string(magic_enum::enum_name(static_cast<_monster_id>(i))))) << i;

	EXPECT_EQ(MonsterTypeArmourPercent(MT_NBLACK), 100);
	EXPECT_EQ(MonsterTypeArmourPercent(MT_GARGOYLE), 100);
	EXPECT_EQ(MonsterTypeArmourPercent(MT_XSKELSD), 75);
	EXPECT_EQ(MonsterTypeArmourPercent(MT_CLEAVER), 50);
	EXPECT_EQ(MonsterTypeArmourPercent(MT_WSKELAX), 25);
	EXPECT_EQ(MonsterTypeArmourPercent(MT_FIEND), 0);
	EXPECT_EQ(MonsterTypeArmourPercent(MT_NZOMBIE), 0);
	EXPECT_EQ(MonsterTypeArmourPercent(-1), 0);
}

TEST_F(Poe2MonsterArmourTest, FormulaMatchesPoe2)
{
	// POE2: armour / (armour + 10 x hit), at most 75% for monsters.
	EXPECT_EQ(ArmourReductionPercentCapped(433, 642 * 64, MaxMonsterArmourReductionPercent), 6);
	EXPECT_EQ(ArmourReductionPercentCapped(1249, 5 * 64, MaxMonsterArmourReductionPercent), 75);
	EXPECT_EQ(ArmourReductionPercentCapped(100, 10 * 64, MaxMonsterArmourReductionPercent), 50);
	EXPECT_EQ(ArmourReductionPercentCapped(100, 40 * 64, MaxMonsterArmourReductionPercent), 20);
	EXPECT_EQ(ArmourReductionPercentCapped(0, 40 * 64, MaxMonsterArmourReductionPercent), 0);
	EXPECT_EQ(ArmourReductionPercentCapped(100, 0, MaxMonsterArmourReductionPercent), 0);
	// The cap holds for any armour against the smallest hit.
	EXPECT_EQ(ArmourReductionPercentCapped(1000000, 1, MaxMonsterArmourReductionPercent), MaxMonsterArmourReductionPercent);
}

TEST_F(Poe2MonsterArmourTest, RatingIsTheLevelsArmourTimesTheKind)
{
	const Monster &knight = PlaceMonster(0, MT_NBLACK);
	const Monster &captain = PlaceMonster(1, MT_WSKELSD);
	const Monster &fiend = PlaceMonster(2, MT_FIEND);
	const int level = Level(knight);
	ASSERT_GT(level, 0);
	ASSERT_GT(MonsterLevelArmour(level), 0);
	EXPECT_EQ(MonsterArmourPercent(knight), 100);
	EXPECT_EQ(MonsterArmourRating(knight), MonsterLevelArmour(level));
	EXPECT_EQ(MonsterArmourRating(captain), MonsterLevelArmour(level) / 2);
	EXPECT_EQ(MonsterArmourRating(fiend), 0);
	EXPECT_EQ(MonsterArmourReductionPercent(fiend, 64), 0);
	// Off the tables (Diablo's numbers): none.
	const Monster &old = PlaceMonster(3, MT_NBLACK, false);
	EXPECT_EQ(MonsterArmourRating(old), 0);
}

TEST_F(Poe2MonsterArmourTest, ZeroOutsidePoe2)
{
	Monster &knight = PlaceMonster(0, MT_NBLACK);
	ASSERT_GT(MonsterArmourRating(knight), 0);
	gbIsPoe2 = false;
	EXPECT_EQ(MonsterArmourRating(knight), 0);
	EXPECT_EQ(MonsterArmourPercent(knight), 0);
	EXPECT_EQ(MonsterHitDamage(*MyPlayer, knight, DamageType::Physical, 640), 640);
	EXPECT_TRUE(MonsterArmourText(knight).empty());
	LoadMonsterArmour();
	EXPECT_EQ(MonsterTypeArmourPercent(MT_NBLACK), 0);
	gbIsPoe2 = true;
	LoadMonsterArmour();
	EXPECT_EQ(MonsterTypeArmourPercent(MT_NBLACK), 100);
}

TEST_F(Poe2MonsterArmourTest, PlayerMinionsHaveNone)
{
	Monster &golem = PlaceMonster(0, MT_NBLACK);
	golem.flags |= MFLAG_GOLEM;
	golem.goalVar3 = 0;
	EXPECT_EQ(MonsterArmourPercent(golem), 0);
	EXPECT_EQ(MonsterArmourRating(golem), 0);
}

TEST_F(Poe2MonsterArmourTest, BrokenArmourIsZero)
{
	Monster &knight = PlaceMonster(0, MT_NBLACK);
	ASSERT_GT(MonsterArmourRating(knight), 0);
	const int taken = MonsterDamageTakenPercent(knight, DamageType::Physical);
	// Smite and Crack Armor: no armour, and the 25% more physical damage taken stays.
	BreakMonsterArmour(knight, MyPlayer);
	EXPECT_EQ(MonsterArmourRating(knight), 0);
	EXPECT_EQ(MonsterArmourPercent(knight), 100) << "the kind keeps its armour, the break hides it";
	EXPECT_EQ(MonsterDamageTakenPercent(knight, DamageType::Physical), taken + ArmourBreakDamagePercent);
	EXPECT_EQ(MonsterArmourText(knight), "Armour broken");
	knight.poe2.armourBreakTicks = 0;
	EXPECT_EQ(MonsterArmourText(knight), "Heavy armour");
}

TEST_F(Poe2MonsterArmourTest, NightmareUsesAreaLevel)
{
	const Monster &knight = PlaceMonster(0, MT_NBLACK);
	const int normal = MonsterArmourRating(knight);
	const int normalLevel = Level(knight);
	sgGameInitInfo.nDifficulty = DIFF_NIGHTMARE;
	ASSERT_EQ(Level(knight), normalLevel + 15);
	EXPECT_EQ(MonsterArmourRating(knight), MonsterLevelArmour(normalLevel + 15));
	EXPECT_GT(MonsterArmourRating(knight), normal);
}

TEST_F(Poe2MonsterArmourTest, UniqueUsesKindRow)
{
	const Monster &king = PlaceMonster(0, MT_SKING, true, UniqueMonsterType::SkeletonKing);
	ASSERT_TRUE(king.isUnique());
	ASSERT_EQ(Level(king), CurrentAreaLevel() + 2);
	EXPECT_EQ(MonsterArmourRating(king), MonsterLevelArmour(CurrentAreaLevel() + 2));
	const Monster &butcher = PlaceMonster(1, MT_CLEAVER, true, UniqueMonsterType::Butcher);
	EXPECT_EQ(MonsterArmourRating(butcher), MonsterLevelArmour(CurrentAreaLevel() + 5) / 2);
}

TEST_F(Poe2MonsterArmourTest, PlatedGivesFloorOnUnarmouredKind)
{
	const auto plated = [](_monster_id kind, MonsterRarity rarity) -> Monster & {
		Monster &monster = PlaceMonster(0, kind);
		for (uint32_t seed = 1; seed < 400000; seed++) {
			monster.rndItemSeed = seed;
			const MonsterAffixes affixes = GetMonsterAffixes(monster);
			if (affixes.rarity == rarity && affixes.has(MonsterMod::Plated))
				break;
		}
		return monster;
	};
	EXPECT_EQ(MonsterArmourPercent(plated(MT_NZOMBIE, MonsterRarity::Rare)), PlatedModMinimumPercent * PlatedModMultiplier);
	EXPECT_EQ(MonsterArmourPercent(plated(MT_WSKELAX, MonsterRarity::Magic)), PlatedModMinimumPercent * PlatedModMultiplier);
	EXPECT_EQ(MonsterArmourPercent(plated(MT_NBLACK, MonsterRarity::Rare)), 200);
	EXPECT_EQ(MonsterArmourText(plated(MT_NZOMBIE, MonsterRarity::Rare)), "Heavy armour");
}

TEST_F(Poe2MonsterArmourTest, MapArmourRaisesArmourNotEvasion)
{
	Player &player = *MyPlayer;
	const auto openMapWith = [&](MapDanger danger) {
		uint8_t family = 0;
		for (uint8_t id = 1; id <= WaystoneModIds; id++) {
			if (GetWaystoneMod(id).danger == danger)
				family = id;
		}
		ASSERT_NE(family, 0);
		currlevel = 21;
		leveltype = DTYPE_HELL;
		player.poe2.mapTier = 1;
		player.poe2.mapLevel = currlevel;
		player.poe2.mapWaystone = {};
		player.poe2.mapWaystone.mods[0] = { family, 1, 0 };
		RefreshMapEffects(player);
		ClearMapPlay();
		ASSERT_GT(ActiveMapTier(), 0);
		ASSERT_GT(ActiveMapEffects()[danger], 0);
	};
	const auto closeMap = [&]() {
		player.poe2.mapTier = 0;
		player.poe2.mapLevel = 0;
		player.poe2.mapWaystone = {};
		RefreshMapEffects(player);
		ClearMapPlay();
	};

	openMapWith(MapDanger::MonsterArmour);
	const int increased = ActiveMapEffects()[MapDanger::MonsterArmour];
	Monster &knight = PlaceMonster(0, MT_NBLACK);
	knight.armorClass = 50;
	ApplyMonsterRarity(knight);
	EXPECT_EQ(knight.armorClass, 50) << "armour no longer makes monsters harder to hit";
	EXPECT_EQ(MonsterArmourPercent(knight), 100 + increased);
	// On a kind without armour, increased armour does nothing (POE2).
	EXPECT_EQ(MonsterArmourPercent(PlaceMonster(1, MT_FIEND)), 0);
	closeMap();

	openMapWith(MapDanger::MonsterEvasion);
	Monster &evasive = PlaceMonster(2, MT_NBLACK);
	evasive.armorClass = 50;
	ApplyMonsterRarity(evasive);
	EXPECT_EQ(evasive.armorClass, 50 * (100 + ActiveMapEffects()[MapDanger::MonsterEvasion]) / 100);
	EXPECT_EQ(MonsterArmourPercent(evasive), 100);
	closeMap();
}

TEST_F(Poe2MonsterArmourTest, ArmourWordTiers)
{
	EXPECT_TRUE(ArmourTierText(0).empty());
	EXPECT_EQ(ArmourTierText(25), "Light armour");
	EXPECT_EQ(ArmourTierText(30), "Light armour");
	EXPECT_EQ(ArmourTierText(50), "Armoured");
	EXPECT_EQ(ArmourTierText(75), "Heavy armour");
	EXPECT_EQ(ArmourTierText(100), "Heavy armour");
	EXPECT_TRUE(MonsterArmourText(PlaceMonster(0, MT_FIEND)).empty());
	EXPECT_EQ(MonsterArmourText(PlaceMonster(1, MT_WSKELSD)), "Armoured");
}

TEST_F(Poe2MonsterArmourTest, PhysicalHitsMeetArmourAndElementsDont)
{
	Player &player = *MyPlayer;
	Monster &knight = PlaceMonster(0, MT_NBLACK);
	Monster &plain = PlaceMonster(1, MT_NBLACK, false);
	const int64_t raw = 20 * 64;
	const int reduction = MonsterArmourReductionPercent(knight, raw);
	ASSERT_GT(reduction, 0);
	ASSERT_LT(reduction, MaxMonsterArmourReductionPercent);
	const int taken = MonsterDamageTakenPercent(knight, DamageType::Physical);
	EXPECT_EQ(MonsterHitDamage(player, knight, DamageType::Physical, raw), (raw - (raw * reduction / 100)) * taken / 100);
	EXPECT_EQ(MonsterHitDamage(player, plain, DamageType::Physical, raw), raw * taken / 100);
	// Fire, cold, lightning and chaos: only the damage taken.
	for (const DamageType type : { DamageType::Fire, DamageType::Cold, DamageType::Lightning, DamageType::Chaos })
		EXPECT_EQ(MonsterHitDamage(player, knight, type, raw), raw * MonsterDamageTakenPercent(knight, type) / 100);
	// Thorns and the hero's minions: never.
	EXPECT_EQ(MonsterHitDamage(player, knight, DamageType::Physical, raw, ArmourHit::Ignore), raw * taken / 100);
}

TEST_F(Poe2MonsterArmourTest, AttackPacketReduced)
{
	Player &player = *MyPlayer;
	Monster &knight = PlaceMonster(0, MT_NBLACK);
	Monster &plain = PlaceMonster(1, MT_NBLACK, false);
	const Fixed26_6 hit = Fixed26_6::fromInt(30);
	SetRndSeed(5);
	ApplyPlayerAttackDamage(player, knight, hit, false, false);
	const uint32_t afterArmoured = GetLCGEngineState();
	SetRndSeed(5);
	ApplyPlayerAttackDamage(player, plain, hit, false, false);
	// The same random numbers either way.
	EXPECT_EQ(GetLCGEngineState(), afterArmoured);
	ASSERT_GT(LifeLost(plain), 0);
	const int reduction = MonsterArmourReductionPercent(knight, LifeLost(plain));
	ASSERT_GT(reduction, 0);
	EXPECT_NEAR(LifeLost(knight), LifeLost(plain) * (100 - reduction) / 100, LifeLost(plain) / 50 + 2);
	EXPECT_LT(LifeLost(knight), LifeLost(plain));
}

TEST_F(Poe2MonsterArmourTest, SpiritHitsMeetArmourButThornsDont)
{
	Player &player = *MyPlayer;
	Monster &knight = PlaceMonster(0, MT_NBLACK);
	const Fixed26_6 hit = Fixed26_6::fromInt(10);
	const int taken = MonsterDamageTakenPercent(knight, DamageType::Physical);
	SpiritHitMonster(player, knight, DamageType::Physical, hit);
	const int reduction = MonsterArmourReductionPercent(knight, hit.raw());
	EXPECT_EQ(LifeLost(knight), (hit.raw() - (hit.raw() * reduction / 100)) * taken / 100);
	Monster &thorned = PlaceMonster(1, MT_NBLACK);
	SpiritHitMonster(player, thorned, DamageType::Physical, hit, 100, true);
	EXPECT_EQ(LifeLost(thorned), hit.raw() * taken / 100);
}

TEST_F(Poe2MonsterArmourTest, BleedTickNotReduced)
{
	Monster &knight = PlaceMonster(0, MT_NBLACK);
	ASSERT_GT(MonsterArmourRating(knight), 0);
	knight.poe2.bleedSource = 0;
	knight.poe2.bleedDamagePerTick = 64;
	knight.poe2.bleedTicks = 5;
	knight.poe2.poisonSource = 0;
	ProcessUniqueMonsterAilments(knight);
	EXPECT_EQ(LifeLost(knight), 64);
}

TEST_F(Poe2MonsterArmourTest, AilmentBasesArePreArmour)
{
	Player &player = *MyPlayer;
	const Fixed26_6 hit = Fixed26_6::fromInt(10);
	// Envenom III always poisons and Serrated Edge III always bleeds: a knight's ailments match a bare knight's.
	for (const SupportGem gem : { SupportGem::Envenom, SupportGem::SerratedEdge }) {
		const SkillSlot skill = Supported(SkillSlot::ForAttack(WeaponSkill::Cleave), gem, 3);
		ActiveMonsterCount = 0;
		Monster &knight = PlaceMonster(0, MT_NBLACK);
		Monster &plain = PlaceMonster(1, MT_NBLACK, false);
		for (Monster *monster : { &knight, &plain }) {
			const SupportHitScope scope(player, skill);
			SpiritHitMonster(player, *monster, DamageType::Physical, hit);
		}
		ASSERT_LT(LifeLost(knight), LifeLost(plain)) << "armour cut the hit";
		if (gem == SupportGem::Envenom) {
			ASSERT_GT(plain.poe2.poisonDamagePerTick, 0);
			EXPECT_NEAR(knight.poe2.poisonDamagePerTick, plain.poe2.poisonDamagePerTick, 1);
		} else {
			ASSERT_GT(plain.poe2.bleedDamagePerTick, 0);
			EXPECT_NEAR(knight.poe2.bleedDamagePerTick, plain.poe2.bleedDamagePerTick, 1);
		}
	}
}

TEST_F(Poe2MonsterArmourTest, InfusionUntouchedByArmour)
{
	Player &player = *MyPlayer;
	const Fixed26_6 hit = Fixed26_6::fromInt(10);
	const SkillSlot skill = Supported(SkillSlot::ForAttack(WeaponSkill::Cleave), SupportGem::InfusedFlames);
	Monster &knight = PlaceMonster(0, MT_NBLACK);
	Monster &plain = PlaceMonster(1, MT_NBLACK, false);
	int64_t physical[2] {};
	int index = 0;
	for (Monster *monster : { &knight, &plain }) {
		const SupportHitScope scope(player, skill);
		SpiritHitMonster(player, *monster, DamageType::Physical, hit);
		physical[index++] = LifeLost(*monster);
	}
	// The fire half is the physical hit before armour: the same on both.
	const int64_t fireKnight = LifeLost(knight) - physical[0];
	const int64_t firePlain = LifeLost(plain) - physical[1];
	ASSERT_GT(firePlain, 0);
	EXPECT_NEAR(fireKnight, firePlain, 2);
	EXPECT_LT(physical[0], physical[1]);
}

TEST_F(Poe2MonsterArmourTest, BrutalityThreeIgnoresArmourOnItsRoll)
{
	Player &player = *MyPlayer;
	const Fixed26_6 hit = Fixed26_6::fromInt(10);
	const auto run = [&](int tier, bool onTables, int &ignored) {
		const SkillSlot skill = Supported(SkillSlot::ForAttack(WeaponSkill::Cleave), SupportGem::Brutality, tier);
		ignored = 0;
		SetRndSeed(11);
		for (int i = 0; i < 400; i++) {
			ActiveMonsterCount = 0;
			Monster &knight = PlaceMonster(0, MT_NBLACK, onTables);
			{
				const SupportHitScope scope(player, skill);
				SpiritHitMonster(player, knight, DamageType::Physical, hit);
			}
			// An ignored hit lost nothing to armour and took the break's 25% more: 125%. One that met armour lost about a
			// sixth (Brutality's own more damage is the weapon's, not this hit's).
			if (onTables && LifeLost(knight) > hit.raw() * 110 / 100)
				ignored++;
		}
		return GetLCGEngineState();
	};
	int ignoredThree = 0;
	int ignoredOne = 0;
	int ignoredBare = 0;
	const uint32_t three = run(3, true, ignoredThree);
	const uint32_t one = run(1, true, ignoredOne);
	const uint32_t bare = run(3, false, ignoredBare);
	// One roll a packet on every tier, armour or not: the game's random numbers don't move.
	EXPECT_EQ(three, one);
	EXPECT_EQ(three, bare);
	EXPECT_EQ(ignoredOne, 0);
	// Brutality III: 20% of hits ignore armour.
	EXPECT_GT(ignoredThree, 400 * 12 / 100);
	EXPECT_LT(ignoredThree, 400 * 28 / 100);
}

TEST_F(Poe2MonsterArmourTest, CrackArmorBreaksOnPhysicalHits)
{
	Player &player = *MyPlayer;
	const SkillSlot skill = Supported(SkillSlot::ForAttack(WeaponSkill::Cleave), SupportGem::SunderArmour, 1);
	Monster &knight = PlaceMonster(0, MT_NBLACK);
	{
		const SupportHitScope scope(player, skill);
		SpiritHitMonster(player, knight, DamageType::Physical, Fixed26_6::fromInt(10));
	}
	EXPECT_EQ(knight.poe2.armourBreakTicks, 6 * 20);
	EXPECT_EQ(MonsterArmourRating(knight), 0);
}

TEST_F(Poe2MonsterArmourTest, DiabloModeUnchanged)
{
	Player &player = *MyPlayer;
	Monster &knight = PlaceMonster(0, MT_NBLACK);
	gbIsPoe2 = false;
	EXPECT_EQ(MonsterHitDamage(player, knight, DamageType::Physical, 1000), 1000);
	EXPECT_EQ(MonsterDamageTakenPercent(knight, DamageType::Physical), 100);
	gbIsPoe2 = true;
}

TEST_F(Poe2MonsterArmourTest, TelemetryShowsWhatArmourStopped)
{
	Player &player = *MyPlayer;
	Monster &knight = PlaceMonster(0, MT_NBLACK);
	TelemetryGameStarted(player, false);
	TelemetryLogLevelsText("damage", "deep");
	ASSERT_TRUE(TelemetryDeepDamage());
	TelemetrySkillUsed(player, "Cleave", 5, 3);
	TelemetryHitStart(false, "Cleave");
	SpiritHitMonster(player, knight, DamageType::Physical, Fixed26_6::fromInt(10));
	TelemetryLogLevelsText("damage", "normal");
	const std::string text = TelemetrySkillDamageText();
	EXPECT_NE(text.find("Cleave gem 3"), std::string::npos) << text;
	EXPECT_NE(text.find("% of its physical)"), std::string::npos) << text;
	EXPECT_NE(text.find("armour stopped"), std::string::npos) << text;
	EXPECT_NE(text.find("in all ("), std::string::npos) << text;
	const std::string events = ReadLatestEvents();
	EXPECT_NE(events.find("{\"step\":\"monster armour\",\"percent\":"), std::string::npos) << events;
	TelemetryGameEnded();
}

} // namespace
} // namespace devilution
