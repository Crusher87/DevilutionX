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

/** @brief Ground Slam and Whirling Staff deal full damage to the target and this much to the other tiles around you. */
constexpr int AreaSkillSideDamagePercent = 75;
/** @brief Ground Slam's shockwave runs this many tiles out in a cone in front of the player. */
constexpr int GroundSlamReach = 3;
/** @brief Whirling Staff hits two rings around the player; the outer ring for this percent. */
constexpr int WhirlingStaffReach = 2;
constexpr int WhirlingStaffOuterDamagePercent = 50;
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
/** @brief Each of Frost Nova's shards hits for this percent of the spell base; about two reach a monster. */
constexpr int FrostNovaShardDamagePercent = 25;
/** @brief Flurry's second strike, a backhand, hits this much harder than its first. */
constexpr int FlurryBackhandDamagePercent = 150;
/** @brief The backhand starts from the end of the first swing, so it skips this many more frames of the animation. */
constexpr int8_t FlurryBackhandSkippedFrames = 4;
/** @brief Ticks between Barrage's arrows and between Ember Fusillade's embers. */
constexpr int BarrageShotTicks = 3;
constexpr int EmberShotTicks = 2;
/** @brief Barrage fires its arrows one after another at one target. */
constexpr int BarrageArrows = 5;
/** @brief Ember Fusillade's embers before its gem level adds more. */
constexpr int EmberFusilladeEmbers = 5;

/** @brief Ice Bolt hits the monsters right beside the one it strikes for this percent of the damage (Firebolt's splash is in spell_damage.tsv). */
constexpr int BoltSplashPercent = 35;

/** @brief Fireball travels at this percent of Diablo's speed and Firebolt at this percent, so the heavy orb and the quick bolt feel apart. */
constexpr int FireballSpeedPercent = 75;
constexpr int FireboltSpeedPercent = 140;

/** @brief Sets a player spell's missile speed: Fireball slower, Firebolt faster; other spells keep theirs. */
void SetSpellMissileSpeed(Missile &missile, SpellID spell);
/** @brief A player's Fireball exploded: throws back the enemies around it, with a ring of flame and a jolt. */
void OnFireballDetonated(Missile &missile, Point position);
/** @brief Whether a tile offset from the player is in Ground Slam's cone (about 70 degrees wide) facing the direction. */
bool InGroundSlamCone(Direction facing, Displacement offset);
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
 * @brief Called when an attack starts: turns a queued skill into the active one and pays its mana.
 */
void ActivateQueuedWeaponSkill(Player &player, bool isRangedAttack);

/** @brief Extra animation frames skipped by the active skill's attack. */
int8_t WeaponSkillSkippedFrames(const Player &player);

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
 */
void BeginSkillAnimation(Player &player, WeaponSkill skill, player_graphic graphic, Point target, bool quickStrike = false);

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
