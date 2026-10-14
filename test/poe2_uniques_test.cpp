#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/belt_uniques.hpp"
#include "poe2/charms.hpp"
#include "poe2/flasks.hpp"
#include "poe2/loot.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/unique_effects.hpp"
#include "poe2/uniques.hpp"
#include "spells.h"
#include "tables/monstdat.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

class Poe2UniquesTest : public ::testing::Test {
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
		LoadPassiveTreeData();
		LoadModifierData();
		LoadMonsterData();
		LoadMissileData();
		Missiles.clear();
		LevelMonsterTypes[0].type = MT_NZOMBIE;

		Player &player = *MyPlayer;
		player._pClass = HeroClass::Warrior;
		player.setCharacterLevel(50);
		player._pBaseStr = player._pStrength = 250;
		player._pBaseMag = player._pMagic = 250;
		player._pBaseDex = player._pDexterity = 250;
		player._pBaseVit = player._pVitality = 100;
		player._pMaxHPBase = player._pHPBase = Fixed26_6::fromInt(200);
		player._pMaxManaBase = player._pManaBase = Fixed26_6::fromInt(100);
		// Matches the unarmed light armor graphic so recalculating stats loads no art.
		player._pgfxnum = 0;
	}

	void TearDown() override
	{
		Missiles.clear();
		gbIsPoe2 = false;
		LoadPassiveTreeData();
		LoadModifierData();
		UnloadModArchives();
	}

	static Item Make(std::string_view name, uint32_t seed = 1234)
	{
		const Poe2Unique *unique = FindPoe2Unique(name);
		Item item;
		if (unique == nullptr) {
			ADD_FAILURE() << name;
			return item;
		}
		EXPECT_TRUE(CreatePoe2Unique(*MyPlayer, item, unique->uid, seed)) << name;
		item._iIdentified = true;
		return item;
	}

	static Item MakeBase(std::string_view name)
	{
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iName != name)
				continue;
			Item item {};
			SetLootRarity(item, ITEM_QUALITY_NORMAL);
			SetRndSeed(5);
			SetupAllItems(*MyPlayer, item, static_cast<_item_indexes>(i), AdvanceRndSeed(), 30, 1, /*onlygood=*/false, /*pregen=*/false, 0, /*forceNotUnique=*/true);
			return item;
		}
		ADD_FAILURE() << name;
		return {};
	}

	static Monster MakeMonster()
	{
		Monster monster {};
		monster.uniqueType = UniqueMonsterType::None;
		monster.rndItemSeed = 1;
		monster.maxHitPoints = Fixed26_6::fromInt(100);
		monster.hitPoints = monster.maxHitPoints;
		return monster;
	}

	static Item MakeBelt(BeltUniqueId id)
	{
		const BeltUnique *unique = GetBeltUnique(id);
		Item item {};
		if (unique == nullptr || !BeltUniqueBase(*unique)) {
			ADD_FAILURE();
			return item;
		}
		InitializeItem(item, static_cast<_item_indexes>(*BeltUniqueBase(*unique)));
		item._iSeed = 4321;
		item.dwBuff |= CF_POE2;
		MakeBeltUnique(item, id);
		return item;
	}

	static int Wear(Item item, inv_body_loc slot)
	{
		Player &player = *MyPlayer;
		player.InvBody[slot] = std::move(item);
		CalcPlrInv(player, false);
		return player._pMaxHP.whole();
	}
};

TEST_F(Poe2UniquesTest, BundleUniquesJoinTheUniqueTable)
{
	const std::vector<Poe2Unique> &uniques = Poe2Uniques();
	ASSERT_GE(uniques.size(), 150U);
	std::set<std::string> names;
	int boss = 0;
	for (const Poe2Unique &unique : uniques) {
		EXPECT_TRUE(names.insert(unique.name).second) << unique.name;
		ASSERT_GE(unique.uid, 0);
		EXPECT_EQ(UniqueItems[unique.uid].UIName, unique.name);
		EXPECT_EQ(UniqueItems[unique.uid].mappingId, Poe2UniqueMappingId + (&unique - uniques.data()));
		EXPECT_EQ(Poe2UniqueFor(unique.uid), &unique);
		EXPECT_FALSE(unique.effects.empty()) << unique.name;
		if (!unique.isGlobal())
			boss++;
	}
	EXPECT_GE(boss, 20);
	// Diablo's uniques come first and keep their places.
	EXPECT_EQ(Poe2UniqueFor(0), nullptr);
	EXPECT_EQ(UniqueItems[0].mappingId, 0);

	// Loading again doesn't add them twice.
	const size_t count = UniqueItems.size();
	LoadModifierData();
	EXPECT_EQ(UniqueItems.size(), count);
}

TEST_F(Poe2UniquesTest, EveryUniqueBuildsAndRebuildsTheSame)
{
	for (const Poe2Unique &unique : Poe2Uniques()) {
		Item item;
		ASSERT_TRUE(CreatePoe2Unique(*MyPlayer, item, unique.uid, 777)) << unique.name;
		EXPECT_EQ(std::string_view(item._iIName), unique.name);
		EXPECT_EQ(item.IDidx, unique.baseItem);
		EXPECT_NE(item.dwBuff & CF_POE2_BUNDLE_UNIQUE, 0U);

		Item rebuilt;
		RecreateItem(*MyPlayer, rebuilt, item.IDidx, item._iCreateInfo, item._iSeed, item._ivalue, item.dwBuff);
		EXPECT_EQ(rebuilt._iUid, unique.uid) << unique.name;
		EXPECT_EQ(rebuilt._iPLHP, item._iPLHP) << unique.name;
		EXPECT_EQ(rebuilt._iPLDam, item._iPLDam) << unique.name;
		EXPECT_EQ(rebuilt._iPLAC, item._iPLAC) << unique.name;
		EXPECT_EQ(rebuilt._iPoe2Stats, item._iPoe2Stats) << unique.name;
		EXPECT_EQ(Poe2UniqueText(rebuilt), Poe2UniqueText(item)) << unique.name;
	}
}

TEST_F(Poe2UniquesTest, ValuesRollInTheirRangesFromTheSeed)
{
	std::set<int> damage;
	for (uint32_t seed = 1; seed <= 40; seed++) {
		const Item axe = Make("Ashen Covenant", seed);
		for (const auto &[effect, value] : RollUniqueEffects(axe)) {
			EXPECT_GE(value, effect->min);
			EXPECT_LE(value, effect->max);
		}
		damage.insert(axe._iPLDam);
	}
	EXPECT_GT(damage.size(), 5U);
	const std::vector<std::string> lines = Poe2UniqueText(Make("Ashen Covenant"));
	ASSERT_EQ(lines.size(), 5U);
	EXPECT_NE(lines[2].find("Converted to Fire"), std::string::npos);
	EXPECT_EQ(lines[4], "You cannot deal Cold Damage");
}

TEST_F(Poe2UniquesTest, WornRulesAddUp)
{
	Player &player = *MyPlayer;
	const int life = Wear({}, INVLOC_AMULET);
	Wear(Make("Astral Debt"), INVLOC_AMULET);
	EXPECT_EQ(player.poe2.itemStats[static_cast<size_t>(Poe2ItemStat::SpellSkillLevels)], 3);
	EXPECT_EQ(player.poe2.rule(UniqueRule::ManaCost), 35);

	Wear(Make("Chain of Names"), INVLOC_AMULET);
	EXPECT_EQ(player.poe2.rule(UniqueRule::LowLifeSkillLevels), 2);
	// 20% less maximum life, after the amulet's attributes.
	EXPECT_LT(player._pMaxHP.whole(), life);

	Wear({}, INVLOC_AMULET);
	EXPECT_EQ(player.poe2.rule(UniqueRule::ManaCost), 0);
	EXPECT_EQ(player._pMaxHP.whole(), life);
}

TEST_F(Poe2UniquesTest, ResistanceRulesMoveTheCaps)
{
	Player &player = *MyPlayer;
	Wear(Make("Circle of Ash"), INVLOC_RING_LEFT);
	EXPECT_EQ(player.poe2.maxResistances[2], MaxResistance - 5);
	EXPECT_EQ(player.poe2.maxResistances[0], MaxResistance);
}

TEST_F(Poe2UniquesTest, CritsAndSpellRules)
{
	Player &player = *MyPlayer;
	EXPECT_EQ(CritDamagePercent(player), 100 + BaseCritBonusPercent);
	Wear(Make("Wolfsplitter"), INVLOC_HAND_LEFT);
	const int bonus = player.poe2.rule(UniqueRule::CritDamageBonus);
	EXPECT_GE(bonus, 17);
	EXPECT_EQ(CritDamagePercent(player), 100 + BaseCritBonusPercent + bonus);
	Wear({}, INVLOC_HAND_LEFT);

	Wear(Make("Astral Debt"), INVLOC_AMULET);
	EXPECT_EQ(UniqueManaCostPercent(player), 135);
	Wear(Make("Glass Thought"), INVLOC_AMULET);
	// Increased spell damage plus 20% of maximum mana.
	const int spell = UniqueSpellDamagePercent(player, SpellID::Firebolt);
	EXPECT_EQ(spell, 100 + player.poe2.rule(UniqueRule::SpellDamage) + player._pMaxMana.whole() * 20 / 100);
}

TEST_F(Poe2UniquesTest, EverySixthSpellRepeatsThreeTimes)
{
	Player &player = *MyPlayer;
	Wear(Make("Oracle's Lens"), INVLOC_HAND_RIGHT);
	for (int i = 0; i < 5; i++)
		OnSpellCast(player, SpellID::Firebolt, { 10, 12 }, 3);
	EXPECT_EQ(player.poe2.uniqueState.repeatsLeft, 0);
	OnSpellCast(player, SpellID::Firebolt, { 10, 12 }, 3);
	EXPECT_EQ(player.poe2.uniqueState.repeatsLeft, 3);
	EXPECT_EQ(player.poe2.uniqueState.repeatPercent, 45);
	EXPECT_EQ(player.poe2.uniqueState.repeatSpell, SpellID::Firebolt);
	EXPECT_TRUE(player.poe2.uniqueState.repeatManaDrain);
	// A repeat being cast doesn't count as a new spell.
	player.poe2.uniqueState.castingRepeat = true;
	OnSpellCast(player, SpellID::Firebolt, { 10, 12 }, 3);
	EXPECT_EQ(player.poe2.uniqueState.spells, 6);
	player.poe2.uniqueState.castingRepeat = false;
}

TEST_F(Poe2UniquesTest, HitsTakenAndRecovery)
{
	Player &player = *MyPlayer;
	const Fixed26_6 hit = Fixed26_6::fromInt(100);
	EXPECT_EQ(ApplyUniqueHitTaken(player, hit, DamageType::Physical), hit);

	Wear(Make("Thoughtcage"), INVLOC_HEAD);
	player.poe2.uniqueState.sinceMovedTicks = 0;
	EXPECT_EQ(ApplyUniqueHitTaken(player, hit, DamageType::Physical), Fixed26_6::fromInt(110));
	player.poe2.uniqueState.sinceMovedTicks = 100;
	EXPECT_EQ(ApplyUniqueHitTaken(player, hit, DamageType::Physical), hit);
	player.poe2.uniqueState.stillTicks = 10;
	EXPECT_EQ(UniqueDefencePercent(player), 125);

	Wear(Make("Mask of Returning"), INVLOC_HEAD);
	EXPECT_EQ(LifeRecoveryPercent(player, false), 100);
	EXPECT_EQ(LifeRecoveryPercent(player, true), 75);

	Wear(Make("Red Harvest"), INVLOC_HAND_LEFT);
	EXPECT_EQ(LifeRecoveryPercent(player, true), 75 * 75 / 100);
	player._pHitPoints = player._pMaxHP / 2;
	EXPECT_EQ(UniqueAttackSpeedPercent(player), 12);
	player._pHitPoints = player._pMaxHP / 10;
	EXPECT_EQ(UniqueAttackSpeedPercent(player), 20);
	player.poe2.uniqueState.lifeLost.fill(0);
	ProcessUniqueRules(player);
	EXPECT_TRUE(AttacksCannotBeEvaded(player));

	// Losing 30% of maximum life within 2 seconds: the mask recovers 15%.
	const Fixed26_6 before = player._pHitPoints;
	player.poe2.uniqueState.lifeLost[1] = player._pMaxHP.raw() * 3 / 10;
	ProcessUniqueRules(player);
	EXPECT_EQ(player._pHitPoints.raw(), before.raw() + player._pMaxHP.raw() * 15 / 100);
	EXPECT_EQ(player.poe2.uniqueState.bigHitCooldown, 40);
}

TEST_F(Poe2UniquesTest, SpellsCanSpendEnergyShield)
{
	Player &player = *MyPlayer;
	Wear(Make("Cold Logic"), INVLOC_HAND_RIGHT);
	player.poe2.maxEnergyShield = 50;
	player.poe2.energyShield = Fixed26_6::fromInt(20).raw();
	EXPECT_EQ(SpendableEnergyShield(player), Fixed26_6::fromInt(20));
	EXPECT_EQ(PaySpellCostWithEnergyShield(player, Fixed26_6::fromInt(30)), Fixed26_6::fromInt(10));
	EXPECT_EQ(player.poe2.energyShield, 0);
	EXPECT_EQ(player.poe2.uniqueState.shieldSpent, Fixed26_6::fromInt(20).raw());
}

TEST_F(Poe2UniquesTest, MirrorRingDoublesTheOtherImplicit)
{
	Player &player = *MyPlayer;
	Wear(MakeBase("Coral Ring"), INVLOC_RING_LEFT);
	const int withRing = player._pMaxHP.whole();
	const int implicitLife = withRing - Wear({}, INVLOC_RING_LEFT);
	ASSERT_GE(implicitLife, 20);

	Wear(Make("Nothing Ends"), INVLOC_RING_RIGHT);
	const int withMirror = Wear({}, INVLOC_RING_LEFT);
	EXPECT_EQ(Wear(MakeBase("Coral Ring"), INVLOC_RING_LEFT) - withMirror, 2 * implicitLife);
}

TEST_F(Poe2UniquesTest, AilmentRules)
{
	Player &player = *MyPlayer;
	Monster monster = MakeMonster();
	Wear(Make("Tyrant's Spark"), INVLOC_HAND_LEFT);
	TryInflictAilment(monster, player, DamageType::Lightning, Fixed26_6::fromInt(60));
	EXPECT_EQ(monster.poe2.electrocuteTicks, 0);
	TryInflictAilment(monster, player, DamageType::Lightning, Fixed26_6::fromInt(60));
	EXPECT_EQ(monster.poe2.electrocuteTicks, ElectrocuteTicks);
	EXPECT_TRUE(IsMonsterImmobilized(monster));
	EXPECT_EQ(MonsterDamageTakenPercent(monster, DamageType::Lightning), 115);
	EXPECT_EQ(MonsterDamageTakenPercent(monster, DamageType::Fire), 100);

	Wear(Make("Winterstring"), INVLOC_HAND_LEFT);
	Monster cold = MakeMonster();
	TryInflictAilment(cold, player, DamageType::Fire, Fixed26_6::fromInt(60));
	EXPECT_EQ(cold.poe2.igniteTicks, 0);
	TryInflictAilment(cold, player, DamageType::Magic, Fixed26_6::fromInt(60));
	EXPECT_GT(cold.poe2.freezeTicks, 0);
	EXPECT_EQ(player.poe2.uniqueState.storedChill, 30);
}

TEST_F(Poe2UniquesTest, EveryBeltUniqueBuildsAndRebuildsTheSame)
{
	ASSERT_EQ(BeltUniques().size(), static_cast<size_t>(BeltUniqueId::Count) - 1);
	for (const BeltUnique &unique : BeltUniques()) {
		const Item item = MakeBelt(unique.id);
		ASSERT_EQ(GetBeltUnique(item), &unique) << unique.name;
		EXPECT_EQ(std::string_view(item._iIName), unique.name);
		EXPECT_EQ(item._iMagical, ITEM_QUALITY_UNIQUE);
		EXPECT_EQ(unique.kind == BeltUniqueKind::Charm, IsCharm(item)) << unique.name;
		EXPECT_FALSE(BeltUniqueText(item).empty());
		if (unique.effectMax > 0) {
			EXPECT_GE(BeltUniqueEffect(item), unique.effectMin);
			EXPECT_LE(BeltUniqueEffect(item), unique.effectMax);
		}

		Item rebuilt;
		RecreateItem(*MyPlayer, rebuilt, item.IDidx, item._iCreateInfo, item._iSeed, item._ivalue, item.dwBuff);
		EXPECT_EQ(GetBeltUnique(rebuilt), &unique) << unique.name;
		EXPECT_EQ(std::string_view(rebuilt._iIName), unique.name);
	}
}

TEST_F(Poe2UniquesTest, UniqueFlasks)
{
	Player &player = *MyPlayer;
	const Item deepWell = MakeBelt(BeltUniqueId::DeepWell);
	EXPECT_EQ(FlaskChargesPerUseFor(deepWell), FlaskChargesPerUse * 3 / 2);
	EXPECT_EQ(GetFlaskTier(MakeBelt(BeltUniqueId::Heartglass)), NumFlaskTiers - 1);
	EXPECT_EQ(GetFlaskTier(MakeBelt(BeltUniqueId::RedMercy)), 0U);

	// Heartglass saves the hero once while it's at least 80% full.
	player.SpdList[0] = MakeBelt(BeltUniqueId::Heartglass);
	player.poe2.flaskCharges[0] = MaxFlaskCharges;
	player._pHitPoints = Fixed26_6::fromInt(0);
	EXPECT_TRUE(TryHeartglassSave(player));
	EXPECT_EQ(player._pHitPoints, Fixed26_6::fromInt(1));
	EXPECT_EQ(player.poe2.flaskCharges[0], 0);
	player.poe2.flaskCharges[0] = MaxFlaskCharges;
	EXPECT_FALSE(TryHeartglassSave(player));

	// Deep Well makes the next skill free.
	Wear({}, INVLOC_AMULET);
	player.SpdList[0] = deepWell;
	player.poe2.flaskCharges[0] = MaxFlaskCharges;
	player._pMana = player._pMaxMana / 2;
	ASSERT_TRUE(UseFlask(player, 0));
	EXPECT_TRUE(TakeFreeSkill(player));
	EXPECT_FALSE(TakeFreeSkill(player));
	player.SpdList[0] = {};
}

TEST_F(Poe2UniquesTest, UniqueCharms)
{
	Player &player = *MyPlayer;
	player.SpdList[0] = MakeBelt(BeltUniqueId::SecondBell);
	const int cost = CharmChargesPerUseFor(player.SpdList[0]);
	player.SpdList[0]._iCharges = MaxCharmCharges;
	int spent = 0;
	for (int i = 0; i < 3; i++) {
		const int before = player.SpdList[0]._iCharges;
		player.poe2.resistCharmTicks.fill(0);
		EXPECT_EQ(ResistanceAgainstHit(player, DamageType::Lightning, 0), ResistCharmBonus);
		spent += before - player.SpdList[0]._iCharges;
		player.SpdList[0]._iCharges = MaxCharmCharges;
	}
	// Every third activation is free.
	EXPECT_EQ(spent, 2 * cost);
	EXPECT_GT(player.poe2.resistCharmTicks[1], ResistCharmTicks);

	// Boss charms work from the belt.
	player.SpdList[0] = MakeBelt(BeltUniqueId::FrozenStep);
	const int resist = BeltUniqueResistance(player.SpdList[0]);
	EXPECT_GE(resist, 8);
	CalcPlrInv(player, false);
	EXPECT_EQ(player.poe2.rule(UniqueRule::FreezeStoresChill), 30);
	EXPECT_EQ(player.poe2.rule(UniqueRule::CannotIgnite), 1);
	player.SpdList[0] = {};
	CalcPlrInv(player, false);
	EXPECT_EQ(player.poe2.rule(UniqueRule::CannotIgnite), 0);
}

} // namespace
} // namespace devilution
