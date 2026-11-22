#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <SDL.h>

#include "engine/assets.hpp"
#include "hd_pack_writer.hpp"
#include "poe2/hd_pack.hpp"
#include "utils/file_util.h"
#include "utils/paths.h"
#include "utils/png.h"

using namespace devilution;
using namespace devilution::hdpacktest;

namespace {

#ifdef POE2_HD_PACK

/**
 * @brief The game's own way to a file (FindAsset, OpenAsset) with HD art in pack files: the game's assets folder is a
 * folder of the test's, with a pack in its hd folder and loose files beside it.
 */
class HdPackAssetsTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		savedOverridePaths_ = OverridePaths;
		savedAssetsPath_ = paths::AssetsPath();
		OverridePaths.clear();
		const auto *testInfo = ::testing::UnitTest::GetInstance()->current_test_info();
		root_ = std::string("Test_") + testInfo->test_suite_name() + "_" + testInfo->name() + DIRECTORY_SEPARATOR_STR;
		assets_ = root_ + "assets" DIRECTORY_SEPARATOR_STR;
		mod_ = root_ + "mod" DIRECTORY_SEPARATOR_STR;
		hd_ = assets_ + "hd" DIRECTORY_SEPARATOR_STR;
		RecursivelyCreateDir(hd_.c_str());
		RemoveFile((hd_ + "index.hdx").c_str());
		paths::SetAssetsPath(assets_);

		packed_["pack.txt"] = "hd pack\r\n";
		packed_["tiles\\town\\1.png"] = SomeBytes(1, 1500);
		packed_["tiles\\town\\2.png"] = SomeBytes(2, 1500);
		packed_["tiles\\town\\3.png"] = packed_["tiles\\town\\1.png"];
		packed_["monsters\\sking\\skinga\\d3_f05.png"] = SomeBytes(3, 800);
	}

	void TearDown() override
	{
		hdpack::Unmount();
		for (const std::string &file : created_)
			RemoveFile(file.c_str());
		RemoveFile((hd_ + "index.hdx").c_str());
		for (uint32_t pack = 0; pack < 8; pack++)
			RemoveFile((hd_ + PackFileName(pack)).c_str());
		OverridePaths = savedOverridePaths_;
		paths::SetAssetsPath(savedAssetsPath_);
		// The next test's folders are another's: the pack is looked for anew.
		hdpack::MountFromGameFolders({}, std::string("Test_no_such_folder") + DIRECTORY_SEPARATOR_STR);
	}

	void WritePackFiles()
	{
		ASSERT_GT(WritePack(hd_, packed_, 2000), 1U);
	}

	/** @brief Writes a loose file by its asset path under a folder (the assets folder or a mod's). */
	void WriteLoose(const std::string &folder, const std::string &assetPath, const std::string &data)
	{
		std::string path = folder + assetPath;
#ifndef _WIN32
		for (char &c : path) {
			if (c == '\\')
				c = '/';
		}
#endif
		RecursivelyCreateDir(path.substr(0, path.find_last_of("\\/")).c_str());
		ASSERT_TRUE(WriteWhole(path, data)) << path;
		created_.push_back(path);
	}

	/** @brief A file as the game reads it, or "<none>". */
	static std::string Asset(const std::string &assetPath)
	{
		AssetRef ref = FindAsset(assetPath);
		if (!ref.ok())
			return "<none>";
		const size_t size = ref.size();
		AssetHandle handle = OpenAsset(std::move(ref));
		if (!handle.ok())
			return "<not opened>";
		std::string data(size, '\0');
		if (size > 0 && !handle.read(data.data(), size))
			return "<not read>";
		return data;
	}

	std::string root_;
	std::string assets_;
	std::string mod_;
	std::string hd_;
	std::map<std::string, std::string> packed_;

private:
	std::vector<std::string> created_;
	std::vector<std::string> savedOverridePaths_;
	std::string savedAssetsPath_;
};

TEST_F(HdPackAssetsTest, TheGameFindsPackedArtWithNoLooseFiles)
{
	EXPECT_FALSE(FindAsset("hd\\pack.txt").ok()) << "nothing is found before there is a pack";
	// The folders are the same and were looked in: only new folders make the game look for a pack again.
	OverridePaths.push_back(mod_);
	WritePackFiles();
	for (const auto &[name, data] : packed_) {
		EXPECT_TRUE(FindAsset("hd\\" + name).ok()) << name;
		EXPECT_EQ(Asset("hd\\" + name), data) << name;
	}
	EXPECT_FALSE(FindAsset("hd\\tiles\\town\\4.png").ok());
	EXPECT_FALSE(FindAsset("hd\\tiles\\town\\").ok());
	EXPECT_FALSE(FindAsset("tiles\\town\\1.png").ok()) << "only what is asked for under hd\\ is looked for in the pack";
}

TEST_F(HdPackAssetsTest, ALooseFileIsUsedBeforeThePack)
{
	WritePackFiles();
	// A repaint dropped into assets\hd beside the pack files.
	WriteLoose(assets_, "hd\\tiles\\town\\2.png", "a loose repaint");
	// And one in a mod's folder, which comes before the game's assets.
	OverridePaths.push_back(mod_);
	WriteLoose(mod_, "hd\\monsters\\sking\\skinga\\d3_f05.png", "the mod's own");
	WriteLoose(assets_, "hd\\monsters\\sking\\skinga\\d3_f05.png", "a loose repaint the mod hides");
	// A loose file the pack has no name for.
	WriteLoose(assets_, "hd\\tiles\\town\\new.png", "new art");

	EXPECT_EQ(Asset("hd\\tiles\\town\\2.png"), "a loose repaint");
	EXPECT_EQ(Asset("hd\\monsters\\sking\\skinga\\d3_f05.png"), "the mod's own");
	EXPECT_EQ(Asset("hd\\tiles\\town\\new.png"), "new art");
	// The rest still comes from the pack, and the pack itself still has what it had.
	EXPECT_EQ(Asset("hd\\tiles\\town\\1.png"), packed_["tiles\\town\\1.png"]);
	EXPECT_EQ(Asset("hd\\tiles\\town\\3.png"), packed_["tiles\\town\\1.png"]);
	EXPECT_EQ(Asset("hd\\pack.txt"), "hd pack\r\n");
	std::vector<uint8_t> bytes;
	ASSERT_TRUE(hdpack::ReadFile("hd\\tiles\\town\\2.png", bytes));
	EXPECT_EQ(std::string(bytes.begin(), bytes.end()), packed_["tiles\\town\\2.png"]);
}

TEST_F(HdPackAssetsTest, APackInAModFolderIsFoundBeforeTheGamesOwn)
{
	WritePackFiles();
	std::map<std::string, std::string> modFiles;
	modFiles["tiles\\town\\1.png"] = "from the mod's pack";
	const std::string modHd = mod_ + "hd" DIRECTORY_SEPARATOR_STR;
	RecursivelyCreateDir(modHd.c_str());
	ASSERT_EQ(WritePack(modHd, modFiles, 2000, 0x00000A0DU), 1U);
	EXPECT_EQ(Asset("hd\\tiles\\town\\1.png"), packed_["tiles\\town\\1.png"]);
	OverridePaths.push_back(mod_);
	EXPECT_EQ(Asset("hd\\tiles\\town\\1.png"), "from the mod's pack");
	OverridePaths.clear();
	EXPECT_EQ(Asset("hd\\tiles\\town\\1.png"), packed_["tiles\\town\\1.png"]);
	hdpack::Unmount();
	RemoveFile((modHd + "index.hdx").c_str());
	RemoveFile((modHd + PackFileName(0)).c_str());
}

TEST_F(HdPackAssetsTest, ADamagedPackedPictureIsNoPictureAndNoCrash)
{
	WritePackFiles();
	const std::string name = "tiles\\town\\2.png";
	bool damaged = false;
	for (uint32_t pack = 0; pack < 8 && !damaged; pack++) {
		std::string bytes = ReadWhole(hd_ + PackFileName(pack));
		const size_t at = bytes.find(packed_[name]);
		if (at == std::string::npos)
			continue;
		bytes[at + 700] = static_cast<char>(bytes[at + 700] ^ 0x08);
		ASSERT_TRUE(WriteWhole(hd_ + PackFileName(pack), bytes));
		damaged = true;
	}
	ASSERT_TRUE(damaged);
	const uint32_t before = hdpack::DamageCount();
	InitPNG();
	EXPECT_EQ(Asset("hd\\" + name), "<not read>");
	SDL_RWops *stream = OpenAssetAsSdlRwOps("hd\\" + name);
	ASSERT_NE(stream, nullptr);
	EXPECT_EQ(IMG_LoadPNG_RW(stream), nullptr);
	SDL_RWclose(stream);
	SDL_ClearError();
	EXPECT_GT(hdpack::DamageCount(), before);
	EXPECT_EQ(Asset("hd\\tiles\\town\\1.png"), packed_["tiles\\town\\1.png"]);
}

/** @brief A real PNG is unpacked to the very same pixels from a pack file as from the loose file it was packed from. */
TEST_F(HdPackAssetsTest, APackedPngUnpacksToTheSamePixels)
{
	ASSERT_NE(InitPNG() & IMG_INIT_PNG, 0);
	constexpr int Width = 96;
	constexpr int Height = 61;
	SDL_Surface *made = SDL_CreateRGBSurfaceWithFormat(0, Width, Height, 32, SDL_PIXELFORMAT_RGBA32);
	ASSERT_NE(made, nullptr);
	uint32_t state = 7;
	for (int y = 0; y < Height; y++) {
		auto *row = static_cast<uint8_t *>(made->pixels) + y * made->pitch;
		for (int x = 0; x < Width * 4; x++) {
			state = state * 1664525U + 1013904223U;
			// Runs of flat colour, noise, and see-through edges, as painted art has.
			row[x] = (x / 4 + y) % 9 < 4 ? static_cast<uint8_t>(y * 3) : static_cast<uint8_t>(state >> 24);
		}
	}
	const std::string loosePath = root_ + "made.png";
	ASSERT_EQ(IMG_SavePNG(made, loosePath.c_str()), 0) << SDL_GetError();
	const std::string png = ReadWhole(loosePath);
	ASSERT_GT(png.size(), 1000U);
	RemoveFile(loosePath.c_str());

	packed_["monsters\\sking\\skinga\\d3_f05.png"] = png;
	packed_["monsters\\sking\\skinga@trn\\red\\d3_f05.png"] = png;
	WritePackFiles();
	EXPECT_EQ(Asset("hd\\monsters\\sking\\skinga\\d3_f05.png"), png) << "the PNG's bytes come out of the pack as they went in";

	for (const char *path : { "hd\\monsters\\sking\\skinga\\d3_f05.png", "hd\\monsters\\sking\\skinga@trn\\red\\d3_f05.png" }) {
		SDL_Surface *loaded = LoadPNG(path);
		ASSERT_NE(loaded, nullptr) << path << ": " << SDL_GetError();
		SDL_Surface *rgba = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
		SDL_FreeSurface(loaded);
		ASSERT_NE(rgba, nullptr);
		ASSERT_EQ(rgba->w, Width);
		ASSERT_EQ(rgba->h, Height);
		int different = 0;
		for (int y = 0; y < Height; y++) {
			if (std::memcmp(static_cast<uint8_t *>(rgba->pixels) + y * rgba->pitch, static_cast<uint8_t *>(made->pixels) + y * made->pitch, Width * 4) != 0)
				different++;
		}
		EXPECT_EQ(different, 0) << path;
		SDL_FreeSurface(rgba);
	}
	SDL_FreeSurface(made);
}

/**
 * @brief Not a test of right and wrong: with HD_PACK_BENCH=<count> it times the game's own way to that many small
 * files, loose and from a pack, and prints what it measured. Without it, it only checks a few come out the same.
 */
TEST_F(HdPackAssetsTest, TimeLooseAgainstPacked)
{
	const char *env = std::getenv("HD_PACK_BENCH");
	const int count = env != nullptr ? std::atoi(env) : 200;
	const std::string benchAssets = root_ + "bench" DIRECTORY_SEPARATOR_STR;
	std::map<std::string, std::string> files;
	std::vector<std::string> paths;
	for (int i = 0; i < count; i++) {
		char name[64];
		std::snprintf(name, sizeof(name), "monsters\\m%03d\\d%d_f%02d.png", i / 64, (i / 8) % 8, i % 8);
		// About the size of a tile's picture; one in four the same as another, as real art has copies.
		const int content = i % 4 == 3 ? i - 1 : i;
		files[name] = SomeBytes(static_cast<uint32_t>(content), 12000 + static_cast<size_t>(content % 7) * 1000);
		paths.push_back(std::string("hd\\") + name);
	}
	// Asked for in an order that is not the order on disk.
	uint32_t shuffle = 99;
	for (size_t i = paths.size(); i > 1; i--) {
		shuffle = shuffle * 1664525U + 1013904223U;
		std::swap(paths[i - 1], paths[(shuffle >> 8) % i]);
	}

	const auto readAll = [&](const char *what) {
		size_t bytes = 0;
		const uint64_t start = SDL_GetPerformanceCounter();
		for (const std::string &path : paths) {
			const std::string data = Asset(path);
			EXPECT_EQ(data, files[path.substr(3)]) << path;
			bytes += data.size();
		}
		const double ms = static_cast<double>(SDL_GetPerformanceCounter() - start) * 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency());
		if (env != nullptr)
			std::printf("HD_PACK_BENCH %-34s %6d files %9zu bytes %9.1f ms %8.1f us a file\n", what, count, bytes, ms, ms * 1000.0 / count);
		return ms;
	};

	paths::SetAssetsPath(benchAssets);
	for (const auto &[name, data] : files)
		WriteLoose(benchAssets, "hd\\" + name, data);
	if (env != nullptr && std::getenv("HD_PACK_BENCH_DROP") != nullptr) {
		// A command of the measurer's own that empties the system's file cache, so that a first read is a first read.
		EXPECT_EQ(std::system(std::getenv("HD_PACK_BENCH_DROP")), 0);
	}
	readAll("loose files, first read");
	readAll("loose files, read again");

	// The same art as pack files only, in a folder of its own.
	const std::string packAssets = root_ + "benchpack" DIRECTORY_SEPARATOR_STR;
	const std::string packHd = packAssets + "hd" DIRECTORY_SEPARATOR_STR;
	RecursivelyCreateDir(packHd.c_str());
	ASSERT_GT(WritePack(packHd, files, 0x7FFFFFF0U), 0U);
	paths::SetAssetsPath(packAssets);
	if (env != nullptr && std::getenv("HD_PACK_BENCH_DROP") != nullptr) {
		// A command of the measurer's own that empties the system's file cache, so that a first read is a first read.
		EXPECT_EQ(std::system(std::getenv("HD_PACK_BENCH_DROP")), 0);
	}
	readAll("pack files, first read");
	readAll("pack files, read again");
	// What a draw asks before it reads: is there art by this name at all.
	const uint64_t start = SDL_GetPerformanceCounter();
	int found = 0;
	for (const std::string &path : paths)
		found += FindAsset(path).ok() ? 1 : 0;
	EXPECT_EQ(found, count);
	if (env != nullptr)
		std::printf("HD_PACK_BENCH %-34s %6d files %29.1f ms\n", "pack files, is it there", count, static_cast<double>(SDL_GetPerformanceCounter() - start) * 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency()));

	hdpack::Unmount();
	RemoveFile((packHd + "index.hdx").c_str());
	RemoveFile((packHd + PackFileName(0)).c_str());

	// How long an index of as many names as the whole of the art has takes to read and check, and a lookup in it.
	const int names = env != nullptr && std::getenv("HD_PACK_BENCH_NAMES") != nullptr ? std::atoi(std::getenv("HD_PACK_BENCH_NAMES")) : 2000;
	std::map<std::string, std::string> many;
	for (int i = 0; i < names; i++) {
		char name[96];
		std::snprintf(name, sizeof(name), "monsters\\monster%04d\\monster%04da\\d%d_f%02d_m%d.png", i / 512, i / 512, (i / 64) % 8, (i / 4) % 16, i % 4);
		many[name] = SomeBytes(static_cast<uint32_t>(i - i % 3), 10);
	}
	ASSERT_GT(WritePack(packHd, many, 0x7FFFFFF0U), 0U);
	uint64_t from = SDL_GetPerformanceCounter();
	ASSERT_TRUE(hdpack::Mount(packHd));
	const double mountMs = static_cast<double>(SDL_GetPerformanceCounter() - from) * 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency());
	EXPECT_EQ(hdpack::NameCount(), static_cast<uint32_t>(names));
	from = SDL_GetPerformanceCounter();
	int there = 0;
	for (const auto &[name, data] : many)
		there += hdpack::Has("hd\\" + name) ? 1 : 0;
	const double hasMs = static_cast<double>(SDL_GetPerformanceCounter() - from) * 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency());
	EXPECT_EQ(there, names);
	std::uintmax_t indexSize = 0;
	GetFileSize((packHd + "index.hdx").c_str(), &indexSize);
	if (env != nullptr)
		std::printf("HD_PACK_BENCH index of %d names: %.1f MB, read and checked in %.1f ms; %d lookups in %.1f ms\n", names, static_cast<double>(indexSize) / 1048576.0, mountMs, names, hasMs);
	hdpack::Unmount();
	RemoveFile((packHd + "index.hdx").c_str());
	RemoveFile((packHd + PackFileName(0)).c_str());
}

#endif

} // namespace
