/**
 * @file spells.cpp
 *
 * Implementation of functionality for casting player spells.
 */
#include "spells.h"

#include <algorithm>
#include <cstdlib>

#include "control/control.hpp"
#include "cursor.h"
#ifdef _DEBUG
#include "debug.h"
#endif
#include "engine/backbuffer_state.hpp"
#include "engine/point.hpp"
#include "engine/random.hpp"
#include "engine/world_tile.hpp"
#include "game_mode.hpp"
#include "gamemenu.h"
#include "inv.h"
#include "levels/gendung.h"
#include "missiles.h"
#include "poe2/gem_levels.hpp"
#include "poe2/support_gems.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/unique_effects.hpp"
#include "poe2/weapon_skills.hpp"

namespace devilution {

namespace {

constexpr int MultipleProjectilesSpreadDegrees = 15;

/**
 * @brief Gets a value indicating whether the player's current readied spell is a valid spell. Readied spells can be
 * invalidaded in a few scenarios where the spell comes from items, for example (like dropping the only scroll that
 * provided the spell).
 * @param player The player whose readied spell is to be checked.
 * @return 'true' when the readied spell is currently valid, and 'false' otherwise.
 */
bool IsReadiedSpellValid(const Player &player)
{
	switch (player._pRSplType) {
	case SpellType::Skill:
	case SpellType::Spell:
	case SpellType::Invalid:
		return true;

	case SpellType::Charges:
		return (player._pISpells & GetSpellBitmask(player._pRSpell)) != 0;

	case SpellType::Scroll:
		return (player._pScrlSpells & GetSpellBitmask(player._pRSpell)) != 0;

	default:
		return false;
	}
}

/**
 * @brief Clears the current player's readied spell selection.
 * @note Will force a UI redraw in case the values actually change, so that the new spell reflects on the bottom panel.
 * @param player The player whose readied spell is to be cleared.
 */
void ClearReadiedSpell(Player &player)
{
	if (player._pRSpell != SpellID::Invalid) {
		player._pRSpell = SpellID::Invalid;
		RedrawEverything();
	}

	if (player._pRSplType != SpellType::Invalid) {
		player._pRSplType = SpellType::Invalid;
		RedrawEverything();
	}
}

} // namespace

bool IsValidSpell(SpellID spl)
{
	return spl > SpellID::Null && static_cast<size_t>(spl) < SpellsData.size();
}

bool IsValidSpellFrom(int spellFrom)
{
	if (spellFrom == 0)
		return true;
	if (spellFrom >= INVITEM_INV_FIRST && spellFrom <= INVITEM_INV_LAST)
		return true;
	if (spellFrom >= INVITEM_BELT_FIRST && spellFrom <= INVITEM_BELT_LAST)
		return true;
	return false;
}

bool IsWallSpell(SpellID spl)
{
	return spl == SpellID::FireWall || spl == SpellID::LightningWall;
}

bool TargetsMonster(SpellID id)
{
	return id == SpellID::Fireball
	    || id == SpellID::FireWall
	    || id == SpellID::Inferno
	    || id == SpellID::Lightning
	    || id == SpellID::StoneCurse
	    || id == SpellID::FlameWave;
}

Fixed26_6 GetManaAmount(const Player &player, SpellID sn)
{
	int ma; // whole mana amount

	// mana adjust
	int adj = 0;

	// spell level
	const int sl = std::max(player.GetSpellLevel(sn) - 1, 0);

	if (sl > 0) {
		adj = sl * GetSpellData(sn).sManaAdj;
	}
	if (sn == SpellID::Firebolt) {
		adj /= 2;
	}
	if (sn == SpellID::Resurrect && sl > 0) {
		adj = sl * (GetSpellData(SpellID::Resurrect).sManaCost / 8);
	}

	// POE2 Mashup: a socketed spell gem costs more as it levels up, like POE2's gems, instead of less.
	const int gemLevel = gbIsPoe2 ? player.poe2.skillLevel(sn) : 0;

	if (sn == SpellID::Healing || sn == SpellID::HealOther) {
		ma = (GetSpellData(SpellID::Healing).sManaCost + 2 * player.getCharacterLevel() - adj);
	} else if (gemLevel > 0 && GetSpellData(sn).sManaCost != 255) {
		ma = GemBaseManaCost(SkillSlot::ForSpell(sn), gemLevel);
	} else if (GetSpellData(sn).sManaCost == 255) {
		ma = player._pMaxManaBase.whole() - adj;
	} else {
		ma = (GetSpellData(sn).sManaCost - adj);
	}

	Fixed26_6 manaAmount = Fixed26_6::fromInt(std::max(ma, 0));

	const ClassAttributes &classAttributes = GetClassAttributes(player._pClass);
	manaAmount = manaAmount * classAttributes.manaCost;

	if (GetSpellData(sn).sMinMana > manaAmount.whole()) {
		manaAmount = Fixed26_6::fromInt(GetSpellData(sn).sMinMana);
	}

	manaAmount = Fixed26_6::fromRaw(manaAmount.raw() * SupportManaCostPercent(player, sn) / 100);

	return manaAmount;
}

void ConsumeSpell(Player &player, SpellID sn)
{
	int manaCost = 0;
	switch (player.executedSpell.spellType) {
	case SpellType::Skill:
	case SpellType::Invalid:
		break;
	case SpellType::Scroll:
		ConsumeScroll(player);
		break;
	case SpellType::Charges:
		ConsumeStaffCharge(player);
		break;
	case SpellType::Spell:
#ifdef _DEBUG
		if (DebugGodMode)
			break;
#endif
		// POE2 Mashup: a unique's repeat of a spell costs nothing, and some uniques pay with energy shield first.
		if (IsCastingRepeat(player) || TakeFreeSkill(player))
			break;
		const Fixed26_6 cost = GetManaAmount(player, sn);
		const Fixed26_6 ma = PaySpellCostWithEnergyShield(player, cost);
		player._pMana -= ma;
		player._pManaBase -= ma;
		manaCost = cost.whole();
		RedrawComponent(PanelDrawComponent::Mana);
		break;
	}
	TelemetrySkillUsed(player, GetSpellData(sn).sNameText, manaCost, player.poe2.skillLevel(sn));
	if (sn == SpellID::BloodStar) {
		TelemetryNoteAttacker("own Blood Star");
		ApplyPlrDamage(DamageType::Physical, player, Fixed26_6::fromInt(5));
	}
	if (sn == SpellID::BoneSpirit) {
		TelemetryNoteAttacker("own Bone Spirit");
		ApplyPlrDamage(DamageType::Physical, player, Fixed26_6::fromInt(6));
	}
}

void EnsureValidReadiedSpell(Player &player)
{
	if (!IsReadiedSpellValid(player)) {
		ClearReadiedSpell(player);
	}
}

SpellCheckResult CheckSpell(const Player &player, SpellID sn, SpellType st, bool manaonly)
{
#ifdef _DEBUG
	if (DebugGodMode)
		return SpellCheckResult::Success;
#endif

	if (!manaonly && pcurs != CURSOR_HAND) {
		return SpellCheckResult::Fail_Busy;
	}

	if (st == SpellType::Skill) {
		return SpellCheckResult::Success;
	}

	if (player.GetSpellLevel(sn) <= 0) {
		return SpellCheckResult::Fail_Level0;
	}

	if (player._pMana + SpendableEnergyShield(player) < GetManaAmount(player, sn) || HasAnyOf(player._pIFlags, ItemSpecialEffect::NoMana)) {
		return SpellCheckResult::Fail_NoMana;
	}

	return SpellCheckResult::Success;
}

void CastSpell(Player &player, SpellID spl, WorldTilePosition src, WorldTilePosition dst, int spllvl)
{
	Direction dir = player._pdir;
	if (IsWallSpell(spl)) {
		dir = player.tempDirection;
	}

	const SpellData &spellData = GetSpellData(spl);
	const bool supported = player.executedSpell.spellType == SpellType::Spell;
	const auto spawnMissiles = [&](WorldTilePosition target) {
		bool fizzled = false;
		for (size_t i = 0; i < sizeof(spellData.sMissiles) / sizeof(spellData.sMissiles[0]) && spellData.sMissiles[i] != MissileID::Null; i++) {
			Missile *missile = AddMissile(src, target, dir, spellData.sMissiles[i], TARGET_MONSTERS, player, 0, spllvl);
			fizzled |= (missile == nullptr);
			if (missile != nullptr && gbIsPoe2)
				SetSpellMissileSpeed(*missile, spl);
			if (missile != nullptr && supported)
				ApplySupportsToMissile(*missile, player, spl);
		}
		if (spl == SpellID::ChargedBolt) {
			for (int i = (spllvl / 2) + 3; i > 0; i--) {
				Missile *missile = AddMissile(src, target, dir, MissileID::ChargedBolt, TARGET_MONSTERS, player, 0, spllvl);
				fizzled |= (missile == nullptr);
				if (missile != nullptr && supported)
					ApplySupportsToMissile(*missile, player, spl);
			}
		}
		return fizzled;
	};

	const bool fizzled = spawnMissiles(dst);
	if (!fizzled && supported) {
		// POE2 Mashup: Multiple Projectiles support fans extra projectiles out around the target.
		const int extra = SupportExtraProjectiles(player, spl);
		for (int i = 1; i <= extra; i++) {
			const int step = (i + 1) / 2;
			const int angle = (i % 2 == 0 ? -1 : 1) * step * MultipleProjectilesSpreadDegrees;
			spawnMissiles(RotateSpellTarget(src, dst, angle));
		}
	}
	if (!fizzled) {
		// POE2 Mashup: uniques count spells cast and queue their repeats.
		if (supported)
			OnSpellCast(player, spl, dst, spllvl);
		ConsumeSpell(player, spl);
	}
}

void SpawnResurrectBeam(Player &caster, Player &target)
{
	AddMissile(
	    target.position.tile,
	    target.position.tile,
	    Direction::South,
	    MissileID::ResurrectBeam,
	    TARGET_MONSTERS,
	    caster.getId(),
	    0,
	    0);
}

void ApplyResurrect(Player &target)
{
	if (&target == MyPlayer) {
		MyPlayerIsDead = false;
		gamemenu_off();
		RedrawComponent(PanelDrawComponent::Health);
		RedrawComponent(PanelDrawComponent::Mana);
	}

	ClrPlrPath(target);
	target.destAction = ACTION_NONE;
	target._pInvincible = false;
	SyncInitPlrPos(target);

	Fixed26_6 hp = Fixed26_6::fromInt(10);
	if (target._pMaxHPBase < hp) {
		hp = target._pMaxHPBase;
	}
	SetPlayerHitPoints(target, hp);

	target._pHPBase = target._pHitPoints + (target._pMaxHPBase - target._pMaxHP); // CODEFIX: does the same stuff as SetPlayerHitPoints above, can be removed
	target._pMana = Fixed26_6::fromInt(0);
	target._pManaBase = target._pMana + (target._pMaxManaBase - target._pMaxMana);

	target._pmode = PM_STAND;

	CalcPlrInv(target, true);

	if (target.isOnActiveLevel()) {
		StartStand(target, target._pdir);
	}
}

void DoHealOther(const Player &caster, Player &target)
{
	if (target.hasNoLife()) {
		return;
	}

	Fixed26_6 hp = Fixed26_6::fromInt(GenerateRnd(10) + 1);
	for (unsigned i = 0; i < caster.getCharacterLevel(); i++) {
		hp += Fixed26_6::fromInt(GenerateRnd(4) + 1);
	}
	for (int i = 0; i < caster.GetSpellLevel(SpellID::HealOther); i++) {
		hp += Fixed26_6::fromInt(GenerateRnd(6) + 1);
	}
	const ClassAttributes &classAttributes = GetClassAttributes(caster._pClass);
	hp = hp * classAttributes.healOtherRestoreLife;

	target._pHitPoints = std::min(target._pHitPoints + hp, target._pMaxHP);
	target._pHPBase = std::min(target._pHPBase + hp, target._pMaxHPBase);

	if (&target == MyPlayer) {
		RedrawComponent(PanelDrawComponent::Health);
	}
}

int GetSpellBookLevel(SpellID s)
{
	if (gbIsSpawn) {
		switch (s) {
		case SpellID::StoneCurse:
		case SpellID::Guardian:
		case SpellID::Golem:
		case SpellID::Elemental:
		case SpellID::BloodStar:
		case SpellID::BoneSpirit:
			return -1;
		default:
			break;
		}
	}

	if (static_cast<uint8_t>(s) >= SpellsData.size()) {
		return -1;
	}

	return GetSpellData(s).sBookLvl;
}

int GetSpellStaffLevel(SpellID s)
{
	if (gbIsSpawn) {
		switch (s) {
		case SpellID::StoneCurse:
		case SpellID::Guardian:
		case SpellID::Golem:
		case SpellID::Apocalypse:
		case SpellID::Elemental:
		case SpellID::BloodStar:
		case SpellID::BoneSpirit:
			return -1;
		default:
			break;
		}
	}

	if (static_cast<uint8_t>(s) >= SpellsData.size()) {
		return -1;
	}

	return GetSpellData(s).sStaffLvl;
}

} // namespace devilution
