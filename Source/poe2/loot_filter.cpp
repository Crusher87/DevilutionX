#include "poe2/loot_filter.hpp"

#include <algorithm>
#include <climits>
#include <string>
#include <unordered_map>

#include "automap.h"
#include "data/file.hpp"
#include "data/record_reader.hpp"
#include "effects.h"
#include "engine/palette.h"
#include "game_mode.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "parse_sfx_id.hpp"
#include "poe2/bases.hpp"
#include "poe2/charms.hpp"
#include "poe2/crafting_items.hpp"
#include "poe2/currency.hpp"
#include "poe2/endgame.hpp"
#include "poe2/flasks.hpp"
#include "poe2/gemcutting.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/stacking.hpp"
#include "poe2/uniques.hpp"
#include "poe2/waystones.hpp"
#include "utils/is_of.hpp"
#include "utils/log.hpp"

namespace devilution {

namespace {

constexpr std::string_view StylesFile = "txtdata\\poe2\\loot_filter_styles.tsv";
constexpr std::string_view ItemsFile = "txtdata\\poe2\\loot_filter_items.tsv";

/** @brief The style anything else falls in: NeverSink's pink and cyan "unknown item". */
constexpr std::string_view UnknownKey = "unknown";

struct NameRule {
	/** @brief An item name, with * matching any run of letters. */
	std::string pattern;
	std::string style;
};

std::vector<LootStyle> Styles;
std::vector<NameRule> NameRules;
bool Loaded = false;
/** @brief Each item's style key, as items are styled several times a frame: by seed, base, creation and whether it is identified. */
std::unordered_map<uint64_t, std::string> KeyCache;

uint64_t CacheKey(const Item &item)
{
	return (static_cast<uint64_t>(static_cast<uint32_t>(item._iSeed)) << 32)
	    ^ (static_cast<uint64_t>(static_cast<uint16_t>(item.IDidx)) << 16)
	    ^ static_cast<uint64_t>(item._iCreateInfo) ^ (item._iIdentified ? 1U << 15 : 0U)
	    ^ (static_cast<uint64_t>(item._iMagical) << 12) ^ (static_cast<uint64_t>(item._iClass) << 48);
}

constexpr std::array<std::pair<std::string_view, UiFlags>, 9> TextColours { {
	{ "white", UiFlags::ColorWhite },
	{ "black", UiFlags::ColorBlack },
	{ "red", UiFlags::ColorRed },
	{ "yellow", UiFlags::ColorYellow },
	{ "gold", UiFlags::ColorGold },
	{ "whitegold", UiFlags::ColorWhitegold },
	{ "orange", UiFlags::ColorOrange },
	{ "blue", UiFlags::ColorBlue },
	{ "green", UiFlagsColorGreen },
} };

constexpr std::array<std::pair<std::string_view, MapIconShape>, 11> IconShapes { {
	{ "circle", MapIconShape::Circle },
	{ "diamond", MapIconShape::Diamond },
	{ "square", MapIconShape::Square },
	{ "triangle", MapIconShape::TriangleUp },
	{ "triangle_down", MapIconShape::TriangleDown },
	{ "cross", MapIconShape::Cross },
	{ "star", MapIconShape::Star },
	{ "hexagon", MapIconShape::Hexagon },
	{ "pentagon", MapIconShape::Pentagon },
	{ "kite", MapIconShape::Kite },
	{ "moon", MapIconShape::Moon },
} };

constexpr std::array<std::pair<std::string_view, LootFilter>, 8> Levels { {
	{ "never", LootFilter::Off },
	{ "soft", LootFilter::Soft },
	{ "regular", LootFilter::Regular },
	{ "semistrict", LootFilter::SemiStrict },
	{ "strict", LootFilter::Strict },
	{ "verystrict", LootFilter::VeryStrict },
	{ "uberstrict", LootFilter::UberStrict },
	{ "uberplusstrict", LootFilter::UberPlusStrict },
} };

template <typename T, size_t N>
std::optional<T> Lookup(const std::array<std::pair<std::string_view, T>, N> &table, std::string_view name)
{
	for (const auto &[key, value] : table) {
		if (key == name)
			return value;
	}
	return std::nullopt;
}

/** @brief "r g b", or "-" for none. */
std::optional<LootRgb> ParseRgb(std::string_view text)
{
	if (text.empty() || text == "-")
		return std::nullopt;
	std::array<int, 3> parts {};
	size_t part = 0;
	int value = -1;
	for (const char c : std::string(text) + " ") {
		if (c >= '0' && c <= '9') {
			value = (value < 0 ? 0 : value * 10) + (c - '0');
		} else if (value >= 0) {
			if (part < parts.size())
				parts[part] = std::min(value, 255);
			part++;
			value = -1;
		}
	}
	if (part != 3)
		return std::nullopt;
	return LootRgb { static_cast<uint8_t>(parts[0]), static_cast<uint8_t>(parts[1]), static_cast<uint8_t>(parts[2]) };
}

/** @brief Whether a name matches a pattern where * stands for any run of letters. */
bool MatchesPattern(std::string_view name, std::string_view pattern)
{
	const size_t star = pattern.find('*');
	if (star == std::string_view::npos)
		return name == pattern;
	const std::string_view head = pattern.substr(0, star);
	if (name.substr(0, head.size()) != head)
		return false;
	const std::string_view rest = pattern.substr(star + 1);
	if (rest.empty())
		return true;
	for (size_t at = head.size(); at <= name.size(); at++) {
		if (MatchesPattern(name.substr(at), rest))
			return true;
	}
	return false;
}

void LoadStyles()
{
	auto dataFile = DataFile::load(StylesFile);
	if (!dataFile.has_value()) {
		LogError("Couldn't load the loot filter styles: {}", StylesFile);
		return;
	}
	dataFile->skipHeaderOrDie(StylesFile);
	for (DataFileRecord record : *dataFile) {
		RecordReader reader { record, StylesFile };
		LootStyle style;
		std::string text, border, background, big, beam, beamTemp, sound, iconSize, iconColour, iconShape, hideFrom;
		reader.readString("key", style.key);
		reader.readString("text", text);
		reader.readString("border", border);
		reader.readString("background", background);
		reader.readString("big", big);
		reader.readString("beam", beam);
		reader.readString("beamTemp", beamTemp);
		reader.readString("sound", sound);
		reader.readString("iconSize", iconSize);
		reader.readString("iconColour", iconColour);
		reader.readString("iconShape", iconShape);
		reader.readString("hideFrom", hideFrom);
		if (const std::optional<UiFlags> colour = Lookup(TextColours, text))
			style.text = *colour;
		else
			LogError("Unknown text colour in {}: {}", StylesFile, text);
		style.border = ParseRgb(border);
		style.background = ParseRgb(background);
		style.big = big == "1";
		style.beam = ParseRgb(beam);
		style.beamTemp = beamTemp == "1";
		if (!sound.empty() && sound != "-") {
			if (const auto sfx = ParseSfxId(sound); sfx.has_value())
				style.sound = *sfx;
			else
				LogError("Unknown sound in {}: {}", StylesFile, sound);
		}
		if (IsAnyOf(iconSize, "0", "1", "2")) {
			style.iconSize = static_cast<uint8_t>(iconSize[0] - '0');
			style.iconColour = ParseRgb(iconColour).value_or(LootRgb { 255, 255, 255 });
			if (const std::optional<MapIconShape> shape = Lookup(IconShapes, iconShape))
				style.iconShape = *shape;
			else
				LogError("Unknown icon shape in {}: {}", StylesFile, iconShape);
		}
		if (const std::optional<LootFilter> level = Lookup(Levels, hideFrom))
			style.hideFrom = *level;
		else
			LogError("Unknown strictness in {}: {}", StylesFile, hideFrom);
		Styles.push_back(std::move(style));
	}
}

void LoadNameRules()
{
	auto dataFile = DataFile::load(ItemsFile);
	if (!dataFile.has_value()) {
		LogError("Couldn't load the loot filter items: {}", ItemsFile);
		return;
	}
	dataFile->skipHeaderOrDie(ItemsFile);
	for (DataFileRecord record : *dataFile) {
		RecordReader reader { record, ItemsFile };
		NameRule rule;
		reader.readString("name", rule.pattern);
		reader.readString("style", rule.style);
		if (FindLootStyle(rule.style) == nullptr)
			LogError("Unknown style in {}: {}", ItemsFile, rule.style);
		NameRules.push_back(std::move(rule));
	}
}

std::optional<std::string> StyleByName(const Item &item)
{
	const std::string_view names[] = {
		item._iIName,
		item._iName,
		item.IDidx >= 0 && static_cast<size_t>(item.IDidx) < AllItemsList.size() ? std::string_view(AllItemsList[item.IDidx].iName) : std::string_view(),
	};
	for (const NameRule &rule : NameRules) {
		for (const std::string_view name : names) {
			if (!name.empty() && MatchesPattern(name, rule.pattern))
				return rule.style;
		}
	}
	return std::nullopt;
}

bool IsGear(const Item &item)
{
	return IsAnyOf(item._iClass, ICLASS_WEAPON, ICLASS_ARMOR) || IsAnyOf(item._itype, ItemType::Ring, ItemType::Amulet);
}

std::string_view UniqueKey(const Item &item)
{
	const Poe2Unique *unique = Poe2UniqueFor(item._iUid);
	if (unique == nullptr)
		return "unique_t3";
	switch (unique->tier) {
	case UniqueTier::Mythic:
		return "unique_t1";
	case UniqueTier::Rare:
		return "unique_t2";
	case UniqueTier::Uncommon:
		return "unique_t3";
	default:
		return "unique_low";
	}
}

/** @brief The style key of the item's kind, when no name row caught it. */
std::string_view KindKey(const Item &item)
{
	if (item._iClass == ICLASS_QUEST)
		return "quest";
	if (item._itype == ItemType::Gold)
		return "gold";
	if (IsUncutGem(item)) {
		const int level = UncutGemLevel(item);
		return level >= 20 ? "gem_top" : level >= 19 ? "gem_high"
		                                              : "gem";
	}
	if (item.IDidx >= 0 && static_cast<size_t>(item.IDidx) < AllItemsList.size() && IsSkillGem(item))
		return "gem";
	if (IsJewel(item))
		return item._iMagical == ITEM_QUALITY_RARE ? "jewel_rare" : "jewel_magic";
	if (IsWaystone(item) || IsTablet(item) || IsMapFragment(item) || IsMapKey(item))
		return "waystone";
	if (IsCharm(item))
		return "charm";
	if (IsFlaskPotion(item))
		return "flask";
	if (item._iMagical == ITEM_QUALITY_UNIQUE)
		return UniqueKey(item);
	if (IsGear(item)) {
		const bool jewellery = IsAnyOf(item._itype, ItemType::Ring, ItemType::Amulet);
		switch (item._iMagical) {
		case ITEM_QUALITY_RARE:
			return jewellery ? "rare_jewellery" : "rare";
		case ITEM_QUALITY_MAGIC:
			return jewellery ? "magic_jewellery" : "magic";
		default:
			return IsExceptional(item) ? "exceptional" : "normal";
		}
	}
	if (item._iClass == ICLASS_MISC && (IsCurrencyMiscId(item._iMiscId) || IsShard(item) || IsCraftingVariantItem(item)))
		return UnknownKey;
	if (item._iClass == ICLASS_MISC)
		return "misc";
	return UnknownKey;
}

} // namespace

void LoadLootFilter()
{
	Styles.clear();
	NameRules.clear();
	KeyCache.clear();
	Loaded = true;
	if (!gbIsPoe2)
		return;
	LoadStyles();
	LoadNameRules();
}

const LootStyle *FindLootStyle(std::string_view key)
{
	for (const LootStyle &style : Styles) {
		if (style.key == key)
			return &style;
	}
	return nullptr;
}

std::string LootFilterStyleKey(const Item &item)
{
	if (!Loaded)
		LoadLootFilter();
	if (item.isEmpty())
		return {};
	// Items made in code (tests, the console) have no seed, so only real drops are remembered.
	const bool cacheable = item._iSeed != 0;
	const uint64_t cacheKey = CacheKey(item);
	if (cacheable) {
		if (const auto it = KeyCache.find(cacheKey); it != KeyCache.end())
			return it->second;
	}
	std::string key;
	// Quest items and gold go by their kind, whatever they're called.
	if (item._iClass != ICLASS_QUEST && item._itype != ItemType::Gold)
		key = StyleByName(item).value_or(std::string());
	if (key.empty())
		key = KindKey(item);
	if (cacheable) {
		if (KeyCache.size() > 4096)
			KeyCache.clear();
		KeyCache.emplace(cacheKey, key);
	}
	return key;
}

const LootStyle *LootFilterStyle(const Item &item)
{
	if (!gbIsPoe2 || item.isEmpty() || *GetOptions().Gameplay.lootFilter == LootFilter::Off)
		return nullptr;
	const std::string key = LootFilterStyleKey(item);
	if (const LootStyle *style = FindLootStyle(key))
		return style;
	return FindLootStyle(UnknownKey);
}

bool IsItemHiddenByLootFilter(const Item &item, LootFilter filter)
{
	if (!gbIsPoe2 || filter == LootFilter::Off || item.isEmpty() || item._iClass == ICLASS_QUEST)
		return false;
	const LootStyle *style = FindLootStyle(LootFilterStyleKey(item));
	if (style == nullptr || style->hideFrom == LootFilter::Off)
		return false;
	return static_cast<uint8_t>(filter) >= static_cast<uint8_t>(style->hideFrom);
}

bool IsItemHiddenByLootFilter(const Item &item)
{
	return IsItemHiddenByLootFilter(item, *GetOptions().Gameplay.lootFilter);
}

std::optional<uint8_t> GroundItemOutlineColor(const Item &item)
{
	if (!gbIsPoe2 || item.isEmpty())
		return std::nullopt;
	// With the loot filter on, the outline matches the label: its border, else its background.
	if (const LootStyle *style = LootFilterStyle(item)) {
		if (style->border)
			return NearestLootPaletteColour(*style->border);
		if (style->background)
			return NearestLootPaletteColour(*style->background);
	}
	// Darker shades than the hover outline, so the floor reads busy only where loot is.
	if (item._iClass == ICLASS_QUEST)
		return PAL16_ORANGE + 2;
	if (item._itype == ItemType::Gold)
		return PAL16_YELLOW + 8;
	switch (item._iMagical) {
	case ITEM_QUALITY_MAGIC:
		return PAL16_BLUE + 6;
	case ITEM_QUALITY_RARE:
		return PAL16_YELLOW + 4;
	case ITEM_QUALITY_UNIQUE:
		return PAL16_ORANGE + 4;
	default:
		break;
	}
	if (item._iClass == ICLASS_MISC && IsCurrencyMiscId(item._iMiscId))
		return PAL16_BEIGE + 4;
	return PAL16_GRAY + 9;
}

uint8_t NearestLootPaletteColour(LootRgb colour)
{
	// Only the UI's half of the palette: the lower half is the level's own, and some of it animates (lava, water).
	static std::unordered_map<uint32_t, uint8_t> cache;
	static uint32_t cachedFor = 0;
	uint32_t checksum = 0;
	for (size_t i = 128; i < 256; i++)
		checksum = (checksum * 31) + logical_palette[i].r + (logical_palette[i].g << 8) + (logical_palette[i].b << 16);
	if (checksum != cachedFor) {
		cache.clear();
		cachedFor = checksum;
	}
	const uint32_t key = colour.r | (colour.g << 8) | (colour.b << 16);
	if (const auto it = cache.find(key); it != cache.end())
		return it->second;
	int best = PAL16_GRAY;
	int bestDistance = INT_MAX;
	for (int i = 128; i < 256; i++) {
		const SDL_Color &candidate = logical_palette[i];
		const int dr = candidate.r - colour.r;
		const int dg = candidate.g - colour.g;
		const int db = candidate.b - colour.b;
		// Green weighs most, as the eye sees it.
		const int distance = (2 * dr * dr) + (4 * dg * dg) + (3 * db * db);
		if (distance < bestDistance) {
			bestDistance = distance;
			best = i;
		}
	}
	cache[key] = static_cast<uint8_t>(best);
	return static_cast<uint8_t>(best);
}

std::optional<LootRgb> LootBeamColour(const Item &item, float msSinceSeen)
{
	if (!*GetOptions().Gameplay.lootFilterBeams)
		return std::nullopt;
	const LootStyle *style = LootFilterStyle(item);
	if (style == nullptr || !style->beam || IsItemHiddenByLootFilter(item))
		return std::nullopt;
	if (style->beamTemp && msSinceSeen > LootTempBeamMs)
		return std::nullopt;
	return style->beam;
}

void PlayLootFilterDropSound(const Item &item)
{
	if (!*GetOptions().Gameplay.lootFilterSounds)
		return;
	const LootStyle *style = LootFilterStyle(item);
	if (style == nullptr || !style->sound || IsItemHiddenByLootFilter(item))
		return;
	PlaySfxLoc(*style->sound, item.position);
}

std::vector<LootMapIcon> CollectLootMapIcons()
{
	std::vector<LootMapIcon> icons;
	if (!gbIsPoe2 || *GetOptions().Gameplay.lootFilter == LootFilter::Off || !*GetOptions().Gameplay.lootFilterMapIcons)
		return icons;
	for (int i = 0; i < ActiveItemCount; i++) {
		const Item &item = Items[ActiveItems[i]];
		const LootStyle *style = LootFilterStyle(item);
		if (style == nullptr || !style->iconSize || IsItemHiddenByLootFilter(item))
			continue;
		// Only loot the hero has seen: on a tile they have had in sight (Curtis 2026-11-05: nothing out of view).
		const Point tile = item.position;
		if (!InDungeonBounds(tile) || !HasAnyOf(dFlags[tile.x][tile.y], DungeonFlag::Explored))
			continue;
		MapIconStyle iconStyle;
		iconStyle.shape = style->iconShape;
		iconStyle.color = NearestLootPaletteColour(style->iconColour);
		icons.push_back({ tile, std::move(iconStyle), *style->iconSize });
	}
	return icons;
}

std::optional<uint8_t> LitGroundOutline(uint8_t color, int lightTableIndex)
{
	if (lightTableIndex >= 12)
		return std::nullopt;
	// The loot filter's colours can also come from the 8-shade groups (128 to 159): darken within those, not into the next.
	if (color >= 128 && color < 160) {
		const int shade8 = std::min((color & 7) + std::max(lightTableIndex, 0) / 4, 7);
		return static_cast<uint8_t>((color & ~7) | shade8);
	}
	const int shade = std::min((color & 15) + std::max(lightTableIndex, 0) / 2, 15);
	return static_cast<uint8_t>((color & ~15) | shade);
}

} // namespace devilution
