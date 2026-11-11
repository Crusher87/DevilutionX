#include <vector>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "missiles.h"
#include "objects.h"
#include "player.h"
#include "poe2/skill_resources.hpp"
#include "poe2/table_skills.hpp"
#include "poe2/weapon_skills.hpp"
#include "spells.h"
#include "storm/storm_net.hpp"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

/** Area skills smash the barrels and urns in their area (Curtis 2026-10-22: they only broke the one swung at). */
class Poe2BreakablesTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(2);
		MyPlayer = &Players[0];
		MyPlayerId = 0;
		*MyPlayer = {};
		MyPlayer->poe2.clear();
		MyPlayer->position.tile = Hero;
		MyPlayer->_pdir = Direction::South;
		gbIsSpawn = false;
		gbIsMultiplayer = false;
		gbIsHellfire = true;
		gbIsPoe2 = true;
		leveltype = DTYPE_CATHEDRAL;
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		LoadItemData();
		LoadSpellData();
		LoadPlayerDataFiles();
		LoadMissileData();
		Missiles.clear();
		// Breaking a barrel tells the other players.
		SNetInitializeProvider(SELCONN_LOOPBACK, nullptr);
		ActiveObjectCount = 0;
	}

	void TearDown() override
	{
		for (const Point tile : placed)
			dObject[tile.x][tile.y] = 0;
		ActiveObjectCount = 0;
		Missiles.clear();
		ClearSkillZones();
		MyPlayer->poe2.castSkill = WeaponSkill::None;
		leveltype = DTYPE_TOWN;
		gbIsPoe2 = false;
		UnloadModArchives();
	}

	Object &PlaceBarrel(Point tile)
	{
		const int id = ActiveObjectCount;
		ActiveObjects[ActiveObjectCount++] = id;
		Object &barrel = Objects[id];
		barrel = {};
		barrel._otype = OBJ_BARREL;
		barrel._oBreak = 1;
		barrel._oSolidFlag = true;
		// Neither an item nor a skeleton inside.
		barrel._oVar2 = 5;
		barrel._oVar4 = -1;
		barrel.position = tile;
		barrel.selectionRegion = SelectionRegion::Bottom;
		dObject[tile.x][tile.y] = static_cast<int8_t>(id + 1);
		placed.push_back(tile);
		return barrel;
	}

	static constexpr Point Hero { 40, 40 };
	std::vector<Point> placed;
};

TEST_F(Poe2BreakablesTest, AreaBreaksEveryBarrelInIt)
{
	const Object &near = PlaceBarrel(Hero + Displacement { 1, 1 });
	const Object &edge = PlaceBarrel(Hero + Displacement { 0, -2 });
	const Object &far = PlaceBarrel(Hero + Displacement { 3, 0 });
	BreakObjectsInArea(*MyPlayer, Hero, 2);
	EXPECT_TRUE(near.IsBroken());
	EXPECT_TRUE(edge.IsBroken());
	EXPECT_FALSE(far.IsBroken());
	EXPECT_TRUE(far.IsBreakable());
}

TEST_F(Poe2BreakablesTest, OnlyTheLocalHerosSkillsBreakThemHere)
{
	// Another player's game breaks them and sends the break.
	const Object &barrel = PlaceBarrel(Hero + Displacement { 1, 0 });
	BreakObjectsInArea(Players[1], Hero, 2);
	BreakObjectAt(Players[1], barrel.position);
	EXPECT_FALSE(barrel.IsBroken());

	// Diablo and Hellfire keep their own rules.
	gbIsPoe2 = false;
	BreakObjectsInArea(*MyPlayer, Hero, 2);
	EXPECT_FALSE(barrel.IsBroken());
}

TEST_F(Poe2BreakablesTest, AroundSkillSmashesTheBarrelsAroundTheHero)
{
	// Wave of Light bursts out 3 tiles all around the hero.
	const TableSkillData *wave = FindTableSkill(WeaponSkill::WaveOfLight);
	ASSERT_NE(wave, nullptr);
	ASSERT_EQ(wave->radius, 3);
	const Object &behind = PlaceBarrel(Hero + Displacement { -3, 0 });
	const Object &aside = PlaceBarrel(Hero + Displacement { 2, -2 });
	const Object &beyond = PlaceBarrel(Hero + Displacement { 5, 0 });
	MyPlayer->poe2.castSkill = WeaponSkill::WaveOfLight;
	ExecuteSkillCast(*MyPlayer, WeaponSkill::WaveOfLight, Hero + Displacement { 0, 3 });
	EXPECT_TRUE(behind.IsBroken());
	EXPECT_TRUE(aside.IsBroken());
	EXPECT_FALSE(beyond.IsBroken());
}

TEST_F(Poe2BreakablesTest, BurstSmashesTheBarrelsAroundIt)
{
	const Point center = Hero + Displacement { 4, 4 };
	const Object &inside = PlaceBarrel(center + Displacement { 1, 0 });
	const Object &outside = PlaceBarrel(center + Displacement { 3, 0 });
	SkillBurst(*MyPlayer, center, 2, DamageType::Fire, Fixed26_6::fromInt(10), MissileGraphicID::Inferno);
	EXPECT_TRUE(inside.IsBroken());
	EXPECT_FALSE(outside.IsBroken());
}

} // namespace
} // namespace devilution
