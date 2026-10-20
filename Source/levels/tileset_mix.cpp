#include "levels/tileset_mix.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <ankerl/unordered_dense.h>

#include "appfat.h"
#include "engine/assets.hpp"
#include "engine/load_cel.hpp"
#include "engine/load_file.hpp"
#include "engine/palette.h"
#include "engine/random.hpp"
#include "levels/dun_tile.hpp"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "levels/reencode_dun_cels.hpp"
#include "utils/endian_swap.hpp"
#include "utils/endian_write.hpp"
#include "utils/log.hpp"
#include "utils/status_macros.hpp"
#include "utils/str_cat.hpp"

namespace devilution {
namespace tilesetmix {
namespace {

constexpr int TileColours = 128; // colours 0-127 belong to the ground and walls, 128-255 to everything else
constexpr int RampCount = 8;
constexpr int RampLength = 16;
constexpr size_t FrameBytesSquare = 32 * 32;
constexpr size_t FrameBytesTriangle = 544;
constexpr size_t FrameBytesTrapezoid = 800;

/** @brief One tile set's files as read from disk, before merging. */
struct RawSet {
	dungeon_type type;
	TileSetFiles files;
	std::unique_ptr<std::byte[]> cel;
	size_t celSize = 0;
	std::unique_ptr<uint16_t[]> min;
	size_t minCount = 0;
	std::unique_ptr<MegaTile[]> til;
	size_t tilCount = 0;
	std::unique_ptr<TileProperties[]> sol;
	size_t solCount = 0;
	std::array<uint8_t, 256 * 3> pal {};
	uint32_t frameCount = 0;
	std::vector<uint8_t> frameType; // 1-based frame -> TileType, 0xFF if never referenced
	std::array<uint8_t, 256> remap {};
};

Spec CurrentMix;
std::vector<LoadedSet> Sets;
std::vector<RawSet> Raw;
std::array<uint8_t, TileColours * 3> Shared {};
bool SharedReady = false;
std::vector<uint32_t> ReencodedToMerged; // re-encoded frame (1-based) -> merged frame before re-encoding

const uint32_t *FrameTable(const RawSet &set)
{
	return reinterpret_cast<const uint32_t *>(set.cel.get());
}

size_t FrameBegin(const RawSet &set, uint32_t frame)
{
	return Swap32LE(FrameTable(set)[frame]);
}

size_t FrameEnd(const RawSet &set, uint32_t frame)
{
	return Swap32LE(FrameTable(set)[frame + 1]);
}

/**
 * @brief Calls `fn(uint8_t &pixel)` for every pixel byte of a frame, skipping run-length commands.
 * Padding bytes of triangles are zero and index 0 always maps to 0, so they are passed through too.
 */
template <typename Fn>
void ForEachPixelByte(uint8_t *data, size_t size, TileType type, Fn &&fn)
{
	if (type != TileType::TransparentSquare) {
		for (size_t i = 0; i < size; ++i)
			fn(data[i]);
		return;
	}
	size_t pos = 0;
	for (int row = 0; row < 32 && pos < size; ++row) {
		int x = 0;
		while (x < 32 && pos < size) {
			const auto n = static_cast<int8_t>(data[pos++]);
			if (n > 0) {
				for (int i = 0; i < n && pos < size; ++i)
					fn(data[pos++]);
				x += n;
			} else {
				x -= n;
			}
		}
	}
}

bool HaveHellfireTown()
{
	return FindAsset("nlevels\\towndata\\town.cel").ok();
}

std::expected<void, std::string> ReadSet(RawSet &set)
{
	const TileSetFiles &f = set.files;
	ASSIGN_OR_RETURN(set.cel, LoadFileInMemWithStatus<std::byte>(f.cel, &set.celSize));
	ASSIGN_OR_RETURN(set.min, LoadFileInMemWithStatus<uint16_t>(f.min, &set.minCount));
	ASSIGN_OR_RETURN(set.til, LoadFileInMemWithStatus<MegaTile>(f.til, &set.tilCount));
	ASSIGN_OR_RETURN(set.sol, LoadFileInMemWithStatus<TileProperties>(f.sol, &set.solCount));
	RETURN_IF_ERROR(LoadFileInMemWithStatus<uint8_t>(f.pal, set.pal.data(), set.pal.size()));
	if (set.celSize < 8)
		return std::unexpected(StrCat("Tile set cel too small: ", f.cel));
	set.frameCount = Swap32LE(FrameTable(set)[0]);
	if (static_cast<size_t>(set.frameCount + 2) * 4 > set.celSize)
		return std::unexpected(StrCat("Tile set cel header broken: ", f.cel));
	const size_t pieces = set.minCount / f.blocks;
	if (pieces != set.solCount)
		return std::unexpected(StrCat("Tile set min/sol mismatch: ", f.min));
	set.frameType.assign(set.frameCount + 1, 0xFF);
	for (size_t p = 0; p < pieces; ++p) {
		for (int b = 0; b < f.blocks; ++b) {
			const LevelCelBlock block { Swap16LE(set.min[p * f.blocks + b]) };
			if (!block.hasValue())
				continue;
			const uint16_t frame = block.data & 0xFFF; // files never use bit 15
			if (frame <= set.frameCount && set.frameType[frame] == 0xFF)
				set.frameType[frame] = static_cast<uint8_t>(block.type());
		}
	}
	return {};
}

std::array<uint64_t, 256> PixelHistogram(RawSet &set)
{
	std::array<uint64_t, 256> hist {};
	auto *data = reinterpret_cast<uint8_t *>(set.cel.get());
	for (uint32_t frame = 1; frame <= set.frameCount; ++frame) {
		if (set.frameType[frame] == 0xFF)
			continue;
		const size_t begin = FrameBegin(set, frame);
		const size_t end = FrameEnd(set, frame);
		if (end > set.celSize || begin > end)
			continue;
		ForEachPixelByte(data + begin, end - begin, static_cast<TileType>(set.frameType[frame]), [&](uint8_t &px) { hist[px]++; });
	}
	return hist;
}

struct Colour {
	float r, g, b, w;
};

float Luminance(float r, float g, float b)
{
	return 0.299F * r + 0.587F * g + 0.114F * b;
}

/**
 * @brief Builds one 128-colour tile palette (8 ramps of 16, light to dark) from every set's real pixel use.
 * Deterministic: same input files, same palette on every machine.
 */
void BuildSharedPalette(const std::vector<std::pair<const RawSet *, std::array<uint64_t, 256>>> &usage)
{
	std::vector<Colour> cols;
	for (const auto &[set, hist] : usage) {
		double total = 0;
		for (int i = 1; i < TileColours; ++i)
			total += static_cast<double>(hist[i]);
		if (total <= 0)
			continue;
		for (int i = 1; i < TileColours; ++i) {
			if (hist[i] == 0)
				continue;
			cols.push_back(Colour {
			    static_cast<float>(set->pal[i * 3]),
			    static_cast<float>(set->pal[i * 3 + 1]),
			    static_cast<float>(set->pal[i * 3 + 2]),
			    static_cast<float>(hist[i] / total) });
		}
	}
	const auto feature = [](const Colour &c, float out[4]) {
		const float l = Luminance(c.r, c.g, c.b);
		out[0] = c.r - l;
		out[1] = c.g - l;
		out[2] = c.b - l;
		out[3] = 0.35F * l;
	};
	const size_t n = cols.size();
	std::vector<std::array<float, 4>> feat(n);
	for (size_t i = 0; i < n; ++i)
		feature(cols[i], feat[i].data());

	std::array<std::array<float, 4>, RampCount> centres {};
	std::vector<int> label(n, 0);
	if (n > 0) {
		// Farthest-point seeding, weighted, so the result does not depend on a random source.
		std::vector<float> minD(n, 1e30F);
		size_t first = 0;
		for (size_t i = 1; i < n; ++i)
			if (cols[i].w > cols[first].w) first = i;
		centres[0] = feat[first];
		for (int k = 1; k < RampCount; ++k) {
			size_t best = 0;
			float bestScore = -1;
			for (size_t i = 0; i < n; ++i) {
				float d = 0;
				for (int j = 0; j < 4; ++j) {
					const float v = feat[i][j] - centres[k - 1][j];
					d += v * v;
				}
				minD[i] = std::min(minD[i], d);
				const float score = minD[i] * cols[i].w;
				if (score > bestScore) {
					bestScore = score;
					best = i;
				}
			}
			centres[k] = feat[best];
		}
		for (int iter = 0; iter < 60; ++iter) {
			for (size_t i = 0; i < n; ++i) {
				int bestK = 0;
				float bestD = 1e30F;
				for (int k = 0; k < RampCount; ++k) {
					float d = 0;
					for (int j = 0; j < 4; ++j) {
						const float v = feat[i][j] - centres[k][j];
						d += v * v;
					}
					if (d < bestD) {
						bestD = d;
						bestK = k;
					}
				}
				label[i] = bestK;
			}
			for (int k = 0; k < RampCount; ++k) {
				std::array<float, 4> sum {};
				float w = 0;
				for (size_t i = 0; i < n; ++i) {
					if (label[i] != k)
						continue;
					for (int j = 0; j < 4; ++j)
						sum[j] += feat[i][j] * cols[i].w;
					w += cols[i].w;
				}
				if (w > 0)
					for (int j = 0; j < 4; ++j)
						centres[k][j] = sum[j] / w;
			}
		}
	}

	Shared.fill(0);
	for (int k = 0; k < RampCount; ++k) {
		std::vector<size_t> members;
		for (size_t i = 0; i < n; ++i)
			if (label[i] == k) members.push_back(i);
		std::vector<std::pair<float, size_t>> byLum;
		for (const size_t i : members)
			byLum.emplace_back(Luminance(cols[i].r, cols[i].g, cols[i].b), i);
		std::sort(byLum.begin(), byLum.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
		for (int s = 0; s < RampLength; ++s) {
			const int idx = k * RampLength + s;
			float r;
			float g;
			float b;
			if (byLum.empty()) {
				r = g = b = 200.0F * (1.0F - static_cast<float>(s) / 15.0F) + 4.0F * (static_cast<float>(s) / 15.0F);
			} else {
				const float top = byLum[0].first;
				const float target = top * (1.0F - static_cast<float>(s) / 15.0F) + 4.0F * (static_cast<float>(s) / 15.0F);
				size_t best = 0;
				float bestD = 1e30F;
				for (size_t j = 0; j < byLum.size(); ++j) {
					const float d = std::fabs(byLum[j].first - target);
					if (d < bestD) {
						bestD = d;
						best = j;
					}
				}
				const Colour &base = cols[byLum[best].second];
				const float bl = std::max(Luminance(base.r, base.g, base.b), 1.0F);
				const float scale = target / bl;
				r = base.r * scale;
				g = base.g * scale;
				b = base.b * scale;
			}
			Shared[idx * 3] = static_cast<uint8_t>(std::clamp(std::lround(r), 0L, 255L));
			Shared[idx * 3 + 1] = static_cast<uint8_t>(std::clamp(std::lround(g), 0L, 255L));
			Shared[idx * 3 + 2] = static_cast<uint8_t>(std::clamp(std::lround(b), 0L, 255L));
		}
	}
	Shared[0] = Shared[1] = Shared[2] = 0;
	SharedReady = true;
}

void BuildRemap(RawSet &set)
{
	for (int i = 0; i < 256; ++i)
		set.remap[i] = static_cast<uint8_t>(i);
	constexpr float Wr = 0.9F;
	constexpr float Wg = 1.77F;
	constexpr float Wb = 0.33F;
	for (int i = 1; i < TileColours; ++i) {
		const float r = set.pal[i * 3];
		const float g = set.pal[i * 3 + 1];
		const float b = set.pal[i * 3 + 2];
		int best = 1;
		float bestD = 1e30F;
		for (int j = 1; j < TileColours; ++j) {
			const float dr = (r - Shared[j * 3]) * Wr;
			const float dg = (g - Shared[j * 3 + 1]) * Wg;
			const float db = (b - Shared[j * 3 + 2]) * Wb;
			const float d = dr * dr + dg * dg + db * db;
			if (d < bestD) {
				bestD = d;
				best = j;
			}
		}
		set.remap[i] = static_cast<uint8_t>(best);
	}
}

std::expected<void, std::string> EnsureLoaded()
{
	if (!Raw.empty())
		return {};
	std::vector<dungeon_type> order;
	order.push_back(CurrentMix.primary);
	for (const dungeon_type guest : CurrentMix.guests)
		order.push_back(guest);
	Raw.reserve(order.size());
	for (const dungeon_type type : order) {
		RawSet set;
		set.type = type;
		set.files = FilesFor(type);
		RETURN_IF_ERROR(ReadSet(set));
		Raw.push_back(std::move(set));
	}
	std::vector<std::pair<const RawSet *, std::array<uint64_t, 256>>> usage;
	for (RawSet &set : Raw)
		usage.emplace_back(&set, PixelHistogram(set));
	BuildSharedPalette(usage);
	for (RawSet &set : Raw)
		BuildRemap(set);

	Sets.clear();
	uint16_t pieceOffset = 0;
	uint16_t megaOffset = 0;
	uint16_t frameOffset = 0;
	for (const RawSet &set : Raw) {
		LoadedSet loaded;
		loaded.type = set.type;
		loaded.pieceOffset = pieceOffset;
		loaded.megaOffset = megaOffset;
		loaded.frameOffset = frameOffset;
		loaded.pieceCount = static_cast<uint16_t>(set.minCount / set.files.blocks);
		loaded.megaCount = static_cast<uint16_t>(set.tilCount);
		loaded.frameCount = static_cast<uint16_t>(set.frameCount);
		Sets.push_back(loaded);
		pieceOffset += loaded.pieceCount;
		megaOffset += loaded.megaCount;
		frameOffset += loaded.frameCount;
	}
	if (pieceOffset > MAXTILES)
		return std::unexpected(StrCat("Mixed tile sets need ", pieceOffset, " pieces, the game holds ", MAXTILES));
	if (frameOffset > LevelCelBlock::MaxFrame)
		return std::unexpected(StrCat("Mixed tile sets need ", frameOffset, " frames, the game holds ", LevelCelBlock::MaxFrame));
	LogVerbose("Tile-set mix: {} sets, {} pieces, {} frames", Raw.size(), pieceOffset, frameOffset);
	return {};
}

void ReleaseRaw()
{
	Raw.clear();
}

} // namespace

TileSetFiles FilesFor(dungeon_type type)
{
	switch (type) {
	case DTYPE_TOWN:
		if (HaveHellfireTown())
			return { "nlevels\\towndata\\town.cel", "nlevels\\towndata\\town.til", "nlevels\\towndata\\town.min", "nlevels\\towndata\\town.sol", "levels\\towndata\\town.pal", 16 };
		return { "levels\\towndata\\town.cel", "levels\\towndata\\town.til", "levels\\towndata\\town.min", "levels\\towndata\\town.sol", "levels\\towndata\\town.pal", 16 };
	case DTYPE_CATHEDRAL:
		return { "levels\\l1data\\l1.cel", "levels\\l1data\\l1.til", "levels\\l1data\\l1.min", "levels\\l1data\\l1.sol", "levels\\l1data\\l1_1.pal", 10 };
	case DTYPE_CATACOMBS:
		return { "levels\\l2data\\l2.cel", "levels\\l2data\\l2.til", "levels\\l2data\\l2.min", "levels\\l2data\\l2.sol", "levels\\l2data\\l2_1.pal", 10 };
	case DTYPE_CAVES:
		return { "levels\\l3data\\l3.cel", "levels\\l3data\\l3.til", "levels\\l3data\\l3.min", "levels\\l3data\\l3.sol", "levels\\l3data\\l3_1.pal", 10 };
	case DTYPE_HELL:
		return { "levels\\l4data\\l4.cel", "levels\\l4data\\l4.til", "levels\\l4data\\l4.min", "levels\\l4data\\l4.sol", "levels\\l4data\\l4_1.pal", 12 };
	case DTYPE_NEST:
		return { "nlevels\\l6data\\l6.cel", "nlevels\\l6data\\l6.til", "nlevels\\l6data\\l6.min", "nlevels\\l6data\\l6.sol", "nlevels\\l6data\\l6base1.pal", 10 };
	case DTYPE_CRYPT:
		return { "nlevels\\l5data\\l5.cel", "nlevels\\l5data\\l5.til", "nlevels\\l5data\\l5.min", "nlevels\\l5data\\l5.sol", "nlevels\\l5data\\l5base.pal", 10 };
	default:
		return { "", "", "", "", "", 10 };
	}
}

bool Active()
{
	return CurrentMix.primary != DTYPE_NONE && !CurrentMix.guests.empty();
}

void Clear()
{
	CurrentMix = Spec {};
	Sets.clear();
	Raw.clear();
	ReencodedToMerged.clear();
	SharedReady = false;
}

void SelectForCurrentLevel()
{
	Clear();
	if (setlevel && setlvlnum == SL_MIXTEST) {
		CurrentMix.primary = DTYPE_CATHEDRAL;
		CurrentMix.guests = { DTYPE_TOWN };
	}
}

const Spec &CurrentSpec()
{
	return CurrentMix;
}

const std::vector<LoadedSet> &LoadedSets()
{
	return Sets;
}

const std::array<uint8_t, 128 * 3> &SharedPalette()
{
	return Shared;
}

std::expected<void, std::string> LoadMixedSOLData()
{
	RETURN_IF_ERROR(EnsureLoaded());
	// The main set keeps its usual load, with the game's own fixes to the data.
	RETURN_IF_ERROR(LoadSingleLevelSOLData());
	for (size_t i = 1; i < Raw.size(); ++i) {
		const RawSet &set = Raw[i];
		const LoadedSet &loaded = Sets[i];
		std::memcpy(&SOLData[loaded.pieceOffset], set.sol.get(), set.solCount * sizeof(TileProperties));
	}
	return {};
}

std::expected<void, std::string> LoadMixedLvlGFX()
{
	RETURN_IF_ERROR(EnsureLoaded());

	// Merged cel: one frame table, every set's frames in order, guest pixels re-painted into the shared palette.
	uint32_t totalFrames = 0;
	size_t totalBytes = 0;
	for (const RawSet &set : Raw) {
		totalFrames += set.frameCount;
		totalBytes += FrameEnd(set, set.frameCount) - FrameBegin(set, 1);
	}
	const size_t headerBytes = (static_cast<size_t>(totalFrames) + 2) * 4;
	std::unique_ptr<std::byte[]> merged { new std::byte[headerBytes + totalBytes] };
	auto *out = reinterpret_cast<uint8_t *>(merged.get());
	WriteLE32(out, totalFrames);
	size_t writePos = headerBytes;
	uint32_t outFrame = 1;
	for (const RawSet &set : Raw) {
		const auto *src = reinterpret_cast<const uint8_t *>(set.cel.get());
		for (uint32_t frame = 1; frame <= set.frameCount; ++frame, ++outFrame) {
			const size_t begin = FrameBegin(set, frame);
			const size_t end = std::min(FrameEnd(set, frame), set.celSize);
			const size_t size = end > begin ? end - begin : 0;
			WriteLE32(out + outFrame * 4, static_cast<uint32_t>(writePos));
			std::memcpy(out + writePos, src + begin, size);
			if (set.frameType[frame] != 0xFF) {
				const std::array<uint8_t, 256> &remap = set.remap;
				ForEachPixelByte(out + writePos, size, static_cast<TileType>(set.frameType[frame]), [&](uint8_t &px) { px = remap[px]; });
			}
			writePos += size;
		}
	}
	WriteLE32(out + (static_cast<size_t>(totalFrames) + 1) * 4, static_cast<uint32_t>(writePos));
	pDungeonCels = std::move(merged);

	// Merged mega tiles, with each set's piece ids moved past the sets before it.
	size_t totalMegas = 0;
	for (const RawSet &set : Raw)
		totalMegas += set.tilCount;
	pMegaTiles = std::make_unique<MegaTile[]>(totalMegas);
	size_t megaPos = 0;
	for (size_t i = 0; i < Raw.size(); ++i) {
		const RawSet &set = Raw[i];
		const uint16_t offset = Sets[i].pieceOffset;
		for (size_t m = 0; m < set.tilCount; ++m) {
			const MegaTile &src = set.til[m];
			MegaTile &dst = pMegaTiles[megaPos++];
			dst.micro1 = Swap16LE(static_cast<uint16_t>(Swap16LE(src.micro1) + offset));
			dst.micro2 = Swap16LE(static_cast<uint16_t>(Swap16LE(src.micro2) + offset));
			dst.micro3 = Swap16LE(static_cast<uint16_t>(Swap16LE(src.micro3) + offset));
			dst.micro4 = Swap16LE(static_cast<uint16_t>(Swap16LE(src.micro4) + offset));
		}
	}

	// Special cels (arches, tree tops) stay the main set's for now.
	constexpr int SpecialCelWidth = 64;
	const char *special;
	switch (CurrentMix.primary) {
	case DTYPE_TOWN: special = "levels\\towndata\\towns"; break;
	case DTYPE_CATHEDRAL:
	case DTYPE_CAVES:
	case DTYPE_NEST: special = "levels\\l1data\\l1s"; break;
	case DTYPE_CATACOMBS:
	case DTYPE_HELL: special = "levels\\l2data\\l2s"; break;
	case DTYPE_CRYPT: special = "nlevels\\l5data\\l5s"; break;
	default: return std::unexpected("LoadMixedLvlGFX");
	}
	ASSIGN_OR_RETURN(pSpecialCels, LoadCelWithStatus(special, SpecialCelWidth));
	return {};
}

void SetMixedDungeonMicros(std::unique_ptr<std::byte[]> &dungeonCels, uint_fast8_t &microTileLen)
{
	if (Raw.empty()) {
		// LoadMixedLvlGFX did not run (or failed); nothing sensible to do.
		app_fatal("SetMixedDungeonMicros without loaded sets");
	}
	microTileLen = 16;
	std::memset(DPieceMicros, 0, sizeof(DPieceMicros));
	size_t pieceCount = 0;
	for (size_t i = 0; i < Raw.size(); ++i) {
		const RawSet &set = Raw[i];
		const LoadedSet &loaded = Sets[i];
		const int blocks = set.files.blocks;
		for (size_t p = 0; p < loaded.pieceCount; ++p) {
			const uint16_t *pieces = &set.min[blocks * p];
			MICROS &micros = DPieceMicros[loaded.pieceOffset + p];
			for (int block = 0; block < blocks; ++block) {
				const LevelCelBlock src { Swap16LE(pieces[blocks - 2 + (block & 1) - (block & 0xE)]) };
				if (!src.hasValue())
					continue;
				micros.mt[block] = LevelCelBlock::make(src.type(), static_cast<uint16_t>((src.data & 0xFFF) + loaded.frameOffset));
			}
		}
		pieceCount += loaded.pieceCount;
	}
	ReleaseRaw();
	// Re-encoding keeps only referenced frames, in ascending order; remember where each one came from.
	std::vector<bool> referenced(static_cast<size_t>(LevelCelBlock::MaxFrame) + 1, false);
	for (size_t p = 0; p < pieceCount; ++p)
		for (int block = 0; block < 16; ++block)
			if (DPieceMicros[p].mt[block].hasValue())
				referenced[DPieceMicros[p].mt[block].frame()] = true;
	ReencodedToMerged.clear();
	ReencodedToMerged.push_back(0);
	for (uint32_t frame = 1; frame <= LevelCelBlock::MaxFrame; ++frame)
		if (referenced[frame])
			ReencodedToMerged.push_back(frame);
	FinishDungeonMicros(dungeonCels, pieceCount, microTileLen);
}

bool FrameSource(uint32_t reencodedFrame, dungeon_type &type, uint32_t &originalFrame)
{
	if (reencodedFrame == 0 || reencodedFrame >= ReencodedToMerged.size())
		return false;
	const uint32_t merged = ReencodedToMerged[reencodedFrame];
	for (const LoadedSet &set : Sets) {
		if (merged > set.frameOffset && merged <= static_cast<uint32_t>(set.frameOffset) + set.frameCount) {
			type = set.type;
			originalFrame = merged - set.frameOffset;
			return true;
		}
	}
	return false;
}

void ApplyMixedPalette()
{
	if (!SharedReady)
		return;
	for (int i = 0; i < TileColours; ++i) {
		logical_palette[i].r = Shared[i * 3];
		logical_palette[i].g = Shared[i * 3 + 1];
		logical_palette[i].b = Shared[i * 3 + 2];
#ifndef USE_SDL1
		logical_palette[i].a = SDL_ALPHA_OPAQUE;
#endif
	}
}

void FillMegaTile(size_t guest, uint16_t megaId, Point position)
{
	if (guest >= Sets.size() || megaId == 0 || megaId > Sets[guest].megaCount)
		return;
	const MegaTile mega = pMegaTiles[Sets[guest].megaOffset + megaId - 1];
	dPiece[position.x + 0][position.y + 0] = Swap16LE(mega.micro1);
	dPiece[position.x + 1][position.y + 0] = Swap16LE(mega.micro2);
	dPiece[position.x + 0][position.y + 1] = Swap16LE(mega.micro3);
	dPiece[position.x + 1][position.y + 1] = Swap16LE(mega.micro4);
}

void BuildMixTestLevel()
{
	// A Tristram yard inside the Cathedral arena's big hall: megas x 3..10, y 13..20 of arena/church.dun,
	// one ring of Cathedral floor left between the hall walls and the grass.
	constexpr int X0 = 3;
	constexpr int Y0 = 13;
	constexpr int Size = 8;
	constexpr uint16_t Ground[] = { 6, 8, 7, 5, 2, 3, 1, 4 }; // flat Tristram grass and dirt
	constexpr uint16_t Trees[] = { 47, 44, 55, 41, 34 };
	constexpr uint16_t Rocks[] = { 63, 62 };
	constexpr uint16_t CathedralFloor = 13;
	const size_t town = 1; // guest 0 is the main set
	for (int my = Y0; my < Y0 + Size; ++my) {
		for (int mx = X0; mx < X0 + Size; ++mx) {
			dungeon[mx][my] = CathedralFloor; // the automap and door logic see plain floor here
			const Point piece = Point { mx, my }.megaToWorld();
			uint16_t mega = Ground[GenerateRnd(static_cast<int32_t>(std::size(Ground)))];
			const bool edge = mx == X0 || my == Y0 || mx == X0 + Size - 1 || my == Y0 + Size - 1;
			if (!edge) {
				const int roll = GenerateRnd(100);
				if (roll < 8)
					mega = Trees[GenerateRnd(static_cast<int32_t>(std::size(Trees)))];
				else if (roll < 12)
					mega = Rocks[GenerateRnd(static_cast<int32_t>(std::size(Rocks)))];
			}
			FillMegaTile(town, mega, piece);
		}
	}
}

} // namespace tilesetmix
} // namespace devilution
