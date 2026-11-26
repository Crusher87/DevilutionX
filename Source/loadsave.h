/**
 * @file loadsave.h
 *
 * Interface of save game functionality.
 */
#pragma once

#include <cstdint>
#include <expected>

#include "pfile.h"
#include "player.h"
#include "utils/attributes.h"

namespace devilution {

extern DVL_API_FOR_TEST bool gbIsHellfireSaveGame;
extern DVL_API_FOR_TEST uint8_t giNumberOfLevels;

void RemoveInvalidItem(Item &pItem);
_item_indexes RemapItemIdxFromDiablo(_item_indexes i);
_item_indexes RemapItemIdxToDiablo(_item_indexes i);
_item_indexes RemapItemIdxFromSpawn(_item_indexes i);
_item_indexes RemapItemIdxToSpawn(_item_indexes i);
bool IsHeaderValid(uint32_t magicNumber);
void LoadHotkeys();
void LoadHeroItems(Player &player);
/**
 * @brief Remove invalid inventory items from the inventory grid
 * @param player The player to remove invalid items from
 */
void RemoveEmptyInventory(Player &player);

/**
 * @brief Load game state
 * @param firstflag Can be set to false if we are simply reloading the current game
 */
std::expected<void, std::string> LoadGame(bool firstflag);
void SaveHotkeys(SaveWriter &saveWriter, const Player &player);
/** @brief Loads the POE2 Mashup character state (support gems, ...) of the current hero; in Reforged, LoadReforgedHero. */
void LoadPoe2HeroData(Player &player);
/** @brief Writes the POE2 Mashup hero entries; does nothing in Sanctuary Reforged, which has SaveReforgedHero. */
void SavePoe2HeroData(SaveWriter &saveWriter, const Player &player);
/**
 * @brief Sanctuary Reforged: loads the hero's own entries: srvhero, and for the local hero the floor records (srvfloors)
 * and the rooms the floors were built from (srvrooms). A hero saved by an older build has its floor records in
 * "poe2floors" instead, which is read once; its other POE2 entries are not read at all.
 */
void LoadReforgedHero(Player &player);
/**
 * @brief Sanctuary Reforged: writes the hero's own entries (srvhero, srvfloors, srvrooms) and drops the POE2 entries an
 * older build wrote. Does nothing outside Reforged.
 */
void SaveReforgedHero(SaveWriter &saveWriter, const Player &player);
/**
 * @brief Sanctuary Reforged: loads the hero's own small save entry (the weapon set that isn't out, and whether the hero
 * is hardcore). A hero saved before the entry existed gets an empty second set and is a normal hero. Does nothing
 * outside Reforged; LoadPoe2HeroData calls it last.
 */
void LoadReforgedHeroData(Player &player);
/** @brief Sanctuary Reforged: writes the hero's own small save entry; does nothing outside Reforged. */
void SaveReforgedHeroData(SaveWriter &saveWriter, const Player &player);
/** @brief Sanctuary Reforged: whether the hero in the archive is hardcore, read from the hero's own entry alone (for the hero list). */
bool ReadReforgedHardcoreFlag(SaveReader &archive);
void SaveHeroItems(SaveWriter &saveWriter, Player &player);
void SaveGameData(SaveWriter &saveWriter);
void SaveGame();
void SaveLevel(SaveWriter &saveWriter);
std::expected<void, std::string> LoadLevel();
std::expected<void, std::string> ConvertLevels(SaveWriter &saveWriter);
void LoadStash();
void SaveStash(SaveWriter &stashWriter);

} // namespace devilution
