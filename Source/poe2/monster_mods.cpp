#include "poe2/monster_mods.hpp"

#include <algorithm>
#include <array>

#include "engine/random.hpp"
#include "game_mode.hpp"
#include "missiles.h"
#include "monster.h"
#include "multi.h"
#include "player.h"
#include "tables/misdat.h"
#include "utils/language.h"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

constexpr int TicksPerSecond = 20;

uint32_t Mix(uint32_t value)
{
	value ^= value >> 16;
	value *= 0x7FEB352DU;
	value ^= value >> 15;
	value *= 0x846CA68BU;
	value ^= value >> 16;
	return value;
}

bool CanHaveRarity(const Monster &monster)
{
	if (!gbIsPoe2 || monster.isUnique() || monster.isPlayerMinion())
		return false;
	const _monster_id type = monster.type().type;
	return type != MT_GOLEM && type != MT_DIABLO && type != MT_NAKRUL;
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

} // namespace

MonsterAffixes GetMonsterAffixes(const Monster &monster)
{
	MonsterAffixes affixes;
	if (!CanHaveRarity(monster))
		return affixes;
	const uint32_t hash = Mix(monster.rndItemSeed ^ 0x6D6F6E73U);
	const int roll = static_cast<int>(hash % 1000);
	if (roll < RareMonsterPerMille) {
		affixes.rarity = MonsterRarity::Rare;
		affixes.mods = ChooseMods(hash, RareMonsterModCount);
	} else if (roll < RareMonsterPerMille + MagicMonsterPerMille) {
		affixes.rarity = MonsterRarity::Magic;
		affixes.mods = ChooseMods(hash, 1);
	}
	return affixes;
}

void ApplyMonsterRarity(Monster &monster)
{
	monster.poe2 = {};
	const MonsterAffixes affixes = GetMonsterAffixes(monster);
	if (affixes.rarity == MonsterRarity::Normal)
		return;

	int lifePercent = 100 + (affixes.rarity == MonsterRarity::Rare ? RareMonsterLifePercent : MagicMonsterLifePercent);
	if (affixes.has(MonsterMod::ExtraLife))
		lifePercent += ExtraLifePercent;
	monster.maxHitPoints = Fixed26_6::fromRaw(static_cast<int32_t>(static_cast<int64_t>(monster.maxHitPoints.raw()) * lifePercent / 100));
	monster.hitPoints = monster.maxHitPoints;

	if (affixes.has(MonsterMod::HardHitting)) {
		const auto boost = [](uint8_t &damage) { damage = static_cast<uint8_t>(std::min(damage * (100 + HardHittingPercent) / 100, 255)); };
		boost(monster.minDamage);
		boost(monster.maxDamage);
		boost(monster.minDamageSpecial);
		boost(monster.maxDamageSpecial);
	}
	if (affixes.has(MonsterMod::Resistant))
		monster.resistance |= RESIST_MAGIC | RESIST_FIRE | RESIST_LIGHTNING;
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
	switch (GetMonsterAffixes(monster).rarity) {
	case MonsterRarity::Magic:
		return 150;
	case MonsterRarity::Rare:
		return 300;
	default:
		return 100;
	}
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

	if (monster.hitPoints < monster.maxHitPoints && GetMonsterAffixes(monster).has(MonsterMod::Regenerating)) {
		constexpr int64_t Divisor = 1000 * TicksPerSecond;
		const int64_t total = static_cast<int64_t>(monster.maxHitPoints.raw()) * RegenPerMillePerSecond + state.regenRemainder;
		state.regenRemainder = static_cast<int>(total % Divisor);
		monster.hitPoints = std::min(monster.hitPoints + Fixed26_6::fromRaw(static_cast<int32_t>(total / Divisor)), monster.maxHitPoints);
	}

	if (state.shockTicks > 0)
		state.shockTicks--;

	if (state.igniteTicks > 0) {
		state.igniteTicks--;
		if (state.igniteTicks % 10 == 0)
			AddVisualEffect(monster.position.tile, MissileGraphicID::FireArrowExplosion);
		// Only the player who lit the fire applies its damage, which the game then syncs.
		if (state.igniteSource >= 0 && state.igniteSource == static_cast<int8_t>(MyPlayerId) && MyPlayer != nullptr) {
			ApplyMonsterDamage(DamageType::Fire, monster, Fixed26_6::fromRaw(state.igniteDamagePerTick));
			if (monster.hasNoLife())
				M_StartKill(monster, *MyPlayer);
		}
	}
}

Fixed26_6 ApplyShock(const Monster &monster, Fixed26_6 damage)
{
	if (!gbIsPoe2 || monster.poe2.shockTicks == 0)
		return damage;
	return Fixed26_6::fromRaw(damage.raw() + damage.raw() * ShockDamagePercent / 100);
}

int AilmentChancePercent(const Monster &monster, Fixed26_6 damage)
{
	if (monster.maxHitPoints <= 0 || damage <= 0)
		return 0;
	const int64_t percent = static_cast<int64_t>(damage.raw()) * 200 / monster.maxHitPoints.raw();
	return static_cast<int>(std::clamp<int64_t>(percent, 5, 100));
}

void TryInflictAilment(Monster &monster, const Player &player, DamageType damageType, Fixed26_6 damage)
{
	if (!gbIsPoe2 || monster.hasNoLife() || damage <= 0)
		return;
	if (damageType != DamageType::Fire && damageType != DamageType::Lightning)
		return;
	if (GenerateRnd(100) >= AilmentChancePercent(monster, damage))
		return;

	Poe2MonsterState &state = monster.poe2;
	if (damageType == DamageType::Fire) {
		const int32_t perTick = std::max<int32_t>(damage.raw() * IgnitePercentPerSecond / 100 / TicksPerSecond, 1);
		// A new ignite only replaces a weaker one, like POE2.
		if (state.igniteTicks == 0 || perTick >= state.igniteDamagePerTick) {
			state.igniteDamagePerTick = perTick;
			state.igniteSource = static_cast<int8_t>(player.getId());
		}
		state.igniteTicks = IgniteTicks;
	} else {
		if (state.shockTicks == 0)
			AddVisualEffect(monster.position.tile, MissileGraphicID::BlueFlareExplosion);
		state.shockTicks = ShockTicks;
	}
}

} // namespace devilution
