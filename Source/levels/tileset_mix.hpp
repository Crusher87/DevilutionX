/**
 * @file levels/tileset_mix.hpp
 *
 * POE2 Mashup: draw one map from more than one tile set.
 *
 * A mixed map has a main tile set (the one `leveltype` names, which keeps deciding light, doors,
 * stairs and the automap) and one or more guest sets whose pieces are appended after the main
 * set's pieces. At load time the guest cels are re-painted into one shared 128-colour tile
 * palette built from every set's real pixel usage, so lighting (which slides colours along the
 * palette's 16-step ramps) keeps working for both.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <vector>

#include "engine/point.hpp"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung_defs.hpp"

namespace devilution {
namespace tilesetmix {

/** @brief One tile set's files and sizes, as read from the game's data. */
struct TileSetFiles {
	const char *cel;
	const char *til;
	const char *min;
	const char *sol;
	const char *pal;
	uint8_t blocks; // micros per piece column in the .min file: town 16, hell 12, others 10
};

/** @brief Where a loaded set landed inside the merged tables. */
struct LoadedSet {
	dungeon_type type;
	uint16_t pieceOffset; // added to this set's 1-based piece ids to get a merged piece id
	uint16_t megaOffset;  // added to this set's 1-based mega tile ids
	uint16_t frameOffset; // added to this set's 1-based frame ids
	uint16_t pieceCount;
	uint16_t megaCount;
	uint16_t frameCount;
};

/** @brief Which sets the level being loaded uses. Empty guests means a normal, single-set map. */
struct Spec {
	dungeon_type primary = DTYPE_NONE;
	std::vector<dungeon_type> guests;
};

/** @brief True while the level being loaded or played is a mixed map. */
bool Active();

/** @brief Picks the spec for the level the game is about to load, from the current level globals. */
void SelectForCurrentLevel();

/** @brief Forgets the mix (back to a normal map). */
void Clear();

const Spec &CurrentSpec();
const std::vector<LoadedSet> &LoadedSets();

/** @brief Replaces LoadLvlGFX for a mixed map: merged cels and mega tiles, main set's special cels. */
std::expected<void, std::string> LoadMixedLvlGFX();

/** @brief Replaces LoadLevelSOLData for a mixed map. */
std::expected<void, std::string> LoadMixedSOLData();

/** @brief Replaces SetDungeonMicros for a mixed map (all pieces are 16 micros tall). */
void SetMixedDungeonMicros(std::unique_ptr<std::byte[]> &dungeonCels, uint_fast8_t &microTileLen);

/** @brief Writes the shared tile palette over colours 0-127 of the logical palette and rebuilds blending. */
void ApplyMixedPalette();

/**
 * @brief Writes one guest mega tile straight into dPiece (guest ids do not fit `dungeon`, which is 8-bit).
 * @param guest index into LoadedSets() (0 is the main set)
 * @param megaId 1-based mega tile id inside that set
 * @param position top-left dPiece position (world tile)
 */
void FillMegaTile(size_t guest, uint16_t megaId, Point position);

/** @brief The shared 128-colour tile palette (RGB triples), valid after LoadMixedLvlGFX. */
const std::array<uint8_t, 128 * 3> &SharedPalette();

/** @brief Builds the test map for SL_MIXTEST on top of the loaded Cathedral arena. Seeded. */
void BuildMixTestLevel();

TileSetFiles FilesFor(dungeon_type type);

/**
 * @brief Which set and which of its own frames a re-encoded frame of the merged cel came from.
 * The HD pack names its pictures by set and original frame number.
 * @return false when the frame is unknown.
 */
bool FrameSource(uint32_t reencodedFrame, dungeon_type &type, uint32_t &originalFrame);

} // namespace tilesetmix
} // namespace devilution
