#include "poe2/stash_layouts.hpp"

#include <algorithm>
#include <array>

#include "poe2/belt_uniques.hpp"
#include "poe2/crafting_items.hpp"
#include "poe2/gemcutting.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/stacking.hpp"
#include "poe2/support_gems.hpp"
#include "poe2/uniques.hpp"
#include "poe2/waystones.hpp"
#include "utils/language.h"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

constexpr std::array<CurrencySectionRange, static_cast<size_t>(CurrencySection::Count)> CurrencySections = { {
    { 0, 30 },   // Orbs
    { 30, 10 },  // Catalysts
    { 40, 10 },  // Augments
    { 50, 20 },  // Other
    { 70, 50 },  // Essences: 12 kinds of 4 tiers (the legacy kinds' places, holding what they became)
    { 120, 30 }, // Omens
} };

/** The orbs in the order of their slots. Saved slot numbers: only ever add to the end. */
constexpr item_misc_id OrbSlots[] = {
	IMISC_ORB_TRANSMUTE,
	IMISC_ORB_AUGMENT,
	IMISC_ORB_REGAL,
	IMISC_ORB_ALCHEMY,
	IMISC_ORB_CHAOS,
	IMISC_ORB_GREATER_CHAOS,
	IMISC_ORB_PERFECT_CHAOS,
	IMISC_ORB_EXALTED,
	IMISC_ORB_GREATER_EXALTED,
	IMISC_ORB_PERFECT_EXALTED,
	IMISC_ORB_DIVINE,
	IMISC_ORB_VAAL,
	IMISC_ORB_ANNUL,
	IMISC_ORB_CHANCE,
	IMISC_ORB_SCOURING,
	IMISC_ORB_ARTIFICER,
	IMISC_ORB_DESECRATE,
	IMISC_ORB_WHETSTONE,
	IMISC_ORB_ETCHER,
	IMISC_ORB_SCRAP,
	IMISC_ORB_BAUBLE,
	IMISC_ORB_LESSER_JEWELLER,
	IMISC_ORB_GREATER_JEWELLER,
	IMISC_ORB_PERFECT_JEWELLER,
	// POE2 Mashup: shards, which turn into their orbs here at ten (Curtis 2026-10-29).
	IMISC_SHARD_TRANSMUTE,
	IMISC_SHARD_REGAL,
	IMISC_SHARD_CHANCE,
	IMISC_SHARD_ARTIFICER,
};

constexpr unsigned EssenceTiers = static_cast<unsigned>(EssenceTier::Count);

static_assert(std::size(OrbSlots) <= 30);
static_assert(static_cast<unsigned>(Catalyst::Count) - 1 <= 10);
static_assert(LegacyAugmentCount <= 10);
static_assert(LegacyEssenceKindCount * EssenceTiers <= 50);
static_assert(static_cast<unsigned>(OmenId::Count) - 1 <= 30);

std::optional<size_t> ItemIndexForMiscId(item_misc_id miscId)
{
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		if (AllItemsList[i].iMiscId == miscId)
			return i;
	}
	return std::nullopt;
}

/** The augment section's place of the item's augment: the place of the legacy id it is, or that became it. */
std::optional<unsigned> AugmentPlace(AugmentId id)
{
	for (int legacy = 1; legacy <= LegacyAugmentCount; legacy++) {
		const auto legacyId = static_cast<AugmentId>(legacy);
		if (id == legacyId || id == CurrentAugmentId(legacyId))
			return static_cast<unsigned>(legacy - 1);
	}
	return std::nullopt;
}

/** The same for the essence sections' kinds. */
std::optional<unsigned> EssencePlace(EssenceKind kind)
{
	for (int legacy = 1; legacy <= LegacyEssenceKindCount; legacy++) {
		const auto legacyKind = static_cast<EssenceKind>(legacy);
		if (kind == legacyKind || kind == CurrentEssenceKind(legacyKind))
			return static_cast<unsigned>(legacy - 1);
	}
	return std::nullopt;
}

} // namespace

CurrencySectionRange GetCurrencySection(CurrencySection section)
{
	return CurrencySections[std::min(static_cast<size_t>(section), CurrencySections.size() - 1)];
}

std::string_view CurrencySectionName(CurrencySection section)
{
	switch (section) {
	case CurrencySection::Orbs:
		return _("Orbs");
	case CurrencySection::Catalysts:
		return _("Catalysts");
	case CurrencySection::Augments:
		return _("Runes, Soul Cores and Talismans");
	case CurrencySection::Other:
		return _("Other currency");
	case CurrencySection::Essences:
		return _("Essences");
	case CurrencySection::Omens:
		return _("Omens");
	default:
		return {};
	}
}

CurrencySlotKind GetCurrencySlotKind(unsigned slot)
{
	const auto inSection = [slot](CurrencySection section) -> std::optional<unsigned> {
		const CurrencySectionRange range = GetCurrencySection(section);
		if (slot >= range.first && slot < range.first + range.count)
			return slot - range.first;
		return std::nullopt;
	};
	if (const auto i = inSection(CurrencySection::Orbs); i && *i < std::size(OrbSlots))
		return { OrbSlots[*i], 0 };
	if (const auto i = inSection(CurrencySection::Catalysts); i && *i + 1 < static_cast<unsigned>(Catalyst::Count))
		return { IMISC_ORB_CATALYST, static_cast<uint8_t>(*i + 1) };
	if (const auto i = inSection(CurrencySection::Augments); i && *i < static_cast<unsigned>(LegacyAugmentCount))
		return { IMISC_AUGMENT, static_cast<uint8_t>(CurrentAugmentId(static_cast<AugmentId>(*i + 1))) };
	if (const auto i = inSection(CurrencySection::Essences); i && *i / EssenceTiers < static_cast<unsigned>(LegacyEssenceKindCount)) {
		const EssenceKind kind = CurrentEssenceKind(static_cast<EssenceKind>((*i / EssenceTiers) + 1));
		const auto tier = static_cast<EssenceTier>(*i % EssenceTiers);
		return { IMISC_ESSENCE, EssenceVariant(kind, tier) };
	}
	if (const auto i = inSection(CurrencySection::Omens); i && *i + 1 < static_cast<unsigned>(OmenId::Count))
		return { IMISC_OMEN, static_cast<uint8_t>(*i + 1) };
	return {};
}

std::optional<unsigned> CurrencyFixedSlot(const Item &item)
{
	if (item.isEmpty() || !IsStackable(item))
		return std::nullopt;
	const auto section = [](CurrencySection s) { return GetCurrencySection(s).first; };
	switch (item._iMiscId) {
	case IMISC_ORB_CATALYST: {
		const auto catalyst = static_cast<unsigned>(GetCatalyst(item));
		if (catalyst == 0 || catalyst >= static_cast<unsigned>(Catalyst::Count))
			return std::nullopt;
		return section(CurrencySection::Catalysts) + catalyst - 1;
	}
	case IMISC_AUGMENT: {
		const std::optional<unsigned> place = AugmentPlace(GetAugmentId(item));
		if (!place)
			return std::nullopt;
		return section(CurrencySection::Augments) + *place;
	}
	case IMISC_ESSENCE: {
		const std::optional<unsigned> place = EssencePlace(GetEssenceKind(item));
		if (!place)
			return std::nullopt;
		return section(CurrencySection::Essences) + (*place * EssenceTiers) + static_cast<unsigned>(GetEssenceTier(item));
	}
	case IMISC_OMEN: {
		const auto id = static_cast<unsigned>(GetOmenId(item));
		if (id == 0)
			return std::nullopt;
		return section(CurrencySection::Omens) + id - 1;
	}
	default:
		break;
	}
	const auto *found = std::find(std::begin(OrbSlots), std::end(OrbSlots), item._iMiscId);
	if (found == std::end(OrbSlots))
		return std::nullopt;
	return section(CurrencySection::Orbs) + static_cast<unsigned>(found - std::begin(OrbSlots));
}

bool MakeCurrencySlotItem(unsigned slot, Item &item)
{
	const CurrencySlotKind kind = GetCurrencySlotKind(slot);
	if (kind.miscId == IMISC_INVALID)
		return false;
	const std::optional<size_t> index = ItemIndexForMiscId(kind.miscId);
	if (!index)
		return false;
	item = {};
	InitializeItem(item, static_cast<_item_indexes>(*index));
	item._iIdentified = true;
	if (kind.variant != 0) {
		SetCraftingItemVariant(item, kind.variant);
		FinishCraftingItem(item);
	}
	return true;
}

int StoredStackCount(const Item &item)
{
	if (!IsStackable(item))
		return 1;
	return std::clamp(item._iCharges, 1, CurrencyTabStackMax);
}

void SetStoredStackCount(Item &item, int count)
{
	if (!IsStackable(item))
		return;
	item._iMaxCharges = StackMax(item);
	item._iCharges = std::clamp(count, 1, CurrencyTabStackMax);
}

std::string_view GemFilterName(GemFilter filter)
{
	switch (filter) {
	case GemFilter::Skill:
		return _("Skill");
	case GemFilter::Support:
		return _("Support");
	case GemFilter::Spirit:
		return _("Spirit");
	case GemFilter::Uncut:
		return _("Uncut");
	default:
		return _("All");
	}
}

namespace {

bool IsSupportGemItem(const Item &item)
{
	return SupportGemForItem(item) != SupportGem::None;
}

} // namespace

bool GemMatchesFilter(const Item &item, GemFilter filter)
{
	switch (filter) {
	case GemFilter::Skill:
		return IsSkillGem(item) && SkillForGem(item).kind != SkillKind::Persistent;
	case GemFilter::Support:
		return IsSupportGemItem(item);
	case GemFilter::Spirit:
		return IsSkillGem(item) && SkillForGem(item).kind == SkillKind::Persistent;
	case GemFilter::Uncut:
		return IsUncutGem(item);
	default:
		return true;
	}
}

GemSection GetGemSection(const Item &item)
{
	if (IsUncutGem(item))
		return { 0, UncutGemLevel(item) };
	if (IsSkillGem(item))
		return { 0, SkillGemItemLevel(item) };
	if (IsSupportGemItem(item))
		return { 1, SupportGemItemTier(item) };
	return { 2, 0 };
}

std::string GemSectionName(GemSection section)
{
	if (section.group == 0)
		return StrCat(_("Level"), " ", section.level);
	if (section.group == 1)
		return StrCat(_("Support tier"), " ", section.level);
	return std::string(_("Other"));
}

bool GemListsBefore(const Item &a, const Item &b)
{
	const GemSection sectionA = GetGemSection(a);
	const GemSection sectionB = GetGemSection(b);
	if (sectionA.group != sectionB.group)
		return sectionA.group < sectionB.group;
	if (sectionA.level != sectionB.level)
		return sectionA.level > sectionB.level;
	const int compared = std::string_view(a._iIName).compare(b._iIName);
	if (compared != 0)
		return compared < 0;
	return a._iSeed < b._iSeed;
}

int MapTabTier(const Item &item)
{
	if (IsWaystone(item))
		return std::clamp(WaystoneTier(item), 1, MaxWaystoneTier);
	return MapTabOtherTier;
}

bool MapListsBefore(const Item &a, const Item &b)
{
	const int tierA = MapTabTier(a);
	const int tierB = MapTabTier(b);
	if (tierA != tierB)
		return tierA < tierB;
	if (a._iMagical != b._iMagical)
		return a._iMagical > b._iMagical;
	const int compared = std::string_view(a._iIName).compare(b._iIName);
	if (compared != 0)
		return compared < 0;
	return a._iSeed < b._iSeed;
}

std::string_view UniqueCategoryName(UniqueCategory category)
{
	switch (category) {
	case UniqueCategory::Weapons:
		return _("Weapons");
	case UniqueCategory::Armour:
		return _("Armour");
	case UniqueCategory::Helmets:
		return _("Helmets");
	case UniqueCategory::Shields:
		return _("Shields");
	case UniqueCategory::Rings:
		return _("Rings");
	case UniqueCategory::Amulets:
		return _("Amulets");
	case UniqueCategory::Belt:
		return _("Flasks");
	default:
		return _("Other");
	}
}

namespace {

/** Belt uniques start at this slot, so new table uniques don't move them. */
constexpr size_t BeltUniqueSlots = 700;

UniqueCategory CategoryOfBase(const ItemData &data)
{
	if (data.itype == ItemType::Shield)
		return UniqueCategory::Shields;
	switch (data.iLoc) {
	case ILOC_ONEHAND:
	case ILOC_TWOHAND:
		return UniqueCategory::Weapons;
	case ILOC_ARMOR:
		return UniqueCategory::Armour;
	case ILOC_HELM:
		return UniqueCategory::Helmets;
	case ILOC_RING:
		return UniqueCategory::Rings;
	case ILOC_AMULET:
		return UniqueCategory::Amulets;
	case ILOC_BELT:
		return UniqueCategory::Belt;
	default:
		return UniqueCategory::Other;
	}
}

std::vector<UniqueCollectionEntry> Collection;
size_t CollectionUniqueCount = 0;
size_t CollectionBeltCount = 0;
size_t CollectionItemCount = 0;

void BuildCollection()
{
	if (CollectionUniqueCount == UniqueItems.size() && CollectionBeltCount == BeltUniques().size() && CollectionItemCount == AllItemsList.size() && !Collection.empty())
		return;
	CollectionUniqueCount = UniqueItems.size();
	CollectionBeltCount = BeltUniques().size();
	CollectionItemCount = AllItemsList.size();
	Collection.assign(BeltUniqueSlots + BeltUniques().size(), {});
	for (size_t uid = 0; uid < UniqueItems.size() && uid < BeltUniqueSlots; uid++) {
		const UniqueItem &unique = UniqueItems[uid];
		UniqueCollectionEntry &entry = Collection[uid];
		entry.name = unique.UIName;
		std::optional<size_t> base;
		if (const Poe2Unique *poe2 = Poe2UniqueFor(static_cast<int>(uid)); poe2 != nullptr) {
			if (poe2->baseItem >= 0 && static_cast<size_t>(poe2->baseItem) < AllItemsList.size())
				base = static_cast<size_t>(poe2->baseItem);
		} else {
			for (size_t i = 0; i < AllItemsList.size() && !base; i++) {
				if (AllItemsList[i].iItemId == unique.UIItemId && AllItemsList[i].iMiscId != IMISC_UNIQUE && IsItemAvailable(static_cast<int>(i)))
					base = i;
			}
			// Quest uniques such as the Undead Crown (Leoric's Crown) have only a base of their own.
			for (size_t i = 0; i < AllItemsList.size() && !base; i++) {
				if (AllItemsList[i].iItemId == unique.UIItemId && IsItemAvailable(static_cast<int>(i)))
					base = i;
			}
		}
		if (!base)
			continue;
		const ItemData &data = AllItemsList[*base];
		entry.cursor = unique.UICurs != ICURS_DEFAULT ? unique.UICurs : data.iCurs;
		entry.category = CategoryOfBase(data);
		entry.listed = true;
	}
	for (size_t i = 0; i < BeltUniques().size(); i++) {
		const BeltUnique &unique = BeltUniques()[i];
		UniqueCollectionEntry &entry = Collection[BeltUniqueSlots + i];
		entry.name = std::string(unique.name);
		const std::optional<int> base = BeltUniqueBase(unique);
		if (!base || *base < 0 || static_cast<size_t>(*base) >= AllItemsList.size())
			continue;
		entry.cursor = AllItemsList[static_cast<size_t>(*base)].iCurs;
		entry.category = UniqueCategory::Belt;
		entry.listed = true;
	}
}

} // namespace

const std::vector<UniqueCollectionEntry> &UniqueCollection()
{
	BuildCollection();
	return Collection;
}

size_t UniqueCollectionSlots()
{
	return BeltUniqueSlots + BeltUniques().size();
}

std::optional<unsigned> UniqueCollectionSlot(const Item &item)
{
	if (item.isEmpty() || item._iMagical != ITEM_QUALITY_UNIQUE)
		return std::nullopt;
	if (const BeltUnique *belt = GetBeltUnique(item); belt != nullptr) {
		const std::vector<BeltUnique> &all = BeltUniques();
		for (size_t i = 0; i < all.size(); i++) {
			if (&all[i] == belt)
				return static_cast<unsigned>(BeltUniqueSlots + i);
		}
		return std::nullopt;
	}
	if (item._iUid < 0 || static_cast<size_t>(item._iUid) >= UniqueItems.size() || static_cast<size_t>(item._iUid) >= BeltUniqueSlots)
		return std::nullopt;
	return static_cast<unsigned>(item._iUid);
}

} // namespace devilution
