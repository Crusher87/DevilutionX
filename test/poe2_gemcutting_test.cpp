#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/gemcutting.hpp"
#include "poe2/meta_gems.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/support_gems.hpp"
#include "poe2/support_table.hpp"
#include "poe2/support_tiers.hpp"
#include "poe2/weapon_skills.hpp"
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
	// 181 before the five crossbow skills were retired (Curtis 2026-10-27).
	EXPECT_EQ(skills, 176U);
	// Lineage supports are found, never cut; the rest have an entry for each tier.
	size_t tiers = 0;
	for (size_t i = 1; i < static_cast<size_t>(SupportGem::Count); i++) {
		// The old trigger supports are meta gems now, and can't be cut.
		if (!IsLineageSupport(static_cast<SupportGem>(i)) && !IsRetiredSupport(static_cast<SupportGem>(i)))
			tiers += static_cast<size_t>(SupportTierCount(static_cast<SupportGem>(i)));
	}
	EXPECT_EQ(supports, tiers);
	EXPECT_GT(tiers, static_cast<size_t>(SupportGem::Count) - 1 - 16);
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

TEST_F(Poe2GemcuttingTest, CrossbowSkillsMovedToBowsOrRetired)
{
	// Curtis 2026-10-27: crossbows left the game. The crossbow skills that make sense with a bow moved to bows.
	std::optional<Item> bow;
	std::optional<Item> crossbow;
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		const ItemData &data = AllItemsList[i];
		if (data.iClass != ICLASS_WEAPON || data.itype != ItemType::Bow)
			continue;
		std::optional<Item> &kind = data.iSName == "Crossbow" ? crossbow : bow;
		if (kind)
			continue;
		kind.emplace();
		InitializeItem(*kind, static_cast<_item_indexes>(i));
	}
	ASSERT_TRUE(bow.has_value());
	ASSERT_TRUE(crossbow.has_value());
	const auto inCatalog = [](SkillSlot skill) {
		return std::any_of(GemCatalog().begin(), GemCatalog().end(), [skill](const GemEntry &gem) { return gem.skill == skill; });
	};

	const std::pair<WeaponSkill, std::string_view> Moved[] = {
		{ WeaponSkill::ClusterBolt, "Fire Arrow" },
		{ WeaponSkill::ExplosiveBolt, "Explosive Arrow" },
	};
	for (const auto &[skill, name] : Moved) {
		const SkillSlot slot = SkillSlot::ForAttack(skill);
		EXPECT_EQ(WeaponSkillName(skill), name);
		EXPECT_FALSE(IsRetiredSkill(slot)) << name;
		EXPECT_TRUE(ItemAllowsWeaponSkill(*bow, skill)) << name;
		EXPECT_EQ(WeaponSkillWeapons(skill), "bow") << name;
		EXPECT_TRUE(inCatalog(slot)) << name;
		// The gem's item is renamed with the skill, so the gem still finds it.
		const std::optional<size_t> index = SkillGemItemIndex(slot);
		ASSERT_TRUE(index.has_value()) << name;
		EXPECT_EQ(std::string_view(AllItemsList[*index].iName), std::string(name) + " Gem");
	}

	// The rest never drop and are never cut, but a gem a hero already has still loads and works with his crossbow.
	for (const WeaponSkill skill : { WeaponSkill::RapidFire, WeaponSkill::Impale, WeaponSkill::SpikeTrap, WeaponSkill::Sentry, WeaponSkill::SiegeVolley }) {
		const SkillSlot slot = SkillSlot::ForAttack(skill);
		EXPECT_TRUE(IsRetiredSkill(slot)) << WeaponSkillName(skill);
		EXPECT_FALSE(inCatalog(slot)) << WeaponSkillName(skill);
		const std::optional<size_t> index = SkillGemItemIndex(slot);
		ASSERT_TRUE(index.has_value()) << WeaponSkillName(skill);
		EXPECT_EQ(AllItemsList[*index].dropRate, 0) << WeaponSkillName(skill);
		EXPECT_TRUE(ItemAllowsWeaponSkill(*crossbow, skill)) << WeaponSkillName(skill);
	}

	// Every skill on the Bow tab works with a bow.
	for (const GemEntry &gem : GemCatalog()) {
		if (GemInCategory(gem, GemCategory::Bow))
			EXPECT_TRUE(ItemAllowsWeaponSkill(*bow, gem.skill.attack())) << GemName(gem);
	}
}

TEST_F(Poe2GemcuttingTest, BowTiersHaveNoTwoSkillsOfOneElement)
{
	// The gem plan: no two fire, cold or lightning skills in one tier of a tab.
	std::map<int, uint32_t> elements;
	std::map<int, int> skills;
	for (const GemEntry &gem : GemCatalog()) {
		if (!GemInCategory(gem, GemCategory::Bow))
			continue;
		const int tier = GemTier(gem.level);
		const uint32_t element = SkillTags(gem.skill) & ST_Elemental;
		EXPECT_EQ(elements[tier] & element, 0U) << GemName(gem);
		elements[tier] |= element;
		skills[tier]++;
	}
	// Tier I has more than Hail of Arrows, the Rogue's first gem.
	EXPECT_GE(skills[1], 2);
}

TEST_F(Poe2GemcuttingTest, TabsFollowTheUncutGem)
{
	const auto has = [](const std::vector<GemCategory> &tabs, GemCategory category) {
		return std::find(tabs.begin(), tabs.end(), category) != tabs.end();
	};
	// Browsing shows every tab.
	EXPECT_EQ(GemcuttingTabs(std::nullopt).size(), static_cast<size_t>(GemCategory::Count));

	// Curtis 2026-10-22: cutting a skill gem shows no Support or Spirit tab, and the other way round.
	const std::vector<GemCategory> skillTabs = GemcuttingTabs(UncutKind::Skill);
	EXPECT_FALSE(has(skillTabs, GemCategory::Support));
	EXPECT_FALSE(has(skillTabs, GemCategory::Spirit));
	EXPECT_TRUE(has(skillTabs, GemCategory::Staff));
	EXPECT_TRUE(has(skillTabs, GemCategory::Elemental));
	EXPECT_TRUE(has(skillTabs, GemCategory::All));
	EXPECT_EQ(GemcuttingTabs(UncutKind::Support), std::vector<GemCategory> { GemCategory::Support });
	EXPECT_EQ(GemcuttingTabs(UncutKind::Spirit), std::vector<GemCategory> { GemCategory::Spirit });

	for (const UncutKind kind : { UncutKind::Skill, UncutKind::Spirit, UncutKind::Support }) {
		const std::vector<GemCategory> tabs = GemcuttingTabs(kind);
		// Every gem the uncut gem can become is on one of its tabs...
		for (const GemEntry &gem : GemCatalog()) {
			if (GemUncutKind(gem) != kind)
				continue;
			EXPECT_TRUE(std::any_of(tabs.begin(), tabs.end(), [&gem](GemCategory tab) { return tab != GemCategory::All && GemInCategory(gem, tab); })) << GemName(gem);
		}
		// ...and none of its tabs is empty.
		for (const GemCategory tab : tabs) {
			EXPECT_TRUE(std::any_of(GemCatalog().begin(), GemCatalog().end(), [kind, tab](const GemEntry &gem) { return GemUncutKind(gem) == kind && GemInCategory(gem, tab); }))
			    << GemCategoryName(tab);
		}
	}
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

TEST_F(Poe2GemcuttingTest, UncutGemLevelsFollowTheArea)
{
	// POE2: a gem level first drops one area level above the character level it needs (level 2: needs 3, drops at 4).
	EXPECT_EQ(UncutGemLevelForArea(1), 1);
	EXPECT_EQ(UncutGemLevelForArea(CharacterLevelForGemLevel(4)), 3);
	EXPECT_EQ(UncutGemLevelForArea(CharacterLevelForGemLevel(4) + 1), 4);
	EXPECT_EQ(UncutGemLevelForArea(7), 4) << "Cathedral floor 4, where Snotspill's gems were level 6";
	EXPECT_EQ(UncutGemLevelForArea(CharacterLevelForGemLevel(MaxGemLevel) + 1), MaxGemLevel);

	// Uncut supports climb POE2's 1-5 ladder by POE2 area level: none below 4.
	EXPECT_EQ(UncutSupportLevelForArea(3), 0);
	EXPECT_EQ(UncutSupportLevelForArea(4), 1);
	EXPECT_EQ(UncutSupportLevelForArea(15), 1);
	EXPECT_EQ(UncutSupportLevelForArea(16), 2);
	EXPECT_EQ(UncutSupportLevelForArea(32), 2);
	EXPECT_EQ(UncutSupportLevelForArea(33), 3);
	EXPECT_EQ(UncutSupportLevelForArea(44), 3);
	EXPECT_EQ(UncutSupportLevelForArea(45), 4);
	EXPECT_EQ(UncutSupportLevelForArea(55), 5);
	EXPECT_EQ(UncutSupportLevelForArea(82), MaxUncutSupportLevel);
	// Our gem level 6 is POE2's uncut support level 3: area level 33, the Crypt, not the Cathedral.
	EXPECT_EQ(UncutSupportLevelForGemLevel(6), 3);
	EXPECT_EQ(UncutSupportGemLevel(UncutSupportLevelForArea(9)), 1);
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

TEST_F(Poe2GemcuttingTest, UncutGemsNameTheirLevel)
{
	EXPECT_EQ(MakeUncut(IMISC_UNCUT_SKILL, 6).getName().str(), "Uncut Skill Gem (Level 6)");
	EXPECT_EQ(MakeUncut(IMISC_UNCUT_SUPPORT, 3).getName().str(), "Uncut Support Gem (Level 3)");
	EXPECT_EQ(MakeUncut(IMISC_UNCUT_SPIRIT, 20).getName().str(), "Uncut Spirit Gem (Level 20)");
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
