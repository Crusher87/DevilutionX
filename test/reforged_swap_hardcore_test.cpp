/**
 * @file reforged_swap_hardcore_test.cpp
 *
 * Sanctuary Reforged's two features taken from POE2 Mashup: plain Diablo weapon swap on X (two sets of hands, Diablo's
 * own equip rules) and Hardcore as a choice for new single player heroes. Both are saved in Reforged's own small hero
 * entry ("srvhero"); POE2 Mashup and plain Diablo and Hellfire stay as they were.
 */
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "DiabloUI/diabloui.h"
#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "init.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "loadsave.h"
#include "menu.h"
#include "pfile.h"
#include "player.h"
#include "options.h"
#include "poe2/cathedral_floors.hpp"
#include "poe2/flasks.hpp"
#include "poe2/floors.hpp"
#include "poe2/hardcore.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/weapon_swap.hpp"
#include "spells.h"
#include "tables/itemdat.h"
#include "tables/monstdat.h"
#include "tables/playerdat.hpp"
#include "utils/file_util.h"
#include "utils/paths.h"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

constexpr uint32_t SaveSlot = 0;

bool ListedHardcore;

class ReforgedSwapHardcoreTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(2);
		MyPlayerId = 0;
		MyPlayer = &Players[0];
		Players[0] = {};
		Players[1] = {};
		gbVanilla = false;
		gbIsSpawn = false;
		gbIsMultiplayer = false;
		gbIsHellfire = true;
		gbIsHellfireSaveGame = true;
		gbIsPoe2 = false;
		gbIsReforged = true;
		LoadMods({ "hf", "reforged" });
		// Saves of each test go in a folder of their own, so they never meet another test's when tests run in parallel.
		prefPath = StrCat(paths::BasePath(), "reforged_swap_saves_", ::testing::UnitTest::GetInstance()->current_test_info()->name(), DIRECTORY_SEPARATOR_STR);
		RecursivelyCreateDir(prefPath.c_str());
		paths::SetPrefPath(prefPath);
	}

	void TearDown() override
	{
		// pfile_write_hero keeps the save it replaces as a backup.
		gSaveNumber = SaveSlot;
		RemoveFile(StrCat(SavePath(), ".bak").c_str());
		gbIsPoe2 = false;
		gbIsReforged = false;
		gbIsHellfire = false;
		gbIsHellfireSaveGame = false;
		gbIsMultiplayer = false;
		UnloadModArchives();
	}

	static void LoadMods(std::initializer_list<std::string_view> mods)
	{
		UnloadModArchives();
		LoadModArchives(mods);
		LoadItemData();
		LoadSpellData();
		LoadPlayerDataFiles();
		LoadMonsterData();
		LoadPassiveTreeData();
	}

	/** @brief A base item of the item table that matches; an empty item if none does. */
	static Item Base(const std::function<bool(const ItemData &)> &matches)
	{
		Item item;
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].dropRate > 0 && matches(AllItemsList[i])) {
				InitializeItem(item, static_cast<_item_indexes>(i));
				return item;
			}
		}
		return item;
	}

	static Item ShortSword()
	{
		Item item;
		InitializeItem(item, IDI_WARRIOR);
		return item;
	}

	static Item OneHanded(ItemType type)
	{
		return Base([type](const ItemData &data) { return data.itype == type && data.iLoc == ILOC_ONEHAND; });
	}

	static Item TwoHandedSword()
	{
		return Base([](const ItemData &data) { return data.itype == ItemType::Sword && data.iLoc == ILOC_TWOHAND; });
	}

	static Item Ring()
	{
		return Base([](const ItemData &data) { return data.iLoc == ILOC_RING; });
	}

	/** @brief A hero who isn't the local one (so nothing goes over the network), strong enough for any base. */
	static Player &Hero(HeroClass heroClass)
	{
		Player &player = Players[1];
		player = {};
		player._pClass = heroClass;
		player._pmode = PM_STAND;
		player._pHitPoints = player._pMaxHP = Fixed26_6::fromInt(100);
		player._pStrength = player._pBaseStr = 250;
		player._pMagic = player._pBaseMag = 250;
		player._pDexterity = player._pBaseDex = 250;
		player._pVitality = player._pBaseVit = 250;
		return player;
	}

	static bool HasEntry(const char *name)
	{
		std::optional<SaveReader> archive = OpenSaveArchive(SaveSlot);
		if (!archive)
			return false;
		size_t length = 0;
		return ReadArchive(*archive, name, &length) != nullptr;
	}

	/** @brief Makes a hero in the save slot and reads it back as the local hero; false when the game's data isn't here. */
	bool MakeHero(bool hardcore, std::initializer_list<std::string_view> mods = { "hf", "reforged" })
	{
		LoadCoreArchives();
		LoadGameArchives();
		if (!HaveMainData())
			return false;
		LoadMods(mods);
		gSaveNumber = SaveSlot;
		RemoveFile(SavePath().c_str());
		_uiheroinfo info {};
		info.heroclass = HeroClass::Warrior;
		info.saveNumber = SaveSlot;
		info.hardcore = hardcore;
		CopyUtf8(info.name, "Swapper", sizeof(info.name));
		EXPECT_TRUE(pfile_ui_save_create(&info));
		gSaveNumber = SaveSlot;
		pfile_read_player_from_save(SaveSlot, *MyPlayer);
		LoadPoe2HeroData(*MyPlayer);
		return true;
	}

	static std::string SavePath()
	{
		return pfile_hero_save_path();
	}

	/** @brief The entries an older Reforged build wrote beside its own. */
	static constexpr const char *OldEntries[] = { "poe2hero", "poe2mode", "poe2floors", "poe2shrines" };

	/** @brief The local hero has been on a floor built from the designed rooms, which is saved and kept. */
	static void VisitDesignedFloor(int level)
	{
		Poe2FloorsReset();
		GetOptions().Gameplay.newCathedralFloors.SetValue(true);
		MyPlayer->_pLvlVisited[level] = false;
		currlevel = static_cast<uint8_t>(level);
		leveltype = DTYPE_CATHEDRAL;
		Poe2ApplyFreshFloor(level, false, 4321, 1);
		EXPECT_TRUE(Poe2FloorBuildFor(level).cathedralFunnel);
		Poe2FloorSaved(level, false);
		MyPlayer->_pLvlVisited[level] = true;
		for (uint32_t tick = 0; tick < 100; tick++)
			Poe2FloorsTick();
	}

	static bool HeroListShowsHardcore()
	{
		ListedHardcore = false;
		pfile_ui_set_hero_infos([](_uiheroinfo *hero) {
			if (std::string_view(hero->name) == "Swapper")
				ListedHardcore = hero->hardcore;
			return true;
		});
		return ListedHardcore;
	}

	std::string prefPath;
};

TEST_F(ReforgedSwapHardcoreTest, XSwapsTheTwoSetsOfHands)
{
	Player &player = Hero(HeroClass::Warrior);
	const Item shield = OneHanded(ItemType::Shield);
	ASSERT_FALSE(shield.isEmpty());
	player.InvBody[INVLOC_HAND_LEFT] = ShortSword();
	player.InvBody[INVLOC_HAND_RIGHT] = shield;

	ASSERT_TRUE(HasWeaponSwap());
	ASSERT_TRUE(SwapWeapons(player)) << "to the empty second set";
	EXPECT_EQ(player.poe2.weaponSet, 1);
	EXPECT_TRUE(player.InvBody[INVLOC_HAND_LEFT].isEmpty());
	EXPECT_TRUE(player.InvBody[INVLOC_HAND_RIGHT].isEmpty());
	EXPECT_EQ(InactiveHand(player, INVLOC_HAND_LEFT).IDidx, IDI_WARRIOR);
	EXPECT_EQ(InactiveHand(player, INVLOC_HAND_RIGHT).IDidx, shield.IDidx);

	// A two-handed sword in set II, then back to the sword and shield.
	player.InvBody[INVLOC_HAND_LEFT] = TwoHandedSword();
	ASSERT_FALSE(player.InvBody[INVLOC_HAND_LEFT].isEmpty());
	ASSERT_TRUE(SwapWeapons(player));
	EXPECT_EQ(player.poe2.weaponSet, 0);
	EXPECT_EQ(player.InvBody[INVLOC_HAND_LEFT].IDidx, IDI_WARRIOR);
	EXPECT_EQ(player.InvBody[INVLOC_HAND_RIGHT].IDidx, shield.IDidx);
	EXPECT_EQ(InactiveHand(player, INVLOC_HAND_LEFT)._iLoc, ILOC_TWOHAND);
	EXPECT_TRUE(InactiveHand(player, INVLOC_HAND_RIGHT).isEmpty());
}

TEST_F(ReforgedSwapHardcoreTest, NoSwapWhileBusyOrDead)
{
	Player &player = Hero(HeroClass::Warrior);
	player.swapHands[0] = ShortSword();
	player._pmode = PM_ATTACK;
	EXPECT_FALSE(SwapWeapons(player)) << "not in the middle of a swing";
	player._pmode = PM_STAND;
	// A hero only counts as dead out of town.
	const dungeon_type levelType = leveltype;
	leveltype = DTYPE_CATHEDRAL;
	player._pHitPoints = Fixed26_6::fromInt(0);
	EXPECT_FALSE(SwapWeapons(player));
	leveltype = levelType;
	EXPECT_EQ(player.poe2.weaponSet, 0);
	EXPECT_EQ(player.swapHands[0].IDidx, IDI_WARRIOR);
}

TEST_F(ReforgedSwapHardcoreTest, SwappingFollowsDiablosEquipRules)
{
	const Item shield = OneHanded(ItemType::Shield);
	const Item twoHander = TwoHandedSword();
	const Item ring = Ring();
	ASSERT_FALSE(shield.isEmpty());
	ASSERT_FALSE(twoHander.isEmpty());
	ASSERT_FALSE(ring.isEmpty());

	const auto trySet = [](Player &player, const Item &left, const Item &right) {
		player.InvBody[INVLOC_HAND_LEFT].clear();
		player.InvBody[INVLOC_HAND_RIGHT].clear();
		player.poe2.weaponSet = 0;
		player.swapHands[0] = left;
		player.swapHands[1] = right;
		const bool swapped = SwapWeapons(player);
		if (!swapped) {
			// A refused swap leaves both sets where they were.
			EXPECT_TRUE(player.InvBody[INVLOC_HAND_LEFT].isEmpty());
			EXPECT_EQ(player.swapHands[0].IDidx, left.IDidx);
			EXPECT_EQ(player.poe2.weaponSet, 0);
		}
		return swapped;
	};

	Player &warrior = Hero(HeroClass::Warrior);
	EXPECT_TRUE(trySet(warrior, ShortSword(), shield)) << "a weapon and a shield";
	EXPECT_TRUE(trySet(warrior, twoHander, {})) << "a two-handed sword alone";
	EXPECT_FALSE(trySet(warrior, twoHander, shield)) << "a two-handed sword leaves no hand for a shield";
	EXPECT_FALSE(trySet(warrior, ShortSword(), ShortSword())) << "only a Bard holds two swords";
	EXPECT_FALSE(trySet(warrior, shield, shield)) << "two shields";
	EXPECT_FALSE(trySet(warrior, ring, {})) << "a ring isn't held in the hands";

	Player &bard = Hero(HeroClass::Bard);
	EXPECT_TRUE(trySet(bard, ShortSword(), ShortSword())) << "the Bard dual wields swords";

	Player &barbarian = Hero(HeroClass::Barbarian);
	EXPECT_TRUE(trySet(barbarian, twoHander, shield)) << "the Barbarian holds a two-handed sword in one hand";

	Player &weakling = Hero(HeroClass::Warrior);
	weakling._pStrength = weakling._pBaseStr = 0;
	weakling._pDexterity = weakling._pBaseDex = 0;
	ASSERT_GT(twoHander._iMinStr, 0);
	EXPECT_FALSE(trySet(weakling, twoHander, {})) << "a weapon the hero isn't strong enough for";
}

TEST_F(ReforgedSwapHardcoreTest, PlainDiabloAndHellfireHaveNoSwapOrHardcore)
{
	gbIsReforged = false;
	EXPECT_FALSE(HasWeaponSwap());
	EXPECT_FALSE(HasHardcoreChoice());
	Player &player = Hero(HeroClass::Warrior);
	player.swapHands[0] = ShortSword();
	EXPECT_FALSE(SwapWeapons(player));
	EXPECT_TRUE(player.InvBody[INVLOC_HAND_LEFT].isEmpty());
	player.poe2.hardcore = true;
	EXPECT_FALSE(IsHardcoreHero(player));
}

TEST_F(ReforgedSwapHardcoreTest, Poe2KeepsItsOwnSwapAndHardcore)
{
	gbIsReforged = false;
	gbIsPoe2 = true;
	LoadMods({ "hf", "poe2" });
	EXPECT_TRUE(HasWeaponSwap());
	EXPECT_TRUE(HasHardcoreChoice());
	Player &player = Hero(HeroClass::Warrior);
	// POE2's own hand rules (a wand and a sceptre, two weapons) are not Reforged's: the swap doesn't check Diablo's.
	player.swapHands[0] = ShortSword();
	player.swapHands[1] = ShortSword();
	EXPECT_TRUE(SwapWeapons(player));
	EXPECT_EQ(player.poe2.weaponSet, 1);
}

TEST_F(ReforgedSwapHardcoreTest, HardcoreIsForSinglePlayerHardcoreHeroesOnly)
{
	EXPECT_TRUE(HasHardcoreChoice());
	Player &player = Hero(HeroClass::Rogue);
	EXPECT_FALSE(IsHardcoreHero(player)) << "a Normal hero";
	player.poe2.hardcore = true;
	EXPECT_TRUE(IsHardcoreHero(player));
	gbIsMultiplayer = true;
	EXPECT_FALSE(IsHardcoreHero(player)) << "multiplayer heroes are never hardcore";
}

TEST_F(ReforgedSwapHardcoreTest, TheSecondSetAndHardcoreSurviveASave)
{
	if (!MakeHero(/*hardcore=*/true))
		GTEST_SKIP() << "MPQ assets (spawn.mpq or DIABDAT.MPQ) not found - skipping test";
	Player &player = *MyPlayer;
	EXPECT_TRUE(player.poe2.hardcore);
	EXPECT_TRUE(IsHardcoreHero(player));
	EXPECT_TRUE(HasEntry("srvhero"));
	EXPECT_TRUE(HeroListShowsHardcore()) << "the hero list marks the hero";

	const Item shield = OneHanded(ItemType::Shield);
	player.swapHands[0] = TwoHandedSword();
	player.swapHands[1] = shield;
	player.poe2.weaponSet = 1;
	pfile_write_hero(/*writeGameData=*/false);

	player.poe2.clear();
	player.swapHands[0].clear();
	player.swapHands[1].clear();
	LoadPoe2HeroData(player);
	EXPECT_TRUE(player.poe2.hardcore);
	EXPECT_EQ(player.poe2.weaponSet, 1);
	EXPECT_EQ(player.swapHands[0].IDidx, TwoHandedSword().IDidx);
	EXPECT_EQ(player.swapHands[0]._iLoc, ILOC_TWOHAND);
	EXPECT_EQ(player.swapHands[1].IDidx, shield.IDidx);

	// A Normal hero.
	player.poe2.hardcore = false;
	pfile_write_hero(/*writeGameData=*/false);
	EXPECT_FALSE(HeroListShowsHardcore());
	player.poe2.hardcore = true;
	LoadPoe2HeroData(player);
	EXPECT_FALSE(player.poe2.hardcore);
	EXPECT_EQ(player.swapHands[1].IDidx, shield.IDidx);
	RemoveFile(SavePath().c_str());
}

TEST_F(ReforgedSwapHardcoreTest, ANormalHeroIsNotHardcore)
{
	if (!MakeHero(/*hardcore=*/false))
		GTEST_SKIP() << "MPQ assets (spawn.mpq or DIABDAT.MPQ) not found - skipping test";
	EXPECT_FALSE(MyPlayer->poe2.hardcore);
	EXPECT_FALSE(IsHardcoreHero(*MyPlayer));
	EXPECT_FALSE(HeroListShowsHardcore());
	EXPECT_TRUE(MyPlayer->swapHands[0].isEmpty());
	EXPECT_TRUE(MyPlayer->swapHands[1].isEmpty());
	RemoveFile(SavePath().c_str());
}

TEST_F(ReforgedSwapHardcoreTest, AnOlderReforgedHeroWithoutTheEntryLoads)
{
	// The hero is written without Reforged's entry, as a Reforged build from before it did.
	gbIsReforged = false;
	if (!MakeHero(/*hardcore=*/false))
		GTEST_SKIP() << "MPQ assets (spawn.mpq or DIABDAT.MPQ) not found - skipping test";
	ASSERT_FALSE(HasEntry("srvhero"));
	gbIsReforged = true;
	Player &player = *MyPlayer;
	player.swapHands[0] = ShortSword();
	player.poe2.weaponSet = 1;
	player.poe2.hardcore = true;
	LoadPoe2HeroData(player);
	EXPECT_EQ(std::string_view(player._pName), "Swapper");
	EXPECT_EQ(player.getCharacterLevel(), 1);
	EXPECT_TRUE(player.swapHands[0].isEmpty()) << "an empty second set";
	EXPECT_TRUE(player.swapHands[1].isEmpty());
	EXPECT_EQ(player.poe2.weaponSet, 0);
	EXPECT_FALSE(player.poe2.hardcore);
	EXPECT_FALSE(HeroListShowsHardcore());
	RemoveFile(SavePath().c_str());
}

TEST_F(ReforgedSwapHardcoreTest, OnlyReforgedWritesItsEntry)
{
	gbIsReforged = false;
	gbIsPoe2 = true;
	if (!MakeHero(/*hardcore=*/true, { "hf", "poe2" }))
		GTEST_SKIP() << "MPQ assets (spawn.mpq or DIABDAT.MPQ) not found - skipping test";
	EXPECT_FALSE(HasEntry("srvhero")) << "POE2 Mashup keeps its hero data in poe2hero alone";
	EXPECT_TRUE(HasEntry("poe2hero"));
	EXPECT_TRUE(MyPlayer->poe2.hardcore) << "POE2's league is read from poe2hero as before";
	RemoveFile(SavePath().c_str());

	gbIsPoe2 = false;
	if (!MakeHero(/*hardcore=*/true, { "hf" }))
		GTEST_SKIP();
	EXPECT_FALSE(HasEntry("srvhero")) << "plain Hellfire";
	EXPECT_FALSE(MyPlayer->poe2.hardcore) << "plain Hellfire heroes are never hardcore";
	RemoveFile(SavePath().c_str());
}

TEST_F(ReforgedSwapHardcoreTest, AFallenHardcoreHeroCarriesOnAsNormal)
{
	if (!MakeHero(/*hardcore=*/true))
		GTEST_SKIP() << "MPQ assets (spawn.mpq or DIABDAT.MPQ) not found - skipping test";
	Player &player = *MyPlayer;
	const std::string hardcoreStash = pfile_stash_save_path();
	EXPECT_NE(hardcoreStash.find("_hardcore"), std::string::npos) << hardcoreStash;
	EXPECT_FALSE(HardcoreHeroFell());

	FallFromHardcore(player);
	EXPECT_TRUE(HardcoreHeroFell());
	EXPECT_FALSE(player.poe2.hardcore);
	EXPECT_FALSE(IsHardcoreHero(player));
	EXPECT_TRUE(FileExists(hardcoreStash.c_str())) << "the hardcore stash is saved for the other hardcore heroes";
	const std::string normalStash = pfile_stash_save_path();
	EXPECT_EQ(normalStash.find("_hardcore"), std::string::npos) << "the hero now uses the Normal stash";

	// The next save keeps the hero Normal.
	pfile_write_hero(/*writeGameData=*/false);
	EXPECT_FALSE(HeroListShowsHardcore());
	RemoveFile(SavePath().c_str());
	RemoveFile(hardcoreStash.c_str());
}

TEST_F(ReforgedSwapHardcoreTest, AReforgedHeroIsSavedInReforgedsOwnEntriesAndLoadsBack)
{
	if (!MakeHero(/*hardcore=*/true))
		GTEST_SKIP() << "MPQ assets (spawn.mpq or DIABDAT.MPQ) not found - skipping test";
	Player &player = *MyPlayer;
	for (const char *entry : OldEntries)
		EXPECT_FALSE(HasEntry(entry)) << "a new hero has no " << entry;

	VisitDesignedFloor(2);
	const std::vector<uint8_t> floors = Poe2PackFloors();
	const std::vector<uint8_t> rooms = PackKeptFloorRooms();
	ASSERT_GT(rooms.size(), 2U) << "the floor's rooms";
	const Item shield = OneHanded(ItemType::Shield);
	player.swapHands[0] = TwoHandedSword();
	player.swapHands[1] = shield;
	player.poe2.weaponSet = 1;
	pfile_write_hero(/*writeGameData=*/false);

	EXPECT_TRUE(HasEntry("srvhero"));
	EXPECT_TRUE(HasEntry("srvfloors"));
	EXPECT_TRUE(HasEntry("srvrooms"));
	for (const char *entry : OldEntries)
		EXPECT_FALSE(HasEntry(entry)) << entry;

	// Read back as a new session would.
	Poe2FloorsReset();
	player.poe2.clear();
	player.swapHands[0].clear();
	player.swapHands[1].clear();
	pfile_read_player_from_save(SaveSlot, player);
	LoadReforgedHero(player);
	EXPECT_EQ(std::string_view(player._pName), "Swapper");
	EXPECT_EQ(player._pClass, HeroClass::Warrior);
	EXPECT_TRUE(player.poe2.hardcore);
	EXPECT_TRUE(HeroListShowsHardcore());
	EXPECT_EQ(player.poe2.weaponSet, 1);
	EXPECT_EQ(player.swapHands[0].IDidx, TwoHandedSword().IDidx);
	EXPECT_EQ(player.swapHands[1].IDidx, shield.IDidx);
	EXPECT_EQ(Poe2PackFloors(), floors) << "the floor records";
	EXPECT_EQ(PackKeptFloorRooms(), rooms) << "the rooms the floor was built from";

	// LoadPoe2HeroData, which tests and older callers use, reads a Reforged hero the same way.
	Poe2FloorsReset();
	player.poe2.clear();
	LoadPoe2HeroData(player);
	EXPECT_TRUE(player.poe2.hardcore);
	EXPECT_EQ(player.poe2.weaponSet, 1);
	EXPECT_EQ(Poe2PackFloors(), floors);
	RemoveFile(SavePath().c_str());
}

TEST_F(ReforgedSwapHardcoreTest, AnOlderReforgedSaveWithPoe2EntriesLoadsAndIsSavedUnderTheNewNames)
{
	if (!MakeHero(/*hardcore=*/true))
		GTEST_SKIP() << "MPQ assets (spawn.mpq or DIABDAT.MPQ) not found - skipping test";
	Player &player = *MyPlayer;
	VisitDesignedFloor(2);
	const std::vector<uint8_t> floors = Poe2PackFloors();
	const Item shield = OneHanded(ItemType::Shield);
	player.swapHands[1] = shield;
	player.poe2.weaponSet = 1;
	// POE2 state an older build left in the hero's POE2 entry, which Reforged never used.
	player.poe2.spiritSkulls = 3;
	player.poe2.flaskCharges[0] = 0;
	{
		// What the older build wrote: the POE2 entries (poe2floors holding the floor records), then srvhero.
		SaveWriter saveWriter(SavePath());
		saveWriter.RemoveHashEntry("srvfloors");
		saveWriter.RemoveHashEntry("srvrooms");
		gbIsReforged = false;
		SavePoe2HeroData(saveWriter, player);
		gbIsReforged = true;
		SaveReforgedHeroData(saveWriter, player);
	}
	for (const char *entry : OldEntries)
		ASSERT_TRUE(HasEntry(entry)) << entry;
	ASSERT_FALSE(HasEntry("srvfloors"));
	ASSERT_FALSE(HasEntry("srvrooms"));

	Poe2FloorsReset();
	player.poe2.clear();
	player.swapHands[1].clear();
	pfile_read_player_from_save(SaveSlot, player);
	LoadReforgedHero(player);
	EXPECT_EQ(std::string_view(player._pName), "Swapper");
	EXPECT_TRUE(player.poe2.hardcore);
	EXPECT_TRUE(HeroListShowsHardcore());
	EXPECT_EQ(player.poe2.weaponSet, 1);
	EXPECT_EQ(player.swapHands[1].IDidx, shield.IDidx);
	EXPECT_EQ(Poe2PackFloors(), floors) << "the floor records, read from poe2floors";
	EXPECT_EQ(player.poe2.spiritSkulls, 0) << "POE2 Mashup's hero entry isn't read";
	EXPECT_EQ(player.poe2.flaskCharges[0], MaxFlaskCharges) << "as a new hero has them";
	// The floor is still built from its rooms, which the next save keeps.
	EXPECT_GT(PackKeptFloorRooms().size(), 2U);

	// The next save writes Reforged's names and drops the old ones.
	pfile_write_hero(/*writeGameData=*/false);
	EXPECT_TRUE(HasEntry("srvhero"));
	EXPECT_TRUE(HasEntry("srvfloors"));
	EXPECT_TRUE(HasEntry("srvrooms"));
	for (const char *entry : OldEntries)
		EXPECT_FALSE(HasEntry(entry)) << entry;
	Poe2FloorsReset();
	player.poe2.clear();
	LoadReforgedHero(player);
	EXPECT_EQ(Poe2PackFloors(), floors);
	EXPECT_TRUE(player.poe2.hardcore);
	EXPECT_EQ(player.swapHands[1].IDidx, shield.IDidx);
	RemoveFile(SavePath().c_str());
}

TEST_F(ReforgedSwapHardcoreTest, Poe2SavesKeepTheirEntries)
{
	gbIsReforged = false;
	gbIsPoe2 = true;
	if (!MakeHero(/*hardcore=*/true, { "hf", "poe2" }))
		GTEST_SKIP() << "MPQ assets (spawn.mpq or DIABDAT.MPQ) not found - skipping test";
	pfile_write_hero(/*writeGameData=*/false);
	for (const char *entry : OldEntries)
		EXPECT_TRUE(HasEntry(entry)) << entry;
	for (const char *entry : { "srvhero", "srvfloors", "srvrooms" })
		EXPECT_FALSE(HasEntry(entry)) << entry;
	RemoveFile(SavePath().c_str());
}

} // namespace
} // namespace devilution
