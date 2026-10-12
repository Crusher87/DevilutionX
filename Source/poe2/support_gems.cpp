#include "poe2/support_gems.hpp"

#include <algorithm>
#include <cstdlib>
#include <optional>

#include "engine/displacement.hpp"
#include "items.h"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "utils/is_of.hpp"
#include "utils/language.h"

namespace devilution {

namespace {

constexpr int MultipleProjectilesExtra = 2;
constexpr int EmpowerLevels = 2;
constexpr int8_t FasterCastingFrames = 3;

bool IsProjectileSpell(SpellID spell)
{
	return IsAnyOf(spell, SpellID::Firebolt, SpellID::Fireball, SpellID::HolyBolt, SpellID::BloodStar,
	    SpellID::Lightning, SpellID::ChargedBolt, SpellID::Elemental, SpellID::BoneSpirit);
}

/** @brief Spells whose cast projectile deals the damage itself, so it can pierce, fork and chain. */
bool IsDirectProjectileSpell(SpellID spell)
{
	return spell != SpellID::Lightning && IsProjectileSpell(spell);
}

bool IsProjectileSupport(SupportGem gem)
{
	return IsAnyOf(gem, SupportGem::Pierce, SupportGem::Fork, SupportGem::Chain, SupportGem::ConcentratedEffect, SupportGem::FasterProjectiles);
}

/** @brief Fires a copy of the projectile from the monster it just hit. */
Missile *SpawnFollowUp(const Missile &missile, const Monster &monster, WorldTilePosition target)
{
	const WorldTilePosition src = monster.position.tile;
	if (src == target)
		return nullptr;
	const Direction dir = GetDirection(src, target);
	const Poe2MissileData data = missile.poe2;
	Missile *followUp = AddMissile(src, target, dir, missile._mitype, missile._micaster, missile._misource, missile._midam, missile._mispllvl);
	if (followUp == nullptr)
		return nullptr;
	followUp->poe2 = data;
	followUp->poe2.lastHitMonster = static_cast<int16_t>(monster.getId());
	if (data.spell != SpellID::Invalid && missile.position.velocity != Displacement { 0, 0 } && followUp->position.velocity != Displacement { 0, 0 }) {
		// Keep Faster Projectiles' speed on the new projectile.
		const int64_t oldSpeed = static_cast<int64_t>(missile.position.velocity.deltaX) * missile.position.velocity.deltaX + static_cast<int64_t>(missile.position.velocity.deltaY) * missile.position.velocity.deltaY;
		const int64_t newSpeed = static_cast<int64_t>(followUp->position.velocity.deltaX) * followUp->position.velocity.deltaX + static_cast<int64_t>(followUp->position.velocity.deltaY) * followUp->position.velocity.deltaY;
		if (oldSpeed > newSpeed * 3 / 2)
			followUp->position.velocity = followUp->position.velocity * FasterProjectilesSpeedPercent / 100;
	}
	return followUp;
}

std::optional<WorldTilePosition> FindChainTarget(const Monster &from)
{
	std::optional<WorldTilePosition> best;
	int bestDistance = ChainRange + 1;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		const Monster &other = Monsters[ActiveMonsters[i]];
		if (&other == &from || other.hitPoints <= 0 || other.isPlayerMinion() || !other.isPossibleToHit() || other.position.tile == GolemHoldingCell)
			continue;
		const int distance = from.position.tile.ApproxDistance(other.position.tile);
		if (distance >= bestDistance || !LineClearMissile(from.position.tile, other.position.tile))
			continue;
		bestDistance = distance;
		best = other.position.tile;
	}
	return best;
}

int ManaCostPercent(SupportGem gem)
{
	switch (gem) {
	case SupportGem::MultipleProjectiles:
		return 150;
	case SupportGem::Empower:
		return 130;
	case SupportGem::FasterCasting:
		return 120;
	case SupportGem::Efficiency:
		return 60;
	case SupportGem::Pierce:
		return 120;
	case SupportGem::Fork:
		return 130;
	case SupportGem::Chain:
		return 150;
	case SupportGem::ConcentratedEffect:
		return 140;
	case SupportGem::FasterProjectiles:
		return 110;
	default:
		return 100;
	}
}

} // namespace

const std::array<SupportGem, MaxSupportsPerSpell> &Poe2PlayerData::supportsFor(SpellID spell) const
{
	static const std::array<SupportGem, MaxSupportsPerSpell> None {};
	const auto index = static_cast<size_t>(static_cast<int8_t>(spell));
	if (spell == SpellID::Invalid || index >= MaxPoe2Spells)
		return None;
	return spellSupports[index];
}

bool Poe2PlayerData::hasSupport(SpellID spell, SupportGem gem) const
{
	const auto &supports = supportsFor(spell);
	return std::find(supports.begin(), supports.end(), gem) != supports.end();
}

SupportGem SupportGemForMiscId(int miscId)
{
	switch (miscId) {
	case IMISC_SUPPORT_MULTIPROJ:
		return SupportGem::MultipleProjectiles;
	case IMISC_SUPPORT_EMPOWER:
		return SupportGem::Empower;
	case IMISC_SUPPORT_FASTCAST:
		return SupportGem::FasterCasting;
	case IMISC_SUPPORT_EFFICIENCY:
		return SupportGem::Efficiency;
	case IMISC_SUPPORT_PIERCE:
		return SupportGem::Pierce;
	case IMISC_SUPPORT_FORK:
		return SupportGem::Fork;
	case IMISC_SUPPORT_CHAIN:
		return SupportGem::Chain;
	case IMISC_SUPPORT_CONCENTRATED:
		return SupportGem::ConcentratedEffect;
	case IMISC_SUPPORT_FASTERPROJ:
		return SupportGem::FasterProjectiles;
	default:
		return SupportGem::None;
	}
}

SupportGem SupportGemForItem(const Item &item)
{
	if (item.isEmpty())
		return SupportGem::None;
	return SupportGemForMiscId(item._iMiscId);
}

std::string_view SupportGemName(SupportGem gem)
{
	switch (gem) {
	case SupportGem::MultipleProjectiles:
		return _("Multiple Projectiles");
	case SupportGem::Empower:
		return _("Empower");
	case SupportGem::FasterCasting:
		return _("Faster Casting");
	case SupportGem::Efficiency:
		return _("Efficiency");
	case SupportGem::Pierce:
		return _("Pierce");
	case SupportGem::Fork:
		return _("Fork");
	case SupportGem::Chain:
		return _("Chain");
	case SupportGem::ConcentratedEffect:
		return _("Concentrated Effect");
	case SupportGem::FasterProjectiles:
		return _("Faster Projectiles");
	default:
		return {};
	}
}

std::string_view SupportGemDescription(SupportGem gem)
{
	switch (gem) {
	case SupportGem::MultipleProjectiles:
		return _("Bolts and balls fire 2 extra\nprojectiles in a spread. Mana +50%");
	case SupportGem::Empower:
		return _("+2 spell level. Mana +30%");
	case SupportGem::FasterCasting:
		return _("Casts much faster. Mana +20%");
	case SupportGem::Efficiency:
		return _("Mana cost -40%");
	case SupportGem::Pierce:
		return _("Bolts pass through 2 monsters.\nMana +20%");
	case SupportGem::Fork:
		return _("Bolts split in two on hit.\nMana +30%");
	case SupportGem::Chain:
		return _("Bolts jump to 2 more monsters.\nMana +50%");
	case SupportGem::ConcentratedEffect:
		return _("Bolts deal 40% more damage.\nMana +40%");
	case SupportGem::FasterProjectiles:
		return _("Bolts fly 60% faster. Mana +10%");
	default:
		return {};
	}
}

bool CreateSupportGemItem(Item &item, SupportGem gem)
{
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		if (SupportGemForMiscId(AllItemsList[i].iMiscId) != gem)
			continue;
		InitializeItem(item, static_cast<_item_indexes>(i));
		GenerateNewSeed(item);
		item._iIdentified = true;
		return true;
	}
	return false;
}

bool SupportGemCanSupport(SupportGem gem, SpellID spell)
{
	if (gem == SupportGem::None || spell == SpellID::Invalid)
		return false;
	if (gem == SupportGem::MultipleProjectiles)
		return IsProjectileSpell(spell);
	if (IsProjectileSupport(gem))
		return IsDirectProjectileSpell(spell);
	return true;
}

SupportLinkResult LinkSupportGem(Poe2PlayerData &data, SpellID spell, SupportGem gem, SupportGem &replaced)
{
	replaced = SupportGem::None;
	const auto index = static_cast<size_t>(static_cast<int8_t>(spell));
	if (spell == SpellID::Invalid || index >= MaxPoe2Spells)
		return SupportLinkResult::NoReadiedSpell;
	if (!SupportGemCanSupport(gem, spell))
		return SupportLinkResult::NotApplicable;
	if (data.hasSupport(spell, gem))
		return SupportLinkResult::AlreadyLinked;

	auto &supports = data.spellSupports[index];
	for (SupportGem &slot : supports) {
		if (slot == SupportGem::None) {
			slot = gem;
			return SupportLinkResult::Linked;
		}
	}

	// Both slots are full: the oldest support makes room, like swapping a gem out in POE2.
	replaced = supports[0];
	std::rotate(supports.begin(), supports.begin() + 1, supports.end());
	supports.back() = gem;
	return SupportLinkResult::Replaced;
}

int SupportSpellLevelBonus(const Player &player, SpellID spell)
{
	return player.poe2.hasSupport(spell, SupportGem::Empower) ? EmpowerLevels : 0;
}

int SupportManaCostPercent(const Player &player, SpellID spell)
{
	int percent = 100;
	for (const SupportGem gem : player.poe2.supportsFor(spell))
		percent = percent * ManaCostPercent(gem) / 100;
	return percent;
}

int8_t SupportCastSkippedFrames(const Player &player, SpellID spell)
{
	return player.poe2.hasSupport(spell, SupportGem::FasterCasting) ? FasterCastingFrames : 0;
}

int SupportExtraProjectiles(const Player &player, SpellID spell)
{
	if (!IsProjectileSpell(spell))
		return 0;
	return player.poe2.hasSupport(spell, SupportGem::MultipleProjectiles) ? MultipleProjectilesExtra : 0;
}

WorldTilePosition RotateSpellTarget(WorldTilePosition src, WorldTilePosition dst, int degrees)
{
	// cos and sin of 0, 15, 30 and 45 degrees, scaled by 1024.
	constexpr int Cos[] = { 1024, 989, 887, 724 };
	constexpr int Sin[] = { 0, 265, 512, 724 };
	const int steps = std::min(std::abs(degrees) / 15, 3);
	const int sign = degrees < 0 ? -1 : 1;

	int dx = dst.x - src.x;
	int dy = dst.y - src.y;
	if (dx == 0 && dy == 0)
		return dst;
	// Aim far enough away that rounding to whole tiles keeps the spread visible.
	int scale = 1;
	while (((dx * scale) * (dx * scale)) + ((dy * scale) * (dy * scale)) < 100)
		scale++;
	dx *= scale;
	dy *= scale;

	const int rx = ((dx * Cos[steps]) - (sign * dy * Sin[steps])) / 1024;
	const int ry = ((sign * dx * Sin[steps]) + (dy * Cos[steps])) / 1024;
	return { static_cast<WorldTileCoord>(std::clamp(src.x + rx, 0, MAXDUNX - 1)), static_cast<WorldTileCoord>(std::clamp(src.y + ry, 0, MAXDUNY - 1)) };
}

void ApplySupportsToMissile(Missile &missile, const Player &player, SpellID spell)
{
	if (!IsDirectProjectileSpell(spell))
		return;
	const Poe2PlayerData &data = player.poe2;
	if (!std::any_of(data.supportsFor(spell).begin(), data.supportsFor(spell).end(), IsProjectileSupport))
		return;

	missile.poe2.spell = spell;
	if (data.hasSupport(spell, SupportGem::Pierce))
		missile.poe2.piercesLeft = PierceCount;
	if (data.hasSupport(spell, SupportGem::Chain))
		missile.poe2.chainsLeft = ChainCount;
	missile.poe2.canFork = data.hasSupport(spell, SupportGem::Fork);
	if (data.hasSupport(spell, SupportGem::ConcentratedEffect))
		missile.poe2.damagePercent = ConcentratedEffectDamagePercent;
	if (data.hasSupport(spell, SupportGem::FasterProjectiles))
		missile.position.velocity = missile.position.velocity * FasterProjectilesSpeedPercent / 100;
}

void ScaleSupportedMissileDamage(const Missile &missile, int &minDamage, int &maxDamage)
{
	if (missile.poe2.damagePercent == 100)
		return;
	minDamage = minDamage * missile.poe2.damagePercent / 100;
	maxDamage = maxDamage * missile.poe2.damagePercent / 100;
}

bool SupportedMissileIgnoresMonster(const Missile &missile, int monsterId)
{
	return missile.poe2.spell != SpellID::Invalid && missile.poe2.lastHitMonster == monsterId;
}

bool OnSupportedMissileHit(Missile &missile, const Monster &monster)
{
	if (missile.poe2.spell == SpellID::Invalid)
		return false;
	missile.poe2.lastHitMonster = static_cast<int16_t>(monster.getId());

	// Like POE2: a projectile pierces first, then forks, then chains.
	if (missile.poe2.piercesLeft > 0) {
		missile.poe2.piercesLeft--;
		return true;
	}

	if (missile.poe2.canFork) {
		missile.poe2.canFork = false;
		const WorldTilePosition hit = monster.position.tile;
		const WorldTilePosition ahead = hit + (hit - missile.position.start);
		for (const int degrees : { -ForkDegrees, ForkDegrees }) {
			if (Missile *fork = SpawnFollowUp(missile, monster, RotateSpellTarget(hit, ahead, degrees)); fork != nullptr)
				fork->poe2.canFork = false;
		}
		return false;
	}

	if (missile.poe2.chainsLeft > 0) {
		if (const std::optional<WorldTilePosition> target = FindChainTarget(monster); target) {
			if (Missile *chain = SpawnFollowUp(missile, monster, *target); chain != nullptr)
				chain->poe2.chainsLeft = static_cast<int8_t>(missile.poe2.chainsLeft - 1);
		}
	}
	return false;
}

} // namespace devilution
