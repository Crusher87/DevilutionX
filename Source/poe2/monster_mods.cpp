#include "poe2/monster_mods.hpp"

#include <algorithm>
#include <array>

#include "engine/random.hpp"
#include "game_mode.hpp"
#include "missiles.h"
#include "monster.h"
#include "multi.h"
#include "player.h"
#include "poe2/map_play.hpp"
#include "poe2/meta_gems.hpp"
#include "poe2/mix.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/spirit_buffs.hpp"
#include "poe2/support_table.hpp"
#include "poe2/table_skills.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/unique_effects.hpp"
#include "poe2/vfx.hpp"
#include "poe2/waystones.hpp"
#include "tables/misdat.h"
#include "utils/language.h"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

constexpr int TicksPerSecond = 20;

/** @brief The map tier's life and damage scaling: every plain monster, risen ones too (they come out at the map's strength). */
bool CanTakeMapScaling(const Monster &monster)
{
	if (!gbIsPoe2 || monster.isUnique() || monster.isPlayerMinion())
		return false;
	const _monster_id type = monster.type().type;
	return type != MT_GOLEM && type != MT_DIABLO && type != MT_NAKRUL;
}

/** @brief The magic and rare roll: a monster risen on Leoric's Road never rolls one. */
bool CanRollRarity(const Monster &monster)
{
	return CanTakeMapScaling(monster) && (monster.flags & MFLAG_RISEN) == 0;
}

uint8_t ChooseMods(uint32_t hash, int count)
{
	constexpr int NumMods = static_cast<int>(MonsterMod::Count);
	uint8_t mods = 0;
	for (int picked = 0, i = 0; picked < count && i < 16; i++) {
		const int mod = static_cast<int>(Mix(hash + static_cast<uint32_t>(i)) % NumMods);
		if ((mods & (1U << mod)) != 0)
			continue;
		mods |= static_cast<uint8_t>(1U << mod);
		picked++;
	}
	return mods;
}

/** @brief The life and damage a magic or rare monster gets on top of a plain one. */
void ApplyRarityStats(Monster &monster, const MonsterAffixes &affixes)
{
	if (affixes.rarity == MonsterRarity::Normal)
		return;

	int lifePercent = 100 + (affixes.rarity == MonsterRarity::Rare ? RareMonsterLifePercent : MagicMonsterLifePercent);
	if (affixes.has(MonsterMod::ExtraLife))
		lifePercent += ExtraLifePercent;
	monster.maxHitPoints = Fixed26_6::fromRaw(static_cast<int32_t>(std::min<int64_t>(static_cast<int64_t>(monster.maxHitPoints.raw()) * lifePercent / 100, INT32_MAX / 4)));
	monster.hitPoints = monster.maxHitPoints;

	// MonsterMod::HardHitting scales each hit (MonsterModsDamagePercent), so the damage bytes stay the monster's data.
	// MonsterMod::Resistant adds to the monster's resistances where they are worked out (monster_resistance.cpp).
}

/** @brief Undoes ApplyRarityStats (to within rounding). */
void RemoveRarityStats(Monster &monster, const MonsterAffixes &affixes)
{
	int lifePercent = 100 + (affixes.rarity == MonsterRarity::Rare ? RareMonsterLifePercent : MagicMonsterLifePercent);
	if (affixes.has(MonsterMod::ExtraLife))
		lifePercent += ExtraLifePercent;
	monster.maxHitPoints = Fixed26_6::fromRaw(static_cast<int32_t>(static_cast<int64_t>(monster.maxHitPoints.raw()) * 100 / lifePercent));
	monster.hitPoints = monster.maxHitPoints;

	if (affixes.has(MonsterMod::HardHitting)) {
		const auto undo = [](uint8_t &damage) { damage = static_cast<uint8_t>(damage * 100 / (100 + HardHittingPercent)); };
		undo(monster.minDamage);
		undo(monster.maxDamage);
		undo(monster.minDamageSpecial);
		undo(monster.maxDamageSpecial);
	}
}

} // namespace

MonsterAffixes GetMonsterAffixes(const Monster &monster)
{
	MonsterAffixes affixes;
	if (!CanRollRarity(monster))
		return affixes;
	const uint32_t hash = Mix(monster.rndItemSeed ^ 0x6D6F6E73U);
	const int roll = static_cast<int>(hash % 1000);
	const int mapTier = ActiveMapTier();
	int rarePerMille = RareMonsterPerMille + (mapTier * MapRarePerMillePerTier);
	int magicPerMille = MagicMonsterPerMille + (mapTier * MapMagicPerMillePerTier);
	if (mapTier > 0) {
		// The map's monster rarity and its "more magic and rare monsters" danger.
		const MapEffects &map = ActiveMapEffects();
		const int percent = map[MapAxis::MonsterRarity] * (100 + map[MapDanger::MoreMagicRare]) / 100;
		rarePerMille = std::min(rarePerMille * percent / 100, 400);
		magicPerMille = std::min(magicPerMille * percent / 100, 900 - rarePerMille);
	}
	if (roll < rarePerMille) {
		affixes.rarity = MonsterRarity::Rare;
		int count = RareMonsterModCount;
		if (const int extra = ActiveMapEffects()[MapDanger::RareExtraMod]; extra > 0 && static_cast<int>((hash >> 12) % 100) < extra)
			count++;
		affixes.mods = ChooseMods(hash, std::min(count, static_cast<int>(MonsterMod::Count)));
	} else if (roll < rarePerMille + magicPerMille) {
		affixes.rarity = MonsterRarity::Magic;
		affixes.mods = ChooseMods(hash, 1);
	}
	return affixes;
}

void ApplyMonsterRarity(Monster &monster)
{
	monster.poe2 = {};
	if (const int mapTier = ActiveMapTier(); mapTier > 0 && CanTakeMapScaling(monster)) {
		const MapEffects &map = ActiveMapEffects();
		// The tier, the waystone's life and energy shield (as life), and monster effectiveness all multiply.
		int64_t lifePercent = 100 + (mapTier * MapLifePercentPerTier);
		lifePercent = lifePercent * (100 + map[MapDanger::MonsterLife]) / 100;
		lifePercent = lifePercent * (100 + map[MapDanger::MonsterEnergyShield]) / 100;
		lifePercent = lifePercent * map[MapAxis::Effectiveness] / 100;
		monster.maxHitPoints = Fixed26_6::fromRaw(static_cast<int32_t>(std::min<int64_t>(static_cast<int64_t>(monster.maxHitPoints.raw()) * lifePercent / 100, INT32_MAX / 4)));
		monster.hitPoints = monster.maxHitPoints;
		const int defencePercent = 100 + map[MapDanger::MonsterArmour] + map[MapDanger::MonsterEvasion];
		monster.armorClass = static_cast<uint8_t>(std::min(monster.armorClass * defencePercent / 100, 255));
		// The map's damage scales each hit (MonsterModsDamagePercent), past the old 255 cap of the bytes.
	}
	ApplyRarityStats(monster, GetMonsterAffixes(monster));
}

int MonsterModsDamagePercent(const Monster &monster)
{
	if (!gbIsPoe2)
		return 100;
	int64_t percent = 100;
	if (const int mapTier = ActiveMapTier(); mapTier > 0 && CanTakeMapScaling(monster)) {
		percent = percent * (100 + (mapTier * MapDamagePercentPerTier)) / 100;
		percent = percent * (100 + ActiveMapEffects()[MapDanger::MonsterDamage]) / 100;
	}
	if (GetMonsterAffixes(monster).has(MonsterMod::HardHitting))
		percent = percent * (100 + HardHittingPercent) / 100;
	return static_cast<int>(std::clamp<int64_t>(percent, 0, 100000));
}

void PromoteMonsterRarity(Monster &monster, MonsterRarity rarity)
{
	if (!CanRollRarity(monster) || rarity == MonsterRarity::Normal || GetMonsterAffixes(monster).rarity != MonsterRarity::Normal)
		return;
	const uint32_t original = monster.rndItemSeed;
	// A rare seed turns up about once in 33 tries, so this always finds one long before it runs out.
	for (uint32_t step = 1; step < 4000; step++) {
		monster.rndItemSeed = (original + (step * 7919U)) & 0x7FFFFFFFU;
		const MonsterAffixes affixes = GetMonsterAffixes(monster);
		if (affixes.rarity == rarity) {
			ApplyRarityStats(monster, affixes);
			return;
		}
	}
	monster.rndItemSeed = original;
}

void DemoteMonsterRarity(Monster &monster)
{
	const MonsterAffixes affixes = GetMonsterAffixes(monster);
	if (affixes.rarity == MonsterRarity::Normal)
		return;
	const uint32_t original = monster.rndItemSeed;
	for (uint32_t step = 1; step < 4000; step++) {
		monster.rndItemSeed = (original + (step * 7919U)) & 0x7FFFFFFFU;
		if (GetMonsterAffixes(monster).rarity == MonsterRarity::Normal) {
			RemoveRarityStats(monster, affixes);
			return;
		}
	}
	monster.rndItemSeed = original;
}

std::string_view MonsterModName(MonsterMod mod)
{
	switch (mod) {
	case MonsterMod::ExtraLife:
		return _("Extra Life");
	case MonsterMod::HardHitting:
		return _("Hard Hitting");
	case MonsterMod::Regenerating:
		return _("Regenerating");
	case MonsterMod::Resistant:
		return _("Elemental Resistance");
	default:
		return {};
	}
}

std::string RareMonsterName(const Monster &monster)
{
	static constexpr std::array<std::string_view, 16> First { "Grim", "Blood", "Doom", "Gloom", "Rot", "Dread", "Bone", "Ash", "Wrath", "Storm", "Pain", "Night", "Hate", "Death", "Corpse", "Plague" };
	static constexpr std::array<std::string_view, 16> Second { "Maw", "Fang", "Song", "Spawn", "Heart", "Howl", "Gnash", "Scar", "Grip", "Thirst", "Mangler", "Wail", "Bane", "Hunger", "Rend", "Shroud" };
	const uint32_t hash = Mix(monster.rndItemSeed ^ 0x6E616D65U);
	return StrCat(First[hash % First.size()], " ", Second[(hash / First.size()) % Second.size()]);
}

int MonsterExperiencePercent(const Monster &monster)
{
	int percent = 100;
	switch (GetMonsterAffixes(monster).rarity) {
	case MonsterRarity::Magic:
		percent = 150;
		break;
	case MonsterRarity::Rare:
		percent = 300;
		break;
	default:
		break;
	}
	return percent * MapExperiencePercent() / 100;
}

int MonsterExtraDrops(const Monster &monster)
{
	switch (GetMonsterAffixes(monster).rarity) {
	case MonsterRarity::Magic:
		return 1;
	case MonsterRarity::Rare:
		return 2;
	default:
		return 0;
	}
}

void ProcessMonsterAilments(Monster &monster)
{
	if (!gbIsPoe2 || monster.hasNoLife())
		return;
	Poe2MonsterState &state = monster.poe2;

	int regenPerMille = GetMonsterAffixes(monster).has(MonsterMod::Regenerating) ? RegenPerMillePerSecond : 0;
	if (!monster.isPlayerMinion())
		regenPerMille += MapMonsterRegenTenths();
	if (monster.hitPoints < monster.maxHitPoints && regenPerMille > 0) {
		constexpr int64_t Divisor = 1000 * TicksPerSecond;
		const int64_t total = static_cast<int64_t>(monster.maxHitPoints.raw()) * regenPerMille + state.regenRemainder;
		state.regenRemainder = static_cast<int>(total % Divisor);
		monster.hitPoints = std::min(monster.hitPoints + Fixed26_6::fromRaw(static_cast<int32_t>(total / Divisor)), monster.maxHitPoints);
	}

	if (state.shockTicks > 0)
		state.shockTicks--;
	if (state.hinderTicks > 0)
		state.hinderTicks--;
	if (state.chillTicks > 0 && --state.chillTicks == 0) {
		state.chillPercent = 0;
		state.chillCarry = 0;
	}
	if (state.freezeTicks > 0)
		state.freezeTicks--;
	if (state.heavyStunTicks > 0)
		state.heavyStunTicks--;
	if (state.amplifyTicks > 0 && --state.amplifyTicks % 20 == 0)
		AddVisualEffect(monster.position.tile, MissileGraphicID::BloodHit);
	if (state.armourBreakTicks > 0)
		state.armourBreakTicks--;
	ProcessTableCurses(monster);
	ProcessSupportMonsterEffects(monster);
	const auto decay = [&](int32_t &buildup, int thresholdPercent) {
		if (buildup <= 0)
			return;
		const int64_t threshold = static_cast<int64_t>(monster.maxHitPoints.raw()) * thresholdPercent / 100;
		const int32_t perTick = std::max<int32_t>(static_cast<int32_t>(threshold * BuildupDecayPercentPerSecond / 100 / TicksPerSecond), 1);
		buildup = std::max<int32_t>(buildup - perTick, 0);
	};
	decay(state.freezeBuildup, FreezeThresholdPercent);
	decay(state.stunBuildup, HeavyStunThresholdPercent);

	if (state.flammabilityTicks > 0 && --state.flammabilityTicks == 0)
		state.flammability = 0;
	if (state.igniteTicks > 0) {
		state.igniteTicks--;
		// Modern Effects draw a slow burn on the body; without them a small flame now and then marks the ignite.
		if (state.igniteTicks % 30 == 0 && !vfx::Enabled())
			AddVisualEffect(monster.position.tile, MissileGraphicID::FireArrowExplosion);
		// Only the player who lit the fire applies its damage, which the game then syncs.
		if (state.igniteSource >= 0 && state.igniteSource == static_cast<int8_t>(MyPlayerId) && MyPlayer != nullptr) {
			TelemetryNoteDamageSource("ignite");
			ApplyMonsterDamage(DamageType::Fire, monster, Fixed26_6::fromRaw(state.igniteDamagePerTick));
			if (monster.hasNoLife())
				M_StartKill(monster, *MyPlayer);
		}
	}
	ProcessUniqueMonsterAilments(monster);
}

Fixed26_6 ApplyShock(const Monster &monster, Fixed26_6 damage)
{
	if (!gbIsPoe2 || monster.poe2.shockTicks == 0)
		return damage;
	const Fixed26_6 shocked = Fixed26_6::fromRaw(damage.raw() + damage.raw() * ShockDamagePercent * MapExposurePercent() / 10000);
	TelemetryHitStep(false, "after shock", shocked.raw(), false, true);
	return shocked;
}

int AilmentChancePerMille(const Monster &monster, DamageType damageType, Fixed26_6 damage)
{
	if (monster.maxHitPoints <= 0 || damage <= 0)
		return 0;
	// The ailment threshold is the monster's life; uniques have half, so their ailments still land. Overwhelming
	// Presence lowers it near the hero.
	const int64_t life = monster.isUnique() ? monster.maxHitPoints.raw() / 2 : monster.maxHitPoints.raw();
	const int64_t threshold = std::max<int64_t>(life * PresenceThresholdPercent(monster, false) / 100, 1);
	const int64_t perMilleAtThreshold = damageType == DamageType::Fire ? 200 : 250;
	return static_cast<int>(std::min<int64_t>(static_cast<int64_t>(damage.raw()) * perMilleAtThreshold / threshold, 1000));
}

bool IsMonsterImmobilized(const Monster &monster)
{
	return gbIsPoe2 && (monster.poe2.freezeTicks > 0 || monster.poe2.heavyStunTicks > 0 || IsMonsterElectrocuted(monster));
}

bool MonsterSkipsTick(Monster &monster)
{
	if (!gbIsPoe2 || monster.hasNoLife())
		return false;
	Poe2MonsterState &state = monster.poe2;
	// Leoric's Road: a risen monster can't act in its first second; a skeleton's rising animation plays meanwhile.
	if (state.risingTicks > 0) {
		state.risingTicks--;
		if (monster.mode != MonsterMode::SpecialStand)
			return true;
	}
	if (IsMonsterImmobilized(monster))
		return true;
	// Bleeding from some uniques hinders, slowing like a chill; the stronger slow counts.
	const int slow = std::max<int>(state.chillTicks > 0 ? state.chillPercent : 0, MonsterHinderPercent(monster));
	if (slow == 0)
		return false;
	state.chillCarry = static_cast<uint8_t>(state.chillCarry + slow);
	if (state.chillCarry < 100)
		return false;
	state.chillCarry = static_cast<uint8_t>(state.chillCarry - 100);
	return true;
}

int MonsterBuildupPercent(const Monster &monster)
{
	int percent = 100;
	if (monster.isUnique())
		percent = UniqueBuildupPercent;
	else if (GetMonsterAffixes(monster).rarity == MonsterRarity::Rare)
		percent = RareBuildupPercent;
	// Waystones can raise the stun and freeze thresholds.
	return percent * 100 / MapThresholdPercent();
}

namespace {

/** @brief Adds a hit to a buildup; true when it reaches the threshold, which empties it. */
bool BuildUp(const Monster &monster, int32_t &buildup, Fixed26_6 damage, int thresholdPercent)
{
	// Overwhelming Presence lowers the stun and freeze thresholds near the hero.
	const int presence = PresenceThresholdPercent(monster, thresholdPercent == HeavyStunThresholdPercent);
	const int64_t threshold = static_cast<int64_t>(monster.maxHitPoints.raw()) * thresholdPercent / 100 * presence / 100;
	if (threshold <= 0)
		return false;
	const int64_t added = static_cast<int64_t>(damage.raw()) * MonsterBuildupPercent(monster) / 100;
	const int64_t total = std::min<int64_t>(buildup + added, threshold);
	if (total >= threshold) {
		buildup = 0;
		return true;
	}
	buildup = static_cast<int32_t>(total);
	return false;
}

/** @return Whether the hit froze the monster. */
bool InflictCold(Monster &monster, Fixed26_6 damage)
{
	Poe2MonsterState &state = monster.poe2;
	// A bigger share of the monster's life makes a stronger chill; a stronger chill replaces a weaker one.
	const int64_t share = static_cast<int64_t>(damage.raw()) * 100 / std::max<int32_t>(monster.maxHitPoints.raw(), 1);
	const int percent = static_cast<int>(std::clamp<int64_t>(share * 2, MinChillPercent, MaxChillPercent));
	if (state.chillTicks == 0 || percent >= state.chillPercent)
		state.chillPercent = static_cast<uint8_t>(percent);
	state.chillTicks = ChillTicks;
	if (BuildUp(monster, state.freezeBuildup, damage, FreezeThresholdPercent)) {
		state.freezeTicks = FreezeTicks;
		AddVisualEffect(monster.position.tile, MissileGraphicID::StoneCurseShatter);
		return true;
	}
	return false;
}

void InflictPhysical(Monster &monster, Fixed26_6 damage)
{
	Poe2MonsterState &state = monster.poe2;
	if (BuildUp(monster, state.stunBuildup, damage, HeavyStunThresholdPercent)) {
		state.heavyStunTicks = HeavyStunTicks;
		AddVisualEffect(monster.position.tile, MissileGraphicID::BigExplosion);
	}
}

} // namespace

void TryInflictAilment(Monster &monster, const Player &player, DamageType damageType, Fixed26_6 damage, AilmentSource source)
{
	if (!gbIsPoe2 || monster.hasNoLife() || damage <= 0)
		return;
	// The Sickness Road: the toll's fever and the pact stop some of the hero's ailments (stun buildup isn't one).
	if (damageType != DamageType::Physical && !AilmentAllowed(player, source))
		return;
	if (damageType == DamageType::Magic) {
		// POE2 Mashup: supports make cold build up harder (Deep Freeze) or not at all (Elemental Focus).
		int effect = SupportAilmentEffectPercent(damageType) * SupportAilmentChancePercent(damageType) / 100;
		if (effect > 0)
			effect = effect * std::max(100 + PassiveTotal(player, PassiveStat::FreezeBuildup), 0) / 100;
		if (effect <= 0)
			return;
		if (InflictCold(monster, Fixed26_6::fromRaw(static_cast<int32_t>(static_cast<int64_t>(damage.raw()) * effect / 100)))) {
			OnMonsterFrozen(player);
			OnSpiritFreezeOrElectrocute(Players[player.getId()]);
			OnSupportedFreeze(player);
			OnMetaAilment(Players[player.getId()], monster, MetaAilment::Freeze);
		}
		OnAilmentInflicted(player, monster, damageType, false);
		return;
	}
	if (damageType == DamageType::Physical) {
		// Some uniques add stun buildup.
		InflictPhysical(monster, Fixed26_6::fromRaw(static_cast<int32_t>(static_cast<int64_t>(damage.raw()) * UniqueStunBuildupPercent(player) / 100)));
		return;
	}
	if (damageType != DamageType::Fire && damageType != DamageType::Lightning)
		return;
	int chance = AilmentChancePerMille(monster, damageType, damage);
	if (damageType == DamageType::Fire)
		chance = chance * UniqueIgniteChancePercent(player) / 100;
	else
		chance = chance * std::max(100 + PassiveTotal(player, PassiveStat::ShockChance), 0) / 100;
	chance = chance * SupportAilmentChancePercent(damageType) / 100;
	Poe2MonsterState &state = monster.poe2;
	if (damageType == DamageType::Fire) {
		// POE2's Flammability: each fire hit adds its chance to the monster's, and the hit ignites with the total.
		if (chance <= 0)
			return;
		state.flammability = static_cast<int16_t>(std::min(state.flammability + chance, 1000));
		state.flammabilityTicks = FlammabilityTicks;
		chance = state.flammability;
	}
	if (GenerateRnd(1000) >= chance)
		return;

	const bool wasShocked = state.shockTicks > 0;
	OnAilmentInflicted(player, monster, damageType, wasShocked);
	if (damageType == DamageType::Fire) {
		const int32_t perTick = std::max<int32_t>(static_cast<int32_t>(static_cast<int64_t>(damage.raw()) * IgnitePercentPerSecond / 100 * SupportAilmentEffectPercent(damageType) / 100 / TicksPerSecond), 1);
		// A new ignite only replaces a weaker one, like POE2.
		if (state.igniteTicks == 0 || perTick >= state.igniteDamagePerTick) {
			state.igniteDamagePerTick = perTick;
			state.igniteSource = static_cast<int8_t>(player.getId());
		}
		state.igniteTicks = IgniteTicks;
		OnMetaAilment(Players[player.getId()], monster, MetaAilment::Ignite);
	} else {
		if (state.shockTicks == 0)
			AddVisualEffect(monster.position.tile, MissileGraphicID::BlueFlareExplosion);
		state.shockTicks = ShockTicks;
		OnMetaAilment(Players[player.getId()], monster, MetaAilment::Shock);
	}
}

bool CarriesAilment(const Monster &monster)
{
	const Poe2MonsterState &state = monster.poe2;
	return state.igniteTicks > 0 || state.shockTicks > 0 || state.chillTicks > 0 || state.poisonTicks > 0;
}

bool CopyAilments(const Monster &from, Monster &to, const Player &player, bool withIgnite)
{
	if (&from == &to || to.hasNoLife() || to.isPlayerMinion())
		return false;
	const Poe2MonsterState &source = from.poe2;
	Poe2MonsterState &target = to.poe2;
	// A copy replaces only a weaker ailment, or as strong a one with less time left.
	const auto better = [](int32_t strength, uint8_t ticks, int32_t theirStrength, uint8_t theirTicks) {
		return theirTicks == 0 || strength > theirStrength || (strength == theirStrength && ticks > theirTicks);
	};
	bool passed = false;
	if (withIgnite && source.igniteTicks > 0 && better(source.igniteDamagePerTick, source.igniteTicks, target.igniteDamagePerTick, target.igniteTicks)) {
		target.igniteTicks = source.igniteTicks;
		target.igniteDamagePerTick = source.igniteDamagePerTick;
		target.igniteSource = static_cast<int8_t>(player.getId());
		passed = true;
	}
	if (source.shockTicks > target.shockTicks) {
		if (target.shockTicks == 0)
			AddVisualEffect(to.position.tile, MissileGraphicID::BlueFlareExplosion);
		target.shockTicks = source.shockTicks;
		passed = true;
	}
	if (source.chillTicks > 0 && better(source.chillPercent, source.chillTicks, target.chillPercent, target.chillTicks)) {
		target.chillTicks = source.chillTicks;
		target.chillPercent = source.chillPercent;
		passed = true;
	}
	if (source.poisonTicks > 0 && better(source.poisonDamagePerTick, source.poisonTicks, target.poisonDamagePerTick, target.poisonTicks)) {
		target.poisonTicks = source.poisonTicks;
		target.poisonDamagePerTick = source.poisonDamagePerTick;
		target.poisonSource = static_cast<int8_t>(player.getId());
		passed = true;
	}
	if (passed)
		to.tag(player);
	return passed;
}

} // namespace devilution
