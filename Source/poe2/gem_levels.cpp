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

/** @brief Attack gems: damage like POE2's attack gems; mana grows 5x, three quarters of POE2's ~6.7x, as Diablo's mana pools are smaller. */
constexpr int16_t AttackManaAt20 = 500;
/** @brief Spell gems: mana grows 5x, half of POE2's ~10x, as Diablo's spells are cast faster. */
constexpr int16_t SpellManaAt20 = 500;
/** @brief Spells without damage (Teleport, Stone Curse...) get dearer more slowly. */
constexpr int16_t UtilitySpellManaAt20 = 300;
/** @brief Battle Cry's empowered melee hits deal this much extra damage at gem level 1. */
constexpr int BattleCryDamagePercentAt1 = 50;

GemCurve AttackCurve(WeaponSkill skill)
{
	switch (skill) {
	case WeaponSkill::Cleave:
		return { 312, AttackManaAt20, 0, "Boneshatter (100% to 312%)" };
	case WeaponSkill::GroundSlam:
		return { 340, AttackManaAt20, 0, "Boneshatter, a little more for a slow slam" };
	case WeaponSkill::Barrage:
		return { 280, AttackManaAt20, 0, "Lightning Arrow, a little less as five arrows fly" };
	case WeaponSkill::WhirlingStaff:
		return { 300, AttackManaAt20, 0, "Boneshatter, a little less for hitting all around" };
	case WeaponSkill::Flurry:
		return { 290, AttackManaAt20, 0, "Boneshatter, a little less for a very fast double strike" };
	case WeaponSkill::LeapSlam:
		return { 408, AttackManaAt20, 0, "Leap Slam (110% to 449%)" };
	case WeaponSkill::RainOfArrows:
		return { 300, AttackManaAt20, 0, "Lightning Arrow, a little less for a whole volley" };
	case WeaponSkill::ShieldCharge:
		return { 340, AttackManaAt20, 0, "Leap Slam, a little less" };
	case WeaponSkill::BattleCry:
		return { 200, AttackManaAt20, 0, "POE2 warcries: the empower doubles from 50% to 100%" };
	case WeaponSkill::LightningArrow:
		return { 312, AttackManaAt20, 0, "Lightning Arrow (80% to 250%)" };
	case WeaponSkill::EscapeShot:
		return { 300, AttackManaAt20, 0, "Lightning Arrow, a little less for an escape" };
	case WeaponSkill::EmberFusillade:
		return { 250, AttackManaAt20, 5, "Spark (4 to 9 projectiles); Firebolt barely grows on its own" };
	case WeaponSkill::Spark:
		return { 200, AttackManaAt20, 5, "Spark (4 to 9 projectiles)" };
	default:
		return { 100, 100, 0, {} };
	}
}

GemCurve SpellCurve(SpellID spell)
{
	switch (spell) {
	// Diablo already adds 12.5% damage a spell level to these, like POE2's Fireball (about 13% a level).
	case SpellID::Flash:
	case SpellID::Fireball:
	case SpellID::Guardian:
	case SpellID::Nova:
	case SpellID::Immolation:
	case SpellID::Elemental:
	case SpellID::RuneOfFire:
	case SpellID::RuneOfNova:
	case SpellID::RuneOfImmolation:
		return { 100, SpellManaAt20, 0, "Fireball: Diablo's spell level already adds 12.5% a level" };
	case SpellID::Firebolt:
		return { 250, SpellManaAt20, 0, "Spark: Diablo adds only 1 damage a spell level" };
	case SpellID::ChargedBolt:
		return { 200, SpellManaAt20, 0, "Spark: Diablo already adds a bolt every 2 spell levels" };
	case SpellID::BloodStar:
		return { 250, SpellManaAt20, 0, "Fireball, less as Diablo adds 3 damage a spell level" };
	case SpellID::Lightning:
	case SpellID::ChainLightning:
	case SpellID::HolyBolt:
	case SpellID::FireWall:
	case SpellID::LightningWall:
	case SpellID::RingOfFire:
	case SpellID::FlameWave:
	case SpellID::Inferno:
	case SpellID::Apocalypse:
	case SpellID::RuneOfLight:
		return { 312, SpellManaAt20, 0, "Boneshatter's curve: Diablo doesn't grow these with spell level" };
	case SpellID::Healing:
	case SpellID::HealOther:
		return { 100, 100, 0, "Diablo's healing formula" };
	default:
		return { 100, UtilitySpellManaAt20, 0, "POE2 utility gems" };
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
		return { 100, 100, 0, "minions grow with the gem level" };
	}
}

int GemCurvePercent(int endAt20, int gemLevel)
{
	const int shape = GemCurveShape[std::clamp(gemLevel, 1, MaxGemLevel) - 1];
	// Rounded by hand: some toolchains (Amiga) have no std::lround.
	return static_cast<int>((100.0 * std::pow(endAt20 / 100.0, shape / 1000.0)) + 0.5);
}

int GemDamagePercent(SkillSlot skill, int gemLevel)
{
	return GemCurvePercent(GetGemCurve(skill).damageAt20, gemLevel);
}

int GemManaPercent(SkillSlot skill, int gemLevel)
{
	return GemCurvePercent(GetGemCurve(skill).manaAt20, gemLevel);
}

int GemExtraProjectiles(SkillSlot skill, int gemLevel)
{
	const int every = GetGemCurve(skill).projectileEvery;
	return every > 0 ? std::clamp(gemLevel, 1, MaxGemLevel) / every : 0;
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
	std::string text = std::format("{:s} (like {:s}):", SkillName(skill), curve.poe2Gem);
	for (int level = 1; level <= MaxGemLevel; level++)
		StrAppend(text, std::format("\n  {:s} (character level {:d})", GemLevelText(skill, level), CharacterLevelForGemLevel(level)));
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
