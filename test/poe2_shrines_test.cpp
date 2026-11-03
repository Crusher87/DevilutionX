#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "player.h"
#include "poe2/effect_primitives.hpp"
#include "poe2/shrines.hpp"
#include "poe2/unique_effects.hpp"
#include "spells.h"
#include "tables/playerdat.hpp"
#include "utils/file_util.h"
#include "utils/paths.h"

namespace devilution {
namespace {

constexpr int Ticks = 20;

class Poe2ShrinesTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(1);
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
		LoadShrineData();
		leveltype = DTYPE_TOWN;
		currlevel = 0;
	}

	void TearDown() override
	{
		MyPlayer->poe2.clear();
		gbIsPoe2 = false;
		LoadShrineData();
		UnloadModArchives();
	}

	static Player &NakedHero(HeroClass heroClass, int level)
	{
		Player &player = *MyPlayer;
		player = {};
		player.poe2.clear();
		player._pClass = heroClass;
		player.setCharacterLevel(static_cast<uint8_t>(level));
		const ClassAttributes &attributes = player.getClassAttributes();
		player._pBaseStr = player._pStrength = attributes.baseStr;
		player._pBaseMag = player._pMagic = attributes.baseMag;
		player._pBaseDex = player._pDexterity = attributes.baseDex;
		player._pBaseVit = player._pVitality = attributes.baseVit;
		player._pMaxHPBase = player._pHPBase = player.calculateBaseLife();
		player._pMaxManaBase = player._pManaBase = player.calculateBaseMana();
		CalcPlrInv(player, false);
		player._pHitPoints = player._pMaxHP;
		return player;
	}

	static void Give(ShrineBuffState &state, ShrineKind kind, uint8_t variant = 0)
	{
		const ShrineData &shrine = GetShrineData(kind);
		AddShrineBuff(state, kind, variant, shrine.seconds * Ticks, shrine.refresh);
	}

	static std::vector<ShrineKind> Kinds(const ShrineBuffState &state)
	{
		std::vector<ShrineKind> kinds;
		for (const ShrineBuff &buff : state.buffs) {
			if (buff.active())
				kinds.push_back(buff.kind);
		}
		return kinds;
	}
};

TEST_F(Poe2ShrinesTest, TableHasEveryShrineInDiablosOrder)
{
	ASSERT_TRUE(HasShrineData());
	for (size_t i = 0; i < static_cast<size_t>(ShrineKind::Count); i++)
		EXPECT_EQ(GetShrineData(static_cast<ShrineKind>(i)).kind, static_cast<ShrineKind>(i));
	EXPECT_EQ(GetShrineData(ShrineKind::Hidden).power, "Echoing");
	EXPECT_EQ(GetShrineData(ShrineKind::Hidden).seconds, 20);
	EXPECT_EQ(GetShrineData(ShrineKind::Divine).seconds, 8);
	EXPECT_FALSE(GetShrineData(ShrineKind::Glowing).refresh);
	EXPECT_EQ(GetShrineData(ShrineKind::Town).effect, ShrineEffect::TownPortal);
}

TEST_F(Poe2ShrinesTest, NoTableOutsidePoe2)
{
	gbIsPoe2 = false;
	LoadShrineData();
	EXPECT_FALSE(HasShrineData());
	MyPlayer->poe2.shrines.buffs[1].kind = ShrineKind::Stone;
	MyPlayer->poe2.shrines.buffs[1].ticksLeft = 100;
	EXPECT_EQ(GetShrineBonuses(*MyPlayer).armourPercent, 0);
	EXPECT_EQ(GainAsExtraPercent(*MyPlayer, DamageType::Fire), 0);
	EXPECT_FALSE(AvoidsAilments(*MyPlayer));
}

TEST_F(Poe2ShrinesTest, AtMostTwoBuffsAndAThirdReplacesTheOldest)
{
	ShrineBuffState state;
	Give(state, ShrineKind::Hidden);
	EXPECT_EQ(ShrineBuffCount(state), 1U);
	Give(state, ShrineKind::Stone);
	EXPECT_EQ(Kinds(state), (std::vector<ShrineKind> { ShrineKind::Hidden, ShrineKind::Stone }));
	Give(state, ShrineKind::Holy);
	EXPECT_EQ(ShrineBuffCount(state), static_cast<size_t>(MaxShrineBuffs));
	EXPECT_EQ(Kinds(state), (std::vector<ShrineKind> { ShrineKind::Stone, ShrineKind::Holy }));
	ASSERT_NE(NewestShrineBuff(state), nullptr);
	EXPECT_EQ(NewestShrineBuff(state)->kind, ShrineKind::Holy);
}

TEST_F(Poe2ShrinesTest, SameShrineRestartsItsTimerAndBecomesNewest)
{
	ShrineBuffState state;
	Give(state, ShrineKind::Stone);
	Give(state, ShrineKind::Holy);
	for (int i = 0; i < 100; i++)
		TickShrineBuffState(state);
	EXPECT_EQ(state.buffs[0].ticksLeft, (45 * Ticks) - 100);
	Give(state, ShrineKind::Stone);
	EXPECT_EQ(Kinds(state), (std::vector<ShrineKind> { ShrineKind::Holy, ShrineKind::Stone }));
	EXPECT_EQ(state.buffs[1].ticksLeft, 45 * Ticks);
	EXPECT_EQ(state.buffs[1].ticksTotal, 45 * Ticks);
}

TEST_F(Poe2ShrinesTest, GlowingDoesNotRefresh)
{
	ShrineBuffState state;
	Give(state, ShrineKind::Glowing);
	for (int i = 0; i < 50; i++)
		TickShrineBuffState(state);
	Give(state, ShrineKind::Glowing);
	EXPECT_EQ(ShrineBuffCount(state), 1U);
	EXPECT_EQ(state.buffs[1].ticksLeft, (30 * Ticks) - 50);
}

TEST_F(Poe2ShrinesTest, TimersRunOutAndSayWhich)
{
	ShrineBuffState state;
	AddShrineBuff(state, ShrineKind::Hidden, 0, 3);
	AddShrineBuff(state, ShrineKind::Stone, 0, 5);
	EXPECT_TRUE(TickShrineBuffState(state).empty());
	EXPECT_TRUE(TickShrineBuffState(state).empty());
	const std::vector<ShrineKind> ended = TickShrineBuffState(state);
	ASSERT_EQ(ended.size(), 1U);
	EXPECT_EQ(ended[0], ShrineKind::Hidden);
	EXPECT_TRUE(state.fadeSound);
	EXPECT_EQ(Kinds(state), (std::vector<ShrineKind> { ShrineKind::Stone }));
	EXPECT_EQ(NewestShrineBuff(state)->kind, ShrineKind::Stone);
	TickShrineBuffState(state);
	EXPECT_EQ(TickShrineBuffState(state), (std::vector<ShrineKind> { ShrineKind::Stone }));
	EXPECT_EQ(ShrineBuffCount(state), 0U);
	EXPECT_EQ(NewestShrineBuff(state), nullptr);
}

TEST_F(Poe2ShrinesTest, CurseRunsOut)
{
	ShrineBuffState state;
	state.curse = ShrineCurse::Vulnerability;
	state.curseTicks = 2;
	state.curseTotal = 2;
	bool ended = true;
	TickShrineBuffState(state, &ended);
	EXPECT_FALSE(ended);
	EXPECT_EQ(state.curse, ShrineCurse::Vulnerability);
	TickShrineBuffState(state, &ended);
	EXPECT_TRUE(ended);
	EXPECT_EQ(state.curse, ShrineCurse::None);
}

TEST_F(Poe2ShrinesTest, RemoveTakesOneBuffOff)
{
	ShrineBuffState state;
	Give(state, ShrineKind::Divine);
	Give(state, ShrineKind::Stone);
	EXPECT_TRUE(RemoveShrineBuff(state, ShrineKind::Divine));
	EXPECT_FALSE(RemoveShrineBuff(state, ShrineKind::Divine));
	EXPECT_EQ(Kinds(state), (std::vector<ShrineKind> { ShrineKind::Stone }));
}

TEST_F(Poe2ShrinesTest, SaveRoundTrip)
{
	ShrineBuffState state;
	Give(state, ShrineKind::Solar, SolarNight);
	Give(state, ShrineKind::Sparkling);
	for (int i = 0; i < 37; i++)
		TickShrineBuffState(state);
	state.curse = ShrineCurse::TemporalChains;
	state.curseTicks = 123;
	state.curseTotal = ShrineCurseTicks;

	const std::vector<uint8_t> bytes = PackShrineBuffs(state);
	ShrineBuffState loaded;
	ASSERT_TRUE(UnpackShrineBuffs(loaded, bytes));
	EXPECT_EQ(Kinds(loaded), Kinds(state));
	for (size_t i = 0; i < state.buffs.size(); i++) {
		EXPECT_EQ(loaded.buffs[i].kind, state.buffs[i].kind);
		EXPECT_EQ(loaded.buffs[i].variant, state.buffs[i].variant);
		EXPECT_EQ(loaded.buffs[i].ticksLeft, state.buffs[i].ticksLeft);
		EXPECT_EQ(loaded.buffs[i].ticksTotal, state.buffs[i].ticksTotal);
	}
	EXPECT_EQ(loaded.curse, ShrineCurse::TemporalChains);
	EXPECT_EQ(loaded.curseTicks, 123);
	EXPECT_EQ(loaded.curseTotal, ShrineCurseTicks);

	// No buffs packs and loads as none.
	ShrineBuffState empty;
	ASSERT_TRUE(UnpackShrineBuffs(loaded, PackShrineBuffs(empty)));
	EXPECT_EQ(ShrineBuffCount(loaded), 0U);
	EXPECT_EQ(loaded.curse, ShrineCurse::None);
}

TEST_F(Poe2ShrinesTest, BrokenSaveEntryLoadsAsNoBuffs)
{
	ShrineBuffState loaded;
	Give(loaded, ShrineKind::Stone);
	EXPECT_FALSE(UnpackShrineBuffs(loaded, std::vector<uint8_t> {}));
	EXPECT_EQ(ShrineBuffCount(loaded), 0U);
	EXPECT_FALSE(UnpackShrineBuffs(loaded, std::vector<uint8_t> { 0, 1 }));
	EXPECT_FALSE(UnpackShrineBuffs(loaded, std::vector<uint8_t> { 1, 200, 3 }));
	EXPECT_FALSE(UnpackShrineBuffs(loaded, std::vector<uint8_t> { 1, 1, 5, 0, 10 }));
	EXPECT_EQ(ShrineBuffCount(loaded), 0U);
	// A kind this build doesn't know is dropped.
	ASSERT_TRUE(UnpackShrineBuffs(loaded, std::vector<uint8_t> { 1, 1, 250, 0, 10, 0, 10, 0, 0, 0, 0, 0, 0 }));
	EXPECT_EQ(ShrineBuffCount(loaded), 0U);
}

TEST_F(Poe2ShrinesTest, ShrinesPerFloor)
{
	for (int level = 1; level <= 4; level++)
		EXPECT_EQ(ShrineCapForLevel(level), 1) << level;
	for (int level = 5; level <= 24; level++)
		EXPECT_EQ(ShrineCapForLevel(level), 2) << level;
}

TEST_F(Poe2ShrinesTest, DivineStaysOffEarlyAndBossFloors)
{
	const auto has = [](const std::vector<ShrineKind> &kinds, ShrineKind kind) { return std::find(kinds.begin(), kinds.end(), kind) != kinds.end(); };
	const int all = static_cast<int>(ShrineKind::Count);
	for (int level = 1; level <= 4; level++)
		EXPECT_FALSE(has(SpawnableShrines(level, false, all), ShrineKind::Divine)) << level;
	EXPECT_TRUE(has(SpawnableShrines(5, false, all), ShrineKind::Divine));
	EXPECT_FALSE(has(SpawnableShrines(10, true, all), ShrineKind::Divine));
	EXPECT_TRUE(IsShrineBossFloor(16, false, false));
	EXPECT_TRUE(IsShrineBossFloor(24, false, false));
	EXPECT_TRUE(IsShrineBossFloor(7, true, false));
	EXPECT_TRUE(IsShrineBossFloor(7, false, true));
	EXPECT_FALSE(IsShrineBossFloor(7, false, false));
	// Eldritch isn't placed, and without Hellfire only Diablo's first 26 shrines are.
	EXPECT_FALSE(has(SpawnableShrines(10, false, all), ShrineKind::Eldritch));
	EXPECT_FALSE(has(SpawnableShrines(10, false, 26), ShrineKind::Oily));
	EXPECT_TRUE(has(SpawnableShrines(10, false, 26), ShrineKind::Tainted));
}

TEST_F(Poe2ShrinesTest, ResolveIsTheSameOnEveryClient)
{
	for (uint32_t seed = 1; seed < 200; seed++) {
		DiabloGenerator a(seed);
		DiabloGenerator b(seed);
		const ShrineGrant first = ResolveShrine(ShrineKind::Murphys, a, false);
		const ShrineGrant second = ResolveShrine(ShrineKind::Murphys, b, false);
		EXPECT_EQ(first.kind, second.kind);
		EXPECT_EQ(first.curse, second.curse);
		// Murphy's always curses, and the random shrines never give Divine or another random shrine.
		EXPECT_NE(first.curse, ShrineCurse::None);
		EXPECT_NE(first.kind, ShrineKind::Count);
		EXPECT_NE(GetShrineData(first.kind).effect, ShrineEffect::Divine);
		EXPECT_NE(GetShrineData(first.kind).effect, ShrineEffect::Random);
		EXPECT_NE(GetShrineData(first.kind).effect, ShrineEffect::RandomCursed);
		DiabloGenerator c(seed);
		const ShrineGrant mysterious = ResolveShrine(ShrineKind::Mysterious, c, false);
		EXPECT_EQ(mysterious.curse, ShrineCurse::None);
		EXPECT_NE(mysterious.kind, ShrineKind::Count);
		EXPECT_NE(GetShrineData(mysterious.kind).effect, ShrineEffect::Divine);
	}
}

TEST_F(Poe2ShrinesTest, ResolvePlainShrinesAndSolar)
{
	DiabloGenerator rng(7);
	EXPECT_EQ(ResolveShrine(ShrineKind::Stone, rng, false).kind, ShrineKind::Stone);
	EXPECT_EQ(ResolveShrine(ShrineKind::Town, rng, false).kind, ShrineKind::Count);
	EXPECT_EQ(ResolveShrine(ShrineKind::Solar, rng, false).variant, SolarDay);
	EXPECT_EQ(ResolveShrine(ShrineKind::Solar, rng, true).variant, SolarNight);
}

TEST_F(Poe2ShrinesTest, EachEffectsNumbers)
{
	const auto bonuses = [](ShrineKind kind, uint8_t variant = 0) {
		ShrineBuffState state;
		Give(state, kind, variant);
		return ShrineStateBonuses(state);
	};
	EXPECT_TRUE(bonuses(ShrineKind::Hidden).echoing);
	EXPECT_TRUE(bonuses(ShrineKind::Gloomy).gloom);
	EXPECT_TRUE(bonuses(ShrineKind::Weird).hexing);
	EXPECT_EQ(bonuses(ShrineKind::Magical).lifeRegenPermille, 40);
	EXPECT_EQ(bonuses(ShrineKind::Magical).manaRegenPermille, 100);
	EXPECT_EQ(bonuses(ShrineKind::Stone).armourPercent, 100);
	EXPECT_EQ(bonuses(ShrineKind::Stone).evasionPercent, 100);
	EXPECT_EQ(bonuses(ShrineKind::Stone).energyShieldPercent, 100);
	EXPECT_EQ(bonuses(ShrineKind::Religious).resistances, 50);
	EXPECT_EQ(bonuses(ShrineKind::Religious).maxResistances, 5);
	EXPECT_TRUE(bonuses(ShrineKind::Enchanted).alwaysCrit);
	EXPECT_EQ(bonuses(ShrineKind::Thaumaturgic).rarityPercent, 50);
	EXPECT_EQ(bonuses(ShrineKind::Thaumaturgic).quantityPercent, 15);
	EXPECT_EQ(bonuses(ShrineKind::Thaumaturgic).damageTakenPercent, 10);
	EXPECT_TRUE(bonuses(ShrineKind::Fascinating).meteoric);
	EXPECT_TRUE(bonuses(ShrineKind::Cryptic).tempest);
	EXPECT_TRUE(bonuses(ShrineKind::Eerie).freezing);
	EXPECT_TRUE(bonuses(ShrineKind::Divine).invulnerable);
	EXPECT_EQ(bonuses(ShrineKind::Holy).movePercent, 40);
	EXPECT_EQ(bonuses(ShrineKind::Holy).skillSpeedPercent, 60);
	EXPECT_TRUE(bonuses(ShrineKind::Sacred).arcs);
	EXPECT_EQ(bonuses(ShrineKind::Spiritual).goldPercent, 100);
	EXPECT_TRUE(bonuses(ShrineKind::Spiritual).noEquipmentDrops);
	EXPECT_EQ(bonuses(ShrineKind::Spooky).skeletons, 3);
	EXPECT_EQ(bonuses(ShrineKind::Abandoned).reflectPercent, 18);
	EXPECT_TRUE(bonuses(ShrineKind::Creepy).knockback);
	EXPECT_EQ(bonuses(ShrineKind::Creepy).moreDamagePercent, 50);
	EXPECT_EQ(bonuses(ShrineKind::Creepy).stunDurationPercent, 30);
	EXPECT_EQ(bonuses(ShrineKind::Quiet).lifePercent, 40);
	EXPECT_EQ(bonuses(ShrineKind::Quiet).areaPercent, 40);
	EXPECT_EQ(bonuses(ShrineKind::Ornate).lifePercent, 50);
	EXPECT_EQ(bonuses(ShrineKind::Ornate).armourPercent, 100);
	EXPECT_EQ(bonuses(ShrineKind::Secluded).rarityPercent, 100);
	EXPECT_EQ(bonuses(ShrineKind::Glimmering).projectileSpeedPercent, 80);
	EXPECT_EQ(bonuses(ShrineKind::Glimmering).skillSpeedPercent, 50);
	EXPECT_TRUE(bonuses(ShrineKind::Tainted).corruptDrops);
	EXPECT_EQ(bonuses(ShrineKind::Oily).extraFirePercent, 50);
	EXPECT_EQ(bonuses(ShrineKind::Glowing).experiencePercent, 50);
	EXPECT_TRUE(bonuses(ShrineKind::Mendicant).equipmentToCurrency);
	EXPECT_EQ(bonuses(ShrineKind::Sparkling).extraLightningPercent, 50);
	EXPECT_EQ(bonuses(ShrineKind::Shimmering).extraColdPercent, 50);
	const ShrineBonuses day = bonuses(ShrineKind::Solar, SolarDay);
	EXPECT_EQ(day.fireDamagePercent, 50);
	EXPECT_TRUE(day.avoidAilments);
	EXPECT_EQ(day.extraProjectiles, 0);
	const ShrineBonuses night = bonuses(ShrineKind::Solar, SolarNight);
	EXPECT_EQ(night.coldDamagePercent, 50);
	EXPECT_EQ(night.extraProjectiles, 2);
	EXPECT_FALSE(night.avoidAilments);

	// Two buffs add up, and the curses take away.
	ShrineBuffState both;
	Give(both, ShrineKind::Stone);
	Give(both, ShrineKind::Ornate);
	EXPECT_EQ(ShrineStateBonuses(both).armourPercent, 200);
	both.curse = ShrineCurse::ElementalWeakness;
	both.curseTicks = 10;
	EXPECT_EQ(ShrineStateBonuses(both).resistances, -20);
	both.curse = ShrineCurse::Vulnerability;
	EXPECT_EQ(ShrineStateBonuses(both).damageTakenPercent, 20);
	both.curse = ShrineCurse::TemporalChains;
	EXPECT_EQ(ShrineStateBonuses(both).movePercent, -20);
	EXPECT_EQ(ShrineStateBonuses(both).skillSpeedPercent, -20);
}

TEST_F(Poe2ShrinesTest, PrimitivesReadTheHerosBuffs)
{
	Player &player = NakedHero(HeroClass::Warrior, 10);
	EXPECT_EQ(GainAsExtraPercent(player, DamageType::Fire), 0);
	Give(player.poe2.shrines, ShrineKind::Oily);
	Give(player.poe2.shrines, ShrineKind::Shimmering);
	EXPECT_EQ(GainAsExtraPercent(player, DamageType::Fire), 50);
	EXPECT_EQ(GainAsExtraPercent(player, DamageType::Cold), 50);
	EXPECT_EQ(GainAsExtraPercent(player, DamageType::Lightning), 0);
	EXPECT_FALSE(AvoidsAilments(player));
	Give(player.poe2.shrines, ShrineKind::Solar, SolarDay);
	EXPECT_TRUE(AvoidsAilments(player));
	EXPECT_TRUE(HasShrineEffect(player, ShrineEffect::Solar));
	EXPECT_FALSE(HasShrineEffect(player, ShrineEffect::ExtraFire));
}

TEST_F(Poe2ShrinesTest, DiamondMakesEveryAttackCrit)
{
	Player &player = NakedHero(HeroClass::Warrior, 10);
	EXPECT_LT(AttackCritChance(player), 100);
	Give(player.poe2.shrines, ShrineKind::Enchanted);
	EXPECT_EQ(AttackCritChance(player), 100);
}

TEST_F(Poe2ShrinesTest, LifeBuffRaisesLifeAndEndsSafely)
{
	Player &player = NakedHero(HeroClass::Warrior, 20);
	const int32_t before = player._pMaxHP.raw();
	ShrineGrant grant;
	grant.kind = ShrineKind::Quiet;
	ApplyShrineGrant(player, grant);
	const int32_t during = player._pMaxHP.raw();
	EXPECT_GT(during, before);
	player._pHitPoints = player._pMaxHP;
	for (int i = 0; i < 30 * Ticks; i++)
		ProcessShrineBuffs(player);
	EXPECT_EQ(ShrineBuffCount(player.poe2.shrines), 0U);
	EXPECT_EQ(player._pMaxHP.raw(), before);
	EXPECT_LE(player._pHitPoints.raw(), player._pMaxHP.raw());
	EXPECT_GT(player._pHitPoints.raw(), 0);
}

TEST_F(Poe2ShrinesTest, DefenceAndResistanceBuffsReachTheHero)
{
	Player &player = NakedHero(HeroClass::Warrior, 20);
	const int fireBefore = player._pFireResist;
	ShrineGrant grant;
	grant.kind = ShrineKind::Religious;
	ApplyShrineGrant(player, grant);
	EXPECT_GT(player._pFireResist, fireBefore);
}

TEST_F(Poe2ShrinesTest, DivineEndsOnABossFloor)
{
	Player &player = NakedHero(HeroClass::Warrior, 20);
	ShrineGrant grant;
	grant.kind = ShrineKind::Divine;
	ApplyShrineGrant(player, grant);
	EXPECT_TRUE(GetShrineBonuses(player).invulnerable);
	leveltype = DTYPE_CATHEDRAL;
	currlevel = 3;
	setlevel = false;
	OnShrineLevelEntered(player);
	EXPECT_TRUE(GetShrineBonuses(player).invulnerable);
	leveltype = DTYPE_HELL;
	currlevel = 16;
	OnShrineLevelEntered(player);
	EXPECT_FALSE(GetShrineBonuses(player).invulnerable);
	leveltype = DTYPE_TOWN;
	currlevel = 0;
}

TEST_F(Poe2ShrinesTest, StormStrikesGrowWithTheArea)
{
	EXPECT_GT(ShrineStrikeRaw(1, 40), 0);
	EXPECT_GT(ShrineStrikeRaw(40, 40), ShrineStrikeRaw(5, 40));
	EXPECT_GT(ShrineStrikeRaw(20, 40), ShrineStrikeRaw(20, 15));
}

TEST_F(Poe2ShrinesTest, TooltipsAreOneLine)
{
	ShrineBuffState state;
	for (size_t i = 0; i < static_cast<size_t>(ShrineKind::Count); i++) {
		const auto kind = static_cast<ShrineKind>(i);
		EXPECT_EQ(ShrineHoverText(kind).find('\n'), std::string::npos);
		Give(state, kind);
		if (const ShrineBuff *newest = NewestShrineBuff(state); newest != nullptr && newest->kind == kind) {
			EXPECT_EQ(ShrineBuffTooltip(*newest).find('\n'), std::string::npos);
		}
	}
	ShrineGrant grant;
	grant.kind = ShrineKind::Solar;
	grant.variant = SolarNight;
	grant.curse = ShrineCurse::Vulnerability;
	EXPECT_EQ(ShrineGrantLine(grant).find('\n'), std::string::npos);
	EXPECT_NE(ShrineGrantLine(grant).find("Vulnerability"), std::string::npos);
}

} // namespace
} // namespace devilution
