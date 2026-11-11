#include "poe2/quest_tracker.hpp"

#include <algorithm>

#include <magic_enum/magic_enum.hpp>

#include "data/file.hpp"
#include "data/record_reader.hpp"
#include "engine/render/primitive_render.hpp"
#include "engine/render/text_render.hpp"
#include "levels/gendung.h"
#include "options.h"
#include "poe2/corner_map.hpp"
#include "utils/language.h"
#include "utils/log.hpp"
#include "utils/parse_int.hpp"
#include "utils/str_cat.hpp"
#include "utils/ui_fwd.h"

namespace devilution {

namespace {

constexpr std::string_view QuestTrackerFile = "txtdata\\poe2\\quest_tracker.tsv";

std::vector<QuestTrackerRow> Rows;
bool RowsLoaded = false;

/** @brief Whether the hero is on the quest's floor or in its own level now. */
bool IsQuestHere(const Quest &quest)
{
	if (setlevel)
		return quest._qslvl != SL_NONE && setlvlnum == quest._qslvl;
	return quest._qlevel == currlevel;
}

int BookOrder(quest_id quest)
{
	const size_t index = static_cast<size_t>(quest);
	return index < QuestsData.size() ? QuestsData[index].questBookOrder : 0;
}

} // namespace

void LoadQuestTracker()
{
	Rows.clear();
	RowsLoaded = true;
	auto dataFile = DataFile::load(QuestTrackerFile);
	if (!dataFile.has_value()) {
		LogError("Couldn't load the quest tracker: {}", QuestTrackerFile);
		return;
	}
	dataFile->skipHeaderOrDie(QuestTrackerFile);
	for (DataFileRecord record : *dataFile) {
		RecordReader reader { record, QuestTrackerFile };
		std::string quest;
		std::string state;
		std::string stage;
		QuestTrackerRow row;
		reader.readString("quest", quest);
		reader.readString("state", state);
		reader.readString("stage", stage);
		reader.readString("title", row.title);
		reader.readString("step", row.step);
		const std::optional<quest_id> id = magic_enum::enum_cast<quest_id>(quest);
		if (!id || *id == Q_INVALID) {
			LogError("Unknown quest in {}: {}", QuestTrackerFile, quest);
			continue;
		}
		row.quest = *id;
		row.done = state == "done";
		if (!stage.empty()) {
			const ParseIntResult<uint8_t> parsed = ParseInt<uint8_t>(stage);
			if (!parsed.has_value()) {
				LogError("Bad quest stage in {}: {}", QuestTrackerFile, stage);
				continue;
			}
			row.stage = *parsed;
		}
		Rows.push_back(std::move(row));
	}
}

const std::vector<QuestTrackerRow> &GetQuestTrackerRows()
{
	if (!RowsLoaded)
		LoadQuestTracker();
	return Rows;
}

std::string QuestFloorName(int level)
{
	if (level <= 0)
		return std::string(_("town"));
	if (level > 20)
		return StrCat(_("Crypt"), " ", level - 20);
	if (level > 16)
		return StrCat(_("Nest"), " ", level - 16);
	return StrCat(_("level"), " ", level);
}

std::optional<QuestTrackerLine> TrackQuest(const Quest &quest)
{
	if (!quest._qlog)
		return std::nullopt;
	const bool done = quest._qactive == QUEST_DONE;
	if (!done && quest._qactive != QUEST_ACTIVE)
		return std::nullopt;
	for (const QuestTrackerRow &row : GetQuestTrackerRows()) {
		if (row.quest != quest._qidx || row.done != done)
			continue;
		if (row.stage && *row.stage != quest._qvar1)
			continue;
		QuestTrackerLine line { quest._qidx, row.title, row.step };
		if (line.title.empty() && static_cast<size_t>(quest._qidx) < QuestsData.size())
			line.title = _(QuestsData[quest._qidx]._qlstr.c_str());
		constexpr std::string_view Placeholder = "{level}";
		if (const size_t at = line.step.find(Placeholder); at != std::string::npos)
			line.step.replace(at, Placeholder.size(), QuestFloorName(quest._qlevel));
		return line;
	}
	// A quest under way with no row still shows, by name; a finished one only when a row says what is left.
	if (done || static_cast<size_t>(quest._qidx) >= QuestsData.size())
		return std::nullopt;
	return QuestTrackerLine { quest._qidx, std::string(_(QuestsData[quest._qidx]._qlstr.c_str())), {} };
}

std::vector<QuestTrackerLine> TrackedQuests(int maxLines)
{
	std::vector<const Quest *> quests;
	for (const Quest &quest : Quests) {
		if (quest._qidx != Q_INVALID && (quest._qactive == QUEST_ACTIVE || quest._qactive == QUEST_DONE) && quest._qlog)
			quests.push_back(&quest);
	}
	std::stable_sort(quests.begin(), quests.end(), [](const Quest *a, const Quest *b) {
		const bool aHere = IsQuestHere(*a);
		const bool bHere = IsQuestHere(*b);
		if (aHere != bHere)
			return aHere;
		return BookOrder(a->_qidx) < BookOrder(b->_qidx);
	});
	std::vector<QuestTrackerLine> lines;
	for (const Quest *quest : quests) {
		if (static_cast<int>(lines.size()) >= maxLines)
			break;
		if (std::optional<QuestTrackerLine> line = TrackQuest(*quest))
			lines.push_back(std::move(*line));
	}
	return lines;
}

std::string FitTextToWidth(std::string_view text, int width, tl::function_ref<int(std::string_view)> measure)
{
	if (measure(text) <= width)
		return std::string(text);
	constexpr std::string_view Ellipsis = "...";
	size_t length = text.size();
	while (length > 0) {
		length--;
		// Never cut a letter written in more than one byte.
		while (length > 0 && (static_cast<uint8_t>(text[length]) & 0xC0) == 0x80)
			length--;
		std::string_view kept = text.substr(0, length);
		while (!kept.empty() && kept.back() == ' ')
			kept.remove_suffix(1);
		std::string candidate = StrCat(kept, Ellipsis);
		if (measure(candidate) <= width)
			return candidate;
	}
	return measure(Ellipsis) <= width ? std::string(Ellipsis) : std::string();
}

bool QuestTrackerShown()
{
	return CornerMapShown() && *GetOptions().Gameplay.enableQuestTracking;
}

void DrawPoe2QuestTracker(const Surface &out)
{
	if (!QuestTrackerShown())
		return;
	const std::vector<QuestTrackerLine> lines = TrackedQuests(QuestTrackerMaxLines);
	if (lines.empty())
		return;
	const Rectangle map = CurrentCornerMapRect();
	// Up to a third of the screen wide, so a small map still leaves room for the steps. Lines are right-aligned and
	// only as long as their text.
	const int width = std::min(std::max(map.size.width, gnScreenWidth / 3), map.position.x + map.size.width);
	const int right = map.position.x + map.size.width;
	const auto measure = [](std::string_view text) { return GetLineWidth(text, GameFont12, 0); };
	int y = map.position.y + map.size.height + QuestTrackerGap;
	for (const QuestTrackerLine &line : lines) {
		if (y + QuestTrackerLineHeight > out.h())
			break;
		std::string title = line.step.empty() ? line.title : StrCat(line.title, ":");
		title = FitTextToWidth(title, width - 4, measure);
		const int titleWidth = measure(title);
		const int stepRoom = width - 4 - titleWidth - 4;
		const std::string step = line.step.empty() || stepRoom <= 0 ? std::string() : FitTextToWidth(line.step, stepRoom, measure);
		const int stepWidth = step.empty() ? 0 : measure(step) + 4;
		const int textWidth = titleWidth + stepWidth;
		const int left = right - textWidth - 2;
		// A faint backing keeps the line readable over bright floors.
		DrawHalfTransparentRectTo(out, left - 2, y, textWidth + 4, QuestTrackerLineHeight);
		const auto draw = [&](std::string_view text, int x, UiFlags color) {
			const int textWidthHere = measure(text);
			DrawString(out, text, { { x + 1, y + 1 }, { textWidthHere + 1, QuestTrackerLineHeight } }, { .flags = UiFlags::ColorBlack, .spacing = 0 });
			DrawString(out, text, { { x, y }, { textWidthHere + 1, QuestTrackerLineHeight } }, { .flags = color, .spacing = 0 });
		};
		draw(title, left, UiFlags::ColorWhitegold);
		if (!step.empty())
			draw(step, left + titleWidth + 4, UiFlags::ColorWhite);
		y += QuestTrackerLineHeight + 1;
	}
}

} // namespace devilution
