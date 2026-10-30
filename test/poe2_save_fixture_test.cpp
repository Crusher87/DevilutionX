/**
 * Golden save (chaos damage plan, step 0): a POE2 hero saved on the build before chaos damage, with every kind of item
 * a save holds, and what each item rebuilt to when it loaded then: its base, rarity, name, modifiers by family id with
 * tiers and values, implicits, POE2 stats and crafting bytes. Every later step must load it the same, so no saved item
 * changes.
 *
 * The fixture is test/fixtures/poe2_save/: hero.sv (the hero, its bag, belt, jewels, weapon set and a town floor with
 * items on it), stash.sv and snapshot.txt. Remake it (only when a change is meant to move it) with
 * POE2_SAVE_FIXTURE_OUT=<folder> set; the test then builds the hero, saves it and writes the three files there.
 *
 * Vendor stock is in the stash (two of Griswold's premium wares): the game data file that holds the vendors' own lists
 * goes through the same item record, but needs a whole game to load. The snapshot records what loading did on the day
 * it was made, quirks too: the socketed jewel's increased damage comes back doubled (pl 32 for its 16%), while the
 * same kind of jewel in the bag keeps its own.
 */
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "DiabloUI/diabloui.h"
#include "cursor.h"
#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "init.hpp"
#include "inv.h"
#include "items.h"
#include "levels/gendung.h"
#include "loadsave.h"
#include "menu.h"
#include "pfile.h"
#include "player.h"
#include "poe2/bases.hpp"
#include "poe2/belt_uniques.hpp"
#include "poe2/craft_state.hpp"
#include "poe2/crafting_items.hpp"
#include "poe2/currency.hpp"
#include "poe2/loot.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/stash_layouts.hpp"
#include "poe2/stash_tabs.hpp"
#include "poe2/uniques.hpp"
#include "poe2/waystones.hpp"
#include "qol/stash.h"
#include "spells.h"
#include "stores.h"
#include "tables/monstdat.h"
#include "tables/playerdat.hpp"
#include "utils/file_util.h"
#include "utils/paths.h"

namespace devilution {
namespace {

constexpr std::string_view FixtureFolder = "test/fixtures/poe2_save/";

class Poe2SaveFixtureTest : public ::testing::Test {
public:
	void SetUp() override
	{
		LoadCoreArchives();
		LoadGameArchives();
		haveMainData_ = HaveMainData();
		Players.resize(1);
		MyPlayerId = 0;
		MyPlayer = &Players[0];
		*MyPlayer = {};
		gbVanilla = false;
		gbIsSpawn = false;
		gbIsMultiplayer = false;
		gbIsHellfire = true;
		gbIsHellfireSaveGame = true;
		gbIsPoe2 = true;
		sgGameInitInfo.nDifficulty = DIFF_NORMAL;
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		LoadItemData();
		LoadSpellData();
		LoadPlayerDataFiles();
		LoadMonsterData();
		LoadModifierData();
		LoadPassiveTreeData();
		previousPrefPath_ = paths::PrefPath();
		// A folder of its own: other tests save heroes in the build folder at the same time.
		folder_ = ::testing::TempDir() + "poe2_save_fixture_" + ::testing::UnitTest::GetInstance()->current_test_info()->name() + DIRECTORY_SEPARATOR_STR;
		CreateDir(folder_.c_str());
		for (const std::string &file : ListFiles(folder_.c_str()))
			RemoveFile((folder_ + file).c_str());
		paths::SetPrefPath(folder_);
		gSaveNumber = 0;
	}

	void TearDown() override
	{
		Stash = {};
		StashTabs.reset();
		ActiveItemCount = 0;
		memset(dItem, 0, sizeof(dItem));
		paths::SetPrefPath(previousPrefPath_);
		gbIsPoe2 = false;
		gbIsHellfire = false;
		gbIsHellfireSaveGame = false;
		leveltype = DTYPE_TOWN;
		currlevel = 0;
		LoadPassiveTreeData();
		LoadModifierData();
		UnloadModArchives();
	}

	static _item_indexes Row(std::string_view name)
	{
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iName == name && AllItemsList[i].iMiscId != IMISC_UNIQUE)
				return static_cast<_item_indexes>(i);
		}
		ADD_FAILURE() << name;
		return IDI_GOLD;
	}

	/** An item of the base as a drop makes it. */
	static Item Make(std::string_view base, uint32_t seed, item_quality rarity, int itemLevel)
	{
		Item item = {};
		SetLootRarity(item, rarity);
		SetPoe2ItemLevel(item, itemLevel);
		SetRndSeed(seed);
		SetupAllItems(*MyPlayer, item, Row(base), AdvanceRndSeed(), 30, 1, /*onlygood=*/true, /*pregen=*/false, 0, /*forceNotUnique=*/true);
		item._iIdentified = true;
		EXPECT_FALSE(item.isEmpty()) << base;
		return item;
	}

	static Item MakeBundleUnique(std::string_view name, uint32_t seed)
	{
		Item item;
		const Poe2Unique *unique = FindPoe2Unique(name);
		if (unique == nullptr || !CreatePoe2Unique(*MyPlayer, item, unique->uid, seed)) {
			ADD_FAILURE() << name;
			return item;
		}
		item._iIdentified = true;
		return item;
	}

	/** The first Diablo unique a staff's home row rolls, the way a unique monster's drop makes it. */
	static Item MakeDiabloUnique()
	{
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iItemId != UITYPE_LONGSTAFF || AllItemsList[i].dropRate == 0)
				continue;
			for (uint32_t seed = 1; seed <= 600; seed++) {
				Item item = {};
				SetRndSeed(seed * 2654435761U);
				SetupAllItems(*MyPlayer, item, static_cast<_item_indexes>(i), AdvanceRndSeed(), 20, 15, /*onlygood=*/true, /*pregen=*/false);
				if (item._iMagical == ITEM_QUALITY_UNIQUE) {
					item._iIdentified = true;
					return item;
				}
			}
			break;
		}
		ADD_FAILURE() << "no Diablo unique";
		return {};
	}

	static Item MakeWaystone(uint32_t seed, int tier)
	{
		Item item = {};
		item.dwBuff |= CF_POE2;
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iMiscId != IMISC_WAYSTONE)
				continue;
			SetRndSeed(seed);
			SetupAllItems(*MyPlayer, item, static_cast<_item_indexes>(i), AdvanceRndSeed(), 30, 1, /*onlygood=*/false, /*pregen=*/false);
			RollWaystone(item, tier);
			return item;
		}
		ADD_FAILURE() << "no waystone";
		return item;
	}

	static Item MakeBeltUnique(BeltUniqueId id, uint32_t seed)
	{
		const BeltUnique *unique = GetBeltUnique(id);
		Item item {};
		if (unique == nullptr || !BeltUniqueBase(*unique)) {
			ADD_FAILURE();
			return item;
		}
		InitializeItem(item, static_cast<_item_indexes>(*BeltUniqueBase(*unique)));
		item._iSeed = seed;
		item.dwBuff |= CF_POE2;
		devilution::MakeBeltUnique(item, id);
		return item;
	}

	/** An essence, rune, soul core or talisman with the variant, as a save from before the runes rework holds it. */
	static Item MakeCraftingItem(item_misc_id miscId, uint8_t variant)
	{
		Item item {};
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iMiscId == miscId && AllItemsList[i].dropRate > 0) {
				InitializeItem(item, static_cast<_item_indexes>(i));
				break;
			}
		}
		EXPECT_FALSE(item.isEmpty()) << static_cast<int>(miscId);
		item.dwBuff |= CF_POE2;
		SetCraftingItemVariant(item, variant);
		FinishCraftingItem(item);
		return item;
	}

	/** The item with an augment in each of its sockets, the way the Artificer's Orb and the augment put them there. */
	static Item Socketed(Item item, std::initializer_list<AugmentId> augments)
	{
		for (const AugmentId augment : augments) {
			EXPECT_TRUE(ApplyCurrencyOrb(CurrencyOrb::Artificer, *MyPlayer, item));
			EXPECT_TRUE(ApplyCurrencyOrb(CurrencyOrb::Augment, *MyPlayer, item, static_cast<uint8_t>(augment)));
		}
		return item;
	}

	/** Saves the loaded hero, stash and town floor again, with today's code. */
	static void SaveAgain()
	{
		pfile_write_hero(/*writeGameData=*/false);
		Stash.dirty = true;
		sfile_write_stash();
		pfile_save_level();
	}

	/** The snapshot without its crafting bytes, which change when ids convert. */
	static std::string WithoutCraftBytes(const std::string &snapshot)
	{
		std::string out;
		for (const std::string &line : Lines(snapshot)) {
			if (line.rfind("  craft", 0) != 0)
				out += line + "\n";
		}
		return out;
	}

	static Item MakeBase(std::string_view name)
	{
		Item item {};
		InitializeItem(item, Row(name));
		return item;
	}

	/** Makes the hero with every kind of item and saves it, its stash and a town floor with items on it. */
	void BuildAndSave()
	{
		_uiheroinfo info {};
		info.heroclass = HeroClass::Sorcerer;
		CopyUtf8(info.name, "Golden", sizeof(info.name));
		ASSERT_TRUE(pfile_ui_save_create(&info));
		pfile_read_player_from_save(0, *MyPlayer);
		LoadPoe2HeroData(*MyPlayer);
		Player &player = *MyPlayer;
		player.setCharacterLevel(60);
		player._pgfxnum = 0;
		// Placing items in the bag and the stash needs the cursor graphics (a new hero's starting items free them).
		InitCursor();

		// Worn: rares, a normal ring with its implicit, a quest unique, a bundle unique and a locked caster wand.
		player.InvBody[INVLOC_HEAD] = Make("Iron Cap", 101, ITEM_QUALITY_RARE, 60);
		player.InvBody[INVLOC_CHEST] = Make("Full Plate", 102, ITEM_QUALITY_RARE, 70);
		player.InvBody[INVLOC_RING_LEFT] = Make("Ruby Ring", 103, ITEM_QUALITY_NORMAL, 40);
		player.InvBody[INVLOC_RING_RIGHT] = Make("Sapphire Ring", 104, ITEM_QUALITY_RARE, 75);
		MakeFixedUnique(player, player.InvBody[INVLOC_AMULET], IDI_OPTAMULET, 16, UITEM_OPTAMULET);
		player.InvBody[INVLOC_HAND_LEFT] = Make("Bone Wand", 106, ITEM_QUALITY_RARE, 65);
		player.InvBody[INVLOC_HAND_RIGHT] = MakeBundleUnique("Eye of the Deep", 107);

		// The bag, after the starting items: a locked caster staff, a magic wand, jewels, a normal ring with its
		// implicit, a waystone, a rare amulet, a magic focus and a Diablo unique.
		const std::vector<Item> bag = {
			Make("Gelid Staff", 201, ITEM_QUALITY_RARE, 80),
			Make("Withered Wand", 202, ITEM_QUALITY_MAGIC, 30),
			Make("Ruby", 205, ITEM_QUALITY_RARE, 50),
			Make("Emerald", 206, ITEM_QUALITY_MAGIC, 30),
			Make("Golden Hoop", 207, ITEM_QUALITY_NORMAL, 20),
			Make("Jade Amulet", 213, ITEM_QUALITY_RARE, 50),
			Make("Carved Focus", 212, ITEM_QUALITY_MAGIC, 45),
			MakeWaystone(209, 7),
			MakeDiabloUnique(),
		};
		for (const Item &item : bag)
			ASSERT_TRUE(AutoPlaceItemInInventory(player, item)) << item._iIName;

		// A charm and a belt unique beside the starting flasks.
		player.SpdList[4] = MakeBase("Ruby Charm");
		player.SpdList[2] = MakeBeltUnique(BeltUniqueId::RedMercy, 4321);

		// A jewel in the tree and a staff in the other weapon set.
		player.jewels[0] = Make("Sapphire", 301, ITEM_QUALITY_RARE, 60);
		player.swapHands[0] = Make("Voltaic Staff", 302, ITEM_QUALITY_RARE, 70);

		// The stash: a quarterstaff, a staff from before the caster weapons rework, bundle uniques, magic and rare armour,
		// and two of Griswold's premium wares.
		Stash = {};
		StashTabs.reset();
		const std::vector<Item> stash = {
			Make("Heavy Ash Staff", 203, ITEM_QUALITY_RARE, 55),
			Make("Focused Ashen Staff", 204, ITEM_QUALITY_RARE, 60),
			Make("Hide Buckler", 208, ITEM_QUALITY_MAGIC, 25),
			Make("Leather Vest", 401, ITEM_QUALITY_RARE, 50),
			MakeBundleUnique("Ashen Covenant", 210),
			MakeBundleUnique("Wolfsplitter", 211),
		};
		for (const Item &item : stash)
			ASSERT_TRUE(AutoPlaceItemInStash(item, true)) << item._iIName;
		SetRndSeed(402);
		PremiumItems.clear();
		PremiumItemLevel = 1;
		SpawnPremium(player);
		ASSERT_GE(PremiumItems.size(), 2U);
		for (size_t i = 0; i < 2; i++)
			ASSERT_TRUE(AutoPlaceItemInStash(PremiumItems[i], true));
		Stash.dirty = true;

		// The town floor, with items lying on it.
		leveltype = DTYPE_TOWN;
		currlevel = 0;
		setlevel = false;
		player.setLevel(0);
		memset(dItem, 0, sizeof(dItem));
		ActiveItemCount = 0;
		const std::vector<Item> ground = {
			Make("Iron Cap", 501, ITEM_QUALITY_MAGIC, 20),
			Make("Ochre Sceptre", 502, ITEM_QUALITY_RARE, 60),
			MakeBundleUnique("Red Harvest", 503),
		};
		for (const Item &item : ground) {
			const int ii = ActiveItemCount;
			ActiveItems[ii] = static_cast<uint8_t>(ii);
			Items[ii] = item;
			Items[ii].position = { 60 + ii, 70 };
			dItem[60 + ii][70] = static_cast<int8_t>(ii + 1);
			ActiveItemCount++;
		}

		FreeCursor();
		pfile_write_hero(/*writeGameData=*/false);
		sfile_write_stash();
		pfile_save_level();
	}

	/** Loads the hero, the stash and the town floor from the saves in the folder. */
	static void Load()
	{
		*MyPlayer = {};
		Stash = {};
		StashTabs.reset();
		pfile_read_player_from_save(0, *MyPlayer);
		LoadPoe2HeroData(*MyPlayer);
		LoadStash();
		leveltype = DTYPE_TOWN;
		currlevel = 0;
		setlevel = false;
		MyPlayer->plractive = false;
		ASSERT_TRUE(LoadLevel().has_value());
	}

	static void AddItem(std::ostringstream &out, std::string_view where, const Item &item)
	{
		if (item.isEmpty())
			return;
		out << where << ": " << AllItemsList[item.IDidx].iName << " (row " << static_cast<int>(item.IDidx) << ") rarity " << static_cast<int>(item._iMagical)
		    << " level " << GetPoe2ItemLevel(item) << " \"" << item._iIName << "\"";
		const BeltUnique *beltUnique = GetBeltUnique(item);
		if (beltUnique != nullptr)
			out << " belt unique " << beltUnique->name << " effect " << BeltUniqueEffect(item);
		else if (item._iMagical == ITEM_QUALITY_UNIQUE)
			out << " unique " << UniqueItems[item._iUid].UIName;
		out << "\n";
		// The Diablo stats the save holds and the rebuild may change.
		out << "  base ac " << item._iAC << " dam " << item._iMinDam << "-" << item._iMaxDam << " pl " << item._iPLDam << " " << item._iPLToHit << " "
		    << item._iPLAC << " attr " << item._iPLStr << " " << item._iPLMag << " " << item._iPLDex << " " << item._iPLVit << " res " << item._iPLFR << " "
		    << item._iPLLR << " " << item._iPLMR << " hp " << item._iPLHP.raw() << " mana " << item._iPLMana.raw() << " flags " << static_cast<uint32_t>(item._iFlags)
		    << " req " << static_cast<int>(item._iMinStr) << " " << static_cast<int>(item._iMinMag) << " " << static_cast<int>(item._iMinDex) << "\n";
		if (item._iMagical == ITEM_QUALITY_UNIQUE && beltUnique == nullptr) {
			for (const auto &[effect, value] : RollUniqueEffects(item))
				out << "  unique effect " << static_cast<int>(effect->mod) << " " << static_cast<int>(effect->rule) << " " << value << "\n";
		}
		for (const Poe2ItemMod &mod : item._iPoe2Mods) {
			const ModFamily *family = ItemModFamily(mod);
			if (family == nullptr)
				continue;
			out << "  mod " << family->id << " T" << static_cast<int>(mod.tier) << " " << mod.values[0] << " " << mod.values[1] << "\n";
		}
		for (const ItemImplicit &implicit : ItemImplicits(item))
			out << "  implicit " << static_cast<int>(implicit.implicit->effect) << " " << implicit.values[0] << " " << implicit.values[1] << "\n";
		bool any = false;
		for (size_t stat = 0; stat < item._iPoe2Stats.size(); stat++) {
			if (item._iPoe2Stats[stat] == 0)
				continue;
			out << (any ? " " : "  stats ") << stat << "=" << item._iPoe2Stats[stat];
			any = true;
		}
		if (any)
			out << "\n";
		out << "  craft";
		char hex[3];
		for (const uint8_t b : PackCraftState(item._iPoe2Craft)) {
			snprintf(hex, sizeof(hex), "%02x", b);
			out << hex;
		}
		out << "\n";
	}

	/** Every item of the loaded hero, stash and floor, as it rebuilt. */
	static std::string Snapshot()
	{
		std::ostringstream out;
		const Player &player = *MyPlayer;
		for (int slot = 0; slot < NUM_INVLOC; slot++)
			AddItem(out, "body " + std::to_string(slot), player.InvBody[slot]);
		for (int i = 0; i < player._pNumInv; i++)
			AddItem(out, "bag " + std::to_string(i), player.InvList[i]);
		for (int i = 0; i < MaxBeltItems; i++)
			AddItem(out, "belt " + std::to_string(i), player.SpdList[i]);
		for (size_t i = 0; i < player.jewels.size(); i++)
			AddItem(out, "jewel " + std::to_string(i), player.jewels[i]);
		for (size_t i = 0; i < player.swapHands.size(); i++)
			AddItem(out, "swap " + std::to_string(i), player.swapHands[i]);
		for (size_t i = 0; i < Stash.stashList.size(); i++)
			AddItem(out, "stash " + std::to_string(i), Stash.stashList[i]);
		for (int i = 0; i < ActiveItemCount; i++)
			AddItem(out, "ground " + std::to_string(i), Items[ActiveItems[i]]);
		return out.str();
	}

	static bool CopyFile(const std::string &from, const std::string &to)
	{
		std::ifstream in(from, std::ios::binary);
		if (!in)
			return false;
		std::ofstream out(to, std::ios::binary | std::ios::trunc);
		out << in.rdbuf();
		return static_cast<bool>(out);
	}

	static std::string ReadText(const std::string &path)
	{
		std::ifstream in(path, std::ios::binary);
		std::ostringstream text;
		text << in.rdbuf();
		return text.str();
	}

	static std::vector<std::string> Lines(const std::string &text)
	{
		std::vector<std::string> lines;
		std::istringstream in(text);
		for (std::string line; std::getline(in, line);)
			lines.push_back(line);
		return lines;
	}

	bool haveMainData_ = false;
	std::string previousPrefPath_;
	std::string folder_;
};

TEST_F(Poe2SaveFixtureTest, GoldenSaveLoadsAsRecorded)
{
	if (!haveMainData_)
		GTEST_SKIP() << "MPQ assets (spawn.mpq or DIABDAT.MPQ) not found - skipping test";

	const char *out = std::getenv("POE2_SAVE_FIXTURE_OUT");
	if (out != nullptr) {
		BuildAndSave();
		ASSERT_FALSE(HasFatalFailure());
		const std::string target = std::string(out) + DIRECTORY_SEPARATOR_STR;
		ASSERT_TRUE(CopyFile(pfile_hero_save_path(), target + "hero.sv"));
		ASSERT_TRUE(CopyFile(pfile_stash_save_path(), target + "stash.sv"));
		Load();
		std::ofstream(target + "snapshot.txt", std::ios::binary) << Snapshot();
		return;
	}

	const std::string fixtures = paths::BasePath() + std::string(FixtureFolder);
	ASSERT_TRUE(CopyFile(fixtures + "hero.sv", pfile_hero_save_path()));
	ASSERT_TRUE(CopyFile(fixtures + "stash.sv", pfile_stash_save_path()));
	Load();

	const std::vector<std::string> expected = Lines(ReadText(fixtures + "snapshot.txt"));
	const std::vector<std::string> actual = Lines(Snapshot());
	ASSERT_GT(expected.size(), 50U);
	for (size_t i = 0; i < std::max<size_t>(expected.size(), actual.size()); i++) {
		const std::string_view want = i < expected.size() ? std::string_view(expected[i]) : std::string_view("(nothing)");
		const std::string_view got = i < actual.size() ? std::string_view(actual[i]) : std::string_view("(nothing)");
		EXPECT_EQ(want, got) << "line " << i + 1 << " of snapshot.txt";
		if (want != got)
			break;
	}
	// Every kind of item the fixture holds came back.
	const std::string snapshot = Snapshot();
	for (const std::string_view kind : { "body ", "bag ", "belt ", "jewel ", "swap ", "stash ", "ground " })
		EXPECT_NE(snapshot.find(kind), std::string::npos) << kind;
}

TEST_F(Poe2SaveFixtureTest, GoldenSaveSavedAgainLoadsTheSame)
{
	if (!haveMainData_)
		GTEST_SKIP() << "MPQ assets (spawn.mpq or DIABDAT.MPQ) not found - skipping test";

	const std::string fixtures = paths::BasePath() + std::string(FixtureFolder);
	ASSERT_TRUE(CopyFile(fixtures + "hero.sv", pfile_hero_save_path()));
	ASSERT_TRUE(CopyFile(fixtures + "stash.sv", pfile_stash_save_path()));
	Load();
	const std::string first = Snapshot();
	SaveAgain();
	Load();
	const std::vector<std::string> expected = Lines(first);
	const std::vector<std::string> actual = Lines(Snapshot());
	ASSERT_EQ(expected.size(), actual.size());
	for (size_t i = 0; i < expected.size(); i++) {
		EXPECT_EQ(expected[i], actual[i]) << "line " << i + 1;
		if (expected[i] != actual[i])
			break;
	}
}

/**
 * Runes rework, step 2: a save from before it holds only the legacy augment and essence ids, packed narrow. Today's
 * code writes such an item exactly as the old code did (the narrow layout is unchanged), so a hero saved with them
 * stands for an old save. Loading gives every place a save holds items (bag, worn gear, stash, floor) today's ids,
 * with the same stats; saving that writes the wide layout, which loads the same again.
 */
TEST_F(Poe2SaveFixtureTest, OldCraftingIdsConvertOnLoad)
{
	if (!haveMainData_)
		GTEST_SKIP() << "MPQ assets (spawn.mpq or DIABDAT.MPQ) not found - skipping test";

	_uiheroinfo info {};
	info.heroclass = HeroClass::Sorcerer;
	CopyUtf8(info.name, "Runes", sizeof(info.name));
	ASSERT_TRUE(pfile_ui_save_create(&info));
	pfile_read_player_from_save(0, *MyPlayer);
	LoadPoe2HeroData(*MyPlayer);
	Player &player = *MyPlayer;
	player.setCharacterLevel(60);
	player._pgfxnum = 0;
	InitCursor();

	// Worn: body armour with a rune and a soul core, and a caster staff with a rune and a talisman.
	player.InvBody[INVLOC_CHEST] = Socketed(Make("Full Plate", 102, ITEM_QUALITY_RARE, 70), { AugmentId::WindRune, AugmentId::HeartCore });
	player.InvBody[INVLOC_HAND_LEFT] = Socketed(Make("Gelid Staff", 201, ITEM_QUALITY_RARE, 80), { AugmentId::EmberRune, AugmentId::HunterTalisman });
	for (const int slot : { INVLOC_CHEST, INVLOC_HAND_LEFT }) {
		ASSERT_EQ(player.InvBody[slot]._iPoe2Craft.sockets, 2);
		ASSERT_EQ(PackCraftState(player.InvBody[slot]._iPoe2Craft)[0], 0xC2) << "packed as before the runes rework";
	}
	// The bag: a helmet with a soul core, runes and essences of the first and the last legacy kinds.
	const std::vector<Item> bag = {
		Socketed(Make("Iron Cap", 101, ITEM_QUALITY_RARE, 60), { AugmentId::KeenCore }),
		MakeCraftingItem(IMISC_AUGMENT, static_cast<uint8_t>(AugmentId::IronRune)),
		MakeCraftingItem(IMISC_AUGMENT, static_cast<uint8_t>(AugmentId::GuardianTalisman)),
		MakeCraftingItem(IMISC_ESSENCE, EssenceVariant(EssenceKind::Vigor, EssenceTier::Lesser)),
		MakeCraftingItem(IMISC_ESSENCE, EssenceVariant(EssenceKind::Summoning, EssenceTier::Perfect)),
	};
	for (const Item &item : bag)
		ASSERT_TRUE(AutoPlaceItemInInventory(player, item)) << item._iIName;
	// The stash: a rune and an essence in the currency tab, and armour with a soul core.
	Stash = {};
	StashTabs.reset();
	const std::vector<Item> stash = {
		MakeCraftingItem(IMISC_AUGMENT, static_cast<uint8_t>(AugmentId::StormRune)),
		MakeCraftingItem(IMISC_ESSENCE, EssenceVariant(EssenceKind::Warding, EssenceTier::Greater)),
		Socketed(Make("Leather Vest", 401, ITEM_QUALITY_RARE, 50), { AugmentId::MindCore }),
	};
	for (const Item &item : stash)
		ASSERT_TRUE(AutoPlaceItemInStash(item, true)) << item._iIName;
	Stash.dirty = true;
	// The town floor: a rune and a sword with one.
	leveltype = DTYPE_TOWN;
	currlevel = 0;
	setlevel = false;
	player.setLevel(0);
	memset(dItem, 0, sizeof(dItem));
	ActiveItemCount = 0;
	const std::vector<Item> ground = {
		MakeCraftingItem(IMISC_AUGMENT, static_cast<uint8_t>(AugmentId::FrostRune)),
		Socketed(Make("Shortsword", 502, ITEM_QUALITY_RARE, 40), { AugmentId::IronRune }),
	};
	for (const Item &item : ground) {
		const int ii = ActiveItemCount;
		ActiveItems[ii] = static_cast<uint8_t>(ii);
		Items[ii] = item;
		Items[ii].position = { 60 + ii, 70 };
		dItem[60 + ii][70] = static_cast<int8_t>(ii + 1);
		ActiveItemCount++;
	}
	FreeCursor();
	const std::string saved = Snapshot();
	std::vector<std::optional<unsigned>> stashSlots;
	for (const Item &item : Stash.stashList)
		stashSlots.push_back(CurrencyFixedSlot(item));
	pfile_write_hero(/*writeGameData=*/false);
	sfile_write_stash();
	pfile_save_level();

	Load();
	const std::string loaded = Snapshot();
	// Every item came back with the same stats and names.
	EXPECT_EQ(WithoutCraftBytes(loaded), WithoutCraftBytes(saved));
	// And today's ids.
	const auto expectSockets = [](const Item &item, std::initializer_list<AugmentId> augments) {
		ASSERT_EQ(item._iPoe2Craft.sockets, augments.size()) << item._iIName;
		size_t i = 0;
		bool wide = false;
		for (const AugmentId augment : augments) {
			EXPECT_EQ(item._iPoe2Craft.augments[i++], static_cast<uint8_t>(augment)) << item._iIName;
			wide = wide || static_cast<uint8_t>(augment) > NarrowMaxAugmentId;
		}
		// Ids up to 15 still pack narrow.
		EXPECT_EQ(PackCraftState(item._iPoe2Craft)[0], wide ? 0xC3 : 0xC2) << item._iIName;
	};
	expectSockets(player.InvBody[INVLOC_CHEST], { AugmentId::LesserShael, AugmentId::LordOfThePit });
	expectSockets(player.InvBody[INVLOC_HAND_LEFT], { AugmentId::LesserRal, AugmentId::RelicOfRakkis });
	std::vector<AugmentId> augments;
	std::vector<uint8_t> essences;
	bool sawHelmet = false;
	for (int i = 0; i < player._pNumInv; i++) {
		const Item &item = player.InvList[i];
		if (item._iMiscId == IMISC_AUGMENT)
			augments.push_back(GetAugmentId(item));
		else if (item._iMiscId == IMISC_ESSENCE)
			essences.push_back(CraftingItemVariant(item));
		else if (item._iPoe2Craft.sockets == 1 && item._itype == ItemType::Helm) {
			expectSockets(item, { AugmentId::StareyeTheWitch });
			sawHelmet = true;
		}
	}
	EXPECT_TRUE(sawHelmet);
	EXPECT_NE(std::find(augments.begin(), augments.end(), AugmentId::LesserOhm), augments.end());
	EXPECT_NE(std::find(augments.begin(), augments.end(), AugmentId::RelicOfAkarat), augments.end());
	EXPECT_NE(std::find(essences.begin(), essences.end(), EssenceVariant(EssenceKind::Body, EssenceTier::Lesser)), essences.end());
	EXPECT_NE(std::find(essences.begin(), essences.end(), EssenceVariant(EssenceKind::Command, EssenceTier::Perfect)), essences.end());
	ASSERT_EQ(Stash.stashList.size(), stashSlots.size());
	for (size_t i = 0; i < Stash.stashList.size(); i++) {
		const Item &item = Stash.stashList[i];
		EXPECT_EQ(CurrentAugmentId(GetAugmentId(item)), GetAugmentId(item));
		EXPECT_EQ(CurrentEssenceKind(GetEssenceKind(item)), GetEssenceKind(item));
		// The currency tab keeps them in the places they had.
		EXPECT_EQ(CurrencyFixedSlot(item), stashSlots[i]) << item._iIName;
		if (item._iClass == ICLASS_ARMOR)
			expectSockets(item, { AugmentId::TheDefiler });
	}
	ASSERT_EQ(ActiveItemCount, 2);
	EXPECT_EQ(GetAugmentId(Items[ActiveItems[0]]), AugmentId::LesserThul);
	expectSockets(Items[ActiveItems[1]], { AugmentId::LesserOhm });

	// Saved in the wide layout, it loads the same again.
	SaveAgain();
	Load();
	EXPECT_EQ(Snapshot(), loaded);

	// The crafting extension rides in the item save's anim widths.
	const std::array<uint8_t, CraftExtensionSize> extension { 9, 0, 0x80, 0, 0xFF, 1 };
	MyPlayer->InvBody[INVLOC_CHEST]._iPoe2Craft.extension = extension;
	SaveAgain();
	Load();
	EXPECT_EQ(MyPlayer->InvBody[INVLOC_CHEST]._iPoe2Craft.extension, extension);
	EXPECT_EQ(Snapshot(), loaded);
}

TEST_F(Poe2SaveFixtureTest, ChaosResistanceSourcesSurviveASave)
{
	if (!haveMainData_)
		GTEST_SKIP() << "MPQ assets (spawn.mpq or DIABDAT.MPQ) not found - skipping test";

	// Chaos damage plan step 5: the Amethyst Ring, a Venom Rune in armour, an essence of Ruin and its crafted modifier.
	_uiheroinfo info {};
	info.heroclass = HeroClass::Sorcerer;
	CopyUtf8(info.name, "Amethyst", sizeof(info.name));
	ASSERT_TRUE(pfile_ui_save_create(&info));
	pfile_read_player_from_save(0, *MyPlayer);
	LoadPoe2HeroData(*MyPlayer);
	Player &player = *MyPlayer;
	player.setCharacterLevel(60);
	// Strong enough to wear the plate after the load too.
	player._pBaseStr = player._pStrength = 200;
	player._pBaseDex = player._pDexterity = 200;
	player._pBaseMag = player._pMagic = 200;
	player._pgfxnum = 0;
	InitCursor();

	player.InvBody[INVLOC_RING_LEFT] = Make("Amethyst Ring", 301, ITEM_QUALITY_NORMAL, 40);
	player.InvBody[INVLOC_CHEST] = Socketed(Make("Full Plate", 102, ITEM_QUALITY_RARE, 70), { AugmentId::VenomRune });
	Item helmet = Make("Iron Cap", 101, ITEM_QUALITY_MAGIC, 60);
	ASSERT_TRUE(ApplyCurrencyOrb(CurrencyOrb::Essence, player, helmet, EssenceVariant(EssenceKind::Ruin, EssenceTier::Greater)));
	player.InvBody[INVLOC_HEAD] = helmet;
	for (const Item &item : { MakeCraftingItem(IMISC_ESSENCE, EssenceVariant(EssenceKind::Ruin, EssenceTier::Perfect)), MakeCraftingItem(IMISC_AUGMENT, static_cast<uint8_t>(AugmentId::VenomRune)) })
		ASSERT_TRUE(AutoPlaceItemInInventory(player, item)) << item._iIName;
	for (Item &item : player.InvBody)
		item._iStatFlag = !item.isEmpty();
	CalcPlrItemVals(player, false);
	const int chaos = player.poe2.chaosResistance;
	EXPECT_GE(chaos, 7 + 10 + 1);
	Stash = {};
	StashTabs.reset();
	Stash.dirty = true;
	leveltype = DTYPE_TOWN;
	currlevel = 0;
	setlevel = false;
	player.setLevel(0);
	memset(dItem, 0, sizeof(dItem));
	ActiveItemCount = 0;
	FreeCursor();
	const std::string saved = Snapshot();
	SaveAgain();

	Load();
	EXPECT_EQ(Snapshot(), saved);
	CalcPlrItemVals(*MyPlayer, false);
	EXPECT_EQ(MyPlayer->poe2.chaosResistance, chaos);
	std::vector<uint8_t> variants;
	for (int i = 0; i < MyPlayer->_pNumInv; i++)
		variants.push_back(CraftingItemVariant(MyPlayer->InvList[i]));
	EXPECT_NE(std::find(variants.begin(), variants.end(), EssenceVariant(EssenceKind::Ruin, EssenceTier::Perfect)), variants.end());
	EXPECT_NE(std::find(variants.begin(), variants.end(), static_cast<uint8_t>(AugmentId::VenomRune)), variants.end());
}

} // namespace
} // namespace devilution
