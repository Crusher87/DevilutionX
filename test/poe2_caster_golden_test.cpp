/**
 * Golden files for the caster weapons rework (plan step S0): fingerprints of what saved staves, sceptres,
 * quarterstaves and Diablo staff and mace uniques rebuild to, taken before the rework. Every later step keeps them,
 * so no saved item re-rolls.
 *
 * Regenerate (only when a change is meant to move them) with POE2_GOLDEN_OUT=<path> set; the test then writes the
 * table to that path instead of comparing.
 */
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/bases.hpp"
#include "poe2/craft_state.hpp"
#include "poe2/currency.hpp"
#include "poe2/desecration.hpp"
#include "poe2/loot.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/uniques.hpp"
#include "spells.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

struct GoldenEntry {
	std::string_view key;
	uint64_t hash;
};

constexpr GoldenEntry GoldenTable[] = {
	{ "", 0 },
#include "poe2_caster_golden_data.inc"
};

class Fingerprint {
public:
	template <typename T>
	Fingerprint &add(T value)
	{
		const auto v = static_cast<uint64_t>(static_cast<int64_t>(value));
		for (int i = 0; i < 8; i++)
			byte(static_cast<uint8_t>(v >> (i * 8)));
		return *this;
	}

	Fingerprint &add(std::string_view text)
	{
		add(text.size());
		for (const char c : text)
			byte(static_cast<uint8_t>(c));
		return *this;
	}

	Fingerprint &add(const std::string &text)
	{
		return add(std::string_view(text));
	}

	[[nodiscard]] uint64_t value() const { return hash_; }

private:
	void byte(uint8_t b)
	{
		hash_ ^= b;
		hash_ *= 0x100000001B3ULL;
	}

	uint64_t hash_ = 0xCBF29CE484222325ULL;
};

/** Everything a rebuilt item's stats, name and modifiers show. Families by id, so their order may grow. */
void AddItem(Fingerprint &print, const Item &item)
{
	print.add(static_cast<int>(item.IDidx)).add(static_cast<int>(item._iMagical)).add(std::string_view(item._iIName)).add(std::string_view(item._iName));
	print.add(item._iSeed).add(item._iCreateInfo).add(item.dwBuff).add(item._ivalue).add(item._iIvalue);
	print.add(item._iMinDam).add(item._iMaxDam).add(item._iAC).add(static_cast<uint64_t>(item._iFlags)).add(static_cast<int>(item._iCurs));
	print.add(item._iPLDam).add(item._iPLToHit).add(item._iPLAC).add(item._iPLStr).add(item._iPLMag).add(item._iPLDex).add(item._iPLVit);
	print.add(item._iPLFR).add(item._iPLLR).add(item._iPLMR).add(item._iPLMana.raw()).add(item._iPLHP.raw()).add(item._iPLDamMod).add(item._iPLGetHit);
	print.add(item._iPLLight).add(item._iSplLvlAdd).add(item._iUid).add(item._iFMinDam).add(item._iFMaxDam).add(item._iLMinDam).add(item._iLMaxDam);
	print.add(item._iPLLifeRegen).add(item._iPLManaRegen).add(item._iMinStr).add(item._iMinMag).add(item._iMinDex);
	print.add(item._iDurability).add(item._iMaxDur).add(item._iCharges).add(item._iMaxCharges).add(static_cast<int>(item._iSpell));
	print.add(static_cast<int>(item._iPrePower)).add(static_cast<int>(item._iSufPower));
	for (const item_effect_type power : item._iRarePowers)
		print.add(static_cast<int>(power));
	for (const Poe2ItemMod &mod : item._iPoe2Mods) {
		const ModFamily *family = ItemModFamily(mod);
		print.add(family != nullptr ? std::string_view(family->id) : std::string_view {}).add(mod.tier).add(mod.values[0]).add(mod.values[1]);
	}
	// The stats from before the rework; the ones it appends must stay 0 on these items.
	constexpr size_t LegacyStatCount = static_cast<size_t>(Poe2ItemStat::LocalSpiritPercent);
	for (size_t stat = 0; stat < item._iPoe2Stats.size(); stat++) {
		if (stat < LegacyStatCount)
			print.add(item._iPoe2Stats[stat]);
		else if (item._iPoe2Stats[stat] != 0)
			print.add(std::string_view { "new stat" });
	}
	for (const ItemImplicit &implicit : ItemImplicits(item))
		print.add(static_cast<int>(implicit.implicit->effect)).add(implicit.values[0]).add(implicit.values[1]);
	for (const uint8_t b : PackCraftState(item._iPoe2Craft))
		print.add(b);
}

std::string StrCatKey(std::string_view what, int row, int level, int rarity)
{
	return std::string(what) + "/" + std::to_string(row) + "/" + std::to_string(level) + "/" + std::to_string(rarity);
}

class Poe2CasterGoldenTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayer = &Players[0];
		*MyPlayer = {};
		MyPlayer->poe2.clear();
		MyPlayer->_pClass = HeroClass::Warrior;
		gbIsSpawn = false;
		gbIsMultiplayer = false;
		gbIsHellfire = true;
		gbIsPoe2 = true;
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		LoadItemData();
		LoadSpellData();
		LoadPlayerDataFiles();
		LoadModifierData();
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		LoadModifierData();
		UnloadModArchives();
	}

	/** Compares with the table, or records for POE2_GOLDEN_OUT. */
	static void Check(std::string key, uint64_t hash)
	{
		Results()[key] = hash;
		if (Writing())
			return;
		const auto *it = std::find_if(std::begin(GoldenTable), std::end(GoldenTable), [&key](const GoldenEntry &entry) { return entry.key == key; });
		ASSERT_NE(it, std::end(GoldenTable)) << "no golden entry for " << key;
		EXPECT_EQ(it->hash, hash) << key << " rebuilds differently from the golden file";
	}

	static bool Writing()
	{
		return std::getenv("POE2_GOLDEN_OUT") != nullptr;
	}

	static std::map<std::string, uint64_t> &Results()
	{
		static std::map<std::string, uint64_t> results;
		return results;
	}

	static void WriteOut()
	{
		const char *path = std::getenv("POE2_GOLDEN_OUT");
		if (path == nullptr)
			return;
		// Every test of this run adds to Results(); keys none of them made keep their old value.
		std::map<std::string, uint64_t> all;
		for (const GoldenEntry &entry : GoldenTable) {
			if (!entry.key.empty())
				all[std::string(entry.key)] = entry.hash;
		}
		for (const auto &[k, h] : Results())
			all[k] = h;
		std::ofstream out(path);
		char buf[32];
		for (const auto &[k, h] : all) {
			snprintf(buf, sizeof(buf), "0x%016llXULL", static_cast<unsigned long long>(h));
			out << "{ \"" << k << "\", " << buf << " },\n";
		}
	}

	static Item Make(_item_indexes base, uint32_t seed, item_quality rarity, int itemLevel)
	{
		Item item = {};
		SetLootRarity(item, rarity);
		SetPoe2ItemLevel(item, itemLevel);
		SetRndSeed(seed);
		SetupAllItems(*MyPlayer, item, base, AdvanceRndSeed(), 30, 1, /*onlygood=*/true, /*pregen=*/false, 0, /*forceNotUnique=*/true);
		item._iIdentified = true;
		return item;
	}

	static Item Rebuilt(const Item &item)
	{
		Item rebuilt;
		RecreateItem(*MyPlayer, rebuilt, item.IDidx, item._iCreateInfo, item._iSeed, item._ivalue, item.dwBuff, item._iPoe2Craft);
		rebuilt._iIdentified = true;
		return rebuilt;
	}

	/** The item rows from before the rework; its bases come after them. */
	static constexpr size_t RowsBeforeTheRework = 951;

	/** Every staff and sceptre row of the bundle today, the Diablo staff rows and every quarterstaff. */
	static std::vector<_item_indexes> LegacyRows()
	{
		std::vector<_item_indexes> rows;
		for (size_t i = 0; i < std::min(AllItemsList.size(), RowsBeforeTheRework); i++) {
			const ItemData &data = AllItemsList[i];
			if (data.itype == ItemType::Staff || data.iSName == "Sceptre")
				rows.push_back(static_cast<_item_indexes>(i));
		}
		return rows;
	}
};

TEST_F(Poe2CasterGoldenTest, LegacyStavesSceptresAndQuarterstavesRerollIdentically)
{
	const std::vector<_item_indexes> rows = LegacyRows();
	ASSERT_GE(rows.size(), 60U);
	int items = 0;
	int withMods = 0;
	for (const _item_indexes row : rows) {
		for (const int itemLevel : { 1, 20, 45, 70, 82 }) {
			for (const item_quality rarity : { ITEM_QUALITY_MAGIC, ITEM_QUALITY_RARE }) {
				Fingerprint print;
				Fingerprint rebuiltPrint;
				for (uint32_t seed = 1; seed <= 300; seed++) {
					const Item item = Make(row, seed * 7919U, rarity, itemLevel);
					items++;
					withMods += item._iPoe2Mods[0].family != 0 ? 1 : 0;
					AddItem(print, item);
					AddItem(rebuiltPrint, Rebuilt(item));
				}
				EXPECT_EQ(print.value(), rebuiltPrint.value()) << row << " " << itemLevel;
				Check(StrCatKey("items", row, itemLevel, rarity), print.value());
			}
		}
	}
	// Most of them roll modifiers (the Diablo rows that don't drop have none).
	EXPECT_GT(withMods * 2, items) << withMods << " of " << items;
	WriteOut();
}

TEST_F(Poe2CasterGoldenTest, LegacyRareNamesStay)
{
	for (const _item_indexes row : LegacyRows()) {
		Fingerprint print;
		for (uint32_t seed = 1; seed <= 300; seed++) {
			const Item item = Make(row, seed * 104729U, ITEM_QUALITY_RARE, 45);
			print.add(std::string_view(item._iIName)).add(GetRareItemName(item));
		}
		Check(StrCatKey("names", row, 45, 0), print.value());
	}
	WriteOut();
}

TEST_F(Poe2CasterGoldenTest, CraftBytesStay)
{
	// A fixed set of crafted states: old families, a desecrated one and the unrevealed placeholder.
	Fingerprint print;
	const size_t families = std::min<size_t>(ModifierFamilies().size(), 94);
	for (uint32_t i = 0; i < 64; i++) {
		Poe2Craft craft {};
		craft.explicitMods = true;
		craft.rarity = ITEM_QUALITY_RARE;
		craft.rolledMagic = (i & 1) != 0;
		craft.sockets = static_cast<uint8_t>(i % 3);
		craft.desecrated = (i % 5) == 0;
		craft.enchant = static_cast<uint8_t>(i % 17);
		craft.magnitude = static_cast<int8_t>(i % 9);
		for (size_t m = 0; m < craft.mods.size(); m++) {
			Poe2CraftMod &mod = craft.mods[m];
			mod.family = static_cast<uint8_t>(1 + ((i * 13 + m * 29) % families));
			mod.tier = static_cast<uint8_t>(1 + ((i + m) % 8));
			mod.salt = static_cast<uint8_t>((i * 7 + m) % (MaxCraftSalt + 1));
		}
		if (i % 4 == 1)
			craft.mods[5] = { static_cast<uint8_t>(DesecratedFamilyBase + (i % 10)), 1, 0 };
		if (i % 4 == 2)
			craft.mods[4] = { UnrevealedModFamily, static_cast<uint8_t>(1 + (i % 2)), 0 };
		const auto bytes = PackCraftState(craft);
		for (const uint8_t b : bytes)
			print.add(b);
		EXPECT_EQ(UnpackCraftState(bytes), craft) << i;
	}
	Check("craft/bytes", print.value());
	WriteOut();
}

TEST_F(Poe2CasterGoldenTest, DesecrationOffersOnLegacyStavesAndSceptresStay)
{
	for (const _item_indexes row : LegacyRows()) {
		// The rows that dropped before the rework, its retired Focused and Quick staves and sceptres among them.
		const BaseMeta *base = BaseMetaFor(row);
		const bool retired = base != nullptr && (base->itemClass == "legacy_staff" || base->itemClass == "legacy_sceptre");
		if (AllItemsList[row].dropRate == 0 && !retired)
			continue;
		Fingerprint print;
		int desecrated = 0;
		for (uint32_t seed = 1; seed <= 300; seed++) {
			Item item = Make(row, seed * 6151U, ITEM_QUALITY_RARE, 80);
			if (!ApplyCurrencyOrb(CurrencyOrb::Desecrate, *MyPlayer, item))
				continue;
			desecrated++;
			print.add(seed);
			for (const uint8_t b : PackCraftState(item._iPoe2Craft))
				print.add(b);
			for (const bool kinship : { false, true }) {
				for (const Poe2CraftMod &offer : DesecrationOffers(item, kinship)) {
					// A desecrated modifier by its id, so the in-memory family numbers may move.
					const DesecratedMod *desecrated = GetDesecratedMod(offer.family);
					print.add(desecrated != nullptr ? 1000 + static_cast<int>(desecrated->id) : static_cast<int>(offer.family)).add(offer.tier).add(offer.salt);
				}
			}
		}
		EXPECT_GT(desecrated, 200) << row;
		Check(StrCatKey("desecration", row, 80, 0), print.value());
	}
	WriteOut();
}

TEST_F(Poe2CasterGoldenTest, DiabloStaffAndMaceUniquesRecreate)
{
	constexpr std::array<unique_base_item, 11> Kinds = { UITYPE_SHORTSTAFF, UITYPE_LONGSTAFF, UITYPE_COMPSTAFF, UITYPE_QUARSTAFF, UITYPE_WARSTAFF,
		UITYPE_MACE, UITYPE_MORNSTAR, UITYPE_SPIKCLUB, UITYPE_FLAIL, UITYPE_WARHAMMER, UITYPE_MAUL };
	int uniques = 0;
	for (const unique_base_item kind : Kinds) {
		// The home row: the first base of that kind that still drops.
		int home = -1;
		for (size_t i = 0; i < AllItemsList.size() && home < 0; i++) {
			if (AllItemsList[i].iItemId == kind && AllItemsList[i].dropRate > 0)
				home = static_cast<int>(i);
		}
		if (home < 0)
			continue;
		Fingerprint print;
		print.add(home);
		for (int level = 1; level <= 30; level++) {
			for (uint32_t seed = 1; seed <= 60; seed++) {
				Item item = {};
				SetRndSeed(seed * 2654435761U + static_cast<uint32_t>(level));
				SetupAllItems(*MyPlayer, item, static_cast<_item_indexes>(home), AdvanceRndSeed(), level, 15, /*onlygood=*/true, /*pregen=*/false);
				if (item._iMagical != ITEM_QUALITY_UNIQUE)
					continue;
				uniques++;
				print.add(level).add(seed).add(item._iUid).add(std::string_view(item._iIName));
				Item recreated;
				RecreateItem(*MyPlayer, recreated, item.IDidx, item._iCreateInfo, item._iSeed, item._ivalue, item.dwBuff, item._iPoe2Craft);
				EXPECT_EQ(recreated._iMagical, ITEM_QUALITY_UNIQUE) << home << " " << level << " " << seed;
				EXPECT_EQ(recreated._iUid, item._iUid) << home << " " << level << " " << seed;
			}
		}
		Check(StrCatKey("diablo_uniques", home, static_cast<int>(kind), 0), print.value());
	}
	EXPECT_GT(uniques, 100);
	WriteOut();
}

TEST_F(Poe2CasterGoldenTest, BundleUniquesOnOldRowsStay)
{
	Fingerprint print;
	for (const _item_indexes row : LegacyRows()) {
		print.add(static_cast<int>(row));
		for (const int uid : Poe2UniquesOnBase(row))
			print.add(uid).add(std::string_view(UniqueItems[uid].UIName));
	}
	Check("bundle_uniques", print.value());
	WriteOut();
}

} // namespace
} // namespace devilution
