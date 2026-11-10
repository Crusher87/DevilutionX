/**
 * Bound demons (build spec step 10): rare monsters frozen in a blood-red crystal, holding essences. What they hold by
 * act, the powers their essences give, the crystal (nothing hurts them in it; a second after it breaks they fight),
 * their drops, the crystal kept by a level save and by the multiplayer delta, and how often and how alike they come on
 * real floors.
 */
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "cursor.h"
#include "drlg_test.hpp"
#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "levels/themes.h"
#include "levels/trigs.h"
#include "lighting.h"
#include "loadsave.h"
#include "menu.h"
#include "missiles.h"
#include "monster.h"
#include "msg.h"
#include "objects.h"
#include "options.h"
#include "pfile.h"
#include "player.h"
#include "poe2/bound_demons.hpp"
#include "poe2/cathedral_floors.hpp"
#include "poe2/crafting_items.hpp"
#include "poe2/floors.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/monster_resistance.hpp"
#include "poe2/passive_tree.hpp"
#include "storm/storm_net.hpp"
#include "tables/monstdat.h"
#include "tables/objdat.h"
#include "tables/playerdat.hpp"
#include "utils/file_util.h"
#include "utils/paths.h"

namespace devilution {
namespace {

EssenceKind KindOf(uint8_t variant)
{
	return EssenceVariantKind(variant);
}

class Poe2BoundDemonsTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayerId = 0;
		MyPlayer = &Players[0];
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
		LoadMissileData();
		LoadMonsterData();
		LoadModifierData();
		LevelMonsterTypes[0].type = MT_NZOMBIE;
		LevelMonsterTypeCount = 1;
		Missiles.clear();
		setlevel = false;
		SetFloor(2, DTYPE_CATHEDRAL);
		MyPlayer->setCharacterLevel(20);
		MyPlayer->position.tile = MyPlayer->position.future = Hero;
		ClearTiles();
		ActiveMonsterCount = 0;
		ActiveItemCount = 0;
		for (int i = 0; i < MAXITEMS; i++)
			ActiveItems[i] = i;
		SNetInitializeProvider(SELCONN_LOOPBACK, nullptr);
	}

	void TearDown() override
	{
		ClearTiles();
		ActiveMonsterCount = 0;
		ActiveItemCount = 0;
		Missiles.clear();
		gbIsMultiplayer = false;
		gbIsPoe2 = false;
		leveltype = DTYPE_TOWN;
		currlevel = 0;
		LoadModifierData();
		UnloadModArchives();
	}

	static constexpr Point Hero { 30, 30 };
	static constexpr Point DemonTile { 36, 36 };

	static void SetFloor(int floor, dungeon_type type)
	{
		currlevel = static_cast<uint8_t>(floor);
		leveltype = type;
		MyPlayer->setLevel(static_cast<uint8_t>(floor));
	}

	static void ClearTiles()
	{
		memset(dMonster, 0, sizeof(dMonster));
		memset(dPlayer, 0, sizeof(dPlayer));
		memset(dItem, 0, sizeof(dItem));
	}

	/** A plain monster on the tile, on the monster tables, made from the seed. */
	static Monster &PlaceMonster(size_t slot, Point tile, uint32_t seed)
	{
		Monster &monster = Monsters[slot];
		monster = {};
		monster.levelType = 0;
		monster.uniqueType = UniqueMonsterType::None;
		monster.talkMsg = TEXT_NONE;
		monster.lightId = NO_LIGHT;
		monster.goal = MonsterGoal::Normal;
		monster.mode = MonsterMode::Stand;
		monster.flags = MFLAG_POE2_TABLES;
		monster.maxHitPoints = monster.hitPoints = Fixed26_6::fromInt(1000);
		monster.position.tile = monster.position.old = monster.position.future = tile;
		monster.rndItemSeed = seed;
		dMonster[tile.x][tile.y] = static_cast<int16_t>(slot + 1);
		ActiveMonsters[ActiveMonsterCount] = static_cast<unsigned>(slot);
		ActiveMonsterCount++;
		return monster;
	}

	/** The item seed DemonHolding started from, before the demon was made rare. */
	static inline uint32_t OriginalSeed = 0;

	/** A monster from the seed made rare the way BindDemon makes it, without the crystal. */
	static Monster &RareTwin(size_t slot, Point tile, uint32_t seed)
	{
		Monster &monster = PlaceMonster(slot, tile, seed);
		if (GetMonsterAffixes(monster).rarity == MonsterRarity::Magic)
			DemoteMonsterRarity(monster);
		PromoteMonsterRarity(monster, MonsterRarity::Rare);
		return monster;
	}

	/** A bound demon whose only essence is of the kind (Act 1 demons hold one), found by trying item seeds. */
	static Monster &DemonHolding(EssenceKind kind, size_t slot = 0)
	{
		for (uint32_t seed = 1; seed < 20000; seed++) {
			ActiveMonsterCount = 0;
			dMonster[DemonTile.x][DemonTile.y] = 0;
			Monster &monster = PlaceMonster(slot, DemonTile, seed);
			if (!BindDemon(monster))
				continue;
			const std::vector<uint8_t> held = BoundDemonEssences(monster);
			if (held.size() == 1 && KindOf(held[0]) == kind) {
				OriginalSeed = seed;
				return monster;
			}
		}
		ADD_FAILURE() << "no demon holds " << static_cast<int>(kind);
		return Monsters[slot];
	}
};

TEST_F(Poe2BoundDemonsTest, InAct1ADemonHoldsOneLesserEssence)
{
	std::set<EssenceKind> kinds;
	for (uint32_t seed = 1; seed <= 600; seed++) {
		const std::vector<uint8_t> held = EssencesForSeed(seed, 1, 7);
		ASSERT_EQ(held.size(), 1U) << seed;
		EXPECT_EQ(EssenceVariantTier(held[0]), EssenceTier::Lesser) << seed;
		EXPECT_FALSE(IsEvilEssence(KindOf(held[0]))) << seed;
		EXPECT_TRUE(GetEssence(KindOf(held[0])).drops) << seed;
		kinds.insert(KindOf(held[0]));
	}
	EXPECT_GE(kinds.size(), 10U) << "the essences vary";
}

TEST_F(Poe2BoundDemonsTest, LaterDemonsHoldOneOrTwoDifferentEssences)
{
	int ones = 0;
	int twos = 0;
	for (uint32_t seed = 1; seed <= 600; seed++) {
		const std::vector<uint8_t> held = EssencesForSeed(seed, 2, 50);
		ASSERT_GE(held.size(), 1U) << seed;
		ASSERT_LE(held.size(), 2U) << seed;
		(held.size() == 1 ? ones : twos)++;
		for (const uint8_t variant : held)
			EXPECT_FALSE(IsEvilEssence(KindOf(variant))) << "no Evil essence before Act 3, seed " << seed;
		if (held.size() == 2)
			EXPECT_NE(KindOf(held[0]), KindOf(held[1])) << seed;
	}
	EXPECT_GT(ones, 150);
	EXPECT_GT(twos, 150);
	// The same seed always gives the same essences.
	EXPECT_EQ(EssencesForSeed(4242, 2, 50), EssencesForSeed(4242, 2, 50));
}

TEST_F(Poe2BoundDemonsTest, FromAct3OneDemonInSixHoldsAnEvilEssence)
{
	constexpr int Seeds = 3000;
	int evil = 0;
	for (uint32_t seed = 1; seed <= Seeds; seed++) {
		const std::vector<uint8_t> held = EssencesForSeed(seed, 3, 28);
		ASSERT_FALSE(held.empty());
		const int evils = static_cast<int>(std::count_if(held.begin(), held.end(), [](uint8_t variant) { return IsEvilEssence(KindOf(variant)); }));
		EXPECT_LE(evils, 1) << seed;
		evil += evils;
	}
	EXPECT_GT(evil, Seeds / 9);
	EXPECT_LT(evil, Seeds / 4);
}

TEST_F(Poe2BoundDemonsTest, EveryEssenceGivesAPower)
{
	for (int kind = static_cast<int>(EssenceKind::Body); kind < static_cast<int>(EssenceKind::Count); kind++) {
		const BoundDemonPower &power = GetBoundDemonPower(static_cast<uint8_t>(kind));
		EXPECT_NE(power.power, BoundPower::None) << kind;
		EXPECT_GT(power.value, 0) << kind;
	}
	EXPECT_EQ(GetBoundDemonPower(static_cast<uint8_t>(EssenceKind::Vigor)).power, BoundPower::None) << "legacy kinds never drop";
	EXPECT_EQ(GetBoundDemonPower(static_cast<uint8_t>(EssenceKind::Flames)).power, BoundPower::FireHit);
	EXPECT_EQ(GetBoundDemonPower(static_cast<uint8_t>(EssenceKind::Hatred)).power, BoundPower::Evil);
}

TEST_F(Poe2BoundDemonsTest, NothingHurtsItUntilItsCrystalBreaksThenItWakesInASecond)
{
	Monster &demon = DemonHolding(EssenceKind::Body);
	ASSERT_TRUE(IsBoundDemon(demon));
	EXPECT_EQ(GetMonsterAffixes(demon).rarity, MonsterRarity::Rare);
	EXPECT_FALSE(demon.isPossibleToHit());
	EXPECT_TRUE(CanTalkToMonst(demon)) << "clicking it walks up to it, as to someone to talk to";
	EXPECT_EQ(BoundDemonEssenceText(demon), "Lesser Essence of the Body");

	const Fixed26_6 life = demon.hitPoints;
	ApplyMonsterDamage(DamageType::Physical, demon, Fixed26_6::fromInt(50));
	EXPECT_EQ(demon.hitPoints, life);
	for (int tick = 0; tick < 100; tick++)
		EXPECT_TRUE(ProcessBoundDemon(demon));
	EXPECT_EQ(demon.activeForTicks, 0) << "it never wakes on its own";

	BreakBoundCrystal(demon);
	EXPECT_FALSE(IsBoundDemon(demon));
	EXPECT_TRUE(IsEssenceDemon(demon));
	EXPECT_TRUE(demon.isPossibleToHit());
	EXPECT_FALSE(CanTalkToMonst(demon));
	for (int tick = 1; tick < BoundDemonWakeTicks; tick++)
		EXPECT_TRUE(ProcessBoundDemon(demon)) << tick;
	EXPECT_EQ(demon.activeForTicks, 0);
	EXPECT_FALSE(ProcessBoundDemon(demon)) << "a second after the crack it fights";
	EXPECT_EQ(demon.activeForTicks, UINT8_MAX);
	EXPECT_FALSE(ProcessBoundDemon(demon));
	// Breaking it again does nothing.
	BreakBoundCrystal(demon);
	EXPECT_EQ(demon.poe2.boundWakeTicks, 0);

	ApplyMonsterDamage(DamageType::Physical, demon, Fixed26_6::fromInt(50));
	EXPECT_LT(demon.hitPoints, life);
}

TEST_F(Poe2BoundDemonsTest, KilledDuringItsWakeItDiesAtOnce)
{
	for (const bool deathMode : { true, false }) {
		Monster &demon = DemonHolding(EssenceKind::Body);
		ASSERT_TRUE(IsBoundDemon(demon));
		BreakBoundCrystal(demon);
		ASSERT_EQ(demon.poe2.boundWakeTicks, BoundDemonWakeTicks);
		EXPECT_TRUE(ProcessBoundDemon(demon));
		EXPECT_TRUE(ProcessBoundDemon(demon));
		// Killed before it is free (its life gone, and its death started or about to be).
		demon.hitPoints = Fixed26_6::fromInt(0);
		if (deathMode)
			demon.mode = MonsterMode::Death;
		EXPECT_FALSE(ProcessBoundDemon(demon)) << deathMode;
		EXPECT_EQ(demon.poe2.boundWakeTicks, 0) << deathMode;
		EXPECT_EQ(demon.activeForTicks, 0) << "a corpse doesn't roar and come for the hero";
		EXPECT_FALSE(ProcessBoundDemon(demon));
	}
}

TEST_F(Poe2BoundDemonsTest, ItsEssencesGiveItTheirPowers)
{
	// The Body: 40% more life than the same rare monster out of a crystal.
	{
		Monster &demon = DemonHolding(EssenceKind::Body);
		const Fixed26_6 life = demon.maxHitPoints;
		Monster &plain = RareTwin(1, { 40, 40 }, OriginalSeed);
		EXPECT_EQ(plain.rndItemSeed, demon.rndItemSeed) << "the same seed, the same rare";
		EXPECT_EQ(life.raw(), static_cast<int64_t>(plain.maxHitPoints.raw()) * 140 / 100);
		EXPECT_EQ(demon.hitPoints, demon.maxHitPoints);
	}
	{
		Monster &demon = DemonHolding(EssenceKind::Abrasion);
		EXPECT_EQ(BoundDemonDamagePercent(demon), 25);
		Monster &plain = PlaceMonster(1, { 40, 40 }, demon.rndItemSeed);
		EXPECT_EQ(MonsterModsDamagePercent(demon), MonsterModsDamagePercent(plain) * 125 / 100);
	}
	{
		Monster &demon = DemonHolding(EssenceKind::Flames);
		Player &player = *MyPlayer;
		player._pFireResist = 0;
		EXPECT_EQ(BoundDemonExtraHitDamage(demon, player, Fixed26_6::fromInt(100)), Fixed26_6::fromInt(30));
		player._pFireResist = 50;
		EXPECT_EQ(BoundDemonExtraHitDamage(demon, player, Fixed26_6::fromInt(100)), Fixed26_6::fromInt(15));
		player._pFireResist = 90;
		EXPECT_EQ(BoundDemonExtraHitDamage(demon, player, Fixed26_6::fromInt(100)).raw(), Fixed26_6::fromInt(30).raw() / 4) << "resistance caps at 75%";
	}
	{
		Monster &demon = DemonHolding(EssenceKind::Insulation);
		Monster &plain = PlaceMonster(1, { 40, 40 }, demon.rndItemSeed);
		EXPECT_EQ(MonsterBaseResistance(demon, DamageType::Fire), MonsterBaseResistance(plain, DamageType::Fire) + 50);
		EXPECT_EQ(MonsterBaseResistance(demon, DamageType::Cold), MonsterBaseResistance(plain, DamageType::Cold));
	}
	{
		Monster &demon = DemonHolding(EssenceKind::Enhancement);
		EXPECT_EQ(BoundDemonArmourPercent(demon), 50);
	}
	{
		// Haste: an extra turn one tick in five, once it is out of its crystal and awake.
		Monster &demon = DemonHolding(EssenceKind::Haste);
		EXPECT_FALSE(BoundDemonExtraTick(demon));
		BreakBoundCrystal(demon);
		while (ProcessBoundDemon(demon)) { }
		int extra = 0;
		for (int tick = 0; tick < 100; tick++)
			extra += BoundDemonExtraTick(demon) ? 1 : 0;
		EXPECT_EQ(extra, 20);
	}
	// A plain monster has none of it.
	Monster &plain = PlaceMonster(2, { 44, 44 }, 7);
	EXPECT_EQ(BoundDemonDamagePercent(plain), 0);
	EXPECT_TRUE(BoundDemonEssences(plain).empty());
	EXPECT_EQ(BoundDemonExtraHitDamage(plain, *MyPlayer, Fixed26_6::fromInt(100)), Fixed26_6::fromInt(0));
	EXPECT_FALSE(BoundDemonExtraTick(plain));
}

TEST_F(Poe2BoundDemonsTest, ItDropsTheEssencesItHeld)
{
	// Caves 2: Act 2, so one or two.
	SetFloor(10, DTYPE_CAVES);
	std::set<size_t> counts;
	for (uint32_t seed = 1; seed <= 40; seed++) {
		ActiveMonsterCount = 0;
		ActiveItemCount = 0;
		memset(dItem, 0, sizeof(dItem));
		dMonster[DemonTile.x][DemonTile.y] = 0;
		Monster &demon = PlaceMonster(0, DemonTile, seed);
		ASSERT_TRUE(BindDemon(demon));
		const std::vector<uint8_t> held = BoundDemonEssences(demon);
		SetRndSeed(seed);
		DropBoundDemonEssences(demon, false);
		ASSERT_EQ(static_cast<size_t>(ActiveItemCount), held.size()) << seed;
		counts.insert(held.size());
		for (int i = 0; i < ActiveItemCount; i++) {
			const Item &item = Items[ActiveItems[i]];
			EXPECT_EQ(item._iMiscId, IMISC_ESSENCE);
			EXPECT_EQ(CraftingItemVariant(item), held[static_cast<size_t>(i)]);
			EXPECT_LE(DemonTile.ManhattanDistance(item.position), 4);
		}
	}
	EXPECT_EQ(counts, (std::set<size_t> { 1, 2 }));
	// A plain monster drops none.
	ActiveItemCount = 0;
	Monster &plain = PlaceMonster(1, { 44, 44 }, 7);
	DropBoundDemonEssences(plain, false);
	EXPECT_EQ(ActiveItemCount, 0);
}

TEST_F(Poe2BoundDemonsTest, ABreakReachesTheDeltaForPlayersWhoComeLater)
{
	gbIsMultiplayer = true;
	delta_init();
	MyPlayer->plractive = true;
	Monster &demon = DemonHolding(EssenceKind::Body);
	const Fixed26_6 life = demon.hitPoints;
	TCmdBreakCrystal cmd {};
	cmd.bCmd = CMD_POE2_BREAKCRYSTAL;
	cmd.x = static_cast<uint8_t>(DemonTile.x);
	cmd.y = static_cast<uint8_t>(DemonTile.y);
	cmd.monsterId = 0;
	cmd.hitPoints = demon.hitPoints.raw();
	EXPECT_EQ(ParseCmd(0, reinterpret_cast<const TCmd *>(&cmd), sizeof(cmd)), sizeof(cmd));
	EXPECT_FALSE(IsBoundDemon(demon)) << "the hero on its level sees it crack";
	EXPECT_EQ(demon.poe2.boundWakeTicks, BoundDemonWakeTicks);

	// The level is made again from its seed when someone comes to it: the demon in its crystal, then the delta.
	demon.flags |= MFLAG_POE2_BOUND;
	demon.poe2 = {};
	DeltaLoadLevel();
	EXPECT_FALSE(IsBoundDemon(demon));
	EXPECT_TRUE(IsEssenceDemon(demon));
	EXPECT_EQ(demon.poe2.boundWakeTicks, 0) << "it is already awake";
	EXPECT_EQ(demon.hitPoints, life);
	EXPECT_EQ(demon.position.tile, DemonTile);
	delta_init();
}

TEST_F(Poe2BoundDemonsTest, AnUnbrokenCrystalIsKeptByALevelSave)
{
	const std::string previousPrefPath = paths::PrefPath();
	const std::string folder = ::testing::TempDir() + "poe2_bound_demons_save" + DIRECTORY_SEPARATOR_STR;
	CreateDir(folder.c_str());
	for (const std::string &file : ListFiles(folder.c_str()))
		RemoveFile((folder + file).c_str());
	paths::SetPrefPath(folder);
	gSaveNumber = 0;
	gbIsHellfireSaveGame = true;

	Monster &demon = DemonHolding(EssenceKind::Flames);
	const unsigned id = static_cast<unsigned>(demon.getId());
	const uint32_t seed = demon.rndItemSeed;
	const Fixed26_6 life = demon.maxHitPoints;
	const std::vector<uint8_t> held = BoundDemonEssences(demon);
	// And one whose crystal broke, which stays out of it.
	Monster &freed = DemonHolding(EssenceKind::Body, 1);
	dMonster[DemonTile.x][DemonTile.y] = 0;
	freed.position.tile = freed.position.old = freed.position.future = { 40, 40 };
	dMonster[40][40] = 2;
	BreakBoundCrystal(freed);
	ActiveMonsters[0] = id;
	ActiveMonsters[1] = 1;
	ActiveMonsterCount = 2;
	dMonster[DemonTile.x][DemonTile.y] = static_cast<int16_t>(id + 1);

	MyPlayer->_pName[0] = 'B';
	MyPlayer->_pName[1] = '\0';
	pfile_write_hero(/*writeGameData=*/false);
	pfile_save_level();

	Monsters[0] = {};
	Monsters[1] = {};
	ActiveMonsterCount = 0;
	ASSERT_TRUE(LoadLevel().has_value());
	ASSERT_EQ(ActiveMonsterCount, 2U);
	const Monster &loaded = Monsters[id];
	EXPECT_TRUE(IsBoundDemon(loaded));
	EXPECT_TRUE(IsEssenceDemon(loaded));
	EXPECT_EQ(loaded.rndItemSeed, seed);
	EXPECT_EQ(loaded.maxHitPoints, life);
	EXPECT_EQ(loaded.position.tile, DemonTile);
	EXPECT_EQ(BoundDemonEssences(loaded), held);
	EXPECT_FALSE(loaded.isPossibleToHit());
	EXPECT_FALSE(IsBoundDemon(Monsters[1]));
	EXPECT_TRUE(IsEssenceDemon(Monsters[1]));

	gbIsHellfireSaveGame = false;
	paths::SetPrefPath(previousPrefPath);
}

/** Real Cathedral floors, made the way a first visit makes them: how often a bound demon comes, and that a floor's seed makes the same one. */
class Poe2BoundDemonFloorsTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		paths::SetPrefPath(paths::BasePath() + "test/fixtures/");
		TestInitGame(true, false, true);
		LoadGameArchives();
		if (!HaveMainData())
			GTEST_SKIP() << "MPQ assets (spawn.mpq or DIABDAT.MPQ) not found - skipping";
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		gbIsHellfire = true;
		gbIsPoe2 = true;
		gbIsMultiplayer = false;
		sgGameInitInfo.nDifficulty = DIFF_NORMAL;
		LoadItemData();
		LoadSpellData();
		LoadMissileData();
		LoadMonsterData();
		LoadObjectData();
		LoadModifierData();
		InitQuests();
		SetCathedralRooms({});
		Poe2FloorsReset();
		GetOptions().Gameplay.newCathedralFloors.SetValue(true);
		const std::expected<void, std::string> sol = LoadLevelSOLData();
		ASSERT_TRUE(sol.has_value()) << sol.error();
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		gbIsHellfire = false;
		SetCathedralFunnelFloor(false);
		pMegaTiles = nullptr;
		ActiveMonsterCount = 0;
		LoadModifierData();
	}

	struct Demon {
		Point tile;
		_monster_id type;
		uint32_t seed;
		std::vector<uint8_t> held;
		uint32_t others;
	};

	/** Makes Cathedral floor 2 from the seed, up to its monsters; returns the random state its monsters start from. */
	static uint32_t MakeFloor(uint32_t levelSeed)
	{
		currlevel = 2;
		leveltype = DTYPE_CATHEDRAL;
		MyPlayer->setLevel(2);
		LevelSeeds[2] = std::nullopt;
		Poe2ApplyFreshFloor(2, false, levelSeed, 1);
		CreateDungeon(levelSeed, ENTRY_MAIN);
		InitL1Triggers();
		memset(dMonster, 0, sizeof(dMonster));
		memset(dPlayer, 0, sizeof(dPlayer));
		memset(dObject, 0, sizeof(dObject));
		InitLevelMonsters();
		SetRndSeed(levelSeed);
		EXPECT_TRUE(GetLevelMTypes().has_value());
		InitThemes();
		HoldThemeRooms();
		InitObjects();
		return GetLCGEngineState();
	}

	/** Places the floor's monsters from the random state, as InitMonsters does on a first visit, and finds its bound demon. */
	static std::optional<Demon> PlaceMonsters(uint32_t state)
	{
		for (Monster &monster : Monsters)
			monster = {};
		ActiveMonsterCount = 0;
		std::iota(std::begin(ActiveMonsters), std::end(ActiveMonsters), 0U);
		memset(dMonster, 0, sizeof(dMonster));
		SetRndSeed(state);
		EXPECT_TRUE(InitMonsters().has_value());
		// Where the floor's other monsters stand, which decides where the demon can go.
		uint32_t others = 0;
		for (size_t i = 0; i < ActiveMonsterCount; i++) {
			const Monster &monster = Monsters[ActiveMonsters[i]];
			if (!IsEssenceDemon(monster))
				others = (others * 31U) + static_cast<uint32_t>((monster.position.tile.x * 256) + monster.position.tile.y);
		}
		std::optional<Demon> found;
		int demons = 0;
		for (size_t i = 0; i < ActiveMonsterCount; i++) {
			const Monster &monster = Monsters[ActiveMonsters[i]];
			if (!IsEssenceDemon(monster))
				continue;
			demons++;
			EXPECT_TRUE(IsBoundDemon(monster));
			EXPECT_EQ(GetMonsterAffixes(monster).rarity, MonsterRarity::Rare);
			EXPECT_FALSE(IsTileVisible(monster.position.tile));
			found = Demon { monster.position.tile, monster.type().type, monster.rndItemSeed, BoundDemonEssences(monster), others };
			// Fought on its own: no other monster within a few steps.
			for (size_t j = 0; j < ActiveMonsterCount; j++) {
				const Monster &other = Monsters[ActiveMonsters[j]];
				if (&other != &monster && !other.isPlayerMinion() && other.position.tile != GolemHoldingCell)
					EXPECT_GE(other.position.tile.WalkingDistance(monster.position.tile), 3);
			}
		}
		EXPECT_LE(demons, 1);
		return found;
	}
};

TEST_F(Poe2BoundDemonFloorsTest, AboutEverySecondFloorHasOneAndAFloorsSeedMakesTheSameOne)
{
	pMegaTiles = LoadFileInMem<MegaTile>("levels\\l1data\\l1.til");
	ASSERT_NE(pMegaTiles, nullptr);
	constexpr int Floors = 40;
	int withDemon = 0;
	int remade = 0;
	for (uint32_t seed = 1; seed <= Floors; seed++) {
		const uint32_t levelSeed = (seed * 104729U) + 2U;
		const uint32_t state = MakeFloor(levelSeed);
		const std::optional<Demon> demon = PlaceMonsters(state);
		if (!demon)
			continue;
		withDemon++;
		EXPECT_EQ(demon->held.size(), 1U) << "Act 1: one Lesser essence";
		EXPECT_EQ(EssenceVariantTier(demon->held[0]), EssenceTier::Lesser);
		// The floor's monsters made again from the same numbers have the same demon holding the same essence, and in the
		// same place when the floor's other monsters came out the same (this test's floors don't always: some state
		// outside the monsters carries over from the first making).
		if (remade < 4) {
			remade++;
			const std::optional<Demon> again = PlaceMonsters(state);
			ASSERT_TRUE(again.has_value()) << levelSeed;
			if (again->others == demon->others)
				EXPECT_EQ(again->tile, demon->tile) << levelSeed;
			EXPECT_EQ(again->type, demon->type) << levelSeed;
			EXPECT_EQ(again->seed, demon->seed) << levelSeed;
			EXPECT_EQ(again->held, demon->held) << levelSeed;
		}
	}
	EXPECT_GE(withDemon, Floors / 4);
	EXPECT_LE(withDemon, Floors * 3 / 4);
}

} // namespace
} // namespace devilution
