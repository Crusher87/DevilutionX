/**
 * @file poe2/skill_looks.hpp
 *
 * POE2 Mashup skill looks: how big, dense, long, bright, fast and what colour each skill's effects are, and how hard
 * they shake the screen. The numbers come from txtdata/poe2/skill_looks.tsv, then from the player's own edits in
 * poe2_logs/skill_looks_mine.tsv (made with the Skill Lab's sliders). While a skill's effects are made, a
 * SkillLookScope says whose they are, and the effects engine (poe2/vfx.cpp) shapes everything it adds by that look.
 */
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "engine/point.hpp"
#include "poe2/player_data.hpp"

namespace devilution {

struct SkillLook {
	/** Rings, sweeps, cracks, particles and how far they fly. */
	float size = 1;
	/** How many particles, bolts and the like. */
	float amount = 1;
	/** How long things linger. */
	float length = 1;
	float brightness = 1;
	/** How fast it plays: quicker and shorter at once. */
	float speed = 1;
	float shake = 1;
	/** How far the colours turn to `colour`, 0 to 1. */
	float tint = 0;
	uint8_t r = 255;
	uint8_t g = 255;
	uint8_t b = 255;
	/** An extra sound played when the skill goes off: an index into SkillLookSounds(), 0 for none. */
	uint8_t sound = 0;

	[[nodiscard]] bool isPlain() const;
	bool operator==(const SkillLook &other) const = default;
};

/** @brief A sound a look can add, by its name in the files. */
struct SkillLookSound {
	std::string_view name;
	/** A SfxID; -1 for none. */
	int16_t sfx;
};

/** @brief The sounds a look can add; the first is none. */
std::span<const SkillLookSound> SkillLookSounds();
/** @brief Plays the skill look's extra sound, if it has one, where the skill went off. */
void PlaySkillLookSound(SkillSlot skill, Point tile);

/** @brief The id a skill's look is stored under, e.g. "Fissure", "Spell Firebolt", "Spirit Blasphemy", "Default". */
std::string SkillLookKey(SkillSlot skill);

/** @brief Reads the shipped looks and the player's edits. */
void LoadSkillLooks();

/** @brief The look a skill's effects use now: the player's edit if there is one (and edits are on), else the file's. */
const SkillLook &GetSkillLook(std::string_view key);
/** @brief The look from the shipped file alone. */
const SkillLook &GetShippedSkillLook(std::string_view key);
/** @brief Whether the player has changed this skill's look. */
bool HasSkillLookEdit(std::string_view key);
/** @brief Changes the player's edit of a skill's look (not saved until SaveSkillLookEdits). */
void SetSkillLookEdit(std::string_view key, std::string_view name, const SkillLook &look);
void ClearSkillLookEdit(std::string_view key);
/** @brief Writes the player's edits to poe2_logs/skill_looks_mine.tsv. */
bool SaveSkillLookEdits();
/** @brief Whether edits show (the lab's A/B switch). */
bool SkillLookEditsOn();
void SetSkillLookEditsOn(bool on);

/** @brief The look the effects being made now belong to, or nullptr for none (monsters, the world). */
const SkillLook *CurrentSkillLook();

/** @brief While alive, effects made belong to this skill. Nests: the inner one wins until it ends. */
class SkillLookScope {
public:
	explicit SkillLookScope(SkillSlot skill);
	explicit SkillLookScope(WeaponSkill skill);
	/** @brief The skill that fired a missile (its poe2 skillKind and skillId). */
	SkillLookScope(uint8_t skillKind, uint8_t skillId);
	~SkillLookScope();

	SkillLookScope(const SkillLookScope &) = delete;
	SkillLookScope &operator=(const SkillLookScope &) = delete;

private:
	const SkillLook *previous_;
};

} // namespace devilution
