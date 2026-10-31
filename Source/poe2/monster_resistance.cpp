#include "poe2/monster_resistance.hpp"

#include <algorithm>
#include <optional>
#include <string_view>
#include <unordered_map>

#include <magic_enum/magic_enum.hpp>

#include "data/file.hpp"
#include "data/record_reader.hpp"
#include "game_mode.hpp"
#include "monster.h"
#include "player.h"
#include "poe2/item_stats.hpp"
#include "poe2/map_play.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/table_skills.hpp"
#include "poe2/unique_effects.hpp"
#include "tables/misdat.h"
#include "tables/monstdat.h"
#include "utils/is_of.hpp"
#include "utils/language.h"
#include "utils/log.hpp"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

/** By _monster_id; kinds not listed have none. */
std::unordered_map<int, int> ChaosResistances;

bool IsResistedElement(DamageType type)
{
	return IsAnyOf(type, DamageType::Fire, DamageType::Lightning, DamageType::Cold);
}

bool IsResisted(DamageType type)
{
	return IsResistedElement(type) || type == DamageType::Chaos;
}

} // namespace

int ResistanceMarkPercent(uint16_t marks, DamageType type, bool undead)
{
	switch (type) {
	case DamageType::Fire:
		return (marks & IMMUNE_FIRE) != 0 ? ImmuneMarkPercent : ((marks & RESIST_FIRE) != 0 ? ResistMarkPercent : 0);
	case DamageType::Lightning:
		return (marks & IMMUNE_LIGHTNING) != 0 ? ImmuneMarkPercent : ((marks & RESIST_LIGHTNING) != 0 ? ResistMarkPercent : 0);
	case DamageType::Cold:
		if ((marks & IMMUNE_MAGIC) != 0)
			return undead ? UndeadColdImmuneMarkPercent : ColdImmuneMarkPercent;
		return (marks & RESIST_MAGIC) != 0 ? ColdResistMarkPercent : 0;
	default:
		return 0;
	}
}

void LoadMonsterChaosResistances()
{
	ChaosResistances.clear();
	if (!gbIsPoe2)
		return;
	constexpr std::string_view Filename = "txtdata\\poe2\\monster_chaos_resistance.tsv";
	auto dataFile = DataFile::load(Filename);
	if (!dataFile.has_value()) {
		// Every monster has 0% chaos resistance without the table.
		LogError("Couldn't load the monster chaos resistances: {}", Filename);
		return;
	}
	dataFile->skipHeaderOrDie(Filename);
	for (DataFileRecord record : *dataFile) {
		RecordReader reader { record, Filename };
		std::string name;
		int chaos = 0;
		reader.readString("type", name);
		reader.readInt("chaosResistance", chaos);
		const std::optional<_monster_id> type = magic_enum::enum_cast<_monster_id>(name);
		if (!type) {
			LogError("Unknown monster type {} in {}", name, Filename);
			continue;
		}
		ChaosResistances[static_cast<int>(*type)] = std::clamp(chaos, MinMonsterResistance, MaxMonsterResistance);
	}
}

int MonsterTypeChaosResistance(int monsterType)
{
	if (!gbIsPoe2)
		return 0;
	const auto it = ChaosResistances.find(monsterType);
	return it != ChaosResistances.end() ? it->second : 0;
}

std::string_view ResistanceName(DamageType type)
{
	switch (type) {
	case DamageType::Fire:
		return _("Fire");
	case DamageType::Lightning:
		return _("Lightning");
	case DamageType::Cold:
		return gbIsPoe2 ? _("Cold") : _("Magic");
	case DamageType::Chaos:
		return _("Chaos");
	default:
		return _("Physical");
	}
}

std::string ResistanceMarksText(uint16_t marks, bool undead, int chaos)
{
	std::string text;
	for (const DamageType type : { DamageType::Fire, DamageType::Lightning, DamageType::Cold }) {
		const int percent = ResistanceMarkPercent(marks, type, undead);
		if (percent == 0)
			continue;
		const std::string_view name = ResistanceName(type);
		StrAppend(text, text.empty() ? "" : ", ", name, " ", percent, "%");
	}
	if (chaos != 0)
		StrAppend(text, text.empty() ? "" : ", ", _("Chaos"), " ", chaos, "%");
	return text.empty() ? std::string(_("No resistances")) : StrCat(_("Resistances: "), text);
}

int CurseEffectPercent(const Monster &monster)
{
	if (monster.isUnique())
		return 50;
	switch (GetMonsterAffixes(monster).rarity) {
	case MonsterRarity::Rare:
		return 70;
	case MonsterRarity::Magic:
		return 85;
	default:
		return 100;
	}
}

int MonsterBaseResistance(const Monster &monster, DamageType type)
{
	if (!gbIsPoe2)
		return 0;
	// Chaos resistance is the kind's alone: no modifier, waystone or minion gear raises it.
	if (type == DamageType::Chaos)
		return MonsterTypeChaosResistance(static_cast<int>(monster.type().type));
	if (!IsResistedElement(type))
		return 0;
	// Only a magic immune mark asks the monster's kind, which needs its data.
	const bool undead = type == DamageType::Cold && (monster.resistance & IMMUNE_MAGIC) != 0 && monster.data().monsterClass == MonsterClass::Undead;
	int resistance = ResistanceMarkPercent(monster.resistance, type, undead);
	if (GetMonsterAffixes(monster).has(MonsterMod::Resistant))
		resistance += ResistantModPercent;
	// Waystones can give map monsters more elemental resistance.
	if (!monster.isPlayerMinion())
		resistance += MapMonsterResistance();
	else if (monster.goalVar3 >= 0 && static_cast<size_t>(monster.goalVar3) < Players.size())
		resistance += Players[static_cast<size_t>(monster.goalVar3)].poe2.itemStats[static_cast<size_t>(Poe2ItemStat::MinionResistances)];
	return resistance;
}

int MonsterResistanceLowered(const Monster &monster, DamageType type)
{
	if (!gbIsPoe2)
		return 0;
	// Despair lowers chaos resistance, scaled down on magic, rare and unique monsters like Lower Resist.
	if (type == DamageType::Chaos) {
		const int lowered = monster.poe2.despairTicks > 0 ? DespairCursePercent * TableCurseStrength(monster, TableCurse::Despair) / 100 : 0;
		return lowered * CurseEffectPercent(monster) / 100;
	}
	// Lower Resist and exposure are elemental: they leave chaos resistance alone.
	if (!IsResistedElement(type))
		return 0;
	int lowered = 0;
	if (monster.poe2.lowerResistTicks > 0)
		lowered += LowerResistCursePercent * TableCurseStrength(monster, TableCurse::LowerResist) / 100;
	// Only the strongest exposure of an element applies, and a monster has one at a time.
	if (monster.poe2.exposureTicks > 0 && static_cast<uint8_t>(type) == monster.poe2.exposureElement)
		lowered += ExposurePercent * MapExposurePercent() / 100;
	return lowered * CurseEffectPercent(monster) / 100;
}

int MonsterResistance(const Monster &monster, DamageType type)
{
	if (!gbIsPoe2 || !IsResisted(type))
		return 0;
	// Curses and exposure lower the uncapped resistance, so on a monster over the cap part of them is lost.
	const int uncapped = MonsterBaseResistance(monster, type) - MonsterResistanceLowered(monster, type);
	return std::clamp(uncapped, MinMonsterResistance, MaxMonsterResistance);
}

int MonsterHitResistance(const Monster &monster, DamageType type, int penetration)
{
	const int resistance = MonsterResistance(monster, type);
	if (resistance <= 0 || penetration <= 0)
		return resistance;
	return std::max(resistance - penetration, 0);
}

} // namespace devilution
