#include <set>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/gemcutting.hpp"
#include "poe2/meta_gems.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/support_gems.hpp"
#include "poe2/support_table.hpp"
#include "poe2/table_skills.hpp"
#include "poe2/weapon_skills.hpp"
#include "spells.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

class Poe2SupportTableTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayer = &Players[0];
		*MyPlayer = {};
		MyPlayer->poe2.clear();
		MyPlayer->_pStrength = 300;
		MyPlayer->_pMagic = 300;
		MyPlayer->_pDexterity = 300;
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
		Missiles.clear();
	}

	void TearDown() override
	{
		Missiles.clear();
		ClearSkillZones();
		ClearSupportEffects();
		gbIsPoe2 = false;
		leveltype = DTYPE_TOWN;
		UnloadModArchives();
	}

	/** @brief The skills the tests try supports on: spells, attacks and minions. */
	static std::vector<SkillSlot> Candidates()
	{
		std::vector<SkillSlot> skills;
		for (const SpellID spell : { SpellID::Firebolt, SpellID::Fireball, SpellID::FireWall, SpellID::ChainLightning, SpellID::Inferno, SpellID::RuneOfFire, SpellID::Healing })
			skills.push_back(SkillSlot::ForSpell(spell));
		for (int i = 1; i <= static_cast<int>(WeaponSkill::Last); i++)
			skills.push_back(SkillSlot::ForAttack(static_cast<WeaponSkill>(i)));
		for (const PersistentSkill skill : { PersistentSkill::SkeletalWarriors, PersistentSkill::SkeletalMages, PersistentSkill::SummonGolem, PersistentSkill::HeraldOfAsh })
			skills.push_back(SkillSlot::ForPersistent(skill));
		return skills;
	}

	static SupportSocketResult Socket(size_t slot, SkillSlot skill, SupportGem gem)
	{
		MyPlayer->poe2.skillSlots[slot] = skill;
		SupportGem replaced;
		return SocketSupportGem(*MyPlayer, slot, std::nullopt, gem, replaced);
	}
};

TEST_F(Poe2SupportTableTest, EveryTableSupportHasAnItemAndFitsSomething)
{
	std::set<std::string_view> names;
	size_t lineage = 0;
	for (const SupportData &data : SupportTable()) {
		EXPECT_TRUE(names.insert(data.name).second) << data.name;
		EXPECT_EQ(FindSupportData(data.gem), &data);
		EXPECT_EQ(SupportGemName(data.gem), data.name);
		EXPECT_FALSE(SupportGemDescription(data.gem).empty()) << data.name;
		Item item;
		ASSERT_TRUE(CreateSupportGemItem(item, data.gem)) << data.name;
		EXPECT_EQ(SupportGemForItem(item), data.gem) << data.name;
		bool fits = false;
		for (const SkillSlot skill : Candidates())
			fits = fits || SupportGemCanSupport(data.gem, skill);
		// The old trigger supports are meta gems now and fit nothing.
		EXPECT_EQ(fits, !IsRetiredSupport(data.gem)) << data.name;
		if (IsLineageSupport(data.gem))
			lineage++;
	}
	EXPECT_EQ(SupportTable().size(), 80U);
	EXPECT_EQ(lineage, 16U);
	EXPECT_EQ(FindSupportData(SupportGem::Empower), nullptr);
}

TEST_F(Poe2SupportTableTest, SupportsFitByTags)
{
	EXPECT_TRUE(SupportGemCanSupport(SupportGem::Volley, SkillSlot::ForSpell(SpellID::Firebolt)));
	EXPECT_FALSE(SupportGemCanSupport(SupportGem::Volley, SkillSlot::ForSpell(SpellID::Healing)));
	EXPECT_TRUE(SupportGemCanSupport(SupportGem::Aftershock, SkillSlot::ForAttack(WeaponSkill::LeapSlam)));
	EXPECT_FALSE(SupportGemCanSupport(SupportGem::Aftershock, SkillSlot::ForSpell(SpellID::Firebolt)));
	EXPECT_TRUE(SupportGemCanSupport(SupportGem::FeedingFrenzy, SkillSlot::ForPersistent(PersistentSkill::SkeletalWarriors)));
	EXPECT_FALSE(SupportGemCanSupport(SupportGem::FeedingFrenzy, SkillSlot::ForAttack(WeaponSkill::Cleave)));
	EXPECT_TRUE(SkillTakesSupports(SkillSlot::ForPersistent(PersistentSkill::SkeletalMages)));
	// The first supports keep their own rules.
	EXPECT_FALSE(SupportGemCanSupport(SupportGem::Empower, SkillSlot::ForPersistent(PersistentSkill::SkeletalMages)));
}

TEST_F(Poe2SupportTableTest, ConflictsAndLineageRules)
{
	EXPECT_TRUE(SupportsConflict(SupportGem::Volley, SupportGem::MultipleProjectiles));
	EXPECT_TRUE(SupportsConflict(SupportGem::InfusedFlames, SupportGem::Brutality));
	EXPECT_FALSE(SupportsConflict(SupportGem::Volley, SupportGem::Pierce));

	EXPECT_EQ(Socket(0, SkillSlot::ForSpell(SpellID::Firebolt), SupportGem::MultipleProjectiles), SupportSocketResult::Socketed);
	EXPECT_EQ(Socket(0, SkillSlot::ForSpell(SpellID::Firebolt), SupportGem::Volley), SupportSocketResult::Conflict);

	SupportGem melee = SupportGem::None;
	for (const SupportData &data : SupportTable()) {
		if (IsLineageSupport(data.gem) && SupportGemCanSupport(data.gem, SkillSlot::ForAttack(WeaponSkill::Cleave))) {
			melee = data.gem;
			break;
		}
	}
	ASSERT_NE(melee, SupportGem::None);
	EXPECT_EQ(Socket(1, SkillSlot::ForAttack(WeaponSkill::Cleave), melee), SupportSocketResult::Socketed);
	EXPECT_EQ(Socket(2, SkillSlot::ForAttack(WeaponSkill::GroundSlam), melee), SupportSocketResult::LineageInUse);
}

TEST_F(Poe2SupportTableTest, LineageSupportsCantBeCut)
{
	for (const GemEntry &gem : GemCatalog())
		EXPECT_FALSE(IsLineageSupport(gem.support));
}

TEST_F(Poe2SupportTableTest, BloodPriceMovesTheCostToLife)
{
	const SkillSlot firebolt = SkillSlot::ForSpell(SpellID::Firebolt);
	const int before = SupportManaCostPercent(*MyPlayer, firebolt);
	ASSERT_EQ(Socket(0, firebolt, SupportGem::BloodPrice), SupportSocketResult::Socketed);
	EXPECT_EQ(SupportManaCostPercent(*MyPlayer, firebolt), 0);
	EXPECT_GE(SupportManaCostPercent(*MyPlayer, firebolt, false), before);

	MyPlayer->_pMaxHP = MyPlayer->_pHitPoints = Fixed26_6::fromInt(100);
	MyPlayer->_pMaxHPBase = MyPlayer->_pHPBase = Fixed26_6::fromInt(100);
	PaySupportLifeCost(*MyPlayer, firebolt, 10);
	EXPECT_EQ(MyPlayer->_pHitPoints.whole(), 88);
}

TEST_F(Poe2SupportTableTest, AreaAndDurationSupportsScale)
{
	const SkillSlot fireball = SkillSlot::ForSpell(SpellID::Fireball);
	EXPECT_EQ(SupportAreaRadius(*MyPlayer, fireball, 10), 10);
	ASSERT_EQ(Socket(0, fireball, SupportGem::IncreasedArea), SupportSocketResult::Socketed);
	EXPECT_GT(SupportAreaRadius(*MyPlayer, fireball, 10), 10);

	const SkillSlot wall = SkillSlot::ForSpell(SpellID::FireWall);
	ASSERT_EQ(Socket(1, wall, SupportGem::IncreasedDuration), SupportSocketResult::Socketed);
	EXPECT_GT(SupportDurationTicks(*MyPlayer, wall, 100), 100);
}

TEST_F(Poe2SupportTableTest, EverySupportGoesOffInAnEmptyDungeon)
{
	leveltype = DTYPE_CATHEDRAL;
	MyPlayer->setCharacterLevel(50);
	MyPlayer->_pMaxMana = MyPlayer->_pMana = Fixed26_6::fromInt(5000);
	MyPlayer->_pMaxManaBase = MyPlayer->_pManaBase = Fixed26_6::fromInt(5000);
	MyPlayer->_pMaxHP = MyPlayer->_pHitPoints = Fixed26_6::fromInt(5000);
	MyPlayer->_pMaxHPBase = MyPlayer->_pHPBase = Fixed26_6::fromInt(5000);
	for (const SupportData &data : SupportTable()) {
		SkillSlot skill;
		for (const SkillSlot candidate : Candidates()) {
			if (candidate.kind != SkillKind::Persistent && SupportGemCanSupport(data.gem, candidate)) {
				skill = candidate;
				break;
			}
		}
		if (skill.isEmpty())
			continue;
		MyPlayer->poe2.clear();
		Missiles.clear();
		ASSERT_EQ(Socket(0, skill, data.gem), SupportSocketResult::Socketed) << data.name;
		MyPlayer->position.tile = { 40, 40 };
		MyPlayer->_pdir = Direction::South;
		if (skill.kind == SkillKind::Spell) {
			MyPlayer->executedSpell.spellType = SpellType::Spell;
			CastSpell(*MyPlayer, skill.spell(), WorldTilePosition { 40, 40 }, WorldTilePosition { 43, 40 }, 1);
		} else {
			MyPlayer->poe2.castSkill = skill.attack();
			ExecuteSkillCast(*MyPlayer, skill.attack(), { 43, 40 });
			MyPlayer->poe2.castSkill = WeaponSkill::None;
		}
		for (int tick = 0; tick < 400; tick++) {
			ProcessSkillVolley(*MyPlayer);
			ProcessSkillZones(*MyPlayer);
			ProcessSupportEffects(*MyPlayer);
			MyPlayer->poe2.tick();
		}
		ClearSkillZones();
		ClearSupportEffects();
		EXPECT_FALSE(MyPlayer->hasNoLife()) << data.name;
	}
}

} // namespace
} // namespace devilution
