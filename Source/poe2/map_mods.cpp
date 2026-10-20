#include "poe2/map_mods.hpp"

#include <algorithm>

#include "poe2/mix.hpp"
#include "utils/format.hpp"
#include "utils/language.h"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

using enum MapDanger;

// Bundle waystone_modifiers.json: the danger's low, mid and high tier ranges and the reward per band.
const std::array<WaystoneMod, WaystoneModIds + 1> WaystoneMods { {
	{ "", false, 0, MonsterDamage, {}, MapAxis::PackSize, {}, "" },
	{ "Ravenous", true, 100, MonsterDamage, { { { 10, 14 }, { 15, 19 }, { 20, 25 } } }, MapAxis::MonsterRarity, { 15, 22, 30 }, "Monsters deal {}% increased damage" },
	{ "Infernal", true, 100, ExtraFire, { { { 8, 11 }, { 12, 15 }, { 16, 20 } } }, MapAxis::Effectiveness, { 10, 14, 18 }, "Monsters gain {}% of damage as extra fire" },
	{ "Glacial", true, 100, ExtraCold, { { { 8, 11 }, { 12, 15 }, { 16, 20 } } }, MapAxis::ItemRarity, { 8, 12, 16 }, "Monsters gain {}% of damage as extra cold" },
	{ "Tempestuous", true, 100, ExtraLightning, { { { 8, 11 }, { 12, 15 }, { 16, 20 } } }, MapAxis::PackSize, { 4, 7, 10 }, "Monsters gain {}% of damage as extra lightning" },
	{ "Profane", true, 85, ExtraChaos, { { { 6, 9 }, { 10, 13 }, { 14, 18 } } }, MapAxis::ItemRarity, { 8, 12, 17 }, "Monsters gain {}% of damage as extra chaos" },
	{ "Unyielding", true, 100, MonsterLife, { { { 15, 20 }, { 21, 27 }, { 28, 36 } } }, MapAxis::MonsterRarity, { 14, 21, 28 }, "Monsters have {}% more life" },
	{ "Swift", true, 90, MonsterSpeed, { { { 8, 12 }, { 13, 17 }, { 18, 22 } } }, MapAxis::PackSize, { 5, 8, 11 }, "Monsters have {}% increased action speed" },
	{ "Venomous", true, 95, PoisonChance, { { { 15, 20 }, { 21, 28 }, { 29, 38 } } }, MapAxis::PackSize, { 4, 7, 10 }, "Monsters have {}% chance to poison on hit" },
	{ "Puncturing", true, 95, BleedChance, { { { 10, 14 }, { 15, 20 }, { 21, 28 } } }, MapAxis::Effectiveness, { 9, 13, 17 }, "Monsters have {}% chance to cause bleeding on hit" },
	{ "Shattering", true, 90, ArmourBreak, { { { 15, 20 }, { 20, 25 }, { 25, 32 } } }, MapAxis::MonsterRarity, { 12, 18, 24 }, "Monsters break armour equal to {}% of physical damage" },
	{ "Penetrating", true, 80, Penetration, { { { 5, 7 }, { 8, 10 }, { 11, 14 } } }, MapAxis::ItemRarity, { 10, 15, 20 }, "Monster hits penetrate {}% elemental resistances" },
	{ "Relentless", true, 80, RareSkillSpeed, { { { 10, 10 }, { 18, 18 }, { 28, 28 } } }, MapAxis::Effectiveness, { 8, 13, 19 }, "Rare monsters act {}% faster" },
	{ "Regenerating", true, 80, MonsterRegen, { { { 10, 10 }, { 15, 15 }, { 20, 20 } } }, MapAxis::WaystoneFind, { 12, 18, 25 }, "Monsters regenerate {}% of life per second" },
	{ "Armoured", true, 100, MonsterArmour, { { { 20, 20 }, { 35, 35 }, { 55, 55 } } }, MapAxis::MonsterRarity, { 12, 19, 26 }, "Monsters have {}% increased armour" },
	{ "Evasive", true, 100, MonsterEvasion, { { { 15, 15 }, { 25, 25 }, { 40, 40 } } }, MapAxis::PackSize, { 4, 7, 9 }, "Monsters have {}% increased evasion" },
	{ "Shielded", true, 90, MonsterEnergyShield, { { { 8, 8 }, { 14, 14 }, { 22, 22 } } }, MapAxis::ItemRarity, { 8, 12, 16 }, "Monsters gain {}% of life as energy shield" },
	{ "Deadly", true, 55, RareExtraMod, { { { 10, 10 }, { 20, 20 }, { 35, 35 } } }, MapAxis::MonsterRarity, { 18, 26, 36 }, "Rare monsters have {}% chance for an extra modifier" },
	{ "Teeming", true, 60, MorePacks, { { { 6, 6 }, { 10, 10 }, { 15, 15 } } }, MapAxis::WaystoneFind, { 10, 15, 22 }, "Area has {}% more monster packs" },
	{ "of Drought", false, 100, FewerFlaskCharges, { { { 15, 15 }, { 25, 25 }, { 35, 35 } } }, MapAxis::PackSize, { 5, 8, 11 }, "Players gain {}% fewer flask charges" },
	{ "of Smothering", false, 90, LessRecovery, { { { 18, 18 }, { 28, 28 }, { 38, 38 } } }, MapAxis::ItemRarity, { 10, 15, 20 }, "Players have {}% less life and energy shield recovery" },
	{ "of Hexwarding", false, 100, LessExposure, { { { 20, 20 }, { 35, 35 }, { 50, 50 } } }, MapAxis::ItemRarity, { 7, 11, 15 }, "Monsters take {}% less effect of exposure and shock" },
	{ "of Obstruction", false, 95, LessCritDamageTaken, { { { 12, 12 }, { 20, 20 }, { 30, 30 } } }, MapAxis::MonsterRarity, { 12, 18, 25 }, "Monsters take {}% reduced extra damage from critical hits" },
	{ "of the Prism", false, 95, MonsterResistance, { { { 15, 15 }, { 23, 23 }, { 32, 32 } } }, MapAxis::ItemRarity, { 9, 14, 19 }, "Monsters have +{}% elemental resistances" },
	{ "of the Unwavering", false, 95, MonsterThreshold, { { { 25, 25 }, { 45, 45 }, { 70, 70 } } }, MapAxis::Effectiveness, { 9, 13, 18 }, "Monsters have {}% increased stun and freeze thresholds" },
	{ "of Enfeebling", false, 70, PlayerLessDamage, { { { 10, 10 }, { 15, 15 }, { 20, 20 } } }, MapAxis::Effectiveness, { 10, 15, 20 }, "Players periodically deal {}% less damage" },
	{ "of Slowness", false, 75, PlayerSlower, { { { 10, 10 }, { 15, 15 }, { 20, 20 } } }, MapAxis::PackSize, { 5, 8, 11 }, "Players periodically have {}% reduced action speed" },
	{ "of Embers", false, 90, BurningGround, { { { 1, 1 }, { 2, 2 }, { 3, 3 } } }, MapAxis::Effectiveness, { 8, 13, 18 }, "Area has patches of burning ground" },
	{ "of Frost", false, 90, ChilledGround, { { { 1, 1 }, { 2, 2 }, { 3, 3 } } }, MapAxis::ItemRarity, { 8, 12, 16 }, "Area has patches of chilled ground" },
	{ "of Storms", false, 90, ShockedGround, { { { 1, 1 }, { 2, 2 }, { 3, 3 } } }, MapAxis::PackSize, { 4, 7, 10 }, "Area has patches of shocked ground" },
	{ "of Exhaustion", false, 80, MoreSkillCost, { { { 8, 8 }, { 13, 13 }, { 20, 20 } } }, MapAxis::WaystoneFind, { 12, 18, 26 }, "Skills cost {}% more mana" },
	{ "of Isolation", false, 75, WeakerAuras, { { { 8, 8 }, { 13, 13 }, { 20, 20 } } }, MapAxis::ItemRarity, { 10, 15, 21 }, "Player auras and heralds are {}% weaker" },
	{ "of Ruin", false, 65, DeathEffects, { { { 1, 1 }, { 2, 2 }, { 3, 3 } } }, MapAxis::MonsterRarity, { 13, 20, 28 }, "Magic and rare monsters explode on death" },
	{ "of Hunger", false, 85, LessLeech, { { { 15, 15 }, { 25, 25 }, { 40, 40 } } }, MapAxis::WaystoneFind, { 12, 18, 25 }, "Players recover {}% less from leech" },
	{ "of the Hunt", false, 70, RareMoveSpeed, { { { 10, 10 }, { 16, 16 }, { 24, 24 } } }, MapAxis::MonsterRarity, { 14, 22, 30 }, "Rare monsters move {}% faster" },
	{ "of Abundance", false, 60, MoreMagicRare, { { { 8, 8 }, { 14, 14 }, { 22, 22 } } }, MapAxis::WaystoneFind, { 10, 16, 24 }, "Area has {}% more magic and rare monsters" },
	{ "of the Gambler", false, 45, LootVariance, { { { 10, 10 }, { 20, 20 }, { 35, 35 } } }, MapAxis::ItemRarity, { 12, 18, 25 }, "Loot quality varies by up to {}%" },
} };

} // namespace

const WaystoneMod &GetWaystoneMod(uint8_t id)
{
	return WaystoneMods[id <= WaystoneModIds ? id : 0];
}

int WaystoneBand(int tier)
{
	return tier <= 5 ? 0 : tier <= 10 ? 1
	                                  : 2;
}

int WaystoneModValue(const Poe2CraftMod &mod)
{
	const WaystoneMod &data = GetWaystoneMod(mod.family);
	const auto band = static_cast<size_t>(std::clamp<int>(mod.tier, 1, 3) - 1);
	const auto [low, high] = data.dangerValues[band];
	if (high <= low)
		return low;
	return low + static_cast<int>(Mix(0x3A9E5U ^ (mod.family * 0x9E3779B9U) ^ (mod.salt * 0x85EBCA6BU)) % static_cast<uint32_t>(high - low + 1));
}

int WaystoneModReward(const Poe2CraftMod &mod)
{
	return GetWaystoneMod(mod.family).reward[static_cast<size_t>(std::clamp<int>(mod.tier, 1, 3) - 1)];
}

std::string_view MapAxisName(MapAxis axis)
{
	switch (axis) {
	case MapAxis::PackSize:
		return _("Pack Size");
	case MapAxis::MonsterRarity:
		return _("Monster Rarity");
	case MapAxis::ItemRarity:
		return _("Item Rarity");
	case MapAxis::Effectiveness:
		return _("Monster Effectiveness");
	case MapAxis::WaystoneFind:
		return _("Waystone Drop Chance");
	default:
		return "";
	}
}

std::vector<std::string> WaystoneModText(const Poe2CraftMod &mod)
{
	const WaystoneMod &data = GetWaystoneMod(mod.family);
	std::vector<std::string> lines;
	if (data.name.empty())
		return lines;
	std::string text { data.text };
	if (const size_t at = text.find("{}"); at != std::string::npos) {
		const int value = WaystoneModValue(mod);
		std::string number = StrCat(value);
		// Regeneration is in tenths of a percent.
		if (data.danger == MonsterRegen && value % 10 != 0)
			number = StrCat(value / 10, ".", value % 10);
		else if (data.danger == MonsterRegen)
			number = StrCat(value / 10);
		text.replace(at, 2, number);
	}
	lines.push_back(std::move(text));
	lines.push_back(FormatRuntime(_("+{:d}% {:s}"), WaystoneModReward(mod), MapAxisName(data.axis)));
	return lines;
}

uint8_t RollWaystoneMod(const std::array<Poe2CraftMod, MaxCraftMods> &existing, int perSide, uint32_t seed, int forcedSide)
{
	int prefixes = 0;
	int suffixes = 0;
	for (const Poe2CraftMod &mod : existing) {
		if (mod.family == 0)
			continue;
		(GetWaystoneMod(mod.family).prefix ? prefixes : suffixes)++;
	}
	int total = 0;
	std::array<int, WaystoneModIds + 1> weights {};
	for (uint8_t id = 1; id <= WaystoneModIds; id++) {
		const WaystoneMod &mod = WaystoneMods[id];
		if (std::any_of(existing.begin(), existing.end(), [id](const Poe2CraftMod &other) { return other.family == id; }))
			continue;
		if ((mod.prefix && prefixes >= perSide) || (!mod.prefix && suffixes >= perSide))
			continue;
		if ((forcedSide > 0 && !mod.prefix) || (forcedSide < 0 && mod.prefix))
			continue;
		weights[id] = mod.weight;
		total += mod.weight;
	}
	if (total == 0)
		return 0;
	int roll = static_cast<int>(Mix(seed) % static_cast<uint32_t>(total));
	for (uint8_t id = 1; id <= WaystoneModIds; id++) {
		roll -= weights[id];
		if (roll < 0)
			return id;
	}
	return 0;
}

} // namespace devilution
