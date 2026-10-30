#include "poe2/bases.hpp"

#include <algorithm>
#include <string_view>
#include <unordered_map>

#include <magic_enum/magic_enum.hpp>

#include "data/file.hpp"
#include "data/record_reader.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/default_attack.hpp"
#include "poe2/defenses.hpp"
#include "poe2/skill_looks.hpp"
#include "poe2/uniques.hpp"
#include "poe2/weapon_kinds.hpp"
#include "tables/itemdat.h"
#include "utils/format.hpp"
#include "utils/log.hpp"
#include "utils/parse_int.hpp"
#include "utils/str_split.hpp"

namespace devilution {

namespace {

std::vector<BaseMeta> Bases;
/** Base metadata index + 1 for each row of the item data; 0 for none. */
std::vector<uint16_t> BaseOfItem;

std::optional<DefenseType> ParseDefense(std::string_view text)
{
	if (text.empty())
		return std::nullopt;
	return magic_enum::enum_cast<DefenseType>(text);
}

QualityKind ParseQuality(std::string_view text)
{
	if (text == "weapon")
		return QualityKind::Weapon;
	if (text == "defence")
		return QualityKind::Defence;
	if (text == "caster")
		return QualityKind::Caster;
	return QualityKind::None;
}

std::vector<BaseImplicit> ParseImplicits(std::string_view text)
{
	std::vector<BaseImplicit> implicits;
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
		const std::optional<ModEffect> effect = magic_enum::enum_cast<ModEffect>(parts[0]);
		if (count != parts.size() || !effect) {
			LogError("Bad base implicit: {}", entry);
			continue;
		}
		implicits.push_back({ *effect, static_cast<int16_t>(ParseInt<int>(parts[1]).value_or(0)), static_cast<int16_t>(ParseInt<int>(parts[2]).value_or(0)), std::string(parts[3]) });
	}
	return implicits;
}

/** Percents separated by '/', like 48/48; empty for none. */
DefenseSplit ParseSplit(std::string_view text)
{
	DefenseSplit split;
	for (const std::string_view part : SplitByChar(text, '/')) {
		if (part.empty())
			continue;
		const int percent = ParseInt<int>(part).value_or(-1);
		if (percent < 0 || percent > 100 || split.parts >= split.percent.size()) {
			LogError("Bad defence split: {}", text);
			return {};
		}
		split.percent[split.parts++] = static_cast<uint8_t>(percent);
	}
	return split;
}

/** A value from the item's seed, apart from the game's random number generator. */
uint32_t ImplicitHash(uint32_t seed, size_t index)
{
	uint32_t hash = (seed ^ 0x85EBCA6BU) + (static_cast<uint32_t>(index) * 0x9E3779B9U);
	hash ^= hash >> 16;
	hash *= 0x7FEB352DU;
	hash ^= hash >> 15;
	hash *= 0x846CA68BU;
	hash ^= hash >> 16;
	return hash;
}

} // namespace

SkillSlot ParseGrantedSkill(std::string_view key)
{
	if (key.empty())
		return {};
	// Staff Bolt is the default attack, made spell-based on the new staves; it has no WeaponSkill.
	if (key == "StaffBolt")
		return SkillSlot::ForDefault(static_cast<uint8_t>(DefaultAttack::StaffBolt));
	return SkillSlotFromEnumName(key);
}

uint16_t ParseElementLock(std::string_view text)
{
	uint16_t bits = 0;
	for (const std::string_view name : SplitByChar(text, ',')) {
		if (name == "fire")
			bits |= ModBaseFireSpell;
		else if (name == "cold")
			bits |= ModBaseColdSpell;
		else if (name == "lightning")
			bits |= ModBaseLightningSpell;
		else if (name == "physical")
			bits |= ModBasePhysicalSpell;
		else if (!name.empty())
			LogError("Bad base element: {}", name);
	}
	return bits;
}

void LoadBaseData()
{
	Bases.clear();
	BaseOfItem.clear();
	if (!gbIsPoe2) {
		LoadWeaponKinds();
		return;
	}

	constexpr std::string_view Filename = "txtdata\\poe2\\base_meta.tsv";
	auto dataFile = DataFile::load(Filename);
	if (!dataFile.has_value()) {
		LogError("Couldn't load the item bases: {}", Filename);
		return;
	}
	dataFile->skipHeaderOrDie(Filename);
	std::unordered_map<std::string, uint16_t> byName;
	for (DataFileRecord record : *dataFile) {
		RecordReader reader { record, Filename };
		BaseMeta base {};
		std::string text;
		reader.readString("name", base.name);
		reader.readString("class", base.itemClass);
		reader.readString("identity", base.identity);
		reader.readString("defense", text);
		base.defense = ParseDefense(text);
		reader.readString("quality", text);
		base.quality = ParseQuality(text);
		reader.readString("implicits", text);
		base.implicits = ParseImplicits(text);
		reader.readString("split", text);
		base.split = ParseSplit(text);
		reader.readInt("level", base.level);
		int block = 0;
		reader.readOptionalInt("block", block);
		base.block = static_cast<uint8_t>(std::clamp(block, 0, 100));
		int aps = 0;
		reader.readOptionalInt("aps", aps);
		base.aps = static_cast<uint16_t>(std::clamp(aps, 0, 1000));
		// The caster weapons rework's columns; rows from before it may leave them out.
		reader.readTrailingString("grants", text);
		base.grantedSkill = ParseGrantedSkill(text);
		if (!text.empty() && base.grantedSkill.kind == SkillKind::None)
			LogError("Base {} grants an unknown skill: {}", base.name, text);
		reader.readTrailingString("element", text);
		base.elementLock = ParseElementLock(text);
		reader.readTrailingString("crit", text);
		// An empty cell leaves the default crit; "0" is a real 0%.
		base.crit = text.empty() ? static_cast<int16_t>(-1) : static_cast<int16_t>(std::clamp(ParseInt<int>(text).value_or(-1), -1, 1000));
		byName[base.name] = static_cast<uint16_t>(Bases.size() + 1);
		Bases.push_back(std::move(base));
	}
	BaseOfItem.assign(AllItemsList.size(), 0);
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		if (const auto it = byName.find(AllItemsList[i].iName); it != byName.end())
			BaseOfItem[i] = it->second;
	}
	LoadWeaponKinds();
}

const BaseMeta *BaseMetaFor(int itemIndex)
{
	if (itemIndex < 0 || static_cast<size_t>(itemIndex) >= BaseOfItem.size())
		return nullptr;
	const uint16_t index = BaseOfItem[static_cast<size_t>(itemIndex)];
	return index == 0 ? nullptr : &Bases[index - 1];
}

const BaseMeta *ItemBaseMeta(const Item &item)
{
	return item.isEmpty() ? nullptr : BaseMetaFor(item.IDidx);
}

int ShieldBlockChance(const Item &item)
{
	if (item.isEmpty() || item._itype != ItemType::Shield)
		return 0;
	// A POE2 base says; a focus has none.
	if (const BaseMeta *base = ItemBaseMeta(item); base != nullptr)
		return base->block;
	// Diablo's shields, by the look of their base (the Warrior's old Buckler has no unique type to go by).
	const ItemData &data = AllItemsList[item.IDidx];
	if (data.iSName == "Focus")
		return 0;
	switch (data.iCurs) {
	case ICURS_KITE_SHIELD:
		return 27;
	case ICURS_LARGE_SHIELD:
	case ICURS_TOWER_SHIELD:
	case ICURS_GOTHIC_SHIELD:
	case ICURS_BLACKOAK_SHIELD:
		return 30;
	default:
		// Bucklers and small shields.
		return 25;
	}
}

std::vector<ItemImplicit> ItemImplicits(const Item &item)
{
	std::vector<ItemImplicit> implicits;
	const BaseMeta *base = ItemBaseMeta(item);
	if (base == nullptr)
		return implicits;
	const int quality = ItemQuality(item);
	for (size_t i = 0; i < base->implicits.size(); i++) {
		const BaseImplicit &implicit = base->implicits[i];
		ItemImplicit rolled { &implicit, { implicit.min, implicit.max } };
		// Added damage keeps its range; everything else rolls one value in it.
		if (implicit.effect != ModEffect::AddedPhysicalToAttacks) {
			const int span = implicit.max - implicit.min + 1;
			rolled.values[0] = implicit.min + (span > 1 ? static_cast<int>(ImplicitHash(static_cast<uint32_t>(item._iSeed), i) % static_cast<uint32_t>(span)) : 0);
			rolled.values[1] = 0;
		}
		// Quality on staves and sceptres raises their spell power.
		if (base->quality == QualityKind::Caster && implicit.effect == ModEffect::Intelligence)
			rolled.values[0] += rolled.values[0] * quality / 100;
		implicits.push_back(rolled);
	}
	return implicits;
}

uint64_t ImplicitEffectBits(const Item &item)
{
	uint64_t bits = 0;
	for (const ItemImplicit &rolled : ItemImplicits(item)) {
		if (rolled.values[0] > 0 || rolled.values[1] > 0)
			bits |= ModEffectBits(rolled.implicit->effect);
	}
	return bits;
}

void ApplyBaseProperties(Item &item)
{
	if (!gbIsPoe2)
		return;
	for (const ItemImplicit &implicit : ItemImplicits(item))
		ApplyModEffect(item, implicit.implicit->effect, implicit.values[0], implicit.values[1]);
	const QualityKind kind = ItemQualityKind(item);
	const int quality = ItemQuality(item);
	if (quality == 0)
		return;
	if (kind == QualityKind::Weapon)
		item._iPLDam = static_cast<int16_t>(item._iPLDam + quality);
	else if (kind == QualityKind::Defence)
		item._iPLAC = static_cast<int16_t>(item._iPLAC + quality);
}

std::vector<std::string> ItemImplicitText(const Item &item)
{
	std::vector<std::string> lines;
	for (const ItemImplicit &implicit : ItemImplicits(item)) {
		// Flat energy shield rolls in defence points, like the modifier.
		const int shown = implicit.implicit->effect == ModEffect::FlatEnergyShield ? implicit.values[0] * EnergyShieldPerDefense : implicit.values[0];
		lines.push_back(FormatRuntime(implicit.implicit->text, shown, implicit.values[1]));
	}
	return lines;
}

int ItemRequiredLevel(const Item &item)
{
	if (!gbIsPoe2)
		return 0;
	const BaseMeta *base = ItemBaseMeta(item);
	if (base == nullptr)
		return 0;
	int level = base->level;
	if (item._iMagical == ITEM_QUALITY_UNIQUE && item._iUid >= 0 && static_cast<size_t>(item._iUid) < UniqueItems.size()) {
		// A bundle unique's drop level is its own; a Diablo unique's is its table's level.
		const Poe2Unique *unique = Poe2UniqueFor(item._iUid);
		level = std::max(level, unique != nullptr ? unique->dropLevel : static_cast<int>(UniqueItems[static_cast<size_t>(item._iUid)].UIMinLvl));
	}
	return level;
}

bool MeetsItemLevelRequirement(const Player &player, const Item &item)
{
	return player.getCharacterLevel() >= ItemRequiredLevel(item);
}

bool IsStarterBase(const Item &item)
{
	const BaseMeta *base = ItemBaseMeta(item);
	return base != nullptr && base->level <= MaxStarterBaseLevel;
}

void ClearStarterBaseRequirements(Item &item)
{
	if (!IsStarterBase(item))
		return;
	item._iMinStr = 0;
	item._iMinMag = 0;
	item._iMinDex = 0;
}

DefenseSplit ItemDefenseSplit(const Item &item, int defense)
{
	const BaseMeta *base = ItemBaseMeta(item);
	if (base == nullptr || base->split.parts == 0)
		return {};
	// Every base now rolls exactly its defence, which only oils raise later; less is an older item's smaller base.
	if (defense < AllItemsList[static_cast<size_t>(item.IDidx)].iMinAC)
		return {};
	return base->split;
}

QualityKind QualityKindFor(int itemIndex)
{
	if (const BaseMeta *base = BaseMetaFor(itemIndex); base != nullptr)
		return base->quality;
	if (itemIndex < 0 || static_cast<size_t>(itemIndex) >= AllItemsList.size())
		return QualityKind::None;
	switch (AllItemsList[static_cast<size_t>(itemIndex)].itype) {
	case ItemType::LightArmor:
	case ItemType::MediumArmor:
	case ItemType::HeavyArmor:
	case ItemType::Helm:
	case ItemType::Shield:
		return QualityKind::Defence;
	case ItemType::Sword:
	case ItemType::Axe:
	case ItemType::Mace:
	case ItemType::Bow:
		return QualityKind::Weapon;
	case ItemType::Staff:
		return QualityKind::Caster;
	default:
		return QualityKind::None;
	}
}

QualityKind ItemQualityKind(const Item &item)
{
	return item.isEmpty() ? QualityKind::None : QualityKindFor(item.IDidx);
}

int ItemQuality(const Item &item)
{
	// Rings and amulets have no quality kind but hold a catalyst's quality.
	if (ItemBaseMeta(item) == nullptr && ItemQualityKind(item) == QualityKind::None)
		return 0;
	return static_cast<int>((item.dwBuff & CF_POE2_QUALITY) >> 23);
}

void SetItemQuality(Item &item, int quality)
{
	quality = std::clamp(quality, 0, MaxExceptionalQuality);
	item.dwBuff = (item.dwBuff & ~static_cast<uint32_t>(CF_POE2_QUALITY)) | (static_cast<uint32_t>(quality) << 23);
}

bool IsExceptional(const Item &item)
{
	return ItemQuality(item) > MaxItemQuality;
}

bool HasQuality(const Item &item)
{
	return ItemQualityKind(item) != QualityKind::None;
}

int LocalQuality(const Item &item, QualityKind kind)
{
	// ApplyBaseProperties only adds it to items made in POE2 mode.
	if ((item.dwBuff & CF_POE2) == 0 || ItemQualityKind(item) != kind)
		return 0;
	return ItemQuality(item);
}

int WeaponDamagePercent(const Item &item)
{
	const int quality = LocalQuality(item, QualityKind::Weapon);
	const bool identified = item._iMagical == ITEM_QUALITY_NORMAL || item._iIdentified;
	const int increased = identified ? item._iPLDam - quality : 0;
	return ((100 + increased) * (100 + quality) / 100) - 100;
}

} // namespace devilution
