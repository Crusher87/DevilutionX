#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "engine/palette.h"
#include "engine/surface.hpp"
#include "game_mode.hpp"
#include "headless_mode.hpp"
#include "levels/gendung.h"
#include "missiles.h"
#include "poe2/player_data.hpp"
#include "poe2/vfx.hpp"
#include "tables/misdat.h"
#include "utils/file_util.h"
#include "utils/paths.h"

namespace devilution {
namespace {

class Poe2VfxTest : public ::testing::Test {
public:
	void SetUp() override
	{
		gbIsPoe2 = true;
		HeadlessMode = false;
		vfx::Clear();
	}

	void TearDown() override
	{
		vfx::Clear();
		HeadlessMode = true;
	}
};

TEST_F(Poe2VfxTest, ElementsFollowTheDamageType)
{
	EXPECT_EQ(vfx::ElementOf(DamageType::Fire, false), vfx::Element::Fire);
	EXPECT_EQ(vfx::ElementOf(DamageType::Cold, true), vfx::Element::Cold);
	EXPECT_EQ(vfx::ElementOf(DamageType::Chaos, true), vfx::Element::Chaos);
	EXPECT_EQ(vfx::ElementOf(DamageType::Physical, false), vfx::Element::Physical);
	EXPECT_EQ(vfx::ElementOf(DamageType::Physical, true), vfx::Element::Holy);
}

TEST_F(Poe2VfxTest, AcidIsChaos)
{
	// Chaos plan step 3: Diablo's acid is the chaos damage type, still 4 in network messages.
	EXPECT_EQ(static_cast<int>(DamageType::Chaos), 4);
	// Each copy of misdat.tsv (the game's, Hellfire's and POE2's) loads its acid missiles as chaos.
	const std::vector<std::string_view> mods[] = { {}, { "hf" }, { "hf", "poe2" } };
	for (const std::vector<std::string_view> &active : mods) {
		UnloadModArchives();
		LoadModArchives(active);
		LoadMissileData();
		for (const MissileID id : { MissileID::Acid, MissileID::AcidSplat, MissileID::AcidPuddle })
			EXPECT_EQ(GetMissileData(id).damageType(), DamageType::Chaos) << active.size() << " " << static_cast<int>(id);
		EXPECT_EQ(GetMissileData(MissileID::Firebolt).damageType(), DamageType::Fire) << active.size();
	}

	// A table that says Chaos instead of Acid reads the same: a loose mod with the game's table, its flags renamed. Its
	// Firebolt turns chaos as well, which shows the mod's table is the one read.
	const std::string previousPrefPath = paths::PrefPath();
	const std::string folder = ::testing::TempDir() + "poe2_vfx_chaos_token" DIRECTORY_SEPARATOR_STR;
	const std::string tables = folder + "mods" DIRECTORY_SEPARATOR_STR "chaostoken" DIRECTORY_SEPARATOR_STR "txtdata" DIRECTORY_SEPARATOR_STR "missiles" DIRECTORY_SEPARATOR_STR;
	RecursivelyCreateDir(tables.c_str());
	ASSERT_TRUE(DirectoryExists(tables.c_str()));
	{
		std::ifstream in(paths::BasePath() + "assets/txtdata/missiles/misdat.tsv", std::ios::binary);
		ASSERT_TRUE(in);
		std::ofstream out(tables + "misdat.tsv", std::ios::binary | std::ios::trunc);
		int renamed = 0;
		for (std::string line; std::getline(in, line);) {
			// The seventh column is the flags.
			size_t start = 0;
			for (int column = 0; column < 6 && start != std::string::npos; column++)
				start = line.find('\t', start) + 1;
			if (line.compare(start, 4, "Acid") == 0) {
				line.replace(start, 4, "Chaos");
				renamed++;
			}
			if (line.starts_with("Firebolt\t") && line.compare(start, 4, "Fire") == 0)
				line.replace(start, 4, "Chaos");
			out << line << '\n';
		}
		EXPECT_EQ(renamed, 3);
	}
	paths::SetPrefPath(folder);
	UnloadModArchives();
	const std::string_view chaosToken[] = { "chaostoken" };
	LoadModArchives(chaosToken);
	LoadMissileData();
	for (const MissileID id : { MissileID::Acid, MissileID::AcidSplat, MissileID::AcidPuddle, MissileID::Firebolt })
		EXPECT_EQ(GetMissileData(id).damageType(), DamageType::Chaos) << static_cast<int>(id);
	paths::SetPrefPath(previousPrefPath);
	UnloadModArchives();
	LoadMissileData();
}

TEST_F(Poe2VfxTest, AnImpactSpraysParticlesThatDieOut)
{
	vfx::Impact({ 40, 40 }, vfx::Element::Fire, 1.F);
	EXPECT_GT(vfx::ParticleCount(), 0U);
	EXPECT_GT(vfx::ShapeCount(), 0U);
	for (int i = 0; i < 200; i++)
		vfx::Advance(50);
	EXPECT_EQ(vfx::ParticleCount(), 0U);
	EXPECT_EQ(vfx::ShapeCount(), 0U);
}

TEST_F(Poe2VfxTest, ASigilLastsAsLongAsItCharges)
{
	vfx::Sigil({ 40, 40 }, 2, 2000, vfx::Element::Arcane);
	ASSERT_EQ(vfx::ShapeCount(), 1U);
	vfx::Advance(1900);
	EXPECT_EQ(vfx::ShapeCount(), 1U);
	vfx::Advance(1000);
	EXPECT_EQ(vfx::ShapeCount(), 0U);
}

TEST_F(Poe2VfxTest, NothingIsMadeOutsidePoe2OrWithoutAScreen)
{
	gbIsPoe2 = false;
	vfx::Showcase({ 40, 40 });
	EXPECT_EQ(vfx::ShapeCount(), 0U);
	gbIsPoe2 = true;
	HeadlessMode = true;
	vfx::Slam({ 40, 40 }, 2, vfx::Element::Physical);
	EXPECT_EQ(vfx::ShapeCount(), 0U);
	EXPECT_EQ(vfx::ParticleCount(), 0U);
}

TEST_F(Poe2VfxTest, ATownPortalIsDrawnByTheEffectsWhileItsMissileLives)
{
	Missiles.clear();
	Missile &portal = Missiles.emplace_back();
	portal._mitype = MissileID::TownPortal;
	portal._miAnimType = MissileGraphicID::None;
	portal.position.tile = { 40, 40 };
	portal._miDrawFlag = true;
	portal._miDelFlag = false;
	portal.setFrameGroup<PortalFrame>(PortalFrame::Start);
	vfx::ScanMissiles();
	EXPECT_EQ(vfx::PortalCount(), 1U);
	// Opening throws its sparks and its light.
	for (int i = 0; i < 20; i++)
		vfx::Advance(50);
	EXPECT_GT(vfx::ParticleCount(), 0U);
	// Gone from the level: it folds shut, then is forgotten.
	Missiles.clear();
	vfx::ScanMissiles();
	EXPECT_EQ(vfx::PortalCount(), 1U);
	for (int i = 0; i < 20; i++)
		vfx::Advance(50);
	EXPECT_EQ(vfx::PortalCount(), 0U);
}

TEST_F(Poe2VfxTest, ArcingSparksAndChargedBoltLookApart)
{
	// Both fly the Charged Bolt missile; the skill that fired it picks the look (Curtis 2026-10-23).
	Missile missile;
	missile._mitype = MissileID::ChargedBolt;
	missile._miAnimType = MissileGraphicID::ChargedBolt;
	const SkillSlot spark = SkillSlot::ForAttack(WeaponSkill::Spark);
	missile.poe2.skillKind = static_cast<uint8_t>(spark.kind);
	missile.poe2.skillId = spark.id;
	EXPECT_TRUE(vfx::IsArcingSpark(missile));
	const SkillSlot chargedBolt = SkillSlot::ForSpell(SpellID::ChargedBolt);
	missile.poe2.skillKind = static_cast<uint8_t>(chargedBolt.kind);
	missile.poe2.skillId = chargedBolt.id;
	EXPECT_FALSE(vfx::IsArcingSpark(missile));
	// A monster's charged bolt has no skill.
	missile.poe2.skillKind = 0;
	missile.poe2.skillId = 0;
	EXPECT_FALSE(vfx::IsArcingSpark(missile));
}

/** @brief A screen of the dungeon showing nothing but one palette colour. */
OwnedSurface ScreenOf(uint8_t colour)
{
	OwnedSurface screen(256, 128);
	for (int y = 0; y < screen.h(); y++) {
		uint8_t *line = screen.at(0, y);
		for (int x = 0; x < screen.w(); x++)
			line[x] = colour;
	}
	return screen;
}

TEST_F(Poe2VfxTest, LavaOnScreenGlowsAndThrowsEmbers)
{
	const dungeon_type oldLevelType = leveltype;
	leveltype = DTYPE_CAVES;
	logical_palette[5] = { 240, 90, 20, 255 };
	vfx::SetCamera({ 40, 40 }, {}, false);
	const OwnedSurface screen = ScreenOf(5);
	vfx::ScanWorld(screen);
	EXPECT_GT(vfx::AmbientCount(), 0U);
	for (int i = 0; i < 20; i++)
		vfx::Advance(100);
	EXPECT_GT(vfx::ParticleCount(), 0U);
	leveltype = oldLevelType;
}

TEST_F(Poe2VfxTest, LowColoursAreOnlyFluidWhereThePaletteMoves)
{
	const dungeon_type oldLevelType = leveltype;
	leveltype = DTYPE_CATHEDRAL;
	logical_palette[5] = { 240, 90, 20, 255 };
	vfx::SetCamera({ 40, 40 }, {}, false);
	const OwnedSurface screen = ScreenOf(5);
	vfx::ScanWorld(screen);
	EXPECT_EQ(vfx::AmbientCount(), 0U);
	leveltype = oldLevelType;
}

} // namespace
} // namespace devilution
