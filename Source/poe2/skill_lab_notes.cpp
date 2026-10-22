#include "poe2/skill_lab_notes.hpp"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

#include <ankerl/unordered_dense.h>

#include "player.h"
#include "poe2/skill_lab.hpp"
#include "poe2/telemetry.hpp"
#include "tables/playerdat.hpp"
#include "utils/file_util.h"
#include "utils/paths.h"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

constexpr std::string_view FileName = "skill_lab_notes.tsv";
constexpr std::string_view Header = "date\tskill\tkind\tstatus\tnote\tmark tick\tmark seconds\thero frame\tclass\tweapon\tgem level\tfloor\ttargets\tspeed\n";

ankerl::unordered_dense::map<std::string, LabStatus> Statuses;
ankerl::unordered_dense::map<std::string, int> NoteCounts;
ankerl::unordered_dense::map<std::string, std::vector<int>> NoteTicks;
std::optional<LabMoment> HeldMoment;

std::string_view StatusName(LabStatus status)
{
	switch (status) {
	case LabStatus::NeedsWork:
		return "needs work";
	case LabStatus::Approved:
		return "approved";
	default:
		return "not looked at";
	}
}

std::optional<LabStatus> ParseStatus(std::string_view name)
{
	for (const LabStatus status : { LabStatus::Unseen, LabStatus::NeedsWork, LabStatus::Approved }) {
		if (name == StatusName(status))
			return status;
	}
	return std::nullopt;
}

std::string NotesPath()
{
	std::string folder = TelemetryFolder();
	if (folder.empty()) {
		folder = StrCat(paths::PrefPath(), "poe2_logs", DIRECTORY_SEPARATOR_STR);
		RecursivelyCreateDir(folder.c_str());
	}
	return StrCat(folder, FileName);
}

std::string Stamp()
{
	const std::time_t now = std::time(nullptr);
	std::tm local {};
#ifdef _WIN32
	localtime_s(&local, &now);
#else
	localtime_r(&now, &local);
#endif
	char stamp[32];
	std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);
	return stamp;
}

/** @brief Keeps a field on one line and in one column. */
std::string Clean(std::string_view text)
{
	std::string clean(text);
	for (char &c : clean) {
		if (c == '\t' || c == '\n' || c == '\r')
			c = ' ';
	}
	return clean;
}

std::string SelectedName()
{
	const int index = LabSelectedSkill();
	if (index < 0)
		return {};
	return LabSkills()[index].name;
}

void WriteLine(std::string_view skill, std::string_view kind, std::string_view note, const std::optional<LabMoment> &moment)
{
	const std::string path = NotesPath();
	const bool isNew = !FileExists(path);
	FILE *file = OpenFile(path.c_str(), "ab");
	if (file == nullptr)
		return;
	if (isNew)
		std::fputs(std::string(Header).c_str(), file);
	const std::string targets = LabTargetMode() == LabTargets::Floor ? std::string("floor") : StrCat(LabTargetCount(), " ", LabMonsterName());
	const std::string speed = LabSlowdown() == 1 ? std::string("1x") : StrCat("1/", LabSlowdown());
	std::string line = StrCat(Stamp(), "\t", Clean(skill), "\t", kind, "\t", StatusName(LabSkillStatus(skill)), "\t", Clean(note), "\t");
	if (moment) {
		const int hundredths = moment->tick * 5;
		StrAppend(line, moment->tick, "\t", hundredths / 100, ".", hundredths % 100 < 10 ? "0" : "", hundredths % 100, "\t", moment->heroFrame, "\t");
	} else {
		line += "\t\t\t";
	}
	StrAppend(line, GetPlayerDataForClass(LabHeroClass()).className, "\t", Clean(LabWeaponName()), "\t", LabGemLevel(), "\t", LabFloorName(LabCurrentFloor()), "\t", Clean(targets), "\t", speed, "\n");
	std::fputs(line.c_str(), file);
	std::fclose(file);
}

/** @brief Splits a line of the notes file into its columns. */
std::vector<std::string_view> Columns(std::string_view line)
{
	std::vector<std::string_view> columns;
	size_t start = 0;
	while (true) {
		const size_t end = line.find('\t', start);
		columns.push_back(line.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
		if (end == std::string_view::npos)
			break;
		start = end + 1;
	}
	return columns;
}

} // namespace

void LabLoadNotes()
{
	Statuses.clear();
	NoteCounts.clear();
	NoteTicks.clear();
	HeldMoment = std::nullopt;
	FILE *file = OpenFile(NotesPath().c_str(), "rb");
	if (file == nullptr)
		return;
	std::string text;
	char buffer[4096];
	size_t read;
	while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
		text.append(buffer, read);
	std::fclose(file);

	size_t start = text.find('\n'); // Skip the header.
	while (start != std::string::npos && start + 1 < text.size()) {
		const size_t end = text.find('\n', start + 1);
		std::string_view line = std::string_view(text).substr(start + 1, end == std::string::npos ? std::string::npos : end - start - 1);
		if (!line.empty() && line.back() == '\r')
			line.remove_suffix(1);
		const std::vector<std::string_view> columns = Columns(line);
		if (columns.size() >= 4) {
			const std::string skill(columns[1]);
			if (columns[2] == "status") {
				if (const std::optional<LabStatus> status = ParseStatus(columns[3]))
					Statuses[skill] = *status;
			} else if (columns[2] == "note") {
				NoteCounts[skill]++;
				if (columns.size() > 5 && !columns[5].empty())
					NoteTicks[skill].push_back(std::atoi(std::string(columns[5]).c_str()));
			}
		}
		start = end;
	}
}

LabStatus LabSkillStatus(std::string_view skill)
{
	const auto it = Statuses.find(std::string(skill));
	return it == Statuses.end() ? LabStatus::Unseen : it->second;
}

void LabSetStatus(LabStatus status)
{
	const std::string skill = SelectedName();
	if (skill.empty() || LabSkillStatus(skill) == status)
		return;
	Statuses[skill] = status;
	WriteLine(skill, "status", {}, std::nullopt);
}

int LabNoteCount(std::string_view skill)
{
	const auto it = NoteCounts.find(std::string(skill));
	return it == NoteCounts.end() ? 0 : it->second;
}

std::vector<int> LabNoteTicks(std::string_view skill)
{
	const auto it = NoteTicks.find(std::string(skill));
	return it == NoteTicks.end() ? std::vector<int> {} : it->second;
}

bool LabMarkMoment()
{
	const int tick = LabTicksSinceCast();
	if (tick < 0 || MyPlayer == nullptr)
		return false;
	HeldMoment = LabMoment { tick, MyPlayer->AnimInfo.currentFrame + 1 };
	LabSetPaused(true);
	return true;
}

std::optional<LabMoment> LabHeldMoment()
{
	return HeldMoment;
}

void LabDropMoment()
{
	HeldMoment = std::nullopt;
}

bool LabSaveNote(std::string_view text)
{
	const std::string skill = SelectedName();
	if (skill.empty() || text.find_first_not_of(' ') == std::string_view::npos)
		return false;
	WriteLine(skill, "note", text, HeldMoment);
	NoteCounts[skill]++;
	if (HeldMoment)
		NoteTicks[skill].push_back(HeldMoment->tick);
	HeldMoment = std::nullopt;
	return true;
}

} // namespace devilution
