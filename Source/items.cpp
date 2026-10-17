/**
 * @file items.cpp
 *
 * Implementation of item functionality.
 */
#include "items.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_timer.h>
#else
#include <SDL.h>
#endif

#include "DiabloUI/ui_flags.hpp"
#include "control/control.hpp"
#include "controls/control_mode.hpp"
#include "controls/controller_buttons.h"
#include "cursor.h"
#include "cursor_defs.hpp"
#include "diablo.h"
#include "doom.h"
#include "effects.h"
#include "engine/animationinfo.h"
#include "engine/backbuffer_state.hpp"
#include "engine/clx_sprite.hpp"
#include "engine/load_cel.hpp"
#include "engine/path.h"
#include "engine/point.hpp"
#include "engine/random.hpp"
#include "engine/render/clx_render.hpp"
#include "engine/render/primitive_render.hpp"
#include "engine/render/text_render.hpp"
#include "engine/surface.hpp"
#include "engine/world_tile.hpp"
#include "function_ref.hpp"
#include "game_mode.hpp"
#include "headless_mode.hpp"
#include "inv.h"
#include "inv_iterators.hpp"
#include "items/validation.h"
#include "levels/gendung.h"
#include "levels/gendung_defs.hpp"
#include "levels/tile_properties.hpp"
#include "levels/town.h"
#include "lighting.h"
#include "minitext.h"
#include "monster.h"
#include "msg.h"
#include "multi.h"
#include "objects.h"
#include "options.h"
#include "pack.h"
#include "panels/desecration_panel.hpp"
#include "panels/info_box.hpp"
#include "panels/skills_panel.hpp"
#include "panels/ui_panels.hpp"
#include "player.h"
#include "plrmsg.h"
#include "poe2/bases.hpp"
#include "poe2/belt_uniques.hpp"
#include "poe2/boss_loot.hpp"
#include "poe2/charms.hpp"
#include "poe2/craft_state.hpp"
#include "poe2/crafting_items.hpp"
#include "poe2/currency.hpp"
#include "poe2/stash_layouts.hpp"
#include "poe2/defenses.hpp"
#include "poe2/desecration.hpp"
#include "poe2/economy.hpp"
#include "poe2/endgame.hpp"
#include "poe2/flasks.hpp"
#include "poe2/gem_levels.hpp"
#include "poe2/gemcutting.hpp"
#include "poe2/jewels.hpp"
#include "poe2/loot.hpp"
#include "poe2/map_play.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/regen.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/spirit.hpp"
#include "poe2/stacking.hpp"
#include "poe2/support_gems.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/uniques.hpp"
#include "poe2/waystones.hpp"
#include "poe2/weapon_skills.hpp"
#include "poe2/weapon_swap.hpp"
#include "qol/stash.h"
#include "quests.h"
#include "sound_effect_enums.h"
#include "spells.h"
#include "stores.h"
#include "tables/itemdat.h"
#include "tables/monstdat.h"
#include "tables/objdat.h"
#include "tables/playerdat.hpp"
#include "tables/spelldat.h"
#include "tables/textdat.h"
#include "utils/enum_traits.h"
#include "utils/format.hpp"
#include "utils/format_int.hpp"
#include "utils/is_of.hpp"
#include "utils/language.h"
#include "utils/log.hpp"
#include "utils/math.h"
#include "utils/sdl_compat.h"
#include "utils/sdl_geometry.h"
#include "utils/static_vector.hpp"
#include "utils/str_cat.hpp"
#include "utils/str_split.hpp"
#include "utils/string_or_view.hpp"
#include "utils/utf8.hpp"

namespace devilution {

Item Items[MAXITEMS + 1];
uint8_t ActiveItems[MAXITEMS];
uint8_t ActiveItemCount;
int8_t dItem[MAXDUNX][MAXDUNY];
bool ShowUniqueItemInfoBox;
CornerStoneStruct CornerStone;
bool UniqueItemFlags[128];
int MaxGold = GOLD_MAX_LIMIT;

/** Maps from item_cursor_graphic to in-memory item type. POE2 Mashup: 43, the Blacksmith's Whetstone, drops as a stone, not armour. */
int8_t ItemCAnimTbl[] = {
	20, 16, 16, 16, 4, 4, 4, 12, 12, 12,
	12, 12, 12, 12, 12, 21, 21, 25, 12, 28,
	28, 28, 38, 38, 38, 32, 38, 38, 38, 24,
	24, 26, 2, 25, 22, 23, 24, 21, 27, 27,
	29, 0, 0, 9, 12, 12, 12, 12, 12, 0,
	8, 8, 0, 8, 8, 8, 8, 8, 8, 6,
	8, 8, 8, 6, 8, 8, 6, 8, 8, 6,
	6, 6, 8, 8, 8, 5, 9, 13, 13, 13,
	5, 5, 5, 15, 5, 5, 18, 18, 18, 30,
	5, 5, 14, 5, 14, 13, 16, 18, 5, 5,
	7, 1, 3, 17, 1, 15, 10, 14, 3, 11,
	8, 0, 1, 7, 0, 7, 15, 7, 3, 3,
	3, 6, 6, 11, 11, 11, 31, 14, 14, 14,
	6, 6, 7, 3, 8, 14, 0, 14, 14, 0,
	33, 1, 1, 1, 1, 1, 7, 7, 7, 14,
	14, 17, 17, 17, 0, 34, 1, 0, 3, 17,
	8, 8, 6, 1, 3, 3, 11, 3, 12, 12,
	12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
	12, 12, 12, 12, 12, 12, 12, 35, 39, 36,
	36, 36, 37, 38, 38, 38, 38, 38, 41, 42,
	8, 8, 8, 17, 0, 6, 8, 11, 11, 3,
	3, 1, 6, 6, 6, 1, 8, 6, 11, 3,
	6, 8, 1, 6, 6, 17, 40, 0, 0
};

/** Maps of drop sounds effect of placing the item in the inventory. */
SfxID ItemInvSnds[] = {
	SfxID::ItemArmor,
	SfxID::ItemAxe,
	SfxID::ItemPotion,
	SfxID::ItemBow,
	SfxID::ItemGold,
	SfxID::ItemCap,
	SfxID::ItemSword,
	SfxID::ItemShield,
	SfxID::ItemSword,
	SfxID::ItemRock,
	SfxID::ItemAxe,
	SfxID::ItemStaff,
	SfxID::ItemRing,
	SfxID::ItemCap,
	SfxID::ItemLeather,
	SfxID::ItemShield,
	SfxID::ItemScroll,
	SfxID::ItemArmor,
	SfxID::ItemBook,
	SfxID::ItemArmor,
	SfxID::ItemPotion,
	SfxID::ItemPotion,
	SfxID::ItemPotion,
	SfxID::ItemPotion,
	SfxID::ItemPotion,
	SfxID::ItemPotion,
	SfxID::ItemPotion,
	SfxID::ItemPotion,
	SfxID::ItemBodyPart,
	SfxID::ItemBodyPart,
	SfxID::ItemMushroom,
	SfxID::ItemSign,
	SfxID::ItemBloodStone,
	SfxID::ItemAnvil,
	SfxID::ItemStaff,
	SfxID::ItemRock,
	SfxID::ItemScroll,
	SfxID::ItemScroll,
	SfxID::ItemRock,
	SfxID::ItemMushroom,
	SfxID::ItemArmor,
	SfxID::ItemLeather,
	SfxID::ItemLeather,
};

namespace {

OptionalOwnedClxSpriteList itemanims[ITEMTYPES];

enum class PlayerArmorGraphic : uint8_t {
	// clang-format off
	Light  = 0,
	Medium = 1 << 4,
	Heavy  = 1 << 5,
	// clang-format on
};

Item curruitem;

/** Holds item get records, tracking items being recently looted. This is in an effort to prevent items being picked up more than once. */
ItemGetRecordStruct itemrecord[MAXITEMS];

bool itemhold[3][3];

/** Specifies the number of active item get records. */
int gnNumGetRecords;

int OilLevels[] = { 1, 10, 1, 10, 4, 1, 5, 17, 1, 10 };
int OilValues[] = { 500, 2500, 500, 2500, 1500, 100, 2500, 15000, 500, 2500 };
item_misc_id OilMagic[] = {
	IMISC_OILACC,
	IMISC_OILMAST,
	IMISC_OILSHARP,
	IMISC_OILDEATH,
	IMISC_OILSKILL,
	IMISC_OILBSMTH,
	IMISC_OILFORT,
	IMISC_OILPERM,
	IMISC_OILHARD,
	IMISC_OILIMP,
};
char OilNames[10][25] = {
	N_("Oil of Accuracy"),
	N_("Oil of Mastery"),
	N_("Oil of Sharpness"),
	N_("Oil of Death"),
	N_("Oil of Skill"),
	N_("Blacksmith Oil"),
	N_("Oil of Fortitude"),
	N_("Oil of Permanence"),
	N_("Oil of Hardening"),
	N_("Oil of Imperviousness")
};

/** Map of item type .cel file names. */
const char *const ItemDropNames[] = {
	"armor2",
	"axe",
	"fbttle",
	"bow",
	"goldflip",
	"helmut",
	"mace",
	"shield",
	"swrdflip",
	"rock",
	"cleaver",
	"staff",
	"ring",
	"crownf",
	"larmor",
	"wshield",
	"scroll",
	"fplatear",
	"fbook",
	"food",
	"fbttlebb",
	"fbttledy",
	"fbttleor",
	"fbttlebr",
	"fbttlebl",
	"fbttleby",
	"fbttlewh",
	"fbttledb",
	"fear",
	"fbrain",
	"fmush",
	"innsign",
	"bldstn",
	"fanvil",
	"flazstaf",
	"bombs1",
	"halfps1",
	"wholeps1",
	"runes1",
	"teddys1",
	"cows1",
	"donkys1",
	"mooses1",
};
/** Maps of item drop animation length. */
int8_t ItemAnimLs[] = {
	15,
	13,
	16,
	13,
	10,
	13,
	13,
	13,
	13,
	20,
	13,
	13,
	13,
	13,
	13,
	13,
	13,
	13,
	13,
	1,
	16,
	16,
	16,
	16,
	16,
	16,
	16,
	16,
	13,
	12,
	12,
	13,
	13,
	13,
	8,
	10,
	16,
	16,
	10,
	10,
	15,
	15,
	15,
};
/** Maps of drop sounds effect of dropping the item on ground. */
SfxID ItemDropSnds[] = {
	SfxID::ItemArmorFlip,
	SfxID::ItemAxeFlip,
	SfxID::ItemPotionFlip,
	SfxID::ItemBowFlip,
	SfxID::ItemGold,
	SfxID::ItemCapFlip,
	SfxID::ItemSwordFlip,
	SfxID::ItemShieldFlip,
	SfxID::ItemSwordFlip,
	SfxID::ItemRockFlip,
	SfxID::ItemAxeFlip,
	SfxID::ItemStaffFlip,
	SfxID::ItemRingFlip,
	SfxID::ItemCapFlip,
	SfxID::ItemLeatherFlip,
	SfxID::ItemShieldFlip,
	SfxID::ItemScrollFlip,
	SfxID::ItemArmorFlip,
	SfxID::ItemBookFlip,
	SfxID::ItemLeatherFlip,
	SfxID::ItemPotionFlip,
	SfxID::ItemPotionFlip,
	SfxID::ItemPotionFlip,
	SfxID::ItemPotionFlip,
	SfxID::ItemPotionFlip,
	SfxID::ItemPotionFlip,
	SfxID::ItemPotionFlip,
	SfxID::ItemPotionFlip,
	SfxID::ItemBodyPartFlip,
	SfxID::ItemBodyPartFlip,
	SfxID::ItemMushroomFlip,
	SfxID::ItemSignFlip,
	SfxID::ItemBloodStoneFlip,
	SfxID::ItemAnvilFlip,
	SfxID::ItemStaffFlip,
	SfxID::ItemRockFlip,
	SfxID::ItemScrollFlip,
	SfxID::ItemScrollFlip,
	SfxID::ItemRockFlip,
	SfxID::ItemMushroomFlip,
	SfxID::ItemArmorFlip,
	SfxID::ItemLeatherFlip,
	SfxID::ItemLeatherFlip,
};
/** Maps from Griswold premium item number to a quality level delta as added to the base quality level. */
int itemLevelAdd[] = {
	// clang-format off
	-1,
	-1,
	 0,
	 0,
	 1,
	 2,
	// clang-format on
};
/** Maps from Griswold premium item number to a quality level delta as added to the base quality level. */
int itemLevelAddHf[] = {
	// clang-format off
	-1,
	-1,
	-1,
	 0,
	 0,
	 0,
	 0,
	 1,
	 1,
	 1,
	 1,
	 2,
	 2,
	 3,
	 3,
	// clang-format on
};

int ItemsGetCurrlevel()
{
	if (setlevel) {
		switch (setlvlnum) {
		case SL_SKELKING:
			return Quests[Q_SKELKING]._qlevel;
		case SL_BONECHAMB:
			return Quests[Q_SCHAMB]._qlevel;
		case SL_POISONWATER:
			return Quests[Q_PWATER]._qlevel;
		case SL_VILEBETRAYER:
			return Quests[Q_BETRAYER]._qlevel;
		default:
			return 1;
		}
	}

	if (leveltype == DTYPE_NEST)
		return currlevel - 8;

	if (leveltype == DTYPE_CRYPT)
		return currlevel - 7;

	return currlevel;
}

bool ItemPlace(Point position)
{
	if (dMonster[position.x][position.y] != 0)
		return false;
	if (dPlayer[position.x][position.y] != 0)
		return false;
	if (dItem[position.x][position.y] != 0)
		return false;
	if (IsObjectAtPosition(position))
		return false;
	if (TileContainsSetPiece(position))
		return false;
	if (IsTileSolid(position))
		return false;

	return true;
}

Point GetRandomAvailableItemPosition()
{
	Point position = {};
	do {
		position = Point { GenerateRnd(80), GenerateRnd(80) } + Displacement { 16, 16 };
	} while (!ItemPlace(position));

	return position;
}

void AddInitItems()
{
	const int curlv = ItemsGetCurrlevel();
	const int rnd = GenerateRnd(3) + 3;
	for (int j = 0; j < rnd; j++) {
		const int ii = AllocateItem();
		auto &item = Items[ii];

		const Point position = GetRandomAvailableItemPosition();
		item.position = position;

		dItem[position.x][position.y] = ii + 1;

		item._iSeed = AdvanceRndSeed();
		SetRndSeed(item._iSeed);

		GetItemAttrs(item, PickRandomlyAmong({ IDI_MANA, IDI_HEAL }), curlv);

		item._iCreateInfo = curlv | CF_PREGEN;
		SetupItem(item);
		item.AnimInfo.currentFrame = static_cast<int8_t>(item.AnimInfo.numberOfFrames - 1);
		item._iAnimFlag = false;
		item.selectionRegion = SelectionRegion::Bottom;
		DeltaAddItem(ii);
	}
}

void SpawnNote()
{
	_item_indexes id;

	switch (currlevel) {
	case 22:
		id = IDI_NOTE2;
		break;
	case 23:
		id = IDI_NOTE3;
		break;
	default:
		id = IDI_NOTE1;
		break;
	}

	const Point position = GetRandomAvailableItemPosition();
	SpawnQuestItem(id, position, 0, SelectionRegion::Bottom, false);
}

void CalcSelfItems(Player &player)
{
	int sa = 0;
	int ma = 0;
	int da = 0;

	// first iteration is used for collecting stat bonuses from items
	for (Item &equipment : EquippedPlayerItemsRange(player)) {
		equipment._iStatFlag = true;
		if (equipment._iIdentified) {
			sa += equipment._iPLStr;
			ma += equipment._iPLMag;
			da += equipment._iPLDex;
		}
	}

	bool changeflag;
	do {
		// cap stats to 0
		const int currstr = std::max(0, sa + player._pBaseStr);
		const int currmag = std::max(0, ma + player._pBaseMag);
		const int currdex = std::max(0, da + player._pBaseDex);

		changeflag = false;
		// Iterate over equipped items and remove stat bonuses if they are not valid
		for (Item &equipment : EquippedPlayerItemsRange(player)) {
			if (!equipment._iStatFlag)
				continue;

			bool isValid = IsItemValid(player, equipment);

			if (currstr < equipment._iMinStr
			    || currmag < equipment._iMinMag
			    || currdex < equipment._iMinDex)
				isValid = false;

			if (isValid)
				continue;

			changeflag = true;
			equipment._iStatFlag = false;
			if (equipment._iIdentified) {
				sa -= equipment._iPLStr;
				ma -= equipment._iPLMag;
				da -= equipment._iPLDex;
			}
		}
	} while (changeflag);
}

bool GetItemSpace(Point position, int8_t inum)
{
	int xx = 0;
	int yy = 0;
	for (int j = position.y - 1; j <= position.y + 1; j++) {
		xx = 0;
		for (int i = position.x - 1; i <= position.x + 1; i++) {
			itemhold[xx][yy] = ItemSpaceOk({ i, j });
			xx++;
		}
		yy++;
	}

	bool savail = false;
	for (int j = 0; j < 3; j++) {
		for (int i = 0; i < 3; i++) { // NOLINT(modernize-loop-convert)
			if (itemhold[i][j])
				savail = true;
		}
	}

	int rs = GenerateRnd(15) + 1;

	if (!savail)
		return false;

	xx = 0;
	yy = 0;
	while (rs > 0) {
		if (itemhold[xx][yy])
			rs--;
		if (rs <= 0)
			continue;
		xx++;
		if (xx != 3)
			continue;
		xx = 0;
		yy++;
		if (yy == 3)
			yy = 0;
	}

	xx += position.x - 1;
	yy += position.y - 1;
	Items[inum].position = { xx, yy };
	dItem[xx][yy] = static_cast<int8_t>(inum + 1);

	return true;
}

void CalcItemValue(Item &item)
{
	int v = item._iVMult1 + item._iVMult2;
	if (v > 0) {
		v *= item._ivalue;
	}
	if (v < 0) {
		v = item._ivalue / v;
	}
	v = item._iVAdd1 + item._iVAdd2 + v;
	item._iIvalue = std::max(v, 1);
}

void GetBookSpell(Item &item, int lvl)
{
	if (lvl == 0)
		lvl = 1;

	int rv = GenerateRnd(static_cast<int32_t>(SpellsData.size())) + 1;

	if (gbIsSpawn && lvl > 5)
		lvl = 5;

	int s = static_cast<int8_t>(SpellID::Firebolt);
	SpellID bs = SpellID::Firebolt;
	while (rv > 0) {
		const int sLevel = GetSpellBookLevel(static_cast<SpellID>(s));
		if (sLevel != -1 && lvl >= sLevel) {
			rv--;
			bs = static_cast<SpellID>(s);
		}
		s++;
		if (!gbIsMultiplayer) {
			if (s == static_cast<int8_t>(SpellID::Resurrect))
				s = static_cast<int8_t>(SpellID::Telekinesis);
		}
		if (!gbIsMultiplayer) {
			if (s == static_cast<int8_t>(SpellID::HealOther))
				s = static_cast<int8_t>(SpellID::BloodStar);
		}
		if (static_cast<size_t>(s) == SpellsData.size())
			s = 1;
	}
	const std::string_view spellName = GetSpellData(bs).sNameText;
	const size_t iNameLen = std::string_view(item._iName).size();
	const size_t iINameLen = std::string_view(item._iIName).size();
	CopyUtf8(item._iName + iNameLen, spellName, ItemNameLength - iNameLen);
	CopyUtf8(item._iIName + iINameLen, spellName, ItemNameLength - iINameLen);
	item._iSpell = bs;
	const SpellData &spellData = GetSpellData(bs);
	item._iMinMag = spellData.minInt;
	item._ivalue += static_cast<int32_t>(spellData.bookCost());
	item._iIvalue += static_cast<int32_t>(spellData.bookCost());
	switch (spellData.type()) {
	case MagicType::Fire:
		item._iCurs = ICURS_BOOK_RED;
		break;
	case MagicType::Lightning:
		item._iCurs = ICURS_BOOK_BLUE;
		break;
	case MagicType::Magic:
		item._iCurs = ICURS_BOOK_GREY;
		break;
	}
}

int RndPL(int param1, int param2)
{
	return param1 + GenerateRnd(param2 - param1 + 1);
}

int CalculateToHitBonus(int level)
{
	switch (level) {
	case -50:
		return -RndPL(6, 10);
	case -25:
		return -RndPL(1, 5);
	case 20:
		return RndPL(1, 5);
	case 36:
		return RndPL(6, 10);
	case 51:
		return RndPL(11, 15);
	case 66:
		return RndPL(16, 20);
	case 81:
		return RndPL(21, 30);
	case 96:
		return RndPL(31, 40);
	case 111:
		return RndPL(41, 50);
	case 126:
		return RndPL(51, 75);
	case 151:
		return RndPL(76, 100);
	default:
		app_fatal("Unknown to hit bonus");
	}
}

int SaveItemPower(const Player &player, Item &item, ItemPower &power)
{
	int r = RndPL(power.param1, power.param2);

	switch (power.type) {
	case IPL_TOHIT:
		item._iPLToHit += static_cast<int16_t>(r);
		break;
	case IPL_TOHIT_CURSE:
		item._iPLToHit -= static_cast<int16_t>(r);
		break;
	case IPL_DAMP:
		item._iPLDam += static_cast<int16_t>(r);
		break;
	case IPL_DAMP_CURSE:
		item._iPLDam -= static_cast<int16_t>(r);
		break;
	case IPL_DOPPELGANGER:
		item._iDamAcFlags |= ItemSpecialEffectHf::Doppelganger;
		[[fallthrough]];
	case IPL_TOHIT_DAMP:
		r = RndPL(power.param1, power.param2);
		item._iPLDam += static_cast<int16_t>(r);
		item._iPLToHit += CalculateToHitBonus(power.param1);
		break;
	case IPL_TOHIT_DAMP_CURSE:
		item._iPLDam -= static_cast<int16_t>(r);
		item._iPLToHit += CalculateToHitBonus(-power.param1);
		break;
	case IPL_ACP:
		item._iPLAC += r;
		break;
	case IPL_ACP_CURSE:
		item._iPLAC -= r;
		break;
	case IPL_SETAC:
		item._iAC = r;
		break;
	case IPL_AC_CURSE:
		item._iAC -= r;
		break;
	case IPL_FIRERES:
		item._iPLFR += r;
		break;
	case IPL_LIGHTRES:
		item._iPLLR += r;
		break;
	case IPL_MAGICRES:
		item._iPLMR += r;
		break;
	case IPL_ALLRES:
		item._iPLFR = std::max(item._iPLFR + r, 0);
		item._iPLLR = std::max(item._iPLLR + r, 0);
		item._iPLMR = std::max(item._iPLMR + r, 0);
		break;
	case IPL_SPLLVLADD:
		item._iSplLvlAdd = r;
		break;
	case IPL_CHARGES:
		item._iCharges *= power.param1;
		item._iMaxCharges = item._iCharges;
		break;
	case IPL_SPELL:
		item._iSpell = static_cast<SpellID>(power.param1);
		item._iCharges = power.param2;
		item._iMaxCharges = power.param2;
		break;
	case IPL_FIREDAM:
		item._iFlags |= ItemSpecialEffect::FireDamage;
		item._iFlags &= ~ItemSpecialEffect::LightningDamage;
		item._iFMinDam = power.param1;
		item._iFMaxDam = power.param2;
		item._iLMinDam = 0;
		item._iLMaxDam = 0;
		break;
	case IPL_LIGHTDAM:
		item._iFlags |= ItemSpecialEffect::LightningDamage;
		item._iFlags &= ~ItemSpecialEffect::FireDamage;
		item._iLMinDam = power.param1;
		item._iLMaxDam = power.param2;
		item._iFMinDam = 0;
		item._iFMaxDam = 0;
		break;
	case IPL_STR:
		item._iPLStr += r;
		break;
	case IPL_STR_CURSE:
		item._iPLStr -= r;
		break;
	case IPL_MAG:
		item._iPLMag += r;
		break;
	case IPL_MAG_CURSE:
		item._iPLMag -= r;
		break;
	case IPL_DEX:
		item._iPLDex += r;
		break;
	case IPL_DEX_CURSE:
		item._iPLDex -= r;
		break;
	case IPL_VIT:
		item._iPLVit += r;
		break;
	case IPL_VIT_CURSE:
		item._iPLVit -= r;
		break;
	case IPL_ATTRIBS:
		item._iPLStr += r;
		item._iPLMag += r;
		item._iPLDex += r;
		item._iPLVit += r;
		break;
	case IPL_ATTRIBS_CURSE:
		item._iPLStr -= r;
		item._iPLMag -= r;
		item._iPLDex -= r;
		item._iPLVit -= r;
		break;
	case IPL_GETHIT_CURSE:
		item._iPLGetHit += r;
		break;
	case IPL_GETHIT:
		item._iPLGetHit -= r;
		break;
	case IPL_LIFE:
		item._iPLHP += Fixed10_6::fromInt(r);
		break;
	case IPL_LIFE_CURSE:
		item._iPLHP -= Fixed10_6::fromInt(r);
		break;
	case IPL_MANA:
		item._iPLMana += Fixed10_6::fromInt(r);
		RedrawComponent(PanelDrawComponent::Mana);
		break;
	case IPL_MANA_CURSE:
		item._iPLMana -= Fixed10_6::fromInt(r);
		RedrawComponent(PanelDrawComponent::Mana);
		break;
	case IPL_DUR: {
		const int bonus = r * item._iMaxDur / 100;
		item._iMaxDur += bonus;
		item._iDurability += bonus;
	} break;
	case IPL_CRYSTALLINE:
		item._iPLDam += 140 + r * 2;
		[[fallthrough]];
	case IPL_DUR_CURSE:
		item._iMaxDur -= r * item._iMaxDur / 100;
		item._iMaxDur = std::max<uint8_t>(item._iMaxDur, 1);
		item._iDurability = item._iMaxDur;
		break;
	case IPL_INDESTRUCTIBLE:
		item._iDurability = DUR_INDESTRUCTIBLE;
		item._iMaxDur = DUR_INDESTRUCTIBLE;
		break;
	case IPL_LIGHT:
		item._iPLLight += power.param1;
		break;
	case IPL_LIGHT_CURSE:
		item._iPLLight -= power.param1;
		break;
	case IPL_MULT_ARROWS:
		item._iFlags |= ItemSpecialEffect::MultipleArrows;
		break;
	case IPL_FIRE_ARROWS:
		item._iFlags |= (ItemSpecialEffect::FireArrows | ItemSpecialEffect::FireDamage);
		item._iFlags &= ~ItemSpecialEffect::LightningArrows;
		item._iFMinDam = power.param1;
		item._iFMaxDam = power.param2;
		item._iLMinDam = 0;
		item._iLMaxDam = 0;
		break;
	case IPL_LIGHT_ARROWS:
		item._iFlags |= (ItemSpecialEffect::LightningArrows | ItemSpecialEffect::LightningDamage);
		item._iFlags &= ~ItemSpecialEffect::FireArrows;
		item._iLMinDam = power.param1;
		item._iLMaxDam = power.param2;
		item._iFMinDam = 0;
		item._iFMaxDam = 0;
		break;
	case IPL_FIREBALL:
		item._iFlags |= (ItemSpecialEffect::LightningArrows | ItemSpecialEffect::FireArrows);
		item._iFMinDam = power.param1;
		item._iFMaxDam = power.param2;
		item._iLMinDam = 0;
		item._iLMaxDam = 0;
		break;
	case IPL_THORNS:
		item._iFlags |= ItemSpecialEffect::Thorns;
		break;
	case IPL_NOMANA:
		item._iFlags |= ItemSpecialEffect::NoMana;
		RedrawComponent(PanelDrawComponent::Mana);
		break;
	case IPL_ABSHALFTRAP:
		item._iFlags |= ItemSpecialEffect::HalfTrapDamage;
		break;
	case IPL_KNOCKBACK:
		item._iFlags |= ItemSpecialEffect::Knockback;
		break;
	case IPL_3XDAMVDEM:
		item._iFlags |= ItemSpecialEffect::TripleDemonDamage;
		break;
	case IPL_ALLRESZERO:
		item._iFlags |= ItemSpecialEffect::ZeroResistance;
		break;
	case IPL_STEALMANA:
		if (power.param1 == 3)
			item._iFlags |= ItemSpecialEffect::StealMana3;
		if (power.param1 == 5)
			item._iFlags |= ItemSpecialEffect::StealMana5;
		RedrawComponent(PanelDrawComponent::Mana);
		break;
	case IPL_STEALLIFE:
		if (power.param1 == 3)
			item._iFlags |= ItemSpecialEffect::StealLife3;
		if (power.param1 == 5)
			item._iFlags |= ItemSpecialEffect::StealLife5;
		RedrawComponent(PanelDrawComponent::Health);
		break;
	case IPL_TARGAC:
		if (gbIsHellfire)
			item._iPLEnAc = power.param1;
		else
			item._iPLEnAc += r;
		break;
	case IPL_FASTATTACK:
		if (power.param1 == 1)
			item._iFlags |= ItemSpecialEffect::QuickAttack;
		if (power.param1 == 2)
			item._iFlags |= ItemSpecialEffect::FastAttack;
		if (power.param1 == 3)
			item._iFlags |= ItemSpecialEffect::FasterAttack;
		if (power.param1 == 4)
			item._iFlags |= ItemSpecialEffect::FastestAttack;
		break;
	case IPL_FASTRECOVER:
		if (power.param1 == 1)
			item._iFlags |= ItemSpecialEffect::FastHitRecovery;
		if (power.param1 == 2)
			item._iFlags |= ItemSpecialEffect::FasterHitRecovery;
		if (power.param1 == 3)
			item._iFlags |= ItemSpecialEffect::FastestHitRecovery;
		break;
	case IPL_FASTBLOCK:
		item._iFlags |= ItemSpecialEffect::FastBlock;
		break;
	case IPL_DAMMOD:
		item._iPLDamMod += r;
		break;
	case IPL_RNDARROWVEL:
		item._iFlags |= ItemSpecialEffect::RandomArrowVelocity;
		break;
	case IPL_SETDAM:
		item._iMinDam = power.param1;
		item._iMaxDam = power.param2;
		break;
	case IPL_SETDUR:
		item._iDurability = power.param1;
		item._iMaxDur = power.param1;
		break;
	case IPL_ONEHAND:
		item._iLoc = ILOC_ONEHAND;
		break;
	case IPL_DRAINLIFE:
		item._iFlags |= ItemSpecialEffect::DrainLife;
		break;
	case IPL_RNDSTEALLIFE:
		item._iFlags |= ItemSpecialEffect::RandomStealLife;
		break;
	case IPL_NOMINSTR:
		item._iMinStr = 0;
		break;
	case IPL_ADDACLIFE:
		item._iFlags |= (ItemSpecialEffect::LightningArrows | ItemSpecialEffect::FireArrows);
		item._iFMinDam = power.param1;
		item._iFMaxDam = power.param2;
		item._iLMinDam = 1;
		item._iLMaxDam = 0;
		break;
	case IPL_ADDMANAAC:
		item._iFlags |= (ItemSpecialEffect::LightningDamage | ItemSpecialEffect::FireDamage);
		item._iFMinDam = power.param1;
		item._iFMaxDam = power.param2;
		item._iLMinDam = 2;
		item._iLMaxDam = 0;
		break;
	case IPL_FIRERES_CURSE:
		item._iPLFR -= r;
		break;
	case IPL_LIGHTRES_CURSE:
		item._iPLLR -= r;
		break;
	case IPL_MAGICRES_CURSE:
		item._iPLMR -= r;
		break;
	case IPL_DEVASTATION:
		item._iDamAcFlags |= ItemSpecialEffectHf::Devastation;
		break;
	case IPL_DECAY:
		item._iDamAcFlags |= ItemSpecialEffectHf::Decay;
		item._iPLDam += r;
		break;
	case IPL_PERIL:
		item._iDamAcFlags |= ItemSpecialEffectHf::Peril;
		break;
	case IPL_JESTERS:
		item._iDamAcFlags |= ItemSpecialEffectHf::Jesters;
		break;
	case IPL_ACDEMON:
		item._iDamAcFlags |= ItemSpecialEffectHf::ACAgainstDemons;
		break;
	case IPL_ACUNDEAD:
		item._iDamAcFlags |= ItemSpecialEffectHf::ACAgainstUndead;
		break;
	case IPL_MANATOLIFE: {
		const Fixed10_6 portion = Fixed10_6::fromInt(player._pMaxManaBase.whole() * 50 / 100);
		item._iPLMana -= portion;
		item._iPLHP += portion;
	} break;
	case IPL_LIFETOMANA: {
		const Fixed10_6 portion = Fixed10_6::fromInt(player._pMaxHPBase.whole() * 40 / 100);
		item._iPLHP -= portion;
		item._iPLMana += portion;
	} break;
	case IPL_LIFEREGEN:
		item._iPLLifeRegen += r;
		break;
	case IPL_MANAREGEN:
		item._iPLManaRegen += r;
		break;
	default:
		break;
	}

	return r;
}

bool StringInPanel(const char *str)
{
	return GetLineWidth(str, GameFont12, 2) < 254;
}

int PLVal(int pv, int p1, int p2, int minv, int maxv)
{
	if (p1 == p2)
		return minv;
	if (minv == maxv)
		return minv;
	return minv + ((maxv - minv) * (100 * (pv - p1) / (p2 - p1)) / 100);
}

void SaveItemAffix(const Player &player, Item &item, const PLStruct &affix)
{
	auto power = affix.power;
	int value = SaveItemPower(player, item, power);

	value = PLVal(value, power.param1, power.param2, affix.minVal, affix.maxVal);
	if (item._iVAdd1 != 0 || item._iVMult1 != 0) {
		item._iVAdd2 = value;
		item._iVMult2 = affix.multVal;
	} else {
		item._iVAdd1 = value;
		item._iVMult1 = affix.multVal;
	}
}

std::optional<const PLStruct *> SelectAffix(
    const std::vector<PLStruct> &affixList,
    AffixItemType type,
    int minlvl, int maxlvl,
    bool onlygood,
    goodorevil goe,
    bool excludeChargesForStaffs)
{
	StaticVector<const PLStruct *, 256> eligibleAffixes;

	for (const PLStruct &affix : affixList) {
		if (!HasAnyOf(type, affix.PLIType))
			continue;
		if (affix.PLMinLvl < minlvl || affix.PLMinLvl > maxlvl)
			continue;
		if (onlygood && !affix.PLOk)
			continue;
		if ((goe == GOE_GOOD && affix.PLGOE == GOE_EVIL) || (goe == GOE_EVIL && affix.PLGOE == GOE_GOOD))
			continue;
		if (excludeChargesForStaffs && type == AffixItemType::Staff && affix.power.type == IPL_CHARGES)
			continue;

		for (int i = 0; i < affix.PLChance; ++i) {
			eligibleAffixes.push_back(&affix);
		}
	}

	if (eligibleAffixes.empty())
		return std::nullopt;

	return eligibleAffixes[GenerateRnd(static_cast<int>(eligibleAffixes.size()))];
}

std::optional<const PLStruct *> GetStaffPrefix(int maxlvl, bool onlygood)
{
	if (!FlipCoin(10) && !onlygood) {
		return std::nullopt;
	}

	return SelectAffix(ItemPrefixes, AffixItemType::Staff, 0, maxlvl, onlygood, GOE_ANY, false);
}

std::string GenerateStaffName(const ItemData &baseItemData, SpellID spellId, bool translate)
{
	std::string_view baseName = translate ? _(baseItemData.iName) : baseItemData.iName;
	std::string_view spellName = translate ? pgettext("spell", GetSpellData(spellId).sNameText) : GetSpellData(spellId).sNameText;
	const std::string_view normalFmt = translate ? pgettext("spell", /* TRANSLATORS: Constructs item names. Format: {Item} of {Spell}. Example: War Staff of Firewall */ "{0} of {1}") : "{0} of {1}";
	std::string name = FormatRuntime(normalFmt, baseName, spellName);
	if (!StringInPanel(name.c_str())) {
		std::string_view shortName = translate ? _(baseItemData.iSName) : baseItemData.iSName;
		name = FormatRuntime(normalFmt, shortName, spellName);
	}
	return name;
}

std::string GenerateStaffNameMagical(const ItemData &baseItemData, SpellID spellId, const PLStruct &power, bool translate, std::optional<bool> forceNameLengthCheck)
{
	std::string_view baseName = translate ? _(baseItemData.iName) : baseItemData.iName;
	const std::string_view magicFmt = translate ? pgettext("spell", /* TRANSLATORS: Constructs item names. Format: {Prefix} {Item} of {Spell}. Example: King's War Staff of Firewall */ "{0} {1} of {2}") : "{0} {1} of {2}";
	std::string_view spellName = translate ? pgettext("spell", GetSpellData(spellId).sNameText) : GetSpellData(spellId).sNameText;
	std::string_view prefixName = translate ? _(power.PLName) : power.PLName;

	std::string identifiedName = FormatRuntime(magicFmt, prefixName, baseName, spellName);
	if (forceNameLengthCheck ? *forceNameLengthCheck : !StringInPanel(identifiedName.c_str())) {
		std::string_view shortName = translate ? _(baseItemData.iSName) : baseItemData.iSName;
		identifiedName = FormatRuntime(magicFmt, prefixName, shortName, spellName);
	}
	return identifiedName;
}

void GetStaffPower(const Player &player, Item &item, int maxlvl, bool onlygood)
{
	std::optional<const PLStruct *> prefix = GetStaffPrefix(maxlvl, onlygood);
	if (prefix.has_value()) {
		item._iMagical = ITEM_QUALITY_MAGIC;
		SaveItemAffix(player, item, **prefix);
		item._iPrePower = (*prefix)->power.type;
	}

	const ItemData &baseItemData = AllItemsList[item.IDidx];
	const std::string staffName = GenerateStaffName(baseItemData, item._iSpell, false);

	CopyUtf8(item._iName, staffName, ItemNameLength);
	if (prefix.has_value()) {
		const std::string staffNameMagical = GenerateStaffNameMagical(baseItemData, item._iSpell, **prefix, false, std::nullopt);
		CopyUtf8(item._iIName, staffNameMagical, ItemNameLength);
	} else {
		CopyUtf8(item._iIName, item._iName, ItemNameLength);
	}

	CalcItemValue(item);
}

std::string GenerateMagicItemName(const std::string_view &baseNamel, const PLStruct *pPrefix, const PLStruct *pSufix, bool translate)
{
	if (pPrefix != nullptr && pSufix != nullptr) {
		const std::string_view fmt = translate ? _(/* TRANSLATORS: Constructs item names. Format: {Prefix} {Item} of {Suffix}. Example: King's Long Sword of the Whale */ "{0} {1} of {2}") : "{0} {1} of {2}";
		return FormatRuntime(fmt, translate ? _(pPrefix->PLName) : pPrefix->PLName, baseNamel, translate ? _(pSufix->PLName) : pSufix->PLName);
	}
	if (pPrefix != nullptr) {
		const std::string_view fmt = translate ? _(/* TRANSLATORS: Constructs item names. Format: {Prefix} {Item}. Example: King's Long Sword */ "{0} {1}") : "{0} {1}";
		return FormatRuntime(fmt, translate ? _(pPrefix->PLName) : pPrefix->PLName, baseNamel);
	}
	if (pSufix != nullptr) {
		const std::string_view fmt = translate ? _(/* TRANSLATORS: Constructs item names. Format: {Item} of {Suffix}. Example: Long Sword of the Whale */ "{0} of {1}") : "{0} of {1}";
		return FormatRuntime(fmt, baseNamel, translate ? _(pSufix->PLName) : pSufix->PLName);
	}

	return std::string(baseNamel);
}

void GetItemPowerPrefixAndSuffix(
    int minlvl, int maxlvl,
    AffixItemType flgs,
    bool onlygood,
    tl::function_ref<void(const PLStruct &prefix)> prefixFound,
    tl::function_ref<void(const PLStruct &suffix)> suffixFound)
{
	bool allocatePrefix = FlipCoin(4);
	bool allocateSuffix = !FlipCoin(3);
	if (!allocatePrefix && !allocateSuffix) {
		// At least try and give each item a prefix or suffix
		if (FlipCoin())
			allocatePrefix = true;
		else
			allocateSuffix = true;
	}
	goodorevil goe = GOE_ANY;
	if (!onlygood && !FlipCoin(3))
		onlygood = true;

	if (allocatePrefix) {
		std::optional<const PLStruct *> prefix = SelectAffix(ItemPrefixes, flgs, minlvl, maxlvl, onlygood, goe, true);
		if (prefix.has_value()) {
			goe = (*prefix)->PLGOE;
			prefixFound(**prefix);
		}
	}

	if (allocateSuffix) {
		std::optional<const PLStruct *> suffix = SelectAffix(ItemSuffixes, flgs, minlvl, maxlvl, onlygood, goe, true);
		if (suffix.has_value()) {
			suffixFound(**suffix);
		}
	}
}

/** POE2 Mashup: 1 in this many magic items rolls as rare instead. */
constexpr int RareItemChance = 5;
constexpr int MaxRareAffixesPerSide = 3;

/**
 * @brief Bit set of the stats an affix changes, for affixes whose tooltip line shows that stat's combined total.
 *
 * Two affixes on one rare that share a stat would print confusing totals (e.g. "+50% damage" twice), so they can't both roll.
 */
uint8_t RareAffixStatGroups(item_effect_type power)
{
	constexpr uint8_t ToHit = 1 << 0;
	constexpr uint8_t Damage = 1 << 1;
	constexpr uint8_t Resists = 1 << 2;
	constexpr uint8_t Attributes = 1 << 3;
	switch (power) {
	case IPL_TOHIT: return ToHit;
	case IPL_DAMP:
	case IPL_CRYSTALLINE: return Damage;
	case IPL_TOHIT_DAMP:
	case IPL_DOPPELGANGER: return ToHit | Damage;
	case IPL_ALLRES: return Resists;
	case IPL_ATTRIBS: return Attributes;
	default: return 0;
	}
}

bool RareAffixesOverlap(item_effect_type a, item_effect_type b)
{
	if (a == b)
		return true;
	if ((RareAffixStatGroups(a) & RareAffixStatGroups(b)) != 0)
		return true;
	// "All resistances" and "all attributes" print the same totals as their single-stat versions.
	const auto coveredBy = [](item_effect_type single, item_effect_type all) {
		return (all == IPL_ALLRES && IsAnyOf(single, IPL_FIRERES, IPL_LIGHTRES, IPL_MAGICRES))
		    || (all == IPL_ATTRIBS && IsAnyOf(single, IPL_STR, IPL_MAG, IPL_DEX, IPL_VIT));
	};
	return coveredBy(a, b) || coveredBy(b, a);
}

/** @brief POE2 Mashup: the implicit a Vaal Orb added to the item, if any. */
void ApplyCorruptionImplicit(const Player &player, Item &item)
{
	const int kind = static_cast<int>((item.dwBuff & CF_POE2_CORRUPT_IMPLICIT) >> 14);
	if (kind == 0)
		return;
	ItemPower power = CorruptionImplicitPower(kind);
	SaveItemPower(player, item, power);
}

struct RareAffixRoll {
	const PLStruct *affix;
	bool prefix;
	int value;
};

/**
 * @brief Picks a rare's affixes and saves their powers to the item.
 *
 * Exalted Orbs raise the count after the base roll, so the first affixes stay the same.
 */
std::vector<RareAffixRoll> RollRareAffixes(const Player &player, Item &item, int minlvl, int maxlvl, AffixItemType flgs)
{
	const int exalts = static_cast<int>((item.dwBuff & CF_POE2_EXALTS) >> 6);
	// Rares from the POE2 loot rules have 4 to 6 affixes, mostly 4; crafted ones 3 to 6.
	const int baseAffixes = GetLootRarity(item) ? RollNaturalRareAffixCount() : 3 + GenerateRnd(4);
	const int wantedAffixes = std::min<int>(baseAffixes + exalts, static_cast<int>(MaxRareAffixes));
	int prefixes = 0;
	int suffixes = 0;
	std::vector<RareAffixRoll> rolls;
	std::array<item_effect_type, MaxRareAffixes> powers;
	powers.fill(IPL_INVALID);

	for (int attempt = 0; attempt < wantedAffixes * 3 && static_cast<int>(rolls.size()) < wantedAffixes; attempt++) {
		bool rollPrefix = (attempt % 2) == 0;
		if (rollPrefix && prefixes >= MaxRareAffixesPerSide)
			rollPrefix = false;
		else if (!rollPrefix && suffixes >= MaxRareAffixesPerSide)
			rollPrefix = true;

		const std::optional<const PLStruct *> affix = SelectAffix(rollPrefix ? ItemPrefixes : ItemSuffixes, flgs, minlvl, maxlvl, /*onlygood=*/true, GOE_ANY, true);
		if (!affix.has_value())
			continue;
		const PLStruct &chosen = **affix;
		const auto powersEnd = powers.begin() + rolls.size();
		if (std::any_of(powers.begin(), powersEnd, [&chosen](item_effect_type power) { return RareAffixesOverlap(power, chosen.power.type); }))
			continue;

		ItemPower power = chosen.power;
		const int value = SaveItemPower(player, item, power);
		powers[rolls.size()] = chosen.power.type;
		rolls.emplace_back(RareAffixRoll { &chosen, rollPrefix, value });
		if (rollPrefix)
			prefixes++;
		else
			suffixes++;
	}
	return rolls;
}

/**
 * @brief Rolls a POE2-style rare item: 3 to 6 distinct affixes, at most 3 prefixes and 3 suffixes, never cursed.
 */
void GetItemPowerRare(const Player &player, Item &item, int minlvl, int maxlvl, AffixItemType flgs)
{
	const uint32_t divine = (item.dwBuff & CF_POE2_DIVINE) >> 9;
	std::vector<RareAffixRoll> rolls;
	if (divine == 0) {
		rolls = RollRareAffixes(player, item, minlvl, maxlvl, flgs);
	} else {
		// A Divine Orb keeps the affixes and rolls their values again from a seed of its own.
		Item scratch = item;
		rolls = RollRareAffixes(player, scratch, minlvl, maxlvl, flgs);
		SetRndSeed(static_cast<uint32_t>(item._iSeed) ^ (divine * 0x9E3779B9U));
		for (RareAffixRoll &roll : rolls) {
			ItemPower power = roll.affix->power;
			roll.value = SaveItemPower(player, item, power);
		}
	}

	if (rolls.empty())
		return;

	size_t numPowers = 0;
	int valueAdd = 0;
	int valueMult = 0;
	bool hasPrefix = false;
	bool hasSuffix = false;
	for (const RareAffixRoll &roll : rolls) {
		const PLStruct &chosen = *roll.affix;
		valueAdd += PLVal(roll.value, chosen.power.param1, chosen.power.param2, chosen.minVal, chosen.maxVal);
		valueMult += chosen.multVal;
		item._iRareTiers[numPowers] = AffixTier(roll.prefix ? ItemPrefixes : ItemSuffixes, chosen);
		item._iRarePowers[numPowers++] = chosen.power.type;
		if (roll.prefix && !hasPrefix) {
			item._iPrePower = chosen.power.type;
			hasPrefix = true;
		} else if (!roll.prefix && !hasSuffix) {
			item._iSufPower = chosen.power.type;
			hasSuffix = true;
		}
	}

	item._iMagical = ITEM_QUALITY_RARE;
	item._iVAdd1 = valueAdd;
	item._iVMult1 = valueMult;
	item._iVAdd2 = 0;
	item._iVMult2 = 0;
	CopyUtf8(item._iIName, GetRareItemName(item), ItemNameLength);
	CalcItemValue(item);
}

/** @brief Whether the item rolls its affixes from the POE2 itemization tables rather than Diablo's affix lists. */
bool HasPoe2ModTables(const Item &item)
{
	return gbIsPoe2 && (item.dwBuff & CF_POE2_MODS) != 0 && GetPoe2ItemLevel(item) > 0 && !ModifierFamilies().empty() && ItemModSlot(item).has_value();
}

/**
 * @brief Rolls a magic or rare item's modifiers from the POE2 itemization tables.
 *
 * The picks come from the item seed, and the values from a seed a Divine Orb changes, so a Divine Orb rerolls values only.
 */
std::vector<Poe2ItemMod> RollNaturalPoe2Mods(const Item &item, bool rare)
{
	const uint32_t seed = static_cast<uint32_t>(item._iSeed);
	const uint32_t divine = (item.dwBuff & CF_POE2_DIVINE) >> 9;
	const uint32_t valueSeed = seed ^ 0x6A09E667U ^ (divine * 0x9E3779B9U);
	int count;
	int perSide;
	if (rare && IsJewel(item)) {
		// Rare jewels have 3 or 4 modifiers, at most 2 of each kind.
		const int exalts = static_cast<int>((item.dwBuff & CF_POE2_EXALTS) >> 6);
		count = std::min(2 + MagicModCount(seed) + exalts, 2 * MaxRareJewelModsPerSide);
		perSide = MaxRareJewelModsPerSide;
	} else if (rare) {
		const int exalts = static_cast<int>((item.dwBuff & CF_POE2_EXALTS) >> 6);
		count = std::min(RareModCount(seed) + exalts, static_cast<int>(MaxRareAffixes));
		perSide = MaxRareModsPerSide;
	} else {
		count = MagicModCount(seed);
		perSide = MaxMagicModsPerSide;
	}
	// Ground drops can roll a hidden quality tier that culls weak modifiers; crafting and shops always use the full pool.
	const int itemLevel = GetPoe2ItemLevel(item);
	const int unidentifiedTier = GetLootRarity(item) ? UnidentifiedTier(seed, itemLevel) : 1;
	return RollItemMods(*ItemModSlot(item), ItemModBaseTags(item), itemLevel, count, perSide, seed, valueSeed, unidentifiedTier);
}

/** @brief The rarity the item is made with: the one crafting gave it, or the one the loot roll picked. */
std::optional<item_quality> RarityToRoll(const Item &item)
{
	// A waystone's rarity and modifiers are its own (poe2/waystones.cpp).
	if (item._iMiscId == IMISC_WAYSTONE)
		return std::nullopt;
	if (item._iPoe2Craft.explicitMods)
		return static_cast<item_quality>(item._iPoe2Craft.rarity);
	return GetLootRarity(item);
}

/**
 * @brief Gives a magic or rare item its modifiers from the POE2 itemization tables.
 *
 * Crafted items have theirs written out; others roll them from their seed.
 */
void GetItemPowerPoe2Mods(Item &item, bool rare)
{
	std::vector<Poe2ItemMod> mods;
	if (item._iPoe2Craft.explicitMods) {
		rare = item._iPoe2Craft.rarity == ITEM_QUALITY_RARE;
		mods = ResolveCraftMods(item, RollNaturalPoe2Mods(item, !item._iPoe2Craft.rolledMagic));
	} else {
		mods = RollNaturalPoe2Mods(item, rare);
		if (mods.empty())
			return;
	}
	// POE2 Mashup: a catalyst's quality on jewellery boosts its kind of modifiers.
	if (const auto catalyst = static_cast<Catalyst>(item._iPoe2Craft.catalyst); catalyst != Catalyst::None) {
		const int quality = ItemQuality(item);
		for (Poe2ItemMod &mod : mods) {
			const ModFamily *family = ItemModFamily(mod);
			if (family == nullptr || !CatalystBoosts(catalyst, family->effect))
				continue;
			for (int16_t &value : mod.values)
				value = static_cast<int16_t>(value + (value * quality / 100));
		}
	}
	// Like POE2, prefixes are listed before suffixes.
	std::stable_partition(mods.begin(), mods.end(), [](const Poe2ItemMod &mod) { return ItemModFamily(mod)->prefix; });

	int valueAdd = 0;
	for (size_t i = 0; i < mods.size(); i++) {
		ApplyItemMod(item, mods[i]);
		item._iPoe2Mods[i] = mods[i];
		// Better tiers are worth much more gold.
		const int strength = 9 - mods[i].tier;
		valueAdd += strength * strength * 120;
	}
	item._iMagical = rare ? ITEM_QUALITY_RARE : ITEM_QUALITY_MAGIC;
	item._iVAdd1 = valueAdd;
	item._iVMult1 = static_cast<int>(mods.size());
	item._iVAdd2 = 0;
	item._iVMult2 = 0;
	if (rare) {
		CopyUtf8(item._iIName, GetRareItemName(item), ItemNameLength);
	} else {
		CopyUtf8(item._iIName, MagicModItemName(item, item._iName), ItemNameLength);
		if (!StringInPanel(item._iIName))
			CopyUtf8(item._iIName, MagicModItemName(item, AllItemsList[item.IDidx].iSName), ItemNameLength);
	}
	CalcItemValue(item);
}

/**
 * @brief POE2 jewels are always magic or rare and roll from the jewel modifier pools.
 *
 * A jewel made without an item level (a shop or the item spawner) takes one from the level it was made at.
 */
void SetupJewel(Item &item, int lvl)
{
	if (GetPoe2ItemLevel(item) == 0)
		SetPoe2ItemLevel(item, Poe2ItemLevel(lvl + (15 * sgGameInitInfo.nDifficulty)));
	const std::optional<item_quality> lootRarity = RarityToRoll(item);
	// Without a loot roll, one jewel in four is rare.
	const bool rare = lootRarity ? *lootRarity == ITEM_QUALITY_RARE : (static_cast<uint32_t>(item._iSeed) * 0x9E3779B9U) >> 30 == 0;
	GetItemPowerPoe2Mods(item, rare);
}

void GetItemPower(const Player &player, Item &item, int minlvl, int maxlvl, AffixItemType flgs, bool onlygood)
{
	// POE2 loot drops already know their rarity; older items roll 1 in 5 magic items as rare.
	const std::optional<item_quality> lootRarity = RarityToRoll(item);
	const bool rare = lootRarity == ITEM_QUALITY_RARE || (!lootRarity && (item.dwBuff & CF_POE2) != 0 && GenerateRnd(RareItemChance) == 0);
	if (HasPoe2ModTables(item)) {
		GetItemPowerPoe2Mods(item, rare);
		return;
	}
	if (rare) {
		GetItemPowerRare(player, item, minlvl, maxlvl, flgs);
		return;
	}
	if (lootRarity)
		onlygood = true;

	const PLStruct *pPrefix = nullptr;
	const PLStruct *pSufix = nullptr;
	GetItemPowerPrefixAndSuffix(
	    minlvl, maxlvl, flgs, onlygood,
	    [&item, &player, &pPrefix](const PLStruct &prefix) {
		    item._iMagical = ITEM_QUALITY_MAGIC;
		    SaveItemAffix(player, item, prefix);
		    item._iPrePower = prefix.power.type;
		    item._iPreTier = AffixTier(ItemPrefixes, prefix);
		    pPrefix = &prefix;
	    },
	    [&item, &player, &pSufix](const PLStruct &suffix) {
		    item._iMagical = ITEM_QUALITY_MAGIC;
		    SaveItemAffix(player, item, suffix);
		    item._iSufPower = suffix.power.type;
		    item._iSufTier = AffixTier(ItemSuffixes, suffix);
		    pSufix = &suffix;
	    });

	CopyUtf8(item._iIName, GenerateMagicItemName(item._iName, pPrefix, pSufix, false), ItemNameLength);
	if (!StringInPanel(item._iIName)) {
		CopyUtf8(item._iIName, GenerateMagicItemName(AllItemsList[item.IDidx].iSName, pPrefix, pSufix, false), ItemNameLength);
	}
	if (pPrefix != nullptr || pSufix != nullptr)
		CalcItemValue(item);
}

void GetStaffSpell(const Player &player, Item &item, int lvl, bool onlygood)
{
	if (!gbIsHellfire && FlipCoin(4)) {
		GetItemPower(player, item, lvl / 2, lvl, AffixItemType::Staff, onlygood);
		return;
	}

	int l = lvl / 2;
	if (l == 0)
		l = 1;
	int rv = GenerateRnd(static_cast<int32_t>(SpellsData.size())) + 1;

	if (gbIsSpawn && lvl > 10)
		lvl = 10;

	int s = static_cast<int8_t>(SpellID::Firebolt);
	SpellID bs = SpellID::Null;
	while (rv > 0) {
		const int sLevel = GetSpellStaffLevel(static_cast<SpellID>(s));
		if (sLevel != -1 && l >= sLevel) {
			rv--;
			bs = static_cast<SpellID>(s);
		}
		s++;
		if (!gbIsMultiplayer && s == static_cast<int8_t>(SpellID::Resurrect))
			s = static_cast<int8_t>(SpellID::Telekinesis);
		if (!gbIsMultiplayer && s == static_cast<int8_t>(SpellID::HealOther))
			s = static_cast<int8_t>(SpellID::BloodStar);
		if (static_cast<size_t>(s) == SpellsData.size())
			s = static_cast<int8_t>(SpellID::Firebolt);
	}

	const int minc = GetSpellData(bs).sStaffMin;
	const int maxc = GetSpellData(bs).sStaffMax - minc + 1;
	item._iSpell = bs;
	item._iCharges = minc + GenerateRnd(maxc);
	item._iMaxCharges = item._iCharges;

	item._iMinMag = GetSpellData(bs).minInt;
	const int v = item._iCharges * GetSpellData(bs).staffCost() / 5;
	item._ivalue += v;
	item._iIvalue += v;
	GetStaffPower(player, item, lvl, onlygood);
}

void GetOilType(Item &item, int maxLvl)
{
	int cnt = 2;
	int8_t rnd[32] = { 5, 6 };

	if (!gbIsMultiplayer) {
		if (maxLvl == 0)
			maxLvl = 1;

		cnt = 0;
		for (size_t j = 0; j < sizeof(OilLevels) / sizeof(OilLevels[0]); j++) {
			if (OilLevels[j] <= maxLvl) {
				rnd[cnt] = static_cast<int8_t>(j);
				cnt++;
			}
		}
	}

	const int8_t t = rnd[GenerateRnd(cnt)];

	CopyUtf8(item._iName, OilNames[t], ItemNameLength);
	CopyUtf8(item._iIName, OilNames[t], ItemNameLength);
	item._iMiscId = OilMagic[t];
	item._ivalue = OilValues[t];
	item._iIvalue = OilValues[t];
}

void GetItemBonus(const Player &player, Item &item, int minlvl, int maxlvl, bool onlygood, bool allowspells)
{
	minlvl = std::min(minlvl, 25);

	switch (item._itype) {
	case ItemType::Sword:
	case ItemType::Axe:
	case ItemType::Mace:
		GetItemPower(player, item, minlvl, maxlvl, AffixItemType::Weapon, onlygood);
		break;
	case ItemType::Bow:
		GetItemPower(player, item, minlvl, maxlvl, AffixItemType::Bow, onlygood);
		break;
	case ItemType::Shield:
		GetItemPower(player, item, minlvl, maxlvl, AffixItemType::Shield, onlygood);
		break;
	case ItemType::LightArmor:
	case ItemType::Helm:
	case ItemType::MediumArmor:
	case ItemType::HeavyArmor:
		GetItemPower(player, item, minlvl, maxlvl, AffixItemType::Armor, onlygood);
		break;
	case ItemType::Staff:
		// POE2 Mashup: quarterstaffs are martial weapons and roll weapon affixes, not spells.
		if (gbIsPoe2 && item._iMiscId != IMISC_STAFF)
			GetItemPower(player, item, minlvl, maxlvl, AffixItemType::Weapon, onlygood);
		else if (allowspells)
			GetStaffSpell(player, item, maxlvl, onlygood);
		else
			GetItemPower(player, item, minlvl, maxlvl, AffixItemType::Staff, onlygood);
		break;
	case ItemType::Ring:
	case ItemType::Amulet:
		GetItemPower(player, item, minlvl, maxlvl, AffixItemType::Misc, onlygood);
		break;
	case ItemType::None:
	case ItemType::Misc:
	case ItemType::Gold:
		break;
	}
}

struct WeightedItemIndex {
	_item_indexes index;
	unsigned cumulativeWeight;
};

_item_indexes GetItemIndexForDroppableItem(bool considerDropRate, tl::function_ref<bool(const ItemData &item)> isItemOkay)
{
	static std::vector<WeightedItemIndex> ril;
	ril.clear();

	unsigned cumulativeWeight = 0;
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		if (!IsItemAvailable(static_cast<int>(i)))
			continue;
		const ItemData &item = AllItemsList[i];
		if (item.dropRate == 0)
			continue;
		if (IsAnyOf(item.iSpell, SpellID::Resurrect, SpellID::HealOther) && !gbIsMultiplayer)
			continue;
		// Skill gems replaced Diablo's spell books, so no book ever drops or sells.
		if (gbIsPoe2 && item.itype == ItemType::Misc && item.iMiscId == IMISC_BOOK)
			continue;
		if (!isItemOkay(item))
			continue;
		unsigned weight = considerDropRate ? item.dropRate : 1;
		// There are many skill gems, so every other item counts four times as much to keep gems a treat.
		if (gbIsPoe2)
			weight *= SkillForGemData(item).isEmpty() ? 4 : 1;
		cumulativeWeight += weight;
		ril.push_back({ static_cast<_item_indexes>(i), cumulativeWeight });
	}
	const auto targetWeight = static_cast<unsigned>(RandomIntLessThan(static_cast<int>(cumulativeWeight)));
	return std::upper_bound(ril.begin(), ril.end(), targetWeight, [](unsigned target, const WeightedItemIndex &value) { return target < value.cumulativeWeight; })->index;
}

_item_indexes RndUItem(Monster *monster)
{
	int itemMaxLevel = ItemsGetCurrlevel() * 2;
	if (monster != nullptr)
		itemMaxLevel = monster->level(sgGameInitInfo.nDifficulty);
	return GetItemIndexForDroppableItem(false, [&itemMaxLevel](const ItemData &item) {
		if (item.itype == ItemType::Misc && item.iMiscId == IMISC_BOOK)
			return true;
		if (itemMaxLevel < item.iMinMLvl)
			return false;
		if (IsAnyOf(item.itype, ItemType::Gold, ItemType::Misc))
			return false;
		return true;
	});
}

_item_indexes RndAllItems()
{
	if (GenerateRnd(100) > 25)
		return IDI_GOLD;

	int itemMaxLevel = ItemsGetCurrlevel() * 2;
	return GetItemIndexForDroppableItem(false, [&itemMaxLevel](const ItemData &item) {
		return itemMaxLevel >= item.iMinMLvl;
	});
}

_item_indexes RndTypeItems(ItemType itemType, int imid, int lvl)
{
	int itemMaxLevel = lvl * 2;
	return GetItemIndexForDroppableItem(false, [&itemMaxLevel, &itemType, &imid](const ItemData &item) {
		if (itemMaxLevel < item.iMinMLvl)
			return false;
		if (item.itype != itemType)
			return false;
		if (imid != -1 && item.iMiscId != imid)
			return false;
		return true;
	});
}

std::vector<uint8_t> GetValidUniques(int lvl, unique_base_item baseItemId)
{
	std::vector<uint8_t> validUniques;
	int index = 0;
	for (const UniqueItem &itemData : UniqueItems) {
		if (itemData.UIItemId == baseItemId && lvl >= itemData.UIMinLvl) {
			validUniques.push_back(index);
		}
		index++;
	}
	return validUniques;
}

_unique_items CheckUnique(Item &item, int lvl, int uper, int uidOffset = 0)
{
	if (GenerateRnd(100) > uper)
		return UITEM_INVALID;

	auto validUniques = GetValidUniques(lvl, AllItemsList[item.IDidx].iItemId);

	if (validUniques.empty())
		return UITEM_INVALID;

	DiscardRandomValues(1);

	// Check if uidOffset is out of bounds
	if (static_cast<size_t>(uidOffset) >= validUniques.size()) {
		return UITEM_INVALID;
	}

	const uint8_t selectedUniqueIndex = validUniques[validUniques.size() - 1 - uidOffset];

	return static_cast<_unique_items>(selectedUniqueIndex);
}

void GetUniqueItem(const Player &player, Item &item, _unique_items uid)
{
	const auto &uniqueItemData = UniqueItems[uid];

	// POE2 Mashup: a Divine Orb rolls the values again from a seed of their own.
	const uint32_t divine = (item.dwBuff & CF_POE2) != 0 ? (item.dwBuff & CF_POE2_DIVINE) >> 9 : 0;
	const uint32_t rngState = GetLCGEngineState();
	if (divine != 0)
		SetRndSeed(static_cast<uint32_t>(item._iSeed) ^ (divine * 0x9E3779B9U));
	for (auto power : uniqueItemData.powers) {
		if (power.type == IPL_INVALID)
			break;
		SaveItemPower(player, item, power);
	}
	if (divine != 0)
		SetRndSeed(rngState);

	CopyUtf8(item._iIName, uniqueItemData.UIName, ItemNameLength);
	if (uniqueItemData.UICurs != ICURS_DEFAULT)
		item._iCurs = uniqueItemData.UICurs;
	item._iIvalue = uniqueItemData.UIValue;

	if (item._iMiscId == IMISC_UNIQUE)
		item._iSeed = uid;

	item._iUid = uid;
	item._iMagical = ITEM_QUALITY_UNIQUE;
	item._iCreateInfo |= CF_UNIQUE;
	ApplyPoe2UniqueEffects(item);
}

void ItemRndDur(Item &item)
{
	if (item._iDurability > 0 && item._iDurability != DUR_INDESTRUCTIBLE)
		item._iDurability = GenerateRnd(item._iMaxDur / 2) + (item._iMaxDur / 4) + 1;
}

int GetItemBLevel(int lvl, item_misc_id miscId, bool onlygood, bool uper15)
{
	int iblvl = -1;
	if (GenerateRnd(100) <= 10
	    || GenerateRnd(100) <= lvl
	    || onlygood
	    || IsAnyOf(miscId, IMISC_STAFF, IMISC_RING, IMISC_AMULET)) {
		iblvl = lvl;
	}
	if (uper15)
		iblvl = lvl + 4;
	return iblvl;
}

void SetupBaseItem(Point position, _item_indexes idx, bool onlygood, bool sendmsg, bool delta, bool spawn = false)
{
	if (ActiveItemCount >= MAXITEMS)
		return;

	const int ii = AllocateItem();
	auto &item = Items[ii];
	GetSuperItemSpace(position, ii);
	const int curlv = ItemsGetCurrlevel();

	if (gbIsPoe2 && EquipmentClassWeight(AllItemsList[idx].itype) > 0)
		SetPoe2ItemLevel(item, Poe2ItemLevel((2 * curlv) + (15 * sgGameInitInfo.nDifficulty)));
	SetupAllItems(*MyPlayer, item, idx, AdvanceRndSeed(), 2 * curlv, 1, onlygood, delta);
	TryRandomUniqueItem(item, idx, 2 * curlv, 1, onlygood, delta);
	SetupItem(item);

	if (sendmsg)
		NetSendCmdPItem(false, CMD_DROPITEM, item.position, item);
	if (delta)
		DeltaAddItem(ii);
	if (spawn)
		NetSendCmdPItem(false, CMD_SPAWNITEM, item.position, item);
}

void SetupAllUseful(Item &item, int iseed, int lvl)
{
	item._iSeed = iseed;
	SetRndSeed(iseed);

	_item_indexes idx;

	if (gbIsHellfire) {
		switch (GenerateRnd(7)) {
		case 0:
			idx = IDI_PORTAL;
			if (lvl <= 1)
				idx = IDI_HEAL;
			break;
		case 1:
		case 2:
			idx = IDI_HEAL;
			break;
		case 3:
			idx = IDI_PORTAL;
			if (lvl <= 1)
				idx = IDI_MANA;
			break;
		case 4:
		case 5:
			idx = IDI_MANA;
			break;
		default:
			idx = IDI_OIL;
			break;
		}
	} else {
		idx = PickRandomlyAmong({ IDI_MANA, IDI_HEAL });

		if (lvl > 1 && FlipCoin(3))
			idx = IDI_PORTAL;
	}

	GetItemAttrs(item, idx, lvl);
	item._iCreateInfo = lvl | CF_USEFUL;
	FinishFlask(item);
	SetupItem(item);
}

uint8_t Char2int(uint8_t input)
{
	if (input >= '0' && input <= '9')
		return input - '0';
	if (input >= 'A' && input <= 'F')
		return input - 'A' + 10;
	return 0;
}

void Hex2bin(const char *src, int bytes, uint8_t *target)
{
	for (int i = 0; i < bytes; i++, src += 2) {
		target[i] = (Char2int(src[0]) << 4) | Char2int(src[1]);
	}
}

void SpawnRock()
{
	if (ActiveItemCount >= MAXITEMS)
		return;

	const Object *stand = nullptr;
	for (int i = 0; i < ActiveObjectCount; i++) {
		const Object &object = Objects[ActiveObjects[i]];
		if (object._otype == OBJ_STAND) {
			stand = &object;
			break;
		}
	}

	if (stand == nullptr)
		return;

	const int ii = AllocateItem();
	Item &item = Items[ii];

	item.position = stand->position;
	dItem[item.position.x][item.position.y] = ii + 1;
	const int curlv = ItemsGetCurrlevel();
	GetItemAttrs(item, IDI_ROCK, curlv);
	SetupItem(item);
	item.selectionRegion = SelectionRegion::Middle;
	item._iPostDraw = true;
	item.AnimInfo.currentFrame = 10;
	item._iAnimFlag = true;
	item._iCreateInfo |= CF_PREGEN;

	DeltaAddItem(ii);
}

void ItemDoppel()
{
	if (!gbIsMultiplayer)
		return;

	static int idoppely = 16;

	for (int idoppelx = 16; idoppelx < 96; idoppelx++) {
		if (dItem[idoppelx][idoppely] != 0) {
			Item *i = &Items[dItem[idoppelx][idoppely] - 1];
			if (i->position.x != idoppelx || i->position.y != idoppely)
				dItem[idoppelx][idoppely] = 0;
		}
	}

	idoppely++;
	if (idoppely == 96)
		idoppely = 16;
}

void AddItemInfoBoxString(const std::string_view str)
{
	// POE2 Mashup: item details always go in the tooltip by the item, never the box at the bottom.
	const bool floatingInfoBoxEnabled = gbIsPoe2 || *GetOptions().Gameplay.floatingInfoBox;
	AddInfoBoxString(str, floatingInfoBoxEnabled);
}

void AddItemInfoBoxString(std::string &&str)
{
	const bool floatingInfoBoxEnabled = gbIsPoe2 || *GetOptions().Gameplay.floatingInfoBox;
	AddInfoBoxString(std::move(str), floatingInfoBoxEnabled);
}

void PrintItemOil(char iDidx)
{
	switch (iDidx) {
	case IMISC_OILACC:
		AddItemInfoBoxString(_("increases a weapon's"));
		AddItemInfoBoxString(_("chance to hit"));
		break;
	case IMISC_OILMAST:
		AddItemInfoBoxString(_("greatly increases a"));
		AddItemInfoBoxString(_("weapon's chance to hit"));
		break;
	case IMISC_OILSHARP:
		AddItemInfoBoxString(_("increases a weapon's"));
		AddItemInfoBoxString(_("damage potential"));
		break;
	case IMISC_OILDEATH:
		AddItemInfoBoxString(_("greatly increases a weapon's"));
		AddItemInfoBoxString(_("damage potential - not bows"));
		break;
	case IMISC_OILSKILL:
		AddItemInfoBoxString(_("reduces attributes needed"));
		AddItemInfoBoxString(_("to use armor or weapons"));
		break;
	case IMISC_OILBSMTH:
		AddItemInfoBoxString(/*xgettext:no-c-format*/ _("restores 20% of an"));
		AddItemInfoBoxString(_("item's durability"));
		break;
	case IMISC_OILFORT:
		AddItemInfoBoxString(_("increases an item's"));
		AddItemInfoBoxString(_("current and max durability"));
		break;
	case IMISC_OILPERM:
		AddItemInfoBoxString(_("makes an item indestructible"));
		break;
	case IMISC_OILHARD:
		AddItemInfoBoxString(_("increases the armor class"));
		AddItemInfoBoxString(_("of armor and shields"));
		break;
	case IMISC_OILIMP:
		AddItemInfoBoxString(_("greatly increases the armor"));
		AddItemInfoBoxString(_("class of armor and shields"));
		break;
	case IMISC_RUNEF:
		AddItemInfoBoxString(_("sets fire trap"));
		break;
	case IMISC_RUNEL:
	case IMISC_GR_RUNEL:
		AddItemInfoBoxString(_("sets lightning trap"));
		break;
	case IMISC_GR_RUNEF:
		AddItemInfoBoxString(_("sets fire trap"));
		break;
	case IMISC_RUNES:
		AddItemInfoBoxString(_("sets petrification trap"));
		break;
	case IMISC_FULLHEAL:
		AddItemInfoBoxString(_("restore all life"));
		break;
	case IMISC_HEAL:
		AddItemInfoBoxString(_("restore some life"));
		break;
	case IMISC_MANA:
		AddItemInfoBoxString(_("restore some mana"));
		break;
	case IMISC_FULLMANA:
		AddItemInfoBoxString(_("restore all mana"));
		break;
	case IMISC_ELIXSTR:
		AddItemInfoBoxString(_("increase strength"));
		break;
	case IMISC_ELIXMAG:
		AddItemInfoBoxString(_("increase magic"));
		break;
	case IMISC_ELIXDEX:
		AddItemInfoBoxString(_("increase dexterity"));
		break;
	case IMISC_ELIXVIT:
		AddItemInfoBoxString(_("increase vitality"));
		break;
	case IMISC_REJUV:
		AddItemInfoBoxString(_("restore some life and mana"));
		break;
	case IMISC_FULLREJUV:
		AddItemInfoBoxString(_("restore all life and mana"));
		break;
	case IMISC_ARENAPOT:
		AddItemInfoBoxString(_("restore all life and mana"));
		AddItemInfoBoxString(_("(works only in arenas)"));
		break;
	}
}

Point DrawUniqueInfoWindow(const Surface &out)
{
	const bool isInStash = IsStashOpen && GetLeftPanel().contains(MousePosition);
	int panelX, panelY;
	if (isInStash) {
		ClxDraw(out, GetPanelPosition(UiPanels::Stash, { 24 + SidePanelSize.width, 327 }), (*pSTextBoxCels)[0]);
		panelX = GetLeftPanel().position.x + SidePanelSize.width + 27;
		panelY = GetLeftPanel().position.y + 28;
	} else {
		ClxDraw(out, GetPanelPosition(UiPanels::Inventory, { 24 - SidePanelSize.width, 327 }), (*pSTextBoxCels)[0]);
		panelX = GetRightPanel().position.x - SidePanelSize.width + 27;
		panelY = GetRightPanel().position.y + 28;
	}

	const Point rightInfoPos = GetRightPanel().position - Displacement { SidePanelSize.width, 0 };
	const Point leftInfoPos = GetLeftPanel().position + Displacement { SidePanelSize.width, 0 };

	const bool isInfoOverlapping = IsLeftPanelOpen() && IsRightPanelOpen() && GetLeftPanel().contains(rightInfoPos);
	const int fadeLevel = isInfoOverlapping ? 3 : 1;

	for (int i = 0; i < fadeLevel; ++i) {
		DrawHalfTransparentRectTo(out, panelX, panelY, 265, 297);
	}

	return isInStash ? leftInfoPos : rightInfoPos;
}

void printItemMiscKBM(const Item &item, const bool isOil, const bool isCastOnTarget)
{
	if (item._iMiscId == IMISC_MAPOFDOOM) {
		AddItemInfoBoxString(_("Right-click to view"));
	} else if (isOil) {
		PrintItemOil(item._iMiscId);
		AddItemInfoBoxString(_("Right-click to use"));
	} else if (isCastOnTarget) {
		AddItemInfoBoxString(_("Right-click to read, then\nleft-click to target"));
	} else if (IsAnyOf(item._iMiscId, IMISC_BOOK, IMISC_NOTE, IMISC_SCROLL, IMISC_SCROLLT)) {
		AddItemInfoBoxString(_("Right-click to read"));
	}
}

void printItemMiscGenericGamepad(const Item &item, const bool isOil, bool isCastOnTarget)
{
	if (item._iMiscId == IMISC_MAPOFDOOM) {
		AddItemInfoBoxString(_("Activate to view"));
	} else if (isOil) {
		PrintItemOil(item._iMiscId);
		if (!invflag) {
			AddItemInfoBoxString(_("Open inventory to use"));
		} else {
			AddItemInfoBoxString(_("Activate to use"));
		}
	} else if (isCastOnTarget) {
		AddItemInfoBoxString(_("Select from spell book, then\ncast spell to read"));
	} else if (IsAnyOf(item._iMiscId, IMISC_BOOK, IMISC_NOTE, IMISC_SCROLL, IMISC_SCROLLT)) {
		AddItemInfoBoxString(_("Activate to read"));
	}
}

void printItemMiscGamepad(const Item &item, bool isOil, bool isCastOnTarget)
{
	if (GamepadType == GamepadLayout::Generic) {
		printItemMiscGenericGamepad(item, isOil, isCastOnTarget);
		return;
	}
	const std::string_view activateButton = GetOptions().Padmapper.InputNameForAction("SecondaryAction");
	const std::string_view castButton = GetOptions().Padmapper.InputNameForAction("SpellAction");

	if (item._iMiscId == IMISC_MAPOFDOOM) {
		AddItemInfoBoxString(FormatRuntime(_("{} to view"), activateButton));
	} else if (isOil) {
		PrintItemOil(item._iMiscId);
		if (!invflag) {
			AddItemInfoBoxString(_("Open inventory to use"));
		} else {
			AddItemInfoBoxString(FormatRuntime(_("{} to use"), activateButton));
		}
	} else if (isCastOnTarget) {
		AddItemInfoBoxString(FormatRuntime(_("Select from spell book,\nthen {} to read"), castButton));
	} else if (IsAnyOf(item._iMiscId, IMISC_BOOK, IMISC_NOTE, IMISC_SCROLL, IMISC_SCROLLT)) {
		AddItemInfoBoxString(FormatRuntime(_("{} to read"), activateButton));
	}
}

void PrintItemMisc(const Item &item)
{
	if (const SupportGem gem = SupportGemForItem(item); gem != SupportGem::None) {
		AddItemInfoBoxString(SupportGemDescription(gem, SupportGemItemTier(item)));
		AddItemInfoBoxString(FormatRuntime(_("{:s} support gem"), GemColorName(SupportGemColor(gem))));
		AddItemInfoBoxString(_("Use to socket in a skill (K: skills)"));
		return;
	}
	if (IsCraftingVariantItem(item)) {
		for (const std::string &line : CraftingItemText(item))
			AddItemInfoBoxString(line);
		if (item._iMiscId != IMISC_OMEN)
			AddItemInfoBoxString(_("Right-click to use, then click an item"));
		if (IsStackable(item))
			AddItemInfoBoxString(FormatRuntime(_("Stack Size: {:d}/{:d}, shift-click to split"), StackCount(item), CurrencyStackMax));
		return;
	}
	if (IsJewel(item)) {
		AddItemInfoBoxString(_("Place in an allocated Jewel Socket"));
		AddItemInfoBoxString(_("on the Passive Tree (P)"));
		AddItemInfoBoxString(_("Right-click to socket"));
		return;
	}
	if (IsWaystone(item)) {
		for (const std::string &line : WaystoneText(item))
			AddItemInfoBoxString(line);
		return;
	}
	if (IsEndgameItem(item)) {
		for (const std::string &line : EndgameItemText(item))
			AddItemInfoBoxString(line);
		return;
	}
	if (const CharmEffect charm = GetCharmEffect(item); charm != CharmEffect::None) {
		AddItemInfoBoxString(CharmDescription(charm));
		AddItemInfoBoxString(FormatRuntime(_("Charges: {:d}/{:d}, {:d} per use"), item._iCharges, MaxCharmCharges, CharmChargesPerUseFor(item)));
		AddItemInfoBoxString(_("Works on its own from the belt; refills from kills and in town"));
		for (const std::string &line : BeltUniqueText(item))
			AddItemInfoBoxString(line);
		return;
	}
	if (const int book = SpecBookIndex(item); book >= 0) {
		AddItemInfoBoxString(FormatRuntime(_("Grants {:d} Passive Points and {:d} Weapon Set Passive Points"), PassivePointsPerBook, WeaponSetPointsPerBook));
		CurrentInfoLineStyle = InfoLineStyle::Note;
		AddItemInfoBoxString(SpecBookSource(book));
		CurrentInfoLineStyle = InfoLineStyle::Requirement;
		AddItemInfoBoxString(_("Right-click to read the book and learn its art"));
		CurrentInfoLineStyle = InfoLineStyle::Default;
		if (MyPlayer != nullptr && (MyPlayer->poe2.specBooks & (1 << book)) != 0)
			AddItemInfoBoxString(_("You have already read this book"));
		return;
	}
	if (const int skull = SpiritSkullIndex(item); skull >= 0) {
		AddItemInfoBoxString(FormatRuntime(_("Grants +{:d} to Maximum Spirit"), SpiritSkullSpirit[skull]));
		AddItemInfoBoxString(_("Torn from a fallen lord of Hell's servants..."));
		AddItemInfoBoxString(_("Use to break the skull and release the energies within"));
		if (MyPlayer != nullptr && (MyPlayer->poe2.spiritSkulls & (1 << skull)) != 0)
			AddItemInfoBoxString(_("You have already broken this skull"));
		return;
	}
	if (IsUncutGem(item)) {
		// Short lines: the info box doesn't wrap, so long ones ran into each other.
		AddItemInfoBoxString(FormatRuntime(_("Level {:d}"), UncutGemLevel(item)));
		AddItemInfoBoxString(FormatRuntime(_("Cuts a {:s} gem of its tier"), UncutKindName(UncutGemKind(item))));
		// The gem it cuts can't be above the hero's own gem level limit: say so, instead of a lower gem appearing.
		if (MyPlayer != nullptr && UncutGemKind(item) != UncutKind::Support) {
			const int cuts = GemLevelFromUncut(item, MyPlayer->getCharacterLevel());
			if (cuts < UncutGemLevel(item))
				AddItemInfoBoxString(FormatRuntime(_("Cuts level {:d} now, {:d} at character level {:d}"), cuts, UncutGemLevel(item), CharacterLevelForGemLevel(UncutGemLevel(item))));
		}
		if (UncutGemKind(item) != UncutKind::Support)
			AddItemInfoBoxString(_("or levels up a socketed gem"));
		AddItemInfoBoxString(_("Right-click to cut"));
		return;
	}
	if (const SkillSlot skill = SkillForGem(item); !skill.isEmpty()) {
		const int gemLevel = SkillGemItemLevel(item);
		AddItemInfoBoxString(FormatRuntime(_("Gem level {:d}, needs character level {:d}"), gemLevel, CharacterLevelForGemLevel(gemLevel)));
		// POE2 Mashup: what the gem does at this level and the next, so the curve is easy to follow.
		AddItemInfoBoxString(FormatRuntime(_("Now {:s}"), GemLevelText(skill, gemLevel)));
		if (gemLevel < MaxGemLevel)
			AddItemInfoBoxString(FormatRuntime(_("Next {:s}"), GemLevelText(skill, gemLevel + 1)));
		if (skill.kind == SkillKind::Spell) {
			AddItemInfoBoxString(FormatRuntime(_("Grants the {:s} spell"), SkillName(skill)));
		} else if (skill.kind == SkillKind::Persistent) {
			AddItemInfoBoxString(PersistentSkillDescription(skill.persistent()));
			AddItemInfoBoxString(FormatRuntime(_("Spirit gem, reserves {:d} Spirit"), SpiritReservation(skill.persistent())));
		} else {
			AddItemInfoBoxString(WeaponSkillDescription(skill.attack()));
			if (WeaponSkillNeedsWeapon(skill.attack()))
				AddItemInfoBoxString(FormatRuntime(_("Attack skill, needs a {:s}"), WeaponSkillWeapons(skill.attack())));
			else
				AddItemInfoBoxString(FormatRuntime(_("Skill, {:d} mana"), WeaponSkillManaCost(skill.attack())));
		}
		AddItemInfoBoxString(_("Use to socket in the Skills panel"));
		return;
	}
	if (const CurrencyOrb orb = CurrencyOrbForMiscId(item._iMiscId); orb != CurrencyOrb::None) {
		AddItemInfoBoxString(CurrencyOrbDescription(orb));
		AddItemInfoBoxString(JewellerSocket(orb) ? _("Use, then click a skill in the Skills panel (K)") : _("Use, then click the item to craft"));
		if (IsStackable(item))
			AddItemInfoBoxString(FormatRuntime(_("Stack Size: {:d}/{:d}, shift-click to split"), StackCount(item), CurrencyStackMax));
		return;
	}
	if (gbIsPoe2 && IsFlaskPotion(item)) {
		AddItemInfoBoxString(FlaskDescription(item));
		for (const std::string &line : FlaskModLines(item))
			AddItemInfoBoxString(line);
		// Holding Alt shows the rest, like other items.
		if ((SDL_GetModState() & SDL_KMOD_ALT) != 0) {
			CurrentInfoLineStyle = InfoLineStyle::Note;
			for (const std::string &line : FlaskAdvancedLines(item, MyPlayer))
				AddItemInfoBoxString(line);
			CurrentInfoLineStyle = InfoLineStyle::Default;
		}
		return;
	}
	if (item._iMiscId == IMISC_EAR) {
		AddItemInfoBoxString(FormatRuntime(pgettext("player", "Level: {:d}"), item._ivalue));
		return;
	}
	if (item._iMiscId == IMISC_AURIC) {
		AddItemInfoBoxString(_("Doubles gold capacity"));
		return;
	}
	const bool isOil = (item._iMiscId >= IMISC_USEFIRST && item._iMiscId <= IMISC_USELAST)
	    || (item._iMiscId > IMISC_OILFIRST && item._iMiscId < IMISC_OILLAST)
	    || (item._iMiscId > IMISC_RUNEFIRST && item._iMiscId < IMISC_RUNELAST)
	    || item._iMiscId == IMISC_ARENAPOT;
	const bool mouseRequiresTarget = (item._iMiscId == IMISC_SCROLLT && item._iSpell != SpellID::Flash)
	    || (item._iMiscId == IMISC_SCROLL && IsAnyOf(item._iSpell, SpellID::TownPortal, SpellID::Identify));
	const bool gamepadRequiresTarget = item.isScroll() && TargetsMonster(item._iSpell);

	switch (ControlMode) {
	case ControlTypes::None:
		break;
	case ControlTypes::KeyboardAndMouse:
		printItemMiscKBM(item, isOil, mouseRequiresTarget);
		break;
	case ControlTypes::VirtualGamepad:
		printItemMiscGenericGamepad(item, isOil, gamepadRequiresTarget);
		break;
	case ControlTypes::Gamepad:
		printItemMiscGamepad(item, isOil, gamepadRequiresTarget);
		break;
	}
}

void PrintItemInfo(const Item &item)
{
	PrintItemMisc(item);
	uint8_t str = item._iMinStr;
	uint8_t dex = item._iMinDex;
	uint8_t mag = item._iMinMag;
	CurrentInfoLineStyle = InfoLineStyle::Requirement;
	if (str != 0 || mag != 0 || dex != 0) {
		std::string text = std::string(_("Required:"));
		if (str != 0)
			text.append(FormatRuntime(_(" {:d} Str"), str));
		if (mag != 0)
			text.append(FormatRuntime(_(" {:d} Mag"), mag));
		if (dex != 0)
			text.append(FormatRuntime(_(" {:d} Dex"), dex));
		AddItemInfoBoxString(text);
	}
	if (gbIsPoe2) {
		if (const int itemLevel = GetPoe2ItemLevel(item); itemLevel > 0) {
			CurrentInfoLineStyle = InfoLineStyle::Note;
			AddItemInfoBoxString(FormatRuntime(_("Item Level: {:d}"), itemLevel));
		}
		// A known unique ends with its flavour text, like POE2's.
		std::string_view uniqueName;
		if (const BeltUnique *beltUnique = GetBeltUnique(item); beltUnique != nullptr)
			uniqueName = beltUnique->name;
		else if (item._iMagical == ITEM_QUALITY_UNIQUE && item._iIdentified && item._iUid >= 0 && static_cast<size_t>(item._iUid) < UniqueItems.size())
			uniqueName = UniqueItems[item._iUid].UIName;
		if (const std::span<const std::string> lore = UniqueLore(uniqueName); !lore.empty()) {
			CurrentInfoLineStyle = InfoLineStyle::Flavour;
			for (const std::string &line : lore)
				AddItemInfoBoxString(line);
		}
	}
}

bool SmithItemOk(const Player & /*player*/, const ItemData &item)
{
	if (item.itype == ItemType::Misc)
		return false;
	if (item.itype == ItemType::Gold)
		return false;
	if (item.itype == ItemType::Staff && (!gbIsHellfire || IsValidSpell(item.iSpell)))
		return false;
	// POE2 Mashup (Curtis 2026-10-17): Griswold keeps a tab of rings and amulets too.
	if (!gbIsPoe2 && IsAnyOf(item.itype, ItemType::Ring, ItemType::Amulet))
		return false;

	return true;
}

template <bool (*Ok)(const Player &, const ItemData &), bool ConsiderDropRate = false>
_item_indexes RndVendorItem(const Player &player, int minlvl, int maxlvl)
{
	return GetItemIndexForDroppableItem(ConsiderDropRate, [&player, &minlvl, &maxlvl](const ItemData &item) {
		if (!Ok(player, item))
			return false;
		if (item.iMinMLvl < minlvl || item.iMinMLvl > maxlvl)
			return false;
		return true;
	});
}

_item_indexes RndSmithItem(const Player &player, int lvl)
{
	return RndVendorItem<SmithItemOk, true>(player, 0, lvl);
}

/** POE2 Mashup: merchants list weapons, then off-hands, armour by slot, jewellery, then the rest. */
int VendorTypeRank(const Item &item)
{
	switch (item._itype) {
	case ItemType::Sword: return item._iLoc == ILOC_TWOHAND ? 1 : 0;
	case ItemType::Axe: return item._iLoc == ILOC_TWOHAND ? 3 : 2;
	case ItemType::Mace: return item._iLoc == ILOC_TWOHAND ? 5 : 4;
	case ItemType::Staff: return 6;
	case ItemType::Bow: return 7;
	default: break;
	}
	if (const std::optional<ModSlot> slot = ItemModSlot(item)) {
		switch (*slot) {
		case ModSlot::Shield: return 10;
		case ModSlot::Focus: return 11;
		case ModSlot::Quiver: return 12;
		case ModSlot::Helmet: return 20;
		case ModSlot::BodyArmour: return 21;
		case ModSlot::Gloves: return 22;
		case ModSlot::Boots: return 23;
		case ModSlot::Belt: return 24;
		case ModSlot::Ring: return 30;
		case ModSlot::Amulet: return 31;
		default: break;
		}
	}
	switch (item._itype) {
	case ItemType::Shield: return 10;
	case ItemType::Helm: return 20;
	case ItemType::LightArmor:
	case ItemType::MediumArmor:
	case ItemType::HeavyArmor: return 21;
	case ItemType::Ring: return 30;
	case ItemType::Amulet: return 31;
	default: return 40;
	}
}

bool VendorOrderLess(const Item &a, const Item &b)
{
	if (!gbIsPoe2)
		return a.IDidx < b.IDidx;
	const int ra = VendorTypeRank(a);
	const int rb = VendorTypeRank(b);
	if (ra != rb)
		return ra < rb;
	if (a.IDidx != b.IDidx)
		return a.IDidx < b.IDidx;
	return a._iIvalue < b._iIvalue;
}

void SortVendor(std::span<Item> itemList, size_t PinnedItemCount)
{
	auto cmp = [](const Item &a, const Item &b) {
		return VendorOrderLess(a, b);
	};

	std::sort(itemList.begin() + PinnedItemCount, itemList.end(), cmp);
}

bool PremiumItemOk(const Player & /*player*/, const ItemData &item)
{
	if (item.itype == ItemType::Misc)
		return false;
	if (item.itype == ItemType::Gold)
		return false;
	if (!gbIsHellfire && item.itype == ItemType::Staff)
		return false;

	if (gbIsMultiplayer) {
		if (item.iMiscId == IMISC_OILOF)
			return false;
		if (item.itype == ItemType::Ring)
			return false;
		if (item.itype == ItemType::Amulet)
			return false;
	}

	return true;
}

_item_indexes RndPremiumItem(const Player &player, int minlvl, int maxlvl)
{
	return RndVendorItem<PremiumItemOk>(player, minlvl, maxlvl);
}

/** @brief Shop equipment gets a POE2 item level from the level it was stocked at. */
void SetVendorPoe2ItemLevel(Item &item, int level)
{
	if (gbIsPoe2 && EquipmentClassWeight(item._itype) > 0)
		SetPoe2ItemLevel(item, Poe2ItemLevel(level));
}

void SpawnOnePremium(Item &premiumItem, int plvl, const Player &player)
{
	int strength = std::max(player.GetMaximumAttributeValue(CharacterAttribute::Strength), player._pStrength);
	int dexterity = std::max(player.GetMaximumAttributeValue(CharacterAttribute::Dexterity), player._pDexterity);
	int magic = std::max(player.GetMaximumAttributeValue(CharacterAttribute::Magic), player._pMagic);
	strength += strength / 5;
	dexterity += dexterity / 5;
	magic += magic / 5;

	plvl = std::clamp(plvl, 1, 30);

	const int maxCount = 150;
	const bool unlimited = !gbIsHellfire; // TODO: This could lead to an infinite loop if a suitable item can never be generated
	for (int count = 0; unlimited || count < maxCount; count++) {
		premiumItem = {};
		premiumItem._iSeed = AdvanceRndSeed();
		SetRndSeed(premiumItem._iSeed);
		const _item_indexes itemType = RndPremiumItem(player, plvl / 4, plvl);
		GetItemAttrs(premiumItem, itemType, plvl);
		GetItemBonus(player, premiumItem, plvl / 2, plvl, true, !gbIsHellfire);

		if (!gbIsHellfire) {
			if (premiumItem._iIvalue <= MaxVendorValue) {
				break;
			}
		} else {
			int itemValue = 0;
			switch (premiumItem._itype) {
			case ItemType::LightArmor:
			case ItemType::MediumArmor:
			case ItemType::HeavyArmor: {
				const auto *const mostValuablePlayerArmor = player.GetMostValuableItem(
				    [](const Item &item) {
					    return IsAnyOf(item._itype, ItemType::LightArmor, ItemType::MediumArmor, ItemType::HeavyArmor);
				    });

				itemValue = mostValuablePlayerArmor == nullptr ? 0 : mostValuablePlayerArmor->_iIvalue;
				break;
			}
			case ItemType::Shield:
			case ItemType::Axe:
			case ItemType::Bow:
			case ItemType::Mace:
			case ItemType::Sword:
			case ItemType::Helm:
			case ItemType::Staff:
			case ItemType::Ring:
			case ItemType::Amulet: {
				const auto *const mostValuablePlayerItem = player.GetMostValuableItem(
				    [filterType = premiumItem._itype](const Item &item) { return item._itype == filterType; });

				itemValue = mostValuablePlayerItem == nullptr ? 0 : mostValuablePlayerItem->_iIvalue;
				break;
			}
			default:
				itemValue = 0;
				break;
			}
			itemValue = itemValue * 4 / 5; // avoids forced int > float > int conversion
			if (premiumItem._iIvalue <= MaxVendorValueHf
			    && premiumItem._iMinStr <= strength
			    && premiumItem._iMinMag <= magic
			    && premiumItem._iMinDex <= dexterity
			    && premiumItem._iIvalue >= itemValue) {
				break;
			}
		}
	}
	premiumItem._iCreateInfo = plvl | CF_SMITHPREMIUM;
	SetVendorPoe2ItemLevel(premiumItem, plvl);
	premiumItem._iIdentified = true;
	premiumItem._iStatFlag = player.CanUseItem(premiumItem);
}

/**
 * @brief POE2 vendors keep valuable currency, waystones, gems and charms out of their stock: those come from drops.
 * Adria still sells the common orbs (Transmute, Augment, Scouring, Bauble) and her scrolls.
 */
bool Poe2VendorWithholds(item_misc_id miscId)
{
	if (IsCurrencyMiscId(miscId))
		return OrbCurrencyBand(miscId) != CurrencyBand::Common;
	return IsAnyOf(miscId, IMISC_WAYSTONE, IMISC_UNCUT_SKILL, IMISC_UNCUT_SUPPORT, IMISC_UNCUT_SPIRIT, IMISC_SPIRIT_SKULL, IMISC_SPEC_BOOK, IMISC_CHARM_LIFE,
	    IMISC_CHARM_MANA, IMISC_CHARM_GUARD, IMISC_CHARM_FIRE, IMISC_CHARM_LIGHTNING, IMISC_CHARM_MAGIC, IMISC_CHARM_STUN, IMISC_CHARM_GOLDEN);
}

bool WitchItemOk(const Player & /*player*/, const ItemData &item)
{
	if (IsNoneOf(item.itype, ItemType::Misc, ItemType::Staff))
		return false;
	// POE2 Mashup (Curtis 2026-10-16): Adria sells staves, elixirs and Scrolls of Identify; no currency, jewels,
	// books, other scrolls or gems.
	if (gbIsPoe2) {
		if (item.itype == ItemType::Staff)
			return SkillForGemData(item).isEmpty();
		// Scrolls of Identify only as her pinned ware, which never sells out (Curtis 2026-10-16).
		return item.iMiscId >= IMISC_ELIXSTR && item.iMiscId <= IMISC_ELIXVIT;
	}
	if (item.iMiscId == IMISC_MANA)
		return false;
	if (item.iMiscId == IMISC_FULLMANA)
		return false;
	if (item.iSpell == SpellID::TownPortal)
		return false;
	if (item.iMiscId == IMISC_FULLHEAL)
		return false;
	if (item.iMiscId == IMISC_HEAL)
		return false;
	if (item.iMiscId > IMISC_OILFIRST && item.iMiscId < IMISC_OILLAST)
		return false;
	if (item.iSpell == SpellID::Resurrect && !gbIsMultiplayer)
		return false;
	if (item.iSpell == SpellID::HealOther && !gbIsMultiplayer)
		return false;

	return true;
}

_item_indexes RndWitchItem(const Player &player, int lvl)
{
	return RndVendorItem<WitchItemOk>(player, 0, lvl);
}

_item_indexes RndBoyItem(const Player &player, int lvl)
{
	return RndVendorItem<PremiumItemOk>(player, 0, lvl);
}

bool HealerItemOk(const Player &player, const ItemData &item)
{
	if (item.itype != ItemType::Misc)
		return false;

	if (item.iMiscId == IMISC_SCROLL)
		return item.iSpell == SpellID::Healing;
	if (item.iMiscId == IMISC_SCROLLT)
		return item.iSpell == SpellID::HealOther && gbIsMultiplayer;

	if (!gbIsMultiplayer) {
		if (item.iMiscId == IMISC_ELIXSTR)
			return !gbIsHellfire || player._pBaseStr < player.GetMaximumAttributeValue(CharacterAttribute::Strength);
		if (item.iMiscId == IMISC_ELIXMAG)
			return !gbIsHellfire || player._pBaseMag < player.GetMaximumAttributeValue(CharacterAttribute::Magic);
		if (item.iMiscId == IMISC_ELIXDEX)
			return !gbIsHellfire || player._pBaseDex < player.GetMaximumAttributeValue(CharacterAttribute::Dexterity);
		if (item.iMiscId == IMISC_ELIXVIT)
			return !gbIsHellfire || player._pBaseVit < player.GetMaximumAttributeValue(CharacterAttribute::Vitality);
	}

	if (item.iMiscId == IMISC_REJUV)
		return true;
	if (item.iMiscId == IMISC_FULLREJUV)
		return true;

	return false;
}

_item_indexes RndHealerItem(const Player &player, int lvl)
{
	return RndVendorItem<HealerItemOk>(player, 0, lvl);
}

void RecreateSmithItem(const Player &player, Item &item, int lvl, int iseed)
{
	SetRndSeed(iseed);
	const _item_indexes itype = RndSmithItem(player, lvl);
	GetItemAttrs(item, itype, lvl);
	// POE2 Mashup: a magic or rare ware keeps its rarity in dwBuff; skip the rarity roll and give it its modifiers.
	if (const std::optional<item_quality> rarity = GetLootRarity(item); rarity && *rarity != ITEM_QUALITY_NORMAL) {
		item._iSeed = iseed; // the modifiers roll from it
		DiscardRandomValues(1);
		GetItemBonus(player, item, lvl / 2, lvl, true, false);
	}

	item._iSeed = iseed;
	item._iCreateInfo = lvl | CF_SMITH;
	item._iIdentified = true;
}

void RecreatePremiumItem(const Player &player, Item &item, int plvl, int iseed)
{
	SetRndSeed(iseed);
	const _item_indexes itype = RndPremiumItem(player, plvl / 4, plvl);
	GetItemAttrs(item, itype, plvl);
	GetItemBonus(player, item, plvl / 2, plvl, true, !gbIsHellfire);

	item._iSeed = iseed;
	item._iCreateInfo = plvl | CF_SMITHPREMIUM;
	item._iIdentified = true;
}

void RecreateBoyItem(const Player &player, Item &item, int lvl, int iseed)
{
	SetRndSeed(iseed);
	const _item_indexes itype = RndBoyItem(player, lvl);
	GetItemAttrs(item, itype, lvl);
	GetItemBonus(player, item, lvl, 2 * lvl, true, true);

	item._iSeed = iseed;
	item._iCreateInfo = lvl | CF_BOY;
	item._iIdentified = true;
}

void RecreateWitchItem(const Player &player, Item &item, _item_indexes idx, int lvl, int iseed)
{
	if (IsAnyOf(idx, IDI_MANA, IDI_FULLMANA, IDI_PORTAL)) {
		GetItemAttrs(item, idx, lvl);
	} else if (gbIsHellfire && idx >= 114 && idx <= 117) {
		SetRndSeed(iseed);
		DiscardRandomValues(1);
		GetItemAttrs(item, idx, lvl);
	} else {
		SetRndSeed(iseed);
		const _item_indexes itype = RndWitchItem(player, lvl);
		GetItemAttrs(item, itype, lvl);
		int iblvl = -1;
		if (GenerateRnd(100) <= 5)
			iblvl = 2 * lvl;
		if (iblvl == -1 && item._iMiscId == IMISC_STAFF)
			iblvl = 2 * lvl;
		if (iblvl != -1)
			GetItemBonus(player, item, iblvl / 2, iblvl, true, true);
	}

	item._iSeed = iseed;
	item._iCreateInfo = lvl | CF_WITCH;
	item._iIdentified = true;
}

void RecreateHealerItem(const Player &player, Item &item, _item_indexes idx, int lvl, int iseed)
{
	if (IsAnyOf(idx, IDI_HEAL, IDI_FULLHEAL, IDI_RESURRECT)) {
		GetItemAttrs(item, idx, lvl);
	} else {
		SetRndSeed(iseed);
		const _item_indexes itype = RndHealerItem(player, lvl);
		GetItemAttrs(item, itype, lvl);
	}

	item._iSeed = iseed;
	item._iCreateInfo = lvl | CF_HEALER;
	item._iIdentified = true;
}

void RecreateTownItem(const Player &player, Item &item, _item_indexes idx, uint16_t icreateinfo, int iseed)
{
	if ((icreateinfo & CF_SMITH) != 0)
		RecreateSmithItem(player, item, icreateinfo & CF_LEVEL, iseed);
	else if ((icreateinfo & CF_SMITHPREMIUM) != 0)
		RecreatePremiumItem(player, item, icreateinfo & CF_LEVEL, iseed);
	else if ((icreateinfo & CF_BOY) != 0)
		RecreateBoyItem(player, item, icreateinfo & CF_LEVEL, iseed);
	else if ((icreateinfo & CF_WITCH) != 0)
		RecreateWitchItem(player, item, idx, icreateinfo & CF_LEVEL, iseed);
	else if ((icreateinfo & CF_HEALER) != 0)
		RecreateHealerItem(player, item, idx, icreateinfo & CF_LEVEL, iseed);
}

void CreateMagicItem(Point position, int lvl, ItemType itemType, int imid, int icurs, bool sendmsg, bool delta, bool spawn = false)
{
	if (ActiveItemCount >= MAXITEMS)
		return;

	const int ii = AllocateItem();
	auto &item = Items[ii];
	_item_indexes idx = RndTypeItems(itemType, imid, lvl);

	while (true) {
		item = {};
		SetupAllItems(*MyPlayer, item, idx, AdvanceRndSeed(), 2 * lvl, 1, true, delta);
		TryRandomUniqueItem(item, idx, 2 * lvl, 1, true, delta);
		SetupItem(item);
		if (item._iCurs == icurs)
			break;

		idx = RndTypeItems(itemType, imid, lvl);
	}
	GetSuperItemSpace(position, ii);

	if (sendmsg)
		NetSendCmdPItem(false, CMD_DROPITEM, item.position, item);
	if (delta)
		DeltaAddItem(ii);
	if (spawn)
		NetSendCmdPItem(false, CMD_SPAWNITEM, item.position, item);
}

void NextItemRecord(int i)
{
	gnNumGetRecords--;

	if (gnNumGetRecords == 0) {
		return;
	}

	itemrecord[i].dwTimestamp = itemrecord[gnNumGetRecords].dwTimestamp;
	itemrecord[i].nSeed = itemrecord[gnNumGetRecords].nSeed;
	itemrecord[i].wCI = itemrecord[gnNumGetRecords].wCI;
	itemrecord[i].nIndex = itemrecord[gnNumGetRecords].nIndex;
}

StringOrView GetTranslatedItemName(const Item &item)
{
	const auto &baseItemData = AllItemsList[static_cast<size_t>(item.IDidx)];

	if (item._iCreateInfo == 0) {
		return _(baseItemData.iName);
	}
	if (item._iMiscId == IMISC_BOOK) {
		const std::string_view spellName = pgettext("spell", GetSpellData(item._iSpell).sNameText);
		return FormatRuntime(_(/* TRANSLATORS: {:s} will be a spell name */ "Book of {:s}"), spellName);
	}
	if (item._iMiscId == IMISC_EAR) {
		return FormatRuntime(_(/* TRANSLATORS: {:s} will be a Character Name */ "Ear of {:s}"), item._iIName);
	}
	if (item._iMiscId > IMISC_OILFIRST && item._iMiscId < IMISC_OILLAST) {
		for (size_t i = 0; i < 10; i++) {
			if (OilMagic[i] != item._iMiscId)
				continue;
			return _(OilNames[i]);
		}
		app_fatal("unknown oil");
	} else if (item._itype == ItemType::Staff && item._iSpell != SpellID::Null && item._iMagical != ITEM_QUALITY_UNIQUE) {
		return GenerateStaffName(baseItemData, item._iSpell, true);
	} else {
		return _(baseItemData.iName);
	}
}

std::string GetTranslatedItemNameMagical(const Item &item, bool hellfireItem, bool translate, std::optional<bool> forceNameLengthCheck)
{
	std::string identifiedName;
	const auto &baseItemData = AllItemsList[static_cast<size_t>(item.IDidx)];

	const int lvl = item._iCreateInfo & CF_LEVEL;
	const bool onlygood = (item._iCreateInfo & (CF_ONLYGOOD | CF_SMITHPREMIUM | CF_BOY | CF_WITCH)) != 0;

	const uint32_t currentSeed = GetLCGEngineState();
	SetRndSeed(item._iSeed);

	int minlvl;
	int maxlvl;
	if ((item._iCreateInfo & CF_SMITHPREMIUM) != 0) {
		DiscardRandomValues(2); // RndVendorItem and GetItemAttrs
		minlvl = lvl / 2;
		maxlvl = lvl;
	} else if ((item._iCreateInfo & CF_BOY) != 0) {
		DiscardRandomValues(2); // RndVendorItem and GetItemAttrs
		minlvl = lvl;
		maxlvl = lvl * 2;
	} else if ((item._iCreateInfo & CF_WITCH) != 0) {
		DiscardRandomValues(2); // RndVendorItem and GetItemAttrs
		int iblvl = -1;
		if (GenerateRnd(100) <= 5)
			iblvl = 2 * lvl;
		if (iblvl == -1 && item._iMiscId == IMISC_STAFF)
			iblvl = 2 * lvl;
		minlvl = iblvl / 2;
		maxlvl = iblvl;
	} else {
		DiscardRandomValues(1); // GetItemAttrs
		const int iblvl = GetItemBLevel(lvl, item._iMiscId, onlygood, (item._iCreateInfo & CF_UPER15) != 0);
		minlvl = iblvl / 2;
		maxlvl = iblvl;
		DiscardRandomValues(1); // CheckUnique
	}

	minlvl = std::min(minlvl, 25);

	AffixItemType affixItemType = AffixItemType::None;

	switch (item._itype) {
	case ItemType::Sword:
	case ItemType::Axe:
	case ItemType::Mace:
		affixItemType = AffixItemType::Weapon;
		break;
	case ItemType::Bow:
		affixItemType = AffixItemType::Bow;
		break;
	case ItemType::Shield:
		affixItemType = AffixItemType::Shield;
		break;
	case ItemType::LightArmor:
	case ItemType::Helm:
	case ItemType::MediumArmor:
	case ItemType::HeavyArmor:
		affixItemType = AffixItemType::Armor;
		break;
	case ItemType::Staff: {
		const bool allowspells = !hellfireItem || ((item._iCreateInfo & CF_SMITHPREMIUM) == 0);

		if (!allowspells)
			affixItemType = AffixItemType::Staff;
		else if (!hellfireItem && FlipCoin(4)) {
			affixItemType = AffixItemType::Staff;
			minlvl = maxlvl / 2;
		} else {
			DiscardRandomValues(2); // Spell and Charges

			std::optional<const PLStruct *> prefix = GetStaffPrefix(maxlvl, onlygood);
			if (!prefix.has_value() || item._iSpell == SpellID::Null) {
				if (forceNameLengthCheck) {
					// We generate names to check if it's a diablo or hellfire item. This checks fails => invalid item => don't generate a item name
					identifiedName.clear();
				} else {
					// This can happen, if the item is hacked or a bug in the logic exists
					LogWarn("GetTranslatedItemNameMagical failed for item '{}' with prefix '{}' and spellid '{}'", item._iIName, prefix.has_value() ? (*prefix)->PLName : "NULL", static_cast<std::underlying_type_t<SpellID>>(item._iSpell));
					identifiedName = item._iIName;
				}
			} else {
				identifiedName = GenerateStaffNameMagical(baseItemData, item._iSpell, **prefix, translate, forceNameLengthCheck);
			}
		}
		break;
	}
	case ItemType::Ring:
	case ItemType::Amulet:
		affixItemType = AffixItemType::Misc;
		break;
	case ItemType::None:
	case ItemType::Misc:
	case ItemType::Gold:
		break;
	}

	if (affixItemType != AffixItemType::None) {
		if ((item.dwBuff & CF_POE2) != 0)
			DiscardRandomValues(1); // Rare roll in GetItemPower
		const PLStruct *pPrefix = nullptr;
		const PLStruct *pSufix = nullptr;
		GetItemPowerPrefixAndSuffix(
		    minlvl, maxlvl, affixItemType, onlygood,
		    [&pPrefix](const PLStruct &prefix) {
			    pPrefix = &prefix;
			    // GenerateRnd(prefix.power.param2 - prefix.power.param2 + 1)
			    DiscardRandomValues(1);
			    switch (pPrefix->power.type) {
			    case IPL_DOPPELGANGER:
			    case IPL_TOHIT_DAMP:
				    DiscardRandomValues(2);
				    break;
			    case IPL_TOHIT_DAMP_CURSE:
				    DiscardRandomValues(1);
				    break;
			    default:
				    break;
			    }
		    },
		    [&pSufix](const PLStruct &suffix) {
			    pSufix = &suffix;
		    });

		identifiedName = GenerateMagicItemName(_(baseItemData.iName), pPrefix, pSufix, translate);
		if (forceNameLengthCheck ? *forceNameLengthCheck : !StringInPanel(identifiedName.c_str())) {
			identifiedName = GenerateMagicItemName(_(baseItemData.iSName), pPrefix, pSufix, translate);
		}
	}

	SetRndSeed(currentSeed);
	return identifiedName;
}

} // namespace

std::string GetRareItemName(const Item &item)
{
	// clang-format off
	static constexpr std::array<std::string_view, 48> FirstWords = {
		"Agony", "Apocalypse", "Armageddon", "Beast", "Behemoth", "Blight", "Blood", "Bramble",
		"Brimstone", "Brood", "Carrion", "Cataclysm", "Corpse", "Damnation", "Death", "Demon",
		"Dire", "Doom", "Dread", "Eagle", "Empyrean", "Fate", "Foe", "Gale",
		"Ghoul", "Gloom", "Golem", "Grim", "Hate", "Havoc", "Horror", "Kraken",
		"Maelstrom", "Morbid", "Oblivion", "Onslaught", "Pain", "Phoenix", "Plague", "Rage",
		"Rune", "Skull", "Sorrow", "Storm", "Tempest", "Torment", "Vengeance", "Wrath",
	};
	// clang-format on
	static constexpr std::array<std::string_view, 12> WeaponWords = { "Bane", "Bite", "Edge", "Fang", "Gnash", "Hunger", "Razor", "Scratch", "Sever", "Slayer", "Thirst", "Wound" };
	static constexpr std::array<std::string_view, 12> BowWords = { "Arch", "Branch", "Fletch", "Mark", "Nock", "Rain", "Reach", "Siege", "Stinger", "Strike", "Volley", "Wing" };
	static constexpr std::array<std::string_view, 12> StaffWords = { "Beam", "Bough", "Call", "Chant", "Cry", "Gnarl", "Mast", "Pillar", "Pole", "Spire", "Weaver", "Branch" };
	static constexpr std::array<std::string_view, 12> ArmorWords = { "Carapace", "Cloak", "Coat", "Hide", "Jack", "Keep", "Pelt", "Shell", "Shelter", "Suit", "Veil", "Ward" };
	static constexpr std::array<std::string_view, 12> ShieldWords = { "Aegis", "Badge", "Barrier", "Bastion", "Bulwark", "Duty", "Emblem", "Fend", "Guard", "Rock", "Wall", "Watch" };
	static constexpr std::array<std::string_view, 12> HelmWords = { "Brow", "Corona", "Cowl", "Crest", "Crown", "Dome", "Glance", "Halo", "Horn", "Peak", "Salvation", "Visor" };
	static constexpr std::array<std::string_view, 12> RingWords = { "Band", "Circle", "Coil", "Eye", "Finger", "Grasp", "Grip", "Gyre", "Hold", "Knot", "Loop", "Turn" };
	static constexpr std::array<std::string_view, 12> AmuletWords = { "Beads", "Charm", "Gorget", "Heart", "Locket", "Medallion", "Noose", "Pendant", "Rosary", "Scarab", "Talisman", "Torc" };

	const std::array<std::string_view, 12> *secondWords;
	switch (item._itype) {
	case ItemType::Bow: secondWords = &BowWords; break;
	case ItemType::Staff: secondWords = &StaffWords; break;
	case ItemType::LightArmor:
	case ItemType::MediumArmor:
	case ItemType::HeavyArmor: secondWords = &ArmorWords; break;
	case ItemType::Shield: secondWords = &ShieldWords; break;
	case ItemType::Helm: secondWords = &HelmWords; break;
	case ItemType::Ring: secondWords = &RingWords; break;
	case ItemType::Amulet: secondWords = &AmuletWords; break;
	default: secondWords = &WeaponWords; break;
	}

	// Mix the seed so that items with nearby seeds get unrelated names.
	uint32_t hash = item._iSeed;
	hash ^= hash >> 16;
	hash *= 0x7FEB352DU;
	hash ^= hash >> 15;
	hash *= 0x846CA68BU;
	hash ^= hash >> 16;

	const std::string_view first = FirstWords[hash % FirstWords.size()];
	const std::string_view second = (*secondWords)[(hash / FirstWords.size()) % secondWords->size()];
	return StrCat(first, " ", second);
}

void RestoreRareItemPowers(Item &item)
{
	// Normal items too when their base has implicits: their POE2 stats aren't saved either.
	if ((item._iMagical == ITEM_QUALITY_NORMAL && ItemBaseMeta(item) == nullptr) || Players.empty())
		return;
	const uint32_t currentSeed = GetLCGEngineState();
	Item regenerated;
	RecreateItem(Players[0], regenerated, item.IDidx, item._iCreateInfo, item._iSeed, item._ivalue, item.dwBuff, item._iPoe2Craft);
	item._iRarePowers = regenerated._iRarePowers;
	item._iRareTiers = regenerated._iRareTiers;
	item._iPreTier = regenerated._iPreTier;
	item._iSufTier = regenerated._iSufTier;
	item._iPLLifeRegen = regenerated._iPLLifeRegen;
	item._iPLManaRegen = regenerated._iPLManaRegen;
	item._iPoe2Mods = regenerated._iPoe2Mods;
	item._iPoe2Stats = regenerated._iPoe2Stats;
	SetRndSeed(currentSeed);
}

bool CreatePoe2Unique(const Player &player, Item &item, int uid, uint32_t seed)
{
	const Poe2Unique *unique = Poe2UniqueFor(uid);
	if (unique == nullptr || !IsItemAvailable(unique->baseItem))
		return false;
	const std::vector<int> onBase = Poe2UniquesOnBase(unique->baseItem);
	const int offset = static_cast<int>(std::find(onBase.begin(), onBase.end(), uid) - onBase.begin());
	item = {};
	SetLootRarity(item, ITEM_QUALITY_UNIQUE);
	SetPoe2ItemLevel(item, unique->dropLevel);
	item.dwBuff |= ((static_cast<uint32_t>(offset) << 1) & CF_UIDOFFSET) | CF_POE2_BUNDLE_UNIQUE;
	SetupAllItems(player, item, static_cast<_item_indexes>(unique->baseItem), seed, std::max<int>(UniqueItems[uid].UIMinLvl, 1), 1, true, false, offset);
	return item._iMagical == ITEM_QUALITY_UNIQUE && item._iUid == uid;
}

ChanceResult CreateChanceUnique(const Player &player, const Item &item, Item &unique, uint32_t roll)
{
	struct Candidate {
		int uid;
		int offset;
		bool bundle;
	};
	std::array<std::vector<Candidate>, static_cast<size_t>(UniqueTier::Count)> byTier;
	const int base = item.IDidx;
	const std::vector<int> bundleUniques = Poe2UniquesOnBase(base);
	for (size_t i = 0; i < bundleUniques.size(); i++) {
		const Poe2Unique *poe2Unique = Poe2UniqueFor(bundleUniques[i]);
		if (poe2Unique != nullptr && poe2Unique->isGlobal() && i <= (CF_UIDOFFSET >> 1))
			byTier[static_cast<size_t>(poe2Unique->tier)].push_back({ bundleUniques[i], static_cast<int>(i), true });
	}
	// Diablo's uniques of the base, up to the higher of the item's and the hero's level.
	const int level = std::clamp(std::max<int>(item._iCreateInfo & CF_LEVEL, player.getCharacterLevel()), 1, static_cast<int>(CF_LEVEL));
	const std::vector<uint8_t> valid = GetValidUniques(level, AllItemsList[base].iItemId);
	for (size_t i = 0; i < valid.size(); i++) {
		const int offset = static_cast<int>(valid.size() - 1 - i);
		if (offset <= static_cast<int>(CF_UIDOFFSET >> 1) && Poe2UniqueFor(valid[i]) == nullptr)
			byTier[static_cast<size_t>(LegacyUniqueTier(UniqueItems[valid[i]].UIMinLvl))].push_back({ valid[i], offset, false });
	}
	if (std::all_of(byTier.begin(), byTier.end(), [](const std::vector<Candidate> &pool) { return pool.empty(); }))
		return ChanceResult::NoUniques;

	const std::array<int, static_cast<size_t>(UniqueTier::Count)> weights = UniqueTierWeights(LootSource::Normal);
	int total = 0;
	for (const int weight : weights)
		total += weight;
	int pick = static_cast<int>(roll % static_cast<uint32_t>(total));
	size_t tier = 0;
	while (tier + 1 < weights.size() && pick >= weights[tier]) {
		pick -= weights[tier];
		tier++;
	}
	const std::vector<Candidate> &pool = byTier[tier];
	if (pool.empty())
		return ChanceResult::Failed;
	const Candidate &chosen = pool[(roll >> 16) % pool.size()];
	const uint32_t seed = (roll * 1103515245U + 12345U) >> 1;
	const uint32_t rngState = GetLCGEngineState();
	bool made;
	if (chosen.bundle) {
		made = CreatePoe2Unique(player, unique, chosen.uid, seed);
	} else {
		unique = {};
		SetLootRarity(unique, ITEM_QUALITY_UNIQUE);
		SetPoe2ItemLevel(unique, GetPoe2ItemLevel(item));
		unique.dwBuff |= (static_cast<uint32_t>(chosen.offset) << 1) & CF_UIDOFFSET;
		SetupAllItems(player, unique, static_cast<_item_indexes>(base), seed, level, 1, true, false, chosen.offset);
		made = unique._iMagical == ITEM_QUALITY_UNIQUE && unique._iUid == chosen.uid;
	}
	SetRndSeed(rngState);
	if (!made)
		return ChanceResult::Failed;
	SetupItem(unique);
	return ChanceResult::Made;
}

bool IsItemAvailable(int i)
{
	if (i < 0 || i >= static_cast<int>(AllItemsList.size()))
		return false;

	if (gbIsSpawn) {
		if (i >= 62 && i <= 70)
			return false; // Medium and heavy armors
		if (IsAnyOf(i, 105, 107, 108, 110, 111, 113))
			return false; // Unavailable scrolls
	}

	if (gbIsHellfire)
		return true;

	return (
	           i != IDI_MAPOFDOOM                   // Cathedral Map
	           && i != IDI_LGTFORGE                 // Bovine Plate
	           && (i < IDI_OIL || i > IDI_GREYSUIT) // Hellfire exclusive items
	           && (i < 83 || i > 86)                // Oils
	           && i != 92                           // Scroll of Search
	           && (i < 161 || i > 165)              // Runes
	           && i != IDI_SORCERER                 // Short Staff of Mana
	           )
	    || (
	        // Bard items are technically Hellfire-exclusive
	        // but are just normal items with adjusted stats.
	        *GetOptions().Gameplay.testBard && IsAnyOf(i, IDI_BARDSWORD, IDI_BARDDAGGER));
}

uint8_t GetOutlineColor(const Item &item, bool checkReq)
{
	if (checkReq && !item._iStatFlag)
		return ICOL_RED;
	if (item._itype == ItemType::Gold)
		return ICOL_YELLOW;
	if (item._iMagical == ITEM_QUALITY_MAGIC)
		return ICOL_BLUE;
	if (item._iMagical == ITEM_QUALITY_UNIQUE || item._iMagical == ITEM_QUALITY_RARE)
		return ICOL_YELLOW;

	return ICOL_WHITE;
}

void ClearUniqueItemFlags()
{
	memset(UniqueItemFlags, 0, sizeof(UniqueItemFlags));
}

void InitItemGFX()
{
	char arglist[64];

	const int itemTypes = gbIsHellfire ? ITEMTYPES : 35;
	for (int i = 0; i < itemTypes; i++) {
		*BufCopy(arglist, "items\\", ItemDropNames[i]) = '\0';
		itemanims[i] = LoadCel(arglist, ItemAnimWidth);
	}
}

void InitItems()
{
	ActiveItemCount = 0;
	memset(dItem, 0, sizeof(dItem));

	for (auto &item : Items) {
		item.clear();
		item.position = { 0, 0 };
		item._iAnimFlag = false;
		item.selectionRegion = SelectionRegion::None;
		item._iIdentified = false;
		item._iPostDraw = false;
	}

	for (uint8_t i = 0; i < MAXITEMS; i++) {
		ActiveItems[i] = i;
	}

	if (!setlevel) {
		DiscardRandomValues(1);
		if (Quests[Q_ROCK].IsAvailable())
			SpawnRock();
		if (Quests[Q_ANVIL].IsAvailable())
			SpawnQuestItem(IDI_ANVIL, SetPiece.position.megaToWorld() + Displacement { 11, 11 }, 0, SelectionRegion::Bottom, false);
		if (sgGameInitInfo.bCowQuest != 0 && currlevel == 20)
			SpawnQuestItem(IDI_BROWNSUIT, { 25, 25 }, 3, SelectionRegion::Bottom, false);
		if (sgGameInitInfo.bCowQuest != 0 && currlevel == 19)
			SpawnQuestItem(IDI_GREYSUIT, { 25, 25 }, 3, SelectionRegion::Bottom, false);
		// In multiplayer items spawn during level generation to avoid desyncs
		if (gbIsMultiplayer) {
			if (Quests[Q_MUSHROOM].IsAvailable())
				SpawnQuestItem(IDI_FUNGALTM, { 0, 0 }, 5, SelectionRegion::Bottom, false);
			if (currlevel == Quests[Q_VEIL]._qlevel + 1 && Quests[Q_VEIL]._qactive != QUEST_NOTAVAIL)
				SpawnQuestItem(IDI_GLDNELIX, { 0, 0 }, 5, SelectionRegion::Bottom, false);
		}
		if (currlevel > 0 && currlevel < 16)
			AddInitItems();
		if (currlevel >= 21 && currlevel <= 23)
			SpawnNote();
	}

	ShowUniqueItemInfoBox = false;

	initItemGetRecords();
}

int GetBonusAC(const Item &item)
{
	if (item._iPLAC != 0) {
		int tempAc = item._iAC;
		tempAc *= item._iPLAC;
		tempAc /= 100;
		if (tempAc == 0)
			tempAc = math::Sign(item._iPLAC);
		return tempAc;
	}

	return 0;
}

void CalcPlrDamage(Player &player, int minDamage, int maxDamage)
{
	const uint8_t playerLevel = player.getCharacterLevel();

	if (minDamage == 0 && maxDamage == 0) {
		minDamage = 1;
		maxDamage = 1;

		if (player.isHoldingItem(ItemType::Shield)) {
			maxDamage = 3;
		}

		if (player._pClass == HeroClass::Monk) {
			minDamage = std::max(minDamage, playerLevel / 2);
			maxDamage = std::max<int>(maxDamage, playerLevel);
		}
	}

	player._pIMinDam = minDamage;
	player._pIMaxDam = maxDamage;
}

void CalcPlrPrimaryStats(Player &player, int strength, int &magic, int dexterity, int &vitality)
{
	const uint8_t playerLevel = player.getCharacterLevel();

	if (HasAnyOf(player._pSpellFlags, SpellFlag::RageActive)) {
		strength += 2 * playerLevel;
		dexterity += playerLevel + playerLevel / 2;
		vitality += 2 * playerLevel;
	}
	if (HasAnyOf(player._pSpellFlags, SpellFlag::RageCooldown)) {
		strength -= 2 * playerLevel;
		dexterity -= playerLevel + playerLevel / 2;
		vitality -= 2 * playerLevel;
	}

	player._pStrength = std::clamp(strength + player._pBaseStr, 0, 750);
	player._pMagic = std::clamp(magic + player._pBaseMag, 0, 750);
	player._pDexterity = std::clamp(dexterity + player._pBaseDex, 0, 750);
	player._pVitality = std::clamp(vitality + player._pBaseVit, 0, 750);
}

void CalcPlrLightRadius(Player &player, int lrad)
{
	lrad = std::clamp(lrad, 2, 15);

	if (player._pLightRad != lrad) {
		if (player.isOnActiveLevel()) {
			ChangeLightRadius(player.lightId, lrad);
			ChangeVisionRadius(player.getId(), lrad);
		}
		player._pLightRad = lrad;
	}
}

void CalcPlrDamageMod(Player &player)
{
	const uint8_t playerLevel = player.getCharacterLevel();
	const Item &leftHandItem = player.InvBody[INVLOC_HAND_LEFT];
	const Item &rightHandItem = player.InvBody[INVLOC_HAND_RIGHT];
	const int strMod = playerLevel * player._pStrength;
	const int strDexMod = playerLevel * (player._pStrength + player._pDexterity);

	switch (player._pClass) {
	case HeroClass::Rogue:
		player._pDamageMod = strDexMod / 200;
		break;
	case HeroClass::Monk:
		// POE2 Mashup: The Sisters' Gift lets the bow take the staff's bonus.
		if (player.isHoldingItem(ItemType::Staff) || (leftHandItem.isEmpty() && rightHandItem.isEmpty())
		    || (gbIsPoe2 && player.isHoldingItem(ItemType::Bow) && PassiveTotal(player, PassiveStat::StaffAppliesToBow) > 0)) {
			player._pDamageMod = strDexMod / 150;
		} else {
			player._pDamageMod = strDexMod / 300;
		}
		break;
	case HeroClass::Bard:
		if (player.isHoldingItem(ItemType::Sword)) {
			player._pDamageMod = strDexMod / 150;
		} else if (player.isHoldingItem(ItemType::Bow)) {
			player._pDamageMod = strDexMod / 250;
		} else {
			player._pDamageMod = strMod / 100;
		}
		break;
	case HeroClass::Barbarian:
		if (player.isHoldingItem(ItemType::Axe) || player.isHoldingItem(ItemType::Mace)) {
			player._pDamageMod = strMod / 75;
		} else if (player.isHoldingItem(ItemType::Bow)) {
			player._pDamageMod = strMod / 300;
		} else {
			player._pDamageMod = strMod / 100;
		}
		if (player.isHoldingItem(ItemType::Shield)) {
			if (leftHandItem._itype == ItemType::Shield)
				player._pIAC -= leftHandItem._iAC / 2;
			else if (rightHandItem._itype == ItemType::Shield)
				player._pIAC -= rightHandItem._iAC / 2;
		} else if (!player.isHoldingItem(ItemType::Staff) && !player.isHoldingItem(ItemType::Bow)) {
			player._pDamageMod += playerLevel * player._pVitality / 100;
		}
		break;
	default:
		player._pDamageMod = strMod / 100;
		break;
	}

	const ClassAttributes &classAttributes = GetClassAttributes(player._pClass);
	if (HasAnyOf(classAttributes.classFlags, PlayerClassFlag::IronSkin)) {
		player._pIAC += playerLevel / 4;
	}
}

void CalcPlrResistances(Player &player, ItemSpecialEffect iflgs, int fire, int lightning, int magic)
{
	const uint8_t playerLevel = player.getCharacterLevel();

	const ClassAttributes &classAttributes = GetClassAttributes(player._pClass);

	if (HasAnyOf(classAttributes.classFlags, PlayerClassFlag::NaturalResistance)) {
		magic += playerLevel;
		fire += playerLevel;
		lightning += playerLevel;
	}

	if (HasAnyOf(player._pSpellFlags, SpellFlag::RageCooldown)) {
		magic -= playerLevel;
		fire -= playerLevel;
		lightning -= playerLevel;
	}

	if (HasAnyOf(iflgs, ItemSpecialEffect::ZeroResistance)) {
		// reset resistances to zero if the respective special effect is active
		magic = 0;
		fire = 0;
		lightning = 0;
	}

	if (gbIsPoe2) {
		// POE2 Mashup: resistances drop as you go deeper and can go negative.
		const int penalty = ResistancePenalty(player);
		Poe2PlayerData &data = player.poe2;
		const int maxElemental = MaxResistance + data.rule(UniqueRule::MaxElementalResistance);
		data.maxResistances = { static_cast<int16_t>(maxElemental), static_cast<int16_t>(maxElemental), static_cast<int16_t>(maxElemental + data.rule(UniqueRule::MaxMagicResistance)) };
		std::array<int, 3> uncapped { fire - penalty, lightning - penalty, magic - penalty };
		if (const int share = data.rule(UniqueRule::ResistanceShare); share > 0) {
			// The highest resistance that isn't over its cap also counts towards the lowest.
			std::optional<size_t> highest;
			size_t lowest = 0;
			for (size_t i = 0; i < uncapped.size(); i++) {
				if (uncapped[i] <= data.maxResistances[i] && (!highest || uncapped[i] > uncapped[*highest]))
					highest = i;
				if (uncapped[i] < uncapped[lowest])
					lowest = i;
			}
			if (highest && *highest != lowest)
				uncapped[lowest] += std::max(uncapped[*highest], 0) * share / 100;
		}
		for (size_t i = 0; i < uncapped.size(); i++)
			data.uncappedResistances[i] = static_cast<int16_t>(std::clamp(uncapped[i], -1000, 1000));
		player._pFireResist = Poe2Resistance(uncapped[0], 0, data.maxResistances[0]);
		player._pLghtResist = Poe2Resistance(uncapped[1], 0, data.maxResistances[1]);
		player._pMagResist = Poe2Resistance(uncapped[2], 0, data.maxResistances[2]);
		return;
	}
	player._pMagResist = std::clamp(magic, 0, MaxResistance);
	player._pFireResist = std::clamp(fire, 0, MaxResistance);
	player._pLghtResist = std::clamp(lightning, 0, MaxResistance);
}

void CalcPlrLifeMana(Player &player, int vitality, int magic, Fixed26_6 life, Fixed26_6 mana, int lessLifePercent = 0)
{
	const ClassAttributes &playerClassAttributes = player.getClassAttributes();
	vitality = (playerClassAttributes.itmLife * vitality).whole();
	life += Fixed26_6::fromInt(vitality);
	// POE2 Mashup: "less maximum life" takes from the bonus, so current life drops by the same amount.
	if (lessLifePercent > 0)
		life -= Fixed26_6::fromRaw(static_cast<int32_t>(static_cast<int64_t>((life + player._pMaxHPBase).raw()) * std::min(lessLifePercent, 90) / 100));

	magic = (playerClassAttributes.itmMana * magic).whole();
	mana += Fixed26_6::fromInt(magic);

	player._pMaxHP = std::clamp(life + player._pMaxHPBase, Fixed26_6::fromInt(1), Fixed26_6::fromInt(2000));
	player._pHitPoints = std::min(life + player._pHPBase, player._pMaxHP);

	if (&player == MyPlayer && player.hasNoLife()) {
		SetPlayerHitPoints(player, Fixed26_6::fromInt(0));
	}

	player._pMaxMana = std::clamp(mana + player._pMaxManaBase, Fixed26_6::fromInt(0), Fixed26_6::fromInt(2000));
	player._pMana = std::min(mana + player._pManaBase, player._pMaxMana);
}

void CalcPlrBlockFlag(Player &player)
{
	const auto &leftHandItem = player.InvBody[INVLOC_HAND_LEFT];
	const auto &rightHandItem = player.InvBody[INVLOC_HAND_RIGHT];

	player._pBlockFlag = false;

	if (player._pClass == HeroClass::Monk) {
		if (player.isHoldingItem(ItemType::Staff)) {
			player._pBlockFlag = true;
			player._pIFlags |= ItemSpecialEffect::FastBlock;
		} else if ((leftHandItem.isEmpty() && rightHandItem.isEmpty()) || (leftHandItem._iClass == ICLASS_WEAPON && leftHandItem._iLoc != ILOC_TWOHAND && rightHandItem.isEmpty()) || (rightHandItem._iClass == ICLASS_WEAPON && rightHandItem._iLoc != ILOC_TWOHAND && leftHandItem.isEmpty())) {
			player._pBlockFlag = true;
		}
	}

	player._pBlockFlag = player._pBlockFlag || player.isHoldingItem(ItemType::Shield);
}

PlayerWeaponGraphic GetPlrAnimWeaponId(const Player &player)
{
	const Item &leftHandItem = player.InvBody[INVLOC_HAND_LEFT];
	const Item &rightHandItem = player.InvBody[INVLOC_HAND_RIGHT];
	const bool holdsShield = player.isHoldingItem(ItemType::Shield);
	const bool leftHandUsable = player.CanUseItem(leftHandItem);
	const bool rightHandUsable = player.CanUseItem(rightHandItem);
	ItemType weaponItemType = ItemType::None;

	if (!leftHandItem.isEmpty() && leftHandItem._iClass == ICLASS_WEAPON && leftHandUsable) {
		weaponItemType = leftHandItem._itype;
	}

	// POE2 Mashup: a sceptre held with another weapon doesn't decide how the hero swings.
	const bool rightIsOffhandSceptre = gbIsPoe2 && ItemGrantsSpirit(rightHandItem) && weaponItemType != ItemType::None && !ItemGrantsSpirit(leftHandItem);
	if (!rightHandItem.isEmpty() && rightHandItem._iClass == ICLASS_WEAPON && rightHandUsable && !rightIsOffhandSceptre) {
		weaponItemType = rightHandItem._itype;
	}

	switch (weaponItemType) {
	case ItemType::Sword:
		return holdsShield ? PlayerWeaponGraphic::SwordShield : PlayerWeaponGraphic::Sword;
	case ItemType::Axe:
		return PlayerWeaponGraphic::Axe;
	case ItemType::Bow:
		return PlayerWeaponGraphic::Bow;
	case ItemType::Mace:
		return holdsShield ? PlayerWeaponGraphic::MaceShield : PlayerWeaponGraphic::Mace;
	case ItemType::Staff:
		return PlayerWeaponGraphic::Staff;
	default:
		return holdsShield ? PlayerWeaponGraphic::UnarmedShield : PlayerWeaponGraphic::Unarmed;
	}
}

PlayerArmorGraphic GetPlrAnimArmorId(Player &player)
{
	const Item &chestItem = player.InvBody[INVLOC_CHEST];
	const bool chestUsable = player.CanUseItem(chestItem);
	const uint8_t playerLevel = player.getCharacterLevel();

	if (chestUsable) {
		switch (chestItem._itype) {
		case ItemType::HeavyArmor:
			if (player._pClass == HeroClass::Monk) {
				if (chestItem._iMagical == ITEM_QUALITY_UNIQUE)
					player._pIAC += playerLevel / 2;
			}
			return PlayerArmorGraphic::Heavy;
		case ItemType::MediumArmor:
			if (player._pClass == HeroClass::Monk) {
				if (chestItem._iMagical == ITEM_QUALITY_UNIQUE)
					player._pIAC += playerLevel * 2;
				else
					player._pIAC += playerLevel / 2;
			}
			return PlayerArmorGraphic::Medium;
		default:
			if (player._pClass == HeroClass::Monk)
				player._pIAC += playerLevel * 2;
			return PlayerArmorGraphic::Light;
		}
	}

	return PlayerArmorGraphic::Light;
}

void CalcPlrGraphics(Player &player, PlayerWeaponGraphic animWeaponId, PlayerArmorGraphic animArmorId, bool loadgfx)
{
	const uint8_t gfxNum = static_cast<uint8_t>(animWeaponId) | static_cast<uint8_t>(animArmorId);
	if (player._pgfxnum != gfxNum && loadgfx) {
		player._pgfxnum = gfxNum;
		ResetPlayerGFX(player);
		SetPlrAnims(player);
		player.previewCelSprite = std::nullopt;
		player_graphic graphic = player.getGraphic();
		int8_t numberOfFrames;
		int8_t ticksPerFrame;
		player.getAnimationFramesAndTicksPerFrame(graphic, numberOfFrames, ticksPerFrame);
		LoadPlrGFX(player, graphic);
		OptionalClxSpriteList sprites;
		if (!HeadlessMode) {
			auto &animData = player.AnimationData[static_cast<size_t>(graphic)];
			if (animData.sprites.has_value()) {
				sprites = animData.spritesForDirection(player._pdir);
			} else {
				// In multiplayer games, a remote player can unequip their shield while that player is blocking an attack on the host.
				// This results in a nonexistent animation state on the host where the remote player must block with no shield equipped.
				// ie, (graphic == player_graphic::Block && !player._pBlockFlag) requests warrior animation "whnbl"
				// Attempting to load the nonexistent animation crashes the host.
				// This could also happen when unequipping a weapon during an attack, etc.
				// To avoid the crash, we can set the remote player into a standing animation before updating the items on their sprite.
				graphic = player_graphic::Stand;
				NewPlrAnim(player, graphic, player._pdir);
				player.getAnimationFramesAndTicksPerFrame(graphic, numberOfFrames, ticksPerFrame);
				sprites = player.AnimationData[static_cast<size_t>(graphic)].spritesForDirection(player._pdir);
			}
		}
		player.AnimInfo.changeAnimationData(sprites, numberOfFrames, ticksPerFrame);
	} else {
		player._pgfxnum = gfxNum;
	}
}

void CalcPlrAuricBonus(Player &player)

{
	if (&player == MyPlayer) {
		if (player.InvBody[INVLOC_AMULET].isEmpty() || player.InvBody[INVLOC_AMULET].IDidx != IDI_AURIC) {
			const int half = MaxGold;
			MaxGold = GOLD_MAX_LIMIT;

			if (half != MaxGold)
				StripTopGold(player);
		} else {
			MaxGold = GOLD_MAX_LIMIT * 2;
		}
	}
}

void CalcPlrItemVals(Player &player, bool loadgfx)
{
	int minDamage = 0;
	int maxDamage = 0;
	int ac = 0;

	int dam = 0;
	int toHit = 0;
	int bonusAc = 0;

	ItemSpecialEffect flags = ItemSpecialEffect::None;
	ItemSpecialEffectHf damAcFlags = ItemSpecialEffectHf::None;

	int strength = 0;
	int magic = 0;
	int dexterity = 0;
	int vitality = 0;

	uint64_t spells = 0;

	int fireRes = 0;
	int lightRes = 0;
	int magicRes = 0;

	int damMod = 0;
	int getHit = 0;

	int lightRadius = 10;

	Fixed26_6 life = Fixed26_6::fromInt(0);
	Fixed26_6 mana = Fixed26_6::fromInt(0);

	int8_t splLvlAdd = 0;
	int targetAc = 0;

	int minFireDam = 0;
	int maxFireDam = 0;
	int minLightDam = 0;
	int maxLightDam = 0;
	int lifeRegen = 0;
	int manaRegen = 0;
	std::array<int, static_cast<size_t>(Poe2ItemStat::Count)> poe2Stats {};
	UniqueRuleTotals uniqueRules {};

	// POE2 Mashup: jewels in allocated passive tree sockets count like worn gear.
	std::vector<const Item *> worn;
	for (const Item &item : player.InvBody)
		worn.push_back(&item);
	for (const Item *jewel : ActiveJewels(player))
		worn.push_back(jewel);
	// A unique ring can double the other ring's implicit: a copy of just the implicit counts as one more worn item.
	Item mirroredImplicit;
	for (const auto &[ring, other] : { std::pair { INVLOC_RING_LEFT, INVLOC_RING_RIGHT }, std::pair { INVLOC_RING_RIGHT, INVLOC_RING_LEFT } }) {
		UniqueRuleTotals ringRules {};
		AddUniqueRules(player.InvBody[ring], ringRules);
		const Item &otherRing = player.InvBody[other];
		if (ringRules[static_cast<size_t>(UniqueRule::MirrorRing)] <= 0 || otherRing.isEmpty() || !otherRing._iStatFlag)
			continue;
		mirroredImplicit._itype = otherRing._itype;
		mirroredImplicit._iStatFlag = true;
		mirroredImplicit._iMagical = ITEM_QUALITY_NORMAL;
		for (const ItemImplicit &implicit : ItemImplicits(otherRing))
			ApplyModEffect(mirroredImplicit, implicit.implicit->effect, implicit.values[0], implicit.values[1]);
		worn.push_back(&mirroredImplicit);
		break;
	}

	// POE2 Mashup: a sceptre held with another weapon adds its modifiers and Spirit but not its damage (Curtis 2026-10-16).
	const Item *offhandSceptre = nullptr;
	if (gbIsPoe2) {
		const Item &left = player.InvBody[INVLOC_HAND_LEFT];
		const Item &right = player.InvBody[INVLOC_HAND_RIGHT];
		const auto isWeapon = [](const Item &hand) { return !hand.isEmpty() && hand._iStatFlag && hand._iClass == ICLASS_WEAPON; };
		if (isWeapon(left) && isWeapon(right)) {
			if (ItemGrantsSpirit(right))
				offhandSceptre = &right;
			else if (ItemGrantsSpirit(left))
				offhandSceptre = &left;
		}
	}

	for (const Item *wornItem : worn) {
		const Item &item = *wornItem;
		if (!item.isEmpty() && item._iStatFlag) {

			if (wornItem != offhandSceptre) {
				minDamage += item._iMinDam;
				maxDamage += item._iMaxDam;
			}
			ac += item._iAC;

			if (IsValidSpell(item._iSpell) && item._iCharges != 0) {
				spells |= GetSpellBitmask(item._iSpell);
			}

			if (item._iMagical == ITEM_QUALITY_NORMAL || item._iIdentified) {
				dam += item._iPLDam;
				toHit += item._iPLToHit;
				bonusAc += GetBonusAC(item);
				flags |= item._iFlags;
				damAcFlags |= item._iDamAcFlags;
				strength += item._iPLStr;
				magic += item._iPLMag;
				dexterity += item._iPLDex;
				vitality += item._iPLVit;
				fireRes += item._iPLFR;
				lightRes += item._iPLLR;
				magicRes += item._iPLMR;
				damMod += item._iPLDamMod;
				getHit += item._iPLGetHit;
				lightRadius += item._iPLLight;
				life += item._iPLHP;
				mana += item._iPLMana;
				splLvlAdd += item._iSplLvlAdd;
				targetAc += item._iPLEnAc;
				minFireDam += item._iFMinDam;
				maxFireDam += item._iFMaxDam;
				minLightDam += item._iLMinDam;
				maxLightDam += item._iLMaxDam;
				lifeRegen += item._iPLLifeRegen;
				manaRegen += item._iPLManaRegen;
				for (size_t stat = 0; stat < poe2Stats.size(); stat++)
					poe2Stats[stat] += item._iPoe2Stats[stat];
				AddUniqueRules(item, uniqueRules);
				AddCraftRules(item, uniqueRules);
			}
		}
	}
	// POE2 Mashup: boss unique flasks and charms work from the belt.
	for (const Item &beltItem : player.SpdList) {
		AddBeltUniqueRules(beltItem, uniqueRules);
		const int beltResistance = BeltUniqueResistance(beltItem);
		fireRes += beltResistance;
		lightRes += beltResistance;
		magicRes += beltResistance;
	}
	// POE2 Mashup: the passive tree's shrines and rule notables join the gear's unique rules.
	PassiveTotals passives = GetPassiveTotals(player);
	AddPassiveRules(passives, uniqueRules);
	player.poe2.uniqueRules = uniqueRules;
	const auto uniqueRule = [&uniqueRules](UniqueRule rule) { return static_cast<int>(uniqueRules[static_cast<size_t>(rule)]); };
	// POE2 Mashup: unique rules that work like gear stats.
	poe2Stats[static_cast<size_t>(Poe2ItemStat::AttackSpeed)] += uniqueRule(UniqueRule::ActionSpeed);
	poe2Stats[static_cast<size_t>(Poe2ItemStat::CastSpeed)] += uniqueRule(UniqueRule::ActionSpeed);
	for (const Poe2ItemStat stat : { Poe2ItemStat::MeleeSkillLevels, Poe2ItemStat::ProjectileSkillLevels, Poe2ItemStat::SpellSkillLevels })
		poe2Stats[static_cast<size_t>(stat)] += uniqueRule(UniqueRule::AllSkillLevels);
	for (size_t stat = 0; stat < poe2Stats.size(); stat++)
		player.poe2.itemStats[stat] = static_cast<int16_t>(std::clamp(poe2Stats[stat], static_cast<int>(INT16_MIN), static_cast<int>(INT16_MAX)));
	const auto gearStat = [&poe2Stats](Poe2ItemStat stat) { return poe2Stats[static_cast<size_t>(stat)]; };

	// POE2 Mashup: the passive tree adds to what the gear gives.
	// Jewels' percent increases work like the tree's.
	passives[static_cast<size_t>(PassiveStat::LifePercent)] += gearStat(Poe2ItemStat::LifePercent);
	passives[static_cast<size_t>(PassiveStat::ManaPercent)] += gearStat(Poe2ItemStat::ManaPercent);
	passives[static_cast<size_t>(PassiveStat::ArmorPercent)] += gearStat(Poe2ItemStat::ArmourPercent);
	passives[static_cast<size_t>(PassiveStat::EvasionPercent)] += gearStat(Poe2ItemStat::EvasionPercent);
	passives[static_cast<size_t>(PassiveStat::EnergyShieldPercent)] += gearStat(Poe2ItemStat::EnergyShieldPercent);
	CachePassiveTotals(player, passives);
	const auto passive = [&passives](PassiveStat stat) { return passives[static_cast<size_t>(stat)]; };
	dam += PassiveWeaponDamage(player, passives);
	lightRadius += passive(PassiveStat::LightRadius);
	player.poe2.lifeRegenPerMille = passive(PassiveStat::LifeRegenRate);
	player.poe2.passiveBlockChance = passive(PassiveStat::BlockChance);
	strength += passive(PassiveStat::Strength);
	magic += passive(PassiveStat::Magic);
	dexterity += passive(PassiveStat::Dexterity);
	vitality += passive(PassiveStat::Vitality);
	dam += passive(PassiveStat::Damage);
	toHit += passive(PassiveStat::ToHit);
	fireRes += passive(PassiveStat::FireResist) + passive(PassiveStat::AllResist);
	lightRes += passive(PassiveStat::LightningResist) + passive(PassiveStat::AllResist);
	magicRes += passive(PassiveStat::MagicResist) + passive(PassiveStat::AllResist);
	if (IsPersistentSkillActive(player, PersistentSkill::PurityOfElements)) {
		const int purity = PurityResistance * MapAuraPercent() / 100;
		fireRes += purity;
		lightRes += purity;
		magicRes += purity;
	}
	lifeRegen += passive(PassiveStat::LifeRegen);
	manaRegen += passive(PassiveStat::ManaRegen);
	splLvlAdd += static_cast<int8_t>(passive(PassiveStat::SpellLevels));
	// Every 10% increased attack or cast speed from the tree and gear skips one more animation frame.
	// Heavy and focused bases slow attacks and casts, but only by cancelling speed from elsewhere.
	player.poe2.passiveAttackFrames = static_cast<int8_t>(std::clamp((passive(PassiveStat::AttackSpeed) + gearStat(Poe2ItemStat::AttackSpeed)) / SpeedPercentPerFrame, 0, static_cast<int>(MaxPassiveSpeedFrames)));
	player.poe2.passiveCastFrames = static_cast<int8_t>(std::clamp((passive(PassiveStat::CastSpeed) + gearStat(Poe2ItemStat::CastSpeed)) / SpeedPercentPerFrame, 0, static_cast<int>(MaxPassiveSpeedFrames)));

	// POE2 Mashup: cloth bases give energy shield and leather gives evasion instead of armour.
	int evasion = 0;
	int energyShield = 0;
	int evasionPercent = 0;
	if (gbIsPoe2) {
		for (const Item &item : player.InvBody) {
			if (item.isEmpty() || !item._iStatFlag || item._iAC == 0)
				continue;
			const int baseDefense = item._iAC;
			const int bonusDefense = (item._iMagical == ITEM_QUALITY_NORMAL || item._iIdentified) ? GetBonusAC(item) : 0;
			const DefenseType type = ItemDefenseType(item);
			int baseArmour;
			int baseEvasion;
			int baseShield;
			SplitDefense(type, baseDefense, baseArmour, baseEvasion, baseShield);
			int bonusArmour;
			int bonusEvasion;
			int bonusShield;
			SplitDefense(type, bonusDefense, bonusArmour, bonusEvasion, bonusShield);
			evasion += baseEvasion + bonusEvasion;
			energyShield += (baseShield + bonusShield) * EnergyShieldPerDefense;
			ac -= baseDefense - baseArmour;
			bonusAc -= bonusDefense - bonusArmour;
		}
		for (const Item &item : player.InvBody) {
			if (item.isEmpty() || !item._iStatFlag)
				continue;
			const BodyArmourImplicit implicit = GetBodyArmourImplicit(item);
			life += Fixed26_6::fromInt(implicit.life);
			fireRes += implicit.allResistances;
			lightRes += implicit.allResistances;
			magicRes += implicit.allResistances + implicit.magicResistance;
			manaRegen += implicit.manaRegenPercent;
			evasionPercent += implicit.evasionPercent;
		}
		evasion += evasion * (passive(PassiveStat::EvasionPercent) + evasionPercent) / 100;
		energyShield += energyShield * passive(PassiveStat::EnergyShieldPercent) / 100;
	}
	ac += ac * passive(PassiveStat::ArmorPercent) / 100;
	ac -= ac * std::min(uniqueRule(UniqueRule::LessArmour), 100) / 100;
	if (uniqueRule(UniqueRule::NoEvasion) > 0)
		evasion = 0;
	player.poe2.evasion = evasion;

	CalcPlrDamage(player, minDamage, maxDamage);
	CalcPlrPrimaryStats(player, strength, magic, dexterity, vitality);
	player._pIAC = ac;
	player._pIBonusDam = dam;
	player._pIBonusToHit = toHit;
	player._pIBonusAC = bonusAc;
	player._pIFlags = flags;
	player.pDamAcFlags = damAcFlags;
	player._pIBonusDamMod = damMod;
	player._pIGetHit = getHit;
	player.poe2.lifeRegen = lifeRegen;
	player.poe2.manaRegenBonus = manaRegen;
	CalcPlrLightRadius(player, lightRadius);
	CalcPlrDamageMod(player);
	player._pISpells = spells;
	EnsureValidReadiedSpell(player);
	player._pISplLvlAdd = splLvlAdd;
	player._pIEnAc = targetAc;
	CalcPlrResistances(player, flags, fireRes, lightRes, magicRes);
	if (gbIsPoe2)
		mana += Fixed26_6::fromInt(Poe2BonusMana(player.getCharacterLevel()));
	if (passive(PassiveStat::LifePercent) != 0 || passive(PassiveStat::ManaPercent) != 0) {
		// Percent increases apply to everything else that adds life and mana.
		const ClassAttributes &classAttributes = player.getClassAttributes();
		const Fixed26_6 baseLife = life + player._pMaxHPBase + Fixed26_6::fromInt((classAttributes.itmLife * vitality).whole());
		const Fixed26_6 baseMana = mana + player._pMaxManaBase + Fixed26_6::fromInt((classAttributes.itmMana * magic).whole());
		life += Fixed26_6::fromRaw(baseLife.raw() * passive(PassiveStat::LifePercent) / 100);
		mana += Fixed26_6::fromRaw(baseMana.raw() * passive(PassiveStat::ManaPercent) / 100);
	}
	CalcPlrLifeMana(player, vitality, magic, life, mana, uniqueRule(UniqueRule::LessMaxLife));
	// A share of maximum mana can count as energy shield too.
	energyShield += player._pMaxMana.whole() * uniqueRule(UniqueRule::ManaToEnergyShield) / 100;
	SetMaxEnergyShield(player, energyShield);
	player._pIFMinDam = minFireDam;
	player._pIFMaxDam = maxFireDam;
	player._pILMinDam = minLightDam;
	player._pILMaxDam = maxLightDam;

	CalcPlrBlockFlag(player);

	CalcPlrGraphics(player, GetPlrAnimWeaponId(player), GetPlrAnimArmorId(player), loadgfx);

	CalcPlrAuricBonus(player);
	RedrawComponent(PanelDrawComponent::Mana);
	RedrawComponent(PanelDrawComponent::Health);
}

void CalcPlrInv(Player &player, bool loadgfx)
{
	// Determine the players current stats, this updates the statFlag on all equipped items that became unusable after
	//  a change in equipment.
	CalcSelfItems(player);

	// Determine the current item bonuses gained from usable equipped items
	if (&player != MyPlayer && !player.isOnActiveLevel()) {
		// Ensure we don't load graphics for players that aren't on our level
		loadgfx = false;
	}
	CalcPlrItemVals(player, loadgfx);

	if (&player == MyPlayer) {
		// Now that stat gains from equipped items have been calculated, mark unusable scrolls etc
		for (Item &item : InventoryAndBeltPlayerItemsRange { player }) {
			item.updateRequiredStatsCacheForPlayer(player);
		}
		player.CalcScrolls();
		if (IsStashOpen) {
			// If stash is open, ensure the items are displayed correctly
			Stash.RefreshItemStatFlags();
		}
		if (!player.HoldItem.isEmpty())
			player.HoldItem.updateRequiredStatsCacheForPlayer(player);
	}
}

void InitializeItem(Item &item, _item_indexes itemData)
{
	auto &pAllItem = AllItemsList[static_cast<size_t>(itemData)];

	// zero-initialize struct
	item = {};

	item._itype = pAllItem.itype;
	item._iCurs = pAllItem.iCurs;
	CopyUtf8(item._iName, pAllItem.iName, ItemNameLength);
	CopyUtf8(item._iIName, pAllItem.iName, ItemNameLength);
	item._iLoc = pAllItem.iLoc;
	item._iClass = pAllItem.iClass;
	item._iMinDam = pAllItem.iMinDam;
	item._iMaxDam = pAllItem.iMaxDam;
	item._iAC = pAllItem.iMinAC;
	item._iMiscId = pAllItem.iMiscId;
	item._iSpell = pAllItem.iSpell;

	if (pAllItem.iMiscId == IMISC_STAFF) {
		item._iCharges = gbIsHellfire ? 18 : 40;
	}
	// Curtis 2026-10-16: charms start empty and fill from kills.
	if (IsCharm(item))
		item._iCharges = 0;

	item._iMaxCharges = IsCharm(item) ? MaxCharmCharges : item._iCharges;
	if (IsStackable(item))
		SetStackCount(item, 1);
	item._iDurability = pAllItem.iDurability;
	item._iMaxDur = pAllItem.iDurability;
	item._iMinStr = pAllItem.iMinStr;
	item._iMinMag = pAllItem.iMinMag;
	item._iMinDex = pAllItem.iMinDex;
	item._ivalue = pAllItem.iValue;
	item._iIvalue = pAllItem.iValue;
	item._iPrePower = IPL_INVALID;
	item._iSufPower = IPL_INVALID;
	item._iPreTier = 0;
	item._iSufTier = 0;
	item._iRareTiers.fill(0);
	item._iMagical = ITEM_QUALITY_NORMAL;
	item._iRarePowers.fill(IPL_INVALID);
	item._iPoe2Mods = {};
	item._iPoe2Stats = {};
	item.IDidx = itemData;
	if (gbIsHellfire)
		item.dwBuff |= CF_HELLFIRE;
	if (gbIsPoe2)
		item.dwBuff |= CF_POE2;
	ApplyBaseProperties(item);
}

void GenerateNewSeed(Item &item)
{
	item._iSeed = AdvanceRndSeed();
}

int GetGoldCursor(int value)
{
	if (value >= GOLD_MEDIUM_LIMIT)
		return ICURS_GOLD_LARGE;

	if (value <= GOLD_SMALL_LIMIT)
		return ICURS_GOLD_SMALL;

	return ICURS_GOLD_MEDIUM;
}

void SetPlrHandGoldCurs(Item &gold)
{
	gold._iCurs = GetGoldCursor(gold._ivalue);
}

namespace {
void CreateStartingItem(Player &player, _item_indexes itemData)
{
	Item item;
	InitializeItem(item, itemData);
	GenerateNewSeed(item);
	item.updateRequiredStatsCacheForPlayer(player);
	AutoEquip(player, item) || AutoPlaceItemInBelt(player, item, true) || AutoPlaceItemInInventory(player, item);
}
} // namespace

void CreatePlrItems(Player &player)
{
	for (auto &item : player.InvBody) {
		item.clear();
	}

	// converting this to a for loop creates a `rep stosd` instruction,
	// so this probably actually was a memset
	memset(&player.InvGrid, 0, sizeof(player.InvGrid));

	for (auto &item : player.InvList) {
		item.clear();
	}

	player._pNumInv = 0;

	for (auto &item : player.SpdList) {
		item.clear();
	}

	const PlayerStartingLoadoutData &loadout = GetPlayerStartingLoadoutForClass(player._pClass);

	if (loadout.spell != SpellID::Null && loadout.spellLevel > 0) {
		player._pMemSpells = GetSpellBitmask(loadout.spell);
		player._pRSplType = SpellType::Spell;
		player._pRSpell = loadout.spell;
		player._pSplLvl[static_cast<unsigned>(loadout.spell)] = loadout.spellLevel;
	} else {
		player._pMemSpells = 0;
	}

	if (loadout.skill != SpellID::Null) {
		player._pAblSpells = GetSpellBitmask(loadout.skill);
		if (player._pRSplType == SpellType::Invalid) {
			player._pRSplType = SpellType::Skill;
			player._pRSpell = loadout.skill;
		}
	}

	InitCursor();
	for (const _item_indexes itemData : loadout.items) {
		if (itemData != _item_indexes::IDI_NONE)
			CreateStartingItem(player, itemData);
	}
	FreeCursor();

	if (loadout.gold > 0) {
		Item &goldItem = player.InvList[player._pNumInv];
		MakeGoldStack(goldItem, loadout.gold);

		player._pNumInv++;
		player.InvGrid[30] = player._pNumInv;

		player._pGold = goldItem._ivalue;
	}

	CalcPlrItemVals(player, false);
}

bool ItemSpaceOk(Point position)
{
	if (!InDungeonBounds(position)) {
		return false;
	}

	if (IsTileSolid(position)) {
		return false;
	}

	if (dItem[position.x][position.y] != 0) {
		return false;
	}

	if (dMonster[position.x][position.y] != 0) {
		return false;
	}

	if (dPlayer[position.x][position.y] != 0) {
		return false;
	}

	if (IsItemBlockingObjectAtPosition(position)) {
		return false;
	}

	return true;
}

int AllocateItem()
{
	assert(ActiveItemCount < MAXITEMS);

	const int inum = ActiveItems[ActiveItemCount];
	ActiveItemCount++;

	Items[inum] = {};

	return inum;
}

uint8_t PlaceItemInWorld(Item &&item, WorldTilePosition position)
{
	assert(ActiveItemCount < MAXITEMS);

	const uint8_t ii = ActiveItems[ActiveItemCount];
	ActiveItemCount++;

	dItem[position.x][position.y] = static_cast<int8_t>(ii + 1);
	auto &newItem = Items[ii];
	newItem = std::move(item);
	newItem.position = position;
	RespawnItem(newItem, true);

	if (CornerStone.isAvailable() && position == CornerStone.position) {
		CornerStone.item = newItem;
		InitQTextMsg(TEXT_CORNSTN);
		Quests[Q_CORNSTN]._qactive = QUEST_DONE;
	}

	return ii;
}

Point GetSuperItemLoc(Point position)
{
	const std::optional<Point> itemPosition = FindClosestValidPosition(ItemSpaceOk, position, 1, 50);

	return itemPosition.value_or(Point { 0, 0 }); // TODO handle no space for dropping items
}

void GetItemAttrs(Item &item, _item_indexes itemData, int lvl)
{
	auto &baseItemData = AllItemsList[static_cast<size_t>(itemData)];
	item._itype = baseItemData.itype;
	item._iCurs = baseItemData.iCurs;
	CopyUtf8(item._iName, baseItemData.iName, ItemNameLength);
	CopyUtf8(item._iIName, baseItemData.iName, ItemNameLength);
	item._iLoc = baseItemData.iLoc;
	item._iClass = baseItemData.iClass;
	item._iMinDam = baseItemData.iMinDam;
	item._iMaxDam = baseItemData.iMaxDam;
	item._iAC = baseItemData.iMinAC + GenerateRnd(baseItemData.iMaxAC - baseItemData.iMinAC + 1);
	item._iFlags = baseItemData.iFlags;
	item._iMiscId = baseItemData.iMiscId;
	item._iSpell = baseItemData.iSpell;
	item._iMagical = ITEM_QUALITY_NORMAL;
	item._ivalue = baseItemData.iValue;
	item._iIvalue = baseItemData.iValue;
	item._iDurability = baseItemData.iDurability;
	item._iMaxDur = baseItemData.iDurability;
	item._iMinStr = baseItemData.iMinStr;
	item._iMinMag = baseItemData.iMinMag;
	item._iMinDex = baseItemData.iMinDex;
	item.IDidx = itemData;
	if (gbIsHellfire)
		item.dwBuff |= CF_HELLFIRE;
	if (gbIsPoe2)
		item.dwBuff |= CF_POE2;
	item._iPrePower = IPL_INVALID;
	item._iSufPower = IPL_INVALID;
	item._iPreTier = 0;
	item._iSufTier = 0;
	item._iRareTiers.fill(0);
	item._iRarePowers.fill(IPL_INVALID);
	item._iPoe2Mods = {};
	item._iPoe2Stats = {};
	ApplyBaseProperties(item);

	if (item._iMiscId == IMISC_BOOK)
		GetBookSpell(item, lvl);

	if (gbIsHellfire && item._iMiscId == IMISC_OILOF)
		GetOilType(item, lvl);

	if (IsCharm(item)) {
		item._iMaxCharges = MaxCharmCharges;
		item._iCharges = 0;
	}
	if (IsStackable(item))
		SetStackCount(item, 1);
	NameFlask(item);

	if (item._itype != ItemType::Gold)
		return;

	int rndv;
	const int itemlevel = ItemsGetCurrlevel();
	switch (sgGameInitInfo.nDifficulty) {
	case DIFF_NORMAL:
		rndv = 5 * itemlevel + GenerateRnd(10 * itemlevel);
		break;
	case DIFF_NIGHTMARE:
		rndv = 5 * (itemlevel + 16) + GenerateRnd(10 * (itemlevel + 16));
		break;
	case DIFF_HELL:
		rndv = 5 * (itemlevel + 32) + GenerateRnd(10 * (itemlevel + 32));
		break;
	}
	if (leveltype == DTYPE_HELL)
		rndv += rndv / 8;

	item._ivalue = std::min(rndv, GOLD_MAX_LIMIT);
	SetPlrHandGoldCurs(item);
}

void SetupItem(Item &item)
{
	item.setNewAnimation(MyPlayer != nullptr && MyPlayer->pLvlLoad == 0);
	item._iIdentified = false;
}

void MakeFixedUnique(const Player &player, Item &item, _item_indexes base, int level, _unique_items uid)
{
	GetItemAttrs(item, base, level);
	GetUniqueItem(player, item, uid);
	SetupItem(item);
}

Item *SpawnUnique(_unique_items uid, Point position, std::optional<int> level /*= std::nullopt*/, bool sendmsg /*= true*/, bool exactPosition /*= false*/)
{
	if (ActiveItemCount >= MAXITEMS)
		return nullptr;

	const int ii = AllocateItem();
	auto &item = Items[ii];
	if (exactPosition && CanPut(position)) {
		item.position = position;
		dItem[position.x][position.y] = ii + 1;
	} else {
		GetSuperItemSpace(position, ii);
	}
	int curlv = ItemsGetCurrlevel();

	std::underlying_type_t<_item_indexes> idx = 0;
	while (AllItemsList[idx].iItemId != UniqueItems[uid].UIItemId)
		idx++;

	if (sgGameInitInfo.nDifficulty == DIFF_NORMAL) {
		GetItemAttrs(item, static_cast<_item_indexes>(idx), curlv);
		GetUniqueItem(*MyPlayer, item, uid);
		SetupItem(item);
	} else {
		if (level)
			curlv = *level;
		const ItemData &uniqueItemData = AllItemsList[idx];
		const _item_indexes dropIdx = GetItemIndexForDroppableItem(false, [&uniqueItemData](const ItemData &item) {
			return item.itype == uniqueItemData.itype;
		});
		SetupAllItems(*MyPlayer, item, dropIdx, AdvanceRndSeed(), curlv * 2, 15, true, false);
		TryRandomUniqueItem(item, dropIdx, curlv * 2, 15, true, false);
		SetupItem(item);
	}

	if (sendmsg)
		NetSendCmdPItem(false, CMD_SPAWNITEM, item.position, item);

	return &item;
}

void GetSuperItemSpace(Point position, int8_t inum)
{
	Point positionToCheck = position;
	if (GetItemSpace(positionToCheck, inum))
		return;
	for (int k = 2; k < 50; k++) {
		for (int j = -k; j <= k; j++) {
			for (int i = -k; i <= k; i++) {
				const Displacement offset = { i, j };
				positionToCheck = position + offset;
				if (!ItemSpaceOk(positionToCheck))
					continue;
				Items[inum].position = positionToCheck;
				dItem[positionToCheck.x][positionToCheck.y] = inum + 1;
				return;
			}
		}
	}
}

_item_indexes RndItemForMonsterLevel(int8_t monsterLevel)
{
	if (GenerateRnd(100) > 40)
		return IDI_NONE;

	if (GenerateRnd(100) > 25)
		return IDI_GOLD;

	return GetItemIndexForDroppableItem(true, [&monsterLevel](const ItemData &item) {
		return item.iMinMLvl <= monsterLevel;
	});
}

void SetupAllItems(const Player &player, Item &item, _item_indexes idx, uint32_t iseed, int lvl, int uper, bool onlygood, bool pregen, int uidOffset /*= 0*/, bool forceNotUnique /*= false*/)
{
	item._iSeed = iseed;
	SetRndSeed(iseed);
	GetItemAttrs(item, idx, lvl / 2);
	item._iCreateInfo = lvl;

	if (pregen)
		item._iCreateInfo |= CF_PREGEN;
	if (onlygood)
		item._iCreateInfo |= CF_ONLYGOOD;

	if (uper == 15)
		item._iCreateInfo |= CF_UPER15;
	else if (uper == 1)
		item._iCreateInfo |= CF_UPER1;

	if (gbIsPoe2 && item._iMiscId == IMISC_JEWEL) {
		SetupJewel(item, lvl);
		return;
	}

	if (const std::optional<item_quality> lootRarity = RarityToRoll(item); lootRarity && item._iMiscId != IMISC_UNIQUE) {
		// POE2 Mashup: the loot roll picked the rarity; a unique keeps the pick stored in its unique offset.
		std::optional<_unique_items> uid;
		if (*lootRarity == ITEM_QUALITY_UNIQUE && (item.dwBuff & CF_POE2_BUNDLE_UNIQUE) != 0) {
			// One of the bundle's uniques: the offset is its place among its base's.
			const std::vector<int> bundleUniques = Poe2UniquesOnBase(item.IDidx);
			if (static_cast<size_t>(uidOffset) < bundleUniques.size())
				uid = static_cast<_unique_items>(bundleUniques[uidOffset]);
		} else if (*lootRarity == ITEM_QUALITY_UNIQUE) {
			const std::vector<uint8_t> validUniques = GetValidUniques(lvl, AllItemsList[item.IDidx].iItemId);
			if (static_cast<size_t>(uidOffset) < validUniques.size())
				uid = static_cast<_unique_items>(validUniques[validUniques.size() - 1 - uidOffset]);
		}
		if (uid) {
			GetUniqueItem(player, item, *uid);
		} else if (*lootRarity != ITEM_QUALITY_NORMAL) {
			if (*lootRarity == ITEM_QUALITY_UNIQUE)
				SetLootRarity(item, ITEM_QUALITY_RARE);
			GetItemBonus(player, item, lvl / 2, lvl, true, true);
		}
		if (item._iMagical != ITEM_QUALITY_UNIQUE)
			ItemRndDur(item);
		ApplyCorruptionImplicit(player, item);
	} else if (item._iMiscId != IMISC_UNIQUE) {
		const int iblvl = GetItemBLevel(lvl, item._iMiscId, onlygood, uper == 15);
		if (iblvl != -1) {
			_unique_items uid = UITEM_INVALID;
			if (!forceNotUnique) {
				uid = CheckUnique(item, iblvl, uper, uidOffset);
			} else {
				DiscardRandomValues(1);
			}
			if (uid == UITEM_INVALID) {
				GetItemBonus(player, item, iblvl / 2, iblvl, onlygood, true);
			} else {
				GetUniqueItem(player, item, uid);
			}
		}
		if (item._iMagical != ITEM_QUALITY_UNIQUE)
			ItemRndDur(item);
		ApplyCorruptionImplicit(player, item);
	} else {
		if (item._iLoc != ILOC_UNEQUIPABLE) {
			if (iseed > 109 || AllItemsList[static_cast<size_t>(idx)].iItemId != UniqueItems[iseed].UIItemId) {
				item.clear();
				return;
			}

			GetUniqueItem(player, item, (_unique_items)iseed); // uid is stored in iseed for uniques
		}
	}
	FinishFlask(item);
	FinishCraftingItem(item);
	FinishWaystone(item);
	FinishEndgameItem(item);
	// POE2 Mashup: normal items have nothing to identify.
	if (gbIsPoe2 && item._iMagical == ITEM_QUALITY_NORMAL)
		item._iIdentified = true;
}

void TryRandomUniqueItem(Item &item, _item_indexes idx, int8_t mLevel, int uper, bool onlygood, bool pregen)
{
	// If the item is a non-quest unique, find a random valid uid and force generate items to get an item with that uid.
	if ((item._iCreateInfo & CF_UNIQUE) == 0 || item._iMiscId == IMISC_UNIQUE)
		return;

	SetRndSeed(item._iSeed);

	// Get item base level, which is used in CheckUnique to get the correct valid uniques for the base item.
	DiscardRandomValues(1); // GetItemAttrs
	const int blvl = GetItemBLevel(mLevel, item._iMiscId, onlygood, uper == 15);

	// Gather all potential unique items. uid is the index into UniqueItems.
	auto validUniques = GetValidUniques(blvl, AllItemsList[static_cast<size_t>(idx)].iItemId);
	assert(!validUniques.empty());
	std::vector<int> uids;
	for (auto &possibleUid : validUniques) {
		// Verify item hasn't been dropped yet. We set this to true in MP, since uniques previously dropping shouldn't prevent further identical uniques from dropping.
		if (!UniqueItemFlags[possibleUid] || gbIsMultiplayer) {
			uids.emplace_back(possibleUid);
		}
	}

	// If we find at least one unique in uids that hasn't been obtained yet, we can proceed getting a random unique.
	if (uids.empty()) {
		// Set uper to 1 and make the level adjustment so we have better odds of not generating a unique item.
		if (uper == 15)
			mLevel += 4;
		uper = 1;

		const Point itemPos = item.position;

		// Force generate a non-unique item.
		DiabloGenerator itemGenerator(item._iSeed);
		do {
			item = {}; // Reset item data
			item.position = itemPos;
			SetupAllItems(*MyPlayer, item, idx, itemGenerator.advanceRndSeed(), mLevel, uper, onlygood, pregen);
		} while (item._iMagical == ITEM_QUALITY_UNIQUE);

		return;
	}

	const int32_t uidsIdx = std::max<int32_t>(0, GenerateRnd(static_cast<int32_t>(uids.size()))); // Index into uids, used to get a random uid from the uids vector.
	const int uid = uids[uidsIdx];                                                                // Actual unique id.
	const UniqueItem &uniqueItem = UniqueItems[uid];

	// If the selected unique was already generated, there is no need to fiddle with its parameters.
	if (item._iUid == uid) {
		if (!gbIsMultiplayer) {
			UniqueItemFlags[uid] = true;
		}
		return;
	}

	// Find our own id to calculate the offset in validUniques and check if we can generate a reverse-compatible version of the item.
	int uidOffset = -1;
	bool canGenerateReverseCompatible = true;
	for (size_t i = 0; i < validUniques.size(); i++) {
		if (validUniques[i] == uid) {
			// Vanilla always picks the last unique, so the offset is calculated from the back of the valid unique list.
			uidOffset = static_cast<int>(validUniques.size() - i - 1);
		} else if (uidOffset != -1 && UniqueItems[validUniques[i]].UIMinLvl <= uniqueItem.UIMinLvl) {
			// Found an item with same or lower level as our desired unique after our unique.
			// This means that we cannot possibly generate the item in reverse compatible mode and must rely on an offset.
			canGenerateReverseCompatible = false;
		}
	}
	assert(uidOffset != -1);

	const Point itemPos = item.position;
	if (canGenerateReverseCompatible) {
		int targetLvl = 1; // Target level for reverse compatibility, since vanilla always takes the last applicable uid in the list.

		// Set target level. Ideally we use uper 15 to have a 16% chance of generating a unique item.
		if (uniqueItem.UIMinLvl - 4 > 0) { // Negative level will underflow. Lvl 0 items may have unintended consequences.
			uper = 15;
			targetLvl = uniqueItem.UIMinLvl - 4;
		} else {
			uper = 1;
			targetLvl = uniqueItem.UIMinLvl;
		}

		// Force generate items until we find a uid match.
		DiabloGenerator itemGenerator(item._iSeed);
		do {
			item = {}; // Reset item data
			item.position = itemPos;
			// Set onlygood = true, to always get the required item base level for the unique.
			SetupAllItems(*MyPlayer, item, idx, itemGenerator.advanceRndSeed(), targetLvl, uper, true, pregen);
		} while (item._iUid != uid);
	} else {
		// Recreate the item with new offset, this creates the desired unique item but is not reverse compatible.
		const int seed = item._iSeed;
		item = {}; // Reset item data
		item.position = itemPos;
		SetupAllItems(*MyPlayer, item, idx, seed, mLevel, uper, onlygood, pregen, uidOffset);
		item.dwBuff |= (uidOffset << 1) & CF_UIDOFFSET;
		assert(item._iUid == uid);
	}

	// Set item as obtained to prevent it from being dropped again in SP.
	if (!gbIsMultiplayer) {
		UniqueItemFlags[uid] = true;
	}
}

namespace {

bool IsPoe2FlaskOrCharm(item_misc_id miscId)
{
	return IsAnyOf(miscId, IMISC_HEAL, IMISC_FULLHEAL, IMISC_MANA, IMISC_FULLMANA, IMISC_REJUV, IMISC_FULLREJUV, IMISC_CHARM_LIFE, IMISC_CHARM_MANA,
	    IMISC_CHARM_GUARD, IMISC_CHARM_FIRE, IMISC_CHARM_LIGHTNING, IMISC_CHARM_MAGIC, IMISC_CHARM_STUN, IMISC_CHARM_GOLDEN);
}

bool IsPoe2Orb(item_misc_id miscId)
{
	return IsCurrencyMiscId(miscId);
}

bool IsPoe2UncutGem(item_misc_id miscId)
{
	return IsAnyOf(miscId, IMISC_UNCUT_SKILL, IMISC_UNCUT_SUPPORT, IMISC_UNCUT_SPIRIT);
}

/** @brief Diablo's leftovers that still drop now and then: scrolls, oils, elixirs and runes, but no spell books. */
bool IsPoe2MiscLoot(const ItemData &data)
{
	return data.itype == ItemType::Misc && data.iMiscId != IMISC_BOOK && !IsPoe2FlaskOrCharm(data.iMiscId) && !IsPoe2Orb(data.iMiscId)
	    && !IsPoe2UncutGem(data.iMiscId) && !IsAnyOf(data.iMiscId, IMISC_WAYSTONE, IMISC_TABLET, IMISC_MAP_FRAGMENT, IMISC_MAP_KEY) && SkillForGemData(data).isEmpty();
}

bool IsPoe2Equipment(const ItemData &data)
{
	return EquipmentClassWeight(data.itype) > 0;
}

bool IsPoe2LootCandidate(size_t index, int itemLevel)
{
	if (!IsItemAvailable(static_cast<int>(index)))
		return false;
	const ItemData &data = AllItemsList[index];
	if (data.dropRate == 0 || data.iMinMLvl > itemLevel)
		return false;
	return !IsAnyOf(data.iSpell, SpellID::Resurrect, SpellID::HealOther) || gbIsMultiplayer;
}

/** @brief Picks a kind of equipment by its class weight, then one of its bases, favouring bases near the item level. */
std::optional<_item_indexes> PickPoe2EquipmentBase(int itemLevel)
{
	// ItemType::None is -1, so the last real type sets the size.
	std::array<int, static_cast<size_t>(ItemType::Amulet) + 1> classWeights {};
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		const ItemData &data = AllItemsList[i];
		if (IsPoe2Equipment(data) && IsPoe2LootCandidate(i, itemLevel))
			classWeights[static_cast<size_t>(data.itype)] = EquipmentClassWeight(data.itype);
	}
	const int classTotal = std::accumulate(classWeights.begin(), classWeights.end(), 0);
	if (classTotal == 0)
		return std::nullopt;
	int roll = LootRandom(classTotal);
	size_t type = 0;
	while (roll >= classWeights[type]) {
		roll -= classWeights[type];
		type++;
	}

	std::vector<std::pair<_item_indexes, int>> bases;
	int total = 0;
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		const ItemData &data = AllItemsList[i];
		if (static_cast<size_t>(data.itype) != type || !IsPoe2LootCandidate(i, itemLevel))
			continue;
		const int weight = data.dropRate * BaseRecencyPercent(itemLevel, data.iMinMLvl);
		bases.emplace_back(static_cast<_item_indexes>(i), weight);
		total += weight;
	}
	roll = LootRandom(total);
	for (const auto &[index, weight] : bases) {
		if (roll < weight)
			return index;
		roll -= weight;
	}
	return bases.back().first;
}

std::optional<_item_indexes> PickPoe2Weighted(int itemLevel, tl::function_ref<bool(const ItemData &)> isOkay)
{
	std::vector<std::pair<_item_indexes, int>> items;
	int total = 0;
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		if (!IsPoe2LootCandidate(i, itemLevel) || !isOkay(AllItemsList[i]))
			continue;
		items.emplace_back(static_cast<_item_indexes>(i), AllItemsList[i].dropRate);
		total += AllItemsList[i].dropRate;
	}
	if (total == 0)
		return std::nullopt;
	int roll = LootRandom(total);
	for (const auto &[index, weight] : items) {
		if (roll < weight)
			return index;
		roll -= weight;
	}
	return items.back().first;
}

std::optional<_item_indexes> PickPoe2Currency(int itemLevel)
{
	// A band with nothing in it yet (chase) falls to the band below.
	for (int band = static_cast<int>(RollCurrencyBand()); band >= 0; band--) {
		const std::optional<_item_indexes> orb = PickPoe2Weighted(itemLevel, [band](const ItemData &data) {
			return IsPoe2Orb(data.iMiscId) && static_cast<int>(OrbCurrencyBand(data.iMiscId)) == band;
		});
		if (orb)
			return orb;
	}
	return std::nullopt;
}

std::optional<_item_indexes> PickPoe2LootItem(LootCategory category, int itemLevel)
{
	switch (category) {
	case LootCategory::Equipment:
		return PickPoe2EquipmentBase(itemLevel);
	case LootCategory::Gold:
		return IDI_GOLD;
	case LootCategory::Currency:
		return PickPoe2Currency(itemLevel);
	case LootCategory::FlaskOrCharm:
		return PickPoe2Weighted(itemLevel, [](const ItemData &data) { return IsPoe2FlaskOrCharm(data.iMiscId); });
	case LootCategory::Gem:
		return PickPoe2Weighted(itemLevel, [](const ItemData &data) { return IsPoe2UncutGem(data.iMiscId); });
	case LootCategory::Endgame:
		return PickPoe2Weighted(itemLevel, [](const ItemData &data) { return data.iMiscId == IMISC_WAYSTONE; });
	case LootCategory::Jewel:
		return PickPoe2Weighted(itemLevel, [](const ItemData &data) { return data.iMiscId == IMISC_JEWEL; });
	default:
		return PickPoe2Weighted(itemLevel, IsPoe2MiscLoot);
	}
}

/** @brief A unique a loot roll picked: its base and its offset there, among Diablo's or the bundle's uniques. */
struct Poe2UniquePick {
	_item_indexes base;
	int offset;
	bool bundle;
	/** A unique flask or charm instead. */
	BeltUniqueId belt = BeltUniqueId::None;
};

/**
 * @brief Turns a unique roll into a unique with the base it belongs on: one of the bundle's, or one of Diablo's the hero
 * hasn't found yet. The tier is rolled first, as in POE2, and a tier with nothing left falls back to the next lower one.
 * @return Nothing if no unique can drop at this level.
 */
std::optional<Poe2UniquePick> PickPoe2Unique(int itemLevel, int poe2ItemLevel, LootSource source)
{
	std::array<std::vector<Poe2UniquePick>, static_cast<size_t>(UniqueTier::Count)> candidates;
	for (const Poe2Unique &unique : Poe2Uniques()) {
		// Boss uniques only drop from their boss's table.
		if (!unique.isGlobal() || unique.dropLevel > poe2ItemLevel || !IsItemAvailable(unique.baseItem))
			continue;
		const std::vector<int> onBase = Poe2UniquesOnBase(unique.baseItem);
		const auto found = std::find(onBase.begin(), onBase.end(), unique.uid);
		const int offset = static_cast<int>(found - onBase.begin());
		if (offset > static_cast<int>(CF_UIDOFFSET >> 1))
			continue;
		candidates[static_cast<size_t>(unique.tier)].push_back({ static_cast<_item_indexes>(unique.baseItem), offset, true });
	}
	for (const BeltUnique &unique : BeltUniques()) {
		const std::optional<int> base = BeltUniqueBase(unique);
		if (!unique.isGlobal() || unique.dropLevel > poe2ItemLevel || !base)
			continue;
		candidates[static_cast<size_t>(unique.tier)].push_back({ static_cast<_item_indexes>(*base), 0, false, unique.id });
	}
	for (size_t uid = 0; uid < UniqueItems.size() && uid < std::size(UniqueItemFlags); uid++) {
		const UniqueItem &unique = UniqueItems[uid];
		if (unique.UIMinLvl > itemLevel || (UniqueItemFlags[uid] && !gbIsMultiplayer))
			continue;
		std::optional<_item_indexes> base;
		for (size_t i = 0; i < AllItemsList.size() && !base; i++) {
			if (AllItemsList[i].iItemId == unique.UIItemId && IsItemAvailable(static_cast<int>(i)) && AllItemsList[i].iMiscId != IMISC_UNIQUE && IsPoe2Equipment(AllItemsList[i]))
				base = static_cast<_item_indexes>(i);
		}
		if (!base)
			continue;
		const std::vector<uint8_t> valid = GetValidUniques(itemLevel, unique.UIItemId);
		const auto found = std::find(valid.begin(), valid.end(), static_cast<uint8_t>(uid));
		if (found == valid.end())
			continue;
		const int offset = static_cast<int>(valid.end() - found) - 1;
		if (offset > static_cast<int>(CF_UIDOFFSET >> 1))
			continue;
		candidates[static_cast<size_t>(LegacyUniqueTier(unique.UIMinLvl))].push_back({ *base, offset, false });
	}
	const int rolled = static_cast<int>(RollUniqueTier(source));
	for (int i = 0; i < static_cast<int>(candidates.size()); i++) {
		// The rolled tier, then lower tiers, then higher ones if only those are left.
		const int tier = i <= rolled ? rolled - i : i;
		const std::vector<Poe2UniquePick> &pool = candidates[tier];
		if (!pool.empty())
			return pool[LootRandom(static_cast<int>(pool.size()))];
	}
	return std::nullopt;
}

/** @brief Where a loot roll comes from and the levels it drops at. */
struct Poe2LootOrigin {
	LootSource source;
	/** Diablo's item level, which picks affixes and uniques and is checked in multiplayer. */
	int itemLevel;
	/** The level that decides which bases can drop. */
	int baseLevel;
	/** The POE2 item level stored on equipment. */
	int poe2ItemLevel;
	std::string_view name;
	/** Generated with the level, so multiplayer rebuilds it the same. */
	bool pregen = false;
};

/** @brief The open map's item rarity, quantity (from monster effectiveness) and waystone drops; none outside maps. */
LootBonus MapLootBonus()
{
	LootBonus bonus;
	if (ActiveMapTier() == 0)
		return bonus;
	const MapEffects &map = ActiveMapEffects();
	bonus.rarity = map[MapAxis::ItemRarity];
	// "Loot quality varies": each roll's rarity swings up or down.
	if (const int variance = map[MapDanger::LootVariance]; variance > 0)
		bonus.rarity = std::max(bonus.rarity + LootRandom((2 * variance) + 1) - variance, 0);
	bonus.quantity = 100 + ((map[MapAxis::Effectiveness] - 100) * 55 / 100);
	bonus.waystones = map[MapAxis::WaystoneFind];
	bonus.equipment = AtlasEquipmentPercent(*MyPlayer);
	bonus.unique = AtlasUniquePercent(*MyPlayer, false);
	return bonus;
}

/** @brief The tier of a waystone dropping here: around the map's own tier inside a map. */
int DropWaystoneTier(int poe2ItemLevel)
{
	const int mapTier = ActiveMapTier();
	if (mapTier == 0)
		return WaystoneTierForItemLevel(poe2ItemLevel);
	const int roll = LootRandom(100);
	return std::clamp(roll < 15 ? mapTier + 1 : roll < 25 ? mapTier - 1
	                                                      : mapTier,
	    1, MaxWaystoneTier);
}

Poe2LootOrigin MonsterLootOrigin(const Monster &monster)
{
	const LootSource source = GetLootSource(monster);
	const int bonus = LootItemLevelBonus(source);
	// Multiplayer checks that a dropped item's level is one some monster has, so the bonus only applies in single player.
	return {
		source,
		std::min<int>(monster.data().level + (gbIsMultiplayer ? 0 : bonus), CF_LEVEL),
		static_cast<int>(monster.level(sgGameInitInfo.nDifficulty)) + bonus,
		Poe2ItemLevel(monster, sgGameInitInfo.nDifficulty),
		monster.name(),
	};
}

Poe2LootOrigin ContainerLootOrigin(bool pregen)
{
	// Chests and barrels drop at twice the dungeon level, as in Diablo.
	const int level = 2 * ItemsGetCurrlevel();
	return {
		LootSource::Normal,
		std::min<int>(level, CF_LEVEL),
		level + (15 * sgGameInitInfo.nDifficulty),
		Poe2ItemLevel(level + (15 * sgGameInitInfo.nDifficulty)),
		"container",
		pregen,
	};
}

/**
 * @brief The quality an equipment drop comes with: some normal and magic items have a little, and at item
 * level 78 and up a rare few are Exceptional bases with 21-30%.
 */
int RollDropQuality(_item_indexes idx, item_quality rarity, int poe2ItemLevel)
{
	if (QualityKindFor(idx) == QualityKind::None)
		return 0;
	if (poe2ItemLevel >= ExceptionalItemLevel && LootRandom(10000) < ExceptionalChancePer10000)
		return MaxItemQuality + 1 + LootRandom(MaxExceptionalQuality - MaxItemQuality);
	if (IsAnyOf(rarity, ITEM_QUALITY_NORMAL, ITEM_QUALITY_MAGIC) && LootRandom(100) < QualityDropPercent)
		return 1 + LootRandom(MaxDropQuality);
	return 0;
}

void DropPoe2BeltUnique(const Poe2LootOrigin &origin, Point position, bool sendmsg, _item_indexes idx, BeltUniqueId id)
{
	const int ii = AllocateItem();
	Item &item = Items[ii];
	GetSuperItemSpace(position, ii);
	item.dwBuff |= CF_POE2;
	SetupAllItems(*MyPlayer, item, idx, AdvanceRndSeed(), origin.itemLevel, 1, /*onlygood=*/false, origin.pregen, 0, /*forceNotUnique=*/true);
	MakeBeltUnique(item, id);
	SetupItem(item);
	TelemetryItemDropped(item, origin.name);
	if (sendmsg)
		NetSendCmdPItem(false, CMD_DROPITEM, item.position, item);
	if (origin.pregen)
		DeltaAddItem(ii);
}

/** @brief The endgame drop: mostly waystones, some tablets, and in maps now and then a pinnacle fragment. */
std::optional<_item_indexes> PickPoe2EndgameItem(const Poe2LootOrigin &origin)
{
	int fragmentWeight = origin.poe2ItemLevel >= MapAreaLevelBase + 1 ? 10 : 0;
	if (MyPlayer != nullptr)
		fragmentWeight = fragmentWeight * AtlasFragmentPercent(*MyPlayer) / 100;
	const int roll = LootRandom(70 + 20 + fragmentWeight);
	const item_misc_id miscId = roll < 70 ? IMISC_WAYSTONE : roll < 90 ? IMISC_TABLET
	                                                                   : IMISC_MAP_FRAGMENT;
	return PickPoe2Weighted(origin.baseLevel, [miscId](const ItemData &data) { return data.iMiscId == miscId; });
}

void DropPoe2LootItem(const Poe2LootOrigin &origin, Point position, bool sendmsg, LootCategory category, std::optional<item_quality> rarity = std::nullopt, std::optional<_item_indexes> forcedIdx = std::nullopt)
{
	if (ActiveItemCount >= MAXITEMS)
		return;
	const int itemLevel = origin.itemLevel;

	std::optional<_item_indexes> idx = forcedIdx ? forcedIdx : category == LootCategory::Endgame ? PickPoe2EndgameItem(origin)
	                                                                                             : PickPoe2LootItem(category, origin.baseLevel);
	int uidOffset = 0;
	bool bundleUnique = false;
	BeltUniqueId beltUnique = BeltUniqueId::None;
	if (category == LootCategory::Equipment) {
		if (!rarity)
			rarity = RollEquipmentRarity(origin.source, MapLootBonus());
		if (*rarity == ITEM_QUALITY_UNIQUE) {
			if (const std::optional<Poe2UniquePick> unique = PickPoe2Unique(itemLevel, origin.poe2ItemLevel, origin.source); unique) {
				idx = unique->base;
				uidOffset = unique->offset;
				bundleUnique = unique->bundle;
				beltUnique = unique->belt;
			} else {
				rarity = ITEM_QUALITY_RARE;
			}
		}
	}
	if (category == LootCategory::Jewel) {
		// Jewels are always magic or rare: a normal roll makes a magic jewel and a unique roll a rare one.
		const item_quality roll = rarity.value_or(RollEquipmentRarity(origin.source, MapLootBonus()));
		rarity = IsAnyOf(roll, ITEM_QUALITY_RARE, ITEM_QUALITY_UNIQUE) ? ITEM_QUALITY_RARE : ITEM_QUALITY_MAGIC;
	}
	if (!idx)
		return;
	if (beltUnique != BeltUniqueId::None) {
		// A unique flask or charm is made on a plain base, then marked.
		DropPoe2BeltUnique(origin, position, sendmsg, *idx, beltUnique);
		return;
	}

	const int ii = AllocateItem();
	Item &item = Items[ii];
	GetSuperItemSpace(position, ii);
	if (IsAnyOf(category, LootCategory::Equipment, LootCategory::Jewel)) {
		SetLootRarity(item, *rarity);
		SetPoe2ItemLevel(item, origin.poe2ItemLevel);
		item.dwBuff |= (static_cast<uint32_t>(uidOffset) << 1) & CF_UIDOFFSET;
		if (bundleUnique)
			item.dwBuff |= CF_POE2_BUNDLE_UNIQUE;
	}
	if (category == LootCategory::Equipment && *rarity != ITEM_QUALITY_UNIQUE)
		SetItemQuality(item, RollDropQuality(*idx, *rarity, origin.poe2ItemLevel));
	SetupAllItems(*MyPlayer, item, *idx, AdvanceRndSeed(), itemLevel, 1, IsAnyOf(category, LootCategory::Equipment, LootCategory::Jewel), origin.pregen, uidOffset);
	// Diablo's uniques drop once per game; the bundle's, like POE2's, can drop again.
	if (item._iMagical == ITEM_QUALITY_UNIQUE && !gbIsMultiplayer && item._iUid >= 0 && item._iUid < static_cast<int>(std::size(UniqueItemFlags)))
		UniqueItemFlags[item._iUid] = true;
	if (*idx == IDI_GOLD) {
		item._ivalue = std::min(item._ivalue * GoldPileMultiplier, GOLD_MAX_LIMIT);
		SetPlrHandGoldCurs(item);
	}
	if (IsPoe2UncutGem(item._iMiscId))
		SetUncutGemLevel(item, UncutGemLevelForArea(origin.baseLevel));
	if (IsWaystone(item))
		RollWaystone(item, DropWaystoneTier(origin.poe2ItemLevel));
	if (IsMapFragment(item))
		RollFragmentSet(item, origin.poe2ItemLevel);
	SetupItem(item);
	TelemetryItemDropped(item, origin.name);
	if (sendmsg)
		NetSendCmdPItem(false, CMD_DROPITEM, item.position, item);
	if (origin.pregen)
		DeltaAddItem(ii);
}

/** @brief A chest, barrel or sarcophagus item: one loot roll, or a guaranteed magic-or-better item when `onlygood`. */
void DropPoe2ContainerItem(Point position, bool onlygood, bool sendmsg, bool delta)
{
	const Poe2LootOrigin origin = ContainerLootOrigin(delta);
	if (!onlygood) {
		DropPoe2LootItem(origin, position, sendmsg, RollLootCategory(origin.source, MapLootBonus()));
		return;
	}
	// Special chests always hold something good: the rare monster table, with normal results made magic.
	item_quality rarity = RollEquipmentRarity(LootSource::Rare);
	if (rarity == ITEM_QUALITY_NORMAL)
		rarity = ITEM_QUALITY_MAGIC;
	DropPoe2LootItem(origin, position, sendmsg, LootCategory::Equipment, rarity);
}

/** @brief Diablo's "useful" container drop (potions and scrolls) becomes gold, currency or a flask, in the bible's proportions. */
void DropPoe2ContainerUseful(Point position, bool sendmsg)
{
	const auto weights = LootCategoryWeights(LootSource::Normal);
	const std::array<LootCategory, 3> categories { LootCategory::Gold, LootCategory::Currency, LootCategory::FlaskOrCharm };
	int total = 0;
	for (const LootCategory category : categories)
		total += weights[static_cast<size_t>(category)];
	int roll = LootRandom(total);
	for (const LootCategory category : categories) {
		const int weight = weights[static_cast<size_t>(category)];
		if (roll < weight) {
			DropPoe2LootItem(ContainerLootOrigin(false), position, sendmsg, category);
			return;
		}
		roll -= weight;
	}
}

} // namespace

void SpawnPoe2LootRolls(Monster &monster, Point position, bool sendmsg, int rolls)
{
	const Poe2LootOrigin origin = MonsterLootOrigin(monster);
	for (int i = 0; i < rolls; i++)
		DropPoe2LootItem(origin, position, sendmsg, RollLootCategory(origin.source, MapLootBonus()));
}

void SpawnPoe2Loot(Monster &monster, Point position, bool sendmsg)
{
	const Poe2LootOrigin origin = MonsterLootOrigin(monster);
	SpawnPoe2LootRolls(monster, position, sendmsg, RollLootCount(origin.source, MapLootBonus()));
	if (origin.source != LootSource::Boss)
		return;
	// Bosses always drop a rare, often some currency and now and then a unique on top of their rolls.
	DropPoe2LootItem(origin, position, sendmsg, LootCategory::Equipment, ITEM_QUALITY_RARE);
	if (LootRandom(100) < BossBonusCurrencyPercent)
		DropPoe2LootItem(origin, position, sendmsg, LootCategory::Currency);
	if (LootRandom(100) < BossBonusUniquePercent)
		DropPoe2LootItem(origin, position, sendmsg, LootCategory::Equipment, ITEM_QUALITY_UNIQUE);
}

void DropWaystone(Point position, int tier, bool sendmsg)
{
	if (ActiveItemCount >= MAXITEMS)
		return;
	std::optional<_item_indexes> base;
	for (size_t i = 0; i < AllItemsList.size() && !base; i++) {
		if (AllItemsList[i].iMiscId == IMISC_WAYSTONE && IsItemAvailable(static_cast<int>(i)))
			base = static_cast<_item_indexes>(i);
	}
	if (!base)
		return;
	const int ii = AllocateItem();
	Item &item = Items[ii];
	GetSuperItemSpace(position, ii);
	item.dwBuff |= CF_POE2;
	SetupAllItems(*MyPlayer, item, *base, AdvanceRndSeed(), std::min<int>(2 * ItemsGetCurrlevel(), CF_LEVEL), 1, /*onlygood=*/false, /*pregen=*/false);
	RollWaystone(item, tier);
	SetupItem(item);
	TelemetryItemDropped(item, "map boss");
	if (sendmsg)
		NetSendCmdPItem(false, CMD_DROPITEM, item.position, item);
}

namespace {

/** @brief Where map rewards drop from: the map's area level, as a rare monster would. */
Poe2LootOrigin MapRewardOrigin()
{
	Poe2LootOrigin origin = ContainerLootOrigin(false);
	origin.source = LootSource::Rare;
	if (const int tier = ActiveMapTier(); tier > 0)
		origin.poe2ItemLevel = MapAreaLevelBase + tier;
	origin.name = "map reward";
	return origin;
}

std::optional<_item_indexes> PickMiscReward(int level, std::initializer_list<item_misc_id> miscIds)
{
	return PickPoe2Weighted(level, [&miscIds](const ItemData &data) { return std::find(miscIds.begin(), miscIds.end(), data.iMiscId) != miscIds.end(); });
}

/** @brief Equipment of one class: picks bases until one fits. */
std::optional<_item_indexes> PickEquipmentOfClass(int level, item_class itemClass)
{
	for (int tries = 0; tries < 20; tries++) {
		const std::optional<_item_indexes> idx = PickPoe2EquipmentBase(level);
		if (idx && AllItemsList[*idx].iClass == itemClass)
			return idx;
	}
	return std::nullopt;
}

} // namespace

void DropMapReward(Point position, MapReward reward, bool sendmsg, int rarityBonus)
{
	const Poe2LootOrigin origin = MapRewardOrigin();
	const int level = origin.baseLevel;
	LootBonus bonus = MapLootBonus();
	bonus.rarity = bonus.rarity * (100 + rarityBonus) / 100;
	const auto equipment = [&](std::optional<item_quality> rarity, std::optional<_item_indexes> idx = std::nullopt) {
		DropPoe2LootItem(origin, position, sendmsg, LootCategory::Equipment, rarity ? rarity : std::optional<item_quality>(RollEquipmentRarity(origin.source, bonus)), idx);
	};
	const auto misc = [&](std::initializer_list<item_misc_id> miscIds) {
		if (const std::optional<_item_indexes> idx = PickMiscReward(level, miscIds); idx)
			DropPoe2LootItem(origin, position, sendmsg, LootCategory::Misc, std::nullopt, idx);
		else
			DropPoe2LootItem(origin, position, sendmsg, LootCategory::Currency);
	};
	switch (reward) {
	case MapReward::Equipment:
		equipment(std::nullopt);
		break;
	case MapReward::RareEquipment:
		equipment(ITEM_QUALITY_RARE);
		break;
	case MapReward::Unique:
		equipment(ITEM_QUALITY_UNIQUE);
		break;
	case MapReward::Armour:
	case MapReward::RareArmour:
		equipment(reward == MapReward::RareArmour ? std::optional<item_quality>(ITEM_QUALITY_RARE) : std::nullopt, PickEquipmentOfClass(level, ICLASS_ARMOR));
		break;
	case MapReward::Weapon:
	case MapReward::RareWeapon:
		equipment(reward == MapReward::RareWeapon ? std::optional<item_quality>(ITEM_QUALITY_RARE) : std::nullopt, PickEquipmentOfClass(level, ICLASS_WEAPON));
		break;
	case MapReward::Currency:
		DropPoe2LootItem(origin, position, sendmsg, LootCategory::Currency);
		break;
	case MapReward::PremiumCurrency:
		misc({ IMISC_ORB_DIVINE, IMISC_ORB_EXALTED, IMISC_ORB_GREATER_EXALTED, IMISC_ORB_PERFECT_EXALTED, IMISC_ORB_GREATER_CHAOS, IMISC_ORB_PERFECT_CHAOS });
		break;
	case MapReward::Gold:
		DropPoe2LootItem(origin, position, sendmsg, LootCategory::Gold);
		break;
	case MapReward::Gem:
		DropPoe2LootItem(origin, position, sendmsg, LootCategory::Gem);
		break;
	case MapReward::Essence:
		misc({ IMISC_ESSENCE });
		break;
	case MapReward::Omen:
		misc({ IMISC_OMEN });
		break;
	case MapReward::Augment:
		misc({ IMISC_AUGMENT });
		break;
	case MapReward::Catalyst:
		misc({ IMISC_ORB_CATALYST });
		break;
	case MapReward::QualityCurrency:
		misc({ IMISC_ORB_WHETSTONE, IMISC_ORB_ETCHER, IMISC_ORB_SCRAP, IMISC_ORB_BAUBLE });
		break;
	case MapReward::Corruption:
		misc({ IMISC_ORB_VAAL });
		break;
	case MapReward::Desecration:
		misc({ IMISC_ORB_DESECRATE });
		break;
	case MapReward::Waystone:
		DropWaystone(position, DropWaystoneTier(origin.poe2ItemLevel), sendmsg);
		break;
	case MapReward::Tablet:
		misc({ IMISC_TABLET });
		break;
	case MapReward::Fragment:
		misc({ IMISC_MAP_FRAGMENT });
		break;
	default:
		DropPoe2LootItem(origin, position, sendmsg, RollLootCategory(origin.source, bonus));
		break;
	}
}

bool MakeGambleItem(const Player &player, Item &item, GambleFamily family, int areaLevel)
{
	const int itemLevel = std::clamp(areaLevel, 1, static_cast<int>(CF_LEVEL));
	const int poe2ItemLevel = Poe2ItemLevel(areaLevel);
	item_quality rarity = RollGambleRarity();
	std::optional<_item_indexes> idx;
	int uidOffset = 0;
	bool bundleUnique = false;
	if (rarity == ITEM_QUALITY_UNIQUE) {
		for (int tries = 0; tries < 40 && !idx; tries++) {
			const std::optional<Poe2UniquePick> unique = PickPoe2Unique(itemLevel, poe2ItemLevel, LootSource::Rare);
			if (!unique)
				break;
			if (unique->belt != BeltUniqueId::None || !GambleFamilyHas(family, AllItemsList[unique->base].itype))
				continue;
			idx = unique->base;
			uidOffset = unique->offset;
			bundleUnique = unique->bundle;
		}
		if (!idx)
			rarity = ITEM_QUALITY_RARE;
	}
	if (!idx)
		idx = PickPoe2Weighted(areaLevel, [family](const ItemData &data) { return IsPoe2Equipment(data) && GambleFamilyHas(family, data.itype); });
	if (!idx)
		return false;
	item = {};
	SetLootRarity(item, rarity);
	SetPoe2ItemLevel(item, poe2ItemLevel);
	item.dwBuff |= (static_cast<uint32_t>(uidOffset) << 1) & CF_UIDOFFSET;
	if (bundleUnique)
		item.dwBuff |= CF_POE2_BUNDLE_UNIQUE;
	if (rarity != ITEM_QUALITY_UNIQUE)
		SetItemQuality(item, RollDropQuality(*idx, rarity, poe2ItemLevel));
	SetupAllItems(player, item, *idx, AdvanceRndSeed(), itemLevel, 1, /*onlygood=*/true, /*pregen=*/false, uidOffset);
	if (item._iMagical == ITEM_QUALITY_UNIQUE && !gbIsMultiplayer && item._iUid >= 0 && item._iUid < static_cast<int>(std::size(UniqueItemFlags)))
		UniqueItemFlags[item._iUid] = true;
	// The gambler hides what it is until it's bought.
	item._iIdentified = true;
	TelemetryItemDropped(item, "gambler");
	return true;
}

void GiveCurrencyItem(Player &player, item_misc_id miscId)
{
	std::optional<_item_indexes> idx;
	for (size_t i = 0; i < AllItemsList.size() && !idx; i++) {
		if (AllItemsList[i].iMiscId == miscId && IsItemAvailable(static_cast<int>(i)))
			idx = static_cast<_item_indexes>(i);
	}
	if (!idx)
		return;
	Item item {};
	SetupAllItems(player, item, *idx, AdvanceRndSeed(), 1, 1, /*onlygood=*/false, /*pregen=*/false);
	item._iIdentified = true;
	TelemetryItemDropped(item, "salvage");
	if (AutoPlaceItemInInventory(player, item))
		return;
	// No room: it lands at the hero's feet.
	if (ActiveItemCount >= MAXITEMS)
		return;
	const int ii = AllocateItem();
	Items[ii] = item;
	GetSuperItemSpace(player.position.tile, ii);
	SetupItem(Items[ii]);
}

void DropBossUnique(Monster &monster, bool sendmsg)
{
	const BossLootTable *table = gbIsPoe2 ? BossLootTableFor(monster) : nullptr;
	if (table == nullptr || ActiveItemCount >= MAXITEMS || LootRandom(100) >= table->chancePercent)
		return;
	int total = 0;
	for (const BossUniqueEntry &entry : table->pool)
		total += entry.weight;
	if (total <= 0)
		return;
	int roll = LootRandom(total);
	std::string_view name;
	for (const BossUniqueEntry &entry : table->pool) {
		if (roll < entry.weight) {
			name = entry.unique;
			break;
		}
		roll -= entry.weight;
	}
	const Poe2LootOrigin origin = MonsterLootOrigin(monster);
	const Point position = monster.position.tile;
	if (const BeltUnique *belt = FindBeltUnique(name); belt != nullptr) {
		if (const std::optional<int> base = BeltUniqueBase(*belt); base)
			DropPoe2BeltUnique(origin, position, sendmsg, static_cast<_item_indexes>(*base), belt->id);
		return;
	}
	const Poe2Unique *unique = FindPoe2Unique(name);
	Item created;
	if (unique == nullptr || !CreatePoe2Unique(*MyPlayer, created, unique->uid, AdvanceRndSeed()))
		return;
	const int ii = AllocateItem();
	Item &item = Items[ii];
	item = std::move(created);
	GetSuperItemSpace(position, ii);
	SetupItem(item);
	TelemetryItemDropped(item, origin.name);
	if (sendmsg)
		NetSendCmdPItem(false, CMD_DROPITEM, item.position, item);
}

void SpawnItem(Monster &monster, Point position, bool sendmsg, bool spawn /*= false*/)
{
	_item_indexes idx;
	bool onlygood = true;

	const bool dropsSpecialTreasure = (monster.data().treasure & T_UNIQ) != 0;
	const bool dropBrain = Quests[Q_MUSHROOM]._qactive == QUEST_ACTIVE && Quests[Q_MUSHROOM]._qvar1 == QS_MUSHGIVEN;

	// POE2 Mashup: loot rolls by monster toughness. Quest items still drop as before, on top of the rolls.
	if (gbIsPoe2 && !spawn) {
		if (dropsSpecialTreasure && !UseMultiplayerQuests()) {
			Item *uniqueItem = SpawnUnique(static_cast<_unique_items>(monster.data().treasure & T_MASK), position, std::nullopt, false);
			if (uniqueItem != nullptr)
				TelemetryItemDropped(*uniqueItem, monster.name());
			if (uniqueItem != nullptr && sendmsg)
				NetSendCmdPItem(false, CMD_DROPITEM, uniqueItem->position, *uniqueItem);
		} else if (dropBrain && !monster.isUnique()) {
			Quests[Q_MUSHROOM]._qvar1 = QS_BRAINSPAWNED;
			NetSendCmdQuest(true, Quests[Q_MUSHROOM]);
			SpawnQuestItem(IDI_BRAIN, GetSuperItemLoc(position), 0, SelectionRegion::None, sendmsg);
		}
		if ((monster.data().treasure & T_NODROP) != 0 && !monster.isUnique())
			return;
		SpawnPoe2Loot(monster, position, sendmsg);
		return;
	}

	if (dropsSpecialTreasure && !UseMultiplayerQuests()) {
		Item *uniqueItem = SpawnUnique(static_cast<_unique_items>(monster.data().treasure & T_MASK), position, std::nullopt, false);
		if (uniqueItem != nullptr)
			TelemetryItemDropped(*uniqueItem, monster.name());
		if (uniqueItem != nullptr && sendmsg)
			NetSendCmdPItem(false, CMD_DROPITEM, uniqueItem->position, *uniqueItem);
		return;
	}
	if (monster.isUnique() || dropsSpecialTreasure) {
		// Unique monster is killed => use better item base (for example no gold)
		idx = RndUItem(&monster);
	} else if (dropBrain && !gbIsMultiplayer) {
		// Normal monster is killed => need to drop brain to progress the quest
		Quests[Q_MUSHROOM]._qvar1 = QS_BRAINSPAWNED;
		NetSendCmdQuest(true, Quests[Q_MUSHROOM]);
		// brain replaces normal drop
		idx = IDI_BRAIN;
	} else {
		if (dropBrain && gbIsMultiplayer && sendmsg) {
			Quests[Q_MUSHROOM]._qvar1 = QS_BRAINSPAWNED;
			NetSendCmdQuest(true, Quests[Q_MUSHROOM]);
			// Drop the brain as extra item to ensure that all clients see the brain drop
			// When executing SpawnItem is not reliable, because another client can already have the quest state updated before SpawnItem is executed
			const Point posBrain = GetSuperItemLoc(position);
			SpawnQuestItem(IDI_BRAIN, posBrain, 0, SelectionRegion::None, true);
		}
		// Normal monster
		if ((monster.data().treasure & T_NODROP) != 0)
			return;
		onlygood = false;
		idx = RndItemForMonsterLevel(static_cast<int8_t>(monster.level(sgGameInitInfo.nDifficulty)));
	}

	if (idx == IDI_NONE)
		return;

	if (ActiveItemCount >= MAXITEMS)
		return;

	const int ii = AllocateItem();
	auto &item = Items[ii];
	GetSuperItemSpace(position, ii);
	const int uper = monster.isUnique() ? 15 : 1;

	const int8_t mLevel = monster.data().level;
	SetupAllItems(*MyPlayer, item, idx, AdvanceRndSeed(), mLevel, uper, onlygood, false);
	TryRandomUniqueItem(item, idx, mLevel, uper, onlygood, false);
	SetupItem(item);
	TelemetryItemDropped(item, monster.name());

	if (sendmsg)
		NetSendCmdPItem(false, CMD_DROPITEM, item.position, item);
	if (spawn)
		NetSendCmdPItem(false, CMD_SPAWNITEM, item.position, item);
}

void CreateRndItem(Point position, bool onlygood, bool sendmsg, bool delta)
{
	if (gbIsPoe2 && !gbIsSpawn) {
		DropPoe2ContainerItem(position, onlygood, sendmsg, delta);
		return;
	}
	const _item_indexes idx = onlygood ? RndUItem(nullptr) : RndAllItems();

	SetupBaseItem(position, idx, onlygood, sendmsg, delta);
}

void CreateRndUseful(Point position, bool sendmsg)
{
	if (gbIsPoe2 && !gbIsSpawn) {
		DropPoe2ContainerUseful(position, sendmsg);
		return;
	}
	if (ActiveItemCount >= MAXITEMS)
		return;

	const int ii = AllocateItem();
	auto &item = Items[ii];
	GetSuperItemSpace(position, ii);
	const int curlv = ItemsGetCurrlevel();

	SetupAllUseful(item, AdvanceRndSeed(), curlv);
	if (sendmsg)
		NetSendCmdPItem(false, CMD_DROPITEM, item.position, item);
}

void CreateTypeItem(Point position, bool onlygood, ItemType itemType, int imisc, bool sendmsg, bool delta, bool spawn)
{
	_item_indexes idx;

	const int curlv = ItemsGetCurrlevel();
	// Bookcases and book stands hold scrolls now that skill gems replaced spell books.
	if (gbIsPoe2 && itemType == ItemType::Misc && imisc == IMISC_BOOK)
		imisc = IMISC_SCROLL;
	if (itemType != ItemType::Gold)
		idx = RndTypeItems(itemType, imisc, curlv);
	else
		idx = IDI_GOLD;

	SetupBaseItem(position, idx, onlygood, sendmsg, delta, spawn);
}

void SpawnBaseItem(Point position, _item_indexes idx, bool sendmsg)
{
	SetupBaseItem(position, idx, false, sendmsg, false);
}

namespace {

void RecreateItemBase(const Player &player, Item &item, _item_indexes idx, uint16_t icreateinfo, uint32_t iseed, int ivalue, uint32_t dwBuff, const Poe2Craft &craft)
{
	// Generate with the mod state the item was created with, like gbIsHellfire below.
	struct Poe2StateGuard {
		bool saved = gbIsPoe2;
		~Poe2StateGuard() { gbIsPoe2 = saved; }
	} poe2StateGuard;
	gbIsPoe2 = (dwBuff & CF_POE2) != 0;

	const bool tmpIsHellfire = gbIsHellfire;
	item.dwBuff = dwBuff;
	// POE2 Mashup: crafting state, which generation reads like dwBuff.
	item._iPoe2Craft = craft;
	gbIsHellfire = (item.dwBuff & CF_HELLFIRE) != 0;

	if (idx == IDI_GOLD) {
		InitializeItem(item, IDI_GOLD);
		item._iSeed = iseed;
		item._iCreateInfo = icreateinfo;
		item._ivalue = ivalue;
		SetPlrHandGoldCurs(item);
		gbIsHellfire = tmpIsHellfire;
		return;
	}

	if (icreateinfo == 0) {
		InitializeItem(item, idx);
		item._iSeed = iseed;
		gbIsHellfire = tmpIsHellfire;
		return;
	}

	if ((icreateinfo & CF_UNIQUE) == 0) {
		if ((icreateinfo & CF_TOWN) != 0) {
			RecreateTownItem(player, item, idx, icreateinfo, iseed);
			gbIsHellfire = tmpIsHellfire;
			return;
		}

		if ((icreateinfo & CF_USEFUL) == CF_USEFUL) {
			SetupAllUseful(item, iseed, icreateinfo & CF_LEVEL);
			gbIsHellfire = tmpIsHellfire;
			return;
		}
	}

	const int level = icreateinfo & CF_LEVEL;

	int uper = 0;
	if ((icreateinfo & CF_UPER1) != 0)
		uper = 1;
	if ((icreateinfo & CF_UPER15) != 0)
		uper = 15;

	const bool onlygood = (icreateinfo & CF_ONLYGOOD) != 0;
	const bool forceNotUnique = (icreateinfo & CF_UNIQUE) == 0;
	const bool pregen = (icreateinfo & CF_PREGEN) != 0;
	auto uidOffset = static_cast<int>((item.dwBuff & CF_UIDOFFSET) >> 1);

	SetupAllItems(player, item, idx, iseed, level, uper, onlygood, pregen, uidOffset, forceNotUnique);
	// POE2 Mashup: augments and corruption enchantments come on top of the item's roll.
	if (!item._iPoe2Craft.isEmpty()) {
		ApplyAugments(item);
		ApplyCorruptionEnchant(item);
	}
	SetupItem(item);
	gbIsHellfire = tmpIsHellfire;
}

} // namespace

void RecreateItem(const Player &player, Item &item, _item_indexes idx, uint16_t icreateinfo, uint32_t iseed, int ivalue, uint32_t dwBuff, const Poe2Craft &craft)
{
	RecreateItemBase(player, item, idx, icreateinfo, iseed, ivalue, dwBuff, craft);
	// POE2 Mashup: a flask keeps the mods and quality it was given.
	if ((dwBuff & CF_POE2_FLASK_ROLLED) != 0) {
		constexpr uint32_t FlaskBits = CF_POE2_FLASK_PREFIX | CF_POE2_FLASK_SUFFIX | CF_POE2_FLASK_QUALITY | CF_POE2_FLASK_ROLLED;
		item.dwBuff = (item.dwBuff & ~FlaskBits) | (dwBuff & FlaskBits);
	}
	// And a unique flask or charm stays that unique.
	if (IsFlaskPotion(item) || IsCharm(item))
		item.dwBuff = (item.dwBuff & ~CF_POE2_BELT_UNIQUE) | (dwBuff & CF_POE2_BELT_UNIQUE);
	FinishFlask(item);
	// And an essence, omen, augment or catalyst stays the one it was.
	if (IsCraftingVariantItem(item)) {
		item.dwBuff = (item.dwBuff & ~CF_POE2_VARIANT) | (dwBuff & CF_POE2_VARIANT);
		FinishCraftingItem(item);
	}
	// POE2 Mashup: and a skill gem keeps its level, and a support gem its tier.
	if (IsSkillGem(item))
		item.dwBuff = (item.dwBuff & ~CF_POE2_GEM_LEVEL) | (dwBuff & CF_POE2_GEM_LEVEL);
	if (SupportGemForItem(item) != SupportGem::None && (dwBuff & CF_POE2_GEM_LEVEL) != 0) {
		constexpr int Shift = std::countr_zero(static_cast<uint32_t>(CF_POE2_GEM_LEVEL));
		SetSupportGemItemTier(item, static_cast<int>((dwBuff & CF_POE2_GEM_LEVEL) >> Shift));
	}
	// And a tablet, fragment or key stays the one it was.
	if (IsEndgameItem(item)) {
		item.dwBuff = (item.dwBuff & ~CF_POE2_VARIANT) | (dwBuff & CF_POE2_VARIANT);
		item._iPoe2Craft = craft;
		FinishEndgameItem(item);
	}
	// And a waystone keeps its tier and modifiers.
	if (IsWaystone(item)) {
		item._iPoe2Craft = craft;
		FinishWaystone(item);
	}
}

void RecreateEar(Item &item, uint16_t ic, uint32_t iseed, uint8_t bCursval, std::string_view heroName)
{
	InitializeItem(item, IDI_EAR);

	const std::string itemName = FormatRuntime("Ear of {:s}", heroName);

	CopyUtf8(item._iName, itemName, ItemNameLength);
	CopyUtf8(item._iIName, heroName, ItemNameLength);

	item._iCurs = ((bCursval >> 6) & 3) + ICURS_EAR_SORCERER;
	item._ivalue = bCursval & 0x3F;
	item._iCreateInfo = ic;
	item._iSeed = iseed;
}

void CornerstoneSave()
{
	if (!CornerStone.activated)
		return;
	if (!CornerStone.item.isEmpty()) {
		ItemPack id;
		PackItem(id, CornerStone.item, (CornerStone.item.dwBuff & CF_HELLFIRE) != 0);
		const auto *buffer = reinterpret_cast<uint8_t *>(&id);
		for (size_t i = 0; i < sizeof(ItemPack); i++) {
			BufCopy(&GetOptions().Hellfire.szItem[i * 2], AsHexPad2(buffer[i], /*uppercase=*/true));
		}
		GetOptions().Hellfire.szItem[sizeof(GetOptions().Hellfire.szItem) - 1] = '\0';
	} else {
		GetOptions().Hellfire.szItem[0] = '\0';
	}
}

void CornerstoneLoad(Point position)
{
	ItemPack pkSItem;

	if (CornerStone.activated || position.x == 0 || position.y == 0) {
		return;
	}

	CornerStone.item.clear();
	CornerStone.activated = true;
	if (dItem[position.x][position.y] != 0) {
		const int ii = dItem[position.x][position.y] - 1;
		for (int i = 0; i < ActiveItemCount; i++) {
			if (ActiveItems[i] == ii) {
				DeleteItem(i);
				break;
			}
		}
		dItem[position.x][position.y] = 0;
	}

	if (strlen(GetOptions().Hellfire.szItem) < sizeof(ItemPack) * 2)
		return;

	Hex2bin(GetOptions().Hellfire.szItem, sizeof(ItemPack), reinterpret_cast<uint8_t *>(&pkSItem));

	const int ii = AllocateItem();
	auto &item = Items[ii];

	dItem[position.x][position.y] = static_cast<int8_t>(ii + 1);

	UnPackItem(pkSItem, *MyPlayer, item, (pkSItem.dwBuff & CF_HELLFIRE) != 0);
	item.position = position;
	RespawnItem(item, false);
	CornerStone.item = item;
}

void SpawnQuestItem(_item_indexes itemid, Point position, int randarea, SelectionRegion selectionRegion, bool sendmsg)
{
	if (randarea > 0) {
		int tries = 0;
		while (true) {
			tries++;
			if (tries > 1000 && randarea > 1)
				randarea--;

			position.x = GenerateRnd(MAXDUNX);
			position.y = GenerateRnd(MAXDUNY);

			bool failed = false;
			for (int i = 0; i < randarea && !failed; i++) {
				for (int j = 0; j < randarea && !failed; j++) {
					failed = !ItemSpaceOk(position + Displacement { i, j });
				}
			}
			if (!failed)
				break;
		}
	}

	if (ActiveItemCount >= MAXITEMS)
		return;

	const int ii = AllocateItem();
	auto &item = Items[ii];

	item.position = position;

	dItem[position.x][position.y] = ii + 1;

	const int curlv = ItemsGetCurrlevel();
	GetItemAttrs(item, itemid, curlv);

	SetupItem(item);
	item._iSeed = AdvanceRndSeed();
	SetRndSeed(item._iSeed);
	item._iPostDraw = true;
	if (selectionRegion != SelectionRegion::None) {
		item.selectionRegion = selectionRegion;
		item.AnimInfo.currentFrame = item.AnimInfo.numberOfFrames - 1;
		item._iAnimFlag = false;
	}

	if (sendmsg)
		NetSendCmdPItem(true, CMD_SPAWNITEM, item.position, item);
	else {
		item._iCreateInfo |= CF_PREGEN;
		DeltaAddItem(ii);
	}
}

void SpawnRewardItem(_item_indexes itemid, Point position, bool sendmsg)
{
	if (ActiveItemCount >= MAXITEMS)
		return;

	const int ii = AllocateItem();
	auto &item = Items[ii];

	item.position = position;
	dItem[position.x][position.y] = ii + 1;
	const int curlv = ItemsGetCurrlevel();
	GetItemAttrs(item, itemid, curlv);
	item.setNewAnimation(true);
	item.selectionRegion = SelectionRegion::Middle;
	item._iPostDraw = true;
	item._iIdentified = true;
	GenerateNewSeed(item);

	if (sendmsg) {
		NetSendCmdPItem(true, CMD_SPAWNITEM, item.position, item);
	}
}

void SpawnMapOfDoom(Point position, bool sendmsg)
{
	SpawnRewardItem(IDI_MAPOFDOOM, position, sendmsg);
}

void SpawnRuneBomb(Point position, bool sendmsg)
{
	SpawnRewardItem(IDI_RUNEBOMB, position, sendmsg);
}

void SpawnTheodore(Point position, bool sendmsg)
{
	SpawnRewardItem(IDI_THEODORE, position, sendmsg);
}

void RespawnItem(Item &item, bool flipFlag)
{
	const int it = ItemCAnimTbl[item._iCurs];
	item.setNewAnimation(flipFlag);
	item._iRequest = false; // Item isn't being picked up by a player

	switch (item._iCurs) {
	case ICURS_TAVERN_SIGN:
	case ICURS_ANVIL_OF_FURY:
		item.selectionRegion = SelectionRegion::Bottom;
		break;
	case ICURS_MAP_OF_THE_STARS:
	case ICURS_RUNE_BOMB:
	case ICURS_THEODORE:
	case ICURS_AURIC_AMULET:
		item.selectionRegion = SelectionRegion::Middle; // Item is selectable at elevated level
		break;
	case ICURS_MAGIC_ROCK:
		Object *stand = FindObjectAtPosition(item.position);
		if (stand != nullptr && stand->_otype == OBJ_STAND) {
			item.selectionRegion = SelectionRegion::Middle; // Item is selectable at elevated level and renders at elevated level
			item._iPostDraw = true;                         // Draw in front of stand
			item.AnimInfo.currentFrame = 10;                // Frame 10 is the start of the elevated frames in the cel
		} else {
			item.selectionRegion = SelectionRegion::Bottom; // Item is selectable at floor level and renders at floor level
		}
		PlaySfxLoc(ItemDropSnds[it], item.position); // Play the drop sound (this item is perpetually in a dropping state, but can always be picked up)
		break;
	}
}

void DeleteItem(int i)
{
	if (ActiveItemCount > 0)
		ActiveItemCount--;

	assert(i >= 0 && i < MAXITEMS && ActiveItemCount < MAXITEMS);

	if (pcursitem == ActiveItems[i]) // Unselect item if player has it highlighted
		pcursitem = -1;

	if (i < ActiveItemCount) {
		// If the deleted item was not already at the end of the active list, swap the indexes around to make the next item allocation simpler.
		std::swap(ActiveItems[i], ActiveItems[ActiveItemCount]);
	}
}

void ProcessItems()
{
	for (int i = 0; i < ActiveItemCount; i++) {
		const int ii = ActiveItems[i];
		auto &item = Items[ii];
		if (!item._iAnimFlag)
			continue;
		item.AnimInfo.processAnimation();
		if (item._iCurs == ICURS_MAGIC_ROCK) {
			if (item.selectionRegion == SelectionRegion::Bottom && item.AnimInfo.currentFrame == 10) // Reached end of floor frames + 1, cycle back
				item.AnimInfo.currentFrame = 0;                                                      // Beginning of floor frames
			if (item.selectionRegion == SelectionRegion::Middle && item.AnimInfo.currentFrame == 19) // Reached end of elevated frames, cycle back
				item.AnimInfo.currentFrame = 10;                                                     // Beginning of elevated frames
		} else {
			if (item.AnimInfo.currentFrame == (item.AnimInfo.numberOfFrames - 1) / 2)
				PlaySfxLoc(ItemDropSnds[ItemCAnimTbl[item._iCurs]], item.position);

			if (item.AnimInfo.isLastFrame()) {
				item.AnimInfo.currentFrame = item.AnimInfo.numberOfFrames - 1;
				item._iAnimFlag = false;
				item.selectionRegion = SelectionRegion::Bottom;
			}
		}
	}
	ItemDoppel();
}

void FreeItemGFX()
{
	for (auto &itemanim : itemanims) {
		itemanim = std::nullopt;
	}
}

void GetItemFrm(Item &item)
{
	const int it = ItemCAnimTbl[item._iCurs];
	if (itemanims[it])
		item.AnimInfo.sprites.emplace(*itemanims[it]);
}

void GetItemStr(Item &item)
{
	if (item._itype != ItemType::Gold) {
		InfoString = item.getName();
		InfoColor = item.getTextColor();
	} else {
		const int nGold = item._ivalue;
		InfoString = FormatRuntime(ngettext("{:s} gold piece", "{:s} gold pieces", nGold), FormatInteger(nGold));
	}
}

void CheckIdentify(Player &player, int cii)
{
	Item *pi;

	if (cii >= NUM_INVLOC)
		pi = &player.InvList[cii - NUM_INVLOC];
	else
		pi = &player.InvBody[cii];

	pi->_iIdentified = true;
	CalcPlrInv(player, true);
}

void IdentifyWithScroll(Player &player)
{
	Item *target = nullptr;
	bool inStash = false;
	if (pcursinvitem != -1 && !IsInspectingPlayer() && pcursinvitem <= INVITEM_INV_LAST)
		target = pcursinvitem >= NUM_INVLOC ? &player.InvList[pcursinvitem - NUM_INVLOC] : &player.InvBody[pcursinvitem];
	else if (pcursstashitem != StashStruct::EmptyCell) {
		target = &Stash.stashList[pcursstashitem];
		inStash = true;
	}
	if (target == nullptr || target->isEmpty())
		return;
	if (target->_iIdentified) {
		EventPlrMsg(_("That item is already identified."), UiFlags::ColorRed);
		return;
	}
	// Find the scroll before identifying: removing an inventory item moves the others.
	int scroll = -1;
	bool belt = false;
	for (int i = 0; i < player._pNumInv && scroll < 0; i++) {
		if (IsIdentifyScroll(player.InvList[i]))
			scroll = i;
	}
	for (int i = 0; i < MaxBeltItems && scroll < 0; i++) {
		if (IsIdentifyScroll(player.SpdList[i])) {
			scroll = i;
			belt = true;
		}
	}
	if (scroll < 0)
		return;
	target->_iIdentified = true;
	if (inStash)
		Stash.dirty = true;
	if (belt) {
		if (!TakeOneFromStack(player.SpdList[scroll]))
			player.RemoveSpdBarItem(scroll);
	} else if (TakeOneFromStack(player.InvList[scroll])) {
		NetSyncInvItem(player, scroll);
	} else {
		player.RemoveInvItem(scroll);
	}
	PlaySFX(SfxID::ItemScroll);
	CalcPlrInv(player, true);
}

void DoRepair(Player &player, int cii)
{
	Item *pi;

	PlaySfxLoc(SfxID::SpellRepair, player.position.tile);

	if (cii >= NUM_INVLOC) {
		pi = &player.InvList[cii - NUM_INVLOC];
	} else {
		pi = &player.InvBody[cii];
	}

	RepairItem(*pi, player.getCharacterLevel());
	CalcPlrInv(player, true);
}

void DoRecharge(Player &player, int cii)
{
	Item *pi;

	if (cii >= NUM_INVLOC) {
		pi = &player.InvList[cii - NUM_INVLOC];
	} else {
		pi = &player.InvBody[cii];
	}

	RechargeItem(*pi, player);
	CalcPlrInv(player, true);
}

namespace {
int HeldCurrencyMisc = -1;
int HeldCurrencySeed = 0;
uint32_t HeldCurrencyVariant = 0;
/** The kind last used up, so Shift can pick up the next one of it. */
int SpentCurrencyMisc = -1;
uint32_t SpentCurrencyVariant = 0;
bool StickyUse = false;
} // namespace

void SetStickyItemUse(bool sticky)
{
	StickyUse = sticky;
}

bool WantsStickyItemUse()
{
	return gbIsPoe2 && (StickyUse || (SDL_GetModState() & SDL_KMOD_SHIFT) != 0);
}

bool KeepCurrencyOnCursor(Player &player)
{
	if (!WantsStickyItemUse() || SpentCurrencyMisc < 0)
		return false;
	for (int i = 0; i < player._pNumInv; i++) {
		const Item &stack = player.InvList[i];
		if (stack._iMiscId == SpentCurrencyMisc && (stack.dwBuff & CF_POE2_VARIANT) == SpentCurrencyVariant) {
			HoldCurrencyForUse(stack);
			return true;
		}
	}
	if (IsStashOpen) {
		for (const Item &stack : Stash.stashList) {
			if (stack._iMiscId == SpentCurrencyMisc && (stack.dwBuff & CF_POE2_VARIANT) == SpentCurrencyVariant) {
				HoldCurrencyForUse(stack);
				return true;
			}
		}
	}
	return false;
}

bool HasIdentifyScroll(const Player &player)
{
	for (int i = 0; i < player._pNumInv; i++) {
		if (IsIdentifyScroll(player.InvList[i]))
			return true;
	}
	for (const Item &item : player.SpdList) {
		if (IsIdentifyScroll(item))
			return true;
	}
	return false;
}

void HoldCurrencyForUse(const Item &item)
{
	HeldCurrencyMisc = item._iMiscId;
	HeldCurrencySeed = item._iSeed;
	HeldCurrencyVariant = item.dwBuff & CF_POE2_VARIANT;
}

bool HeldCurrencyFits(const Item &item)
{
	// A Scroll of Identify on the cursor picks out what it can identify.
	if (gbIsPoe2 && pcurs == CURSOR_IDENTIFY)
		return !item.isEmpty() && !item._iIdentified && item._iMagical != ITEM_QUALITY_NORMAL;
	if (!gbIsPoe2 || HeldCurrencyMisc < 0 || pcurs != CURSOR_OIL)
		return false;
	return CurrencyFitsItem(CurrencyOrbForMiscId(HeldCurrencyMisc), item);
}

void SpendHeldCurrency(Player &player)
{
	if (HeldCurrencyMisc < 0)
		return;
	// The very stack it came from if it's still there, else any stack of the same kind.
	for (const bool exact : { true, false }) {
		for (int i = 0; i < player._pNumInv; i++) {
			Item &stack = player.InvList[i];
			if (stack._iMiscId != HeldCurrencyMisc || (stack.dwBuff & CF_POE2_VARIANT) != HeldCurrencyVariant || (exact && stack._iSeed != HeldCurrencySeed))
				continue;
			SpentCurrencyMisc = HeldCurrencyMisc;
			SpentCurrencyVariant = HeldCurrencyVariant;
			if (TakeOneFromStack(stack)) {
				if (&player == MyPlayer)
					NetSyncInvItem(player, i);
			} else {
				player.RemoveInvItem(i);
			}
			HeldCurrencyMisc = -1;
			return;
		}
		// Used from the stash: one comes off its stack there.
		if (&player != MyPlayer)
			continue;
		for (size_t i = 0; i < Stash.stashList.size(); i++) {
			Item &stack = Stash.stashList[i];
			if (stack._iMiscId != HeldCurrencyMisc || (stack.dwBuff & CF_POE2_VARIANT) != HeldCurrencyVariant || (exact && stack._iSeed != HeldCurrencySeed))
				continue;
			SpentCurrencyMisc = HeldCurrencyMisc;
			SpentCurrencyVariant = HeldCurrencyVariant;
			if (const int count = StoredStackCount(stack); count > 1)
				SetStoredStackCount(stack, count - 1);
			else
				Stash.RemoveStashItem(static_cast<StashStruct::StashCell>(i));
			Stash.dirty = true;
			HeldCurrencyMisc = -1;
			return;
		}
	}
	HeldCurrencyMisc = -1;
}

bool DoOil(Player &player, int cii)
{
	Item *pi;
	if (cii >= INVITEM_BELT_FIRST) {
		// POE2 Mashup: orbs and baubles can be used on flasks in the belt.
		pi = &player.SpdList[cii - INVITEM_BELT_FIRST];
	} else if (cii >= NUM_INVLOC) {
		pi = &player.InvList[cii - NUM_INVLOC];
	} else {
		pi = &player.InvBody[cii];
	}
	if (!ApplyOilToItem(*pi, player)) {
		if (DesecrationRevealItem && &player == MyPlayer)
			OpenDesecrationReveal(*pi);
		return false;
	}
	SpendHeldCurrency(player);
	for (const ItemData &data : AllItemsList) {
		if (data.iMiscId == player._pOilType) {
			TelemetryCrafted(player, data.iName, *pi);
			break;
		}
	}
	// POE2 Mashup: a failed Orb of Chance destroys the item.
	if (CurrencyDestroyedItem) {
		CurrencyDestroyedItem = false;
		if (cii >= INVITEM_BELT_FIRST)
			player.RemoveSpdBarItem(cii - INVITEM_BELT_FIRST);
		else if (cii >= NUM_INVLOC)
			player.RemoveInvItem(cii - NUM_INVLOC);
		else
			RemoveEquipment(player, static_cast<inv_body_loc>(cii), false);
	}
	ConsumeCraftOmens(player);
	CalcPlrInv(player, true);
	if (DesecrationRevealItem && &player == MyPlayer)
		OpenDesecrationReveal(*pi);
	return true;
}

[[nodiscard]] StringOrView PrintItemPower(char plidx, const Item &item)
{
	switch (plidx) {
	case IPL_TOHIT:
	case IPL_TOHIT_CURSE:
		return FormatRuntime(_("chance to hit: {:+d}%"), item._iPLToHit);
	case IPL_DAMP:
	case IPL_DAMP_CURSE:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "{:+d}% damage"), item._iPLDam);
	case IPL_TOHIT_DAMP:
	case IPL_TOHIT_DAMP_CURSE:
		return FormatRuntime(_("to hit: {:+d}%, {:+d}% damage"), item._iPLToHit, item._iPLDam);
	case IPL_ACP:
	case IPL_ACP_CURSE:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "{:+d}% armor"), item._iPLAC);
	case IPL_SETAC:
	case IPL_AC_CURSE:
		return FormatRuntime(_("armor class: {:d}"), item._iAC);
	case IPL_FIRERES:
	case IPL_FIRERES_CURSE:
		if (item._iPLFR < MaxResistance)
			return FormatRuntime(_("Resist Fire: {:+d}%"), item._iPLFR);
		else
			return FormatRuntime(_("Resist Fire: {:+d}% MAX"), MaxResistance);
	case IPL_LIGHTRES:
	case IPL_LIGHTRES_CURSE:
		if (item._iPLLR < MaxResistance)
			return FormatRuntime(_("Resist Lightning: {:+d}%"), item._iPLLR);
		else
			return FormatRuntime(_("Resist Lightning: {:+d}% MAX"), MaxResistance);
	case IPL_MAGICRES:
	case IPL_MAGICRES_CURSE:
		if (item._iPLMR < MaxResistance)
			return FormatRuntime(_("Resist Magic: {:+d}%"), item._iPLMR);
		else
			return FormatRuntime(_("Resist Magic: {:+d}% MAX"), MaxResistance);
	case IPL_ALLRES:
		if (item._iPLFR < MaxResistance)
			return FormatRuntime(_("Resist All: {:+d}%"), item._iPLFR);
		else
			return FormatRuntime(_("Resist All: {:+d}% MAX"), MaxResistance);
	case IPL_SPLLVLADD:
		if (item._iSplLvlAdd > 0)
			return FormatRuntime(ngettext("spells are increased {:d} level", "spells are increased {:d} levels", item._iSplLvlAdd), item._iSplLvlAdd);
		else if (item._iSplLvlAdd < 0)
			return FormatRuntime(ngettext("spells are decreased {:d} level", "spells are decreased {:d} levels", -item._iSplLvlAdd), -item._iSplLvlAdd);
		else
			return _("spell levels unchanged (?)");
	case IPL_CHARGES:
		return _("Extra charges");
	case IPL_SPELL:
		return FormatRuntime(ngettext("{:d} {:s} charge", "{:d} {:s} charges", item._iMaxCharges), item._iMaxCharges, pgettext("spell", GetSpellData(item._iSpell).sNameText));
	case IPL_FIREDAM:
		if (item._iFMinDam == item._iFMaxDam)
			return FormatRuntime(_("Fire hit damage: {:d}"), item._iFMinDam);
		else
			return FormatRuntime(_("Fire hit damage: {:d}-{:d}"), item._iFMinDam, item._iFMaxDam);
	case IPL_LIGHTDAM:
		if (item._iLMinDam == item._iLMaxDam)
			return FormatRuntime(_("Lightning hit damage: {:d}"), item._iLMinDam);
		else
			return FormatRuntime(_("Lightning hit damage: {:d}-{:d}"), item._iLMinDam, item._iLMaxDam);
	case IPL_STR:
	case IPL_STR_CURSE:
		return FormatRuntime(_("{:+d} to strength"), item._iPLStr);
	case IPL_MAG:
	case IPL_MAG_CURSE:
		return FormatRuntime(_("{:+d} to magic"), item._iPLMag);
	case IPL_DEX:
	case IPL_DEX_CURSE:
		return FormatRuntime(_("{:+d} to dexterity"), item._iPLDex);
	case IPL_VIT:
	case IPL_VIT_CURSE:
		return FormatRuntime(_("{:+d} to vitality"), item._iPLVit);
	case IPL_ATTRIBS:
	case IPL_ATTRIBS_CURSE:
		return FormatRuntime(_("{:+d} to all attributes"), item._iPLStr);
	case IPL_GETHIT_CURSE:
	case IPL_GETHIT:
		return FormatRuntime(_("{:+d} damage from enemies"), item._iPLGetHit);
	case IPL_LIFE:
	case IPL_LIFE_CURSE:
		return FormatRuntime(_("Hit Points: {:+d}"), item._iPLHP.whole());
	case IPL_MANA:
	case IPL_MANA_CURSE:
		return FormatRuntime(_("Mana: {:+d}"), item._iPLMana.whole());
	case IPL_DUR:
		return _("high durability");
	case IPL_DUR_CURSE:
		return _("decreased durability");
	case IPL_INDESTRUCTIBLE:
		return _("indestructible");
	case IPL_LIGHT:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "+{:d}% light radius"), 10 * item._iPLLight);
	case IPL_LIGHT_CURSE:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "-{:d}% light radius"), -10 * item._iPLLight);
	case IPL_MULT_ARROWS:
		return _("multiple arrows per shot");
	case IPL_FIRE_ARROWS:
		if (item._iFMinDam == item._iFMaxDam)
			return FormatRuntime(_("fire arrows damage: {:d}"), item._iFMinDam);
		else
			return FormatRuntime(_("fire arrows damage: {:d}-{:d}"), item._iFMinDam, item._iFMaxDam);
	case IPL_LIGHT_ARROWS:
		if (item._iLMinDam == item._iLMaxDam)
			return FormatRuntime(_("lightning arrows damage {:d}"), item._iLMinDam);
		else
			return FormatRuntime(_("lightning arrows damage {:d}-{:d}"), item._iLMinDam, item._iLMaxDam);
	case IPL_FIREBALL:
		if (item._iFMinDam == item._iFMaxDam)
			return FormatRuntime(_("fireball damage: {:d}"), item._iFMinDam);
		else
			return FormatRuntime(_("fireball damage: {:d}-{:d}"), item._iFMinDam, item._iFMaxDam);
	case IPL_THORNS:
		return _("attacker takes 1-3 damage");
	case IPL_NOMANA:
		return _("user loses all mana");
	case IPL_ABSHALFTRAP:
		return _("absorbs half of trap damage");
	case IPL_KNOCKBACK:
		return _("knocks target back");
	case IPL_3XDAMVDEM:
		return _(/*xgettext:no-c-format*/ "+200% damage vs. demons");
	case IPL_ALLRESZERO:
		return _("All Resistance equals 0");
	case IPL_STEALMANA:
		if (HasAnyOf(item._iFlags, ItemSpecialEffect::StealMana3))
			return _(/*xgettext:no-c-format*/ "hit steals 3% mana");
		if (HasAnyOf(item._iFlags, ItemSpecialEffect::StealMana5))
			return _(/*xgettext:no-c-format*/ "hit steals 5% mana");
		return {};
	case IPL_STEALLIFE:
		if (HasAnyOf(item._iFlags, ItemSpecialEffect::StealLife3))
			return _(/*xgettext:no-c-format*/ "hit steals 3% life");
		if (HasAnyOf(item._iFlags, ItemSpecialEffect::StealLife5))
			return _(/*xgettext:no-c-format*/ "hit steals 5% life");
		return {};
	case IPL_TARGAC:
		return _("penetrates target's armor");
	case IPL_FASTATTACK:
		// Fastest first: an item can carry a base-type speed and an affix speed, and the fastest one applies.
		if (HasAnyOf(item._iFlags, ItemSpecialEffect::FastestAttack))
			return _("fastest attack");
		if (HasAnyOf(item._iFlags, ItemSpecialEffect::FasterAttack))
			return _("faster attack");
		if (HasAnyOf(item._iFlags, ItemSpecialEffect::FastAttack))
			return _("fast attack");
		if (HasAnyOf(item._iFlags, ItemSpecialEffect::QuickAttack))
			return _("quick attack");
		return _("Another ability (NW)");
	case IPL_FASTRECOVER:
		if (HasAnyOf(item._iFlags, ItemSpecialEffect::FastHitRecovery))
			return _("fast hit recovery");
		if (HasAnyOf(item._iFlags, ItemSpecialEffect::FasterHitRecovery))
			return _("faster hit recovery");
		if (HasAnyOf(item._iFlags, ItemSpecialEffect::FastestHitRecovery))
			return _("fastest hit recovery");
		return _("Another ability (NW)");
	case IPL_FASTBLOCK:
		return _("fast block");
	case IPL_DAMMOD:
		return FormatRuntime(ngettext("adds {:d} point to damage", "adds {:d} points to damage", item._iPLDamMod), item._iPLDamMod);
	case IPL_RNDARROWVEL:
		return _("fires random speed arrows");
	case IPL_SETDAM:
		return _("unusual item damage");
	case IPL_SETDUR:
		return _("altered durability");
	case IPL_ONEHAND:
		return _("one handed sword");
	case IPL_DRAINLIFE:
		return _("constantly lose hit points");
	case IPL_RNDSTEALLIFE:
		return _("life stealing");
	case IPL_NOMINSTR:
		return _("no strength requirement");
	case IPL_ADDACLIFE:
		if (item._iFMinDam == item._iFMaxDam)
			return FormatRuntime(_("lightning damage: {:d}"), item._iFMinDam);
		else
			return FormatRuntime(_("lightning damage: {:d}-{:d}"), item._iFMinDam, item._iFMaxDam);
	case IPL_ADDMANAAC:
		return _("charged bolts on hits");
	case IPL_DEVASTATION:
		return _("occasional triple damage");
	case IPL_DECAY:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "decaying {:+d}% damage"), item._iPLDam);
	case IPL_PERIL:
		return _("2x dmg to monst, 1x to you");
	case IPL_JESTERS:
		return std::string(_(/*xgettext:no-c-format*/ "Random 0 - 600% damage"));
	case IPL_CRYSTALLINE:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "low dur, {:+d}% damage"), item._iPLDam);
	case IPL_DOPPELGANGER:
		return FormatRuntime(_("to hit: {:+d}%, {:+d}% damage"), item._iPLToHit, item._iPLDam);
	case IPL_ACDEMON:
		return _("extra AC vs demons");
	case IPL_ACUNDEAD:
		return _("extra AC vs undead");
	case IPL_MANATOLIFE:
		return _("50% Mana moved to Health");
	case IPL_LIFETOMANA:
		return _("40% Health moved to Mana");
	case IPL_LIFEREGEN:
		return FormatRuntime(_("regenerate {:d} life per second"), item._iPLLifeRegen);
	case IPL_MANAREGEN:
		return FormatRuntime(_(/*xgettext:no-c-format*/ "{:d}% increased mana regeneration"), item._iPLManaRegen);
	default:
		return _("Another ability (NW)");
	}
}

void DrawUniqueInfo(const Surface &out)
{
	const Point position = DrawUniqueInfoWindow(out);

	Rectangle rect { position + Displacement { 32, 56 }, { 257, 0 } };
	const UniqueItem &uitem = UniqueItems[curruitem._iUid];
	DrawString(out, _(uitem.UIName), rect, { .flags = UiFlags::AlignCenter });

	const Rectangle dividerLineRect { position + Displacement { 26, 25 }, { 267, 3 } };
	out.BlitFrom(out, MakeSdlRect(dividerLineRect), dividerLineRect.position + Displacement { 0, (5 * 12) + 13 });

	rect.position.y += (10 - uitem.UINumPL) * 12;
	assert(uitem.UINumPL <= sizeof(uitem.powers) / sizeof(*uitem.powers));
	const TextRenderOptions textRenderOptions { .flags = UiFlags::ColorWhite | UiFlags::AlignCenter };
	const GameFontTables fontSize = GetFontSizeFromUiFlags(textRenderOptions.flags);
	for (const auto &power : uitem.powers) {
		if (power.type == IPL_INVALID)
			break;
		rect.position.y += 2 * 12;
		// Pre-wrap the string at spaces, otherwise DrawString would hard wrap in the middle of words.
		const std::string wrapped = WordWrapString(PrintItemPower(power.type, curruitem), rect.size.width);
		DrawString(out, wrapped, rect, textRenderOptions);
		for (const std::string_view line : SplitByChar(wrapped, '\n')) {
			if (line.data() + line.size() == wrapped.data() + wrapped.size()) break;
			rect.position.y += GetLineHeight(line, fontSize);
		}
	}
}

/** @brief POE2 Mashup: armor pieces show the defense their base grants. */
void PrintPoe2ItemDefense(const Item &item)
{
	std::string defense;
	int armour;
	int evasion;
	int energyShield;
	SplitDefense(ItemDefenseType(item), item._iAC, armour, evasion, energyShield);
	if (armour > 0)
		StrAppend(defense, FormatRuntime(_("armour: {:d}"), armour));
	if (evasion > 0)
		StrAppend(defense, defense.empty() ? "" : "  ", FormatRuntime(_("evasion: {:d}"), evasion));
	if (energyShield > 0)
		StrAppend(defense, defense.empty() ? "" : "  ", FormatRuntime(_("energy shield: {:d}"), energyShield * EnergyShieldPerDefense));
	if (defense.empty())
		defense = FormatRuntime(_("armour: {:d}"), item._iAC);
	AddItemInfoBoxString(defense);
	if (item._iMaxDur == DUR_INDESTRUCTIBLE)
		AddItemInfoBoxString(_("Indestructible"));
	else
		AddItemInfoBoxString(FormatRuntime(_(/* TRANSLATORS: Dur: is durability */ "Dur: {:d}/{:d}"), item._iDurability, item._iMaxDur));
}

/** @brief POE2 Mashup: Vaal Orb corruption and the implicit it added. */
uint8_t AffixTier(const std::vector<PLStruct> &affixes, const PLStruct &affix)
{
	// Count the distinct higher levels, so two affixes of the same level share a tier.
	std::vector<int8_t> betterLevels;
	for (const PLStruct &other : affixes) {
		if (other.power.type != affix.power.type || other.PLMinLvl <= affix.PLMinLvl)
			continue;
		if ((other.PLIType & affix.PLIType) == AffixItemType::None || (other.PLGOE == GOE_EVIL) != (affix.PLGOE == GOE_EVIL))
			continue;
		if (std::find(betterLevels.begin(), betterLevels.end(), other.PLMinLvl) == betterLevels.end())
			betterLevels.push_back(other.PLMinLvl);
	}
	return static_cast<uint8_t>(std::min<size_t>(betterLevels.size() + 1, UINT8_MAX));
}

namespace {

/** @brief The affix text, with its POE2-style tier at the end while Alt is held. */
std::string PrintItemPowerWithTier(item_effect_type power, const Item &item, uint8_t tier)
{
	std::string text { PrintItemPower(power, item).str() };
	if (gbIsPoe2 && tier > 0 && (SDL_GetModState() & SDL_KMOD_ALT) != 0)
		text.append(FormatRuntime(" (T{:d})", tier));
	return text;
}

} // namespace

/** @brief POE2 Mashup: crafting state shown under the modifiers: an unrevealed modifier, catalyst quality and augment sockets. */
void PrintItemCraftState(const Item &item)
{
	// Waystones and tablets keep their tier and uses where jewellery keeps its catalyst.
	if (IsWaystone(item) || IsEndgameItem(item))
		return;
	const Poe2Craft &craft = item._iPoe2Craft;
	for (const Poe2CraftMod &mod : craft.mods) {
		if (mod.family == UnrevealedModFamily)
			AddItemInfoBoxString(IsCraftPrefix(mod) ? _("Unrevealed desecrated prefix") : _("Unrevealed desecrated suffix"));
		if (const DesecratedMod *desecrated = GetDesecratedMod(mod.family); desecrated != nullptr) {
			for (const std::string &line : DesecratedModText(item, mod))
				AddItemInfoBoxString(StrCat(line, " (", desecrated->name, ")"));
		}
	}
	if (const auto catalyst = static_cast<Catalyst>(craft.catalyst); catalyst != Catalyst::None)
		AddItemInfoBoxString(FormatRuntime(_("Quality: +{:d}% ({:s} modifiers)"), ItemQuality(item), CatalystName(catalyst)));
	if (craft.desecrated)
		AddItemInfoBoxString(_("Desecrated"));
	for (const std::string &line : AugmentSocketText(item))
		AddItemInfoBoxString(line);
}

void PrintItemCorruption(const Item &item)
{
	if ((item.dwBuff & CF_POE2_CORRUPTED) == 0)
		return;
	if (const std::string enchant = CorruptionEnchantText(item); !enchant.empty())
		AddItemInfoBoxString(FormatRuntime(_("enchant: {:s}"), enchant));
	const int kind = static_cast<int>((item.dwBuff & CF_POE2_CORRUPT_IMPLICIT) >> 14);
	const ItemPower power = CorruptionImplicitPower(kind);
	switch (power.type) {
	case IPL_ALLRES:
		AddItemInfoBoxString(FormatRuntime(_("corrupted: {:+d}% all resistances"), power.param1));
		break;
	case IPL_LIFE:
		AddItemInfoBoxString(FormatRuntime(_("corrupted: {:+d} hit points"), power.param1));
		break;
	case IPL_ATTRIBS:
		AddItemInfoBoxString(FormatRuntime(_("corrupted: {:+d} to all attributes"), power.param1));
		break;
	default:
		break;
	}
	AddItemInfoBoxString(_("Corrupted"));
}

/**
 * @brief POE2 Mashup: shows effects that come with the base item type itself (like quick attack on daggers).
 */
void PrintItemImplicits(const Item &item)
{
	if (gbIsPoe2 && ItemGrantsSpirit(item))
		AddItemInfoBoxString(FormatRuntime(_("implicit: +{:d} to Spirit"), SceptreSpirit));
	for (const std::string &line : ItemImplicitText(item))
		AddItemInfoBoxString(FormatRuntime(_("implicit: {:s}"), line));
	if (const int quality = ItemQuality(item); quality > 0 && HasQuality(item))
		AddItemInfoBoxString(FormatRuntime(IsExceptional(item) ? _("Exceptional, quality: +{:d}%") : _("Quality: +{:d}%"), quality));
	const BodyArmourImplicit armourImplicit = GetBodyArmourImplicit(item);
	if (armourImplicit.life > 0)
		AddItemInfoBoxString(FormatRuntime(_("implicit: +{:d} to life"), armourImplicit.life));
	if (armourImplicit.spirit > 0)
		AddItemInfoBoxString(FormatRuntime(_("implicit: +{:d} to Spirit"), armourImplicit.spirit));
	if (armourImplicit.allResistances > 0)
		AddItemInfoBoxString(FormatRuntime(_("implicit: +{:d}% to all resistances"), armourImplicit.allResistances));
	if (armourImplicit.magicResistance > 0)
		AddItemInfoBoxString(FormatRuntime(_("implicit: +{:d}% to magic resistance"), armourImplicit.magicResistance));
	if (armourImplicit.manaRegenPercent > 0)
		AddItemInfoBoxString(FormatRuntime(_("implicit: {:d}% increased mana regeneration"), armourImplicit.manaRegenPercent));
	if (armourImplicit.evasionPercent > 0)
		AddItemInfoBoxString(FormatRuntime(_("implicit: {:d}% increased evasion"), armourImplicit.evasionPercent));
	// The list of skill gems a weapon can use was dropped: it read like the weapon granted them all.
	// Each skill gem's own text says which weapons it needs.

	const ItemSpecialEffect baseFlags = AllItemsList[item.IDidx].iFlags;
	if (baseFlags == ItemSpecialEffect::None)
		return;

	Item implicitView = item;
	implicitView._iFlags = baseFlags;
	if (HasAnyOf(baseFlags, ItemSpecialEffect::QuickAttack | ItemSpecialEffect::FastAttack | ItemSpecialEffect::FasterAttack | ItemSpecialEffect::FastestAttack))
		AddItemInfoBoxString(FormatRuntime(_("implicit: {:s}"), PrintItemPower(IPL_FASTATTACK, implicitView).str()));
	if (HasAnyOf(baseFlags, ItemSpecialEffect::Knockback))
		AddItemInfoBoxString(FormatRuntime(_("implicit: {:s}"), PrintItemPower(IPL_KNOCKBACK, implicitView).str()));
	if (HasAnyOf(baseFlags, ItemSpecialEffect::FastHitRecovery | ItemSpecialEffect::FasterHitRecovery | ItemSpecialEffect::FastestHitRecovery))
		AddItemInfoBoxString(FormatRuntime(_("implicit: {:s}"), PrintItemPower(IPL_FASTRECOVER, implicitView).str()));
}

void PrintItemDetails(const Item &item)
{
	if (HeadlessMode)
		return;

	CurrentInfoLineStyle = InfoLineStyle::Subtitle;
	if (item._iMagical == ITEM_QUALITY_RARE)
		AddItemInfoBoxString(std::string_view(item._iName));
	CurrentInfoLineStyle = InfoLineStyle::Property;
	if (item._iClass == ICLASS_WEAPON) {
		// POE2 Mashup: the damage shown is the weapon's whole hit, with its increased physical and added elemental damage.
		int minDamage = item._iMinDam;
		int maxDamage = item._iMaxDam;
		const int physicalMin = item._iMinDam * (100 + item._iPLDam) / 100;
		const int physicalMax = item._iMaxDam * (100 + item._iPLDam) / 100;
		if (gbIsPoe2) {
			minDamage = physicalMin + item._iFMinDam + item._iLMinDam;
			maxDamage = physicalMax + item._iFMaxDam + item._iLMaxDam;
		}
		if (minDamage == maxDamage) {
			if (item._iMaxDur == DUR_INDESTRUCTIBLE)
				AddItemInfoBoxString(FormatRuntime(_("damage: {:d}  Indestructible"), minDamage));
			else
				AddItemInfoBoxString(FormatRuntime(_(/* TRANSLATORS: Dur: is durability */ "damage: {:d}  Dur: {:d}/{:d}"), minDamage, item._iDurability, item._iMaxDur));
		} else {
			if (item._iMaxDur == DUR_INDESTRUCTIBLE)
				AddItemInfoBoxString(FormatRuntime(_("damage: {:d}-{:d}  Indestructible"), minDamage, maxDamage));
			else
				AddItemInfoBoxString(FormatRuntime(_(/* TRANSLATORS: Dur: is durability */ "damage: {:d}-{:d}  Dur: {:d}/{:d}"), minDamage, maxDamage, item._iDurability, item._iMaxDur));
		}
		// Holding Alt splits it by damage type, each in its colour.
		if (gbIsPoe2 && (SDL_GetModState() & SDL_KMOD_ALT) != 0 && (item._iFMaxDam > 0 || item._iLMaxDam > 0 || item._iPLDam != 0)) {
			CurrentInfoLineStyle = InfoLineStyle::PhysicalDamage;
			AddItemInfoBoxString(FormatRuntime(_("Physical Damage: {:d}-{:d}"), physicalMin, physicalMax));
			if (item._iFMaxDam > 0) {
				CurrentInfoLineStyle = InfoLineStyle::FireDamage;
				AddItemInfoBoxString(FormatRuntime(_("Fire Damage: {:d}-{:d}"), item._iFMinDam, item._iFMaxDam));
			}
			if (item._iLMaxDam > 0) {
				CurrentInfoLineStyle = InfoLineStyle::LightningDamage;
				AddItemInfoBoxString(FormatRuntime(_("Lightning Damage: {:d}-{:d}"), item._iLMinDam, item._iLMaxDam));
			}
			CurrentInfoLineStyle = InfoLineStyle::Property;
		}
	}
	CurrentInfoLineStyle = InfoLineStyle::Implicit;
	PrintItemImplicits(item);
	CurrentInfoLineStyle = InfoLineStyle::Corrupted;
	PrintItemCorruption(item);
	CurrentInfoLineStyle = InfoLineStyle::Property;
	if (item._iClass == ICLASS_ARMOR && gbIsPoe2) {
		PrintPoe2ItemDefense(item);
	} else if (item._iClass == ICLASS_ARMOR) {
		if (item._iMaxDur == DUR_INDESTRUCTIBLE)
			AddItemInfoBoxString(FormatRuntime(_("armor: {:d}  Indestructible"), item._iAC));
		else
			AddItemInfoBoxString(FormatRuntime(_(/* TRANSLATORS: Dur: is durability */ "armor: {:d}  Dur: {:d}/{:d}"), item._iAC, item._iDurability, item._iMaxDur));
	}
	if (item._iMiscId == IMISC_STAFF && item._iMaxCharges != 0) {
		AddItemInfoBoxString(FormatRuntime(_("Charges: {:d}/{:d}"), item._iCharges, item._iMaxCharges));
	}
	CurrentInfoLineStyle = InfoLineStyle::Mod;
	if (item._iPoe2Mods[0].family != 0 || item._iPoe2Craft.explicitMods) {
		// Holding Alt shows each modifier's side, name and tier, and the range it can roll, as in POE2.
		const bool advanced = gbIsPoe2 && (SDL_GetModState() & SDL_KMOD_ALT) != 0;
		for (const Poe2ItemMod &mod : item._iPoe2Mods) {
			if (!advanced) {
				for (const std::string &line : ItemModText(mod))
					AddItemInfoBoxString(line);
				continue;
			}
			const std::vector<std::string> lines = ItemModAdvancedText(mod);
			for (size_t i = 0; i < lines.size(); i++) {
				CurrentInfoLineStyle = i == 0 ? InfoLineStyle::Note : InfoLineStyle::Mod;
				AddItemInfoBoxString(lines[i]);
			}
		}
		CurrentInfoLineStyle = InfoLineStyle::Mod;
	} else if (item._iMagical == ITEM_QUALITY_RARE) {
		for (size_t i = 0; i < item._iRarePowers.size(); i++) {
			if (item._iRarePowers[i] != IPL_INVALID)
				AddItemInfoBoxString(PrintItemPowerWithTier(item._iRarePowers[i], item, item._iRareTiers[i]));
		}
	} else {
		if (item._iPrePower != -1) {
			AddItemInfoBoxString(PrintItemPowerWithTier(item._iPrePower, item, item._iPreTier));
		}
		if (item._iSufPower != -1) {
			AddItemInfoBoxString(PrintItemPowerWithTier(item._iSufPower, item, item._iSufTier));
		}
	}
	PrintItemCraftState(item);
	if (item._iMagical == ITEM_QUALITY_UNIQUE && gbIsPoe2) {
		// POE2 Mashup: a unique's powers are listed in its tooltip, not in a separate window.
		for (const std::string &line : Poe2UniqueText(item))
			AddItemInfoBoxString(line);
		for (const auto &power : UniqueItems[item._iUid].powers) {
			if (power.type == IPL_INVALID)
				break;
			AddItemInfoBoxString(PrintItemPower(power.type, item));
		}
	} else if (item._iMagical == ITEM_QUALITY_UNIQUE) {
		AddItemInfoBoxString(_("unique item"));
		ShowUniqueItemInfoBox = true;
		curruitem = item;
	}
	CurrentInfoLineStyle = InfoLineStyle::Note;
	PrintItemInfo(item);
	CurrentInfoLineStyle = InfoLineStyle::Default;
}

namespace {

/** @brief The worn item the given item would replace; nullptr if it isn't equipment. */
const Item *EquippedCounterpart(const Player &player, const Item &item)
{
	// A hand item is compared with a held one of its own kind (a sceptre with a sceptre), else of its class
	// (a weapon with a weapon, a shield with a shield or focus), never across classes.
	static const Item Nothing {};
	const auto hand = [&player, &item](item_class itemClass) {
		const std::optional<ModSlot> kind = ItemModSlot(item);
		for (const inv_body_loc slot : { INVLOC_HAND_LEFT, INVLOC_HAND_RIGHT }) {
			const Item &held = player.InvBody[slot];
			if (!held.isEmpty() && held._iClass == itemClass && kind && ItemModSlot(held) == kind)
				return &held;
		}
		for (const inv_body_loc slot : { INVLOC_HAND_LEFT, INVLOC_HAND_RIGHT }) {
			if (!player.InvBody[slot].isEmpty() && player.InvBody[slot]._iClass == itemClass)
				return &player.InvBody[slot];
		}
		return &Nothing;
	};
	switch (player.GetItemLocation(item)) {
	case ILOC_ARMOR:
		return &player.InvBody[INVLOC_CHEST];
	case ILOC_HELM:
		return &player.InvBody[INVLOC_HEAD];
	case ILOC_AMULET:
		return &player.InvBody[INVLOC_AMULET];
	case ILOC_RING:
		return &player.InvBody[INVLOC_RING_LEFT];
	case ILOC_ONEHAND:
		return hand(item._iClass == ICLASS_ARMOR ? ICLASS_ARMOR : ICLASS_WEAPON);
	case ILOC_TWOHAND:
		return hand(ICLASS_WEAPON);
	default:
		return nullptr;
	}
}

} // namespace

void PrintItemComparisonWith(const Item &item, const Item &worn, std::string_view slot);

void PrintItemComparison(const Item &item)
{
	if (!gbIsPoe2 || MyPlayer == nullptr || !item.isEquipment())
		return;
	const Player &player = *MyPlayer;
	const Item *worn = EquippedCounterpart(player, item);
	if (worn == nullptr || worn == &item)
		return;
	// Comparing only while Shift is held, like POE2's.
	if ((SDL_GetModState() & SDL_KMOD_SHIFT) == 0) {
		CurrentInfoLineStyle = InfoLineStyle::Note;
		AddInfoBoxString(_("Hold Shift to compare"), true);
		CurrentInfoLineStyle = InfoLineStyle::Default;
		return;
	}
	// A ring is compared with both rings worn, each in its own box, like POE2's.
	if (worn == &player.InvBody[INVLOC_RING_LEFT]) {
		if (&item == &player.InvBody[INVLOC_RING_RIGHT])
			return;
		PrintItemComparisonWith(item, player.InvBody[INVLOC_RING_LEFT], _("Left Ring"));
		PrintItemComparisonWith(item, player.InvBody[INVLOC_RING_RIGHT], _("Right Ring"));
		return;
	}
	PrintItemComparisonWith(item, *worn, {});
}

void PrintItemComparisonWith(const Item &item, const Item &worn, std::string_view slot)
{
	static const Item Nothing {};
	const Item &other = worn.isEmpty() ? Nothing : worn;

	// The equipped item's own tooltip goes beside this one, like POE2's.
	if (!other.isEmpty()) {
		StringOrView savedFloating = std::move(FloatingInfoString);
		StringOrView savedInfo = std::move(InfoString);
		std::vector<InfoLineStyle> savedStyles = std::move(FloatingInfoStyles);
		FloatingInfoString = other.getName();
		InfoString = StringOrView {};
		FloatingInfoStyles.clear();
		if (other._iIdentified)
			PrintItemDetails(other);
		else
			PrintItemDur(other);
		CompareTooltip &compare = CompareTooltips.emplace_back();
		compare.label = slot.empty() ? std::string(_("Equipped")) : StrCat(_("Equipped: "), slot);
		compare.text = std::move(FloatingInfoString);
		compare.styles = std::move(FloatingInfoStyles);
		compare.color = other.getTextColor();
		FloatingInfoString = std::move(savedFloating);
		InfoString = std::move(savedInfo);
		FloatingInfoStyles = std::move(savedStyles);
	}

	CurrentInfoLineStyle = InfoLineStyle::Note;
	if (slot.empty())
		AddInfoBoxString(other.isEmpty() ? std::string(_("Nothing equipped there; equipping it gives:")) : std::string(_("Equipping it instead:")), true);
	else
		AddInfoBoxString(other.isEmpty() ? StrCat(_("Nothing on your "), slot, _("; wearing it there gives:")) : StrCat(_("Instead of your "), slot, ":"), true);
	int shown = 0;
	const auto compare = [&shown](int mine, int theirs, std::string_view label, std::string_view suffix = {}) {
		if (mine == theirs)
			return;
		const int difference = mine - theirs;
		CurrentInfoLineStyle = difference > 0 ? InfoLineStyle::Better : InfoLineStyle::Worse;
		AddInfoBoxString(StrCat(difference > 0 ? "+" : "", difference, suffix, " ", label), true);
		shown++;
	};
	// Unidentified items only show their base: damage and defence. Identified weapons compare their whole hit, as the tooltip shows it.
	const auto hitDamage = [](const Item &it, bool max) {
		const int base = max ? it._iMaxDam : it._iMinDam;
		if (it.isEmpty() || !it._iIdentified)
			return base;
		return (base * (100 + it._iPLDam) / 100) + (max ? it._iFMaxDam + it._iLMaxDam : it._iFMinDam + it._iLMinDam);
	};
	compare(hitDamage(item, false), hitDamage(other, false), _("to minimum Damage"));
	compare(hitDamage(item, true), hitDamage(other, true), _("to maximum Damage"));
	int armour = 0;
	int evasion = 0;
	int energyShield = 0;
	SplitDefense(ItemDefenseType(item), item._iAC, armour, evasion, energyShield);
	int otherArmour = 0;
	int otherEvasion = 0;
	int otherEnergyShield = 0;
	SplitDefense(ItemDefenseType(other), other._iAC, otherArmour, otherEvasion, otherEnergyShield);
	compare(armour, otherArmour, _("Armour"));
	compare(evasion, otherEvasion, _("Evasion"));
	compare(energyShield * EnergyShieldPerDefense, otherEnergyShield * EnergyShieldPerDefense, _("Energy Shield"));
	// Normal items have no modifiers to hide.
	const auto identified = [](const Item &it) { return it.isEmpty() || it._iIdentified || it._iMagical == ITEM_QUALITY_NORMAL; };
	const bool bothIdentified = identified(item) && identified(other);
	if (bothIdentified) {
		compare(item._iPLDam, other._iPLDam, _("increased Damage"), "%");
		compare(item._iPLToHit, other._iPLToHit, _("Accuracy"), "%");
		compare(item._iPLAC, other._iPLAC, _("increased Defence"), "%");
		compare((item._iFMinDam + item._iFMaxDam) / 2, (other._iFMinDam + other._iFMaxDam) / 2, _("average Fire Damage"));
		compare((item._iLMinDam + item._iLMaxDam) / 2, (other._iLMinDam + other._iLMaxDam) / 2, _("average Lightning Damage"));
		compare(item._iPLStr, other._iPLStr, _("Strength"));
		compare(item._iPLDex, other._iPLDex, _("Dexterity"));
		compare(item._iPLMag, other._iPLMag, _("Intelligence"));
		compare(item._iPLVit, other._iPLVit, _("Vitality"));
		compare(item._iPLHP.whole(), other._iPLHP.whole(), _("maximum Life"));
		compare(item._iPLMana.whole(), other._iPLMana.whole(), _("maximum Mana"));
		compare(item._iPLFR, other._iPLFR, _("Fire Resistance"), "%");
		compare(item._iPLLR, other._iPLLR, _("Lightning Resistance"), "%");
		compare(item._iPLMR, other._iPLMR, _("Magic Resistance"), "%");
		compare(item._iPLLifeRegen, other._iPLLifeRegen, _("Life regenerated per second"));
		compare(item._iPLManaRegen, other._iPLManaRegen, _("increased Mana Regeneration"), "%");
		compare(item._iSplLvlAdd, other._iSplLvlAdd, _("to Level of all Spells"));
		compare(-item._iPLGetHit, -other._iPLGetHit, _("less damage taken from hits"));
	} else {
		CurrentInfoLineStyle = InfoLineStyle::Requirement;
		AddInfoBoxString(_("Identify both to compare modifiers"), true);
	}
	if (shown == 0 && bothIdentified) {
		CurrentInfoLineStyle = InfoLineStyle::Requirement;
		AddInfoBoxString(_("No differences"), true);
	}
	CurrentInfoLineStyle = InfoLineStyle::Default;
}

void PrintItemDur(const Item &item)
{
	if (HeadlessMode)
		return;

	CurrentInfoLineStyle = InfoLineStyle::Property;
	if (item._iClass == ICLASS_WEAPON) {
		if (item._iMinDam == item._iMaxDam) {
			if (item._iMaxDur == DUR_INDESTRUCTIBLE)
				AddItemInfoBoxString(FormatRuntime(_("damage: {:d}  Indestructible"), item._iMinDam));
			else
				AddItemInfoBoxString(FormatRuntime(_("damage: {:d}  Dur: {:d}/{:d}"), item._iMinDam, item._iDurability, item._iMaxDur));
		} else {
			if (item._iMaxDur == DUR_INDESTRUCTIBLE)
				AddItemInfoBoxString(FormatRuntime(_("damage: {:d}-{:d}  Indestructible"), item._iMinDam, item._iMaxDam));
			else
				AddItemInfoBoxString(FormatRuntime(_("damage: {:d}-{:d}  Dur: {:d}/{:d}"), item._iMinDam, item._iMaxDam, item._iDurability, item._iMaxDur));
		}
		if (item._iMiscId == IMISC_STAFF && item._iMaxCharges > 0) {
			AddItemInfoBoxString(FormatRuntime(_("Charges: {:d}/{:d}"), item._iCharges, item._iMaxCharges));
		}
		if (item._iMagical != ITEM_QUALITY_NORMAL)
			AddItemInfoBoxString(_("Not Identified"));
	}
	PrintItemImplicits(item);
	PrintItemCorruption(item);
	if (item._iClass == ICLASS_ARMOR) {
		if (gbIsPoe2)
			PrintPoe2ItemDefense(item);
		else if (item._iMaxDur == DUR_INDESTRUCTIBLE)
			AddItemInfoBoxString(FormatRuntime(_("armor: {:d}  Indestructible"), item._iAC));
		else
			AddItemInfoBoxString(FormatRuntime(_("armor: {:d}  Dur: {:d}/{:d}"), item._iAC, item._iDurability, item._iMaxDur));
		if (item._iMagical != ITEM_QUALITY_NORMAL)
			AddItemInfoBoxString(_("Not Identified"));
		if (item._iMiscId == IMISC_STAFF && item._iMaxCharges > 0) {
			AddItemInfoBoxString(FormatRuntime(_("Charges: {:d}/{:d}"), item._iCharges, item._iMaxCharges));
		}
	}
	if (IsAnyOf(item._itype, ItemType::Ring, ItemType::Amulet) && item._iMagical != ITEM_QUALITY_NORMAL)
		AddItemInfoBoxString(_("Not Identified"));
	CurrentInfoLineStyle = InfoLineStyle::Note;
	PrintItemInfo(item);
	CurrentInfoLineStyle = InfoLineStyle::Default;
}

void UseItem(Player &player, item_misc_id mid, SpellID spellID, int spellFrom)
{
	std::optional<SpellID> prepareSpellID;

	switch (mid) {
	case IMISC_HEAL:
		player.RestorePartialLife();
		if (&player == MyPlayer) {
			RedrawComponent(PanelDrawComponent::Health);
		}
		break;
	case IMISC_FULLHEAL:
		player.RestoreFullLife();
		if (&player == MyPlayer) {
			RedrawComponent(PanelDrawComponent::Health);
		}
		break;
	case IMISC_MANA:
		player.RestorePartialMana();
		if (&player == MyPlayer) {
			RedrawComponent(PanelDrawComponent::Mana);
		}
		break;
	case IMISC_FULLMANA:
		player.RestoreFullMana();
		if (&player == MyPlayer) {
			RedrawComponent(PanelDrawComponent::Mana);
		}
		break;
	case IMISC_ELIXSTR:
		ModifyPlrStr(player, 1);
		break;
	case IMISC_ELIXMAG:
		ModifyPlrMag(player, 1);
		if (gbIsHellfire) {
			player.RestoreFullMana();
			if (&player == MyPlayer) {
				RedrawComponent(PanelDrawComponent::Mana);
			}
		}
		break;
	case IMISC_ELIXDEX:
		ModifyPlrDex(player, 1);
		break;
	case IMISC_ELIXVIT:
		ModifyPlrVit(player, 1);
		if (gbIsHellfire) {
			player.RestoreFullLife();
			if (&player == MyPlayer) {
				RedrawComponent(PanelDrawComponent::Health);
			}
		}
		break;
	case IMISC_REJUV: {
		player.RestorePartialLife();
		player.RestorePartialMana();
		if (&player == MyPlayer) {
			RedrawComponent(PanelDrawComponent::Health);
			RedrawComponent(PanelDrawComponent::Mana);
		}
	} break;
	case IMISC_FULLREJUV:
	case IMISC_ARENAPOT:
		player.RestoreFullLife();
		player.RestoreFullMana();
		if (&player == MyPlayer) {
			RedrawComponent(PanelDrawComponent::Health);
			RedrawComponent(PanelDrawComponent::Mana);
		}
		break;
	case IMISC_SCROLL:
	case IMISC_SCROLLT:
		if (ControlMode == ControlTypes::KeyboardAndMouse && GetSpellData(spellID).isTargeted()) {
			prepareSpellID = spellID;
		} else {
			// Find a valid target for the spell because tile coords
			// will be validated when processing the network message
			Point target = cursPosition;
			if (!InDungeonBounds(target))
				target = player.position.future + Displacement(player._pdir);
			// Use CMD_SPELLXY because it's the same behavior as normal casting
			assert(IsValidSpellFrom(spellFrom));
			NetSendCmdLocParam3(true, CMD_SPELLXY, target, static_cast<int8_t>(spellID), static_cast<uint8_t>(SpellType::Scroll), static_cast<uint16_t>(spellFrom));
		}
		break;
	case IMISC_BOOK: {
		const uint8_t newSpellLevel = player._pSplLvl[static_cast<int8_t>(spellID)] + 1;
		if (newSpellLevel <= MaxSpellLevel) {
			player._pSplLvl[static_cast<int8_t>(spellID)] = newSpellLevel;
			NetSendCmdParam2(true, CMD_CHANGE_SPELL_LEVEL, static_cast<uint16_t>(spellID), newSpellLevel);
		}
		if (HasNoneOf(player._pIFlags, ItemSpecialEffect::NoMana)) {
			player._pMana += Fixed26_6::fromInt(GetSpellData(spellID).sManaCost);
			player._pMana = std::min(player._pMana, player._pMaxMana);
			player._pManaBase += Fixed26_6::fromInt(GetSpellData(spellID).sManaCost);
			player._pManaBase = std::min(player._pManaBase, player._pMaxManaBase);
		}
		if (&player == MyPlayer) {
			for (Item &item : InventoryPlayerItemsRange { player }) {
				item.updateRequiredStatsCacheForPlayer(player);
			}
			if (IsStashOpen) {
				Stash.RefreshItemStatFlags();
			}
		}
		RedrawComponent(PanelDrawComponent::Mana);
	} break;
	case IMISC_MAPOFDOOM:
		doom_init();
		break;
	case IMISC_OILACC:
	case IMISC_OILMAST:
	case IMISC_OILSHARP:
	case IMISC_OILDEATH:
	case IMISC_OILSKILL:
	case IMISC_OILBSMTH:
	case IMISC_OILFORT:
	case IMISC_OILPERM:
	case IMISC_OILHARD:
	case IMISC_OILIMP:
	case IMISC_ORB_TRANSMUTE:
	case IMISC_ORB_ALCHEMY:
	case IMISC_ORB_CHAOS:
	case IMISC_ORB_SCOURING:
	case IMISC_ORB_AUGMENT:
	case IMISC_ORB_REGAL:
	case IMISC_ORB_EXALTED:
	case IMISC_ORB_DIVINE:
	case IMISC_ORB_VAAL:
	case IMISC_ORB_BAUBLE:
	case IMISC_ORB_CHANCE:
	case IMISC_ORB_ANNUL:
	case IMISC_ORB_WHETSTONE:
	case IMISC_ORB_ETCHER:
	case IMISC_ORB_SCRAP:
	case IMISC_ORB_GREATER_EXALTED:
	case IMISC_ORB_PERFECT_EXALTED:
	case IMISC_ORB_GREATER_CHAOS:
	case IMISC_ORB_PERFECT_CHAOS:
	case IMISC_ORB_ARTIFICER:
	case IMISC_ORB_DESECRATE:
	case IMISC_ORB_CATALYST:
	case IMISC_ESSENCE:
	case IMISC_AUGMENT:
	case IMISC_ORB_LESSER_JEWELLER:
	case IMISC_ORB_GREATER_JEWELLER:
	case IMISC_ORB_PERFECT_JEWELLER:
		player._pOilType = mid;
		if (&player != MyPlayer) {
			return;
		}
		if (SpellbookFlag) {
			SpellbookFlag = false;
		}
		if (!invflag) {
			invflag = true;
		}
		// Jeweller's Orbs go on a skill, so the Skills panel opens beside the bag.
		if (JewellerSocket(CurrencyOrbForMiscId(mid)) && !IsSkillsPanelOpen())
			OpenSkillsPanel();
		NewCursor(CURSOR_OIL);
		break;
	case IMISC_SPECELIX:
		ModifyPlrStr(player, 3);
		ModifyPlrMag(player, 3);
		ModifyPlrDex(player, 3);
		ModifyPlrVit(player, 3);
		break;
	case IMISC_RUNEF:
		prepareSpellID = SpellID::RuneOfFire;
		break;
	case IMISC_RUNEL:
		prepareSpellID = SpellID::RuneOfLight;
		break;
	case IMISC_GR_RUNEL:
		prepareSpellID = SpellID::RuneOfNova;
		break;
	case IMISC_GR_RUNEF:
		prepareSpellID = SpellID::RuneOfImmolation;
		break;
	case IMISC_RUNES:
		prepareSpellID = SpellID::RuneOfStone;
		break;
	default:
		break;
	}

	if (prepareSpellID) {
		assert(IsValidSpellFrom(spellFrom));
		player.inventorySpell = *prepareSpellID;
		player.spellFrom = spellFrom;
		if (&player == MyPlayer)
			NewCursor(CURSOR_TELEPORT);
	}
}

bool UseItemOpensHive(const Item &item, Point position)
{
	if (item.IDidx != IDI_RUNEBOMB)
		return false;
	for (auto dir : PathDirs) {
		const Point adjacentPosition = position + dir;
		if (OpensHive(adjacentPosition))
			return true;
	}
	return false;
}

bool UseItemOpensGrave(const Item &item, Point position)
{
	if (item.IDidx != IDI_MAPOFDOOM)
		return false;
	for (auto dir : PathDirs) {
		const Point adjacentPosition = position + dir;
		if (OpensGrave(adjacentPosition))
			return true;
	}
	return false;
}

VendorCategory GetVendorCategory(const Item &item)
{
	switch (item._itype) {
	case ItemType::Sword:
	case ItemType::Axe:
	case ItemType::Mace:
		return item._iLoc == ILOC_TWOHAND ? VendorCategory::TwoHanded : VendorCategory::OneHanded;
	case ItemType::Staff:
		return VendorCategory::TwoHanded;
	case ItemType::Bow:
		return VendorCategory::Bows;
	case ItemType::LightArmor:
	case ItemType::MediumArmor:
	case ItemType::HeavyArmor:
		return VendorCategory::BodyArmour;
	case ItemType::Helm:
		return VendorCategory::Helmets;
	case ItemType::Shield:
		return VendorCategory::Shields;
	case ItemType::Ring:
	case ItemType::Amulet:
		return VendorCategory::Jewelry;
	default:
		return VendorCategory::Supplies;
	}
}

void SortVendorStock(std::span<Item> stock, size_t pinnedCount)
{
	SortVendor(stock, pinnedCount);
}

namespace {

/** @brief POE2 Mashup: how many wares of one kind a vendor stocks. */
struct VendorStockQuota {
	VendorCategory category;
	int min;
	int max;
};

/**
 * Griswold's stock: each shop tab is one page (Curtis 2026-10-16), with room left for his premium wares.
 * One-handers, two-handers and bows share the Weapons tab.
 */
constexpr VendorStockQuota SmithStockQuotas[] = {
	{ VendorCategory::OneHanded, 5, 6 },
	{ VendorCategory::TwoHanded, 2, 3 },
	{ VendorCategory::Bows, 2, 3 },
	{ VendorCategory::BodyArmour, 8, 10 },
	{ VendorCategory::Helmets, 10, 12 },
	{ VendorCategory::Shields, 8, 10 },
	{ VendorCategory::Jewelry, 5, 7 },
};

/** Adria's stock besides her pinned wares: a tab of staves, and a few more scrolls and elixirs. */
constexpr VendorStockQuota WitchStockQuotas[] = {
	{ VendorCategory::TwoHanded, 15, 20 },
	{ VendorCategory::Supplies, 3, 6 },
};

constexpr int MaxStockCount(std::span<const VendorStockQuota> quotas)
{
	int count = 0;
	for (const VendorStockQuota &quota : quotas)
		count += quota.max;
	return count;
}

static_assert(MaxStockCount(SmithStockQuotas) <= NumSmithBasicItemsPoe2);
static_assert(NumWitchPinnedItems + MaxStockCount(WitchStockQuotas) <= NumWitchItemsPoe2);

/** A vendor gives up on a kind of ware it can't find at its level after this many rolls. */
constexpr int MaxStockRolls = 4000;

/**
 * @brief POE2 Mashup: rolls wares until each kind has its share, so every shop tab fills up.
 * Each ware is rolled the usual way from its own seed, so it is recreated the same from that seed; a ware of a kind
 * the vendor already has enough of is passed over.
 */
template <typename Stock, typename Roll>
void FillStockByCategory(Stock &stock, std::span<const VendorStockQuota> quotas, Roll roll)
{
	std::array<int, NumVendorCategories> wanted {};
	int remaining = 0;
	for (const VendorStockQuota &quota : quotas) {
		const int count = RandomIntBetween(quota.min, quota.max);
		wanted[static_cast<size_t>(quota.category)] += count;
		remaining += count;
	}
	for (int rolls = 0; remaining > 0 && rolls < MaxStockRolls; rolls++) {
		const Item item = roll();
		int &want = wanted[static_cast<size_t>(GetVendorCategory(item))];
		if (want == 0)
			continue;
		want--;
		remaining--;
		stock.push_back(item);
	}
}

Item RollSmithItem(int lvl, int maxValue)
{
	Item newItem;
	do {
		newItem = {};
		newItem._iSeed = AdvanceRndSeed();
		SetRndSeed(newItem._iSeed);
		const _item_indexes itemData = RndSmithItem(*MyPlayer, lvl);
		GetItemAttrs(newItem, itemData, lvl);
		if (gbIsPoe2 && EquipmentClassWeight(newItem._itype) > 0) {
			// POE2 Mashup (Curtis 2026-10-16): mostly normal wares, some magic, a few rare, never unique.
			SetVendorPoe2ItemLevel(newItem, lvl);
			SetLootRarity(newItem, RollVendorRarity());
			if (GetLootRarity(newItem) != ITEM_QUALITY_NORMAL)
				GetItemBonus(*MyPlayer, newItem, lvl / 2, lvl, true, false);
		}
	} while (newItem._iIvalue > maxValue);

	newItem._iCreateInfo = lvl | CF_SMITH;
	SetVendorPoe2ItemLevel(newItem, lvl);
	newItem._iIdentified = true;
	return newItem;
}

Item RollWitchItem(int lvl, int maxValue)
{
	Item item;
	do {
		item = {};
		item._iSeed = AdvanceRndSeed();
		SetRndSeed(item._iSeed);
		const _item_indexes itemData = RndWitchItem(*MyPlayer, lvl);
		GetItemAttrs(item, itemData, lvl);
		int maxlvl = -1;
		if (GenerateRnd(100) <= 5)
			maxlvl = 2 * lvl;
		if (maxlvl == -1 && item._iMiscId == IMISC_STAFF)
			maxlvl = 2 * lvl;
		if (maxlvl != -1)
			GetItemBonus(*MyPlayer, item, maxlvl / 2, maxlvl, true, true);
	} while (item._iIvalue > maxValue);

	item._iCreateInfo = lvl | CF_WITCH;
	SetVendorPoe2ItemLevel(item, lvl);
	item._iIdentified = true;
	return item;
}

} // namespace

void SpawnSmith(int lvl)
{
	constexpr int PinnedItemCount = 0;

	int maxValue = MaxVendorValue;
	int maxItems = NumSmithBasicItems;
	if (gbIsHellfire) {
		maxValue = MaxVendorValueHf;
		maxItems = NumSmithBasicItemsHf;
	}

	if (gbIsPoe2) {
		// POE2 Mashup: a shop tab's worth of each kind of equipment.
		SmithItems.clear();
		FillStockByCategory(SmithItems, SmithStockQuotas, [lvl, maxValue]() { return RollSmithItem(lvl, maxValue); });
		SortVendor(SmithItems, PinnedItemCount);
		return;
	}

	const size_t iCnt = RandomIntBetween(10, maxItems);
	SmithItems.clear();

	while (SmithItems.size() < iCnt)
		SmithItems.push_back(RollSmithItem(lvl, maxValue));

	SortVendor(SmithItems, PinnedItemCount);
}

void ReplacePremium(const Player &player, int idx)
{
	int plvl = gbIsHellfire ? itemLevelAddHf[idx] : itemLevelAdd[idx];
	plvl += player.getCharacterLevel();
	SpawnOnePremium(PremiumItems[idx], plvl, player);
	if (gbIsPoe2)
		SortVendor(PremiumItems, 0);
}

void SpawnPremium(const Player &player)
{
	const int lvl = player.getCharacterLevel();
	const size_t maxItems = gbIsHellfire ? NumSmithItemsHf : NumSmithItems;

	while (PremiumItems.size() < maxItems) {
		int plvl = PremiumItemLevel + (gbIsHellfire ? itemLevelAddHf[PremiumItems.size()] : itemLevelAdd[PremiumItems.size()]);
		Item item = {};
		SpawnOnePremium(item, plvl, player);
		PremiumItems.push_back(item);
	}

	if (gbIsPoe2 && PremiumItemLevel < lvl) {
		// POE2 Mashup (Curtis 2026-10-17): a level-up replaces all of the premium stock.
		PremiumItemLevel = lvl;
		for (size_t i = 0; i < PremiumItems.size(); i++)
			SpawnOnePremium(PremiumItems[i], PremiumItemLevel + (gbIsHellfire ? itemLevelAddHf[i] : itemLevelAdd[i]), player);
	}
	if (gbIsPoe2)
		SortVendor(PremiumItems, 0);

	while (PremiumItemLevel < lvl) {
		PremiumItemLevel++;
		Item *ptr = PremiumItems.begin();
		if (gbIsHellfire) {
			std::move(ptr + 3, ptr + 13, ptr);
			PremiumItems[11] = PremiumItems[13];
			PremiumItems[13] = PremiumItems[14];
			SpawnOnePremium(PremiumItems[10], PremiumItemLevel + itemLevelAddHf[10], player);
			SpawnOnePremium(PremiumItems[12], PremiumItemLevel + itemLevelAddHf[12], player);
			SpawnOnePremium(PremiumItems[14], PremiumItemLevel + itemLevelAddHf[14], player);
		} else {
			std::move(ptr + 2, ptr + 5, ptr);
			PremiumItems[4] = PremiumItems[5];
			SpawnOnePremium(PremiumItems[3], PremiumItemLevel + itemLevelAdd[3], player);
			SpawnOnePremium(PremiumItems[5], PremiumItemLevel + itemLevelAdd[5], player);
		}
	}
}

void SpawnWitch(int lvl)
{
	constexpr int PinnedItemCount = NumWitchPinnedItems;
	// POE2 Mashup: Adria always has Scrolls of Identify instead of Town Portal (the tray has a portal button).
	const std::array<_item_indexes, PinnedItemCount> PinnedItemTypes = { IDI_MANA, IDI_FULLMANA, gbIsPoe2 ? IDI_IDENTIFY : IDI_PORTAL };
	constexpr int MaxPinnedBookCount = 4;
	constexpr std::array<_item_indexes, MaxPinnedBookCount> PinnedBookTypes = { IDI_BOOK1, IDI_BOOK2, IDI_BOOK3, IDI_BOOK4 };

	int bookCount = 0;
	int pinnedBookCount = gbIsHellfire ? RandomIntLessThan(MaxPinnedBookCount) : 0;
	if (gbIsPoe2)
		pinnedBookCount = 0; // Skill gems replaced spell books.
	const int itemCount = RandomIntBetween(10, gbIsHellfire ? NumWitchItemsHf : NumWitchItems);
	const int maxValue = gbIsHellfire ? MaxVendorValueHf : MaxVendorValue;
	WitchItems.clear();

	const auto addPinnedItem = [&](int i) {
		Item item = {};
		item._iSeed = AdvanceRndSeed();
		GetItemAttrs(item, PinnedItemTypes[i], 1);
		item._iCreateInfo = lvl;
		item._iStatFlag = true;
		WitchItems.push_back(item);
	};

	if (gbIsPoe2) {
		// POE2 Mashup: her pinned wares, then a shop tab of staves and a few more scrolls and elixirs.
		for (int i = 0; i < PinnedItemCount; i++)
			addPinnedItem(i);
		FillStockByCategory(WitchItems, WitchStockQuotas, [lvl, maxValue]() { return RollWitchItem(lvl, maxValue); });
		SortVendor(WitchItems, PinnedItemCount);
		return;
	}

	for (int i = 0; i < itemCount; i++) {
		Item item = {};

		if (i < PinnedItemCount) {
			addPinnedItem(i);
			continue;
		}

		if (gbIsHellfire) {
			if (i < PinnedItemCount + MaxPinnedBookCount && bookCount < pinnedBookCount) {
				const _item_indexes bookType = PinnedBookTypes[i - PinnedItemCount];
				if (lvl >= AllItemsList[bookType].iMinMLvl) {
					item._iSeed = AdvanceRndSeed();
					SetRndSeed(item._iSeed);
					DiscardRandomValues(1);
					GetItemAttrs(item, bookType, lvl);
					item._iCreateInfo = lvl | CF_WITCH;
					item._iIdentified = true;
					bookCount++;
					WitchItems.push_back(item);
					continue;
				}
			}
		}

		WitchItems.push_back(RollWitchItem(lvl, maxValue));
	}

	SortVendor(WitchItems, PinnedItemCount);
}

void SpawnBoy(int lvl)
{
	int ivalue = 0;
	bool keepgoing = false;
	int count = 0;

	const Player &myPlayer = *MyPlayer;

	const HeroClass pc = myPlayer._pClass;
	int strength = std::max(myPlayer.GetMaximumAttributeValue(CharacterAttribute::Strength), myPlayer._pStrength);
	int dexterity = std::max(myPlayer.GetMaximumAttributeValue(CharacterAttribute::Dexterity), myPlayer._pDexterity);
	int magic = std::max(myPlayer.GetMaximumAttributeValue(CharacterAttribute::Magic), myPlayer._pMagic);
	strength += strength / 5;
	dexterity += dexterity / 5;
	magic += magic / 5;

	if (BoyItemLevel >= (lvl / 2) && !BoyItem.isEmpty())
		return;
	do {
		keepgoing = false;
		BoyItem = {};
		BoyItem._iSeed = AdvanceRndSeed();
		SetRndSeed(BoyItem._iSeed);
		const _item_indexes itype = RndBoyItem(*MyPlayer, lvl);
		GetItemAttrs(BoyItem, itype, lvl);
		GetItemBonus(*MyPlayer, BoyItem, lvl, 2 * lvl, true, true);

		if (!gbIsHellfire) {
			if (BoyItem._iIvalue > MaxBoyValue) {
				keepgoing = true; // prevent breaking the do/while loop too early by failing hellfire's condition in while
				continue;
			}
			break;
		}

		ivalue = 0;

		const ItemType itemType = BoyItem._itype;

		switch (itemType) {
		case ItemType::LightArmor:
		case ItemType::MediumArmor:
		case ItemType::HeavyArmor: {
			const auto *const mostValuablePlayerArmor = myPlayer.GetMostValuableItem(
			    [](const Item &item) {
				    return IsAnyOf(item._itype, ItemType::LightArmor, ItemType::MediumArmor, ItemType::HeavyArmor);
			    });

			ivalue = mostValuablePlayerArmor == nullptr ? 0 : mostValuablePlayerArmor->_iIvalue;
			break;
		}
		case ItemType::Shield:
		case ItemType::Axe:
		case ItemType::Bow:
		case ItemType::Mace:
		case ItemType::Sword:
		case ItemType::Helm:
		case ItemType::Staff:
		case ItemType::Ring:
		case ItemType::Amulet: {
			const auto *const mostValuablePlayerItem = myPlayer.GetMostValuableItem(
			    [itemType](const Item &item) { return item._itype == itemType; });

			ivalue = mostValuablePlayerItem == nullptr ? 0 : mostValuablePlayerItem->_iIvalue;
			break;
		}
		default:
			app_fatal("Invalid item spawn");
		}
		ivalue = ivalue * 4 / 5; // avoids forced int > float > int conversion

		count++;

		if (count < 200) {
			switch (pc) {
			case HeroClass::Warrior:
				if (IsAnyOf(itemType, ItemType::Bow, ItemType::Staff))
					ivalue = INT_MAX;
				break;
			case HeroClass::Rogue:
				if (IsAnyOf(itemType, ItemType::Sword, ItemType::Staff, ItemType::Axe, ItemType::Mace, ItemType::Shield))
					ivalue = INT_MAX;
				break;
			case HeroClass::Sorcerer:
				if (IsAnyOf(itemType, ItemType::Staff, ItemType::Axe, ItemType::Bow, ItemType::Mace))
					ivalue = INT_MAX;
				break;
			case HeroClass::Monk:
				if (IsAnyOf(itemType, ItemType::Bow, ItemType::MediumArmor, ItemType::Shield, ItemType::Mace))
					ivalue = INT_MAX;
				break;
			case HeroClass::Bard:
				if (IsAnyOf(itemType, ItemType::Axe, ItemType::Mace, ItemType::Staff))
					ivalue = INT_MAX;
				break;
			case HeroClass::Barbarian:
				if (IsAnyOf(itemType, ItemType::Bow, ItemType::Staff))
					ivalue = INT_MAX;
				break;
			default:
				break;
			}
		}
	} while (keepgoing
	    || ((
	            BoyItem._iIvalue > MaxBoyValueHf
	            || BoyItem._iMinStr > strength
	            || BoyItem._iMinMag > magic
	            || BoyItem._iMinDex > dexterity
	            || BoyItem._iIvalue < ivalue)
	        && count < 250));
	BoyItem._iCreateInfo = lvl | CF_BOY;
	SetVendorPoe2ItemLevel(BoyItem, lvl);
	BoyItem._iIdentified = true;
	BoyItemLevel = lvl / 2;
}

void SpawnHealer(int lvl)
{
	constexpr size_t PinnedItemCount = NumHealerPinnedItems;
	constexpr std::array<_item_indexes, PinnedItemCount + 1> PinnedItemTypes = { IDI_HEAL, IDI_FULLHEAL, IDI_RESURRECT };
	const auto itemCount = static_cast<size_t>(RandomIntBetween(10, gbIsHellfire ? NumHealerItemsHf : NumHealerItems));
	HealerItems.clear();

	for (size_t i = 0; i < itemCount; i++) {
		Item item = {};

		if (i < PinnedItemCount || (gbIsMultiplayer && i < NumHealerPinnedItemsMp)) {
			item._iSeed = AdvanceRndSeed();
			GetItemAttrs(item, PinnedItemTypes[i], 1);
			item._iCreateInfo = lvl;
			item._iStatFlag = true;
		} else {
			item._iSeed = AdvanceRndSeed();
			SetRndSeed(item._iSeed);
			const _item_indexes itype = RndHealerItem(*MyPlayer, lvl);
			GetItemAttrs(item, itype, lvl);
			item._iCreateInfo = lvl | CF_HEALER;
			item._iIdentified = true;
		}

		HealerItems.push_back(item);
	}

	SortVendor(HealerItems, PinnedItemCount);
}

void MakeGoldStack(Item &goldItem, int value)
{
	InitializeItem(goldItem, IDI_GOLD);
	GenerateNewSeed(goldItem);
	goldItem._iStatFlag = true;
	goldItem._ivalue = value;
	SetPlrHandGoldCurs(goldItem);
}

int ItemNoFlippy()
{
	const int r = ActiveItems[ActiveItemCount - 1];
	Items[r].AnimInfo.currentFrame = Items[r].AnimInfo.numberOfFrames - 1;
	Items[r]._iAnimFlag = false;
	Items[r].selectionRegion = SelectionRegion::Bottom;

	return r;
}

void CreateSpellBook(Point position, SpellID ispell, bool sendmsg, bool delta)
{
	// A boss's spell book becomes an uncut skill gem now that skill gems replaced spell books.
	if (gbIsPoe2) {
		CreateTypeItem(position, false, ItemType::Misc, IMISC_UNCUT_SKILL, sendmsg, delta);
		return;
	}
	int lvl = currlevel;

	if (gbIsHellfire) {
		lvl = GetSpellBookLevel(ispell) + 1;
		if (lvl < 1) {
			return;
		}
	}

	const _item_indexes idx = RndTypeItems(ItemType::Misc, IMISC_BOOK, lvl);
	if (ActiveItemCount >= MAXITEMS)
		return;

	const int ii = AllocateItem();
	auto &item = Items[ii];

	while (true) {
		item = {};
		SetupAllItems(*MyPlayer, item, idx, AdvanceRndSeed(), 2 * lvl, 1, true, delta);
		SetupItem(item);
		if (item._iMiscId == IMISC_BOOK && item._iSpell == ispell)
			break;
	}
	GetSuperItemSpace(position, ii);

	if (sendmsg)
		NetSendCmdPItem(false, CMD_DROPITEM, item.position, item);
	if (delta)
		DeltaAddItem(ii);
}

void CreateMagicArmor(Point position, ItemType itemType, int icurs, bool sendmsg, bool delta)
{
	const int lvl = ItemsGetCurrlevel();
	CreateMagicItem(position, lvl, itemType, IMISC_NONE, icurs, sendmsg, delta);
}

void CreateAmulet(Point position, int lvl, bool sendmsg, bool delta, bool spawn /*= false*/)
{
	CreateMagicItem(position, lvl, ItemType::Amulet, IMISC_AMULET, ICURS_AMULET, sendmsg, delta, spawn);
}

void CreateMagicWeapon(Point position, ItemType itemType, int icurs, bool sendmsg, bool delta)
{
	int imid = IMISC_NONE;
	if (itemType == ItemType::Staff)
		imid = IMISC_STAFF;

	const int curlv = ItemsGetCurrlevel();

	CreateMagicItem(position, curlv, itemType, imid, icurs, sendmsg, delta);
}

bool GetItemRecord(uint32_t nSeed, uint16_t wCI, int nIndex)
{
	const uint32_t ticks = SDL_GetTicks();

	for (int i = 0; i < gnNumGetRecords; i++) {
		if (ticks - itemrecord[i].dwTimestamp > 6000) {
			// BUGFIX: loot actions for multiple quest items with same seed (e.g. blood stone) performed within less than 6 seconds will be ignored.
			NextItemRecord(i);
			i--;
		} else if (nSeed == itemrecord[i].nSeed && wCI == itemrecord[i].wCI && nIndex == itemrecord[i].nIndex) {
			return false;
		}
	}

	return true;
}

void SetItemRecord(uint32_t nSeed, uint16_t wCI, int nIndex)
{
	const uint32_t ticks = SDL_GetTicks();

	if (gnNumGetRecords == MAXITEMS) {
		return;
	}

	itemrecord[gnNumGetRecords].dwTimestamp = ticks;
	itemrecord[gnNumGetRecords].nSeed = nSeed;
	itemrecord[gnNumGetRecords].wCI = wCI;
	itemrecord[gnNumGetRecords].nIndex = nIndex;
	gnNumGetRecords++;
}

void PutItemRecord(uint32_t nSeed, uint16_t wCI, int nIndex)
{
	const uint32_t ticks = SDL_GetTicks();

	for (int i = 0; i < gnNumGetRecords; i++) {
		if (ticks - itemrecord[i].dwTimestamp > 6000) {
			NextItemRecord(i);
			i--;
		} else if (nSeed == itemrecord[i].nSeed && wCI == itemrecord[i].wCI && nIndex == itemrecord[i].nIndex) {
			NextItemRecord(i);
			break;
		}
	}
}

bool Item::isUsable() const
{
	if (IDidx == IDI_SPECELIX && Quests[Q_MUSHROOM]._qactive != QUEST_DONE)
		return false;
	return AllItemsList[IDidx].iUsable;
}

void Item::setNewAnimation(bool showAnimation)
{
	const int8_t it = ItemCAnimTbl[_iCurs];
	const int8_t numberOfFrames = ItemAnimLs[it];
	const OptionalClxSpriteList sprite = itemanims[it] ? OptionalClxSpriteList { *itemanims[static_cast<size_t>(it)] } : std::nullopt;
	if (_iCurs != ICURS_MAGIC_ROCK)
		AnimInfo.setNewAnimation(sprite, numberOfFrames, 1, AnimationDistributionFlags::ProcessAnimationPending, 0, numberOfFrames);
	else
		AnimInfo.setNewAnimation(sprite, numberOfFrames, 1);
	_iPostDraw = false;
	_iRequest = false;
	if (showAnimation) {
		_iAnimFlag = true;
		selectionRegion = SelectionRegion::None;
	} else {
		AnimInfo.currentFrame = AnimInfo.numberOfFrames - 1;
		_iAnimFlag = false;
		selectionRegion = SelectionRegion::Bottom;
	}
}

void Item::updateRequiredStatsCacheForPlayer(const Player &player)
{
	if (_itype == ItemType::Misc && _iMiscId == IMISC_BOOK) {
		_iMinMag = GetSpellData(_iSpell).minInt;
		int8_t spellLevel = player._pSplLvl[static_cast<int8_t>(_iSpell)];
		while (spellLevel != 0) {
			_iMinMag += 20 * _iMinMag / 100;
			spellLevel--;
			if (_iMinMag + 20 * _iMinMag / 100 > 255) {
				_iMinMag = 255;
				spellLevel = 0;
			}
		}
	}
	_iStatFlag = player.CanUseItem(*this);
}

StringOrView Item::getName() const
{
	if (isEmpty()) {
		return std::string_view("");
	}
	if (!_iIdentified || _iCreateInfo == 0 || _iMagical == ITEM_QUALITY_NORMAL) {
		return GetTranslatedItemName(*this);
	}
	if (_iMagical == ITEM_QUALITY_UNIQUE) {
		return _(UniqueItems[_iUid].UIName);
	}
	if (_iMagical == ITEM_QUALITY_RARE) {
		return GetRareItemName(*this);
	}
	if (_iPoe2Mods[0].family != 0) {
		return std::string_view(_iIName);
	}
	return GetTranslatedItemNameMagical(*this, dwBuff & CF_HELLFIRE, true, std::nullopt);
}

bool CornerStoneStruct::isAvailable()
{
	return currlevel == 21 && !gbIsMultiplayer;
}

void initItemGetRecords()
{
	memset(itemrecord, 0, sizeof(itemrecord));
	gnNumGetRecords = 0;
}

void RepairItem(Item &item, int lvl)
{
	if (item._iDurability == item._iMaxDur) {
		return;
	}

	if (item._iMaxDur <= 0) {
		item.clear();
		return;
	}

	int rep = 0;
	do {
		rep += lvl + GenerateRnd(lvl);
		item._iMaxDur -= std::max(item._iMaxDur / (lvl + 9), 1);
		if (item._iMaxDur == 0) {
			item.clear();
			return;
		}
	} while (rep + item._iDurability < item._iMaxDur);

	item._iDurability = std::min<int>(item._iDurability + rep, item._iMaxDur);
}

void RechargeItem(Item &item, Player &player)
{
	if (item._itype != ItemType::Staff || !IsValidSpell(item._iSpell))
		return;

	if (item._iCharges == item._iMaxCharges)
		return;

	const int rechargeStrength = RandomIntBetween(1, player.getCharacterLevel() / GetSpellStaffLevel(item._iSpell));

	do {
		item._iMaxCharges--;
		if (item._iMaxCharges == 0) {
			return;
		}
		item._iCharges += rechargeStrength;
	} while (item._iCharges < item._iMaxCharges);

	item._iCharges = std::min(item._iCharges, item._iMaxCharges);

	if (&player != MyPlayer)
		return;
	if (&item == &player.InvBody[INVLOC_HAND_LEFT]) {
		NetSendCmdChItem(true, INVLOC_HAND_LEFT);
		return;
	}
	if (&item == &player.InvBody[INVLOC_HAND_RIGHT]) {
		NetSendCmdChItem(true, INVLOC_HAND_RIGHT);
		return;
	}
	for (int i = 0; i < player._pNumInv; i++) {
		if (&item == &player.InvList[i]) {
			NetSyncInvItem(player, i);
			break;
		}
	}
}

/** @brief POE2 Mashup: a currency's sound as it takes hold, layered from Diablo's own effects: a clink, then its magic. */
void PlayCurrencySound(CurrencyOrb orb, item_quality before, const Item &item)
{
	if (CurrencyDestroyedItem) {
		PlaySFX(SfxID::BarrelBreak);
		return;
	}
	switch (orb) {
	case CurrencyOrb::Whetstone:
		PlaySFX(SfxID::ItemSword);
		PlaySFX(SfxID::ItemAnvil);
		return;
	case CurrencyOrb::Scrap:
		PlaySFX(SfxID::ItemArmor);
		PlaySFX(SfxID::ItemAnvil);
		return;
	case CurrencyOrb::Etcher:
		PlaySFX(SfxID::ItemStaff);
		PlaySFX(SfxID::SpellRepair);
		return;
	case CurrencyOrb::Bauble:
	case CurrencyOrb::Catalyst:
		PlaySFX(SfxID::ItemPotion);
		PlaySFX(SfxID::SpellRepair);
		return;
	case CurrencyOrb::Chaos:
	case CurrencyOrb::GreaterChaos:
	case CurrencyOrb::PerfectChaos:
		PlaySFX(SfxID::ItemRing);
		PlaySFX(SfxID::SpellBloodStar);
		return;
	case CurrencyOrb::Scouring:
	case CurrencyOrb::Annul:
		PlaySFX(SfxID::ItemRing);
		PlaySFX(SfxID::SpellEtherealize);
		return;
	case CurrencyOrb::Divine:
		PlaySFX(SfxID::ItemRing);
		PlaySFX(SfxID::SpellResurrect);
		return;
	case CurrencyOrb::Vaal:
	case CurrencyOrb::Desecrate:
		PlaySFX(SfxID::ItemBodyPart);
		PlaySFX(SfxID::SpellBoneSpirit);
		return;
	case CurrencyOrb::Chance:
		PlaySFX(SfxID::ItemRing);
		PlaySFX(item._iMagical == ITEM_QUALITY_UNIQUE ? SfxID::QuestDone : SfxID::OperateShrine);
		return;
	case CurrencyOrb::Artificer:
	case CurrencyOrb::Augment:
		PlaySFX(SfxID::ItemRock);
		PlaySFX(SfxID::ItemAnvil);
		return;
	default:
		break;
	}
	// Orbs that add modifiers: brighter the higher the item climbs.
	PlaySFX(SfxID::ItemRing);
	if (item._iMagical == ITEM_QUALITY_RARE && before != ITEM_QUALITY_RARE)
		PlaySFX(SfxID::SpellHolyBolt);
	else if (IsAnyOf(orb, CurrencyOrb::Exalted, CurrencyOrb::GreaterExalted, CurrencyOrb::PerfectExalted, CurrencyOrb::Regal, CurrencyOrb::Essence))
		PlaySFX(SfxID::SpellHolyBolt);
	else
		PlaySFX(SfxID::SpellInfravision);
}

bool ApplyOilToItem(Item &item, Player &player)
{
	int r;

	if (const CurrencyOrb orb = CurrencyOrbForMiscId(player._pOilType); orb != CurrencyOrb::None) {
		const item_quality before = item._iMagical;
		const bool applied = ApplyCurrencyOrb(orb, player, item, player.poe2.currencyVariant);
		if (applied && &player == MyPlayer)
			PlayCurrencySound(orb, before, item);
		return applied;
	}

	if (item._iClass == ICLASS_MISC) {
		return false;
	}
	if (item._iClass == ICLASS_GOLD) {
		return false;
	}
	if (item._iClass == ICLASS_QUEST) {
		return false;
	}

	switch (player._pOilType) {
	case IMISC_OILACC:
	case IMISC_OILMAST:
	case IMISC_OILSHARP:
		if (item._iClass == ICLASS_ARMOR) {
			return false;
		}
		break;
	case IMISC_OILDEATH:
		if (item._iClass == ICLASS_ARMOR) {
			return false;
		}
		if (item._itype == ItemType::Bow) {
			return false;
		}
		break;
	case IMISC_OILHARD:
	case IMISC_OILIMP:
		if (item._iClass == ICLASS_WEAPON) {
			return false;
		}
		break;
	default:
		break;
	}

	switch (player._pOilType) {
	case IMISC_OILACC:
		if (item._iPLToHit < 50) {
			item._iPLToHit += RandomIntBetween(1, 2);
		}
		break;
	case IMISC_OILMAST:
		if (item._iPLToHit < 100) {
			item._iPLToHit += RandomIntBetween(3, 5);
		}
		break;
	case IMISC_OILSHARP:
		if (item._iMaxDam - item._iMinDam < 30 && item._iMaxDam < 255) {
			item._iMaxDam = item._iMaxDam + 1;
		}
		break;
	case IMISC_OILDEATH:
		if (item._iMaxDam - item._iMinDam < 30 && item._iMaxDam < 254) {
			item._iMinDam = item._iMinDam + 1;
			item._iMaxDam = item._iMaxDam + 2;
		}
		break;
	case IMISC_OILSKILL:
		r = RandomIntBetween(5, 10);
		item._iMinStr = std::max(0, item._iMinStr - r);
		item._iMinMag = std::max(0, item._iMinMag - r);
		item._iMinDex = std::max(0, item._iMinDex - r);
		break;
	case IMISC_OILBSMTH:
		if (item._iMaxDur == DUR_INDESTRUCTIBLE)
			return true;
		if (item._iDurability < item._iMaxDur) {
			item._iDurability = (item._iMaxDur + 4) / 5 + item._iDurability;
			item._iDurability = std::min<int>(item._iDurability, item._iMaxDur);
		} else {
			if (item._iMaxDur >= 100) {
				return true;
			}
			item._iMaxDur++;
			item._iDurability = item._iMaxDur;
		}
		break;
	case IMISC_OILFORT:
		if (item._iMaxDur != DUR_INDESTRUCTIBLE && item._iMaxDur < 200) {
			r = RandomIntBetween(10, 50);
			item._iMaxDur += r;
			item._iDurability += r;
		}
		break;
	case IMISC_OILPERM:
		item._iDurability = DUR_INDESTRUCTIBLE;
		item._iMaxDur = DUR_INDESTRUCTIBLE;
		break;
	case IMISC_OILHARD:
		if (item._iAC < 60) {
			item._iAC += RandomIntBetween(1, 2);
		}
		break;
	case IMISC_OILIMP:
		if (item._iAC < 120) {
			item._iAC += RandomIntBetween(3, 5);
		}
		break;
	default:
		return false;
	}
	return true;
}

void UpdateHellfireFlag(Item &item, const char *identifiedItemName)
{
	// DevilutionX support vanilla and hellfire items in one save file and for that introduced CF_HELLFIRE
	// But vanilla hellfire items don't have CF_HELLFIRE set in Item::dwBuff
	// This functions tries to set this flag for vanilla hellfire items based on the item name
	// This ensures that Item::getName() returns the correct translated item name
	if ((item.dwBuff & CF_HELLFIRE) != 0U)
		return; // Item is already a hellfire item
	if (item._iMagical != ITEM_QUALITY_MAGIC)
		return; // Only magic item's name can differ between diablo and hellfire
	if (gbIsMultiplayer)
		return; // Vanilla hellfire multiplayer is not supported in devilutionX, so there can't be items with missing dwBuff from there
	// We need to test both short and long name, because StringInPanel can return a different result (other font and some bugfixes)
	const std::string diabloItemNameShort = GetTranslatedItemNameMagical(item, false, false, false);
	if (diabloItemNameShort == identifiedItemName)
		return; // Diablo item name is identical => not a hellfire specific item
	const std::string diabloItemNameLong = GetTranslatedItemNameMagical(item, false, false, true);
	if (diabloItemNameLong == identifiedItemName)
		return; // Diablo item name is identical => not a hellfire specific item
	const std::string hellfireItemNameShort = GetTranslatedItemNameMagical(item, true, false, false);
	const std::string hellfireItemNameLong = GetTranslatedItemNameMagical(item, true, false, true);
	if (hellfireItemNameShort == identifiedItemName || hellfireItemNameLong == identifiedItemName) {
		// This item should be a vanilla hellfire item that has CF_HELLFIRE missing, cause only then the item name matches
		item.dwBuff |= CF_HELLFIRE;
	}
}

} // namespace devilution
