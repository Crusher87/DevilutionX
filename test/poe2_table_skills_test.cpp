#include <algorithm>
#include <array>
#include <optional>
#include <set>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "lighting.h"
#include "missiles.h"
#include "monster.h"
#include "objects.h"
#include "player.h"
#include "poe2/bases.hpp"
#include "poe2/gem_levels.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/spell_base.hpp"
#include "poe2/support_table.hpp"
#include "poe2/table_skills.hpp"
#include "poe2/unique_effects.hpp"
#include "poe2/weapon_skills.hpp"
#include "spells.h"
#include "storm/storm_net.hpp"
#include "tables/monstdat.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

class Poe2TableSkillsTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayer = &Players[0];
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
		Missiles.clear();
	}

	void TearDown() override
	{
		Missiles.clear();
		ClearSkillZones();
		gbIsPoe2 = false;
		UnloadModArchives();
	}

	/** @brief A hero who hits often, in a dungeon with monsters' data loaded and the network up for their hits. */
	static Player &SetUpFight()
	{
		leveltype = DTYPE_CATHEDRAL;
		LoadMonsterData();
		LevelMonsterTypes[0].type = MT_NZOMBIE;
		MyPlayerId = 0;
		SNetInitializeProvider(SELCONN_LOOPBACK, nullptr);
		Player &player = *MyPlayer;
		player.setCharacterLevel(30);
		player._pBaseDex = player._pDexterity = 200;
		player._pBaseStr = player._pStrength = 100;
		player._pIMinDam = 20;
		player._pIMaxDam = 30;
		player.position.tile = { 40, 40 };
		player._pdir = Direction::East;
		return player;
	}

	/** @brief A normal monster standing on the tile, with plenty of life. */
	static Monster &PlaceMonster(size_t slot, Point tile)
	{
		Monster &monster = Monsters[slot];
		monster = {};
		monster.levelType = 0;
		monster.uniqueType = UniqueMonsterType::None;
		monster.talkMsg = TEXT_NONE;
		monster.lightId = NO_LIGHT;
		monster.maxHitPoints = monster.hitPoints = Fixed26_6::fromInt(100000);
		monster.position.tile = monster.position.old = monster.position.future = tile;
		dMonster[tile.x][tile.y] = static_cast<int16_t>(slot + 1);
		ActiveMonsters[ActiveMonsterCount] = static_cast<unsigned>(slot);
		ActiveMonsterCount++;
		return monster;
	}

	static void ClearMonsters()
	{
		for (int x = 30; x < 50; x++) {
			for (int y = 30; y < 50; y++)
				dMonster[x][y] = 0;
		}
		ActiveMonsterCount = 0;
	}
};

TEST_F(Poe2TableSkillsTest, SpellBaseRollMatchesTheTooltipRange)
{
	Player &player = *MyPlayer;
	player._pMagic = 80;
	EXPECT_EQ(SpellBaseMin(player, 3), 15);
	EXPECT_EQ(SpellBaseMax(player, 3), 24);
	for (uint32_t seed = 1; seed < 50; seed++) {
		SetRndSeed(seed);
		const int rolled = RollSpellBase(player, 3);
		EXPECT_GE(rolled, SpellBaseMin(player, 3));
		EXPECT_LE(rolled, SpellBaseMax(player, 3));
		SetRndSeed(seed);
		EXPECT_EQ(rolled, GenerateRnd(10) + (80 / 8) + 3 + 2) << "same roll as before the helper";
	}
}

TEST_F(Poe2TableSkillsTest, EveryTableSkillHasAGem)
{
	std::set<WeaponSkill> seen;
	size_t gems = 0;
	for (const TableSkillData &data : TableSkills()) {
		EXPECT_TRUE(seen.insert(data.skill).second) << data.name;
		EXPECT_EQ(FindTableSkill(data.skill), &data);
		// A caster weapon's granted skills have no gem (caster-weapons-plan-v2.md 9.1).
		if (IsGrantedTableSkill(data)) {
			EXPECT_FALSE(SkillGemItemIndex(SkillSlot::ForAttack(data.skill)).has_value()) << data.name;
			continue;
		}
		gems++;
		const std::optional<size_t> index = SkillGemItemIndex(SkillSlot::ForAttack(data.skill));
		ASSERT_TRUE(index.has_value()) << data.name;
		EXPECT_EQ(AllItemsList[*index].iMinMLvl, data.level) << data.name;
	}
	// 55, then Despair (chaos damage plan step 11), Plague Arrow and Hex Rupture, then Bone Spikes, Bone Spear and Blood Nova.
	EXPECT_EQ(gems, 61U);
}

TEST_F(Poe2TableSkillsTest, TheTableCoversTheRestOfTheRoster)
{
	for (int i = static_cast<int>(WeaponSkill::Whirlwind); i < static_cast<int>(FirstGrantedSkill); i++)
		EXPECT_NE(FindTableSkill(static_cast<WeaponSkill>(i)), nullptr) << i;
	EXPECT_EQ(FindTableSkill(WeaponSkill::Cleave), nullptr);
	// The granted skills come in as they are built; those that are in are granted rows, and only they are.
	for (int i = static_cast<int>(WeaponSkill::Whirlwind); i <= static_cast<int>(WeaponSkill::Last); i++) {
		const auto skill = static_cast<WeaponSkill>(i);
		if (const TableSkillData *table = FindTableSkill(skill); table != nullptr)
			EXPECT_EQ(IsGrantedTableSkill(*table), IsGrantedSkill(skill)) << table->name;
	}
	EXPECT_NE(FindTableSkill(WeaponSkill::FreezingShards), nullptr);
}

TEST_F(Poe2TableSkillsTest, TableSkillsHaveNamesCostsAndCurves)
{
	LoadBaseData();
	for (const TableSkillData &data : TableSkills()) {
		EXPECT_EQ(WeaponSkillName(data.skill), data.name);
		EXPECT_FALSE(WeaponSkillDescription(data.skill).empty()) << data.name;
		if (IsGrantedTableSkill(data)) {
			// A granted skill has no gem tier, cost or gemcutting tab, and a base grants it (caster-weapons-plan-v2.md 9.1).
			EXPECT_EQ(data.level, 0) << data.name;
			EXPECT_EQ(data.mana, 0) << data.name;
			EXPECT_EQ(data.tabs, 0) << data.name;
			EXPECT_EQ(WeaponSkillManaCost(data.skill), 0) << data.name;
			EXPECT_NE(data.flags & TF_Spell, 0) << data.name;
			bool granted = false;
			for (size_t i = 0; i < AllItemsList.size(); i++) {
				const BaseMeta *base = BaseMetaFor(static_cast<int>(i));
				granted = granted || (base != nullptr && base->grantedSkill == SkillSlot::ForAttack(data.skill));
			}
			EXPECT_TRUE(granted) << data.name;
		} else {
			EXPECT_GT(WeaponSkillManaCost(data.skill), 0) << data.name;
		}
		EXPECT_EQ(WeaponSkillNeedsWeapon(data.skill), data.weapon != TableWeapon::None) << data.name;
		if (data.weapon != TableWeapon::None)
			EXPECT_FALSE(WeaponSkillWeapons(data.skill).empty()) << data.name;
		// The gem grows with its level, unless it is a flat curse or buff.
		const SkillSlot slot = SkillSlot::ForAttack(data.skill);
		EXPECT_GE(GemDamagePercent(slot, 20), GemDamagePercent(slot, 1)) << data.name;
	}
}

TEST_F(Poe2TableSkillsTest, EveryTableSkillGoesOffInAnEmptyDungeon)
{
	leveltype = DTYPE_CATHEDRAL;
	for (const TableSkillData &data : TableSkills()) {
		Missiles.clear();
		MyPlayer->position.tile = { 40, 40 };
		MyPlayer->_pdir = Direction::South;
		MyPlayer->poe2.castSkill = data.skill;
		ExecuteSkillCast(*MyPlayer, data.skill, { 43, 40 });
		for (int tick = 0; tick < 400; tick++) {
			ProcessSkillVolley(*MyPlayer);
			ProcessSkillZones(*MyPlayer);
			MyPlayer->poe2.tick();
		}
		EXPECT_EQ(SkillZoneCount(), 0U) << data.name;
		ClearSkillZones();
	}
	MyPlayer->poe2.castSkill = WeaponSkill::None;
	leveltype = DTYPE_TOWN;
}

TEST_F(Poe2TableSkillsTest, CursesMakeMonstersTakeMoreDamage)
{
	Monster monster {};
	EXPECT_EQ(TableCurseDamageTakenPercent(monster, DamageType::Physical), 0);
	monster.poe2.deathMarkTicks = 10;
	EXPECT_GT(TableCurseDamageTakenPercent(monster, DamageType::Physical), 0);
	monster.poe2.deathMarkTicks = 0;
	monster.poe2.lowerResistTicks = 10;
	// Lower Resist lowers resistance rather than adding damage taken.
	EXPECT_EQ(TableCurseDamageTakenPercent(monster, DamageType::Physical), 0);
	EXPECT_EQ(TableCurseDamageTakenPercent(monster, DamageType::Fire), 0);
	ProcessTableCurses(monster);
	EXPECT_EQ(monster.poe2.lowerResistTicks, 9);
}

TEST_F(Poe2TableSkillsTest, SoulsAddDamage)
{
	EXPECT_EQ(SoulHarvestDamagePercent(*MyPlayer), 0);
	MyPlayer->poe2.soulStacks = 3;
	EXPECT_GT(SoulHarvestDamagePercent(*MyPlayer), 0);
}

TEST_F(Poe2TableSkillsTest, AreaSkillsHitEachMonsterForFiftyToEightyPercent)
{
	// Area skills hit each monster for 50-80% of a hit; a delay, crowd control, a leap or damage over time leaves some
	// outside that band.
	const std::pair<WeaponSkill, int> Percents[] = {
		{ WeaponSkill::Whirlwind, 85 },
		{ WeaponSkill::Fissure, 65 },
		{ WeaponSkill::Shockwave, 70 },
		{ WeaponSkill::CallOfTheAncients, 60 },
		{ WeaponSkill::FallingSword, 100 },
		{ WeaponSkill::LashingTailKick, 65 },
		{ WeaponSkill::WaveOfLight, 50 },
		{ WeaponSkill::CycloneStrike, 50 },
		{ WeaponSkill::BlessedHammer, 20 },
		{ WeaponSkill::Condemn, 50 },
		{ WeaponSkill::BlessedShield, 70 },
		{ WeaponSkill::FistOfTheHeavens, 110 },
		{ WeaponSkill::CorpseExplosion, 50 },
		// Curtis 2026-10-28: 35 -> 400, a tier V area spike (audit-casters-v3.md); then 200 with Firebolt (Q27).
		{ WeaponSkill::GlacialSpike, 200 },
		{ WeaponSkill::Blizzard, 21 },
		{ WeaponSkill::FrozenOrb, 70 },
		{ WeaponSkill::Meteor, 150 },
		{ WeaponSkill::Thunderstorm, 100 },
		{ WeaponSkill::SpikeTrap, 75 },
		{ WeaponSkill::BladeSentinel, 35 },
		{ WeaponSkill::Sentry, 60 },
		{ WeaponSkill::SiegeVolley, 75 },
		{ WeaponSkill::FreezingArrow, 100 },
		{ WeaponSkill::ExplosiveBolt, 110 },
		// Each monster on the dash (Curtis 2026-10-28; it was 150% around where it landed).
		{ WeaponSkill::DashingStrike, 80 },
	};
	for (const auto &[skill, percent] : Percents) {
		const TableSkillData *data = FindTableSkill(skill);
		ASSERT_NE(data, nullptr);
		EXPECT_EQ(data->damagePercent, percent) << data->name;
	}
	EXPECT_EQ(FindTableSkill(WeaponSkill::BlessedHammer)->radius, 1);
	EXPECT_EQ(FindTableSkill(WeaponSkill::SpikeTrap)->radius, 1);
	EXPECT_EQ(FindTableSkill(WeaponSkill::FallingSword)->curveEnd, 340);

	// Pulses: Blessed Hammer 4 x 20 = 80 a monster; Blade Sentinel 5 x 35 = 175 over its 5 seconds.
	const TableSkillData &hammer = *FindTableSkill(WeaponSkill::BlessedHammer);
	EXPECT_EQ(hammer.duration / hammer.interval, 4);
	EXPECT_EQ(hammer.damagePercent * hammer.duration / hammer.interval, 80);
	const TableSkillData &sentinel = *FindTableSkill(WeaponSkill::BladeSentinel);
	EXPECT_EQ(sentinel.duration / sentinel.interval, 5);
	EXPECT_EQ(sentinel.damagePercent * sentinel.duration / sentinel.interval, 175);

	// Landing bursts: a set share of a base hit on top of the arrow; Fire Arrow keeps two thirds of its hit.
	EXPECT_EQ(TableLandingBurstPercent(*FindTableSkill(WeaponSkill::FreezingArrow)), 40);
	EXPECT_EQ(TableLandingBurstPercent(*FindTableSkill(WeaponSkill::ExplosiveBolt)), 45);
	EXPECT_EQ(TableLandingBurstPercent(*FindTableSkill(WeaponSkill::ClusterBolt)), 66);
}

TEST_F(Poe2TableSkillsTest, TableSpellsGrowLikeDiabloSpells)
{
	// Every damaging table spell on Fireball's steps ends at 312% at gem level 20, like the Diablo spells.
	const int fireball = GemCurveDamagePercent(SkillSlot::ForSpell(SpellID::Fireball), 20);
	EXPECT_EQ(fireball, 312);
	int spells = 0;
	for (const TableSkillData &data : TableSkills()) {
		if ((data.flags & TF_Spell) == 0 || data.curve != TableCurve::Fireball)
			continue;
		// The caster weapons' granted skills follow the same curve; the count is of the gems'.
		if (!IsGrantedTableSkill(data))
			spells++;
		EXPECT_EQ(data.curveEnd, 312) << data.name;
		EXPECT_EQ(GemCurveDamagePercent(SkillSlot::ForAttack(data.skill), 20), fireball) << data.name;
	}
	// 12, then Hex Rupture, then Bone Prison, Bone Spikes, Bone Spear and Blood Nova.
	EXPECT_EQ(spells, 17);
}

TEST_F(Poe2TableSkillsTest, PulsingZonesHitOncePerInterval)
{
	leveltype = DTYPE_CATHEDRAL;
	Player &player = *MyPlayer;
	player._pHitPoints = player._pMaxHP = Fixed26_6::fromInt(100);
	// Each pulse plays the skill's effect, so a tick that adds missiles is a pulse.
	const auto pulses = [&player](WeaponSkill skill) {
		Missiles.clear();
		ClearSkillZones();
		player.position.tile = { 40, 40 };
		player._pdir = Direction::South;
		player.poe2.castSkill = skill;
		ExecuteSkillCast(player, skill, { 43, 40 });
		int count = 0;
		for (int tick = 0; tick < 400 && SkillZoneCount() > 0; tick++) {
			const size_t before = Missiles.size();
			ProcessSkillZones(player);
			if (Missiles.size() > before)
				count++;
		}
		player.poe2.castSkill = WeaponSkill::None;
		return count;
	};
	EXPECT_EQ(pulses(WeaponSkill::BlessedHammer), 4);
	EXPECT_EQ(pulses(WeaponSkill::BladeSentinel), 5);
	leveltype = DTYPE_TOWN;
}

TEST_F(Poe2TableSkillsTest, WarCryOnlyStuns)
{
	// Curtis 2026-10-23: War Cry gave Battle Cry's +50% (with no Battle Cry socketed) and shared its timer. Now it stuns only.
	leveltype = DTYPE_CATHEDRAL;
	Player &player = *MyPlayer;
	player.position.tile = { 40, 40 };
	player._pdir = Direction::South;
	ExecuteSkillCast(player, WeaponSkill::WarCry, { 41, 40 });
	EXPECT_EQ(player.poe2.warcryTicks, 0);

	// A Battle Cry already running keeps its own time left.
	player.poe2.warcryTicks = 37;
	ExecuteSkillCast(player, WeaponSkill::WarCry, { 41, 40 });
	EXPECT_EQ(player.poe2.warcryTicks, 37);
	player.poe2.warcryTicks = 0;
	ClearSkillZones();
	leveltype = DTYPE_TOWN;
}

TEST_F(Poe2TableSkillsTest, FistOfTheHeavensBoltsHitForHalfTheSpellBase)
{
	leveltype = DTYPE_CATHEDRAL;
	Player &player = *MyPlayer;
	player._pMagic = 80;
	player.position.tile = { 40, 40 };
	player._pdir = Direction::South;
	Missiles.clear();
	ExecuteSkillCast(player, WeaponSkill::FistOfTheHeavens, { 44, 40 });
	const int curve = GemDamagePercent(SkillSlot::ForAttack(WeaponSkill::FistOfTheHeavens), player.poe2.skillLevel(WeaponSkill::FistOfTheHeavens));
	int bolts = 0;
	for (const Missile &missile : Missiles) {
		if (missile._mitype != MissileID::ChargedBolt)
			continue;
		bolts++;
		EXPECT_EQ(missile.poe2.damagePercent, curve * BurstBoltDamagePercent / 100);
		EXPECT_GE(missile._midam, SpellBaseMin(player, 1));
		EXPECT_LE(missile._midam, SpellBaseMax(player, 1));
	}
	EXPECT_EQ(bolts, FindTableSkill(WeaponSkill::FistOfTheHeavens)->count);
	EXPECT_EQ(BurstBoltDamagePercent, 50);
	ClearSkillZones();
	leveltype = DTYPE_TOWN;
}

TEST_F(Poe2TableSkillsTest, FrostNovaShardsHitForTheSpellBase)
{
	// About two shards reach a monster: 2 x 100 = 200% of the spell base (Curtis 2026-10-28: 25 -> 100 a shard).
	leveltype = DTYPE_CATHEDRAL;
	Player &player = *MyPlayer;
	player._pMagic = 80;
	player.position.tile = { 40, 40 };
	player._pdir = Direction::South;
	Missiles.clear();
	ExecuteSkillCast(player, WeaponSkill::FrostNova, { 44, 40 });
	const int curve = GemDamagePercent(SkillSlot::ForAttack(WeaponSkill::FrostNova), 1);
	int shards = 0;
	for (const Missile &missile : Missiles) {
		if (missile._mitype != MissileID::BlueFlare)
			continue;
		shards++;
		EXPECT_EQ(missile.poe2.damagePercent, curve * FrostNovaShardDamagePercent / 100);
	}
	EXPECT_GT(shards, 0);
	// 25 -> 100 a shard, then 50 with Firebolt (Curtis 2026-10-28, Q27).
	EXPECT_EQ(FrostNovaShardDamagePercent, 50);
	// Its mana 14 -> 10, and Arcing Sparks' 10 -> 6, at gem level 1 (Curtis 2026-10-28); then a third more as spells,
	// 13 and 8 (Curtis 2026-10-29).
	EXPECT_EQ(GemBaseManaCost(SkillSlot::ForAttack(WeaponSkill::FrostNova), 1), 13);
	EXPECT_EQ(GemBaseManaCost(SkillSlot::ForAttack(WeaponSkill::Spark), 1), 8);
	// Haunt: 150 -> 350 over its 5 seconds (Curtis 2026-10-28).
	EXPECT_EQ(FindTableSkill(WeaponSkill::Haunt)->damagePercent, 350);
	EXPECT_EQ(FindTableSkill(WeaponSkill::Haunt)->duration, 100); // 5 seconds of 20 ticks
	leveltype = DTYPE_TOWN;
}

TEST_F(Poe2TableSkillsTest, GlacialSpikeLeadsAWalkingMonster)
{
	// Curtis 2026-10-28: the spike came up behind moving enemies. It erupts where the monster will be.
	Monster monster {};
	monster.mode = MonsterMode::MoveSideways;
	monster.position.tile = { 10, 10 };
	monster.position.future = { 11, 10 };
	monster.var1 = 1;
	monster.var2 = 0;
	monster.animInfo.numberOfFrames = 8;
	monster.animInfo.ticksPerFrame = 1;
	monster.animInfo.currentFrame = 2;
	monster.animInfo.tickCounterOfCurrentFrame = 0;
	// 6 ticks are left of this step, then 8 a tile.
	EXPECT_EQ(PredictMonsterTile(monster, 0), Point(10, 10));
	EXPECT_EQ(PredictMonsterTile(monster, 5), Point(10, 10));
	EXPECT_EQ(PredictMonsterTile(monster, 6), Point(11, 10));
	EXPECT_EQ(PredictMonsterTile(monster, 13), Point(11, 10));
	EXPECT_EQ(PredictMonsterTile(monster, 14), Point(12, 10));
	// A very fast monster outruns the lead.
	EXPECT_EQ(PredictMonsterTile(monster, 200), Point(10 + MaxPredictedSteps, 10));
	// One standing still is hit where it stands.
	monster.mode = MonsterMode::Stand;
	EXPECT_EQ(PredictMonsterTile(monster, 14), Point(10, 10));
	// Glacial Spike's delay is short; the lead covers it.
	EXPECT_LT(FindTableSkill(WeaponSkill::GlacialSpike)->delay, 8);
}

/** @brief The visual effects with this look that are waiting or playing. */
int VisualEffectsShowing(MissileGraphicID look)
{
	int count = 0;
	for (const Missile &missile : Missiles) {
		if (missile._mitype == MissileID::VisualEffect && static_cast<MissileGraphicID>(missile._mispllvl & 0xFF) == look)
			count++;
	}
	return count;
}

TEST_F(Poe2TableSkillsTest, HauntIsASpiritThatJumps)
{
	// Curtis 2026-10-28: "is it a bleed skill, or a ghost that finds a monster then jumps?"
	EXPECT_EQ(WeaponSkillDescription(WeaponSkill::Haunt), "A spirit drains the enemy with chaos over time. When it dies, the spirit jumps to the nearest enemy.");
	EXPECT_EQ(FindTableSkill(WeaponSkill::Haunt)->effect, MissileGraphicID::BoneSpirit);

	leveltype = DTYPE_CATHEDRAL;
	LoadMonsterData();
	LevelMonsterTypes[0].type = MT_NZOMBIE;
	ActiveMonsterCount = 2;
	for (size_t i = 0; i < 2; i++) {
		Monster &monster = Monsters[i];
		monster = {};
		monster.levelType = 0;
		monster.uniqueType = UniqueMonsterType::None;
		monster.maxHitPoints = monster.hitPoints = Fixed26_6::fromInt(100);
		monster.position.tile = { static_cast<WorldTileCoord>(40 + (3 * i)), 40 };
		monster.talkMsg = TEXT_NONE;
		ActiveMonsters[i] = static_cast<unsigned>(i);
	}
	Monster &haunted = Monsters[0];
	haunted.poe2.hauntTicks = 100;
	haunted.poe2.chaosTicks = 11;
	haunted.poe2.chaosSource = -1;
	// The spirit shows on the monster it drains, not a wound.
	ProcessUniqueMonsterAilments(haunted);
	EXPECT_EQ(VisualEffectsShowing(MissileGraphicID::BoneSpirit), 1);
	EXPECT_EQ(VisualEffectsShowing(MissileGraphicID::RedFlareExplosion), 0);

	// When it dies, the spirit leaves it and shows up on the next one.
	Missiles.clear();
	haunted.poe2.chaosSource = 0;
	haunted.poe2.chaosSkill = WeaponSkill::Haunt;
	OnTableCursedMonsterKilled(haunted);
	EXPECT_EQ(Monsters[1].poe2.hauntTicks, 100);
	EXPECT_EQ(Monsters[1].poe2.chaosSkill, WeaponSkill::Haunt);
	EXPECT_EQ(Monsters[1].poe2.bleedTicks, 0);
	EXPECT_EQ(VisualEffectsShowing(MissileGraphicID::BoneSpirit), 2);
	EXPECT_EQ(VisualEffectsShowing(MissileGraphicID::BloodStarRedExplosion), 0);
	ActiveMonsterCount = 0;
}

TEST_F(Poe2TableSkillsTest, CycloneStrikePullsWhatItHitsIn)
{
	// Curtis 2026-10-28, "Cyclone pulls": it was a copy of Wave of Light (radius 3 around the hero).
	Player &player = SetUpFight();
	const auto place = PlaceMonster;
	const auto clearTiles = ClearMonsters;
	const auto cast = [&](WeaponSkill skill, uint32_t seed, bool &farWasHit) {
		clearTiles();
		Monster &far = place(0, { 43, 40 });
		Monster &beside = place(1, { 41, 41 });
		SetRndSeed(seed);
		ExecuteSkillCast(player, skill, { 43, 40 });
		farWasHit = far.hitPoints < far.maxHitPoints;
		// One beside the hero stays where it is.
		EXPECT_EQ(beside.position.tile, Point(41, 41));
		return Point(far.position.tile);
	};
	int pulled = 0;
	for (uint32_t seed = 1; seed <= 10; seed++) {
		bool hit = false;
		const Point after = cast(WeaponSkill::CycloneStrike, seed, hit);
		// A monster it hits comes a step in; one it misses stays.
		EXPECT_EQ(after, hit ? Point(42, 40) : Point(43, 40)) << seed;
		if (hit)
			pulled++;
	}
	EXPECT_GT(pulled, 0);
	// Wave of Light only hits.
	for (uint32_t seed = 1; seed <= 3; seed++) {
		bool hit = false;
		EXPECT_EQ(cast(WeaponSkill::WaveOfLight, seed, hit), Point(43, 40)) << seed;
	}
	clearTiles();
	ClearSkillZones();
	leveltype = DTYPE_TOWN;
}

TEST_F(Poe2TableSkillsTest, DashingStrikeHitsEachMonsterOnItsPathOnce)
{
	// Curtis 2026-10-28: 150% around where it landed, now 80% to each monster along the dash.
	Player &player = SetUpFight();
	const TableSkillData &dash = *FindTableSkill(WeaponSkill::DashingStrike);
	EXPECT_EQ(dash.damagePercent, 80);
	ASSERT_EQ(dash.radius, 1);
	const auto strike = [&](bool dashed, uint32_t seed, std::array<bool, 4> &hit) {
		ClearMonsters();
		// The hero dashed from (40, 40) east to (44, 40).
		player.position.tile = { 44, 40 };
		player.poe2.dashStart = dashed ? Point(40, 40) : Point();
		const std::array<Monster *, 4> monsters {
			&PlaceMonster(0, { 41, 41 }), // beside the start of the path
			&PlaceMonster(1, { 42, 39 }), // beside its middle, and beside three of its tiles
			&PlaceMonster(2, { 45, 41 }), // beside where it landed
			&PlaceMonster(3, { 38, 40 }), // behind where it started: out of reach
		};
		const std::array<Fixed26_6, 4> before { monsters[0]->hitPoints, monsters[1]->hitPoints, monsters[2]->hitPoints, monsters[3]->hitPoints };
		SetRndSeed(seed);
		ExecuteSkillCast(player, WeaponSkill::DashingStrike, { 46, 40 });
		for (size_t i = 0; i < monsters.size(); i++)
			hit[i] = monsters[i]->hitPoints < before[i];
		// The start is used once.
		EXPECT_EQ(player.poe2.dashStart, Point());
	};
	std::array<int, 4> hits {};
	for (uint32_t seed = 1; seed <= 10; seed++) {
		std::array<bool, 4> hit {};
		strike(true, seed, hit);
		for (size_t i = 0; i < hit.size(); i++)
			hits[i] += hit[i] ? 1 : 0;
		// Without a dash, only around where the hero stands.
		strike(false, seed, hit);
		EXPECT_FALSE(hit[0]) << seed;
		EXPECT_FALSE(hit[1]) << seed;
		EXPECT_FALSE(hit[3]) << seed;
	}
	EXPECT_GT(hits[0], 0);
	EXPECT_GT(hits[1], 0);
	EXPECT_GT(hits[2], 0);
	EXPECT_EQ(hits[3], 0);
	ClearMonsters();
	ClearSkillZones();
	player.poe2.dashStart = {};
	leveltype = DTYPE_TOWN;
}

TEST_F(Poe2TableSkillsTest, StackingZonesAreOneCastAtATime)
{
	// Curtis 2026-10-28: Thunderstorm stacked to 11 at once. Thunderstorm, Blessed Hammer and Blizzard are one cast at a
	// time and a recast replaces it; traps and sentries keep their 3.
	leveltype = DTYPE_CATHEDRAL;
	Player &player = *MyPlayer;
	player._pMagic = 80;
	player.position.tile = { 40, 40 };
	player._pdir = Direction::South;
	for (const WeaponSkill skill : { WeaponSkill::Thunderstorm, WeaponSkill::BlessedHammer, WeaponSkill::Blizzard }) {
		ClearSkillZones();
		for (int cast = 0; cast < 4; cast++)
			ExecuteSkillCast(player, skill, { 43, 40 });
		EXPECT_EQ(SkillZoneCount(), 1U) << FindTableSkill(skill)->name;
	}
	ClearSkillZones();
	for (int cast = 0; cast < 5; cast++)
		ExecuteSkillCast(player, WeaponSkill::SpikeTrap, { 43, 40 });
	EXPECT_EQ(SkillZoneCount(), 3U);
	ClearSkillZones();
	leveltype = DTYPE_TOWN;
}

/** @brief Open floor around the hero at 40,40, with no objects or missiles on it. */
void ClearFloor()
{
	SOLData[0] = TileProperties::None;
	SOLData[1] = TileProperties::None;
	for (int x = 30; x < 60; x++) {
		for (int y = 30; y < 60; y++) {
			dPiece[x][y] = 0;
			dObject[x][y] = 0;
			dFlags[x][y] = {};
		}
	}
}

/** @brief Lays one of the hero's runes aimed at the target, as a cast does: the rune, or nullptr if none went down. */
Missile *LayRune(Player &player, MissileID rune, Point target)
{
	Missile *missile = AddMissile(player.position.tile, target, GetDirection(player.position.tile, target), rune, TARGET_MONSTERS, player, 0, 1);
	return missile != nullptr && !missile->_miDelFlag ? missile : nullptr;
}

int LiveRunes(MissileID rune)
{
	return static_cast<int>(std::count_if(Missiles.begin(), Missiles.end(), [rune](const Missile &missile) { return missile._mitype == rune && !missile._miDelFlag; }));
}

TEST_F(Poe2TableSkillsTest, TrapsAndRunesAreThreeAtATimeAndEndlessHordeAddsTwo)
{
	// Curtis 2026-10-29: he spammed runes and killed the Butcher in 9 seconds. Three of each trap or rune at once, the
	// fourth takes away the oldest, as in POE2; Endless Horde makes it five.
	EXPECT_EQ(MaxPlacedTraps, 3);
	EXPECT_EQ(LegionExtraTraps, 2);
	leveltype = DTYPE_CATHEDRAL;
	ClearFloor();
	Player &player = *MyPlayer;
	player._pMagic = 80;
	player.position.tile = { 40, 40 };
	player._pdir = Direction::East;

	// Spike Trap, a table skill's trap.
	ClearSkillZones();
	for (int cast = 0; cast < 5; cast++)
		ExecuteSkillCast(player, WeaponSkill::SpikeTrap, { 43, 36 + (2 * cast) });
	ASSERT_EQ(SkillZoneCount(), 3U);
	// The two oldest went: the ones left are the last three, oldest first.
	EXPECT_EQ(SkillZoneCenter(0), std::optional<Point>(Point { 43, 40 }));
	EXPECT_EQ(SkillZoneCenter(2), std::optional<Point>(Point { 43, 44 }));
	player.poe2.skillSlots[0] = SkillSlot::ForAttack(WeaponSkill::SpikeTrap);
	player.poe2.slotSupports[0][0] = SupportGem::Legion;
	EXPECT_EQ(SupportTrapLimit(player, SkillSlot::ForAttack(WeaponSkill::SpikeTrap)), 5);
	for (int cast = 0; cast < 7; cast++)
		ExecuteSkillCast(player, WeaponSkill::SpikeTrap, { 44, 36 + cast });
	EXPECT_EQ(SkillZoneCount(), 5U);
	ClearSkillZones();
	player.poe2.skillSlots[0] = {};
	player.poe2.slotSupports[0] = {};

	// Rune of Fire, the Sorcerer's rune: the fourth takes away the first.
	Missiles.clear();
	Missile *first = LayRune(player, MissileID::RuneOfFire, { 43, 37 });
	ASSERT_NE(first, nullptr);
	for (int y = 39; y <= 43; y += 2)
		ASSERT_NE(LayRune(player, MissileID::RuneOfFire, { 43, y }), nullptr);
	EXPECT_EQ(LiveRunes(MissileID::RuneOfFire), 3);
	EXPECT_TRUE(first->_miDelFlag) << "the oldest rune goes";
	// Each kind of rune counts on its own.
	ASSERT_NE(LayRune(player, MissileID::RuneOfLight, { 45, 40 }), nullptr);
	EXPECT_EQ(LiveRunes(MissileID::RuneOfFire), 3);
	EXPECT_EQ(LiveRunes(MissileID::RuneOfLight), 1);
	// Endless Horde on the rune's gem: five.
	player.poe2.skillSlots[0] = SkillSlot::ForSpell(SpellID::RuneOfFire);
	player.poe2.slotSupports[0][0] = SupportGem::Legion;
	for (int y = 36; y <= 44; y += 2)
		ASSERT_NE(LayRune(player, MissileID::RuneOfFire, { 46, y }), nullptr);
	EXPECT_EQ(LiveRunes(MissileID::RuneOfFire), 5);
	player.poe2.skillSlots[0] = {};
	player.poe2.slotSupports[0] = {};

	// Outside POE2 mode runes have no limit.
	gbIsPoe2 = false;
	Missiles.clear();
	for (int y = 36; y <= 44; y += 2)
		ASSERT_NE(LayRune(player, MissileID::RuneOfFire, { 43, y }), nullptr);
	EXPECT_EQ(LiveRunes(MissileID::RuneOfFire), 5);
	gbIsPoe2 = true;
	Missiles.clear();
	leveltype = DTYPE_TOWN;
}

TEST_F(Poe2TableSkillsTest, RunesAndTrapsGoOnlyWhereTheHeroCanSee)
{
	// Curtis 2026-10-29: he laid runes "inside the Butcher's room", behind its closed door. A rune or trap aimed past a
	// wall or a closed door lands on the last tile in sight on the way, as a projectile stops at the wall.
	leveltype = DTYPE_CATHEDRAL;
	ClearFloor();
	SOLData[1] = TileProperties::Solid | TileProperties::BlockLight | TileProperties::BlockMissile;
	// A wall down x = 44 with a closed door in it at 44,40.
	for (int y = 30; y < 60; y++)
		dPiece[44][y] = 1;
	dPiece[44][40] = 0;
	Object &door = Objects[0];
	door = {};
	door._otype = OBJ_L1LDOOR;
	door.position = { 44, 40 };
	door._oSolidFlag = true;
	door._oMissFlag = false;
	dObject[44][40] = 1;
	const Point hero { 40, 40 };
	ASSERT_TRUE(IsMissileBlockedByTile({ 44, 40 }));

	// Through the closed door: the tile before it. Past the wall: the last tile before it on the line.
	EXPECT_EQ(LastTileInSight(hero, { 47, 40 }), Point(43, 40));
	EXPECT_EQ(LastTileInSight(hero, { 47, 42 }), Point(43, 41));
	// In sight: the target itself.
	EXPECT_EQ(LastTileInSight(hero, { 42, 38 }), Point(42, 38));
	EXPECT_EQ(LastTileInSight(hero, hero), hero);
	// The door open, the room is in sight.
	door._oMissFlag = true;
	EXPECT_EQ(LastTileInSight(hero, { 47, 40 }), Point(47, 40));
	door._oMissFlag = false;

	Player &player = *MyPlayer;
	player._pMagic = 80;
	player.position.tile = hero;
	player._pdir = Direction::East;

	// A rune aimed into the room goes down in front of the door, in sight.
	Missiles.clear();
	Missile *rune = LayRune(player, MissileID::RuneOfFire, { 47, 40 });
	ASSERT_NE(rune, nullptr);
	EXPECT_EQ(rune->position.tile, Point(43, 40));
	EXPECT_TRUE(LineClearMovingMissile(hero, rune->position.tile));
	// One aimed past the wall stays on the hero's side of it.
	rune = LayRune(player, MissileID::RuneOfNova, { 50, 44 });
	ASSERT_NE(rune, nullptr);
	EXPECT_LT(rune->position.tile.x, 44);
	EXPECT_TRUE(LineClearMovingMissile(hero, rune->position.tile));
	// With the door open, the room.
	door._oMissFlag = true;
	rune = LayRune(player, MissileID::RuneOfLight, { 47, 40 });
	ASSERT_NE(rune, nullptr);
	EXPECT_EQ(rune->position.tile, Point(47, 40));
	door._oMissFlag = false;

	// Spike Trap, the same.
	ClearSkillZones();
	ExecuteSkillCast(player, WeaponSkill::SpikeTrap, { 47, 40 });
	EXPECT_EQ(SkillZoneCenter(0), std::optional<Point>(Point { 43, 40 }));
	ClearSkillZones();

	// Diablo's runes are as they were: its line check passes doors.
	gbIsPoe2 = false;
	Missiles.clear();
	rune = LayRune(player, MissileID::RuneOfFire, { 47, 40 });
	ASSERT_NE(rune, nullptr);
	EXPECT_EQ(rune->position.tile, Point(47, 40));
	gbIsPoe2 = true;

	Missiles.clear();
	door = {};
	ClearFloor();
	leveltype = DTYPE_TOWN;
}

TEST_F(Poe2TableSkillsTest, EverySkillHasACastAndASpellSound)
{
	// Curtis 2026-10-29: Freezing Shards, Lightning Bolt, Withering Bolt and Mana Drain made no sound, cast or spell.
	// Every row is heard as it is cast and as it goes off, and a spell's bolts as they land.
	for (const TableSkillData &data : TableSkills()) {
		const SkillSounds sounds = TableSkillSounds(data);
		EXPECT_FALSE(sounds.cast.empty()) << data.name << " has no cast sound";
		EXPECT_FALSE(sounds.spell.empty()) << data.name << " has no spell sound";
		if ((data.flags & TF_Spell) != 0 && (data.shape == TableShape::Projectiles || data.shape == TableShape::Stream)) {
			EXPECT_FALSE(sounds.hit.empty()) << data.name << "'s bolts land without a sound";
		}
	}
	const auto soundsOf = [](WeaponSkill skill) { return TableSkillSounds(*FindTableSkill(skill)); };
	// The ones Curtis named, each by its element.
	EXPECT_EQ(soundsOf(WeaponSkill::FreezingShards).cast.sfx, SfxID::CastSkill);
	EXPECT_EQ(soundsOf(WeaponSkill::FreezingShards).spell.sfx, SfxID::SpellHolyBolt);
	EXPECT_EQ(soundsOf(WeaponSkill::FreezingShards).hit.mod, "sfx\\poe2\\blizzard_shard1.wav");
	EXPECT_EQ(soundsOf(WeaponSkill::LightningBolt).cast.sfx, SfxID::CastLightning);
	EXPECT_EQ(soundsOf(WeaponSkill::LightningBolt).spell.sfx, SfxID::SpellLightning);
	EXPECT_EQ(soundsOf(WeaponSkill::WitheringBolt).cast.sfx, SfxID::CastSkill);
	EXPECT_EQ(soundsOf(WeaponSkill::WitheringBolt).spell.sfx, SfxID::SpellBloodStar);
	EXPECT_EQ(soundsOf(WeaponSkill::WitheringBolt).hit.sfx, SfxID::SpellBloodStarHit);
	EXPECT_EQ(soundsOf(WeaponSkill::ManaDrain).cast.sfx, SfxID::CastSkill);
	EXPECT_EQ(soundsOf(WeaponSkill::ManaDrain).hit.sfx, SfxID::SpellEnd);
	// An attack is heard as it swings or shoots; Blizzard keeps its own sounds.
	EXPECT_EQ(soundsOf(WeaponSkill::Multishot).cast.sfx, SfxID::ShootBow2);
	EXPECT_EQ(soundsOf(WeaponSkill::Sacrifice).cast.sfx, SfxID::Swing);
	EXPECT_EQ(soundsOf(WeaponSkill::Blizzard).spell.mod, "sfx\\poe2\\blizzard_cast.wav");
}

} // namespace
} // namespace devilution
