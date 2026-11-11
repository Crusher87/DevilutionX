#include <algorithm>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "missiles.h"
#include "player.h"
#include "poe2/table_skills.hpp"
#include "poe2/vfx.hpp"
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
	// MetalHit has no sprite file of its own, so it borrows the stone curse's grey shatter (the effects draw a steel hit).
	EXPECT_EQ(missile._miAnimType, MissileGraphicID::StoneCurseShatter);
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
		if (const TableSkillData *data = FindTableSkill(skill); data != nullptr && (data->flags & TF_Spell) != 0 && data->shape != TableShape::Curse && TableSkillLook(*data) == vfx::Element::Poison)
			continue; // The poison spells flash the acid beasts' splat, loaded with every level's graphics in game but not here.
		Missiles.clear();
		MyPlayer->position.tile = { 40, 40 };
		PlaySkillEffects(*MyPlayer, skill, { 43, 40 });
		const TableSkillData *data = FindTableSkill(skill);
		if (data == nullptr || TableSkillCastFlash(*data)) {
			EXPECT_FALSE(Missiles.empty()) << WeaponSkillName(skill);
			continue;
		}
		// Rogue run 2: only lightning flashes where a skill starts. A bow's arrows and a spell's bolts carry their own look,
		// so nothing is left at the hero's feet as they go off (a blue spark on Plague Arrow, flames under Fire Arrow).
		if (IsAnyOf(data->shape, TableShape::Projectiles, TableShape::Stream)) {
			for (const Missile &missile : Missiles)
				EXPECT_NE(missile._mitype, MissileID::VisualEffect) << WeaponSkillName(skill);
		}
	}
}

bool IsLightningGraphic(MissileGraphicID graphic)
{
	return IsAnyOf(graphic, MissileGraphicID::ChargedBolt, MissileGraphicID::LightningArrow, MissileGraphicID::Lightning, MissileGraphicID::ThinLightning,
	    MissileGraphicID::YellowFlare, MissileGraphicID::YellowFlareExplosion, MissileGraphicID::BloodStarYellow, MissileGraphicID::BloodStarYellowExplosion);
}

/** @brief The skills whose look is lightning: their own damage is lightning, or they crackle by design (Smite's bolt from the sky). */
bool IsLightningSkill(WeaponSkill skill)
{
	if (const TableSkillData *data = FindTableSkill(skill); data != nullptr)
		return data->type == DamageType::Lightning;
	return IsAnyOf(skill, WeaponSkill::LightningArrow, WeaponSkill::Smite, WeaponSkill::FistsOfThunder, WeaponSkill::Spark);
}

TEST_F(Poe2SkillEffectsTest, PhysicalHitsShowNoLightning)
{
	// Rogue run 2: every Cleave hit logged "visual MetalHit" then "hit flash ChargedBolt": the blow of steel borrowed Charged
	// Bolt's blue crackle, and the owner won't have lightning on skills that aren't lightning.
	AddVisualEffect({ 40, 40 }, MissileGraphicID::MetalHit, 0, 3);
	ASSERT_EQ(Missiles.size(), 1U);
	const Missile &hit = Missiles.front();
	EXPECT_FALSE(IsLightningGraphic(hit._miAnimType));
	// The effects draw it as a pale flash and sparks of steel.
	EXPECT_EQ(hit.poe2.vfxElement, static_cast<int8_t>(vfx::Element::Physical));
	EXPECT_TRUE(vfx::IsSteelHit(hit));
	// No skill that isn't lightning leaves a lightning sprite anywhere.
	for (int i = 1; i <= static_cast<int>(WeaponSkill::Last); i++) {
		const auto skill = static_cast<WeaponSkill>(i);
		if (IsLightningSkill(skill) || (IsGrantedSkill(skill) && FindTableSkill(skill) == nullptr))
			continue;
		Missiles.clear();
		MyPlayer->position.tile = { 40, 40 };
		PlaySkillEffects(*MyPlayer, skill, { 43, 40 });
		for (const Missile &missile : Missiles) {
			if (missile._mitype == MissileID::VisualEffect)
				EXPECT_FALSE(IsLightningGraphic(missile._miAnimType)) << WeaponSkillName(skill) << " " << static_cast<int>(missile._miAnimType);
		}
	}
	// A lightning skill keeps its lightning.
	Missiles.clear();
	PlaySkillEffects(*MyPlayer, WeaponSkill::Spark, { 43, 40 });
	EXPECT_TRUE(std::any_of(Missiles.begin(), Missiles.end(), [](const Missile &missile) { return IsLightningGraphic(missile._miAnimType); }));
}

TEST_F(Poe2SkillEffectsTest, NoEffectsOutsidePoe2)
{
	gbIsPoe2 = false;
	AddVisualEffect({ 40, 40 }, MissileGraphicID::MetalHit);
	EXPECT_TRUE(Missiles.empty());
}

} // namespace
} // namespace devilution
