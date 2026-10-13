#include "poe2/support_gems.hpp"

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <utility>

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

bool IsMeleeAttackSkill(WeaponSkill skill)
{
	return IsAnyOf(skill, WeaponSkill::Cleave, WeaponSkill::GroundSlam, WeaponSkill::WhirlingStaff, WeaponSkill::Flurry, WeaponSkill::LeapSlam, WeaponSkill::ShieldCharge);
}

/** @brief Attack skills that use the normal attack animation, so they can attack faster. */
bool IsQuickenableAttackSkill(WeaponSkill skill)
{
	return IsAnyOf(skill, WeaponSkill::Cleave, WeaponSkill::GroundSlam, WeaponSkill::WhirlingStaff, WeaponSkill::Flurry,
	    WeaponSkill::Barrage, WeaponSkill::RainOfArrows, WeaponSkill::LightningArrow);
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
	// Keep the supports' speed on the new projectile.
	if (data.speedPercent != 100)
		followUp->position.velocity = followUp->position.velocity * data.speedPercent / 100;
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
		return 130;
	case SupportGem::Empower:
		return 140;
	case SupportGem::FasterCasting:
		return 120;
	case SupportGem::Efficiency:
		return 70;
	case SupportGem::Pierce:
		return 110;
	case SupportGem::Fork:
		return 110;
	case SupportGem::Chain:
		return 130;
	case SupportGem::ConcentratedEffect:
		return 130;
	case SupportGem::FasterProjectiles:
		return 120;
	case SupportGem::Brutality:
		return 120;
	case SupportGem::MartialTempo:
		return 120;
	case SupportGem::Knockback:
		return 110;
	default:
		return 100;
	}
}

} // namespace

const SupportSockets &Poe2PlayerData::supportsFor(SkillSlot skill) const
{
	static const SupportSockets None {};
	if (skill.isEmpty())
		return None;
	for (size_t slot = 0; slot < NumSkillSlots; slot++) {
		if (skillSlots[slot] == skill)
			return slotSupports[slot];
	}
	return None;
}

bool Poe2PlayerData::hasSupport(SkillSlot skill, SupportGem gem) const
{
	if (gem == SupportGem::None)
		return false;
	const SupportSockets &supports = supportsFor(skill);
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
	case IMISC_SUPPORT_BRUTALITY:
		return SupportGem::Brutality;
	case IMISC_SUPPORT_MARTIALTEMPO:
		return SupportGem::MartialTempo;
	case IMISC_SUPPORT_KNOCKBACK:
		return SupportGem::Knockback;
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
	case SupportGem::Brutality:
		return _("Brutality");
	case SupportGem::MartialTempo:
		return _("Martial Tempo");
	case SupportGem::Knockback:
		return _("Knockback");
	default:
		return {};
	}
}

std::string_view SupportGemDescription(SupportGem gem)
{
	switch (gem) {
	case SupportGem::MultipleProjectiles:
		return _("Fires 2 extra projectiles. 35% less\ndamage, 20% slower casts. Mana +30%");
	case SupportGem::Empower:
		return _("+2 spell level. 10% slower casts.\nMana +40%");
	case SupportGem::FasterCasting:
		return _("Casts much faster. Mana +20%");
	case SupportGem::Efficiency:
		return _("30% less mana cost");
	case SupportGem::Pierce:
		return _("Bolts pass through 2 monsters,\nthen deal 20% less damage. Mana +10%");
	case SupportGem::Fork:
		return _("Bolts split in two on hit; forks\ndeal 30% less damage. Mana +10%");
	case SupportGem::Chain:
		return _("Bolts jump to 2 more monsters.\n30% less damage. Mana +30%");
	case SupportGem::ConcentratedEffect:
		return _("Bolts deal 30% more damage but fly\n30% slower. Mana +30%");
	case SupportGem::FasterProjectiles:
		return _("Bolts fly 60% faster. Mana +20%");
	case SupportGem::Brutality:
		return _("25% more melee damage, but no fire\nor lightning from the weapon. Mana +20%");
	case SupportGem::MartialTempo:
		return _("Attack skills attack faster.\nMana +20%");
	case SupportGem::Knockback:
		return _("Melee attack skills knock enemies\nback. Mana +10%");
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

GemColor SupportGemColor(SupportGem gem)
{
	switch (gem) {
	case SupportGem::ConcentratedEffect:
	case SupportGem::Brutality:
	case SupportGem::Knockback:
		return GemColor::Strength;
	case SupportGem::MultipleProjectiles:
	case SupportGem::Pierce:
	case SupportGem::Fork:
	case SupportGem::FasterProjectiles:
	case SupportGem::MartialTempo:
		return GemColor::Dexterity;
	default:
		return GemColor::Magic;
	}
}

std::string_view GemColorName(GemColor color)
{
	switch (color) {
	case GemColor::Strength:
		return _("Str");
	case GemColor::Dexterity:
		return _("Dex");
	default:
		return _("Mag");
	}
}

bool SkillTakesSupports(SkillSlot skill)
{
	return skill.kind == SkillKind::Spell || skill.kind == SkillKind::Attack;
}

std::string_view SupportGemNeeds(SupportGem gem)
{
	switch (gem) {
	case SupportGem::Brutality:
	case SupportGem::Knockback:
		return _("Needs a melee attack");
	case SupportGem::MartialTempo:
		return _("Needs an attack");
	case SupportGem::MultipleProjectiles:
		return _("Needs a bolt or ball spell");
	case SupportGem::Pierce:
	case SupportGem::Fork:
	case SupportGem::Chain:
	case SupportGem::FasterProjectiles:
	case SupportGem::ConcentratedEffect:
		return _("Needs a bolt spell");
	case SupportGem::Empower:
	case SupportGem::FasterCasting:
		return _("Needs a spell");
	default:
		return {};
	}
}

bool SupportGemCanSupport(SupportGem gem, SkillSlot skill)
{
	if (gem == SupportGem::None || !SkillTakesSupports(skill))
		return false;
	if (gem == SupportGem::Efficiency)
		return true;
	if (skill.kind == SkillKind::Attack) {
		const WeaponSkill attack = skill.attack();
		switch (gem) {
		case SupportGem::Brutality:
		case SupportGem::Knockback:
			return IsMeleeAttackSkill(attack);
		case SupportGem::MartialTempo:
			return IsQuickenableAttackSkill(attack);
		default:
			return false;
		}
	}

	const SpellID spell = skill.spell();
	if (spell == SpellID::Invalid)
		return false;
	switch (gem) {
	case SupportGem::Brutality:
	case SupportGem::Knockback:
	case SupportGem::MartialTempo:
		return false;
	case SupportGem::MultipleProjectiles:
		return IsProjectileSpell(spell);
	default:
		return !IsProjectileSupport(gem) || IsDirectProjectileSpell(spell);
	}
}

size_t SupportSocketCount(int characterLevel)
{
	return std::min<size_t>(2 + static_cast<size_t>(std::max(characterLevel, 0) / 10), MaxSupportSockets);
}

size_t SupportSocketCount(const Player &player)
{
	return SupportSocketCount(player.getCharacterLevel());
}

int SupportCapacity(const Player &player, GemColor color)
{
	int attribute;
	switch (color) {
	case GemColor::Strength:
		attribute = player._pStrength;
		break;
	case GemColor::Dexterity:
		attribute = player._pDexterity;
		break;
	default:
		attribute = player._pMagic;
		break;
	}
	return 1 + (std::max(attribute, 0) / 15);
}

int SupportsUsed(const Poe2PlayerData &data, GemColor color)
{
	int used = 0;
	for (const SupportSockets &supports : data.slotSupports) {
		for (const SupportGem gem : supports) {
			if (gem != SupportGem::None && SupportGemColor(gem) == color)
				used++;
		}
	}
	return used;
}

SupportSocketResult SocketSupportGem(Player &player, size_t slot, std::optional<size_t> socket, SupportGem gem, SupportGem &replaced)
{
	replaced = SupportGem::None;
	if (slot >= NumSkillSlots || player.poe2.skillSlots[slot].isEmpty())
		return SupportSocketResult::NoSkill;
	const SkillSlot skill = player.poe2.skillSlots[slot];
	if (!SupportGemCanSupport(gem, skill))
		return SupportSocketResult::NotApplicable;

	SupportSockets &supports = player.poe2.slotSupports[slot];
	const size_t open = SupportSocketCount(player);
	if (socket) {
		if (*socket >= open)
			return SupportSocketResult::Locked;
	} else {
		for (size_t i = 0; i < open; i++) {
			if (supports[i] == SupportGem::None) {
				socket = i;
				break;
			}
		}
		if (!socket)
			return SupportSocketResult::NoFreeSocket;
	}

	const SupportGem current = supports[*socket];
	if (current == gem)
		return SupportSocketResult::AlreadySocketed;
	for (size_t i = 0; i < MaxSupportSockets; i++) {
		if (i != *socket && supports[i] == gem)
			return SupportSocketResult::AlreadySocketed;
	}
	const GemColor color = SupportGemColor(gem);
	const int freed = current != SupportGem::None && SupportGemColor(current) == color ? 1 : 0;
	if (SupportsUsed(player.poe2, color) - freed >= SupportCapacity(player, color))
		return SupportSocketResult::NoCapacity;

	supports[*socket] = gem;
	replaced = current;
	return current == SupportGem::None ? SupportSocketResult::Socketed : SupportSocketResult::Replaced;
}

std::optional<size_t> FindSlotForSupport(const Player &player, SupportGem gem)
{
	const size_t open = SupportSocketCount(player);
	for (size_t slot = 0; slot < NumSkillSlots; slot++) {
		const SkillSlot skill = player.poe2.skillSlots[slot];
		const SupportSockets &supports = player.poe2.slotSupports[slot];
		if (!SupportGemCanSupport(gem, skill) || std::find(supports.begin(), supports.end(), gem) != supports.end())
			continue;
		if (std::find(supports.begin(), supports.begin() + static_cast<std::ptrdiff_t>(open), SupportGem::None) != supports.begin() + static_cast<std::ptrdiff_t>(open))
			return slot;
	}
	return std::nullopt;
}

SupportGem UnsocketSupportGem(Poe2PlayerData &data, size_t slot, size_t socket)
{
	if (slot >= NumSkillSlots || socket >= MaxSupportSockets)
		return SupportGem::None;
	return std::exchange(data.slotSupports[slot][socket], SupportGem::None);
}

std::vector<SupportGem> TakeSlotSupports(Poe2PlayerData &data, size_t slot)
{
	std::vector<SupportGem> taken;
	if (slot >= NumSkillSlots)
		return taken;
	for (SupportGem &gem : data.slotSupports[slot]) {
		if (gem != SupportGem::None)
			taken.push_back(std::exchange(gem, SupportGem::None));
	}
	return taken;
}

int SupportSpellLevelBonus(const Player &player, SpellID spell)
{
	return player.poe2.hasSupport(spell, SupportGem::Empower) ? EmpowerLevels : 0;
}

int SupportManaCostPercent(const Player &player, SkillSlot skill)
{
	int percent = 100;
	for (const SupportGem gem : player.poe2.supportsFor(skill))
		percent = percent * ManaCostPercent(gem) / 100;
	return percent;
}

int8_t SupportCastSkippedFrames(const Player &player, SpellID spell)
{
	return player.poe2.hasSupport(spell, SupportGem::FasterCasting) ? FasterCastingFrames : 0;
}

int SupportCastRecoveryTicks(const Player &player, SpellID spell, int castFrames)
{
	int speed = 100;
	if (IsProjectileSpell(spell) && player.poe2.hasSupport(spell, SupportGem::MultipleProjectiles))
		speed = speed * (100 - MultipleProjectilesSlowPercent) / 100;
	if (player.poe2.hasSupport(spell, SupportGem::Empower))
		speed = speed * (100 - EmpowerSlowPercent) / 100;
	if (speed >= 100 || castFrames <= 0)
		return 0;
	// At speed% of the normal speed the cast takes 100/speed as long.
	return castFrames * (100 - speed) / speed;
}

int SupportSpellDamagePercent(const Player &player, SpellID spell)
{
	int percent = 100;
	if (IsProjectileSpell(spell) && player.poe2.hasSupport(spell, SupportGem::MultipleProjectiles))
		percent = percent * MultipleProjectilesDamagePercent / 100;
	if (IsDirectProjectileSpell(spell)) {
		if (player.poe2.hasSupport(spell, SupportGem::Chain))
			percent = percent * ChainDamagePercent / 100;
		if (player.poe2.hasSupport(spell, SupportGem::ConcentratedEffect))
			percent = percent * ConcentratedEffectDamagePercent / 100;
	}
	return percent;
}

int SupportExtraProjectiles(const Player &player, SpellID spell)
{
	if (!IsProjectileSpell(spell))
		return 0;
	return player.poe2.hasSupport(spell, SupportGem::MultipleProjectiles) ? MultipleProjectilesExtra : 0;
}

WeaponSkill CurrentWeaponSkill(const Player &player)
{
	return player.poe2.castSkill != WeaponSkill::None ? player.poe2.castSkill : player.poe2.activeSkill;
}

int SupportMeleeDamagePercent(const Player &player)
{
	const WeaponSkill skill = CurrentWeaponSkill(player);
	if (skill == WeaponSkill::None)
		return 100;
	return player.poe2.hasSupport(SkillSlot::ForAttack(skill), SupportGem::Brutality) ? BrutalityDamagePercent : 100;
}

bool SupportMeleeKnocksBack(const Player &player)
{
	const WeaponSkill skill = CurrentWeaponSkill(player);
	return skill != WeaponSkill::None && player.poe2.hasSupport(SkillSlot::ForAttack(skill), SupportGem::Knockback);
}

bool SupportMeleeIsPhysicalOnly(const Player &player)
{
	const WeaponSkill skill = CurrentWeaponSkill(player);
	return skill != WeaponSkill::None && player.poe2.hasSupport(SkillSlot::ForAttack(skill), SupportGem::Brutality);
}

int8_t SupportAttackSkippedFrames(const Player &player, WeaponSkill skill)
{
	if (skill == WeaponSkill::None)
		return 0;
	return player.poe2.hasSupport(SkillSlot::ForAttack(skill), SupportGem::MartialTempo) ? MartialTempoFrames : 0;
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
	missile.poe2.damagePercent = static_cast<int16_t>(SupportSpellDamagePercent(player, spell));
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
	int speed = 100;
	if (data.hasSupport(spell, SupportGem::ConcentratedEffect))
		speed = speed * ConcentratedEffectSpeedPercent / 100;
	if (data.hasSupport(spell, SupportGem::FasterProjectiles))
		speed = speed * FasterProjectilesSpeedPercent / 100;
	missile.poe2.speedPercent = static_cast<int16_t>(speed);
	if (speed != 100)
		missile.position.velocity = missile.position.velocity * speed / 100;
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
		// Once it has pierced, it hits for less.
		if (missile.poe2.piercesLeft == PierceCount)
			missile.poe2.damagePercent = static_cast<int16_t>(missile.poe2.damagePercent * PierceDamagePercent / 100);
		missile.poe2.piercesLeft--;
		return true;
	}

	if (missile.poe2.canFork) {
		missile.poe2.canFork = false;
		const WorldTilePosition hit = monster.position.tile;
		const WorldTilePosition ahead = hit + (hit - missile.position.start);
		for (const int degrees : { -ForkDegrees, ForkDegrees }) {
			if (Missile *fork = SpawnFollowUp(missile, monster, RotateSpellTarget(hit, ahead, degrees)); fork != nullptr) {
				fork->poe2.canFork = false;
				fork->poe2.damagePercent = static_cast<int16_t>(fork->poe2.damagePercent * ForkDamagePercent / 100);
			}
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
