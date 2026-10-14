#include "poe2/currency.hpp"

#include <algorithm>

#include "engine/random.hpp"
#include "player.h"
#include "plrmsg.h"
#include "poe2/flasks.hpp"
#include "poe2/loot.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/telemetry.hpp"
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
	return IsAnyOf(item._iClass, ICLASS_WEAPON, ICLASS_ARMOR) || IsAnyOf(item._itype, ItemType::Ring, ItemType::Amulet) || IsJewel(item);
}

bool HasPower(const Item &item, item_effect_type power)
{
	if (power == IPL_INVALID)
		return true;
	if (item._iMagical == ITEM_QUALITY_RARE)
		return std::find(item._iRarePowers.begin(), item._iRarePowers.end(), power) != item._iRarePowers.end();
	return item._iPrePower == power || item._iSufPower == power;
}

int ModAffixCount(const Item &item)
{
	return static_cast<int>(std::count_if(item._iPoe2Mods.begin(), item._iPoe2Mods.end(), [](const Poe2ItemMod &mod) { return mod.family != 0; }));
}

/** @brief Whether the candidate has every modifier family, at the same tier, that the item had. */
bool KeepsMods(const Item &candidate, const std::array<Poe2ItemMod, MaxRareAffixes> &mods)
{
	return std::all_of(mods.begin(), mods.end(), [&candidate](const Poe2ItemMod &mod) {
		return mod.family == 0 || std::any_of(candidate._iPoe2Mods.begin(), candidate._iPoe2Mods.end(), [&mod](const Poe2ItemMod &other) { return other.family == mod.family && other.tier == mod.tier; });
	});
}

uint32_t Mix(uint32_t value)
{
	value ^= value >> 16;
	value *= 0x7FEB352DU;
	value ^= value >> 15;
	value *= 0x846CA68BU;
	value ^= value >> 16;
	return value;
}

/** @brief Rebuilds the item from its seed with new crafting bits. */
Item RebuildWithBuff(const Player &player, const Item &item, uint32_t dwBuff)
{
	const uint32_t rngState = GetLCGEngineState();
	Item rebuilt;
	RecreateItem(player, rebuilt, item.IDidx, item._iCreateInfo, item._iSeed, item._ivalue, dwBuff);
	SetRndSeed(rngState);
	rebuilt._iIdentified = true;
	rebuilt.position = item.position;
	rebuilt._iDurability = std::min<int>(item._iDurability, rebuilt._iMaxDur);
	return rebuilt;
}

/** @brief A rerolled item keeps the POE2 item level it dropped with. */
uint32_t KeptItemLevelBits(const Item &item)
{
	return GetPoe2ItemLevel(item) > 0 ? item.dwBuff & (CF_POE2_ILVL | CF_POE2_MODS) : 0;
}

int ItemLevel(const Player &player, const Item &item)
{
	// Keep the item level it dropped at; starting gear has none, so use the character's.
	const int level = item._iCreateInfo & CF_LEVEL;
	return level != 0 ? level : std::max<int>(1, player.getCharacterLevel());
}

FlaskPrefix RandomFlaskPrefix(uint32_t roll)
{
	return static_cast<FlaskPrefix>(1 + (roll % (static_cast<uint32_t>(FlaskPrefix::Count) - 1)));
}

FlaskSuffix RandomFlaskSuffix(uint32_t roll)
{
	return static_cast<FlaskSuffix>(1 + (roll % (static_cast<uint32_t>(FlaskSuffix::Count) - 1)));
}

/** @brief Orbs on flasks, which can only be normal or magic, as in POE2. */
bool ApplyCurrencyToFlask(CurrencyOrb orb, Item &item)
{
	FlaskPrefix prefix = GetFlaskPrefix(item);
	FlaskSuffix suffix = GetFlaskSuffix(item);
	int quality = GetFlaskQuality(item);
	const bool magic = prefix != FlaskPrefix::None || suffix != FlaskSuffix::None;
	// Mixing in the current mods gives a different roll each time an orb is used.
	const uint32_t roll = Mix(static_cast<uint32_t>(item._iSeed) ^ item.dwBuff ^ (static_cast<uint32_t>(orb) * 0x9E3779B9U));
	switch (orb) {
	case CurrencyOrb::Transmutation:
		if (magic) {
			EventPlrMsg(_("Orb of Transmutation needs a normal flask."), UiFlags::ColorRed);
			return false;
		}
		if (roll % 2 == 0)
			prefix = RandomFlaskPrefix(roll / 2);
		else
			suffix = RandomFlaskSuffix(roll / 2);
		break;
	case CurrencyOrb::Augmentation:
		if (!magic || (prefix != FlaskPrefix::None && suffix != FlaskSuffix::None)) {
			EventPlrMsg(_("Orb of Augmentation needs a magic flask with one mod."), UiFlags::ColorRed);
			return false;
		}
		if (prefix == FlaskPrefix::None)
			prefix = RandomFlaskPrefix(roll);
		else
			suffix = RandomFlaskSuffix(roll);
		break;
	case CurrencyOrb::Scouring:
		if (!magic) {
			EventPlrMsg(_("This flask has no mods to remove."), UiFlags::ColorRed);
			return false;
		}
		prefix = FlaskPrefix::None;
		suffix = FlaskSuffix::None;
		break;
	case CurrencyOrb::Bauble:
		if (quality >= MaxFlaskQuality) {
			EventPlrMsg(_("This flask is already at 20% quality."), UiFlags::ColorRed);
			return false;
		}
		quality++;
		break;
	default:
		EventPlrMsg(_("Flasks only take Transmutation, Augmentation, Scouring and Glassblower's Baubles."), UiFlags::ColorRed);
		return false;
	}
	SetFlaskMods(item, prefix, suffix, quality);
	EventPlrMsg(FormatRuntime(_("Crafted: {:s}"), item.getName().str()));
	return true;
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
	case IMISC_ORB_AUGMENT:
		return CurrencyOrb::Augmentation;
	case IMISC_ORB_REGAL:
		return CurrencyOrb::Regal;
	case IMISC_ORB_EXALTED:
		return CurrencyOrb::Exalted;
	case IMISC_ORB_DIVINE:
		return CurrencyOrb::Divine;
	case IMISC_ORB_VAAL:
		return CurrencyOrb::Vaal;
	case IMISC_ORB_BAUBLE:
		return CurrencyOrb::Bauble;
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
	case CurrencyOrb::Augmentation:
		return _("Adds an affix to a magic item with one");
	case CurrencyOrb::Regal:
		return _("Turns a magic item rare, keeping its affixes");
	case CurrencyOrb::Exalted:
		return _("Adds an affix to a rare item");
	case CurrencyOrb::Divine:
		return _("Rerolls the values of a rare item's affixes");
	case CurrencyOrb::Vaal:
		return _("Corrupts an item, with unpredictable results");
	case CurrencyOrb::Bauble:
		return _("Adds 1% quality to a flask, up to 20%");
	default:
		return {};
	}
}

ItemPower CorruptionImplicitPower(int kind)
{
	switch (kind) {
	case 1:
		return { IPL_ALLRES, 15, 15 };
	case 2:
		return { IPL_LIFE, 20, 20 };
	case 3:
		return { IPL_ATTRIBS, 5, 5 };
	default:
		return {};
	}
}

int RareAffixCount(const Item &item)
{
	if (item._iPoe2Mods[0].family != 0)
		return ModAffixCount(item);
	return static_cast<int>(std::count_if(item._iRarePowers.begin(), item._iRarePowers.end(), [](item_effect_type power) { return power != IPL_INVALID; }));
}

bool RollItemOfQuality(const Player &player, Item &item, _item_indexes baseIndex, item_quality quality, int level, uint32_t firstSeed, uint32_t avoidSeed, const std::function<bool(const Item &)> &accept, uint32_t keepBuff)
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
		candidate.dwBuff = keepBuff;
		SetupAllItems(player, candidate, baseIndex, itemSeed, level, uper, onlyGood, false, 0, quality != ITEM_QUALITY_UNIQUE);
		if (candidate.isEmpty() || candidate._iMagical != quality || (accept && !accept(candidate)))
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
	if (IsFlaskPotion(item) && (item.dwBuff & CF_POE2) != 0)
		return ApplyCurrencyToFlask(orb, item);
	if (orb == CurrencyOrb::Bauble) {
		EventPlrMsg(_("Glassblower's Baubles only work on flasks."), UiFlags::ColorRed);
		return false;
	}
	if (!CanTakeCurrency(item)) {
		EventPlrMsg(_("Orbs work on weapons, armor and jewelry, but not uniques."), UiFlags::ColorRed);
		return false;
	}

	if ((item.dwBuff & CF_POE2_CORRUPTED) != 0) {
		EventPlrMsg(_("Corrupted items can't be crafted."), UiFlags::ColorRed);
		return false;
	}

	switch (orb) {
	case CurrencyOrb::Exalted: {
		if (item._iMagical != ITEM_QUALITY_RARE) {
			EventPlrMsg(_("Exalted Orb needs a rare item."), UiFlags::ColorRed);
			return false;
		}
		const int affixes = RareAffixCount(item);
		const uint32_t exalts = (item.dwBuff & CF_POE2_EXALTS) >> 6;
		if (affixes >= MaxExaltedAffixes || exalts >= 7) {
			EventPlrMsg(_("This item can't have more affixes."), UiFlags::ColorRed);
			return false;
		}
		Item exalted = RebuildWithBuff(player, item, (item.dwBuff & ~CF_POE2_EXALTS) | ((exalts + 1) << 6));
		if (RareAffixCount(exalted) <= affixes) {
			EventPlrMsg(_("The orb fizzles. No more affixes fit this item."), UiFlags::ColorRed);
			return false;
		}
		item = std::move(exalted);
		EventPlrMsg(FormatRuntime(_("Crafted: {:s}"), item.getName().str()));
		return true;
	}
	case CurrencyOrb::Divine: {
		if (item._iMagical != ITEM_QUALITY_RARE) {
			EventPlrMsg(_("Divine Orb needs a rare item."), UiFlags::ColorRed);
			return false;
		}
		const uint32_t divine = (item.dwBuff & CF_POE2_DIVINE) >> 9;
		const uint32_t next = divine >= 15 ? 1 : divine + 1;
		item = RebuildWithBuff(player, item, (item.dwBuff & ~CF_POE2_DIVINE) | (next << 9));
		EventPlrMsg(FormatRuntime(_("Crafted: {:s}"), item.getName().str()));
		return true;
	}
	case CurrencyOrb::Vaal: {
		const uint32_t roll = Mix(static_cast<uint32_t>(item._iSeed) ^ item.dwBuff ^ 0x5AA1u);
		uint32_t dwBuff = item.dwBuff | CF_POE2_CORRUPTED;
		switch (roll % 4) {
		case 0:
			EventPlrMsg(_("Corrupted. Nothing else happens."));
			break;
		case 1:
		case 2: {
			const uint32_t kind = 1 + ((roll / 4) % 3);
			dwBuff |= kind << 14;
			EventPlrMsg(_("Corrupted. The item gains an implicit."));
			break;
		}
		default:
			if (item._iMagical != ITEM_QUALITY_NORMAL) {
				Item rerolled;
				if (RollItemOfQuality(player, rerolled, item.IDidx, static_cast<item_quality>(item._iMagical), ItemLevel(player, item), roll, static_cast<uint32_t>(item._iSeed), {}, KeptItemLevelBits(item))) {
					rerolled.dwBuff |= CF_POE2_CORRUPTED;
					rerolled.position = item.position;
					item = RebuildWithBuff(player, rerolled, rerolled.dwBuff);
					EventPlrMsg(FormatRuntime(_("Corrupted. The item became {:s}"), item.getName().str()));
					return true;
				}
			}
			EventPlrMsg(_("Corrupted. Nothing else happens."));
			break;
		}
		item = RebuildWithBuff(player, item, dwBuff);
		return true;
	}
	default:
		break;
	}

	item_quality wanted;
	std::function<bool(const Item &)> accept;
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
	case CurrencyOrb::Augmentation: {
		if (item._iPoe2Mods[0].family != 0) {
			if (item._iMagical != ITEM_QUALITY_MAGIC || ModAffixCount(item) != 1) {
				EventPlrMsg(_("Orb of Augmentation needs a magic item with one affix."), UiFlags::ColorRed);
				return false;
			}
			wanted = ITEM_QUALITY_MAGIC;
			accept = [mods = item._iPoe2Mods](const Item &candidate) { return ModAffixCount(candidate) == 2 && KeepsMods(candidate, mods); };
			break;
		}
		if (item._iMagical != ITEM_QUALITY_MAGIC || (item._iPrePower != IPL_INVALID && item._iSufPower != IPL_INVALID)) {
			EventPlrMsg(_("Orb of Augmentation needs a magic item with one affix."), UiFlags::ColorRed);
			return false;
		}
		wanted = ITEM_QUALITY_MAGIC;
		const item_effect_type prefix = item._iPrePower;
		const item_effect_type suffix = item._iSufPower;
		accept = [prefix, suffix](const Item &candidate) {
			return candidate._iPrePower != IPL_INVALID && candidate._iSufPower != IPL_INVALID && HasPower(candidate, prefix) && HasPower(candidate, suffix);
		};
		break;
	}
	case CurrencyOrb::Regal: {
		if (item._iMagical != ITEM_QUALITY_MAGIC) {
			EventPlrMsg(_("Regal Orb needs a magic item."), UiFlags::ColorRed);
			return false;
		}
		wanted = ITEM_QUALITY_RARE;
		if (item._iPoe2Mods[0].family != 0) {
			accept = [mods = item._iPoe2Mods](const Item &candidate) { return KeepsMods(candidate, mods); };
			break;
		}
		const item_effect_type prefix = item._iPrePower;
		const item_effect_type suffix = item._iSufPower;
		accept = [prefix, suffix](const Item &candidate) { return HasPower(candidate, prefix) && HasPower(candidate, suffix); };
		break;
	}
	default:
		return false;
	}

	const int level = ItemLevel(player, item);

	Item rolled;
	const uint32_t firstSeed = (static_cast<uint32_t>(item._iSeed) * 31U) + static_cast<uint32_t>(orb);
	if (!RollItemOfQuality(player, rolled, item.IDidx, wanted, level, firstSeed, static_cast<uint32_t>(item._iSeed), accept, KeptItemLevelBits(item))) {
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
