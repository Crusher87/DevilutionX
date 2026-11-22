#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include <SDL.h>

#include "hd_pack_writer.hpp"
#include "poe2/hd_pack.hpp"
#include "utils/file_util.h"

using namespace devilution;
using namespace devilution::hdpacktest;

namespace {

#ifdef POE2_HD_PACK

/** @brief The index's name of a file as the game asks for it. */
std::string Asked(const std::string &name)
{
	return "hd\\" + name;
}

std::string FromPack(const std::string &name)
{
	std::vector<uint8_t> bytes;
	if (!hdpack::ReadFile(Asked(name), bytes))
		return "<not read>";
	return std::string(bytes.begin(), bytes.end());
}

/** @brief Reads a file of the pack through its stream, a few bytes at a time as a PNG reader does. */
std::string ThroughStream(const std::string &assetPath, size_t step = 7)
{
	SDL_RWops *stream = hdpack::Open(assetPath);
	if (stream == nullptr)
		return "<not opened>";
	std::string data;
	std::vector<char> buffer(step);
	size_t got;
	while ((got = SDL_RWread(stream, buffer.data(), 1, step)) > 0)
		data.append(buffer.data(), got);
	SDL_RWclose(stream);
	return data;
}

class HdPackTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		const auto *testInfo = ::testing::UnitTest::GetInstance()->current_test_info();
		dir_ = std::string("Test_") + testInfo->test_suite_name() + "_" + testInfo->name() + DIRECTORY_SEPARATOR_STR;
		RecursivelyCreateDir(dir_.c_str());
		RemoveLeftovers();
		damageBefore_ = hdpack::DamageCount();

		// Like a monster's art: many frames, some of them the very same picture, and a recolour that is all copies.
		const std::string shared = SomeBytes(1000, 900);
		for (int direction = 0; direction < 8; direction++) {
			for (int frame = 0; frame < 6; frame++) {
				char name[64];
				std::snprintf(name, sizeof(name), "monsters\\sking\\skinga\\d%d_f%02d.png", direction, frame);
				files_[name] = frame % 3 == 0 ? shared : SomeBytes(direction * 16 + frame, 300 + static_cast<size_t>(direction * 37 + frame * 91));
				std::snprintf(name, sizeof(name), "monsters\\sking\\skinga@trn\\red\\d%d_f%02d.png", direction, frame);
				files_[name] = files_[std::string(name).replace(21, 8, "")];
			}
		}
		for (int tile = 0; tile < 30; tile++)
			files_["tiles\\l1\\" + std::to_string(tile) + ".png"] = SomeBytes(5000 + tile, 2000);
		files_["pack.txt"] = "hd pack\r\n";
		files_["empty.png"] = "";
		for (const auto &[name, data] : files_)
			totalBytes_ += data.size();
	}

	void TearDown() override
	{
		hdpack::Unmount();
		RemoveLeftovers();
	}

	void RemoveLeftovers()
	{
		RemoveFile((dir_ + "index.hdx").c_str());
		for (uint32_t pack = 0; pack < 64; pack++)
			RemoveFile((dir_ + PackFileName(pack)).c_str());
	}

	/** @brief Writes the files as a pack of small pack files and mounts it. */
	void WriteAndMount(uint32_t packLimit = 6000)
	{
		packCount_ = WritePack(dir_, files_, packLimit);
		ASSERT_GT(packCount_, 0U);
		ASSERT_TRUE(hdpack::Mount(dir_));
	}

	void FlipByte(const std::string &file, size_t at)
	{
		std::string data = ReadWhole(dir_ + file);
		ASSERT_LT(at, data.size());
		data[at] = static_cast<char>(data[at] ^ 0x20);
		ASSERT_TRUE(WriteWhole(dir_ + file, data));
	}

	std::string dir_;
	std::map<std::string, std::string> files_;
	size_t totalBytes_ = 0;
	uint32_t packCount_ = 0;
	/** @brief The count of damage found is the whole program's: what the tests before this one found is not this one's. */
	uint32_t damageBefore_ = 0;
};

TEST_F(HdPackTest, EveryNameGivesItsOwnBytes)
{
	WriteAndMount();
	EXPECT_EQ(hdpack::NameCount(), files_.size());
	for (const auto &[name, data] : files_) {
		EXPECT_TRUE(hdpack::Has(Asked(name))) << name;
		EXPECT_EQ(FromPack(name), data) << name;
		EXPECT_EQ(ThroughStream(Asked(name)), data) << name;
	}
}

TEST_F(HdPackTest, ArtIsSplitOverSeveralPackFilesAndTheSameBytesAreStoredOnce)
{
	WriteAndMount(6000);
	EXPECT_GT(packCount_, 5U);
	size_t stored = 0;
	for (uint32_t pack = 0; pack < packCount_; pack++) {
		const size_t size = ReadWhole(dir_ + PackFileName(pack)).size();
		EXPECT_LE(size, 6000U);
		stored += size;
	}
	// The recolour's frames and the standing frames are copies: they take no room of their own.
	EXPECT_LT(stored, totalBytes_ / 2 + 24U * packCount_ + 30U * 2000U);
	EXPECT_EQ(FromPack("monsters\\sking\\skinga\\d0_f00.png"), FromPack("monsters\\sking\\skinga@trn\\red\\d7_f03.png"));
	// A file in the last pack file and one in the first.
	EXPECT_EQ(FromPack("tiles\\l1\\9.png"), files_["tiles\\l1\\9.png"]);
	EXPECT_EQ(FromPack("empty.png"), "");
}

TEST_F(HdPackTest, NamesAreFoundWhateverTheirCaseAndSlashes)
{
	WriteAndMount();
	const std::string &data = files_["monsters\\sking\\skinga\\d3_f04.png"];
	EXPECT_EQ(ThroughStream("hd\\monsters\\sking\\skinga\\d3_f04.png"), data);
	EXPECT_EQ(ThroughStream("hd/monsters/sking/skinga/d3_f04.png"), data);
	EXPECT_EQ(ThroughStream("HD\\Monsters\\SKing\\SKingA\\D3_F04.PNG"), data);
	EXPECT_TRUE(hdpack::Has("hd/Tiles\\l1/12.png"));
	EXPECT_TRUE(hdpack::IsPackPath("hd\\pack.txt"));
	EXPECT_TRUE(hdpack::IsPackPath("HD/pack.txt"));
	EXPECT_FALSE(hdpack::IsPackPath("data\\inv\\objcurs.clx"));
	EXPECT_FALSE(hdpack::IsPackPath("hdx\\pack.txt"));
}

TEST_F(HdPackTest, MissingNamesAreMissing)
{
	WriteAndMount();
	for (const char *name : { "hd\\tiles\\l1\\30.png", "hd\\tiles\\l1\\1.pn", "hd\\tiles\\l1\\1.png2", "hd\\tiles\\l1\\", "hd\\tiles", "hd\\", "hd", "",
	         "tiles\\l1\\1.png", "levels\\l1data\\l1.pal", "hd\\hd\\pack.txt", "hd\\zzz.png", "hd\\!.png", "xd\\pack.txt" }) {
		EXPECT_FALSE(hdpack::Has(name)) << name;
		EXPECT_EQ(hdpack::Open(name), nullptr) << name;
		std::vector<uint8_t> bytes;
		EXPECT_FALSE(hdpack::ReadFile(name, bytes)) << name;
	}
	EXPECT_FALSE(hdpack::Has(std::string_view("hd\\pack.txt\0x", 13)));
	EXPECT_FALSE(hdpack::Has("hd\\" + std::string(5000, 'a') + ".png"));
	EXPECT_EQ(hdpack::DamageCount(), damageBefore_) << "a name the pack doesn't have is no damage";
}

TEST_F(HdPackTest, NothingIsFoundWithNoPackMounted)
{
	EXPECT_FALSE(hdpack::Mount(dir_)) << "there is no index in the folder yet";
	EXPECT_FALSE(hdpack::Has("hd\\pack.txt"));
	EXPECT_EQ(hdpack::Open("hd\\pack.txt"), nullptr);
	EXPECT_EQ(hdpack::NameCount(), 0U);
	WriteAndMount();
	EXPECT_TRUE(hdpack::Has("hd\\pack.txt"));
	hdpack::Unmount();
	EXPECT_FALSE(hdpack::Has("hd\\pack.txt"));
}

TEST_F(HdPackTest, AStreamTellsItsSizeAndSeeks)
{
	WriteAndMount();
	const std::string &data = files_["tiles\\l1\\7.png"];
	SDL_RWops *stream = hdpack::Open("hd\\tiles\\l1\\7.png");
	ASSERT_NE(stream, nullptr);
	EXPECT_EQ(SDL_RWsize(stream), static_cast<Sint64>(data.size()));
	char buffer[64];
	EXPECT_EQ(SDL_RWseek(stream, 100, RW_SEEK_SET), 100);
	EXPECT_EQ(SDL_RWread(stream, buffer, 1, 10), 10U);
	EXPECT_EQ(std::string(buffer, 10), data.substr(100, 10));
	EXPECT_EQ(SDL_RWtell(stream), 110);
	EXPECT_EQ(SDL_RWseek(stream, -10, RW_SEEK_CUR), 100);
	EXPECT_EQ(SDL_RWseek(stream, -5, RW_SEEK_END), static_cast<Sint64>(data.size()) - 5);
	// Whole items only, and nothing past the end.
	EXPECT_EQ(SDL_RWread(stream, buffer, 2, 10), 2U);
	EXPECT_EQ(std::string(buffer, 4), data.substr(data.size() - 5, 4));
	EXPECT_EQ(SDL_RWread(stream, buffer, 1, 10), 1U);
	EXPECT_EQ(SDL_RWread(stream, buffer, 1, 10), 0U);
	EXPECT_LT(SDL_RWseek(stream, 1, RW_SEEK_END), 0);
	EXPECT_LT(SDL_RWseek(stream, -1, RW_SEEK_SET), 0);
	EXPECT_EQ(SDL_RWseek(stream, 0, RW_SEEK_SET), 0);
	EXPECT_EQ(SDL_RWread(stream, buffer, 1, 8), 8U);
	EXPECT_EQ(std::string(buffer, 8), data.substr(0, 8));
	EXPECT_EQ(SDL_RWclose(stream), 0);

	stream = hdpack::Open("hd\\empty.png");
	ASSERT_NE(stream, nullptr);
	EXPECT_EQ(SDL_RWsize(stream), 0);
	EXPECT_EQ(SDL_RWread(stream, buffer, 1, 8), 0U);
	SDL_RWclose(stream);
	EXPECT_EQ(hdpack::DamageCount(), damageBefore_);
}

TEST_F(HdPackTest, AStreamOutlivesItsPackBeingUnmounted)
{
	WriteAndMount();
	SDL_RWops *stream = hdpack::Open("hd\\tiles\\l1\\7.png");
	ASSERT_NE(stream, nullptr);
	hdpack::Unmount();
	char buffer[16];
	EXPECT_EQ(SDL_RWread(stream, buffer, 1, 16), 16U);
	EXPECT_EQ(std::string(buffer, 16), files_["tiles\\l1\\7.png"].substr(0, 16));
	SDL_RWclose(stream);
}

TEST_F(HdPackTest, ADamagedPictureIsFoundAndTheRestStillRead)
{
	WriteAndMount(1U << 20);
	ASSERT_EQ(packCount_, 1U);
	hdpack::Unmount();
	// One byte wrong in the middle of the pack file, as a bad copy leaves it.
	const std::string pack = ReadWhole(dir_ + "pack_000.hdp");
	const size_t at = pack.find(files_["tiles\\l1\\11.png"]);
	ASSERT_NE(at, std::string::npos);
	FlipByte("pack_000.hdp", at + 1000);
	ASSERT_TRUE(hdpack::Mount(dir_));
	const uint32_t before = hdpack::DamageCount();

	EXPECT_TRUE(hdpack::Has("hd\\tiles\\l1\\11.png"));
	std::vector<uint8_t> bytes;
	EXPECT_FALSE(hdpack::ReadFile("hd\\tiles\\l1\\11.png", bytes));
	SDL_RWops *stream = hdpack::Open("hd\\tiles\\l1\\11.png");
	ASSERT_NE(stream, nullptr);
	char buffer[16];
	EXPECT_EQ(SDL_RWread(stream, buffer, 1, 16), 0U) << "not one byte of a damaged picture is handed out";
	EXPECT_EQ(SDL_RWread(stream, buffer, 1, 16), 0U);
	SDL_RWclose(stream);
	EXPECT_GE(hdpack::DamageCount(), before + 2);

	int good = 0;
	for (const auto &[name, data] : files_) {
		if (name == "tiles\\l1\\11.png")
			continue;
		EXPECT_EQ(FromPack(name), data) << name;
		good++;
	}
	EXPECT_EQ(good, static_cast<int>(files_.size()) - 1);
}

TEST_F(HdPackTest, ADamagedIndexIsRefusedWhole)
{
	WriteAndMount();
	hdpack::Unmount();
	const std::string good = ReadWhole(dir_ + "index.hdx");
	const auto refused = [&](const std::string &index) {
		if (!WriteWhole(dir_ + "index.hdx", index))
			return false;
		const bool mounted = hdpack::Mount(dir_);
		return !mounted && !hdpack::Has("hd\\pack.txt") && hdpack::Open("hd\\pack.txt") == nullptr;
	};
	// A wrong byte anywhere: in the header, the tables, the names.
	for (const size_t at : { size_t { 0 }, size_t { 9 }, size_t { 13 }, size_t { 17 }, size_t { 22 }, size_t { 25 }, size_t { 30 }, size_t { 33 }, size_t { 38 }, size_t { 41 }, size_t { 200 }, good.size() / 2, good.size() - 1 }) {
		std::string bad = good;
		bad[at] = static_cast<char>(bad[at] ^ 0x01);
		EXPECT_TRUE(refused(bad)) << "a wrong byte at " << at;
	}
	// Cut short, grown, emptied.
	EXPECT_TRUE(refused(good.substr(0, good.size() - 1)));
	EXPECT_TRUE(refused(good.substr(0, 40)));
	EXPECT_TRUE(refused(good.substr(0, 17)));
	EXPECT_TRUE(refused(good + "x"));
	EXPECT_TRUE(refused(""));
	// Sound, but of a version this game does not know.
	ASSERT_GT(WritePack(dir_, files_, 6000, 0x5EED1234U, 2), 0U);
	EXPECT_FALSE(hdpack::Mount(dir_));
	EXPECT_FALSE(hdpack::Has("hd\\pack.txt"));
	// And as it was, it is read again.
	ASSERT_TRUE(WriteWhole(dir_ + "index.hdx", good));
	WriteAndMount();
	EXPECT_EQ(FromPack("pack.txt"), "hd pack\r\n");
}

/** @brief An index whose checksums are right but whose numbers lead outside its tables or files must be refused too. */
TEST_F(HdPackTest, AnIndexThatLeadsOutsideItsFilesIsRefused)
{
	WriteAndMount();
	hdpack::Unmount();
	const std::string good = ReadWhole(dir_ + "index.hdx");
	const auto get32 = [&](size_t at) {
		return static_cast<uint32_t>(static_cast<uint8_t>(good[at])) | (static_cast<uint32_t>(static_cast<uint8_t>(good[at + 1])) << 8) | (static_cast<uint32_t>(static_cast<uint8_t>(good[at + 2])) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(good[at + 3])) << 24);
	};
	const uint32_t packs = get32(16);
	const uint32_t blobs = get32(20);
	const uint32_t names = get32(24);
	const size_t blobTable = 40 + static_cast<size_t>(packs) * 4;
	const size_t nameTable = blobTable + static_cast<size_t>(blobs) * 16;
	const size_t nameBytes = nameTable + static_cast<size_t>(names) * 8;
	const auto refusedWith = [&](size_t at, uint32_t value) {
		std::string body = good.substr(40);
		std::string number;
		Put32(number, value);
		body.replace(at - 40, 4, number);
		std::string index = good.substr(0, 32);
		Put32(index, Crc32(body));
		Put32(index, Crc32(index));
		index += body;
		if (!WriteWhole(dir_ + "index.hdx", index))
			return false;
		return !hdpack::Mount(dir_) && !hdpack::Has("hd\\pack.txt");
	};
	EXPECT_TRUE(refusedWith(blobTable, packs)) << "a blob in a pack file there is none of";
	EXPECT_TRUE(refusedWith(blobTable + 4, 0x7FFFFFF0U)) << "a blob past the end of its pack file";
	EXPECT_TRUE(refusedWith(blobTable + 4, 0xFFFFFFF0U)) << "a blob whose end wraps round";
	EXPECT_TRUE(refusedWith(blobTable + 8, 0x7FFFFFF0U)) << "a blob longer than its pack file";
	EXPECT_TRUE(refusedWith(blobTable + 4, 3)) << "a blob inside the pack file's header";
	EXPECT_TRUE(refusedWith(nameTable + 4, blobs)) << "a name of a blob there is none of";
	EXPECT_TRUE(refusedWith(nameTable, 0x00FFFFFFU)) << "a name outside the names";
	EXPECT_TRUE(refusedWith(nameTable + 8, get32(nameTable + 16))) << "names out of order";
	EXPECT_TRUE(refusedWith(nameTable + 8, get32(nameTable))) << "the same name twice";
	EXPECT_TRUE(refusedWith(nameBytes + (good.size() - nameBytes) - 4, 0x61616161U)) << "a last name with no end";
	ASSERT_TRUE(WriteWhole(dir_ + "index.hdx", good));
	EXPECT_TRUE(hdpack::Mount(dir_));
}

TEST_F(HdPackTest, APackFileCutShortOrMissingLosesOnlyItsOwnPictures)
{
	WriteAndMount(6000);
	ASSERT_GT(packCount_, 3U);
	hdpack::Unmount();
	const std::string second = ReadWhole(dir_ + PackFileName(1));
	const std::string third = ReadWhole(dir_ + PackFileName(2));
	ASSERT_TRUE(WriteWhole(dir_ + PackFileName(1), second.substr(0, second.size() - 1)));
	RemoveFile((dir_ + PackFileName(2)).c_str());
	const uint32_t before = hdpack::DamageCount();
	ASSERT_TRUE(hdpack::Mount(dir_));
	EXPECT_EQ(hdpack::DamageCount(), before + 2) << "each bad pack file is told of once";
	int found = 0;
	int lost = 0;
	for (const auto &[name, data] : files_) {
		const bool inBadPack = (!data.empty() && (second.find(data) != std::string::npos || third.find(data) != std::string::npos));
		if (hdpack::Has(Asked(name))) {
			EXPECT_EQ(FromPack(name), data) << name;
			EXPECT_FALSE(inBadPack) << name;
			found++;
		} else {
			EXPECT_EQ(hdpack::Open(Asked(name)), nullptr) << name;
			lost++;
		}
	}
	EXPECT_GT(found, 0);
	EXPECT_GT(lost, 0);
	EXPECT_EQ(hdpack::DamageCount(), before + 2);
}

TEST_F(HdPackTest, APackFileOfAnotherIndexIsNotRead)
{
	WriteAndMount(6000);
	hdpack::Unmount();
	const std::string index = ReadWhole(dir_ + "index.hdx");
	const std::string first = ReadWhole(dir_ + PackFileName(0));
	// The same art packed another time: the same sizes and places, another set.
	ASSERT_GT(WritePack(dir_, files_, 6000, 0x0BADF00DU), 0U);
	ASSERT_TRUE(WriteWhole(dir_ + "index.hdx", index));
	ASSERT_TRUE(WriteWhole(dir_ + PackFileName(0), first));
	ASSERT_TRUE(hdpack::Mount(dir_));
	int found = 0;
	for (const auto &[name, data] : files_) {
		if (!hdpack::Has(Asked(name)))
			continue;
		EXPECT_NE(first.find(data), std::string::npos) << name << " was read from a pack file of another index";
		found++;
	}
	EXPECT_GT(found, 0);
	EXPECT_LT(found, static_cast<int>(files_.size()));
	// Pack files renamed into each other's places are not read either.
	ASSERT_GT(WritePack(dir_, files_, 6000), 1U);
	const std::string a = ReadWhole(dir_ + PackFileName(0));
	const std::string b = ReadWhole(dir_ + PackFileName(1));
	ASSERT_TRUE(WriteWhole(dir_ + PackFileName(0), b + std::string(6000, '\0')));
	ASSERT_TRUE(WriteWhole(dir_ + PackFileName(1), a + std::string(6000, '\0')));
	ASSERT_TRUE(hdpack::Mount(dir_));
	for (const auto &[name, data] : files_) {
		if (hdpack::Has(Asked(name))) {
			EXPECT_EQ(FromPack(name), data) << name;
		}
	}
}

struct ReadingThread {
	const std::map<std::string, std::string> *files;
	int seat;
	int wrong = 0;
	int read = 0;
};

int SDLCALL ReadEverything(void *data)
{
	auto *mine = static_cast<ReadingThread *>(data);
	for (int round = 0; round < 40; round++) {
		int number = 0;
		for (const auto &[name, bytes] : *mine->files) {
			// Each thread goes its own way through the files, so that they cross in the pack files.
			if ((number++ + round) % 4 != mine->seat % 4 && round % 2 == 0)
				continue;
			const std::string got = (round + mine->seat) % 2 == 0 ? ThroughStream(Asked(name), 512) : FromPack(name);
			if (got != bytes)
				mine->wrong++;
			mine->read++;
		}
	}
	return 0;
}

TEST_F(HdPackTest, SeveralThreadsReadAtOnce)
{
	WriteAndMount(6000);
	constexpr int Threads = 6;
	ReadingThread seats[Threads];
	SDL_Thread *threads[Threads];
	for (int i = 0; i < Threads; i++) {
		seats[i].files = &files_;
		seats[i].seat = i;
		threads[i] = SDL_CreateThread(ReadEverything, "hd pack test", &seats[i]);
		ASSERT_NE(threads[i], nullptr);
	}
	for (int i = 0; i < Threads; i++) {
		SDL_WaitThread(threads[i], nullptr);
		EXPECT_EQ(seats[i].wrong, 0) << "thread " << i;
		EXPECT_GT(seats[i].read, 1000) << "thread " << i;
	}
	EXPECT_EQ(hdpack::DamageCount(), damageBefore_);
}

struct RemountingThread {
	std::string dir;
	SDL_atomic_t stop;
};

int SDLCALL RemountOverAndOver(void *data)
{
	auto *mine = static_cast<RemountingThread *>(data);
	while (SDL_AtomicGet(&mine->stop) == 0) {
		hdpack::Mount(mine->dir);
		hdpack::Unmount();
		hdpack::Mount(mine->dir);
	}
	return 0;
}

/** @brief The pack is mounted anew (a mod folder was added, say) while the reader's threads are in it. */
TEST_F(HdPackTest, ReadingWhileThePackIsMountedAnew)
{
	WriteAndMount(6000);
	RemountingThread remounting;
	remounting.dir = dir_;
	SDL_AtomicSet(&remounting.stop, 0);
	SDL_Thread *thread = SDL_CreateThread(RemountOverAndOver, "hd pack remount", &remounting);
	ASSERT_NE(thread, nullptr);
	int read = 0;
	for (int round = 0; round < 20; round++) {
		for (const auto &[name, data] : files_) {
			// Between an unmount and the next mount there is no pack: then nothing is found, never something wrong.
			SDL_RWops *stream = hdpack::Open(Asked(name));
			if (stream == nullptr)
				continue;
			std::string got(data.size() + 1, '\0');
			got.resize(SDL_RWread(stream, got.data(), 1, got.size()));
			SDL_RWclose(stream);
			EXPECT_EQ(got, data) << name;
			read++;
		}
	}
	SDL_AtomicSet(&remounting.stop, 1);
	SDL_WaitThread(thread, nullptr);
	EXPECT_GT(read, 0);
	EXPECT_EQ(hdpack::DamageCount(), damageBefore_);
}

#ifdef HD_PACK_PYTHON
std::string Quoted(const std::string &text)
{
	return "\"" + text + "\"";
}

/** @brief The pack tools/hd_pack.py makes of a folder of loose files is read here to the same bytes. */
TEST_F(HdPackTest, APackMadeByThePackerIsRead)
{
	const std::string loose = dir_ + "loose" DIRECTORY_SEPARATOR_STR;
	const std::string packed = dir_ + "packed" DIRECTORY_SEPARATOR_STR;
	std::vector<std::string> written;
	for (const auto &[name, data] : files_) {
		std::string path = loose + name;
#ifndef _WIN32
		for (char &c : path) {
			if (c == '\\')
				c = '/';
		}
#endif
		RecursivelyCreateDir(path.substr(0, path.find_last_of("\\/")).c_str());
		ASSERT_TRUE(WriteWhole(path, data)) << path;
		written.push_back(path);
	}
	RecursivelyCreateDir(packed.c_str());
	std::string command = Quoted(HD_PACK_PYTHON) + " " + Quoted(HD_PACK_TOOL) + " pack " + Quoted(loose.substr(0, loose.size() - 1)) + " " + Quoted(packed.substr(0, packed.size() - 1)) + " --fresh --pack-size-mb 0.01";
#ifdef _WIN32
	// cmd.exe takes the outer quotes off a line that begins with one.
	command = "\"" + command + "\" > NUL";
#else
	command += " > /dev/null";
#endif
	ASSERT_EQ(std::system(command.c_str()), 0) << command;

	ASSERT_TRUE(hdpack::Mount(packed));
	EXPECT_EQ(hdpack::NameCount(), files_.size());
	for (const auto &[name, data] : files_) {
		EXPECT_EQ(FromPack(name), data) << name;
		EXPECT_EQ(ThroughStream(Asked(name)), data) << name;
	}
	EXPECT_EQ(hdpack::DamageCount(), damageBefore_);
	hdpack::Unmount();

	uint32_t packs = 0;
	while (FileExists((packed + PackFileName(packs)).c_str())) {
		std::uintmax_t size = 0;
		ASSERT_TRUE(GetFileSize((packed + PackFileName(packs)).c_str(), &size));
		EXPECT_LE(size, 10486U);
		RemoveFile((packed + PackFileName(packs)).c_str());
		packs++;
	}
	EXPECT_GT(packs, 3U);
	RemoveFile((packed + "index.hdx").c_str());
	RemoveFile((packed + "hd_pack.journal").c_str());
	for (const std::string &path : written)
		RemoveFile(path.c_str());
}
#endif

#else

TEST(HdPackTest, NothingIsReadWhereThePackReaderIsLeftOut)
{
	EXPECT_FALSE(hdpack::IsPackPath("hd\\pack.txt"));
	EXPECT_FALSE(hdpack::Mount("."));
	EXPECT_FALSE(hdpack::Has("hd\\pack.txt"));
	EXPECT_EQ(hdpack::Open("hd\\pack.txt"), nullptr);
}

#endif

} // namespace
