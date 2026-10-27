#include "poe2/spirit_buffs.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "engine/backbuffer_state.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "levels/gendung.h"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/defenses.hpp"
#include "poe2/flasks.hpp"
#include "poe2/gem_levels.hpp"
#include "poe2/minion_gems.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/skill_resources.hpp"
#include "poe2/spirit.hpp"
#include "poe2/spirit_auras.hpp"
#include "poe2/spirit_uses.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/unique_effects.hpp"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

struct Caltrop {
	Point position;
	uint16_t ticks;
};

struct ManaRemnant {
	Point position;
	uint16_t ticks;
};

std::vector<Caltrop> Caltrops;
std::vector<ManaRemnant> Remnants;

static_assert(std::tuple_size_v<decltype(SpiritRuntime::barkArmour)> == BarkskinMaxStacks);

/** @brief A value at gem level 1 and 20, in between on a straight line; levels from gear go on past 20. */
int AtLevel(int gemLevel, int at1, int at20)
{
	const int level = std::clamp(gemLevel, 1, 30);
	return at1 + ((at20 - at1) * (level - 1) / 19);
}

bool On(const Player &player, PersistentSkill skill)
{
	return gbIsPoe2 && IsPersistentSkillActive(player, skill);
}

int GemLevel(const Player &player, PersistentSkill skill)
{
	return std::max(player.poe2.skillLevel(skill), 1);
}

bool CanHit(const Monster &monster)
{
	return !monster.hasNoLife() && !monster.isPlayerMinion() && monster.isPossibleToHit() && monster.mode != MonsterMode::Death;
}

bool IsRareOrUnique(const Monster &monster)
{
	return monster.isUnique() || GetMonsterAffixes(monster).rarity == MonsterRarity::Rare;
}

bool HasElementalAilment(const Monster &monster)
{
	const Poe2MonsterState &state = monster.poe2;
	return state.igniteTicks > 0 || state.shockTicks > 0 || state.chillTicks > 0 || state.freezeTicks > 0;
}

/** @brief An average hit of the hero's weapon, as for a burst, in percent. */
Fixed26_6 WeaponHit(const Player &player, int percent)
{
	int base = (player._pIMinDam + player._pIMaxDam) / 2;
	base += base * player._pIBonusDam / 100 + player._pIBonusDamMod + player._pDamageMod;
	const int64_t raw = static_cast<int64_t>(std::max(base, 1)) * 64 * percent / 100;
	return Fixed26_6::fromRaw(static_cast<int32_t>(std::clamp<int64_t>(raw, 64, INT32_MAX / 2)));
}

/** @brief Deals the damage to the monster for the local hero; whether it lives. */
bool HitMonster(Player &player, Monster &monster, DamageType type, Fixed26_6 damage, int ailmentPercent = 100)
{
	const Fixed26_6 dealt = Fixed26_6::fromRaw(static_cast<int32_t>(static_cast<int64_t>(damage.raw()) * MonsterDamageTakenPercent(monster, type) / 100));
	ApplyMonsterDamage(type, monster, dealt);
	monster.tag(player);
	TryInflictAilment(monster, player, type, Fixed26_6::fromRaw(static_cast<int32_t>(std::min<int64_t>(static_cast<int64_t>(dealt.raw()) * ailmentPercent / 100, INT32_MAX / 2))), AilmentSource::Spell);
	if (monster.hasNoLife()) {
		M_StartKill(monster, player);
		return false;
	}
	return true;
}

void ReleaseGaleForce(Player &player, int stages)
{
	static constexpr std::array<int, WindDancerMaxStages + 1> Radius { 1, 2, 2, 3 };
	const int radius = Radius[static_cast<size_t>(std::clamp(stages, 0, WindDancerMaxStages))];
	const int percent = GaleForceDamagePercent(GemLevel(player, PersistentSkill::WindDancer)) * (100 + (GaleForceDamagePerStage * stages)) / 100;
	const Fixed26_6 damage = WeaponHit(player, percent);
	// A ring of wind around the hero, wider with more stages.
	for (int dx = -radius; dx <= radius; dx++) {
		for (int dy = -radius; dy <= radius; dy++) {
			if (std::max(std::abs(dx), std::abs(dy)) == radius || (dx == 0 && dy == 0))
				AddVisualEffect(player.position.tile + Displacement { dx, dy }, MissileGraphicID::BlueFlareExplosion);
		}
	}
	TelemetryNoteDamageSource("Gale Force");
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (!CanHit(monster) || player.position.tile.WalkingDistance(monster.position.tile) > radius)
			continue;
		if (HitMonster(player, monster, DamageType::Physical, damage) && !monster.isUnique() && monster.mode != MonsterMode::Petrified)
			M_GetKnockback(monster, player.position.tile);
	}
	TelemetryLog("spirit", StrCat("Gale Force with ", stages, " stages"));
}

void TickWindDancer(Player &player, SpiritRuntime &state)
{
	if (state.galeCooldown > 0)
		state.galeCooldown--;
	if (!On(player, PersistentSkill::WindDancer)) {
		state.windStages = 0;
		state.windTicks = 0;
		state.galePending = -1;
		return;
	}
	if (state.galePending >= 0) {
		ReleaseGaleForce(player, state.galePending);
		state.galePending = -1;
		state.galeCooldown = GaleForceCooldownTicks;
	}
	if (state.windStages >= WindDancerMaxStages) {
		state.windTicks = 0;
		return;
	}
	if (++state.windTicks >= WindDancerStageTicks(GemLevel(player, PersistentSkill::WindDancer))) {
		state.windTicks = 0;
		state.windStages++;
	}
}

void TickGhostDance(Player &player, SpiritRuntime &state)
{
	if (!On(player, PersistentSkill::GhostDance)) {
		state.shrouds = 0;
		state.shroudTicks = 0;
		state.shroudRegenTicks = 0;
		return;
	}
	if (state.shrouds < GhostShroudMax && ++state.shroudTicks >= GhostShroudTicks(GemLevel(player, PersistentSkill::GhostDance))) {
		state.shroudTicks = 0;
		state.shrouds++;
		AddVisualEffect(player.position.tile, MissileGraphicID::Etherealize);
	}
	if (state.shroudRegenTicks == 0)
		return;
	state.shroudRegenTicks--;
	Poe2PlayerData &data = player.poe2;
	const int32_t maxRaw = data.maxEnergyShield * 64;
	if (data.energyShield >= maxRaw)
		return;
	const int64_t total = (static_cast<int64_t>(EvasionRating(player)) * GhostDanceRegenPercent * 64 / 100) + state.shroudRegenCarry;
	state.shroudRegenCarry = static_cast<uint8_t>(total % 20);
	data.energyShield = static_cast<int32_t>(std::min<int64_t>(data.energyShield + (total / 20), maxRaw));
	RedrawComponent(PanelDrawComponent::Health);
}

void TickBarkskin(SpiritRuntime &state)
{
	for (size_t i = 0; i < state.barkTicks.size(); i++) {
		if (state.barkTicks[i] > 0 && --state.barkTicks[i] == 0)
			state.barkArmour[i] = 0;
	}
}

void TickArcticArmour(Player &player, SpiritRuntime &state)
{
	if (!On(player, PersistentSkill::ArcticArmour)) {
		state.arcticStages = 0;
		state.arcticTicks = 0;
		return;
	}
	const int level = GemLevel(player, PersistentSkill::ArcticArmour);
	if (state.arcticStages >= ArcticArmourMaxStages(level)) {
		state.arcticStages = static_cast<uint8_t>(ArcticArmourMaxStages(level));
		state.arcticTicks = 0;
		return;
	}
	if (++state.arcticTicks >= ArcticArmourStageTicks(level)) {
		state.arcticTicks = 0;
		state.arcticStages++;
	}
}

void TickEternalRage(Player &player, SpiritRuntime &state)
{
	if (!On(player, PersistentSkill::EternalRage)) {
		state.rageCarry = 0;
		return;
	}
	// Regenerating counts as gaining Rage, so it doesn't fade while this is up.
	player.poe2.rageTicks = std::max(player.poe2.rageTicks, RageHoldTicks);
	state.rageCarry = static_cast<uint16_t>(state.rageCarry + EternalRageCentiPerSecond(GemLevel(player, PersistentSkill::EternalRage)));
	while (state.rageCarry >= 100 * 20) {
		state.rageCarry = static_cast<uint16_t>(state.rageCarry - (100 * 20));
		if (player.poe2.rage < MaxRage)
			player.poe2.rage++;
	}
}

void TickConflux(Player &player, SpiritRuntime &state)
{
	if (!On(player, PersistentSkill::ElementalConflux)) {
		state.confluxElement = -1;
		state.confluxTicks = 0;
		return;
	}
	if (state.confluxTicks > 0) {
		state.confluxTicks--;
		return;
	}
	static constexpr std::array<DamageType, 3> Elements { DamageType::Fire, DamageType::Lightning, DamageType::Magic };
	const DamageType element = Elements[static_cast<size_t>(GenerateRnd(3))];
	state.confluxElement = static_cast<int8_t>(element);
	state.confluxTicks = ConfluxTicks;
	const MissileGraphicID effect = element == DamageType::Fire ? MissileGraphicID::MagmaBallExplosion : element == DamageType::Lightning ? MissileGraphicID::BlueFlareExplosion2
	                                                                                                                                      : MissileGraphicID::BlueFlareExplosion;
	AddVisualEffect(player.position.tile, effect, 0, 4);
}

void TickAlchemist(Player &player, SpiritRuntime &state)
{
	if (!On(player, PersistentSkill::AlchemistsBoon)) {
		state.flaskCarry = 0;
		return;
	}
	state.flaskCarry = static_cast<uint16_t>(state.flaskCarry + AlchemistChargesMilliPerSecond(GemLevel(player, PersistentSkill::AlchemistsBoon)));
	if (state.flaskCarry < 1000 * 20)
		return;
	state.flaskCarry = static_cast<uint16_t>(state.flaskCarry - (1000 * 20));
	for (size_t slot = 0; slot < NumFlasks; slot++) {
		uint8_t &charges = player.poe2.flaskCharges[slot];
		charges = static_cast<uint8_t>(std::min<int>(MaxFlaskChargesFor(player.SpdList[slot]), charges + 1));
	}
}

void TickAttrition(const Player &player)
{
	if (!On(player, PersistentSkill::Attrition))
		return;
	const int cullTicks = AttritionCullTicks(GemLevel(player, PersistentSkill::Attrition));
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (!CanHit(monster) || !IsRareOrUnique(monster) || player.position.tile.WalkingDistance(monster.position.tile) > PresenceTiles)
			continue;
		uint16_t &ticks = monster.poe2.attritionTicks;
		if (ticks < UINT16_MAX)
			ticks++;
		// It can be culled from now on.
		if (ticks == cullTicks)
			AddVisualEffect(monster.position.tile, MissileGraphicID::BloodStarRedExplosion, 0, 4);
	}
}

void TickCaltrops(Player &player)
{
	const int level = GemLevel(player, PersistentSkill::TrailOfCaltrops);
	for (size_t i = 0; i < Caltrops.size();) {
		Caltrop &caltrop = Caltrops[i];
		if (caltrop.ticks % 20 == 0)
			AddVisualEffect(caltrop.position, MissileGraphicID::MetalHit);
		Monster *stepped = nullptr;
		if (InDungeonBounds(caltrop.position)) {
			const int id = dMonster[caltrop.position.x][caltrop.position.y];
			if (id != 0) {
				Monster &monster = Monsters[std::abs(id) - 1];
				if (CanHit(monster))
					stepped = &monster;
			}
		}
		if (stepped != nullptr) {
			TelemetryNoteDamageSource("Trail of Caltrops");
			AddVisualEffect(caltrop.position, MissileGraphicID::BloodHit);
			stepped->poe2.hinderTicks = std::max<uint8_t>(stepped->poe2.hinderTicks, CaltropHinderTicks);
			HitMonster(player, *stepped, DamageType::Physical, WeaponHit(player, CaltropDamagePercent(level)));
			Caltrops.erase(Caltrops.begin() + static_cast<std::ptrdiff_t>(i));
			continue;
		}
		if (--caltrop.ticks == 0) {
			Caltrops.erase(Caltrops.begin() + static_cast<std::ptrdiff_t>(i));
			continue;
		}
		i++;
	}
}

void TickRemnants(Player &player)
{
	const int level = GemLevel(player, PersistentSkill::ManaRemnants);
	for (size_t i = 0; i < Remnants.size();) {
		ManaRemnant &remnant = Remnants[i];
		if (remnant.ticks % 15 == 0)
			AddVisualEffect(remnant.position, MissileGraphicID::BlueFlare);
		if (player.position.tile.WalkingDistance(remnant.position) <= 1) {
			const auto raw = static_cast<int32_t>(static_cast<int64_t>(ManaRemnantMana(level)) * 64 * ManaRecoveryPercent(player) / 100);
			player._pMana = std::min(player._pMana + Fixed26_6::fromRaw(raw), player._pMaxMana);
			player._pManaBase = std::min(player._pManaBase + Fixed26_6::fromRaw(raw), player._pMaxManaBase);
			RedrawComponent(PanelDrawComponent::Mana);
			AddVisualEffect(player.position.tile, MissileGraphicID::BlueFlareExplosion, 0, 4);
			TelemetryLog("spirit", "picked up a mana remnant");
			Remnants.erase(Remnants.begin() + static_cast<std::ptrdiff_t>(i));
			continue;
		}
		if (--remnant.ticks == 0) {
			Remnants.erase(Remnants.begin() + static_cast<std::ptrdiff_t>(i));
			continue;
		}
		i++;
	}
}

void DropRemnant(Point position)
{
	if (Remnants.size() >= MaxManaRemnants)
		Remnants.erase(Remnants.begin());
	Remnants.push_back({ position, ManaRemnantTicks });
	AddVisualEffect(position, MissileGraphicID::BlueFlareExplosion);
}

} // namespace

int WindDancerStageTicks(int gemLevel)
{
	return AtLevel(gemLevel, 30, 22);
}

int GaleForceDamagePercent(int gemLevel)
{
	return AtLevel(gemLevel, 20, 82);
}

int GhostShroudTicks(int gemLevel)
{
	return AtLevel(gemLevel, 240, 202);
}

int BarkskinArmourPercent(int gemLevel)
{
	return AtLevel(gemLevel, 43, 62);
}

int ArcticArmourStageTicks(int gemLevel)
{
	return AtLevel(gemLevel, 25, 15);
}

int ArcticArmourMaxStages(int gemLevel)
{
	return AtLevel(gemLevel, 2, 5);
}

int CombatFrenzyCooldownTicks(int gemLevel)
{
	return AtLevel(gemLevel, 160, 122);
}

int EternalRageCentiPerSecond(int gemLevel)
{
	return AtLevel(gemLevel, 25, 500);
}

int ConfluxDamagePercent(int gemLevel)
{
	return AtLevel(gemLevel, 40, 59);
}

int AlchemistChargesMilliPerSecond(int gemLevel)
{
	return AtLevel(gemLevel, 170, 230);
}

int AlchemistMinionHealPercent(int gemLevel)
{
	return AtLevel(gemLevel, 15, 34);
}

int PresenceStunThresholdPercent(int gemLevel)
{
	return AtLevel(gemLevel, 15, 25);
}

int PresenceAilmentThresholdPercent(int gemLevel)
{
	return AtLevel(gemLevel, 15, 24);
}

int CaltropDamagePercent(int gemLevel)
{
	return AtLevel(gemLevel, 35, 143);
}

int HeraldOfPlagueHinderChance(int gemLevel)
{
	return AtLevel(gemLevel, 10, 29);
}

int ManaRemnantMana(int gemLevel)
{
	return AtLevel(gemLevel, 28, 281);
}

int AttritionMaxPercent(int gemLevel)
{
	return AtLevel(gemLevel, 40, 59);
}

int AttritionCullTicks(int gemLevel)
{
	return AtLevel(gemLevel, 40 * 20, 31 * 20);
}

int BlinkTiles(int gemLevel)
{
	return AtLevel(gemLevel, 3, 4);
}

int BlinkCooldownTicks(int gemLevel)
{
	return AtLevel(gemLevel, 90, 70);
}

int SpiritBuffEvasionPercent(const Player &player)
{
	if (!On(player, PersistentSkill::WindDancer))
		return 0;
	return player.poe2.spiritState.windStages * WindDancerEvasionPerStage;
}

int SpiritBuffArmour(const Player &player)
{
	if (!On(player, PersistentSkill::Barkskin))
		return 0;
	const SpiritRuntime &state = player.poe2.spiritState;
	int armour = 0;
	for (size_t i = 0; i < state.barkArmour.size(); i++) {
		if (state.barkTicks[i] > 0)
			armour += state.barkArmour[i];
	}
	return std::min(armour, std::max(player._pIAC + player._pIBonusAC, 0));
}

void OnSpiritHitTaken(Player &player, Fixed26_6 shieldLost)
{
	if (!gbIsPoe2 || &player != MyPlayer)
		return;
	SpiritRuntime &state = player.poe2.spiritState;
	if (On(player, PersistentSkill::WindDancer)) {
		if (state.galeCooldown == 0 && state.galePending < 0)
			state.galePending = static_cast<int8_t>(state.windStages);
		state.windStages = 0;
		state.windTicks = 0;
	}
	if (state.shrouds > 0 && On(player, PersistentSkill::GhostDance)) {
		state.shrouds--;
		state.shroudRegenTicks = GhostDanceRegenTicks;
		AddVisualEffect(player.position.tile, MissileGraphicID::Etherealize);
	}
	if (shieldLost > 0 && On(player, PersistentSkill::Barkskin)) {
		const int armour = static_cast<int>(static_cast<int64_t>(shieldLost.raw()) * BarkskinArmourPercent(GemLevel(player, PersistentSkill::Barkskin)) / 100 / 64);
		if (armour > 0) {
			// A new stack takes a free slot, or the one closest to running out.
			size_t slot = 0;
			for (size_t i = 1; i < state.barkTicks.size(); i++) {
				if (state.barkTicks[i] < state.barkTicks[slot])
					slot = i;
			}
			state.barkArmour[slot] = static_cast<uint16_t>(std::min(armour, static_cast<int>(UINT16_MAX)));
			state.barkTicks[slot] = BarkskinTicks;
		}
	}
}

void OnSpiritMeleeHitTaken(Player &player, Monster &monster)
{
	if (!gbIsPoe2 || &player != MyPlayer || !CanHit(monster))
		return;
	OnAuraMeleeHitTaken(player, monster);
	SpiritRuntime &state = player.poe2.spiritState;
	if (state.arcticStages == 0 || !On(player, PersistentSkill::ArcticArmour) || !CanHit(monster))
		return;
	state.arcticStages--;
	const int level = GemLevel(player, PersistentSkill::ArcticArmour);
	const int base = 5 + (player._pMagic / 6) + (2 * level);
	const int64_t raw = static_cast<int64_t>(base) * 64 * GemCurveDamagePercent(SkillSlot::ForSpell(SpellID::Fireball), level) / 100 * ArcticArmourDamagePercent / 100;
	TelemetryNoteDamageSource("Arctic Armour");
	AddVisualEffect(monster.position.tile, MissileGraphicID::BlueFlareExplosion);
	AddVisualEffect(player.position.tile, MissileGraphicID::StoneCurseShatter);
	HitMonster(player, monster, DamageType::Magic, Fixed26_6::fromRaw(static_cast<int32_t>(std::clamp<int64_t>(raw, 64, INT32_MAX / 2))), ArcticArmourAilmentPercent);
}

void OnSpiritFreezeOrElectrocute(Player &player)
{
	if (!gbIsPoe2 || &player != MyPlayer)
		return;
	SpiritRuntime &state = player.poe2.spiritState;
	if (state.frenzyCooldown > 0 || !On(player, PersistentSkill::CombatFrenzy))
		return;
	GainFrenzyCharge(player);
	state.frenzyCooldown = static_cast<uint8_t>(std::clamp(CombatFrenzyCooldownTicks(GemLevel(player, PersistentSkill::CombatFrenzy)), 1, 255));
}

void OnSpiritCrit(Player &player, const Monster &monster)
{
	if (!gbIsPoe2 || &player != MyPlayer || monster.isPlayerMinion() || !HasElementalAilment(monster))
		return;
	SpiritRuntime &state = player.poe2.spiritState;
	if (state.remnantCooldown > 0 || !On(player, PersistentSkill::ManaRemnants))
		return;
	DropRemnant(monster.position.tile);
	state.remnantCooldown = ManaRemnantCritCooldownTicks;
}

void OnSpiritBuffKill(const Monster &monster)
{
	if (!gbIsPoe2 || MyPlayer == nullptr || monster.isPlayerMinion() || !HasElementalAilment(monster) || !On(*MyPlayer, PersistentSkill::ManaRemnants))
		return;
	if (GenerateRnd(100) < ManaRemnantDropPercent)
		DropRemnant(monster.position.tile);
}

int SpiritBuffDamagePercent(const Monster &monster, DamageType type)
{
	if (!gbIsPoe2 || MyPlayer == nullptr || monster.isPlayerMinion())
		return 100;
	const Player &player = *MyPlayer;
	int percent = 100;
	if (ConfluxElement(player) == static_cast<int>(type))
		percent = percent * (100 + ConfluxDamagePercent(GemLevel(player, PersistentSkill::ElementalConflux))) / 100;
	if (monster.poe2.attritionTicks > 0 && On(player, PersistentSkill::Attrition)) {
		const int more = std::min(monster.poe2.attritionTicks / AttritionTicksPerPercent, AttritionMaxPercent(GemLevel(player, PersistentSkill::Attrition)));
		percent = percent * (100 + more) / 100;
	}
	percent = percent * (100 + TrinityDamagePercent(player, type)) / 100;
	return percent;
}

int ConfluxElement(const Player &player)
{
	return On(player, PersistentSkill::ElementalConflux) ? player.poe2.spiritState.confluxElement : -1;
}

int PresenceThresholdPercent(const Monster &monster, bool stun)
{
	if (!gbIsPoe2 || MyPlayer == nullptr || monster.isPlayerMinion() || !On(*MyPlayer, PersistentSkill::OverwhelmingPresence))
		return 100;
	if (MyPlayer->position.tile.WalkingDistance(monster.position.tile) > PresenceTiles)
		return 100;
	const int level = GemLevel(*MyPlayer, PersistentSkill::OverwhelmingPresence);
	return 100 - (stun ? PresenceStunThresholdPercent(level) : PresenceAilmentThresholdPercent(level));
}

bool AttritionCulls(const Player &player, const Monster &monster)
{
	if (monster.hasNoLife() || monster.isPlayerMinion() || monster.type().type == MT_DIABLO || !IsRareOrUnique(monster))
		return false;
	if (!On(player, PersistentSkill::Attrition) || monster.poe2.attritionTicks < AttritionCullTicks(GemLevel(player, PersistentSkill::Attrition)))
		return false;
	const int threshold = monster.isUnique() ? AttritionCullUniquePercent : AttritionCullRarePercent;
	return static_cast<int64_t>(monster.hitPoints.raw()) * 100 <= static_cast<int64_t>(monster.maxHitPoints.raw()) * threshold;
}

int FlaskMinionHealPercent(const Player &player)
{
	return On(player, PersistentSkill::AlchemistsBoon) ? AlchemistMinionHealPercent(GemLevel(player, PersistentSkill::AlchemistsBoon)) : 0;
}

bool SpiritHitMonster(Player &player, Monster &monster, DamageType type, Fixed26_6 damage, int ailmentPercent)
{
	return HitMonster(player, monster, type, damage, ailmentPercent);
}

bool BlinkReplacesDodge(const Player &player)
{
	return On(player, PersistentSkill::Blink);
}

void OnSpiritDodge(Player &player, Point start, Point end)
{
	if (!gbIsPoe2 || &player != MyPlayer || !On(player, PersistentSkill::TrailOfCaltrops) || start == end)
		return;
	const Direction dir = GetDirection(start, end);
	const int tiles = std::min(start.WalkingDistance(end), 3);
	for (int i = 0; i < tiles; i++) {
		if (Caltrops.size() >= MaxCaltrops)
			Caltrops.erase(Caltrops.begin());
		const Point position = start + Displacement { dir } * i;
		Caltrops.push_back({ position, CaltropTicks });
		AddVisualEffect(position, MissileGraphicID::MetalHit);
	}
}

std::string SpiritBuffPortraitText(const Player &player, PersistentSkill skill)
{
	if (!On(player, skill))
		return {};
	const SpiritRuntime &state = player.poe2.spiritState;
	switch (skill) {
	case PersistentSkill::WindDancer:
		return StrCat(state.windStages);
	case PersistentSkill::GhostDance:
		return StrCat(state.shrouds);
	case PersistentSkill::ArcticArmour:
		return StrCat(state.arcticStages);
	case PersistentSkill::Barkskin:
		if (const int armour = SpiritBuffArmour(player); armour > 0)
			return StrCat("+", armour);
		return {};
	case PersistentSkill::ElementalConflux:
		switch (static_cast<DamageType>(state.confluxElement)) {
		case DamageType::Fire:
			return "Fire";
		case DamageType::Lightning:
			return "Ltng";
		case DamageType::Magic:
			return "Cold";
		default:
			return {};
		}
	case PersistentSkill::TrailOfCaltrops:
		return Caltrops.empty() ? std::string {} : StrCat(Caltrops.size());
	case PersistentSkill::RagingSpirits:
		if (const size_t spirits = CountSkeletonMinions(player, MT_FIREBAT); spirits > 0)
			return StrCat(spirits);
		return {};
	case PersistentSkill::RavenousSwarm:
		if (const int swarms = SwarmCount(player); swarms > 0)
			return StrCat(swarms);
		return {};
	case PersistentSkill::LingeringIllusion:
		if (const size_t remnants = FrenzyRemnantCount(); remnants > 0)
			return StrCat(remnants);
		return {};
	default:
		return SpiritUsePortraitText(player, skill);
	}
}

void ProcessSpiritBuffs(Player &player)
{
	if (!gbIsPoe2 || &player != MyPlayer || player.hasNoLife())
		return;
	SpiritRuntime &state = player.poe2.spiritState;
	if (state.frenzyCooldown > 0)
		state.frenzyCooldown--;
	if (state.remnantCooldown > 0)
		state.remnantCooldown--;
	TickWindDancer(player, state);
	TickGhostDance(player, state);
	TickBarkskin(state);
	TickArcticArmour(player, state);
	TickEternalRage(player, state);
	TickConflux(player, state);
	TickAlchemist(player, state);
	if (leveltype == DTYPE_TOWN) {
		ClearSpiritBuffs();
		return;
	}
	TickAttrition(player);
	TickCaltrops(player);
	TickRemnants(player);
}

void ClearSpiritBuffs()
{
	Caltrops.clear();
	Remnants.clear();
	ClearSpiritUses();
}

size_t CaltropCount()
{
	return Caltrops.size();
}

size_t ManaRemnantCount()
{
	return Remnants.size();
}

} // namespace devilution
