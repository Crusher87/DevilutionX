#include "poe2/skill_details.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <optional>
#include <string_view>

#include "items.h"
#include "missiles.h"
#include "player.h"
#include "poe2/default_attack.hpp"
#include "poe2/gem_levels.hpp"
#include "poe2/meta_gems.hpp"
#include "poe2/minion_gems.hpp"
#include "poe2/player_data.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/skill_resources.hpp"
#include "poe2/spell_base.hpp"
#include "poe2/spell_damage.hpp"
#include "poe2/spirit.hpp"
#include "poe2/spirit_auras.hpp"
#include "poe2/spirit_buffs.hpp"
#include "poe2/spirit_uses.hpp"
#include "poe2/support_gems.hpp"
#include "poe2/support_table.hpp"
#include "poe2/table_skills.hpp"
#include "poe2/unique_effects.hpp"
#include "poe2/weapon_skills.hpp"
#include "tables/misdat.h"
#include "tables/playerdat.hpp"
#include "tables/spelldat.h"
#include "utils/is_of.hpp"

namespace devilution {

namespace {

/** Game ticks in one second at normal speed. */
constexpr int TicksPerSecond = 20;

struct HitPart {
	DamageType type;
	int min;
	int max;
};

struct HitEstimate {
	std::vector<HitPart> parts;
	/** Shown instead of damage for skills that don't hit, like curses and buffs. */
	std::string effect;
};

std::string_view DamageTypeName(DamageType type)
{
	switch (type) {
	case DamageType::Fire:
		return "Fire";
	case DamageType::Lightning:
		return "Lightning";
	case DamageType::Magic:
		return "Cold (magic)";
	case DamageType::Acid:
		return "Chaos";
	default:
		return "Physical";
	}
}

DamageType SpellDamageType(SpellID spell)
{
	switch (GetSpellData(spell).type()) {
	case MagicType::Fire:
		return DamageType::Fire;
	case MagicType::Lightning:
		return DamageType::Lightning;
	default:
		return DamageType::Magic;
	}
}

int Scale(int value, int percent)
{
	return static_cast<int>(static_cast<int64_t>(value) * percent / 100);
}

/**
 * @brief A weapon hit before the skill: the weapon's damage with the hero's bonuses, as the character panel shows it.
 * Bare fists hit at the skill's gem level, the one shown.
 */
std::pair<int, int> WeaponHit(const Player &player, SkillSlot skill, int level)
{
	int damageMod = player._pIBonusDamMod;
	if (player.InvBody[INVLOC_HAND_LEFT]._itype == ItemType::Bow && player._pClass != HeroClass::Rogue)
		damageMod += player._pDamageMod / 2;
	else
		damageMod += player._pDamageMod;
	std::pair<int, int> weapon = AttackWeaponDamage(player, skill);
	if (skill.kind == SkillKind::Attack && level > 0 && FightsWithBareFists(player))
		weapon = BareFistDamage(level);
	const auto [weaponMin, weaponMax] = weapon;
	const int min = weaponMin + (player._pIBonusDam * weaponMin / 100) + damageMod;
	const int max = weaponMax + (player._pIBonusDam * weaponMax / 100) + damageMod;
	return { std::max(min, 1), std::max(max, std::max(min, 1)) };
}

/** @brief A weapon hit scaled by the skill, with the weapon's own fire and lightning added. */
void AddWeaponHit(HitEstimate &hit, const Player &player, SkillSlot skill, int level, int percent)
{
	const auto [min, max] = WeaponHit(player, skill, level);
	hit.parts.push_back({ DamageType::Physical, Scale(min, percent), Scale(max, percent) });
	if (player._pIFMaxDam > 0)
		hit.parts.push_back({ DamageType::Fire, Scale(player._pIFMinDam, percent), Scale(player._pIFMaxDam, percent) });
	if (player._pILMaxDam > 0)
		hit.parts.push_back({ DamageType::Lightning, Scale(player._pILMinDam, percent), Scale(player._pILMaxDam, percent) });
}

/** @brief The base of a spell-like skill, from Magic and the gem level (Ice Bolt, table spells). */
void AddSpellBase(HitEstimate &hit, const Player &player, int gemLevel, DamageType type, int percent)
{
	hit.parts.push_back({ type, Scale(SpellBaseMin(player, gemLevel), percent), Scale(SpellBaseMax(player, gemLevel), percent) });
}

HitEstimate EstimateHit(const Player &player, SkillSlot skill, int level)
{
	HitEstimate hit;
	switch (skill.kind) {
	case SkillKind::Spell: {
		const SpellID spell = skill.spell();
		const DamageRange range = GetDamageAmt(spell, player.GetSpellLevel(spell));
		if (range.min < 0)
			return hit;
		const int percent = SupportSpellDamagePercent(player, spell);
		hit.parts.push_back({ SpellDamageType(spell), Scale(range.min, percent), Scale(range.max, percent) });
		return hit;
	}
	case SkillKind::Persistent:
		if ((SkillTags(skill) & ST_Minion) != 0)
			hit.effect = std::format("Minions at level {:d}", level);
		return hit;
	case SkillKind::Attack:
		break;
	case SkillKind::Default:
		if (static_cast<DefaultAttack>(skill.id) == DefaultAttack::StaffBolt) {
			// RollStaffBoltDamage: the staff's damage with its bonus, cut down, plus a little of the hero's Magic.
			const int magic = player._pMagic * StaffBoltMagicPercent / 100;
			const int min = player._pIMinDam + (player._pIMinDam * player._pIBonusDam / 100);
			const int max = player._pIMaxDam + (player._pIMaxDam * player._pIBonusDam / 100);
			hit.parts.push_back({ DamageType::Fire, std::max(1, Scale(min, StaffBoltDamagePercent) + magic), std::max(1, Scale(max, StaffBoltDamagePercent) + magic) });
		} else {
			AddWeaponHit(hit, player, skill, level, DefaultAttackDamagePercent);
		}
		return hit;
	default:
		return hit;
	}
	const WeaponSkill attack = skill.attack();
	const int percent = GemDamagePercent(skill, level);
	switch (attack) {
	case WeaponSkill::BattleCry:
		hit.effect = std::format("+{:d}% melee damage for a while", BattleCryDamagePercent(level));
		return hit;
	case WeaponSkill::AmplifyDamage:
		hit.effect = std::format("Enemies take +{:d}% physical damage", AmplifyDamagePercent + (AmplifyDamagePercentPerLevel * (level - 1)));
		return hit;
	case WeaponSkill::IceBolt:
		AddSpellBase(hit, player, level, DamageType::Magic, percent);
		return hit;
	case WeaponSkill::FrostNova:
		AddSpellBase(hit, player, level, DamageType::Magic, Scale(percent, FrostNovaShardDamagePercent));
		return hit;
	case WeaponSkill::LeapSlam:
		AddWeaponHit(hit, player, skill, level, Scale(percent, LeapSlamDamagePercent));
		return hit;
	case WeaponSkill::EmberFusillade:
		// Each ember and spark rolls the spell base times its own row (poe2/spell_damage.hpp), then the gem's curve.
		AddSpellBase(hit, player, level, DamageType::Fire, Scale(percent, SkillSpellDamagePercent(attack)));
		return hit;
	case WeaponSkill::Spark:
		AddSpellBase(hit, player, level, DamageType::Lightning, Scale(percent, SkillSpellDamagePercent(attack)));
		return hit;
	default:
		break;
	}
	if (const TableSkillData *table = FindTableSkill(attack); table != nullptr) {
		if (table->buff == TableBuff::WarCry) {
			hit.effect = std::format("Stuns enemies within {:d} tiles", table->radius);
			return hit;
		}
		if (table->shape == TableShape::Buff) {
			hit.effect = std::format("Buff for {:d} seconds", table->duration / TicksPerSecond);
			return hit;
		}
		if (table->shape == TableShape::Curse && table->curse != TableCurse::Haunt && table->curse != TableCurse::Locust) {
			hit.effect = std::format("Curse for {:d} seconds", table->duration / TicksPerSecond);
			return hit;
		}
		const int tablePercent = percent * table->damagePercent / 100;
		if ((table->flags & TF_Spell) != 0) {
			AddSpellBase(hit, player, level, table->type, tablePercent);
		} else {
			AddWeaponHit(hit, player, skill, level, tablePercent);
			if (table->type != DamageType::Physical && !hit.parts.empty())
				hit.parts.front().type = table->type;
		}
		return hit;
	}
	AddWeaponHit(hit, player, skill, level, percent);
	return hit;
}

/** @brief Frames of the skill's animation, from the class's animation data (the hero's own aren't set in town). */
int AttackFrames(const Player &player, SkillSlot skill)
{
	const PlayerAnimData &data = GetPlayerAnimDataForClass(player._pClass);
	if (skill.kind == SkillKind::Attack) {
		const TableSkillData *table = FindTableSkill(skill.attack());
		if ((table != nullptr && table->graphic != 0) || IsAnyOf(skill.attack(), WeaponSkill::IceBolt, WeaponSkill::Spark, WeaponSkill::EmberFusillade, WeaponSkill::FrostNova))
			return data.castingFrames;
	}
	switch (static_cast<PlayerWeaponGraphic>(player._pgfxnum & 0xFU)) {
	case PlayerWeaponGraphic::UnarmedShield:
		return data.unarmedShieldFrames;
	case PlayerWeaponGraphic::Sword:
		return data.swordFrames;
	case PlayerWeaponGraphic::SwordShield:
		return data.swordShieldFrames;
	case PlayerWeaponGraphic::Bow:
		return data.bowFrames;
	case PlayerWeaponGraphic::Axe:
		return data.axeFrames;
	case PlayerWeaponGraphic::Mace:
		return data.maceFrames;
	case PlayerWeaponGraphic::MaceShield:
		return data.maceShieldFrames;
	case PlayerWeaponGraphic::Staff:
		return data.staffFrames;
	default:
		return data.unarmedFrames;
	}
}

/** @brief Seconds one use takes, from the animation and what speeds it up. */
double UseSeconds(const Player &player, SkillSlot skill)
{
	if (skill.kind == SkillKind::Spell) {
		const SpellID spell = skill.spell();
		const int frames = std::max<int>(GetPlayerAnimDataForClass(player._pClass).castingFrames, 1);
		const int skipped = SupportCastSkippedFrames(player, spell) + player.poe2.passiveCastFrames;
		const int recovery = SupportCastRecoveryTicks(player, spell, frames);
		return std::max(frames - skipped + recovery, 2) / static_cast<double>(TicksPerSecond);
	}
	int skipped = 0;
	const ItemSpecialEffect flags = player._pIFlags;
	if (HasAnyOf(flags, ItemSpecialEffect::FastestAttack))
		skipped = 4;
	else if (HasAnyOf(flags, ItemSpecialEffect::FasterAttack))
		skipped = 3;
	else if (HasAnyOf(flags, ItemSpecialEffect::FastAttack))
		skipped = 2;
	else if (HasAnyOf(flags, ItemSpecialEffect::QuickAttack))
		skipped = 1;
	skipped += player.poe2.passiveAttackFrames + UniqueAttackSpeedPercent(player) / 10;
	if (skill.kind == SkillKind::Attack)
		skipped += SupportAttackSkippedFrames(player, skill.attack());
	else if (skill.kind == SkillKind::Default)
		skipped += SupportAttackSkippedFrames(player, WeaponSkill::None);
	const int frames = AttackFrames(player, skill);
	return std::max(frames - skipped, 2) / static_cast<double>(TicksPerSecond);
}

std::string_view SpellDescription(SpellID spell)
{
	switch (spell) {
	case SpellID::Firebolt:
		return "Hurls a fast bolt of fire at the target";
	case SpellID::Fireball:
		return "Hurls a ball of fire that explodes on impact, burning everything close by";
	case SpellID::ChargedBolt:
		return "Releases crackling sparks that wander erratically towards the target";
	case SpellID::Lightning:
		return "A bolt of lightning that strikes everything in a line";
	case SpellID::ChainLightning:
		return "Lightning that leaps from enemy to enemy";
	case SpellID::HolyBolt:
		return "A bolt of holy light that is deadly to the undead";
	case SpellID::FireWall:
		return "Raises a wall of fire that burns anything walking through it";
	case SpellID::RingOfFire:
		return "Surrounds you with a ring of fire";
	case SpellID::FlameWave:
		return "Sends a wave of fire rolling out across the ground";
	case SpellID::Inferno:
		return "Breathes a stream of fire at close range";
	case SpellID::Nova:
		return "A ring of lightning bursts out in every direction";
	case SpellID::Guardian:
		return "Summons a three-headed guardian that spits fire at enemies";
	case SpellID::BloodStar:
		return "A star of blood that pays for itself with life when mana runs low";
	case SpellID::Elemental:
		return "A fire elemental that seeks out the nearest enemy and explodes";
	case SpellID::Apocalypse:
		return "Fire rains on every enemy in sight";
	case SpellID::StoneCurse:
		return "Turns an enemy to stone for a while";
	case SpellID::Healing:
		return "Restores some of your life";
	case SpellID::Teleport:
		return "Moves you instantly to the target";
	case SpellID::ManaShield:
		return "Damage taken drains mana instead of life";
	default:
		return {};
	}
}

std::string_view SkillDescription(SkillSlot skill)
{
	switch (skill.kind) {
	case SkillKind::Spell:
		return SpellDescription(skill.spell());
	case SkillKind::Attack:
		return WeaponSkillDescription(skill.attack());
	case SkillKind::Persistent:
		return PersistentSkillDescription(skill.persistent());
	case SkillKind::Default:
		return IsRangedDefaultAttack(static_cast<DefaultAttack>(skill.id)) ? "Your weapon's own ranged attack. It costs no mana, so it hits softer than a skill gem's attack"
		                                                                  : "Your weapon's own attack. It costs no mana, so it hits softer than a skill gem's attack";
	default:
		return {};
	}
}

/** @brief Plain words on the ailments the skill's damage types can cause. */
std::vector<std::string> AilmentLines(const HitEstimate &hit, uint32_t tags)
{
	std::vector<std::string> lines;
	bool fire = (tags & ST_Fire) != 0;
	bool lightning = (tags & ST_Lightning) != 0;
	bool cold = (tags & ST_Frost) != 0;
	bool physical = (tags & ST_Physical) != 0;
	for (const HitPart &part : hit.parts) {
		fire = fire || part.type == DamageType::Fire;
		lightning = lightning || part.type == DamageType::Lightning;
		cold = cold || part.type == DamageType::Magic;
		physical = physical || part.type == DamageType::Physical;
	}
	if (fire)
		lines.emplace_back("Fire hits build up Ignite (burns over time)");
	if (cold)
		lines.emplace_back("Cold hits Chill (slow), big ones Freeze");
	if (lightning)
		lines.emplace_back("Lightning hits Shock (more damage taken)");
	if (physical)
		lines.emplace_back("Physical hits build up Stun");
	return lines;
}

void Heading(std::vector<SkillDetailLine> &lines, std::string label)
{
	lines.push_back({ SkillDetailLine::Kind::Heading, std::move(label), {} });
}

void Value(std::vector<SkillDetailLine> &lines, std::string label, std::string value)
{
	lines.push_back({ SkillDetailLine::Kind::Value, std::move(label), std::move(value) });
}

void Text(std::vector<SkillDetailLine> &lines, SkillDetailLine::Kind kind, std::string text)
{
	lines.push_back({ kind, std::move(text), {} });
}

std::string Seconds(double seconds)
{
	return std::format("{:.2f}s", seconds);
}

/** @brief The numbers of POE2's buff Spirit gems at the gem's level (poe2/spirit_buffs.hpp). */
/** @brief How a skeleton gem's minions fight, against a Skeletal Warrior. */
void MinionGemDetails(std::vector<SkillDetailLine> &lines, const MinionGemData &gem)
{
	if (gem.skill == PersistentSkill::SkeletalWarriors || gem.skill == PersistentSkill::LingeringIllusion)
		return;
	Value(lines, "Life", std::format("{:d}% of a Skeleton Warrior's", gem.lifePercent));
	if (gem.damagePercent > 0)
		Value(lines, "Blows", std::format("{:d}% of a Skeleton Warrior's", gem.damagePercent));
	if (gem.shot != MinionShot::None) {
		const std::string_view shot = gem.shot == MinionShot::Arrow ? "Arrows" : gem.shot == MinionShot::FrostBolt ? "Frost bolts" : gem.shot == MinionShot::ChargedBolts ? "Two charged bolts" : "Firebolts";
		Value(lines, std::string(shot), std::format("{:d}% of a blow each, from up to {:d} tiles", gem.shotPercent, gem.range));
	}
	if (gem.attackTicks > 0)
		Value(lines, "Attacks every", Seconds(gem.attackTicks / 20.0));
}

void SpiritBuffDetails(std::vector<SkillDetailLine> &lines, PersistentSkill skill, int level)
{
	const auto ticks = [](int gameTicks) { return Seconds(gameTicks / 20.0); };
	if (const MinionGemData *gem = FindMinionGem(skill); gem != nullptr)
		MinionGemDetails(lines, *gem);
	switch (skill) {
	case PersistentSkill::WindDancer:
		Value(lines, "A stage every", ticks(WindDancerStageTicks(level)));
		Value(lines, "Each stage", std::format("{:d}% more evasion, up to {:d}", WindDancerEvasionPerStage, WindDancerMaxStages));
		Value(lines, "Gale Force", std::format("{:d}% of the weapon's damage, {:d}% more per stage", GaleForceDamagePercent(level), GaleForceDamagePerStage));
		break;
	case PersistentSkill::GhostDance:
		Value(lines, "A shroud every", ticks(GhostShroudTicks(level)));
		Value(lines, "Losing one", std::format("{:d}% of evasion as energy shield per second, for {:d} seconds", GhostDanceRegenPercent, GhostDanceRegenTicks / 20));
		break;
	case PersistentSkill::Barkskin:
		Value(lines, "Armour", std::format("{:d}% of the energy shield lost, for {:d} seconds", BarkskinArmourPercent(level), BarkskinTicks / 20));
		break;
	case PersistentSkill::ArcticArmour:
		Value(lines, "A stage every", ticks(ArcticArmourStageTicks(level)));
		Value(lines, "Stages", std::format("up to {:d}", ArcticArmourMaxStages(level)));
		break;
	case PersistentSkill::HeraldOfPlague:
		Value(lines, "Spreads to", std::format("{:d} tiles around", HeraldOfPlagueRadius));
		Value(lines, "Hinder chance", std::format("{:d}% for {:d} seconds", HeraldOfPlagueHinderChance(level), HeraldOfPlagueHinderTicks / 20));
		break;
	case PersistentSkill::CombatFrenzy:
		Value(lines, "At most every", ticks(CombatFrenzyCooldownTicks(level)));
		break;
	case PersistentSkill::EternalRage:
		Value(lines, "Rage per second", std::format("{:.2f}", EternalRageCentiPerSecond(level) / 100.0));
		break;
	case PersistentSkill::ElementalConflux:
		Value(lines, "More damage", std::format("{:d}%, the element changes every {:d} seconds", ConfluxDamagePercent(level), ConfluxTicks / 20));
		break;
	case PersistentSkill::AlchemistsBoon:
		Value(lines, "Flask charges", std::format("{:.2f} per second", AlchemistChargesMilliPerSecond(level) / 1000.0));
		Value(lines, "Minions heal", std::format("{:d}% of a life flask's recovery", AlchemistMinionHealPercent(level)));
		break;
	case PersistentSkill::OverwhelmingPresence:
		Value(lines, "Stun threshold", std::format("{:d}% lower", PresenceStunThresholdPercent(level)));
		Value(lines, "Ailment threshold", std::format("{:d}% lower", PresenceAilmentThresholdPercent(level)));
		Value(lines, "Reaches", std::format("{:d} tiles around you", PresenceTiles));
		break;
	case PersistentSkill::TrailOfCaltrops:
		Value(lines, "Caltrop damage", std::format("{:d}% of the weapon's", CaltropDamagePercent(level)));
		Value(lines, "Caltrops last", std::format("{:d} seconds, up to {:d}", CaltropTicks / 20, MaxCaltrops));
		break;
	case PersistentSkill::ManaRemnants:
		Value(lines, "Mana each", std::format("{:d}", ManaRemnantMana(level)));
		Value(lines, "From kills", std::format("{:d}% chance", ManaRemnantDropPercent));
		break;
	case PersistentSkill::Attrition:
		Value(lines, "More damage", std::format("1% each {:d} seconds near you, up to {:d}%", AttritionTicksPerPercent / 20, AttritionMaxPercent(level)));
		Value(lines, "Cull after", std::format("{:d} seconds, rares at {:d}%, uniques at {:d}%", AttritionCullTicks(level) / 20, AttritionCullRarePercent, AttritionCullUniquePercent));
		break;
	case PersistentSkill::Blink:
		Value(lines, "Distance", std::format("{:d} tiles", BlinkTiles(level)));
		Value(lines, "Cooldown", ticks(BlinkCooldownTicks(level)));
		break;
	case PersistentSkill::IronWard:
		Value(lines, "Stores", std::format("{:d}% of the damage armour stops", IronWardStorePercent(level)));
		Value(lines, "Holds up to", std::format("{:d}% of your maximum life", SpiritStoreCapPercent(level)));
		Value(lines, "Nova", std::format("{:d} to {:d} tiles, {:d}% more stun", IronWardMinRadius, IronWardMaxRadius, IronWardStunPercent - 100));
		break;
	case PersistentSkill::PlagueBearer:
		Value(lines, "Stores", std::format("{:d}% of your poison damage", PlagueBearerStorePercent));
		Value(lines, "Holds up to", std::format("{:d}% of your maximum life", SpiritStoreCapPercent(level)));
		Value(lines, "Nova", std::format("{:d} tiles, always poisons", PlagueBearerRadius));
		break;
	case PersistentSkill::Convalescence:
		Value(lines, "Mana", std::format("{:d}", ConvalescenceManaCost));
		Value(lines, "Can't be stopped for", ticks(ConvalescenceTicks(level)));
		Value(lines, "Cooldown", ticks(ConvalescenceCooldownTicks));
		break;
	case PersistentSkill::GrimFeast:
		Value(lines, "Remnant", std::format("{:d}% of the skeleton's life, for {:d} seconds", GrimFeastRemnantPercent, GrimFeastRemnantTicks / 20));
		Value(lines, "Holds up to", std::format("{:d}% of your toughest skeleton's life", GrimFeastCapPercent(level)));
		break;
	case PersistentSkill::WarBanner:
	case PersistentSkill::DefianceBanner:
	case PersistentSkill::DreadBanner:
		Value(lines, "Glory to plant", std::format("{:d}", GloryForBanner));
		Value(lines, "Banner lasts", ticks(BannerTicks(level)));
		Value(lines, "Area", std::format("{:d} tiles", BannerTiles));
		if (skill == PersistentSkill::WarBanner)
			Value(lines, "In its area", std::format("{:d}% more attack damage, {:d}% more accuracy, {:d}% faster attacks", WarBannerDamagePercent, WarBannerAccuracyPercent, WarBannerAttackSpeedPercent));
		else if (skill == PersistentSkill::DefianceBanner)
			Value(lines, "In its area", std::format("{:d}% more armour and evasion, {:d}% faster movement", DefianceBannerDefencePercent, DefianceBannerMovementPercent));
		else
			Value(lines, "In its area", std::format("+{:d}% maximum resistances, half a flask charge a second", DreadBannerMaxResistance));
		break;
	case PersistentSkill::Briarpatch:
		Value(lines, "Stores", std::format("{:d}% of your attacks' physical damage", BriarpatchStorePercent));
		Value(lines, "Holds up to", std::format("{:d}% of your maximum life", BriarpatchCapPercent(level)));
		Value(lines, "Thorny ground", std::format("{:d} tiles for {:d} seconds, at most every {:d} seconds", BriarpatchRadius, BriarpatchTicks / 20, BriarpatchCooldownTicks / 20));
		break;
	case PersistentSkill::MagmaBarrier:
		Value(lines, "Block chance", std::format("{:d}% increased, with a shield", MagmaBarrierBlockPercent));
		Value(lines, "Charges in", ticks(MagmaBarrierChargeTicks(level)));
		Value(lines, "Fire spray", std::format("{:d} plus {:d}% of the shield's armour, {:d} tiles ahead", MagmaBarrierDamage(level), MagmaBarrierArmourPercent, MagmaBarrierTiles));
		break;
	case PersistentSkill::ScavengedPlating:
		Value(lines, "Each plating", std::format("{:d}% more armour, {:d} thorns damage", ScavengedPlatingArmourPerStack, ScavengedPlatingThorns(level)));
		Value(lines, "Platings", std::format("up to {:d}, for {:d} seconds", ScavengedPlatingMaxStacks, ScavengedPlatingTicks / 20));
		break;
	case PersistentSkill::TimeOfNeed:
		Value(lines, "Heals", std::format("{:d}% of your maximum life", TimeOfNeedHealPercent(level)));
		Value(lines, "At most every", ticks(TimeOfNeedTicks(level)));
		break;
	case PersistentSkill::ChargeRegulation:
		Value(lines, "Speed", std::format("{:d}% more attack and cast speed", ChargeRegulationSpeedPercent(level)));
		Value(lines, "Each Frenzy charge", std::format("{:d} seconds", ChargeRegulationTicksPerCharge / 20));
		break;
	case PersistentSkill::WitheringPresence:
		Value(lines, "A stack every", ticks(WitheringPresencePulseTicks(level)));
		Value(lines, "Stacks", std::format("up to {:d}, for {:d} seconds", WitheringPresenceMaxStacks, WitheringPresenceTicks / 20));
		Value(lines, "Reaches", std::format("{:d} tiles around you", PresenceTiles));
		break;
	case PersistentSkill::Trinity:
		Value(lines, "Resonance a hit", std::format("+{:d} of its element, -{:d} of the others", TrinityResonanceGained(level), TrinityResonanceLost));
		Value(lines, "More elemental damage", std::format("{:d}% for every {:d} Resonance in all", TrinityPercentPerStep(level), TrinityResonancePerStep));
		Value(lines, "Fades", std::format("after {:d} seconds without an elemental hit", TrinityIdleTicks / 20));
		break;
	case PersistentSkill::SiphonElements:
		Value(lines, "Infusion chance", std::format("{:d}% a freeze, shock or ignite", SiphonElementsChance(level)));
		Value(lines, "Picked up from", std::format("{:d} tiles", SiphonElementsPickupTiles(level)));
		break;
	case PersistentSkill::HeraldOfBlood:
		Value(lines, "Burst", std::format("{:d}% of the dead enemy's life, {:d} tiles around", HeraldOfBloodPercent, HeraldOfBloodRadius));
		Value(lines, "Bleed chance", std::format("{:d}%", HeraldOfBloodBleedChance(level)));
		break;
	case PersistentSkill::SkeletalReavers:
		Value(lines, "Rage", std::format("+1 a blow, up to {:d}; {:d}% more damage each", ReaverMaxRage, ReaverRagePercent));
		Value(lines, "Rage fades", std::format("{:d} seconds after its last blow", ReaverRageTicks / 20));
		break;
	case PersistentSkill::SkeletalBrutes:
		Value(lines, "Heavy stun", std::format("{:d}% more buildup from its blows", BruteStunPercent));
		break;
	case PersistentSkill::SkeletalClerics:
		Value(lines, "Heals", std::format("{:d}% of a hurt minion's life every {:d} seconds, within {:d} tiles", ClericHealPercent(level), ClericHealTicks / 20, ClericRadius));
		Value(lines, "Fallen minions", "rise twice as fast while a Cleric stands");
		break;
	case PersistentSkill::WolfPack:
		Value(lines, "Wolves", std::format("{:d}", WolfPackSize(level)));
		Value(lines, "Minion level", std::format("{:d}", MinionLevelForGemLevel(level)));
		Value(lines, "Revives after", std::format("{:d} seconds", SkeletonReviveTicks / 20));
		break;
	case PersistentSkill::RagingSpirits:
		Value(lines, "Spirits", std::format("{:d} a cast at most, {:d} at once", RagingSpiritsPerCast, RagingSpiritsMax));
		Value(lines, "Burn for", ticks(RagingSpiritTicks));
		Value(lines, "Fire", std::format("{:d}% of their blows", RagingSpiritFirePercent));
		Value(lines, "Minion level", std::format("{:d}", MinionLevelForGemLevel(level)));
		break;
	case PersistentSkill::RavenousSwarm:
		Value(lines, "A swarm every", std::format("{:d} seconds, with an enemy within {:d} tiles", SwarmEveryTicks / 20, SwarmReachTiles));
		Value(lines, "Each swarm lasts", ticks(SwarmTicks(level)));
		Value(lines, "Bites", std::format("every {:.1f}s for {:d}% of a Skeleton Warrior's blow", SwarmBiteTicks / 20.0, SwarmBitePercent));
		Value(lines, "Poison", std::format("{:d}% of the bite over {:d} seconds", SwarmPoisonPercent, SwarmPoisonTicks / 20));
		break;
	case PersistentSkill::Sacrifice:
		Value(lines, "Corpses", "your skeletons, for Corpse Explosion and Soul Harvest");
		Value(lines, "Minions rise again after", std::format("{:.1f} seconds", SkeletonReviveTicks * SacrificeRevivePercent(level) / 100 / 20.0));
		break;
	case PersistentSkill::LingeringIllusion:
		Value(lines, "Illusion", std::format("{:d} seconds, 1% of your life, draws enemies within {:d} tiles", IllusionTicks / 20, IllusionDrawTiles));
		Value(lines, "Broken by an enemy", std::format("a Frenzy remnant for {:d} seconds", IllusionRemnantTicks / 20));
		Value(lines, "Second remnant", std::format("{:d}% chance", IllusionSecondRemnantChance(level)));
		break;
	default:
		break;
	}
}

} // namespace

std::string SkillTagText(SkillSlot skill)
{
	const uint32_t tags = SkillTags(skill);
	std::string text;
	const auto add = [&text](std::string_view name) {
		if (!text.empty())
			text.append(", ");
		text.append(name);
	};
	if (skill.kind == SkillKind::Persistent)
		add("Spirit");
	if ((tags & ST_Attack) != 0)
		add("Attack");
	if ((tags & ST_Spell) != 0)
		add("Spell");
	if ((tags & ST_Melee) != 0)
		add("Melee");
	if ((tags & ST_Strike) != 0)
		add("Strike");
	if ((tags & ST_Slam) != 0)
		add("Slam");
	if ((tags & ST_Projectile) != 0)
		add("Projectile");
	if ((tags & ST_Area) != 0)
		add("Area");
	if ((tags & ST_Duration) != 0)
		add("Duration");
	if ((tags & ST_Warcry) != 0)
		add("Warcry");
	if ((tags & ST_Curse) != 0)
		add("Curse");
	if ((tags & ST_Minion) != 0)
		add("Minion");
	if ((tags & ST_Movement) != 0)
		add("Movement");
	if ((tags & ST_Channelling) != 0)
		add("Channelling");
	if ((tags & ST_Fire) != 0)
		add("Fire");
	if ((tags & ST_Lightning) != 0)
		add("Lightning");
	if ((tags & ST_Frost) != 0)
		add("Cold");
	if ((tags & ST_Physical) != 0)
		add("Physical");
	return text;
}

std::vector<SkillDetailLine> SkillDetails(const Player &player, size_t socket)
{
	using Kind = SkillDetailLine::Kind;
	std::vector<SkillDetailLine> lines;
	if (socket > DefaultAttackRow)
		return lines;
	const SkillSlot skill = RowSkill(player, socket);
	if (skill.isEmpty())
		return lines;
	const bool isDefault = socket == DefaultAttackRow;

	// The default attack has no gem: it is level 1 and stays so.
	const int gemLevel = isDefault ? MaxGemLevel : player.poe2.gemLevel(socket);
	const int level = isDefault ? 1 : skill.kind == SkillKind::Spell ? player.GetSpellLevel(skill.spell()) : player.poe2.skillLevel(skill);
	const std::string tags = SkillTagText(skill);
	if (!tags.empty())
		Text(lines, Kind::Note, tags);
	if (const std::string_view description = SkillDescription(skill); !description.empty())
		Text(lines, Kind::Text, std::string(description));

	Heading(lines, isDefault ? "Weapon attack" : "Gem");
	if (isDefault)
		Value(lines, "Damage", static_cast<DefaultAttack>(skill.id) == DefaultAttack::StaffBolt ? std::format("{:d}% of the staff's, +{:d}% of Magic", StaffBoltDamagePercent, StaffBoltMagicPercent)
		                                                                                      : std::format("{:d}% of the weapon's", DefaultAttackDamagePercent));
	else
		Value(lines, "Level", level != gemLevel ? std::format("{:d} (gem {:d}, {:+d} from gear)", level, gemLevel, level - gemLevel) : std::format("{:d}", level));
	if (skill.kind == SkillKind::Persistent && IsMinionCountSkill(skill.persistent())) {
		const PersistentSkill persistent = skill.persistent();
		const int chosen = ChosenMinionCount(player, persistent);
		const int active = ActiveMinionCount(player, persistent);
		Value(lines, "Minions", active < chosen ? std::format("{:d} of {:d} (not enough Spirit)", active, chosen) : std::format("{:d}", chosen));
		Value(lines, "Reserves", std::format("{:d} Spirit", GemSpiritReservation(player, persistent)));
		const int free = FreeMinions(persistent);
		Value(lines, "Spirit per minion", free > 0 ? std::format("{:d}, first {:d} free", SpiritPerMinion(player, persistent), free) : std::format("{:d}", SpiritPerMinion(player, persistent)));
		Value(lines, "Minion level", std::format("{:d}", MinionLevelForGemLevel(level)));
		Value(lines, "Revives after", std::format("{:.1f} seconds", MinionReviveTicks(player) / 20.0));
	} else if (skill.persistent() == PersistentSkill::HoradricTotem && skill.kind == SkillKind::Persistent)
		Value(lines, "Cost", std::format("{:d} Mana for each totem", HoradricTotemManaCost(gemLevel)));
	else if (skill.kind == SkillKind::Persistent)
		Value(lines, "Reserves", std::format("{:d} Spirit", GemSpiritCost(player, skill.persistent())));
	else if (const int mana = SkillManaCost(player, skill); mana > 0)
		Value(lines, "Cost", std::format("{:d} Mana", mana));
	else
		Value(lines, "Cost", "No mana");

	if (skill.kind == SkillKind::Persistent && !isDefault)
		SpiritBuffDetails(lines, skill.persistent(), std::max(level, 1));

	if (IsMetaGem(skill) && !isDefault) {
		const PersistentSkill meta = skill.persistent();
		if (UsesMetaEnergy(skill)) {
			Value(lines, "Energy", std::format("{:d} of {:d}", MetaEnergy(player, socket), MetaEnergyCap(player.poe2, socket)));
			if (IsInvocationGem(skill))
				Value(lines, "A cast takes", std::format("{:d} Energy", MetaMaxEnergy(player.poe2, socket)));
			Value(lines, "Energy gain", std::format("+{:d}% from gem level", MetaEnergyPercentPerLevel * (gemLevel - 1)));
		}
		if (meta == PersistentSkill::CastOnCritical)
			Value(lines, "Damage of the spells", std::format("{:d}%", CastOnCriticalDamagePercent));
		if (meta == PersistentSkill::Blasphemy) {
			Value(lines, "Curse strength", std::format("{:d}%", BlasphemyStrength(gemLevel)));
			Value(lines, "Reaches", std::format("{:d} tiles around you", BlasphemyRadius));
			Value(lines, "Spirit per curse", std::format("{:d}", BlasphemySpiritPerCurse));
		}
		if (meta == PersistentSkill::HoradricTotem) {
			Value(lines, "Totems", std::format("up to {:d}, for {:d} seconds", HoradricTotemLimit(gemLevel), HoradricTotemTicks / 20));
			Value(lines, "Casts", std::format("one spell each {:s}, taking turns", Seconds(HoradricTotemCastTicks / 20.0)));
		}
		Heading(lines, meta == PersistentSkill::Blasphemy ? "Curses inside" : "Spells inside");
		if (MetaSkillCount(player.poe2, socket) == 0)
			Text(lines, Kind::Note, meta == PersistentSkill::Blasphemy ? "None yet: drop a curse gem in a socket" : "None yet: drop a spell gem in a socket");
		for (const SkillSlot inside : player.poe2.metaSkills[socket]) {
			if (!inside.isEmpty())
				Value(lines, std::string(SkillName(inside)), std::format("level {:d}, {:d} mana", player.poe2.skillLevel(inside), SkillManaCost(player, inside)));
		}
		if (meta == PersistentSkill::Blasphemy)
			Text(lines, Kind::Note, "They curse every enemy around you, and cost no mana");
		else if (meta == PersistentSkill::HoradricTotem)
			Text(lines, Kind::Note, "The totem casts them at the nearest enemy, and they cost no mana");
		else
			Text(lines, Kind::Note, IsInvocationGem(skill) ? "They cast together when you use it from the skill tray, for their mana" : "They cast together when the Energy is full, for their mana");
	}

	const HitEstimate hit = EstimateHit(player, skill, std::max(skill.kind == SkillKind::Spell ? gemLevel : level, 1));
	const bool usable = skill.kind != SkillKind::Persistent;
	double seconds = 0;
	if (usable) {
		seconds = UseSeconds(player, skill);
		Value(lines, skill.kind == SkillKind::Spell ? "Cast time" : "Use time", std::format("{:s} ({:.1f} per second)", Seconds(seconds), 1.0 / seconds));
	}

	if (!hit.parts.empty()) {
		Heading(lines, "Damage per hit");
		int totalMin = 0;
		int totalMax = 0;
		for (const HitPart &part : hit.parts) {
			if (part.max <= 0)
				continue;
			Value(lines, std::string(DamageTypeName(part.type)), std::format("{:d}-{:d}", part.min, part.max));
			totalMin += part.min;
			totalMax += part.max;
		}
		const int average = (totalMin + totalMax) / 2;
		Value(lines, "Average hit", std::format("{:d}", average));
		if (usable && seconds > 0)
			Value(lines, "Damage per second", std::format("{:d} (one target)", static_cast<int>(average / seconds)));
		Text(lines, Kind::Note, "Before the enemy's armour and resistances");
	} else if (!hit.effect.empty()) {
		Heading(lines, "Effect");
		Text(lines, Kind::Text, hit.effect);
	}

	const uint32_t tagBits = SkillTags(skill);
	int extraProjectiles = GemExtraProjectiles(skill, std::max(level, 1));
	if (skill.kind == SkillKind::Spell)
		extraProjectiles += SupportExtraProjectiles(player, skill.spell());
	else
		extraProjectiles += SupportExtraProjectileCount(player, skill);
	const TableSkillData *table = skill.kind == SkillKind::Attack ? FindTableSkill(skill.attack()) : nullptr;
	std::vector<SkillDetailLine> shape;
	if (table != nullptr && table->count > 1 && IsAnyOf(table->shape, TableShape::Projectiles, TableShape::Stream))
		Value(shape, "Projectiles", std::format("{:d}", table->count + extraProjectiles));
	else if (extraProjectiles > 0)
		Value(shape, "Extra projectiles", std::format("+{:d}", extraProjectiles));
	if (table != nullptr && table->radius > 0)
		Value(shape, "Area radius", std::format("{:d} tiles", static_cast<int>(table->radius)));
	if (table != nullptr && table->duration > 0)
		Value(shape, "Duration", Seconds(table->duration / static_cast<double>(TicksPerSecond)));
	if ((tagBits & ST_Projectile) != 0)
		Text(shape, Kind::Note, "Projectiles fly where you aim, at any angle");
	if (!shape.empty()) {
		Heading(lines, "Area and projectiles");
		lines.insert(lines.end(), shape.begin(), shape.end());
	}

	if (const std::vector<std::string> ailments = AilmentLines(hit, tagBits); !ailments.empty()) {
		Heading(lines, "Ailments");
		for (const std::string &line : ailments)
			Text(lines, Kind::Text, line);
	}

	if (SkillTakesSupports(skill)) {
		Heading(lines, "Supports");
		bool any = false;
		for (size_t i = 0; i < MaxSupportSockets; i++) {
			const SupportGem gem = player.poe2.supportsAt(socket)[i];
			if (gem == SupportGem::None)
				continue;
			any = true;
			const int tier = player.poe2.socketTier(socket, i);
			Value(lines, SupportGemTierName(gem, tier), {});
			if (!SupportGemCanSupport(gem, skill))
				Text(lines, Kind::Note, std::format("Resting: it doesn't fit {:s}", SkillName(skill)));
			else
				Text(lines, Kind::Good, std::string(SupportGemDescription(gem, tier)));
		}
		if (!any)
			Text(lines, Kind::Note, "No supports yet: drop a support gem in a socket");
	}

	if (gemLevel < MaxGemLevel) {
		Heading(lines, "Next gem level");
		Value(lines, "Needs", std::format("character level {:d}", CharacterLevelForGemLevel(gemLevel + 1)));
		Text(lines, Kind::Note, GemLevelText(skill, gemLevel + 1));
	}
	return lines;
}

} // namespace devilution
