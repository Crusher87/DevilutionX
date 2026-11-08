#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "lighting.h"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/attack_speed.hpp"
#include "poe2/gem_levels.hpp"
#include "poe2/gemcutting.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/player_data.hpp"
#include "poe2/skill_details.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/support_gems.hpp"
#include "poe2/support_table.hpp"
#include "poe2/table_skills.hpp"
#include "poe2/unique_effects.hpp"
#include "poe2/vfx.hpp"
#include "poe2/weapon_skills.hpp"
#include "spells.h"
#include "storm/storm_net.hpp"
#include "tables/monstdat.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

/** @brief The draft gems' Mace, Axe and Sword batch (Curtis 2026-11-07, Q48), each built as its row in drafts-61-priced.json. */
class Poe2WeaponDraftsTest : public ::testing::Test {
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
		LevelMonsterTypes[0].type = MT_NZOMBIE;
		Missiles.clear();
		ClearSkillZones();
		ClearSupportEffects();
		ClearMonsters();
	}

	void TearDown() override
	{
		Missiles.clear();
		ClearSkillZones();
		ClearSupportEffects();
		ClearMonsters();
		gbIsPoe2 = false;
		leveltype = DTYPE_TOWN;
		UnloadModArchives();
	}

	/** @brief A hero who hits often, in a dungeon, with the network up for the hits. */
	static Player &SetUpFight()
	{
		leveltype = DTYPE_CATHEDRAL;
		SNetInitializeProvider(SELCONN_LOOPBACK, nullptr);
		Player &player = *MyPlayer;
		player.setCharacterLevel(30);
		player._pBaseDex = player._pDexterity = 200;
		player._pBaseStr = player._pStrength = 100;
		player._pIMinDam = 20;
		player._pIMaxDam = 30;
		player._pMaxHP = player._pHitPoints = Fixed26_6::fromInt(200);
		player.position.tile = player.position.future = player.position.old = { 40, 40 };
		player._pdir = Direction::SouthEast;
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
		for (int x = 25; x < 60; x++) {
			for (int y = 25; y < 60; y++) {
				dMonster[x][y] = 0;
				dPlayer[x][y] = 0;
			}
		}
		for (size_t i = 0; i < ActiveMonsterCount; i++)
			Monsters[ActiveMonsters[i]] = {};
		ActiveMonsterCount = 0;
	}

	static const TableSkillData &Row(WeaponSkill skill)
	{
		const TableSkillData *data = FindTableSkill(skill);
		EXPECT_NE(data, nullptr);
		return *data;
	}

	static bool Hurt(const Monster &monster)
	{
		return monster.hitPoints < monster.maxHitPoints;
	}

	static int32_t Lost(const Monster &monster)
	{
		return (monster.maxHitPoints - monster.hitPoints).raw();
	}

	static void Heal(Monster &monster)
	{
		monster.hitPoints = monster.maxHitPoints;
	}

	static std::vector<const Missile *> MissilesOf(MissileID type)
	{
		std::vector<const Missile *> found;
		for (const Missile &missile : Missiles) {
			if (missile._mitype == type)
				found.push_back(&missile);
		}
		return found;
	}

	/** @brief Uses the skill on the target as its animation's action frame would, then lets a tick pass. */
	static void Use(Player &player, WeaponSkill skill, Point target)
	{
		player.poe2.castSkill = skill;
		ExecuteSkillCast(player, skill, target);
		player.poe2.castSkill = WeaponSkill::None;
	}

	/** @brief Uses the skill with one seed after another until the monster is hurt (a weapon hit can miss), at most eight times. */
	static void UseUntilHurt(Player &player, WeaponSkill skill, Point target, const Monster &monster)
	{
		for (uint32_t seed = 1; seed <= 8 && !Hurt(monster); seed++) {
			Missiles.clear();
			SetRndSeed(seed);
			Use(player, skill, target);
			player.poe2.tick();
		}
	}

	static void Zones(Player &player, int ticks)
	{
		for (int tick = 0; tick < ticks; tick++)
			ProcessSkillZones(player);
	}

	static void Supports(Player &player, int ticks)
	{
		for (int tick = 0; tick < ticks; tick++) {
			ProcessSupportEffects(player);
			player.poe2.tick();
		}
	}
};

struct DraftRow {
	WeaponSkill skill;
	int tier;
	int level;
	/** The listed mana, before the melee third off, and what the gem costs at level 1 (mana_gem1). */
	int mana;
	int gemOneMana;
	int percent;
	/** The speed row's damage_pct and mana_pct. */
	int speedDamage;
	int speedMana;
	DamageType type;
	TableWeapon weapon;
	uint32_t tabs;
	/** Whether it pays two thirds (melee_mana_cut); the warcries and the thrown gems pay it all. */
	bool meleeCut;
};

constexpr uint32_t AllThree = TT_Mace | TT_Axe | TT_Sword;

// drafts-61-priced.json (Curtis 19:33 UTC 11-07, Q46-Q48; build order 2, the Mace and Axe batch; Tremor Slam dropped).
constexpr std::array<DraftRow, 21> Drafts { {
	{ WeaponSkill::HellfireRoar, 1, 1, 6, 6, 8, 100, 100, DamageType::Fire, TableWeapon::None, AllThree, false },
	{ WeaponSkill::FrostforgedHammer, 1, 1, 5, 3, 155, 118, 109, DamageType::Cold, TableWeapon::Mace, TT_Mace, true },
	{ WeaponSkill::PlateBreaker, 3, 3, 8, 6, 155, 125, 113, DamageType::Physical, TableWeapon::Mace, TT_Mace, true },
	{ WeaponSkill::MagmaHurl, 3, 3, 8, 9, 75, 134, 117, DamageType::Fire, TableWeapon::Mace, TT_Mace, false },
	{ WeaponSkill::ArreatCrash, 7, 10, 12, 9, 95, 134, 117, DamageType::Cold, TableWeapon::MaceOrAxe, TT_Mace | TT_Axe, true },
	{ WeaponSkill::Earthsplitter, 9, 15, 14, 14, 55, 200, 150, DamageType::Physical, TableWeapon::Mace, TT_Mace, true },
	{ WeaponSkill::BrimstoneRift, 9, 15, 14, 10, 95, 125, 113, DamageType::Fire, TableWeapon::Mace, TT_Mace, true },
	{ WeaponSkill::AncestorTotem, 9, 15, 14, 9, 60, 100, 100, DamageType::Physical, TableWeapon::Mace, TT_Mace, true },
	{ WeaponSkill::HephastosHammer, 11, 20, 16, 20, 215, 143, 122, DamageType::Fire, TableWeapon::Mace, TT_Mace, false },
	{ WeaponSkill::QuakingRoar, 11, 20, 16, 16, 40, 100, 100, DamageType::Physical, TableWeapon::None, AllThree, false },
	{ WeaponSkill::HammerOfZakarum, 13, 25, 18, 18, 65, 200, 150, DamageType::Physical, TableWeapon::Mace, TT_Mace, true },
	{ WeaponSkill::MountainsFall, 13, 25, 18, 16, 110, 167, 134, DamageType::Cold, TableWeapon::Mace, TT_Mace, true },
	{ WeaponSkill::CryOfBulKathos, 13, 25, 18, 18, 40, 100, 100, DamageType::Physical, TableWeapon::None, AllThree, false },
	{ WeaponSkill::StormEdge, 3, 3, 7, 5, 225, 100, 100, DamageType::Lightning, TableWeapon::AxeOrSword, TT_Axe | TT_Sword, true },
	{ WeaponSkill::FrostbiteBlades, 5, 6, 10, 7, 225, 100, 100, DamageType::Cold, TableWeapon::AxeOrSword, TT_Axe | TT_Sword, true },
	{ WeaponSkill::CinderStrike, 7, 10, 12, 8, 170, 100, 100, DamageType::Fire, TableWeapon::AxeOrSword, TT_Axe | TT_Sword, true },
	{ WeaponSkill::SteelTempest, 9, 15, 14, 11, 105, 143, 122, DamageType::Physical, TableWeapon::AxeOrSword, TT_Axe | TT_Sword, true },
	{ WeaponSkill::ThunderHatchet, 9, 15, 14, 16, 330, 125, 113, DamageType::Lightning, TableWeapon::Axe, TT_Axe, false },
	{ WeaponSkill::BloodHarvest, 11, 20, 16, 12, 240, 118, 109, DamageType::Physical, TableWeapon::Axe, TT_Axe, true },
	{ WeaponSkill::PyreBlow, 13, 25, 18, 15, 355, 143, 122, DamageType::Fire, TableWeapon::AxeOrSword, TT_Axe | TT_Sword, true },
	{ WeaponSkill::MammothCharge, 13, 25, 18, 16, 90, 172, 136, DamageType::Physical, TableWeapon::Axe, TT_Axe, true },
} };

// Build order 3: the Sword and Shield batch.
constexpr std::array<DraftRow, 5> SwordShieldDrafts { {
	{ WeaponSkill::PhantomBlade, 11, 20, 16, 17, 110, 112, 106, DamageType::Physical, TableWeapon::Sword, TT_Sword, false },
	{ WeaponSkill::HoradricSundering, 13, 25, 18, 17, 80, 182, 141, DamageType::Physical, TableWeapon::Sword, TT_Sword, true },
	{ WeaponSkill::TollingShield, 9, 15, 14, 10, 85, 124, 112, DamageType::Physical, TableWeapon::Shield, TT_Shield, true },
	{ WeaponSkill::BulwarkRoar, 11, 20, 15, 15, 60, 100, 100, DamageType::Physical, TableWeapon::Shield, TT_Shield, false },
	{ WeaponSkill::ZakarumsJudgement, 13, 25, 18, 15, 55, 155, 128, DamageType::Physical, TableWeapon::Shield, TT_Shield, true },
} };

bool HasTags(WeaponSkill skill, uint32_t tag)
{
	return (SkillTags(SkillSlot::ForAttack(skill)) & tag) == tag;
}

TEST_F(Poe2WeaponDraftsTest, WeaponDraftsAreStrengthGemsAtTheirTiers)
{
	// Appended after the Staff batch, in the batch's order.
	EXPECT_EQ(static_cast<int>(WeaponSkill::HellfireRoar), static_cast<int>(WeaponSkill::PalmOfTheFrozenNorth) + 1);
	for (size_t i = 0; i < Drafts.size(); i++)
		EXPECT_EQ(static_cast<int>(Drafts[i].skill), static_cast<int>(WeaponSkill::HellfireRoar) + static_cast<int>(i));
	for (size_t i = 0; i < SwordShieldDrafts.size(); i++)
		EXPECT_EQ(static_cast<int>(SwordShieldDrafts[i].skill), static_cast<int>(WeaponSkill::PhantomBlade) + static_cast<int>(i));
	EXPECT_EQ(static_cast<int>(WeaponSkill::PhantomBlade), static_cast<int>(WeaponSkill::MammothCharge) + 1);
	// The Bow batch follows (poe2_bow_drafts_test.cpp).
	EXPECT_EQ(static_cast<int>(WeaponSkill::ColdArrow), static_cast<int>(WeaponSkill::ZakarumsJudgement) + 1);
	const std::array<std::pair<uint32_t, GemCategory>, 4> tabs { { { TT_Mace, GemCategory::Mace }, { TT_Axe, GemCategory::Axe }, { TT_Sword, GemCategory::Sword }, { TT_Shield, GemCategory::Shield } } };
	std::vector<DraftRow> all(Drafts.begin(), Drafts.end());
	all.insert(all.end(), SwordShieldDrafts.begin(), SwordShieldDrafts.end());
	for (const DraftRow &draft : all) {
		const TableSkillData &data = Row(draft.skill);
		const SkillSlot slot = SkillSlot::ForAttack(draft.skill);
		EXPECT_EQ(data.weapon, draft.weapon) << data.name;
		EXPECT_EQ(data.tabs, draft.tabs) << data.name;
		EXPECT_FALSE(IsGrantedTableSkill(data)) << data.name;
		EXPECT_EQ(data.flags & TF_Spell, 0U) << data.name;
		EXPECT_EQ(data.level, draft.level) << data.name;
		EXPECT_EQ(GemTier(data.level), draft.tier) << data.name;
		EXPECT_EQ(data.mana, draft.mana) << data.name;
		EXPECT_EQ(data.damagePercent, draft.percent) << data.name;
		EXPECT_EQ(data.type, draft.type) << data.name;
		EXPECT_EQ(data.curve, TableCurve::Boneshatter) << data.name;
		EXPECT_EQ(data.curveEnd, 0) << data.name;
		EXPECT_EQ(HasMeleeManaCut(draft.skill), draft.meleeCut) << data.name;
		EXPECT_EQ(WeaponSkillManaCost(draft.skill), draft.meleeCut ? std::max(((draft.mana * 2) + 1) / 3, 1) : draft.mana) << data.name;
		EXPECT_EQ(GemBaseManaCost(slot, 1), draft.gemOneMana) << data.name;
		EXPECT_EQ(SkillSpeedDamagePercent(slot), draft.speedDamage) << data.name;
		EXPECT_EQ(SkillSpeedManaPercent(slot), draft.speedMana) << data.name;
		// Its item row: appended, uncut-only, at the table's level, Strength as the other weapon gems.
		const std::string name = std::string(data.name) + " Gem";
		const auto row = std::find_if(AllItemsList.begin(), AllItemsList.end(), [&](const ItemData &item) { return item.iName == name; });
		ASSERT_NE(row, AllItemsList.end()) << name;
		EXPECT_EQ(row->iMinMLvl, data.level) << name;
		EXPECT_EQ(row->dropRate, 0) << name;
		EXPECT_EQ(row->iMinStr, 18 + 3 * draft.level) << name;
		EXPECT_EQ(row->iMinDex, 0) << name;
		EXPECT_EQ(row->iMinMag, 0) << name;
		EXPECT_EQ(row->iValue, 450 + 130 * draft.level) << name;
		EXPECT_EQ(FindTableSkillGem(*row), &data) << name;
		Item gem;
		ASSERT_TRUE(CreateSkillGemItem(gem, slot, 1)) << data.name;
		EXPECT_EQ(SkillForGem(gem), slot) << data.name;
		// It is cut on its own tabs only.
		const std::vector<GemEntry> &catalog = GemCatalog();
		const auto entry = std::find_if(catalog.begin(), catalog.end(), [&](const GemEntry &gemEntry) { return gemEntry.skill == slot; });
		ASSERT_NE(entry, catalog.end()) << data.name;
		EXPECT_EQ(entry->level, draft.level) << data.name;
		for (const auto &[tab, category] : tabs)
			EXPECT_EQ(GemInCategory(*entry, category), (draft.tabs & tab) != 0) << data.name;
		EXPECT_FALSE(GemInCategory(*entry, GemCategory::Staff)) << data.name;
		EXPECT_FALSE(GemInCategory(*entry, GemCategory::Elemental)) << data.name;
		EXPECT_FALSE(TableSkillExtraNote(draft.skill).empty()) << data.name;
	}
	// A mace or an axe uses Arreat Crash; neither a sword nor a staff does.
	const auto base = [](std::string_view name) {
		Item item;
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iName == name && AllItemsList[i].dropRate > 0) {
				InitializeItem(item, static_cast<_item_indexes>(i));
				break;
			}
		}
		EXPECT_FALSE(item.isEmpty()) << name;
		return item;
	};
	EXPECT_TRUE(ItemAllowsWeaponSkill(base("Wooden Club"), WeaponSkill::ArreatCrash));
	EXPECT_TRUE(ItemAllowsWeaponSkill(base("Dull Hatchet"), WeaponSkill::ArreatCrash));
	EXPECT_FALSE(ItemAllowsWeaponSkill(base("Shortsword"), WeaponSkill::ArreatCrash));
	EXPECT_FALSE(ItemAllowsWeaponSkill(base("Fine Long Quarterstaff"), WeaponSkill::ArreatCrash));
	EXPECT_EQ(WeaponSkillWeapons(WeaponSkill::ArreatCrash), "mace or axe");
	// Tremor Slam was dropped.
	for (const TableSkillData &data : TableSkills())
		EXPECT_NE(data.name, "Tremor Slam");
}

TEST_F(Poe2WeaponDraftsTest, WeaponDraftsHaveTheWorkshopsTags)
{
	for (const WeaponSkill cry : { WeaponSkill::HellfireRoar, WeaponSkill::QuakingRoar, WeaponSkill::CryOfBulKathos }) {
		EXPECT_TRUE(HasTags(cry, ST_Warcry | ST_Area | ST_Duration)) << Row(cry).name;
		EXPECT_FALSE(HasTags(cry, ST_Melee)) << Row(cry).name;
	}
	EXPECT_TRUE(HasTags(WeaponSkill::HellfireRoar, ST_Fire));
	// The thrown gems are neither melee nor slams: they pay their mana in full.
	for (const WeaponSkill thrown : { WeaponSkill::MagmaHurl, WeaponSkill::HephastosHammer, WeaponSkill::ThunderHatchet }) {
		EXPECT_TRUE(IsThrowingSkill(thrown)) << Row(thrown).name;
		EXPECT_TRUE(HasTags(thrown, ST_Attack | ST_Projectile)) << Row(thrown).name;
		EXPECT_FALSE(HasTags(thrown, ST_Melee)) << Row(thrown).name;
		EXPECT_FALSE(HasTags(thrown, ST_Slam)) << Row(thrown).name;
		EXPECT_FALSE(HasMeleeManaCut(thrown)) << Row(thrown).name;
	}
	EXPECT_TRUE(HasTags(WeaponSkill::MagmaHurl, ST_Fire | ST_Area));
	EXPECT_TRUE(HasTags(WeaponSkill::HephastosHammer, ST_Fire | ST_Area));
	EXPECT_TRUE(HasTags(WeaponSkill::ThunderHatchet, ST_Lightning));
	// The rest are melee.
	for (const DraftRow &draft : Drafts) {
		if (draft.meleeCut)
			EXPECT_TRUE(HasTags(draft.skill, ST_Attack | ST_Melee)) << Row(draft.skill).name;
	}
	EXPECT_TRUE(HasTags(WeaponSkill::FrostforgedHammer, ST_Strike | ST_Frost));
	EXPECT_FALSE(HasTags(WeaponSkill::FrostforgedHammer, ST_Slam));
	EXPECT_TRUE(HasTags(WeaponSkill::PlateBreaker, ST_Strike | ST_Physical));
	EXPECT_FALSE(HasTags(WeaponSkill::PlateBreaker, ST_Slam));
	EXPECT_TRUE(HasTags(WeaponSkill::ArreatCrash, ST_Slam | ST_Frost | ST_Area));
	EXPECT_TRUE(HasTags(WeaponSkill::Earthsplitter, ST_Slam | ST_Physical));
	EXPECT_TRUE(HasTags(WeaponSkill::BrimstoneRift, ST_Slam | ST_Fire | ST_Duration));
	EXPECT_TRUE(HasTags(WeaponSkill::AncestorTotem, ST_Melee | ST_Placed));
	EXPECT_TRUE(HasTags(WeaponSkill::HammerOfZakarum, ST_Slam | ST_Physical));
	EXPECT_FALSE(HasTags(WeaponSkill::HammerOfZakarum, ST_Duration));
	EXPECT_TRUE(HasTags(WeaponSkill::MountainsFall, ST_Slam | ST_Frost));
	EXPECT_TRUE(HasTags(WeaponSkill::StormEdge, ST_Strike | ST_Lightning));
	EXPECT_TRUE(HasTags(WeaponSkill::FrostbiteBlades, ST_Strike | ST_Frost));
	EXPECT_TRUE(HasTags(WeaponSkill::CinderStrike, ST_Strike | ST_Fire));
	EXPECT_TRUE(HasTags(WeaponSkill::SteelTempest, ST_Strike | ST_Duration | ST_Physical));
	EXPECT_TRUE(HasTags(WeaponSkill::BloodHarvest, ST_Strike | ST_Physical));
	EXPECT_TRUE(HasTags(WeaponSkill::PyreBlow, ST_Strike | ST_Fire));
	EXPECT_TRUE(HasTags(WeaponSkill::MammothCharge, ST_Slam | ST_Movement));
	// The Sword and Shield batch: the ghost sword is thrown, the sundering is all three elements and no physical, the
	// judgement is holy physical, not fire.
	EXPECT_TRUE(IsThrowingSkill(WeaponSkill::PhantomBlade));
	EXPECT_TRUE(HasTags(WeaponSkill::PhantomBlade, ST_Attack | ST_Projectile | ST_Physical));
	EXPECT_FALSE(HasTags(WeaponSkill::PhantomBlade, ST_Melee));
	EXPECT_FALSE(HasTags(WeaponSkill::PhantomBlade, ST_Slam));
	EXPECT_FALSE(HasMeleeManaCut(WeaponSkill::PhantomBlade));
	EXPECT_TRUE(HasTags(WeaponSkill::HoradricSundering, ST_Attack | ST_Melee | ST_Area | ST_Elemental));
	EXPECT_FALSE(HasTags(WeaponSkill::HoradricSundering, ST_Physical));
	EXPECT_TRUE(HasTags(WeaponSkill::TollingShield, ST_Attack | ST_Melee | ST_Strike | ST_Area | ST_Physical));
	EXPECT_TRUE(HasTags(WeaponSkill::BulwarkRoar, ST_Warcry | ST_Area | ST_Duration));
	EXPECT_FALSE(HasTags(WeaponSkill::BulwarkRoar, ST_Attack));
	EXPECT_TRUE(HasTags(WeaponSkill::ZakarumsJudgement, ST_Attack | ST_Melee | ST_Slam | ST_Area | ST_Physical));
	EXPECT_FALSE(HasTags(WeaponSkill::ZakarumsJudgement, ST_Fire));
	EXPECT_EQ(Row(WeaponSkill::ZakarumsJudgement).type, DamageType::Physical);
	// No channelling.
	for (const DraftRow &draft : Drafts)
		EXPECT_FALSE(HasTags(draft.skill, ST_Channelling)) << Row(draft.skill).name;
	for (const DraftRow &draft : SwordShieldDrafts)
		EXPECT_FALSE(HasTags(draft.skill, ST_Channelling)) << Row(draft.skill).name;
}

// The drafts' rule: no two fire, cold or lightning gems in one tier of a tab.
TEST_F(Poe2WeaponDraftsTest, NoTwoGemsOfOneElementInAWeaponTier)
{
	for (const GemCategory tab : { GemCategory::Mace, GemCategory::Axe, GemCategory::Sword, GemCategory::Shield }) {
		std::map<std::pair<int, uint32_t>, std::vector<std::string>> byTierAndElement;
		for (const GemEntry &gem : GemCatalog()) {
			if (gem.skill.kind != SkillKind::Attack || !GemInCategory(gem, tab) || IsRetiredSkill(gem.skill))
				continue;
			const uint32_t elements = SkillTags(gem.skill) & (ST_Fire | ST_Frost | ST_Lightning);
			// A gem of mixed elements is fine.
			if (elements == 0 || (elements & (elements - 1)) != 0)
				continue;
			byTierAndElement[{ GemTier(gem.level), elements }].push_back(GemName(gem));
		}
		for (const auto &[key, names] : byTierAndElement)
			EXPECT_EQ(names.size(), 1U) << "tab " << static_cast<int>(tab) << " tier " << key.first << ": " << names.front() << " and " << names.back();
	}
}

TEST_F(Poe2WeaponDraftsTest, HellfireRoarsHitsBurstIntoFire)
{
	Player &player = SetUpFight();
	Monster &target = PlaceMonster(0, { 41, 40 });
	// Two tiles from the hero, out of the hammer's reach: only the burst gets it.
	Monster &beside = PlaceMonster(1, { 42, 40 });
	Use(player, WeaponSkill::HellfireRoar, { 41, 40 });
	EXPECT_EQ(player.poe2.cryHits[static_cast<size_t>(EmpoweringCry::Hellfire)], 3);
	EXPECT_FALSE(Hurt(target));
	player.poe2.tick();
	UseUntilHurt(player, WeaponSkill::FrostforgedHammer, { 41, 40 }, target);
	ASSERT_TRUE(Hurt(target));
	EXPECT_EQ(player.poe2.cryHits[static_cast<size_t>(EmpoweringCry::Hellfire)], 2);
	EXPECT_FALSE(Hurt(beside));
	Supports(player, 3);
	EXPECT_TRUE(Hurt(beside));
	// Unused hits run out with the cry.
	for (int i = 0; i < Row(WeaponSkill::HellfireRoar).duration; i++)
		player.poe2.tick();
	EXPECT_EQ(player.poe2.cryHits[static_cast<size_t>(EmpoweringCry::Hellfire)], 0);
	// A spell hit, or a thrown one, spends none.
	Use(player, WeaponSkill::HellfireRoar, { 41, 40 });
	player.poe2.tick();
	OnEmpoweredMeleeHit(player, SkillSlot::ForAttack(WeaponSkill::ThunderHatchet), target, Fixed26_6::fromInt(10));
	OnEmpoweredMeleeHit(player, SkillSlot::ForSpell(SpellID::Firebolt), target, Fixed26_6::fromInt(10));
	EXPECT_EQ(player.poe2.cryHits[static_cast<size_t>(EmpoweringCry::Hellfire)], 3);
	// One use spends one, however many it hits.
	OnEmpoweredMeleeHit(player, SkillSlot::ForAttack(WeaponSkill::PlateBreaker), target, Fixed26_6::fromInt(10));
	OnEmpoweredMeleeHit(player, SkillSlot::ForAttack(WeaponSkill::PlateBreaker), beside, Fixed26_6::fromInt(10));
	EXPECT_EQ(player.poe2.cryHits[static_cast<size_t>(EmpoweringCry::Hellfire)], 2);
}

TEST_F(Poe2WeaponDraftsTest, QuakingRoarQuakesAndItsSlamsHaveAftershocks)
{
	Player &player = SetUpFight();
	Monster &near = PlaceMonster(0, { 41, 40 });
	Monster &far = PlaceMonster(1, { 44, 40 });
	Use(player, WeaponSkill::QuakingRoar, { 41, 40 });
	// The roar's own quake always lands, as a spell does.
	EXPECT_TRUE(Hurt(near));
	EXPECT_FALSE(Hurt(far));
	EXPECT_EQ(player.poe2.cryHits[static_cast<size_t>(EmpoweringCry::Quaking)], 2);
	player.poe2.tick();
	// A strike spends none of it: only a slam does.
	OnEmpoweredMeleeHit(player, SkillSlot::ForAttack(WeaponSkill::StormEdge), far, Fixed26_6::fromInt(100));
	EXPECT_EQ(player.poe2.cryHits[static_cast<size_t>(EmpoweringCry::Quaking)], 2);
	OnEmpoweredMeleeHit(player, SkillSlot::ForAttack(WeaponSkill::ArreatCrash), far, Fixed26_6::fromInt(100));
	EXPECT_EQ(player.poe2.cryHits[static_cast<size_t>(EmpoweringCry::Quaking)], 1);
	// The aftershock comes a moment later, 40% of the hit.
	Supports(player, QuakingAftershockTicks - 1);
	EXPECT_FALSE(Hurt(far));
	Supports(player, 3);
	EXPECT_TRUE(Hurt(far));
	EXPECT_LE(Lost(far), Fixed26_6::fromInt(40).raw());
}

TEST_F(Poe2WeaponDraftsTest, CryOfBulKathosStrikesAgain)
{
	Player &player = SetUpFight();
	Monster &monster = PlaceMonster(0, { 41, 40 });
	bool again = false;
	for (uint32_t seed = 1; seed <= 8 && !again; seed++) {
		ClearSkillZones();
		ClearSupportEffects();
		Heal(monster);
		player.poe2.clear();
		SetRndSeed(seed);
		Use(player, WeaponSkill::CryOfBulKathos, { 41, 40 });
		EXPECT_EQ(player.poe2.cryHits[static_cast<size_t>(EmpoweringCry::BulKathos)], 3);
		player.poe2.tick();
		Use(player, WeaponSkill::StormEdge, { 41, 40 });
		if (!Hurt(monster))
			continue;
		EXPECT_EQ(player.poe2.cryHits[static_cast<size_t>(EmpoweringCry::BulKathos)], 2);
		const int32_t first = Lost(monster);
		Supports(player, BulKathosRepeatTicks + 2);
		again = Lost(monster) > first;
		// The repeat spends no more of the cry.
		EXPECT_EQ(player.poe2.cryHits[static_cast<size_t>(EmpoweringCry::BulKathos)], 2);
	}
	EXPECT_TRUE(again);
	// Not a slam.
	player.poe2.clear();
	Use(player, WeaponSkill::CryOfBulKathos, { 41, 40 });
	player.poe2.tick();
	OnEmpoweredMeleeHit(player, SkillSlot::ForAttack(WeaponSkill::ArreatCrash), monster, Fixed26_6::fromInt(100));
	EXPECT_EQ(player.poe2.cryHits[static_cast<size_t>(EmpoweringCry::BulKathos)], 3);
}

TEST_F(Poe2WeaponDraftsTest, FrostforgedHammerFreezesEveryThirdBlow)
{
	Player &player = SetUpFight();
	Monster &front = PlaceMonster(0, { 41, 40 });
	Monster &side = PlaceMonster(1, { 41, 41 });
	int frozenBlows = 0;
	for (int blow = 1; blow <= 6; blow++) {
		EXPECT_EQ(FrostforgedNextFreezes(player), blow % FrostforgedFreezeEvery == 0) << blow;
		front.poe2.freezeTicks = 0;
		Heal(front);
		SetRndSeed(static_cast<uint32_t>(blow));
		Use(player, WeaponSkill::FrostforgedHammer, { 41, 40 });
		player.poe2.tick();
		if (blow % FrostforgedFreezeEvery != 0) {
			EXPECT_EQ(front.poe2.freezeTicks, 0) << blow;
		} else if (Hurt(front)) {
			EXPECT_GT(front.poe2.freezeTicks, 0) << blow;
			frozenBlows++;
		}
		EXPECT_EQ(side.poe2.freezeTicks, 0) << blow;
	}
	EXPECT_GT(frozenBlows, 0);
	// The count starts over when the hero stops.
	Use(player, WeaponSkill::FrostforgedHammer, { 41, 40 });
	EXPECT_EQ(player.poe2.frostforgedCount, 1);
	for (int i = 0; i < FrostforgedResetTicks; i++)
		player.poe2.tick();
	EXPECT_EQ(player.poe2.frostforgedCount, 0);
}

TEST_F(Poe2WeaponDraftsTest, MagmaHurlBurstsAndThrowsShrapnel)
{
	Player &player = SetUpFight();
	Monster &target = PlaceMonster(0, { 43, 40 });
	Monster &beside = PlaceMonster(1, { 44, 41 });
	Use(player, WeaponSkill::MagmaHurl, { 43, 40 });
	const std::vector<const Missile *> rocks = MissilesOf(MissileID::Firebolt);
	ASSERT_EQ(rocks.size(), 1U);
	EXPECT_EQ(rocks.front()->poe2.damageType, static_cast<int8_t>(DamageType::Fire));
	EXPECT_GT(rocks.front()->_midam, 0);
	const Missile rock = *rocks.front();
	OnTableMissileHit(rock, player, target, 0, false);
	EXPECT_TRUE(Hurt(beside));
	const std::vector<const Missile *> shrapnel = MissilesOf(MissileID::Firebolt);
	ASSERT_EQ(shrapnel.size(), 1U + MagmaHurlShrapnel);
	const int percent = GemDamagePercent(SkillSlot::ForAttack(WeaponSkill::MagmaHurl), player.poe2.effectiveLevel(WeaponSkill::MagmaHurl));
	for (size_t i = 1; i < shrapnel.size(); i++) {
		EXPECT_TRUE(shrapnel[i]->poe2.draftPart);
		EXPECT_EQ(shrapnel[i]->poe2.damagePercent, percent * (75 * MagmaHurlShrapnelPercent / 100) / 100);
		// Three from the target, the fourth straight on from two tiles further (batch2-fixes.md 1).
		EXPECT_EQ(Point(shrapnel[i]->position.start), i <= 3 ? Point(43, 40) : Point(45, 40));
	}
	// A shard sets off nothing more.
	Heal(beside);
	OnTableMissileHit(*shrapnel[1], player, target, 0, false);
	EXPECT_FALSE(Hurt(beside));
	EXPECT_EQ(MissilesOf(MissileID::Firebolt).size(), 1U + MagmaHurlShrapnel);
}

TEST_F(Poe2WeaponDraftsTest, ArreatCrashSendsOutTwoRings)
{
	Player &player = SetUpFight();
	PlaceMonster(0, { 42, 40 });
	Monster &firstRing = PlaceMonster(1, { 44, 40 });
	Monster &secondRing = PlaceMonster(2, { 45, 40 });
	Monster &past = PlaceMonster(3, { 46, 40 });
	EXPECT_EQ(SlamLanding(player, { 45, 40 }, ArreatReachTiles), Point(42, 40));
	Use(player, WeaponSkill::ArreatCrash, { 45, 40 });
	EXPECT_FALSE(Hurt(firstRing));
	EXPECT_EQ(SkillZoneCount(), 2U);
	Zones(player, ArreatRingTicks[0] + 1);
	EXPECT_TRUE(Hurt(firstRing));
	EXPECT_FALSE(Hurt(secondRing));
	const int32_t firstLost = Lost(firstRing);
	Zones(player, ArreatRingTicks[1] - ArreatRingTicks[0] + 1);
	EXPECT_TRUE(Hurt(secondRing));
	// Each ring hits only what is out on it.
	EXPECT_EQ(Lost(firstRing), firstLost);
	EXPECT_LT(Lost(secondRing), firstLost);
	EXPECT_FALSE(Hurt(past));
	EXPECT_EQ(SkillZoneCount(), 0U);
}

TEST_F(Poe2WeaponDraftsTest, EarthsplitterShockwavesFromBrokenArmour)
{
	Player &player = SetUpFight();
	for (const bool broken : { false, true }) {
		ClearMonsters();
		Monster &target = PlaceMonster(0, { 41, 40 });
		// Beside the first burst, which is one tile: only a shockwave reaches it.
		Monster &beside = PlaceMonster(1, { 40, 41 });
		Monster &end = PlaceMonster(2, { 45, 41 });
		for (uint32_t seed = 1; seed <= 8 && !Hurt(target); seed++) {
			target.poe2.armourBreakTicks = broken ? 60 : 0;
			SetRndSeed(seed);
			Use(player, WeaponSkill::Earthsplitter, { 46, 40 });
		}
		EXPECT_TRUE(Hurt(target));
		EXPECT_EQ(Hurt(beside), broken);
		UseUntilHurt(player, WeaponSkill::Earthsplitter, { 46, 40 }, end);
		EXPECT_TRUE(Hurt(end));
	}
}

TEST_F(Poe2WeaponDraftsTest, BrimstoneRiftForksAndBurns)
{
	Player &player = SetUpFight();
	const std::vector<Point> tiles = BrimstoneRiftTiles({ 40, 40 }, Direction::SouthEast, 6);
	ASSERT_EQ(tiles.size(), 6U + (2 * BrimstoneForkTiles));
	EXPECT_EQ(tiles[5], Point(46, 40));
	Monster &onRift = PlaceMonster(0, { 43, 40 });
	Monster &onFork = PlaceMonster(1, tiles[7]);
	Monster &off = PlaceMonster(2, { 43, 42 });
	Use(player, WeaponSkill::BrimstoneRift, { 46, 40 });
	EXPECT_FALSE(Hurt(off));
	Heal(onRift);
	Heal(onFork);
	ASSERT_EQ(SkillZoneCount(), 1U);
	Zones(player, Row(WeaponSkill::BrimstoneRift).duration + 2);
	// The ground burns what stands on the rift and its forks, always, as a spell does.
	EXPECT_TRUE(Hurt(onRift));
	EXPECT_TRUE(Hurt(onFork));
	EXPECT_FALSE(Hurt(off));
	EXPECT_EQ(SkillZoneCount(), 0U);
	// At most two rifts burn at once.
	for (int i = 0; i < 3; i++)
		Use(player, WeaponSkill::BrimstoneRift, { 46, 40 });
	EXPECT_EQ(SkillZoneCount(), 2U);
}

TEST_F(Poe2WeaponDraftsTest, AncestorTotemSlamsTheNearest)
{
	Player &player = SetUpFight();
	Monster &nearest = PlaceMonster(0, { 43, 40 });
	Monster &beside = PlaceMonster(1, { 44, 40 });
	Monster &apart = PlaceMonster(2, { 40, 44 });
	Use(player, WeaponSkill::AncestorTotem, { 43, 40 });
	ASSERT_EQ(SkillZoneCount(), 1U);
	EXPECT_EQ(SkillZoneCenter(0), std::optional<Point>(Point(40, 40)));
	Zones(player, Row(WeaponSkill::AncestorTotem).interval + 1);
	EXPECT_TRUE(Hurt(nearest));
	EXPECT_TRUE(Hurt(beside));
	EXPECT_FALSE(Hurt(apart));
	// One totem at a time.
	Use(player, WeaponSkill::AncestorTotem, { 43, 40 });
	EXPECT_EQ(SkillZoneCount(), 1U);
}

TEST_F(Poe2WeaponDraftsTest, HephastosHammerLodgesAndAWarcryShattersIt)
{
	Player &player = SetUpFight();
	Monster &target = PlaceMonster(0, { 44, 40 });
	Monster &beside = PlaceMonster(1, { 45, 40 });
	// Two tiles south of the hammer: on a line of fire, out of the splash.
	Monster &onLine = PlaceMonster(2, Point(44, 40) + Displacement { Direction::South } * 2);
	Use(player, WeaponSkill::HephastosHammer, { 44, 40 });
	const std::vector<const Missile *> hammers = MissilesOf(MissileID::Firebolt);
	ASSERT_EQ(hammers.size(), 1U);
	EXPECT_EQ(SkillZoneCount(), 0U);
	const Missile hammer = *hammers.front();
	OnTableMissileHit(hammer, player, target, 0, false);
	EXPECT_GT(target.poe2.heavyStunTicks, 0);
	EXPECT_TRUE(Hurt(beside));
	EXPECT_FALSE(Hurt(onLine));
	ASSERT_EQ(SkillZoneCount(), 1U);
	EXPECT_EQ(SkillZoneCenter(0), std::optional<Point>(Point(44, 40)));
	// It smoulders and does nothing more on its own.
	Zones(player, 10);
	EXPECT_FALSE(Hurt(onLine));
	EXPECT_EQ(SkillZoneCount(), 1U);
	// A warcry too far off leaves it.
	player.position.tile = { 36, 40 };
	Use(player, WeaponSkill::HellfireRoar, { 37, 40 });
	EXPECT_EQ(SkillZoneCount(), 1U);
	player.position.tile = { 40, 40 };
	Use(player, WeaponSkill::HellfireRoar, { 41, 40 });
	EXPECT_EQ(SkillZoneCount(), 0U);
	EXPECT_TRUE(Hurt(onLine));
	// Its time runs out on its own.
	OnTableMissileHit(hammer, player, target, 0, false);
	Zones(player, Row(WeaponSkill::HephastosHammer).duration + 2);
	EXPECT_EQ(SkillZoneCount(), 0U);
}

TEST_F(Poe2WeaponDraftsTest, HammerOfZakarumFallsHarderOnTheStunned)
{
	Player &player = SetUpFight();
	const auto fall = [&](bool stunned) {
		ClearSkillZones();
		ClearMonsters();
		Monster &monster = PlaceMonster(0, { 43, 40 });
		monster.poe2.heavyStunTicks = stunned ? 60 : 0;
		Use(player, WeaponSkill::HammerOfZakarum, { 43, 40 });
		EXPECT_FALSE(Hurt(monster));
		Zones(player, Row(WeaponSkill::HammerOfZakarum).delay + 2);
		EXPECT_GT(monster.poe2.heavyStunTicks, 0);
		return Lost(monster);
	};
	const int32_t plain = fall(false);
	EXPECT_GT(plain, 0);
	EXPECT_GE(fall(true), plain * 3 / 2);
	// It falls no further off than its reach.
	EXPECT_EQ(SlamLanding(player, { 48, 40 }, HammerOfZakarumReachTiles), Point(44, 40));
}

TEST_F(Poe2WeaponDraftsTest, MountainsFallsMiddleSpikeIsGreatest)
{
	Player &player = SetUpFight();
	const std::vector<Point> spikes = MountainsFallSpikes({ 43, 40 });
	ASSERT_EQ(spikes.size(), 5U);
	EXPECT_EQ(spikes.front(), Point(43, 40));
	Monster &middle = PlaceMonster(0, spikes[0]);
	Monster &arm = PlaceMonster(1, spikes[1]);
	int64_t middleLost = 0;
	int64_t armLost = 0;
	for (uint32_t seed = 1; seed <= 16; seed++) {
		Heal(middle);
		Heal(arm);
		SetRndSeed(seed);
		Use(player, WeaponSkill::MountainsFall, { 43, 40 });
		middleLost += Lost(middle);
		armLost += Lost(arm);
	}
	EXPECT_GT(armLost, 0);
	EXPECT_GT(middleLost, armLost * 5 / 4);
}

TEST_F(Poe2WeaponDraftsTest, StormEdgeLeavesALashingStorm)
{
	Player &player = SetUpFight();
	Monster &monster = PlaceMonster(0, { 41, 40 });
	UseUntilHurt(player, WeaponSkill::StormEdge, { 41, 40 }, monster);
	ASSERT_TRUE(Hurt(monster));
	ClearSkillZones();
	Use(player, WeaponSkill::StormEdge, { 41, 40 });
	ASSERT_EQ(SkillZoneCount(), 1U);
	EXPECT_EQ(SkillZoneCenter(0), std::optional<Point>(Point(41, 40)));
	Heal(monster);
	Zones(player, Row(WeaponSkill::StormEdge).interval + 1);
	EXPECT_TRUE(Hurt(monster));
	Zones(player, Row(WeaponSkill::StormEdge).duration);
	EXPECT_EQ(SkillZoneCount(), 0U);
}

TEST_F(Poe2WeaponDraftsTest, FrostbiteBladesPierceOnFromTheTarget)
{
	Player &player = SetUpFight();
	Monster &monster = PlaceMonster(0, { 41, 40 });
	UseUntilHurt(player, WeaponSkill::FrostbiteBlades, { 41, 40 }, monster);
	const std::vector<const Missile *> blades = MissilesOf(MissileID::BlueFlare);
	ASSERT_EQ(blades.size(), 3U);
	const int percent = GemDamagePercent(SkillSlot::ForAttack(WeaponSkill::FrostbiteBlades), player.poe2.effectiveLevel(WeaponSkill::FrostbiteBlades));
	for (const Missile *blade : blades) {
		EXPECT_EQ(Point(blade->position.start), Point(41, 40));
		EXPECT_EQ(blade->poe2.damagePercent, percent * (225 * FrostbiteBladePercent / 100) / 100);
		EXPECT_EQ(blade->poe2.damageType, static_cast<int8_t>(DamageType::Cold));
		EXPECT_GE(blade->poe2.piercesLeft, FrostbiteBladePierces);
		// They don't hit the struck monster again where they start.
		EXPECT_EQ(blade->lastCollisionTargetHash, dMonster[41][40] ^ dPlayer[41][40]);
		EXPECT_LE(blade->duration, 25);
	}
}

TEST_F(Poe2WeaponDraftsTest, CinderStrikeThrowsMoltenBallsAhead)
{
	Player &player = SetUpFight();
	PlaceMonster(0, { 41, 40 });
	Use(player, WeaponSkill::CinderStrike, { 41, 40 });
	ASSERT_EQ(SkillZoneCount(), 3U);
	for (size_t i = 0; i < 3; i++) {
		const std::optional<Point> at = SkillZoneCenter(i);
		ASSERT_TRUE(at.has_value());
		EXPECT_GE(at->x, 41);
		EXPECT_LE(at->x, 40 + CinderBallTiles);
		EXPECT_LE(std::abs(at->y - 40), 1);
	}
	Zones(player, 20);
	EXPECT_EQ(SkillZoneCount(), 0U);
}

TEST_F(Poe2WeaponDraftsTest, SteelTempestLeavesAStormOfBlades)
{
	Player &player = SetUpFight();
	Monster &monster = PlaceMonster(0, { 41, 41 });
	Use(player, WeaponSkill::SteelTempest, { 41, 40 });
	ASSERT_EQ(SkillZoneCount(), 1U);
	Heal(monster);
	Zones(player, Row(WeaponSkill::SteelTempest).interval + 1);
	EXPECT_TRUE(Hurt(monster));
	Use(player, WeaponSkill::SteelTempest, { 41, 40 });
	Use(player, WeaponSkill::SteelTempest, { 41, 40 });
	EXPECT_EQ(SkillZoneCount(), 2U);
}

TEST_F(Poe2WeaponDraftsTest, ThunderHatchetsLightningLeapsOn)
{
	Player &player = SetUpFight();
	Monster &target = PlaceMonster(0, { 44, 40 });
	Monster &first = PlaceMonster(1, { 45, 40 });
	Monster &second = PlaceMonster(2, { 46, 41 });
	Monster &third = PlaceMonster(3, { 48, 41 });
	Monster &fourth = PlaceMonster(4, { 49, 41 });
	Monster &far = PlaceMonster(5, { 55, 40 });
	Use(player, WeaponSkill::ThunderHatchet, { 44, 40 });
	const std::vector<const Missile *> axes = MissilesOf(MissileID::Arrow);
	ASSERT_EQ(axes.size(), 1U);
	const Missile axe = *axes.front();
	OnTableMissileHit(axe, player, target, 0, false);
	// The axe's own hit is the missile's; the leaps are lightning, three of them.
	EXPECT_FALSE(Hurt(target));
	EXPECT_TRUE(Hurt(first));
	EXPECT_TRUE(Hurt(second));
	EXPECT_TRUE(Hurt(third));
	EXPECT_FALSE(Hurt(fourth));
	EXPECT_FALSE(Hurt(far));
}

TEST_F(Poe2WeaponDraftsTest, BloodHarvestReapsTheBleedingHarder)
{
	Player &player = SetUpFight();
	Monster &monster = PlaceMonster(0, { 41, 40 });
	const auto reap = [&](bool bleeding) {
		int64_t lost = 0;
		for (uint32_t seed = 1; seed <= 16; seed++) {
			Heal(monster);
			monster.poe2.bleedTicks = bleeding ? 60 : 0;
			SetRndSeed(seed);
			Use(player, WeaponSkill::BloodHarvest, { 41, 40 });
			lost += Lost(monster);
		}
		return lost;
	};
	const int64_t plain = reap(false);
	EXPECT_GT(plain, 0);
	EXPECT_GT(reap(true), plain * 5 / 4);
}

TEST_F(Poe2WeaponDraftsTest, PyreBlowsMarkBurstsWhenItDies)
{
	Player &player = SetUpFight();
	Monster &target = PlaceMonster(0, { 41, 40 });
	Monster &beside = PlaceMonster(1, { 42, 40 });
	UseUntilHurt(player, WeaponSkill::PyreBlow, { 41, 40 }, target);
	ASSERT_TRUE(Hurt(target));
	EXPECT_EQ(target.poe2.pyreTicks, Row(WeaponSkill::PyreBlow).duration);
	EXPECT_EQ(beside.poe2.pyreTicks, 0);
	EXPECT_FALSE(Hurt(beside));
	OnTableCursedMonsterKilled(target);
	EXPECT_TRUE(Hurt(beside));
	// Once only.
	EXPECT_EQ(target.poe2.pyreTicks, 0);
	Heal(beside);
	OnTableCursedMonsterKilled(target);
	EXPECT_FALSE(Hurt(beside));
	// The mark fades.
	Use(player, WeaponSkill::PyreBlow, { 41, 40 });
	target.poe2.pyreTicks = 3;
	for (int i = 0; i < 3; i++)
		ProcessTableCurses(target);
	EXPECT_EQ(target.poe2.pyreTicks, 0);
}

TEST_F(Poe2WeaponDraftsTest, MammothChargeTramplesAndSlams)
{
	Player &player = SetUpFight();
	player.position.tile = { 45, 40 };
	Monster &trampled = PlaceMonster(0, { 42, 41 });
	Monster &slammed = PlaceMonster(1, { 46, 40 });
	Monster &behind = PlaceMonster(2, { 37, 40 });
	for (uint32_t seed = 1; seed <= 8 && !Hurt(slammed); seed++) {
		player.poe2.dashStart = Point(40, 40);
		SetRndSeed(seed);
		Use(player, WeaponSkill::MammothCharge, { 47, 40 });
		EXPECT_EQ(player.poe2.dashStart, Point());
	}
	// The crack's burst always lands on what the charge passed.
	EXPECT_TRUE(Hurt(trampled));
	EXPECT_TRUE(Hurt(slammed));
	EXPECT_FALSE(Hurt(behind));
}

TEST_F(Poe2WeaponDraftsTest, PhantomBladeFliesOutAndBack)
{
	Player &player = SetUpFight();
	PlaceMonster(0, { 43, 40 });
	Use(player, WeaponSkill::PhantomBlade, { 43, 40 });
	std::vector<const Missile *> blades = MissilesOf(MissileID::Arrow);
	ASSERT_EQ(blades.size(), 1U);
	const Missile out = *blades.front();
	EXPECT_EQ(out.poe2.look, vfx::SpinningBladeLook);
	EXPECT_GE(out.poe2.piercesLeft, PhantomBladePierces);
	EXPECT_EQ(out.poe2.damagePercent, GemDamagePercent(SkillSlot::ForAttack(WeaponSkill::PhantomBlade), player.poe2.effectiveLevel(WeaponSkill::PhantomBlade)) * 110 / 100);
	// Six tiles at an arrow's speed, not the arrow's whole flight.
	EXPECT_GT(out.duration, 4);
	EXPECT_LT(out.duration, 20);
	ASSERT_EQ(SkillZoneCount(), 1U);
	EXPECT_EQ(SkillZoneCenter(0), std::optional<Point>(Point(46, 40)));
	// It comes back from its end to where the hero is then.
	player.position.tile = { 40, 41 };
	Zones(player, out.duration);
	EXPECT_EQ(MissilesOf(MissileID::Arrow).size(), 1U);
	Zones(player, 2);
	EXPECT_EQ(SkillZoneCount(), 0U);
	blades = MissilesOf(MissileID::Arrow);
	ASSERT_EQ(blades.size(), 2U);
	const Missile &back = *blades.back();
	EXPECT_EQ(Point(back.position.start), Point(46, 40));
	EXPECT_EQ(back.lastCollisionTargetHash, dMonster[46][40] ^ dPlayer[46][40]);
	EXPECT_GE(back.poe2.piercesLeft, PhantomBladePierces);
}

TEST_F(Poe2WeaponDraftsTest, HoradricSunderingBurstsTheAilingAndUsesUpTheirAilments)
{
	Player &player = SetUpFight();
	Monster &frozen = PlaceMonster(0, { 42, 40 });
	Monster &shocked = PlaceMonster(1, { 38, 40 });
	Monster &burning = PlaceMonster(2, { 40, 38 });
	Monster &plain = PlaceMonster(3, { 40, 42 });
	// Each just out of the pulse, beside one of them.
	Monster &byFrozen = PlaceMonster(4, { 43, 40 });
	Monster &byShocked = PlaceMonster(5, { 37, 40 });
	Monster &byBurning = PlaceMonster(6, { 40, 37 });
	Monster &byPlain = PlaceMonster(7, { 40, 43 });
	frozen.poe2.freezeTicks = 60;
	shocked.poe2.shockTicks = 60;
	burning.poe2.igniteTicks = 60;
	burning.poe2.igniteDamagePerTick = 1;
	Use(player, WeaponSkill::HoradricSundering, { 41, 40 });
	// The pulse always lands, as a spell does.
	for (const Monster *monster : { &frozen, &shocked, &burning, &plain })
		EXPECT_TRUE(Hurt(*monster));
	EXPECT_TRUE(Hurt(byFrozen));
	EXPECT_TRUE(Hurt(byShocked));
	EXPECT_TRUE(Hurt(byBurning));
	EXPECT_FALSE(Hurt(byPlain));
	EXPECT_EQ(frozen.poe2.freezeTicks, 0);
	EXPECT_EQ(shocked.poe2.shockTicks, 0);
	EXPECT_EQ(burning.poe2.igniteTicks, 0);
	// A frozen enemy's burst is the bigger.
	EXPECT_GT(Lost(byFrozen), Lost(byBurning));
}

TEST_F(Poe2WeaponDraftsTest, TollingShieldRingsOutStunningAndBreakingArmour)
{
	Player &player = SetUpFight();
	Monster &near = PlaceMonster(0, { 42, 40 });
	Monster &far = PlaceMonster(1, { 43, 40 });
	UseUntilHurt(player, WeaponSkill::TollingShield, { 41, 40 }, near);
	ASSERT_TRUE(Hurt(near));
	EXPECT_GT(near.poe2.heavyStunTicks, 0);
	EXPECT_GT(near.poe2.armourBreakTicks, 0);
	EXPECT_FALSE(Hurt(far));
	EXPECT_TRUE(IsShieldAttack(SkillSlot::ForAttack(WeaponSkill::TollingShield)));
}

TEST_F(Poe2WeaponDraftsTest, BulwarkRoarBlocksGuardsAndSendsShieldWaves)
{
	Player &player = SetUpFight();
	player.poe2.shieldBlockChance = 20;
	const int block = player.GetBlockChance();
	Monster &near = PlaceMonster(0, { 41, 40 });
	Use(player, WeaponSkill::BulwarkRoar, { 41, 40 });
	EXPECT_FALSE(Hurt(near));
	EXPECT_EQ(player.poe2.cryHits[BulwarkRoarCry], 3);
	EXPECT_EQ(player.GetBlockChance(), block + BulwarkBlockChance);
	// The Guard: 10% of 200 life takes the first 20 of what hits the hero.
	EXPECT_EQ(player.poe2.bulwarkGuard, Fixed26_6::fromInt(20).raw());
	EXPECT_EQ(AbsorbBulwarkGuard(player, Fixed26_6::fromInt(15)), Fixed26_6::fromInt(0));
	EXPECT_EQ(AbsorbBulwarkGuard(player, Fixed26_6::fromInt(15)), Fixed26_6::fromInt(10));
	EXPECT_EQ(AbsorbBulwarkGuard(player, Fixed26_6::fromInt(15)), Fixed26_6::fromInt(15));
	// A shield attack sends a wave four tiles on; another attack sends none.
	ClearMonsters();
	Monster &onWave = PlaceMonster(0, { 44, 40 });
	player.poe2.tick();
	Use(player, WeaponSkill::StormEdge, { 44, 40 });
	EXPECT_EQ(player.poe2.cryHits[BulwarkRoarCry], 3);
	EXPECT_FALSE(Hurt(onWave));
	EXPECT_FALSE(IsShieldAttack(SkillSlot::ForAttack(WeaponSkill::BulwarkRoar)));
	EXPECT_TRUE(IsShieldAttack(SkillSlot::ForAttack(WeaponSkill::ShieldCharge)));
	Use(player, WeaponSkill::TollingShield, { 44, 40 });
	EXPECT_EQ(player.poe2.cryHits[BulwarkRoarCry], 2);
	EXPECT_TRUE(Hurt(onWave));
	// It all ends with the roar.
	for (int i = 0; i < Row(WeaponSkill::BulwarkRoar).duration; i++)
		player.poe2.tick();
	EXPECT_EQ(player.poe2.cryHits[BulwarkRoarCry], 0);
	EXPECT_EQ(player.GetBlockChance(), block);
	player.poe2.bulwarkGuard = 100;
	player.poe2.tick();
	EXPECT_EQ(player.poe2.bulwarkGuard, 0);
}

TEST_F(Poe2WeaponDraftsTest, ZakarumsJudgementBurstsTwice)
{
	Player &player = SetUpFight();
	Monster &monster = PlaceMonster(0, { 42, 40 });
	Monster &far = PlaceMonster(1, { 43, 40 });
	UseUntilHurt(player, WeaponSkill::ZakarumsJudgement, { 41, 40 }, monster);
	ASSERT_TRUE(Hurt(monster));
	ClearSkillZones();
	Heal(monster);
	monster.poe2.heavyStunTicks = 0;
	Use(player, WeaponSkill::ZakarumsJudgement, { 41, 40 });
	ASSERT_EQ(SkillZoneCount(), 1U);
	Heal(monster);
	monster.poe2.heavyStunTicks = 0;
	Zones(player, ZakarumsJudgementTicks - 1);
	EXPECT_FALSE(Hurt(monster));
	Zones(player, 2);
	// The second ring always lands, and stuns.
	EXPECT_TRUE(Hurt(monster));
	EXPECT_GT(monster.poe2.heavyStunTicks, 0);
	EXPECT_FALSE(Hurt(far));
	EXPECT_EQ(SkillZoneCount(), 0U);
}

// The batch-2 build fixes from the balance re-score (batch2-fixes.md, 11-07).

TEST_F(Poe2WeaponDraftsTest, Fix1FanFliesAWholeDirectionApart)
{
	Player &player = SetUpFight();
	Monster &target = PlaceMonster(0, { 43, 40 });
	Use(player, WeaponSkill::MagmaHurl, { 43, 40 });
	ASSERT_EQ(MissilesOf(MissileID::Firebolt).size(), 1U);
	const Missile rock = *MissilesOf(MissileID::Firebolt).front();
	Missiles.clear();
	OnTableMissileHit(rock, player, target, 0, false);
	const std::vector<const Missile *> shrapnel = MissilesOf(MissileID::Firebolt);
	ASSERT_EQ(shrapnel.size(), static_cast<size_t>(MagmaHurlShrapnel));
	// The screen's angle of each one's flight (a missile's y speed is halved on screen).
	const auto angle = [](const Missile &missile) {
		return std::atan2(2.0 * missile.position.velocity.deltaY, static_cast<double>(missile.position.velocity.deltaX)) * 180.0 / 3.14159265358979;
	};
	const auto apart = [&](const Missile &a, const Missile &b) {
		double degrees = std::abs(angle(a) - angle(b));
		return degrees > 180.0 ? 360.0 - degrees : degrees;
	};
	// Straight on, then a whole direction to either side; the fourth straight on again, from two tiles further.
	EXPECT_NEAR(apart(*shrapnel[0], *shrapnel[1]), 45.0, 3.0);
	EXPECT_NEAR(apart(*shrapnel[0], *shrapnel[2]), 45.0, 3.0);
	EXPECT_NEAR(apart(*shrapnel[1], *shrapnel[2]), 90.0, 3.0);
	EXPECT_NEAR(apart(*shrapnel[0], *shrapnel[3]), 0.0, 3.0);
	EXPECT_EQ(Point(shrapnel[3]->position.start), Point(45, 40));
	// A row that would start in a wall starts where the first did.
	EXPECT_EQ(Point(shrapnel[0]->position.start), Point(43, 40));
}

TEST_F(Poe2WeaponDraftsTest, Fix2BulKathosRepeatIsAShareOfTheHitDealt)
{
	Player &player = SetUpFight();
	for (const int dealt : { 100, 1000 }) {
		ClearSupportEffects();
		ClearMonsters();
		player.poe2.clear();
		Monster &monster = PlaceMonster(0, { 41, 40 });
		Use(player, WeaponSkill::CryOfBulKathos, { 41, 40 });
		player.poe2.tick();
		OnEmpoweredMeleeHit(player, SkillSlot::ForAttack(WeaponSkill::StormEdge), monster, Fixed26_6::fromInt(dealt));
		EXPECT_EQ(player.poe2.cryHits[static_cast<size_t>(EmpoweringCry::BulKathos)], 2);
		// No repeat use waits: the strike comes again as a share of what it dealt, a moment later.
		Supports(player, BulKathosRepeatTicks - 1);
		EXPECT_FALSE(Hurt(monster));
		Supports(player, 3);
		EXPECT_TRUE(Hurt(monster)) << dealt;
		EXPECT_LE(Lost(monster), Fixed26_6::fromInt(dealt * Row(WeaponSkill::CryOfBulKathos).damagePercent / 100).raw()) << dealt;
		EXPECT_GT(Lost(monster), Fixed26_6::fromInt(dealt * Row(WeaponSkill::CryOfBulKathos).damagePercent / 200).raw()) << dealt;
	}
}

TEST_F(Poe2WeaponDraftsTest, Fix3BulwarkWaveIsOneTileWide)
{
	Player &player = SetUpFight();
	Monster &onLine = PlaceMonster(0, { 43, 40 });
	Monster &beside = PlaceMonster(1, { 43, 41 });
	Monster &end = PlaceMonster(2, { 44, 40 });
	Monster &past = PlaceMonster(3, { 45, 40 });
	SendBulwarkWave(player, { 46, 40 });
	EXPECT_TRUE(Hurt(onLine));
	EXPECT_TRUE(Hurt(end));
	EXPECT_FALSE(Hurt(beside));
	EXPECT_FALSE(Hurt(past));
}

// A fire row's weapon hit lands as fire: Cinder Strike, Pyre Blow and Brimstone Rift had landed physical.
TEST_F(Poe2WeaponDraftsTest, Fix4FireRowsLandAsFire)
{
	Player &player = SetUpFight();
	Monster &monster = PlaceMonster(0, { 41, 40 });
	ApplyPlayerAttackDamage(player, monster, Fixed26_6::fromInt(50), false);
	EXPECT_EQ(monster.poe2.fireHitTicks, 0);
	for (const WeaponSkill skill : { WeaponSkill::CinderStrike, WeaponSkill::PyreBlow, WeaponSkill::BrimstoneRift }) {
		Monster &struck = PlaceMonster(1, { 42, 40 });
		player.poe2.castSkill = skill;
		ApplyPlayerAttackDamage(player, struck, Fixed26_6::fromInt(50), false);
		player.poe2.castSkill = WeaponSkill::None;
		EXPECT_TRUE(Hurt(struck)) << Row(skill).name;
		EXPECT_GT(struck.poe2.fireHitTicks, 0) << Row(skill).name;
		ClearMonsters();
	}
}

TEST_F(Poe2WeaponDraftsTest, Fix5EarthsplitterShakesEachEnemyOnce)
{
	Player &player = SetUpFight();
	// Two broken enemies at the line's edge, and one just off it beside both: only the shockwaves reach it.
	const auto shaken = [&](bool both) {
		for (uint32_t seed = 1; seed <= 32; seed++) {
			ClearMonsters();
			Monster &first = PlaceMonster(0, { 43, 41 });
			Monster &second = PlaceMonster(1, { 45, 41 });
			Monster &off = PlaceMonster(2, { 44, 42 });
			first.poe2.armourBreakTicks = 60;
			second.poe2.armourBreakTicks = both ? 60 : 0;
			SetRndSeed(seed);
			Use(player, WeaponSkill::Earthsplitter, { 46, 40 });
			if (Hurt(first) && Hurt(second))
				return Lost(off);
		}
		ADD_FAILURE() << "the line never hit both";
		return 0;
	};
	const int32_t one = shaken(false);
	EXPECT_GT(one, 0);
	EXPECT_EQ(shaken(true), one);
}

TEST_F(Poe2WeaponDraftsTest, Fix5HoradricBurstsAnEnemyTwiceAtMost)
{
	Player &player = SetUpFight();
	// Burning enemies in the pulse, and one just out of it beside them all.
	const auto burst = [&](int burning) {
		ClearMonsters();
		const std::array<Point, 3> tiles { { { 42, 40 }, { 42, 41 }, { 42, 39 } } };
		for (int i = 0; i < 3; i++) {
			Monster &primed = PlaceMonster(static_cast<size_t>(i), tiles[static_cast<size_t>(i)]);
			primed.poe2.igniteTicks = i < burning ? 60 : 0;
			primed.poe2.igniteDamagePerTick = i < burning ? 1 : 0;
		}
		Monster &beside = PlaceMonster(3, { 43, 40 });
		Use(player, WeaponSkill::HoradricSundering, { 41, 40 });
		return Lost(beside);
	};
	const int32_t once = burst(1);
	EXPECT_GT(once, 0);
	const int32_t twice = burst(2);
	EXPECT_GT(twice, once);
	EXPECT_EQ(burst(3), twice);
	EXPECT_EQ(HoradricMaxBursts, 2);
}

TEST_F(Poe2WeaponDraftsTest, Fix5OneHephastosHammerLodgedAtATime)
{
	Player &player = SetUpFight();
	Monster &first = PlaceMonster(0, { 44, 40 });
	Monster &second = PlaceMonster(1, { 44, 43 });
	Use(player, WeaponSkill::HephastosHammer, { 44, 40 });
	ASSERT_EQ(MissilesOf(MissileID::Firebolt).size(), 1U);
	const Missile hammer = *MissilesOf(MissileID::Firebolt).front();
	OnTableMissileHit(hammer, player, first, 0, false);
	ASSERT_EQ(SkillZoneCount(), 1U);
	OnTableMissileHit(hammer, player, second, 0, false);
	ASSERT_EQ(SkillZoneCount(), 1U);
	EXPECT_EQ(SkillZoneCenter(0), std::optional<Point>(Point(44, 43)));
}

TEST_F(Poe2WeaponDraftsTest, Fix5HellfireAndQuakingSpendASwingsWorth)
{
	Player &player = SetUpFight();
	// A use's time in the hero's free attacks: a quick strike is one, a slow sweep two, a long spin three.
	EXPECT_EQ(CrySwingsOfUse(player, SkillSlot::ForAttack(WeaponSkill::StormEdge)), 1);
	EXPECT_EQ(CrySwingsOfUse(player, SkillSlot::ForAttack(WeaponSkill::HoradricSundering)), 2);
	EXPECT_EQ(CrySwingsOfUse(player, SkillSlot::ForAttack(WeaponSkill::MountainsFall)), 2);
	EXPECT_EQ(CrySwingsOfUse(player, SkillSlot::ForAttack(WeaponSkill::DervishAdvance)), 3);
	Monster &monster = PlaceMonster(0, { 41, 40 });
	Use(player, WeaponSkill::HellfireRoar, { 41, 40 });
	player.poe2.tick();
	OnEmpoweredMeleeHit(player, SkillSlot::ForAttack(WeaponSkill::HoradricSundering), monster, Fixed26_6::fromInt(10));
	EXPECT_EQ(player.poe2.cryHits[static_cast<size_t>(EmpoweringCry::Hellfire)], 1);
	// Still one use's worth however many it hits.
	OnEmpoweredMeleeHit(player, SkillSlot::ForAttack(WeaponSkill::HoradricSundering), monster, Fixed26_6::fromInt(10));
	EXPECT_EQ(player.poe2.cryHits[static_cast<size_t>(EmpoweringCry::Hellfire)], 1);
	// A use worth more than is left spends what is left, and is empowered.
	player.poe2.tick();
	OnEmpoweredMeleeHit(player, SkillSlot::ForAttack(WeaponSkill::DervishAdvance), monster, Fixed26_6::fromInt(10));
	EXPECT_EQ(player.poe2.cryHits[static_cast<size_t>(EmpoweringCry::Hellfire)], 0);
	// Quaking Roar's two hits go on one slow slam.
	player.poe2.clear();
	Use(player, WeaponSkill::QuakingRoar, { 41, 40 });
	player.poe2.tick();
	OnEmpoweredMeleeHit(player, SkillSlot::ForAttack(WeaponSkill::MountainsFall), monster, Fixed26_6::fromInt(10));
	EXPECT_EQ(player.poe2.cryHits[static_cast<size_t>(EmpoweringCry::Quaking)], 0);
	// Cry of Bul-Kathos still spends one a use (only a strike spends it).
	player.poe2.clear();
	Use(player, WeaponSkill::CryOfBulKathos, { 41, 40 });
	player.poe2.tick();
	OnEmpoweredMeleeHit(player, SkillSlot::ForAttack(WeaponSkill::StormEdge), monster, Fixed26_6::fromInt(10));
	EXPECT_EQ(player.poe2.cryHits[static_cast<size_t>(EmpoweringCry::BulKathos)], 2);
}

TEST_F(Poe2WeaponDraftsTest, Fix6MountainsFallIsAPlusOnScreen)
{
	Player &player = SetUpFight();
	const Point center { 43, 40 };
	const int width = Row(WeaponSkill::MountainsFall).radius;
	int tiles = 0;
	int middle = 0;
	for (int dx = -6; dx <= 6; dx++) {
		for (int dy = -6; dy <= 6; dy++) {
			const int percent = MountainsFallPercentAt(center, center + Displacement { dx, dy }, width, 100);
			tiles += percent > 0 ? 1 : 0;
			middle += percent == MountainsFallCentrePercent ? 1 : 0;
		}
	}
	EXPECT_EQ(tiles, 21);
	EXPECT_EQ(middle, 5);
	// Its arms run down and across the screen, through the arm spikes.
	for (const Point spike : MountainsFallSpikes(center))
		EXPECT_GT(MountainsFallPercentAt(center, spike, width, 100), 0);
	EXPECT_EQ(MountainsFallPercentAt(center, center + Displacement { 1, 1 }, width, 100), 100);
	// Off it: a grid step two out, and the far corners the old grid arms reached.
	EXPECT_EQ(MountainsFallPercentAt(center, center + Displacement { 2, 0 }, width, 100), 0);
	EXPECT_EQ(MountainsFallPercentAt(center, center + Displacement { 3, 3 }, width, 100), 0);
	Monster &onArm = PlaceMonster(0, center + Displacement { 2, 2 });
	Monster &offArm = PlaceMonster(1, center + Displacement { 3, 3 });
	UseUntilHurt(player, WeaponSkill::MountainsFall, center, onArm);
	EXPECT_TRUE(Hurt(onArm));
	EXPECT_FALSE(Hurt(offArm));
}

TEST_F(Poe2WeaponDraftsTest, Fix7OnlyWarCrySaysItStuns)
{
	Player &player = SetUpFight();
	EXPECT_NE(GemLevelText(SkillSlot::ForAttack(WeaponSkill::WarCry), 1).find("stuns"), std::string::npos);
	for (const WeaponSkill cry : { WeaponSkill::HellfireRoar, WeaponSkill::QuakingRoar, WeaponSkill::CryOfBulKathos, WeaponSkill::BulwarkRoar }) {
		const std::string text = GemLevelText(SkillSlot::ForAttack(cry), 1);
		EXPECT_EQ(text.find("tuns"), std::string::npos) << text;
		EXPECT_NE(text.find(TableSkillExtraNote(cry)), std::string::npos) << text;
		player.poe2.skillSlots[0] = SkillSlot::ForAttack(cry);
		player.poe2.slotLevels[0] = 1;
		bool noted = false;
		for (const SkillDetailLine &line : SkillDetails(player, 0)) {
			EXPECT_EQ(line.value.find("Stuns enemies"), std::string::npos) << Row(cry).name;
			EXPECT_EQ(line.label.find("Stuns enemies"), std::string::npos) << Row(cry).name;
			noted = noted || line.value == TableSkillExtraNote(cry) || line.label == TableSkillExtraNote(cry);
		}
		EXPECT_TRUE(noted) << Row(cry).name;
	}
}

// Curtis's three lowers (23:35 UTC 11-07, batch2-fixes.md "Approved"): Frostbite Blades pierce 1, Blood Harvest's kill
// burst 25% of the hit, Hellfire Roar's burst 8%.
TEST_F(Poe2WeaponDraftsTest, ApprovedLowersFrostbiteBloodHarvestAndHellfire)
{
	EXPECT_EQ(FrostbiteBladePierces, 1);
	EXPECT_EQ(BloodHarvestBurstPercent, 25);
	EXPECT_EQ(Row(WeaponSkill::HellfireRoar).damagePercent, 8);
	EXPECT_NE(TableSkillExtraNote(WeaponSkill::FrostbiteBlades).find("through 1 enemies"), std::string::npos);
	EXPECT_NE(TableSkillExtraNote(WeaponSkill::BloodHarvest).find("bursts for 25% of the hit"), std::string::npos);
	EXPECT_NE(TableSkillExtraNote(WeaponSkill::HellfireRoar).find("8% of the hit"), std::string::npos);
	// A blade goes through one more enemy only.
	Player &player = SetUpFight();
	Monster &monster = PlaceMonster(0, { 41, 40 });
	UseUntilHurt(player, WeaponSkill::FrostbiteBlades, { 41, 40 }, monster);
	for (const Missile *blade : MissilesOf(MissileID::BlueFlare))
		EXPECT_EQ(blade->poe2.piercesLeft, 1);
	// Hellfire's burst is 8% of the hit it empowers.
	ClearMonsters();
	Monster &struck = PlaceMonster(0, { 41, 40 });
	Monster &beside = PlaceMonster(1, { 42, 40 });
	Use(player, WeaponSkill::HellfireRoar, { 41, 40 });
	player.poe2.tick();
	OnEmpoweredMeleeHit(player, SkillSlot::ForAttack(WeaponSkill::StormEdge), struck, Fixed26_6::fromInt(1000));
	Supports(player, 3);
	EXPECT_NEAR(static_cast<double>(Lost(beside)) / Fixed26_6::fromInt(80).raw(), 1.0, 0.1);
}

// audit-drafts-bow.md 8.2 (Curtis): a use's Hellfire bursts burn each monster once, however many of them overlap it.
TEST_F(Poe2WeaponDraftsTest, ApprovedHellfireBurnsEachMonsterOnceAUse)
{
	Player &player = SetUpFight();
	Monster &a = PlaceMonster(0, { 41, 40 });
	Monster &b = PlaceMonster(1, { 41, 41 });
	Monster &between = PlaceMonster(2, { 42, 40 });
	Use(player, WeaponSkill::HellfireRoar, { 41, 40 });
	player.poe2.tick();
	// One use hits a and b; both bursts reach all three, but each takes one.
	OnEmpoweredMeleeHit(player, SkillSlot::ForAttack(WeaponSkill::StormEdge), a, Fixed26_6::fromInt(1000));
	OnEmpoweredMeleeHit(player, SkillSlot::ForAttack(WeaponSkill::StormEdge), b, Fixed26_6::fromInt(1000));
	Supports(player, 3);
	const double one = Fixed26_6::fromInt(80).raw();
	for (const Monster *monster : { &a, &b, &between })
		EXPECT_NEAR(Lost(*monster) / one, 1.0, 0.1);
	EXPECT_NE(TableSkillExtraNote(WeaponSkill::HellfireRoar).find("each enemy once a use"), std::string::npos);
	// The next use burns them again.
	player.poe2.tick();
	OnEmpoweredMeleeHit(player, SkillSlot::ForAttack(WeaponSkill::StormEdge), a, Fixed26_6::fromInt(1000));
	Supports(player, 3);
	EXPECT_NEAR(Lost(between) / one, 2.0, 0.2);
}

// audit-drafts-bow.md 7.4: a use that finds fewer of the cry's charges left than its swings gets only that share.
TEST_F(Poe2WeaponDraftsTest, BowFix4ACryUseShortOfChargesGetsItsShare)
{
	Player &player = SetUpFight();
	Monster &struck = PlaceMonster(0, { 41, 40 });
	Monster &beside = PlaceMonster(1, { 42, 40 });
	ASSERT_EQ(CrySwingsOfUse(player, SkillSlot::ForAttack(WeaponSkill::HoradricSundering)), 2);
	Use(player, WeaponSkill::HellfireRoar, { 41, 40 });
	player.poe2.tick();
	// Three charges: a 2-swing use pays 2 and is empowered in full; the next finds 1 and gets half.
	OnEmpoweredMeleeHit(player, SkillSlot::ForAttack(WeaponSkill::HoradricSundering), struck, Fixed26_6::fromInt(1000));
	Supports(player, 3);
	const int32_t full = Lost(beside);
	ASSERT_GT(full, 0);
	EXPECT_EQ(player.poe2.cryHits[static_cast<size_t>(EmpoweringCry::Hellfire)], 1);
	Heal(beside);
	Heal(struck);
	OnEmpoweredMeleeHit(player, SkillSlot::ForAttack(WeaponSkill::HoradricSundering), struck, Fixed26_6::fromInt(1000));
	// The same use's other monsters get the same share, and spend no more.
	OnEmpoweredMeleeHit(player, SkillSlot::ForAttack(WeaponSkill::HoradricSundering), beside, Fixed26_6::fromInt(1000));
	EXPECT_EQ(player.poe2.cryShareThisTick[static_cast<size_t>(EmpoweringCry::Hellfire)], 50);
	EXPECT_EQ(player.poe2.cryHits[static_cast<size_t>(EmpoweringCry::Hellfire)], 0);
	Supports(player, 3);
	// Half of one burst: the use's two bursts overlap it, but burn it once (audit-drafts-bow.md 8.2).
	EXPECT_NEAR(static_cast<double>(Lost(struck)) / full, 0.5, 0.05);
	// Quaking Roar's two charges on a 3-swing slam: two thirds of the aftershock.
	player.poe2.clear();
	ClearMonsters();
	Monster &slammed = PlaceMonster(0, { 41, 40 });
	Use(player, WeaponSkill::QuakingRoar, { 41, 40 });
	player.poe2.tick();
	ASSERT_EQ(CrySwingsOfUse(player, SkillSlot::ForAttack(WeaponSkill::DervishAdvance)), 3);
	EXPECT_EQ(player.poe2.cryHits[static_cast<size_t>(EmpoweringCry::Quaking)], 2);
	ASSERT_NE(SkillTags(SkillSlot::ForAttack(WeaponSkill::MountainsFall)) & ST_Slam, 0U);
	const int swings = CrySwingsOfUse(player, SkillSlot::ForAttack(WeaponSkill::MountainsFall));
	OnEmpoweredMeleeHit(player, SkillSlot::ForAttack(WeaponSkill::MountainsFall), slammed, Fixed26_6::fromInt(1000));
	EXPECT_EQ(player.poe2.cryShareThisTick[static_cast<size_t>(EmpoweringCry::Quaking)], std::min(2, swings) * 100 / swings);
}

TEST_F(Poe2WeaponDraftsTest, Fix8PhantomBladeTakesNoMoreProjectiles)
{
	const SkillSlot blade = SkillSlot::ForAttack(WeaponSkill::PhantomBlade);
	for (const SupportGem gem : { SupportGem::Volley, SupportGem::StoredVolley, SupportGem::NovaProjectiles })
		EXPECT_FALSE(SupportGemCanSupport(gem, blade));
	// Other projectile skills still take them, and the blade its other projectile supports.
	EXPECT_TRUE(SupportGemCanSupport(SupportGem::Volley, SkillSlot::ForAttack(WeaponSkill::ThunderHatchet)));
	EXPECT_TRUE(SupportGemCanSupport(SupportGem::Ricochet, blade));
}

TEST_F(Poe2WeaponDraftsTest, Fix9EchoesLandWhereTheSlamLands)
{
	Player &player = SetUpFight();
	const Point aimed { 46, 40 };
	EXPECT_EQ(TableSkillHitTarget(player, WeaponSkill::ZakarumsJudgement, aimed), player.position.tile);
	EXPECT_EQ(TableSkillHitTarget(player, WeaponSkill::MammothCharge, aimed), player.position.tile);
	EXPECT_EQ(TableSkillHitTarget(player, WeaponSkill::Earthsplitter, aimed), Point(41, 40));
	EXPECT_EQ(TableSkillHitTarget(player, WeaponSkill::BrimstoneRift, aimed), Point(41, 40));
	EXPECT_EQ(TableSkillHitTarget(player, WeaponSkill::Earthsplitter, player.position.tile), player.position.tile + player._pdir);
	// The others as they were.
	EXPECT_EQ(TableSkillHitTarget(player, WeaponSkill::ArreatCrash, aimed), Point(42, 40));
	EXPECT_EQ(TableSkillHitTarget(player, WeaponSkill::StormEdge, aimed), aimed);
}

} // namespace
} // namespace devilution
