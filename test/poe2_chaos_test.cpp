#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <sstream>
#include <string>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/defenses.hpp"
#include "poe2/item_stats.hpp"
#include "engine/random.hpp"
#include "poe2/loot.hpp"
#include "poe2/map_mods.hpp"
#include "poe2/map_play.hpp"
#include "poe2/waystones.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/monster_resistance.hpp"
#include "poe2/unique_effects.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/telemetry.hpp"
#include "missiles.h"
#include "monster.h"
#include "spells.h"
#include "tables/misdat.h"
#include "tables/monstdat.h"
#include "tables/playerdat.hpp"
#include "utils/file_util.h"
#include "utils/paths.h"

namespace devilution {
namespace {

/** @brief Chaos damage (research/chaos-damage/plan-v2.md): the rules that cut across files, end to end. */
class Poe2ChaosTest : public ::testing::Test {
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
		sgGameInitInfo.nDifficulty = DIFF_NORMAL;
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		LoadItemData();
		LoadSpellData();
		LoadMissileData();
		LoadPlayerDataFiles();
		LoadPassiveTreeData();
		LoadModifierData();

		Player &player = *MyPlayer;
		player._pClass = HeroClass::Sorcerer;
		player.setCharacterLevel(10);
		player._pBaseStr = player._pStrength = 100;
		player._pBaseMag = player._pMagic = 100;
		player._pBaseDex = player._pDexterity = 50;
		player._pBaseVit = player._pVitality = 25;
		player._pMaxHPBase = player._pHPBase = Fixed26_6::fromInt(100);
		player._pMaxManaBase = player._pManaBase = Fixed26_6::fromInt(20);
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		LoadPassiveTreeData();
		UnloadModArchives();
	}

	/** @brief Puts a ring with these chaos stats on the hero; slot picks the ring finger. */
	static Item &WearChaosRing(Player &player, inv_body_loc slot, int chaos, int maxChaos = 0)
	{
		Item &ring = player.InvBody[slot];
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iMiscId == IMISC_RING) {
				InitializeItem(ring, static_cast<_item_indexes>(i));
				break;
			}
		}
		ring._iStatFlag = true;
		ring._iIdentified = true;
		ring._iMagical = ITEM_QUALITY_MAGIC;
		if (chaos != 0)
			ApplyModEffect(ring, ModEffect::ChaosResistance, chaos, 0);
		if (maxChaos != 0)
			ApplyModEffect(ring, ModEffect::MaxChaosResistance, maxChaos, 0);
		return ring;
	}

	static void Recalculate(Player &player)
	{
		CalcPlrItemVals(player, false);
	}

	/** @brief Life the hero loses to a trap's 100 point hit of this type and missile (traps always hit for that much). */
	static int HitTaken(Player &player, DamageType type, MissileID missile = MissileID::Acid)
	{
		MyPlayerId = 0;
		player._pMaxHP = player._pMaxHPBase = Fixed26_6::fromInt(1000);
		for (uint32_t seed = 1; seed <= 400; seed++) {
			player._pHitPoints = player._pHPBase = Fixed26_6::fromInt(1000);
			SetRndSeed(seed);
			bool blocked = false;
			if (PlayerMHit(player, nullptr, 0, 100 * 64, 100 * 64, missile, type, true, DeathReason::MonsterOrTrap, &blocked))
				return 1000 - player._pHitPoints.whole();
		}
		ADD_FAILURE() << "never hit";
		return 0;
	}

	/** @brief Opens a map of tier 1 in the dungeon level the hero is on, with this one waystone modifier. */
	static void OpenMapWith(Player &player, MapDanger danger)
	{
		uint8_t family = 0;
		for (uint8_t id = 1; id <= WaystoneModIds; id++) {
			if (GetWaystoneMod(id).danger == danger)
				family = id;
		}
		ASSERT_NE(family, 0);
		currlevel = 21;
		leveltype = DTYPE_HELL;
		setlevel = false;
		player.poe2.mapTier = 1;
		player.poe2.mapLevel = currlevel;
		player.poe2.mapWaystone = {};
		player.poe2.mapWaystone.mods[0] = { family, 1, 0 };
		RefreshMapEffects(player);
		ClearMapPlay();
		ASSERT_GT(ActiveMapTier(), 0);
		ASSERT_GT(ActiveMapEffects()[danger], 0);
	}

	static void CloseMap(Player &player)
	{
		player.poe2.mapTier = 0;
		player.poe2.mapLevel = 0;
		player.poe2.mapWaystone = {};
		RefreshMapEffects(player);
		ClearMapPlay();
		currlevel = 0;
		leveltype = DTYPE_TOWN;
	}
};

TEST_F(Poe2ChaosTest, ChaosResistanceFamilyComesOnlyFromWhatNamesIt)
{
	const std::optional<uint8_t> family = FindModifierFamily("chaos_resistance");
	ASSERT_TRUE(family.has_value());
	const ModFamily &chaos = ModifierFamilies()[*family];
	EXPECT_EQ(chaos.effect, ModEffect::ChaosResistance);
	EXPECT_TRUE(chaos.enabled);
	EXPECT_FALSE(chaos.natural);
}

TEST_F(Poe2ChaosTest, ChaosResistanceStartsAtZero)
{
	Player &player = *MyPlayer;
	Recalculate(player);
	EXPECT_EQ(player.poe2.chaosResistance, 0);
	EXPECT_EQ(player.poe2.maxChaosResistance, MaxResistance);
}

TEST_F(Poe2ChaosTest, ChaosResistanceAddsUpFromGear)
{
	Player &player = *MyPlayer;
	WearChaosRing(player, INVLOC_RING_LEFT, 13);
	WearChaosRing(player, INVLOC_RING_RIGHT, 9);
	Recalculate(player);
	EXPECT_EQ(player.poe2.chaosResistance, 22);
	EXPECT_EQ(player.poe2.uncappedChaosResistance, 22);
	// The elements don't get it, and it doesn't get theirs.
	EXPECT_EQ(player._pFireResist, 0);
	EXPECT_EQ(player._pMagResist, 0);
	EXPECT_EQ(player._pLghtResist, 0);

	// Unidentified gear doesn't count, as for every other modifier.
	player.InvBody[INVLOC_RING_RIGHT]._iIdentified = false;
	Recalculate(player);
	EXPECT_EQ(player.poe2.chaosResistance, 13);
}

TEST_F(Poe2ChaosTest, AllResistancesLeaveChaosOut)
{
	Player &player = *MyPlayer;
	Item &ring = WearChaosRing(player, INVLOC_RING_LEFT, 0);
	ApplyModEffect(ring, ModEffect::AllResistances, 20, 0);
	Recalculate(player);
	EXPECT_EQ(player._pFireResist, 20);
	EXPECT_EQ(player.poe2.chaosResistance, 0);
}

TEST_F(Poe2ChaosTest, ChaosResistanceHasItsOwnCap)
{
	Player &player = *MyPlayer;
	WearChaosRing(player, INVLOC_RING_LEFT, 60);
	WearChaosRing(player, INVLOC_RING_RIGHT, 40);
	Recalculate(player);
	EXPECT_EQ(player.poe2.uncappedChaosResistance, 100);
	EXPECT_EQ(player.poe2.chaosResistance, MaxResistance);

	// Maximum chaos resistance raises the cap, up to the hard cap.
	WearChaosRing(player, INVLOC_RING_RIGHT, 40, 5);
	Recalculate(player);
	EXPECT_EQ(player.poe2.maxChaosResistance, MaxResistance + 5);
	EXPECT_EQ(player.poe2.chaosResistance, MaxResistance + 5);
	WearChaosRing(player, INVLOC_RING_RIGHT, 40, 50);
	Recalculate(player);
	EXPECT_EQ(player.poe2.maxChaosResistance, HardMaxResistance);
	EXPECT_EQ(player.poe2.chaosResistance, HardMaxResistance);
}

TEST_F(Poe2ChaosTest, ChaosResistanceHasAFloorButNoDepthPenalty)
{
	Player &player = *MyPlayer;
	player._pLvlVisited[13] = true;
	sgGameInitInfo.nDifficulty = DIFF_HELL;
	WearChaosRing(player, INVLOC_RING_LEFT, 10);
	Recalculate(player);
	EXPECT_LT(player._pFireResist, 0);
	EXPECT_EQ(player.poe2.chaosResistance, 10);

	WearChaosRing(player, INVLOC_RING_LEFT, -200);
	Recalculate(player);
	EXPECT_EQ(player.poe2.uncappedChaosResistance, -200);
	EXPECT_EQ(player.poe2.chaosResistance, MinPoe2Resistance);
	sgGameInitInfo.nDifficulty = DIFF_NORMAL;
}

TEST_F(Poe2ChaosTest, TreeNodesGiveChaosResistance)
{
	// Chaos plan step 5: four notables trade their all resistances for chaos resistance.
	const std::pair<std::string_view, int> nodes[] = { { "Poisoned Water", 12 }, { "Perfect Harmony", 7 }, { "Grave Matters", 10 }, { "Soulstone Ward", 8 } };
	for (const auto &[name, chaos] : nodes) {
		Player &player = *MyPlayer;
		player.poe2.passives.reset();
		const PassiveNode *found = nullptr;
		for (const PassiveNode &node : PassiveTree()) {
			if (node.name == name)
				found = &node;
		}
		ASSERT_NE(found, nullptr) << name;
		player.poe2.passives.set(found->id);
		Recalculate(player);
		EXPECT_EQ(player.poe2.chaosResistance, chaos) << name;
		EXPECT_EQ(player._pFireResist, 0) << name;
	}
	EXPECT_EQ(PassiveStatText(PassiveStat::ChaosResist, 12), "+12% to Chaos Resistance");
	EXPECT_EQ(PassiveStatText(PassiveStat::MaxChaosResist, 2), "+2% to maximum Chaos Resistance");
	EXPECT_EQ(PassiveStatText(PassiveStat::AllResist, 5), "+5% to all Elemental Resistances");
}

TEST_F(Poe2ChaosTest, AmethystRingCarriesChaosResistance)
{
	Player &player = *MyPlayer;
	std::optional<size_t> row;
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		if (AllItemsList[i].iName == std::string_view("Amethyst Ring"))
			row = i;
	}
	ASSERT_TRUE(row);
	EXPECT_GT(AllItemsList[*row].dropRate, 0);
	for (uint32_t seed = 1; seed <= 20; seed++) {
		Item &ring = player.InvBody[INVLOC_RING_LEFT];
		ring = {};
		SetPoe2ItemLevel(ring, 40);
		SetRndSeed(seed);
		SetupAllItems(player, ring, static_cast<_item_indexes>(*row), AdvanceRndSeed(), 40, 1, /*onlygood=*/false, /*pregen=*/false, 0, /*forceNotUnique=*/true);
		ring._iStatFlag = true;
		ring._iIdentified = true;
		Recalculate(player);
		const int chaos = player.poe2.chaosResistance;
		EXPECT_GE(chaos, 7) << seed;
		EXPECT_LE(chaos, 13) << seed;
	}
}

TEST_F(Poe2ChaosTest, AcidMeetsChaosResistance)
{
	// Chaos plan step 6: the acid beasts' spit, splash and puddles are chaos.
	for (const MissileID missile : { MissileID::Acid, MissileID::AcidSplat, MissileID::AcidPuddle })
		EXPECT_EQ(GetMissileData(missile).damageType(), DamageType::Chaos) << static_cast<int>(missile);
	Player &player = *MyPlayer;
	player._pMagResist = 40;
	player.poe2.chaosResistance = 0;
	EXPECT_EQ(HitTaken(player, DamageType::Chaos), 100);
	player.poe2.chaosResistance = 30;
	EXPECT_EQ(HitTaken(player, DamageType::Chaos), 70);
	player.poe2.chaosResistance = 75;
	EXPECT_EQ(HitTaken(player, DamageType::Chaos), 25);
	player.poe2.chaosResistance = -60;
	EXPECT_EQ(HitTaken(player, DamageType::Chaos), 160);
	// Magic still meets magic resistance.
	EXPECT_EQ(HitTaken(player, DamageType::Magic), 60);

	// Outside POE2 mode chaos meets magic resistance, as acid always did.
	gbIsPoe2 = false;
	EXPECT_EQ(PlayerChaosResistance(player), 40);
	EXPECT_EQ(HitTaken(player, DamageType::Chaos), 60);
}

TEST_F(Poe2ChaosTest, LichesAndDiablosApocalypseDealChaos)
{
	Monster monster {};
	monster.ai = MonsterAIID::Lich;
	EXPECT_EQ(MonsterHitDamageType(&monster, MissileID::OrangeFlare, DamageType::Magic), DamageType::Chaos);
	monster.ai = MonsterAIID::ArchLich;
	EXPECT_EQ(MonsterHitDamageType(&monster, MissileID::YellowFlare, DamageType::Magic), DamageType::Chaos);
	monster.ai = MonsterAIID::Diablo;
	EXPECT_EQ(MonsterHitDamageType(&monster, MissileID::DiabloApocalypseBoom, DamageType::Physical), DamageType::Chaos);
	// Diablo's melee and the Succubi's Blood Star keep their types.
	EXPECT_EQ(MonsterHitDamageType(&monster, MissileID::Null, DamageType::Physical), DamageType::Physical);
	monster.ai = MonsterAIID::Succubus;
	EXPECT_EQ(MonsterHitDamageType(&monster, MissileID::BloodStar, DamageType::Magic), DamageType::Magic);
	// Traps and the hero's own skills on the same missiles too.
	EXPECT_EQ(MonsterHitDamageType(nullptr, MissileID::OrangeFlare, DamageType::Fire), DamageType::Fire);
	// Only in POE2 mode.
	gbIsPoe2 = false;
	monster.ai = MonsterAIID::Lich;
	EXPECT_EQ(MonsterHitDamageType(&monster, MissileID::OrangeFlare, DamageType::Magic), DamageType::Magic);
}

TEST_F(Poe2ChaosTest, ChaosDrainsEnergyShieldTwiceAsFast)
{
	Player &player = *MyPlayer;
	player.poe2.maxEnergyShield = 100;
	player.poe2.energyShield = 100 * 64;
	EXPECT_EQ(AbsorbWithEnergyShield(player, Fixed26_6::fromInt(30), DamageType::Chaos), Fixed26_6::fromInt(0));
	EXPECT_EQ(player.poe2.energyShield, 40 * 64);
	// A shield smaller than twice the hit stops half of what it has.
	EXPECT_EQ(AbsorbWithEnergyShield(player, Fixed26_6::fromInt(30), DamageType::Chaos), Fixed26_6::fromInt(10));
	EXPECT_EQ(player.poe2.energyShield, 0);
	// An odd shield rounds the damage stopped up.
	player.poe2.energyShield = 5;
	EXPECT_EQ(AbsorbWithEnergyShield(player, Fixed26_6::fromInt(1), DamageType::Chaos).raw(), 64 - 3);
	EXPECT_EQ(player.poe2.energyShield, 0);
	// Other damage costs the shield what it stops.
	player.poe2.energyShield = 100 * 64;
	EXPECT_EQ(AbsorbWithEnergyShield(player, Fixed26_6::fromInt(30), DamageType::Fire), Fixed26_6::fromInt(0));
	EXPECT_EQ(player.poe2.energyShield, 70 * 64);
	// One hit with both: the chaos part costs double, the rest single.
	player.poe2.energyShield = 50 * 64;
	const ShieldLeftover left = AbsorbHitWithEnergyShield(player, Fixed26_6::fromInt(20), Fixed26_6::fromInt(20));
	EXPECT_EQ(left.chaos, Fixed26_6::fromInt(0));
	EXPECT_EQ(left.damage, Fixed26_6::fromInt(10));
	EXPECT_EQ(player.poe2.energyShield, 0);
}

TEST_F(Poe2ChaosTest, AcidPuddlesDrainEnergyShieldTwiceAsFast)
{
	// Puddles hit like the spit does, so they take the double drain too (plan v2: no puddle exception).
	Player &player = *MyPlayer;
	player.poe2.maxEnergyShield = 500;
	player.poe2.energyShield = 500 * 64;
	EXPECT_EQ(HitTaken(player, DamageType::Chaos, MissileID::AcidPuddle), 0);
	EXPECT_EQ(player.poe2.energyShield, 300 * 64);
}

TEST_F(Poe2ChaosTest, ProfaneExtraChaosStaysChaos)
{
	Player &player = *MyPlayer;
	OpenMapWith(player, MapDanger::ExtraChaos);
	const int extra = ActiveMapEffects()[MapDanger::ExtraChaos];
	player.poe2.chaosResistance = 50;
	const MapMonsterHit hit = ApplyMapMonsterHit(player, Fixed26_6::fromInt(100));
	EXPECT_EQ(hit.physical, Fixed26_6::fromInt(100));
	EXPECT_EQ(hit.chaos.raw(), 100 * 64 * extra / 100 * 50 / 100);
	CloseMap(player);
}

TEST_F(Poe2ChaosTest, VenomousPoisonIsChaosAndGoesAroundEnergyShield)
{
	Player &player = *MyPlayer;
	MyPlayerId = 0;
	OpenMapWith(player, MapDanger::PoisonChance);
	for (uint32_t seed = 1; seed <= 200 && MapPoisonTicksLeft() == 0; seed++) {
		SetRndSeed(seed);
		OnMapMonsterSpellHit(player, Fixed26_6::fromInt(100));
	}
	ASSERT_EQ(MapPoisonTicksLeft(), MapPoisonTicks);
	player._pMaxHP = player._pMaxHPBase = Fixed26_6::fromInt(1000);
	player._pHitPoints = player._pHPBase = Fixed26_6::fromInt(1000);
	player.poe2.maxEnergyShield = 100;
	player.poe2.energyShield = 100 * 64;
	player.poe2.chaosResistance = 50;
	for (int i = 0; i < MapPoisonTicks; i++)
		ProcessMapPlayer(player);
	// 20% of the hit a second for two seconds, halved by chaos resistance; the shield never sees it.
	EXPECT_EQ(player._pHitPoints.whole(), 1000 - (100 * MapPoisonPercentPerSecond / 100 * 2 / 2));
	EXPECT_EQ(player.poe2.energyShield, 100 * 64);
	EXPECT_EQ(MapPoisonTicksLeft(), 0);
	CloseMap(player);
}

TEST_F(Poe2ChaosTest, TelemetryLogsChaosResistance)
{
	paths::SetPrefPath(::testing::TempDir() + "poe2_chaos_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + DIRECTORY_SEPARATOR_STR);
	Player &player = *MyPlayer;
	player._pMaxHP = player._pMaxHPBase;
	player._pHitPoints = player._pHPBase;
	WearChaosRing(player, INVLOC_RING_LEFT, 17);
	Recalculate(player);
	EXPECT_NE(TelemetryBuildText(player).find("chaos 17% (max 75%)"), std::string::npos) << TelemetryBuildText(player);

	TelemetryInit();
	TelemetryGameStarted(player, false);
	TelemetryFlush();
	const std::string folder = paths::PrefPath() + "poe2_logs" + DIRECTORY_SEPARATOR_STR;
	std::string latest;
	for (const std::string &file : ListFiles(folder.c_str())) {
		if (file.ends_with(".events") && file > latest)
			latest = file;
	}
	std::ifstream stream(folder + latest);
	std::stringstream events;
	events << stream.rdbuf();
	EXPECT_NE(events.str().find("\"chaosRes\":17,\"maxChaosRes\":75"), std::string::npos) << events.str();
}

/** @brief A monster of this kind in the level's first slot, with no modifiers. */
Monster MonsterOfKind(_monster_id kind)
{
	LevelMonsterTypes[0].type = kind;
	Monster monster {};
	monster.levelType = 0;
	monster.uniqueType = UniqueMonsterType::None;
	monster.maxHitPoints = monster.hitPoints = Fixed26_6::fromInt(100);
	return monster;
}

TEST_F(Poe2ChaosTest, MonstersResistChaosByTheirKind)
{
	// Acid beasts, spiders and other poison creatures 30%, demons 20%, Diablo 25%, the rest 0 (Curtis 2026-10-30).
	const struct {
		_monster_id kind;
		int chaos;
	} Kinds[] = {
		{ MT_NACID, 30 }, { MT_XACID, 30 }, { MT_SPIDLORD, 30 }, { MT_ARACHNON, 30 }, { MT_VENMTAIL, 30 }, { MT_DEFILER, 30 },
		{ MT_SUCCUBUS, 20 }, { MT_BALROG, 20 }, { MT_NBLACK, 20 }, { MT_STALKER, 20 }, { MT_NAKRUL, 20 },
		{ MT_DIABLO, 25 },
		{ MT_NZOMBIE, 0 }, { MT_WSKELAX, 0 }, { MT_RFALLSP, 0 }, { MT_NGOATMC, 0 }, { MT_COUNSLR, 0 }, { MT_LICH, 0 },
		{ MT_CLEAVER, 0 }, { MT_SKING, 0 }, { MT_HORNED, 0 }, { MT_GOLEM, 0 },
	};
	for (const auto &kind : Kinds) {
		EXPECT_EQ(MonsterTypeChaosResistance(kind.kind), kind.chaos) << kind.kind;
		const Monster monster = MonsterOfKind(kind.kind);
		EXPECT_EQ(MonsterResistance(monster, DamageType::Chaos), kind.chaos) << kind.kind;
		EXPECT_EQ(MonsterDamageTakenPercent(monster, DamageType::Chaos), 100 - kind.chaos) << kind.kind;
	}
	EXPECT_EQ(ResistanceMarksText(0, false, 20), "Resistances: Chaos 20%");
	EXPECT_EQ(ResistanceMarksText(RESIST_FIRE, false, 30), "Resistances: Fire 30%, Chaos 30%");

	// Not outside POE2 mode.
	gbIsPoe2 = false;
	EXPECT_EQ(MonsterResistance(MonsterOfKind(MT_NACID), DamageType::Chaos), 0);
	LoadMonsterChaosResistances();
	gbIsPoe2 = true;
	EXPECT_EQ(MonsterTypeChaosResistance(MT_NACID), 0);
	LoadMonsterChaosResistances();
	EXPECT_EQ(MonsterTypeChaosResistance(MT_NACID), 30);
}

TEST_F(Poe2ChaosTest, WaystonesAndMinionGearLeaveChaosResistanceAlone)
{
	Player &player = *MyPlayer;
	Monster demon = MonsterOfKind(MT_SUCCUBUS);
	OpenMapWith(player, MapDanger::MonsterResistance);
	EXPECT_GT(MonsterResistance(demon, DamageType::Fire), 0);
	EXPECT_EQ(MonsterResistance(demon, DamageType::Chaos), 20);
	CloseMap(player);

	// A raised minion keeps its kind's chaos resistance, and the owner's minion resistances don't add to it.
	demon.flags = MFLAG_GOLEM;
	demon.goalVar3 = 0;
	const int fire = MonsterBaseResistance(demon, DamageType::Fire);
	player.poe2.itemStats[static_cast<size_t>(Poe2ItemStat::MinionResistances)] = 30;
	EXPECT_EQ(MonsterBaseResistance(demon, DamageType::Fire), fire + 30);
	EXPECT_EQ(MonsterResistance(demon, DamageType::Chaos), 20);
}

TEST_F(Poe2ChaosTest, LowerResistAndExposureLeaveChaosResistanceAlone)
{
	Monster spider = MonsterOfKind(MT_SPIDLORD);
	spider.poe2.lowerResistTicks = 10;
	spider.poe2.exposureTicks = 10;
	spider.poe2.exposureElement = static_cast<uint8_t>(DamageType::Chaos);
	EXPECT_EQ(MonsterResistanceLowered(spider, DamageType::Chaos), 0);
	EXPECT_EQ(MonsterResistance(spider, DamageType::Chaos), 30);
	EXPECT_EQ(MonsterResistance(spider, DamageType::Fire), -LowerResistCursePercent);
	EXPECT_EQ(MonsterHitResistance(spider, DamageType::Chaos, 0), 30);
}

} // namespace
} // namespace devilution
