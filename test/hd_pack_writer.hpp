/**
 * @file hd_pack_writer.hpp
 *
 * Writes HD pack files (hd\index.hdx, hd\pack_NNN.hdp) for the tests of their reader. The game itself never writes
 * them: tools/hd_pack.py does, and the reader's tests read one of its packs too where Python is to be had.
 */
#pragma once

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "utils/file_util.h"

namespace devilution {

namespace hdpacktest {

inline uint32_t Crc32(const std::string &data)
{
	uint32_t crc = 0xFFFFFFFFU;
	for (const char c : data) {
		crc ^= static_cast<uint8_t>(c);
		for (int bit = 0; bit < 8; bit++)
			crc = (crc & 1) != 0 ? (crc >> 1) ^ 0xEDB88320U : crc >> 1;
	}
	return crc ^ 0xFFFFFFFFU;
}

inline void Put32(std::string &out, uint32_t value)
{
	for (int shift = 0; shift < 32; shift += 8)
		out.push_back(static_cast<char>((value >> shift) & 0xFF));
}

inline bool WriteWhole(const std::string &path, const std::string &data)
{
	FILE *file = OpenFile(path.c_str(), "wb");
	if (file == nullptr)
		return false;
	const bool ok = data.empty() || std::fwrite(data.data(), data.size(), 1, file) == 1;
	return std::fclose(file) == 0 && ok;
}

inline std::string ReadWhole(const std::string &path)
{
	std::string data;
	FILE *file = OpenFile(path.c_str(), "rb");
	if (file == nullptr)
		return data;
	char buffer[4096];
	size_t got;
	while ((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
		data.append(buffer, got);
	std::fclose(file);
	return data;
}

inline std::string PackFileName(uint32_t pack)
{
	char name[32];
	std::snprintf(name, sizeof(name), "pack_%03u.hdp", static_cast<unsigned>(pack));
	return name;
}

/**
 * @brief Writes an index and its pack files into `dir` (given with its last slash; it must be there already).
 * @param files The files by the names the index knows them by: under hd\, lower case, backslashes.
 * @param packLimit The most bytes a pack file may hold: a new one is begun when the next file would pass it.
 * @return How many pack files were written, or 0 if one could not be.
 */
inline uint32_t WritePack(const std::string &dir, const std::map<std::string, std::string> &files, uint32_t packLimit, uint32_t setId = 0x5EED1234U, uint32_t version = 1)
{
	struct Blob {
		uint32_t pack, offset, size, crc;
	};
	std::vector<Blob> blobs;
	std::map<std::string, uint32_t> blobOfContent;
	std::vector<std::string> packs;
	std::string names;
	std::string nameTable;
	const auto header = [&](uint32_t pack) {
		std::string start("HDPKDAT\0", 8);
		Put32(start, version);
		Put32(start, setId);
		Put32(start, pack);
		Put32(start, 0);
		return start;
	};
	// std::map goes through the names in the order of their bytes, which is the order the index keeps them in.
	for (const auto &[name, data] : files) {
		auto [it, isNew] = blobOfContent.try_emplace(data, static_cast<uint32_t>(blobs.size()));
		if (isNew) {
			if (packs.empty() || packs.back().size() + data.size() > packLimit)
				packs.push_back(header(static_cast<uint32_t>(packs.size())));
			blobs.push_back({ static_cast<uint32_t>(packs.size() - 1), static_cast<uint32_t>(packs.back().size()), static_cast<uint32_t>(data.size()), Crc32(data) });
			packs.back() += data;
		}
		Put32(nameTable, static_cast<uint32_t>(names.size()));
		Put32(nameTable, it->second);
		names += name;
		names.push_back('\0');
	}
	std::string body;
	for (const std::string &pack : packs)
		Put32(body, static_cast<uint32_t>(pack.size()));
	for (const Blob &blob : blobs) {
		Put32(body, blob.pack);
		Put32(body, blob.offset);
		Put32(body, blob.size);
		Put32(body, blob.crc);
	}
	body += nameTable;
	body += names;
	std::string index("HDPKIDX\0", 8);
	Put32(index, version);
	Put32(index, setId);
	Put32(index, static_cast<uint32_t>(packs.size()));
	Put32(index, static_cast<uint32_t>(blobs.size()));
	Put32(index, static_cast<uint32_t>(files.size()));
	Put32(index, static_cast<uint32_t>(names.size()));
	Put32(index, Crc32(body));
	Put32(index, Crc32(index));
	index += body;
	for (size_t pack = 0; pack < packs.size(); pack++) {
		if (!WriteWhole(dir + PackFileName(static_cast<uint32_t>(pack)), packs[pack]))
			return 0;
	}
	if (!WriteWhole(dir + "index.hdx", index))
		return 0;
	return static_cast<uint32_t>(packs.size());
}

/** @brief Bytes that are different for every `seed`, and not all text. */
inline std::string SomeBytes(uint32_t seed, size_t size)
{
	std::string data;
	data.reserve(size);
	uint32_t state = seed * 2654435761U + 12345U;
	for (size_t i = 0; i < size; i++) {
		state = state * 1664525U + 1013904223U;
		data.push_back(static_cast<char>(state >> 24));
	}
	return data;
}

} // namespace hdpacktest

} // namespace devilution
