#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <magic_enum/magic_enum.hpp>

#include "data/file.hpp"
#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/defenses.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/monster_resistance.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/skill_details.hpp"
#include "poe2/support_table.hpp"
#include "poe2/unique_rules.hpp"
#include "spells.h"
#include "tables/itemdat.h"
#include "tables/misdat.h"
#include "tables/monstdat.h"
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
		LoadMonsterData();
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

	/** @brief Life the hero loses to a 100 point hit of this missile, from this monster (or a trap), with the missile's own type. */
	static int HitTaken(Player &player, MissileID missile, Monster *monster = nullptr)
	{
		MyPlayerId = 0;
		player._pMaxHP = player._pMaxHPBase = Fixed26_6::fromInt(1000);
		for (uint32_t seed = 1; seed <= 400; seed++) {
			player._pHitPoints = player._pHPBase = Fixed26_6::fromInt(1000);
			SetRndSeed(seed);
			bool blocked = false;
			if (PlayerMHit(player, monster, 0, 100 * 64, 100 * 64, missile, GetMissileData(missile).damageType(), true, DeathReason::MonsterOrTrap, &blocked))
				return 1000 - player._pHitPoints.whole();
		}
		ADD_FAILURE() << "never hit";
		return 0;
	}

	static Monster MakeMonster(_monster_id type, uint16_t resistance)
	{
		LevelMonsterTypes[0].type = type;
		Monster monster {};
		monster.levelType = 0;
		monster.uniqueType = UniqueMonsterType::None;
		monster.flags = 0;
		monster.rndItemSeed = 1;
		monster.maxHitPoints = Fixed26_6::fromInt(100);
		monster.hitPoints = monster.maxHitPoints;
		monster.resistance = resistance;
		return monster;
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

TEST_F(Poe2ColdTest, PlayersReadColdInPoe2Mode)
{
	Item item {};
	item._iPLMR = 20;
	EXPECT_EQ(PrintItemPower(IPL_MAGICRES, item).str(), "Resist Cold: +20%");
	EXPECT_EQ(ResistanceName(DamageType::Cold), "Cold");
	EXPECT_EQ(ResistanceMarksText(RESIST_MAGIC), "Resistances: Cold 20%");
	EXPECT_EQ(PassiveStatText(PassiveStat::ColdResist, 20), "+20% to Cold Resistance");
	EXPECT_EQ(PassiveStatText(PassiveStat::MaxColdResist, 2), "+2% to maximum Cold Resistance");
	// Maximum elemental resistance raises cold's maximum too.
	EXPECT_EQ(PassiveStatText(PassiveStat::MaxElementalResist, 1), "+1% to maximum Elemental Resistances");
	EXPECT_EQ(PassiveStatText(PassiveStat::ColdDamage, 10), "10% increased Cold Damage");
	const std::optional<uint8_t> cold = FindModifierFamily("cold_resistance");
	ASSERT_TRUE(cold.has_value());
	EXPECT_EQ(ModifierFamilies()[*cold].name, "Cold Resistance");
}

TEST_F(Poe2ColdTest, DiabloModeStillSaysMagic)
{
	gbIsPoe2 = false;
	Item item {};
	item._iPLMR = 20;
	EXPECT_EQ(PrintItemPower(IPL_MAGICRES, item).str(), "Resist Magic: +20%");
	EXPECT_EQ(ResistanceName(DamageType::Cold), "Magic");
	gbIsPoe2 = true;
}

TEST_F(Poe2ColdTest, SpellTooltipsNameTheTypeTheirHitsDeal)
{
	// The missile's type, not Diablo's spell school: Nova, Charged Bolt and Lightning hit as lightning.
	EXPECT_EQ(SpellDamageType(SpellID::Nova), DamageType::Lightning);
	EXPECT_EQ(SpellDamageType(SpellID::ChargedBolt), DamageType::Lightning);
	EXPECT_EQ(SpellDamageType(SpellID::Lightning), DamageType::Lightning);
	EXPECT_EQ(SpellDamageType(SpellID::Firebolt), DamageType::Fire);
	EXPECT_EQ(SpellDamageType(SpellID::FireWall), DamageType::Fire);
	EXPECT_EQ(SpellDamageType(SpellID::Inferno), DamageType::Fire);
	// Guardian shoots firebolts, and Apocalypse's booms carry its damage, not its control missile.
	EXPECT_EQ(SpellDamageType(SpellID::Guardian), DamageType::Fire);
	EXPECT_EQ(SpellDamageType(SpellID::Apocalypse), GetMissileData(MissileID::ApocalypseBoom).damageType());
	// A rune bursts into the missile it holds.
	EXPECT_EQ(SpellDamageType(SpellID::RuneOfFire), DamageType::Fire);
	EXPECT_EQ(SpellDamageType(SpellID::RuneOfNova), DamageType::Lightning);
	EXPECT_EQ(SpellDamageType(SpellID::RuneOfLight), DamageType::Lightning);
	EXPECT_EQ(SpellDamageType(SpellID::HolyBolt), GetMissileData(MissileID::HolyBolt).damageType());
	EXPECT_EQ(SpellDamageType(SpellID::BoneSpirit), GetMissileData(MissileID::BoneSpirit).damageType());
}

TEST_F(Poe2ColdTest, DiabloSpellsHitWithTheirOwnTypes)
{
	// Flash is lightning, Holy Bolt and Apocalypse fire, Blood Star physical and Bone Spirit chaos (Curtis 2026-10-31).
	EXPECT_EQ(GetMissileData(MissileID::FlashBottom).damageType(), DamageType::Lightning);
	EXPECT_EQ(GetMissileData(MissileID::FlashTop).damageType(), DamageType::Lightning);
	EXPECT_EQ(GetMissileData(MissileID::HolyBolt).damageType(), DamageType::Fire);
	EXPECT_EQ(GetMissileData(MissileID::ApocalypseBoom).damageType(), DamageType::Fire);
	EXPECT_EQ(GetMissileData(MissileID::BloodStar).damageType(), DamageType::Physical);
	EXPECT_EQ(GetMissileData(MissileID::BoneSpirit).damageType(), DamageType::Chaos);
	// Hellfire's Holy Bolt item effect stays physical.
	EXPECT_EQ(GetMissileData(MissileID::HolyBoltBow).damageType(), DamageType::Physical);
	for (const SpellID spell : { SpellID::Flash, SpellID::HolyBolt, SpellID::Apocalypse, SpellID::BloodStar, SpellID::BoneSpirit })
		EXPECT_NE(SpellDamageType(spell), DamageType::Cold) << static_cast<int>(spell);
	EXPECT_EQ(SpellDamageType(SpellID::Apocalypse), DamageType::Fire);

	// Each meets its own resistance on a marked monster.
	const Monster zombie = MakeMonster(MT_NZOMBIE, RESIST_FIRE | IMMUNE_LIGHTNING | IMMUNE_MAGIC);
	EXPECT_EQ(MonsterResistance(zombie, SpellDamageType(SpellID::HolyBolt)), ResistMarkPercent);
	EXPECT_EQ(MonsterResistance(zombie, SpellDamageType(SpellID::Flash)), ImmuneMarkPercent);
	EXPECT_EQ(MonsterResistance(zombie, SpellDamageType(SpellID::BloodStar)), 0);
	EXPECT_EQ(MonsterResistance(zombie, SpellDamageType(SpellID::BoneSpirit)), MonsterTypeChaosResistance(MT_NZOMBIE));

	// The tags follow, so supports of the type fit.
	EXPECT_NE(SkillTags(SkillSlot::ForSpell(SpellID::HolyBolt)) & ST_Fire, 0U);
	EXPECT_NE(SkillTags(SkillSlot::ForSpell(SpellID::BloodStar)) & ST_Physical, 0U);
	EXPECT_NE(SkillTags(SkillSlot::ForSpell(SpellID::BoneSpirit)) & ST_Chaos, 0U);
	EXPECT_EQ(SkillTags(SkillSlot::ForSpell(SpellID::BoneSpirit)) & ST_Frost, 0U);
}

TEST_F(Poe2ColdTest, HolyBoltStillOnlyHurtsTheUndead)
{
	const Monster zombie = MakeMonster(MT_NZOMBIE, 0);
	EXPECT_FALSE(zombie.isImmune(MissileID::HolyBolt, DamageType::Fire));
	const Monster toad = MakeMonster(MT_TOAD, 0);
	EXPECT_TRUE(toad.isImmune(MissileID::HolyBolt, DamageType::Fire));
}

TEST_F(Poe2ColdTest, FlashMeetsLightningResistance)
{
	// The Counselors' flash and the Sparkling Shrine's are the same missiles.
	Player &player = *MyPlayer;
	player._pLghtResist = 75;
	player._pMagResist = 0;
	EXPECT_EQ(HitTaken(player, MissileID::FlashBottom), 25);
	player._pLghtResist = 0;
	player._pMagResist = 75;
	EXPECT_EQ(HitTaken(player, MissileID::FlashTop), 100);
}

TEST_F(Poe2ColdTest, BloodStarMeetsArmourNotColdResistance)
{
	Player &player = *MyPlayer;
	player._pMagResist = 75;
	Monster succubus = MakeMonster(MT_SUCCUBUS, 0);
	succubus.ai = MonsterAIID::Succubus;
	EXPECT_EQ(MonsterHitDamageType(&succubus, MissileID::BloodStar, DamageType::Physical), DamageType::Physical);
	const int resisted = HitTaken(player, MissileID::BloodStar, &succubus);
	player._pMagResist = 0;
	EXPECT_GT(resisted, 0);
	EXPECT_EQ(HitTaken(player, MissileID::BloodStar, &succubus), resisted);
	// A Snow Witch's blue star stays cold.
	Monster witch = MakeMonster(MT_SNOWWICH, 0);
	witch.ai = MonsterAIID::Succubus;
	EXPECT_EQ(MonsterHitDamageType(&witch, MissileID::BloodStar, DamageType::Physical), DamageType::Cold);
	// The wand skills on the same missile keep their own chaos.
	Missile missile {};
	missile._mitype = MissileID::BloodStar;
	missile.poe2.damageType = static_cast<int8_t>(DamageType::Chaos);
	EXPECT_EQ(missile.poe2.damageType >= 0 ? static_cast<DamageType>(missile.poe2.damageType) : GetMissileData(missile._mitype).damageType(), DamageType::Chaos);
}

TEST_F(Poe2ColdTest, DiabloModeSpellsKeepTheirTypes)
{
	gbIsPoe2 = false;
	UnloadModArchives();
	LoadModArchives({ { "hf" } });
	LoadMissileData();
	EXPECT_EQ(GetMissileData(MissileID::FlashBottom).damageType(), DamageType::Cold);
	EXPECT_EQ(GetMissileData(MissileID::HolyBolt).damageType(), DamageType::Physical);
	EXPECT_EQ(GetMissileData(MissileID::BloodStar).damageType(), DamageType::Cold);
	EXPECT_EQ(GetMissileData(MissileID::BoneSpirit).damageType(), DamageType::Cold);
	EXPECT_EQ(GetMissileData(MissileID::ApocalypseBoom).damageType(), DamageType::Physical);
	Monster witch = MakeMonster(MT_SNOWWICH, 0);
	EXPECT_EQ(MonsterHitDamageType(&witch, MissileID::BloodStar, DamageType::Cold), DamageType::Cold);
}

} // namespace
} // namespace devilution
