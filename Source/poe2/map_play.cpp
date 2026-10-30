#include "poe2/map_play.hpp"

#include <algorithm>
#include <string_view>
#include <vector>

#include "engine/random.hpp"
#include "game_mode.hpp"
#include "levels/tile_properties.hpp"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/mix.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/waystones.hpp"
#include "tables/misdat.h"
#include "utils/is_of.hpp"

namespace devilution {

namespace {

constexpr int TicksPerSecond = 20;

struct GroundPatch {
	Point position;
	MapDanger kind;
};

/** The local hero's map ailments and the level's ground patches; not saved, like the map's monsters' state. */
struct MapPlayState {
	int poisonTicks = 0;
	int32_t poisonPerTick = 0;
	int bleedTicks = 0;
	int32_t bleedPerTick = 0;
	uint32_t tick = 0;
	std::vector<GroundPatch> patches;
};

MapPlayState State;

int Danger(MapDanger danger)
{
	return ActiveMapEffects()[danger];
}

bool InMap()
{
	return gbIsPoe2 && ActiveMapTier() > 0;
}

/** @brief Whether a periodic penalty is on right now. */
bool PeriodicActive()
{
	return State.tick % MapPeriodicCycleTicks < MapPeriodicActiveTicks;
}

int Resistance(const Player &player, DamageType type)
{
	int resistance = 0;
	switch (type) {
	case DamageType::Fire:
		resistance = player._pFireResist;
		break;
	case DamageType::Lightning:
		resistance = player._pLghtResist;
		break;
	case DamageType::Magic:
		resistance = player._pMagResist;
		break;
	case DamageType::Chaos:
		// Penetration is elemental, as in POE2; chaos resistance has its own cap already.
		return player.poe2.chaosResistance;
	default:
		return 0;
	}
	return std::min(resistance - MapPenetration(), 75);
}

int32_t AfterResistance(const Player &player, DamageType type, int64_t raw)
{
	return static_cast<int32_t>(raw * (100 - Resistance(player, type)) / 100);
}

/** @brief The ground patch the hero stands in, if any. */
const GroundPatch *PatchAt(Point position)
{
	for (const GroundPatch &patch : State.patches) {
		if (patch.position.ManhattanDistance(position) <= MapPatchRadius)
			return &patch;
	}
	return nullptr;
}

/** @param poisonRaw The hit's physical and chaos damage before armour and resistance; 0 when it can't poison. */
void TryAilments(Player &player, int32_t hitRaw, int32_t poisonRaw)
{
	if (hitRaw <= 0)
		return;
	if (const int chance = Danger(MapDanger::PoisonChance); chance > 0 && poisonRaw > 0 && GenerateRnd(100) < chance) {
		// POE2: only the strongest poison deals damage. A weaker one is lost; one as strong or stronger takes over.
		const int32_t perTick = std::max<int32_t>(static_cast<int32_t>(static_cast<int64_t>(poisonRaw) * MapPoisonPercentPerSecond / 100 / TicksPerSecond), 1);
		if (State.poisonTicks == 0 || perTick >= State.poisonPerTick) {
			State.poisonTicks = perTick == State.poisonPerTick ? std::max(State.poisonTicks, MapPoisonTicks) : MapPoisonTicks;
			State.poisonPerTick = perTick;
		}
	}
	if (const int chance = Danger(MapDanger::BleedChance); chance > 0 && GenerateRnd(100) < chance) {
		const int32_t perTick = std::max<int32_t>(hitRaw * MapBleedPercentPerSecond / 100 / TicksPerSecond, 1);
		State.bleedPerTick = std::max(State.bleedPerTick, perTick);
		State.bleedTicks = MapBleedTicks;
	}
}

void DamageOverTime(Player &player, DamageType type, int64_t raw, std::string_view source)
{
	if (raw <= 0)
		return;
	TelemetryNoteAttacker(source);
	ApplyPlrDamage(type, player, Fixed26_6::fromRaw(static_cast<int32_t>(std::min<int64_t>(raw, INT32_MAX / 2))));
}

void DrawPatches(const Player &player)
{
	if (State.tick % 10 != 0)
		return;
	for (const GroundPatch &patch : State.patches) {
		if (patch.position.WalkingDistance(player.position.tile) > 14)
			continue;
		const uint32_t hash = Mix(State.tick ^ (static_cast<uint32_t>(patch.position.x) << 8) ^ static_cast<uint32_t>(patch.position.y));
		const Point at = patch.position + Displacement { static_cast<int>(hash % 5) - 2, static_cast<int>((hash / 5) % 5) - 2 };
		if (!InDungeonBounds(at) || IsTileSolid(at))
			continue;
		MissileGraphicID graphic = MissileGraphicID::FireArrowExplosion;
		if (patch.kind == MapDanger::ChilledGround)
			graphic = MissileGraphicID::StoneCurseShatter;
		else if (patch.kind == MapDanger::ShockedGround)
			graphic = MissileGraphicID::BlueFlareExplosion;
		AddVisualEffect(at, graphic);
	}
}

} // namespace

MapMonsterHit ApplyMapMonsterHit(Player &player, Fixed26_6 physical, Fixed26_6 beforeArmour)
{
	if (!InMap() || physical <= 0)
		return { physical, Fixed26_6::fromInt(0) };
	const int64_t raw = physical.raw();
	int64_t total = raw;
	// Armour break: the hit's armour-broken share lands as if the hero had none.
	total += raw * Danger(MapDanger::ArmourBreak) / 200;
	total += AfterResistance(player, DamageType::Fire, raw * Danger(MapDanger::ExtraFire) / 100);
	total += AfterResistance(player, DamageType::Magic, raw * Danger(MapDanger::ExtraCold) / 100);
	total += AfterResistance(player, DamageType::Lightning, raw * Danger(MapDanger::ExtraLightning) / 100);
	const int64_t chaos = AfterResistance(player, DamageType::Chaos, raw * Danger(MapDanger::ExtraChaos) / 100);
	// Poison counts the hit's physical and extra chaos from before armour and resistance.
	const int64_t poisonRaw = std::max<int64_t>(beforeArmour > 0 ? beforeArmour.raw() : raw, raw);
	TryAilments(player, static_cast<int32_t>(raw), static_cast<int32_t>(std::min<int64_t>(poisonRaw * (100 + Danger(MapDanger::ExtraChaos)) / 100, INT32_MAX / 2)));
	return { Fixed26_6::fromRaw(static_cast<int32_t>(std::min<int64_t>(total, INT32_MAX / 2))), Fixed26_6::fromRaw(static_cast<int32_t>(std::clamp<int64_t>(chaos, 0, INT32_MAX / 2))) };
}

void OnMapMonsterSpellHit(Player &player, Fixed26_6 damage, DamageType type)
{
	if (InMap())
		TryAilments(player, damage.raw(), IsAnyOf(type, DamageType::Physical, DamageType::Chaos) ? damage.raw() : 0);
}

int MapPenetration()
{
	return InMap() ? Danger(MapDanger::Penetration) : 0;
}

int MapPlayerDamagePercent()
{
	if (!InMap() || !PeriodicActive())
		return 100;
	return 100 - Danger(MapDanger::PlayerLessDamage);
}

int MapPlayerSlowPercent(const Player &player)
{
	if (!InMap())
		return 0;
	int slow = PeriodicActive() ? Danger(MapDanger::PlayerSlower) : 0;
	if (const GroundPatch *patch = PatchAt(player.position.tile); patch != nullptr && patch->kind == MapDanger::ChilledGround)
		slow += ChilledGroundSlowPercent;
	return slow;
}

int MapFlaskChargePercent()
{
	return InMap() ? 100 - Danger(MapDanger::FewerFlaskCharges) : 100;
}

int MapRecoveryPercent()
{
	return InMap() ? 100 - Danger(MapDanger::LessRecovery) : 100;
}

int MapSkillCostPercent()
{
	return InMap() ? 100 + Danger(MapDanger::MoreSkillCost) : 100;
}

int MapAuraPercent()
{
	return InMap() ? 100 - Danger(MapDanger::WeakerAuras) : 100;
}

int MapLeechPercent()
{
	return InMap() ? 100 - Danger(MapDanger::LessLeech) : 100;
}

int MapDamageTakenPercent(const Player &player)
{
	if (!InMap())
		return 100;
	if (const GroundPatch *patch = PatchAt(player.position.tile); patch != nullptr && patch->kind == MapDanger::ShockedGround)
		return 100 + ShockedGroundDamageTakenPercent;
	return 100;
}

int MapMonsterResistance()
{
	return InMap() ? Danger(MapDanger::MonsterResistance) : 0;
}

int MapExposurePercent()
{
	return InMap() ? 100 - Danger(MapDanger::LessExposure) : 100;
}

int MapCritBonusPercent()
{
	return InMap() ? 100 - Danger(MapDanger::LessCritDamageTaken) : 100;
}

int MapThresholdPercent()
{
	return InMap() ? 100 + Danger(MapDanger::MonsterThreshold) : 100;
}

bool MapMonsterExtraTick(Monster &monster)
{
	if (!InMap() || monster.isPlayerMinion() || monster.hasNoLife())
		return false;
	int haste = Danger(MapDanger::MonsterSpeed);
	if (GetMonsterAffixes(monster).rarity == MonsterRarity::Rare) {
		haste += Danger(MapDanger::RareSkillSpeed);
		if (IsAnyOf(monster.mode, MonsterMode::MoveNorthwards, MonsterMode::MoveSouthwards, MonsterMode::MoveSideways))
			haste += Danger(MapDanger::RareMoveSpeed);
	}
	if (haste <= 0)
		return false;
	monster.poe2.hasteCarry = static_cast<uint8_t>(monster.poe2.hasteCarry + std::min(haste, 99));
	if (monster.poe2.hasteCarry < 100)
		return false;
	monster.poe2.hasteCarry = static_cast<uint8_t>(monster.poe2.hasteCarry - 100);
	return true;
}

int MapMonsterRegenTenths()
{
	return InMap() ? Danger(MapDanger::MonsterRegen) : 0;
}

int MapExperiencePercent()
{
	if (!InMap())
		return 100;
	// Monster effectiveness gives 65% of its bonus as experience (bundle reward_axes).
	return 100 + ((ActiveMapEffects()[MapAxis::Effectiveness] - 100) * 65 / 100);
}

void ProcessMapPlayer(Player &player)
{
	if (&player != MyPlayer)
		return;
	if (!InMap()) {
		if (!State.patches.empty() || State.poisonTicks > 0 || State.bleedTicks > 0)
			ClearMapPlay();
		return;
	}
	State.tick++;
	if (player.hasNoLife())
		return;
	DrawPatches(player);
	// Poison is chaos damage over time: it meets chaos resistance and, like all damage over time, goes around energy
	// shield. Bleeding stays physical.
	if (State.poisonTicks > 0) {
		State.poisonTicks--;
		DamageOverTime(player, DamageType::Chaos, AfterResistance(player, DamageType::Chaos, State.poisonPerTick), "poison");
		if (State.poisonTicks == 0)
			State.poisonPerTick = 0;
	}
	if (State.bleedTicks > 0) {
		State.bleedTicks--;
		DamageOverTime(player, DamageType::Physical, State.bleedPerTick, "bleeding");
		if (State.bleedTicks == 0)
			State.bleedPerTick = 0;
	}
	if (const GroundPatch *patch = PatchAt(player.position.tile); patch != nullptr && patch->kind == MapDanger::BurningGround && State.tick % 5 == 0) {
		const int64_t burn = static_cast<int64_t>(player._pMaxHP.raw()) * BurningGroundLifePerMillePerSecond / 1000 / 4;
		DamageOverTime(player, DamageType::Fire, AfterResistance(player, DamageType::Fire, burn), "burning ground");
	}
}

void PlaceMapGroundPatches(uint32_t seed, Point entrance)
{
	State.patches.clear();
	if (!InMap())
		return;
	for (const MapDanger kind : { MapDanger::BurningGround, MapDanger::ChilledGround, MapDanger::ShockedGround }) {
		const int count = Danger(kind) * MapPatchesPerPoint;
		for (int i = 0, tries = 0; i < count && tries < count * 20; tries++) {
			seed = Mix(seed + 0x9E3779B9U);
			const Point at { 16 + static_cast<int>(seed % 80), 16 + static_cast<int>((seed >> 8) % 80) };
			if (IsTileSolid(at) || at.WalkingDistance(entrance) < 10 || PatchAt(at) != nullptr)
				continue;
			State.patches.push_back({ at, kind });
			i++;
		}
	}
}

void OnMapMonsterDeath(const Monster &monster)
{
	if (!InMap() || monster.isPlayerMinion() || MyPlayer == nullptr)
		return;
	const int points = Danger(MapDanger::DeathEffects);
	if (points <= 0 || GetMonsterAffixes(monster).rarity == MonsterRarity::Normal)
		return;
	AddVisualEffect(monster.position.tile, MissileGraphicID::BigExplosion);
	Player &player = *MyPlayer;
	if (player.hasNoLife() || player.position.tile.WalkingDistance(monster.position.tile) > 2)
		return;
	const int64_t blast = static_cast<int64_t>(monster.maxHitPoints.raw()) * DeathExplosionLifePercent * points / 100;
	const int64_t capped = std::min<int64_t>(blast, static_cast<int64_t>(player._pMaxHP.raw()) * DeathExplosionMaxPlayerLifePercent / 100);
	TelemetryNoteAttacker("a death explosion");
	ApplyPlrDamage(DamageType::Fire, player, Fixed26_6::fromRaw(AfterResistance(player, DamageType::Fire, capped)));
}

int MapPoisonTicksLeft()
{
	return State.poisonTicks;
}

void ClearMapPlay()
{
	State = {};
}

} // namespace devilution
