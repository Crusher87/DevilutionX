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
#include "poe2/support_table.hpp"
#include "poe2/support_gems.hpp"
#include "poe2/table_skills.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/weapon_skills.hpp"
#include "poe2/telemetry.hpp"
#include "missiles.h"
#include "monster.h"
#include "spells.h"
#include "storm/storm_net.hpp"
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

TEST_F(Poe2ChaosTest, ChaosResistanceFamilyRollsNaturally)
{
	const std::optional<uint8_t> family = FindModifierFamily("chaos_resistance");
	ASSERT_TRUE(family.has_value());
	const ModFamily &chaos = ModifierFamilies()[*family];
	EXPECT_EQ(chaos.effect, ModEffect::ChaosResistance);
	EXPECT_TRUE(chaos.enabled);
	EXPECT_TRUE(chaos.natural);
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
		// A normal ring, so only the implicit counts (rare rings roll chaos resistance too since step 14).
		SetLootRarity(ring, ITEM_QUALITY_NORMAL);
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
	EXPECT_EQ(HitTaken(player, DamageType::Cold), 60);

	// Outside POE2 mode chaos meets magic resistance, as acid always did.
	gbIsPoe2 = false;
	EXPECT_EQ(PlayerChaosResistance(player), 40);
	EXPECT_EQ(HitTaken(player, DamageType::Chaos), 60);
}

TEST_F(Poe2ChaosTest, LichesAndDiablosApocalypseDealChaos)
{
	Monster monster {};
	monster.ai = MonsterAIID::Lich;
	EXPECT_EQ(MonsterHitDamageType(&monster, MissileID::OrangeFlare, DamageType::Cold), DamageType::Chaos);
	monster.ai = MonsterAIID::ArchLich;
	EXPECT_EQ(MonsterHitDamageType(&monster, MissileID::YellowFlare, DamageType::Cold), DamageType::Chaos);
	monster.ai = MonsterAIID::Diablo;
	EXPECT_EQ(MonsterHitDamageType(&monster, MissileID::DiabloApocalypseBoom, DamageType::Physical), DamageType::Chaos);
	// Diablo's melee and the Succubi's Blood Star keep their types.
	EXPECT_EQ(MonsterHitDamageType(&monster, MissileID::Null, DamageType::Physical), DamageType::Physical);
	monster.ai = MonsterAIID::Succubus;
	EXPECT_EQ(MonsterHitDamageType(&monster, MissileID::BloodStar, DamageType::Cold), DamageType::Cold);
	// Traps and the hero's own skills on the same missiles too.
	EXPECT_EQ(MonsterHitDamageType(nullptr, MissileID::OrangeFlare, DamageType::Fire), DamageType::Fire);
	// Only in POE2 mode.
	gbIsPoe2 = false;
	monster.ai = MonsterAIID::Lich;
	EXPECT_EQ(MonsterHitDamageType(&monster, MissileID::OrangeFlare, DamageType::Cold), DamageType::Cold);
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
		OnMapMonsterSpellHit(player, Fixed26_6::fromInt(100), DamageType::Chaos);
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
	monster.talkMsg = TEXT_NONE;
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

TEST_F(Poe2ChaosTest, PoisonTicksMeetChaosResistance)
{
	EXPECT_EQ(ChaosTickDamage(MonsterOfKind(MT_NZOMBIE), 1000), 1000);
	EXPECT_EQ(ChaosTickDamage(MonsterOfKind(MT_NACID), 1000), 700);
	EXPECT_EQ(ChaosTickDamage(MonsterOfKind(MT_SUCCUBUS), 1000), 800);
	// Lower Resist leaves it alone; a tick always does something.
	Monster spider = MonsterOfKind(MT_SPIDLORD);
	spider.poe2.lowerResistTicks = 10;
	EXPECT_EQ(ChaosTickDamage(spider, 1000), 700);
	EXPECT_EQ(ChaosTickDamage(spider, 1), 1);
}

TEST_F(Poe2ChaosTest, OnlyTheStrongestPoisonCounts)
{
	Player &player = *MyPlayer;
	Monster monster = MonsterOfKind(MT_NZOMBIE);
	InflictPoison(player, monster, 40 * 100, PoisonTicks);
	EXPECT_EQ(monster.poe2.poisonDamagePerTick, 100);
	EXPECT_EQ(monster.poe2.poisonTicks, PoisonTicks);
	// A weaker poison is lost, not added.
	monster.poe2.poisonTicks = 10;
	InflictPoison(player, monster, 40 * 50, PoisonTicks);
	EXPECT_EQ(monster.poe2.poisonDamagePerTick, 100);
	EXPECT_EQ(monster.poe2.poisonTicks, 10);
	// A stronger one takes over, with its own duration.
	InflictPoison(player, monster, 40 * 150, PoisonTicks);
	EXPECT_EQ(monster.poe2.poisonDamagePerTick, 150);
	EXPECT_EQ(monster.poe2.poisonTicks, PoisonTicks);
	// A poison that says it stacks adds up, to its limit.
	Monster other = MonsterOfKind(MT_NZOMBIE);
	for (int i = 0; i < 7; i++)
		InflictPoison(player, other, 80 * 10, 80, 5);
	EXPECT_EQ(other.poe2.poisonDamagePerTick, 50);
}

TEST_F(Poe2ChaosTest, VenomCountsPhysicalAndChaosBeforeResistance)
{
	Player &player = *MyPlayer;
	MyPlayerId = 0;
	const SkillSlot skill = SkillSlot::ForAttack(WeaponSkill::Impale);
	ASSERT_NE(SkillTags(skill) & ST_Physical, 0U);
	player.poe2.skillSlots[0] = skill;
	player.poe2.slotSupports[0] = {};
	player.poe2.slotSupports[0][0] = SupportGem::Envenom;
	player.poe2.slotSupportTiers[0] = {};
	player.poe2.slotSupportTiers[0][0] = 3;
	ASSERT_EQ(player.poe2.supportTier(skill, SupportGem::Envenom), 3);

	Monster &monster = Monsters[0];
	monster = MonsterOfKind(MT_NACID);
	monster.mode = MonsterMode::Stand;
	{
		const SupportHitScope scope(player, skill);
		// 50 physical got through at 50% (an Acid Beast's own numbers don't matter here), 70 chaos at 70%.
		NoteHitDamageTakenPercent(monster, DamageType::Physical, 50);
		NoteSupportedDamage(monster, DamageType::Physical, Fixed26_6::fromInt(50));
		NoteHitDamageTakenPercent(monster, DamageType::Chaos, 70);
		NoteSupportedDamage(monster, DamageType::Chaos, Fixed26_6::fromInt(70));
		// Fire doesn't poison.
		NoteSupportedDamage(monster, DamageType::Fire, Fixed26_6::fromInt(500));
	}
	// 100 physical and 100 chaos before resistance: 20% of 200 a second for 2 seconds is 80, or 2 a tick.
	EXPECT_EQ(monster.poe2.poisonTicks, PoisonTicks);
	EXPECT_EQ(monster.poe2.poisonDamagePerTick, Fixed26_6::fromInt(80).raw() / PoisonTicks);
	// Each tick then meets the Acid Beast's 30% chaos resistance.
	EXPECT_EQ(ChaosTickDamage(monster, monster.poe2.poisonDamagePerTick), Fixed26_6::fromInt(2).raw() * 70 / 100);
	monster = {};
	player.poe2.slotSupports[0] = {};
	player.poe2.skillSlots[0] = {};
}

TEST_F(Poe2ChaosTest, MonsterPoisonIsTheStrongestOfPhysicalAndChaosHits)
{
	Player &player = *MyPlayer;
	MyPlayerId = 0;
	OpenMapWith(player, MapDanger::PoisonChance);
	// Fire and lightning hits never poison.
	for (uint32_t seed = 1; seed <= 200; seed++) {
		SetRndSeed(seed);
		OnMapMonsterSpellHit(player, Fixed26_6::fromInt(100), DamageType::Fire);
	}
	EXPECT_EQ(MapPoisonTicksLeft(), 0);
	// Many physical hits don't add up: the poison is one hit's worth, the strongest.
	player._pMaxHP = player._pMaxHPBase = Fixed26_6::fromInt(1000);
	player._pHitPoints = player._pHPBase = Fixed26_6::fromInt(1000);
	for (uint32_t seed = 1; seed <= 200; seed++) {
		SetRndSeed(seed);
		OnMapMonsterSpellHit(player, Fixed26_6::fromInt(100), DamageType::Physical);
	}
	ASSERT_GT(MapPoisonTicksLeft(), 0);
	while (MapPoisonTicksLeft() > 0)
		ProcessMapPlayer(player);
	EXPECT_LE(1000 - player._pHitPoints.whole(), 100 * MapPoisonPercentPerSecond / 100 * 2);
	CloseMap(player);
}

TEST_F(Poe2ChaosTest, WandAndStaffSkillsDealChaos)
{
	for (const WeaponSkill skill : { WeaponSkill::Soulrend, WeaponSkill::DarkPact, WeaponSkill::WitheringBolt, WeaponSkill::Decompose, WeaponSkill::WitheringRay }) {
		const TableSkillData &row = *FindTableSkill(skill);
		EXPECT_EQ(row.type, DamageType::Chaos) << row.name;
		EXPECT_NE(std::string_view(row.description).find("chaos"), std::string_view::npos) << row.name;
		const SkillSlot slot = SkillSlot::ForAttack(skill);
		const uint32_t tags = SkillTags(slot);
		EXPECT_NE(tags & ST_Chaos, 0U) << row.name;
		EXPECT_EQ(tags & ST_Physical, 0U) << row.name;
		EXPECT_EQ(tags & ST_Elemental, 0U) << row.name;
		// Venom fits chaos skills; the physical-only supports no longer do.
		EXPECT_TRUE(SupportGemCanSupport(SupportGem::Envenom, slot)) << row.name;
		EXPECT_FALSE(SupportGemCanSupport(SupportGem::InfusedFlames, slot)) << row.name;
		EXPECT_FALSE(SupportGemCanSupport(SupportGem::SerratedEdge, slot)) << row.name;
	}
	// Cobra Strike, Reap and Bone Bolt stay physical.
	for (const WeaponSkill skill : { WeaponSkill::ViperStrike, WeaponSkill::Reap, WeaponSkill::BoneBolt })
		EXPECT_EQ(FindTableSkill(skill)->type, DamageType::Physical) << FindTableSkill(skill)->name;
	EXPECT_NE(SkillTags(SkillSlot::ForAttack(WeaponSkill::ViperStrike)) & ST_Physical, 0U);
}

TEST_F(Poe2ChaosTest, WitheredRaisesChaosDamageOnly)
{
	Monster monster = MonsterOfKind(MT_NZOMBIE);
	monster.poe2.witherStacks = 10;
	monster.poe2.witherTicks = 40;
	EXPECT_EQ(WitheredDamageTakenPercent(monster, DamageType::Chaos), 50);
	for (const DamageType type : { DamageType::Physical, DamageType::Fire, DamageType::Lightning, DamageType::Cold }) {
		EXPECT_EQ(WitheredDamageTakenPercent(monster, type), 0);
		EXPECT_EQ(SupportHitDamagePercent(monster, type), 100);
		// Only its resistance: a zombie's kind resists cold.
		EXPECT_EQ(MonsterDamageTakenPercent(monster, type), 100 - MonsterResistance(monster, type));
	}
	EXPECT_EQ(SupportHitDamagePercent(monster, DamageType::Chaos), 150);
	EXPECT_EQ(MonsterDamageTakenPercent(monster, DamageType::Chaos), 150);
	// Poison ticks too, before chaos resistance.
	EXPECT_EQ(ChaosTickDamage(monster, 1000), 1500);
	Monster beast = MonsterOfKind(MT_NACID);
	beast.poe2.witherStacks = 4;
	EXPECT_EQ(ChaosTickDamage(beast, 1000), 1000 * 120 / 100 * 70 / 100);
}

TEST_F(Poe2ChaosTest, PhysicalOnlyRulesSkipChaosHits)
{
	// Amplify Damage and armour break raise physical damage only.
	Monster monster = MonsterOfKind(MT_NZOMBIE);
	monster.poe2.amplifyTicks = 40;
	monster.poe2.amplifyPercent = 30;
	monster.poe2.armourBreakTicks = 40;
	EXPECT_GT(MonsterDamageTakenPercent(monster, DamageType::Physical), 100);
	EXPECT_EQ(MonsterDamageTakenPercent(monster, DamageType::Chaos), 100);
}

TEST_F(Poe2ChaosTest, HauntAndLocustSwarmDrainWithChaos)
{
	for (const WeaponSkill skill : { WeaponSkill::Haunt, WeaponSkill::LocustSwarm }) {
		const TableSkillData &row = *FindTableSkill(skill);
		EXPECT_EQ(row.type, DamageType::Chaos) << row.name;
		const uint32_t tags = SkillTags(SkillSlot::ForAttack(skill));
		EXPECT_NE(tags & ST_Chaos, 0U) << row.name;
		EXPECT_NE(tags & ST_DamageOverTime, 0U) << row.name;
		EXPECT_EQ(tags & ST_Physical, 0U) << row.name;
	}
	// Cobra Strike stays a physical stab, and poisons instead of bleeding.
	const TableSkillData &cobra = *FindTableSkill(WeaponSkill::ViperStrike);
	EXPECT_EQ(cobra.type, DamageType::Physical);
	EXPECT_NE(cobra.flags & TF_Poison, 0U);
	EXPECT_EQ(cobra.flags & TF_Bleed, 0U);
	const uint32_t cobraTags = SkillTags(SkillSlot::ForAttack(WeaponSkill::ViperStrike));
	EXPECT_NE(cobraTags & ST_Physical, 0U);
	EXPECT_NE(cobraTags & ST_DamageOverTime, 0U);
}

TEST_F(Poe2ChaosTest, ChaosDrainMeetsResistanceAndWitheredBesideABleed)
{
	MyPlayerId = 0;
	SNetInitializeProvider(SELCONN_LOOPBACK, nullptr);
	Missiles.clear();
	Monster &monster = Monsters[0];
	monster = MonsterOfKind(MT_NACID);
	monster.maxHitPoints = monster.hitPoints = Fixed26_6::fromInt(1000);
	monster.poe2.witherStacks = 4;
	monster.poe2.witherTicks = 40;
	monster.poe2.chaosSource = 0;
	monster.poe2.chaosSkill = WeaponSkill::LocustSwarm;
	monster.poe2.chaosDamagePerTick = 1000;
	monster.poe2.chaosTicks = 5;
	// A bleed at the same time keeps its own damage.
	monster.poe2.bleedSource = 0;
	monster.poe2.bleedDamagePerTick = 64;
	monster.poe2.bleedTicks = 5;
	const int32_t before = monster.hitPoints.raw();
	ProcessUniqueMonsterAilments(monster);
	// 20% more from 4 Withered stacks, 30% less from the Acid Beast's chaos resistance.
	EXPECT_EQ(before - monster.hitPoints.raw(), 1000 * 120 / 100 * 70 / 100 + 64);
	EXPECT_EQ(monster.poe2.chaosTicks, 4);
	EXPECT_EQ(monster.poe2.bleedTicks, 4);
	monster = {};
	Missiles.clear();
}

TEST_F(Poe2ChaosTest, DespairIsAGemCurseNotAGrantedSkill)
{
	EXPECT_TRUE(IsGrantedSkill(WeaponSkill::TwinBolts));
	EXPECT_FALSE(IsGrantedSkill(WeaponSkill::Despair));
	EXPECT_EQ(LastGrantedSkill, WeaponSkill::TwinBolts);
	const TableSkillData &despair = *FindTableSkill(WeaponSkill::Despair);
	EXPECT_FALSE(IsGrantedTableSkill(despair));
	EXPECT_EQ(despair.curse, TableCurse::Despair);
	EXPECT_EQ(despair.level, 9);
	const std::optional<size_t> gem = SkillGemItemIndex(SkillSlot::ForAttack(WeaponSkill::Despair));
	ASSERT_TRUE(gem.has_value());
	EXPECT_EQ(std::string_view(AllItemsList[*gem].iName), "Despair Gem");
	// A gem skill costs mana, unlike a granted one with no supports.
	EXPECT_GT(WeaponSkillManaCost(WeaponSkill::Despair), 0);
	const uint32_t tags = SkillTags(SkillSlot::ForAttack(WeaponSkill::Despair));
	EXPECT_NE(tags & ST_Curse, 0U);
	EXPECT_EQ(tags & ST_Damage, 0U);
}

TEST_F(Poe2ChaosTest, DespairLowersChaosResistanceOnly)
{
	Monster beast = MonsterOfKind(MT_NACID);
	beast.poe2.despairTicks = 40;
	EXPECT_EQ(MonsterResistanceLowered(beast, DamageType::Chaos), DespairCursePercent);
	EXPECT_EQ(MonsterResistance(beast, DamageType::Chaos), 30 - DespairCursePercent);
	EXPECT_EQ(MonsterResistanceLowered(beast, DamageType::Fire), 0);
	Monster zombie = MonsterOfKind(MT_NZOMBIE);
	zombie.poe2.despairTicks = 40;
	EXPECT_EQ(MonsterResistance(zombie, DamageType::Chaos), -DespairCursePercent);
	EXPECT_EQ(ChaosTickDamage(zombie, 1000), 1000 * (100 + DespairCursePercent) / 100);
	// Unique monsters shrug off half, as they do Lower Resist.
	Monster unique = MonsterOfKind(MT_NZOMBIE);
	unique.uniqueType = UniqueMonsterType::Garbud;
	unique.poe2.despairTicks = 40;
	EXPECT_EQ(MonsterResistance(unique, DamageType::Chaos), -DespairCursePercent / 2);
	// Lower Resist leaves chaos alone, and Despair the elements.
	Monster cursed = MonsterOfKind(MT_NZOMBIE);
	cursed.poe2.lowerResistTicks = 40;
	EXPECT_EQ(MonsterResistance(cursed, DamageType::Chaos), 0);
	cursed.poe2.lowerResistTicks = 0;
	cursed.poe2.despairTicks = 40;
	EXPECT_EQ(MonsterResistance(cursed, DamageType::Fire), 0);
	// It runs out with the other curses.
	cursed.poe2.despairTicks = 1;
	ProcessTableCurses(cursed);
	EXPECT_EQ(cursed.poe2.despairTicks, 0);
	EXPECT_EQ(MonsterResistance(cursed, DamageType::Chaos), 0);
}

} // namespace
} // namespace devilution
