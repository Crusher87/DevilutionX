#include "poe2/uniques.hpp"

#include <algorithm>
#include <string_view>

#include <magic_enum/magic_enum.hpp>

#include "data/file.hpp"
#include "data/record_reader.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "tables/itemdat.h"
#include "utils/format.hpp"
#include "utils/log.hpp"
#include "utils/parse_int.hpp"
#include "utils/str_split.hpp"

namespace devilution {

namespace {

std::vector<Poe2Unique> Uniques;

std::optional<UniqueTier> ParseTier(std::string_view text)
{
	if (text == "common")
		return UniqueTier::Common;
	if (text == "uncommon")
		return UniqueTier::Uncommon;
	if (text == "rare")
		return UniqueTier::Rare;
	if (text == "mythic")
		return UniqueTier::Mythic;
	if (text == "boss")
		return UniqueTier::Rare;
	return std::nullopt;
}

std::vector<UniqueEffect> ParseEffects(std::string_view text)
{
	std::vector<UniqueEffect> effects;
	for (const std::string_view entry : SplitByChar(text, ';')) {
		if (entry.empty())
			continue;
		std::array<std::string_view, 4> parts {};
		size_t count = 0;
		for (const std::string_view part : SplitByChar(entry, '|')) {
			if (count < parts.size())
				parts[count] = part;
			count++;
		}
		UniqueEffect effect;
		if (const std::optional<ModEffect> mod = magic_enum::enum_cast<ModEffect>(parts[0]); mod && *mod != ModEffect::None)
			effect.mod = *mod;
		else if (const std::optional<UniqueRule> rule = magic_enum::enum_cast<UniqueRule>(parts[0]); rule && *rule != UniqueRule::None)
			effect.rule = *rule;
		if (count != parts.size() || (effect.mod == ModEffect::None && effect.rule == UniqueRule::None)) {
			LogError("Bad unique modifier: {}", entry);
			continue;
		}
		effect.min = static_cast<int16_t>(ParseInt<int>(parts[1]).value_or(0));
		effect.max = static_cast<int16_t>(ParseInt<int>(parts[2]).value_or(0));
		effect.text = std::string(parts[3]);
		effects.push_back(std::move(effect));
	}
	return effects;
}

/** Drops the bundle's uniques a previous load added, so loading again doesn't add them twice. */
void RemoveLoadedUniques()
{
	while (!UniqueItems.empty() && UniqueItems.back().mappingId >= Poe2UniqueMappingId) {
		UniqueItemMappingIdsToIndices.erase(UniqueItems.back().mappingId);
		UniqueItems.pop_back();
	}
	Uniques.clear();
}

/** A value from the item's seed, apart from the game's random number generator and the base's implicits. */
uint32_t EffectHash(uint32_t seed, size_t index)
{
	uint32_t hash = (seed ^ 0x2C1B3C6DU) + (static_cast<uint32_t>(index) * 0x9E3779B9U);
	hash ^= hash >> 16;
	hash *= 0x7FEB352DU;
	hash ^= hash >> 15;
	hash *= 0x846CA68BU;
	hash ^= hash >> 16;
	return hash;
}

} // namespace

void LoadPoe2Uniques()
{
	RemoveLoadedUniques();
	if (!gbIsPoe2)
		return;

	constexpr std::string_view Filename = "txtdata\\poe2\\uniques.tsv";
	auto dataFile = DataFile::load(Filename);
	if (!dataFile.has_value()) {
		LogError("Couldn't load the uniques: {}", Filename);
		return;
	}
	dataFile->skipHeaderOrDie(Filename);
	int32_t mappingId = Poe2UniqueMappingId;
	for (DataFileRecord record : *dataFile) {
		RecordReader reader { record, Filename };
		Poe2Unique unique;
		std::string baseName;
		std::string text;
		int minLevel = 1;
		int value = 0;
		reader.readString("name", unique.name);
		reader.readString("base", baseName);
		reader.readString("tier", text);
		const std::optional<UniqueTier> tier = ParseTier(text);
		reader.readString("pool", unique.pool);
		reader.readInt("minLevel", minLevel);
		reader.readInt("dropLevel", unique.dropLevel);
		reader.readInt("value", value);
		reader.readString("effects", text);
		unique.effects = ParseEffects(text);
		// The mapping id follows the row, so a row that can't load doesn't shift the ones after it.
		const int32_t id = mappingId++;
		for (size_t i = 0; i < AllItemsList.size() && unique.baseItem < 0; i++) {
			if (AllItemsList[i].iName == baseName && AllItemsList[i].iMiscId != IMISC_UNIQUE)
				unique.baseItem = static_cast<int>(i);
		}
		if (unique.baseItem < 0 || !tier) {
			LogError("Unique {} has no base {} or tier {}", unique.name, baseName, text);
			continue;
		}
		unique.tier = *tier;

		UniqueItem &item = UniqueItems.emplace_back();
		item.UIName = unique.name;
		item.UICurs = ICURS_DEFAULT;
		// No Diablo base type: a bundle unique is only made on its own base, by its place in Poe2UniquesOnBase.
		item.UIItemId = UITYPE_INVALID;
		item.UIMinLvl = static_cast<int8_t>(std::clamp(minLevel, 1, 127));
		item.UINumPL = 0;
		item.UIValue = value;
		item.mappingId = id;
		UniqueItemMappingIdsToIndices[id] = static_cast<int32_t>(UniqueItems.size()) - 1;
		unique.uid = static_cast<int>(UniqueItems.size()) - 1;
		Uniques.push_back(std::move(unique));
	}
}

const std::vector<Poe2Unique> &Poe2Uniques()
{
	return Uniques;
}

const Poe2Unique *Poe2UniqueFor(int uid)
{
	if (Uniques.empty() || uid < Uniques.front().uid || uid > Uniques.back().uid)
		return nullptr;
	return &Uniques[static_cast<size_t>(uid - Uniques.front().uid)];
}

const Poe2Unique *FindPoe2Unique(std::string_view name)
{
	for (const Poe2Unique &unique : Uniques) {
		if (unique.name == name)
			return &unique;
	}
	return nullptr;
}

std::vector<int> Poe2UniquesOnBase(int itemIndex)
{
	std::vector<int> uids;
	for (const Poe2Unique &unique : Uniques) {
		if (unique.baseItem == itemIndex)
			uids.push_back(unique.uid);
	}
	return uids;
}

std::vector<std::pair<const UniqueEffect *, int>> RollUniqueEffects(const Item &item)
{
	std::vector<std::pair<const UniqueEffect *, int>> rolled;
	if (item._iMagical != ITEM_QUALITY_UNIQUE)
		return rolled;
	const Poe2Unique *unique = Poe2UniqueFor(item._iUid);
	if (unique == nullptr)
		return rolled;
	for (size_t i = 0; i < unique->effects.size(); i++) {
		const UniqueEffect &effect = unique->effects[i];
		const int span = effect.max - effect.min + 1;
		const int value = effect.min + (span > 1 ? static_cast<int>(EffectHash(static_cast<uint32_t>(item._iSeed), i) % static_cast<uint32_t>(span)) : 0);
		rolled.emplace_back(&effect, value);
	}
	return rolled;
}

void ApplyPoe2UniqueEffects(Item &item)
{
	for (const auto &[effect, value] : RollUniqueEffects(item)) {
		if (effect->mod != ModEffect::None)
			ApplyModEffect(item, effect->mod, value, value);
	}
}

void AddUniqueRules(const Item &item, UniqueRuleTotals &totals)
{
	for (const auto &[effect, value] : RollUniqueEffects(item)) {
		if (effect->rule == UniqueRule::None || effect->rule == UniqueRule::Text)
			continue;
		int16_t &total = totals[static_cast<size_t>(effect->rule)];
		total = static_cast<int16_t>(std::clamp(total + value, -30000, 30000));
	}
}

std::vector<std::string> Poe2UniqueText(const Item &item)
{
	std::vector<std::string> lines;
	for (const auto &[effect, value] : RollUniqueEffects(item))
		lines.push_back(FormatRuntime(effect->text, value));
	return lines;
}

} // namespace devilution
