/**
 * @file poe2/skill_details.hpp
 *
 * POE2 Mashup skill details: what the Skills panel's details pane shows for a socketed skill, worked out for the
 * hero as they are now (gear, passives, supports), like POE2's skill details pop-out: damage by type, speed, cost,
 * area and projectiles, ailments, what each support does, and what the next gem level brings.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace devilution {

struct Player;
struct SkillSlot;

struct SkillDetailLine {
	enum class Kind : uint8_t {
		/** A section heading, like "Damage". */
		Heading,
		/** A label and its value. */
		Value,
		/** Plain text, like the description. */
		Text,
		/** Something that helps, like a support's effect. */
		Good,
		/** A small grey note. */
		Note,
	};
	Kind kind;
	std::string label;
	std::string value;
};

/** @brief The details of the skill in a Skills panel socket, top to bottom. */
std::vector<SkillDetailLine> SkillDetails(const Player &player, size_t socket);

/**
 * @brief Seconds one use of the skill takes while it's held, from the tick it starts to the tick the next one does: the
 * attack or cast time the game uses (poe2/attack_speed.hpp); Double Swing's holds both strikes.
 */
double UseSeconds(const Player &player, SkillSlot skill);

/** @brief The tags of a skill, like "Spell, Projectile, Fire". */
std::string SkillTagText(SkillSlot skill);

/** @brief One damage type of a hit: the least, the most and the average it deals, with any fraction the hit keeps. */
struct DamageEstimate {
	double min;
	double max;
	double average;
};

/**
 * @brief A melee weapon hit, as PlrHitMonst rolls it: a whole-point weapon roll, its increased and added damage in
 * whole points, then the skill's percents in 1/64 points, so a small hit keeps its fraction (85% of 2 is 1.69).
 */
DamageEstimate MeleeHitEstimate(int weaponMin, int weaponMax, int increasedPercent, int addedDamage, int percent, int secondPercent = 100);

/**
 * @brief An arrow, as ProcessArrow and MonsterMHit roll it: the skill's percent cuts the bow's range to whole points
 * first, then each roll gets the hero's increased and added damage in whole points.
 */
DamageEstimate ArrowHitEstimate(int weaponMin, int weaponMax, int increasedPercent, int addedDamage, int percent);

/** @brief A damage number as the details print it: to the nearest whole point. */
int ShownDamage(double damage);

} // namespace devilution
