#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "missiles.h"
#include "player.h"
#include "poe2/hud_minions.hpp"
#include "poe2/player_data.hpp"
#include "poe2/shrines.hpp"
#include "poe2/support_table.hpp"
#include "poe2/table_skills.hpp"
#include "poe2/weapon_skills.hpp"
#include "spells.h"
#include "storm/storm_net.hpp"
#include "tables/monstdat.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

constexpr int Ticks = 20;

/** @brief The skill gems' timed buffs on the hero get an icon in the top left row, like the shrine buffs. */
class Poe2GemBuffIconsTest : public ::testing::Test {
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
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		LoadItemData();
		LoadSpellData();
		LoadPlayerDataFiles();
		LoadMissileData();
		LoadMonsterData();
		LoadShrineData();
		Missiles.clear();
		ClearSkillZones();
		ClearSupportEffects();
	}

	void TearDown() override
	{
		Missiles.clear();
		ClearSkillZones();
		ClearSupportEffects();
		MyPlayer->poe2.clear();
		gbIsPoe2 = false;
		leveltype = DTYPE_TOWN;
		LoadShrineData();
		UnloadModArchives();
	}

	/** @brief A hero in a dungeon, with the network up, nothing near him. */
	static Player &Hero()
	{
		leveltype = DTYPE_CATHEDRAL;
		SNetInitializeProvider(SELCONN_LOOPBACK, nullptr);
		Player &player = *MyPlayer;
		player.setCharacterLevel(30);
		player._pBaseStr = player._pStrength = 100;
		player._pMaxHP = player._pHitPoints = Fixed26_6::fromInt(200);
		player.position.tile = player.position.future = player.position.old = { 40, 40 };
		player._pdir = Direction::SouthEast;
		return player;
	}

	static void Use(Player &player, WeaponSkill skill)
	{
		player.poe2.castSkill = skill;
		ExecuteSkillCast(player, skill, player.position.tile + Direction::SouthEast);
		player.poe2.castSkill = WeaponSkill::None;
	}

	static void Tick(Player &player, int ticks)
	{
		for (int i = 0; i < ticks; i++)
			player.poe2.tick();
	}

	/** @brief The gem's icon in the row, if it has one. */
	static std::optional<TimedHudIcon> IconOf(const Player &player, WeaponSkill skill)
	{
		const std::vector<TimedHudIcon> icons = TimedHudIcons(player);
		const auto found = std::find_if(icons.begin(), icons.end(), [skill](const TimedHudIcon &icon) { return icon.skill == SkillSlot::ForAttack(skill); });
		if (found == icons.end())
			return std::nullopt;
		return *found;
	}

	static int Duration(WeaponSkill skill)
	{
		const TableSkillData *data = FindTableSkill(skill);
		EXPECT_NE(data, nullptr);
		return data != nullptr ? data->duration : 0;
	}
};

TEST_F(Poe2GemBuffIconsTest, NothingCastShowsNothing)
{
	EXPECT_TRUE(TimedHudIcons(Hero()).empty());
}

TEST_F(Poe2GemBuffIconsTest, BattleCryShowsItsTimeAndRunsOut)
{
	Player &player = Hero();
	Use(player, WeaponSkill::BattleCry);
	std::optional<TimedHudIcon> icon = IconOf(player, WeaponSkill::BattleCry);
	ASSERT_TRUE(icon);
	EXPECT_EQ(icon->ticksLeft, BattleCryTicks);
	EXPECT_EQ(icon->ticksTotal, BattleCryTicks);
	EXPECT_EQ(icon->count, 0) << "it has no hits to count";
	EXPECT_FALSE(icon->harmful);
	EXPECT_EQ(icon->tooltip, "Battle Cry (8s)");

	Tick(player, 3 * Ticks);
	icon = IconOf(player, WeaponSkill::BattleCry);
	ASSERT_TRUE(icon);
	EXPECT_EQ(icon->ticksLeft, BattleCryTicks - (3 * Ticks));
	EXPECT_EQ(icon->ticksTotal, BattleCryTicks) << "the ring drains from the whole time";
	EXPECT_EQ(icon->tooltip, "Battle Cry (5s)");

	Tick(player, BattleCryTicks - (3 * Ticks));
	EXPECT_FALSE(IconOf(player, WeaponSkill::BattleCry));
	EXPECT_TRUE(TimedHudIcons(player).empty());
}

TEST_F(Poe2GemBuffIconsTest, WarCriesCountTheirHits)
{
	Player &player = Hero();
	Use(player, WeaponSkill::HellfireRoar);
	std::optional<TimedHudIcon> icon = IconOf(player, WeaponSkill::HellfireRoar);
	ASSERT_TRUE(icon);
	const int duration = Duration(WeaponSkill::HellfireRoar);
	EXPECT_EQ(duration, 8 * Ticks);
	EXPECT_EQ(icon->ticksLeft, duration);
	EXPECT_EQ(icon->ticksTotal, duration);
	EXPECT_EQ(icon->count, 3);
	EXPECT_EQ(icon->tooltip, "Hellfire Roar (8s)");

	// A hit spent shows on the icon.
	player.poe2.cryHits[static_cast<size_t>(EmpoweringCry::Hellfire)] = 2;
	Tick(player, Ticks);
	icon = IconOf(player, WeaponSkill::HellfireRoar);
	ASSERT_TRUE(icon);
	EXPECT_EQ(icon->count, 2);
	EXPECT_EQ(icon->ticksLeft, duration - Ticks);

	// The cry is over with its last hit, time left or not.
	player.poe2.cryHits[static_cast<size_t>(EmpoweringCry::Hellfire)] = 0;
	EXPECT_FALSE(IconOf(player, WeaponSkill::HellfireRoar));

	// And when its time runs out with hits unused.
	Use(player, WeaponSkill::CryOfBulKathos);
	ASSERT_TRUE(IconOf(player, WeaponSkill::CryOfBulKathos));
	EXPECT_EQ(IconOf(player, WeaponSkill::CryOfBulKathos)->count, 3);
	Tick(player, Duration(WeaponSkill::CryOfBulKathos) - 1);
	ASSERT_TRUE(IconOf(player, WeaponSkill::CryOfBulKathos));
	EXPECT_EQ(IconOf(player, WeaponSkill::CryOfBulKathos)->ticksLeft, 1);
	Tick(player, 1);
	EXPECT_FALSE(IconOf(player, WeaponSkill::CryOfBulKathos));

	Use(player, WeaponSkill::QuakingRoar);
	ASSERT_TRUE(IconOf(player, WeaponSkill::QuakingRoar));
	EXPECT_EQ(IconOf(player, WeaponSkill::QuakingRoar)->count, 2);
}

TEST_F(Poe2GemBuffIconsTest, BulwarkRoarStaysUpWithItsWavesSpent)
{
	Player &player = Hero();
	Use(player, WeaponSkill::BulwarkRoar);
	std::optional<TimedHudIcon> icon = IconOf(player, WeaponSkill::BulwarkRoar);
	ASSERT_TRUE(icon);
	EXPECT_EQ(icon->count, 3);
	EXPECT_EQ(icon->ticksTotal, Duration(WeaponSkill::BulwarkRoar));
	// Its block and Guard last the whole cry.
	player.poe2.cryHits[BulwarkRoarCry] = 0;
	icon = IconOf(player, WeaponSkill::BulwarkRoar);
	ASSERT_TRUE(icon);
	EXPECT_EQ(icon->count, 0);
	Tick(player, Duration(WeaponSkill::BulwarkRoar));
	EXPECT_FALSE(IconOf(player, WeaponSkill::BulwarkRoar));
}

TEST_F(Poe2GemBuffIconsTest, ArmourBuffsShowTheirTime)
{
	Player &player = Hero();
	Use(player, WeaponSkill::FrozenArmor);
	std::optional<TimedHudIcon> icon = IconOf(player, WeaponSkill::FrozenArmor);
	ASSERT_TRUE(icon);
	EXPECT_EQ(icon->ticksLeft, 15 * Ticks);
	EXPECT_EQ(icon->ticksTotal, 15 * Ticks);
	EXPECT_EQ(icon->count, 0);
	EXPECT_EQ(icon->tooltip, "Frozen Armor (15s)");

	Use(player, WeaponSkill::HolyShield);
	icon = IconOf(player, WeaponSkill::HolyShield);
	ASSERT_TRUE(icon);
	EXPECT_EQ(icon->ticksLeft, 10 * Ticks);
	EXPECT_EQ(icon->tooltip, "Holy Shield (10s)");
	EXPECT_EQ(TimedHudIcons(player).size(), 2U);

	Tick(player, 10 * Ticks);
	EXPECT_FALSE(IconOf(player, WeaponSkill::HolyShield));
	ASSERT_TRUE(IconOf(player, WeaponSkill::FrozenArmor));
	EXPECT_EQ(IconOf(player, WeaponSkill::FrozenArmor)->tooltip, "Frozen Armor (5s)");
	Tick(player, 5 * Ticks);
	EXPECT_TRUE(TimedHudIcons(player).empty());
}

TEST_F(Poe2GemBuffIconsTest, SoulHarvestCountsItsSouls)
{
	Player &player = Hero();
	const int duration = Duration(WeaponSkill::SoulHarvest);
	player.poe2.soulStacks = 4;
	player.poe2.soulTicks = static_cast<uint16_t>(duration);
	std::optional<TimedHudIcon> icon = IconOf(player, WeaponSkill::SoulHarvest);
	ASSERT_TRUE(icon);
	EXPECT_EQ(icon->count, 4);
	EXPECT_EQ(icon->ticksLeft, duration);
	EXPECT_EQ(icon->ticksTotal, duration);
	Tick(player, duration);
	EXPECT_EQ(player.poe2.soulStacks, 0);
	EXPECT_FALSE(IconOf(player, WeaponSkill::SoulHarvest));

	// A harvest that took no souls does nothing, and shows nothing.
	player.poe2.soulTicks = static_cast<uint16_t>(duration);
	EXPECT_FALSE(IconOf(player, WeaponSkill::SoulHarvest));
}

TEST_F(Poe2GemBuffIconsTest, GemBuffsComeAfterTheShrines)
{
	Player &player = Hero();
	const ShrineData &shrine = GetShrineData(ShrineKind::Hidden);
	AddShrineBuff(player.poe2.shrines, ShrineKind::Hidden, 0, shrine.seconds * Ticks, shrine.refresh);
	Use(player, WeaponSkill::BattleCry);
	const std::vector<TimedHudIcon> icons = TimedHudIcons(player);
	ASSERT_EQ(icons.size(), 2U);
	EXPECT_TRUE(icons[0].skill.isEmpty());
	EXPECT_EQ(icons[1].skill, SkillSlot::ForAttack(WeaponSkill::BattleCry));
}

TEST_F(Poe2GemBuffIconsTest, OnlyInPoe2)
{
	Player &player = Hero();
	Use(player, WeaponSkill::BattleCry);
	ASSERT_FALSE(TimedHudIcons(player).empty());
	gbIsPoe2 = false;
	EXPECT_TRUE(TimedHudIcons(player).empty());
	gbIsPoe2 = true;
}

} // namespace
} // namespace devilution
