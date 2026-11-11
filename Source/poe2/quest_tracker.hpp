/**
 * @file poe2/quest_tracker.hpp
 *
 * POE2 Mashup: the quest tracker under the corner map (Curtis 2026-10-28, question 18). Each quest in the quest log
 * that is under way gets one short line with its next step, such as "The Butcher: kill him on level 2". Small, a few
 * lines at most, and only while the corner map shows. The short names and the steps come from
 * txtdata/poe2/quest_tracker.tsv.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <function_ref.hpp>

#include "engine/surface.hpp"
#include "tables/questdat.hpp"

namespace devilution {

/** @brief At most this many quests at once. */
constexpr int QuestTrackerMaxLines = 4;
/** @brief Line height (GameFont12) and gap under the corner map, in UI pixels. */
constexpr int QuestTrackerLineHeight = 12;
constexpr int QuestTrackerGap = 4;

/** @brief One row of the table: the step shown for a quest in a given state. */
struct QuestTrackerRow {
	quest_id quest = Q_INVALID;
	/** @brief Whether the row is for a quest that is done but still has something to hand in. */
	bool done = false;
	/** @brief The quest's stage (its first quest variable) the row is for; none matches any stage. */
	std::optional<uint8_t> stage;
	std::string title;
	/** @brief "{level}" is the quest's floor, as "level 2", "Nest 1" or "Crypt 1". */
	std::string step;
};

/** @brief One tracked quest: its short name and its next step. */
struct QuestTrackerLine {
	quest_id quest;
	std::string title;
	std::string step;
};

/** @brief The rows, from txtdata/poe2/quest_tracker.tsv. */
void LoadQuestTracker();
const std::vector<QuestTrackerRow> &GetQuestTrackerRows();

/** @brief A floor's name in a step: "level 2", "Nest 1", "Crypt 1", or "town". */
std::string QuestFloorName(int level);
/** @brief The row for a quest as it stands now, if the tracker shows it. Only quests in the quest log are shown. */
std::optional<QuestTrackerLine> TrackQuest(const Quest &quest);
/** @brief The tracked quests: those on this floor first, then in the quest log's order, at most maxLines. */
std::vector<QuestTrackerLine> TrackedQuests(int maxLines);
/**
 * @brief Cuts text to fit a width, ending it with "..." when cut.
 * @param measure The width of a piece of text in pixels.
 */
std::string FitTextToWidth(std::string_view text, int width, tl::function_ref<int(std::string_view)> measure);

/** @brief Whether the tracker shows now: Enable Quest Tracking on, with the corner map showing. */
bool QuestTrackerShown();
/** @brief Draws the tracker under the corner map, when it shows. */
void DrawPoe2QuestTracker(const Surface &out);

} // namespace devilution
