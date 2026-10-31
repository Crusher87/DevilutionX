#include "poe2/spirit_auras.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

#include "engine/backbuffer_state.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "missiles.h"
#include "monster.h"
#include "objects.h"
#include "player.h"
#include "poe2/meta_gems.hpp"
#include "poe2/spirit.hpp"
#include "poe2/spirit_buffs.hpp"
#include "poe2/spirit_skills.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/vfx.hpp"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

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

bool OnForMe(const Player &player, PersistentSkill skill)
{
	return &player == MyPlayer && On(player, skill);
}

int GemLevel(const Player &player, PersistentSkill skill)
{
	return std::max(player.poe2.effectiveLevel(skill), 1);
}

bool CanHit(const Monster &monster)
{
	return !monster.hasNoLife() && !monster.isPlayerMinion() && monster.isPossibleToHit() && monster.mode != MonsterMode::Death;
}

int32_t Percent(int64_t raw, int percent)
{
	return static_cast<int32_t>(std::clamp<int64_t>(raw * percent / 100, 0, INT32_MAX / 2));
}

/** @brief The shield the hero holds, or nullptr. */
const Item *HeldShield(const Player &player)
{
	for (const inv_body_loc hand : { INVLOC_HAND_LEFT, INVLOC_HAND_RIGHT }) {
		const Item &item = player.InvBody[hand];
		if (!item.isEmpty() && item._itype == ItemType::Shield && item._iStatFlag)
			return &item;
	}
	return nullptr;
}

int ResonanceIndex(DamageType type)
{
	switch (type) {
	case DamageType::Fire:
		return 0;
	case DamageType::Lightning:
		return 1;
	case DamageType::Cold:
		return 2;
	default:
		return -1;
	}
}

int TotalResonance(const SpiritRuntime &state)
{
	return state.resonance[0] + state.resonance[1] + state.resonance[2];
}

void Heal(Player &player, Fixed26_6 heal)
{
	player._pHitPoints = std::min(player._pHitPoints + heal, player._pMaxHP);
	player._pHPBase = std::min(player._pHPBase + heal, player._pMaxHPBase);
	RedrawComponent(PanelDrawComponent::Health);
}

/** @brief Magma Barrier: fire sprays from the shield over the tiles ahead of the hero. */
void SprayMagma(Player &player)
{
	const Point center = player.position.tile;
	const Displacement facing { player._pdir };
	for (int tile = 1; tile <= MagmaBarrierTiles; tile++)
		AddVisualEffect(center + facing * tile, MissileGraphicID::MagmaBallExplosion);
	vfx::Impact(center + facing, vfx::Element::Fire, 1.5F);
	const Item *shield = HeldShield(player);
	const int armour = shield != nullptr ? std::max<int>(shield->_iAC, 0) : 0;
	const Fixed26_6 damage = Fixed26_6::fromInt(MagmaBarrierDamage(GemLevel(player, PersistentSkill::MagmaBarrier)) + (armour * MagmaBarrierArmourPercent / 100));
	TelemetryNoteDamageSource("Magma Barrier");
	// Ahead of the hero: within about 50 degrees of the way the hero faces.
	const auto inSpray = [&](Point target) {
		const Displacement offset = target - center;
		const int distance = center.WalkingDistance(target);
		if (distance < 1 || distance > MagmaBarrierTiles)
			return false;
		const int64_t dot = (static_cast<int64_t>(offset.deltaX) * facing.deltaX) + (static_cast<int64_t>(offset.deltaY) * facing.deltaY);
		const int64_t lengths = ((static_cast<int64_t>(offset.deltaX) * offset.deltaX) + (static_cast<int64_t>(offset.deltaY) * offset.deltaY))
		    * ((facing.deltaX * facing.deltaX) + (facing.deltaY * facing.deltaY));
		return dot > 0 && dot * dot * 100 >= lengths * 41;
	};
	// The spray smashes the barrels and urns it covers too, like the other area skills (Curtis 2026-10-22).
	for (int dy = -MagmaBarrierTiles; dy <= MagmaBarrierTiles; dy++) {
		for (int dx = -MagmaBarrierTiles; dx <= MagmaBarrierTiles; dx++) {
			if (inSpray(center + Displacement { dx, dy }))
				BreakObjectAt(player, center + Displacement { dx, dy });
		}
	}
	int hit = 0;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (!CanHit(monster) || !inSpray(monster.position.tile))
			continue;
		SpiritHitMonster(player, monster, DamageType::Fire, damage);
		hit++;
	}
	TelemetryLog("spirit", StrCat("Magma Barrier sprayed ", damage.whole(), " fire, hit ", hit));
}

void TickBriarpatch(Player &player, SpiritRuntime &state)
{
	if (state.briarCooldown > 0)
		state.briarCooldown--;
	if (state.briarTicks == 0)
		return;
	state.briarTicks--;
	if (state.briarTicks % BriarpatchPulseTicks != 0)
		return;
	for (int dx = -BriarpatchRadius; dx <= BriarpatchRadius; dx += BriarpatchRadius) {
		for (int dy = -BriarpatchRadius; dy <= BriarpatchRadius; dy += BriarpatchRadius)
			AddVisualEffect(state.briarPosition + Displacement { dx, dy }, MissileGraphicID::BoneHit);
	}
	TelemetryNoteDamageSource("Briarpatch");
	for (int dy = -BriarpatchRadius; dy <= BriarpatchRadius; dy++) {
		for (int dx = -BriarpatchRadius; dx <= BriarpatchRadius; dx++)
			BreakObjectAt(player, state.briarPosition + Displacement { dx, dy });
	}
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (!CanHit(monster) || state.briarPosition.WalkingDistance(monster.position.tile) > BriarpatchRadius)
			continue;
		monster.poe2.hinderTicks = std::max<uint8_t>(monster.poe2.hinderTicks, BriarpatchHinderTicks);
		SpiritHitMonster(player, monster, DamageType::Physical, Fixed26_6::fromRaw(state.briarPulse));
	}
}

void TickMagmaBarrier(const Player &player, SpiritRuntime &state)
{
	const bool on = On(player, PersistentSkill::MagmaBarrier) && HeldShield(player) != nullptr;
	state.blockPercent = on ? MagmaBarrierBlockPercent : 0;
	if (!on) {
		state.magmaTicks = static_cast<uint8_t>(MagmaBarrierChargeTicks(GemLevel(player, PersistentSkill::MagmaBarrier)));
		return;
	}
	if (state.magmaTicks > 0 && --state.magmaTicks == 0)
		AddVisualEffect(player.position.tile, MissileGraphicID::OrangeFlare);
}

void TickTimeOfNeed(Player &player, SpiritRuntime &state)
{
	if (!On(player, PersistentSkill::TimeOfNeed)) {
		state.needTicks = 0;
		return;
	}
	if (state.needTicks > 0) {
		state.needTicks--;
		return;
	}
	// It waits until the hero is hurt, then heals at once.
	if (player._pHitPoints >= player._pMaxHP)
		return;
	const int level = GemLevel(player, PersistentSkill::TimeOfNeed);
	const Fixed26_6 heal = Fixed26_6::fromRaw(Percent(player._pMaxHP.raw(), TimeOfNeedHealPercent(level)));
	Heal(player, heal);
	state.needTicks = static_cast<uint8_t>(std::clamp(TimeOfNeedTicks(level), 1, 255));
	AddVisualEffect(player.position.tile, MissileGraphicID::YellowFlareExplosion);
	vfx::Impact(player.position.tile, vfx::Element::Holy, 1.2F);
	TelemetryLog("spirit", StrCat("Time of Need healed ", heal.whole()));
}

void TickChargeRegulation(Player &player, SpiritRuntime &state)
{
	if (!On(player, PersistentSkill::ChargeRegulation)) {
		state.regulationTicks = 0;
		return;
	}
	if (state.regulationTicks > 0)
		state.regulationTicks--;
	// It takes the next Frenzy charge as the speed runs low, so none is wasted.
	Poe2PlayerData &data = player.poe2;
	if (state.regulationTicks <= ChargeRegulationRefillTicks && data.frenzyCharges > 0) {
		data.frenzyCharges--;
		state.regulationTicks = static_cast<uint16_t>(state.regulationTicks + ChargeRegulationTicksPerCharge);
		AddVisualEffect(player.position.tile, MissileGraphicID::YellowFlare);
		TelemetryLog("spirit", "Charge Regulation took a Frenzy charge");
	}
}

void TickWitheringPresence(const Player &player, SpiritRuntime &state)
{
	if (!On(player, PersistentSkill::WitheringPresence) || leveltype == DTYPE_TOWN) {
		state.witherPulseTicks = 0;
		return;
	}
	if (state.witherPulseTicks > 0) {
		state.witherPulseTicks--;
		return;
	}
	state.witherPulseTicks = static_cast<uint8_t>(std::clamp(WitheringPresencePulseTicks(GemLevel(player, PersistentSkill::WitheringPresence)), 1, 255));
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (!CanHit(monster) || player.position.tile.WalkingDistance(monster.position.tile) > PresenceTiles)
			continue;
		Poe2MonsterState &target = monster.poe2;
		// The aura adds stacks up to its own cap; Withering Touch can stack higher on top.
		if (target.witherStacks < WitheringPresenceMaxStacks)
			target.witherStacks++;
		target.witherTicks = std::max<uint8_t>(target.witherTicks, WitheringPresenceTicks);
		monster.tag(player);
		vfx::Sparks(monster.position.tile, vfx::Element::Chaos, 0.6F);
	}
}

void TickTrinity(const Player &player, SpiritRuntime &state)
{
	state.resonanceGained = 0;
	if (!On(player, PersistentSkill::Trinity)) {
		state.resonance = {};
		state.resonanceIdleTicks = 0;
		return;
	}
	if (state.resonanceIdleTicks < TrinityIdleTicks) {
		state.resonanceIdleTicks++;
		return;
	}
	// Then it fades 10 a second: a point every other tick.
	if (state.resonanceIdleTicks == TrinityIdleTicks) {
		state.resonanceIdleTicks++;
		return;
	}
	state.resonanceIdleTicks = TrinityIdleTicks;
	for (uint8_t &resonance : state.resonance) {
		if (resonance > 0)
			resonance--;
	}
}

std::string SecondsText(int ticks)
{
	return StrCat((ticks + 19) / 20, "s");
}

} // namespace

int BriarpatchCapPercent(int gemLevel)
{
	return AtLevel(gemLevel, 50, 150);
}

int MagmaBarrierChargeTicks(int gemLevel)
{
	return AtLevel(gemLevel, 160, 112);
}

int MagmaBarrierDamage(int gemLevel)
{
	return AtLevel(gemLevel, 15, 150);
}

int ScavengedPlatingThorns(int gemLevel)
{
	return AtLevel(gemLevel, 3, 25);
}

int TimeOfNeedTicks(int gemLevel)
{
	return AtLevel(gemLevel, 200, 162);
}

int TimeOfNeedHealPercent(int gemLevel)
{
	return AtLevel(gemLevel, 10, 25);
}

int ChargeRegulationSpeedPercent(int gemLevel)
{
	return AtLevel(gemLevel, 20, 30);
}

int WitheringPresencePulseTicks(int gemLevel)
{
	return AtLevel(gemLevel, 36, 28);
}

int TrinityResonanceGained(int gemLevel)
{
	return AtLevel(gemLevel, 6, 14);
}

int TrinityPercentPerStep(int gemLevel)
{
	return AtLevel(gemLevel, 1, 4);
}

int SiphonElementsChance(int gemLevel)
{
	return AtLevel(gemLevel, 5, 9);
}

int SiphonElementsPickupTiles(int gemLevel)
{
	return gemLevel >= 11 ? 2 : 1;
}

int HeraldOfBloodBleedChance(int gemLevel)
{
	return AtLevel(gemLevel, 40, 59);
}

void OnSpiritPhysicalAttack(const Player &player, int32_t physicalRaw)
{
	if (physicalRaw <= 0 || !OnForMe(player, PersistentSkill::Briarpatch))
		return;
	SpiritRuntime &state = player.poe2.spiritState;
	const int32_t cap = Percent(player._pMaxHP.raw(), BriarpatchCapPercent(GemLevel(player, PersistentSkill::Briarpatch)));
	state.briarStore = std::min(state.briarStore + Percent(physicalRaw, BriarpatchStorePercent), std::max(cap, state.briarStore));
}

void OnSpiritSpellCast(const Player &player)
{
	if (!OnForMe(player, PersistentSkill::Briarpatch) || leveltype == DTYPE_TOWN)
		return;
	SpiritRuntime &state = player.poe2.spiritState;
	if (state.briarCooldown > 0 || state.briarStore < 64)
		return;
	state.briarPosition = player.position.tile;
	state.briarTicks = BriarpatchTicks;
	state.briarPulse = std::max<int32_t>(state.briarStore / (BriarpatchTicks / BriarpatchPulseTicks), 1);
	state.briarCooldown = BriarpatchCooldownTicks;
	TelemetryLog("spirit", StrCat("Briarpatch spread ", state.briarStore / 64, " thorns"));
	state.briarStore = 0;
	vfx::Sigil(state.briarPosition, BriarpatchRadius, BriarpatchTicks * 50, vfx::Element::Physical);
	AddVisualEffect(state.briarPosition, MissileGraphicID::StoneCurseShatter);
}

void OnSpiritBlocked(Player &player)
{
	if (!OnForMe(player, PersistentSkill::MagmaBarrier) || HeldShield(player) == nullptr || leveltype == DTYPE_TOWN)
		return;
	SpiritRuntime &state = player.poe2.spiritState;
	if (state.magmaTicks > 0)
		return;
	SprayMagma(player);
	state.magmaTicks = static_cast<uint8_t>(MagmaBarrierChargeTicks(GemLevel(player, PersistentSkill::MagmaBarrier)));
}

void OnSpiritArmourBroken(const Player &player, const Monster &monster)
{
	if (!OnForMe(player, PersistentSkill::ScavengedPlating))
		return;
	SpiritRuntime &state = player.poe2.spiritState;
	// Stronger monsters give more plating: Monster Power.
	state.platingStacks = static_cast<uint8_t>(std::min(state.platingStacks + MonsterPower(monster), ScavengedPlatingMaxStacks));
	state.platingTicks = ScavengedPlatingTicks;
	AddVisualEffect(player.position.tile, MissileGraphicID::MetalHit);
}

void OnAuraMeleeHitTaken(Player &player, Monster &monster)
{
	if (!OnForMe(player, PersistentSkill::ScavengedPlating) || player.poe2.spiritState.platingStacks == 0 || !CanHit(monster))
		return;
	const int thorns = player.poe2.spiritState.platingStacks * ScavengedPlatingThorns(GemLevel(player, PersistentSkill::ScavengedPlating));
	TelemetryNoteDamageSource("Scavenged Plating");
	AddVisualEffect(monster.position.tile, MissileGraphicID::MetalHit);
	SpiritHitMonster(player, monster, DamageType::Physical, Fixed26_6::fromInt(thorns));
}

void OnSpiritElementalHit(const Player &player, DamageType type)
{
	const int index = ResonanceIndex(type);
	if (index < 0 || !OnForMe(player, PersistentSkill::Trinity))
		return;
	SpiritRuntime &state = player.poe2.spiritState;
	// One gain an element a tick, so a skill that hits a crowd doesn't fill it at once.
	const auto bit = static_cast<uint8_t>(1 << index);
	if ((state.resonanceGained & bit) != 0)
		return;
	state.resonanceGained |= bit;
	state.resonanceIdleTicks = 0;
	const int gained = TrinityResonanceGained(GemLevel(player, PersistentSkill::Trinity));
	for (size_t element = 0; element < state.resonance.size(); element++) {
		uint8_t &resonance = state.resonance[element];
		if (static_cast<int>(element) == index)
			resonance = static_cast<uint8_t>(std::min(resonance + gained, TrinityMaxResonance));
		else
			resonance = static_cast<uint8_t>(std::max(resonance - TrinityResonanceLost, 0));
	}
}

void OnSpiritElementalAilment(const Player &player, const Monster &monster, DamageType type)
{
	if (!OnForMe(player, PersistentSkill::SiphonElements) || monster.isPlayerMinion())
		return;
	uint8_t element = Infusion_Cold;
	if (type == DamageType::Fire)
		element = Infusion_Fire;
	else if (type == DamageType::Lightning)
		element = Infusion_Lightning;
	if (GenerateRnd(100) >= SiphonElementsChance(GemLevel(player, PersistentSkill::SiphonElements)))
		return;
	if (DropInfusionMote(player, monster.position.tile, element))
		TelemetryLog("spirit", "Siphon Elements left an infusion");
}

int ScavengedArmourPercent(const Player &player)
{
	if (!OnForMe(player, PersistentSkill::ScavengedPlating))
		return 0;
	return player.poe2.spiritState.platingStacks * ScavengedPlatingArmourPerStack;
}

int SpiritAuraSpeedPercent(const Player &player)
{
	if (!OnForMe(player, PersistentSkill::ChargeRegulation) || player.poe2.spiritState.regulationTicks == 0)
		return 0;
	return ChargeRegulationSpeedPercent(GemLevel(player, PersistentSkill::ChargeRegulation));
}

int TrinityDamagePercent(const Player &player, DamageType type)
{
	if (ResonanceIndex(type) < 0 || !OnForMe(player, PersistentSkill::Trinity))
		return 0;
	return TotalResonance(player.poe2.spiritState) / TrinityResonancePerStep * TrinityPercentPerStep(GemLevel(player, PersistentSkill::Trinity));
}

int InfusionPickupTiles(const Player &player)
{
	if (!OnForMe(player, PersistentSkill::SiphonElements))
		return 1;
	return SiphonElementsPickupTiles(GemLevel(player, PersistentSkill::SiphonElements));
}

std::string SpiritAuraPortraitText(const Player &player, PersistentSkill skill)
{
	if (!On(player, skill))
		return {};
	const SpiritRuntime &state = player.poe2.spiritState;
	switch (skill) {
	case PersistentSkill::Briarpatch: {
		if (state.briarTicks > 0)
			return SecondsText(state.briarTicks);
		const int32_t cap = Percent(player._pMaxHP.raw(), BriarpatchCapPercent(GemLevel(player, skill)));
		if (state.briarStore <= 0 || cap <= 0)
			return {};
		return StrCat(std::min<int64_t>(static_cast<int64_t>(state.briarStore) * 100 / cap, 100), "%");
	}
	case PersistentSkill::MagmaBarrier:
		if (HeldShield(player) == nullptr)
			return {};
		return state.magmaTicks > 0 ? SecondsText(state.magmaTicks) : std::string("Ready");
	case PersistentSkill::ScavengedPlating:
		return state.platingStacks > 0 ? StrCat(state.platingStacks) : std::string {};
	case PersistentSkill::TimeOfNeed:
		return state.needTicks > 0 ? SecondsText(state.needTicks) : std::string("Ready");
	case PersistentSkill::ChargeRegulation:
		return state.regulationTicks > 0 ? SecondsText(state.regulationTicks) : std::string {};
	case PersistentSkill::Trinity: {
		const int percent = TrinityDamagePercent(player, DamageType::Fire);
		return percent > 0 ? StrCat("+", percent, "%") : std::string {};
	}
	default:
		return {};
	}
}

void ProcessSpiritAuras(Player &player)
{
	if (!gbIsPoe2 || &player != MyPlayer || player.hasNoLife())
		return;
	SpiritRuntime &state = player.poe2.spiritState;
	// A gem switched off loses what it held.
	if (!On(player, PersistentSkill::Briarpatch)) {
		state.briarStore = 0;
		state.briarTicks = 0;
	}
	if (leveltype == DTYPE_TOWN)
		state.briarTicks = 0;
	if (!On(player, PersistentSkill::ScavengedPlating) || (state.platingTicks > 0 && --state.platingTicks == 0))
		state.platingStacks = 0;
	TickBriarpatch(player, state);
	TickMagmaBarrier(player, state);
	TickTimeOfNeed(player, state);
	TickChargeRegulation(player, state);
	TickWitheringPresence(player, state);
	TickTrinity(player, state);
}

void ClearSpiritAuras()
{
	if (MyPlayer == nullptr)
		return;
	SpiritRuntime &state = MyPlayer->poe2.spiritState;
	state.briarTicks = 0;
	state.platingStacks = 0;
	state.platingTicks = 0;
}

} // namespace devilution
