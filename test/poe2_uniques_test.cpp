#include <algorithm>
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
#include "poe2/bases.hpp"
#include "poe2/belt_uniques.hpp"
#include "poe2/boss_loot.hpp"
#include "poe2/charms.hpp"
#include "poe2/flasks.hpp"
#include "poe2/loot.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/unique_effects.hpp"
#include "poe2/uniques.hpp"
#include "poe2/weapon_kinds.hpp"
#include "spells.h"
#include "tables/monstdat.h"
#include "tables/playerdat.hpp"
#include "utils/is_of.hpp"

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
		// High enough for every unique: gear needs its character level, and the latest uniques drop at area level 80.
		player.setCharacterLevel(80);
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

	static int FindBase(std::string_view name)
	{
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iName == name && AllItemsList[i].iMiscId != IMISC_UNIQUE)
				return static_cast<int>(i);
		}
		ADD_FAILURE() << name;
		return -1;
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

TEST_F(Poe2UniquesTest, UniquesOfRetiredBasesMoveAndSavedOnesStay)
{
	// Curtis 2026-10-27: the heavy, swift and crossbow bases no longer drop, so their uniques drop on the balanced base of
	// their class and tier, a crossbow's on the bow of its tier. The caster weapons rework retired the Focused and Quick
	// staves and sceptres too: their uniques went to the new staves, wands and sceptres (plan 4.3).
	const auto retired = [](int index) {
		const BaseMeta *meta = BaseMetaFor(index);
		if (IsAnyOf(WeaponKindOf(index), WeaponKind::LegacyStaff, WeaponKind::LegacySceptre) && AllItemsList[index].dropRate == 0)
			return true;
		return meta != nullptr && (meta->identity == "heavy" || meta->identity == "swift" || meta->itemClass == "crossbow");
	};
	int moved = 0;
	for (const Poe2Unique &unique : Poe2Uniques()) {
		EXPECT_FALSE(retired(unique.baseItem)) << unique.name;
		if (unique.savedBaseItem != unique.baseItem) {
			EXPECT_TRUE(retired(unique.savedBaseItem)) << unique.name;
			moved++;
		}
	}
	EXPECT_EQ(moved, 61 + 22);
	const Poe2Unique &mercy = *FindPoe2Unique("Mercy's End");
	EXPECT_EQ(mercy.baseItem, FindBase("Dark Blade"));
	EXPECT_EQ(mercy.savedBaseItem, FindBase("Heavy Imperial Edge"));
	EXPECT_EQ(FindPoe2Unique("Dragon's Argument")->baseItem, FindBase("Warmonger Bow"));
	EXPECT_EQ(FindPoe2Unique("Dragon's Argument")->savedBaseItem, FindBase("Dragonspine Crossbow"));

	for (const Poe2Unique &unique : Poe2Uniques()) {
		// A base lists the uniques saved on it first, in table order, as before they moved, so saved offsets still hold.
		std::vector<int> saved;
		for (const Poe2Unique &other : Poe2Uniques()) {
			if (other.savedBaseItem == unique.savedBaseItem)
				saved.push_back(other.uid);
		}
		const std::vector<int> onSaved = Poe2UniquesOnBase(unique.savedBaseItem);
		ASSERT_GE(onSaved.size(), saved.size());
		EXPECT_TRUE(std::equal(saved.begin(), saved.end(), onSaved.begin())) << unique.name;
		if (unique.savedBaseItem == unique.baseItem)
			continue;

		// An item saved on its old base still rebuilds as it.
		Item item;
		ASSERT_TRUE(CreatePoe2Unique(*MyPlayer, item, unique.uid, 777)) << unique.name;
		const int offset = static_cast<int>(std::find(onSaved.begin(), onSaved.end(), unique.uid) - onSaved.begin());
		Item rebuilt;
		RecreateItem(*MyPlayer, rebuilt, static_cast<_item_indexes>(unique.savedBaseItem), item._iCreateInfo, item._iSeed, item._ivalue,
		    (item.dwBuff & ~static_cast<uint32_t>(CF_UIDOFFSET)) | ((static_cast<uint32_t>(offset) << 1) & CF_UIDOFFSET));
		EXPECT_EQ(rebuilt._iUid, unique.uid) << unique.name;
		EXPECT_EQ(std::string_view(rebuilt._iIName), unique.name);
	}
}

TEST_F(Poe2UniquesTest, CasterUniquesMovedToTheNewCasterWeapons)
{
	struct Move {
		std::string_view unique;
		std::string_view base;
		std::string_view saved;
	};
	// Plan 4.3: the Quick sceptres' uniques are spell items and go to wands, the Focused ones to sceptres.
	constexpr Move Moves[] = {
		{ "Worldroot", "Spriggan Staff", "Focused Ashen Staff" },
		{ "Glass Thesis", "Roaring Staff", "Focused Worldroot Staff" },
		{ "Glass Thought", "Siphoning Wand", "Quick Bronze Sceptre" },
		{ "The Empty Word", "Dueling Wand", "Quick Sovereign Sceptre" },
		{ "Saint's Debt", "Lupine Sceptre", "Focused Bronze Sceptre" },
		{ "Bone Choir", "Devotional Sceptre", "Focused Saint's Sceptre" },
		{ "Tyrant's Spark", "Wrath Sceptre", "Quick Saint's Sceptre" },
	};
	for (const Move &move : Moves) {
		const Poe2Unique *unique = FindPoe2Unique(move.unique);
		ASSERT_NE(unique, nullptr) << move.unique;
		EXPECT_EQ(unique->baseItem, FindBase(move.base)) << move.unique;
		EXPECT_EQ(unique->savedBaseItem, FindBase(move.saved)) << move.unique;
	}
	int onCasterWeapons = 0;
	for (const Poe2Unique &unique : Poe2Uniques()) {
		EXPECT_FALSE(IsLegacyCaster(MakeBase(AllItemsList[unique.baseItem].iName))) << unique.name << " still on a retired base";
		onCasterWeapons += IsCasterWeapon(unique.baseItem) ? 1 : 0;
	}
	EXPECT_EQ(onCasterWeapons, 22);
	// Two on one new base list in table order.
	const std::vector<int> hallowed = Poe2UniquesOnBase(FindBase("Hallowed Sceptre"));
	ASSERT_EQ(hallowed.size(), 2U);
	EXPECT_EQ(hallowed[0], FindPoe2Unique("Sunbound")->uid);
	EXPECT_EQ(hallowed[1], FindPoe2Unique("Reliquary of Breath")->uid);
}

/** @brief The unique table index of a Diablo unique. */
int DiabloUid(std::string_view name)
{
	for (size_t i = 0; i < UniqueItems.size(); i++) {
		if (std::string_view(UniqueItems[i].UIName) == name)
			return static_cast<int>(i);
	}
	ADD_FAILURE() << name;
	return -1;
}

TEST_F(Poe2UniquesTest, DiabloStaffUniquesKeepToTheirHomes)
{
	const int gleamsong = DiabloUid("Gleamsong");
	const int protector = DiabloUid("The Protector");
	EXPECT_EQ(DiabloUniqueHome(gleamsong), WeaponKind::CasterStaff);
	EXPECT_EQ(DiabloUniqueHome(DiabloUid("Naj's Puzzler")), WeaponKind::CasterStaff);
	EXPECT_EQ(DiabloUniqueHome(DiabloUid("Mindcry")), WeaponKind::CasterStaff);
	for (const std::string_view name : { "Rod of Onan", "Staff of Shadows", "Immolator", "Storm Spire", "The Protector", "Thundercall" })
		EXPECT_EQ(DiabloUniqueHome(DiabloUid(name)), WeaponKind::Quarterstaff) << name;
	EXPECT_EQ(DiabloUniqueHome(0), WeaponKind::None) << "The Butcher's Cleaver";

	// Only a new caster row turns one away; legacy rows, Diablo's staves and quarterstaves never do.
	const int ashen = FindBase("Ashen Staff");
	const int wrapped = FindBase("Fine Wrapped Quarterstaff");
	EXPECT_FALSE(DiabloUniqueFitsRow(protector, ashen));
	EXPECT_TRUE(DiabloUniqueFitsRow(gleamsong, ashen));
	EXPECT_TRUE(DiabloUniqueFitsRow(protector, wrapped));
	EXPECT_TRUE(DiabloUniqueFitsRow(gleamsong, wrapped));
	EXPECT_TRUE(DiabloUniqueFitsRow(protector, FindBase("Focused Pilgrim Staff")));
	EXPECT_TRUE(DiabloUniqueFitsRow(protector, FindBase("Short Staff")));

	// A drop picks a base of its home's kind: the caster ones a caster staff, the others a quarterstaff, as before.
	EXPECT_EQ(Poe2DiabloUniqueBase(gleamsong), static_cast<_item_indexes>(ashen));
	EXPECT_EQ(Poe2DiabloUniqueBase(DiabloUid("Naj's Puzzler")), static_cast<_item_indexes>(FindBase("Pyrophyte Staff")));
	EXPECT_EQ(Poe2DiabloUniqueBase(DiabloUid("Mindcry")), static_cast<_item_indexes>(FindBase("Spriggan Staff")));
	EXPECT_EQ(Poe2DiabloUniqueBase(protector), static_cast<_item_indexes>(wrapped));
	EXPECT_EQ(Poe2DiabloUniqueBase(DiabloUid("Thundercall")), static_cast<_item_indexes>(FindBase("Steelpoint Quarterstaff")));
	EXPECT_EQ(Poe2DiabloUniqueBase(DiabloUid("Rod of Onan")), static_cast<_item_indexes>(FindBase("Slicing Quarterstaff")));

	// Rolled on an Ashen Staff, a unique is never The Protector; on a quarterstaff of the type it can be.
	const auto rollUniques = [](int base) {
		std::set<int> uids;
		for (int level = 8; level <= 30; level++) {
			for (uint32_t seed = 1; seed <= 80; seed++) {
				Item item = {};
				SetRndSeed(seed * 2654435761U + static_cast<uint32_t>(level));
				SetupAllItems(*MyPlayer, item, static_cast<_item_indexes>(base), AdvanceRndSeed(), level, 15, /*onlygood=*/true, /*pregen=*/false);
				if (item._iMagical == ITEM_QUALITY_UNIQUE)
					uids.insert(item._iUid);
			}
		}
		return uids;
	};
	const std::set<int> onAshen = rollUniques(ashen);
	EXPECT_TRUE(onAshen.contains(gleamsong));
	EXPECT_FALSE(onAshen.contains(protector));
	const std::set<int> onWrapped = rollUniques(wrapped);
	EXPECT_TRUE(onWrapped.contains(gleamsong));
	EXPECT_TRUE(onWrapped.contains(protector));

	// An Orb of Chance on an Ashen Staff makes no quarterstaff unique either.
	Item staff = MakeBase("Ashen Staff");
	int gleamsongs = 0;
	for (uint32_t roll = 0; roll < 3000; roll += 7) {
		Item unique;
		if (CreateChanceUnique(*MyPlayer, staff, unique, roll * 2654435761U) != ChanceResult::Made)
			continue;
		EXPECT_NE(unique._iUid, protector);
		gleamsongs += unique._iUid == gleamsong ? 1 : 0;
	}
	EXPECT_GT(gleamsongs, 0);

	// Outside POE2 there are no homes.
	gbIsPoe2 = false;
	EXPECT_EQ(DiabloUniqueHome(protector), WeaponKind::None);
	EXPECT_TRUE(DiabloUniqueFitsRow(protector, ashen));
	gbIsPoe2 = true;
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
	// Shock is a chance (1% per 4% of the monster's life), so hit until it lands.
	for (int i = 0; i < 200 && monster.poe2.shockTicks == 0; i++)
		TryInflictAilment(monster, player, DamageType::Lightning, Fixed26_6::fromInt(60));
	EXPECT_EQ(monster.poe2.electrocuteTicks, 0);
	for (int i = 0; i < 200 && monster.poe2.electrocuteTicks == 0; i++)
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
		// Belt uniques keep unique id 0 (The Butcher's Cleaver): their name and lines must still be their own.
		EXPECT_EQ(item.getName().str(), unique.name);
		EXPECT_TRUE(Poe2UniqueText(item).empty()) << unique.name;
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
	EXPECT_EQ(GetFlaskTier(MakeBelt(BeltUniqueId::RedMercy)), 2U);
	// A unique flask is on the base a plain flask of its drop level has.
	for (const BeltUnique &unique : BeltUniques()) {
		if (unique.kind != BeltUniqueKind::Charm)
			EXPECT_EQ(unique.flaskTier, FlaskTierForLevel(unique.dropLevel)) << unique.name;
	}

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

TEST_F(Poe2UniquesTest, BossTablesPointAtRealBossesAndUniques)
{
	ASSERT_EQ(BossLootTables().size(), 11U);
	for (const BossLootTable &table : BossLootTables()) {
		EXPECT_TRUE(table.chancePercent == 18 || table.chancePercent == 100) << table.id;
		EXPECT_FALSE(table.pool.empty()) << table.id;
		for (const BossUniqueEntry &entry : table.pool) {
			const Poe2Unique *unique = FindPoe2Unique(entry.unique);
			const BeltUnique *belt = FindBeltUnique(entry.unique);
			ASSERT_TRUE(unique != nullptr || belt != nullptr) << entry.unique;
			// Boss uniques only drop from their own boss.
			EXPECT_EQ(unique != nullptr ? unique->pool : belt->pool, table.id) << entry.unique;
		}
		for (const std::string_view monster : table.monsters) {
			const bool found = std::any_of(UniqueMonstersData.begin(), UniqueMonstersData.end(), [&](const UniqueMonsterData &data) { return data.mName == monster; });
			EXPECT_TRUE(found) << monster;
		}
	}
	Monster butcher = MakeMonster();
	for (size_t i = 0; i < UniqueMonstersData.size(); i++) {
		if (UniqueMonstersData[i].mName == "The Butcher")
			butcher.uniqueType = static_cast<UniqueMonsterType>(i);
	}
	ASSERT_NE(BossLootTableFor(butcher), nullptr);
	EXPECT_EQ(BossLootTableFor(butcher)->id, "saint_of_iron");
	EXPECT_EQ(BossLootTableFor(MakeMonster()), nullptr);
}

TEST_F(Poe2UniquesTest, UniquesNeedTheHigherLevel)
{
	// A unique on a POE2 base needs the higher of the base's level and its own drop level.
	EXPECT_EQ(ItemRequiredLevel(Make("Circle of Ash")), 12) << "Sapphire Ring 12, dropping from 8";
	EXPECT_EQ(ItemRequiredLevel(Make("Astral Debt")), 80) << "Azure Amulet 1, dropping from 80";
	EXPECT_EQ(ItemRequiredLevel(Make("Red Harvest")), 26) << "Crescent Axe 26, dropping from 24";

	const auto diabloUnique = [](std::string_view base, std::string_view name) {
		Item item = MakeBase(base);
		for (size_t i = 0; i < UniqueItems.size(); i++) {
			if (std::string_view(UniqueItems[i].UIName) == name) {
				item._iMagical = ITEM_QUALITY_UNIQUE;
				item._iUid = static_cast<int>(i);
				return item;
			}
		}
		ADD_FAILURE() << name;
		return item;
	};
	// A Diablo unique on a POE2 base takes its table's level when that is higher.
	EXPECT_EQ(ItemRequiredLevel(diabloUnique("Crude Bow", "The Needler")), 2);
	EXPECT_EQ(ItemRequiredLevel(diabloUnique("Crude Bow", "The Rift Bow")), 1);
	// On an old Diablo base it needs no level.
	Item cleaver;
	InitializeItem(cleaver, IDI_CLEAVER);
	cleaver._iMagical = ITEM_QUALITY_UNIQUE;
	cleaver._iUid = 0;
	EXPECT_EQ(ItemRequiredLevel(cleaver), 0);
}

} // namespace
} // namespace devilution
