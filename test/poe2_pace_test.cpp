/**
 * POE2 Mashup pace: how long each class's free attack and early skills take to kill an ordinary white monster.
 *
 * Every number comes from the game: the hero's damage (CalcPlrInv, the Skills panel's estimate of a hit), the monster's
 * life (monster_levels.tsv) and the time of a held use, measured by holding the button as the game does. The pace
 * checks hold the owner's rule that a slower skill hits harder, so it still beats the free attack per second
 * (Curtis 2026-10-27; damage_pct in skill_speed.tsv, written by tools/poe2_skill_speed_damage.py).
 *
 * With POE2_PACE_OUT set to a file, PrintPace writes every number to it for the rebalance's pace_compare.py and
 * solve_pace.py (research/attack-speed/rebalance).
 */
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "engine/lighting_defs.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/attack_speed.hpp"
#include "poe2/bases.hpp"
#include "poe2/default_attack.hpp"
#include "poe2/gem_levels.hpp"
#include "poe2/item_stats.hpp"
#include "poe2/loot.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/monster_levels.hpp"
#include "poe2/player_data.hpp"
#include "poe2/skill_details.hpp"
#include "poe2/skill_resources.hpp"
#include "poe2/support_table.hpp"
#include "poe2/table_skills.hpp"
#include "poe2/unique_effects.hpp"
#include "poe2/weapon_kinds.hpp"
#include "poe2/weapon_skills.hpp"
#include "spells.h"
#include "tables/itemdat.h"
#include "tables/playerdat.hpp"
#include "tables/spelldat.h"
#include "utils/is_of.hpp"

namespace devilution {

extern void TestPlayerProcessAction(Player &player);

namespace {

/** @brief A class as it starts: its weapons' kinds (by their short name), and the skills it uses early. */
struct PaceClass {
	HeroClass heroClass;
	std::string_view name;
	std::string_view mainHand;
	std::string_view offHand;
	/** A base for the off hand that stays the same (the Warrior's starting shield), else the best of offHand. */
	std::string_view fixedOffHand;
	/** Whether the main hand is one-handed (beside a shield or a second weapon). */
	bool oneHanded;
	std::vector<WeaponSkill> skills;
	SpellID spell = SpellID::Null;
	/**
	 * Further skills and spells measured for the record, not in the class's pace: per-strike skills, more bow skills and
	 * the Sorcerer's later spells. Written with "x:" before their name; timed by UseSeconds, the Skills panel's time.
	 */
	std::vector<WeaponSkill> extraSkills = {};
	std::vector<SpellID> extraSpells = {};
};

const std::vector<PaceClass> &PaceClasses()
{
	static const std::vector<PaceClass> Classes {
		{ HeroClass::Warrior, "Warrior", "Sword", "", "poe2:shield_armour_wooden_tower_shield", true, { WeaponSkill::GroundSlam, WeaponSkill::Cleave }, SpellID::Null, { WeaponSkill::Zeal } },
		{ HeroClass::Rogue, "Rogue", "Bow", "", "", false, { WeaponSkill::Barrage, WeaponSkill::LightningArrow }, SpellID::Null,
		    { WeaponSkill::RainOfArrows, WeaponSkill::ClusterBolt, WeaponSkill::Multishot, WeaponSkill::GuidedArrow, WeaponSkill::EscapeShot } },
		{ HeroClass::Sorcerer, "Sorcerer", "Staff", "", "", false, {}, SpellID::Firebolt, { WeaponSkill::EmberFusillade, WeaponSkill::IceBolt },
		    { SpellID::ChargedBolt, SpellID::HolyBolt, SpellID::Lightning, SpellID::Flash, SpellID::Fireball, SpellID::ChainLightning, SpellID::Elemental,
		        SpellID::FlameWave, SpellID::Immolation, SpellID::Nova, SpellID::BloodStar, SpellID::Apocalypse, SpellID::RuneOfFire, SpellID::RuneOfNova } },
		{ HeroClass::Monk, "Monk", "Quarterstaff", "", "", false, { WeaponSkill::WhirlingStaff, WeaponSkill::FistsOfThunder }, SpellID::Null, { WeaponSkill::SevenSidedStrike } },
		{ HeroClass::Bard, "Bard", "Sword", "Dagger", "", true, { WeaponSkill::Flurry }, SpellID::Null, { WeaponSkill::Zeal } },
	};
	return Classes;
}

/**
 * @brief The share of a per-strike skill's strike a lone monster takes, in percent: Seven-Sided Strike spreads its strikes
 * over the enemies around, so one alone takes a part of each; the others land in full.
 */
double LoneTargetStrikePercent(WeaponSkill skill)
{
	if (const TableSkillData *table = FindTableSkill(skill); table != nullptr && table->shape == TableShape::Seven)
		return SevenSidedStrikeSharePercent(1, table->count);
	return 100.0;
}

constexpr double TicksPerSecond = 20;
constexpr int PaceLevels[] = { 1, 5, 15, 30, 45, 60 };

/**
 * @brief A self-found weapon from area 13 on, as the harder-monsters fight check counts it: the best tiers of local added
 * and increased physical damage at its item level (the area's), each rolled in the middle (modifier_tiers.tsv).
 */
struct FoundWeaponMods {
	int level;
	int addedMin;
	int addedMax;
	int increased;
};
constexpr FoundWeaponMods FoundMods[] = {
	{ 12, (4 + 6) / 2, (7 + 10) / 2, (35 + 49) / 2 },
	{ 24, (7 + 10) / 2, (11 + 16) / 2, (50 + 64) / 2 },
	{ 36, (11 + 15) / 2, (17 + 24) / 2, (65 + 79) / 2 },
	{ 48, (16 + 22) / 2, (25 + 34) / 2, (80 + 99) / 2 },
	{ 60, (23 + 31) / 2, (35 + 47) / 2, (100 + 119) / 2 },
};
constexpr int FirstFoundLevel = 13;

/** @brief Hail of Arrows' arrows that hit one target, of its five (the harder-monsters fight check's count). */
constexpr double BarrageArrowsOnOneTarget = 3;

class Poe2PaceTest : public ::testing::Test {
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
		leveltype = DTYPE_CATHEDRAL;
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		LoadPlayerDataFiles();
		LoadMissileData();
		LoadItemData();
		LoadSpellData();
		LoadModifierData();
		LoadSkillSpeedData();
		LoadMonsterLevels();
		Missiles.clear();
	}

	void TearDown() override
	{
		dMonster[Front().x][Front().y] = 0;
		Missiles.clear();
		leveltype = DTYPE_TOWN;
		gbIsPoe2 = false;
		LoadModifierData();
		LoadSkillSpeedData();
		LoadMonsterLevels();
		UnloadModArchives();
		LoadMissileData();
	}

	static Point Front()
	{
		return Point { 20, 20 } + Direction::South;
	}

	/** @brief A fresh hero of the class at the level: the class's own attributes, no passives, no supports. */
	static void MakeHero(Player &player, HeroClass heroClass, int level)
	{
		player = {};
		player._pClass = heroClass;
		const ClassAttributes &attributes = GetClassAttributes(heroClass);
		player._pBaseStr = player._pStrength = attributes.baseStr;
		player._pBaseMag = player._pMagic = attributes.baseMag;
		player._pBaseDex = player._pDexterity = attributes.baseDex;
		player._pBaseVit = player._pVitality = attributes.baseVit;
		player.setCharacterLevel(static_cast<uint8_t>(level));
		player.lightId = NO_LIGHT;
		player._pHitPoints = Fixed26_6::fromInt(100);
		player._pHPBase = player._pHitPoints;
		player._pMaxHP = player._pHitPoints;
		player.position.tile = { 20, 20 };
		player.position.future = player.position.tile;
		player.position.old = player.position.tile;
		player._pdir = Direction::South;
	}

	/** @brief Puts a normal item of the base in the hand, with the attributes it asks for. */
	static void Hold(Player &player, inv_body_loc hand, _item_indexes base)
	{
		Item &item = player.InvBody[hand];
		item = {};
		SetLootRarity(item, ITEM_QUALITY_NORMAL);
		SetRndSeed(1);
		SetupAllItems(player, item, base, AdvanceRndSeed(), 30, 1, /*onlygood=*/false, /*pregen=*/false, 0, /*forceNotUnique=*/true);
		item._iStatFlag = true;
		// The hero puts points into what the base asks for, as a player would to wield it.
		player._pBaseStr = std::max<int>(player._pBaseStr, item._iMinStr);
		player._pBaseMag = std::max<int>(player._pBaseMag, item._iMinMag);
		player._pBaseDex = std::max<int>(player._pBaseDex, item._iMinDex);
		player._pStrength = player._pBaseStr;
		player._pMagic = player._pBaseMag;
		player._pDexterity = player._pBaseDex;
	}

	static void Ready(Player &player)
	{
		CalcPlrInv(player, false);
		SetPlrAnims(player);
		player._pMana = Fixed26_6::fromInt(100000);
		player._pManaBase = player._pMana;
		player._pmode = PM_STAND;
		player.destAction = ACTION_NONE;
		player.poe2.actionTimeRemainder = 0;
		ClrPlrPath(player);
	}

	/** @brief The bases of a kind that drop by the area level, as the item table has them. */
	static std::vector<_item_indexes> Bases(std::string_view kind, int areaLevel, bool oneHanded = false)
	{
		std::vector<_item_indexes> bases;
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			const ItemData &data = AllItemsList[i];
			// The Sorcerer's staves are the Focused and Quick ones, pinned by kind (they no longer drop): the caster
			// weapons rework's staves have no damage and cast their own skills, measured by the caster pace case.
			const bool available = kind == "Staff" ? WeaponKindOf(static_cast<int>(i)) == WeaponKind::LegacyStaff && BaseMetaFor(static_cast<int>(i)) != nullptr : data.dropRate > 0;
			if (available && data.iClass == ICLASS_WEAPON && data.iSName == kind && data.iMinMLvl <= areaLevel && (!oneHanded || data.iLoc == ILOC_ONEHAND))
				bases.push_back(static_cast<_item_indexes>(i));
		}
		return bases;
	}

	/**
	 * @brief Holds the attack (or a swing skill) for a number of ticks, as RepeatPlayerAction does; the average ticks from
	 * one use's start to the next. Double Swing's backhand belongs to its use.
	 */
	static double HeldTicks(Player &player, WeaponSkill skill, bool ranged, int ticks = 400)
	{
		player._pmode = PM_STAND;
		player.destAction = ACTION_NONE;
		player.poe2.actionTimeRemainder = 0;
		const PLR_MODE attackMode = ranged ? PM_RATTACK : PM_ATTACK;
		const Point front = Front();
		Monster &monster = Monsters[0];
		monster.position.tile = front;
		monster.position.old = front;
		monster.position.future = front;
		monster.hitPoints = Fixed26_6::fromInt(100);
		monster.goal = MonsterGoal::Normal;
		std::vector<int> starts;
		for (int tick = 0; tick < ticks; tick++) {
			if (player.destAction == ACTION_NONE && player.CanChangeAction()) {
				player.poe2.pendingSkill = skill;
				player.poe2.pendingSkillTicks = 40;
				player.destAction = ranged ? ACTION_RATTACK : ACTION_ATTACK;
				player.destParam1 = front.x;
				player.destParam2 = front.y;
			}
			// A monster stands in front through the follow-through only, so Double Swing backhands but no hit lands.
			const bool followThrough = player._pmode == PM_ATTACK && player.poe2.flurryStrike == 0 && player.AnimInfo.currentFrame >= player._pAFNum;
			dMonster[front.x][front.y] = followThrough ? 1 : 0;
			const PLR_MODE modeBefore = player._pmode;
			const int frameBefore = player.AnimInfo.currentFrame;
			const uint8_t strikeBefore = player.poe2.flurryStrike;
			TestPlayerProcessAction(player);
			player.poe2.tick();
			Missiles.clear();
			if (player._pmode != attackMode || (modeBefore == attackMode && player.AnimInfo.currentFrame > frameBefore))
				continue;
			if (strikeBefore == 0 && player.poe2.flurryStrike == 1)
				continue;
			starts.push_back(tick);
		}
		dMonster[front.x][front.y] = 0;
		if (starts.size() < 3)
			return 0;
		return static_cast<double>(starts.back() - starts[1]) / static_cast<double>(starts.size() - 2);
	}

	/** @brief Holds a spell's button, as HeldTicks holds an attack. */
	static double HeldCastTicks(Player &player, SpellID spell, int ticks = 400)
	{
		player._pmode = PM_STAND;
		player.destAction = ACTION_NONE;
		player.poe2.actionTimeRemainder = 0;
		const Point target = Front() + Direction::South + Direction::South;
		std::vector<int> starts;
		for (int tick = 0; tick < ticks; tick++) {
			if (player.destAction == ACTION_NONE && player.CanChangeAction()) {
				player.queuedSpell.spellId = spell;
				player.queuedSpell.spellType = SpellType::Spell;
				player.destAction = ACTION_SPELL;
				player.destParam1 = target.x;
				player.destParam2 = target.y;
			}
			const PLR_MODE modeBefore = player._pmode;
			const int frameBefore = player.AnimInfo.currentFrame;
			TestPlayerProcessAction(player);
			player.poe2.tick();
			Missiles.clear();
			if (player._pmode != PM_SPELL || (modeBefore == PM_SPELL && player.AnimInfo.currentFrame > frameBefore))
				continue;
			starts.push_back(tick);
		}
		if (starts.size() < 3)
			return 0;
		return static_cast<double>(starts.back() - starts[1]) / static_cast<double>(starts.size() - 2);
	}

	/** @brief Seconds of a held use of the skill (None: the free attack). */
	static double UseTime(Player &player, WeaponSkill skill)
	{
		const bool ranged = IsRangedDefaultAttack(DefaultAttackFor(player));
		if (skill != WeaponSkill::FistsOfThunder)
			return HeldTicks(player, skill, ranged) / TicksPerSecond;
		// Fists of Thunder: two strikes, then the bursting one (the Combo stays put with nothing to hit).
		player.poe2.combo = 0;
		player.poe2.comboTicks = 255;
		const double strike = HeldTicks(player, skill, ranged);
		player.poe2.combo = 2;
		player.poe2.comboTicks = 255;
		const double burst = HeldTicks(player, skill, ranged);
		player.poe2.combo = 0;
		return (((FistsOfThunderBurstEvery - 1) * strike) + burst) / FistsOfThunderBurstEvery / TicksPerSecond;
	}

	/** @brief One use's average damage to a lone target, before its defences. */
	static double UseDamage(Player &player, WeaponSkill skill, int gemLevel)
	{
		if (skill == WeaponSkill::None)
			return SkillHitAverage(player, DefaultSkillSlot(player), 1);
		const SkillSlot slot = SkillSlot::ForAttack(skill);
		player.poe2.skillSlots[0] = slot;
		player.poe2.slotLevels[0] = static_cast<uint8_t>(gemLevel);
		const double hit = SkillHitAverage(player, slot, gemLevel);
		const double percent = GemDamagePercent(slot, gemLevel) / 100.0;
		switch (skill) {
		case WeaponSkill::Flurry:
			// The strike, then the backhand.
			return hit * (100 + FlurryBackhandDamagePercent) / 100;
		case WeaponSkill::Barrage:
			return hit * BarrageArrowsOnOneTarget;
		case WeaponSkill::LightningArrow:
			// The burst rolls half to all of its damage (ProcessElementalArrow), and lands on the target too.
			return hit + (LightningArrowDamage(player) * 0.75 * percent);
		case WeaponSkill::FistsOfThunder: {
			// Every third strike bursts for 60% of the weapon's average roll (FistsOfThunderBurst).
			const auto [low, high] = AttackWeaponDamage(player, slot);
			const double burst = std::max(((low + high) / 2) * GemDamagePercent(slot, gemLevel) / 100 * 6 / 10, 1);
			return hit + (burst / FistsOfThunderBurstEvery);
		}
		default:
			return hit;
		}
	}

	static double CastDamage(Player &player, SpellID spell, int gemLevel)
	{
		const SkillSlot slot = SkillSlot::ForSpell(spell);
		player.poe2.skillSlots[0] = slot;
		player.poe2.slotLevels[0] = static_cast<uint8_t>(gemLevel);
		return SkillHitAverage(player, slot, gemLevel);
	}

	struct Row {
		std::string what;
		int gemLevel;
		double damage;
		double seconds;
	};

	/** @brief The hero with the best bases of the class's kinds for the level, by the free attack's damage per second. */
	static std::string BestGear(Player &player, const PaceClass &paceClass, int level)
	{
		double best = -1;
		_item_indexes bestMain = IDI_NONE;
		_item_indexes bestOff = IDI_NONE;
		const std::vector<_item_indexes> mains = Bases(paceClass.mainHand, level, paceClass.oneHanded);
		std::vector<_item_indexes> offs = paceClass.offHand.empty() ? std::vector<_item_indexes> { IDI_NONE } : Bases(paceClass.offHand, level);
		if (!paceClass.fixedOffHand.empty())
			offs = { FindPoe2ItemId(paceClass.fixedOffHand).value_or(IDI_NONE) };
		for (const _item_indexes main : mains) {
			for (const _item_indexes off : offs) {
				MakeHero(player, paceClass.heroClass, level);
				Hold(player, INVLOC_HAND_LEFT, main);
				if (off != IDI_NONE)
					Hold(player, INVLOC_HAND_RIGHT, off);
				Ready(player);
				const double seconds = UseTime(player, WeaponSkill::None);
				const double dps = seconds > 0 ? UseDamage(player, WeaponSkill::None, 1) / seconds : 0;
				if (dps > best) {
					best = dps;
					bestMain = main;
					bestOff = off;
				}
			}
		}
		MakeHero(player, paceClass.heroClass, level);
		Hold(player, INVLOC_HAND_LEFT, bestMain);
		std::string gear = AllItemsList[bestMain].iName;
		if (bestOff != IDI_NONE) {
			Hold(player, INVLOC_HAND_RIGHT, bestOff);
			gear += " + " + AllItemsList[bestOff].iName;
		}
		Ready(player);
		return gear;
	}

	/** @brief Rolls the self-found weapon modifiers for the level onto the hero's weapons. */
	static void Find(Player &player, int level)
	{
		const FoundWeaponMods *mods = nullptr;
		for (const FoundWeaponMods &tier : FoundMods) {
			if (tier.level <= level)
				mods = &tier;
		}
		if (mods == nullptr)
			return;
		for (const inv_body_loc hand : { INVLOC_HAND_LEFT, INVLOC_HAND_RIGHT }) {
			Item &item = player.InvBody[hand];
			if (item.isEmpty() || item._iClass != ICLASS_WEAPON)
				continue;
			ApplyModEffect(item, ModEffect::AddedPhysical, mods->addedMin, mods->addedMax);
			ApplyModEffect(item, ModEffect::PhysicalPercent, mods->increased, 0);
		}
		Ready(player);
	}

	static std::vector<Row> Measure(Player &player, const PaceClass &paceClass, int level)
	{
		std::vector<Row> rows;
		const int gemLevel = MaxGemLevelForCharacter(level);
		rows.push_back({ "free", 1, UseDamage(player, WeaponSkill::None, 1), UseTime(player, WeaponSkill::None) });
		for (const WeaponSkill skill : paceClass.skills) {
			const double damage = UseDamage(player, skill, gemLevel);
			rows.push_back({ std::string(WeaponSkillName(skill)), gemLevel, damage, UseTime(player, skill) });
		}
		if (paceClass.spell != SpellID::Null) {
			// The starting spell's book level (Firebolt 2), which a socketed gem of a higher level beats.
			const PlayerStartingLoadoutData &loadout = GetPlayerStartingLoadoutForClass(paceClass.heroClass);
			player._pSplLvl[static_cast<size_t>(paceClass.spell)] = static_cast<uint8_t>(loadout.spell == paceClass.spell ? loadout.spellLevel : 1);
			const double damage = CastDamage(player, paceClass.spell, gemLevel);
			rows.push_back({ std::string(GetSpellData(paceClass.spell).sNameText), gemLevel, damage, HeldCastTicks(player, paceClass.spell) / TicksPerSecond });
		}
		return rows;
	}

	/** @brief The class's further skills and spells (PaceClass::extraSkills, extraSpells), one use against a lone target. */
	static std::vector<Row> MeasureExtras(Player &player, const PaceClass &paceClass, int level)
	{
		std::vector<Row> rows;
		const int gemLevel = MaxGemLevelForCharacter(level);
		for (const WeaponSkill skill : paceClass.extraSkills) {
			const SkillSlot slot = SkillSlot::ForAttack(skill);
			player.poe2.skillSlots[0] = slot;
			player.poe2.slotLevels[0] = static_cast<uint8_t>(gemLevel);
			const double damage = SkillHitAverage(player, slot, gemLevel) * LoneTargetStrikePercent(skill) / 100;
			rows.push_back({ "x:" + std::string(WeaponSkillName(skill)), gemLevel, damage, UseSeconds(player, slot) });
		}
		for (const SpellID spell : paceClass.extraSpells) {
			player._pSplLvl[static_cast<size_t>(spell)] = 1;
			const double damage = CastDamage(player, spell, gemLevel);
			rows.push_back({ "x:" + std::string(GetSpellData(spell).sNameText), gemLevel, damage, UseSeconds(player, SkillSlot::ForSpell(spell)) });
		}
		return rows;
	}
};

/** @brief How much longer than its free use one use of the skill takes, from its skill_speed.tsv row (the tool's rule). */
double TimeRatio(SkillSlot skill)
{
	constexpr double ReferenceAps = 1.55;
	constexpr double BowAps = DefaultBowAps / 100.0;
	constexpr double FreeCastSeconds = 0.9;
	constexpr double SpellBaselineSeconds = 0.645;
	const SkillSpeedRow *row = FindSkillSpeedRow(skill);
	if (row == nullptr)
		return 1.0;
	const double added = row->addedMs / 1000.0;
	if (row->attackMs > 0)
		return (row->attackMs / 1000.0 + added) * ReferenceAps;
	if (row->castMs > 0) {
		// Diablo's damage spells against the one spell baseline (a Shortsword swing); the rest against the staff bolt.
		const bool damageSpell = skill.kind == SkillKind::Spell
		    && IsAnyOf(skill.spell(), SpellID::Firebolt, SpellID::HolyBolt, SpellID::Lightning, SpellID::ChargedBolt, SpellID::Flash, SpellID::Fireball,
		        SpellID::Elemental, SpellID::ChainLightning, SpellID::FlameWave, SpellID::Immolation, SpellID::Nova, SpellID::Apocalypse, SpellID::BloodStar);
		return (row->castMs / 1000.0 + added) / (damageSpell ? SpellBaselineSeconds : FreeCastSeconds);
	}
	// Bow skills are weighed against a Shortsword's attacks a second, not the bow's own (Curtis 2026-10-27).
	if (skill.kind == SkillKind::Attack && (SkillTags(skill) & ST_Bow) != 0)
		return ((100.0 / row->attackPercent / BowAps) + added) * ReferenceAps;
	return 100.0 / row->attackPercent + (added * ReferenceAps);
}

TEST_F(Poe2PaceTest, SlowerSkillsHitHarderByTheirTime)
{
	const SkillSlot slow[] = {
		SkillSlot::ForAttack(WeaponSkill::GroundSlam),
		SkillSlot::ForAttack(WeaponSkill::Cleave),
		SkillSlot::ForAttack(WeaponSkill::WhirlingStaff),
		SkillSlot::ForAttack(WeaponSkill::LightningArrow),
		SkillSlot::ForAttack(WeaponSkill::RainOfArrows),
		SkillSlot::ForAttack(WeaponSkill::LeapSlam),
		SkillSlot::ForAttack(WeaponSkill::BlessedShield),
		SkillSlot::ForAttack(WeaponSkill::Meteor),
		SkillSlot::ForAttack(WeaponSkill::Barrage),
		SkillSlot::ForAttack(WeaponSkill::EscapeShot),
		SkillSlot::ForAttack(WeaponSkill::GuidedArrow),
		SkillSlot::ForSpell(SpellID::Firebolt),
		SkillSlot::ForSpell(SpellID::ChargedBolt),
		SkillSlot::ForSpell(SpellID::Fireball),
		SkillSlot::ForSpell(SpellID::Apocalypse),
		SkillSlot::ForSpell(SpellID::FireWall),
	};
	for (const SkillSlot skill : slow) {
		const double ratio = TimeRatio(skill);
		ASSERT_GT(ratio, 1.0) << SkillSpeedKey(skill);
		EXPECT_EQ(SkillSpeedDamagePercent(skill), static_cast<int>(std::ceil((100 * ratio) - 1e-6))) << SkillSpeedKey(skill);
		// The gem's curve and the speed's damage multiply.
		EXPECT_EQ(GemDamagePercent(skill, 1), SkillSpeedDamagePercent(skill)) << SkillSpeedKey(skill);
		EXPECT_EQ(GemDamagePercent(skill, 20), GemCurveDamagePercent(skill, 20) * SkillSpeedDamagePercent(skill) / 100) << SkillSpeedKey(skill);
	}
	// As quick as the free use or quicker, or no damage of its own: no more damage.
	for (const SkillSlot skill : { SkillSlot::ForAttack(WeaponSkill::Flurry), SkillSlot::ForAttack(WeaponSkill::FistsOfThunder), SkillSlot::ForAttack(WeaponSkill::Spark), SkillSlot::ForAttack(WeaponSkill::BattleCry), SkillSlot::ForSpell(SpellID::Healing), SkillSlot::ForSpell(SpellID::Teleport) }) {
		EXPECT_EQ(SkillSpeedDamagePercent(skill), 100) << SkillSpeedKey(skill);
	}
}

TEST_F(Poe2PaceTest, RoundTwoSpeedRules)
{
	// Re-time all spells: Diablo's damage spells are weighed against a 0.645 s Shortsword swing, not the old 0.40 s cast.
	EXPECT_EQ(SkillSpeedDamagePercent(SkillSlot::ForSpell(SpellID::Firebolt)), 140);
	EXPECT_EQ(SkillSpeedDamagePercent(SkillSlot::ForSpell(SpellID::Fireball)), 187);
	EXPECT_EQ(SkillSpeedDamagePercent(SkillSlot::ForSpell(SpellID::Apocalypse)), 218);
	// Runes, walls and the Elemental and Occult gems' casts stay on the 0.9 s staff bolt.
	EXPECT_EQ(SkillSpeedDamagePercent(SkillSlot::ForSpell(SpellID::RuneOfFire)), 112);
	EXPECT_EQ(SkillSpeedDamagePercent(SkillSlot::ForAttack(WeaponSkill::Meteor)), 223);
	// Bow skills +29%: weighed against 1.55 attacks a second; the free bow shot keeps 100.
	EXPECT_EQ(SkillSpeedDamagePercent(SkillSlot::ForAttack(WeaponSkill::Barrage)), 130);
	EXPECT_EQ(SkillSpeedDamagePercent(SkillSlot::ForAttack(WeaponSkill::RainOfArrows)), 162);
	EXPECT_EQ(SkillSpeedDamagePercent(SkillSlot::ForDefault(static_cast<uint8_t>(DefaultAttack::BowShot))), 100);
	// Half as much: the mana cost rises by half of what the speed adds to the damage, rounded up.
	for (const SkillSlot skill : { SkillSlot::ForAttack(WeaponSkill::Cleave), SkillSlot::ForAttack(WeaponSkill::Whirlwind), SkillSlot::ForAttack(WeaponSkill::Barrage),
	         SkillSlot::ForSpell(SpellID::Apocalypse), SkillSlot::ForAttack(WeaponSkill::Flurry), SkillSlot::ForSpell(SpellID::Healing) }) {
		const int damage = SkillSpeedDamagePercent(skill);
		EXPECT_EQ(SkillSpeedManaPercent(skill), 100 + ((damage - 100 + 1) / 2)) << SkillSpeedKey(skill);
	}
	EXPECT_EQ(SkillSpeedManaPercent(SkillSlot::ForAttack(WeaponSkill::Whirlwind)), 187);
	// Firebolt keeps the bolts' flat 4 mana (Curtis 2026-10-23) though it hits harder for its speed.
	EXPECT_EQ(SkillSpeedManaPercent(SkillSlot::ForSpell(SpellID::Firebolt)), 100);
	// Cleave: 3 mana x 117% = 3.51, to the nearest point.
	EXPECT_EQ(GemBaseManaCost(SkillSlot::ForAttack(WeaponSkill::Cleave), 1), 4);
	// Raise each strike: a lone monster takes a third of each Seven-Sided Strike strike (was a seventh); three or more
	// enemies about take whole strikes.
	EXPECT_EQ(SevenSidedStrikeSharePercent(1, 7), 33);
	EXPECT_EQ(SevenSidedStrikeSharePercent(2, 7), 66);
	EXPECT_EQ(SevenSidedStrikeSharePercent(3, 7), 100);
	EXPECT_EQ(SevenSidedStrikeSharePercent(7, 7), 100);
	EXPECT_EQ(FindTableSkill(WeaponSkill::SevenSidedStrike)->damagePercent, 200);
	EXPECT_EQ(FindTableSkill(WeaponSkill::Zeal)->damagePercent, 140);
}

TEST_F(Poe2PaceTest, TwoWeaponsSwingInTurn)
{
	// POE2's dual wielding (Curtis 2026-10-27): each swing uses one weapon's damage, left hand first, then the other's.
	Player &player = *MyPlayer;
	MakeHero(player, HeroClass::Bard, 15);
	const _item_indexes sword = Bases("Sword", 15, true).back();
	const _item_indexes dagger = Bases("Dagger", 15).front();
	Hold(player, INVLOC_HAND_LEFT, sword);
	Hold(player, INVLOC_HAND_RIGHT, dagger);
	// The sword's local increase counts on its own swings only.
	ApplyModEffect(player.InvBody[INVLOC_HAND_LEFT], ModEffect::PhysicalPercent, 50, 0);
	Ready(player);
	ASSERT_TRUE(DualWields(player));
	const Item &left = player.InvBody[INVLOC_HAND_LEFT];
	const Item &right = player.InvBody[INVLOC_HAND_RIGHT];
	const int increased = player._pIBonusDam;
	ASSERT_EQ(increased, WeaponDamagePercent(left) + WeaponDamagePercent(right));

	player.poe2.swingHand = 0;
	const auto [leftMin, leftMax] = AttackWeaponDamage(player, DefaultSkillSlot(player));
	// What a hit then makes of it (base + base x _pIBonusDam) is the sword's own range with its own increase.
	EXPECT_NEAR(leftMax * (100.0 + increased) / 100, left._iMaxDam * (100.0 + WeaponDamagePercent(left)) / 100, 1.0);
	EndSwing(player);
	EXPECT_EQ(player.poe2.swingHand, 1);
	const auto [rightMin, rightMax] = AttackWeaponDamage(player, DefaultSkillSlot(player));
	EXPECT_NEAR(rightMax * (100.0 + increased) / 100, right._iMaxDam * (100.0 + WeaponDamagePercent(right)) / 100, 1.0);
	EXPECT_NE(leftMax, rightMax);
	EndSwing(player);
	EXPECT_EQ(player.poe2.swingHand, 0);
	// The panel (and _pIMinDam) shows a swing's average, not both weapons together.
	EXPECT_EQ(player._pIMinDam, (leftMin + rightMin) / 2);
	EXPECT_EQ(player._pIMaxDam, (leftMax + rightMax + 1) / 2);
	EXPECT_LT(player._pIMaxDam, left._iMaxDam + right._iMaxDam);

	// A held attack changes hands once a swing: 10 s of 1.55 swings a second.
	int changes = 0;
	player.poe2.swingHand = 0;
	player._pmode = PM_STAND;
	player.destAction = ACTION_NONE;
	const Point front = Front();
	for (int tick = 0; tick < 200; tick++) {
		if (player.destAction == ACTION_NONE && player.CanChangeAction()) {
			player.destAction = ACTION_ATTACK;
			player.destParam1 = front.x;
			player.destParam2 = front.y;
		}
		const uint8_t handBefore = player.poe2.swingHand;
		TestPlayerProcessAction(player);
		player.poe2.tick();
		Missiles.clear();
		changes += player.poe2.swingHand != handBefore ? 1 : 0;
	}
	const double seconds = HeldTicks(player, WeaponSkill::None, false) / TicksPerSecond;
	ASSERT_GT(seconds, 0);
	EXPECT_NEAR(changes, 10 / seconds, 2);

	// One weapon (or a weapon and a shield) swings as before; so does Diablo's Bard.
	MakeHero(player, HeroClass::Bard, 15);
	Hold(player, INVLOC_HAND_LEFT, sword);
	Ready(player);
	EXPECT_FALSE(DualWields(player));
	gbIsPoe2 = false;
	MakeHero(player, HeroClass::Bard, 15);
	Hold(player, INVLOC_HAND_LEFT, sword);
	Hold(player, INVLOC_HAND_RIGHT, dagger);
	Ready(player);
	EXPECT_FALSE(DualWields(player));
	EXPECT_EQ(player._pIMaxDam, player.InvBody[INVLOC_HAND_LEFT]._iMaxDam + player.InvBody[INVLOC_HAND_RIGHT]._iMaxDam);
	gbIsPoe2 = true;
}

TEST_F(Poe2PaceTest, EarlySkillsBeatTheFreeAttackPerSecond)
{
	// Curtis 2026-10-27: "a skill that takes longer hits harder, so it beats the free attack per second". Each class with
	// its best white weapons, one use's damage to a lone white over the time a held use takes.
	Player &player = *MyPlayer;
	for (const PaceClass &paceClass : PaceClasses()) {
		for (const int level : { 1, 5, 15 }) {
			BestGear(player, paceClass, level);
			const std::vector<Row> rows = Measure(player, paceClass, level);
			ASSERT_GT(rows.size(), 1U);
			const double free = rows[0].damage / rows[0].seconds;
			ASSERT_GT(free, 0) << paceClass.name << ' ' << level;
			for (size_t i = 1; i < rows.size(); i++) {
				ASSERT_GT(rows[i].seconds, 0) << paceClass.name << ' ' << rows[i].what;
				EXPECT_GT(rows[i].damage / rows[i].seconds, free) << paceClass.name << " level " << level << ' ' << rows[i].what;
			}
		}
	}
}

/** @brief A modifier forced onto a staff for the caster pace case: a family's best tier at an item level, rolled high. */
struct ForcedMod {
	const ModFamily *family;
	std::array<int, 2> values;
};

/** @brief The game's value of a table value at a scale, as modifiers.cpp rounds it. */
int ScaledModValue(int value, int scale)
{
	if (scale == 100 || value == 0)
		return value;
	const int scaled = ((value * scale) + (value > 0 ? 50 : -50)) / 100;
	return scaled == 0 ? (value > 0 ? 1 : -1) : scaled;
}

/** @brief The enabled families of a slot that raise Firebolt's damage per second, each at its best tier for the item level. */
std::vector<ForcedMod> FireboltMods(ModSlot slot, int itemLevel)
{
	std::vector<ForcedMod> mods;
	for (const ModFamily &family : ModifierFamilies()) {
		if (!family.enabled || (family.slots & (1U << static_cast<uint32_t>(slot))) == 0)
			continue;
		if (!IsAnyOf(family.effect, ModEffect::SpellDamage, ModEffect::SpellDamageAndMana, ModEffect::FireSpellDamage, ModEffect::CastSpeed, ModEffect::SpellSkillLevels,
		        ModEffect::FireSkillLevels, ModEffect::Intelligence))
			continue;
		for (const ModTier &tier : family.tiers) {
			if (tier.itemLevel > itemLevel || tier.weight == 0)
				continue;
			mods.push_back({ &family, { ScaledModValue(std::max(tier.min[0], tier.max[0]), family.scale[0]), ScaledModValue(std::max(tier.min[1], tier.max[1]), family.scale[1]) } });
			break;
		}
	}
	return mods;
}

/** @brief Whether a rare can carry these together: three prefixes and three suffixes at most, one of each group, no conflicts. */
bool RareCanCarry(const std::vector<const ForcedMod *> &mods)
{
	int prefixes = 0;
	int suffixes = 0;
	for (size_t i = 0; i < mods.size(); i++) {
		(mods[i]->family->prefix ? prefixes : suffixes)++;
		for (size_t j = 0; j < i; j++) {
			if (mods[i]->family->group == mods[j]->family->group)
				return false;
		}
	}
	return prefixes <= MaxRareModsPerSide && suffixes <= MaxRareModsPerSide;
}

TEST_F(Poe2PaceTest, RareCasterStaffAtDepthStaysNearTodaysBest)
{
	// caster-weapons-plan-v2.md 5.2 and 9: a rare staff of the rework at the depth's item level, with the best legal
	// modifiers for Firebolt, against the best staff of today (the Focused and Quick staves, pinned by row) with the best of
	// theirs. The Sorcerer's Firebolt per second may grow by at most 60%.
	constexpr int FirstLegacyStaff = 544;
	constexpr int LastLegacyStaff = 559;
	Player &player = *MyPlayer;
	std::map<int, double> secondsBySpeed;
	const auto dps = [&](Item staff, const std::vector<const ForcedMod *> &mods, int level, int magic) {
		for (const ForcedMod *mod : mods)
			ApplyModEffect(staff, mod->family->effect, mod->values[0], mod->values[1]);
		MakeHero(player, HeroClass::Sorcerer, level);
		player._pBaseMag = player._pMagic = magic;
		player.InvBody[INVLOC_HAND_LEFT] = staff;
		Ready(player);
		player._pSplLvl[static_cast<size_t>(SpellID::Firebolt)] = 1;
		const double hit = CastDamage(player, SpellID::Firebolt, 1);
		// The gear's fire spell damage adds to the spell's increased damage as the hit lands (UniqueSpellHitPercent).
		const int increased = std::max(UniqueSpellDamagePercent(player, SpellID::Firebolt), 1);
		const double fire = hit * (increased + GearElementSpellDamage(player, DamageType::Fire)) / increased;
		const int speed = player.poe2.itemStats[static_cast<size_t>(Poe2ItemStat::CastSpeed)];
		if (!secondsBySpeed.contains(speed))
			secondsBySpeed[speed] = HeldCastTicks(player, SpellID::Firebolt) / TicksPerSecond;
		return fire / secondsBySpeed[speed];
	};
	const auto best = [&](const Item &staff, ModSlot slot, int level, int magic) {
		const std::vector<ForcedMod> pool = FireboltMods(slot, level);
		double top = 0;
		for (uint32_t mask = 0; mask < (1U << pool.size()); mask++) {
			std::vector<const ForcedMod *> mods;
			for (size_t i = 0; i < pool.size(); i++) {
				if ((mask & (1U << i)) != 0)
					mods.push_back(&pool[i]);
			}
			if (RareCanCarry(mods))
				top = std::max(top, dps(staff, mods, level, magic));
		}
		return top;
	};

	for (const int level : { 9, 17, 33, 48 }) {
		// The same Magic for both, enough for any of today's staves at this depth.
		int magic = GetClassAttributes(HeroClass::Sorcerer).baseMag;
		for (int row = FirstLegacyStaff; row <= LastLegacyStaff; row++) {
			ASSERT_EQ(WeaponKindOf(row), WeaponKind::LegacyStaff) << AllItemsList[row].iName;
			if (AllItemsList[row].iMinMLvl <= level)
				magic = std::max<int>(magic, AllItemsList[row].iMinMag);
		}
		double today = 0;
		for (int row = FirstLegacyStaff; row <= LastLegacyStaff; row++) {
			if (AllItemsList[row].iMinMLvl > level)
				continue;
			MakeHero(player, HeroClass::Sorcerer, level);
			Hold(player, INVLOC_HAND_LEFT, static_cast<_item_indexes>(row));
			const Item staff = player.InvBody[INVLOC_HAND_LEFT];
			today = std::max(today, best(staff, ModSlot::LegacyStaff, level, magic));
		}
		// The rework's staves have no implicit and no damage: only their modifiers count. The Ashen Staff, pinned by
		// its id: its fire lock leaves Firebolt every modifier it wants.
		const std::optional<_item_indexes> ashen = FindPoe2ItemId("poe2:staff_ashen_staff");
		ASSERT_TRUE(ashen.has_value());
		ASSERT_EQ(WeaponKindOf(*ashen), WeaponKind::CasterStaff);
		MakeHero(player, HeroClass::Sorcerer, level);
		Hold(player, INVLOC_HAND_LEFT, *ashen);
		Item newStaff = player.InvBody[INVLOC_HAND_LEFT];
		ASSERT_EQ(newStaff._iMaxDam, 0);
		ASSERT_TRUE(ItemImplicits(newStaff).empty());
		newStaff._iIdentified = true;
		newStaff._iMagical = ITEM_QUALITY_RARE;
		const double rework = best(newStaff, ModSlot::Staff, level, magic);
		ASSERT_GT(today, 0) << level;
		const double ratio = rework / today;
		std::printf("[pace] item level %d: rare caster staff %.2f, today's best %.2f Firebolt/s damage: %.3fx\n", level, rework, today, ratio);
		RecordProperty("CasterStaffPace" + std::to_string(level), std::to_string(ratio));
		// Curtis 2026-10-30: a staff's "+N to level of all fire spell skills" rolls POE2's full numbers (no longer half),
		// which lifts the deep rows to about 1.53x and 1.57x; the ceiling allows that, and not much more.
		EXPECT_LE(ratio, 1.6) << "item level " << level;
		EXPECT_GE(ratio, 1.0) << "item level " << level << ": the new staff shouldn't lose to today's";
	}
}

/** @brief An item row by its name. */
_item_indexes RowNamed(std::string_view name)
{
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		if (AllItemsList[i].iName == name)
			return static_cast<_item_indexes>(i);
	}
	ADD_FAILURE() << name;
	return IDI_NONE;
}

/** @brief Act 1's floors as area_levels.tsv has them: Cathedral 1-4, then the Catacombs. */
struct PaceFloor {
	std::string_view name;
	int areaLevel;
};
constexpr PaceFloor ActOneFloors[] = {
	{ "Cathedral 1", 2 },
	{ "Cathedral 2", 4 },
	{ "Cathedral 3", 5 },
	{ "Cathedral 4", 7 },
	{ "Catacombs 1", 8 },
	{ "Catacombs 2", 10 },
	{ "Catacombs 3", 11 },
	{ "Catacombs 4", 13 },
};

TEST_F(Poe2PaceTest, CasterWeaponsPaceThroughActOne)
{
	// Plan S24: seconds to kill a white monster of the area's level on every floor of Act 1, the hero at the area's level
	// with gems at the most that level allows: the Sorcerer with a rare staff of today (the Focused and Quick rows, pinned)
	// and of the rework, a wand beside a sceptre, and the Monk with the new Fine Long Quarterstaff against the best
	// quarterstaff of before. Rare weapons are the average of ten rolls at the area's item level. One target, before its
	// defences; riders (Wither, bleeds) and minions are not counted.
	constexpr uint32_t Rolls = 10;
	constexpr int FirstLegacyStaff = 544;
	constexpr int LastLegacyStaff = 559;
	Player &player = *MyPlayer;

	const auto holdRare = [&](inv_body_loc hand, _item_indexes base, uint32_t seed, int itemLevel) {
		Item &item = player.InvBody[hand];
		item = {};
		SetLootRarity(item, ITEM_QUALITY_RARE);
		SetPoe2ItemLevel(item, itemLevel);
		SetRndSeed(seed * 7919U + static_cast<uint32_t>(itemLevel));
		SetupAllItems(player, item, base, AdvanceRndSeed(), 30, 1, /*onlygood=*/true, /*pregen=*/false, 0, /*forceNotUnique=*/true);
		item._iIdentified = true;
		item._iStatFlag = true;
		player._pBaseMag = player._pMagic = std::max<int>(player._pBaseMag, item._iMinMag);
		player._pBaseStr = player._pStrength = std::max<int>(player._pBaseStr, item._iMinStr);
		player._pBaseDex = player._pDexterity = std::max<int>(player._pBaseDex, item._iMinDex);
	};
	// A spell hit with the gear's element damage, which joins the spell's increased damage as it lands.
	const auto withElement = [&](double hit, DamageType type) {
		const int increased = std::max(UniqueSpellDamagePercent(player, SpellID::Firebolt), 1);
		return hit * (increased + GearElementSpellDamage(player, type)) / increased;
	};
	const auto fireboltPerSecond = [&](int gemLevel) {
		player._pSplLvl[static_cast<size_t>(SpellID::Firebolt)] = 2;
		const double hit = withElement(CastDamage(player, SpellID::Firebolt, gemLevel), DamageType::Fire);
		const double seconds = HeldCastTicks(player, SpellID::Firebolt) / TicksPerSecond;
		return seconds > 0 ? hit / seconds : 0;
	};
	// The left click: a granted skill or the granted Staff Bolt, at the weapon's grant level.
	const auto leftClickPerSecond = [&]() {
		const SkillSlot slot = DefaultSkillSlot(player);
		double hit = 0;
		DamageType type = DamageType::Fire;
		if (slot.kind == SkillKind::Attack) {
			hit = SkillHitAverage(player, slot, player.poe2.effectiveLevel(slot));
			type = FindTableSkill(static_cast<WeaponSkill>(slot.id))->type;
		} else {
			hit = SkillHitAverage(player, slot, 1);
		}
		const double seconds = UseSeconds(player, slot);
		return seconds > 0 ? withElement(hit, type) / seconds : 0;
	};
	const auto seconds = [](int life, double perSecond) { return perSecond > 0 ? life / perSecond : 999.0; };

	std::printf("[pace] floor (area level), white monster life: seconds to kill\n");
	for (const PaceFloor &floor : ActOneFloors) {
		const int level = floor.areaLevel;
		const int life = MonsterLevelLife(level);
		const int gemLevel = MaxGemLevelForCharacter(level);

		// Today: the best rare Focused or Quick staff of the depth, Firebolt, and its Staff Bolt.
		double today = 0;
		double todayBolt = 0;
		std::string todayStaff;
		for (int row = FirstLegacyStaff; row <= LastLegacyStaff; row++) {
			if (AllItemsList[row].iMinMLvl > level)
				continue;
			double sum = 0;
			double bolt = 0;
			for (uint32_t seed = 1; seed <= Rolls; seed++) {
				MakeHero(player, HeroClass::Sorcerer, level);
				holdRare(INVLOC_HAND_LEFT, static_cast<_item_indexes>(row), seed, level);
				Ready(player);
				sum += fireboltPerSecond(gemLevel);
				bolt += leftClickPerSecond();
			}
			if (sum / Rolls > today) {
				today = sum / Rolls;
				todayBolt = bolt / Rolls;
				todayStaff = AllItemsList[row].iName;
			}
		}

		// The rework's staves: Firebolt with the staff, and its own left click.
		const auto staffRow = [&](std::string_view name) {
			double firebolt = 0;
			double click = 0;
			for (uint32_t seed = 1; seed <= Rolls; seed++) {
				MakeHero(player, HeroClass::Sorcerer, level);
				holdRare(INVLOC_HAND_LEFT, RowNamed(name), seed, level);
				Ready(player);
				firebolt += fireboltPerSecond(gemLevel);
				click += leftClickPerSecond();
			}
			return std::pair { firebolt / Rolls, click / Rolls };
		};
		const auto [ashenFirebolt, ashenBolt] = staffRow("Ashen Staff");
		const auto [voltaicFirebolt, voltaicBolt] = staffRow("Voltaic Staff");

		// A wand beside a sceptre: the wand's bolt and Firebolt.
		const std::string_view sceptre = level >= 12 ? "Lupine Sceptre" : (level >= 6 ? "Fine Stoic Sceptre" : "Fine Rattling Sceptre");
		double wandFirebolt = 0;
		double boneBolt = 0;
		for (uint32_t seed = 1; seed <= Rolls; seed++) {
			MakeHero(player, HeroClass::Sorcerer, level);
			holdRare(INVLOC_HAND_LEFT, RowNamed("Bone Wand"), seed, level);
			holdRare(INVLOC_HAND_RIGHT, RowNamed(sceptre), seed + 100, level);
			Ready(player);
			EXPECT_EQ(GrantedAttackSkill(player), WeaponSkill::BoneBolt);
			wandFirebolt += fireboltPerSecond(gemLevel);
			boneBolt += leftClickPerSecond();
		}
		wandFirebolt /= Rolls;
		boneBolt /= Rolls;

		// The Monk: the best white quarterstaff of before against the new Fine Long Quarterstaff.
		const PaceClass &monk = PaceClasses()[3];
		ASSERT_EQ(monk.heroClass, HeroClass::Monk);
		const auto monkRows = [&](_item_indexes base) {
			MakeHero(player, HeroClass::Monk, level);
			Hold(player, INVLOC_HAND_LEFT, base);
			Ready(player);
			return Measure(player, monk, level);
		};
		const auto freePerSecond = [](const std::vector<Row> &rows) { return rows[0].seconds > 0 ? rows[0].damage / rows[0].seconds : 0; };
		_item_indexes bestOld = IDI_NONE;
		double bestOldFree = 0;
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (WeaponKindOf(static_cast<int>(i)) != WeaponKind::Quarterstaff || IsCasterWeapon(static_cast<int>(i)) || AllItemsList[i].dropRate == 0
			    || AllItemsList[i].iMinMLvl > level || AllItemsList[i].iName == "Fine Long Quarterstaff")
				continue;
			const double free = freePerSecond(monkRows(static_cast<_item_indexes>(i)));
			if (free > bestOldFree) {
				bestOldFree = free;
				bestOld = static_cast<_item_indexes>(i);
			}
		}
		ASSERT_NE(bestOld, IDI_NONE) << floor.name;
		const std::vector<Row> oldMonk = monkRows(bestOld);
		const std::vector<Row> newMonk = monkRows(RowNamed("Fine Long Quarterstaff"));
		ASSERT_EQ(oldMonk.size(), 3U);
		ASSERT_EQ(newMonk.size(), 3U);

		std::printf("[pace] %s (%d), life %d, gem level %d:\n", std::string(floor.name).c_str(), level, life, gemLevel);
		std::printf("[pace]   Sorcerer, rare %s (today): Firebolt %.1f s, Staff Bolt %.1f s\n", todayStaff.c_str(), seconds(life, today), seconds(life, todayBolt));
		std::printf("[pace]   Sorcerer, rare Ashen Staff: Firebolt %.1f s, Staff Bolt %.1f s\n", seconds(life, ashenFirebolt), seconds(life, ashenBolt));
		std::printf("[pace]   Sorcerer, rare Voltaic Staff: Firebolt %.1f s, Lightning Bolt %.1f s\n", seconds(life, voltaicFirebolt), seconds(life, voltaicBolt));
		std::printf("[pace]   Sorcerer, rare Bone Wand + %s: Firebolt %.1f s, Bone Bolt %.1f s\n", std::string(sceptre).c_str(), seconds(life, wandFirebolt), seconds(life, boneBolt));
		for (size_t r = 0; r < 3; r++) {
			std::printf("[pace]   Monk %s: %s %.1f s, Fine Long Quarterstaff %.1f s\n", oldMonk[r].what.c_str(), AllItemsList[bestOld].iName.c_str(),
			    seconds(life, oldMonk[r].damage / oldMonk[r].seconds), seconds(life, newMonk[r].damage / newMonk[r].seconds));
		}
		RecordProperty("ActOnePace" + std::to_string(level), std::to_string(seconds(life, ashenFirebolt) / seconds(life, today)));

		// Every scenario kills; the new staff's Firebolt stays within the S7 budget of today's (at most half again).
		EXPECT_GT(today, 0) << floor.name;
		EXPECT_GT(ashenBolt, 0) << floor.name;
		EXPECT_GT(voltaicBolt, 0) << floor.name;
		EXPECT_GT(boneBolt, 0) << floor.name;
		EXPECT_LE(ashenFirebolt, today * 1.5) << floor.name;
		// The free left click never outpaces the Firebolt gem on the same staff.
		EXPECT_LE(ashenBolt, ashenFirebolt * 1.01) << floor.name;
		// Every staff is worth holding (the coordinator, 10-28): a Voltaic Staff's Lightning Bolt, priced at 75% of Staff
		// Bolt on one target, kills a lone monster in at most twice an Ashen Staff's time (it took four times before).
		EXPECT_GE(voltaicBolt, ashenBolt / 2) << floor.name;
		EXPECT_GE(freePerSecond(newMonk), freePerSecond(oldMonk)) << floor.name << ": the new quarterstaff replaces the old on the first drop";
	}
}

TEST_F(Poe2PaceTest, PrintPace)
{
	const char *path = std::getenv("POE2_PACE_OUT");
	if (path == nullptr)
		GTEST_SKIP() << "set POE2_PACE_OUT to a file to write the pace table";
	FILE *out = std::fopen(path, "w");
	ASSERT_NE(out, nullptr);
	std::fprintf(out, "class\tlevel\tgear\tscenario\twhat\tgem\tdamage\tseconds\tlife\n");
	Player &player = *MyPlayer;
	for (const PaceClass &paceClass : PaceClasses()) {
		for (const int level : PaceLevels) {
			const std::string gear = BestGear(player, paceClass, level);
			for (const bool found : { false, true }) {
				// The Sorcerer's staff rolls spell modifiers, not physical ones: only its white staff is measured.
				if (found && (level < FirstFoundLevel || paceClass.spell != SpellID::Null))
					continue;
				if (found)
					Find(player, level);
				std::vector<Row> rows = Measure(player, paceClass, level);
				for (Row &extra : MeasureExtras(player, paceClass, level))
					rows.push_back(std::move(extra));
				for (const Row &row : rows) {
					std::fprintf(out, "%s\t%d\t%s\t%s\t%s\t%d\t%.4f\t%.4f\t%d\n", std::string(paceClass.name).c_str(), level, gear.c_str(), found ? "found" : "white",
					    row.what.c_str(), row.gemLevel, row.damage, row.seconds, MonsterLevelLife(level));
				}
			}
		}
	}
	std::fclose(out);
}

} // namespace
} // namespace devilution
