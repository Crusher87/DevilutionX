#include "poe2/monster_resistance.hpp"

#include <algorithm>
#include <string_view>

#include "game_mode.hpp"
#include "monster.h"
#include "poe2/map_play.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/unique_effects.hpp"
#include "tables/misdat.h"
#include "tables/monstdat.h"
#include "utils/is_of.hpp"
#include "utils/language.h"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

bool IsResistedElement(DamageType type)
{
	return IsAnyOf(type, DamageType::Fire, DamageType::Lightning, DamageType::Magic);
}

} // namespace

int ResistanceMarkPercent(uint16_t marks, DamageType type)
{
	switch (type) {
	case DamageType::Fire:
		return (marks & IMMUNE_FIRE) != 0 ? ImmuneMarkPercent : ((marks & RESIST_FIRE) != 0 ? ResistMarkPercent : 0);
	case DamageType::Lightning:
		return (marks & IMMUNE_LIGHTNING) != 0 ? ImmuneMarkPercent : ((marks & RESIST_LIGHTNING) != 0 ? ResistMarkPercent : 0);
	case DamageType::Magic:
		return (marks & IMMUNE_MAGIC) != 0 ? MagicImmuneMarkPercent : ((marks & RESIST_MAGIC) != 0 ? MagicResistMarkPercent : 0);
	default:
		return 0;
	}
}

std::string ResistanceMarksText(uint16_t marks)
{
	std::string text;
	for (const DamageType type : { DamageType::Fire, DamageType::Lightning, DamageType::Magic }) {
		const int percent = ResistanceMarkPercent(marks, type);
		if (percent == 0)
			continue;
		const std::string_view name = type == DamageType::Fire ? _("Fire") : (type == DamageType::Lightning ? _("Lightning") : _("Magic"));
		StrAppend(text, text.empty() ? "" : ", ", name, " ", percent, "%");
	}
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
	if (!gbIsPoe2 || !IsResistedElement(type))
		return 0;
	int resistance = ResistanceMarkPercent(monster.resistance, type);
	if (GetMonsterAffixes(monster).has(MonsterMod::Resistant))
		resistance += ResistantModPercent;
	// Waystones can give map monsters more elemental resistance.
	if (!monster.isPlayerMinion())
		resistance += MapMonsterResistance();
	return resistance;
}

int MonsterResistanceLowered(const Monster &monster, DamageType type)
{
	if (!gbIsPoe2 || !IsResistedElement(type))
		return 0;
	int lowered = 0;
	if (monster.poe2.lowerResistTicks > 0)
		lowered += LowerResistCursePercent;
	// Only the strongest exposure of an element applies, and a monster has one at a time.
	if (monster.poe2.exposureTicks > 0 && static_cast<uint8_t>(type) == monster.poe2.exposureElement)
		lowered += ExposurePercent * MapExposurePercent() / 100;
	return lowered * CurseEffectPercent(monster) / 100;
}

int MonsterResistance(const Monster &monster, DamageType type)
{
	if (!gbIsPoe2 || !IsResistedElement(type))
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
