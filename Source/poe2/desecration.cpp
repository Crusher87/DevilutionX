#include "poe2/desecration.hpp"

#include <algorithm>
#include <optional>

#include "player.h"
#include "plrmsg.h"
#include "poe2/bases.hpp"
#include "poe2/craft_state.hpp"
#include "poe2/loot.hpp"
#include "poe2/mix.hpp"
#include "poe2/modifiers.hpp"
#include "utils/format.hpp"
#include "utils/language.h"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

constexpr uint32_t SlotBit(ModSlot slot)
{
	return 1U << static_cast<unsigned>(slot);
}

constexpr uint32_t MartialSlots = SlotBit(ModSlot::OneHandMelee) | SlotBit(ModSlot::TwoHandMelee) | SlotBit(ModSlot::Bow) | SlotBit(ModSlot::Crossbow)
    | SlotBit(ModSlot::Spear) | SlotBit(ModSlot::Quarterstaff) | SlotBit(ModSlot::Flail) | SlotBit(ModSlot::Claw) | SlotBit(ModSlot::Dagger);
constexpr uint32_t CasterSlots = SlotBit(ModSlot::Wand) | SlotBit(ModSlot::Staff) | SlotBit(ModSlot::Sceptre) | SlotBit(ModSlot::Focus)
    | SlotBit(ModSlot::LegacyStaff) | SlotBit(ModSlot::LegacySceptre);
constexpr uint32_t ArmourSlots = SlotBit(ModSlot::Helmet) | SlotBit(ModSlot::BodyArmour) | SlotBit(ModSlot::Gloves) | SlotBit(ModSlot::Boots) | SlotBit(ModSlot::Shield) | SlotBit(ModSlot::Focus);

// Bundle desecration_rules: the modifiers only desecration gives.
const std::array<DesecratedMod, static_cast<size_t>(DesecratedModId::Count)> DesecratedMods { {
	{ DesecratedModId::None, "", false, 0, 0, 0, {}, "" },
	{ DesecratedModId::AshBeyondMeasure, "Ash Beyond Measure", false, SlotBit(ModSlot::Ring) | SlotBit(ModSlot::Amulet), 65, 20,
	    { { { UniqueRule::FireOvercapDamage, 4 }, { UniqueRule::None, 0 } } }, "1% more damage per 4% fire resistance above its maximum" },
	{ DesecratedModId::HungerOfTheWound, "Hunger of the Wound", true, SlotBit(ModSlot::BodyArmour) | SlotBit(ModSlot::Belt) | SlotBit(ModSlot::Helmet), 60, 24,
	    { { { UniqueRule::MissingLifeDamage, 2 }, { UniqueRule::LessLifeRecovery, 15 } } }, "1% more damage per 2% life missing\n15% less life recovery" },
	{ DesecratedModId::BlackReservoir, "Black Reservoir", true, SlotBit(ModSlot::BodyArmour) | SlotBit(ModSlot::Focus) | SlotBit(ModSlot::Amulet), 65, 20,
	    { { { UniqueRule::HitDamageFromMana, 10 }, { UniqueRule::LessManaRecovery, 20 } } }, "10% of damage from hits is taken from mana before life\n20% less mana recovery" },
	{ DesecratedModId::ThirdEye, "Third Eye", false, MartialSlots | CasterSlots | SlotBit(ModSlot::Quiver) | SlotBit(ModSlot::Amulet), 70, 18,
	    { { { UniqueRule::ThirdHitCritChance, 50 }, { UniqueRule::OtherHitsLessCrit, 15 } } }, "Every third hit has +50% critical hit chance\nOther hits have -15% critical hit chance" },
	{ DesecratedModId::RedPursuit, "Red Pursuit", false, SlotBit(ModSlot::Boots) | SlotBit(ModSlot::Gloves) | MartialSlots, 65, 20,
	    { { { UniqueRule::BleedMovingDamage, 20 }, { UniqueRule::BleedStationaryLess, 10 } } }, "20% more damage against bleeding enemies while moving\n10% less while standing still" },
	{ DesecratedModId::ChoirOfTeeth, "Choir of Teeth", true, SlotBit(ModSlot::Helmet) | SlotBit(ModSlot::BodyArmour) | SlotBit(ModSlot::Sceptre) | SlotBit(ModSlot::LegacySceptre) | SlotBit(ModSlot::Focus) | SlotBit(ModSlot::Amulet), 65, 18,
	    { { { UniqueRule::MinionLowLifeDamage, 25 }, { UniqueRule::LessMinionLife, 20 } } }, "Minions deal 25% more damage while below half life\nMinions have 20% less maximum life" },
	{ DesecratedModId::HollowSaint, "Hollow Saint", true, ArmourSlots | MartialSlots | CasterSlots, 72, 14,
	    { { { UniqueRule::None, 0 }, { UniqueRule::None, 0 } } }, "Augments in this item have 35% increased effect\nThis item has room for one modifier fewer" },
	{ DesecratedModId::UncleanPerfection, "Unclean Perfection", true, MartialSlots | ArmourSlots, 75, 12,
	    { { { UniqueRule::None, 0 }, { UniqueRule::None, 0 } } }, "2% increased effect of this item's modifiers per 5% quality\nMaximum quality is 5% lower" },
	{ DesecratedModId::PredatorsMark, "Predator's Mark", false, MartialSlots | CasterSlots | SlotBit(ModSlot::Quiver) | SlotBit(ModSlot::Ring) | SlotBit(ModSlot::Amulet), 72, 15,
	    { { { UniqueRule::DamageVsRareUnique, 12 }, { UniqueRule::LessDamageVsNormal, 8 } } }, "12% more damage against rare and unique enemies\n8% less damage against normal enemies" },
	{ DesecratedModId::ClosedWound, "Closed Wound", false, SlotBit(ModSlot::Helmet) | SlotBit(ModSlot::BodyArmour) | SlotBit(ModSlot::Belt) | SlotBit(ModSlot::Amulet), 70, 16,
	    { { { UniqueRule::FullLifeLessHitDamage, 8 }, { UniqueRule::None, 0 } } }, "8% less damage taken from hits while at full life" },
} };

/** @brief Desecrated modifiers that are ordinary ones at their best tier for the item level (Profane Vitality, Reservoir, Resistance and Speed). */
struct ProfaneMod {
	std::string_view family;
	int minItemLevel;
	int weight;
};

constexpr std::array<ProfaneMod, 8> ProfaneMods { {
	{ "maximum_life", 35, 120 },
	{ "maximum_mana", 35, 110 },
	// Profane Resistance: one element, chosen at reveal, so each element is its own offer.
	{ "fire_resistance", 35, 40 },
	{ "cold_resistance", 35, 40 },
	{ "lightning_resistance", 35, 40 },
	// Profane Speed: whichever speed fits the item.
	{ "movement_speed", 50, 75 },
	{ "attack_speed", 50, 75 },
	{ "cast_speed", 50, 75 },
} };

std::optional<size_t> UnrevealedIndex(const Poe2Craft &craft)
{
	for (size_t i = 0; i < craft.mods.size(); i++) {
		if (craft.mods[i].family == UnrevealedModFamily)
			return i;
	}
	return std::nullopt;
}

struct Offer {
	Poe2CraftMod mod;
	int weight;
	bool exclusive;
};

/** @brief The best tier of the family the item level allows. */
std::optional<uint8_t> BestTier(const ModFamily &family, int itemLevel)
{
	for (const ModTier &tier : family.tiers) {
		if (tier.itemLevel <= itemLevel)
			return tier.tier;
	}
	return std::nullopt;
}

bool OnItem(const Poe2Craft &craft, uint8_t family)
{
	return std::any_of(craft.mods.begin(), craft.mods.end(), [family](const Poe2CraftMod &mod) { return mod.family == family; });
}

std::vector<Offer> OfferPool(const Item &item, bool prefix, uint32_t seed)
{
	std::vector<Offer> pool;
	const std::optional<ModSlot> slot = ItemModSlot(item);
	if (!slot)
		return pool;
	const uint32_t slotBit = SlotBit(*slot);
	const int itemLevel = GetPoe2ItemLevel(item);
	const Poe2Craft &craft = item._iPoe2Craft;
	for (const ProfaneMod &profane : ProfaneMods) {
		const std::optional<uint8_t> index = FindFamilyForItem(profane.family, *slot);
		if (!index || itemLevel < profane.minItemLevel)
			continue;
		const ModFamily &family = ModifierFamilies()[*index];
		const auto familyId = static_cast<uint8_t>(*index + 1);
		if (family.prefix != prefix || (family.slots & slotBit) == 0 || !family.enabled || OnItem(craft, familyId))
			continue;
		const std::optional<uint8_t> tier = BestTier(family, itemLevel);
		if (!tier)
			continue;
		const uint8_t salt = RollCraftSalt(familyId, Mix(seed ^ familyId));
		pool.push_back({ { familyId, *tier, salt }, profane.weight, false });
	}
	for (const DesecratedMod &mod : DesecratedMods) {
		if (mod.id == DesecratedModId::None || mod.prefix != prefix || (mod.slots & slotBit) == 0 || itemLevel < mod.minItemLevel)
			continue;
		const auto family = static_cast<uint8_t>(DesecratedFamilyBase + static_cast<uint8_t>(mod.id) - 1);
		if (OnItem(craft, family))
			continue;
		pool.push_back({ { family, 1, 0 }, mod.weight, true });
	}
	return pool;
}

std::optional<size_t> PickWeighted(const std::vector<Offer> &pool, uint32_t roll, bool exclusiveOnly)
{
	int total = 0;
	for (const Offer &offer : pool)
		total += exclusiveOnly && !offer.exclusive ? 0 : offer.weight;
	if (total <= 0)
		return std::nullopt;
	int pick = static_cast<int>(roll % static_cast<uint32_t>(total));
	for (size_t i = 0; i < pool.size(); i++) {
		if (exclusiveOnly && !pool[i].exclusive)
			continue;
		pick -= pool[i].weight;
		if (pick < 0)
			return i;
	}
	return std::nullopt;
}

} // namespace

bool IsDesecratedFamily(uint8_t family)
{
	return family >= DesecratedFamilyBase && family < DesecratedFamilyBase + static_cast<uint8_t>(DesecratedModId::Count) - 1;
}

const DesecratedMod *GetDesecratedMod(uint8_t family)
{
	if (!IsDesecratedFamily(family))
		return nullptr;
	return &DesecratedMods[family - DesecratedFamilyBase + 1];
}

bool HasDesecratedMod(const Item &item, DesecratedModId id)
{
	const auto family = static_cast<uint8_t>(DesecratedFamilyBase + static_cast<uint8_t>(id) - 1);
	return id != DesecratedModId::None && OnItem(item._iPoe2Craft, family);
}

bool HasUnrevealedMod(const Item &item)
{
	return UnrevealedIndex(item._iPoe2Craft).has_value();
}

bool DesecrateCraft(const Item &item, Poe2Craft &craft, uint32_t seed, int forcedSide)
{
	if (craft.rarity != ITEM_QUALITY_RARE) {
		EventPlrMsg(_("Ancient Bones need a rare item."), UiFlags::ColorRed);
		return false;
	}
	if (craft.desecrated) {
		EventPlrMsg(_("This item was already desecrated."), UiFlags::ColorRed);
		return false;
	}
	const int perSide = IsJewel(item) ? MaxRareJewelModsPerSide : MaxRareModsPerSide;
	bool prefix = forcedSide != 0 ? forcedSide > 0 : (Mix(seed) & 1) != 0;
	if (forcedSide == 0 && CraftSideCount(craft, prefix) >= perSide && CraftSideCount(craft, !prefix) < perSide)
		prefix = !prefix;
	if (CraftSideCount(craft, prefix) >= perSide || CraftModCount(craft) >= CraftModLimit(item, perSide)) {
		// A full side gives up one of its modifiers.
		std::vector<size_t> removable;
		for (size_t i = 0; i < craft.mods.size(); i++) {
			if (craft.mods[i].family != 0 && IsCraftPrefix(craft.mods[i]) == prefix)
				removable.push_back(i);
		}
		if (removable.empty()) {
			EventPlrMsg(_("There's no room to desecrate this item."), UiFlags::ColorRed);
			return false;
		}
		RemoveCraftMod(craft, removable[Mix(seed ^ 0x0B0E5U) % removable.size()]);
	}
	craft.desecrated = true;
	return AddCraftMod(craft, { UnrevealedModFamily, static_cast<uint8_t>(prefix ? 1 : 2), {} }, seed);
}

std::vector<Poe2CraftMod> DesecrationOffers(const Item &item, bool kinship)
{
	std::vector<Poe2CraftMod> offers;
	const Poe2Craft &craft = item._iPoe2Craft;
	const std::optional<size_t> index = UnrevealedIndex(craft);
	if (!index)
		return offers;
	const bool prefix = IsCraftPrefix(craft.mods[*index]);
	uint32_t seed = Mix(static_cast<uint32_t>(item._iSeed) ^ 0xDE5EC8A7U);
	for (const uint8_t byte : PackCraftState(craft))
		seed = Mix(seed ^ byte);
	std::vector<Offer> pool = OfferPool(item, prefix, seed);

	const auto take = [&](size_t i) {
		offers.push_back(pool[i].mod);
		pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(i));
	};
	uint32_t step = 0;
	if (kinship) {
		// One offer shares a kind with the item's other modifiers.
		const std::vector<Poe2ItemMod> existing = CraftModList(craft);
		std::vector<size_t> kin;
		for (size_t i = 0; i < pool.size(); i++) {
			const Poe2ItemMod mod { pool[i].mod.family, pool[i].mod.tier, {} };
			if (!pool[i].exclusive && std::any_of(existing.begin(), existing.end(), [&mod](const Poe2ItemMod &other) { return ModsShareTag(mod, other); }))
				kin.push_back(i);
		}
		if (!kin.empty())
			take(kin[Mix(seed ^ 0x6B1Du) % kin.size()]);
	}
	if (GetPoe2ItemLevel(item) >= EndgameItemLevel && static_cast<int>(Mix(seed ^ 0xE7Cu) % 100) < EndgameExclusiveOfferChance) {
		if (const std::optional<size_t> exclusive = PickWeighted(pool, Mix(seed ^ 0xE7Du), true); exclusive)
			take(*exclusive);
	}
	while (offers.size() < DesecrationOfferCount) {
		const std::optional<size_t> pick = PickWeighted(pool, Mix(seed ^ (++step * 0x9E3779B9U)), false);
		if (!pick)
			break;
		take(*pick);
	}
	// Too few desecrated modifiers fit (a low item level): ordinary ones of that side fill in.
	const std::optional<ModSlot> slot = ItemModSlot(item);
	for (uint32_t attempt = 0; slot && offers.size() < DesecrationOfferCount && attempt < 16; attempt++) {
		std::vector<Poe2ItemMod> existing = CraftModList(craft);
		for (const Poe2CraftMod &offer : offers)
			existing.push_back({ offer.family, offer.tier, {} });
		ModRollFilter filter;
		filter.side = prefix ? ModSide::Prefix : ModSide::Suffix;
		filter.avoidEffects = ImplicitEffectBits(item) | ModsEffectBits(existing);
		const std::optional<Poe2ItemMod> mod = RollAddedMod(*slot, ItemModBaseTags(item), GetPoe2ItemLevel(item), existing, MaxRareModsPerSide + 1, Mix(seed ^ (0xF111U + attempt)), filter);
		if (!mod)
			break;
		offers.push_back({ mod->family, mod->tier, RollCraftSalt(mod->family, Mix(seed ^ attempt)) });
	}
	return offers;
}

bool RevealDesecratedMod(const Player &player, Item &item, const Poe2CraftMod &choice)
{
	Poe2Craft craft = item._iPoe2Craft;
	const std::optional<size_t> index = UnrevealedIndex(craft);
	if (!index || IsCraftPrefix(choice) != IsCraftPrefix(craft.mods[*index]))
		return false;
	craft.mods[*index] = choice;
	item = RebuildWithCraft(player, item, item.dwBuff, craft);
	return true;
}

std::vector<std::string> DesecratedModText(const Item &item, const Poe2CraftMod &mod)
{
	std::vector<std::string> lines;
	if (const DesecratedMod *desecrated = GetDesecratedMod(mod.family); desecrated != nullptr) {
		std::string_view text = desecrated->text;
		while (!text.empty()) {
			const size_t end = text.find('\n');
			lines.emplace_back(text.substr(0, end));
			text = end == std::string_view::npos ? std::string_view {} : text.substr(end + 1);
		}
		return lines;
	}
	return ItemModText({ mod.family, mod.tier, CraftModValues(item, mod) });
}

void AddDesecratedRules(const Item &item, UniqueRuleTotals &totals)
{
	for (const Poe2CraftMod &mod : item._iPoe2Craft.mods) {
		const DesecratedMod *desecrated = GetDesecratedMod(mod.family);
		if (desecrated == nullptr)
			continue;
		for (const auto &[rule, value] : desecrated->rules) {
			if (rule == UniqueRule::None)
				continue;
			int16_t &total = totals[static_cast<size_t>(rule)];
			total = static_cast<int16_t>(total + value);
		}
	}
}

int CraftModLimit(const Item &item, int perSide)
{
	return (perSide * 2) - (HasDesecratedMod(item, DesecratedModId::HollowSaint) ? 1 : 0);
}

int CraftQualityLimit(const Item &item)
{
	return 20 - (HasDesecratedMod(item, DesecratedModId::UncleanPerfection) ? 5 : 0);
}

int DesecratedModEffectPercent(const Item &item)
{
	if (!HasDesecratedMod(item, DesecratedModId::UncleanPerfection))
		return 100;
	return 100 + (ItemQuality(item) / 5 * 2);
}

} // namespace devilution
