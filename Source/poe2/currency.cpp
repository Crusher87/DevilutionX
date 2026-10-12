#include "poe2/currency.hpp"

#include <algorithm>

#include "engine/random.hpp"
#include "player.h"
#include "plrmsg.h"
#include "utils/format.hpp"
#include "utils/is_of.hpp"
#include "utils/language.h"

namespace devilution {

namespace {

/** How many seeds to try before giving up on rolling the wanted quality. */
constexpr int MaxItemRolls = 20000;

bool CanTakeCurrency(const Item &item)
{
	if (item.isEmpty() || item._iMagical == ITEM_QUALITY_UNIQUE)
		return false;
	return IsAnyOf(item._iClass, ICLASS_WEAPON, ICLASS_ARMOR) || IsAnyOf(item._itype, ItemType::Ring, ItemType::Amulet);
}

} // namespace

CurrencyOrb CurrencyOrbForMiscId(int miscId)
{
	switch (miscId) {
	case IMISC_ORB_TRANSMUTE:
		return CurrencyOrb::Transmutation;
	case IMISC_ORB_ALCHEMY:
		return CurrencyOrb::Alchemy;
	case IMISC_ORB_CHAOS:
		return CurrencyOrb::Chaos;
	case IMISC_ORB_SCOURING:
		return CurrencyOrb::Scouring;
	default:
		return CurrencyOrb::None;
	}
}

std::string_view CurrencyOrbDescription(CurrencyOrb orb)
{
	switch (orb) {
	case CurrencyOrb::Transmutation:
		return _("Turns a normal item magic");
	case CurrencyOrb::Alchemy:
		return _("Turns a normal or magic item rare");
	case CurrencyOrb::Chaos:
		return _("Rerolls a rare item's affixes");
	case CurrencyOrb::Scouring:
		return _("Removes all affixes from an item");
	default:
		return {};
	}
}

bool RollItemOfQuality(const Player &player, Item &item, _item_indexes baseIndex, item_quality quality, int level, uint32_t firstSeed, uint32_t avoidSeed)
{
	// Rolling items reseeds the game's random number generator, so put it back afterwards.
	const uint32_t rngState = GetLCGEngineState();

	const bool onlyGood = quality != ITEM_QUALITY_NORMAL;
	const int uper = quality == ITEM_QUALITY_UNIQUE ? 15 : 1;
	bool found = false;
	uint32_t seed = firstSeed;
	for (int i = 0; i < MaxItemRolls && !found; i++) {
		// A simple LCG step gives a fresh seed each try without touching the game's generator.
		seed = (seed * 1103515245U) + 12345U;
		const uint32_t itemSeed = seed >> 1;
		if (avoidSeed != 0 && itemSeed == avoidSeed)
			continue;
		Item candidate {};
		SetupAllItems(player, candidate, baseIndex, itemSeed, level, uper, onlyGood, false, 0, quality != ITEM_QUALITY_UNIQUE);
		if (candidate.isEmpty() || candidate._iMagical != quality)
			continue;
		SetupItem(candidate);
		item = std::move(candidate);
		found = true;
	}

	SetRndSeed(rngState);
	return found;
}

bool ApplyCurrencyOrb(CurrencyOrb orb, const Player &player, Item &item)
{
	if (!CanTakeCurrency(item)) {
		EventPlrMsg(_("Orbs work on weapons, armor and jewelry, but not uniques."), UiFlags::ColorRed);
		return false;
	}

	item_quality wanted;
	switch (orb) {
	case CurrencyOrb::Transmutation:
		if (item._iMagical != ITEM_QUALITY_NORMAL) {
			EventPlrMsg(_("Orb of Transmutation needs a normal item."), UiFlags::ColorRed);
			return false;
		}
		wanted = ITEM_QUALITY_MAGIC;
		break;
	case CurrencyOrb::Alchemy:
		if (IsNoneOf(item._iMagical, ITEM_QUALITY_NORMAL, ITEM_QUALITY_MAGIC)) {
			EventPlrMsg(_("Orb of Alchemy needs a normal or magic item."), UiFlags::ColorRed);
			return false;
		}
		wanted = ITEM_QUALITY_RARE;
		break;
	case CurrencyOrb::Chaos:
		if (item._iMagical != ITEM_QUALITY_RARE) {
			EventPlrMsg(_("Chaos Orb needs a rare item."), UiFlags::ColorRed);
			return false;
		}
		wanted = ITEM_QUALITY_RARE;
		break;
	case CurrencyOrb::Scouring:
		if (item._iMagical == ITEM_QUALITY_NORMAL || IsAnyOf(item._itype, ItemType::Ring, ItemType::Amulet)) {
			EventPlrMsg(_("Orb of Scouring needs a magic or rare weapon or armor."), UiFlags::ColorRed);
			return false;
		}
		wanted = ITEM_QUALITY_NORMAL;
		break;
	default:
		return false;
	}

	// Keep the item level it dropped at; starting gear has none, so use the character's.
	int level = item._iCreateInfo & CF_LEVEL;
	if (level == 0)
		level = std::max<int>(1, player.getCharacterLevel());

	Item rolled;
	const uint32_t firstSeed = (static_cast<uint32_t>(item._iSeed) * 31U) + static_cast<uint32_t>(orb);
	if (!RollItemOfQuality(player, rolled, item.IDidx, wanted, level, firstSeed, static_cast<uint32_t>(item._iSeed))) {
		EventPlrMsg(_("The orb fizzles. This item can't roll that."), UiFlags::ColorRed);
		return false;
	}

	rolled._iIdentified = true;
	rolled.position = item.position;
	item = std::move(rolled);
	EventPlrMsg(FormatRuntime(_("Crafted: {:s}"), item.getName().str()));
	return true;
}

} // namespace devilution
