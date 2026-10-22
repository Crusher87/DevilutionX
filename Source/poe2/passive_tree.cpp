#include "poe2/passive_tree.hpp"

#include <algorithm>
#include <bit>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>

#include <ankerl/unordered_dense.h>
#include <magic_enum/magic_enum.hpp>

#include "data/file.hpp"
#include "data/record_reader.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "multi.h"
#include "player.h"
#include "poe2/jewels.hpp"
#include "poe2/weapon_swap.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/tree_telemetry.hpp"
#include "utils/format.hpp"
#include "utils/is_of.hpp"
#include "utils/language.h"
#include "utils/log.hpp"
#include "utils/str_cat.hpp"
#include "utils/str_split.hpp"

namespace devilution {

namespace {

std::vector<PassiveNode> Tree;
uint32_t TreeHash = 0;
/** @brief Which nodes are ascendancy nodes, and which of those cost ascendancy points, for counting points by bit masks. */
std::bitset<MaxPassiveNodes> AscendancyNodes;
std::bitset<MaxPassiveNodes> AscendancyNotables;
std::vector<PassiveNodeId> AscendancyRoots;

/** @brief Links from the node to every other, or -1 where there's no way. */
std::vector<int> StepsFrom(PassiveNodeId from)
{
	std::vector<int> steps(Tree.size(), -1);
	if (from >= Tree.size())
		return steps;
	std::vector<PassiveNodeId> open { from };
	steps[from] = 0;
	for (size_t i = 0; i < open.size(); i++) {
		for (const PassiveNodeId next : Tree[open[i]].links) {
			if (steps[next] < 0) {
				steps[next] = steps[open[i]] + 1;
				open.push_back(next);
			}
		}
	}
	return steps;
}

std::optional<int> ParseInt(std::string_view text)
{
	int value = 0;
	bool negative = false;
	size_t i = 0;
	if (!text.empty() && text[0] == '-') {
		negative = true;
		i = 1;
	}
	if (i == text.size())
		return std::nullopt;
	for (; i < text.size(); i++) {
		if (text[i] < '0' || text[i] > '9')
			return std::nullopt;
		value = value * 10 + (text[i] - '0');
	}
	return negative ? -value : value;
}

/** @brief Parses "Stat=value,Stat=value". */
std::vector<std::pair<PassiveStat, int>> ParseStats(std::string_view text)
{
	std::vector<std::pair<PassiveStat, int>> stats;
	for (const std::string_view entry : SplitByChar(text, ',')) {
		const size_t equals = entry.find('=');
		if (equals == std::string_view::npos)
			continue;
		const auto stat = magic_enum::enum_cast<PassiveStat>(entry.substr(0, equals));
		const std::optional<int> value = ParseInt(entry.substr(equals + 1));
		if (stat && *stat != PassiveStat::Count && value)
			stats.emplace_back(*stat, *value);
		else
			LogError("Unknown passive stat \"{}\"", entry);
	}
	return stats;
}

/** @brief Each Profane Road's door, its 8 nodes from the door, and its far gate. */
struct RoadEnds {
	PassiveNodeId door;
	std::array<PassiveNodeId, ProfaneRoadNodes> nodes;
	PassiveNodeId farGate;
};
std::array<std::optional<RoadEnds>, static_cast<size_t>(ProfaneRoad::Count)> Roads;

/** @brief Reads a node's road column: "leoric:3" is the third node from Leoric's Road's door. */
void ParseRoad(std::string_view text, PassiveNode &node)
{
	const size_t colon = text.find(':');
	if (text.empty() || colon == std::string_view::npos)
		return;
	const std::string_view name = text.substr(0, colon);
	const std::optional<int> step = ParseInt(text.substr(colon + 1));
	std::optional<ProfaneRoad> road;
	if (name == "leoric")
		road = ProfaneRoad::Leoric;
	else if (name == "bartuc")
		road = ProfaneRoad::Bartuc;
	else if (name == "sickness")
		road = ProfaneRoad::Sickness;
	else if (name == "maiden")
		road = ProfaneRoad::Maiden;
	if (!road || !step || *step < 1 || *step > ProfaneRoadNodes) {
		LogError("Passive tree node {} has a bad road \"{}\"", node.id, text);
		return;
	}
	node.road = road;
	node.roadStep = static_cast<uint8_t>(*step);
}

/** @brief The node's one link that isn't on its road, or nothing when there are none or several. */
std::optional<PassiveNodeId> OffRoadLink(const PassiveNode &node)
{
	std::optional<PassiveNodeId> found;
	for (const PassiveNodeId link : node.links) {
		if (Tree[link].road)
			continue;
		if (found)
			return std::nullopt;
		found = link;
	}
	return found;
}

/** @brief Finds each road's door and far gate, checking every road is one chain of 8 with one way in and one out. */
void FindRoadEnds()
{
	for (size_t road = 0; road < Roads.size(); road++) {
		Roads[road] = std::nullopt;
		std::array<std::optional<PassiveNodeId>, ProfaneRoadNodes> nodes;
		bool ok = true;
		for (const PassiveNode &node : Tree) {
			if (node.road != static_cast<ProfaneRoad>(road))
				continue;
			std::optional<PassiveNodeId> &slot = nodes[node.roadStep - 1];
			ok = ok && !slot;
			slot = node.id;
		}
		if (std::none_of(nodes.begin(), nodes.end(), [](const auto &node) { return node.has_value(); }))
			continue;
		RoadEnds ends {};
		for (int step = 0; ok && step < ProfaneRoadNodes; step++) {
			ok = nodes[step].has_value();
			if (ok)
				ends.nodes[step] = *nodes[step];
		}
		for (int step = 0; ok && step + 1 < ProfaneRoadNodes; step++) {
			const std::vector<PassiveNodeId> &links = Tree[ends.nodes[step]].links;
			ok = std::find(links.begin(), links.end(), ends.nodes[step + 1]) != links.end();
		}
		const std::optional<PassiveNodeId> door = ok ? OffRoadLink(Tree[ends.nodes.front()]) : std::nullopt;
		const std::optional<PassiveNodeId> farGate = ok ? OffRoadLink(Tree[ends.nodes.back()]) : std::nullopt;
		// The middle nodes link only along the road.
		for (int step = 1; ok && step + 1 < ProfaneRoadNodes; step++)
			ok = Tree[ends.nodes[step]].links.size() == 2;
		if (!ok || !door || !farGate || *door == *farGate) {
			LogError("Profane Road {} isn't one chain of {} nodes with a door and a far gate; it is left out", road, ProfaneRoadNodes);
			for (PassiveNode &node : Tree) {
				if (node.road == static_cast<ProfaneRoad>(road)) {
					node.road = std::nullopt;
					node.roadStep = 0;
				}
			}
			continue;
		}
		ends.door = *door;
		ends.farGate = *farGate;
		Roads[road] = ends;
	}
}

/** @brief How a link bends, as the tree file gives it for each link from its lower id end. */
struct LinkArc {
	PassiveNodeId from;
	PassiveNodeId to;
	/** @brief 0: straight; otherwise the radius of a circular arc, positive when it bends clockwise on screen going from the lower id node to the higher. */
	float radius = 0;
	/** @brief Follows the orbit round the cluster's centre. */
	bool orbit = false;
};

/** @brief Every link's points, in one list, and where each link's run of points starts, by (lower id, higher id). */
std::vector<PassiveTreePoint> LinkPoints;
std::vector<std::pair<uint32_t, std::pair<uint32_t, uint32_t>>> LinkRuns;

uint32_t LinkKey(PassiveNodeId a, PassiveNodeId b)
{
	return (static_cast<uint32_t>(std::min(a, b)) << 16) | std::max(a, b);
}

constexpr float Pi = 3.14159265F;
/** @brief Points along each curved link, ends included. */
constexpr int CurvePoints = 17;

/** @brief Adds the points of an arc round a centre, from one angle turning by sweep (positive is clockwise on screen). */
void AddArcPoints(PassiveTreePoint centre, float radius, float start, float sweep)
{
	for (int i = 0; i < CurvePoints; i++) {
		const float angle = start + sweep * static_cast<float>(i) / static_cast<float>(CurvePoints - 1);
		LinkPoints.push_back({ centre.x + radius * std::cos(angle), centre.y + radius * std::sin(angle) });
	}
}

/** @brief The shorter turn from one angle to another, between -pi and pi. */
float ShortTurn(float from, float to)
{
	float turn = std::fmod(to - from, 2 * Pi);
	if (turn > Pi)
		turn -= 2 * Pi;
	if (turn < -Pi)
		turn += 2 * Pi;
	return turn;
}

/** @brief Works out the points every link is drawn through, once, when the tree loads. */
void BuildLinkPoints(const std::vector<LinkArc> &arcs)
{
	LinkPoints.clear();
	LinkRuns.clear();
	ankerl::unordered_dense::map<uint32_t, const LinkArc *> arcByLink;
	for (const LinkArc &arc : arcs)
		arcByLink.emplace(LinkKey(arc.from, arc.to), &arc);
	for (const PassiveNode &node : Tree) {
		for (const PassiveNodeId link : node.links) {
			if (link < node.id)
				continue;
			const PassiveNode &other = Tree[link];
			const PassiveTreePoint a { static_cast<float>(node.x), static_cast<float>(node.y) };
			const PassiveTreePoint b { static_cast<float>(other.x), static_cast<float>(other.y) };
			const auto first = static_cast<uint32_t>(LinkPoints.size());
			const auto found = arcByLink.find(LinkKey(node.id, link));
			const LinkArc *arc = found != arcByLink.end() ? found->second : nullptr;
			const float dx = b.x - a.x;
			const float dy = b.y - a.y;
			const float chord = std::sqrt(dx * dx + dy * dy);
			if (arc != nullptr && arc->orbit && node.clusterId == other.clusterId && node.orbit == other.orbit && node.orbit >= 0) {
				// Round the cluster's centre, the short way; the ends snap to the nodes, which the file rounds.
				const PassiveTreePoint centre { static_cast<float>(node.gx), static_cast<float>(node.gy) };
				const float start = std::atan2(a.y - centre.y, a.x - centre.x);
				const float end = std::atan2(b.y - centre.y, b.x - centre.x);
				const float radiusA = std::sqrt((a.x - centre.x) * (a.x - centre.x) + (a.y - centre.y) * (a.y - centre.y));
				const float radiusB = std::sqrt((b.x - centre.x) * (b.x - centre.x) + (b.y - centre.y) * (b.y - centre.y));
				AddArcPoints(centre, (radiusA + radiusB) / 2, start, ShortTurn(start, end));
				LinkPoints[first] = a;
				LinkPoints.back() = b;
			} else if (arc != nullptr && arc->radius != 0 && chord > 0 && std::abs(arc->radius) * 2 >= chord - 0.5F) {
				// A circular arc, the minor one: its centre is on the side it turns towards.
				const float radius = std::max(std::abs(arc->radius), chord / 2);
				const float side = arc->radius > 0 ? 1.F : -1.F;
				const float height = std::sqrt(std::max(radius * radius - (chord / 2) * (chord / 2), 0.F));
				const PassiveTreePoint centre { (a.x + b.x) / 2 - dy / chord * height * side, (a.y + b.y) / 2 + dx / chord * height * side };
				const float start = std::atan2(a.y - centre.y, a.x - centre.x);
				const float end = std::atan2(b.y - centre.y, b.x - centre.x);
				float sweep = ShortTurn(start, end);
				if (sweep * side < 0)
					sweep += 2 * Pi * side; // a half circle can land on either side
				AddArcPoints(centre, radius, start, sweep);
				LinkPoints[first] = a;
				LinkPoints.back() = b;
			} else {
				LinkPoints.push_back(a);
				LinkPoints.push_back(b);
			}
			LinkRuns.push_back({ LinkKey(node.id, link), { first, static_cast<uint32_t>(LinkPoints.size()) - first } });
		}
	}
	std::sort(LinkRuns.begin(), LinkRuns.end());
}

/** @brief Reads a link's arc token: "-" straight, "o" along the orbit, "+R" or "-R" a circular arc of radius R. */
std::optional<LinkArc> ParseArc(std::string_view token, PassiveNodeId from, PassiveNodeId to)
{
	LinkArc arc { from, to };
	if (token == "-" || token.empty())
		return arc;
	if (token == "o") {
		arc.orbit = true;
		return arc;
	}
	if (token.size() < 2 || (token[0] != '+' && token[0] != '-'))
		return std::nullopt;
	const std::optional<int> radius = ParseInt(token.substr(1));
	if (!radius || *radius <= 0)
		return std::nullopt;
	arc.radius = static_cast<float>(token[0] == '+' ? *radius : -*radius);
	return arc;
}

bool CanUsePassives()
{
	return gbIsPoe2 && !gbIsMultiplayer;
}

/** @brief The nodes of a pool's tree: the main nodes, plus that set's own for a weapon set pool. */
std::bitset<MaxPassiveNodes> PoolNodes(const Player &player, PassivePool pool)
{
	std::bitset<MaxPassiveNodes> nodes = player.poe2.passives;
	if (pool != PassivePool::Main)
		nodes |= player.poe2.setPassives[static_cast<size_t>(pool) - 1];
	return nodes;
}

/** @brief Whether every node of the pool's tree is linked to the start without going through `without`. */
bool AllConnected(const Player &player, std::optional<PassiveNodeId> without, PassivePool pool = PassivePool::Main)
{
	const std::bitset<MaxPassiveNodes> nodes = PoolNodes(player, pool);
	const std::optional<PassiveNodeId> start = PassiveStartNode(player._pClass);
	if (!start)
		return nodes.none();
	std::bitset<MaxPassiveNodes> reached;
	std::vector<PassiveNodeId> open { *start };
	reached.set(*start);
	// The ascendancy is its own small tree, rooted at the ascendancy node.
	const std::optional<PassiveNodeId> ascendancy = ChosenAscendancy(player);
	if (ascendancy && *ascendancy != without) {
		open.push_back(*ascendancy);
		reached.set(*ascendancy);
	}
	while (!open.empty()) {
		const PassiveNodeId node = open.back();
		open.pop_back();
		for (const PassiveNodeId next : Tree[node].links) {
			// A Profane Road holds only while it is linked from its door's side: it is never walked back.
			if (reached.test(next) || next == without || !nodes.test(next) || !CanStepPassive(node, next))
				continue;
			reached.set(next);
			open.push_back(next);
		}
	}
	for (size_t node = 0; node < Tree.size(); node++) {
		if (nodes.test(node) && node != without && !reached.test(node))
			return false;
	}
	return true;
}

} // namespace

void LoadPassiveTreeData()
{
	Tree.clear();
	TreeHash = 0;
	LinkPoints.clear();
	LinkRuns.clear();
	AscendancyNodes.reset();
	AscendancyNotables.reset();
	AscendancyRoots.clear();
	Roads = {};
	if (!gbIsPoe2)
		return;

	constexpr std::string_view Filename = "txtdata\\poe2\\passive_tree.tsv";
	auto dataFile = DataFile::load(Filename);
	if (!dataFile.has_value()) {
		LogError("Couldn't load the passive tree: {}", Filename);
		return;
	}
	dataFile->skipHeaderOrDie(Filename);
	std::vector<LinkArc> arcs;
	for (DataFileRecord record : *dataFile) {
		RecordReader reader { record, Filename };
		PassiveNode node {};
		std::string text;
		reader.readString("id", text);
		const std::optional<int> id = ParseInt(text);
		if (!id || *id != static_cast<int>(Tree.size()) || *id >= static_cast<int>(MaxPassiveNodes)) {
			LogError("Passive tree node ids must count up from 0, got \"{}\"", text);
			Tree.clear();
			return;
		}
		node.id = static_cast<PassiveNodeId>(*id);
		reader.readString("name", node.name);
		reader.readString("kind", text);
		node.kind = magic_enum::enum_cast<PassiveKind>(text).value_or(PassiveKind::Small);
		reader.readString("class", text);
		if (!text.empty())
			node.startClass = magic_enum::enum_cast<HeroClass>(text);
		reader.readString("x", text);
		node.x = ParseInt(text).value_or(0);
		reader.readString("y", text);
		node.y = ParseInt(text).value_or(0);
		reader.readString("stats", text);
		node.stats = ParseStats(text);
		reader.readString("links", text);
		for (const std::string_view link : SplitByChar(text, ',')) {
			const std::optional<int> target = ParseInt(link);
			if (target && *target >= 0 && *target < static_cast<int>(MaxPassiveNodes))
				node.links.push_back(static_cast<PassiveNodeId>(*target));
		}
		reader.readString("region", node.region);
		reader.readString("lore", node.lore);
		reader.readString("group", node.group);
		reader.readString("cluster_id", text);
		node.clusterId = ParseInt(text).value_or(node.id);
		reader.readString("gx", text);
		node.gx = ParseInt(text).value_or(node.x);
		reader.readString("gy", text);
		node.gy = ParseInt(text).value_or(node.y);
		reader.readString("orbit", text);
		node.orbit = static_cast<int8_t>(std::clamp(ParseInt(text).value_or(-1), -1, 6));
		// One arc token for each of this row's links to a higher id, in the row's order.
		reader.readString("arcs", text);
		if (!text.empty()) {
			std::vector<PassiveNodeId> higher;
			for (const PassiveNodeId link : node.links) {
				if (link > node.id)
					higher.push_back(link);
			}
			std::vector<std::string_view> tokens;
			for (const std::string_view token : SplitByChar(text, ','))
				tokens.push_back(token);
			if (tokens.size() != higher.size()) {
				LogError("Passive tree node {} has {} arcs for {} links; drawing them straight", node.id, tokens.size(), higher.size());
			} else {
				for (size_t i = 0; i < tokens.size(); i++) {
					if (const std::optional<LinkArc> arc = ParseArc(tokens[i], node.id, higher[i]); arc)
						arcs.push_back(*arc);
					else
						LogError("Passive tree node {} has a bad arc \"{}\"; drawing it straight", node.id, tokens[i]);
				}
			}
		}
		reader.readString("motif", text);
		reader.readString("road", text);
		ParseRoad(text, node);
		Tree.push_back(std::move(node));
	}
	// Drop links to nodes that don't exist, and make every link go both ways.
	for (PassiveNode &node : Tree) {
		std::erase_if(node.links, [&node](PassiveNodeId link) { return link >= Tree.size() || link == node.id; });
		for (const PassiveNodeId link : node.links)
			Tree[link].links.push_back(node.id);
	}
	for (PassiveNode &node : Tree) {
		std::sort(node.links.begin(), node.links.end());
		node.links.erase(std::unique(node.links.begin(), node.links.end()), node.links.end());
	}
	BuildLinkPoints(arcs);
	FindRoadEnds();
	uint8_t socket = 0;
	for (PassiveNode &node : Tree) {
		if (node.kind != PassiveKind::JewelSocket)
			continue;
		if (socket < MaxJewelSockets)
			node.socketIndex = socket;
		else
			LogError("The passive tree has more jewel sockets than the {} a hero can fill", MaxJewelSockets);
		socket++;
	}
	// Each ascendancy is the root and the ascendancy nodes linked to it.
	for (const PassiveNode &root : Tree) {
		if (root.kind != PassiveKind::Ascendancy)
			continue;
		std::vector<PassiveNodeId> open { root.id };
		Tree[root.id].ascendancyRoot = root.id;
		while (!open.empty()) {
			const PassiveNodeId node = open.back();
			open.pop_back();
			for (const PassiveNodeId next : Tree[node].links) {
				if (Tree[next].kind == PassiveKind::AscendancyNotable && !Tree[next].ascendancyRoot) {
					Tree[next].ascendancyRoot = root.id;
					open.push_back(next);
				}
			}
		}
	}
	for (const PassiveNode &node : Tree) {
		if (IsAscendancyNode(node))
			AscendancyNodes.set(node.id);
		if (node.kind == PassiveKind::AscendancyNotable)
			AscendancyNotables.set(node.id);
		if (node.kind == PassiveKind::Ascendancy)
			AscendancyRoots.push_back(node.id);
	}
	// The tree's shape: its nodes' kinds and links. Stats can change without refunding anyone.
	TreeHash = 2166136261U;
	const auto mix = [](uint32_t value) {
		TreeHash = (TreeHash ^ value) * 16777619U;
	};
	for (const PassiveNode &node : Tree) {
		mix(node.id);
		mix(static_cast<uint32_t>(node.kind));
		for (const PassiveNodeId link : node.links)
			mix(link);
	}
}

uint32_t PassiveTreeHash()
{
	return TreeHash;
}

const std::vector<PassiveNode> &PassiveTree()
{
	return Tree;
}

std::span<const PassiveTreePoint> PassiveLinkPoints(PassiveNodeId a, PassiveNodeId b)
{
	const uint32_t key = LinkKey(a, b);
	const auto it = std::lower_bound(LinkRuns.begin(), LinkRuns.end(), key, [](const auto &run, uint32_t value) { return run.first < value; });
	if (it == LinkRuns.end() || it->first != key)
		return {};
	return { LinkPoints.data() + it->second.first, it->second.second };
}

bool IsLatticeLink(const PassiveNode &a, const PassiveNode &b)
{
	return a.orbit < 0 && b.orbit < 0;
}

bool IsProfaneNode(const PassiveNode &node)
{
	return node.group == "profane";
}

bool IsCapstoneNode(const PassiveNode &node)
{
	return node.group == "capstone";
}

std::optional<PassiveNodeId> ProfaneRoadDoor(ProfaneRoad road)
{
	const std::optional<RoadEnds> &ends = Roads[static_cast<size_t>(road)];
	return ends ? std::optional<PassiveNodeId>(ends->door) : std::nullopt;
}

std::optional<PassiveNodeId> ProfaneRoadFarGate(ProfaneRoad road)
{
	const std::optional<RoadEnds> &ends = Roads[static_cast<size_t>(road)];
	return ends ? std::optional<PassiveNodeId>(ends->farGate) : std::nullopt;
}

bool CanStepPassive(PassiveNodeId from, PassiveNodeId to)
{
	if (from >= Tree.size() || to >= Tree.size())
		return false;
	const PassiveNode &next = Tree[to];
	if (!next.road)
		return true;
	const std::optional<RoadEnds> &ends = Roads[static_cast<size_t>(*next.road)];
	if (!ends)
		return true;
	// Each road node needs the one before it, the first one the door; from the far gate the road can't be entered.
	return from == (next.roadStep == 1 ? ends->door : ends->nodes[next.roadStep - 2]);
}

HeroClass PassiveTreeClass(HeroClass heroClass)
{
	switch (heroClass) {
	case HeroClass::Bard:
		return HeroClass::Rogue;
	case HeroClass::Barbarian:
		return HeroClass::Warrior;
	default:
		return heroClass;
	}
}

std::optional<PassiveNodeId> PassiveStartNode(HeroClass heroClass)
{
	const HeroClass treeClass = PassiveTreeClass(heroClass);
	for (const PassiveNode &node : Tree) {
		if (node.kind == PassiveKind::Start && node.startClass == treeClass)
			return node.id;
	}
	return std::nullopt;
}

int BookPassivePoints(const Player &player)
{
	return std::popcount(static_cast<unsigned>(player.poe2.specBooks)) * PassivePointsPerBook;
}

int PassivePointsTotal(const Player &player)
{
	return std::max(player.getCharacterLevel() - 1, 0) + BookPassivePoints(player);
}

int PassivePointsSpent(const Player &player)
{
	const int spent = static_cast<int>((player.poe2.passives & ~AscendancyNodes).count());
	// Like POE2, weapon set passives spend normal points, but a Set I node and a Set II node cost one point together.
	return spent + static_cast<int>(std::max(player.poe2.setPassives[0].count(), player.poe2.setPassives[1].count()));
}

int PassivePointsLeft(const Player &player)
{
	return PassivePointsTotal(player) - PassivePointsSpent(player);
}

bool IsAscendancyNode(const PassiveNode &node)
{
	return node.kind == PassiveKind::Ascendancy || node.kind == PassiveKind::AscendancyNotable;
}

int AscendancyPointsTotal(const Player &player)
{
	const int level = player.getCharacterLevel();
	if (level < AscendancyLevel)
		return 0;
	return std::min(2 + (2 * ((level - AscendancyLevel) / 10)), MaxAscendancyPoints);
}

int AscendancyPointsSpent(const Player &player)
{
	return static_cast<int>((player.poe2.passives & AscendancyNotables).count());
}

int AscendancyPointsLeft(const Player &player)
{
	return AscendancyPointsTotal(player) - AscendancyPointsSpent(player);
}

std::optional<PassiveNodeId> ChosenAscendancy(const Player &player)
{
	for (const PassiveNodeId root : AscendancyRoots) {
		if (player.poe2.passives.test(root))
			return root;
	}
	return std::nullopt;
}

bool IsPassiveNodeVisible(const Player &player, const PassiveNode &node)
{
	if (!IsAscendancyNode(node))
		return true;
	if (node.startClass != PassiveTreeClass(player._pClass) || player.getCharacterLevel() < AscendancyLevel)
		return false;
	const std::optional<PassiveNodeId> chosen = ChosenAscendancy(player);
	return !chosen || node.ascendancyRoot == chosen;
}

bool IsPassiveAllocated(const Player &player, PassiveNodeId node)
{
	return IsPassiveAllocatedIn(player, node, player.poe2.weaponSet == 0 ? PassivePool::SetI : PassivePool::SetII);
}

bool IsPassiveAllocatedIn(const Player &player, PassiveNodeId node, PassivePool pool)
{
	if (node >= Tree.size())
		return false;
	if (Tree[node].kind == PassiveKind::Start)
		return Tree[node].startClass == PassiveTreeClass(player._pClass);
	// Test the bits directly: building the pool's whole bitset for one node is slow on a big tree.
	if (player.poe2.passives.test(node))
		return true;
	return pool != PassivePool::Main && player.poe2.setPassives[static_cast<size_t>(pool) - 1].test(node);
}

bool IsPassiveInPool(const Player &player, PassiveNodeId node, PassivePool pool)
{
	if (node >= Tree.size() || Tree[node].kind == PassiveKind::Start)
		return false;
	if (pool == PassivePool::Main)
		return player.poe2.passives.test(node);
	return player.poe2.setPassives[static_cast<size_t>(pool) - 1].test(node);
}

bool CanTakeWeaponSetPoints(const PassiveNode &node)
{
	// Profane nodes take main points only (design v8 §1.5).
	return IsAnyOf(node.kind, PassiveKind::Small, PassiveKind::Notable, PassiveKind::Attribute) && !IsProfaneNode(node);
}

int WeaponSetPointsSpent(const Player &player, PassivePool pool)
{
	if (pool == PassivePool::Main)
		return 0;
	return static_cast<int>(player.poe2.setPassives[static_cast<size_t>(pool) - 1].count());
}

int WeaponSetPointsLeft(const Player &player, PassivePool pool)
{
	return WeaponSetPointsTotal(player) - WeaponSetPointsSpent(player, pool);
}

bool WeaponSetNeedsPassivePoint(const Player &player, PassivePool pool)
{
	if (pool == PassivePool::Main)
		return false;
	// A node in the set with more nodes costs a normal point; one that pairs up with the other set is free.
	const PassivePool other = pool == PassivePool::SetI ? PassivePool::SetII : PassivePool::SetI;
	return WeaponSetPointsSpent(player, pool) >= WeaponSetPointsSpent(player, other) && PassivePointsLeft(player) <= 0;
}

bool CanAllocatePassive(const Player &player, PassiveNodeId node, PassivePool pool)
{
	if (!CanUsePassives() || node >= Tree.size() || IsPassiveAllocatedIn(player, node, pool))
		return false;
	const PassiveNode &passive = Tree[node];
	if (passive.kind == PassiveKind::Start)
		return false;
	if (pool != PassivePool::Main) {
		if (!CanTakeWeaponSetPoints(passive) || WeaponSetPointsLeft(player, pool) <= 0 || WeaponSetNeedsPassivePoint(player, pool))
			return false;
		return std::any_of(passive.links.begin(), passive.links.end(), [&](PassiveNodeId link) { return IsPassiveAllocatedIn(player, link, pool) && CanStepPassive(link, node); });
	}
	// The main tree may grow through weapon set nodes too; taking one with a main point moves it out of the sets.
	if (player.poe2.passives.test(node))
		return false;
	if (passive.kind == PassiveKind::Ascendancy)
		return passive.startClass == PassiveTreeClass(player._pClass) && player.getCharacterLevel() >= AscendancyLevel && !ChosenAscendancy(player);
	if (passive.kind == PassiveKind::AscendancyNotable) {
		if (!IsPassiveNodeVisible(player, passive) || AscendancyPointsLeft(player) <= 0)
			return false;
	} else if (PassivePointsLeft(player) <= 0) {
		return false;
	}
	return std::any_of(Tree[node].links.begin(), Tree[node].links.end(), [&](PassiveNodeId link) { return IsPassiveAllocatedIn(player, link, PassivePool::Main) && CanStepPassive(link, node); });
}

std::vector<PassiveNodeId> PassiveRoute(const Player &player, PassiveNodeId target, PassivePool pool)
{
	if (!CanUsePassives() || target >= Tree.size() || IsPassiveAllocatedIn(player, target, pool))
		return {};
	const auto canTake = [&](const PassiveNode &node) {
		if (IsAscendancyNode(node) || node.kind == PassiveKind::Start)
			return false;
		return pool == PassivePool::Main || CanTakeWeaponSetPoints(node);
	};
	if (!canTake(Tree[target]))
		return {};
	// Searches outwards from the target, so the first allocated node found ends the quickest route.
	std::vector<int> previous(Tree.size(), -1);
	std::vector<PassiveNodeId> open { target };
	previous[target] = target;
	for (size_t i = 0; i < open.size(); i++) {
		const PassiveNodeId node = open[i];
		for (const PassiveNodeId next : Tree[node].links) {
			// The route runs from next to node, so a Profane Road is followed only from its door.
			if (previous[next] >= 0 || !CanStepPassive(next, node))
				continue;
			previous[next] = node;
			if (IsPassiveAllocatedIn(player, next, pool) && IsPassiveNodeVisible(player, Tree[next])) {
				std::vector<PassiveNodeId> route { next };
				for (PassiveNodeId step = node; step != target; step = static_cast<PassiveNodeId>(previous[step]))
					route.push_back(step);
				route.push_back(target);
				return route;
			}
			// The marked route never goes through a Profane Path's notable the hero hasn't taken.
			if (canTake(Tree[next]) && !(IsProfaneNode(Tree[next]) && Tree[next].kind == PassiveKind::Notable))
				open.push_back(next);
		}
	}
	return {};
}

bool PassiveNeedsAttributeChoice(PassiveNodeId node)
{
	return node < Tree.size() && Tree[node].kind == PassiveKind::Attribute;
}

bool AllocatePassive(Player &player, PassiveNodeId node, PassiveAttribute attribute, PassivePool pool)
{
	if (!CanAllocatePassive(player, node, pool) || attribute >= PassiveAttribute::Count)
		return false;
	if (pool == PassivePool::Main) {
		player.poe2.passives.set(node);
		for (auto &set : player.poe2.setPassives)
			set.reset(node);
	} else {
		player.poe2.setPassives[static_cast<size_t>(pool) - 1].set(node);
	}
	SetChosenPassiveAttribute(player, node, PassiveNeedsAttributeChoice(node) ? attribute : PassiveAttribute::Strength, pool);
	CalcPlrInv(player, true);
	TelemetryPassiveTaken(player, node, pool);
	return true;
}

PassiveAttribute ChosenPassiveAttribute(const Player &player, PassiveNodeId node)
{
	if (node >= MaxPassiveNodes)
		return PassiveAttribute::Strength;
	const bool setII = !player.poe2.passives.test(node) && player.poe2.weaponSet == 1 && player.poe2.setPassives[1].test(node);
	return ChosenPassiveAttributeIn(player, node, setII ? PassivePool::SetII : PassivePool::Main);
}

PassiveAttribute ChosenPassiveAttributeIn(const Player &player, PassiveNodeId node, PassivePool pool)
{
	if (node >= MaxPassiveNodes)
		return PassiveAttribute::Strength;
	const uint8_t value = pool == PassivePool::SetII ? (player.poe2.passiveAttributes[node] >> 4) : (player.poe2.passiveAttributes[node] & 0x0F);
	return value < static_cast<uint8_t>(PassiveAttribute::Count) ? static_cast<PassiveAttribute>(value) : PassiveAttribute::Strength;
}

void SetChosenPassiveAttribute(Player &player, PassiveNodeId node, PassiveAttribute attribute, PassivePool pool)
{
	if (node >= MaxPassiveNodes)
		return;
	uint8_t &value = player.poe2.passiveAttributes[node];
	const uint8_t pick = static_cast<uint8_t>(attribute) & 0x0F;
	if (pool == PassivePool::Main)
		value = static_cast<uint8_t>(pick | (pick << 4));
	else if (pool == PassivePool::SetI)
		value = static_cast<uint8_t>((value & 0xF0) | pick);
	else
		value = static_cast<uint8_t>((value & 0x0F) | (pick << 4));
}

PassiveStat PassiveAttributeStat(PassiveAttribute attribute)
{
	switch (attribute) {
	case PassiveAttribute::Dexterity:
		return PassiveStat::Dexterity;
	case PassiveAttribute::Magic:
		return PassiveStat::Magic;
	case PassiveAttribute::Vitality:
		return PassiveStat::Vitality;
	default:
		return PassiveStat::Strength;
	}
}

std::string_view PassiveAttributeName(PassiveAttribute attribute)
{
	switch (attribute) {
	case PassiveAttribute::Dexterity:
		return _("Dexterity");
	case PassiveAttribute::Magic:
		return _("Magic");
	case PassiveAttribute::Vitality:
		return _("Vitality");
	default:
		return _("Strength");
	}
}

bool CanRefundPassive(const Player &player, PassiveNodeId node, PassivePool pool)
{
	if (!CanUsePassives() || !IsPassiveInPool(player, node, pool))
		return false;
	if (pool != PassivePool::Main)
		return AllConnected(player, node, pool);
	// A socket holding a jewel stays until the jewel is taken out.
	if (Tree[node].kind == PassiveKind::JewelSocket && SocketedJewel(player, node) != nullptr)
		return false;
	// Both weapon sets grow from the main tree, so they must stay linked too.
	return AllConnected(player, node) && AllConnected(player, node, PassivePool::SetI) && AllConnected(player, node, PassivePool::SetII);
}

bool RefundPassive(Player &player, PassiveNodeId node, PassivePool pool)
{
	if (!CanRefundPassive(player, node, pool))
		return false;
	if (pool == PassivePool::Main)
		player.poe2.passives.reset(node);
	else
		player.poe2.setPassives[static_cast<size_t>(pool) - 1].reset(node);
	if (!player.poe2.passives.test(node) && !player.poe2.setPassives[1].test(node))
		player.poe2.passiveAttributes[node] &= 0x0F;
	if (!player.poe2.passives.test(node) && !player.poe2.setPassives[0].test(node))
		player.poe2.passiveAttributes[node] &= 0xF0;
	CalcPlrInv(player, true);
	TelemetryPassiveRefunded(player, node, pool);
	return true;
}

void RefundOverspentWeaponSets(Player &player)
{
	const int total = WeaponSetPointsTotal(player);
	if (WeaponSetPointsSpent(player, PassivePool::SetI) <= total && WeaponSetPointsSpent(player, PassivePool::SetII) <= total)
		return;
	for (size_t node = 0; node < player.poe2.passiveAttributes.size(); node++) {
		if (!player.poe2.passives.test(node))
			player.poe2.passiveAttributes[node] = 0;
	}
	TelemetryPassivesCleared(player, "weapon sets refunded", static_cast<int>(player.poe2.setPassives[0].count() + player.poe2.setPassives[1].count()));
	for (auto &set : player.poe2.setPassives)
		set.reset();
	TelemetryLog("weaponset", FormatRuntime("refunded weapon sets: {:d} points each now", total));
}

bool RefundOverspentPassives(Player &player)
{
	if (PassivePointsLeft(player) >= 0)
		return false;
	TelemetryPassivesCleared(player, "overspent refund", static_cast<int>(player.poe2.passives.count() + player.poe2.setPassives[0].count() + player.poe2.setPassives[1].count()));
	player.poe2.passives.reset();
	for (auto &set : player.poe2.setPassives)
		set.reset();
	player.poe2.passiveAttributes.fill(0);
	return true;
}

void RefundPassivesForNewTree(Player &player)
{
	if (TreeHash == 0 || player.poe2.passiveTreeHash == TreeHash)
		return;
	TelemetryPassivesCleared(player, "tree update refund", static_cast<int>(player.poe2.passives.count() + player.poe2.setPassives[0].count() + player.poe2.setPassives[1].count()));
	player.poe2.passives.reset();
	for (auto &set : player.poe2.setPassives)
		set.reset();
	player.poe2.passiveAttributes.fill(0);
	player.poe2.passiveTreeHash = TreeHash;
	// Jewels are kept by socket number, so they wait in the new tree's sockets nearest the hero's start, and work again once
	// those are taken.
	const std::optional<PassiveNodeId> start = PassiveStartNode(player._pClass);
	if (!start)
		return;
	std::vector<Item> jewels;
	for (Item &jewel : player.jewels) {
		if (!jewel.isEmpty())
			jewels.push_back(jewel);
		jewel.clear();
	}
	std::vector<std::pair<int, size_t>> sockets; // steps from the start, socket number
	const std::vector<int> steps = StepsFrom(*start);
	for (const PassiveNode &node : Tree) {
		if (node.kind != PassiveKind::JewelSocket)
			continue;
		if (const std::optional<size_t> index = JewelSocketIndex(node.id); index)
			sockets.emplace_back(steps[node.id] < 0 ? INT_MAX : steps[node.id], *index);
	}
	std::sort(sockets.begin(), sockets.end());
	for (size_t i = 0; i < jewels.size() && i < sockets.size(); i++)
		player.jewels[sockets[i].second] = jewels[i];
}

void ResetPassives(Player &player)
{
	TelemetryPassivesCleared(player, "reset", static_cast<int>(player.poe2.passives.count() + player.poe2.setPassives[0].count() + player.poe2.setPassives[1].count()));
	player.poe2.passives.reset();
	for (auto &set : player.poe2.setPassives)
		set.reset();
	player.poe2.passiveAttributes.fill(0);
	CalcPlrInv(player, true);
}

PassiveTotals GetPassiveTotals(const Player &player)
{
	PassiveTotals totals {};
	if (!CanUsePassives())
		return totals;
	for (const PassiveNode &node : Tree) {
		if (!IsPassiveAllocated(player, node.id))
			continue;
		for (const auto &[stat, value] : PassiveNodeStats(player, node))
			totals[static_cast<size_t>(stat)] += value;
	}
	return totals;
}

static_assert(static_cast<size_t>(PassiveStat::Count) <= MaxPassiveStats, "Poe2PlayerData::passiveTotals is too small");

int PassiveTotal(const Player &player, PassiveStat stat)
{
	return player.poe2.passiveTotals[static_cast<size_t>(stat)];
}

void CachePassiveTotals(Player &player, const PassiveTotals &passives)
{
	for (size_t stat = 0; stat < passives.size(); stat++)
		player.poe2.passiveTotals[stat] = static_cast<int16_t>(std::clamp<int>(passives[stat], INT16_MIN, INT16_MAX));
}

namespace {

std::optional<UniqueRule> RuleForStat(PassiveStat stat)
{
	switch (stat) {
	case PassiveStat::SpellDamage:
		return UniqueRule::SpellDamage;
	case PassiveStat::StunThreshold:
		return UniqueRule::StunThreshold;
	case PassiveStat::ManaCost:
		return UniqueRule::ManaCost;
	case PassiveStat::MaxElementalResist:
		return UniqueRule::MaxElementalResistance;
	case PassiveStat::MaxMagicResist:
		return UniqueRule::MaxMagicResistance;
	case PassiveStat::ArmourVsElements:
		return UniqueRule::ArmourVsElements;
	case PassiveStat::EvasionVsSpells:
		return UniqueRule::EvasionVsSpells;
	case PassiveStat::HitDamageFromMana:
		return UniqueRule::HitDamageFromMana;
	case PassiveStat::ManaSpellDamage:
		return UniqueRule::ManaSpellDamage;
	case PassiveStat::LifeRecoveryToShield:
		return UniqueRule::LifeRecoveryToShield;
	case PassiveStat::NoEnergyShieldRecharge:
		return UniqueRule::NoEnergyShieldRecharge;
	case PassiveStat::StationaryDefences:
		return UniqueRule::StationaryDefences;
	case PassiveStat::MovingVulnerability:
		return UniqueRule::MovingVulnerability;
	case PassiveStat::PhysicalToFire:
		return UniqueRule::PhysicalToFire;
	case PassiveStat::SpellsSpendEnergyShield:
		return UniqueRule::SpellsSpendEnergyShield;
	case PassiveStat::LessEnergyShieldRecharge:
		return UniqueRule::LessEnergyShieldRecharge;
	case PassiveStat::LessManaRecovery:
		return UniqueRule::LessManaRecovery;
	case PassiveStat::SpiritDamage:
		return UniqueRule::SpiritDamage;
	case PassiveStat::DamageVsRareUnique:
		return UniqueRule::DamageVsRareUnique;
	case PassiveStat::LessDamageVsNormal:
		return UniqueRule::LessDamageVsNormal;
	case PassiveStat::FifthAttackRepeat:
		return UniqueRule::FifthAttackRepeat;
	case PassiveStat::NoEvasion:
		return UniqueRule::NoEvasion;
	case PassiveStat::LessMaxLife:
		return UniqueRule::LessMaxLife;
	case PassiveStat::DistanceRule:
		return UniqueRule::DistanceRule;
	case PassiveStat::SpellElementCycle:
		return UniqueRule::SpellElementCycle;
	case PassiveStat::OffElementLess:
		return UniqueRule::OffElementLess;
	case PassiveStat::TwoHandKillFrenzy:
		return UniqueRule::TwoHandKillFrenzy;
	case PassiveStat::BlockRiposte:
		return UniqueRule::BlockRiposte;
	case PassiveStat::ElementRotation:
		return UniqueRule::ElementRotation;
	case PassiveStat::MinionSpellDamage:
		return UniqueRule::MinionSpellDamage;
	case PassiveStat::StaffToLightning:
		return UniqueRule::StaffToLightning;
	case PassiveStat::StaffArc:
		return UniqueRule::StaffArc;
	case PassiveStat::UnarmedHitStacks:
		return UniqueRule::UnarmedHitStacks;
	case PassiveStat::ArrowPierce:
		return UniqueRule::ArrowPierce;
	case PassiveStat::SwapStrike:
		return UniqueRule::SwapStrike;
	case PassiveStat::ArrowDamageMod:
		return UniqueRule::ArrowDamageMod;
	case PassiveStat::SpellDamageToFireAttacks:
		return UniqueRule::SpellDamageToFireAttacks;
	case PassiveStat::StaffAppliesToBow:
		return UniqueRule::StaffAppliesToBow;
	case PassiveStat::Cull:
		return UniqueRule::Cull;
	case PassiveStat::FirstStrike:
		return UniqueRule::FirstStrike;
	case PassiveStat::KillBurst:
		return UniqueRule::KillBurst;
	case PassiveStat::FlaskFrenzy:
		return UniqueRule::FlaskFrenzy;
	case PassiveStat::MoreVsStunned:
		return UniqueRule::MoreVsStunned;
	default:
		return std::nullopt;
	}
}

/** @brief Rules a shrine and a unique can both give: the stronger one counts, they don't add up. */
bool IsKeystoneRule(UniqueRule rule)
{
	return IsAnyOf(rule, UniqueRule::FifthAttackRepeat, UniqueRule::SpellsSpendEnergyShield, UniqueRule::LessEnergyShieldRecharge,
	    UniqueRule::LessManaRecovery, UniqueRule::LifeRecoveryToShield, UniqueRule::NoEnergyShieldRecharge, UniqueRule::SpiritDamage,
	    UniqueRule::LessDamageVsNormal, UniqueRule::NoEvasion, UniqueRule::LessMaxLife, UniqueRule::DistanceRule,
	    UniqueRule::SpellElementCycle, UniqueRule::OffElementLess, UniqueRule::MovingVulnerability);
}

struct RuleCap {
	UniqueRule rule;
	int min;
	int max;
};

/** @brief Rules that tree, gear and shrines together must not push past these. */
constexpr RuleCap RuleCaps[] {
	{ UniqueRule::DamageVsRareUnique, INT16_MIN, 40 },
	{ UniqueRule::EvasionVsSpells, INT16_MIN, 25 },
	{ UniqueRule::StationaryDefences, INT16_MIN, 25 },
	{ UniqueRule::ArmourVsElements, INT16_MIN, 30 },
	{ UniqueRule::ManaCost, -25, INT16_MAX },
	{ UniqueRule::HitDamageFromMana, INT16_MIN, 50 },
	{ UniqueRule::PhysicalToFire, INT16_MIN, 100 },
	{ UniqueRule::StaffToLightning, INT16_MIN, 100 },
	{ UniqueRule::Cull, INT16_MIN, 20 },
	{ UniqueRule::ArrowPierce, INT16_MIN, 5 },
	// Iron Draw and the Black Curse together: arrows can't take more than the whole bonus.
	{ UniqueRule::ArrowDamageMod, INT16_MIN, 100 },
};

} // namespace

void AddPassiveRules(const PassiveTotals &passives, UniqueRuleTotals &rules)
{
	for (size_t stat = 0; stat < passives.size(); stat++) {
		const int value = passives[stat];
		const std::optional<UniqueRule> rule = RuleForStat(static_cast<PassiveStat>(stat));
		if (value == 0 || !rule)
			continue;
		int16_t &total = rules[static_cast<size_t>(*rule)];
		const int combined = IsKeystoneRule(*rule) ? std::max<int>(total, value) : total + value;
		total = static_cast<int16_t>(std::clamp<int>(combined, INT16_MIN, INT16_MAX));
	}
	for (const RuleCap &cap : RuleCaps) {
		int16_t &total = rules[static_cast<size_t>(cap.rule)];
		total = static_cast<int16_t>(std::clamp<int>(total, cap.min, cap.max));
	}
}

int RequirementStrength(const Player &player)
{
	if (PassiveTotal(player, PassiveStat::PaidInBlood) <= 0)
		return player._pStrength;
	return player._pStrength + player._pMagic / 2;
}

int RequirementStrength(const Player &player, int strength, int magic)
{
	if (GetPassiveTotals(player)[static_cast<size_t>(PassiveStat::PaidInBlood)] <= 0)
		return strength;
	return strength + magic / 2;
}

int PassiveWeaponDamage(const Player &player, const PassiveTotals &passives)
{
	const auto passive = [&passives](PassiveStat stat) { return passives[static_cast<size_t>(stat)]; };
	const Item *weapon = nullptr;
	for (const Item &item : { std::cref(player.InvBody[INVLOC_HAND_LEFT]), std::cref(player.InvBody[INVLOC_HAND_RIGHT]) }) {
		if (!item.isEmpty() && item._iStatFlag && IsAnyOf(item._itype, ItemType::Sword, ItemType::Axe, ItemType::Mace, ItemType::Bow, ItemType::Staff)) {
			weapon = &item;
			break;
		}
	}
	// Unarmed counts as a melee attack; the Monk's staff training works barehanded too. The Maiden's bow bridge doesn't reach bare hands.
	if (weapon == nullptr)
		return passive(PassiveStat::MeleeDamage) + passive(PassiveStat::StaffDamage);
	// A bridge copies only the other weapon's own stat, never its total, so no bridge feeds another (design v8 §1.6).
	switch (weapon->_itype) {
	case ItemType::Bow:
		// The Sisters' Gift: staff training carries over to the bow. Leoric's pact: two-handed training does too.
		return passive(PassiveStat::BowDamage) + (passive(PassiveStat::StaffAppliesToBow) > 0 ? passive(PassiveStat::StaffDamage) : 0)
		    + (passive(PassiveStat::KingsDeadPact) > 0 ? passive(PassiveStat::TwoHandDamage) : 0);
	case ItemType::Staff:
		// Like POE2's quarterstaves: a two-handed melee weapon with nodes of its own. The Maiden's pact adds bow training.
		return passive(PassiveStat::StaffDamage) + passive(PassiveStat::MeleeDamage) + passive(PassiveStat::TwoHandDamage)
		    + (passive(PassiveStat::OutOfDarkness) > 0 ? passive(PassiveStat::BowDamage) : 0);
	default:
		return passive(PassiveStat::MeleeDamage) + passive(weapon->_iLoc == ILOC_TWOHAND ? PassiveStat::TwoHandDamage : PassiveStat::OneHandDamage);
	}
}

int PassiveElementDamage(const Player &player, DamageType type)
{
	switch (type) {
	case DamageType::Fire:
		return PassiveTotal(player, PassiveStat::FireDamage);
	case DamageType::Lightning:
		return PassiveTotal(player, PassiveStat::LightningDamage);
	case DamageType::Magic:
		return PassiveTotal(player, PassiveStat::ColdDamage);
	default:
		return 0;
	}
}

std::string PassiveStatText(PassiveStat stat, int value)
{
	// Keystones take as well as give: their negative percentages read as "reduced".
	const auto percent = [value](std::string_view increased, std::string_view reduced) {
		return value >= 0 ? FormatRuntime(increased, value) : FormatRuntime(reduced, -value);
	};
	switch (stat) {
	case PassiveStat::Strength:
		return FormatRuntime(_("{:+d} to Strength"), value);
	case PassiveStat::Magic:
		return FormatRuntime(_("{:+d} to Magic"), value);
	case PassiveStat::Dexterity:
		return FormatRuntime(_("{:+d} to Dexterity"), value);
	case PassiveStat::Vitality:
		return FormatRuntime(_("{:+d} to Vitality"), value);
	case PassiveStat::LifePercent:
		return percent(_(/*xgettext:no-c-format*/ "{:d}% increased maximum Life"), _(/*xgettext:no-c-format*/ "{:d}% reduced maximum Life"));
	case PassiveStat::ManaPercent:
		return percent(_(/*xgettext:no-c-format*/ "{:d}% increased maximum Mana"), _(/*xgettext:no-c-format*/ "{:d}% reduced maximum Mana"));
	case PassiveStat::Damage:
		return percent(_(/*xgettext:no-c-format*/ "{:d}% increased Damage"), _(/*xgettext:no-c-format*/ "{:d}% reduced Damage"));
	case PassiveStat::ToHit:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "{:+d}% to hit"), value);
	case PassiveStat::ArmorPercent:
		return percent(_(/*xgettext:no-c-format*/ "{:d}% increased Armor"), _(/*xgettext:no-c-format*/ "{:d}% reduced Armor"));
	case PassiveStat::FireResist:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "{:+d}% to Fire Resistance"), value);
	case PassiveStat::LightningResist:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "{:+d}% to Lightning Resistance"), value);
	case PassiveStat::MagicResist:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "{:+d}% to Magic Resistance"), value);
	case PassiveStat::AllResist:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "{:+d}% to all Resistances"), value);
	case PassiveStat::LifeRegen:
		return FormatRuntime(_("Regenerate {:d} Life per second"), value);
	case PassiveStat::ManaRegen:
		return percent(_(/*xgettext:no-c-format*/ "{:d}% increased Mana Regeneration"), _(/*xgettext:no-c-format*/ "{:d}% reduced Mana Regeneration"));
	case PassiveStat::SpellLevels:
		return FormatRuntime(_("{:+d} to all Spell Levels"), value);
	case PassiveStat::AttackSpeed:
		return percent(_(/*xgettext:no-c-format*/ "{:d}% increased Attack Speed"), _(/*xgettext:no-c-format*/ "{:d}% reduced Attack Speed"));
	case PassiveStat::CastSpeed:
		return percent(_(/*xgettext:no-c-format*/ "{:d}% increased Cast Speed"), _(/*xgettext:no-c-format*/ "{:d}% reduced Cast Speed"));
	case PassiveStat::EvasionPercent:
		return percent(_(/*xgettext:no-c-format*/ "{:d}% increased Evasion"), _(/*xgettext:no-c-format*/ "{:d}% reduced Evasion"));
	case PassiveStat::EnergyShieldPercent:
		return percent(_(/*xgettext:no-c-format*/ "{:d}% increased maximum Energy Shield"), _(/*xgettext:no-c-format*/ "{:d}% reduced maximum Energy Shield"));
	case PassiveStat::Spirit:
		return FormatRuntime(_("{:+d} to maximum Spirit"), value);
	case PassiveStat::MinionDamage:
		return percent(_(/*xgettext:no-c-format*/ "Minions deal {:d}% increased Damage"), _(/*xgettext:no-c-format*/ "Minions deal {:d}% reduced Damage"));
	case PassiveStat::AnyAttribute:
		return FormatRuntime(_("{:+d} to any Attribute"), value);
	case PassiveStat::MeleeDamage:
		return percent(_(/*xgettext:no-c-format*/ "{:d}% increased Melee Damage"), _(/*xgettext:no-c-format*/ "{:d}% reduced Melee Damage"));
	case PassiveStat::BowDamage:
		return percent(_(/*xgettext:no-c-format*/ "{:d}% increased Damage with Bows"), _(/*xgettext:no-c-format*/ "{:d}% reduced Damage with Bows"));
	case PassiveStat::StaffDamage:
		return percent(_(/*xgettext:no-c-format*/ "{:d}% increased Damage with Staves"), _(/*xgettext:no-c-format*/ "{:d}% reduced Damage with Staves"));
	case PassiveStat::TwoHandDamage:
		return percent(_(/*xgettext:no-c-format*/ "{:d}% increased Damage with Two Handed Weapons"), _(/*xgettext:no-c-format*/ "{:d}% reduced Damage with Two Handed Weapons"));
	case PassiveStat::OneHandDamage:
		return percent(_(/*xgettext:no-c-format*/ "{:d}% increased Damage with One Handed Weapons"), _(/*xgettext:no-c-format*/ "{:d}% reduced Damage with One Handed Weapons"));
	case PassiveStat::SpellDamage:
		return percent(_(/*xgettext:no-c-format*/ "{:d}% increased Spell Damage"), _(/*xgettext:no-c-format*/ "{:d}% reduced Spell Damage"));
	case PassiveStat::FireDamage:
		return percent(_(/*xgettext:no-c-format*/ "{:d}% increased Fire Damage"), _(/*xgettext:no-c-format*/ "{:d}% reduced Fire Damage"));
	case PassiveStat::LightningDamage:
		return percent(_(/*xgettext:no-c-format*/ "{:d}% increased Lightning Damage"), _(/*xgettext:no-c-format*/ "{:d}% reduced Lightning Damage"));
	case PassiveStat::ColdDamage:
		return percent(_(/*xgettext:no-c-format*/ "{:d}% increased Magic (cold) Damage"), _(/*xgettext:no-c-format*/ "{:d}% reduced Magic (cold) Damage"));
	case PassiveStat::IgniteChance:
		return percent(_(/*xgettext:no-c-format*/ "{:d}% increased chance to Ignite"), _(/*xgettext:no-c-format*/ "{:d}% reduced chance to Ignite"));
	case PassiveStat::ShockChance:
		return percent(_(/*xgettext:no-c-format*/ "{:d}% increased chance to Shock"), _(/*xgettext:no-c-format*/ "{:d}% reduced chance to Shock"));
	case PassiveStat::FreezeBuildup:
		return percent(_(/*xgettext:no-c-format*/ "{:d}% increased Freeze Buildup"), _(/*xgettext:no-c-format*/ "{:d}% reduced Freeze Buildup"));
	case PassiveStat::StunBuildup:
		return percent(_(/*xgettext:no-c-format*/ "{:d}% increased Stun Buildup"), _(/*xgettext:no-c-format*/ "{:d}% reduced Stun Buildup"));
	case PassiveStat::StunThreshold:
		return percent(_(/*xgettext:no-c-format*/ "{:d}% increased Stun Threshold"), _(/*xgettext:no-c-format*/ "{:d}% reduced Stun Threshold"));
	case PassiveStat::BlockChance:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "{:+d}% to Block chance with a shield"), value);
	case PassiveStat::LifeOnKill:
		return FormatRuntime(_("Recover {:d} Life on kill"), value);
	case PassiveStat::ManaOnKill:
		return FormatRuntime(_("Recover {:d} Mana on kill"), value);
	case PassiveStat::FlaskRecovery:
		return percent(_(/*xgettext:no-c-format*/ "{:d}% increased Flask Recovery"), _(/*xgettext:no-c-format*/ "{:d}% reduced Flask Recovery"));
	case PassiveStat::FlaskCharges:
		return percent(_(/*xgettext:no-c-format*/ "{:d}% increased Flask Charges gained"), _(/*xgettext:no-c-format*/ "{:d}% reduced Flask Charges gained"));
	case PassiveStat::EnergyShieldRecharge:
		return percent(_(/*xgettext:no-c-format*/ "{:d}% faster Energy Shield Recharge"), _(/*xgettext:no-c-format*/ "{:d}% slower Energy Shield Recharge"));
	case PassiveStat::MinionLife:
		return percent(_(/*xgettext:no-c-format*/ "Minions have {:d}% increased maximum Life"), _(/*xgettext:no-c-format*/ "Minions have {:d}% reduced maximum Life"));
	case PassiveStat::LightRadius:
		return FormatRuntime(_("{:+d} to Light Radius"), value);
	case PassiveStat::LifeRegenRate:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "Regenerate {:d}.{:d}% of maximum Life per second"), value / 10, std::abs(value) % 10);
	case PassiveStat::ManaCost:
		return percent(_(/*xgettext:no-c-format*/ "{:d}% increased Mana Cost of Skills"), _(/*xgettext:no-c-format*/ "{:d}% reduced Mana Cost of Skills"));
	case PassiveStat::MaxElementalResist:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "{:+d}% to maximum Fire and Lightning Resistances"), value);
	case PassiveStat::MaxMagicResist:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "{:+d}% to maximum Magic Resistance"), value);
	case PassiveStat::ArmourVsElements:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "{:d}% of Armour also applies to Fire, Cold and Lightning Damage"), value);
	case PassiveStat::EvasionVsSpells:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "{:d}% of Evasion also applies to Spells"), value);
	case PassiveStat::HitDamageFromMana:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "{:d}% of Damage from Hits is taken from Mana before Life"), value);
	case PassiveStat::ManaSpellDamage:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "Spells deal {:d}% increased Damage per 100 maximum Mana"), value);
	case PassiveStat::LifeRecoveryToShield:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "{:d}% of Life Recovery also recovers Energy Shield"), value);
	case PassiveStat::NoEnergyShieldRecharge:
		return std::string(_("Energy Shield does not Recharge"));
	case PassiveStat::StationaryDefences:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "{:d}% more Defences while standing still"), value);
	case PassiveStat::MovingVulnerability:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "{:d}% more Damage taken for a second after moving"), value);
	case PassiveStat::PhysicalToFire:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "{:d}% of Physical Damage converted to Fire"), value);
	case PassiveStat::SpellsSpendEnergyShield:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "Spells cost Energy Shield before Mana; up to {:d}% more Spell Damage from Energy Shield spent recently"), value);
	case PassiveStat::LessEnergyShieldRecharge:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "{:d}% slower Energy Shield Recharge"), value);
	case PassiveStat::LessManaRecovery:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "{:d}% less Mana Recovery"), value);
	case PassiveStat::SpiritDamage:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "{:d}% more Damage per 10 unreserved Spirit"), value);
	case PassiveStat::DamageVsRareUnique:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "{:d}% more Damage against Rare and Unique monsters"), value);
	case PassiveStat::LessDamageVsNormal:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "{:d}% less Damage against Normal monsters"), value);
	case PassiveStat::FifthAttackRepeat:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "Every 5th Attack repeats at {:d}% of its Damage"), value);
	case PassiveStat::NoEvasion:
		return std::string(_("You have no Evasion"));
	case PassiveStat::LessMaxLife:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "{:d}% less maximum Life"), value);
	case PassiveStat::DistanceRule:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "{:d}% more Damage while no enemy is within 3 tiles"), value);
	case PassiveStat::SpellElementCycle:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "Spells cycle Fire, Lightning and Magic: {:d}% more Damage with the current element"), value);
	case PassiveStat::OffElementLess:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "{:d}% less Spell Damage with the other elements"), value);
	case PassiveStat::TwoHandKillFrenzy:
		return std::string(_("Kills with Two Handed melee weapons grant a Frenzy Charge for 4 seconds, up to 2"));
	case PassiveStat::BlockRiposte:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "After you Block, your next melee hit within 3 seconds deals {:d}% more Damage"), value);
	case PassiveStat::ElementRotation:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "Spells deal {:d}% more Damage if your last Spell hit, within 3 seconds, was a different element"), value);
	case PassiveStat::MinionSpellDamage:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "Minions get {:d}% of your increased Spell Damage as increased Damage, up to 40%"), value);
	case PassiveStat::StaffToLightning:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "{:d}% of Physical Damage with Staves converted to Lightning"), value);
	case PassiveStat::StaffArc:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "Staff hits on Shocked enemies arc to another enemy nearby for {:d}% of their Damage"), value);
	case PassiveStat::UnarmedHitStacks:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "While Unarmed, each hit grants a stack and refreshes them all for {:d} seconds, up to 10: 2% more Evasion each, and 10% increased Attack Speed per 5 stacks"), value);
	case PassiveStat::ArrowPierce:
		return FormatRuntime(_("Arrows Pierce {:d} additional enemies"), value);
	case PassiveStat::SwapStrike:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "After swapping to a different weapon type, your next 3 hits within 4 seconds deal {:d}% more Damage; once every 4 seconds"), value);
	case PassiveStat::ArrowDamageMod:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "Arrows get {:d}% of the damage bonus from your attributes, instead of half"), value);
	case PassiveStat::SpellDamageToFireAttacks:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "{:d}% of your increased Spell Damage also applies to the Fire Damage of your attacks"), value);
	case PassiveStat::StaffAppliesToBow:
		return std::string(_("Increases to Staff Damage also apply to Bows, and Bows take the Staff damage bonus from your attributes"));
	case PassiveStat::Cull:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "Your hits kill Normal and Magic monsters left at {:d}% Life or less"), value);
	case PassiveStat::FirstStrike:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "Your first hit on an enemy at full Life deals {:d}% more Damage"), value);
	case PassiveStat::KillBurst:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "Enemies you kill with melee hits burst for {:d}% of their maximum Life as Physical Damage to enemies beside them"), value);
	case PassiveStat::FlaskFrenzy:
		return std::string(_("Drinking a Flask grants a Frenzy Charge for 4 seconds, up to 2"));
	case PassiveStat::MoreVsStunned:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "Your hits deal {:d}% more Damage to Stunned enemies"), value);
	case PassiveStat::DeadDoNotRest:
	case PassiveStat::KingsDeadPact:
	case PassiveStat::BartucsHunger:
	case PassiveStat::PaidInBlood:
	case PassiveStat::DarkSickness:
	case PassiveStat::HiveBreath:
	case PassiveStat::EyesInTheDark:
	case PassiveStat::OutOfDarkness:
		return PassiveStatLines(stat, value).front().text;
	default:
		return {};
	}
}

std::vector<PassiveStatLine> PassiveStatLines(PassiveStat stat, int value)
{
	const auto line = [](std::string_view text, bool price = false) { return PassiveStatLine { std::string(text), price }; };
	switch (stat) {
	case PassiveStat::DeadDoNotRest:
		return { line(_("The dead do not rest: 1 in 4 of your kills rise again as a skeleton, hostile, at half Life"), true),
			line(_("Culled monsters stay dead, and any hit in a rising skeleton's first second puts it down for good")) };
	case PassiveStat::KingsDeadPact:
		return { line(_("Increases to Two Handed Damage also apply to Bows, and Fresh Meat works with Bow kills")),
			line(_("The King's dead: monsters you Cull, and skeletons you put down, rise for you for 12 seconds")),
			line(_("1 in 3 of your other kills rise against you instead, at full Life"), true) };
	case PassiveStat::BartucsHunger:
		return { line(_("A quarter of your skills' Mana costs is paid in Life"), true) };
	case PassiveStat::PaidInBlood:
		return { line(_("Half your Magic counts as Strength, for damage and item requirements")),
			line(_("Each melee attack costs 1% of your maximum Life"), true) };
	case PassiveStat::DarkSickness:
		return { line(_("Your Kill Burst needs a monster carrying an ailment, and passes its ignite, shock, chill and poison on")),
			line(_("You carry it: each time your Kill Burst passes an ailment on, you catch the fever. For 2 seconds you can't ignite, shock, chill, freeze or poison anything, but the sickness keeps spreading without you"), true) };
	case PassiveStat::HiveBreath:
		return { line(_("75% of your increased Melee Damage also applies to your Spell hits")),
			line(_("Every death spreads it: a monster that dies carrying an ailment passes it to the enemies beside it")),
			line(_("Your spells, Aspects and curses can't freeze, ignite, shock, chill or poison, and deal 20% less to anything not already sick"), true) };
	case PassiveStat::EyesInTheDark:
		return { line(_("Eyes in the dark: you see monsters past your light as shapes. Hold Shift to shoot at shapes in the dark")),
			line(_("Your first hit on a monster that hasn't noticed you deals 40% more Damage again")),
			line(_("Your Light Radius can't exceed 7, and your darkness doesn't hide you: monsters within 10 tiles that have a clear line to you wake"), true) };
	case PassiveStat::OutOfDarkness:
		return { line(_("Increases to Bow Damage also apply to Staves")),
			line(_("Out of mind: while you wield a staff, no monster shoots or casts at you from more than 5 tiles away, or without a clear line, until it has attacked you once")),
			line(_("While you wield a staff, your Light Radius can't exceed 4, and each enemy's first attack on you can't be evaded and ignores your Armour"), true) };
	default:
		return { line(PassiveStatText(stat, value), value < 0) };
	}
}

std::vector<PassiveStatLine> ProfaneRoadEndLines(PassiveNodeId node)
{
	std::vector<PassiveStatLine> lines;
	for (size_t road = 0; road < Roads.size(); road++) {
		const std::optional<RoadEnds> &ends = Roads[road];
		if (!ends)
			continue;
		if (ends->door == node) {
			lines.push_back({ std::string(_("The door of a Profane Road: it is walked one way, from here")), true });
			if (static_cast<ProfaneRoad>(road) == ProfaneRoad::Sickness)
				lines.push_back({ std::string(_("This road's pact stops your spells, Aspects and curses from inflicting ailments, and weakens them against healthy enemies")), true });
		}
		if (ends->farGate == node)
			lines.push_back({ std::string(_("The far end of a Profane Road: the road can't be entered from here")), false });
	}
	return lines;
}

std::vector<std::pair<PassiveStat, int>> PassiveNodeStats(const Player &player, const PassiveNode &node)
{
	std::vector<std::pair<PassiveStat, int>> stats = node.stats;
	if (node.kind == PassiveKind::Attribute && IsPassiveAllocated(player, node.id)) {
		const PassiveStat chosen = PassiveAttributeStat(ChosenPassiveAttribute(player, node.id));
		for (auto &[stat, value] : stats) {
			if (stat == PassiveStat::AnyAttribute)
				stat = chosen;
		}
	}
	return stats;
}

std::string PassiveNodeName(const PassiveNode &node)
{
	if (node.kind == PassiveKind::Attribute)
		return std::string(_("Attribute"));
	if (node.kind == PassiveKind::JewelSocket)
		return std::string(_("Jewel Socket"));
	if (!node.name.empty())
		return node.kind == PassiveKind::Start ? StrCat(node.name, " ", _("Start")) : node.name;
	if (node.stats.empty())
		return {};
	return PassiveStatText(node.stats[0].first, node.stats[0].second);
}

} // namespace devilution
