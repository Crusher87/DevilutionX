#include "poe2/loot.hpp"

#include <algorithm>
#include <numeric>

#include "engine/random.hpp"
#include "game_mode.hpp"
#include "monster.h"
#include "poe2/monster_mods.hpp"
#include "utils/is_of.hpp"

namespace devilution {

namespace {

template <size_t N>
size_t RollWeighted(const std::array<int, N> &weights)
{
	const int total = std::accumulate(weights.begin(), weights.end(), 0);
	if (total <= 0)
		return 0;
	int roll = LootRandom(total);
	for (size_t i = 0; i < N; i++) {
		if (roll < weights[i])
			return i;
		roll -= weights[i];
	}
	return N - 1;
}

} // namespace

int LootRandom(int bound)
{
	if (bound <= 1)
		return 0;
	const uint32_t high = static_cast<uint32_t>(AdvanceRndSeed()) >> 16;
	const uint32_t low = static_cast<uint32_t>(AdvanceRndSeed()) >> 16;
	return static_cast<int>((((high & 0x7FFF) << 15) | (low & 0x7FFF)) % static_cast<uint32_t>(bound));
}

LootSource GetLootSource(const Monster &monster)
{
	if (IsAnyOf(monster.type().type, MT_DIABLO, MT_NAKRUL, MT_CLEAVER))
		return LootSource::Boss;
	if (monster.isUnique()) {
		// The named quest uniques come first in the unique monster table; the rest are wandering minibosses.
		const bool questBoss = static_cast<uint8_t>(monster.uniqueType) <= static_cast<uint8_t>(UniqueMonsterType::NaKrul);
		return questBoss ? LootSource::Boss : LootSource::Miniboss;
	}
	switch (GetMonsterAffixes(monster).rarity) {
	case MonsterRarity::Magic:
		return LootSource::Magic;
	case MonsterRarity::Rare:
		return LootSource::Rare;
	default:
		return LootSource::Normal;
	}
}

int ExpectedLootRollsPerMille(LootSource source)
{
	switch (source) {
	case LootSource::Magic:
		return 650;
	case LootSource::Rare:
		return 2000;
	case LootSource::Miniboss:
		return 3250;
	case LootSource::Boss:
		return 6000;
	default:
		return 180;
	}
}

int RollLootCount(LootSource source, const LootBonus &bonus)
{
	const int expected = ExpectedLootRollsPerMille(source) * bonus.quantity / 100;
	int count = expected / 1000;
	const int fraction = expected % 1000;
	if (fraction > 0 && LootRandom(1000) < fraction)
		count++;
	return count;
}

std::array<int, static_cast<size_t>(LootCategory::Count)> LootCategoryWeights(LootSource source, const LootBonus &bonus)
{
	// Equipment, gold, currency, flask or charm, gem, endgame, misc, jewel. Augments (4) aren't in the game yet, so
	// their weight goes to jewels for now. Uncut gems take 4 instead of the bible's 3, from gold, since skills only
	// level with them (Curtis 2026-10-20: none dropped in a whole Rogue run; 4 to 6 a run feels right).
	std::array<int, static_cast<size_t>(LootCategory::Count)> weights { 48, 21, 15, 4, 4, 3, 1, 4 };
	if (IsAnyOf(source, LootSource::Miniboss, LootSource::Boss)) {
		// Bosses favour equipment and give less of the low-value categories.
		weights[static_cast<size_t>(LootCategory::Equipment)] *= 2;
		weights[static_cast<size_t>(LootCategory::Gold)] /= 2;
		weights[static_cast<size_t>(LootCategory::Misc)] = 0;
	}
	weights[static_cast<size_t>(LootCategory::Endgame)] = weights[static_cast<size_t>(LootCategory::Endgame)] * bonus.waystones / 100;
	weights[static_cast<size_t>(LootCategory::Equipment)] = weights[static_cast<size_t>(LootCategory::Equipment)] * bonus.equipment / 100;
	return weights;
}

LootCategory RollLootCategory(LootSource source, const LootBonus &bonus)
{
	return static_cast<LootCategory>(RollWeighted(LootCategoryWeights(source, bonus)));
}

namespace {

std::array<int, 4> BaseRarityWeights(LootSource source)
{
	// Normal, magic, rare, unique, in hundredths of a percent.
	switch (source) {
	case LootSource::Magic:
		return { 3700, 4700, 1550, 50 };
	case LootSource::Rare:
		return { 1250, 4400, 4200, 150 };
	case LootSource::Miniboss:
	case LootSource::Boss:
		return { 100, 2900, 6500, 500 };
	default:
		return { 5700, 3500, 775, 25 };
	}
}

} // namespace

std::array<int, 4> EquipmentRarityWeights(LootSource source, const LootBonus &bonus)
{
	std::array<int, 4> weights = BaseRarityWeights(source);
	// Item rarity makes magic, rare and unique items more likely; normal ones take what's left.
	int total = 0;
	weights[3] = weights[3] * bonus.unique / 100;
	for (size_t i = 1; i < weights.size(); i++) {
		weights[i] = weights[i] * bonus.rarity / 100;
		total += weights[i];
	}
	weights[0] = std::max(10000 - total, 0);
	return weights;
}

item_quality RollEquipmentRarity(LootSource source, const LootBonus &bonus)
{
	constexpr std::array<item_quality, 4> Rarities { ITEM_QUALITY_NORMAL, ITEM_QUALITY_MAGIC, ITEM_QUALITY_RARE, ITEM_QUALITY_UNIQUE };
	return Rarities[RollWeighted(EquipmentRarityWeights(source, bonus))];
}

std::array<int, static_cast<size_t>(UniqueTier::Count)> UniqueTierWeights(LootSource source)
{
	switch (source) {
	case LootSource::Rare:
	case LootSource::Miniboss:
	case LootSource::Boss:
		return { 6500, 2300, 1000, 200 };
	default:
		return { 8400, 1200, 350, 50 };
	}
}

UniqueTier RollUniqueTier(LootSource source)
{
	return static_cast<UniqueTier>(RollWeighted(UniqueTierWeights(source)));
}

UniqueTier LegacyUniqueTier(int minLevel)
{
	if (minLevel <= 8)
		return UniqueTier::Common;
	if (minLevel < 20)
		return UniqueTier::Uncommon;
	return UniqueTier::Rare;
}

int LootItemLevelBonus(LootSource source)
{
	switch (source) {
	case LootSource::Rare:
		return 1;
	case LootSource::Miniboss:
	case LootSource::Boss:
		return 2;
	default:
		return 0;
	}
}

CurrencyBand RollCurrencyBand()
{
	// Common 56%, uncommon 28%, rare 12%, premium 3.8%, chase 0.2%.
	constexpr std::array<int, static_cast<size_t>(CurrencyBand::Count)> Weights { 5600, 2800, 1200, 380, 20 };
	return static_cast<CurrencyBand>(RollWeighted(Weights));
}

CurrencyBand OrbCurrencyBand(item_misc_id miscId)
{
	switch (miscId) {
	case IMISC_ORB_REGAL:
	case IMISC_ORB_ALCHEMY:
	case IMISC_ORB_ETCHER:
	case IMISC_ORB_ARTIFICER:
	case IMISC_ORB_CATALYST:
	case IMISC_ORB_LESSER_JEWELLER:
	case IMISC_AUGMENT:
	case IMISC_ESSENCE:
		return CurrencyBand::Uncommon;
	case IMISC_ORB_EXALTED:
	case IMISC_ORB_CHAOS:
	case IMISC_ORB_SCOURING:
	case IMISC_ORB_VAAL:
	case IMISC_ORB_CHANCE:
	case IMISC_ORB_ANNUL:
	case IMISC_ORB_DESECRATE:
	case IMISC_OMEN:
	case IMISC_ORB_GREATER_JEWELLER:
		return CurrencyBand::Rare;
	case IMISC_ORB_DIVINE:
	case IMISC_ORB_GREATER_EXALTED:
	case IMISC_ORB_GREATER_CHAOS:
	case IMISC_ORB_PERFECT_JEWELLER:
		return CurrencyBand::Premium;
	case IMISC_ORB_PERFECT_EXALTED:
	case IMISC_ORB_PERFECT_CHAOS:
		return CurrencyBand::Chase;
	default:
		return CurrencyBand::Common;
	}
}

int CurrencyDropLevel(item_misc_id miscId)
{
	// The bundle's drop levels, except Alchemy, which waits for Regal's 12, and Scouring, which the bundle doesn't
	// have (Curtis 2026-10-20: both were dropping far too early), and the Lesser Jeweller's Orb, which waits for
	// Act 2's first floor as in POE2 (Curtis 2026-10-22). The rest are the item table's levels on this scale.
	switch (miscId) {
	case IMISC_ORB_BAUBLE:
		return 8;
	case IMISC_ORB_ETCHER:
		return 10;
	case IMISC_ORB_LESSER_JEWELLER:
		return 14;
	case IMISC_AUGMENT:
		return 11;
	case IMISC_ORB_REGAL:
	case IMISC_ORB_ALCHEMY:
	case IMISC_ORB_EXALTED:
	case IMISC_ORB_CHAOS:
	case IMISC_ORB_CHANCE:
		return 12;
	case IMISC_ORB_VAAL:
	case IMISC_ORB_ARTIFICER:
		return 15;
	case IMISC_ORB_SCOURING:
	case IMISC_ESSENCE:
		return 20;
	case IMISC_ORB_GREATER_JEWELLER:
		return 24;
	case IMISC_ORB_DESECRATE:
		return 25;
	case IMISC_ORB_ANNUL:
	case IMISC_ORB_DIVINE:
	case IMISC_ORB_CATALYST:
		return 35;
	case IMISC_OMEN:
		return 41;
	case IMISC_ORB_PERFECT_JEWELLER:
		return 46;
	case IMISC_ORB_GREATER_EXALTED:
	case IMISC_ORB_GREATER_CHAOS:
		return 72;
	case IMISC_ORB_PERFECT_EXALTED:
	case IMISC_ORB_PERFECT_CHAOS:
		return 79;
	default:
		return 1;
	}
}

int CurrencyBandWeight(item_misc_id miscId)
{
	// The bundle's within-band weights; the ones it doesn't list sit with the scarcer orbs of their band.
	switch (miscId) {
	case IMISC_ORB_TRANSMUTE:
	case IMISC_ORB_AUGMENT:
		return 800;
	case IMISC_ORB_WHETSTONE:
	case IMISC_ORB_SCRAP:
		return 500;
	case IMISC_ORB_REGAL:
		return 450;
	case IMISC_ORB_ALCHEMY:
		return 400;
	case IMISC_ORB_EXALTED:
		return 350;
	case IMISC_ORB_BAUBLE:
		return 260;
	case IMISC_ORB_ETCHER:
		return 250;
	case IMISC_ORB_CHAOS:
	case IMISC_ORB_ARTIFICER:
	case IMISC_ORB_LESSER_JEWELLER:
		return 220;
	case IMISC_ORB_VAAL:
		return 200;
	case IMISC_ORB_ANNUL:
	case IMISC_ORB_DESECRATE:
		return 180;
	case IMISC_ORB_CHANCE:
		return 160;
	case IMISC_ORB_GREATER_EXALTED:
		return 130;
	case IMISC_ORB_DIVINE:
		return 120;
	case IMISC_ORB_GREATER_CHAOS:
	case IMISC_ORB_PERFECT_JEWELLER:
		return 100;
	case IMISC_ORB_PERFECT_EXALTED:
		return 30;
	case IMISC_ORB_PERFECT_CHAOS:
		return 24;
	default:
		return 150;
	}
}

int RollNaturalRareAffixCount()
{
	constexpr std::array<int, 3> Weights { 8, 3, 1 };
	return 4 + static_cast<int>(RollWeighted(Weights));
}

int BaseRecencyPercent(int itemLevel, int baseLevel)
{
	// The bible's gaps (5, 12, 20) are in POE2 levels, which run about twice as far as Diablo's monster levels.
	const int gap = itemLevel - baseLevel;
	if (gap <= 3)
		return 100;
	if (gap <= 7)
		return 70;
	if (gap <= 12)
		return 35;
	return 10;
}

int EquipmentClassWeight(ItemType type)
{
	switch (type) {
	case ItemType::LightArmor:
	case ItemType::MediumArmor:
	case ItemType::HeavyArmor:
		// Body armour, split over its three weights.
		return 5;
	case ItemType::Helm:
		return 9;
	case ItemType::Shield:
		return 8;
	case ItemType::Sword:
	case ItemType::Bow:
	case ItemType::Ring:
		return 7;
	case ItemType::Mace:
	case ItemType::Staff:
		return 6;
	case ItemType::Axe:
		return 5;
	case ItemType::Amulet:
		return 4;
	default:
		return 0;
	}
}

int Poe2ItemLevel(int areaLevel, int bonus)
{
	return std::clamp(((areaLevel * 13) + 5) / 10 + bonus, 1, MaxPoe2ItemLevel);
}

int Poe2ItemLevel(const Monster &monster, int difficulty)
{
	return Poe2ItemLevel(monster.data().level + (15 * difficulty), LootItemLevelBonus(GetLootSource(monster)));
}

int GetPoe2ItemLevel(const Item &item)
{
	if (EquipmentClassWeight(item._itype) == 0 && item._iMiscId != IMISC_JEWEL)
		return 0;
	return static_cast<int>((item.dwBuff & CF_POE2_ILVL) >> 16);
}

void SetPoe2ItemLevel(Item &item, int level)
{
	item.dwBuff = (item.dwBuff & ~(CF_POE2_ILVL | CF_POE2_MODS)) | ((static_cast<uint32_t>(std::clamp(level, 0, MaxPoe2ItemLevel)) << 16) & CF_POE2_ILVL);
	// Items made with an item level roll their affixes from the itemization tables.
	if (level > 0)
		item.dwBuff |= CF_POE2_MODS;
}

int VendorSellValue(int value)
{
	if (gbIsPoe2)
		return std::max(value * 18 / 100, 1);
	return std::max(value / 4, 1);
}

std::optional<item_quality> GetLootRarity(const Item &item)
{
	if ((item.dwBuff & CF_POE2_LOOT_SET) == 0)
		return std::nullopt;
	return static_cast<item_quality>((item.dwBuff & CF_POE2_LOOT_RARITY) >> 28);
}

void SetLootRarity(Item &item, item_quality rarity)
{
	item.dwBuff = (item.dwBuff & ~(CF_POE2_LOOT_RARITY | CF_POE2_LOOT_SET)) | ((static_cast<uint32_t>(rarity) << 28) & CF_POE2_LOOT_RARITY) | CF_POE2_LOOT_SET;
}

} // namespace devilution
