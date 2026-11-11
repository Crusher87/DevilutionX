#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/mix.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/monster_resistance.hpp"
#include "poe2/unique_effects.hpp"
#include "tables/misdat.h"
#include "tables/monstdat.h"

namespace devilution {
namespace {

class Poe2MonsterModsTest : public ::testing::Test {
public:
	void SetUp() override
	{
		gbIsSpawn = false;
		gbIsMultiplayer = false;
		gbIsHellfire = true;
		gbIsPoe2 = true;
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		LoadMonsterData();
		LoadMissileData();
		Missiles.clear();
		LevelMonsterTypes[0].type = MT_NZOMBIE;
	}

	void TearDown() override
	{
		Missiles.clear();
		gbIsPoe2 = false;
		UnloadModArchives();
	}

	static Monster MakeMonster(uint32_t seed)
	{
		Monster monster {};
		monster.levelType = 0;
		monster.uniqueType = UniqueMonsterType::None;
		monster.flags = 0;
		monster.rndItemSeed = seed;
		monster.maxHitPoints = Fixed26_6::fromInt(100);
		monster.hitPoints = monster.maxHitPoints;
		monster.minDamage = 10;
		monster.maxDamage = 20;
		monster.resistance = 0;
		return monster;
	}

	static uint32_t FindSeed(MonsterRarity rarity, MonsterMod mod)
	{
		for (uint32_t seed = 1; seed < 100000; seed++) {
			const MonsterAffixes affixes = GetMonsterAffixes(MakeMonster(seed));
			if (affixes.rarity == rarity && affixes.has(mod))
				return seed;
		}
		ADD_FAILURE();
		return 0;
	}
};

TEST_F(Poe2MonsterModsTest, SomeMonstersAreMagicOrRare)
{
	int magic = 0;
	int rare = 0;
	constexpr int Count = 20000;
	for (uint32_t seed = 0; seed < Count; seed++) {
		const MonsterAffixes affixes = GetMonsterAffixes(MakeMonster(seed * 7919U));
		// The picked modifiers; Plated is rolled on top of them (PlatedIsRolledOnTop).
		int mods = 0;
		for (uint8_t mod = 0; mod < static_cast<uint8_t>(RolledModCount); mod++)
			mods += affixes.has(static_cast<MonsterMod>(mod)) ? 1 : 0;
		if (affixes.rarity == MonsterRarity::Magic) {
			magic++;
			EXPECT_EQ(mods, 1);
		} else if (affixes.rarity == MonsterRarity::Rare) {
			rare++;
			EXPECT_EQ(mods, RareMonsterModCount);
		} else {
			EXPECT_EQ(mods, 0);
		}
	}
	EXPECT_NEAR(magic * 1000 / Count, MagicMonsterPerMille, 15);
	EXPECT_NEAR(rare * 1000 / Count, RareMonsterPerMille, 8);
}

/** @brief GetMonsterAffixes as it was before Plated (monster armour plan step 5), off a map: the golden picks. */
MonsterAffixes AffixesBeforePlated(uint32_t seed)
{
	MonsterAffixes affixes;
	const uint32_t hash = Mix(seed ^ 0x6D6F6E73U);
	const int roll = static_cast<int>(hash % 1000);
	const auto choose = [hash](int count) {
		uint8_t mods = 0;
		for (int picked = 0, i = 0; picked < count && i < 16; i++) {
			const int mod = static_cast<int>(Mix(hash + static_cast<uint32_t>(i)) % 4);
			if ((mods & (1U << mod)) != 0)
				continue;
			mods |= static_cast<uint8_t>(1U << mod);
			picked++;
		}
		return mods;
	};
	if (roll < RareMonsterPerMille) {
		affixes.rarity = MonsterRarity::Rare;
		affixes.mods = choose(RareMonsterModCount);
	} else if (roll < RareMonsterPerMille + MagicMonsterPerMille) {
		affixes.rarity = MonsterRarity::Magic;
		affixes.mods = choose(1);
	}
	return affixes;
}

TEST_F(Poe2MonsterModsTest, ExistingModsUnchangedForSeeds)
{
	// Adding Plated moved no monster's rarity or other modifiers.
	for (uint32_t seed = 0; seed < 10000; seed++) {
		const MonsterAffixes now = GetMonsterAffixes(MakeMonster(seed));
		const MonsterAffixes before = AffixesBeforePlated(seed);
		ASSERT_EQ(now.rarity, before.rarity) << seed;
		ASSERT_EQ(now.mods & 0x0F, before.mods) << seed;
	}
}

TEST_F(Poe2MonsterModsTest, PlatedIsRolledOnTop)
{
	int rare = 0;
	int rarePlated = 0;
	int magic = 0;
	int magicPlated = 0;
	for (uint32_t seed = 0; seed < 200000; seed++) {
		const MonsterAffixes affixes = GetMonsterAffixes(MakeMonster(seed));
		if (affixes.rarity == MonsterRarity::Rare) {
			rare++;
			rarePlated += affixes.has(MonsterMod::Plated) ? 1 : 0;
		} else if (affixes.rarity == MonsterRarity::Magic) {
			magic++;
			magicPlated += affixes.has(MonsterMod::Plated) ? 1 : 0;
		} else {
			ASSERT_FALSE(affixes.has(MonsterMod::Plated));
		}
	}
	ASSERT_GT(rare, 1000);
	EXPECT_NEAR(rarePlated * 100 / rare, RarePlatedPercent, 3);
	EXPECT_NEAR(magicPlated * 100 / magic, MagicPlatedPercent, 2);
	EXPECT_EQ(MonsterModName(MonsterMod::Plated), "Plated");
}

TEST_F(Poe2MonsterModsTest, ImmunitiesAreResistancesInPoe2)
{
	// Zombies and skeletons are immune to Magic in Diablo, so Ice Bolt flew straight through them.
	Monster zombie = MakeMonster(1);
	zombie.resistance = IMMUNE_MAGIC | RESIST_FIRE;
	EXPECT_FALSE(zombie.isImmune(MissileID::BlueFlare, DamageType::Cold));
	EXPECT_TRUE(zombie.isResistant(MissileID::BlueFlare, DamageType::Cold));
	EXPECT_TRUE(zombie.isResistant(MissileID::Firebolt, DamageType::Fire));
	EXPECT_FALSE(zombie.isResistant(MissileID::LightningControl, DamageType::Lightning));
	EXPECT_EQ(zombie.shownResistance(), RESIST_MAGIC | RESIST_FIRE);

	gbIsPoe2 = false;
	EXPECT_TRUE(zombie.isImmune(MissileID::BlueFlare, DamageType::Cold));
	EXPECT_FALSE(zombie.isResistant(MissileID::BlueFlare, DamageType::Cold));
	EXPECT_EQ(zombie.shownResistance(), IMMUNE_MAGIC | RESIST_FIRE);
	gbIsPoe2 = true;
}

TEST_F(Poe2MonsterModsTest, UniquesAndVanillaStayNormal)
{
	const uint32_t seed = FindSeed(MonsterRarity::Rare, MonsterMod::ExtraLife);
	Monster unique = MakeMonster(seed);
	unique.uniqueType = UniqueMonsterType::Garbud;
	EXPECT_EQ(GetMonsterAffixes(unique).rarity, MonsterRarity::Normal);

	gbIsPoe2 = false;
	EXPECT_EQ(GetMonsterAffixes(MakeMonster(seed)).rarity, MonsterRarity::Normal);
}

TEST_F(Poe2MonsterModsTest, ModifiersChangeTheMonster)
{
	Monster tough = MakeMonster(FindSeed(MonsterRarity::Rare, MonsterMod::ExtraLife));
	ApplyMonsterRarity(tough);
	EXPECT_EQ(tough.maxHitPoints, Fixed26_6::fromInt(100 + RareMonsterLifePercent + ExtraLifePercent));
	EXPECT_EQ(tough.hitPoints, tough.maxHitPoints);
	EXPECT_EQ(MonsterExperiencePercent(tough), 300);
	EXPECT_EQ(MonsterExtraDrops(tough), 2);
	EXPECT_FALSE(RareMonsterName(tough).empty());

	Monster brute = MakeMonster(FindSeed(MonsterRarity::Magic, MonsterMod::HardHitting));
	ApplyMonsterRarity(brute);
	// Hard Hitting scales each hit; the damage bytes stay the monster's own.
	EXPECT_EQ(brute.minDamage, 10);
	EXPECT_EQ(brute.maxDamage, 20);
	EXPECT_EQ(MonsterModsDamagePercent(brute), 100 + HardHittingPercent);
	EXPECT_EQ(MonsterModsDamagePercent(tough), 100);
	EXPECT_EQ(brute.maxHitPoints, Fixed26_6::fromInt(100 + MagicMonsterLifePercent));

	Monster resistant = MakeMonster(FindSeed(MonsterRarity::Magic, MonsterMod::Resistant));
	ApplyMonsterRarity(resistant);
	EXPECT_EQ(resistant.resistance, 0);
	EXPECT_EQ(MonsterResistance(resistant, DamageType::Fire), ResistantModPercent);
	EXPECT_EQ(MonsterResistance(resistant, DamageType::Cold), ResistantModPercent);
	EXPECT_EQ(MonsterResistance(resistant, DamageType::Physical), 0);
}

TEST_F(Poe2MonsterModsTest, DiablosMarksArePercentages)
{
	Monster zombie = MakeMonster(1);
	zombie.resistance = IMMUNE_MAGIC | RESIST_FIRE;
	ASSERT_EQ(GetMonsterAffixes(zombie).rarity, MonsterRarity::Normal);
	EXPECT_EQ(MonsterResistance(zombie, DamageType::Fire), ResistMarkPercent);
	EXPECT_EQ(MonsterResistance(zombie, DamageType::Lightning), 0);
	// A zombie is undead: its cold (Diablo's magic) immune mark is 20%, not 40% (Curtis 2026-10-28).
	ASSERT_EQ(zombie.data().monsterClass, MonsterClass::Undead);
	EXPECT_EQ(MonsterResistance(zombie, DamageType::Cold), UndeadColdImmuneMarkPercent);
	EXPECT_EQ(UndeadColdImmuneMarkPercent, 20);
	EXPECT_EQ(ResistanceMarkPercent(IMMUNE_FIRE, DamageType::Fire), ImmuneMarkPercent);
	EXPECT_EQ(ResistanceMarkPercent(RESIST_MAGIC, DamageType::Cold), ColdResistMarkPercent);
	EXPECT_EQ(ResistanceMarkPercent(RESIST_MAGIC, DamageType::Cold, /*undead=*/true), ColdResistMarkPercent);
	EXPECT_EQ(ResistanceMarksText(IMMUNE_MAGIC | RESIST_FIRE), "Resistances: Fire 30%, Cold 40%");
	EXPECT_EQ(ResistanceMarksText(IMMUNE_MAGIC | RESIST_FIRE, /*undead=*/true), "Resistances: Fire 30%, Cold 20%");
	EXPECT_EQ(ResistanceMarksText(0), "No resistances");

	// A magic-immune zombie takes 80% of Ice Bolt's damage rather than none.
	EXPECT_EQ(MonsterDamageTakenPercent(zombie, DamageType::Cold), 100 - UndeadColdImmuneMarkPercent);

	// A demon with the same mark keeps 40%.
	LevelMonsterTypes[1].type = MT_TOAD;
	Monster demon = MakeMonster(1);
	demon.levelType = 1;
	demon.resistance = IMMUNE_MAGIC;
	ASSERT_EQ(demon.data().monsterClass, MonsterClass::Demon);
	EXPECT_EQ(MonsterResistance(demon, DamageType::Cold), ColdImmuneMarkPercent);
	EXPECT_EQ(ColdImmuneMarkPercent, 40);

	gbIsPoe2 = false;
	EXPECT_EQ(MonsterResistance(zombie, DamageType::Cold), 0);
	gbIsPoe2 = true;
}

TEST_F(Poe2MonsterModsTest, CursesAndExposureLowerResistance)
{
	// A zombie is undead, so its magic immune mark is UndeadColdImmuneMarkPercent (Curtis 2026-10-28).
	Monster zombie = MakeMonster(1);
	zombie.resistance = IMMUNE_MAGIC;
	zombie.poe2.lowerResistTicks = 10;
	EXPECT_EQ(MonsterResistance(zombie, DamageType::Cold), UndeadColdImmuneMarkPercent - LowerResistCursePercent);
	EXPECT_EQ(MonsterResistance(zombie, DamageType::Fire), -LowerResistCursePercent);
	zombie.poe2.exposureTicks = 10;
	zombie.poe2.exposureElement = static_cast<uint8_t>(DamageType::Cold);
	EXPECT_EQ(MonsterResistance(zombie, DamageType::Cold), UndeadColdImmuneMarkPercent - LowerResistCursePercent - ExposurePercent);
	EXPECT_EQ(MonsterResistance(zombie, DamageType::Fire), -LowerResistCursePercent);
	// Below zero the monster takes more damage.
	EXPECT_EQ(MonsterDamageTakenPercent(zombie, DamageType::Cold), 100 - (UndeadColdImmuneMarkPercent - LowerResistCursePercent - ExposurePercent));

	// Rare and unique monsters shrug off part of it.
	Monster rare = MakeMonster(FindSeed(MonsterRarity::Rare, MonsterMod::ExtraLife));
	rare.poe2.lowerResistTicks = 10;
	EXPECT_EQ(CurseEffectPercent(rare), 70);
	EXPECT_EQ(MonsterResistanceLowered(rare, DamageType::Fire), LowerResistCursePercent * 70 / 100);
	Monster unique = MakeMonster(1);
	unique.uniqueType = UniqueMonsterType::Garbud;
	EXPECT_EQ(CurseEffectPercent(unique), 50);
}

TEST_F(Poe2MonsterModsTest, ResistanceIsCappedAndPenetrationStopsAtZero)
{
	Monster lord = MakeMonster(FindSeed(MonsterRarity::Rare, MonsterMod::Resistant));
	lord.resistance = IMMUNE_FIRE;
	EXPECT_EQ(MonsterBaseResistance(lord, DamageType::Fire), ImmuneMarkPercent + ResistantModPercent);
	EXPECT_EQ(MonsterResistance(lord, DamageType::Fire), MaxMonsterResistance);
	EXPECT_EQ(MonsterHitResistance(lord, DamageType::Fire, 25), MaxMonsterResistance - 25);
	EXPECT_EQ(MonsterHitResistance(lord, DamageType::Fire, 200), 0);

	Monster cursed = MakeMonster(1);
	cursed.poe2.lowerResistTicks = 10;
	EXPECT_EQ(MonsterHitResistance(cursed, DamageType::Fire, 25), -LowerResistCursePercent);
}

TEST_F(Poe2MonsterModsTest, ElementalResistanceModLeavesChaosAlone)
{
	LoadMonsterChaosResistances();
	LevelMonsterTypes[0].type = MT_NACID;
	Monster beast = MakeMonster(FindSeed(MonsterRarity::Rare, MonsterMod::Resistant));
	EXPECT_EQ(MonsterResistance(beast, DamageType::Fire), ResistantModPercent);
	EXPECT_EQ(MonsterResistance(beast, DamageType::Chaos), 30);
	LevelMonsterTypes[0].type = MT_NZOMBIE;
	EXPECT_EQ(MonsterResistance(beast, DamageType::Chaos), 0);
}

TEST_F(Poe2MonsterModsTest, RegeneratingMonstersHeal)
{
	Monster monster = MakeMonster(FindSeed(MonsterRarity::Magic, MonsterMod::Regenerating));
	ApplyMonsterRarity(monster);
	monster.hitPoints = Fixed26_6::fromInt(10);
	for (int i = 0; i < 20; i++)
		ProcessMonsterAilments(monster);
	// 1.5% of 130 life per second.
	EXPECT_EQ(monster.hitPoints.raw(), Fixed26_6::fromInt(10).raw() + (Fixed26_6::fromInt(130).raw() * RegenPerMillePerSecond / 1000));
}

TEST_F(Poe2MonsterModsTest, BigElementalHitsIgniteAndShock)
{
	Monster monster = MakeMonster(1);
	// Like POE2: 1% per 5% of the monster's life as fire, 1% per 4% as lightning.
	EXPECT_EQ(AilmentChancePerMille(monster, DamageType::Fire, Fixed26_6::fromInt(50)), 100);
	EXPECT_EQ(AilmentChancePerMille(monster, DamageType::Fire, Fixed26_6::fromInt(10)), 20);
	EXPECT_EQ(AilmentChancePerMille(monster, DamageType::Lightning, Fixed26_6::fromInt(50)), 125);
	EXPECT_EQ(AilmentChancePerMille(monster, DamageType::Fire, Fixed26_6::fromInt(1000)), 1000);

	Players.resize(1);
	for (int i = 0; i < 200 && monster.poe2.shockTicks == 0; i++)
		TryInflictAilment(monster, Players[0], DamageType::Lightning, Fixed26_6::fromInt(60));
	EXPECT_EQ(monster.poe2.shockTicks, ShockTicks);
	EXPECT_EQ(ApplyShock(monster, Fixed26_6::fromInt(10)), Fixed26_6::fromInt(12));

	// Each fire hit adds Flammability, so repeated hits surely ignite once it reaches 100%.
	TryInflictAilment(monster, Players[0], DamageType::Fire, Fixed26_6::fromInt(10));
	EXPECT_EQ(monster.poe2.flammability, 20);
	for (int i = 0; i < 9; i++)
		TryInflictAilment(monster, Players[0], DamageType::Fire, Fixed26_6::fromInt(60));
	EXPECT_EQ(monster.poe2.flammability, 1000);
	EXPECT_EQ(monster.poe2.igniteTicks, IgniteTicks);
	// 20% of the hit per second, spread over 20 ticks.
	EXPECT_EQ(monster.poe2.igniteDamagePerTick, Fixed26_6::fromInt(60).raw() * IgnitePercentPerSecond / 100 / 20);

	Monster physical = MakeMonster(2);
	TryInflictAilment(physical, Players[0], DamageType::Physical, Fixed26_6::fromInt(60));
	EXPECT_EQ(physical.poe2.igniteTicks, 0);
	EXPECT_EQ(physical.poe2.shockTicks, 0);
}

TEST_F(Poe2MonsterModsTest, ColdHitsChillAndFreeze)
{
	uint32_t seed = 1;
	while (GetMonsterAffixes(MakeMonster(seed)).rarity != MonsterRarity::Normal)
		seed++;
	Monster monster = MakeMonster(seed);
	Players.resize(1);

	// 10 of 100 life chills by 20% and builds a fifth of the way to a freeze.
	TryInflictAilment(monster, Players[0], DamageType::Cold, Fixed26_6::fromInt(10));
	EXPECT_EQ(monster.poe2.chillTicks, ChillTicks);
	EXPECT_EQ(monster.poe2.chillPercent, 20);
	EXPECT_EQ(monster.poe2.freezeTicks, 0);
	int skipped = 0;
	for (int i = 0; i < 10; i++)
		skipped += MonsterSkipsTick(monster) ? 1 : 0;
	EXPECT_EQ(skipped, 2);

	TryInflictAilment(monster, Players[0], DamageType::Cold, Fixed26_6::fromInt(40));
	EXPECT_EQ(monster.poe2.chillPercent, MaxChillPercent);
	EXPECT_EQ(monster.poe2.freezeTicks, FreezeTicks);
	EXPECT_EQ(monster.poe2.freezeBuildup, 0);
	EXPECT_TRUE(IsMonsterImmobilized(monster));
	EXPECT_TRUE(MonsterSkipsTick(monster));

	for (int i = 0; i < FreezeTicks; i++)
		ProcessMonsterAilments(monster);
	EXPECT_FALSE(IsMonsterImmobilized(monster));
}

TEST_F(Poe2MonsterModsTest, PhysicalHitsBuildUpHeavyStun)
{
	uint32_t seed = 1;
	while (GetMonsterAffixes(MakeMonster(seed)).rarity != MonsterRarity::Normal)
		seed++;
	Monster monster = MakeMonster(seed);
	Players.resize(1);

	TryInflictAilment(monster, Players[0], DamageType::Physical, Fixed26_6::fromInt(60));
	EXPECT_EQ(monster.poe2.heavyStunTicks, 0);
	EXPECT_EQ(monster.poe2.stunBuildup, Fixed26_6::fromInt(60).raw());

	// Buildup fades by a fifth of the threshold per second.
	for (int i = 0; i < 20; i++)
		ProcessMonsterAilments(monster);
	EXPECT_EQ(monster.poe2.stunBuildup, Fixed26_6::fromInt(40).raw());

	TryInflictAilment(monster, Players[0], DamageType::Physical, Fixed26_6::fromInt(60));
	EXPECT_EQ(monster.poe2.heavyStunTicks, HeavyStunTicks);
	EXPECT_TRUE(IsMonsterImmobilized(monster));

	Monster unique = MakeMonster(seed);
	unique.uniqueType = UniqueMonsterType::Garbud;
	EXPECT_EQ(MonsterBuildupPercent(unique), UniqueBuildupPercent);
}

TEST_F(Poe2MonsterModsTest, BlackDeathTakesNoMaxLife)
{
	ASSERT_GT(MonstersData[MT_YZOMBIE].reducePlayerMaxHP, 0) << "Diablo's Black Death drains max life";
	Players.resize(1);
	MyPlayerId = 0;
	MyPlayer = &Players[0];
	Player &player = *MyPlayer;
	player = {};
	player._pMaxHPBase = player._pMaxHP = player._pHPBase = player._pHitPoints = Fixed26_6::fromInt(100);
	Monster monster = MakeMonster(1);
	monster.reducePlayerMaxHP = MonstersData[MT_YZOMBIE].reducePlayerMaxHP;

	// POE2 (Curtis 2026-10-28): Black Death just deals its damage.
	MonsterReducePlayerAttribute(monster, player);
	EXPECT_EQ(player._pMaxHPBase, Fixed26_6::fromInt(100));
	EXPECT_EQ(player._pMaxHP, Fixed26_6::fromInt(100));

	// Diablo keeps the drain.
	gbIsPoe2 = false;
	MonsterReducePlayerAttribute(monster, player);
	gbIsPoe2 = true;
	EXPECT_LT(player._pMaxHPBase, Fixed26_6::fromInt(100));
}

} // namespace
} // namespace devilution
