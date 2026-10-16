#include "poe2/currency.hpp"

#include <algorithm>

#include "engine/random.hpp"
#include "player.h"
#include "plrmsg.h"
#include "poe2/bases.hpp"
#include "poe2/craft_state.hpp"
#include "poe2/crafting_items.hpp"
#include "poe2/desecration.hpp"
#include "poe2/endgame.hpp"
#include "poe2/flasks.hpp"
#include "poe2/loot.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/stacking.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/waystones.hpp"
#include "utils/format.hpp"
#include "utils/is_of.hpp"
#include "utils/language.h"

namespace devilution {

bool CurrencyDestroyedItem = false;
bool DesecrationRevealItem = false;

namespace {

/** Omens a crafting action used, removed from the inventory once the action is done. */
std::vector<OmenId> PendingOmens;

/** How many seeds to try before giving up on rolling the wanted quality. */
constexpr int MaxItemRolls = 20000;

/** @brief Weapons, armour, jewellery and jewels take currency; uniques only some of it. */
bool IsCraftableKind(const Item &item)
{
	if (item.isEmpty())
		return false;
	return IsAnyOf(item._iClass, ICLASS_WEAPON, ICLASS_ARMOR) || IsAnyOf(item._itype, ItemType::Ring, ItemType::Amulet) || IsJewel(item);
}

bool CanTakeCurrency(const Item &item)
{
	return IsCraftableKind(item) && item._iMagical != ITEM_QUALITY_UNIQUE;
}

/** @brief Items bought from a vendor rebuild from the vendor's roll, which can't carry crafting state. */
bool IsVendorItem(const Item &item)
{
	// Both CF_UPER bits mark a "useful" item; one alone is an ordinary monster drop.
	return (item._iCreateInfo & CF_TOWN) != 0 || (item._iCreateInfo & CF_USEFUL) == CF_USEFUL;
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
	return RebuildWithCraft(player, item, dwBuff, item._iPoe2Craft);
}

/** @brief A rerolled item keeps the POE2 item level it dropped with. */
uint32_t KeptItemLevelBits(const Item &item)
{
	return GetPoe2ItemLevel(item) > 0 ? item.dwBuff & (CF_POE2_ILVL | CF_POE2_MODS | CF_POE2_QUALITY) : 0;
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
		// Curtis's rule (2026-10-16): quality currency adds 2% to normal and magic items.
		quality = std::min(quality + 2, MaxFlaskQuality);
		break;
	default:
		EventPlrMsg(_("Flasks only take Transmutation, Augmentation, Scouring and Glassblower's Baubles."), UiFlags::ColorRed);
		return false;
	}
	SetFlaskMods(item, prefix, suffix, quality);
	EventPlrMsg(FormatRuntime(_("Crafted: {:s}"), item.getName().str()));
	return true;
}

void Fizzle(std::string_view message)
{
	EventPlrMsg(message, UiFlags::ColorRed);
}

void Crafted(const Item &item)
{
	EventPlrMsg(FormatRuntime(_("Crafted: {:s}"), item.getName().str()));
}

/** @brief A seed for a crafting roll that changes with everything about the item, so the same orb rolls anew each time. */
uint32_t CraftSeed(const Item &item, CurrencyOrb orb, uint32_t step = 0)
{
	uint32_t hash = Mix(static_cast<uint32_t>(item._iSeed) ^ item.dwBuff);
	for (const uint8_t byte : PackCraftState(item._iPoe2Craft))
		hash = Mix(hash ^ byte);
	return Mix(hash ^ (static_cast<uint32_t>(orb) * 0x9E3779B9U) ^ (step * 0x85EBCA6BU));
}

int PerSideFor(const Item &item, uint8_t rarity)
{
	if (rarity == ITEM_QUALITY_MAGIC)
		return MaxMagicModsPerSide;
	return IsJewel(item) ? MaxRareJewelModsPerSide : MaxRareModsPerSide;
}

/** @brief The omens in the player's inventory that change this crafting action. */
struct CraftOmens {
	Player &player;
	std::vector<OmenId> held;

	CraftOmens(Player &who)
	    : player(who)
	{
		for (int i = 0; i < player._pNumInv; i++) {
			if (const OmenId omen = GetOmenId(player.InvList[i]); omen != OmenId::None)
				held.push_back(omen);
		}
	}

	[[nodiscard]] bool has(OmenId omen) const { return std::find(held.begin(), held.end(), omen) != held.end(); }

	/** @brief Marks the omen used up if it is held. */
	bool use(OmenId omen)
	{
		if (!has(omen))
			return false;
		PendingOmens.push_back(omen);
		return true;
	}
};

/** @brief Adds a random modifier that meets the filter; a floor no modifier meets falls back to any. */
bool AddRandomMod(const Item &item, Poe2Craft &craft, uint32_t seed, ModRollFilter filter)
{
	const int perSide = PerSideFor(item, craft.rarity);
	if (CraftModCount(craft) >= CraftModLimit(item, perSide))
		return false;
	// Desecrated modifiers aren't in the tables, so a side they fill is closed here.
	const bool prefixesFull = CraftSideCount(craft, true) >= perSide;
	const bool suffixesFull = CraftSideCount(craft, false) >= perSide;
	if (filter.side == ModSide::Any && prefixesFull != suffixesFull)
		filter.side = prefixesFull ? ModSide::Suffix : ModSide::Prefix;
	if ((prefixesFull && suffixesFull) || (filter.side == ModSide::Prefix && prefixesFull) || (filter.side == ModSide::Suffix && suffixesFull))
		return false;
	std::vector<Poe2ItemMod> existing = CraftModList(craft);
	std::optional<Poe2ItemMod> mod = RollAddedMod(*ItemModSlot(item), ItemModBaseTags(item), GetPoe2ItemLevel(item), existing, perSide, seed, filter);
	if (!mod && filter.minModLevel > 0) {
		filter.minModLevel = 0;
		mod = RollAddedMod(*ItemModSlot(item), ItemModBaseTags(item), GetPoe2ItemLevel(item), existing, perSide, seed, filter);
	}
	if (!mod && filter.tags != ModTagRule::Any) {
		filter.tags = ModTagRule::Any;
		mod = RollAddedMod(*ItemModSlot(item), ItemModBaseTags(item), GetPoe2ItemLevel(item), existing, perSide, seed, filter);
	}
	return mod && AddCraftMod(craft, *mod, seed ^ 0x51ED27U);
}

enum class RemovePick : uint8_t {
	Random,
	LowestLevel,
	HighestLevel,
};

/** @brief Removes a modifier from the allowed side. @return Whether one was removed. */
bool RemoveMod(Poe2Craft &craft, uint32_t seed, ModSide side, RemovePick pick)
{
	std::vector<size_t> candidates;
	for (size_t i = 0; i < craft.mods.size(); i++) {
		const Poe2CraftMod &mod = craft.mods[i];
		if (mod.family == 0 || mod.family == UnrevealedModFamily)
			continue;
		if (side != ModSide::Any && IsCraftPrefix(mod) != (side == ModSide::Prefix))
			continue;
		candidates.push_back(i);
	}
	if (candidates.empty())
		return false;
	size_t chosen = candidates[Mix(seed) % candidates.size()];
	if (pick != RemovePick::Random) {
		const auto level = [&craft](size_t i) {
			if (const DesecratedMod *desecrated = GetDesecratedMod(craft.mods[i].family); desecrated != nullptr)
				return desecrated->minItemLevel;
			return ModTierItemLevel(craft.mods[i].family, craft.mods[i].tier);
		};
		const auto it = pick == RemovePick::LowestLevel
		    ? std::min_element(candidates.begin(), candidates.end(), [&](size_t a, size_t b) { return level(a) < level(b); })
		    : std::max_element(candidates.begin(), candidates.end(), [&](size_t a, size_t b) { return level(a) < level(b); });
		chosen = *it;
	}
	RemoveCraftMod(craft, chosen);
	return true;
}

/** @brief Rebuilds the item with its new crafting state and says what it became. */
void FinishCraft(const Player &player, Item &item, const Poe2Craft &craft, uint32_t dwBuff)
{
	item = RebuildWithCraft(player, item, dwBuff, craft);
	Crafted(item);
}

ModRollFilter ExaltFilter(CraftOmens &omens)
{
	ModRollFilter filter;
	if (omens.use(OmenId::SinistralExaltation))
		filter.side = ModSide::Prefix;
	else if (omens.use(OmenId::DextralExaltation))
		filter.side = ModSide::Suffix;
	if (omens.use(OmenId::HomogenisingExaltation))
		filter.tags = ModTagRule::Shared;
	else if (omens.use(OmenId::DiscordantExaltation))
		filter.tags = ModTagRule::NotShared;
	return filter;
}

/** @brief Orbs on items whose modifiers come from the POE2 tables, worked one modifier at a time like POE2. */
std::optional<bool> CraftMods(CurrencyOrb orb, Player &player, Item &item, uint8_t variant, CraftOmens &omens)
{
	Poe2Craft craft = CurrentCraft(item);
	const int count = CraftModCount(craft);
	const uint8_t rarity = craft.rarity;
	switch (orb) {
	case CurrencyOrb::Transmutation:
		if (rarity != ITEM_QUALITY_NORMAL) {
			Fizzle(_("Orb of Transmutation needs a normal item."));
			return false;
		}
		craft.rarity = ITEM_QUALITY_MAGIC;
		if (!AddRandomMod(item, craft, CraftSeed(item, orb), {})) {
			Fizzle(_("The orb fizzles. This item can't roll that."));
			return false;
		}
		break;
	case CurrencyOrb::Augmentation:
		if (rarity != ITEM_QUALITY_MAGIC || count >= 2) {
			Fizzle(_("Orb of Augmentation needs a magic item with room for a modifier."));
			return false;
		}
		if (!AddRandomMod(item, craft, CraftSeed(item, orb), {})) {
			Fizzle(_("The orb fizzles. This item can't roll that."));
			return false;
		}
		break;
	case CurrencyOrb::Regal:
		if (rarity != ITEM_QUALITY_MAGIC) {
			Fizzle(_("Regal Orb needs a magic item."));
			return false;
		}
		craft.rarity = ITEM_QUALITY_RARE;
		if (!AddRandomMod(item, craft, CraftSeed(item, orb), {})) {
			Fizzle(_("The orb fizzles. This item can't roll that."));
			return false;
		}
		break;
	case CurrencyOrb::Alchemy: {
		if (IsNoneOf(rarity, ITEM_QUALITY_NORMAL, ITEM_QUALITY_MAGIC)) {
			Fizzle(_("Orb of Alchemy needs a normal or magic item."));
			return false;
		}
		// POE2: the item is reforged as a rare with exactly four modifiers.
		craft.rarity = ITEM_QUALITY_RARE;
		craft.mods = {};
		const int wanted = IsJewel(item) ? 3 : 4;
		for (int i = 0; i < wanted; i++)
			AddRandomMod(item, craft, CraftSeed(item, orb, static_cast<uint32_t>(i)), {});
		if (CraftModCount(craft) == 0) {
			Fizzle(_("The orb fizzles. This item can't roll that."));
			return false;
		}
		break;
	}
	case CurrencyOrb::Exalted:
	case CurrencyOrb::GreaterExalted:
	case CurrencyOrb::PerfectExalted: {
		if (rarity != ITEM_QUALITY_RARE) {
			Fizzle(_("Exalted Orbs need a rare item."));
			return false;
		}
		if (count >= CraftModLimit(item, PerSideFor(item, rarity))) {
			Fizzle(_("This item can't have more affixes."));
			return false;
		}
		ModRollFilter filter = ExaltFilter(omens);
		filter.minModLevel = orb == CurrencyOrb::GreaterExalted ? GreaterOrbModLevel : orb == CurrencyOrb::PerfectExalted ? PerfectOrbModLevel
		                                                                                                                  : 0;
		if (!AddRandomMod(item, craft, CraftSeed(item, orb), filter)) {
			Fizzle(_("The orb fizzles. No more affixes fit this item."));
			PendingOmens.clear();
			return false;
		}
		if (omens.use(OmenId::GreaterExaltation))
			AddRandomMod(item, craft, CraftSeed(item, orb, 1), filter);
		break;
	}
	case CurrencyOrb::Annul: {
		if (IsNoneOf(rarity, ITEM_QUALITY_MAGIC, ITEM_QUALITY_RARE) || count == 0) {
			Fizzle(_("Orb of Annulment needs a magic or rare item with a modifier."));
			return false;
		}
		ModSide side = ModSide::Any;
		if (omens.use(OmenId::SinistralAnnulment))
			side = ModSide::Prefix;
		else if (omens.use(OmenId::DextralAnnulment))
			side = ModSide::Suffix;
		const RemovePick pick = omens.use(OmenId::Whittling) ? RemovePick::LowestLevel : RemovePick::Random;
		if (!RemoveMod(craft, CraftSeed(item, orb), side, pick)) {
			Fizzle(_("There's no modifier of that kind to remove."));
			PendingOmens.clear();
			return false;
		}
		break;
	}
	case CurrencyOrb::Chaos:
	case CurrencyOrb::GreaterChaos:
	case CurrencyOrb::PerfectChaos: {
		if (rarity != ITEM_QUALITY_RARE || count == 0) {
			Fizzle(_("Chaos Orbs need a rare item."));
			return false;
		}
		// POE2: one random modifier is replaced by a new one.
		ModSide side = ModSide::Any;
		if (omens.use(OmenId::SinistralErasure) || omens.use(OmenId::DextralPreservation))
			side = ModSide::Prefix;
		else if (omens.use(OmenId::DextralErasure) || omens.use(OmenId::SinistralPreservation))
			side = ModSide::Suffix;
		if (!RemoveMod(craft, CraftSeed(item, orb), side, RemovePick::Random)) {
			Fizzle(_("There's no modifier of that kind to remove."));
			PendingOmens.clear();
			return false;
		}
		ModRollFilter filter;
		filter.minModLevel = orb == CurrencyOrb::GreaterChaos ? GreaterOrbModLevel : orb == CurrencyOrb::PerfectChaos ? PerfectOrbModLevel
		                                                                                                              : 0;
		AddRandomMod(item, craft, CraftSeed(item, orb, 1), filter);
		break;
	}
	case CurrencyOrb::Scouring:
		if (rarity == ITEM_QUALITY_NORMAL) {
			Fizzle(_("This item has no modifiers to remove."));
			return false;
		}
		craft.rarity = ITEM_QUALITY_NORMAL;
		craft.mods = {};
		break;
	case CurrencyOrb::Divine: {
		if (IsNoneOf(rarity, ITEM_QUALITY_MAGIC, ITEM_QUALITY_RARE) || count == 0) {
			Fizzle(_("Divine Orb needs a magic, rare or unique item."));
			return false;
		}
		// Each modifier rolls its values again from a new salt.
		const uint32_t seed = CraftSeed(item, orb);
		for (size_t i = 0; i < craft.mods.size(); i++) {
			Poe2CraftMod &mod = craft.mods[i];
			if (mod.family == 0 || mod.family == UnrevealedModFamily)
				continue;
			const auto salt = static_cast<uint8_t>(1 + (Mix(seed ^ static_cast<uint32_t>(i)) % (MaxCraftSalt - 1)));
			mod.salt = salt >= mod.salt ? static_cast<uint8_t>(salt + 1) : salt;
		}
		break;
	}
	case CurrencyOrb::Desecrate: {
		int side = 0;
		if (omens.use(OmenId::SinistralNecromancy))
			side = 1;
		else if (omens.use(OmenId::DextralNecromancy))
			side = -1;
		if (!DesecrateCraft(item, craft, CraftSeed(item, orb), side)) {
			PendingOmens.clear();
			return false;
		}
		FinishCraft(player, item, craft, item.dwBuff);
		DesecrationRevealItem = true;
		return true;
	}
	case CurrencyOrb::Essence: {
		const EssenceTier tier = static_cast<EssenceTier>((variant >> 4) & 3);
		const Essence &essence = GetEssence(static_cast<EssenceKind>(variant & 0xF));
		const std::optional<uint8_t> family = FindModifierFamily(essence.family);
		if (!family) {
			Fizzle(_("This essence does nothing."));
			return false;
		}
		ModRollFilter filter;
		filter.family = static_cast<uint8_t>(*family + 1);
		filter.minModLevel = EssenceModLevelFloors[static_cast<size_t>(tier)];
		if (tier == EssenceTier::Perfect) {
			if (rarity != ITEM_QUALITY_RARE) {
				Fizzle(_("Perfect essences need a rare item."));
				return false;
			}
			ModSide side = ModSide::Any;
			if (omens.use(OmenId::SinistralCrystallisation))
				side = ModSide::Prefix;
			else if (omens.use(OmenId::DextralCrystallisation))
				side = ModSide::Suffix;
			const RemovePick pick = omens.use(OmenId::Severance) ? RemovePick::HighestLevel : RemovePick::Random;
			if (count > 0)
				RemoveMod(craft, CraftSeed(item, orb), side, pick);
		} else {
			if (rarity != ITEM_QUALITY_MAGIC) {
				Fizzle(_("Essences need a magic item; Perfect ones a rare item."));
				return false;
			}
			craft.rarity = ITEM_QUALITY_RARE;
		}
		if (!AddRandomMod(item, craft, CraftSeed(item, orb, 1), filter)) {
			Fizzle(_("This essence's modifier can't go on this item."));
			PendingOmens.clear();
			return false;
		}
		break;
	}
	default:
		return std::nullopt;
	}
	FinishCraft(player, item, craft, item.dwBuff);
	return true;
}

/** @brief Blacksmith's Whetstone, Arcanist's Etcher, Armourer's Scrap and catalysts add quality: +2% on normal, magic and rare items, +1% on uniques. */
bool ApplyQualityCurrency(CurrencyOrb orb, const Player &player, Item &item, uint8_t variant)
{
	const QualityKind kind = ItemQualityKind(item);
	const bool jewellery = IsAnyOf(item._itype, ItemType::Ring, ItemType::Amulet);
	bool fits = false;
	switch (orb) {
	case CurrencyOrb::Whetstone:
		fits = kind == QualityKind::Weapon;
		break;
	case CurrencyOrb::Etcher:
		fits = kind == QualityKind::Caster;
		break;
	case CurrencyOrb::Scrap:
		fits = kind == QualityKind::Defence;
		break;
	case CurrencyOrb::Catalyst:
		fits = ItemBaseMeta(item) != nullptr && jewellery;
		break;
	default:
		break;
	}
	if (!fits) {
		Fizzle(orb == CurrencyOrb::Catalyst ? _("Catalysts work on rings and amulets.") : _("That quality currency doesn't fit this item."));
		return false;
	}
	Poe2Craft craft = item._iPoe2Craft;
	int quality = ItemQuality(item);
	if (orb == CurrencyOrb::Catalyst && craft.catalyst != variant) {
		// A different catalyst changes the kind and keeps the amount.
		craft.catalyst = variant;
	} else if (quality >= CraftQualityLimit(item)) {
		Fizzle(FormatRuntime(_("This item is already at {:d}% quality."), CraftQualityLimit(item)));
		return false;
	}
	// Curtis's rule (2026-10-16): +2% on normal, magic and rare items, +1% on uniques.
	const int step = item._iMagical == ITEM_QUALITY_UNIQUE ? 1 : 2;
	quality = std::min(std::max(quality, std::min(quality + step, CraftQualityLimit(item))), MaxExceptionalQuality);
	Item probe = item;
	SetItemQuality(probe, quality);
	FinishCraft(player, item, craft, probe.dwBuff);
	return true;
}

/** @brief Artificer's Orb: one more augment socket. */
bool AddSocket(const Player &player, Item &item)
{
	const int max = MaxAugmentSockets(item);
	if (max == 0 || IsVendorItem(item)) {
		Fizzle(_("Artificer's Orbs work on weapons and armour."));
		return false;
	}
	Poe2Craft craft = item._iPoe2Craft;
	if (craft.sockets >= max) {
		Fizzle(_("This item has all the sockets it can."));
		return false;
	}
	craft.sockets++;
	FinishCraft(player, item, craft, item.dwBuff);
	return true;
}

/** @brief An augment goes in an empty socket, or replaces the last one; corrupted items allow it. */
bool InsertAugment(const Player &player, Item &item, uint8_t variant)
{
	const Augment &augment = GetAugment(static_cast<AugmentId>(variant));
	Poe2Craft craft = item._iPoe2Craft;
	if (augment.id == AugmentId::None || craft.sockets == 0) {
		Fizzle(_("Augments go in an item's augment sockets; Artificer's Orbs add them."));
		return false;
	}
	size_t slot = craft.sockets - 1U;
	for (size_t i = 0; i < craft.sockets; i++) {
		if (craft.augments[i] == 0) {
			slot = i;
			break;
		}
	}
	// One talisman per item.
	if (augment.tier == 3) {
		for (size_t i = 0; i < craft.sockets; i++) {
			if (i != slot && GetAugment(static_cast<AugmentId>(craft.augments[i])).tier == 3) {
				Fizzle(_("An item takes only one talisman."));
				return false;
			}
		}
	}
	craft.augments[slot] = variant;
	FinishCraft(player, item, craft, item.dwBuff);
	return true;
}

/** @brief Vaal Orb on an item that can hold crafting state: the bundle's six outcomes, by item class. */
bool CorruptV2(Player &player, Item &item, CraftOmens &omens)
{
	const uint32_t seed = CraftSeed(item, CurrencyOrb::Vaal);
	const bool unique = item._iMagical == ITEM_QUALITY_UNIQUE;
	const bool explicitMods = CanCraftMods(item) && CraftModCount(CurrentCraft(item)) > 0;
	Poe2Craft craft = unique ? item._iPoe2Craft : CurrentCraft(item);
	if (!CanCraftMods(item))
		craft = item._iPoe2Craft;
	uint32_t dwBuff = item.dwBuff | CF_POE2_CORRUPTED;

	// No change 20, numbers 20, enchantment 25, socket 15, quality 10, disruption 10.
	std::array<int, 6> weights { 20, 20, 25, 15, 10, 10 };
	if (omens.use(OmenId::Corruption))
		weights[0] = 0;
	if (omens.use(OmenId::Artificer))
		weights[3] *= 4;
	if (!explicitMods)
		weights[1] = weights[5] = 0;
	if (MaxAugmentSockets(item) == 0)
		weights[3] = 0;
	if (!HasQuality(item) && !IsAnyOf(item._itype, ItemType::Ring, ItemType::Amulet))
		weights[4] = 0;
	const std::vector<std::pair<CorruptionEnchant, int>> enchants = CorruptionEnchantChoices(item);
	if (enchants.empty())
		weights[2] = 0;
	int total = 0;
	for (const int weight : weights)
		total += weight;
	int roll = total > 0 ? static_cast<int>(seed % static_cast<uint32_t>(total)) : 0;
	size_t category = 0;
	while (category + 1 < weights.size() && roll >= weights[category]) {
		roll -= weights[category];
		category++;
	}
	const uint32_t detail = Mix(seed ^ 0xC0221U);
	std::string_view message = _("Corrupted. Nothing else happens.");
	switch (category) {
	case 1: {
		// The explicit modifiers get 5 to 12% stronger (55%) or weaker (45%).
		const int amount = 5 + static_cast<int>(detail % 8);
		craft.magnitude = static_cast<int8_t>((detail >> 8) % 100 < 55 ? amount : -amount);
		message = craft.magnitude > 0 ? _("Corrupted. Its modifiers grow stronger.") : _("Corrupted. Its modifiers weaken.");
		break;
	}
	case 2: {
		int pick = static_cast<int>(detail % 1000U);
		int enchantTotal = 0;
		for (const auto &[id, weight] : enchants)
			enchantTotal += weight;
		pick = enchantTotal > 0 ? pick % enchantTotal : 0;
		CorruptionEnchant chosen = enchants.empty() ? CorruptionEnchant::None : enchants.back().first;
		for (const auto &[id, weight] : enchants) {
			if (pick < weight) {
				chosen = id;
				break;
			}
			pick -= weight;
		}
		craft.enchant = static_cast<uint8_t>(chosen);
		craft.enchantRoll = static_cast<uint8_t>((detail >> 12) & 7);
		message = _("Corrupted. The item gains an enchantment.");
		break;
	}
	case 3:
		if ((detail >> 8) % 100 < 60 && craft.sockets < MaxAugmentSockets(item)) {
			craft.sockets++;
			message = _("Corrupted. The item gains a socket.");
		} else if (craft.sockets > 0) {
			craft.sockets--;
			craft.augments[craft.sockets] = 0;
			message = _("Corrupted. The item loses a socket.");
		}
		break;
	case 4: {
		Item probe = item;
		const int amount = 1 + static_cast<int>(detail % 10);
		// Corruption can push quality past the usual cap.
		if ((detail >> 8) % 100 < 55) {
			SetItemQuality(probe, ItemQuality(item) + amount);
			message = _("Corrupted. The item's quality rises.");
		} else {
			SetItemQuality(probe, ItemQuality(item) - amount);
			message = _("Corrupted. The item's quality falls.");
		}
		dwBuff = (dwBuff & ~static_cast<uint32_t>(CF_POE2_QUALITY)) | (probe.dwBuff & CF_POE2_QUALITY);
		break;
	}
	case 5: {
		const uint32_t which = (detail >> 8) % 100;
		if (which < 55) {
			RemoveMod(craft, detail, ModSide::Any, RemovePick::Random);
			AddRandomMod(item, craft, detail ^ 0x5EEDU, {});
			message = _("Corrupted. A modifier changes.");
		} else if (which < 80) {
			RemoveMod(craft, detail, ModSide::Any, RemovePick::Random);
			message = _("Corrupted. A modifier vanishes.");
		} else {
			// A tier up or, more often, down.
			const size_t index = detail % static_cast<uint32_t>(std::max(CraftModCount(craft), 1));
			Poe2CraftMod &mod = craft.mods[index];
			const int step = (detail >> 16) % 100 < 70 ? 1 : -1;
			const int tier = mod.tier + step;
			if (mod.family != UnrevealedModFamily && tier >= 1 && ModTierItemLevel(mod.family, static_cast<uint8_t>(tier)) > 0) {
				mod.tier = static_cast<uint8_t>(tier);
				mod.salt = static_cast<uint8_t>(1 + (detail % MaxCraftSalt));
			}
			message = step > 0 ? _("Corrupted. A modifier weakens a tier.") : _("Corrupted. A modifier strengthens a tier.");
		}
		break;
	}
	default:
		break;
	}
	item = RebuildWithCraft(player, item, dwBuff, craft);
	EventPlrMsg(message);
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
	case IMISC_ORB_CHANCE:
		return CurrencyOrb::Chance;
	case IMISC_ORB_ANNUL:
		return CurrencyOrb::Annul;
	case IMISC_ORB_WHETSTONE:
		return CurrencyOrb::Whetstone;
	case IMISC_ORB_ETCHER:
		return CurrencyOrb::Etcher;
	case IMISC_ORB_SCRAP:
		return CurrencyOrb::Scrap;
	case IMISC_ORB_GREATER_EXALTED:
		return CurrencyOrb::GreaterExalted;
	case IMISC_ORB_PERFECT_EXALTED:
		return CurrencyOrb::PerfectExalted;
	case IMISC_ORB_GREATER_CHAOS:
		return CurrencyOrb::GreaterChaos;
	case IMISC_ORB_PERFECT_CHAOS:
		return CurrencyOrb::PerfectChaos;
	case IMISC_ORB_ARTIFICER:
		return CurrencyOrb::Artificer;
	case IMISC_ORB_DESECRATE:
		return CurrencyOrb::Desecrate;
	case IMISC_ORB_CATALYST:
		return CurrencyOrb::Catalyst;
	case IMISC_ESSENCE:
		return CurrencyOrb::Essence;
	case IMISC_AUGMENT:
		return CurrencyOrb::Augment;
	default:
		return CurrencyOrb::None;
	}
}

bool IsCurrencyMiscId(int miscId)
{
	return CurrencyOrbForMiscId(miscId) != CurrencyOrb::None || miscId == IMISC_OMEN;
}

std::string_view CurrencyOrbDescription(CurrencyOrb orb)
{
	switch (orb) {
	case CurrencyOrb::Transmutation:
		return _("Turns a normal item magic");
	case CurrencyOrb::Alchemy:
		return _("Turns a normal or magic item rare with four modifiers");
	case CurrencyOrb::Chaos:
		return _("Removes a random modifier from a rare item and adds a new one");
	case CurrencyOrb::Scouring:
		return _("Removes all affixes from an item");
	case CurrencyOrb::Augmentation:
		return _("Adds an affix to a magic item with one");
	case CurrencyOrb::Regal:
		return _("Turns a magic item rare, keeping its affixes");
	case CurrencyOrb::Exalted:
		return _("Adds an affix to a rare item");
	case CurrencyOrb::Divine:
		return _("Rerolls the values of a magic, rare or unique item's modifiers");
	case CurrencyOrb::Vaal:
		return _("Corrupts an item, with unpredictable results");
	case CurrencyOrb::Bauble:
		return _("Adds 2% quality to a flask, up to 20%");
	case CurrencyOrb::Chance:
		return _("Turns a normal item into a unique of its base, or destroys it");
	case CurrencyOrb::Annul:
		return _("Removes a random modifier from a magic or rare item");
	case CurrencyOrb::Whetstone:
		return _("Adds quality to a martial weapon");
	case CurrencyOrb::Etcher:
		return _("Adds quality to a staff or sceptre");
	case CurrencyOrb::Scrap:
		return _("Adds quality to armour");
	case CurrencyOrb::GreaterExalted:
		return _("Adds a modifier of level 35 or more to a rare item");
	case CurrencyOrb::PerfectExalted:
		return _("Adds a modifier of level 50 or more to a rare item");
	case CurrencyOrb::GreaterChaos:
		return _("Replaces a random modifier of a rare item with one of level 35 or more");
	case CurrencyOrb::PerfectChaos:
		return _("Replaces a random modifier of a rare item with one of level 50 or more");
	case CurrencyOrb::Artificer:
		return _("Adds an augment socket to a weapon or armour");
	case CurrencyOrb::Desecrate:
		return _("Desecrates a rare item: adds a modifier you reveal by using another Ancient Bone");
	case CurrencyOrb::Catalyst:
		return _("Adds quality to a ring or amulet");
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

void ConsumeCraftOmens(Player &player)
{
	for (const OmenId omen : PendingOmens) {
		for (int i = 0; i < player._pNumInv; i++) {
			if (GetOmenId(player.InvList[i]) == omen) {
				EventPlrMsg(FormatRuntime(_("{:s} is used up."), GetOmen(omen).name));
				if (!TakeOneFromStack(player.InvList[i]))
					player.RemoveInvItem(i);
				break;
			}
		}
	}
	PendingOmens.clear();
}

bool ApplyCurrencyOrb(CurrencyOrb orb, Player &player, Item &item, uint8_t variant)
{
	PendingOmens.clear();
	CurrencyDestroyedItem = false;
	DesecrationRevealItem = false;
	if (IsFlaskPotion(item) && (item.dwBuff & CF_POE2) != 0)
		return ApplyCurrencyToFlask(orb, item);
	if (IsWaystone(item))
		return CraftWaystone(orb, player, item);
	if (IsTablet(item))
		return CraftTablet(orb, player, item);
	if (orb == CurrencyOrb::Bauble) {
		EventPlrMsg(_("Glassblower's Baubles only work on flasks."), UiFlags::ColorRed);
		return false;
	}
	if (!IsCraftableKind(item)) {
		EventPlrMsg(_("Currency works on weapons, armour and jewellery."), UiFlags::ColorRed);
		return false;
	}

	const bool corrupted = (item.dwBuff & CF_POE2_CORRUPTED) != 0;
	// An unrevealed modifier's choice comes back without using up another bone.
	if (orb == CurrencyOrb::Desecrate && HasUnrevealedMod(item)) {
		DesecrationRevealItem = true;
		return false;
	}
	// Augments can still be inserted and replaced on corrupted items.
	if (orb == CurrencyOrb::Augment)
		return InsertAugment(player, item, variant);
	if (corrupted) {
		EventPlrMsg(_("Corrupted items can't be crafted."), UiFlags::ColorRed);
		return false;
	}

	CraftOmens omens(player);
	switch (orb) {
	case CurrencyOrb::Whetstone:
	case CurrencyOrb::Etcher:
	case CurrencyOrb::Scrap:
	case CurrencyOrb::Catalyst:
		return ApplyQualityCurrency(orb, player, item, variant);
	case CurrencyOrb::Artificer:
		return AddSocket(player, item);
	case CurrencyOrb::Chance: {
		if (item._iMagical != ITEM_QUALITY_NORMAL) {
			EventPlrMsg(_("Orb of Chance needs a normal item."), UiFlags::ColorRed);
			return false;
		}
		Item unique;
		switch (CreateChanceUnique(player, item, unique, CraftSeed(item, orb))) {
		case ChanceResult::NoUniques:
			EventPlrMsg(_("No unique is made on this base."), UiFlags::ColorRed);
			return false;
		case ChanceResult::Failed:
			if (omens.use(OmenId::Mercy)) {
				EventPlrMsg(_("The orb fails, but the Omen of Mercy spares the item."));
			} else {
				EventPlrMsg(_("The orb fails and the item crumbles to dust."), UiFlags::ColorRed);
				CurrencyDestroyedItem = true;
			}
			return true;
		case ChanceResult::Made:
			unique._iIdentified = true;
			unique.position = item.position;
			item = std::move(unique);
			EventPlrMsg(FormatRuntime(_("Chance favours you: {:s}"), item.getName().str()));
			return true;
		}
		return false;
	}
	case CurrencyOrb::Divine:
		if (item._iMagical == ITEM_QUALITY_UNIQUE) {
			// Uniques roll their modifiers' values again.
			const uint32_t divine = (item.dwBuff & CF_POE2_DIVINE) >> 9;
			const uint32_t next = divine >= 15 ? 1 : divine + 1;
			item = RebuildWithCraft(player, item, (item.dwBuff & ~CF_POE2_DIVINE) | (next << 9), item._iPoe2Craft);
			Crafted(item);
			return true;
		}
		break;
	case CurrencyOrb::Vaal:
		if (item._iMagical == ITEM_QUALITY_UNIQUE || (CanCraftMods(item) && !IsVendorItem(item)))
			return CorruptV2(player, item, omens);
		break;
	default:
		break;
	}

	if (item._iMagical == ITEM_QUALITY_UNIQUE) {
		EventPlrMsg(_("Uniques only take Divine and Vaal Orbs, quality currency and augments."), UiFlags::ColorRed);
		return false;
	}

	if (CanCraftMods(item) && !IsVendorItem(item)) {
		if (const std::optional<bool> crafted = CraftMods(orb, player, item, variant, omens); crafted)
			return *crafted;
	}

	switch (orb) {
	case CurrencyOrb::Annul:
	case CurrencyOrb::GreaterExalted:
	case CurrencyOrb::PerfectExalted:
	case CurrencyOrb::GreaterChaos:
	case CurrencyOrb::PerfectChaos:
	case CurrencyOrb::Essence:
	case CurrencyOrb::Desecrate:
		EventPlrMsg(_("That needs an item with POE2 modifiers, like a recent drop."), UiFlags::ColorRed);
		return false;
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
			EventPlrMsg(_("Divine Orb needs a magic, rare or unique item."), UiFlags::ColorRed);
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
		// POE2: Transmutation gives exactly one modifier.
		accept = [](const Item &candidate) {
			if (candidate._iPoe2Mods[0].family != 0)
				return ModAffixCount(candidate) == 1;
			return (candidate._iPrePower != IPL_INVALID) != (candidate._iSufPower != IPL_INVALID);
		};
		break;
	case CurrencyOrb::Alchemy:
		if (IsNoneOf(item._iMagical, ITEM_QUALITY_NORMAL, ITEM_QUALITY_MAGIC)) {
			EventPlrMsg(_("Orb of Alchemy needs a normal or magic item."), UiFlags::ColorRed);
			return false;
		}
		wanted = ITEM_QUALITY_RARE;
		// POE2: Alchemy makes a rare with exactly four modifiers.
		accept = [](const Item &candidate) { return RareAffixCount(candidate) == 4; };
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
		// POE2: Regal adds one modifier to the magic item's two.
		const int wantedCount = (prefix != IPL_INVALID ? 1 : 0) + (suffix != IPL_INVALID ? 1 : 0) + 1;
		accept = [prefix, suffix, wantedCount](const Item &candidate) { return HasPower(candidate, prefix) && HasPower(candidate, suffix) && RareAffixCount(candidate) <= std::max(wantedCount, 3); };
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
