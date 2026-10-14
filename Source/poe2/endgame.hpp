/**
 * @file poe2/endgame.hpp
 *
 * POE2 Mashup endgame beyond the waystone (bundle 05_Endgame_Loot_Environment): tablets that add to a map,
 * encounters and reward chests inside maps, the Atlas passives, and fragments that assemble into keys for
 * pinnacle bosses.
 */
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "engine/point.hpp"
#include "poe2/map_mods.hpp"
#include "poe2/map_state.hpp"

namespace devilution {

struct Item;
struct Monster;
struct Player;
enum class CurrencyOrb : uint8_t;

/** @brief What a map's encounters and reward chests drop, one roll at a time. */
enum class MapReward : uint8_t {
	Equipment,
	RareEquipment,
	Unique,
	Armour,
	Weapon,
	RareArmour,
	RareWeapon,
	Currency,
	PremiumCurrency,
	Gold,
	Gem,
	Essence,
	Omen,
	Augment,
	Catalyst,
	QualityCurrency,
	Corruption,
	Desecration,
	Waystone,
	Tablet,
	Fragment,
	Other,
};

// --- Tablets ---

enum class TabletKind : uint8_t {
	None,
	Cartographer,
	Bounty,
	Horde,
	Overseer,
	Rift,
	Ritual,
	Excavation,
	Mist,
	Crystallised,
	Antiquarian,
	Exiled,
	Summoner,
	Count,
};

/** @brief The modifiers tablets roll; each raises one reward axis. */
enum class TabletMod : uint8_t {
	None,
	Bountiful,
	Breeding,
	Elevated,
	Hunting,
	Cartographic,
	Count,
};

struct TabletData {
	std::string_view name;
	int weight;
	std::array<TabletMod, 2> pool;
	/** @brief The encounter it brings, and its chance per map in percent. */
	std::optional<Encounter> encounter;
	int encounterPercent;
};

constexpr int TabletUses = 10;
/** @brief Rare tablets' modifiers are this much stronger, in percent. */
constexpr int RareTabletPercent = 150;

const TabletData &GetTablet(TabletKind kind);
[[nodiscard]] bool IsTablet(const Item &item);
TabletKind GetTabletKind(const Item &item);
int TabletUsesLeft(const Item &item);
/** @brief The reward a tablet modifier gives: its range picked by its salt, more on a rare tablet. */
int TabletModValue(TabletMod mod, uint8_t salt, bool rare);
MapAxis TabletModAxis(TabletMod mod);
/** @brief Gives a fresh tablet its kind, rarity and modifiers from its seed. */
void RollTablet(Item &item);
/** @brief Currency on a tablet: Transmutation, Augmentation, Alchemy, Chaos, Annulment, Scouring and Divine. */
bool CraftTablet(CurrencyOrb orb, const Player &player, Item &item);

// --- Fragments, keys and pinnacle bosses ---

enum class FragmentSet : uint8_t {
	None,
	Ember,
	Hollow,
	Iron,
	Storm,
	Frost,
	Deep,
	Clock,
	Thorn,
	Night,
	Glass,
	Titan,
	Hunger,
	Count,
};

struct FragmentData {
	std::string_view fragmentName;
	std::string_view keyName;
	std::string_view bossName;
	int minimumAreaLevel;
	int keyAreaLevel;
	int countRequired;
	int weight;
	/** @brief What the pinnacle boss hits harder with. */
	MapDanger theme;
};

const FragmentData &GetFragmentSet(FragmentSet set);
[[nodiscard]] bool IsMapFragment(const Item &item);
[[nodiscard]] bool IsMapKey(const Item &item);
FragmentSet GetFragmentSet(const Item &item);
/** @brief Gives a fragment a set that can drop at the area level. */
void RollFragmentSet(Item &item, int areaLevel);
/** @brief Right-clicked a fragment: with enough of its set in the inventory, they become the key. */
void AssembleMapKey(Player &player, int invIndex);
/** @brief Opens the key's pinnacle arena from town. */
bool UseMapKey(Player &player, const Item &key);
/** @brief The open map's pinnacle boss, if it is a pinnacle arena. */
FragmentSet ActivePinnacle();

// --- Items in general ---

/** @brief Whether the item is a tablet, fragment or key. */
[[nodiscard]] bool IsEndgameItem(const Item &item);
/** @brief Names a tablet, fragment or key after its kind, rolling one if it has none. */
void FinishEndgameItem(Item &item);
std::vector<std::string> EndgameItemText(const Item &item);

// --- Maps ---

/**
 * @brief Picks the tablets in the inventory a map takes (as many as the waystone has slots) and rolls the
 * map's encounters. Call when the waystone is used; SpendMapTablets() uses the tablets up afterwards.
 */
void PrepareMapEndgame(Player &player, const Item &waystone);
void SpendMapTablets(Player &player);
/** @brief The tablets' and Atlas passives' share of the map's effects. */
void AddEndgameMapEffects(const Player &player, MapEffects &effects);
/** @brief Waystone modifiers' strength in percent (Deadlier Bargain makes them stronger). */
int WaystoneModStrengthPercent(const Player &player);
/** @brief Revives a map gives, after the Atlas keystones. */
int MapRevivesFor(const Player &player, int waystoneRevives);

/** @brief Places the map's encounters and reward chests on a freshly made map level. */
void PlaceMapEncounters();
/** @brief A label for monsters with a role in the map: its boss, an encounter's leader or the pinnacle boss. */
std::optional<std::string> MapMonsterTitle(const Monster &monster);
/** @brief More life and damage for the map boss: a powerful boss or a pinnacle boss. */
int MapBossLifePercentBonus();
/** @brief A monster died in a map: finishes its encounter and drops the encounter's rewards. */
void OnEndgameMonsterKilled(const Monster &monster, bool sendmsg);
/** @brief The map boss died: drops what a powerful or pinnacle boss adds. */
void OnEndgameBossKilled(const Monster &monster, bool sendmsg);
/** @brief A chest opened: true if it was a map reward chest, which dropped its profile's rewards. */
bool OpenMapRewardChest(Point position, bool sendmsg);

// --- Atlas ---

struct AtlasNodeData {
	std::string_view name;
	int maxRank;
	std::string_view effect;
	std::string_view tradeoff;
};

const AtlasNodeData &GetAtlasNode(AtlasNode node);
int AtlasRank(const Player &player, AtlasNode node);
int AtlasPointsSpent(const Player &player);
/** @brief One point per completed map and two per pinnacle boss. */
int AtlasPointsEarned(const Player &player);
bool AllocateAtlasNode(Player &player, AtlasNode node);
bool RefundAtlasNode(Player &player, AtlasNode node);
/** @brief The Atlas' boost to map boss waystone drops, in percent. */
int AtlasBossWaystonePercent(const Player &player);
/** @brief The Atlas' effect on loot in maps: fragments, equipment and uniques, in percent. */
int AtlasFragmentPercent(const Player &player);
int AtlasEquipmentPercent(const Player &player);
int AtlasUniquePercent(const Player &player, bool fromBoss);

} // namespace devilution
