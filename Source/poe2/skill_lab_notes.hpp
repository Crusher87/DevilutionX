/**
 * @file poe2/skill_lab_notes.hpp
 *
 * POE2 Mashup Skill Lab notes: a status per skill (not looked at, needs work, approved), notes for Claude, and
 * "mark this moment", which pins a note to the exact tick of the cast. Everything goes to
 * poe2_logs/skill_lab_notes.tsv, one line per change, read by tools/skill_lab_notes.py.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace devilution {

enum class LabStatus : uint8_t {
	Unseen,
	NeedsWork,
	Approved,
};

/** @brief A moment in a cast that a note is pinned to. */
struct LabMoment {
	/** Game ticks since the cast began (20 a second at normal speed, whatever the lab's speed). */
	int tick;
	/** The hero's animation frame then. */
	int heroFrame;
};

/** @brief Reads the statuses back from the notes file, the latest line for each skill winning. */
void LabLoadNotes();

LabStatus LabSkillStatus(std::string_view skill);
/** @brief Sets the selected skill's status and writes it down. */
void LabSetStatus(LabStatus status);
/** @brief How many notes the skill has. */
int LabNoteCount(std::string_view skill);

/** @brief Pins the next note to this moment of the cast and pauses. False when no cast has started yet. */
bool LabMarkMoment();
std::optional<LabMoment> LabHeldMoment();
void LabDropMoment();
/** @brief Writes a note for the selected skill, pinned to the held moment if there is one. */
bool LabSaveNote(std::string_view text);

} // namespace devilution
