/**
 * @file items.h
 *
 * Interface of item functionality.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "DiabloUI/ui_flags.hpp"
#include "cursor.h"
#include "cursor_defs.hpp"
#include "engine/animationinfo.h"
#include "engine/point.hpp"
#include "engine/surface.hpp"
#include "game_mode.hpp"
#include "levels/dun_tile.hpp"
#include "monster.h"
#include "poe2/craft_types.hpp"
#include "poe2/item_stats.hpp"
#include "tables/itemdat.h"
#include "utils/fixed_point.hpp"
#include "utils/is_of.hpp"
#include "utils/string_or_view.hpp"

namespace devilution {

enum class MapReward : uint8_t;
enum class GambleFamily : uint8_t;

#define MAXITEMS 127
#define ITEMTYPES 43

#define GOLD_SMALL_LIMIT 1000
#define GOLD_MEDIUM_LIMIT 2500
#define GOLD_MAX_LIMIT 5000

// Item indestructible durability
#define DUR_INDESTRUCTIBLE 255

constexpr int ItemNameLength = 64;
constexpr int MaxVendorValue = 140000;
constexpr int MaxVendorValueHf = 200000;
constexpr int MaxBoyValue = 90000;
constexpr int MaxBoyValueHf = 200000;

/** Maximum number of affixes on a rare item (up to 3 prefixes and 3 suffixes). */
constexpr size_t MaxRareAffixes = 6;

/** POE2 Mashup: a modifier rolled from the itemization tables (see poe2/modifiers.hpp). */
struct Poe2ItemMod {
	/** Index of the modifier family plus 1; 0 for none. */
	uint8_t family = 0;
	/** 1 is the strongest. */
	uint8_t tier = 0;
	/** The rolled values, already scaled to the game. */
	std::array<int16_t, 2> values {};
};

enum item_quality : uint8_t {
	ITEM_QUALITY_NORMAL,
	ITEM_QUALITY_MAGIC,
	ITEM_QUALITY_UNIQUE,
	/** POE2 Mashup: several prefixes and suffixes and a random name. */
	ITEM_QUALITY_RARE,
};

enum _unique_items : int32_t {
	UITEM_CLEAVER,
	UITEM_SKCROWN,
	UITEM_INFRARING,
	UITEM_OPTAMULET,
	UITEM_TRING,
	UITEM_HARCREST,
	UITEM_STEELVEIL,
	UITEM_ARMOFVAL,
	UITEM_GRISWOLD,
	UITEM_BOVINE,
	UITEM_RIFTBOW,
	UITEM_NEEDLER,
	UITEM_CELESTBOW,
	UITEM_DEADLYHUNT,
	UITEM_BOWOFDEAD,
	UITEM_BLKOAKBOW,
	UITEM_FLAMEDART,
	UITEM_FLESHSTING,
	UITEM_WINDFORCE,
	UITEM_EAGLEHORN,
	UITEM_GONNAGALDIRK,
	UITEM_DEFENDER,
	UITEM_GRYPHONCLAW,
	UITEM_BLACKRAZOR,
	UITEM_GIBBOUSMOON,
	UITEM_ICESHANK,
	UITEM_EXECUTIONER,
	UITEM_BONESAW,
	UITEM_SHADHAWK,
	UITEM_WIZSPIKE,
	UITEM_LGTSABRE,
	UITEM_FALCONTALON,
	UITEM_INFERNO,
	UITEM_DOOMBRINGER,
	UITEM_GRIZZLY,
	UITEM_GRANDFATHER,
	UITEM_MANGLER,
	UITEM_SHARPBEAK,
	UITEM_BLOODLSLAYER,
	UITEM_CELESTAXE,
	UITEM_WICKEDAXE,
	UITEM_STONECLEAV,
	UITEM_AGUHATCHET,
	UITEM_HELLSLAYER,
	UITEM_MESSERREAVER,
	UITEM_CRACKRUST,
	UITEM_JHOLMHAMM,
	UITEM_CIVERBS,
	UITEM_CELESTSTAR,
	UITEM_BARANSTAR,
	UITEM_GNARLROOT,
	UITEM_CRANBASH,
	UITEM_SCHAEFHAMM,
	UITEM_DREAMFLANGE,
	UITEM_STAFFOFSHAD,
	UITEM_IMMOLATOR,
	UITEM_STORMSPIRE,
	UITEM_GLEAMSONG,
	UITEM_THUNDERCALL,
	UITEM_PROTECTOR,
	UITEM_NAJPUZZLE,
	UITEM_MINDCRY,
	UITEM_RODOFONAN,
	UITEM_SPIRITSHELM,
	UITEM_THINKINGCAP,
	UITEM_OVERLORDHELM,
	UITEM_FOOLSCREST,
	UITEM_GOTTERDAM,
	UITEM_ROYCIRCLET,
	UITEM_TORNFLESH,
	UITEM_GLADBANE,
	UITEM_RAINCLOAK,
	UITEM_LEATHAUT,
	UITEM_WISDWRAP,
	UITEM_SPARKMAIL,
	UITEM_SCAVCARAP,
	UITEM_NIGHTSCAPE,
	UITEM_NAJPLATE,
	UITEM_DEMONSPIKE,
	UITEM_DEFLECTOR,
	UITEM_SKULLSHLD,
	UITEM_DRAGONBRCH,
	UITEM_BLKOAKSHLD,
	UITEM_HOLYDEF,
	UITEM_STORMSHLD,
	UITEM_BRAMBLE,
	UITEM_REGHA,
	UITEM_BLEEDER,
	UITEM_CONSTRICT,
	UITEM_ENGAGE,
	UITEM_INVALID = -1,
};

/*
CF_LEVEL: Item Level (6 bits; value ranges from 0-63)
CF_ONLYGOOD: Item is not able to have affixes with PLOK set to false
CF_UPER15: Item is from a Unique Monster and has 15% chance of being a Unique Item
CF_UPER1: Item is from the dungeon and has a 1% chance of being a Unique Item
CF_UNIQUE: Item is a Unique Item
CF_SMITH: Item is from Griswold (Basic)
CF_SMITHPREMIUM: Item is from Griswold (Premium)
CF_BOY: Item is from Wirt
CF_WITCH: Item is from Adria
CF_HEALER: Item is from Pepin
CF_PREGEN: Item is pre-generated, mostly associated with Quest items found in the dungeon or potions on the dungeon floor

Items that have both CF_UPER15 and CF_UPER1 are CF_USEFUL, which is used to generate Potions and Town Portal scrolls on the dungeon floor
Items that have any of CF_SMITH, CF_SMITHPREMIUM, CF_BOY, CF_WICTH, and CF_HEALER are CF_TOWN, indicating the item is sourced from an NPC
*/
enum icreateinfo_flag {
	// clang-format off
	CF_LEVEL        = (1 << 6) - 1,
	CF_ONLYGOOD     = 1 << 6,
	CF_UPER15       = 1 << 7,
	CF_UPER1        = 1 << 8,
	CF_UNIQUE       = 1 << 9,
	CF_SMITH        = 1 << 10,
	CF_SMITHPREMIUM = 1 << 11,
	CF_BOY          = 1 << 12,
	CF_WITCH        = 1 << 13,
	CF_HEALER       = 1 << 14,
	CF_PREGEN       = 1 << 15,

	CF_USEFUL = CF_UPER15 | CF_UPER1,
	CF_TOWN   = CF_SMITH | CF_SMITHPREMIUM | CF_BOY | CF_WITCH | CF_HEALER,
	// clang-format on
};

enum icreateinfo_flag2 {
	// clang-format off
	CF_HELLFIRE = 1 << 0,
	CF_UIDOFFSET = ((1 << 4) - 1) << 1,
	/** Item was generated with the POE2 Mashup mod active (can roll as rare). */
	CF_POE2 = 1 << 5,
	/** Extra affixes added to a rare by Exalted Orbs. */
	CF_POE2_EXALTS = ((1 << 3) - 1) << 6,
	/** Times a Divine Orb rerolled a rare's affix values (wraps around). */
	CF_POE2_DIVINE = ((1 << 4) - 1) << 9,
	/** Corrupted by a Vaal Orb: no more crafting. */
	CF_POE2_CORRUPTED = 1 << 13,
	/** The implicit a Vaal Orb added; 0 for none. */
	CF_POE2_CORRUPT_IMPLICIT = ((1 << 2) - 1) << 14,
	/** A flask's prefix (FlaskPrefix); 0 for none. */
	CF_POE2_FLASK_PREFIX = ((1 << 3) - 1) << 16,
	/** A flask's suffix (FlaskSuffix); 0 for none. */
	CF_POE2_FLASK_SUFFIX = ((1 << 3) - 1) << 19,
	/** A flask's quality from Glassblower's Baubles, 0 to 20. */
	CF_POE2_FLASK_QUALITY = ((1 << 5) - 1) << 22,
	/** The flask's mods have been rolled, so rebuilding it keeps them. */
	CF_POE2_FLASK_ROLLED = 1 << 27,
	/** A skill gem's level, 1 to 20; 0 on gems from before gem levels (level 1). Shares the flask bits, which only flasks use. */
	CF_POE2_GEM_LEVEL = ((1 << 5) - 1) << 16,
	/** POE2 item level, 1 to 82, on equipment only (the same bits hold flask mods and gem levels); 0 on older items. */
	CF_POE2_ILVL = ((1 << 7) - 1) << 16,
	/** Equipment quality, 0 to 30 (above 20 on Exceptional bases); shares the flask bits, which only flasks use. */
	CF_POE2_QUALITY = ((1 << 5) - 1) << 23,
	/** The rarity a POE2 loot roll gave this item (item_quality: normal, magic, rare or unique). */
	CF_POE2_LOOT_RARITY = ((1 << 2) - 1) << 28,
	/** The item was dropped by the POE2 loot rules, so it's built with the rarity above. */
	CF_POE2_LOOT_SET = 1 << 30,
	// clang-format on
};

/** POE2 Mashup: the item rolls its affixes from the itemization tables (poe2/modifiers.hpp); set with its POE2 item level. */
constexpr uint32_t CF_POE2_MODS = 1U << 31;

// All item animation frames have this width.
constexpr int ItemAnimWidth = 96;

// Defined in player.h, forward declared here to allow for functions which operate in the context of a player.
struct Player;

struct Item {
	/** Randomly generated identifier */
	uint32_t _iSeed = 0;
	uint16_t _iCreateInfo = 0;
	ItemType _itype = ItemType::None;
	bool _iAnimFlag = false;
	Point position = { 0, 0 };
	/*
	 * @brief Contains Information for current Animation
	 */
	AnimationInfo AnimInfo;
	bool _iDelFlag = false; // set when item is flagged for deletion, deprecated in 1.02
	SelectionRegion selectionRegion = SelectionRegion::None;
	bool _iPostDraw = false;
	bool _iIdentified = false;
	item_quality _iMagical = ITEM_QUALITY_NORMAL;
	char _iName[ItemNameLength] = {};
	char _iIName[ItemNameLength] = {};
	item_equip_type _iLoc = ILOC_NONE;
	item_class _iClass = ICLASS_NONE;
	uint8_t _iCurs = 0;
	int _ivalue = 0;
	int _iIvalue = 0;
	/** POE2 Mashup: 16 bits, for POE2-sized weapons; game saves keep 32 bits and the network 16 for the maximum. */
	uint16_t _iMinDam = 0;
	uint16_t _iMaxDam = 0;
	int16_t _iAC = 0;
	ItemSpecialEffect _iFlags = ItemSpecialEffect::None;
	item_misc_id _iMiscId = IMISC_NONE;
	SpellID _iSpell = SpellID::Null;
	_item_indexes IDidx = IDI_NONE;
	int _iCharges = 0;
	int _iMaxCharges = 0;
	int _iDurability = 0;
	int _iMaxDur = 0;
	int16_t _iPLDam = 0;
	int16_t _iPLToHit = 0;
	int16_t _iPLAC = 0;
	int16_t _iPLStr = 0;
	int16_t _iPLMag = 0;
	int16_t _iPLDex = 0;
	int16_t _iPLVit = 0;
	int16_t _iPLFR = 0;
	int16_t _iPLLR = 0;
	int16_t _iPLMR = 0;
	Fixed10_6 _iPLMana = Fixed10_6::fromInt(0);
	Fixed10_6 _iPLHP = Fixed10_6::fromInt(0);
	int16_t _iPLDamMod = 0;
	int16_t _iPLGetHit = 0;
	int16_t _iPLLight = 0;
	int8_t _iSplLvlAdd = 0;
	bool _iRequest = false;
	/** Unique item ID, used as an index into UniqueItemList */
	int _iUid = 0;
	int16_t _iFMinDam = 0;
	int16_t _iFMaxDam = 0;
	int16_t _iLMinDam = 0;
	int16_t _iLMaxDam = 0;
	int16_t _iPLEnAc = 0;
	/** POE2 Mashup: life regenerated per second. Not saved; restored from the item seed. */
	int16_t _iPLLifeRegen = 0;
	/** POE2 Mashup: increased mana regeneration in percent. Not saved; restored from the item seed. */
	int16_t _iPLManaRegen = 0;
	enum item_effect_type _iPrePower = IPL_INVALID;
	enum item_effect_type _iSufPower = IPL_INVALID;
	/** Affix powers of a rare item, in the order they were rolled. Not saved; restored from the item seed. */
	std::array<item_effect_type, MaxRareAffixes> _iRarePowers = { IPL_INVALID, IPL_INVALID, IPL_INVALID, IPL_INVALID, IPL_INVALID, IPL_INVALID };
	/** POE2 Mashup: affix tiers, 1 being the best; 0 when unknown. Not saved; restored from the item seed. */
	uint8_t _iPreTier = 0;
	uint8_t _iSufTier = 0;
	std::array<uint8_t, MaxRareAffixes> _iRareTiers {};
	/** POE2 Mashup: modifiers of an item with CF_POE2_MODS, in the order they were rolled. Not saved; restored from the item seed. */
	std::array<Poe2ItemMod, MaxRareAffixes> _iPoe2Mods {};
	/** POE2 Mashup: crafting state the seed can't rebuild. */
	Poe2Craft _iPoe2Craft {};
	/** POE2 Mashup: stats from those modifiers. Not saved; restored from the item seed. */
	std::array<int16_t, static_cast<size_t>(Poe2ItemStat::Count)> _iPoe2Stats {};
	int _iVAdd1 = 0;
	int _iVMult1 = 0;
	int _iVAdd2 = 0;
	int _iVMult2 = 0;
	uint8_t _iMinStr = 0;
	uint8_t _iMinMag = 0;
	uint8_t _iMinDex = 0;
	bool _iStatFlag = false;
	ItemSpecialEffectHf _iDamAcFlags = ItemSpecialEffectHf::None;
	uint32_t dwBuff = 0;

	/**
	 * @brief Clears this item and returns the old value
	 */
	Item pop() &
	{
		Item temp = std::move(*this);
		clear();
		return temp;
	}

	/**
	 * @brief Resets the item so isEmpty() returns true without needing to reinitialise the whole object
	 */
	DVL_REINITIALIZES void clear()
	{
		this->_itype = ItemType::None;
	}

	/**
	 * @brief Checks whether this item is empty or not.
	 * @return 'True' in case the item is empty and 'False' otherwise.
	 */
	bool isEmpty() const
	{
		return this->_itype == ItemType::None;
	}

	/**
	 * @brief Checks whether this item is an equipment.
	 * @return 'True' in case the item is an equipment and 'False' otherwise.
	 */
	bool isEquipment() const
	{
		if (this->isEmpty()) {
			return false;
		}

		switch (this->_iLoc) {
		case ILOC_AMULET:
		case ILOC_ARMOR:
		case ILOC_HELM:
		case ILOC_ONEHAND:
		case ILOC_RING:
		case ILOC_TWOHAND:
			return true;

		default:
			return false;
		}
	}

	/**
	 * @brief Checks whether this item is a weapon.
	 * @return 'True' in case the item is a weapon and 'False' otherwise.
	 */
	bool isWeapon() const
	{
		switch (this->_itype) {
		case ItemType::Axe:
		case ItemType::Bow:
		case ItemType::Mace:
		case ItemType::Staff:
		case ItemType::Sword:
			return true;

		default:
			return false;
		}
	}

	/**
	 * @brief Checks whether this item is an armor.
	 * @return 'True' in case the item is an armor and 'False' otherwise.
	 */
	bool isArmor() const
	{
		switch (this->_itype) {
		case ItemType::HeavyArmor:
		case ItemType::LightArmor:
		case ItemType::MediumArmor:
			return true;

		default:
			return false;
		}
	}

	/**
	 * @brief Checks whether this item is gold.
	 * @return 'True' in case the item is gold and 'False' otherwise.
	 */
	bool isGold() const
	{
		return this->_itype == ItemType::Gold;
	}

	/**
	 * @brief Checks whether this item is a helm.
	 * @return 'True' in case the item is a helm and 'False' otherwise.
	 */
	bool isHelm() const
	{
		return this->_itype == ItemType::Helm;
	}

	/**
	 * @brief Checks whether this item is a shield.
	 * @return 'True' in case the item is a shield and 'False' otherwise.
	 */
	bool isShield() const
	{
		return this->_itype == ItemType::Shield;
	}

	/**
	 * @brief Checks whether this item is a jewelry.
	 * @return 'True' in case the item is a jewelry and 'False' otherwise.
	 */
	bool isJewelry() const
	{
		switch (this->_itype) {
		case ItemType::Amulet:
		case ItemType::Ring:
			return true;

		default:
			return false;
		}
	}

	[[nodiscard]] bool isScroll() const
	{
		return IsAnyOf(_iMiscId, IMISC_SCROLL, IMISC_SCROLLT);
	}

	[[nodiscard]] bool isScrollOf(SpellID spellId) const
	{
		return isScroll() && _iSpell == spellId;
	}

	[[nodiscard]] bool isRune() const
	{
		return _iMiscId > IMISC_RUNEFIRST && _iMiscId < IMISC_RUNELAST;
	}

	[[nodiscard]] bool isRuneOf(SpellID spellId) const
	{
		if (!isRune())
			return false;
		switch (_iMiscId) {
		case IMISC_RUNEF:
			return spellId == SpellID::RuneOfFire;
		case IMISC_RUNEL:
			return spellId == SpellID::RuneOfLight;
		case IMISC_GR_RUNEL:
			return spellId == SpellID::RuneOfNova;
		case IMISC_GR_RUNEF:
			return spellId == SpellID::RuneOfImmolation;
		case IMISC_RUNES:
			return spellId == SpellID::RuneOfStone;
		default:
			return false;
		}
	}

	[[nodiscard]] bool isUsable() const;

	[[nodiscard]] bool keyAttributesMatch(uint32_t seed, _item_indexes itemIndex, uint16_t createInfo) const
	{
		return _iSeed == seed && IDidx == itemIndex && _iCreateInfo == createInfo;
	}

	UiFlags getTextColor() const
	{
		switch (_iMagical) {
		case ITEM_QUALITY_MAGIC:
			return UiFlags::ColorBlue;
		case ITEM_QUALITY_UNIQUE:
			// POE2 Mashup: uniques are orange, as in POE2.
			return gbIsPoe2 ? UiFlags::ColorOrange : UiFlags::ColorWhitegold;
		case ITEM_QUALITY_RARE:
			return UiFlags::ColorYellow;
		default:
			// POE2 Mashup: Books of Specialisation are quest rewards, gold like POE2's.
			if (gbIsPoe2 && _iMiscId == IMISC_SPEC_BOOK)
				return UiFlags::ColorWhitegold;
			return UiFlags::ColorWhite;
		}
	}

	UiFlags getTextColorWithStatCheck() const
	{
		if (!_iStatFlag)
			return UiFlags::ColorRed;
		return getTextColor();
	}

	/**
	 * @brief Sets the current Animation for the Item
	 * @param showAnimation Definies if the Animation (Flipping) is shown or if only the final Frame (item on the ground) is shown
	 */
	void setNewAnimation(bool showAnimation);

	/**
	 * @brief If this item is a spell book, calculates the magic requirement to learn a new level, then for all items sets _iStatFlag
	 * @param player Player to compare stats against requirements
	 */
	void updateRequiredStatsCacheForPlayer(const Player &player);

	/** @brief Returns the translated item name to display (respects identified flag) */
	StringOrView getName() const;

	[[nodiscard]] Displacement getRenderingOffset(const ClxSprite sprite) const
	{
		return { -CalculateSpriteTileCenterX(sprite.width()), 0 };
	}
};

struct ItemGetRecordStruct {
	uint32_t nSeed;
	uint16_t wCI;
	int nIndex;
	uint32_t dwTimestamp;
};

struct CornerStoneStruct {
	Point position;
	bool activated;
	Item item;
	bool isAvailable();
};

/** Contains the items on ground in the current game. */
extern Item Items[MAXITEMS + 1];
extern uint8_t ActiveItems[MAXITEMS];
extern uint8_t ActiveItemCount;
/** Contains the location of dropped items. */
extern int8_t dItem[MAXDUNX][MAXDUNY];
extern bool ShowUniqueItemInfoBox;
extern CornerStoneStruct CornerStone;
extern DVL_API_FOR_TEST bool UniqueItemFlags[128];

uint8_t GetOutlineColor(const Item &item, bool checkReq);
bool IsItemAvailable(int i);
void ClearUniqueItemFlags();
void InitItemGFX();
void InitItems();
void CalcPlrItemVals(Player &player, bool Loadgfx);
void CalcPlrInv(Player &player, bool Loadgfx);
/** @brief POE2 Mashup: whether the item shows a one-cell copy of a bigger picture. */
inline bool IsOneCellCopy(const Item &item)
{
	return FullSizeItemCursor(static_cast<item_cursor_graphic>(item._iCurs)) != item._iCurs;
}
/** @brief POE2 Mashup: whether the item takes one cell, whatever picture it was saved with (one-cell copies and charms). */
bool TakesOneCell(const Item &item);
/** @brief POE2 Mashup: items saved when they were bigger let go of the inventory cells they no longer cover. */
void FitOneCellItems(Player &player);
/** @param quality POE2 Mashup: the quality the item is made with, as starting gear rebuilt by crafting keeps its quality. */
void InitializeItem(Item &item, _item_indexes itemData, int quality = 0);
void GenerateNewSeed(Item &item);
int GetGoldCursor(int value);

/**
 * @brief Update the gold cursor on the given gold item
 * @param gold The item to update
 */
void SetPlrHandGoldCurs(Item &gold);
void CreatePlrItems(Player &player);
bool ItemSpaceOk(Point position);
int AllocateItem();
/**
 * @brief Moves the item onto the floor of the current dungeon level
 * @param item The source of the item data, should not be used after calling this function
 * @param position Coordinates of the tile to place the item on
 * @return The index assigned to the item
 */
uint8_t PlaceItemInWorld(Item &&item, WorldTilePosition position);
Point GetSuperItemLoc(Point position);
void GetItemAttrs(Item &item, _item_indexes itemData, int lvl);
void SetupItem(Item &item);
Item *SpawnUnique(_unique_items uid, Point position, std::optional<int> level = std::nullopt, bool sendmsg = true, bool exactPosition = false);
/** @brief Makes a fixed unique the way quests hand them out (Arkaine's Valor, Optic Amulet...), on the given base. */
void MakeFixedUnique(const Player &player, Item &item, _item_indexes base, int level, _unique_items uid);
/** @return false when no free tile was found nearby (the item has no tile then). */
bool GetSuperItemSpace(Point position, int8_t inum);
_item_indexes RndItemForMonsterLevel(int8_t monsterLevel);
void SetupAllItems(const Player &player, Item &item, _item_indexes idx, uint32_t iseed, int lvl, int uper, bool onlygood, bool pregen, int uidOffset = 0, bool forceNotUnique = false);
void TryRandomUniqueItem(Item &item, _item_indexes idx, int8_t mLevel, int uper, bool onlygood, bool pregen);
void SpawnItem(Monster &monster, Point position, bool sendmsg, bool spawn = false);
/** @brief POE2 Mashup: a boss rolls a unique from its own table (poe2/boss_loot.hpp). */
/** @brief POE2 Mashup: drops a waystone of the tier, like a map boss does. */
void DropWaystone(Point position, int tier, bool sendmsg);
/** @brief POE2 Mashup: drops one roll of a map encounter's or reward chest's loot; `rarityBonus` raises its item rarity, in percent. */
void DropMapReward(Point position, MapReward reward, bool sendmsg, int rarityBonus = 0);
/** @brief POE2 Mashup: makes the gambler's item of the family, its rarity rolled as the gambler's. */
bool MakeGambleItem(const Player &player, Item &item, GambleFamily family, int areaLevel);
/** @brief POE2 Mashup: puts a currency item in the hero's inventory, or at their feet if it's full. */
void GiveCurrencyItem(Player &player, item_misc_id miscId);
void DropBossUnique(Monster &monster, bool sendmsg);
/** @brief POE2 Mashup: drops a kill's loot by the POE2 rules: a number of rolls by how tough the monster was, and a boss's extras. */
void SpawnPoe2Loot(Monster &monster, Point position, bool sendmsg);
/** @brief POE2 Mashup: drops this many extra loot rolls, e.g. from a Golden Charm. */
void SpawnPoe2LootRolls(Monster &monster, Point position, bool sendmsg, int rolls);
/** @brief POE2 Mashup: a magic or rare chest's or a strongbox's sure item: equipment of that rarity. */
void DropPoe2ChestItem(Point position, item_quality rarity, bool sendmsg, std::string_view name);
void CreateRndItem(Point position, bool onlygood, bool sendmsg, bool delta);
void CreateRndUseful(Point position, bool sendmsg);
void CreateTypeItem(Point position, bool onlygood, ItemType itemType, int imisc, bool sendmsg, bool delta, bool spawn = false);
/** @brief Drops a fresh item of the given base near a tile, e.g. a boss's Spirit skull. */
void SpawnBaseItem(Point position, _item_indexes idx, bool sendmsg);
/** @param craft POE2 Mashup: crafting state the item had, which its seed can't rebuild. */
void RecreateItem(const Player &player, Item &item, _item_indexes idx, uint16_t icreateinfo, uint32_t iseed, int ivalue, uint32_t dwBuff, const Poe2Craft &craft = {});
void RecreateEar(Item &item, uint16_t ic, uint32_t iseed, uint8_t bCursval, std::string_view heroName);
void CornerstoneSave();
void CornerstoneLoad(Point position);
void SpawnQuestItem(_item_indexes itemid, Point position, int randarea, SelectionRegion selectionRegion, bool sendmsg);
void SpawnRewardItem(_item_indexes itemid, Point position, bool sendmsg);
void SpawnMapOfDoom(Point position, bool sendmsg);
void SpawnRuneBomb(Point position, bool sendmsg);
void SpawnTheodore(Point position, bool sendmsg);
void RespawnItem(Item &item, bool FlipFlag);
void DeleteItem(int i);
void ProcessItems();
void FreeItemGFX();
void GetItemFrm(Item &item);
void GetItemStr(Item &item);
void CheckIdentify(Player &player, int cii);
/**
 * @brief POE2 Mashup: identifies the item under the cursor and only then uses up one Scroll of Identify;
 * nothing is used up when there is no unidentified item there.
 */
void IdentifyWithScroll(Player &player);
void DoRepair(Player &player, int cii);
void DoRecharge(Player &player, int cii);
bool DoOil(Player &player, int cii);
/** @brief POE2 Mashup: uses the currency on the cursor on a stash item, as DoOil does on one of the hero's. */
bool DoOilOnStashItem(Player &player, uint16_t index);
/** @brief POE2 Mashup: notes the currency put on the cursor; it is only used up once it takes (see SpendHeldCurrency). */
void HoldCurrencyForUse(const Item &item);
/** @brief POE2 Mashup: while currency is held for use, whether it fits the item (the inventory outlines those). */
bool HeldCurrencyFits(const Item &item);
/** @brief Uses up one of the currency on the cursor, after it changed an item. */
void SpendHeldCurrency(Player &player);
/** @brief POE2 Mashup: whether the currency on the cursor is still in the bag or stash to be used up. */
[[nodiscard]] bool HeldCurrencyAvailable(const Player &player);
/** @brief POE2 Mashup: whether the item was picked up with Shift+right-click, to use it again and again (Shift held when using it counts too). */
void SetStickyItemUse(bool sticky);
[[nodiscard]] bool WantsStickyItemUse();
/** @brief After currency was used with Shift: puts the next one of its kind on the cursor; false if there's none or no Shift. */
bool KeepCurrencyOnCursor(Player &player);
[[nodiscard]] bool HasIdentifyScroll(const Player &player);
[[nodiscard]] StringOrView PrintItemPower(char plidx, const Item &item);
void DrawUniqueInfo(const Surface &out);
void PrintItemDetails(const Item &item);
/**
 * @brief POE2-style affix tier: 1 for the best affix of its kind, counting up for each lower level
 * of affix with the same power that can roll on the same items.
 */
uint8_t AffixTier(const std::vector<PLStruct> &affixes, const PLStruct &affix);
void PrintItemDur(const Item &item);
/**
 * @brief The requirement line, like "Required: Level 48, 78 Str"; empty when nothing is needed. A level of 1 or less
 * isn't shown, like POE2's starter bases.
 */
std::string ItemRequirementText(int level, int strength, int magic, int dexterity);
/** @brief POE2 Mashup: while Shift is held, lists how the item's stats differ from the equipped one in green and red. */
void PrintItemComparison(const Item &item);
void UseItem(Player &player, item_misc_id Mid, SpellID spellID, int spellFrom);
bool UseItemOpensHive(const Item &item, Point position);
bool UseItemOpensGrave(const Item &item, Point position);
void SpawnSmith(int lvl);
void ReplacePremium(const Player &player, int idx);
void SpawnPremium(const Player &player);
void SpawnWitch(int lvl);
void SpawnBoy(int lvl);
void SpawnHealer(int lvl);
/** @brief POE2 Mashup: the kinds of wares vendors group onto their shop tabs. */
enum class VendorCategory : uint8_t {
	OneHanded,
	TwoHanded,
	Bows,
	BodyArmour,
	Helmets,
	Shields,
	Jewelry,
	/** Potions, scrolls, elixirs and everything else that isn't equipment. */
	Supplies,
};
constexpr size_t NumVendorCategories = 8;
/** @brief Which kind of ware an item is on a POE2 Mashup vendor's shop tabs. */
VendorCategory GetVendorCategory(const Item &item);
/** @brief Sorts a vendor's stock, after the pinned wares, the way the vendor lists it. */
void SortVendorStock(std::span<Item> stock, size_t pinnedCount);
void MakeGoldStack(Item &goldItem, int value);
int ItemNoFlippy();
void CreateSpellBook(Point position, SpellID ispell, bool sendmsg, bool delta);
void CreateMagicArmor(Point position, ItemType itemType, int icurs, bool sendmsg, bool delta);
void CreateAmulet(Point position, int lvl, bool sendmsg, bool delta, bool spawn = false);
void CreateMagicWeapon(Point position, ItemType itemType, int icurs, bool sendmsg, bool delta);
bool GetItemRecord(uint32_t nSeed, uint16_t wCI, int nIndex);
void SetItemRecord(uint32_t nSeed, uint16_t wCI, int nIndex);
void PutItemRecord(uint32_t nSeed, uint16_t wCI, int nIndex);

/**
 * @brief Resets item get records.
 */
void initItemGetRecords();

void RepairItem(Item &item, int lvl);
void RechargeItem(Item &item, Player &player);
bool ApplyOilToItem(Item &item, Player &player);
/**
 * @brief Checks if the item is generated in vanilla hellfire. If yes it updates dwBuff to include CF_HELLFIRE.
 */
void UpdateHellfireFlag(Item &item, const char *identifiedItemName);

/**
 * @brief POE2 Mashup: the two-word name of a rare item, picked from its seed and item type.
 */
std::string GetRareItemName(const Item &item);

/**
 * @brief POE2 Mashup: refills the affix data that isn't saved (rare powers, regeneration) by regenerating the item from its seed.
 */
void RestoreRareItemPowers(Item &item);

/**
 * @brief POE2 Mashup: makes one of the bundle's uniques (poe2/uniques.hpp) on its base, the way a drop of it is made.
 * @return false if the uid isn't a bundle unique.
 */
bool CreatePoe2Unique(const Player &player, Item &item, int uid, uint32_t seed, int quality = 0);

enum class ChanceResult : uint8_t {
	/** No unique is made on the base. */
	NoUniques,
	/** The rolled tier has no unique on the base: the item is destroyed. */
	Failed,
	Made,
};

/**
 * @brief POE2 Mashup: an Orb of Chance on a normal item rolls a unique tier, then a unique of that tier on the item's base.
 * @param unique Receives the unique when one is made.
 */
ChanceResult CreateChanceUnique(const Player &player, const Item &item, Item &unique, uint32_t roll);

/* data */

extern int MaxGold;

extern int8_t ItemCAnimTbl[];
extern SfxID ItemInvSnds[];

} // namespace devilution
