/**
 * @file poe2/loot_filter.hpp
 *
 * POE2 Mashup loot filter, after NeverSink's POE2 filter (Curtis 2026-11-05: "I want the user to be able to choose
 * from these level of filters ... I want the old loot filter removed and be replaced with this one").
 *  - The Loot Filter option is Off or one of NeverSink's seven strictness levels, Soft to Uber Plus Strict.
 *  - With a level on, each ground item gets a style: its name label's text, border and background colours, a light
 *    beam (some only for a few seconds after it is seen), a drop sound, a map icon, and the strictness level from which
 *    it is hidden. The styles are NeverSink's own colours.
 *  - Which item gets which style is data: txtdata/poe2/loot_filter_styles.tsv holds the styles and
 *    txtdata/poe2/loot_filter_items.tsv sends items to them by name (with * wildcards). Items no name row catches get
 *    their kind's style (rares, uniques by tier, uncut gems by level, ...). Anything left over shows in NeverSink's
 *    pink "unknown item" style, so nothing new goes missing quietly.
 *  - It only changes what is drawn and played on this screen: nothing is saved or sent over the network.
 */
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "DiabloUI/ui_flags.hpp"
#include "engine/point.hpp"
#include "options.h"
#include "poe2/corner_map.hpp"
#include "sound_effect_enums.h"

namespace devilution {

struct Item;

struct LootRgb {
	uint8_t r = 0;
	uint8_t g = 0;
	uint8_t b = 0;

	bool operator==(const LootRgb &other) const = default;
};

/** @brief One loot filter style, a row of loot_filter_styles.tsv. */
struct LootStyle {
	std::string key;
	/** @brief The name's colour: one of the game's text colours. */
	UiFlags text = UiFlags::ColorWhite;
	std::optional<LootRgb> border;
	std::optional<LootRgb> background;
	/** @brief NeverSink's biggest labels: a thicker border. */
	bool big = false;
	/** @brief The light beam's colour; none without one. */
	std::optional<LootRgb> beam;
	/** @brief The beam shows only for a few seconds after the item is first seen (NeverSink's "Temp" beams). */
	bool beamTemp = false;
	/** @brief The sound played as it drops, a Diablo sound by name; none for silence. */
	std::optional<SfxID> sound;
	/** @brief Map icon size: 0 big, 1 medium, 2 small; none without an icon. */
	std::optional<uint8_t> iconSize;
	LootRgb iconColour;
	MapIconShape iconShape = MapIconShape::Circle;
	/** @brief The first strictness level that hides the item; Off for never. */
	LootFilter hideFrom = LootFilter::Off;
};

/** @brief How long a "Temp" beam shows after its item is first seen, in ms. */
constexpr float LootTempBeamMs = 4000;

/** @brief Reads the two tables (lazily, on first use); POE2 mode only. */
void LoadLootFilter();
/** @brief The style key the item falls in (tests and the console). */
std::string LootFilterStyleKey(const Item &item);
/** @brief The style with this key, or nullptr. */
const LootStyle *FindLootStyle(std::string_view key);
/** @brief The item's style under the Loot Filter option: nullptr when the filter is off, outside POE2 mode, or for an empty item. */
const LootStyle *LootFilterStyle(const Item &item);

/** @brief Whether the filter at this level hides this ground item. Quest items and unknown items never hide. */
bool IsItemHiddenByLootFilter(const Item &item, LootFilter filter);
/** @brief Same, using the Loot Filter gameplay option. */
bool IsItemHiddenByLootFilter(const Item &item);

/**
 * @brief The faint rarity outline every ground item gets in POE2 mode so it stands out from the floor.
 *
 * A palette index, or nothing when the item gets no outline (outside POE2 mode). The item under the cursor keeps its
 * brighter hover outline instead. With the loot filter on, the outline takes the item's filter colour.
 */
std::optional<uint8_t> GroundItemOutlineColor(const Item &item);

/** @brief The palette colour nearest to an RGB colour, from the colours every level's palette shares (128 to 255). */
uint8_t NearestLootPaletteColour(LootRgb colour);

/** @brief The item's beam colour now: none when it has no beam, the Beams option is off, or a Temp beam has run out. */
std::optional<LootRgb> LootBeamColour(const Item &item, float msSinceSeen);

/** @brief Plays the item's drop sound, if its style has one and the Sounds option is on. Called as it lands. */
void PlayLootFilterDropSound(const Item &item);

/** @brief A ground item's icon on the maps. */
struct LootMapIcon {
	Point tile;
	MapIconStyle style;
	/** @brief 0 big, 1 medium, 2 small. */
	uint8_t size;
};
/**
 * @brief Whether the hero has had the ground item's tile in sight on this floor, or it lies in town. Its name label and
 * map icon show only then: never behind a closed door or in a room not yet seen (Curtis 2026-11-05 and 2026-11-07).
 */
bool HeroHasSeenGroundItem(const Item &item);
/** @brief The icons for the ground items on explored tiles, when the filter and its Map Icons option are on. */
std::vector<LootMapIcon> CollectLootMapIcons();

/**
 * @brief Darkens a ground outline to the light on the item's tile, so items in the dark stay hidden.
 *
 * Takes a shade from a 16-shade palette group and the tile's light table index (0 brightest, 15 black); the outline
 * fades a shade per two light steps and is gone where the floor is near black.
 */
std::optional<uint8_t> LitGroundOutline(uint8_t color, int lightTableIndex);

} // namespace devilution
