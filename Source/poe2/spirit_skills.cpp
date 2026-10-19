#include "poe2/spirit_skills.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

#include "engine/backbuffer_state.hpp"
#include "engine/path.h"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "levels/gendung.h"
#include "levels/tile_properties.hpp"
#include "missiles.h"
#include "monster.h"
#include "multi.h"
#include "player.h"
#include "poe2/monster_mods.hpp"
#include "poe2/profane_roads.hpp"
#include "poe2/skill_resources.hpp"
#include "poe2/spirit.hpp"
#include "poe2/spirit_buffs.hpp"
#include "poe2/spirit_uses.hpp"
#include "poe2/support_table.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/unique_effects.hpp"
#include "utils/is_of.hpp"

namespace devilution {

namespace {

struct InfusionMote {
	Point position;
	uint8_t element;
	uint16_t ticks;
};

std::vector<InfusionMote> Motes;

/** @brief The strongest monster killed lately that Revive can raise. */
struct ReviveCandidate {
	bool valid = false;
	_monster_id type {};
	Point position;
	RevivedStats stats {};
	bool rare = false;
	uint16_t ticks = 0;
};

ReviveCandidate Candidate;

bool Active(const Player &player, PersistentSkill skill)
{
	return gbIsPoe2 && IsPersistentSkillActive(player, skill);
}

int GemLevel(const Player &player, PersistentSkill skill)
{
	return std::max(player.poe2.skillLevel(skill), 1);
}

bool CanTarget(const Monster &monster)
{
	return !monster.hasNoLife() && !monster.isPlayerMinion() && monster.isPossibleToHit() && monster.mode != MonsterMode::Death;
}

Monster *NearestEnemy(Point from, int range)
{
	Monster *nearest = nullptr;
	int best = range + 1;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (!CanTarget(monster))
			continue;
		const int distance = from.WalkingDistance(monster.position.tile);
		if (distance < best) {
			best = distance;
			nearest = &monster;
		}
	}
	return nearest;
}

MissileGraphicID InfusionGraphic(uint8_t element)
{
	switch (element) {
	case Infusion_Fire:
		return MissileGraphicID::MagmaBallExplosion;
	case Infusion_Lightning:
		return MissileGraphicID::BlueFlareExplosion2;
	default:
		return MissileGraphicID::BlueFlareExplosion;
	}
}

uint8_t SkillElement(SkillSlot skill)
{
	if (skill.kind == SkillKind::Spell)
		return skill.spell() == SpellID::Apocalypse ? Infusion_Fire : 0;
	switch (skill.attack()) {
	case WeaponSkill::Meteor:
		return Infusion_Fire;
	case WeaponSkill::Thunderstorm:
		return Infusion_Lightning;
	case WeaponSkill::FrozenOrb:
		return Infusion_Cold;
	default:
		return 0;
	}
}

/** @brief Whether a socketed skill can eat an element infusion; without one, infusions would only glow forever. */
bool UsesInfusions(const Player &player)
{
	for (const SkillSlot &slot : player.poe2.skillSlots) {
		if (!slot.isEmpty() && SkillElement(slot) != 0)
			return true;
	}
	return false;
}

void KeepGuardianWatch(Player &player)
{
	SpiritRuntime &state = player.poe2.spiritState;
	if (state.guardianCooldown > 0) {
		state.guardianCooldown--;
		return;
	}
	if (!Active(player, PersistentSkill::Guardian))
		return;
	Monster *enemy = NearestEnemy(player.position.tile, GuardianRange);
	if (enemy == nullptr)
		return;
	const Direction dir = GetDirection(player.position.tile, enemy->position.tile);
	const Point at = player.position.tile + Displacement { dir } * 2;
	if (AddMissile(player.position.tile, at, dir, MissileID::Guardian, TARGET_MONSTERS, player, 0, GemLevel(player, PersistentSkill::Guardian)) != nullptr)
		TelemetryLog("spirit", "Guardian rose");
	state.guardianCooldown = GuardianCooldownTicks;
}

void BurnAroundFireGolem(Player &player)
{
	if (ActiveGolemSkill(player) != PersistentSkill::FireGolem)
		return;
	SpiritRuntime &state = player.poe2.spiritState;
	if (++state.auraTicks < 20)
		return;
	state.auraTicks = 0;
	const Monster *golem = FindGolemForPlayer(player);
	if (golem == nullptr || golem->hasNoLife())
		return;
	const int level = GemLevel(player, PersistentSkill::FireGolem);
	const Fixed26_6 damage = Fixed26_6::fromInt((4 + level + (player.getCharacterLevel() / 2)) * MinionDamagePercent(player) / 100);
	TelemetryNoteDamageSource("Fire Golem");
	SkillBurst(player, golem->position.tile, 2, DamageType::Fire, damage, MissileGraphicID::Inferno);
}

void KeepRevivedAlive(Player &player)
{
	SpiritRuntime &state = player.poe2.spiritState;
	if (Candidate.valid && Candidate.ticks > 0 && --Candidate.ticks == 0)
		Candidate.valid = false;
	if (!Active(player, PersistentSkill::Revive) || gbIsMultiplayer) {
		Candidate.valid = false;
		if (CountRevivedMinions(player) > 0)
			KillRevivedMinions(player);
		return;
	}
	if (state.reviveCooldown > 0) {
		state.reviveCooldown--;
		return;
	}
	const int limit = ReviveMinionLimit(player);
	const size_t revived = CountRevivedMinions(player);
	if (!Candidate.valid || leveltype == DTYPE_TOWN || revived >= static_cast<size_t>(std::max(limit, 0)))
		return;
	const std::optional<Point> position = FindClosestValidPosition([](Point target) { return !IsTileOccupied(target); }, Candidate.position, 0, 3);
	Candidate.valid = false;
	if (!position)
		return;
	// Leoric's Road: Revive comes first under the cap it shares with the King's dead.
	MakeRoomForRevive(player, revived, limit);
	RevivedStats stats = Candidate.stats;
	const int lifePercent = SupportMinionLifePercent(player, PersistentSkill::Revive) * (100 + (5 * GemLevel(player, PersistentSkill::Revive))) / 100;
	stats.life = Fixed26_6::fromRaw(static_cast<int32_t>(std::min<int64_t>(static_cast<int64_t>(stats.life.raw()) * lifePercent / 100, INT32_MAX / 4)));
	if (SpawnRevivedMinion(player, *position, Candidate.type, stats) != nullptr) {
		AddVisualEffect(*position, MissileGraphicID::Resurrect);
		TelemetryLog("spirit", Candidate.rare ? "Revive raised a rare monster" : "Revive raised a monster");
	}
	state.reviveCooldown = ReviveCooldownTicks;
}

void ProcessMotes(Player &player)
{
	SpiritRuntime &state = player.poe2.spiritState;
	// Unsocketing the last skill that eats infusions drops the ones carried.
	if (state.infusions != 0 && !UsesInfusions(player))
		state.infusions = 0;
	for (size_t i = 0; i < Motes.size();) {
		InfusionMote &mote = Motes[i];
		if (mote.ticks % 15 == 0)
			AddVisualEffect(mote.position, InfusionGraphic(mote.element));
		if (player.position.tile.WalkingDistance(mote.position) <= 1 && (state.infusions & mote.element) == 0) {
			state.infusions |= mote.element;
			AddVisualEffect(player.position.tile, InfusionGraphic(mote.element), 0, 4);
			TelemetryLog("infusion", mote.element == Infusion_Fire ? "picked up fire" : mote.element == Infusion_Lightning ? "picked up lightning"
			                                                                                                               : "picked up cold");
			Motes.erase(Motes.begin() + static_cast<std::ptrdiff_t>(i));
			continue;
		}
		if (--mote.ticks == 0) {
			Motes.erase(Motes.begin() + static_cast<std::ptrdiff_t>(i));
			continue;
		}
		i++;
	}
	// The infusions carried glow around the character.
	if (state.infusions != 0 && ++state.glowTicks >= 20) {
		state.glowTicks = 0;
		for (const uint8_t element : { Infusion_Fire, Infusion_Lightning, Infusion_Cold }) {
			if ((state.infusions & element) != 0 && GenerateRnd(3) == 0)
				AddVisualEffect(player.position.tile, InfusionGraphic(element));
		}
	}
}

} // namespace

int ReviveMinionLimit(const Player &player)
{
	if (!Active(player, PersistentSkill::Revive))
		return 0;
	return std::min(1 + (player.getCharacterLevel() / 15), 4) + SupportExtraMinions(player, PersistentSkill::Revive);
}

PersistentSkill ActiveGolemSkill(const Player &player)
{
	if (!gbIsPoe2)
		return PersistentSkill::None;
	for (const SkillSlot &slot : player.poe2.skillSlots) {
		const PersistentSkill skill = slot.persistent();
		if (IsAnyOf(skill, PersistentSkill::SummonGolem, PersistentSkill::BloodGolem, PersistentSkill::FireGolem) && IsPersistentSkillActive(player, skill))
			return skill;
	}
	return PersistentSkill::None;
}

int GolemLifePercent(const Player &player)
{
	const PersistentSkill skill = ActiveGolemSkill(player);
	const int percent = skill == PersistentSkill::BloodGolem ? BloodGolemLifePercent : 100;
	return skill == PersistentSkill::None ? percent : percent * SupportMinionLifePercent(player, skill) / 100;
}

void OnMinionHitMonster(Player &owner, const Monster &minion, Monster &target, Fixed26_6 damage)
{
	if (!gbIsPoe2 || &owner != MyPlayer || damage <= 0 || minion.type().type != MT_GOLEM)
		return;
	switch (ActiveGolemSkill(owner)) {
	case PersistentSkill::BloodGolem: {
		if (owner.hasNoLife())
			break;
		const Fixed26_6 heal = Fixed26_6::fromRaw(damage.raw() * BloodGolemHealPercent / 100);
		owner._pHitPoints = std::min(owner._pHitPoints + heal, owner._pMaxHP);
		owner._pHPBase = std::min(owner._pHPBase + heal, owner._pMaxHPBase);
		RedrawComponent(PanelDrawComponent::Health);
		break;
	}
	case PersistentSkill::FireGolem:
		if (!target.hasNoLife()) {
			const Fixed26_6 fire = Fixed26_6::fromRaw(damage.raw() * FireGolemFirePercent / 100 * MonsterDamageTakenPercent(target, DamageType::Fire) / 100);
			ApplyMonsterDamage(DamageType::Fire, target, fire);
			target.tag(owner);
			TryInflictAilment(target, owner, DamageType::Fire, fire);
			AddVisualEffect(target.position.tile, MissileGraphicID::MagmaBallExplosion);
		}
		break;
	default:
		break;
	}
}

bool ShadowWarriorActive(const Player &player)
{
	return Active(player, PersistentSkill::ShadowWarrior);
}

void OnReviveCandidateKilled(const Player &player, const Monster &monster)
{
	if (!gbIsPoe2 || &player != MyPlayer || monster.isUnique() || monster.isPlayerMinion() || IsAnyOf(monster.type().type, MT_GOLEM, MT_DIABLO, MT_NAKRUL))
		return;
	if (!Active(player, PersistentSkill::Revive))
		return;
	const bool rare = GetMonsterAffixes(monster).rarity == MonsterRarity::Rare;
	// The strongest catch wins: a rare one first, then the one with more life.
	if (Candidate.valid && (Candidate.rare > rare || (Candidate.rare == rare && Candidate.stats.life > monster.maxHitPoints)))
		return;
	Candidate.valid = true;
	Candidate.type = monster.type().type;
	Candidate.position = monster.position.tile;
	Candidate.stats = { monster.maxHitPoints, monster.minDamage, monster.maxDamage, monster.armorClass, monster.resistance };
	Candidate.rare = rare;
	Candidate.ticks = 10 * 20;
}

Fixed26_6 ApplySpiritHitTaken(Player &player, Fixed26_6 damage)
{
	if (!gbIsPoe2 || damage <= 0)
		return damage;
	int64_t raw = damage.raw();
	if (Active(player, PersistentSkill::Berserk))
		raw = raw * (100 + BerserkDamageTakenPercent) / 100;
	if (Active(player, PersistentSkill::ManaShield) && player._pMana > 0) {
		const int64_t fromMana = std::min<int64_t>(raw * ManaShieldPercent / 100, player._pMana.raw());
		player._pMana -= Fixed26_6::fromRaw(static_cast<int32_t>(fromMana));
		player._pManaBase -= Fixed26_6::fromRaw(static_cast<int32_t>(fromMana));
		raw -= fromMana;
		if (fromMana > 0) {
			AddVisualEffect(player.position.tile, MissileGraphicID::ManaShield);
			RedrawComponent(PanelDrawComponent::Mana);
		}
	}
	return Fixed26_6::fromRaw(static_cast<int32_t>(std::clamp<int64_t>(raw, 0, INT32_MAX / 2)));
}

int SpiritAttackDamagePercent(const Player &player)
{
	return Active(player, PersistentSkill::Berserk) ? 100 + BerserkDamagePercent : 100;
}

int SpiritSpellDamagePercent(const Player &player)
{
	if (!Active(player, PersistentSkill::VizjereiFocus))
		return 100;
	return 100 + (std::max<int32_t>(player._pMaxMana.whole(), 0) / 100 * VizjereiPercentPer100Mana);
}

int32_t SpiritSpellExtraManaRaw(const Player &player)
{
	if (!Active(player, PersistentSkill::VizjereiFocus))
		return 0;
	return std::max<int32_t>(player._pMaxMana.raw(), 0) / 100 * VizjereiManaCostPercent;
}

void OnSpiritMeleeHit(Player &player)
{
	if (Active(player, PersistentSkill::Berserk))
		AddRage(player, 2);
}

void ReflectMeleeHit(Player &player, Monster &monster, Fixed26_6 damage)
{
	OnSpiritMeleeHitTaken(player, monster);
	if (!gbIsPoe2 || &player != MyPlayer || damage <= 0 || monster.hasNoLife() || monster.mode == MonsterMode::Death || !Active(player, PersistentSkill::Reflect))
		return;
	const Fixed26_6 reflected = Fixed26_6::fromRaw(damage.raw() * ReflectPercent / 100);
	if (reflected <= 0)
		return;
	TelemetryNoteDamageSource("Reflect");
	ApplyMonsterDamage(DamageType::Physical, monster, reflected);
	monster.tag(player);
	AddVisualEffect(monster.position.tile, MissileGraphicID::Reflect);
	if (monster.hasNoLife())
		M_StartKill(monster, player);
}

void TryDropInfusion(const Monster &monster)
{
	OnSpiritBuffKill(monster);
	if (!gbIsPoe2 || MyPlayer == nullptr || monster.isPlayerMinion() || !UsesInfusions(*MyPlayer))
		return;
	uint8_t element = 0;
	if (monster.poe2.igniteTicks > 0)
		element = Infusion_Fire;
	else if (monster.poe2.shockTicks > 0)
		element = Infusion_Lightning;
	else if (monster.poe2.chillTicks > 0 || monster.poe2.freezeTicks > 0)
		element = Infusion_Cold;
	if (element == 0 || GenerateRnd(100) >= InfusionDropPercent)
		return;
	if (std::count_if(Motes.begin(), Motes.end(), [&](const InfusionMote &mote) { return mote.element == element; }) >= 3)
		return;
	Motes.push_back({ monster.position.tile, element, InfusionMoteTicks });
	AddVisualEffect(monster.position.tile, InfusionGraphic(element), 0, 3);
}

uint8_t TakeInfusion(Player &player, SkillSlot skill)
{
	const uint8_t own = SkillElement(skill);
	if (!gbIsPoe2 || own == 0)
		return 0;
	uint8_t &carried = player.poe2.spiritState.infusions;
	uint8_t eaten = 0;
	if ((carried & own) != 0) {
		eaten = own;
	} else {
		for (const uint8_t element : { Infusion_Fire, Infusion_Lightning, Infusion_Cold }) {
			if ((carried & element) != 0) {
				eaten = element;
				break;
			}
		}
	}
	carried &= static_cast<uint8_t>(~eaten);
	if (eaten != 0)
		AddVisualEffect(player.position.tile, InfusionGraphic(eaten), 0, 6);
	return eaten;
}

DamageType InfusionDamageType(uint8_t infusion)
{
	switch (infusion) {
	case Infusion_Fire:
		return DamageType::Fire;
	case Infusion_Lightning:
		return DamageType::Lightning;
	default:
		return DamageType::Magic;
	}
}

void ProcessSpiritSkills(Player &player)
{
	if (!gbIsPoe2 || &player != MyPlayer || player.hasNoLife())
		return;
	ProcessSpiritBuffs(player);
	ProcessSpiritUses(player);
	if (leveltype == DTYPE_TOWN) {
		Motes.clear();
		return;
	}
	KeepGuardianWatch(player);
	BurnAroundFireGolem(player);
	KeepRevivedAlive(player);
	ProcessMotes(player);
}

void ClearSpiritSkills()
{
	ClearSpiritBuffs();
	Motes.clear();
	Candidate = {};
}

size_t InfusionMoteCount()
{
	return Motes.size();
}

} // namespace devilution
