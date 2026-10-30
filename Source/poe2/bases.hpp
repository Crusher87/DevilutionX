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
#include "poe2/player_data.hpp"

namespace devilution {

struct Item;
struct Player;

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
	/** @brief How a hybrid's defence divides between its kinds; none for single-kind bases. */
	DefenseSplit split;
	/** @brief The character level the base needs, like POE2's required level. */
	int level = 0;
	/** @brief A shield's own chance to block, in percent; 0 for every other base. */
	uint8_t block = 0;
	/** @brief A weapon's attacks per second in hundredths, like POE2's (1.35 is 135); 0 for every other base. */
	uint16_t aps = 0;
	/** @brief The skill a caster weapon of the rework grants (column `grants`); empty for every other base. */
	SkillSlot grantedSkill {};
	/** @brief The element spell modifiers the base is locked to, as ModBaseTag element bits (column `element`); 0 for none. */
	uint16_t elementLock = 0;
	/** @brief POE2's critical hit chance in tenths of a percent (column `crit`); -1 when the cell is empty (the default). */
	int16_t crit = -1;
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
/** @brief The skill a `grants` key names: "StaffBolt" (the default attack), a WeaponSkill or a PersistentSkill; empty if none. */
SkillSlot ParseGrantedSkill(std::string_view key);
/** @brief The element bits an `element` cell names (fire, cold, lightning, physical, comma separated); 0 for none. */
uint16_t ParseElementLock(std::string_view text);
/** @brief The base metadata of a row of the item data, or nullptr for a base without any. */
const BaseMeta *BaseMetaFor(int itemIndex);
/** @brief The base metadata of the item's base, or nullptr for a base without any. */
const BaseMeta *ItemBaseMeta(const Item &item);
/**
 * @brief The shield's own chance to block, in percent: its POE2 base's, or a POE2-like one for Diablo's shields (25% for a
 * buckler or small shield up to 30% for a large or tower shield). 0 for a focus and anything that isn't a shield.
 */
int ShieldBlockChance(const Item &item);
/** @brief The item's implicits with their values, rolled from its seed and raised by its quality. */
std::vector<ItemImplicit> ItemImplicits(const Item &item);
/** @brief The effects the item's implicits raise, as ModEffectBits; one that lowers (slower attacks) doesn't count. */
ModEffectMask ImplicitEffectBits(const Item &item);
/** @brief Applies the base's implicits and quality to a new item. */
void ApplyBaseProperties(Item &item);
/** @brief The implicits' tooltip lines. */
std::vector<std::string> ItemImplicitText(const Item &item);

/**
 * @brief The character level the item needs, like POE2's: its base's, and for a unique the higher of that and the
 * unique's own drop level. 0 outside POE2 mode and for bases without metadata (Diablo's bases and their uniques, gems,
 * flasks and charms). Worked out from the base row, so nothing is saved.
 */
int ItemRequiredLevel(const Item &item);
/** @brief Whether the hero's level is enough for the item; an item that needs more is unusable, like one that needs more Strength. */
bool MeetsItemLevelRequirement(const Player &player, const Item &item);

/**
 * @brief Like POE2, the first base of each line needs no attributes, and the needs grow with the base's level from
 * there: a base of this level or below needs none (Curtis 2026-10-30: "5 Mag" and "3 Mag, 3 Dex" on starter gear).
 */
constexpr int MaxStarterBaseLevel = 1;
/** @brief Whether the item is on a POE2 base of MaxStarterBaseLevel or below, which needs no attributes. */
bool IsStarterBase(const Item &item);
/** @brief Clears an item's attribute needs if it is on a starter base, for items saved while those bases still had a few points. */
void ClearStarterBaseRequirements(Item &item);

/**
 * @brief How the item's defence (from its base, without flat modifiers) divides: by its base's split, or evenly for an
 * item whose defence is below its base's, which was saved before the bases took POE2's numbers and keeps its old split.
 */
DefenseSplit ItemDefenseSplit(const Item &item, int defense);

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
/**
 * @brief The quality ApplyBaseProperties added to the item's _iPLAC (QualityKind::Defence) or _iPLDam (Weapon), where
 * saves keep it with the local % increases; 0 for another kind.
 */
int LocalQuality(const Item &item, QualityKind kind);
/**
 * @brief A weapon's % increased physical damage: its local % increases and its quality, which POE2 multiplies,
 * (1 + increased) x (1 + quality). Before identifying, only the quality, which isn't a hidden modifier.
 */
int WeaponDamagePercent(const Item &item);

} // namespace devilution
