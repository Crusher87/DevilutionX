#include "poe2/endgame.hpp"

#include <algorithm>
#include <array>

#include "engine/path.h"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "inv.h"
#include "items.h"
#include "levels/gendung.h"
#include "levels/tile_properties.hpp"
#include "missiles.h"
#include "monster.h"
#include "objects.h"
#include "player.h"
#include "plrmsg.h"
#include "poe2/craft_state.hpp"
#include "poe2/crafting_items.hpp"
#include "poe2/currency.hpp"
#include "poe2/loot.hpp"
#include "poe2/mix.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/waystones.hpp"
#include "utils/format.hpp"
#include "utils/language.h"
#include "utils/str_cat.hpp"
#include "utils/utf8.hpp"

namespace devilution {

namespace {

// Bundle tablets.json.
const std::array<TabletData, static_cast<size_t>(TabletKind::Count)> Tablets { {
	{ "", 0, { TabletMod::None, TabletMod::None }, std::nullopt, 0 },
	{ "Cartographer Tablet", 100, { TabletMod::Cartographic, TabletMod::Hunting }, std::nullopt, 0 },
	{ "Bounty Tablet", 90, { TabletMod::Bountiful, TabletMod::Hunting }, std::nullopt, 0 },
	{ "Horde Tablet", 90, { TabletMod::Breeding, TabletMod::Elevated }, std::nullopt, 0 },
	{ "Overseer Tablet", 55, { TabletMod::Hunting, TabletMod::Elevated }, Encounter::PowerfulBoss, 35 },
	{ "Rift Tablet", 60, { TabletMod::Breeding, TabletMod::Bountiful }, Encounter::Rift, 65 },
	{ "Ritual Tablet", 55, { TabletMod::Hunting, TabletMod::Bountiful }, Encounter::BloodRitual, 60 },
	{ "Excavation Tablet", 55, { TabletMod::Cartographic, TabletMod::Bountiful }, Encounter::Excavation, 60 },
	{ "Mist Tablet", 50, { TabletMod::Elevated, TabletMod::Breeding }, Encounter::NightmareMist, 60 },
	{ "Crystallised Tablet", 60, { TabletMod::Bountiful, TabletMod::Elevated }, Encounter::EssencePrison, 70 },
	{ "Antiquarian Tablet", 65, { TabletMod::Bountiful, TabletMod::Cartographic }, Encounter::Strongbox, 75 },
	{ "Exiled Tablet", 40, { TabletMod::Hunting, TabletMod::Elevated }, Encounter::RogueExile, 50 },
	{ "Summoner Tablet", 45, { TabletMod::Breeding, TabletMod::Hunting }, Encounter::SummoningCircle, 65 },
} };

struct TabletModData {
	std::string_view name;
	MapAxis axis;
	int low;
	int high;
};

const std::array<TabletModData, static_cast<size_t>(TabletMod::Count)> TabletMods { {
	{ "", MapAxis::PackSize, 0, 0 },
	{ "Bountiful", MapAxis::ItemRarity, 10, 35 },
	{ "Breeding", MapAxis::PackSize, 4, 10 },
	{ "Elevated", MapAxis::Effectiveness, 6, 18 },
	{ "Hunting", MapAxis::MonsterRarity, 10, 30 },
	{ "Cartographic", MapAxis::WaystoneFind, 15, 40 },
} };

struct RewardWeight {
	MapReward reward;
	int weight;
};

struct EncounterData {
	std::string_view name;
	/** Natural chance per map, in tenths of a percent. */
	int chancePerMille;
	/** How much tougher its leader is, in percent. */
	int difficultyPercent;
	/** Monsters that come with the leader. */
	int extras;
	std::array<RewardWeight, 6> rewards;
	std::string_view completed;
};

// Bundle encounters.json; chests and the powerful boss reward through their own rules.
const std::array<EncounterData, EncounterCount> Encounters { {
	{ "Essence Prison", 100, 115, 0, { { { MapReward::Essence, 65 }, { MapReward::RareEquipment, 20 }, { MapReward::Currency, 10 }, { MapReward::Other, 5 } } }, "The essence prison is broken." },
	{ "Rift", 80, 130, 10, { { { MapReward::Currency, 25 }, { MapReward::Equipment, 25 }, { MapReward::Waystone, 15 }, { MapReward::Fragment, 10 }, { MapReward::Other, 25 } } }, "The rift is sealed." },
	{ "Blood Ritual", 70, 125, 6, { { { MapReward::PremiumCurrency, 20 }, { MapReward::RareEquipment, 30 }, { MapReward::Omen, 20 }, { MapReward::Currency, 10 }, { MapReward::Unique, 5 }, { MapReward::Other, 15 } } }, "The blood ritual is complete." },
	{ "Excavation", 70, 130, 4, { { { MapReward::Currency, 20 }, { MapReward::Catalyst, 10 }, { MapReward::Essence, 10 }, { MapReward::Omen, 5 }, { MapReward::Equipment, 25 }, { MapReward::Fragment, 10 } } }, "The excavation is unearthed." },
	{ "Nightmare Mist", 60, 145, 8, { { { MapReward::Equipment, 20 }, { MapReward::Currency, 25 }, { MapReward::Omen, 15 }, { MapReward::Fragment, 15 }, { MapReward::Other, 25 } } }, "The nightmare mist lifts." },
	{ "Strongbox", 140, 110, 4, {}, "The strongbox is open." },
	{ "Rogue Exile", 50, 135, 0, { { { MapReward::RareEquipment, 50 }, { MapReward::Currency, 15 }, { MapReward::Unique, 5 }, { MapReward::Other, 30 } } }, "The rogue exile is defeated." },
	{ "Wild Spirit", 60, 125, 0, { { { MapReward::Equipment, 30 }, { MapReward::Augment, 25 }, { MapReward::Currency, 15 }, { MapReward::Other, 30 } } }, "The wild spirit is bound." },
	{ "Summoning Circle", 50, 135, 6, { { { MapReward::Augment, 25 }, { MapReward::Unique, 10 }, { MapReward::Currency, 20 }, { MapReward::Equipment, 25 }, { MapReward::Other, 20 } } }, "The summoning circle is broken." },
	{ "Powerful Map Boss", 100, 170, 0, {}, "" },
	{ "Sealed Vault", 25, 120, 0, {}, "The sealed vault is open." },
	{ "Corrupted Nexus", 25, 155, 3, { { { MapReward::Corruption, 35 }, { MapReward::Desecration, 20 }, { MapReward::RareEquipment, 25 }, { MapReward::Fragment, 10 }, { MapReward::Other, 10 } } }, "The corrupted nexus is stabilised." },
} };

/** Encounter leaders drop this many rolls, plus one per four tiers. */
constexpr int EncounterRewardRolls = 4;

struct ChestProfile {
	std::string_view name;
	int minRolls;
	int maxRolls;
	std::array<RewardWeight, 7> weights;
	std::array<RewardWeight, 2> guarantees;
	int rarityBonus;
};

// Bundle reward_chests.json; a guarantee's weight is its count.
const std::array<ChestProfile, 8> ChestProfiles { {
	{ "Strongbox", 4, 7, { { { MapReward::Equipment, 35 }, { MapReward::Currency, 25 }, { MapReward::Gold, 15 }, { MapReward::Augment, 8 }, { MapReward::Gem, 5 }, { MapReward::Waystone, 7 }, { MapReward::Omen, 5 } } }, {}, 25 },
	{ "Armour Cache", 5, 8, { { { MapReward::Armour, 70 }, { MapReward::Currency, 10 }, { MapReward::Augment, 10 }, { MapReward::Gold, 10 } } }, { { { MapReward::RareArmour, 1 } } }, 45 },
	{ "Weapon Cache", 5, 8, { { { MapReward::Weapon, 70 }, { MapReward::Currency, 10 }, { MapReward::Augment, 10 }, { MapReward::Gold, 10 } } }, { { { MapReward::RareWeapon, 1 } } }, 45 },
	{ "Currency Cache", 5, 9, { { { MapReward::Currency, 80 }, { MapReward::Gold, 10 }, { MapReward::Catalyst, 10 } } }, { { { MapReward::PremiumCurrency, 1 } } }, 35 },
	{ "Artificer's Cache", 4, 7, { { { MapReward::Essence, 30 }, { MapReward::Omen, 20 }, { MapReward::Currency, 25 }, { MapReward::Augment, 15 }, { MapReward::QualityCurrency, 10 } } }, { { { MapReward::Essence, 1 } } }, 50 },
	{ "Relic Cache", 3, 5, { { { MapReward::Equipment, 20 }, { MapReward::Unique, 35 }, { MapReward::Currency, 20 }, { MapReward::Augment, 10 }, { MapReward::Fragment, 15 } } }, { { { MapReward::Unique, 1 } } }, 75 },
	{ "Cartographer Cache", 4, 8, { { { MapReward::Waystone, 70 }, { MapReward::Tablet, 15 }, { MapReward::Currency, 10 }, { MapReward::Fragment, 5 } } }, { { { MapReward::Waystone, 2 } } }, 20 },
	{ "Sealed Vault", 8, 14, { { { MapReward::Equipment, 25 }, { MapReward::Currency, 25 }, { MapReward::Unique, 10 }, { MapReward::Catalyst, 15 }, { MapReward::Augment, 10 }, { MapReward::Fragment, 15 } } }, { { { MapReward::RareEquipment, 2 }, { MapReward::PremiumCurrency, 1 } } }, 100 },
} };
constexpr uint8_t VaultProfile = 7;

// Bundle fragments_and_keys.json.
const std::array<FragmentData, static_cast<size_t>(FragmentSet::Count)> Fragments { {
	{ "", "", "", 0, 0, 0, 0, MapDanger::MonsterDamage },
	{ "Ember Sigil", "Furnace Gate", "Warden of Embers", 65, 68, 4, 15, MapDanger::ExtraFire },
	{ "Hollow Sigil", "Bone Gate", "The Hollow Queen", 65, 68, 4, 15, MapDanger::MorePacks },
	{ "Iron Sigil", "Saint's Gate", "Saint of Iron", 67, 70, 4, 15, MapDanger::MonsterDamage },
	{ "Storm Sigil", "Tempest Gate", "Storm Tyrant", 67, 70, 4, 15, MapDanger::ExtraLightning },
	{ "Frost Sigil", "White Gate", "The White Hart", 69, 72, 4, 15, MapDanger::ExtraCold },
	{ "Deep Sigil", "Abyss Gate", "Keeper of the Deep", 69, 72, 4, 15, MapDanger::ExtraChaos },
	{ "Clock Fragment", "Gate of Hours", "The Clockwork Judge", 73, 76, 5, 6, MapDanger::MonsterSpeed },
	{ "Thorn Fragment", "Briar Gate", "Mother of Thorns", 73, 76, 5, 6, MapDanger::BleedChance },
	{ "Night Fragment", "Sunless Gate", "The Sunless King", 75, 78, 5, 6, MapDanger::PlayerLessDamage },
	{ "Glass Fragment", "Oracle Gate", "Oracle of Glass", 75, 78, 5, 6, MapDanger::Penetration },
	{ "Titan Fragment", "Colossus Gate", "The Last Colossus", 76, 79, 5, 6, MapDanger::MonsterArmour },
	{ "Hunger Fragment", "Starved Gate", "The Starved God", 77, 80, 6, 6, MapDanger::MoreSkillCost },
} };

/** A pinnacle arena's theme danger, and how much tougher its boss is than a map boss. */
constexpr int PinnacleThemeValue = 35;
constexpr int PinnacleBossLifePercent = 600;
constexpr int PinnacleBossLifePercentPerTier = 40;

// Bundle atlas_investment.json.
const std::array<AtlasNodeData, AtlasNodeCount> AtlasNodes { {
	{ "Packed Paths", 5, "+3% Pack Size in maps per rank", "" },
	{ "Rare Hunting", 5, "+5% Monster Rarity in maps per rank", "" },
	{ "Precious Ground", 5, "+4% Item Rarity in maps per rank", "" },
	{ "Effective Enemy", 5, "+4% Monster Effectiveness in maps per rank", "" },
	{ "Cartographer's Instinct", 5, "+8% Waystone Drop Chance per rank", "" },
	{ "Boss Hunter", 4, "+7% waystones from map bosses per rank", "" },
	{ "Powerful Bosses", 3, "+8% chance the map boss is Powerful per rank", "" },
	{ "Crystallised Paths", 4, "+12% chance for an Essence Prison per rank", "" },
	{ "Ritual Ground", 4, "+10% chance for a Blood Ritual per rank", "" },
	{ "Open Rifts", 4, "+10% chance for a Rift per rank", "" },
	{ "Excavated History", 4, "+10% chance for an Excavation per rank", "" },
	{ "Nightmare Depth", 4, "+10% chance for Nightmare Mist per rank", "" },
	{ "Locked Wealth", 4, "+12% chance for a Strongbox per rank", "" },
	{ "Exiled World", 3, "+10% chance for a Rogue Exile per rank", "" },
	{ "Spirit Tracks", 3, "+10% chance for a Wild Spirit per rank", "" },
	{ "Circle of Calling", 3, "+10% chance for a Summoning Circle per rank", "" },
	{ "Vault Seeker", 3, "+8% chance for a Sealed Vault per rank", "" },
	{ "Fragmentation", 5, "+6% pinnacle fragment drops per rank", "" },
	{ "Deadlier Bargain", 1, "Waystone modifiers' rewards are 20% stronger", "Their dangers are 20% stronger too" },
	{ "One Life, Rich World", 1, "Maps have +25% Item Rarity and +15% Pack Size", "Maps have exactly 1 revive" },
	{ "Scarcity Engine", 1, "Currency is 25% more likely to drop", "Equipment drops 20% less often" },
	{ "Hunter of Kings", 1, "Bosses' uniques are 30% more likely", "Other uniques in maps are 25% less likely" },
} };

/** Which Atlas node raises each encounter's chance, and by how much per rank in tenths of a percent. */
struct EncounterAtlas {
	Encounter encounter;
	AtlasNode node;
	int perMille;
};
constexpr std::array<EncounterAtlas, 11> EncounterAtlasNodes { {
	{ Encounter::EssencePrison, AtlasNode::CrystallisedPaths, 120 },
	{ Encounter::BloodRitual, AtlasNode::RitualGround, 100 },
	{ Encounter::Rift, AtlasNode::OpenRifts, 100 },
	{ Encounter::Excavation, AtlasNode::ExcavatedHistory, 100 },
	{ Encounter::NightmareMist, AtlasNode::NightmareDepth, 100 },
	{ Encounter::Strongbox, AtlasNode::LockedWealth, 120 },
	{ Encounter::RogueExile, AtlasNode::ExiledWorld, 100 },
	{ Encounter::WildSpirit, AtlasNode::SpiritTracks, 100 },
	{ Encounter::SummoningCircle, AtlasNode::CircleOfCalling, 100 },
	{ Encounter::SealedVault, AtlasNode::VaultSeeker, 80 },
	{ Encounter::PowerfulBoss, AtlasNode::PowerfulBosses, 80 },
} };

struct AtlasAxis {
	AtlasNode node;
	MapAxis axis;
	int perRank;
};
constexpr std::array<AtlasAxis, 5> AtlasAxes { {
	{ AtlasNode::PackedPaths, MapAxis::PackSize, 3 },
	{ AtlasNode::RareHunting, MapAxis::MonsterRarity, 5 },
	{ AtlasNode::PreciousGround, MapAxis::ItemRarity, 4 },
	{ AtlasNode::EffectiveEnemy, MapAxis::Effectiveness, 4 },
	{ AtlasNode::CartographersInstinct, MapAxis::WaystoneFind, 8 },
} };

template <size_t N>
MapReward PickReward(const std::array<RewardWeight, N> &weights)
{
	int total = 0;
	for (const RewardWeight &weight : weights)
		total += weight.weight;
	if (total <= 0)
		return MapReward::Other;
	int roll = LootRandom(total);
	for (const RewardWeight &weight : weights) {
		if (roll < weight.weight)
			return weight.reward;
		roll -= weight.weight;
	}
	return MapReward::Other;
}

Poe2PlayerData *LocalData()
{
	return MyPlayer != nullptr ? &MyPlayer->poe2 : nullptr;
}

TabletMod RollTabletMod(TabletKind kind, uint32_t seed, TabletMod other)
{
	const TabletData &data = GetTablet(kind);
	const TabletMod first = data.pool[seed % 2];
	return first != other ? first : data.pool[(seed + 1) % 2];
}

uint8_t RollSalt(uint32_t seed)
{
	return static_cast<uint8_t>(1 + (seed % MaxCraftSalt));
}

int TabletModCount(const Poe2Craft &craft)
{
	return static_cast<int>(std::count_if(craft.mods.begin(), craft.mods.begin() + 2, [](const Poe2CraftMod &mod) { return mod.family != 0; }));
}

uint32_t TabletCraftSeed(const Item &item, CurrencyOrb orb)
{
	uint32_t hash = Mix(static_cast<uint32_t>(item._iSeed) ^ 0x7AB1E7U);
	for (const uint8_t byte : PackCraftState(item._iPoe2Craft))
		hash = Mix(hash ^ byte);
	return Mix(hash ^ (static_cast<uint32_t>(orb) * 0x9E3779B9U));
}

bool AddTabletMod(Poe2Craft &craft, TabletKind kind, uint32_t seed)
{
	const int count = TabletModCount(craft);
	if (count >= 2)
		return false;
	const TabletMod other = count == 1 ? static_cast<TabletMod>(craft.mods[0].family) : TabletMod::None;
	craft.mods[static_cast<size_t>(count)] = { static_cast<uint8_t>(RollTabletMod(kind, seed, other)), 1, RollSalt(Mix(seed ^ 0x51)) };
	return true;
}

void DropRewards(Point position, MapReward reward, int count, bool sendmsg, int rarityBonus = 0)
{
	for (int i = 0; i < count; i++)
		DropMapReward(position, reward, sendmsg, rarityBonus);
}

bool CanPlaceExtra(Point position)
{
	return InDungeonBounds(position) && !IsTileSolid(position) && !IsTileOccupied(position) && dMonster[position.x][position.y] == 0 && dPlayer[position.x][position.y] == 0 && dObject[position.x][position.y] == 0;
}

/** @brief Brings monsters of a kind on the level in around a place. */
void PlaceExtras(Point around, size_t typeIndex, int count, bool spawnNow)
{
	for (int i = 0; i < count; i++) {
		const std::optional<Point> at = FindClosestValidPosition(CanPlaceExtra, around, 1, 6);
		if (!at)
			return;
		if (spawnNow)
			SpawnMonster(*at, Direction::South, typeIndex);
		else
			AddMonster(*at, Direction::South, typeIndex, true);
	}
}

void BuffLeader(Monster &monster, int difficultyPercent)
{
	// Leaders have three times their difficulty in life and their difficulty in damage.
	const int64_t life = static_cast<int64_t>(monster.maxHitPoints.raw()) * difficultyPercent * 3 / 100;
	monster.maxHitPoints = Fixed26_6::fromRaw(static_cast<int32_t>(std::min<int64_t>(life, INT32_MAX / 4)));
	monster.hitPoints = monster.maxHitPoints;
	const auto scale = [difficultyPercent](uint8_t &damage) { damage = static_cast<uint8_t>(std::min(damage * difficultyPercent / 100, 255)); };
	scale(monster.minDamage);
	scale(monster.maxDamage);
	scale(monster.minDamageSpecial);
	scale(monster.maxDamageSpecial);
}

int MapRewardRolls()
{
	return EncounterRewardRolls + (ActiveMapTier() / 4);
}

} // namespace

// --- Tablets ---

const TabletData &GetTablet(TabletKind kind)
{
	return Tablets[std::min(static_cast<size_t>(kind), Tablets.size() - 1)];
}

bool IsTablet(const Item &item)
{
	return !item.isEmpty() && item._iMiscId == IMISC_TABLET;
}

TabletKind GetTabletKind(const Item &item)
{
	const uint8_t variant = CraftingItemVariant(item);
	return IsTablet(item) && variant < static_cast<uint8_t>(TabletKind::Count) ? static_cast<TabletKind>(variant) : TabletKind::None;
}

int TabletUsesLeft(const Item &item)
{
	return item._iPoe2Craft.catalyst;
}

int TabletModValue(TabletMod mod, uint8_t salt, bool rare)
{
	const TabletModData &data = TabletMods[std::min(static_cast<size_t>(mod), TabletMods.size() - 1)];
	const int value = data.low + ((data.high - data.low) * std::clamp<int>(salt - 1, 0, MaxCraftSalt - 1) / (MaxCraftSalt - 1));
	return rare ? value * RareTabletPercent / 100 : value;
}

MapAxis TabletModAxis(TabletMod mod)
{
	return TabletMods[std::min(static_cast<size_t>(mod), TabletMods.size() - 1)].axis;
}

void RollTablet(Item &item)
{
	const uint32_t seed = Mix(static_cast<uint32_t>(item._iSeed) ^ 0x7AB1E75U);
	if (GetTabletKind(item) == TabletKind::None) {
		int total = 0;
		for (size_t kind = 1; kind < Tablets.size(); kind++)
			total += Tablets[kind].weight;
		int roll = static_cast<int>(seed % static_cast<uint32_t>(total));
		for (size_t kind = 1; kind < Tablets.size(); kind++) {
			roll -= Tablets[kind].weight;
			if (roll < 0) {
				SetCraftingItemVariant(item, static_cast<uint8_t>(kind));
				break;
			}
		}
	}
	const TabletKind kind = GetTabletKind(item);
	Poe2Craft craft;
	craft.explicitMods = true;
	craft.catalyst = TabletUses;
	// Normal 60%, magic 30%, rare 10%, like other endgame drops.
	const uint32_t rarityRoll = Mix(seed ^ 1) % 100;
	craft.rarity = rarityRoll < 60 ? ITEM_QUALITY_NORMAL : rarityRoll < 90 ? ITEM_QUALITY_MAGIC
	                                                                       : ITEM_QUALITY_RARE;
	const int count = craft.rarity == ITEM_QUALITY_RARE ? 2 : craft.rarity == ITEM_QUALITY_MAGIC ? 1 + static_cast<int>(Mix(seed ^ 2) % 2)
	                                                                                             : 0;
	for (int i = 0; i < count; i++)
		AddTabletMod(craft, kind, Mix(seed ^ (0x10U + static_cast<uint32_t>(i))));
	item._iPoe2Craft = craft;
}

bool CraftTablet(CurrencyOrb orb, const Player &player, Item &item)
{
	if ((item.dwBuff & CF_POE2_CORRUPTED) != 0) {
		EventPlrMsg(_("Corrupted tablets can't be crafted."), UiFlags::ColorRed);
		return false;
	}
	Poe2Craft craft = item._iPoe2Craft;
	const TabletKind kind = GetTabletKind(item);
	const uint32_t seed = TabletCraftSeed(item, orb);
	const int count = TabletModCount(craft);
	const auto fail = [](std::string_view message) {
		EventPlrMsg(message, UiFlags::ColorRed);
		return false;
	};
	switch (orb) {
	case CurrencyOrb::Transmutation:
		if (craft.rarity != ITEM_QUALITY_NORMAL)
			return fail(_("Orb of Transmutation needs a normal tablet."));
		craft.rarity = ITEM_QUALITY_MAGIC;
		AddTabletMod(craft, kind, seed);
		break;
	case CurrencyOrb::Augmentation:
		if (craft.rarity != ITEM_QUALITY_MAGIC || !AddTabletMod(craft, kind, seed))
			return fail(_("Orb of Augmentation needs a magic tablet with one modifier."));
		break;
	case CurrencyOrb::Alchemy:
	case CurrencyOrb::Regal:
		if (craft.rarity == ITEM_QUALITY_RARE)
			return fail(_("This tablet is already rare."));
		craft.rarity = ITEM_QUALITY_RARE;
		while (AddTabletMod(craft, kind, Mix(seed ^ static_cast<uint32_t>(TabletModCount(craft))))) { }
		break;
	case CurrencyOrb::Chaos:
	case CurrencyOrb::GreaterChaos:
	case CurrencyOrb::PerfectChaos:
	case CurrencyOrb::Divine:
		if (count == 0)
			return fail(_("This tablet has no modifiers to change."));
		for (size_t i = 0; i < 2; i++) {
			if (craft.mods[i].family != 0)
				craft.mods[i].salt = RollSalt(Mix(seed ^ static_cast<uint32_t>(i)));
		}
		break;
	case CurrencyOrb::Annul:
		if (count == 0)
			return fail(_("This tablet has no modifiers to remove."));
		RemoveCraftMod(craft, Mix(seed) % static_cast<uint32_t>(count));
		break;
	case CurrencyOrb::Scouring:
		if (craft.rarity == ITEM_QUALITY_NORMAL)
			return fail(_("This tablet has no modifiers to remove."));
		craft.rarity = ITEM_QUALITY_NORMAL;
		craft.mods = {};
		break;
	default:
		return fail(_("That currency doesn't work on tablets."));
	}
	item = RebuildWithCraft(player, item, item.dwBuff, craft);
	EventPlrMsg(FormatRuntime(_("Crafted: {:s}"), item.getName().str()));
	return true;
}

// --- Fragments, keys and pinnacle bosses ---

const FragmentData &GetFragmentSet(FragmentSet set)
{
	return Fragments[std::min(static_cast<size_t>(set), Fragments.size() - 1)];
}

bool IsMapFragment(const Item &item)
{
	return !item.isEmpty() && item._iMiscId == IMISC_MAP_FRAGMENT;
}

bool IsMapKey(const Item &item)
{
	return !item.isEmpty() && item._iMiscId == IMISC_MAP_KEY;
}

FragmentSet GetFragmentSet(const Item &item)
{
	const uint8_t variant = CraftingItemVariant(item);
	if (!IsMapFragment(item) && !IsMapKey(item))
		return FragmentSet::None;
	return variant < static_cast<uint8_t>(FragmentSet::Count) ? static_cast<FragmentSet>(variant) : FragmentSet::None;
}

void RollFragmentSet(Item &item, int areaLevel)
{
	int total = 0;
	for (size_t set = 1; set < Fragments.size(); set++) {
		if (Fragments[set].minimumAreaLevel <= areaLevel)
			total += Fragments[set].weight;
	}
	uint8_t chosen = 1;
	if (total > 0) {
		int roll = static_cast<int>(Mix(static_cast<uint32_t>(item._iSeed) ^ 0xF2A6U) % static_cast<uint32_t>(total));
		for (size_t set = 1; set < Fragments.size(); set++) {
			if (Fragments[set].minimumAreaLevel > areaLevel)
				continue;
			roll -= Fragments[set].weight;
			if (roll < 0) {
				chosen = static_cast<uint8_t>(set);
				break;
			}
		}
	}
	SetCraftingItemVariant(item, chosen);
	FinishEndgameItem(item);
}

void AssembleMapKey(Player &player, int invIndex)
{
	if (invIndex < 0 || invIndex >= player._pNumInv)
		return;
	const FragmentSet set = GetFragmentSet(player.InvList[invIndex]);
	if (set == FragmentSet::None)
		return;
	const FragmentData &data = GetFragmentSet(set);
	std::vector<int> pieces;
	for (int i = 0; i < player._pNumInv; i++) {
		if (IsMapFragment(player.InvList[i]) && GetFragmentSet(player.InvList[i]) == set)
			pieces.push_back(i);
	}
	if (static_cast<int>(pieces.size()) < data.countRequired) {
		EventPlrMsg(FormatRuntime(_("{:d} of {:d} {:s}s: gather them all to form the {:s}."), static_cast<int>(pieces.size()), data.countRequired, data.fragmentName, data.keyName), UiFlags::ColorWhite);
		return;
	}
	Item key = player.InvList[invIndex];
	pieces.resize(static_cast<size_t>(data.countRequired));
	// Remove from the back so the indices stay right.
	std::sort(pieces.rbegin(), pieces.rend());
	for (const int index : pieces)
		player.RemoveInvItem(index);
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		if (AllItemsList[i].iMiscId != IMISC_MAP_KEY)
			continue;
		const int32_t seed = key._iSeed;
		InitializeItem(key, static_cast<_item_indexes>(i));
		key._iSeed = seed;
		key.dwBuff |= CF_POE2;
		SetCraftingItemVariant(key, static_cast<uint8_t>(set));
		FinishEndgameItem(key);
		key._iIdentified = true;
		key._iStatFlag = true;
		break;
	}
	if (!AutoPlaceItemInInventory(player, key, true))
		return;
	CalcPlrInv(player, true);
	EventPlrMsg(FormatRuntime(_("The {:s} is formed. Use it in town to face {:s}."), data.keyName, data.bossName), UiFlags::ColorWhitegold);
}

bool UseMapKey(Player &player, const Item &key)
{
	const FragmentSet set = GetFragmentSet(key);
	if (set == FragmentSet::None || &player != MyPlayer)
		return false;
	if (gbIsMultiplayer) {
		EventPlrMsg(_("Pinnacle bosses aren't available in multiplayer yet."), UiFlags::ColorRed);
		return false;
	}
	if (!player.isOnLevel(0)) {
		EventPlrMsg(_("Use keys in town to open the arena."), UiFlags::ColorRed);
		return false;
	}
	const FragmentData &data = GetFragmentSet(set);
	const int tier = std::clamp(data.keyAreaLevel - MapAreaLevelBase, 1, MaxWaystoneTier);
	const int level = PrepareMap(player, tier, static_cast<uint32_t>(key._iSeed));
	player.poe2.mapWaystone = {};
	player.poe2.mapRun.pinnacle = static_cast<uint8_t>(set);
	player.poe2.mapRevives = 1;
	RefreshMapEffects(player);
	EventPlrMsg(FormatRuntime(_("{:s}: {:s} awaits. 1 revive."), data.keyName, data.bossName), UiFlags::ColorWhitegold);
	EnterMap(player, level);
	return true;
}

FragmentSet ActivePinnacle()
{
	if (ActiveMapTier() == 0 || MyPlayer == nullptr)
		return FragmentSet::None;
	return static_cast<FragmentSet>(std::min<uint8_t>(MyPlayer->poe2.mapRun.pinnacle, static_cast<uint8_t>(FragmentSet::Count) - 1));
}

// --- Items in general ---

bool IsEndgameItem(const Item &item)
{
	return IsTablet(item) || IsMapFragment(item) || IsMapKey(item);
}

void FinishEndgameItem(Item &item)
{
	std::string name;
	if (IsTablet(item)) {
		if (GetTabletKind(item) == TabletKind::None || !item._iPoe2Craft.explicitMods)
			RollTablet(item);
		const Poe2Craft &craft = item._iPoe2Craft;
		item._iMagical = static_cast<item_quality>(craft.rarity);
		name = std::string(GetTablet(GetTabletKind(item)).name);
		if (craft.rarity == ITEM_QUALITY_MAGIC && craft.mods[0].family != 0)
			name = StrCat(TabletMods[std::min<size_t>(craft.mods[0].family, TabletMods.size() - 1)].name, " ", name);
		else if (craft.rarity == ITEM_QUALITY_RARE)
			name = StrCat(GetRareItemName(item), " ", name);
	} else if (IsMapFragment(item) || IsMapKey(item)) {
		if (GetFragmentSet(item) == FragmentSet::None)
			SetCraftingItemVariant(item, static_cast<uint8_t>(1 + (Mix(static_cast<uint32_t>(item._iSeed)) % 6)));
		const FragmentData &data = GetFragmentSet(GetFragmentSet(item));
		name = std::string(IsMapKey(item) ? data.keyName : data.fragmentName);
		item._iMagical = IsMapKey(item) ? ITEM_QUALITY_UNIQUE : ITEM_QUALITY_MAGIC;
	} else {
		return;
	}
	CopyUtf8(item._iName, name, ItemNameLength);
	CopyUtf8(item._iIName, name, ItemNameLength);
	item._iIdentified = true;
}

std::vector<std::string> EndgameItemText(const Item &item)
{
	std::vector<std::string> lines;
	if (IsTablet(item)) {
		const TabletData &data = GetTablet(GetTabletKind(item));
		const bool rare = item._iPoe2Craft.rarity == ITEM_QUALITY_RARE;
		for (size_t i = 0; i < 2; i++) {
			const Poe2CraftMod &mod = item._iPoe2Craft.mods[i];
			if (mod.family == 0)
				continue;
			const auto tabletMod = static_cast<TabletMod>(mod.family);
			lines.push_back(FormatRuntime(_("+{:d}% {:s} in maps"), TabletModValue(tabletMod, mod.salt, rare), MapAxisName(TabletModAxis(tabletMod))));
		}
		if (data.encounter)
			lines.push_back(FormatRuntime(_("{:d}% chance for a {:s} in each map"), data.encounterPercent, Encounters[static_cast<size_t>(*data.encounter)].name));
		lines.push_back(FormatRuntime(_("Uses left: {:d}/{:d}"), TabletUsesLeft(item), TabletUses));
		lines.emplace_back(_("Works from your inventory when you open a map"));
		lines.emplace_back(_("(as many as the waystone has tablet slots)"));
		return lines;
	}
	const FragmentData &data = GetFragmentSet(GetFragmentSet(item));
	if (IsMapFragment(item)) {
		lines.push_back(FormatRuntime(_("{:d} form the {:s}"), data.countRequired, data.keyName));
		lines.emplace_back(_("Right-click to assemble"));
	} else if (IsMapKey(item)) {
		lines.push_back(FormatRuntime(_("Opens the arena of {:s}"), data.bossName));
		lines.push_back(FormatRuntime(_("Area level {:d}"), data.keyAreaLevel));
		lines.emplace_back(_("Use in town; it's used up"));
	}
	return lines;
}

// --- Maps ---

void PrepareMapEndgame(Player &player, const Item &waystone)
{
	MapRunState &run = player.poe2.mapRun;
	run = {};
	// Tablets in the inventory, in order, as many as the waystone has slots.
	const int slots = WaystoneTabletSlots(waystone);
	size_t used = 0;
	for (int i = 0; i < player._pNumInv && used < static_cast<size_t>(slots); i++) {
		const Item &item = player.InvList[i];
		if (!IsTablet(item) || TabletUsesLeft(item) <= 0)
			continue;
		MapTablet &tablet = run.tablets[used++];
		tablet.kind = static_cast<uint8_t>(GetTabletKind(item));
		tablet.rarity = item._iPoe2Craft.rarity;
		tablet.mods = { item._iPoe2Craft.mods[0], item._iPoe2Craft.mods[1] };
		tablet.seed = item._iSeed;
	}
	// Encounters: their natural chance, the Atlas, and the tablets' injections.
	std::array<int, EncounterCount> chances {};
	for (size_t i = 0; i < EncounterCount; i++)
		chances[i] = Encounters[i].chancePerMille;
	for (const EncounterAtlas &bonus : EncounterAtlasNodes)
		chances[static_cast<size_t>(bonus.encounter)] += AtlasRank(player, bonus.node) * bonus.perMille;
	for (const MapTablet &tablet : run.tablets) {
		const TabletData &data = GetTablet(static_cast<TabletKind>(tablet.kind));
		if (tablet.kind != 0 && data.encounter)
			chances[static_cast<size_t>(*data.encounter)] += data.encounterPercent * 10;
	}
	uint32_t seed = Mix(static_cast<uint32_t>(waystone._iSeed) ^ 0xE7C0U ^ (player.poe2.mapsCompleted * 0x9E37U));
	for (size_t i = 0; i < EncounterCount; i++) {
		seed = Mix(seed + static_cast<uint32_t>(i));
		if (static_cast<int>(seed % 1000) < chances[i])
			run.encounters |= static_cast<uint16_t>(1U << i);
	}
	std::string found;
	for (size_t i = 0; i < EncounterCount; i++) {
		if ((run.encounters & (1U << i)) != 0)
			found = found.empty() ? std::string(Encounters[i].name) : StrCat(found, ", ", Encounters[i].name);
	}
	if (used > 0)
		EventPlrMsg(FormatRuntime(_("Tablets used: {:d}"), static_cast<int>(used)), UiFlags::ColorWhite);
	if (!found.empty())
		EventPlrMsg(FormatRuntime(_("This map holds: {:s}"), found), UiFlags::ColorWhitegold);
}

void SpendMapTablets(Player &player)
{
	for (const MapTablet &tablet : player.poe2.mapRun.tablets) {
		if (tablet.kind == 0)
			continue;
		for (int i = 0; i < player._pNumInv; i++) {
			Item &item = player.InvList[i];
			if (!IsTablet(item) || item._iSeed != tablet.seed)
				continue;
			if (item._iPoe2Craft.catalyst <= 1) {
				EventPlrMsg(FormatRuntime(_("{:s} is used up."), item.getName().str()));
				player.RemoveInvItem(i);
			} else {
				item._iPoe2Craft.catalyst--;
			}
			break;
		}
	}
	CalcPlrInv(player, true);
}

void AddEndgameMapEffects(const Player &player, MapEffects &effects)
{
	const MapRunState &run = player.poe2.mapRun;
	// Each tablet is its own source, so it multiplies.
	for (const MapTablet &tablet : run.tablets) {
		if (tablet.kind == 0)
			continue;
		std::array<int, static_cast<size_t>(MapAxis::Count)> rewards {};
		for (const Poe2CraftMod &mod : tablet.mods) {
			if (mod.family == 0 || mod.family >= static_cast<uint8_t>(TabletMod::Count))
				continue;
			const auto tabletMod = static_cast<TabletMod>(mod.family);
			rewards[static_cast<size_t>(TabletModAxis(tabletMod))] += TabletModValue(tabletMod, mod.salt, tablet.rarity == ITEM_QUALITY_RARE);
		}
		for (size_t axis = 0; axis < rewards.size(); axis++) {
			if (rewards[axis] != 0)
				effects.multiply(static_cast<MapAxis>(axis), rewards[axis]);
		}
	}
	// The Atlas is one more source.
	std::array<int, static_cast<size_t>(MapAxis::Count)> atlas {};
	for (const AtlasAxis &node : AtlasAxes)
		atlas[static_cast<size_t>(node.axis)] += AtlasRank(player, node.node) * node.perRank;
	if (AtlasRank(player, AtlasNode::OneLife) > 0) {
		atlas[static_cast<size_t>(MapAxis::ItemRarity)] += 25;
		atlas[static_cast<size_t>(MapAxis::PackSize)] += 15;
	}
	for (size_t axis = 0; axis < atlas.size(); axis++) {
		if (atlas[axis] != 0)
			effects.multiply(static_cast<MapAxis>(axis), atlas[axis]);
	}
	// A pinnacle arena hits hard in its boss's way.
	if (run.pinnacle != 0) {
		const FragmentData &data = GetFragmentSet(static_cast<FragmentSet>(std::min<uint8_t>(run.pinnacle, static_cast<uint8_t>(FragmentSet::Count) - 1)));
		effects.danger[static_cast<size_t>(data.theme)] += PinnacleThemeValue;
	}
}

int WaystoneModStrengthPercent(const Player &player)
{
	return AtlasRank(player, AtlasNode::DeadlierBargain) > 0 ? 120 : 100;
}

int MapRevivesFor(const Player &player, int waystoneRevives)
{
	return AtlasRank(player, AtlasNode::OneLife) > 0 ? 1 : waystoneRevives;
}

void PlaceMapEncounters()
{
	Poe2PlayerData *data = LocalData();
	if (data == nullptr || ActiveMapTier() == 0 || data->mapRun.placed)
		return;
	MapRunState &run = data->mapRun;
	run.placed = true;
	run.leaders.fill(-1);
	// Leaders come from the level's monsters, never the map boss.
	std::vector<size_t> candidates;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		const Monster &monster = Monsters[ActiveMonsters[i]];
		if (monster.isPlayerMinion() || monster.isUnique() || monster.hasNoLife() || static_cast<int>(ActiveMonsters[i]) == data->mapBoss || monster.position.tile == GolemHoldingCell)
			continue;
		candidates.push_back(ActiveMonsters[i]);
	}
	uint32_t seed = Mix(DungeonSeeds[data->mapLevel] ^ 0xE4C0U);
	const auto take = [&]() -> std::optional<size_t> {
		if (candidates.empty())
			return std::nullopt;
		seed = Mix(seed + 1);
		const size_t pick = seed % candidates.size();
		const size_t id = candidates[pick];
		candidates.erase(candidates.begin() + static_cast<std::ptrdiff_t>(pick));
		return id;
	};
	// Chests already on the level become the strongbox and the vault.
	std::vector<Point> chests;
	for (int i = 0; i < ActiveObjectCount; i++) {
		const Object &object = Objects[ActiveObjects[i]];
		if (object.IsChest() && object.canInteractWith())
			chests.push_back(object.position);
	}
	const auto takeChest = [&](uint8_t profile) {
		if (chests.empty())
			return false;
		seed = Mix(seed + 7);
		const size_t pick = seed % chests.size();
		for (MapChest &chest : run.chests) {
			if (chest.profile != 0)
				continue;
			chest = { static_cast<uint8_t>(chests[pick].x), static_cast<uint8_t>(chests[pick].y), static_cast<uint8_t>(profile + 1) };
			chests.erase(chests.begin() + static_cast<std::ptrdiff_t>(pick));
			return true;
		}
		return false;
	};
	for (size_t i = 0; i < EncounterCount; i++) {
		const auto encounter = static_cast<Encounter>(i);
		if (!run.has(encounter) || encounter == Encounter::PowerfulBoss)
			continue;
		bool placed = false;
		if (encounter == Encounter::Strongbox) {
			// Half are plain strongboxes, the rest one of the caches.
			seed = Mix(seed + 3);
			placed = takeChest(seed % 2 == 0 ? 0 : static_cast<uint8_t>(1 + (seed / 2) % 6));
		} else if (encounter == Encounter::SealedVault) {
			placed = takeChest(VaultProfile);
		} else if (const std::optional<size_t> id = take(); id) {
			Monster &leader = Monsters[*id];
			BuffLeader(leader, Encounters[i].difficultyPercent);
			run.leaders[i] = static_cast<int16_t>(*id);
			PlaceExtras(leader.position.tile, leader.levelType, Encounters[i].extras, false);
			placed = true;
		}
		if (!placed)
			run.encounters &= static_cast<uint16_t>(~(1U << i));
	}
}

std::optional<std::string> MapMonsterTitle(const Monster &monster)
{
	if (ActiveMapTier() == 0 || MyPlayer == nullptr)
		return std::nullopt;
	const Poe2PlayerData &data = MyPlayer->poe2;
	if (IsMapBoss(monster)) {
		if (const FragmentSet set = ActivePinnacle(); set != FragmentSet::None)
			return std::string(GetFragmentSet(set).bossName);
		return std::string(data.mapRun.has(Encounter::PowerfulBoss) ? _("Powerful Map Boss") : _("Map Boss"));
	}
	for (size_t i = 0; i < EncounterCount; i++) {
		if (data.mapRun.leaders[i] >= 0 && static_cast<size_t>(data.mapRun.leaders[i]) == monster.getId() && !data.mapRun.done(static_cast<Encounter>(i)))
			return std::string(Encounters[i].name);
	}
	return std::nullopt;
}

int MapBossLifePercentBonus()
{
	if (ActiveMapTier() == 0 || MyPlayer == nullptr)
		return 100;
	if (ActivePinnacle() != FragmentSet::None)
		return 100 + PinnacleBossLifePercent + (ActiveMapTier() * PinnacleBossLifePercentPerTier);
	if (MyPlayer->poe2.mapRun.has(Encounter::PowerfulBoss))
		return Encounters[static_cast<size_t>(Encounter::PowerfulBoss)].difficultyPercent;
	return 100;
}

void OnEndgameMonsterKilled(const Monster &monster, bool sendmsg)
{
	Poe2PlayerData *data = LocalData();
	if (data == nullptr || ActiveMapTier() == 0)
		return;
	MapRunState &run = data->mapRun;
	for (size_t i = 0; i < EncounterCount; i++) {
		const auto encounter = static_cast<Encounter>(i);
		if (run.leaders[i] < 0 || static_cast<size_t>(run.leaders[i]) != monster.getId() || run.done(encounter))
			continue;
		run.encountersDone |= static_cast<uint16_t>(1U << i);
		const EncounterData &encounterData = Encounters[i];
		EventPlrMsg(std::string(encounterData.completed), UiFlags::ColorWhitegold);
		const int rolls = MapRewardRolls();
		for (int roll = 0; roll < rolls; roll++)
			DropMapReward(monster.position.tile, PickReward(encounterData.rewards), sendmsg);
	}
}

void OnEndgameBossKilled(const Monster &monster, bool sendmsg)
{
	Poe2PlayerData *data = LocalData();
	if (data == nullptr)
		return;
	MapRunState &run = data->mapRun;
	const Point at = monster.position.tile;
	if (const FragmentSet set = ActivePinnacle(); set != FragmentSet::None) {
		data->atlas.pinnaclesDefeated = static_cast<uint16_t>(std::min(data->atlas.pinnaclesDefeated + 1, 65535));
		EventPlrMsg(FormatRuntime(_("{:s} is defeated!"), GetFragmentSet(set).bossName), UiFlags::ColorWhitegold);
		// A pinnacle boss: a unique, two rares, premium currency and maybe the next set's fragment.
		DropRewards(at, MapReward::Unique, 1, sendmsg, 50);
		DropRewards(at, MapReward::RareEquipment, 2, sendmsg, 50);
		DropRewards(at, MapReward::PremiumCurrency, 2, sendmsg);
		DropRewards(at, MapReward::Currency, 3, sendmsg);
		if (LootRandom(100) < 30 * AtlasFragmentPercent(*MyPlayer) / 100)
			DropMapReward(at, MapReward::Fragment, sendmsg);
		run.pinnacle = 0;
		return;
	}
	if (!run.has(Encounter::PowerfulBoss))
		return;
	run.encountersDone |= static_cast<uint16_t>(1U << static_cast<uint8_t>(Encounter::PowerfulBoss));
	// A powerful boss: three more rolls, and chances for a unique, premium currency and a waystone.
	for (int roll = 0; roll < 3; roll++)
		DropMapReward(at, MapReward::Other, sendmsg);
	if (LootRandom(100) < 8 * AtlasUniquePercent(*MyPlayer, true) / 100)
		DropMapReward(at, MapReward::Unique, sendmsg);
	if (LootRandom(100) < 12)
		DropMapReward(at, MapReward::PremiumCurrency, sendmsg);
	if (LootRandom(100) < 40)
		DropMapReward(at, MapReward::Waystone, sendmsg);
}

bool OpenMapRewardChest(Point position, bool sendmsg)
{
	Poe2PlayerData *data = LocalData();
	if (data == nullptr || ActiveMapTier() == 0)
		return false;
	for (MapChest &chest : data->mapRun.chests) {
		if (chest.profile == 0 || chest.x != position.x || chest.y != position.y)
			continue;
		const ChestProfile &profile = ChestProfiles[std::min<size_t>(chest.profile - 1, ChestProfiles.size() - 1)];
		const bool vault = chest.profile - 1 == VaultProfile;
		chest.profile = 0;
		const auto encounter = vault ? Encounter::SealedVault : Encounter::Strongbox;
		data->mapRun.encountersDone |= static_cast<uint16_t>(1U << static_cast<uint8_t>(encounter));
		EventPlrMsg(FormatRuntime(_("{:s} opened!"), profile.name), UiFlags::ColorWhitegold);
		for (const RewardWeight &guarantee : profile.guarantees)
			DropRewards(position, guarantee.reward, guarantee.weight, sendmsg, profile.rarityBonus);
		const int rolls = profile.minRolls + LootRandom(profile.maxRolls - profile.minRolls + 1);
		for (int i = 0; i < rolls; i++)
			DropMapReward(position, PickReward(profile.weights), sendmsg, profile.rarityBonus);
		// Strongboxes bring an ambush.
		if (!vault) {
			for (size_t i = 0; i < ActiveMonsterCount; i++) {
				const Monster &monster = Monsters[ActiveMonsters[i]];
				if (monster.isPlayerMinion() || monster.isUnique() || monster.position.tile == GolemHoldingCell)
					continue;
				PlaceExtras(position, monster.levelType, Encounters[static_cast<size_t>(Encounter::Strongbox)].extras, true);
				break;
			}
		}
		return true;
	}
	return false;
}

// --- Atlas ---

const AtlasNodeData &GetAtlasNode(AtlasNode node)
{
	return AtlasNodes[std::min(static_cast<size_t>(node), AtlasNodes.size() - 1)];
}

int AtlasRank(const Player &player, AtlasNode node)
{
	if (static_cast<size_t>(node) >= AtlasNodeCount)
		return 0;
	return std::min<int>(player.poe2.atlas.ranks[static_cast<size_t>(node)], GetAtlasNode(node).maxRank);
}

int AtlasPointsSpent(const Player &player)
{
	int spent = 0;
	for (size_t node = 0; node < AtlasNodeCount; node++)
		spent += AtlasRank(player, static_cast<AtlasNode>(node));
	return spent;
}

int AtlasPointsEarned(const Player &player)
{
	return player.poe2.mapsCompleted + (2 * player.poe2.atlas.pinnaclesDefeated);
}

bool AllocateAtlasNode(Player &player, AtlasNode node)
{
	if (static_cast<size_t>(node) >= AtlasNodeCount || AtlasPointsSpent(player) >= AtlasPointsEarned(player))
		return false;
	uint8_t &rank = player.poe2.atlas.ranks[static_cast<size_t>(node)];
	if (rank >= GetAtlasNode(node).maxRank)
		return false;
	rank++;
	return true;
}

bool RefundAtlasNode(Player &player, AtlasNode node)
{
	if (static_cast<size_t>(node) >= AtlasNodeCount)
		return false;
	uint8_t &rank = player.poe2.atlas.ranks[static_cast<size_t>(node)];
	if (rank == 0)
		return false;
	rank--;
	return true;
}

int AtlasBossWaystonePercent(const Player &player)
{
	return 100 + (7 * AtlasRank(player, AtlasNode::BossHunter));
}

int AtlasFragmentPercent(const Player &player)
{
	return 100 + (6 * AtlasRank(player, AtlasNode::Fragmentation));
}

int AtlasEquipmentPercent(const Player &player)
{
	return AtlasRank(player, AtlasNode::ScarcityEngine) > 0 ? 80 : 100;
}

int AtlasUniquePercent(const Player &player, bool fromBoss)
{
	if (AtlasRank(player, AtlasNode::HunterOfKings) == 0)
		return 100;
	return fromBoss ? 130 : 75;
}

} // namespace devilution
