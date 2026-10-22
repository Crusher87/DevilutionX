#include "poe2/tree_telemetry.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <initializer_list>
#include <map>
#include <optional>
#include <vector>

#include <magic_enum/magic_enum.hpp>

#ifdef USE_SDL3
#include <SDL3/SDL_timer.h>
#else
#include <SDL.h>
#endif

#include "player.h"
#include "poe2/telemetry.hpp"
#include "utils/file_util.h"
#include "utils/format.hpp"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

std::string_view PoolName(PassivePool pool)
{
	switch (pool) {
	case PassivePool::SetI:
		return "set1";
	case PassivePool::SetII:
		return "set2";
	default:
		return "main";
	}
}

const PassiveNode *NodeOf(PassiveNodeId id)
{
	const std::vector<PassiveNode> &tree = PassiveTree();
	return id < tree.size() ? &tree[id] : nullptr;
}

/** @brief Text safe for one TSV cell. */
std::string Cell(std::string_view text)
{
	std::string cell(text);
	for (char &c : cell) {
		if (c == '\t' || c == '\n' || c == '\r')
			c = ' ';
	}
	return cell;
}

std::string_view RoadName(const PassiveNode &node)
{
	return node.road ? magic_enum::enum_name(*node.road) : std::string_view();
}

/** @brief Appends a line to a file in poe2_logs that is never pruned, writing its header first when the file is new. */
void AppendKeptLine(std::string_view file, std::string_view header, std::string_view line)
{
	if (!TelemetryEnabled() || TelemetryFolder().empty())
		return;
	const std::string path = StrCat(TelemetryFolder(), file);
	const bool isNew = !FileExists(path);
	FILE *out = OpenFile(path.c_str(), "ab");
	if (out == nullptr)
		return;
	if (isNew)
		std::fwrite(header.data(), 1, header.size(), out);
	std::fwrite(line.data(), 1, line.size(), out);
	std::fclose(out);
}

/** @brief Who and when, the first five cells of every kept line. */
std::string LineStart(const Player &player)
{
	return std::format("{:s}\t{:s}\t{:s}\t{:s}\t{:d}", TelemetryLocalStamp(), TelemetrySessionName(), Cell(player._pName), player.getClassName(), player.getCharacterLevel());
}

constexpr std::string_view HistoryHeader = "date\tsession\thero\tclass\thero level\taction\tpool\tnode\tname\tkind\tarea\tgroup\troad\troad step\tattribute\tpoints spent\tpoints total\tset I spent\tset II spent\ttree\n";

/** @param count For a clear, the nodes cleared, written in the node column. */
void WriteHistory(const Player &player, std::string_view action, const PassiveNode *node, PassivePool pool, int count, bool taken)
{
	std::string attribute;
	if (taken && node != nullptr && node->kind == PassiveKind::Attribute)
		attribute = PassiveAttributeName(ChosenPassiveAttributeIn(player, node->id, pool));
	const std::string line = std::format("{:s}\t{:s}\t{:s}\t{:s}\t{:s}\t{:s}\t{:s}\t{:s}\t{:s}\t{:s}\t{:s}\t{:d}\t{:d}\t{:d}\t{:d}\t{:08x}\n",
	    LineStart(player), action, PoolName(pool),
	    node != nullptr ? std::to_string(node->id) : std::to_string(count),
	    node != nullptr ? Cell(PassiveNodeName(*node)) : std::string(),
	    node != nullptr ? magic_enum::enum_name(node->kind) : std::string_view(),
	    node != nullptr ? Cell(node->region) : std::string(),
	    node != nullptr ? Cell(node->group) : std::string(),
	    node != nullptr ? RoadName(*node) : std::string_view(),
	    node != nullptr && node->road ? std::to_string(node->roadStep) : std::string(),
	    attribute,
	    PassivePointsSpent(player), PassivePointsTotal(player), WeaponSetPointsSpent(player, PassivePool::SetI), WeaponSetPointsSpent(player, PassivePool::SetII),
	    PassiveTreeHash());
	AppendKeptLine("tree_history.tsv", HistoryHeader, line);
}

/** @brief The node's stats as a JSON object, by stat name. */
std::string StatsJson(const Player &player, const PassiveNode &node)
{
	std::string json = "{";
	for (const auto &[stat, value] : PassiveNodeStats(player, node)) {
		if (json.size() > 1)
			json += ',';
		AppendJsonString(json, magic_enum::enum_name(stat));
		StrAppend(json, ":", value);
	}
	return json + '}';
}

void WriteNodeEvent(const Player &player, std::string_view kind, std::string_view verb, PassiveNodeId id, PassivePool pool)
{
	const bool taken = kind == "tree_take";
	const PassiveNode *node = NodeOf(id);
	if (node == nullptr || &player != MyPlayer || !TelemetryEnabled())
		return;
	const int spent = PassivePointsSpent(player);
	const int total = PassivePointsTotal(player);
	std::string where = node->region;
	if (node->road)
		where = StrCat(RoadName(*node), " road step ", node->roadStep);
	if (pool != PassivePool::Main)
		StrAppend(where, pool == PassivePool::SetI ? ", weapon set I" : ", weapon set II");
	TelemetryText("tree", std::format("{:s} {:s} {:s} ({:s}), {:d}/{:d} points", verb, magic_enum::enum_name(node->kind), PassiveNodeName(*node), where, spent, total));
	TelemetryEvent event = TelemetryEventNow(LogChannel::Tree, LogLevel::Always, kind);
	if (event) {
		event.add("p", player.getId())
		    .add("node", node->id)
		    .add("name", PassiveNodeName(*node))
		    .add("kind", magic_enum::enum_name(node->kind))
		    .add("area", node->region)
		    .add("group", node->group)
		    .add("pool", PoolName(pool))
		    .add("heroLevel", player.getCharacterLevel())
		    .add("spent", spent)
		    .add("total", total)
		    .add("set1Spent", WeaponSetPointsSpent(player, PassivePool::SetI))
		    .add("set2Spent", WeaponSetPointsSpent(player, PassivePool::SetII))
		    .addRaw("stats", StatsJson(player, *node));
		if (node->road)
			event.add("road", RoadName(*node)).add("roadStep", node->roadStep);
		// A refunded node's pick is already cleared.
		if (taken && node->kind == PassiveKind::Attribute)
			event.add("attribute", PassiveAttributeName(ChosenPassiveAttributeIn(player, node->id, pool)));
	}
	WriteHistory(player, verb, node, pool, 0, taken);
}

/** @brief Node ids held in a pool, lowest first. */
std::vector<PassiveNodeId> HeldIn(const Player &player, PassivePool pool)
{
	std::vector<PassiveNodeId> held;
	for (const PassiveNode &node : PassiveTree()) {
		if (IsPassiveInPool(player, node.id, pool))
			held.push_back(node.id);
	}
	return held;
}

std::string IdsText(const std::vector<PassiveNodeId> &ids)
{
	std::string text;
	for (const PassiveNodeId id : ids)
		StrAppend(text, text.empty() ? "" : " ", id);
	return text;
}

std::string IdsJson(const std::vector<PassiveNodeId> &ids)
{
	std::string json = "[";
	for (const PassiveNodeId id : ids)
		StrAppend(json, json.size() > 1 ? "," : "", id);
	return json + ']';
}

/** @brief The deepest step held on each Profane Road. */
std::array<int, static_cast<size_t>(ProfaneRoad::Count)> RoadDepths(const Player &player)
{
	std::array<int, static_cast<size_t>(ProfaneRoad::Count)> depths {};
	for (const PassiveNode &node : PassiveTree()) {
		if (node.road && IsPassiveInPool(player, node.id, PassivePool::Main))
			depths[static_cast<size_t>(*node.road)] = std::max<int>(depths[static_cast<size_t>(*node.road)], node.roadStep);
	}
	return depths;
}

/** @brief The whole of a small text file, or empty. */
std::string ReadKeptFile(std::string_view file)
{
	std::string text;
	if (TelemetryFolder().empty())
		return text;
	FILE *in = OpenFile(StrCat(TelemetryFolder(), file).c_str(), "rb");
	if (in == nullptr)
		return text;
	char chunk[4096];
	size_t read;
	while ((read = std::fread(chunk, 1, sizeof(chunk), in)) > 0)
		text.append(chunk, read);
	std::fclose(in);
	return text;
}

std::vector<std::string_view> SplitTabs(std::string_view line)
{
	std::vector<std::string_view> cells;
	size_t start = 0;
	while (true) {
		const size_t tab = line.find('\t', start);
		cells.push_back(line.substr(start, tab == std::string_view::npos ? std::string_view::npos : tab - start));
		if (tab == std::string_view::npos)
			return cells;
		start = tab + 1;
	}
}

/** @brief Calls back with each data line's cells, found by the header's column names. */
template <typename Callback>
void ForEachRow(std::string_view text, std::initializer_list<std::string_view> columns, Callback &&callback)
{
	std::vector<size_t> at;
	size_t start = 0;
	bool header = true;
	std::vector<std::string_view> picked(columns.size());
	while (start < text.size()) {
		size_t end = text.find('\n', start);
		if (end == std::string_view::npos)
			end = text.size();
		std::string_view line = text.substr(start, end - start);
		start = end + 1;
		if (!line.empty() && line.back() == '\r')
			line.remove_suffix(1);
		if (line.empty())
			continue;
		const std::vector<std::string_view> cells = SplitTabs(line);
		if (header) {
			header = false;
			for (const std::string_view column : columns) {
				const auto found = std::find(cells.begin(), cells.end(), column);
				if (found == cells.end())
					return;
				at.push_back(static_cast<size_t>(found - cells.begin()));
			}
			continue;
		}
		bool complete = true;
		for (size_t i = 0; i < at.size(); i++) {
			if (at[i] >= cells.size()) {
				complete = false;
				break;
			}
			picked[i] = cells[at[i]];
		}
		if (complete)
			callback(picked);
	}
}

std::optional<size_t> NodeIndex(std::string_view text)
{
	size_t value = 0;
	if (text.empty() || text.size() > 5)
		return std::nullopt;
	for (const char c : text) {
		if (c < '0' || c > '9')
			return std::nullopt;
		value = (value * 10) + static_cast<size_t>(c - '0');
	}
	return value < PassiveTree().size() ? std::optional<size_t>(value) : std::nullopt;
}

uint32_t OpenedMs = 0;
int OpenedSpent = 0;
int ChangesWhileOpen = 0;

constexpr std::string_view BuildsHeader = "date\tsession\thero\tclass\thero level\twhen\tpoints spent\tpoints total\tascendancy\tmain\tset I\tset II\tattributes\ttree\n";

} // namespace

void TelemetryPassiveTaken(const Player &player, PassiveNodeId node, PassivePool pool)
{
	ChangesWhileOpen++;
	WriteNodeEvent(player, "tree_take", "took", node, pool);
}

void TelemetryPassiveRefunded(const Player &player, PassiveNodeId node, PassivePool pool)
{
	ChangesWhileOpen++;
	WriteNodeEvent(player, "tree_refund", "refunded", node, pool);
}

void TelemetryPassivesCleared(const Player &player, std::string_view reason, int nodes)
{
	if (&player != MyPlayer || !TelemetryEnabled() || nodes <= 0)
		return;
	TelemetryText("tree", std::format("cleared {:d} nodes: {:s}", nodes, reason));
	TelemetryEventNow(LogChannel::Tree, LogLevel::Always, "tree_clear")
	    .add("p", player.getId())
	    .add("reason", reason)
	    .add("nodes", nodes)
	    .add("heroLevel", player.getCharacterLevel());
	WriteHistory(player, StrCat("cleared: ", reason), nullptr, PassivePool::Main, nodes, false);
}

void TelemetryTreeSnapshot(const Player &player, std::string_view when)
{
	if (&player != MyPlayer || !TelemetryEnabled() || PassiveTree().empty())
		return;
	const std::vector<PassiveNodeId> main = HeldIn(player, PassivePool::Main);
	const std::vector<PassiveNodeId> setI = HeldIn(player, PassivePool::SetI);
	const std::vector<PassiveNodeId> setII = HeldIn(player, PassivePool::SetII);
	const std::optional<PassiveNodeId> ascendancy = ChosenAscendancy(player);
	const std::string ascendancyName = ascendancy ? PassiveNodeName(PassiveTree()[*ascendancy]) : std::string();
	std::string attributes;
	std::string attributesJson = "{";
	for (const auto &[pool, ids] : { std::pair { PassivePool::Main, &main }, std::pair { PassivePool::SetI, &setI }, std::pair { PassivePool::SetII, &setII } }) {
		for (const PassiveNodeId id : *ids) {
			if (PassiveTree()[id].kind != PassiveKind::Attribute)
				continue;
			const std::string_view picked = PassiveAttributeName(ChosenPassiveAttributeIn(player, id, pool));
			const std::string key = pool == PassivePool::Main ? std::to_string(id) : StrCat(id, "/", PoolName(pool));
			StrAppend(attributes, attributes.empty() ? "" : " ", key, "=", picked.substr(0, 3));
			if (attributesJson.size() > 1)
				attributesJson += ',';
			AppendJsonString(attributesJson, key);
			attributesJson += ':';
			AppendJsonString(attributesJson, picked);
		}
	}
	attributesJson += '}';

	TelemetryEvent event = TelemetryEventNow(LogChannel::Tree, LogLevel::Always, "tree");
	if (event) {
		JsonArray notables;
		for (const PassiveNodeId id : main) {
			const PassiveNode &node = PassiveTree()[id];
			if (node.kind == PassiveKind::Notable || node.kind == PassiveKind::Keystone || node.kind == PassiveKind::AscendancyNotable)
				notables.add(PassiveNodeName(node));
		}
		std::string roads = "{";
		const auto depths = RoadDepths(player);
		for (size_t i = 0; i < depths.size(); i++) {
			if (depths[i] == 0)
				continue;
			if (roads.size() > 1)
				roads += ',';
			AppendJsonString(roads, magic_enum::enum_name(static_cast<ProfaneRoad>(i)));
			StrAppend(roads, ":", depths[i]);
		}
		roads += '}';
		std::string totals = "{";
		const PassiveTotals sums = GetPassiveTotals(player);
		for (size_t i = 0; i < sums.size(); i++) {
			if (sums[i] == 0)
				continue;
			if (totals.size() > 1)
				totals += ',';
			AppendJsonString(totals, magic_enum::enum_name(static_cast<PassiveStat>(i)));
			StrAppend(totals, ":", sums[i]);
		}
		totals += '}';
		event.add("p", player.getId())
		    .add("when", when)
		    .add("hero", player._pName)
		    .add("class", player.getClassName())
		    .add("heroLevel", player.getCharacterLevel())
		    .add("tree", std::format("{:08x}", PassiveTreeHash()))
		    .add("spent", PassivePointsSpent(player))
		    .add("total", PassivePointsTotal(player))
		    .add("ascendancySpent", AscendancyPointsSpent(player))
		    .add("ascendancyTotal", AscendancyPointsTotal(player))
		    .add("ascendancy", ascendancyName)
		    .add("weaponSet", static_cast<int>(player.poe2.weaponSet))
		    .addRaw("main", IdsJson(main))
		    .addRaw("set1", IdsJson(setI))
		    .addRaw("set2", IdsJson(setII))
		    .addRaw("attributes", attributesJson)
		    .addRaw("notables", notables.str())
		    .addRaw("roads", roads)
		    .addRaw("totals", totals);
	}
	AppendKeptLine("tree_builds.tsv", BuildsHeader,
	    std::format("{:s}\t{:s}\t{:d}\t{:d}\t{:s}\t{:s}\t{:s}\t{:s}\t{:s}\t{:08x}\n", LineStart(player), when, PassivePointsSpent(player), PassivePointsTotal(player),
	        Cell(ascendancyName), IdsText(main), IdsText(setI), IdsText(setII), attributes, PassiveTreeHash()));
}

void TelemetryTreeOpened(const Player &player)
{
	OpenedMs = SDL_GetTicks();
	OpenedSpent = PassivePointsSpent(player);
	ChangesWhileOpen = 0;
}

void TelemetryTreeClosed(const Player &player)
{
	if (OpenedMs == 0 || &player != MyPlayer || !TelemetryEnabled()) {
		OpenedMs = 0;
		return;
	}
	const double seconds = (static_cast<uint32_t>(SDL_GetTicks()) - OpenedMs) / 1000.0;
	OpenedMs = 0;
	TelemetryEventNow(LogChannel::Tree, LogLevel::Normal, "tree_panel")
	    .add("p", player.getId())
	    .add("seconds", seconds)
	    .add("changes", ChangesWhileOpen)
	    .add("pointsSpent", PassivePointsSpent(player) - OpenedSpent)
	    .add("pointsLeft", PassivePointsLeft(player))
	    .add("heroLevel", player.getCharacterLevel());
}

TreeHeat LoadTreeHeat()
{
	TreeHeat heat;
	const size_t size = PassiveTree().size();
	heat.holders.assign(size, 0);
	heat.takes.assign(size, 0);
	heat.refunds.assign(size, 0);
	const std::string hash = std::format("{:08x}", PassiveTreeHash());
	// Each hero's latest build on this tree.
	std::map<std::string, std::string, std::less<>> latest;
	const std::string builds = ReadKeptFile("tree_builds.tsv");
	ForEachRow(builds, { "hero", "tree", "main", "set I", "set II" }, [&](const std::vector<std::string_view> &row) {
		if (row[1] == hash)
			latest[std::string(row[0])] = StrCat(row[2], " ", row[3], " ", row[4]);
	});
	std::vector<bool> held(size);
	for (const auto &[hero, ids] : latest) {
		heat.heroes++;
		std::fill(held.begin(), held.end(), false);
		std::string_view rest = ids;
		while (!rest.empty()) {
			const size_t space = rest.find(' ');
			const std::string_view id = rest.substr(0, space);
			rest = space == std::string_view::npos ? std::string_view() : rest.substr(space + 1);
			if (const std::optional<size_t> index = NodeIndex(id); index && !held[*index]) {
				held[*index] = true;
				heat.holders[*index]++;
				heat.mostHolders = std::max(heat.mostHolders, heat.holders[*index]);
			}
		}
	}
	const std::string history = ReadKeptFile("tree_history.tsv");
	ForEachRow(history, { "action", "node", "tree" }, [&](const std::vector<std::string_view> &row) {
		if (row[2] != hash)
			return;
		if (const std::optional<size_t> index = NodeIndex(row[1]); index) {
			if (row[0] == "took")
				heat.takes[*index]++;
			else if (row[0] == "refunded")
				heat.refunds[*index]++;
		}
	});
	return heat;
}

std::string TreeTrackerText(const Player &player)
{
	if (PassiveTree().empty())
		return "The passive tree is only in POE2 mode.";
	std::string text = std::format("Passive points {:d}/{:d}, weapon set I {:d}, set II {:d}, ascendancy {:d}/{:d}", PassivePointsSpent(player), PassivePointsTotal(player),
	    WeaponSetPointsSpent(player, PassivePool::SetI), WeaponSetPointsSpent(player, PassivePool::SetII), AscendancyPointsSpent(player), AscendancyPointsTotal(player));
	std::map<std::string, std::vector<std::string>> byArea;
	for (const PassiveNode &node : PassiveTree()) {
		const bool notable = node.kind == PassiveKind::Notable || node.kind == PassiveKind::Keystone || node.kind == PassiveKind::AscendancyNotable;
		if (!notable)
			continue;
		for (const PassivePool pool : { PassivePool::Main, PassivePool::SetI, PassivePool::SetII }) {
			if (!IsPassiveInPool(player, node.id, pool))
				continue;
			std::string name = PassiveNodeName(node);
			if (pool != PassivePool::Main)
				StrAppend(name, pool == PassivePool::SetI ? " (set I)" : " (set II)");
			byArea[node.region].push_back(std::move(name));
		}
	}
	for (const auto &[area, names] : byArea) {
		StrAppend(text, "\n", area.empty() ? std::string_view("other") : std::string_view(area), ":");
		for (size_t i = 0; i < names.size(); i++)
			StrAppend(text, i == 0 ? " " : ", ", names[i]);
	}
	const auto depths = RoadDepths(player);
	for (size_t i = 0; i < depths.size(); i++) {
		if (depths[i] > 0)
			StrAppend(text, "\n", magic_enum::enum_name(static_cast<ProfaneRoad>(i)), "'s road: ", depths[i], " of ", ProfaneRoadNodes, " steps");
	}
	const PassiveTotals sums = GetPassiveTotals(player);
	std::string totals;
	for (size_t i = 0; i < sums.size(); i++) {
		if (sums[i] != 0)
			StrAppend(totals, totals.empty() ? "" : ", ", PassiveStatText(static_cast<PassiveStat>(i), sums[i]));
	}
	if (!totals.empty())
		StrAppend(text, "\nTree totals: ", totals);
	return text;
}

} // namespace devilution
