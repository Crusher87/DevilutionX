#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <magic_enum/magic_enum.hpp>

#include "data/file.hpp"
#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/defenses.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/unique_rules.hpp"
#include "spells.h"
#include "tables/misdat.h"
#include "tables/playerdat.hpp"
#include "utils/str_split.hpp"

namespace devilution {
namespace {

/** @brief Magic becomes cold (research/chaos-damage/addendum-cold-and-gems-v1.md, Part A). */
class Poe2ColdTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayer = &Players[0];
		*MyPlayer = {};
		MyPlayer->poe2.clear();
		gbIsSpawn = false;
		gbIsMultiplayer = false;
		gbIsHellfire = true;
		gbIsPoe2 = true;
		sgGameInitInfo.nDifficulty = DIFF_NORMAL;
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		LoadItemData();
		LoadSpellData();
		LoadMissileData();
		LoadPlayerDataFiles();
		LoadPassiveTreeData();
		LoadModifierData();
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		LoadPassiveTreeData();
		UnloadModArchives();
	}

	/** @brief Each row's values in these columns of a POE2 table, row by row in the columns' order. */
	static std::vector<std::string> Column(std::string_view filename, std::initializer_list<std::string_view> columns)
	{
		std::vector<std::string> values;
		auto dataFile = DataFile::load(filename);
		if (!dataFile.has_value()) {
			ADD_FAILURE() << "couldn't load " << filename;
			return values;
		}
		std::vector<std::string> header;
		bool first = true;
		for (DataFileRecord record : *dataFile) {
			std::vector<std::string> fields;
			for (DataFileField field : record)
				fields.emplace_back(field.value());
			if (first) {
				header = std::move(fields);
				first = false;
				continue;
			}
			for (const std::string_view column : columns) {
				size_t index = 0;
				while (index < header.size() && header[index] != column)
					index++;
				EXPECT_LT(index, header.size()) << column;
				values.push_back(index < fields.size() ? fields[index] : std::string());
			}
		}
		return values;
	}
};

TEST_F(Poe2ColdTest, OldMagicTokensStillRead)
{
	EXPECT_EQ(ParseModEffectName("MagicResistance"), ModEffect::ColdResistance);
	EXPECT_EQ(ParseModEffectName("ColdResistance"), ModEffect::ColdResistance);
	EXPECT_EQ(ParseModEffectName("FireResistance"), ModEffect::FireResistance);
	EXPECT_FALSE(ParseModEffectName("NoSuchEffect").has_value());
	EXPECT_EQ(ParseUniqueRuleName("MaxMagicResistance"), UniqueRule::MaxColdResistance);
	EXPECT_EQ(ParseUniqueRuleName("MaxColdResistance"), UniqueRule::MaxColdResistance);
	EXPECT_EQ(ParseMissileDataFlag("Magic"), MissileDataFlags::Cold);
	EXPECT_EQ(ParseMissileDataFlag("Cold"), MissileDataFlags::Cold);
	EXPECT_EQ(ParseMissileDataFlag("Acid"), MissileDataFlags::Chaos);
	// Cold is still Diablo's magic underneath: the value network messages and saves carry.
	EXPECT_EQ(static_cast<int>(DamageType::Cold), 3);
	EXPECT_EQ(static_cast<int>(MissileDataFlags::Cold), 3);
}

TEST_F(Poe2ColdTest, EveryModifierFamilyEffectReads)
{
	// A family whose effect doesn't read is quietly switched off, and rares that rolled it re-roll on load.
	const std::vector<std::string> rows = Column("txtdata\\poe2\\modifier_families.tsv", { "id", "effect" });
	ASSERT_FALSE(rows.empty());
	for (size_t i = 0; i + 1 < rows.size(); i += 2) {
		const std::string &effect = rows[i + 1];
		EXPECT_NE(effect, "MagicResistance") << rows[i];
		if (effect.empty() || effect == "None")
			continue;
		const std::optional<ModEffect> parsed = ParseModEffectName(effect);
		ASSERT_TRUE(parsed.has_value()) << rows[i] << " " << effect;
		EXPECT_NE(*parsed, ModEffect::None) << rows[i];
	}
	const std::optional<uint8_t> cold = FindModifierFamily("cold_resistance");
	ASSERT_TRUE(cold.has_value());
	const ModFamily &family = ModifierFamilies()[*cold];
	EXPECT_EQ(family.effect, ModEffect::ColdResistance);
	EXPECT_TRUE(family.enabled);
}

TEST_F(Poe2ColdTest, EveryImplicitAugmentAndUniqueTokenReads)
{
	for (const std::string &implicits : Column("txtdata\\poe2\\base_meta.tsv", { "implicits" })) {
		for (const std::string_view entry : SplitByChar(implicits, ';')) {
			if (entry.empty())
				continue;
			const std::string_view token = entry.substr(0, entry.find('|'));
			EXPECT_NE(token, "MagicResistance");
			EXPECT_TRUE(ParseModEffectName(token).has_value()) << entry;
		}
	}
	for (const std::string &effect : Column("txtdata\\poe2\\augments.tsv", { "weaponEffect", "staffEffect", "armourEffect" })) {
		if (effect.empty())
			continue;
		EXPECT_NE(effect, "MagicResistance");
		EXPECT_TRUE(ParseModEffectName(effect).has_value()) << effect;
	}
	for (const std::string &rule : Column("txtdata\\poe2\\augments.tsv", { "rule" })) {
		if (!rule.empty())
			EXPECT_TRUE(ParseUniqueRuleName(rule).has_value()) << rule;
	}
	for (const std::string &effects : Column("txtdata\\poe2\\uniques.tsv", { "effects" })) {
		for (const std::string_view entry : SplitByChar(effects, ';')) {
			if (entry.empty())
				continue;
			const std::string_view token = entry.substr(0, entry.find('|'));
			EXPECT_NE(token, "MaxMagicResistance");
			EXPECT_NE(token, "MagicResistance");
			EXPECT_TRUE(ParseModEffectName(token).has_value() || ParseUniqueRuleName(token).has_value()) << entry;
		}
	}
}

TEST_F(Poe2ColdTest, EveryPassiveStatReads)
{
	int coldNodes = 0;
	for (const std::string &stats : Column("txtdata\\poe2\\passive_tree.tsv", { "stats" })) {
		for (const std::string_view entry : SplitByChar(stats, ',')) {
			if (entry.empty())
				continue;
			const std::string_view name = entry.substr(0, entry.find('='));
			EXPECT_NE(name, "MagicResist");
			EXPECT_NE(name, "MaxMagicResist");
			EXPECT_TRUE(magic_enum::enum_cast<PassiveStat>(name).has_value()) << entry;
			if (name == "ColdResist" || name == "MaxColdResist")
				coldNodes++;
		}
	}
	// Warding Circle and Faith Ward.
	EXPECT_EQ(coldNodes, 2);
}

TEST_F(Poe2ColdTest, ColdResistanceIsTheSavedMagicResistance)
{
	Player &player = *MyPlayer;
	player._pMagResist = 37;
	EXPECT_EQ(Poe2ColdResistance(player), 37);
	EXPECT_EQ(Poe2PlayerData::ColdResistanceIndex, 2U);
}

TEST_F(Poe2ColdTest, ColdMissilesStillColdInEveryMode)
{
	EXPECT_EQ(GetMissileData(MissileID::BlueFlare).damageType(), DamageType::Cold);
	EXPECT_EQ(GetMissileData(MissileID::Firebolt).damageType(), DamageType::Fire);
	gbIsPoe2 = false;
	UnloadModArchives();
	LoadModArchives({ { "hf" } });
	LoadMissileData();
	EXPECT_EQ(GetMissileData(MissileID::BlueFlare).damageType(), DamageType::Cold);
	UnloadModArchives();
	gbIsHellfire = false;
	LoadMissileData();
	EXPECT_EQ(GetMissileData(MissileID::FlashBottom).damageType(), DamageType::Cold);
	gbIsHellfire = true;
}

} // namespace
} // namespace devilution
