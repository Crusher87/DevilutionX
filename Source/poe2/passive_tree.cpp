#include "poe2/passive_tree.hpp"

#include <algorithm>

#include <magic_enum/magic_enum.hpp>

#include "data/file.hpp"
#include "data/record_reader.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "multi.h"
#include "player.h"
#include "utils/format.hpp"
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

/** @brief Whether every allocated node is linked to the start without going through `without`. */
bool AllConnected(const Player &player, std::optional<uint8_t> without)
{
	const std::optional<uint8_t> start = PassiveStartNode(player._pClass);
	if (!start)
		return player.poe2.passives.none();
	std::bitset<MaxPassiveNodes> reached;
	std::vector<uint8_t> open { *start };
	reached.set(*start);
	while (!open.empty()) {
		const uint8_t node = open.back();
		open.pop_back();
		for (const uint8_t next : Tree[node].links) {
			if (reached.test(next) || next == without || !player.poe2.passives.test(next))
				continue;
			reached.set(next);
			open.push_back(next);
		}
	}
	for (size_t node = 0; node < Tree.size(); node++) {
		if (player.poe2.passives.test(node) && node != without && !reached.test(node))
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
		node.id = static_cast<uint8_t>(*id);
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
				node.links.push_back(static_cast<uint8_t>(*target));
		}
		Tree.push_back(std::move(node));
	}
	// Drop links to nodes that don't exist.
	for (PassiveNode &node : Tree) {
		std::erase_if(node.links, [](uint8_t link) { return link >= Tree.size(); });
	}
}

const std::vector<PassiveNode> &PassiveTree()
{
	return Tree;
}

std::optional<uint8_t> PassiveStartNode(HeroClass heroClass)
{
	for (const PassiveNode &node : Tree) {
		if (node.kind == PassiveKind::Start && node.startClass == heroClass)
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
	return static_cast<int>(player.poe2.passives.count());
}

int PassivePointsLeft(const Player &player)
{
	return PassivePointsTotal(player) - PassivePointsSpent(player);
}

bool IsPassiveAllocated(const Player &player, uint8_t node)
{
	if (node >= Tree.size())
		return false;
	if (Tree[node].kind == PassiveKind::Start)
		return Tree[node].startClass == player._pClass;
	return player.poe2.passives.test(node);
}

bool CanAllocatePassive(const Player &player, uint8_t node)
{
	if (!CanUsePassives() || node >= Tree.size() || IsPassiveAllocated(player, node))
		return false;
	if (Tree[node].kind == PassiveKind::Start || PassivePointsLeft(player) <= 0)
		return false;
	return std::any_of(Tree[node].links.begin(), Tree[node].links.end(), [&](uint8_t link) { return IsPassiveAllocated(player, link); });
}

bool AllocatePassive(Player &player, uint8_t node)
{
	if (!CanAllocatePassive(player, node))
		return false;
	player.poe2.passives.set(node);
	CalcPlrInv(player, true);
	return true;
}

bool CanRefundPassive(const Player &player, uint8_t node)
{
	if (!CanUsePassives() || node >= Tree.size() || Tree[node].kind == PassiveKind::Start || !player.poe2.passives.test(node))
		return false;
	return AllConnected(player, node);
}

bool RefundPassive(Player &player, uint8_t node)
{
	if (!CanRefundPassive(player, node))
		return false;
	player.poe2.passives.reset(node);
	CalcPlrInv(player, true);
	return true;
}

void ResetPassives(Player &player)
{
	player.poe2.passives.reset();
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
		for (const auto &[stat, value] : node.stats)
			totals[static_cast<size_t>(stat)] += value;
	}
	return totals;
}

std::string PassiveStatText(PassiveStat stat, int value)
{
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
		return FormatRuntime(_(/*xgettext:no-c-format*/ "{:d}% increased maximum Life"), value);
	case PassiveStat::ManaPercent:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "{:d}% increased maximum Mana"), value);
	case PassiveStat::Damage:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "{:d}% increased Damage"), value);
	case PassiveStat::ToHit:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "{:+d}% to hit"), value);
	case PassiveStat::ArmorPercent:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "{:d}% increased Armor"), value);
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
		return FormatRuntime(_(/*xgettext:no-c-format*/ "{:d}% increased Mana Regeneration"), value);
	case PassiveStat::SpellLevels:
		return FormatRuntime(_("{:+d} to all Spell Levels"), value);
	case PassiveStat::AttackSpeed:
		return value > 1 ? std::string(_("Much faster Attacks")) : std::string(_("Faster Attacks"));
	case PassiveStat::CastSpeed:
		return value > 1 ? std::string(_("Much faster Casting")) : std::string(_("Faster Casting"));
	case PassiveStat::EvasionPercent:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "{:d}% increased Evasion"), value);
	case PassiveStat::EnergyShieldPercent:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "{:d}% increased maximum Energy Shield"), value);
	default:
		return {};
	}
}

std::string PassiveNodeName(const PassiveNode &node)
{
	if (!node.name.empty())
		return node.kind == PassiveKind::Start ? StrCat(node.name, " ", _("Start")) : node.name;
	if (node.stats.empty())
		return {};
	return PassiveStatText(node.stats[0].first, node.stats[0].second);
}

} // namespace devilution
