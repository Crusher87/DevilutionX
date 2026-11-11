#include "poe2/default_attack.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "cursor.h"
#include "diablo.h"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "levels/gendung.h"
#include "monster.h"
#include "msg.h"
#include "player.h"
#include "poe2/aim.hpp"
#include "poe2/bases.hpp"
#include "poe2/gem_levels.hpp"
#include "poe2/spell_base.hpp"
#include "poe2/spirit.hpp"
#include "poe2/spirit_skills.hpp"
#include "poe2/support_table.hpp"
#include "poe2/table_skills.hpp"
#include "poe2/unique_effects.hpp"
#include "poe2/weapon_kinds.hpp"
#include "poe2/weapon_skills.hpp"
#include "poe2/weapon_swap.hpp"
#include "tables/itemdat.h"
#include "utils/is_of.hpp"
#include "utils/language.h"

namespace devilution {

namespace {

const Item *MainWeapon(const Item &left, const Item &right)
{
	const Item *weapon = nullptr;
	for (const Item *item : { &left, &right }) {
		// A sceptre held with another weapon isn't the main one: a wand beside a sceptre is.
		if (!item->isEmpty() && item->_iClass == ICLASS_WEAPON && (weapon == nullptr || IsAnySceptre(*weapon)))
			weapon = item;
	}
	return weapon;
}

/** @brief The granted table skill a staff or wand the hero can use grants, or None. */
WeaponSkill GrantOf(const Item *weapon)
{
	if (weapon == nullptr || !weapon->_iStatFlag || !(IsCasterStaff(*weapon) || IsWand(*weapon)))
		return WeaponSkill::None;
	const BaseMeta *base = GrantingBase(*weapon);
	if (base == nullptr || base->grantedSkill.kind != SkillKind::Attack)
		return WeaponSkill::None;
	const TableSkillData *table = FindTableSkill(base->grantedSkill.attack());
	return table != nullptr && IsGrantedTableSkill(*table) ? table->skill : WeaponSkill::None;
}

DefaultAttack AttackForHands(const Player &player, const Item &left, const Item &right)
{
	const Item *weapon = MainWeapon(left, right);
	if (weapon == nullptr)
		return left._itype == ItemType::Shield || right._itype == ItemType::Shield ? DefaultAttack::ShieldBash : DefaultAttack::Punch;
	// The caster weapons rework (caster-weapons-plan-v2.md 6.6): a new staff or wand casts its Staff Bolt; a new sceptre
	// has no attack, so the hero punches (beside a shield or focus too); a quarterstaff strikes for every class.
	if (IsCasterStaff(*weapon) || IsWand(*weapon)) {
		// Granted skills aren't sent over the network yet: in multiplayer every caster weapon casts Staff Bolt (6.10).
		return GrantOf(weapon) != WeaponSkill::None && !gbIsMultiplayer ? DefaultAttack::Granted : DefaultAttack::StaffBolt;
	}
	if (IsNewSceptre(*weapon))
		return DefaultAttack::Punch;
	if (IsQuarterstaff(*weapon))
		return DefaultAttack::StaffStrike;
	switch (weapon->_itype) {
	case ItemType::Sword:
		return AllItemsList[weapon->IDidx].iItemId == UITYPE_DAGGER ? DefaultAttack::DaggerStab : DefaultAttack::SwordStrike;
	case ItemType::Axe:
		return DefaultAttack::AxeChop;
	case ItemType::Mace:
		// POE2 Mashup: a sceptre is a caster's focus, not a club; it channels the staff bolt.
		if (gbIsPoe2 && IsAnySceptre(*weapon))
			return DefaultAttack::StaffBolt;
		return DefaultAttack::MaceSmash;
	case ItemType::Bow:
		return DefaultAttack::BowShot;
	case ItemType::Staff:
		// A Monk fights with the staff itself; everyone else channels a bolt through it.
		return player._pClass == HeroClass::Monk ? DefaultAttack::StaffStrike : DefaultAttack::StaffBolt;
	default:
		return DefaultAttack::Punch;
	}
}

} // namespace

int GrantedGemLevel(int itemLevel)
{
	int level = 0;
	for (const int needed : Poe2CharacterLevelForGemLevel) {
		if (needed <= itemLevel)
			level++;
	}
	return std::max(level, 1);
}

DefaultAttack DefaultAttackFor(const Player &player)
{
	return AttackForHands(player, player.InvBody[INVLOC_HAND_LEFT], player.InvBody[INVLOC_HAND_RIGHT]);
}

DefaultAttack DefaultAttackForSet(const Player &player, uint8_t set)
{
	if (set == player.poe2.weaponSet)
		return DefaultAttackFor(player);
	return AttackForHands(player, InactiveHand(player, INVLOC_HAND_LEFT), InactiveHand(player, INVLOC_HAND_RIGHT));
}

SkillSlot DefaultSkillSlot(const Player &player)
{
	if (!gbIsPoe2)
		return {};
	return DefaultSkillSlotForSet(player, player.poe2.weaponSet);
}

SkillSlot DefaultSkillSlotForSet(const Player &player, uint8_t set)
{
	const DefaultAttack attack = DefaultAttackForSet(player, set);
	if (attack == DefaultAttack::Granted)
		return SkillSlot::ForAttack(GrantedAttackSkillForSet(player, set));
	return SkillSlot::ForDefault(static_cast<uint8_t>(attack));
}

WeaponSkill GrantedAttackSkill(const Player &player)
{
	if (!gbIsPoe2)
		return WeaponSkill::None;
	return GrantOf(MainWeapon(player.InvBody[INVLOC_HAND_LEFT], player.InvBody[INVLOC_HAND_RIGHT]));
}

WeaponSkill GrantedAttackSkillForSet(const Player &player, uint8_t set)
{
	if (set == player.poe2.weaponSet)
		return GrantedAttackSkill(player);
	if (!gbIsPoe2)
		return WeaponSkill::None;
	return GrantOf(MainWeapon(InactiveHand(player, INVLOC_HAND_LEFT), InactiveHand(player, INVLOC_HAND_RIGHT)));
}

bool UsesGrantedSkill(const Player &player)
{
	return gbIsPoe2 && DefaultAttackFor(player) == DefaultAttack::Granted;
}

bool IsRangedGrantedSkill(WeaponSkill skill)
{
	const TableSkillData *table = FindTableSkill(skill);
	return table != nullptr && IsAnyOf(table->shape, TableShape::Projectiles, TableShape::Stream, TableShape::Burst, TableShape::Zone, TableShape::Curse)
	    && (table->flags & TF_AtPlayer) == 0;
}

std::string_view DefaultAttackName(DefaultAttack attack)
{
	switch (attack) {
	case DefaultAttack::SwordStrike:
		return _("Sword Strike");
	case DefaultAttack::DaggerStab:
		return _("Dagger Stab");
	case DefaultAttack::AxeChop:
		return _("Axe Chop");
	case DefaultAttack::MaceSmash:
		return _("Mace Smash");
	case DefaultAttack::StaffStrike:
		return _("Staff Strike");
	case DefaultAttack::StaffBolt:
		return _("Staff Bolt");
	case DefaultAttack::BowShot:
		return _("Bow Shot");
	case DefaultAttack::ShieldBash:
		return _("Shield Bash");
	case DefaultAttack::Granted:
		return _("Weapon Skill");
	default:
		return _("Punch");
	}
}

std::string_view DefaultAttackNameForSet(const Player &player, uint8_t set)
{
	const DefaultAttack attack = DefaultAttackForSet(player, set);
	if (attack == DefaultAttack::Granted)
		return WeaponSkillName(GrantedAttackSkillForSet(player, set));
	return DefaultAttackName(attack);
}

bool IsRangedDefaultAttack(DefaultAttack attack)
{
	return attack == DefaultAttack::BowShot || attack == DefaultAttack::StaffBolt;
}

bool IsRangedDefaultAttack(const Player &player)
{
	const DefaultAttack attack = DefaultAttackFor(player);
	if (attack == DefaultAttack::Granted)
		return IsRangedGrantedSkill(GrantedAttackSkill(player));
	return IsRangedDefaultAttack(attack);
}

bool FiresStaffBolt(const Player &player)
{
	return gbIsPoe2 && DefaultAttackFor(player) == DefaultAttack::StaffBolt;
}

bool AttacksAtRange(const Player &player)
{
	return player.UsesRangedWeapon() || FiresStaffBolt(player) || (UsesGrantedSkill(player) && IsRangedGrantedSkill(GrantedAttackSkill(player)));
}

bool FightsWithBareFists(const Player &player)
{
	if (!gbIsPoe2 || player._pClass != HeroClass::Monk)
		return false;
	// Like the gear's weapon damage in CalcPlrItemVals: only what the hero can use counts.
	for (const Item &item : player.InvBody) {
		if (!item.isEmpty() && item._iStatFlag && (item._iMinDam != 0 || item._iMaxDam != 0))
			return false;
		// A new staff or wand hits for nothing but casts its own skill; a new sceptre leaves his fists free.
		if (!item.isEmpty() && item._iStatFlag && (IsCasterStaff(item) || IsWand(item)))
			return false;
	}
	return true;
}

std::pair<int, int> BareFistDamage(int gemLevel)
{
	struct Staff {
		int level;
		int minDamage;
		int maxDamage;
	};
	// The quarterstaffs that drop, POE2's own bases, by drop level: the bundle's, not the caster weapons rework's
	// (identity poe2), so fists hit as they did.
	std::vector<Staff> staffs;
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		const ItemData &data = AllItemsList[i];
		const BaseMeta *base = BaseMetaFor(static_cast<int>(i));
		if (data.dropRate <= 0 || base == nullptr || base->itemClass != "quarterstaff" || base->identity == "poe2" || data.iMinDam <= 0)
			continue;
		staffs.push_back({ data.iMinMLvl, data.iMinDam, std::max<int>(data.iMaxDam, data.iMinDam) });
	}
	if (staffs.empty())
		return { 1, 1 };
	std::sort(staffs.begin(), staffs.end(), [](const Staff &a, const Staff &b) { return a.level < b.level; });
	// The Act 1 staves hit harder than the next ones (plan step 14), so fists keep the best damage reached so far and
	// never weaken as the gem levels.
	for (size_t i = 1; i < staffs.size(); i++) {
		staffs[i].minDamage = std::max(staffs[i].minDamage, staffs[i - 1].minDamage);
		staffs[i].maxDamage = std::max(staffs[i].maxDamage, staffs[i - 1].maxDamage);
	}

	// The staffs' drop levels are area levels, so the gem level maps to POE2's character level for it, not the mod's.
	const int level = Poe2CharacterLevelForGemLevel[static_cast<size_t>(std::clamp(gemLevel, 1, static_cast<int>(Poe2CharacterLevelForGemLevel.size())) - 1)];
	const Staff *below = &staffs.front();
	const Staff *above = &staffs.front();
	for (const Staff &staff : staffs) {
		above = &staff;
		if (staff.level > level)
			break;
		below = &staff;
	}
	// Before the first staff or past the last one: that staff. In between: the same growth every level.
	double fraction = 0;
	if (above->level > below->level && level > below->level)
		fraction = static_cast<double>(level - below->level) / (above->level - below->level);
	const auto between = [fraction](int from, int to) {
		const double damage = from * std::pow(static_cast<double>(to) / from, fraction);
		return static_cast<int>(std::round(damage * BareFistStaffPercent / 100));
	};
	const int minDamage = std::max(between(below->minDamage, above->minDamage), 1);
	return { minDamage, std::max(between(below->maxDamage, above->maxDamage), minDamage) };
}

int BareFistGemLevel(const Player &player, SkillSlot skill)
{
	if (skill.kind == SkillKind::Attack) {
		if (const int level = player.poe2.skillLevel(skill); level > 0)
			return level;
	}
	int best = 1;
	for (const SkillSlot &socketed : player.poe2.skillSlots) {
		if (socketed.kind == SkillKind::Attack && (SkillTags(socketed) & ST_Attack) != 0)
			best = std::max(best, player.poe2.skillLevel(socketed));
	}
	return best;
}

std::pair<int, int> AttackWeaponDamage(const Player &player, SkillSlot skill)
{
	if (FightsWithBareFists(player))
		return BareFistDamage(BareFistGemLevel(player, skill));
	if (DualWields(player)) {
		const Poe2PlayerData::HandDamage &hand = player.poe2.handDamage[player.poe2.swingHand % 2];
		return { hand.minDamage, hand.maxDamage };
	}
	return { player._pIMinDam, player._pIMaxDam };
}

bool DualWields(const Player &player)
{
	return gbIsPoe2 && player.poe2.dualWield;
}

std::pair<int, int> SwingElementDamage(const Player &player, bool lightning)
{
	if (DualWields(player)) {
		const Poe2PlayerData::HandDamage &hand = player.poe2.handDamage[player.poe2.swingHand % 2];
		return lightning ? std::pair { hand.minLightning, hand.maxLightning } : std::pair { hand.minFire, hand.maxFire };
	}
	return lightning ? std::pair { player._pILMinDam, player._pILMaxDam } : std::pair { player._pIFMinDam, player._pIFMaxDam };
}

void EndSwing(Player &player)
{
	if (DualWields(player))
		player.poe2.swingHand = static_cast<uint8_t>((player.poe2.swingHand + 1) % 2);
}

int RollStaffBoltDamage(const Player &player)
{
	const int base = RandomIntBetween(player._pIMinDam, std::max(player._pIMinDam, player._pIMaxDam));
	const int bonus = (base * player._pIBonusDam) / 100;
	return std::max(1, ((base + bonus) * StaffBoltDamagePercent / 100) + (player._pMagic * StaffBoltMagicPercent / 100));
}

bool FiresGrantedBolt(const Player &player)
{
	if (!FiresStaffBolt(player))
		return false;
	const Item *weapon = MainWeapon(player.InvBody[INVLOC_HAND_LEFT], player.InvBody[INVLOC_HAND_RIGHT]);
	return weapon != nullptr && IsCasterWeapon(*weapon);
}

int GrantedBoltLevel(const Player &player)
{
	const Item *weapon = MainWeapon(player.InvBody[INVLOC_HAND_LEFT], player.InvBody[INVLOC_HAND_RIGHT]);
	if (weapon == nullptr || !IsCasterWeapon(*weapon))
		return 0;
	const uint8_t hand = weapon == &player.InvBody[INVLOC_HAND_LEFT] ? 0 : 1;
	for (const GrantedSkill &entry : player.poe2.granted) {
		if (entry.baseLevel > 0 && entry.hand == hand) {
			const int bonus = ItemSkillLevelBonus(player.poe2, SkillSlot::ForDefault(static_cast<uint8_t>(DefaultAttack::StaffBolt)));
			return std::clamp(entry.baseLevel + bonus, 1, static_cast<int>(MaxGemLevel));
		}
	}
	// A weapon the hero can't use grants nothing; its bolt is level 1.
	return 1;
}

int GrantedBoltPercent(const Player &player, int level)
{
	// As a Firebolt gem's: the curve, then the spell damage of gear, the tree and Spirit skills. The element's own
	// damage comes on the hit (UniqueSpellHitPercent), as for every spell.
	int percent = GrantedStaffBoltPercent * FireballCurvePercent(GrantedCurveEndAt20, level) / 100;
	percent = percent * UniqueSpellDamagePercent(player, SpellID::Firebolt) / 100;
	percent = percent * SpiritSpellDamagePercent(player) / 100;
	return std::max(percent, 1);
}

int RollGrantedBoltDamage(const Player &player)
{
	const int level = std::max(GrantedBoltLevel(player), 1);
	return std::max(1, RollSpellBase(player, level) * GrantedBoltPercent(player, level) / 100);
}

bool ReadyWeaponSet(Player &player, uint8_t set)
{
	if (set == player.poe2.weaponSet || set >= NumWeaponSets)
		return true;
	return SwapWeapons(player);
}

bool UseDefaultAttack(Player &player, uint8_t set)
{
	if (player.hasNoLife() || player._pInvincible)
		return false;
	if (leveltype == DTYPE_TOWN)
		return false;
	// Like POE2, the attack of the other weapon set swaps to it first.
	if (!ReadyWeaponSet(player, set))
		return false;
	// A caster weapon's granted skill goes through the skill path, as a left click does.
	if (UsesGrantedSkill(player))
		return TryUseGrantedSkill(player);
	const bool ranged = IsRangedDefaultAttack(DefaultAttackFor(player));
	if (pcursmonst != -1 && !CanTalkToMonst(Monsters[pcursmonst])) {
		LastPlayerAction = PlayerActionType::AttackMonsterTarget;
		NetSendCmdParam1(true, ranged ? CMD_RATTACKID : CMD_ATTACKID, static_cast<uint16_t>(pcursmonst));
		return true;
	}
	Point target = cursPosition;
	if (target == player.position.tile || !InDungeonBounds(target))
		target = player.position.tile + player._pdir;
	// Arrows and staff bolts fly exactly where the cursor points, at any angle.
	if (ranged)
		target = AimAlongCursor(player, AimTiles);
	LastPlayerAction = PlayerActionType::Attack;
	NetSendCmdLoc(player.getId(), true, ranged ? CMD_RATTACKXY : CMD_SATTACKXY, target);
	return true;
}

} // namespace devilution
