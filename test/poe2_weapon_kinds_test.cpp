#include <string_view>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/bases.hpp"
#include "poe2/default_attack.hpp"
#include "poe2/economy.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/spirit.hpp"
#include "poe2/weapon_kinds.hpp"
#include "spells.h"
#include "tables/itemdat.h"

namespace devilution {
namespace {

class Poe2WeaponKindsTest : public ::testing::Test {
public:
	void SetUp() override
	{
		gbIsSpawn = false;
		gbIsMultiplayer = false;
		gbIsHellfire = true;
		gbIsPoe2 = true;
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		LoadItemData();
		LoadSpellData();
		LoadModifierData();
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		LoadModifierData();
		UnloadModArchives();
	}

	static int FindRow(std::string_view id)
	{
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iName == id)
				return static_cast<int>(i);
		}
		ADD_FAILURE() << id;
		return 0;
	}

	static Item ItemOf(int row)
	{
		Item item {};
		item.IDidx = static_cast<_item_indexes>(row);
		item._itype = AllItemsList[row].itype;
		item._iClass = AllItemsList[row].iClass;
		return item;
	}
};

TEST_F(Poe2WeaponKindsTest, ExistingRowsHaveTheirLegacyKinds)
{
	// The bundle's Focused and Quick staves and sceptres, from before the rework.
	for (int row = 544; row <= 559; row++)
		EXPECT_EQ(WeaponKindOf(row), WeaponKind::LegacyStaff) << AllItemsList[row].iName;
	for (int row = 560; row <= 575; row++)
		EXPECT_EQ(WeaponKindOf(row), WeaponKind::LegacySceptre) << AllItemsList[row].iName;
	// Every quarterstaff, the Diablo staff homes among them.
	for (const int row : { 473, 476, 479, 482, 950 })
		EXPECT_EQ(WeaponKindOf(row), WeaponKind::Quarterstaff) << AllItemsList[row].iName;
	for (int row = 472; row <= 495; row++)
		EXPECT_TRUE(IsQuarterstaff(ItemOf(row))) << AllItemsList[row].iName;
	EXPECT_EQ(WeaponKindOf(FindRow("Short Staff")), WeaponKind::LegacyStaff);
	EXPECT_EQ(WeaponKindOf(FindRow("Cinquedea")), WeaponKind::Martial);
	EXPECT_EQ(WeaponKindOf(FindRow("Iron Cap")), WeaponKind::None);
	EXPECT_EQ(WeaponKindOf(-1), WeaponKind::None);

	for (size_t i = 0; i < AllItemsList.size(); i++) {
		const Item item = ItemOf(static_cast<int>(i));
		// Nothing is a new caster weapon until the new bases exist.
		EXPECT_FALSE(IsCasterWeapon(item)) << AllItemsList[i].iName;
		EXPECT_EQ(GrantingBase(item), nullptr);
		// A sceptre is what ItemGrantsSpirit always called one.
		EXPECT_EQ(IsAnySceptre(item), std::string_view(AllItemsList[i].iName).ends_with("Sceptre")) << AllItemsList[i].iName;
		EXPECT_EQ(IsMartialStaff(item), AllItemsList[i].itype == ItemType::Staff && AllItemsList[i].iClass == ICLASS_WEAPON) << AllItemsList[i].iName;
	}
}

TEST_F(Poe2WeaponKindsTest, ReclassedBaseNamesMapOnlyToTheOldCasterRows)
{
	int staves = 0;
	int sceptres = 0;
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		const BaseMeta *base = BaseMetaFor(static_cast<int>(i));
		if (base == nullptr)
			continue;
		if (base->itemClass == "legacy_staff" || base->itemClass == "legacy_sceptre") {
			EXPECT_GE(i, 544U) << base->name;
			EXPECT_LE(i, 575U) << base->name;
			(base->itemClass == "legacy_staff" ? staves : sceptres)++;
		}
		EXPECT_NE(base->itemClass, "staff") << base->name;
		EXPECT_NE(base->itemClass, "sceptre") << base->name;
	}
	EXPECT_EQ(staves, 16);
	EXPECT_EQ(sceptres, 16);
}

TEST_F(Poe2WeaponKindsTest, NewBaseColumnsParse)
{
	// Rows from before the rework leave the new columns empty.
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		const BaseMeta *base = BaseMetaFor(static_cast<int>(i));
		if (base == nullptr)
			continue;
		EXPECT_EQ(base->grantedSkill.kind, SkillKind::None) << base->name;
		EXPECT_EQ(base->elementLock, 0) << base->name;
		EXPECT_EQ(base->crit, -1) << base->name;
	}
	EXPECT_EQ(ParseGrantedSkill("StaffBolt"), SkillSlot::ForDefault(static_cast<uint8_t>(DefaultAttack::StaffBolt)));
	EXPECT_EQ(ParseGrantedSkill("SkeletalWarriors"), SkillSlot::ForPersistent(PersistentSkill::SkeletalWarriors));
	EXPECT_EQ(ParseGrantedSkill("ManaShield"), SkillSlot::ForPersistent(PersistentSkill::ManaShield));
	EXPECT_EQ(ParseGrantedSkill("Cleave"), SkillSlot::ForAttack(WeaponSkill::Cleave));
	EXPECT_EQ(ParseGrantedSkill("NoSuchSkill").kind, SkillKind::None);
	EXPECT_EQ(ParseGrantedSkill("").kind, SkillKind::None);
	EXPECT_EQ(ParseElementLock("fire"), ModBaseFireSpell);
	EXPECT_EQ(ParseElementLock("cold,lightning"), ModBaseColdSpell | ModBaseLightningSpell);
	EXPECT_EQ(ParseElementLock("physical"), ModBasePhysicalSpell);
	EXPECT_EQ(ParseElementLock(""), 0);
}

TEST_F(Poe2WeaponKindsTest, RoutedSitesAnswerAsBeforeForTheOldRows)
{
	// Until the new bases exist, deciding by kind gives what deciding by item type gave.
	Players.resize(1);
	Player &player = Players[0];
	player = {};
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		const ItemData &data = AllItemsList[i];
		EXPECT_EQ(GambleFamilyHas(GambleFamily::Mace, static_cast<int>(i)), data.itype == ItemType::Mace) << data.iName;
		EXPECT_EQ(GambleFamilyHas(GambleFamily::Staff, static_cast<int>(i)), data.itype == ItemType::Staff) << data.iName;
		if (data.iClass != ICLASS_WEAPON)
			continue;
		player.InvBody[INVLOC_HAND_LEFT] = ItemOf(static_cast<int>(i));
		player.InvBody[INVLOC_HAND_LEFT]._iStatFlag = true;
		EXPECT_EQ(HoldsMartialStaff(player), player.isHoldingItem(ItemType::Staff)) << data.iName;
	}
	// A hand-built item without a row keeps its type's answer.
	Item staff {};
	staff._itype = ItemType::Staff;
	EXPECT_TRUE(IsMartialStaff(staff));
}

TEST_F(Poe2WeaponKindsTest, OutsidePoe2EveryHelperGivesTheLegacyAnswer)
{
	gbIsPoe2 = false;
	UnloadModArchives();
	LoadModArchives({ { "hf" } });
	LoadItemData();
	LoadModifierData();
	int staves = 0;
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		const ItemData &data = AllItemsList[i];
		const Item item = ItemOf(static_cast<int>(i));
		EXPECT_FALSE(IsCasterWeapon(item)) << data.iName;
		EXPECT_FALSE(IsQuarterstaff(item)) << data.iName;
		EXPECT_FALSE(IsNewSceptre(item)) << data.iName;
		if (data.itype == ItemType::Staff && data.iClass == ICLASS_WEAPON) {
			EXPECT_EQ(WeaponKindOf(item), WeaponKind::LegacyStaff) << data.iName;
			staves++;
		}
		EXPECT_EQ(IsAnySceptre(item), std::string_view(data.iName).ends_with("Sceptre")) << data.iName;
	}
	EXPECT_GT(staves, 0);

	// A game started after a POE2 game doesn't keep its kinds.
	gbIsPoe2 = true;
	UnloadModArchives();
	LoadModArchives({ { "hf", "poe2" } });
	LoadItemData();
	LoadModifierData();
	EXPECT_EQ(WeaponKindOf(473), WeaponKind::Quarterstaff);
	gbIsPoe2 = false;
	UnloadModArchives();
	LoadModArchives({ { "hf" } });
	LoadItemData();
	LoadModifierData();
	for (size_t i = 0; i < AllItemsList.size(); i++)
		EXPECT_NE(WeaponKindOf(static_cast<int>(i)), WeaponKind::Quarterstaff) << AllItemsList[i].iName;
}

} // namespace
} // namespace devilution
