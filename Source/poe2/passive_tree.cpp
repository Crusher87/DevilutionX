#include "poe2/passive_tree.hpp"

#include <algorithm>

#include <magic_enum/magic_enum.hpp>

#include "data/file.hpp"
#include "data/record_reader.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "multi.h"
#include "player.h"
#include "poe2/jewels.hpp"
#include "poe2/weapon_swap.hpp"
#include "utils/format.hpp"
#include "utils/is_of.hpp"
#include "utils/language.h"
#include "utils/log.hpp"
#include "utils/str_cat.hpp"
#include "utils/str_split.hpp"

namespace devilution {

namespace {

std::vector<PassiveNode> Tree;

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
			if (reached.test(next) || next == without || !nodes.test(next))
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
	if (!gbIsPoe2)
		return;

	constexpr std::string_view Filename = "txtdata\\poe2\\passive_tree.tsv";
	auto dataFile = DataFile::load(Filename);
	if (!dataFile.has_value()) {
		LogError("Couldn't load the passive tree: {}", Filename);
		return;
	}
	dataFile->skipHeaderOrDie(Filename);
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
}

const std::vector<PassiveNode> &PassiveTree()
{
	return Tree;
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

int PassivePointsTotal(const Player &player)
{
	return std::max(player.getCharacterLevel() - 1, 0);
}

int PassivePointsSpent(const Player &player)
{
	int spent = 0;
	for (const PassiveNode &node : Tree) {
		if (!IsAscendancyNode(node) && player.poe2.passives.test(node.id))
			spent++;
	}
	return spent;
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
	int spent = 0;
	for (const PassiveNode &node : Tree) {
		if (node.kind == PassiveKind::AscendancyNotable && player.poe2.passives.test(node.id))
			spent++;
	}
	return spent;
}

int AscendancyPointsLeft(const Player &player)
{
	return AscendancyPointsTotal(player) - AscendancyPointsSpent(player);
}

std::optional<PassiveNodeId> ChosenAscendancy(const Player &player)
{
	for (const PassiveNode &node : Tree) {
		if (node.kind == PassiveKind::Ascendancy && player.poe2.passives.test(node.id))
			return node.id;
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
	return PoolNodes(player, pool).test(node);
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
	return IsAnyOf(node.kind, PassiveKind::Small, PassiveKind::Notable, PassiveKind::Attribute);
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

bool CanAllocatePassive(const Player &player, PassiveNodeId node, PassivePool pool)
{
	if (!CanUsePassives() || node >= Tree.size() || IsPassiveAllocatedIn(player, node, pool))
		return false;
	const PassiveNode &passive = Tree[node];
	if (passive.kind == PassiveKind::Start)
		return false;
	if (pool != PassivePool::Main) {
		if (!CanTakeWeaponSetPoints(passive) || WeaponSetPointsLeft(player, pool) <= 0)
			return false;
		return std::any_of(passive.links.begin(), passive.links.end(), [&](PassiveNodeId link) { return IsPassiveAllocatedIn(player, link, pool); });
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
	return std::any_of(Tree[node].links.begin(), Tree[node].links.end(), [&](PassiveNodeId link) { return IsPassiveAllocatedIn(player, link, PassivePool::Main); });
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
	player.poe2.passiveAttributes[node] = PassiveNeedsAttributeChoice(node) ? static_cast<uint8_t>(attribute) : 0;
	CalcPlrInv(player, true);
	return true;
}

PassiveAttribute ChosenPassiveAttribute(const Player &player, PassiveNodeId node)
{
	if (node >= MaxPassiveNodes || player.poe2.passiveAttributes[node] >= static_cast<uint8_t>(PassiveAttribute::Count))
		return PassiveAttribute::Strength;
	return static_cast<PassiveAttribute>(player.poe2.passiveAttributes[node]);
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
	if (!player.poe2.passives.test(node) && !player.poe2.setPassives[0].test(node) && !player.poe2.setPassives[1].test(node))
		player.poe2.passiveAttributes[node] = 0;
	CalcPlrInv(player, true);
	return true;
}

void ResetPassives(Player &player)
{
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

std::string PassiveStatText(PassiveStat stat, int value)
{
	// Shrines take as well as give: their negative percentages read as "reduced".
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
		return value > 1 ? std::string(_("Much faster Attacks")) : std::string(_("Faster Attacks"));
	case PassiveStat::CastSpeed:
		return value > 1 ? std::string(_("Much faster Casting")) : std::string(_("Faster Casting"));
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
	default:
		return {};
	}
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
