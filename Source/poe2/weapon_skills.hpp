/**
 * @file poe2/weapon_skills.hpp
 *
 * POE2 Mashup weapon skills: each weapon type grants a skill, used with the weapon skill key.
 */
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "engine/displacement.hpp"
#include "engine/point.hpp"
#include "poe2/player_data.hpp"

namespace devilution {

struct Item;
struct Missile;
struct Player;
enum class player_graphic : uint8_t;
namespace vfx {
enum class Element : uint8_t;
enum class SwingStyle : uint8_t;
} // namespace vfx

/** @brief Where the arrows of Rain of Arrows come down, around the target. */
constexpr std::array<Displacement, 9> RainOfArrowsVolley = { { { 0, 0 }, { 2, 0 }, { -2, 0 }, { 0, 2 }, { 0, -2 }, { 2, 2 }, { -2, -2 }, { 2, -2 }, { -2, 2 } } };

/**
 * @brief Ground Slam (Seismic Slam) deals full damage to the target and this much to the rest of its cone (Curtis
 * 2026-10-28: 75 to 50; audit-damage-v2.md section 7, 1.7 to 1.3 times the budget).
 */
constexpr int GroundSlamConeDamagePercent = 50;
/** @brief Ground Slam's shockwave runs this many tiles out in a cone in front of the player. */
constexpr int GroundSlamReach = 3;
/**
 * @brief Whirling Staff (Sweeping Wind) hits the target in full and two rings around the player: the inner ring for
 * this percent, the outer for that (Curtis 2026-10-28: 75 and 50 to 55 and 25).
 */
constexpr int WhirlingStaffReach = 2;
constexpr int WhirlingStaffInnerDamagePercent = 55;
constexpr int WhirlingStaffOuterDamagePercent = 25;
/** @brief Leap Slam hits everything around where it lands for this percent of a weapon hit. */
constexpr int LeapSlamDamagePercent = 80;
/**
 * @brief Lightning Arrow's burst: true gives it Diablo's 2 x character level + Dexterity / 4 (plan step 14, Curtis
 * 2026-10-27: the only way the Rogue clears packs); false gives it LightningArrowBurstPercent of the arrow's hit.
 */
constexpr bool LightningArrowBurstFromLevel = true;
/** @brief The burst from the character level and Dexterity: this much a level, plus Dexterity over the divisor. */
constexpr int LightningArrowBurstPerLevel = 2;
constexpr int LightningArrowBurstDexterityDivisor = 4;
/** @brief Without LightningArrowBurstFromLevel: the burst hits the 3x3 tiles where it lands for this percent of the arrow's hit, on average. */
constexpr int LightningArrowBurstPercent = 60;
/**
 * @brief Each of Frost Nova's shards hits for this percent of the spell base; about two reach a monster (Curtis
 * 2026-10-28: 25 to 100, at 10 mana; audit-casters-v3.md).
 */
constexpr int FrostNovaShardDamagePercent = 100;
/** @brief Each of Ice Bolt's shards hits for this percent of the spell base (Curtis 2026-10-28: 100 to 160). */
constexpr int IceBoltShardDamagePercent = 160;
/** @brief Flurry's second strike, a backhand, hits this much harder than its first. */
constexpr int FlurryBackhandDamagePercent = 150;
/**
 * @brief Smite's and Frenzy's swing hits for this percent of a weapon hit, before the gem curve (Curtis 2026-10-28:
 * 100 to 300 and 200; they were far under the budget as single-target skills).
 */
constexpr int SmiteDamagePercent = 300;
constexpr int FrenzyDamagePercent = 200;
/** @brief Shield Bash hits this hard before the gem curve (Curtis 2026-10-28: 120 to 200). */
constexpr int ShieldBashDamagePercent = 200;

/** @brief A swing skill's own percent of a weapon hit (Smite, Frenzy), 100 for the others and a plain attack. */
[[nodiscard]] constexpr int SwingSkillDamagePercent(WeaponSkill skill)
{
	switch (skill) {
	case WeaponSkill::Smite:
		return SmiteDamagePercent;
	case WeaponSkill::Frenzy:
		return FrenzyDamagePercent;
	default:
		return 100;
	}
}
/** @brief The backhand starts from the end of the first swing, so it skips this many more frames of the animation. */
constexpr int8_t FlurryBackhandSkippedFrames = 4;
/** @brief Ticks between Barrage's arrows and between Ember Fusillade's embers. */
constexpr int BarrageShotTicks = 3;
constexpr int EmberShotTicks = 2;
/** @brief Barrage fires its arrows one after another at one target. */
constexpr int BarrageArrows = 5;
/** @brief Ember Fusillade's embers before its gem level adds more. */
constexpr int EmberFusilladeEmbers = 5;

/**
 * @brief Whether a swing skill's hit is lightning rather than physical: Smite, like POE2's (Curtis 2026-10-28, "Smite
 * lightning"). Only POE2 Mashup has weapon skills.
 */
[[nodiscard]] constexpr bool IsLightningAttackSkill(WeaponSkill skill)
{
	return skill == WeaponSkill::Smite;
}

/** @brief Ice Bolt hits the monsters right beside the one it strikes for this percent of the damage (Firebolt's splash is in spell_damage.tsv). */
constexpr int BoltSplashPercent = 35;

/** @brief Fireball travels at this percent of Diablo's speed and Firebolt at this percent, so the heavy orb and the quick bolt feel apart. */
constexpr int FireballSpeedPercent = 75;
constexpr int FireboltSpeedPercent = 140;
/**
 * @brief A player's Elemental is a slow homing blast, no longer a second Fireball (Curtis 2026-10-28, "Elemental
 * homes"): it flies at this percent of Diablo's speed and turns every ElementalHomingTicks game ticks towards the
 * monster it was cast at, or the nearest within ElementalHomingTiles of it.
 */
constexpr int ElementalSpeedPercent = 50;
constexpr int ElementalHomingTicks = 3;
constexpr int ElementalHomingTiles = 6;

/** @brief Sets a player spell's missile speed: Fireball and Elemental slower, Firebolt faster; other spells keep theirs. */
void SetSpellMissileSpeed(Missile &missile, SpellID spell);
/** @brief A player's Fireball exploded: throws back the enemies around it, with a ring of flame and a jolt. */
void OnFireballDetonated(Missile &missile, Point position);
/** @brief Whether a tile offset from the player is in Ground Slam's cone (about 70 degrees wide) facing the direction. */
bool InGroundSlamCone(Direction facing, Displacement offset);
/**
 * @brief In POE2 Mashup, how many hits a use of a multi-hit skill landed before it was timed per hit, or 0 for a skill
 * timed per use. A per-hit skill (Hellfire Darts, Rapid Fire, Zeal, Blade Fury, Seven-Sided Strike) lands one hit a
 * use, at POE2's per-hit time from its skill_speed.tsv row, and holding it repeats; one without a row keeps its whole
 * burst, so a skill whose per-hit time isn't known yet doesn't strike once at its per-use rate.
 */
int PerHitBurst(WeaponSkill skill);
/**
 * @brief A per-hit skill's damage for each hit, in percent of one hit of its old burst (100 for any other skill). The
 * extra projectiles a use fired (gem level, Blade Fury's supports) add their share, (burst + extra) / burst, and a hit
 * the shortest action (MinActionTicks) slows adds what it lost; so the damage per second stays the old burst's.
 */
int PerHitDamagePercent(const Player &player, WeaponSkill skill);
/** @brief The number of the per-hit skill's hit this use lands, counting on from the last (it wraps at a multiple of every burst). */
int TakePerHitIndex(Player &player);
/** @brief Starts a stream of shots, one every few ticks; the first fires now. */
void StartSkillVolley(Player &player, WeaponSkill skill, Point target, int shots);
/** @brief Fires the next shot of a stream when it's due; once per game tick. */
void ProcessSkillVolley(Player &player);

/** @brief The skill the given weapon grants, or WeaponSkill::None. */
WeaponSkill WeaponSkillForItem(const Item &item);
/** @brief The skill the player's equipped weapons grant. */
WeaponSkill WeaponSkillFor(const Player &player);
std::string_view WeaponSkillName(WeaponSkill skill);
std::string_view WeaponSkillDescription(WeaponSkill skill);
/** @brief Whether the skill is a melee attack (a strike or a slam), whose mana cost is a third less than listed. */
bool HasMeleeManaCut(WeaponSkill skill);
/** @brief The mana cost at gem level 1, before supports; in POE2 mode a melee attack costs two thirds of its listed cost, rounded, at least 1. */
int WeaponSkillManaCost(WeaponSkill skill);
/** @brief The mana cost with the skill's supports. */
int WeaponSkillManaCost(const Player &player, WeaponSkill skill, bool lifeShare = true);
/** @brief Whether the skill is fired with a ranged attack. */
bool IsRangedWeaponSkill(WeaponSkill skill);

/** @brief Whether the skill needs a certain weapon (or a shield); Battle Cry and the fire and lightning skills don't. */
bool WeaponSkillNeedsWeapon(WeaponSkill skill);

/** @brief Whether the weapon can be used for the skill, e.g. a mace for Ground Slam. */
bool ItemAllowsWeaponSkill(const Item &item, WeaponSkill skill);
/** @brief Whether either equipped weapon (or shield) can be used for the skill, or it needs none. */
bool WeaponAllowsSkill(const Player &player, WeaponSkill skill);
/** @brief The weapons a skill needs, e.g. "sword or axe". */
std::string_view WeaponSkillWeapons(WeaponSkill skill);

/**
 * @brief Queues a weapon skill and attacks towards the cursor.
 * @return true if the skill was queued.
 */
bool TryUseWeaponSkill(Player &player, WeaponSkill skill);
/**
 * @brief Casts the main caster weapon's granted skill at the cursor (caster-weapons-plan-v2.md 6.6): the left click and its
 * hold-to-repeat, the controller's attack and the skill bar's weapon attack. It needs no gem or weapon check, and costs
 * no mana unless supports sit on the default attack row (GrantedSupportedManaCost). Single player only. Sets
 * LastPlayerAction for the local hero.
 * @return true if the cast started.
 */
bool TryUseGrantedSkill(Player &player);
/**
 * @brief A granted skill's mana cost before supports, once any support sits on the default attack row: the supports'
 * cost multipliers then apply to it. A bare granted skill is free (skill-balance review of plan v2, change 2).
 */
constexpr int GrantedSupportedManaCost = 2;

/**
 * @brief Called when an attack starts: turns a queued skill into the active one and pays its mana.
 */
void ActivateQueuedWeaponSkill(Player &player, bool isRangedAttack);

/** @brief Extra animation frames skipped by an attack with the skill (WeaponSkill::None for a plain attack). */
int8_t WeaponSkillSkippedFrames(const Player &player, WeaponSkill skill);

/** @brief Whether the skill is used through a plain melee or ranged attack, not an animation of its own (see TryUseWeaponSkill). */
bool IsSwingWeaponSkill(WeaponSkill skill);

/**
 * @brief Bonus lightning damage of a Lightning Arrow, carried by the arrow missile: the most its burst adds. The burst
 * rolls half to all of it (ProcessElementalArrow). With LightningArrowBurstFromLevel it is 2 x character level +
 * Dexterity / 4; otherwise it averages LightningArrowBurstPercent of the arrow's average hit. The gem curve scales the
 * arrow and the burst alike.
 */
int LightningArrowDamage(const Player &player);

/** @brief Game ticks from a dash skill's landing to its hit: the swing's wind-up is cut to this (see BeginSkillAnimation). */
constexpr int8_t DashStrikeWindupTicks = 2;

/**
 * @brief Plays a skill's own animation (as PM_SPELL); the skill goes off on its action frame.
 * @param graphic Attack for weapon skills, else one of the spell casting animations.
 * @param quickStrike Cuts the wind-up to DashStrikeWindupTicks, for a dash skill striking as it lands.
 * @param dashTicks In POE2 Mashup, the game ticks the dash before it took: the animation takes the rest of the use
 * time (SkillUseTime, with its time for the dash's length). -1 when the skill didn't dash.
 */
void BeginSkillAnimation(Player &player, WeaponSkill skill, player_graphic graphic, Point target, bool quickStrike = false, int dashTicks = -1);
/** @brief A dash skill's dash is over: it lands with its strike (see BeginSkillAnimation). */
void LandDashSkill(Player &player);

/**
 * @brief Makes a cast skill go off, on the action frame of its animation.
 * @return true if the skill ended the animation early.
 */
bool ExecuteSkillCast(Player &player, WeaponSkill skill, Point target);

/** @brief Plays the visual effects and sounds of a skill hitting or going off. */
void PlaySkillEffects(const Player &player, WeaponSkill skill, Point target);

/** @brief Whether the hero holds a heavy weapon (an axe, mace or staff, and no sword): slower, thicker swings. */
bool HoldsHeavyWeapon(const Player &player);
/**
 * @brief How the hero's weapon moves on an attack, following the attack animation he plays: an axe or a mace comes down
 * from overhead, a fist or a shield punches straight out, a sword or a staff sweeps across from his right to his left.
 */
vfx::SwingStyle WeaponSwingStyle(const Player &player);
/** @brief The element the hero's weapon adds to its hits, for the faint flickers on its swing: fire, lightning or none. */
vfx::Element WeaponElementLook(const Player &player);

/** @brief Whether the hero spins round through every facing while the skill's animation plays (Whirlwind). */
bool SpinsWhileStriking(WeaponSkill skill);
/** @brief Turns a spinning hero to the next facing for the animation frame (see SpinsWhileStriking). */
void TurnWithSpin(Player &player);
/** @brief Whether the hero's attack spins him round through every facing as he swings (Whirling Staff). */
bool SpinsWhileSwinging(WeaponSkill skill);
/** @brief Starts a spinning attack's look as its swing begins: its funnel of wind and its whoosh (see SpinsWhileSwinging). */
void BeginSpinningSwing(const Player &player);
/** @brief Shows a spinning hero's next facing for the attack frame; only his sprite turns, so the strike lands where he faces. */
void TurnSpriteWithSpin(Player &player);

} // namespace devilution
