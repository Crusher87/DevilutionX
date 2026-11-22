/**
 * @file poe2/hd_pack.hpp
 *
 * POE2 Mashup: the HD art read from a few big pack files (hd\index.hdx, hd\pack_000.hdp ...) instead of one loose
 * file a picture. A pack holds the files' own bytes untouched, each different content once however many names it has.
 * tools/hd_pack.py makes, checks, lists and unpacks them. A loose file of the same name is always used first: the
 * pack is only looked in for what the folders don't have (see FindAsset).
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Read where the game's files are opened through SDL 2 streams (the same builds that can show HD art from an archive).
#if !defined(UNPACKED_MPQS) && !defined(USE_SDL1) && !defined(USE_SDL3)
#define POE2_HD_PACK 1
#endif

struct SDL_RWops;

namespace devilution {

namespace hdpack {

/** @brief Whether an asset path is one the pack may hold: anything under hd\ (either slash, any case). */
bool IsPackPath(std::string_view assetPath);

/**
 * @brief Reads the index in a folder (given with its last slash) and opens its pack files, in place of any read
 * before. False, with the reason in the log, if there is no good index there: nothing is mounted then.
 */
bool Mount(const std::string &hdDir);
void Unmount();

/**
 * @brief Mounts the first pack found in the game's folders, in the order files are looked for: `<folder>hd/index.hdx`
 * in each of `first`, then in `last`. Looks again only when the folders have changed. Safe on any thread.
 */
void MountFromGameFolders(const std::vector<std::string> &first, const std::string &last);

/** @brief Whether the mounted pack has a file by this asset path (hd\tiles\town\12.png). No disk is touched. */
bool Has(std::string_view assetPath);

/**
 * @brief Opens a file of the mounted pack for reading, or nullptr if it has none by that name. Nothing is read until
 * the stream is: then the whole file is read and checked at once. A damaged file reads as an error, and is logged.
 * Safe on any thread; the caller closes the stream.
 */
SDL_RWops *Open(std::string_view assetPath);

/** @brief A file of the mounted pack, whole. False if it has none by that name or what was read is damaged. */
bool ReadFile(std::string_view assetPath, std::vector<uint8_t> &bytes);

/** @brief How many files the mounted pack has names for (0 with none mounted). */
uint32_t NameCount();

/** @brief How many reads found a damaged or missing piece of a pack since the game started. */
uint32_t DamageCount();

} // namespace hdpack

} // namespace devilution
