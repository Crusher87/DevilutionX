#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <string>
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
#include "poe2/support_gems.hpp"
#include "poe2/weapon_skills.hpp"
#include "spells.h"
#include "storm/storm_net.hpp"
#include "tables/monstdat.h"
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
	// Curtis 2026-10-28 (audit-casters-v3.md): Charged Bolt 50 -> 120 a bolt, as only about 1.4 of its bolts land.
	EXPECT_EQ(SpellDamagePercent(SpellID::ChargedBolt), 120);
	EXPECT_EQ(SpellDamagePercent(SpellID::Flash), 110);
	EXPECT_EQ(SpellDamagePercent(SpellID::Fireball), 120);
	EXPECT_EQ(SpellDamagePercent(SpellID::ChainLightning), 400);
	EXPECT_EQ(SpellDamagePercent(SpellID::Elemental), 220);
	EXPECT_EQ(SpellDamagePercent(SpellID::Immolation), 160);
	EXPECT_EQ(SpellDamagePercent(SpellID::Nova), 200);
	EXPECT_EQ(SpellDamageHits(SpellID::Nova), 5);
	EXPECT_EQ(SpellDamagePercent(SpellID::Apocalypse), 130);
	EXPECT_EQ(SpellDamagePercent(SpellID::BloodStar), 1100);
	// Runes hit as their spell, but Rune of Fire, Rune of Nova and Rune of Immolation have their own rows (Curtis
	// 2026-10-28: Rune of Fire got Fireball's 120%; Q26 cut Nova 480 -> 200, Immolation 390 -> 160 and gave their runes
	// 110% and 150%).
	EXPECT_EQ(SpellDamagePercent(SpellID::RuneOfFire), 280);
	EXPECT_EQ(SpellDamagePercent(SpellID::RuneOfNova), 110);
	EXPECT_EQ(SpellDamagePercent(SpellID::RuneOfImmolation), 150);
	// A spell with no row keeps the plain base.
	EXPECT_EQ(SpellDamagePercent(SpellID::Teleport), 100);
	EXPECT_EQ(SpellDamageHits(SpellID::Teleport), 1);
	// Arcing Sparks 35 -> 90 a spark (Curtis 2026-10-28), at 6 mana.
	EXPECT_EQ(SkillSpellDamagePercent(WeaponSkill::Spark), 90);
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
	// The credit gives no supports: this rune has none, so neither has its burst (one with them passes them on, see
	// RuneOfFiresBurstKeepsTheRunesPercentAndSupports).
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

TEST_F(Poe2SpellDamageTest, RuneOfFiresBurstKeepsTheRunesPercentAndSupports)
{
	// Curtis 2026-10-28: the burst was a new missile at 100%, losing the rune's speed factor, gem curve and supports.
	Player &player = *MyPlayer;
	leveltype = DTYPE_CATHEDRAL;
	LoadMonsterData();
	LevelMonsterTypes[0].type = MT_NZOMBIE;
	SNetInitializeProvider(SELCONN_LOOPBACK, nullptr);
	const Point tile { 44, 44 };
	const auto burst = [&](MissileID runeType, MissileID burstType, SpellID runeSpell) -> Missile & {
		Missiles.clear();
		Missile &rune = Missiles.emplace_back();
		rune._mitype = runeType;
		rune._micaster = TARGET_MONSTERS;
		rune._misource = player.getId();
		rune._mispllvl = 1;
		rune._mlid = NO_LIGHT;
		rune.var1 = static_cast<int>(burstType);
		rune.position.tile = tile;
		rune.poe2.damagePercent = 250;
		rune.poe2.spell = runeSpell;
		rune.poe2.skillKind = static_cast<uint8_t>(SkillKind::Spell);
		rune.poe2.skillId = static_cast<uint8_t>(runeSpell);
		// A monster steps on it.
		Monster &monster = Monsters[0];
		monster = {};
		monster.levelType = 0;
		monster.talkMsg = TEXT_NONE;
		monster.lightId = NO_LIGHT;
		monster.maxHitPoints = monster.hitPoints = Fixed26_6::fromInt(100000);
		monster.position.tile = monster.position.old = monster.position.future = tile;
		dMonster[tile.x][tile.y] = 1;
		ProcessRune(rune);
		dMonster[tile.x][tile.y] = 0;
		EXPECT_TRUE(rune._miDelFlag);
		EXPECT_EQ(Missiles.size(), 2U);
		return Missiles.back();
	};
	const Missile &fire = burst(MissileID::RuneOfFire, MissileID::BigExplosion, SpellID::RuneOfFire);
	ASSERT_EQ(fire._mitype, MissileID::BigExplosion);
	EXPECT_EQ(fire.poe2.damagePercent, 250);
	EXPECT_EQ(fire.poe2.spell, SpellID::RuneOfFire);
	EXPECT_EQ(fire.poe2.skillKind, static_cast<uint8_t>(SkillKind::Spell));
	EXPECT_EQ(fire.poe2.skillId, static_cast<uint8_t>(SpellID::RuneOfFire));
	EXPECT_EQ(fire.poe2.creditSpell, SpellID::RuneOfFire);
	// Rune of Nova's and Rune of Immolation's novas keep them too (they started afresh at 100%), and so do their balls.
	struct NovaRune {
		MissileID rune;
		MissileID nova;
		SpellID spell;
		MissileID ball;
	};
	for (const NovaRune &nr : { NovaRune { MissileID::RuneOfNova, MissileID::Nova, SpellID::RuneOfNova, MissileID::NovaBall },
	         NovaRune { MissileID::RuneOfImmolation, MissileID::Immolation, SpellID::RuneOfImmolation, MissileID::FireballBow } }) {
		Missile &nova = burst(nr.rune, nr.nova, nr.spell);
		ASSERT_EQ(nova._mitype, nr.nova);
		EXPECT_EQ(nova.poe2.damagePercent, 250);
		EXPECT_EQ(nova.poe2.spell, nr.spell);
		EXPECT_EQ(nova.poe2.skillKind, static_cast<uint8_t>(SkillKind::Spell));
		EXPECT_EQ(nova.poe2.skillId, static_cast<uint8_t>(nr.spell));
		EXPECT_EQ(nova.poe2.creditSpell, nr.spell);
		if (nr.nova == MissileID::Nova)
			ProcessNova(nova);
		else
			ProcessImmolation(nova);
		int balls = 0;
		for (const Missile &missile : Missiles) {
			if (missile._mitype != nr.ball)
				continue;
			balls++;
			EXPECT_EQ(missile.poe2.damagePercent, 250);
			EXPECT_EQ(missile.poe2.spell, nr.spell);
			EXPECT_EQ(missile.poe2.skillKind, static_cast<uint8_t>(SkillKind::Spell));
			EXPECT_EQ(missile.poe2.skillId, static_cast<uint8_t>(nr.spell));
			EXPECT_EQ(missile.poe2.creditSpell, nr.spell);
		}
		EXPECT_EQ(balls, 36);
	}
	// A missile added after it keeps nothing.
	const Missile *cast = AddMissile(player.position.tile, { 45, 40 }, Direction::South, MissileID::Firebolt, TARGET_MONSTERS, player, 0, 1);
	ASSERT_NE(cast, nullptr);
	EXPECT_EQ(cast->poe2.damagePercent, 100);
	Monsters[0] = {};
	Missiles.clear();
}

TEST_F(Poe2SpellDamageTest, NovaBallsHitForTheTooltipsPercent)
{
	// The balls of a Nova or an Immolation, cast or from a rune, started at 100%: the gem's speed factor, curve and
	// supports reached the tooltip but not the hits (round3-built.md, approved as a bug fix).
	Player &player = *MyPlayer;
	leveltype = DTYPE_CATHEDRAL;
	struct Case {
		SpellID spell;
		MissileID cast;
		MissileID ball;
		int gemLevel;
		int gemPercent;
		int minHit;
		int maxHit;
	};
	// The gem's percent is its curve (100% at gem 1, 312% at gem 20) times its damage_pct (Nova 156, Immolation 187,
	// runes 112). A ball's roll is a fifth of the spell base (Magic 80: 13-22 at gem 1, 32-41 at gem 20) times the row
	// (Nova 200%, Immolation 160%, Rune of Nova 110%, Rune of Immolation 150%), then the gem's percent.
	constexpr Case Cases[] = {
		{ SpellID::Nova, MissileID::Nova, MissileID::NovaBall, 1, 156, 7, 12 },
		{ SpellID::Nova, MissileID::Nova, MissileID::NovaBall, 20, 486, 58, 77 },
		{ SpellID::Immolation, MissileID::Immolation, MissileID::FireballBow, 1, 187, 7, 13 },
		{ SpellID::Immolation, MissileID::Immolation, MissileID::FireballBow, 20, 583, 58, 75 },
		{ SpellID::RuneOfNova, MissileID::Nova, MissileID::NovaBall, 1, 112, 2, 4 },
		{ SpellID::RuneOfNova, MissileID::Nova, MissileID::NovaBall, 20, 349, 24, 31 },
		{ SpellID::RuneOfImmolation, MissileID::Immolation, MissileID::FireballBow, 1, 112, 3, 6 },
		{ SpellID::RuneOfImmolation, MissileID::Immolation, MissileID::FireballBow, 20, 349, 31, 41 },
	};
	for (const Case &c : Cases) {
		const std::string label = "spell " + std::to_string(static_cast<int>(c.spell)) + " gem " + std::to_string(c.gemLevel);
		player.poe2.clear();
		SocketSkill(player, 0, SkillSlot::ForSpell(c.spell), c.gemLevel);
		EXPECT_EQ(GemDamagePercent(SkillSlot::ForSpell(c.spell), c.gemLevel), c.gemPercent) << label;
		// What the tooltip multiplies by (EstimateHit, DamageText): a bare hero's is the gem's.
		const int percent = SupportSpellDamagePercent(player, c.spell);
		EXPECT_EQ(percent, c.gemPercent) << label;
		const int row = SpellDamagePercent(c.spell);
		const auto ballRoll = [row](int base) { return std::max(base * row / 100 / 5, 1); };
		EXPECT_EQ(ballRoll(SpellBaseMin(player, c.gemLevel)) * c.gemPercent / 100, c.minHit) << label;
		EXPECT_EQ(ballRoll(SpellBaseMax(player, c.gemLevel)) * c.gemPercent / 100, c.maxHit) << label;
		for (uint32_t seed = 1; seed < 6; seed++) {
			// The nova, as CastSpell makes it for a cast from the bar, or as a rune's burst carries the rune's.
			Missile &nova = Cast(c.cast, c.gemLevel, seed);
			if (c.spell == SpellID::RuneOfNova || c.spell == SpellID::RuneOfImmolation) {
				Missile rune {};
				ApplySupportsToMissile(rune, player, c.spell);
				nova.poe2.damagePercent = rune.poe2.damagePercent;
				nova.poe2.skillKind = rune.poe2.skillKind;
				nova.poe2.skillId = rune.poe2.skillId;
			} else {
				ApplySupportsToMissile(nova, player, c.spell);
			}
			EXPECT_EQ(nova.poe2.damagePercent, percent) << label;
			const int roll = nova._midam;
			EXPECT_GE(roll, ballRoll(SpellBaseMin(player, c.gemLevel))) << label;
			EXPECT_LE(roll, ballRoll(SpellBaseMax(player, c.gemLevel))) << label;
			if (c.cast == MissileID::Nova)
				ProcessNova(nova);
			else
				ProcessImmolation(nova);
			int balls = 0;
			for (const Missile &missile : Missiles) {
				if (missile._mitype != c.ball)
					continue;
				balls++;
				EXPECT_EQ(missile.poe2.damagePercent, percent) << label;
				EXPECT_EQ(missile.poe2.skillKind, static_cast<uint8_t>(SkillKind::Spell)) << label;
				EXPECT_EQ(missile.poe2.skillId, static_cast<uint8_t>(c.spell)) << label;
				// A ball's hit on a monster, as CheckMissileCol scales it: the roll times the gem's percent (it was the roll).
				int minHit = missile._midam;
				int maxHit = missile._midam;
				ScaleSupportedMissileDamage(missile, minHit, maxHit);
				EXPECT_EQ(minHit, roll * percent / 100) << label;
				EXPECT_EQ(maxHit, roll * percent / 100) << label;
				EXPECT_GE(minHit, c.minHit) << label;
				EXPECT_LE(maxHit, c.maxHit) << label;
			}
			EXPECT_EQ(balls, 36) << label;
			// Five balls are what the tooltip counts: they come to at most the tooltip's top.
			const DamageRange shown = GetDamageAmt(c.spell, c.gemLevel);
			EXPECT_LE(roll * percent / 100 * 5, shown.max * percent / 100) << label;
		}
	}
}

TEST_F(Poe2SpellDamageTest, NovaBallsOfTrapsAndDiabloStayAtTheirRoll)
{
	leveltype = DTYPE_CATHEDRAL;
	// A trap's nova (a nova trap or shrine) has no caster's percent to pass on.
	Missiles.clear();
	SetRndSeed(4);
	Missile *trap = AddMissile({ 40, 40 }, { 40, 41 }, Direction::South, MissileID::Nova, TARGET_PLAYERS, -1, 0, 1);
	ASSERT_NE(trap, nullptr);
	trap->poe2.damagePercent = 250;
	ProcessNova(*trap);
	int balls = 0;
	for (const Missile &missile : Missiles) {
		if (missile._mitype != MissileID::NovaBall)
			continue;
		balls++;
		EXPECT_EQ(missile.poe2.damagePercent, 100);
		EXPECT_EQ(missile.poe2.skillKind, 0);
	}
	EXPECT_EQ(balls, 36);

	// Diablo and Hellfire: the balls start afresh, as before.
	gbIsPoe2 = false;
	for (const MissileID type : { MissileID::Nova, MissileID::Immolation }) {
		Missile &nova = Cast(type, 3, 4);
		nova.poe2.damagePercent = 250;
		nova.poe2.skillKind = static_cast<uint8_t>(SkillKind::Spell);
		nova.poe2.skillId = static_cast<uint8_t>(SpellID::Nova);
		if (type == MissileID::Nova)
			ProcessNova(nova);
		else
			ProcessImmolation(nova);
		const MissileID ballType = type == MissileID::Nova ? MissileID::NovaBall : MissileID::FireballBow;
		balls = 0;
		for (const Missile &missile : Missiles) {
			if (missile._mitype != ballType)
				continue;
			balls++;
			EXPECT_EQ(missile.poe2.damagePercent, 100);
			EXPECT_EQ(missile.poe2.skillKind, 0);
		}
		EXPECT_EQ(balls, 36);
	}
	gbIsPoe2 = true;
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
		EXPECT_GE(missile._midam, std::max(Percent(SpellBaseMin(player, 1), 90), 1));
		EXPECT_LE(missile._midam, Percent(SpellBaseMax(player, 1), 90));
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

	// Each shard hits for 160% of the spell base (Curtis 2026-10-28: 100 -> 160), then the gem curve.
	EXPECT_EQ(IceBoltShardDamagePercent, 160);
	shards(1);
	for (const Missile &missile : Missiles) {
		if (missile._mitype == MissileID::BlueFlare)
			EXPECT_EQ(missile.poe2.damagePercent, GemDamagePercent(SkillSlot::ForAttack(WeaponSkill::IceBolt), 1) * IceBoltShardDamagePercent / 100);
	}
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

TEST_F(Poe2SpellDamageTest, ElementalIsASlowHomingBlast)
{
	// Curtis 2026-10-28, "Elemental homes": it was a second Fireball.
	Player &player = *MyPlayer;
	leveltype = DTYPE_CATHEDRAL;
	SOLData[0] = TileProperties::None;
	for (int x = 30; x < 60; x++) {
		for (int y = 30; y < 60; y++)
			dPiece[x][y] = 0;
	}
	// A cast flies at half Diablo's speed, slower than Fireball.
	Missile &plain = Cast(MissileID::Elemental, 1, 1);
	const Displacement diablo = plain.position.velocity;
	SetSpellMissileSpeed(plain, SpellID::Elemental);
	EXPECT_EQ(plain.position.velocity.deltaX, diablo.deltaX * ElementalSpeedPercent / 100);
	EXPECT_EQ(plain.position.velocity.deltaY, diablo.deltaY * ElementalSpeedPercent / 100);
	EXPECT_LT(ElementalSpeedPercent, FireballSpeedPercent);

	// A monster off to the side of the aim.
	const Point side { 41, 45 };
	Monsters[0] = {};
	Monsters[0].position.tile = side;
	Monsters[0].maxHitPoints = Monsters[0].hitPoints = Fixed26_6::fromInt(100);
	Monsters[0].talkMsg = TEXT_NONE;
	dMonster[side.x][side.y] = 1;
	// Cast at it, the blast keeps it as its target.
	Missiles.clear();
	Missile *aimed = AddMissile(player.position.tile, side, Direction::South, MissileID::Elemental, TARGET_MONSTERS, player, 0, 1);
	ASSERT_NE(aimed, nullptr);
	SetSpellMissileSpeed(*aimed, SpellID::Elemental);
	EXPECT_EQ(aimed->var6, 1);
	const Displacement towards = aimed->position.velocity;

	// Cast past it, the blast turns towards it on its first tick.
	Missiles.clear();
	Missile *missile = AddMissile(player.position.tile, { 45, 40 }, Direction::South, MissileID::Elemental, TARGET_MONSTERS, player, 0, 1);
	ASSERT_NE(missile, nullptr);
	SetSpellMissileSpeed(*missile, SpellID::Elemental);
	EXPECT_EQ(missile->var6, 0);
	const Displacement straight = missile->position.velocity;
	ProcessElemental(*missile);
	EXPECT_EQ(missile->var6, 1);
	EXPECT_NE(missile->position.velocity, straight);
	EXPECT_NEAR(missile->position.velocity.deltaX, towards.deltaX, std::abs(towards.deltaX) / 50 + 2);
	EXPECT_NEAR(missile->position.velocity.deltaY, towards.deltaY, std::abs(towards.deltaY) / 50 + 2);

	// Diablo's Elemental flies to where it was cast first.
	gbIsPoe2 = false;
	Missiles.clear();
	missile = AddMissile(player.position.tile, { 45, 40 }, Direction::South, MissileID::Elemental, TARGET_MONSTERS, player, 0, 1);
	ASSERT_NE(missile, nullptr);
	const Displacement diabloStraight = missile->position.velocity;
	ProcessElemental(*missile);
	EXPECT_EQ(missile->position.velocity, diabloStraight);
	gbIsPoe2 = true;
	dMonster[side.x][side.y] = 0;
	Monsters[0] = {};
}

} // namespace
} // namespace devilution
