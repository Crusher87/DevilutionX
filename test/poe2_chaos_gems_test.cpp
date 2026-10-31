#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>

#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "lighting.h"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/gemcutting.hpp"
#include "poe2/item_stats.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/monster_resistance.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/player_data.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/spirit.hpp"
#include "poe2/support_gems.hpp"
#include "poe2/support_table.hpp"
#include "poe2/support_tiers.hpp"
#include "poe2/table_skills.hpp"
#include "poe2/unique_effects.hpp"
#include "poe2/weapon_skills.hpp"
#include "spells.h"
#include "storm/storm_net.hpp"
#include "tables/misdat.h"
#include "tables/monstdat.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

/** @brief The chaos skill and support gems of the chaos gems addendum (research/chaos-damage), one by one. */
class Poe2ChaosGemsTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayer = &Players[0];
		MyPlayerId = 0;
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
		LoadMonsterData();
		LevelMonsterTypes[0].type = MT_NZOMBIE;
		Missiles.clear();

		Player &player = *MyPlayer;
		player._pClass = HeroClass::Rogue;
		player.setCharacterLevel(30);
		player._pBaseStr = player._pStrength = 100;
		player._pBaseMag = player._pMagic = 100;
		player._pBaseDex = player._pDexterity = 200;
		player._pBaseVit = player._pVitality = 25;
		player._pMaxHPBase = player._pHPBase = player._pMaxHP = player._pHitPoints = Fixed26_6::fromInt(200);
		player._pMaxManaBase = player._pManaBase = player._pMaxMana = player._pMana = Fixed26_6::fromInt(200);
		player._pIMinDam = 20;
		player._pIMaxDam = 30;
		player.position.tile = { 40, 40 };
		player._pdir = Direction::East;
	}

	void TearDown() override
	{
		Missiles.clear();
		ClearSkillZones();
		ClearSupportEffects();
		ClearMonsters();
		gbIsPoe2 = false;
		leveltype = DTYPE_TOWN;
		LoadPassiveTreeData();
		UnloadModArchives();
	}

	/** @brief Socket a skill in the first Skills panel row with one support of the given tier. */
	static SkillSlot Supported(SkillSlot skill, SupportGem gem = SupportGem::None, int tier = 1)
	{
		Player &player = *MyPlayer;
		player.poe2.skillSlots[0] = skill;
		player.poe2.slotLevels[0] = 1;
		player.poe2.slotSupports[0] = {};
		player.poe2.slotSupportTiers[0] = {};
		if (gem != SupportGem::None) {
			player.poe2.slotSupports[0][0] = gem;
			player.poe2.slotSupportTiers[0][0] = static_cast<uint8_t>(tier);
		}
		return skill;
	}

	/** @brief A normal monster of the kind standing on the tile, with plenty of life. */
	static Monster &PlaceMonster(size_t slot, Point tile, _monster_id kind = MT_NZOMBIE)
	{
		LevelMonsterTypes[0].type = kind;
		Monster &monster = Monsters[slot];
		monster = {};
		monster.levelType = 0;
		monster.uniqueType = UniqueMonsterType::None;
		monster.talkMsg = TEXT_NONE;
		monster.lightId = NO_LIGHT;
		monster.mode = MonsterMode::Stand;
		monster.maxHitPoints = monster.hitPoints = Fixed26_6::fromInt(100000);
		monster.position.tile = monster.position.old = monster.position.future = tile;
		dMonster[tile.x][tile.y] = static_cast<int16_t>(slot + 1);
		ActiveMonsters[ActiveMonsterCount] = static_cast<unsigned>(slot);
		ActiveMonsterCount++;
		return monster;
	}

	static void ClearMonsters()
	{
		for (int x = 30; x < 55; x++) {
			for (int y = 30; y < 55; y++) {
				dMonster[x][y] = 0;
				dCorpse[x][y] = 0;
			}
		}
		for (size_t i = 0; i < ActiveMonsterCount; i++)
			Monsters[ActiveMonsters[i]] = {};
		ActiveMonsterCount = 0;
	}

	/** @brief A dungeon to fight in, with the network up for the hits. */
	static void EnterDungeon()
	{
		leveltype = DTYPE_CATHEDRAL;
		SNetInitializeProvider(SELCONN_LOOPBACK, nullptr);
	}
};

// Twin Fangs (POE2's Escalating Poison): one more poison stack, 20% shorter poisons.
TEST_F(Poe2ChaosGemsTest, TwinFangsStacksPoisonOnceMore)
{
	Player &player = *MyPlayer;
	const SkillSlot cobra = SkillSlot::ForAttack(WeaponSkill::ViperStrike);
	const SupportData *data = FindSupportData(SupportGem::TwinFangs);
	ASSERT_NE(data, nullptr);
	EXPECT_EQ(data->name, "Twin Fangs");
	EXPECT_TRUE(SupportGemCanSupport(SupportGem::TwinFangs, cobra));
	EXPECT_TRUE(SupportGemCanSupport(SupportGem::TwinFangs, SkillSlot::ForAttack(WeaponSkill::WitheringBolt)));
	EXPECT_FALSE(SupportGemCanSupport(SupportGem::TwinFangs, SkillSlot::ForSpell(SpellID::Fireball)));
	EXPECT_EQ(SupportTierGemLevel(SupportGem::TwinFangs, 1, 0), 1);

	// Without it, two equal poisons are one.
	Supported(cobra);
	Monster &plain = PlaceMonster(0, { 45, 45 });
	{
		const SupportHitScope scope(player, cobra);
		InflictPoison(player, plain, 40 * 100, PoisonTicks);
		InflictPoison(player, plain, 40 * 100, PoisonTicks);
	}
	EXPECT_EQ(plain.poe2.poisonDamagePerTick, 100);
	EXPECT_EQ(plain.poe2.poisonTicks, PoisonTicks);

	// With it, they add up to two, and each lasts 20% shorter with the same damage a tick.
	Supported(cobra, SupportGem::TwinFangs);
	Monster &stacked = PlaceMonster(1, { 46, 45 });
	{
		const SupportHitScope scope(player, cobra);
		for (int i = 0; i < 3; i++)
			InflictPoison(player, stacked, 40 * 100, PoisonTicks);
	}
	EXPECT_EQ(stacked.poe2.poisonDamagePerTick, 200);
	EXPECT_EQ(stacked.poe2.poisonTicks, PoisonTicks * 80 / 100);
}

// Deadly Venom (POE2's Deadly Poison I-II): more poison damage, less damage with hits.
TEST_F(Poe2ChaosGemsTest, DeadlyVenomTradesHitDamageForPoison)
{
	Player &player = *MyPlayer;
	const SkillSlot cobra = SkillSlot::ForAttack(WeaponSkill::ViperStrike);
	EXPECT_TRUE(SupportGemCanSupport(SupportGem::DeadlyVenom, cobra));
	EXPECT_FALSE(SupportGemCanSupport(SupportGem::DeadlyVenom, SkillSlot::ForSpell(SpellID::ChainLightning)));
	EXPECT_EQ(SupportTierCount(SupportGem::DeadlyVenom), 2);
	EXPECT_EQ(SupportTierGemLevel(SupportGem::DeadlyVenom, 1, 0), 3);
	EXPECT_EQ(SupportTierGemLevel(SupportGem::DeadlyVenom, 2, 0), 10);

	for (const auto [tier, more, less] : { std::array<int, 3> { 1, 75, 25 }, std::array<int, 3> { 2, 100, 30 } }) {
		Supported(cobra, SupportGem::DeadlyVenom, tier);
		Monster &monster = PlaceMonster(0, { 45, 45 });
		{
			const SupportHitScope scope(player, cobra);
			EXPECT_EQ(SupportHitDamagePercent(monster, DamageType::Physical), 100 - less) << tier;
			InflictPoison(player, monster, 40 * 100, PoisonTicks);
		}
		EXPECT_EQ(monster.poe2.poisonDamagePerTick, 100 * (100 + more) / 100) << tier;
		EXPECT_EQ(monster.poe2.poisonTicks, PoisonTicks) << tier;
		ClearMonsters();
	}
	// Outside a hit of the supported skill, poison is as it was.
	Monster &other = PlaceMonster(0, { 45, 45 });
	InflictPoison(player, other, 40 * 100, PoisonTicks);
	EXPECT_EQ(other.poe2.poisonDamagePerTick, 100);
}

// Dark Enchant (POE2's Chaos Attunement): hits gain chaos, other damage is less.
TEST_F(Poe2ChaosGemsTest, DarkEnchantTurnsHitsTowardChaos)
{
	Player &player = *MyPlayer;
	EnterDungeon();
	const SkillSlot cleave = SkillSlot::ForAttack(WeaponSkill::Cleave);
	const SkillSlot bolt = SkillSlot::ForAttack(WeaponSkill::WitheringBolt);
	const SkillSlot firebolt = SkillSlot::ForSpell(SpellID::Firebolt);
	for (const SkillSlot skill : { cleave, bolt, firebolt })
		EXPECT_TRUE(SupportGemCanSupport(SupportGem::DarkEnchant, skill));
	EXPECT_EQ(SupportTierGemLevel(SupportGem::DarkEnchant, 1, 0), 6);

	// 50 physical got through at 50% less: the gain is 25% of the 100 before it, as chaos the zombie doesn't resist.
	Supported(cleave, SupportGem::DarkEnchant);
	Monster &hit = PlaceMonster(0, { 45, 45 });
	ASSERT_EQ(MonsterDamageTakenPercent(hit, DamageType::Chaos), 100);
	{
		const SupportHitScope scope(player, cleave);
		EXPECT_EQ(SupportHitDamagePercent(hit, DamageType::Physical), 50);
		EXPECT_EQ(SupportHitDamagePercent(hit, DamageType::Chaos), 100);
		NoteSupportedDamage(hit, DamageType::Physical, Fixed26_6::fromInt(50));
	}
	EXPECT_EQ(hit.hitPoints, Fixed26_6::fromInt(100000 - 25));

	// A chaos skill takes the gain and nothing less.
	Supported(bolt, SupportGem::DarkEnchant);
	Monster &chaos = PlaceMonster(1, { 46, 45 });
	{
		const SupportHitScope scope(player, bolt);
		EXPECT_EQ(SupportHitDamagePercent(chaos, DamageType::Chaos), 100);
		NoteSupportedDamage(chaos, DamageType::Chaos, Fixed26_6::fromInt(40));
	}
	EXPECT_EQ(chaos.hitPoints, Fixed26_6::fromInt(100000 - 10));

	// Fire spells are less too.
	Supported(firebolt, SupportGem::DarkEnchant);
	Monster &burnt = PlaceMonster(2, { 47, 45 });
	{
		const SupportHitScope scope(player, firebolt);
		EXPECT_EQ(SupportHitDamagePercent(burnt, DamageType::Fire), 50);
	}

	// Without it, nothing extra.
	Supported(cleave);
	Monster &plain = PlaceMonster(3, { 48, 45 });
	{
		const SupportHitScope scope(player, cleave);
		NoteSupportedDamage(plain, DamageType::Physical, Fixed26_6::fromInt(50));
	}
	EXPECT_EQ(plain.hitPoints, Fixed26_6::fromInt(100000));
}

// Cleansing (POE2's Impurity): an aura of chaos resistance that grows with the gem.
TEST_F(Poe2ChaosGemsTest, CleansingGrantsChaosResistance)
{
	Player &player = *MyPlayer;
	EXPECT_EQ(PersistentSkillName(PersistentSkill::Impurity), "Cleansing");
	EXPECT_EQ(SpiritReservation(PersistentSkill::Impurity), 30);
	EXPECT_EQ(CleansingChaosResistance(1), 10);
	EXPECT_EQ(CleansingChaosResistance(20), 29);
	const SkillSlot cleansing = SkillSlot::ForPersistent(PersistentSkill::Impurity);
	Item gem;
	ASSERT_TRUE(CreateSkillGemItem(gem, cleansing, 1));
	EXPECT_EQ(SkillForGem(gem), cleansing);

	// A sceptre's Spirit carries it.
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		if (AllItemsList[i].iName == std::string_view("Rattling Sceptre"))
			InitializeItem(player.InvBody[INVLOC_HAND_LEFT], static_cast<_item_indexes>(i));
	}
	player.InvBody[INVLOC_HAND_LEFT]._iStatFlag = true;
	CalcPlrInv(player, false);
	ASSERT_EQ(player.poe2.chaosResistance, 0);
	SocketSkill(player, 0, cleansing);
	if (!player.poe2.spiritGemOn(0))
		ToggleSpiritGem(player, 0);
	ASSERT_TRUE(IsPersistentSkillActive(player, PersistentSkill::Impurity));
	CalcPlrInv(player, false);
	EXPECT_EQ(player.poe2.chaosResistance, 10);
	// A level 20 gem needs more Magic.
	player._pBaseMag = player._pMagic = 250;
	player.poe2.slotLevels[0] = 20;
	CalcPlrInv(player, false);
	ASSERT_TRUE(IsPersistentSkillActive(player, PersistentSkill::Impurity));
	EXPECT_EQ(player.poe2.chaosResistance, 29);
	// Salvation doesn't touch chaos, and Cleansing doesn't touch the elements.
	EXPECT_EQ(player._pFireResist, 0);
	EXPECT_EQ(player._pLghtResist, 0);
	EXPECT_EQ(player._pMagResist, 0);

	// It respects the cap.
	Item &ring = player.InvBody[INVLOC_RING_LEFT];
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		if (AllItemsList[i].iMiscId == IMISC_RING) {
			InitializeItem(ring, static_cast<_item_indexes>(i));
			break;
		}
	}
	ring._iStatFlag = true;
	ring._iIdentified = true;
	ring._iMagical = ITEM_QUALITY_MAGIC;
	ApplyModEffect(ring, ModEffect::ChaosResistance, 60, 0);
	CalcPlrInv(player, false);
	EXPECT_EQ(player.poe2.uncappedChaosResistance, 89);
	EXPECT_EQ(player.poe2.chaosResistance, MaxResistance);

	// Switched off, it's gone.
	SocketSkill(player, 0, {});
	CalcPlrInv(player, false);
	EXPECT_EQ(player.poe2.chaosResistance, 60);
}

TEST_F(Poe2ChaosGemsTest, SalvationLeavesChaosAlone)
{
	Player &player = *MyPlayer;
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		if (AllItemsList[i].iName == std::string_view("Rattling Sceptre"))
			InitializeItem(player.InvBody[INVLOC_HAND_LEFT], static_cast<_item_indexes>(i));
	}
	player.InvBody[INVLOC_HAND_LEFT]._iStatFlag = true;
	SocketSkill(player, 0, SkillSlot::ForPersistent(PersistentSkill::PurityOfElements));
	if (!player.poe2.spiritGemOn(0))
		ToggleSpiritGem(player, 0);
	CalcPlrInv(player, false);
	ASSERT_TRUE(IsPersistentSkillActive(player, PersistentSkill::PurityOfElements));
	EXPECT_EQ(player._pFireResist, PurityResistance);
	EXPECT_EQ(player.poe2.chaosResistance, 0);
}

// The supports' items exist, appended after the others, and are cut from uncut support gems.
TEST_F(Poe2ChaosGemsTest, ChaosSupportsHaveItems)
{
	for (const SupportGem gem : { SupportGem::TwinFangs, SupportGem::DeadlyVenom, SupportGem::DarkEnchant }) {
		const SupportData *data = FindSupportData(gem);
		ASSERT_NE(data, nullptr);
		EXPECT_GT(static_cast<int>(gem), static_cast<int>(SupportGem::DeadlyHerald));
		Item item;
		ASSERT_TRUE(CreateSupportGemItem(item, gem)) << data->name;
		EXPECT_EQ(SupportGemForItem(item), gem) << data->name;
		EXPECT_FALSE(IsLineageSupport(gem)) << data->name;
	}
}

} // namespace
} // namespace devilution
