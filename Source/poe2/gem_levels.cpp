#include "poe2/gem_levels.hpp"

#include <algorithm>
#include <cmath>
#include <format>

#include "missiles.h"
#include "player.h"
#include "poe2/skill_gems.hpp"
#include "poe2/weapon_skills.hpp"
#include "utils/is_of.hpp"
#include "utils/str_case.hpp"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

// POE2's gems, from poe2db.tw: damage % (or a spell's minimum plus maximum damage) and mana cost at levels 1 to 20.
constexpr Poe2GemTable Boneshatter { "Boneshatter",
	{ 100, 110, 121, 132, 142, 153, 164, 174, 182, 191, 201, 211, 222, 233, 244, 257, 269, 283, 297, 312 },
	{ 9, 10, 11, 13, 15, 17, 19, 21, 23, 26, 28, 31, 34, 37, 40, 44, 48, 52, 56, 60 } };
constexpr Poe2GemTable LeapSlam { "Leap Slam",
	{ 110, 121, 134, 147, 160, 173, 187, 200, 212, 226, 241, 257, 274, 293, 313, 336, 361, 387, 417, 449 },
	{ 14, 16, 19, 21, 24, 27, 30, 34, 37, 41, 45, 50, 54, 59, 65, 70, 76, 83, 90, 97 } };
constexpr Poe2GemTable LightningArrow { "Lightning Arrow",
	{ 80, 88, 97, 106, 114, 122, 131, 139, 146, 153, 161, 169, 177, 186, 196, 205, 216, 226, 238, 250 },
	{ 6, 7, 8, 9, 10, 11, 12, 14, 15, 17, 19, 21, 23, 25, 27, 29, 32, 35, 37, 40 } };
constexpr Poe2GemTable Earthquake { "Earthquake",
	{ 40, 44, 48, 53, 57, 61, 66, 69, 73, 77, 80, 84, 89, 93, 98, 103, 108, 113, 119, 125 },
	{ 8, 9, 11, 12, 14, 16, 17, 19, 22, 24, 26, 29, 32, 34, 38, 41, 44, 48, 52, 56 } };
constexpr Poe2GemTable RainOfArrows { "Rain of Arrows",
	{ 50, 55, 61, 66, 72, 77, 83, 88, 93, 99, 105, 111, 117, 125, 132, 141, 150, 159, 170, 181 },
	{ 15, 17, 20, 23, 26, 29, 32, 36, 40, 44, 48, 53, 58, 63, 69, 75, 81, 88, 95, 103 } };
constexpr Poe2GemTable Fireball { "Fireball",
	{ 20, 27, 37, 48, 60, 74, 89, 106, 125, 147, 170, 197, 227, 260, 297, 338, 385, 436, 495, 560 },
	{ 10, 12, 14, 16, 18, 21, 24, 28, 31, 35, 40, 45, 50, 56, 62, 69, 77, 85, 94, 104 } };
constexpr Poe2GemTable Spark { "Spark",
	{ 11, 14, 18, 22, 27, 34, 41, 49, 58, 67, 79, 91, 104, 120, 137, 156, 177, 201, 228, 258 },
	{ 5, 6, 7, 8, 10, 11, 13, 15, 17, 19, 21, 24, 27, 30, 33, 37, 41, 46, 51, 56 } };

/** @brief Mana grows 5x by level 20: three quarters of POE2's ~6.7x for attacks and half its ~10x for spells, as Diablo's mana pools are smaller. */
constexpr int16_t ManaAt20 = 500;
/** @brief Spells without damage (Teleport, Stone Curse...) get dearer more slowly. */
constexpr int16_t UtilitySpellManaAt20 = 300;
/** @brief Battle Cry's empowered melee hits deal this much extra damage at gem level 1. */
constexpr int BattleCryDamagePercentAt1 = 50;

GemCurve AttackCurve(WeaponSkill skill)
{
	switch (skill) {
	case WeaponSkill::Cleave:
		return { &Boneshatter, 0, ManaAt20, false, "Boneshatter, level for level" };
	case WeaponSkill::GroundSlam:
		return { &Earthquake, 0, ManaAt20, false, "Earthquake, level for level" };
	case WeaponSkill::Barrage:
		return { &LightningArrow, 280, ManaAt20, false, "Lightning Arrow's steps, a little less as five arrows fly" };
	case WeaponSkill::WhirlingStaff:
		return { &Boneshatter, 300, ManaAt20, false, "Boneshatter's steps, a little less for hitting all around" };
	case WeaponSkill::Flurry:
		return { &Boneshatter, 290, ManaAt20, false, "Boneshatter's steps, a little less for a very fast double strike" };
	case WeaponSkill::LeapSlam:
		return { &LeapSlam, 0, ManaAt20, false, "Leap Slam, level for level" };
	case WeaponSkill::RainOfArrows:
		return { &RainOfArrows, 0, ManaAt20, false, "Rain of Arrows, level for level" };
	case WeaponSkill::ShieldCharge:
		return { &LeapSlam, 340, ManaAt20, false, "Leap Slam's steps, a little less" };
	case WeaponSkill::BattleCry:
		return { &Boneshatter, 200, ManaAt20, false, "warcries: the empower doubles from 50% to 100% along Boneshatter's steps" };
	case WeaponSkill::LightningArrow:
		return { &LightningArrow, 0, ManaAt20, false, "Lightning Arrow, level for level" };
	case WeaponSkill::EscapeShot:
		return { &LightningArrow, 300, ManaAt20, false, "Lightning Arrow's steps, a little less for an escape" };
	case WeaponSkill::EmberFusillade:
		return { &Fireball, 250, ManaAt20, true, "Fireball's steps and Spark's projectiles; Diablo's Firebolt barely grows on its own" };
	case WeaponSkill::Spark:
		return { &Spark, 200, ManaAt20, true, "Spark's steps and projectiles, on top of Diablo's Charged Bolt" };
	default:
		return { &Boneshatter, 100, 100, false, {} };
	}
}

GemCurve SpellCurve(SpellID spell)
{
	switch (spell) {
	// Diablo already adds 12.5% damage a spell level to these.
	case SpellID::Flash:
	case SpellID::Fireball:
	case SpellID::Guardian:
	case SpellID::Nova:
	case SpellID::Immolation:
	case SpellID::Elemental:
	case SpellID::RuneOfFire:
	case SpellID::RuneOfNova:
	case SpellID::RuneOfImmolation:
		return { &Fireball, 100, ManaAt20, false, "Diablo's spell level already adds 12.5% a level; mana follows Fireball" };
	case SpellID::Firebolt:
		return { &Fireball, 250, ManaAt20, false, "Fireball's steps; Diablo adds only 1 damage a spell level" };
	case SpellID::ChargedBolt:
		return { &Spark, 200, ManaAt20, false, "Spark's steps; Diablo already adds a bolt every 2 spell levels" };
	case SpellID::BloodStar:
		return { &Fireball, 250, ManaAt20, false, "Fireball's steps; Diablo adds 3 damage a spell level" };
	case SpellID::Lightning:
	case SpellID::ChainLightning:
	case SpellID::HolyBolt:
	case SpellID::LightningWall:
	case SpellID::RuneOfLight:
		return { &Spark, 312, ManaAt20, false, "Spark's steps; Diablo doesn't grow these with spell level" };
	case SpellID::FireWall:
	case SpellID::RingOfFire:
	case SpellID::FlameWave:
	case SpellID::Inferno:
	case SpellID::Apocalypse:
		return { &Fireball, 312, ManaAt20, false, "Fireball's steps; Diablo doesn't grow these with spell level" };
	case SpellID::Healing:
	case SpellID::HealOther:
		return { &Fireball, 100, 100, false, "Diablo's healing formula" };
	default:
		return { &Fireball, 100, UtilitySpellManaAt20, false, "utility gem, mana follows Fireball" };
	}
}

int BaseManaCost(SkillSlot skill)
{
	switch (skill.kind) {
	case SkillKind::Attack:
		return WeaponSkillManaCost(skill.attack());
	case SkillKind::Spell:
		return GetSpellData(skill.spell()).sManaCost;
	default:
		return 0;
	}
}

std::string DamageText(SkillSlot skill, int gemLevel)
{
	const int percent = GemDamagePercent(skill, gemLevel);
	switch (skill.kind) {
	case SkillKind::Attack:
		if (skill.attack() == WeaponSkill::BattleCry)
			return std::format("+{:d}% melee damage", BattleCryDamagePercent(gemLevel));
		if (IsAnyOf(skill.attack(), WeaponSkill::EmberFusillade, WeaponSkill::Spark))
			return std::format("{:d}% spell damage", percent);
		return std::format("{:d}% weapon damage", percent);
	case SkillKind::Spell: {
		if (MyPlayer == nullptr)
			return std::format("{:d}% damage", percent);
		const DamageRange range = GetDamageAmt(skill.spell(), gemLevel);
		if (range.min < 0)
			return {};
		return std::format("{:d}-{:d} damage", range.min * percent / 100, range.max * percent / 100);
	}
	case SkillKind::Persistent:
		return std::format("minion level {:d}", gemLevel);
	default:
		return {};
	}
}

} // namespace

GemCurve GetGemCurve(SkillSlot skill)
{
	switch (skill.kind) {
	case SkillKind::Attack:
		return AttackCurve(skill.attack());
	case SkillKind::Spell:
		return SpellCurve(skill.spell());
	default:
		return { &Boneshatter, 100, 100, false, "minions grow with the gem level" };
	}
}

int GemCurvePercent(const std::array<int16_t, MaxGemLevel> &steps, int endAt20, int gemLevel)
{
	const int level = std::clamp(gemLevel, 1, MaxGemLevel);
	const double ratio = static_cast<double>(steps[level - 1]) / steps[0];
	double percent = 100.0 * ratio;
	if (endAt20 != 0) {
		// Keep the POE2 gem's steps but scale them so level 20 lands on endAt20.
		const double total = static_cast<double>(steps[MaxGemLevel - 1]) / steps[0];
		percent = 100.0 * std::pow(endAt20 / 100.0, std::log(ratio) / std::log(total));
	}
	// Rounded by hand: some toolchains (Amiga) have no std::lround.
	return static_cast<int>(percent + 0.5);
}

int GemDamagePercent(SkillSlot skill, int gemLevel)
{
	const GemCurve curve = GetGemCurve(skill);
	return GemCurvePercent(curve.table->damage, curve.damageAt20, gemLevel);
}

int GemManaPercent(SkillSlot skill, int gemLevel)
{
	const GemCurve curve = GetGemCurve(skill);
	return GemCurvePercent(curve.table->mana, curve.manaAt20, gemLevel);
}

int GemExtraProjectiles(SkillSlot skill, int gemLevel)
{
	if (!GetGemCurve(skill).sparkProjectiles)
		return 0;
	return static_cast<int>(std::count_if(SparkProjectileLevels.begin(), SparkProjectileLevels.end(), [gemLevel](uint8_t level) { return level <= gemLevel; }));
}

int GemBaseManaCost(SkillSlot skill, int gemLevel)
{
	return BaseManaCost(skill) * GemManaPercent(skill, gemLevel) / 100;
}

int BattleCryDamagePercent(int gemLevel)
{
	return BattleCryDamagePercentAt1 * GemDamagePercent(SkillSlot::ForAttack(WeaponSkill::BattleCry), gemLevel) / 100;
}

std::string GemLevelText(SkillSlot skill, int gemLevel)
{
	std::string text = std::format("level {:d}", gemLevel);
	const std::string damage = DamageText(skill, gemLevel);
	if (!damage.empty())
		StrAppend(text, ": ", damage);
	const int mana = GemBaseManaCost(skill, gemLevel);
	if (mana > 0)
		StrAppend(text, std::format(", {:d} mana", mana));
	const int projectiles = GemExtraProjectiles(skill, gemLevel);
	if (projectiles > 0)
		StrAppend(text, std::format(", +{:d} projectiles", projectiles));
	return text;
}

std::string GemCurveText(SkillSlot skill)
{
	const GemCurve curve = GetGemCurve(skill);
	std::string text = std::format("{:s} ({:s}):", SkillName(skill), curve.note);
	for (int level = 1; level <= MaxGemLevel; level++) {
		StrAppend(text, std::format("\n  {:s} (character level {:d})", GemLevelText(skill, level), CharacterLevelForGemLevel(level)));
		if (level > 1)
			StrAppend(text, " ", GemStepText(skill, level - 1, level));
	}
	return text;
}

std::string GemStepText(SkillSlot skill, int fromLevel, int toLevel)
{
	const int damageFrom = GemDamagePercent(skill, fromLevel);
	const int manaFrom = GemManaPercent(skill, fromLevel);
	const int damageStep = damageFrom > 0 ? ((GemDamagePercent(skill, toLevel) * 1000 / damageFrom) - 1000 + 5) / 10 : 0;
	const int manaStep = manaFrom > 0 ? ((GemManaPercent(skill, toLevel) * 1000 / manaFrom) - 1000 + 5) / 10 : 0;
	std::string text = std::format("[+{:d}% damage, +{:d}% mana", damageStep, manaStep);
	const int projectiles = GemExtraProjectiles(skill, toLevel) - GemExtraProjectiles(skill, fromLevel);
	if (projectiles > 0)
		StrAppend(text, std::format(", +{:d} projectile", projectiles));
	// A level that adds clearly more than the curve's average compounding step is one of the gem's payoff levels.
	const double average = (std::pow(GemDamagePercent(skill, MaxGemLevel) / 100.0, 1.0 / (MaxGemLevel - 1)) - 1.0) * 100.0;
	if (toLevel == fromLevel + 1 && ((average > 0 && damageStep > average * 1.15) || projectiles > 0))
		text += ", big step";
	text += ']';
	return text;
}

std::string GemCurveTextByName(std::string_view name)
{
	const std::string wanted = AsciiStrToLower(name);
	const auto matches = [&](SkillSlot skill) { return SkillGemItemIndex(skill).has_value() && AsciiStrToLower(SkillName(skill)) == wanted; };
	for (int i = 1; i <= static_cast<int>(WeaponSkill::Last); i++) {
		if (const SkillSlot skill = SkillSlot::ForAttack(static_cast<WeaponSkill>(i)); matches(skill))
			return GemCurveText(skill);
	}
	for (int i = 1; i <= static_cast<int>(SpellID::LAST); i++) {
		if (const SkillSlot skill = SkillSlot::ForSpell(static_cast<SpellID>(i)); matches(skill))
			return GemCurveText(skill);
	}
	for (int i = 1; i <= static_cast<int>(PersistentSkill::Last); i++) {
		if (const SkillSlot skill = SkillSlot::ForPersistent(static_cast<PersistentSkill>(i)); matches(skill))
			return GemCurveText(skill);
	}
	return StrCat("No skill gem is called ", name, ".");
}

std::string SocketedGemsText(const Player &player)
{
	std::string text;
	for (size_t socket = 0; socket < player.poe2.skillSlots.size(); socket++) {
		const SkillSlot skill = player.poe2.skillSlots[socket];
		if (skill.isEmpty())
			continue;
		const int level = player.poe2.gemLevel(socket);
		if (!text.empty())
			text += '\n';
		StrAppend(text, SkillName(skill), " ", GemLevelText(skill, level));
		if (level < MaxGemLevel) {
			StrAppend(text, std::format("; next {:s}, needs character level {:d}", GemLevelText(skill, level + 1), CharacterLevelForGemLevel(level + 1)));
		}
	}
	return text.empty() ? "no skill gems socketed" : text;
}

} // namespace devilution
