#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "missiles.h"
#include "player.h"
#include "poe2/table_skills.hpp"
#include "poe2/weapon_skills.hpp"
#include "utils/is_of.hpp"

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
	// Function addresses differ across the DLL boundary on Windows, so check the row loaded instead.
	const MissileData &data = GetMissileData(MissileID::VisualEffect);
	EXPECT_NE(data.addFn, nullptr);
	EXPECT_NE(data.processFn, nullptr);
	EXPECT_NE(data.addFn, GetMissileData(MissileID::OrangeExplosion).addFn);
}

TEST_F(Poe2SkillEffectsTest, EffectsPlayOnceAfterTheirDelay)
{
	AddVisualEffect({ 40, 40 }, MissileGraphicID::MetalHit, /*delay=*/3);
	ASSERT_EQ(Missiles.size(), 1U);
	Missile &missile = Missiles.front();
	EXPECT_EQ(missile._mitype, MissileID::VisualEffect);
	// MetalHit has no sprite file of its own, so it borrows Charged Bolt's crackle.
	EXPECT_EQ(missile._miAnimType, MissileGraphicID::ChargedBolt);
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
		if (IsAnyOf(skill, WeaponSkill::Barrage, WeaponSkill::RapidFire))
			continue; // Their volley is drawn by the effects layer alone, with no sprite.
		if (IsGrantedSkill(skill) && FindTableSkill(skill) == nullptr)
			continue; // A caster weapon's granted skill that isn't built yet.
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
