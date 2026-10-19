#include "poe2/spirit_uses.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/backbuffer_state.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/defenses.hpp"
#include "poe2/flasks.hpp"
#include "poe2/meta_gems.hpp"
#include "poe2/minion_gems.hpp"
#include "poe2/spirit.hpp"
#include "poe2/spirit_auras.hpp"
#include "poe2/spirit_buffs.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/unique_effects.hpp"
#include "poe2/vfx.hpp"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

struct GrimRemnant {
	Point position;
	uint16_t ticks;
	int32_t life;
};

std::vector<GrimRemnant> GrimRemnants;

constexpr std::array<PersistentSkill, 3> Banners { PersistentSkill::WarBanner, PersistentSkill::DefianceBanner, PersistentSkill::DreadBanner };

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

int32_t Percent(int64_t raw, int percent)
{
	return static_cast<int32_t>(std::clamp<int64_t>(raw * percent / 100, 0, INT32_MAX / 2));
}

/** @brief Iron Ward's and Plague Bearer's cap, in 1/64 points. */
int32_t StoreCap(const Player &player, PersistentSkill skill)
{
	return Percent(player._pMaxHP.raw(), SpiritStoreCapPercent(GemLevel(player, skill)));
}

/** @brief Which of the skeleton gems a minion belongs to, or -1. */
int SkeletonIndex(const Monster &minion)
{
	const MinionGemData *gem = FindMinionGemByType(minion.type().type);
	return gem != nullptr ? MinionCountSkillIndex(gem->skill) : -1;
}

/** @brief The life of a skeleton of that gem: the toughest one standing, else the last one that died. */
int32_t SkeletonLife(const Player &player, int index)
{
	int32_t life = player.poe2.spiritState.grimMinionLife[static_cast<size_t>(index)];
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		const Monster &monster = Monsters[ActiveMonsters[i]];
		if (monster.isPlayerMinion() && !monster.hasNoLife() && static_cast<size_t>(monster.goalVar3) == player.getId() && SkeletonIndex(monster) == index)
			life = std::max(life, monster.maxHitPoints.raw());
	}
	return life;
}

int32_t GrimFeastCap(const Player &player)
{
	int32_t toughest = 0;
	for (size_t i = 0; i < MinionCountSkills.size(); i++)
		toughest = std::max(toughest, SkeletonLife(player, static_cast<int>(i)));
	return Percent(toughest, GrimFeastCapPercent(GemLevel(player, PersistentSkill::GrimFeast)));
}

int BannerIndex(PersistentSkill skill)
{
	for (size_t i = 0; i < Banners.size(); i++) {
		if (Banners[i] == skill)
			return static_cast<int>(i);
	}
	return -1;
}

/** @brief Whether the spot is in the area of the hero's standing banner of that kind. */
bool InBanner(const Player &player, int kind, Point position)
{
	const SpiritRuntime &state = player.poe2.spiritState;
	return gbIsPoe2 && &player == MyPlayer && state.bannerKind == kind && state.bannerTicks > 0
	    && position.WalkingDistance(state.bannerPosition) <= BannerTiles;
}

bool InBanner(const Player &player, int kind)
{
	return InBanner(player, kind, player.position.tile);
}

void RingOfEffects(Point center, int radius, MissileGraphicID graphic)
{
	for (int dx = -radius; dx <= radius; dx++) {
		for (int dy = -radius; dy <= radius; dy++) {
			if (std::max(std::abs(dx), std::abs(dy)) == radius && (dx == 0 || dy == 0 || std::abs(dx) == std::abs(dy)))
				AddVisualEffect(center + Displacement { dx, dy }, graphic);
		}
	}
}

SpiritUseResult UseIronWard(Player &player)
{
	SpiritRuntime &state = player.poe2.spiritState;
	if (state.ironWardStore < 64)
		return SpiritUseResult::Empty;
	const int32_t cap = std::max<int32_t>(StoreCap(player, PersistentSkill::IronWard), 1);
	const int radius = IronWardMinRadius + ((IronWardMaxRadius - IronWardMinRadius) * std::min(state.ironWardStore, cap) / cap);
	const Fixed26_6 damage = Fixed26_6::fromRaw(state.ironWardStore);
	state.ironWardStore = 0;
	const Point center = player.position.tile;
	vfx::Slam(center, radius, vfx::Element::Physical);
	AddVisualEffect(center, MissileGraphicID::StoneCurseShatter);
	RingOfEffects(center, radius, MissileGraphicID::MetalHit);
	TelemetryNoteDamageSource("Iron Ward");
	int hit = 0;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (!CanHit(monster) || center.ExactDistance(monster.position.tile) > radius)
			continue;
		SpiritHitMonster(player, monster, DamageType::Physical, damage, IronWardStunPercent);
		hit++;
	}
	TelemetryLog("spirit", StrCat("Iron Ward released ", damage.whole(), " damage in ", radius, " tiles, hit ", hit));
	return SpiritUseResult::Used;
}

SpiritUseResult UsePlagueBearer(Player &player)
{
	SpiritRuntime &state = player.poe2.spiritState;
	const int32_t raw = state.plagueStore / 100;
	if (raw < 64)
		return SpiritUseResult::Empty;
	state.plagueStore = 0;
	const Point center = player.position.tile;
	vfx::Impact(center, vfx::Element::Chaos, 2.F);
	AddVisualEffect(center, MissileGraphicID::AcidSplat);
	RingOfEffects(center, PlagueBearerRadius, MissileGraphicID::AcidSplat);
	TelemetryNoteDamageSource("Plague Bearer");
	int hit = 0;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (!CanHit(monster) || center.ExactDistance(monster.position.tile) > PlagueBearerRadius)
			continue;
		hit++;
		if (!SpiritHitMonster(player, monster, DamageType::Physical, Fixed26_6::fromRaw(raw)))
			continue;
		// It always poisons, unless a stronger poison is on the monster already.
		Poe2MonsterState &target = monster.poe2;
		const int32_t perTick = std::max<int32_t>(Percent(raw, PlagueBearerPoisonPercent) / PlagueBearerPoisonTicks, 1);
		if (target.poisonTicks == 0 || target.poisonDamagePerTick <= perTick) {
			target.poisonDamagePerTick = perTick;
			target.poisonTicks = std::max<uint8_t>(target.poisonTicks, PlagueBearerPoisonTicks);
			target.poisonSource = static_cast<int8_t>(MyPlayerId);
		}
	}
	TelemetryLog("spirit", StrCat("Plague Bearer released ", raw / 64, " damage, hit ", hit));
	return SpiritUseResult::Used;
}

SpiritUseResult UseConvalescence(Player &player)
{
	SpiritRuntime &state = player.poe2.spiritState;
	Poe2PlayerData &data = player.poe2;
	if (state.convalescenceTicks > 0 || state.convalescenceCooldown > 0)
		return SpiritUseResult::Cooldown;
	if (data.maxEnergyShield <= 0 || data.energyShield >= data.maxEnergyShield * 64)
		return SpiritUseResult::ShieldFull;
	const Fixed26_6 cost = Fixed26_6::fromInt(ConvalescenceManaCost);
	if (player._pMana < cost)
		return SpiritUseResult::NoMana;
	const Fixed26_6 paid = PaySpellCostWithEnergyShield(player, cost);
	player._pMana -= paid;
	player._pManaBase -= paid;
	RedrawComponent(PanelDrawComponent::Mana);
	state.convalescenceTicks = static_cast<uint8_t>(std::clamp(ConvalescenceTicks(GemLevel(player, PersistentSkill::Convalescence)), 1, 255));
	data.energyShieldRechargeTicks = EnergyShieldRechargeDelayTicks;
	AddVisualEffect(player.position.tile, MissileGraphicID::ManaShield);
	TelemetryLog("spirit", "Convalescence started energy shield recharge");
	return SpiritUseResult::Used;
}

SpiritUseResult UseGrimFeast(Player &player)
{
	SpiritRuntime &state = player.poe2.spiritState;
	if (state.grimFeastCooldown > 0)
		return SpiritUseResult::Cooldown;
	struct Candidate {
		MinionRevives *revives;
		size_t index;
		int32_t life;
	};
	std::vector<Candidate> candidates;
	for (size_t gem = 0; gem < player.poe2.minionRevives.size(); gem++) {
		MinionRevives &revives = player.poe2.minionRevives[gem];
		const int32_t life = std::max<int32_t>(SkeletonLife(player, static_cast<int>(gem)), 64);
		for (size_t i = 0; i < revives.pending; i++) {
			if (revives.ticks[i] > 1)
				candidates.push_back({ &revives, i, life });
		}
	}
	if (candidates.empty())
		return SpiritUseResult::NothingToRaise;
	// Like POE2, the toughest minions rise first, as long as the stored life pays for them.
	std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) { return a.life > b.life; });
	int raised = 0;
	for (const Candidate &candidate : candidates) {
		if (state.grimFeastStore < candidate.life)
			continue;
		state.grimFeastStore -= candidate.life;
		candidate.revives->ticks[candidate.index] = 1;
		raised++;
	}
	if (raised == 0)
		return SpiritUseResult::Empty;
	state.grimFeastCooldown = GrimFeastCooldownTicks;
	AddVisualEffect(player.position.tile, MissileGraphicID::BloodStarRedExplosion);
	TelemetryLog("spirit", StrCat("Grim Feast raised ", raised, " skeletons"));
	return SpiritUseResult::Used;
}

SpiritUseResult UseBanner(Player &player, int kind)
{
	SpiritRuntime &state = player.poe2.spiritState;
	uint16_t &glory = state.glory[static_cast<size_t>(kind)];
	if (glory < GloryForBanner)
		return SpiritUseResult::NotEnoughGlory;
	glory = static_cast<uint16_t>(glory - GloryForBanner);
	// One banner stands at a time, like POE2: a new one takes the old one's place.
	state.bannerKind = static_cast<int8_t>(kind);
	state.bannerPosition = player.position.tile;
	state.bannerTicks = static_cast<uint8_t>(std::clamp(BannerTicks(GemLevel(player, Banners[static_cast<size_t>(kind)])), 1, 255));
	static constexpr std::array<vfx::Element, 3> Looks { vfx::Element::Fire, vfx::Element::Holy, vfx::Element::Arcane };
	vfx::Sigil(state.bannerPosition, BannerTiles, state.bannerTicks * 50, Looks[static_cast<size_t>(kind)]);
	AddVisualEffect(state.bannerPosition, MissileGraphicID::Resurrect);
	TelemetryLog("spirit", StrCat(PersistentSkillName(Banners[static_cast<size_t>(kind)]), " planted"));
	return SpiritUseResult::Used;
}

void TickGrimRemnants(Player &player)
{
	SpiritRuntime &state = player.poe2.spiritState;
	for (size_t i = 0; i < GrimRemnants.size();) {
		GrimRemnant &remnant = GrimRemnants[i];
		if (remnant.ticks % 15 == 0)
			AddVisualEffect(remnant.position, MissileGraphicID::RedFlare);
		if (player.position.tile.WalkingDistance(remnant.position) <= 1) {
			state.grimFeastStore = std::min(state.grimFeastStore + remnant.life, std::max(GrimFeastCap(player), state.grimFeastStore));
			AddVisualEffect(player.position.tile, MissileGraphicID::BloodStarRedExplosion);
			TelemetryLog("spirit", StrCat("picked up a Grim Feast remnant of ", remnant.life / 64, " life"));
			GrimRemnants.erase(GrimRemnants.begin() + static_cast<std::ptrdiff_t>(i));
			continue;
		}
		if (--remnant.ticks == 0) {
			GrimRemnants.erase(GrimRemnants.begin() + static_cast<std::ptrdiff_t>(i));
			continue;
		}
		i++;
	}
}

void TickBanner(Player &player, SpiritRuntime &state)
{
	if (state.bannerKind >= 0 && (state.bannerTicks == 0 || !On(player, Banners[static_cast<size_t>(state.bannerKind)]))) {
		state.bannerKind = -1;
		state.bannerTicks = 0;
	}
	if (state.bannerKind >= 0) {
		state.bannerTicks--;
		static constexpr std::array<MissileGraphicID, 3> Flares { MissileGraphicID::RedFlare, MissileGraphicID::YellowFlare, MissileGraphicID::BlueFlare };
		if (state.bannerTicks % 20 == 0)
			AddVisualEffect(state.bannerPosition, Flares[static_cast<size_t>(state.bannerKind)]);
	}
	// Dread Banner raises the resistance caps, which the hero's stats work out.
	const bool inDread = InBanner(player, 2);
	if (inDread != state.inDreadBanner) {
		state.inDreadBanner = inDread;
		CalcPlrInv(player, false);
	}
	if (!inDread) {
		state.bannerFlaskCarry = 0;
		return;
	}
	state.bannerFlaskCarry = static_cast<uint16_t>(state.bannerFlaskCarry + DreadBannerChargesMilliPerSecond);
	if (state.bannerFlaskCarry < 1000 * 20)
		return;
	state.bannerFlaskCarry = static_cast<uint16_t>(state.bannerFlaskCarry - (1000 * 20));
	for (size_t slot = 0; slot < NumFlasks; slot++) {
		uint8_t &charges = player.poe2.flaskCharges[slot];
		charges = static_cast<uint8_t>(std::min<int>(MaxFlaskChargesFor(player.SpdList[slot]), charges + 1));
	}
}

std::string PercentText(int32_t value, int32_t cap)
{
	if (value <= 0 || cap <= 0)
		return {};
	return StrCat(std::min<int64_t>(static_cast<int64_t>(value) * 100 / cap, 100), "%");
}

std::string SecondsText(int ticks)
{
	return StrCat((ticks + 19) / 20, "s");
}

} // namespace

int IronWardStorePercent(int gemLevel)
{
	return AtLevel(gemLevel, 100, 480);
}

int SpiritStoreCapPercent(int gemLevel)
{
	return AtLevel(gemLevel, 100, 300);
}

int ConvalescenceTicks(int gemLevel)
{
	return AtLevel(gemLevel, 60, 79);
}

int GrimFeastCapPercent(int gemLevel)
{
	return AtLevel(gemLevel, 200, 500);
}

int BannerTicks(int gemLevel)
{
	return AtLevel(gemLevel, 120, 196);
}

bool IsSpiritUseGem(PersistentSkill skill)
{
	return skill >= PersistentSkill::IronWard && skill <= PersistentSkill::DreadBanner;
}

SpiritUseResult UseSpiritGem(Player &player, PersistentSkill skill)
{
	if (!On(player, skill))
		return SpiritUseResult::Off;
	if (skill == PersistentSkill::Convalescence)
		return UseConvalescence(player);
	if (leveltype == DTYPE_TOWN)
		return SpiritUseResult::Town;
	switch (skill) {
	case PersistentSkill::IronWard:
		return UseIronWard(player);
	case PersistentSkill::PlagueBearer:
		return UsePlagueBearer(player);
	case PersistentSkill::GrimFeast:
		return UseGrimFeast(player);
	default:
		return UseBanner(player, std::max(BannerIndex(skill), 0));
	}
}

bool SpiritGemReady(const Player &player, PersistentSkill skill)
{
	if (!On(player, skill))
		return false;
	const SpiritRuntime &state = player.poe2.spiritState;
	if (skill == PersistentSkill::Convalescence) {
		const Poe2PlayerData &data = player.poe2;
		return state.convalescenceTicks == 0 && state.convalescenceCooldown == 0 && data.maxEnergyShield > 0
		    && data.energyShield < data.maxEnergyShield * 64 && player._pMana >= Fixed26_6::fromInt(ConvalescenceManaCost);
	}
	if (leveltype == DTYPE_TOWN)
		return false;
	switch (skill) {
	case PersistentSkill::IronWard:
		return state.ironWardStore >= 64;
	case PersistentSkill::PlagueBearer:
		return state.plagueStore / 100 >= 64;
	case PersistentSkill::GrimFeast:
		return state.grimFeastCooldown == 0 && state.grimFeastStore > 0;
	default:
		return state.glory[static_cast<size_t>(std::max(BannerIndex(skill), 0))] >= GloryForBanner;
	}
}

SpiritStore SpiritGemStore(const Player &player, PersistentSkill skill)
{
	const SpiritRuntime &state = player.poe2.spiritState;
	switch (skill) {
	case PersistentSkill::IronWard:
		return { state.ironWardStore / 64, StoreCap(player, skill) / 64 };
	case PersistentSkill::PlagueBearer:
		return { state.plagueStore / 6400, StoreCap(player, skill) / 64 };
	case PersistentSkill::GrimFeast:
		return { state.grimFeastStore / 64, GrimFeastCap(player) / 64 };
	case PersistentSkill::WarBanner:
	case PersistentSkill::DefianceBanner:
	case PersistentSkill::DreadBanner:
		return { state.glory[static_cast<size_t>(BannerIndex(skill))], GloryForBanner };
	default:
		return { 0, 0 };
	}
}

void OnSpiritArmourStopped(const Player &player, Fixed26_6 stopped)
{
	if (&player != MyPlayer || stopped <= 0 || !On(player, PersistentSkill::IronWard))
		return;
	SpiritRuntime &state = player.poe2.spiritState;
	const int64_t stored = static_cast<int64_t>(stopped.raw()) * IronWardStorePercent(GemLevel(player, PersistentSkill::IronWard)) / 100;
	state.ironWardStore = static_cast<int32_t>(std::min<int64_t>(state.ironWardStore + stored, std::max(StoreCap(player, PersistentSkill::IronWard), state.ironWardStore)));
}

void OnSpiritPoisonDealt(const Player &player, Fixed26_6 damage)
{
	if (&player != MyPlayer || damage <= 0 || !On(player, PersistentSkill::PlagueBearer))
		return;
	// Kept in hundredths, so the small share of each poison tick isn't lost to rounding.
	SpiritRuntime &state = player.poe2.spiritState;
	const int64_t cap = static_cast<int64_t>(StoreCap(player, PersistentSkill::PlagueBearer)) * 100;
	state.plagueStore = static_cast<int32_t>(std::min<int64_t>(state.plagueStore + (static_cast<int64_t>(damage.raw()) * PlagueBearerStorePercent), std::max<int64_t>(cap, state.plagueStore)));
}

void OnSpiritMinionDied(const Player &owner, const Monster &minion)
{
	if (!gbIsPoe2 || &owner != MyPlayer)
		return;
	const int index = SkeletonIndex(minion);
	if (index < 0)
		return;
	owner.poe2.spiritState.grimMinionLife[static_cast<size_t>(index)] = minion.maxHitPoints.raw();
	if (!On(owner, PersistentSkill::GrimFeast) || leveltype == DTYPE_TOWN || owner.position.tile.WalkingDistance(minion.position.tile) > GrimFeastTiles)
		return;
	if (GrimRemnants.size() >= MaxGrimRemnants)
		GrimRemnants.erase(GrimRemnants.begin());
	GrimRemnants.push_back({ minion.position.tile, GrimFeastRemnantTicks, Percent(minion.maxHitPoints.raw(), GrimFeastRemnantPercent) });
	AddVisualEffect(minion.position.tile, MissileGraphicID::BloodStarRed);
}

void OnSpiritAttackHit(const Player &player, const Monster &monster)
{
	if (!gbIsPoe2 || &player != MyPlayer)
		return;
	SpiritRuntime &state = player.poe2.spiritState;
	for (size_t kind = 0; kind < Banners.size(); kind++) {
		if (On(player, Banners[kind]))
			state.glory[kind] = static_cast<uint16_t>(std::min(state.glory[kind] + MonsterPower(monster), MaxGlory));
	}
}

bool RechargeCantBeInterrupted(const Player &player)
{
	return gbIsPoe2 && &player == MyPlayer && player.poe2.spiritState.convalescenceTicks > 0;
}

int BannerAttackDamagePercent(const Player &player)
{
	return InBanner(player, 0) ? WarBannerDamagePercent : 0;
}

int BannerAccuracyPercent(const Player &player)
{
	return InBanner(player, 0) ? WarBannerAccuracyPercent : 0;
}

int BannerAttackSpeedPercent(const Player &player)
{
	return InBanner(player, 0) ? WarBannerAttackSpeedPercent : 0;
}

int BannerMinionDamagePercent(const Player &owner, const Monster &minion)
{
	return InBanner(owner, 0, minion.position.tile) ? WarBannerDamagePercent : 0;
}

int BannerDefencePercent(const Player &player)
{
	return InBanner(player, 1) ? DefianceBannerDefencePercent : 0;
}

int BannerWalkSkippedFrames(const Player &player, int walkFrames)
{
	if (!InBanner(player, 1))
		return 0;
	// A tile takes the frames left after the usual 2 skipped; 30% faster takes 100/130 of them.
	const int frames = std::max(walkFrames - 2, 1);
	const int extraHundredths = (frames * 100) - (frames * 100 * 100 / (100 + DefianceBannerMovementPercent));
	SpiritRuntime &state = player.poe2.spiritState;
	const int total = state.walkCarry + extraHundredths;
	state.walkCarry = static_cast<uint8_t>(total % 100);
	return total / 100;
}

int BannerMaxResistance(const Player &player)
{
	return gbIsPoe2 && &player == MyPlayer && player.poe2.spiritState.inDreadBanner ? DreadBannerMaxResistance : 0;
}

std::string SpiritUsePortraitText(const Player &player, PersistentSkill skill)
{
	if (!On(player, skill))
		return {};
	const SpiritRuntime &state = player.poe2.spiritState;
	switch (skill) {
	case PersistentSkill::IronWard:
		return PercentText(state.ironWardStore, StoreCap(player, skill));
	case PersistentSkill::PlagueBearer:
		return PercentText(state.plagueStore / 100, StoreCap(player, skill));
	case PersistentSkill::GrimFeast:
		return PercentText(state.grimFeastStore, GrimFeastCap(player));
	case PersistentSkill::Convalescence:
		if (state.convalescenceTicks > 0)
			return SecondsText(state.convalescenceTicks);
		return state.convalescenceCooldown > 0 ? SecondsText(state.convalescenceCooldown) : std::string {};
	case PersistentSkill::WarBanner:
	case PersistentSkill::DefianceBanner:
	case PersistentSkill::DreadBanner: {
		const int kind = BannerIndex(skill);
		if (state.bannerKind == kind && state.bannerTicks > 0)
			return SecondsText(state.bannerTicks);
		return PercentText(state.glory[static_cast<size_t>(kind)], GloryForBanner);
	}
	default:
		return SpiritAuraPortraitText(player, skill);
	}
}

void ProcessSpiritUses(Player &player)
{
	if (!gbIsPoe2 || &player != MyPlayer || player.hasNoLife())
		return;
	SpiritRuntime &state = player.poe2.spiritState;
	// A gem switched off loses what it held.
	if (!On(player, PersistentSkill::IronWard))
		state.ironWardStore = 0;
	if (!On(player, PersistentSkill::PlagueBearer))
		state.plagueStore = 0;
	if (!On(player, PersistentSkill::GrimFeast)) {
		state.grimFeastStore = 0;
		GrimRemnants.clear();
	}
	for (size_t kind = 0; kind < Banners.size(); kind++) {
		if (!On(player, Banners[kind]))
			state.glory[kind] = 0;
	}
	if (state.grimFeastCooldown > 0)
		state.grimFeastCooldown--;
	if (state.convalescenceTicks > 0) {
		// It ends early once the shield is full, and its cooldown starts when it ends.
		const Poe2PlayerData &data = player.poe2;
		if (--state.convalescenceTicks == 0 || data.energyShield >= data.maxEnergyShield * 64) {
			state.convalescenceTicks = 0;
			state.convalescenceCooldown = ConvalescenceCooldownTicks;
		}
	} else if (state.convalescenceCooldown > 0) {
		state.convalescenceCooldown--;
	}
	if (leveltype == DTYPE_TOWN) {
		GrimRemnants.clear();
		state.bannerKind = -1;
		state.bannerTicks = 0;
	}
	TickBanner(player, state);
	TickGrimRemnants(player);
}

void ClearSpiritUses()
{
	GrimRemnants.clear();
	if (MyPlayer == nullptr)
		return;
	SpiritRuntime &state = MyPlayer->poe2.spiritState;
	state.bannerKind = -1;
	state.bannerTicks = 0;
}

size_t GrimRemnantCount()
{
	return GrimRemnants.size();
}

} // namespace devilution
