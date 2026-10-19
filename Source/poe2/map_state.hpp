/**
 * @file poe2/map_state.hpp
 *
 * POE2 Mashup endgame state kept with the hero: the open map's tablets, encounters, reward chests and
 * pinnacle boss, and the Atlas passives. Everything here is saved with the hero.
 */
#pragma once

#include <array>
#include <cstdint>

#include "poe2/craft_types.hpp"

namespace devilution {

/** @brief The encounters a map can hold (bundle encounters.json). */
enum class Encounter : uint8_t {
	EssencePrison,
	Rift,
	BloodRitual,
	Excavation,
	NightmareMist,
	Strongbox,
	RogueExile,
	WildSpirit,
	SummoningCircle,
	PowerfulBoss,
	SealedVault,
	CorruptedNexus,
	Count,
};
constexpr size_t EncounterCount = static_cast<size_t>(Encounter::Count);

/** @brief The Atlas passives (bundle atlas_investment.json). */
enum class AtlasNode : uint8_t {
	PackedPaths,
	RareHunting,
	PreciousGround,
	EffectiveEnemy,
	CartographersInstinct,
	BossHunter,
	PowerfulBosses,
	CrystallisedPaths,
	RitualGround,
	OpenRifts,
	ExcavatedHistory,
	NightmareDepth,
	LockedWealth,
	ExiledWorld,
	SpiritTracks,
	CircleOfCalling,
	VaultSeeker,
	Fragmentation,
	DeadlierBargain,
	OneLife,
	ScarcityEngine,
	HunterOfKings,
	Count,
};
constexpr size_t AtlasNodeCount = static_cast<size_t>(AtlasNode::Count);

/** @brief Most tablets a map takes. */
constexpr size_t MaxMapTablets = 3;
/** @brief Reward chests a map marks: a strongbox and a vault. */
constexpr size_t MaxMapChests = 2;

/** @brief A tablet used on the open map: its kind, rarity and up to two modifiers. */
struct MapTablet {
	uint8_t kind = 0;
	uint8_t rarity = 0;
	std::array<Poe2CraftMod, 2> mods {};
	/** @brief Its seed, to find the item again and use it up (not saved). */
	int32_t seed = 0;
};

/** @brief A chest on the map that holds a reward profile instead of its own loot. */
struct MapChest {
	uint8_t x = 0;
	uint8_t y = 0;
	/** @brief Its reward profile plus one; 0 is none. */
	uint8_t profile = 0;
};

struct MapRunState {
	std::array<MapTablet, MaxMapTablets> tablets {};
	/** @brief The encounters rolled for the map, and the ones finished. */
	uint16_t encounters = 0;
	uint16_t encountersDone = 0;
	/** @brief The monster leading each encounter, -1 for none. */
	std::array<int16_t, EncounterCount> leaders {};
	std::array<MapChest, MaxMapChests> chests {};
	/** @brief The pinnacle boss the map holds (a fragment set), 0 for an ordinary map. */
	uint8_t pinnacle = 0;
	/** @brief Whether the encounters have been placed on the level. */
	bool placed = false;

	MapRunState() { leaders.fill(-1); }

	[[nodiscard]] bool has(Encounter encounter) const { return (encounters & (1U << static_cast<uint8_t>(encounter))) != 0; }
	[[nodiscard]] bool done(Encounter encounter) const { return (encountersDone & (1U << static_cast<uint8_t>(encounter))) != 0; }
};

/** @brief Bytes the open map's state takes in the save: tablets, encounters, leaders, chests, pinnacle and placed. */
constexpr size_t MapRunSaveSize = (MaxMapTablets * 8) + 4 + (EncounterCount * 2) + (MaxMapChests * 3) + 2;

struct AtlasState {
	std::array<uint8_t, AtlasNodeCount> ranks {};
	/** @brief Pinnacle bosses the hero has defeated. */
	uint16_t pinnaclesDefeated = 0;
};

} // namespace devilution
