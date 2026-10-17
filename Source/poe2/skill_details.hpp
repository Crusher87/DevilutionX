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

/** @brief The tags of a skill, like "Spell, Projectile, Fire". */
std::string SkillTagText(SkillSlot skill);

} // namespace devilution
