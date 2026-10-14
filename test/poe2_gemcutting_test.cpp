#include <set>
#include <string>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/gemcutting.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/support_gems.hpp"
#include "spells.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

class Poe2GemcuttingTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayer = &Players[0];
		*MyPlayer = {};
		gbIsSpawn = false;
		gbIsMultiplayer = false;
		gbIsHellfire = true;
		gbIsPoe2 = true;
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		LoadItemData();
		LoadSpellData();
		LoadPlayerDataFiles();
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		UnloadModArchives();
	}

	static Item MakeUncut(item_misc_id miscId, int level)
	{
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iMiscId != miscId)
				continue;
			Item item;
			InitializeItem(item, static_cast<_item_indexes>(i));
			item._iCreateInfo = static_cast<uint16_t>(level);
			return item;
		}
		ADD_FAILURE() << "no uncut gem item";
		return {};
	}
};

TEST_F(Poe2GemcuttingTest, CatalogHoldsEveryGem)
{
	const std::vector<GemEntry> &catalog = GemCatalog();
	size_t skills = 0;
	size_t supports = 0;
	int lastLevel = 0;
	for (const GemEntry &gem : catalog) {
		EXPECT_GE(gem.level, lastLevel) << "the catalog is sorted by level";
		lastLevel = gem.level;
		EXPECT_FALSE(GemName(gem).empty());
		if (gem.support != SupportGem::None)
			supports++;
		else
			skills++;

		// Every gem shows up in at least one tab besides All Gems.
		bool inTab = false;
		for (size_t i = 0; i < static_cast<size_t>(GemCategory::All); i++)
			inTab = inTab || GemInCategory(gem, static_cast<GemCategory>(i));
		EXPECT_TRUE(inTab) << GemName(gem);
		EXPECT_TRUE(GemInCategory(gem, GemCategory::All));

		Item item;
		ASSERT_TRUE(CreateGemItem(item, gem)) << GemName(gem);
		EXPECT_EQ(static_cast<size_t>(item.IDidx), gem.itemIndex);
	}
	EXPECT_EQ(skills, 64U);
	EXPECT_EQ(supports, static_cast<size_t>(SupportGem::Count) - 1);
}

TEST_F(Poe2GemcuttingTest, CategoriesFollowWeapons)
{
	const auto find = [](SkillSlot skill) {
		for (const GemEntry &gem : GemCatalog()) {
			if (gem.skill == skill)
				return gem;
		}
		ADD_FAILURE();
		return GemCatalog().front();
	};
	const GemEntry groundSlam = find(SkillSlot::ForAttack(WeaponSkill::GroundSlam));
	EXPECT_TRUE(GemInCategory(groundSlam, GemCategory::Mace));
	EXPECT_FALSE(GemInCategory(groundSlam, GemCategory::Bow));
	EXPECT_TRUE(GemInCategory(find(SkillSlot::ForAttack(WeaponSkill::Barrage)), GemCategory::Bow));
	EXPECT_TRUE(GemInCategory(find(SkillSlot::ForAttack(WeaponSkill::ShieldCharge)), GemCategory::Shield));
	EXPECT_TRUE(GemInCategory(find(SkillSlot::ForSpell(SpellID::Fireball)), GemCategory::Elemental));
	EXPECT_TRUE(GemInCategory(find(SkillSlot::ForSpell(SpellID::BoneSpirit)), GemCategory::Occult));
	EXPECT_TRUE(GemInCategory(find(SkillSlot::ForPersistent(PersistentSkill::HeraldOfAsh)), GemCategory::Spirit));
	EXPECT_FALSE(GemInCategory(find(SkillSlot::ForPersistent(PersistentSkill::HeraldOfAsh)), GemCategory::Support));
}

TEST_F(Poe2GemcuttingTest, TiersGroupLevels)
{
	EXPECT_EQ(GemTier(1), 1);
	EXPECT_EQ(GemTier(2), 1);
	EXPECT_EQ(GemTier(3), 3);
	EXPECT_EQ(GemTier(6), 5);
	EXPECT_EQ(GemTier(14), 7);
	EXPECT_EQ(GemTier(16), 9);
	EXPECT_EQ(GemTier(22), 11);
	EXPECT_EQ(GemTier(28), 13);
	EXPECT_EQ(GemTierNumeral(9), "IX");
}

TEST_F(Poe2GemcuttingTest, UncutGemsCutUpToTheirLevel)
{
	const Item skillUncut = MakeUncut(IMISC_UNCUT_SKILL, 6);
	const Item supportUncut = MakeUncut(IMISC_UNCUT_SUPPORT, 3);
	const Item spiritUncut = MakeUncut(IMISC_UNCUT_SPIRIT, 20);
	ASSERT_TRUE(IsUncutGem(skillUncut));
	ASSERT_TRUE(IsUncutGem(spiritUncut));
	EXPECT_EQ(UncutGemKind(spiritUncut), UncutKind::Spirit);
	EXPECT_FALSE(IsUncutSupportGem(skillUncut));
	EXPECT_TRUE(IsUncutSupportGem(supportUncut));
	EXPECT_EQ(UncutGemLevel(skillUncut), 6);

	int cuttable = 0;
	for (const GemEntry &gem : GemCatalog()) {
		const bool isSupport = gem.support != SupportGem::None;
		// POE2: Spirit gems need uncut Spirit gems, not uncut skill gems.
		const bool isSpirit = gem.skill.kind == SkillKind::Persistent;
		// An uncut gem cuts any gem of its tier or lower (levels 1-2 are tier I, 3-5 tier III, 6-9 tier V).
		EXPECT_EQ(CanCutGem(skillUncut, gem), !isSupport && !isSpirit && GemTier(gem.level) <= GemTier(6)) << GemName(gem);
		EXPECT_EQ(CanCutGem(supportUncut, gem), isSupport && GemTier(gem.level) <= GemTier(3)) << GemName(gem);
		EXPECT_EQ(CanCutGem(spiritUncut, gem), isSpirit) << GemName(gem);
		cuttable += CanCutGem(skillUncut, gem) ? 1 : 0;
	}
	EXPECT_GT(cuttable, 5);

	// A level 1 uncut gem cuts the level 2 tier I spells like Firebolt.
	const Item firstUncut = MakeUncut(IMISC_UNCUT_SKILL, 1);
	for (const GemEntry &gem : GemCatalog()) {
		if (gem.skill.kind == SkillKind::Spell && gem.skill.spell() == SpellID::Firebolt)
			EXPECT_TRUE(CanCutGem(firstUncut, gem));
	}
}

TEST_F(Poe2GemcuttingTest, OnlyUncutGemsDrop)
{
	for (const GemEntry &gem : GemCatalog())
		EXPECT_EQ(AllItemsList[gem.itemIndex].dropRate, 0) << GemName(gem);
	EXPECT_GT(MakeUncut(IMISC_UNCUT_SKILL, 1).isEmpty() ? 0 : 1, 0);
	for (const ItemData &data : AllItemsList) {
		if (data.iMiscId == IMISC_UNCUT_SKILL || data.iMiscId == IMISC_UNCUT_SUPPORT)
			EXPECT_GT(data.dropRate, 0);
	}
}

} // namespace
} // namespace devilution
