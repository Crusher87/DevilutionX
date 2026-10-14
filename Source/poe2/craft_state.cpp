#include "poe2/craft_state.hpp"

#include <algorithm>

#include "engine/random.hpp"
#include "poe2/desecration.hpp"
#include "poe2/loot.hpp"
#include "poe2/modifiers.hpp"

namespace devilution {

namespace {

constexpr uint8_t CraftStateMarker = 0xC2;

uint8_t Checksum(const std::array<uint8_t, CraftStateSaveSize> &bytes)
{
	uint32_t sum = 0;
	for (size_t i = 0; i + 1 < bytes.size(); i++)
		sum = (sum * 31U) + bytes[i];
	return static_cast<uint8_t>((sum ^ (sum >> 8) ^ 0xA5U) & 0xFFU);
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

} // namespace

std::array<uint8_t, CraftStateSaveSize> PackCraftState(const Poe2Craft &craft)
{
	std::array<uint8_t, CraftStateSaveSize> bytes {};
	if (craft.isEmpty())
		return bytes;
	bytes[0] = CraftStateMarker;
	bytes[1] = static_cast<uint8_t>((craft.explicitMods ? 1 : 0) | ((craft.rarity & 3) << 1) | ((craft.rolledMagic ? 1 : 0) << 3) | ((craft.sockets & 3) << 4) | ((craft.desecrated ? 1 : 0) << 6));
	bytes[2] = static_cast<uint8_t>((craft.enchant & 0x1F) | ((craft.enchantRoll & 7) << 5));
	bytes[3] = static_cast<uint8_t>(craft.magnitude);
	bytes[4] = static_cast<uint8_t>((craft.augments[0] & 0xF) | ((craft.augments[1] & 0xF) << 4));
	bytes[5] = static_cast<uint8_t>((craft.augments[2] & 0xF) | ((craft.catalyst & 0xF) << 4));
	for (size_t i = 0; i < MaxRareAffixes; i++) {
		const Poe2CraftMod &mod = craft.mods[i];
		const auto packed = static_cast<uint16_t>((mod.family & 0x7F) | ((mod.tier & 0xF) << 7) | ((mod.salt & 0x1F) << 11));
		bytes[6 + (i * 2)] = static_cast<uint8_t>(packed & 0xFF);
		bytes[7 + (i * 2)] = static_cast<uint8_t>(packed >> 8);
	}
	bytes[CraftStateSaveSize - 1] = Checksum(bytes);
	return bytes;
}

Poe2Craft UnpackCraftState(const std::array<uint8_t, CraftStateSaveSize> &bytes)
{
	Poe2Craft craft;
	if (bytes[0] != CraftStateMarker || bytes[CraftStateSaveSize - 1] != Checksum(bytes))
		return craft;
	craft.explicitMods = (bytes[1] & 1) != 0;
	craft.rarity = (bytes[1] >> 1) & 3;
	craft.rolledMagic = ((bytes[1] >> 3) & 1) != 0;
	craft.sockets = (bytes[1] >> 4) & 3;
	craft.desecrated = ((bytes[1] >> 6) & 1) != 0;
	craft.enchant = bytes[2] & 0x1F;
	craft.enchantRoll = bytes[2] >> 5;
	craft.magnitude = static_cast<int8_t>(bytes[3]);
	craft.augments[0] = bytes[4] & 0xF;
	craft.augments[1] = bytes[4] >> 4;
	craft.augments[2] = bytes[5] & 0xF;
	craft.catalyst = bytes[5] >> 4;
	for (size_t i = 0; i < MaxRareAffixes; i++) {
		const auto packed = static_cast<uint16_t>(bytes[6 + (i * 2)] | (bytes[7 + (i * 2)] << 8));
		craft.mods[i] = { static_cast<uint8_t>(packed & 0x7F), static_cast<uint8_t>((packed >> 7) & 0xF), static_cast<uint8_t>(packed >> 11) };
	}
	return craft;
}

bool CanCraftMods(const Item &item)
{
	if (item.isEmpty() || item._iMagical == ITEM_QUALITY_UNIQUE || (item.dwBuff & CF_POE2_MODS) == 0 || GetPoe2ItemLevel(item) == 0)
		return false;
	return ItemModSlot(item).has_value() && !ModifierFamilies().empty();
}

Poe2Craft CurrentCraft(const Item &item)
{
	Poe2Craft craft = item._iPoe2Craft;
	if (craft.explicitMods)
		return craft;
	craft.explicitMods = true;
	craft.rarity = item._iMagical == ITEM_QUALITY_RARE ? ITEM_QUALITY_RARE : item._iMagical;
	craft.rolledMagic = item._iMagical != ITEM_QUALITY_RARE;
	craft.mods = {};
	size_t count = 0;
	for (const Poe2ItemMod &mod : item._iPoe2Mods) {
		if (mod.family != 0 && count < craft.mods.size())
			craft.mods[count++] = { mod.family, mod.tier, 0 };
	}
	return craft;
}

std::vector<Poe2ItemMod> CraftModList(const Poe2Craft &craft)
{
	std::vector<Poe2ItemMod> mods;
	for (const Poe2CraftMod &mod : craft.mods) {
		if (mod.family != 0 && mod.family != UnrevealedModFamily && !IsDesecratedFamily(mod.family))
			mods.push_back({ mod.family, mod.tier, {} });
	}
	return mods;
}

int CraftModCount(const Poe2Craft &craft)
{
	return static_cast<int>(std::count_if(craft.mods.begin(), craft.mods.end(), [](const Poe2CraftMod &mod) { return mod.family != 0; }));
}

bool IsCraftPrefix(const Poe2CraftMod &mod)
{
	if (mod.family == UnrevealedModFamily)
		return mod.tier == 1;
	if (const DesecratedMod *desecrated = GetDesecratedMod(mod.family); desecrated != nullptr)
		return desecrated->prefix;
	const Poe2ItemMod itemMod { mod.family, mod.tier, {} };
	const ModFamily *family = ItemModFamily(itemMod);
	return family != nullptr && family->prefix;
}

int CraftSideCount(const Poe2Craft &craft, bool prefix)
{
	return static_cast<int>(std::count_if(craft.mods.begin(), craft.mods.end(), [prefix](const Poe2CraftMod &mod) { return mod.family != 0 && IsCraftPrefix(mod) == prefix; }));
}

bool AddCraftMod(Poe2Craft &craft, const Poe2ItemMod &mod, uint32_t saltSeed)
{
	const int count = CraftModCount(craft);
	if (count >= static_cast<int>(craft.mods.size()))
		return false;
	const auto salt = static_cast<uint8_t>(1 + (Mix(saltSeed) % MaxCraftSalt));
	craft.mods[static_cast<size_t>(count)] = { mod.family, mod.tier, mod.family == UnrevealedModFamily ? static_cast<uint8_t>(0) : salt };
	// Like POE2, prefixes are listed before suffixes.
	std::stable_partition(craft.mods.begin(), craft.mods.begin() + count + 1, [](const Poe2CraftMod &other) { return IsCraftPrefix(other); });
	return true;
}

void RemoveCraftMod(Poe2Craft &craft, size_t index)
{
	if (index >= craft.mods.size())
		return;
	for (size_t i = index; i + 1 < craft.mods.size(); i++)
		craft.mods[i] = craft.mods[i + 1];
	craft.mods.back() = {};
}

std::array<int16_t, 2> CraftModValues(const Item &item, const Poe2CraftMod &mod)
{
	const uint32_t valueSeed = Mix(static_cast<uint32_t>(item._iSeed) ^ (mod.family * 0x9E3779B9U) ^ (mod.salt * 0x85EBCA6BU) ^ 0x2545F491U);
	return RollModValues(mod.family, mod.tier, valueSeed);
}

std::vector<Poe2ItemMod> ResolveCraftMods(const Item &item, const std::vector<Poe2ItemMod> &natural)
{
	const Poe2Craft &craft = item._iPoe2Craft;
	const int effectPercent = DesecratedModEffectPercent(item);
	std::vector<Poe2ItemMod> mods;
	for (const Poe2CraftMod &crafted : craft.mods) {
		if (crafted.family == 0 || crafted.family == UnrevealedModFamily || IsDesecratedFamily(crafted.family))
			continue;
		Poe2ItemMod mod { crafted.family, crafted.tier, {} };
		const auto rolled = std::find_if(natural.begin(), natural.end(), [&crafted](const Poe2ItemMod &other) { return other.family == crafted.family && other.tier == crafted.tier; });
		if (crafted.salt == 0 && rolled != natural.end()) {
			mod.values = rolled->values;
		} else {
			mod.values = CraftModValues(item, crafted);
		}
		// Corruption can make the explicit modifiers stronger or weaker, and Unclean Perfection scales them with quality.
		const int percent = (100 + craft.magnitude) * effectPercent / 100;
		if (percent != 100) {
			for (int16_t &value : mod.values) {
				const int shifted = value * percent / 100;
				value = static_cast<int16_t>(value > 0 ? std::max(shifted, 1) : shifted);
			}
		}
		mods.push_back(mod);
	}
	return mods;
}

Item RebuildWithCraft(const Player &player, const Item &item, uint32_t dwBuff, const Poe2Craft &craft)
{
	const uint32_t rngState = GetLCGEngineState();
	Item rebuilt;
	RecreateItem(player, rebuilt, item.IDidx, item._iCreateInfo, item._iSeed, item._ivalue, dwBuff, craft);
	SetRndSeed(rngState);
	rebuilt._iIdentified = true;
	rebuilt.position = item.position;
	rebuilt._iDurability = std::min<int>(item._iDurability, rebuilt._iMaxDur);
	return rebuilt;
}

} // namespace devilution
