#include "poe2/hd_pack.hpp"

#ifdef POE2_HD_PACK

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>

#include <SDL.h>

#include "utils/file_util.h"
#include "utils/log.hpp"
#include "utils/sdl_mutex.h"

// The files (little-endian; made by tools/hd_pack.py, which describes them too):
//
// index.hdx    "HDPKIDX\0", version, set id, pack count, blob count, name count, bytes of names, CRC-32 of all that
//              follows the header, CRC-32 of the header up to here (40 bytes);
//              a pack: file size;  a blob: pack, offset, length, CRC-32;  a name: offset into the names, blob;
//              the names, each ended by a zero byte. The names' table is sorted by name.
// pack_NNN.hdp "HDPKDAT\0", version, set id, pack number, 0 (24 bytes); then the blobs, end to end.
//
// A name is the file's path under hd\, in lower case with backslashes. Every number is 32 bits: a pack stays under 2 GB.

namespace devilution {

namespace hdpack {

namespace {

constexpr char IndexMagic[8] = { 'H', 'D', 'P', 'K', 'I', 'D', 'X', '\0' };
constexpr char PackMagic[8] = { 'H', 'D', 'P', 'K', 'D', 'A', 'T', '\0' };
constexpr uint32_t FormatVersion = 1;
constexpr size_t IndexHeaderSize = 40;
constexpr size_t PackHeaderSize = 24;
constexpr uint32_t MaxPackSize = 0x7FFFFFFF;
constexpr size_t MaxNameSize = 512;
/** @brief A damaged pack is told of in the log this many times, then only counted. */
constexpr int MaxDamageLogged = 20;

uint32_t Le32(const uint8_t *p)
{
	return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

/** @brief Eight tables of 256: the CRC is worked out eight bytes at a step (a picture's worth in a few microseconds). */
const uint32_t *CrcTables()
{
	static const uint32_t *tables = [] {
		auto *made = new uint32_t[8 * 256];
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t c = i;
			for (int bit = 0; bit < 8; bit++)
				c = (c & 1) != 0 ? 0xEDB88320U ^ (c >> 1) : c >> 1;
			made[i] = c;
		}
		for (uint32_t table = 1; table < 8; table++) {
			for (uint32_t i = 0; i < 256; i++) {
				const uint32_t before = made[(table - 1) * 256 + i];
				made[table * 256 + i] = (before >> 8) ^ made[before & 0xFF];
			}
		}
		return made;
	}();
	return tables;
}

/** @brief The CRC-32 zlib and PNG use. */
uint32_t Crc32(const uint8_t *data, size_t size)
{
	const uint32_t *t = CrcTables();
	uint32_t crc = 0xFFFFFFFFU;
	for (; size >= 8; size -= 8, data += 8) {
		const uint32_t one = Le32(data) ^ crc;
		const uint32_t two = Le32(data + 4);
		crc = t[7 * 256 + (one & 0xFF)] ^ t[6 * 256 + ((one >> 8) & 0xFF)] ^ t[5 * 256 + ((one >> 16) & 0xFF)] ^ t[4 * 256 + (one >> 24)]
		    ^ t[3 * 256 + (two & 0xFF)] ^ t[2 * 256 + ((two >> 8) & 0xFF)] ^ t[1 * 256 + ((two >> 16) & 0xFF)] ^ t[two >> 24];
	}
	for (size_t i = 0; i < size; i++)
		crc = t[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
	return crc ^ 0xFFFFFFFFU;
}

SDL_atomic_t Damage;

void TellDamage(const std::string &what)
{
	if (SDL_AtomicAdd(&Damage, 1) < MaxDamageLogged)
		LogError("HD pack: {}", what);
}

struct PackFile {
	FILE *file = nullptr;
	/** @brief Held while the file is moved in and read: one file is read from several threads. */
	SdlMutex lock;
};

/** @brief An index and its pack files, as read by Mount. Nothing in it changes afterwards but the packs' file positions. */
class PackSet {
public:
	~PackSet()
	{
		// An index refused part of the way through being read has no pack files yet.
		for (uint32_t i = 0; packs_ != nullptr && i < packCount_; i++) {
			if (packs_[i].file != nullptr)
				std::fclose(packs_[i].file);
		}
	}

	static std::shared_ptr<PackSet> Load(const std::string &dir, std::string &error);

	/** @brief The blob of a name (as Normalise makes it), or -1. */
	[[nodiscard]] int64_t Find(const char *name, size_t size) const
	{
		uint32_t low = 0;
		uint32_t high = nameCount_;
		while (low < high) {
			const uint32_t mid = low + (high - low) / 2;
			const uint8_t *entry = names_ + static_cast<size_t>(mid) * 8;
			const char *stored = nameBytes_ + Le32(entry);
			int order = std::strncmp(stored, name, size);
			if (order == 0 && stored[size] != '\0')
				order = 1;
			if (order == 0) {
				const uint32_t blob = Le32(entry + 4);
				// Names in a pack that is missing or cut short are not there.
				return packs_[Le32(blobs_ + static_cast<size_t>(blob) * 16)].file != nullptr ? static_cast<int64_t>(blob) : -1;
			}
			if (order < 0)
				low = mid + 1;
			else
				high = mid;
		}
		return -1;
	}

	[[nodiscard]] uint32_t BlobSize(uint32_t blob) const
	{
		return Le32(blobs_ + static_cast<size_t>(blob) * 16 + 8);
	}

	/** @brief Reads a blob whole into `out` (BlobSize bytes) and checks it. Safe on any thread. */
	bool ReadBlob(uint32_t blob, uint8_t *out)
	{
		const uint8_t *entry = blobs_ + static_cast<size_t>(blob) * 16;
		const uint32_t pack = Le32(entry);
		const uint32_t offset = Le32(entry + 4);
		const uint32_t size = Le32(entry + 8);
		bool read;
		{
			const std::lock_guard<SdlMutex> guard(packs_[pack].lock);
			FILE *file = packs_[pack].file;
			read = std::fseek(file, static_cast<long>(offset), SEEK_SET) == 0 && (size == 0 || std::fread(out, size, 1, file) == 1);
			if (!read)
				std::clearerr(file);
		}
		if (!read) {
			TellDamage(dir_ + PackName(pack) + " could not be read at " + std::to_string(offset) + " (" + std::to_string(size) + " bytes)");
			return false;
		}
		if (Crc32(out, size) != Le32(entry + 12)) {
			TellDamage(dir_ + PackName(pack) + " is damaged at " + std::to_string(offset) + " (" + std::to_string(size) + " bytes): its checksum is wrong. Copy the pack again, or run tools/hd_pack.py check");
			return false;
		}
		return true;
	}

	[[nodiscard]] uint32_t NameCount() const
	{
		return nameCount_;
	}

	static std::string PackName(uint32_t pack)
	{
		char name[32];
		std::snprintf(name, sizeof(name), "pack_%03u.hdp", static_cast<unsigned>(pack));
		return name;
	}

private:
	std::string dir_;
	std::vector<uint8_t> index_;
	uint32_t packCount_ = 0;
	uint32_t blobCount_ = 0;
	uint32_t nameCount_ = 0;
	const uint8_t *blobs_ = nullptr;
	const uint8_t *names_ = nullptr;
	const char *nameBytes_ = nullptr;
	std::unique_ptr<PackFile[]> packs_;
};

std::shared_ptr<PackSet> PackSet::Load(const std::string &dir, std::string &error)
{
	const std::string indexPath = dir + "index.hdx";
	FILE *file = OpenFile(indexPath.c_str(), "rb");
	if (file == nullptr) {
		error = "no " + indexPath;
		return nullptr;
	}
	auto set = std::make_shared<PackSet>();
	set->dir_ = dir;
	std::vector<uint8_t> &index = set->index_;
	uint8_t header[IndexHeaderSize];
	bool ok = std::fread(header, sizeof(header), 1, file) == 1;
	if (ok && (std::memcmp(header, IndexMagic, sizeof(IndexMagic)) != 0 || Crc32(header, 36) != Le32(header + 36))) {
		std::fclose(file);
		error = indexPath + " is not an HD pack index, or its start is damaged";
		return nullptr;
	}
	if (ok && Le32(header + 8) != FormatVersion) {
		std::fclose(file);
		error = indexPath + " is of version " + std::to_string(Le32(header + 8)) + "; this game reads version " + std::to_string(FormatVersion);
		return nullptr;
	}
	const uint32_t setId = Le32(header + 12);
	const uint64_t packCount = Le32(header + 16);
	const uint64_t blobCount = Le32(header + 20);
	const uint64_t nameCount = Le32(header + 24);
	const uint64_t nameBytes = Le32(header + 28);
	const uint64_t bodySize = packCount * 4 + blobCount * 16 + nameCount * 8 + nameBytes;
	if (ok) {
		std::uintmax_t fileSize = 0;
		ok = GetFileSize(indexPath.c_str(), &fileSize) && fileSize == IndexHeaderSize + bodySize && packCount <= 0xFFFF && bodySize < MaxPackSize;
	}
	if (ok) {
		index.resize(static_cast<size_t>(bodySize));
		ok = bodySize == 0 || std::fread(index.data(), index.size(), 1, file) == 1;
	}
	std::fclose(file);
	if (!ok || Crc32(index.data(), index.size()) != Le32(header + 32)) {
		error = indexPath + " is cut short or damaged (its size or checksum is wrong)";
		return nullptr;
	}
	set->packCount_ = static_cast<uint32_t>(packCount);
	set->blobCount_ = static_cast<uint32_t>(blobCount);
	set->nameCount_ = static_cast<uint32_t>(nameCount);
	const uint8_t *packSizes = index.data();
	set->blobs_ = packSizes + packCount * 4;
	set->names_ = set->blobs_ + blobCount * 16;
	set->nameBytes_ = reinterpret_cast<const char *>(set->names_ + nameCount * 8);

	// The checksum says the index is as it was written; what follows makes sure no number in it leads outside a
	// table or a file whoever wrote it, so that a lookup needs no checks of its own.
	for (uint64_t blob = 0; blob < blobCount; blob++) {
		const uint8_t *entry = set->blobs_ + blob * 16;
		const uint64_t pack = Le32(entry);
		if (pack >= packCount || static_cast<uint64_t>(Le32(entry + 4)) + Le32(entry + 8) > Le32(packSizes + pack * 4) || Le32(entry + 4) < PackHeaderSize) {
			error = indexPath + " names a place outside its pack files";
			return nullptr;
		}
	}
	if (nameCount > 0 && (nameBytes == 0 || set->nameBytes_[nameBytes - 1] != '\0')) {
		error = indexPath + " has a name without an end";
		return nullptr;
	}
	const char *previous = nullptr;
	for (uint64_t name = 0; name < nameCount; name++) {
		const uint8_t *entry = set->names_ + name * 8;
		if (Le32(entry) >= nameBytes || Le32(entry + 4) >= blobCount) {
			error = indexPath + " has a name that leads outside its tables";
			return nullptr;
		}
		const char *stored = set->nameBytes_ + Le32(entry);
		if (previous != nullptr && std::strcmp(previous, stored) >= 0) {
			error = indexPath + " has its names out of order";
			return nullptr;
		}
		previous = stored;
	}

	set->packs_ = std::make_unique<PackFile[]>(set->packCount_ > 0 ? set->packCount_ : 1);
	for (uint32_t pack = 0; pack < set->packCount_; pack++) {
		const std::string path = dir + PackName(pack);
		const uint32_t wantedSize = Le32(packSizes + static_cast<size_t>(pack) * 4);
		std::uintmax_t fileSize = 0;
		FILE *packFile = OpenFile(path.c_str(), "rb");
		uint8_t packHeader[PackHeaderSize];
		std::string wrong;
		if (packFile == nullptr)
			wrong = " is missing";
		else if (!GetFileSize(path.c_str(), &fileSize) || fileSize < wantedSize)
			wrong = " is cut short (" + std::to_string(static_cast<uint64_t>(fileSize)) + " bytes of " + std::to_string(wantedSize) + ")";
		else if (fileSize > MaxPackSize)
			wrong = " is too big";
		else if (std::fread(packHeader, sizeof(packHeader), 1, packFile) != 1 || std::memcmp(packHeader, PackMagic, sizeof(PackMagic)) != 0 || Le32(packHeader + 8) != FormatVersion)
			wrong = " is not an HD pack file of this version";
		else if (Le32(packHeader + 12) != setId || Le32(packHeader + 16) != pack)
			wrong = " belongs to another index (the packs and index.hdx must be copied together)";
		if (!wrong.empty()) {
			// The rest of the art is still shown: only this file's pictures are missing.
			TellDamage(path + wrong + ": its pictures are left out");
			if (packFile != nullptr)
				std::fclose(packFile);
			continue;
		}
		set->packs_[pack].file = packFile;
	}
	return set;
}

/** @brief What is mounted. Made once and never freed: the HD reader's threads may still look in it as the game ends. */
struct State {
	SdlMutex lock;
	std::shared_ptr<PackSet> set;
	/** @brief The game's folders MountFromGameFolders last looked in. */
	std::vector<std::string> dirs;
	bool searched = false;
};

State &TheState()
{
	static State *state = new State();
	return *state;
}

std::shared_ptr<PackSet> Mounted()
{
	State &state = TheState();
	const std::lock_guard<SdlMutex> guard(state.lock);
	return state.set;
}

/** @brief A path under hd\ as the index names it: without the hd\, in lower case, with backslashes. False if it is no such path. */
bool Normalise(std::string_view assetPath, char *name, size_t &size)
{
	if (assetPath.size() < 4 || assetPath.size() - 3 >= MaxNameSize)
		return false;
	if ((assetPath[0] != 'h' && assetPath[0] != 'H') || (assetPath[1] != 'd' && assetPath[1] != 'D') || (assetPath[2] != '\\' && assetPath[2] != '/'))
		return false;
	size = assetPath.size() - 3;
	for (size_t i = 0; i < size; i++) {
		char c = assetPath[i + 3];
		if (c == '\0')
			return false;
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
		else if (c == '/')
			c = '\\';
		name[i] = c;
	}
	name[size] = '\0';
	return true;
}

/** @brief A file of a pack being read through an SDL stream: read whole the first time any of it is asked for. */
struct PackStream {
	std::shared_ptr<PackSet> set;
	uint32_t blob = 0;
	uint32_t size = 0;
	uint32_t position = 0;
	bool failed = false;
	std::unique_ptr<uint8_t[]> data;
};

Sint64 SDLCALL StreamSize(SDL_RWops *rw)
{
	return static_cast<const PackStream *>(rw->hidden.unknown.data1)->size;
}

Sint64 SDLCALL StreamSeek(SDL_RWops *rw, Sint64 offset, int whence)
{
	auto *stream = static_cast<PackStream *>(rw->hidden.unknown.data1);
	Sint64 position;
	switch (whence) {
	case RW_SEEK_SET: position = offset; break;
	case RW_SEEK_CUR: position = static_cast<Sint64>(stream->position) + offset; break;
	case RW_SEEK_END: position = static_cast<Sint64>(stream->size) + offset; break;
	default: return SDL_SetError("HD pack: unknown seek");
	}
	if (position < 0 || position > static_cast<Sint64>(stream->size))
		return SDL_SetError("HD pack: seek outside the file");
	stream->position = static_cast<uint32_t>(position);
	return position;
}

size_t SDLCALL StreamRead(SDL_RWops *rw, void *out, size_t size, size_t count)
{
	auto *stream = static_cast<PackStream *>(rw->hidden.unknown.data1);
	if (size == 0 || count == 0 || stream->failed)
		return 0;
	if (stream->data == nullptr) {
		stream->data.reset(new (std::nothrow) uint8_t[stream->size > 0 ? stream->size : 1]);
		if (stream->data == nullptr || !stream->set->ReadBlob(stream->blob, stream->data.get())) {
			stream->failed = true;
			stream->data = nullptr;
			SDL_SetError("HD pack: the file could not be read");
			return 0;
		}
	}
	const size_t left = stream->size - stream->position;
	const size_t whole = left / size < count ? left / size : count;
	std::memcpy(out, stream->data.get() + stream->position, whole * size);
	stream->position += static_cast<uint32_t>(whole * size);
	return whole;
}

size_t SDLCALL StreamWrite(SDL_RWops * /*rw*/, const void * /*data*/, size_t /*size*/, size_t /*count*/)
{
	SDL_SetError("HD pack: read-only");
	return 0;
}

int SDLCALL StreamClose(SDL_RWops *rw)
{
	if (rw != nullptr) {
		delete static_cast<PackStream *>(rw->hidden.unknown.data1);
		SDL_FreeRW(rw);
	}
	return 0;
}

} // namespace

bool IsPackPath(std::string_view assetPath)
{
	return assetPath.size() >= 4 && (assetPath[0] == 'h' || assetPath[0] == 'H') && (assetPath[1] == 'd' || assetPath[1] == 'D') && (assetPath[2] == '\\' || assetPath[2] == '/');
}

bool Mount(const std::string &hdDir)
{
	std::string error;
	std::shared_ptr<PackSet> set = PackSet::Load(hdDir, error);
	if (set == nullptr)
		LogError("HD pack: {}", error);
	else
		LogVerbose("HD pack: {} pictures in the pack files of {}", set->NameCount(), hdDir);
	State &state = TheState();
	const std::lock_guard<SdlMutex> guard(state.lock);
	state.set = set;
	// Mounted by hand: the game's folders are looked in afresh when they are next asked for.
	state.searched = false;
	return set != nullptr;
}

void Unmount()
{
	State &state = TheState();
	const std::lock_guard<SdlMutex> guard(state.lock);
	state.set = nullptr;
	state.searched = false;
}

void MountFromGameFolders(const std::vector<std::string> &first, const std::string &last)
{
	State &state = TheState();
	const std::lock_guard<SdlMutex> guard(state.lock);
	if (state.searched && state.dirs.size() == first.size() + 1 && state.dirs.back() == last && std::equal(first.begin(), first.end(), state.dirs.begin()))
		return;
	state.dirs = first;
	state.dirs.push_back(last);
	state.searched = true;
	state.set = nullptr;
	for (const std::string &dir : state.dirs) {
		const std::string hdDir = dir + "hd" DIRECTORY_SEPARATOR_STR;
		if (!FileExists((hdDir + "index.hdx").c_str()))
			continue;
		std::string error;
		state.set = PackSet::Load(hdDir, error);
		if (state.set != nullptr) {
			Log("HD pack: {} pictures in the pack files of {}", state.set->NameCount(), hdDir);
			return;
		}
		LogError("HD pack: {}", error);
	}
}

bool Has(std::string_view assetPath)
{
	char name[MaxNameSize];
	size_t size;
	if (!Normalise(assetPath, name, size))
		return false;
	State &state = TheState();
	const std::lock_guard<SdlMutex> guard(state.lock);
	return state.set != nullptr && state.set->Find(name, size) >= 0;
}

SDL_RWops *Open(std::string_view assetPath)
{
	char name[MaxNameSize];
	size_t size;
	if (!Normalise(assetPath, name, size))
		return nullptr;
	std::shared_ptr<PackSet> set = Mounted();
	if (set == nullptr)
		return nullptr;
	const int64_t blob = set->Find(name, size);
	if (blob < 0)
		return nullptr;
	auto *stream = new (std::nothrow) PackStream();
	SDL_RWops *rw = stream != nullptr ? SDL_AllocRW() : nullptr;
	if (rw == nullptr) {
		delete stream;
		return nullptr;
	}
	stream->blob = static_cast<uint32_t>(blob);
	stream->size = set->BlobSize(stream->blob);
	stream->set = std::move(set);
	rw->type = SDL_RWOPS_UNKNOWN;
	rw->size = StreamSize;
	rw->seek = StreamSeek;
	rw->read = StreamRead;
	rw->write = StreamWrite;
	rw->close = StreamClose;
	rw->hidden.unknown.data1 = stream;
	return rw;
}

bool ReadFile(std::string_view assetPath, std::vector<uint8_t> &bytes)
{
	char name[MaxNameSize];
	size_t size;
	if (!Normalise(assetPath, name, size))
		return false;
	const std::shared_ptr<PackSet> set = Mounted();
	if (set == nullptr)
		return false;
	const int64_t blob = set->Find(name, size);
	if (blob < 0)
		return false;
	bytes.resize(set->BlobSize(static_cast<uint32_t>(blob)));
	uint8_t none = 0;
	return set->ReadBlob(static_cast<uint32_t>(blob), bytes.empty() ? &none : bytes.data());
}

uint32_t NameCount()
{
	const std::shared_ptr<PackSet> set = Mounted();
	return set != nullptr ? set->NameCount() : 0;
}

uint32_t DamageCount()
{
	return static_cast<uint32_t>(SDL_AtomicGet(&Damage));
}

} // namespace hdpack

} // namespace devilution

#else

namespace devilution {

namespace hdpack {

bool IsPackPath(std::string_view) { return false; }
bool Mount(const std::string &) { return false; }
void Unmount() { }
void MountFromGameFolders(const std::vector<std::string> &, const std::string &) { }
bool Has(std::string_view) { return false; }
SDL_RWops *Open(std::string_view) { return nullptr; }
bool ReadFile(std::string_view, std::vector<uint8_t> &) { return false; }
uint32_t NameCount() { return 0; }
uint32_t DamageCount() { return 0; }

} // namespace hdpack

} // namespace devilution

#endif
