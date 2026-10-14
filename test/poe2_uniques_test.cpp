#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/loot.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/uniques.hpp"
#include "spells.h"
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

} // namespace
} // namespace devilution
