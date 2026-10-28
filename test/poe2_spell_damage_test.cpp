#include <algorithm>
#include <iterator>
#include <vector>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "lighting.h"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/gem_levels.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/spell_base.hpp"
#include "poe2/spell_damage.hpp"
#include "poe2/weapon_skills.hpp"
#include "spells.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

class Poe2SpellDamageTest : public ::testing::Test {
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
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		LoadItemData();
		LoadSpellData();
		LoadPlayerDataFiles();
		LoadMissileData();
		LoadSpellDamage();
		Missiles.clear();

		Player &player = *MyPlayer;
		player._pClass = HeroClass::Sorcerer;
		player.setCharacterLevel(10);
		player._pMagic = 80;
		player._pHitPoints = player._pMaxHP = Fixed26_6::fromInt(100);
		player.position.tile = { 40, 40 };
		player._pdir = Direction::South;
	}

	void TearDown() override
	{
		Missiles.clear();
		leveltype = DTYPE_TOWN;
		gbIsPoe2 = false;
		LoadSpellDamage();
		UnloadModArchives();
		LoadMissileData();
	}

	/** @brief A hero's spell missile, as a cast makes it, with the RNG seeded first. */
	static Missile &Cast(MissileID type, int spellLevel, uint32_t seed)
	{
		Missiles.clear();
		SetRndSeed(seed);
		Missile *missile = AddMissile({ 40, 40 }, { 45, 40 }, Direction::South, type, TARGET_MONSTERS, *MyPlayer, 0, spellLevel);
		EXPECT_NE(missile, nullptr);
		return *missile;
	}

	static int Percent(int value, int percent)
	{
		return value * percent / 100;
	}
};

TEST_F(Poe2SpellDamageTest, TheFileGivesThePlannedPercents)
{
	// Round 2 (Curtis 2026-10-27): spells re-timed against a 0.645 s swing instead of the old 0.40 s cast. Firebolt's
	// hit is re-fit so it kills as fast as before (161% x 140% for its speed = the old 100% x 225%), its splash cut to
	// 25%; the spells still over budget a second were trimmed; Apocalypse to its budget.
	EXPECT_EQ(SpellDamagePercent(SpellID::Firebolt), 161);
	EXPECT_EQ(SpellSplashPercent(SpellID::Firebolt), 25);
	EXPECT_EQ(SpellDamagePercent(SpellID::HolyBolt), 165);
	EXPECT_EQ(SpellDamagePercent(SpellID::Lightning), 135);
	EXPECT_EQ(SpellDamagePercent(SpellID::ChargedBolt), 50);
	EXPECT_EQ(SpellDamagePercent(SpellID::Flash), 110);
	EXPECT_EQ(SpellDamagePercent(SpellID::Fireball), 120);
	EXPECT_EQ(SpellDamagePercent(SpellID::ChainLightning), 400);
	EXPECT_EQ(SpellDamagePercent(SpellID::Elemental), 220);
	EXPECT_EQ(SpellDamagePercent(SpellID::Immolation), 390);
	EXPECT_EQ(SpellDamagePercent(SpellID::Nova), 480);
	EXPECT_EQ(SpellDamageHits(SpellID::Nova), 5);
	EXPECT_EQ(SpellDamagePercent(SpellID::Apocalypse), 130);
	EXPECT_EQ(SpellDamagePercent(SpellID::BloodStar), 1100);
	// Runes hit as their spell.
	EXPECT_EQ(SpellDamagePercent(SpellID::RuneOfFire), 120);
	EXPECT_EQ(SpellDamagePercent(SpellID::RuneOfNova), 480);
	// A spell with no row keeps the plain base.
	EXPECT_EQ(SpellDamagePercent(SpellID::Teleport), 100);
	EXPECT_EQ(SpellDamageHits(SpellID::Teleport), 1);
	EXPECT_EQ(SkillSpellDamagePercent(WeaponSkill::Spark), 35);
	// Raise each strike (Curtis 2026-10-27): an ember 100% -> 150%.
	EXPECT_EQ(SkillSpellDamagePercent(WeaponSkill::EmberFusillade), 150);
}

TEST_F(Poe2SpellDamageTest, FireboltIsItsPercentOfBase)
{
	const Player &player = *MyPlayer;
	for (uint32_t seed = 1; seed < 60; seed++) {
		const Missile &bolt = Cast(MissileID::Firebolt, 3, seed);
		SetRndSeed(seed);
		EXPECT_EQ(bolt._midam, Percent(RollSpellBase(player, 3), 161)) << seed;
		EXPECT_GE(bolt._midam, Percent(SpellBaseMin(player, 3), 161));
		EXPECT_LE(bolt._midam, Percent(SpellBaseMax(player, 3), 161));
	}
	const Missile &holy = Cast(MissileID::HolyBolt, 3, 7);
	SetRndSeed(7);
	EXPECT_EQ(holy._midam, Percent(RollSpellBase(player, 3), 165));
}

TEST_F(Poe2SpellDamageTest, NoTwelvePointFivePercentStacking)
{
	// The gem curve ends at 312% for every damaging spell, Fireball's steps; Diablo's 1.125^level is gone.
	const SkillSlot fireball = SkillSlot::ForSpell(SpellID::Fireball);
	EXPECT_EQ(GemCurveDamagePercent(fireball, 1), 100);
	EXPECT_EQ(GemCurveDamagePercent(fireball, 20), 312);
	for (const SpellID spell : { SpellID::Firebolt, SpellID::HolyBolt, SpellID::Lightning, SpellID::Flash, SpellID::Nova, SpellID::Elemental, SpellID::ChainLightning, SpellID::FlameWave, SpellID::Apocalypse, SpellID::BloodStar, SpellID::FireWall, SpellID::Guardian })
		EXPECT_EQ(GemCurveDamagePercent(SkillSlot::ForSpell(spell), 20), 312) << static_cast<int>(spell);
	// Lightning takes Fireball's damage steps too, not Spark's.
	EXPECT_EQ(GemCurveDamagePercent(SkillSlot::ForSpell(SpellID::Lightning), 10), GemCurveDamagePercent(fireball, 10));
	// Charged Bolt grows by its bolts only.
	EXPECT_EQ(GemCurveDamagePercent(SkillSlot::ForSpell(SpellID::ChargedBolt), 20), 100);
	// A cast hits for the curve times the spell's damage for its cast time: Fireball's 1.2 s over the 0.645 s spell
	// baseline, a Shortsword swing (skill_speed.tsv).
	EXPECT_EQ(GemDamagePercent(fireball, 1), 187);
	EXPECT_EQ(GemDamagePercent(fireball, 20), 312 * 187 / 100);

	// A level 20 Fireball's missile is the base times 120%, with no 1.125^20 on top.
	const Player &player = *MyPlayer;
	for (uint32_t seed = 1; seed < 30; seed++) {
		const Missile &ball = Cast(MissileID::Fireball, 20, seed);
		EXPECT_GE(ball._midam, Percent(SpellBaseMin(player, 20), 120));
		EXPECT_LE(ball._midam, Percent(SpellBaseMax(player, 20), 120));
	}
}

TEST_F(Poe2SpellDamageTest, CharacterLevelDoesNotChangeSpells)
{
	Player &player = *MyPlayer;
	constexpr SpellID Spells[] = { SpellID::Firebolt, SpellID::HolyBolt, SpellID::Lightning, SpellID::ChargedBolt, SpellID::Flash, SpellID::Fireball, SpellID::Elemental, SpellID::ChainLightning, SpellID::FlameWave, SpellID::Nova, SpellID::Immolation, SpellID::Apocalypse, SpellID::BloodStar, SpellID::FireWall, SpellID::LightningWall, SpellID::RingOfFire, SpellID::Inferno, SpellID::Guardian };
	constexpr MissileID Types[] = { MissileID::Firebolt, MissileID::HolyBolt, MissileID::Fireball, MissileID::Elemental, MissileID::Nova, MissileID::Apocalypse, MissileID::BloodStar, MissileID::FlashBottom, MissileID::FlashTop, MissileID::FlameWave, MissileID::ChargedBolt };
	std::vector<DamageRange> shown;
	std::vector<int> rolled;
	player.setCharacterLevel(50);
	for (const SpellID spell : Spells)
		shown.push_back(GetDamageAmt(spell, 5));
	for (const MissileID type : Types)
		rolled.push_back(Cast(type, 5, 11)._midam);

	player.setCharacterLevel(1);
	for (size_t i = 0; i < std::size(Spells); i++) {
		const DamageRange low = GetDamageAmt(Spells[i], 5);
		EXPECT_EQ(low.min, shown[i].min) << static_cast<int>(Spells[i]);
		EXPECT_EQ(low.max, shown[i].max) << static_cast<int>(Spells[i]);
	}
	for (size_t i = 0; i < std::size(Types); i++)
		EXPECT_EQ(Cast(Types[i], 5, 11)._midam, rolled[i]) << static_cast<int>(Types[i]);
}

TEST_F(Poe2SpellDamageTest, TooltipMatchesRoll)
{
	for (uint32_t seed = 1; seed < 60; seed++) {
		const DamageRange bolt = GetDamageAmt(SpellID::Firebolt, 4);
		const int firebolt = Cast(MissileID::Firebolt, 4, seed)._midam;
		EXPECT_GE(firebolt, bolt.min);
		EXPECT_LE(firebolt, bolt.max);

		// Nova's damage counts five of its balls, as Diablo's does.
		const DamageRange nova = GetDamageAmt(SpellID::Nova, 4);
		const int ball = Cast(MissileID::Nova, 4, seed)._midam;
		EXPECT_LE(ball * 5, nova.max);
		EXPECT_GT((ball + 1) * 5, nova.min);
	}
}

TEST_F(Poe2SpellDamageTest, ChainLightningBoltsCarryTheirHitAndCurve)
{
	Missile &chain = Cast(MissileID::ChainLightning, 4, 5);
	chain.poe2.damagePercent = 150;
	ProcessChainLightning(chain);
	int bolts = 0;
	for (const Missile &missile : Missiles) {
		if (missile._mitype != MissileID::LightningControl)
			continue;
		bolts++;
		EXPECT_GE(missile._midam, Percent(SpellBaseMin(*MyPlayer, 4), 400) / 2);
		EXPECT_LE(missile._midam, Percent(SpellBaseMax(*MyPlayer, 4), 400) / 2);
		EXPECT_EQ(missile.poe2.damagePercent, 150);
	}
	EXPECT_EQ(bolts, 1);
}

TEST_F(Poe2SpellDamageTest, ArcingSparksBesideAWallTurnAlongIt)
{
	// Play log 2026-10-28: cast beside a wall, most sparks went one tile into it and were gone.
	Player &player = *MyPlayer;
	leveltype = DTYPE_CATHEDRAL;
	SOLData[0] = TileProperties::None;
	SOLData[1] = TileProperties::Solid | TileProperties::BlockLight | TileProperties::BlockMissile;
	for (int x = 30; x < 60; x++) {
		for (int y = 30; y < 60; y++)
			dPiece[x][y] = 0;
	}
	// A wall along the hero's side: the spark's aim, east, is open; its turns to the north-east are not.
	const Point hero = player.position.tile;
	const Direction aim = GetDirection(hero, { 45, 40 });
	for (int step = -6; step <= 6; step++) {
		dPiece[(hero + Left(aim)).x + step][(hero + Left(aim)).y - step] = 1;
		dPiece[(hero + Left(aim) + Left(aim)).x + step][(hero + Left(aim) + Left(aim)).y - step] = 1;
	}
	ASSERT_TRUE(IsMissileBlockedByTile(hero + Left(aim)));
	ASSERT_FALSE(IsMissileBlockedByTile(hero + aim));

	Missiles.clear();
	SetRndSeed(7);
	ExecuteSkillCast(player, WeaponSkill::Spark, { 45, 40 });
	int sparks = 0;
	for (Missile &missile : Missiles) {
		if (missile._mitype != MissileID::ChargedBolt)
			continue;
		sparks++;
		for (int tick = 0; tick < 24 && !missile._miDelFlag; tick++)
			ProcessChargedBolt(missile);
		EXPECT_FALSE(missile._miDelFlag) << "spark " << sparks << " went into the wall at " << static_cast<int>(missile.position.tile.x) << "," << static_cast<int>(missile.position.tile.y);
	}
	EXPECT_GE(sparks, 6);
	for (int x = 30; x < 60; x++) {
		for (int y = 30; y < 60; y++)
			dPiece[x][y] = 0;
	}
	SOLData[1] = TileProperties::None;
}

TEST_F(Poe2SpellDamageTest, ARunesBurstIsCreditedToTheRune)
{
	// Play log 2026-10-28: a rune that went off after 3 s was credited to whatever was cast last.
	Player &player = *MyPlayer;
	leveltype = DTYPE_CATHEDRAL;
	Missiles.clear();
	Missile &rune = Missiles.emplace_back();
	rune._mitype = MissileID::RuneOfNova;
	rune._micaster = TARGET_MONSTERS;
	rune._misource = player.getId();
	rune._mispllvl = 1;
	rune._mlid = NO_LIGHT;
	rune.var1 = static_cast<int>(MissileID::Nova);
	const Point tile { 44, 44 };
	rune.position.tile = tile;
	// A monster steps on it.
	Monsters[0] = {};
	Monsters[0].position.tile = tile;
	dMonster[tile.x][tile.y] = 1;
	ProcessRune(rune);
	dMonster[tile.x][tile.y] = 0;
	ASSERT_TRUE(rune._miDelFlag);
	ASSERT_EQ(Missiles.size(), 2U);
	Missile &nova = Missiles.back();
	ASSERT_EQ(nova._mitype, MissileID::Nova);
	EXPECT_EQ(nova.poe2.creditSpell, SpellID::RuneOfNova);
	// The play log's credit only: the burst gets none of the rune's supports.
	EXPECT_EQ(nova.poe2.skillKind, 0);
	// The nova's bolts carry it on.
	ProcessNova(nova);
	int bolts = 0;
	for (const Missile &missile : Missiles) {
		if (missile._mitype != MissileID::NovaBall)
			continue;
		bolts++;
		EXPECT_EQ(missile.poe2.creditSpell, SpellID::RuneOfNova);
	}
	EXPECT_GT(bolts, 0);
	// A missile cast the usual way has none.
	const Missile *cast = AddMissile(player.position.tile, { 45, 40 }, Direction::South, MissileID::NovaBall, TARGET_MONSTERS, player, 0, 1);
	ASSERT_NE(cast, nullptr);
	EXPECT_EQ(cast->poe2.creditSpell, SpellID::Invalid);
}

TEST_F(Poe2SpellDamageTest, TheHerosOwnRunesNeverHurtTheHero)
{
	// Curtis 2026-10-28: his own Rune of Fire went off under him three times, down to 7 life ("from a trap").
	Player &player = *MyPlayer;
	leveltype = DTYPE_CATHEDRAL;
	const Point tile = player.position.tile;
	dPlayer[tile.x][tile.y] = static_cast<int8_t>(player.getId() + 1);
	Missiles.clear();
	// The hero walks over the rune: it waits for an enemy.
	Missile &rune = Missiles.emplace_back();
	rune._mitype = MissileID::RuneOfFire;
	rune._micaster = TARGET_MONSTERS;
	rune._misource = player.getId();
	rune._mispllvl = 1;
	rune._mlid = NO_LIGHT;
	rune.var1 = static_cast<int>(MissileID::BigExplosion);
	rune.position.tile = tile;
	ProcessRune(rune);
	EXPECT_FALSE(rune._miDelFlag);
	EXPECT_EQ(Missiles.size(), 1U);

	// A burst that goes off beside the hero (a monster set it off) spares them.
	for (int i = 0; i < 10; i++) {
		SetRndSeed(i);
		ASSERT_NE(AddMissile(tile, tile, Direction::South, MissileID::BigExplosion, TARGET_BOTH, player, 0, 1), nullptr);
	}
	EXPECT_EQ(player._pHitPoints, Fixed26_6::fromInt(100));
	dPlayer[tile.x][tile.y] = 0;
}

TEST_F(Poe2SpellDamageTest, SparkAndEmberRollTheirOwnRows)
{
	// Audit B6: the weapon skills that fire Charged Bolt and Firebolt missiles keep their own percent and curve.
	Player &player = *MyPlayer;
	leveltype = DTYPE_CATHEDRAL;
	Missiles.clear();
	ExecuteSkillCast(player, WeaponSkill::Spark, { 45, 40 });
	int sparks = 0;
	for (const Missile &missile : Missiles) {
		if (missile._mitype != MissileID::ChargedBolt)
			continue;
		sparks++;
		EXPECT_GE(missile._midam, std::max(Percent(SpellBaseMin(player, 1), 35), 1));
		EXPECT_LE(missile._midam, Percent(SpellBaseMax(player, 1), 35));
		EXPECT_EQ(missile.poe2.damagePercent, GemDamagePercent(SkillSlot::ForAttack(WeaponSkill::Spark), 1));
	}
	EXPECT_GE(sparks, 6);

	Missiles.clear();
	ExecuteSkillCast(player, WeaponSkill::EmberFusillade, { 45, 40 });
	int embers = 0;
	for (const Missile &missile : Missiles) {
		if (missile._mitype != MissileID::Firebolt)
			continue;
		embers++;
		EXPECT_GE(missile._midam, Percent(SpellBaseMin(player, 1), 150));
		EXPECT_LE(missile._midam, Percent(SpellBaseMax(player, 1), 150));
		EXPECT_EQ(missile.poe2.damagePercent, GemDamagePercent(SkillSlot::ForAttack(WeaponSkill::EmberFusillade), 1));
	}
	EXPECT_EQ(embers, 1);
	player.poe2.volleyShotsLeft = 0;
}

TEST_F(Poe2SpellDamageTest, IceBoltGainsShardsWithItsGem)
{
	// Ice Bolt fired one shard at every gem level: its curve had no projectiles, so the loop ran once (Curtis 2026-10-23).
	const SkillSlot iceBolt = SkillSlot::ForAttack(WeaponSkill::IceBolt);
	EXPECT_EQ(GemExtraProjectiles(iceBolt, 1), 0);
	EXPECT_EQ(GemExtraProjectiles(iceBolt, 2), 1);
	EXPECT_EQ(GemExtraProjectiles(iceBolt, 7), 2);
	EXPECT_EQ(GemExtraProjectiles(iceBolt, 20), 5);

	Player &player = *MyPlayer;
	leveltype = DTYPE_CATHEDRAL;
	const auto shards = [&player](int gemLevel) {
		SocketSkill(player, 0, SkillSlot::ForAttack(WeaponSkill::IceBolt), gemLevel);
		Missiles.clear();
		ExecuteSkillCast(player, WeaponSkill::IceBolt, { 45, 40 });
		return std::count_if(Missiles.begin(), Missiles.end(), [](const Missile &missile) { return missile._mitype == MissileID::BlueFlare; });
	};
	EXPECT_EQ(shards(1), 1);
	EXPECT_EQ(shards(7), 3);
	EXPECT_EQ(shards(20), 6);
}

TEST_F(Poe2SpellDamageTest, BoneSpiritTakesLittleOfABoss)
{
	// Curtis 2026-10-23: a third of current life broke bosses with 600%+ map life; uniques and Diablo lose 5%.
	const _monster_id previousType = LevelMonsterTypes[0].type;
	LevelMonsterTypes[0].type = MT_NZOMBIE;
	Monster monster {};
	monster.levelType = 0;
	monster.uniqueType = UniqueMonsterType::None;
	monster.hitPoints = Fixed26_6::fromInt(3000);
	EXPECT_EQ(BoneSpiritDamage(monster), 1000);
	monster.uniqueType = UniqueMonsterType::SkeletonKing;
	EXPECT_EQ(BoneSpiritDamage(monster), 150);
	EXPECT_EQ(BoneSpiritUniqueLifePercent, 5);
	monster.uniqueType = UniqueMonsterType::None;
	LevelMonsterTypes[0].type = MT_DIABLO;
	EXPECT_EQ(BoneSpiritDamage(monster), 150);

	// Diablo keeps a third for everyone.
	gbIsPoe2 = false;
	monster.uniqueType = UniqueMonsterType::SkeletonKing;
	EXPECT_EQ(BoneSpiritDamage(monster), 1000);
	gbIsPoe2 = true;
	LevelMonsterTypes[0].type = previousType;
}

TEST_F(Poe2SpellDamageTest, DiabloModeUnchanged)
{
	gbIsPoe2 = false;
	const Player &player = *MyPlayer;
	const DamageRange firebolt = GetDamageAmt(SpellID::Firebolt, 3);
	EXPECT_EQ(firebolt.min, (80 / 8) + 3 + 1);
	EXPECT_EQ(firebolt.max, (80 / 8) + 3 + 10);
	const DamageRange lightning = GetDamageAmt(SpellID::Lightning, 3);
	EXPECT_EQ(lightning.max, 2 + 10);
	for (uint32_t seed = 1; seed < 30; seed++) {
		const Missile &bolt = Cast(MissileID::Firebolt, 3, seed);
		SetRndSeed(seed);
		EXPECT_EQ(bolt._midam, GenerateRnd(10) + (player._pMagic / 8) + 3 + 1);

		const Missile &ball = Cast(MissileID::Fireball, 3, seed);
		SetRndSeed(seed);
		int expected = (2 * (10 + GenerateRnd(10) + GenerateRnd(10))) + 4;
		for (int i = 0; i < 3; i++)
			expected += expected / 8;
		EXPECT_EQ(ball._midam, expected);
	}
}

} // namespace
} // namespace devilution
