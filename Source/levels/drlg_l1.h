/**
 * @file levels/drlg_l1.h
 *
 * Interface of the cathedral level generation algorithms.
 */
#pragma once

#include <cstdint>
#include <expected>

#include "engine/world_tile.hpp"
#include "levels/gendung.h"
#include "utils/bitset2d.hpp"

namespace devilution {

/** @param keepOut POE2: blocks the miniset may not touch (the new floors' designed rooms). */
void PlaceMiniSetRandom(const Miniset &miniset, int rndper, const Bitset2d<DMAXX, DMAXY> *keepOut = nullptr);
WorldTilePosition SelectChamber();
void CreateL5Dungeon(uint32_t rseed, lvl_entry entry);
void LoadPreL1Dungeon(const char *path);
std::expected<void, std::string> LoadL1Dungeon(const char *path, Point spawn);

} // namespace devilution
