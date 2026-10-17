/**
 * @file poe2/bases.hpp
 *
 * POE2 Mashup item bases from the itemization tables: their defence line, how quality works on them,
 * and their implicits, including the attack or cast speed of heavy, swift, focused and quick bases.
 */
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "poe2/defenses.hpp"
#include "poe2/modifiers.hpp"

namespace devilution {

struct Item;

/** @brief What a base's quality improves. */
enum class QualityKind : uint8_t {
	None,
	/** Physical damage, on martial weapons. */
	Weapon,
	/** Armour, evasion and energy shield. */
	Defence,
	/** The spell power (Intelligence) of staves and sceptres. */
	Caster,
};

struct BaseImplicit {
	ModEffect effect;
	int16_t min;
	int16_t max;
	/** @brief Tooltip text, with {0} and {1} for the values. */
	std::string text;
};

struct BaseMeta {
	std::string name;
	/** @brief The bundle's item class, like one_hand_sword. */
	std::string itemClass;
	/** @brief heavy, balanced, swift, focused, rapid or a defence line. */
	std::string identity;
	std::optional<DefenseType> defense;
	QualityKind quality;
	std::vector<BaseImplicit> implicits;
};

/** @brief A rolled implicit: its values on this item. */
struct ItemImplicit {
	const BaseImplicit *implicit;
	std::array<int, 2> values;
};

/** @brief POE2: quality goes to 20%, Exceptional bases to 30%. */
constexpr int MaxItemQuality = 20;
constexpr int MaxExceptionalQuality = 30;
/** @brief Exceptional bases drop at POE2 item level 78 and up. */
constexpr int ExceptionalItemLevel = 78;
/** @brief In ten thousand eligible drops. */
constexpr int ExceptionalChancePer10000 = 25;
/** @brief Percent of normal and magic equipment drops that come with some quality. */
constexpr int QualityDropPercent = 10;
constexpr int MaxDropQuality = 10;

/** @brief Loads the base metadata of the POE2 Mashup mod, or clears it outside POE2 mode. Needs the item data. */
void LoadBaseData();
/** @brief The base metadata of a row of the item data, or nullptr for a base without any. */
const BaseMeta *BaseMetaFor(int itemIndex);
/** @brief The base metadata of the item's base, or nullptr for a base without any. */
const BaseMeta *ItemBaseMeta(const Item &item);
/** @brief The item's implicits with their values, rolled from its seed and raised by its quality. */
std::vector<ItemImplicit> ItemImplicits(const Item &item);
/** @brief Applies the base's implicits and quality to a new item. */
void ApplyBaseProperties(Item &item);
/** @brief The implicits' tooltip lines. */
std::vector<std::string> ItemImplicitText(const Item &item);

/**
 * @brief What quality does on a base: from its metadata, else from its kind of item, so Diablo's body armours
 * and older bases take quality too.
 */
QualityKind QualityKindFor(int itemIndex);
QualityKind ItemQualityKind(const Item &item);
/** @brief The item's quality, 0 to 30; stored on equipment only. */
int ItemQuality(const Item &item);
void SetItemQuality(Item &item, int quality);
/** @brief An Exceptional base: quality above the normal 20%. */
bool IsExceptional(const Item &item);
/** @brief Whether quality does anything on the item's base. */
bool HasQuality(const Item &item);

} // namespace devilution
