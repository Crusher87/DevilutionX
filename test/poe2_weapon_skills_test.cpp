#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/bases.hpp"
#include "poe2/default_attack.hpp"
#include "poe2/gem_levels.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/table_skills.hpp"
#include "poe2/weapon_skills.hpp"
#include "spells.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

class Poe2WeaponSkillsTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayer = &Players[0];
		gbIsSpawn = false;
		gbIsMultiplayer = false;
		gbIsHellfire = true;
		gbIsPoe2 = true;
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		LoadItemData();
		LoadSpellData();
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		UnloadModArchives();
	}

	static Item MakeBase(std::string_view name)
	{
		// The row that drops, else the one of that name (the Focused and Quick staves and sceptres no longer drop; saved
		// ones still exist).
		for (const bool dropping : { true, false }) {
			for (size_t i = 0; i < AllItemsList.size(); i++) {
				if (AllItemsList[i].iName == name && (!dropping || AllItemsList[i].dropRate > 0)) {
					Item item;
					InitializeItem(item, static_cast<_item_indexes>(i));
					return item;
				}
			}
		}
		return {};
	}
};

TEST_F(Poe2WeaponSkillsTest, WeaponTypesGrantSkills)
{
	EXPECT_EQ(WeaponSkillForItem(MakeBase("Glass Shank")), WeaponSkill::Flurry);
	EXPECT_EQ(WeaponSkillForItem(MakeBase("Shortsword")), WeaponSkill::Cleave);
	EXPECT_EQ(WeaponSkillForItem(MakeBase("Splitting Greataxe")), WeaponSkill::Cleave);
	EXPECT_EQ(WeaponSkillForItem(MakeBase("Felled Greatclub")), WeaponSkill::GroundSlam);
	EXPECT_EQ(WeaponSkillForItem(MakeBase("Recurve Bow")), WeaponSkill::Barrage);
	EXPECT_EQ(WeaponSkillForItem(MakeBase("Quick Pilgrim Staff")), WeaponSkill::WhirlingStaff);
	EXPECT_EQ(WeaponSkillForItem(MakeBase("Hide Buckler")), WeaponSkill::None);
	EXPECT_EQ(WeaponSkillForItem(Item {}), WeaponSkill::None);

	Player &player = *MyPlayer;
	player = {};
	player.InvBody[INVLOC_HAND_RIGHT] = MakeBase("Recurve Bow");
	EXPECT_EQ(WeaponSkillFor(player), WeaponSkill::Barrage);
	player.InvBody[INVLOC_HAND_LEFT] = MakeBase("Felled Greatclub");
	EXPECT_EQ(WeaponSkillFor(player), WeaponSkill::GroundSlam);
}

TEST_F(Poe2WeaponSkillsTest, EveryWeaponHasAFreeDefaultAttack)
{
	Player &player = *MyPlayer;
	player = {};
	player._pClass = HeroClass::Rogue;
	EXPECT_EQ(DefaultAttackFor(player), DefaultAttack::Punch);
	player.InvBody[INVLOC_HAND_RIGHT] = MakeBase("Hide Buckler");
	EXPECT_EQ(DefaultAttackFor(player), DefaultAttack::ShieldBash);
	player.InvBody[INVLOC_HAND_LEFT] = MakeBase("Shortsword");
	EXPECT_EQ(DefaultAttackFor(player), DefaultAttack::SwordStrike);
	player.InvBody[INVLOC_HAND_LEFT] = MakeBase("Glass Shank");
	EXPECT_EQ(DefaultAttackFor(player), DefaultAttack::DaggerStab);
	player.InvBody[INVLOC_HAND_LEFT] = MakeBase("Splitting Greataxe");
	EXPECT_EQ(DefaultAttackFor(player), DefaultAttack::AxeChop);
	player.InvBody[INVLOC_HAND_LEFT] = MakeBase("Felled Greatclub");
	EXPECT_EQ(DefaultAttackFor(player), DefaultAttack::MaceSmash);
	// A sceptre is a caster's focus, not a club: it channels the staff bolt.
	player.InvBody[INVLOC_HAND_LEFT] = MakeBase("Quick Bone Sceptre");
	EXPECT_EQ(DefaultAttackFor(player), DefaultAttack::StaffBolt);
	EXPECT_TRUE(FiresStaffBolt(player));
	player.InvBody[INVLOC_HAND_RIGHT] = {};
	player.InvBody[INVLOC_HAND_LEFT] = MakeBase("Recurve Bow");
	EXPECT_EQ(DefaultAttackFor(player), DefaultAttack::BowShot);
	EXPECT_TRUE(IsRangedDefaultAttack(DefaultAttack::BowShot));
	EXPECT_FALSE(FiresStaffBolt(player));

	// A staff channels a bolt, except for a Monk, who fights with it.
	player.InvBody[INVLOC_HAND_LEFT] = MakeBase("Quick Pilgrim Staff");
	EXPECT_EQ(DefaultAttackFor(player), DefaultAttack::StaffBolt);
	EXPECT_TRUE(FiresStaffBolt(player));
	player._pClass = HeroClass::Monk;
	EXPECT_EQ(DefaultAttackFor(player), DefaultAttack::StaffStrike);
	EXPECT_FALSE(FiresStaffBolt(player));
}

TEST_F(Poe2WeaponSkillsTest, StaffBoltScalesWithTheStaff)
{
	Player &player = *MyPlayer;
	player = {};
	player._pIMinDam = 4;
	player._pIMaxDam = 12;
	player._pIBonusDam = 50;
	player._pMagic = 50;
	SetRndSeed(3);
	for (int i = 0; i < 200; i++) {
		const int damage = RollStaffBoltDamage(player);
		// The staff's damage with its bonus, cut to StaffBoltDamagePercent, plus 10% of Magic.
		EXPECT_GE(damage, ((4 + 2) * StaffBoltDamagePercent / 100) + 5);
		EXPECT_LE(damage, ((12 + 6) * StaffBoltDamagePercent / 100) + 5);
	}
}

TEST_F(Poe2WeaponSkillsTest, QueuedSkillPaysManaWhenTheAttackStarts)
{
	Player &player = *MyPlayer;
	player = {};
	player._pMana = Fixed26_6::fromInt(20);
	player._pManaBase = Fixed26_6::fromInt(20);

	// A melee skill isn't spent on a ranged attack.
	player.poe2.pendingSkill = WeaponSkill::Cleave;
	ActivateQueuedWeaponSkill(player, /*isRangedAttack=*/true);
	EXPECT_EQ(player.poe2.activeSkill, WeaponSkill::None);
	EXPECT_EQ(player.poe2.pendingSkill, WeaponSkill::Cleave);

	ActivateQueuedWeaponSkill(player, /*isRangedAttack=*/false);
	EXPECT_EQ(player.poe2.activeSkill, WeaponSkill::Cleave);
	EXPECT_EQ(player.poe2.pendingSkill, WeaponSkill::None);
	// The listed cost, x117% for Cleave's speed (half of its damage_pct 134): 3 -> 3.51, to 4.
	EXPECT_EQ(player._pMana.whole(), 20 - GemBaseManaCost(SkillSlot::ForAttack(WeaponSkill::Cleave), 1));
	EXPECT_EQ(player._pMana.whole(), 16);

	// The next plain attack is normal again.
	ActivateQueuedWeaponSkill(player, /*isRangedAttack=*/false);
	EXPECT_EQ(player.poe2.activeSkill, WeaponSkill::None);

	// Without enough mana the attack happens without the skill.
	player._pMana = Fixed26_6::fromInt(1);
	player.poe2.pendingSkill = WeaponSkill::GroundSlam;
	ActivateQueuedWeaponSkill(player, /*isRangedAttack=*/false);
	EXPECT_EQ(player.poe2.activeSkill, WeaponSkill::None);
	EXPECT_EQ(player._pMana.whole(), 1);
}

TEST_F(Poe2WeaponSkillsTest, QueuedSkillExpires)
{
	Poe2PlayerData data;
	data.pendingSkill = WeaponSkill::Flurry;
	data.pendingSkillTicks = 2;
	data.tick();
	EXPECT_EQ(data.pendingSkill, WeaponSkill::Flurry);
	data.tick();
	EXPECT_EQ(data.pendingSkill, WeaponSkill::None);
}

TEST_F(Poe2WeaponSkillsTest, BattleCryWearsOff)
{
	Poe2PlayerData data;
	data.warcryTicks = 2;
	data.tick();
	EXPECT_EQ(data.warcryTicks, 1);
	data.tick();
	data.tick();
	EXPECT_EQ(data.warcryTicks, 0);
	data.warcryTicks = 5;
	data.clear();
	EXPECT_EQ(data.warcryTicks, 0);
}

TEST_F(Poe2WeaponSkillsTest, DashSkillStrikesAsItLands)
{
	// Curtis 2026-10-22: Dashing Strike stood about a second after the dash before it struck. A Monk's staff swing:
	// 13 frames, hitting on the eighth.
	Player &player = *MyPlayer;
	player._pAFrames = 13;
	player._pAFNum = 8;
	player._pdir = Direction::South;
	player.position.tile = { 20, 20 };
	player.position.future = player.position.tile;

	const auto ticksToHit = [&player](bool quickStrike) {
		BeginSkillAnimation(player, WeaponSkill::DashingStrike, player_graphic::Attack, player.position.tile + Direction::South, quickStrike);
		EXPECT_EQ(player._pmode, PM_SPELL);
		int ticks = 0;
		while (player.AnimInfo.currentFrame != player.poe2.castFrame && ticks < 30) {
			player.AnimInfo.processAnimation();
			ticks++;
		}
		player.poe2.castSkill = WeaponSkill::None;
		player._pmode = PM_STAND;
		return ticks;
	};
	// Without the dash, the swing takes its attack time: bare-handed 1.65 a second at Dashing Strike's 75% (POE2's
	// research row; it was 100% before), 16 ticks, hitting 7/12 of the way in.
	EXPECT_EQ(ticksToHit(/*quickStrike=*/false), 9);
	EXPECT_EQ(ticksToHit(/*quickStrike=*/true), DashStrikeWindupTicks);
}

TEST_F(Poe2WeaponSkillsTest, AreaSkillsHitEachMonsterForFiftyToEightyPercent)
{
	EXPECT_EQ(LeapSlamDamagePercent, 80);
	// About two of Frost Nova's shards reach a monster: 2 x 100 = 200 (Curtis 2026-10-28: 25 -> 100 a shard).
	EXPECT_EQ(FrostNovaShardDamagePercent, 100);
	EXPECT_EQ(IceBoltShardDamagePercent, 160);
	// Curtis 2026-10-28: Seismic Slam's cone 75 to 50, Sweeping Wind's rings 75 and 50 to 55 and 25.
	EXPECT_EQ(GroundSlamConeDamagePercent, 50);
	EXPECT_EQ(WhirlingStaffInnerDamagePercent, 55);
	EXPECT_EQ(WhirlingStaffOuterDamagePercent, 25);
	// Lightning Arrow's burst is Diablo's 2 x character level + Dexterity / 4 again (plan step 14, Curtis 2026-10-27):
	// with 60% of the arrow the Rogue could not clear packs.
	EXPECT_TRUE(LightningArrowBurstFromLevel);
	EXPECT_EQ(LightningArrowBurstPerLevel, 2);
	EXPECT_EQ(LightningArrowBurstDexterityDivisor, 4);
	EXPECT_EQ(LightningArrowBurstPercent, 60);
	Player &player = *MyPlayer;
	player = {};
	LoadPlayerDataFiles(); // the level cap comes from the experience table
	player._pClass = HeroClass::Rogue;
	player.setCharacterLevel(10);
	player._pDexterity = 40;
	player._pIMinDam = 10;
	player._pIMaxDam = 20;
	// 2 x 10 + 40 / 4.
	EXPECT_EQ(LightningArrowDamage(player), 30);
	player.setCharacterLevel(40);
	player._pDexterity = 200;
	EXPECT_EQ(LightningArrowDamage(player), 130);
	// The weapon's damage doesn't add to it: the gem curve scales the arrow and the burst alike when they hit.
	player._pIMinDam = 100;
	player._pIMaxDam = 200;
	player._pIBonusDam = 50;
	EXPECT_EQ(LightningArrowDamage(player), 130);
}

TEST_F(Poe2WeaponSkillsTest, AttackSkillsCostLessMana)
{
	// The mana audit's cuts for weapon attacks, at gem level 1; melee attacks then cost a third less (two thirds of
	// GroundSlam 6, Whirlwind 8, LeapSlam 8, Smite 4, Frenzy 3, ShieldBash 4, ShadowStep 5, Vengeance 5, ViperStrike 5,
	// Zeal 5 and SevenSidedStrike 11, rounded). Zeal and Seven-Sided Strike now strike once a use (PerHitBurst), for a
	// quarter and a seventh of that: 3 / 4 and 7 / 7, rounded.
	const std::pair<WeaponSkill, int> Costs[] = {
		{ WeaponSkill::GroundSlam, 4 },
		{ WeaponSkill::Whirlwind, 5 },
		{ WeaponSkill::LeapSlam, 5 },
		{ WeaponSkill::Smite, 3 },
		{ WeaponSkill::Frenzy, 2 },
		{ WeaponSkill::ShieldBash, 3 },
		{ WeaponSkill::ShadowStep, 3 },
		{ WeaponSkill::Vengeance, 3 },
		{ WeaponSkill::ViperStrike, 3 },
		{ WeaponSkill::Zeal, 1 },
		{ WeaponSkill::FanOfBlades, 5 },
		{ WeaponSkill::DoubleThrow, 5 },
		{ WeaponSkill::EscapeShot, 6 },
		{ WeaponSkill::Multishot, 6 },
		{ WeaponSkill::GuidedArrow, 5 },
		{ WeaponSkill::RainOfArrows, 10 },
		{ WeaponSkill::SevenSidedStrike, 1 },
	};
	for (const auto &[skill, cost] : Costs)
		EXPECT_EQ(WeaponSkillManaCost(skill), cost) << WeaponSkillName(skill);
}

TEST_F(Poe2WeaponSkillsTest, MeleeSkillsCostAThirdLessMana)
{
	// Casters are the mana classes: Cleave 4, Double Swing 4 and Seismic Slam 6 cost 3, 3 and 4 at gem level 1.
	EXPECT_EQ(WeaponSkillManaCost(WeaponSkill::Cleave), 3);
	EXPECT_EQ(WeaponSkillManaCost(WeaponSkill::Flurry), 3);
	EXPECT_EQ(WeaponSkillManaCost(WeaponSkill::GroundSlam), 4);
	// Since 2026-10-27 a skill costs more by half of what its speed adds to its damage: Cleave x117%, 3.51, so 4.
	EXPECT_EQ(GemBaseManaCost(SkillSlot::ForAttack(WeaponSkill::Cleave), 1), 4);
	Player &player = *MyPlayer;
	player = {};
	player.poe2.skillSlots[0] = SkillSlot::ForAttack(WeaponSkill::Cleave);
	player.poe2.slotLevels[0] = 1;
	EXPECT_EQ(WeaponSkillManaCost(player, WeaponSkill::Cleave), 4);
	EXPECT_EQ(SkillManaCost(player, SkillSlot::ForAttack(WeaponSkill::Cleave)), 4);
	// The gem's level still raises it along its curve.
	EXPECT_GT(GemBaseManaCost(SkillSlot::ForAttack(WeaponSkill::Cleave), 20), 3 * 3);

	// Bow, spell and warcry skills keep their cost.
	EXPECT_FALSE(HasMeleeManaCut(WeaponSkill::Barrage));
	EXPECT_FALSE(HasMeleeManaCut(WeaponSkill::IceBolt));
	EXPECT_FALSE(HasMeleeManaCut(WeaponSkill::BattleCry));
	EXPECT_FALSE(HasMeleeManaCut(WeaponSkill::Multishot));
	EXPECT_FALSE(HasMeleeManaCut(WeaponSkill::Meteor));
	EXPECT_TRUE(HasMeleeManaCut(WeaponSkill::Fissure));
	EXPECT_EQ(WeaponSkillManaCost(WeaponSkill::Barrage), 7);
	EXPECT_EQ(WeaponSkillManaCost(WeaponSkill::IceBolt), 4);

	// Outside POE2 mode the listed cost stands.
	gbIsPoe2 = false;
	EXPECT_EQ(WeaponSkillManaCost(WeaponSkill::Cleave), 4);
	gbIsPoe2 = true;
}

TEST(Poe2WeaponSkills, GroundSlamHitsAForwardCone)
{
	// Facing south (down the screen), the cone covers the tiles ahead and not the ones behind or beside.
	EXPECT_TRUE(InGroundSlamCone(Direction::South, Displacement { 1, 1 }));
	EXPECT_TRUE(InGroundSlamCone(Direction::South, Displacement { 3, 3 }));
	EXPECT_TRUE(InGroundSlamCone(Direction::South, Displacement { 3, 2 }));
	EXPECT_FALSE(InGroundSlamCone(Direction::South, Displacement { -1, -1 }));
	EXPECT_FALSE(InGroundSlamCone(Direction::South, Displacement { 1, -1 }));
	EXPECT_FALSE(InGroundSlamCone(Direction::South, Displacement { 3, 0 }));
}

TEST(Poe2WeaponSkills, SingleTargetMeleeSkillsHitHarder)
{
	// Curtis 2026-10-28: Smite 100 to 300, Frenzy 100 to 200, Shield Bash 120 to 200 (audit-damage-v2.md section 7).
	EXPECT_EQ(SwingSkillDamagePercent(WeaponSkill::Smite), 300);
	EXPECT_EQ(SwingSkillDamagePercent(WeaponSkill::Frenzy), 200);
	EXPECT_EQ(ShieldBashDamagePercent, 200);
	// A plain swing (and every swing outside POE2 Mashup, which has no active skill) keeps its weapon hit.
	EXPECT_EQ(SwingSkillDamagePercent(WeaponSkill::None), 100);
	EXPECT_EQ(SwingSkillDamagePercent(WeaponSkill::Flurry), 100);
	EXPECT_EQ(SwingSkillDamagePercent(WeaponSkill::GroundSlam), 100);
	// Double Throw and Vengeance are table rows.
	EXPECT_EQ(FindTableSkill(WeaponSkill::DoubleThrow)->damagePercent, 230);
	EXPECT_EQ(FindTableSkill(WeaponSkill::Vengeance)->damagePercent, 260);
	EXPECT_EQ(WeaponSkillDescription(WeaponSkill::BattleCry), "Knock back foes, +30% melee damage");
}

TEST(Poe2WeaponSkills, FireballIsSlowAndFireboltIsFast)
{
	EXPECT_LT(FireballSpeedPercent, 100);
	EXPECT_GT(FireboltSpeedPercent, 100);
	EXPECT_GE(BarrageArrows, 5);
	EXPECT_GE(EmberFusilladeEmbers, 5);
}

} // namespace
} // namespace devilution
