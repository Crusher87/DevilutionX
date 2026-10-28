#include "poe2/spell_damage.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "data/file.hpp"
#include "data/record_reader.hpp"
#include "game_mode.hpp"
#include "monster.h"
#include "player.h"
#include "poe2/spell_base.hpp"
#include "utils/log.hpp"

namespace devilution {

namespace {

constexpr std::string_view Filename = "txtdata\\poe2\\spell_damage.tsv";

struct SpellDamageRow {
	bool isSpell = true;
	SpellID spell = SpellID::Invalid;
	WeaponSkill skill = WeaponSkill::None;
	int16_t percent = 100;
	int16_t hits = 1;
	int16_t splash = 0;
};

/** @brief A spell with no row: the plain spell base, one hit, no splash. */
constexpr SpellDamageRow MissingRow {};

std::vector<SpellDamageRow> Rows;
bool Loaded = false;

/** @brief The spell-like weapon skills the file can name (WeaponSkill has no name table outside skill_looks.cpp). */
WeaponSkill ParseSkill(std::string_view name)
{
	if (name == "Spark")
		return WeaponSkill::Spark;
	if (name == "EmberFusillade")
		return WeaponSkill::EmberFusillade;
	return WeaponSkill::None;
}

void Load()
{
	Rows.clear();
	auto dataFile = DataFile::load(Filename);
	if (!dataFile.has_value()) {
		// Not marked loaded: the mod's files may not be mounted yet, so try again next time.
		LogError("Couldn't load the spell damage: {}", Filename);
		return;
	}
	dataFile->skipHeaderOrDie(Filename);
	for (DataFileRecord record : *dataFile) {
		RecordReader reader { record, Filename };
		std::string kind;
		std::string name;
		SpellDamageRow row;
		reader.readString("kind", kind);
		reader.readString("name", name);
		reader.readInt("percent", row.percent);
		reader.readInt("hits", row.hits);
		reader.readInt("splash", row.splash);
		row.hits = std::max<int16_t>(row.hits, 1);
		if (kind == "attack") {
			row.isSpell = false;
			row.skill = ParseSkill(name);
			if (row.skill == WeaponSkill::None) {
				LogError("Unknown skill in {}: {}", Filename, name);
				continue;
			}
		} else {
			const std::expected<SpellID, std::string> spell = ParseSpellId(name);
			if (!spell.has_value()) {
				LogError("Unknown spell in {}: {}", Filename, name);
				continue;
			}
			row.spell = *spell;
		}
		Rows.push_back(row);
	}
	Loaded = true;
}

void EnsureLoaded()
{
	if (!Loaded && gbIsPoe2)
		Load();
}

/** @brief Runes deal their spell's damage, unless they have a row of their own (Rune of Fire, Curtis 2026-10-28). */
SpellID DamageSpell(SpellID spell)
{
	switch (spell) {
	case SpellID::RuneOfFire:
		return SpellID::Fireball;
	case SpellID::RuneOfNova:
		return SpellID::Nova;
	case SpellID::RuneOfImmolation:
		return SpellID::Immolation;
	default:
		return spell;
	}
}

const SpellDamageRow &FindSpellRow(SpellID spell)
{
	EnsureLoaded();
	for (const SpellID wanted : { spell, DamageSpell(spell) }) {
		for (const SpellDamageRow &row : Rows) {
			if (row.isSpell && row.spell == wanted)
				return row;
		}
	}
	return MissingRow;
}

const SpellDamageRow &FindSkillRow(WeaponSkill skill)
{
	EnsureLoaded();
	for (const SpellDamageRow &row : Rows) {
		if (!row.isSpell && row.skill == skill)
			return row;
	}
	return MissingRow;
}

int Scale(int base, const SpellDamageRow &row)
{
	return static_cast<int>(static_cast<int64_t>(base) * row.percent / 100);
}

} // namespace

bool UsesPoe2SpellRule(Missile &missile)
{
	return gbIsPoe2 && missile.sourceType() == MissileSource::Player;
}

int SpellDamagePercent(SpellID spell)
{
	return FindSpellRow(spell).percent;
}

int SpellDamageHits(SpellID spell)
{
	return FindSpellRow(spell).hits;
}

int SpellSplashPercent(SpellID spell)
{
	return FindSpellRow(spell).splash;
}

int SkillSpellDamagePercent(WeaponSkill skill)
{
	return FindSkillRow(skill).percent;
}

int RollPoe2SpellHit(const Player &player, SpellID spell, int spellLevel)
{
	const SpellDamageRow &row = FindSpellRow(spell);
	return std::max(Scale(RollSpellBase(player, spellLevel), row) / row.hits, 1);
}

int RollPoe2SkillSpellHit(const Player &player, WeaponSkill skill, int gemLevel)
{
	const SpellDamageRow &row = FindSkillRow(skill);
	return std::max(Scale(RollSpellBase(player, gemLevel), row) / row.hits, 1);
}

DamageRange Poe2SpellDamageRange(const Player &player, SpellID spell, int spellLevel)
{
	const SpellDamageRow &row = FindSpellRow(spell);
	return { std::max(Scale(SpellBaseMin(player, spellLevel), row), 1), std::max(Scale(SpellBaseMax(player, spellLevel), row), 1) };
}

int BoneSpiritDamage(const Monster &monster)
{
	// POE2 Mashup: a third of a boss's life broke bosses with 600%+ map life (Curtis 2026-10-23).
	if (gbIsPoe2 && (monster.isUnique() || monster.type().type == MT_DIABLO))
		return static_cast<int>(static_cast<int64_t>(monster.hitPoints.raw()) * BoneSpiritUniqueLifePercent / 100 / 64);
	return (monster.hitPoints / 3).whole();
}

void LoadSpellDamage()
{
	Rows.clear();
	Loaded = false;
	if (gbIsPoe2)
		Load();
}

} // namespace devilution
