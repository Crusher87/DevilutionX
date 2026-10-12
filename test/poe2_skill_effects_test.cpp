#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "missiles.h"
#include "player.h"
#include "poe2/weapon_skills.hpp"

namespace devilution {
namespace {

class Poe2SkillEffectsTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayerId = 0;
		MyPlayer = &Players[0];
		*MyPlayer = {};
		gbIsSpawn = false;
		gbIsMultiplayer = false;
		gbIsHellfire = true;
		gbIsPoe2 = true;
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		LoadMissileData();
		Missiles.clear();
	}

	void TearDown() override
	{
		Missiles.clear();
		gbIsPoe2 = false;
		UnloadModArchives();
		LoadMissileData();
	}
};

TEST_F(Poe2SkillEffectsTest, TheEffectMissileIsInTheModData)
{
	const MissileData &data = GetMissileData(MissileID::VisualEffect);
	EXPECT_EQ(data.addFn, static_cast<void (*)(Missile &, AddMissileParameter &)>(&AddVisualEffect));
	EXPECT_EQ(data.processFn, &ProcessVisualEffect);
}

TEST_F(Poe2SkillEffectsTest, EffectsPlayOnceAfterTheirDelay)
{
	AddVisualEffect({ 40, 40 }, MissileGraphicID::MetalHit, /*delay=*/3);
	ASSERT_EQ(Missiles.size(), 1U);
	Missile &missile = Missiles.front();
	EXPECT_EQ(missile._mitype, MissileID::VisualEffect);
	EXPECT_EQ(missile._miAnimType, MissileGraphicID::MetalHit);
	EXPECT_EQ(missile.position.tile, Point(40, 40));
	EXPECT_FALSE(missile._miDrawFlag);

	for (int i = 0; i < 3; i++)
		ProcessVisualEffect(missile);
	EXPECT_FALSE(missile._miDrawFlag);
	ProcessVisualEffect(missile);
	EXPECT_TRUE(missile._miDrawFlag);

	for (int i = 0; i < 100 && !missile._miDelFlag; i++)
		ProcessVisualEffect(missile);
	EXPECT_TRUE(missile._miDelFlag);
}

TEST_F(Poe2SkillEffectsTest, EverySkillHasEffects)
{
	for (int i = 1; i <= static_cast<int>(WeaponSkill::Last); i++) {
		const auto skill = static_cast<WeaponSkill>(i);
		if (skill == WeaponSkill::LightningArrow)
			continue; // Its burst plays where the arrow lands.
		Missiles.clear();
		MyPlayer->position.tile = { 40, 40 };
		PlaySkillEffects(*MyPlayer, skill, { 43, 40 });
		EXPECT_FALSE(Missiles.empty()) << WeaponSkillName(skill);
	}
}

TEST_F(Poe2SkillEffectsTest, NoEffectsOutsidePoe2)
{
	gbIsPoe2 = false;
	AddVisualEffect({ 40, 40 }, MissileGraphicID::MetalHit);
	EXPECT_TRUE(Missiles.empty());
}

} // namespace
} // namespace devilution
