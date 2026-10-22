#include "poe2/default_attack.hpp"

#include <algorithm>
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
#include "poe2/spirit.hpp"
#include "poe2/support_table.hpp"
#include "poe2/weapon_swap.hpp"
#include "tables/itemdat.h"
#include "utils/language.h"

namespace devilution {

namespace {

const Item *MainWeapon(const Item &left, const Item &right)
{
	const Item *weapon = nullptr;
	for (const Item *item : { &left, &right }) {
		// A sceptre held with another weapon isn't the main one.
		if (!item->isEmpty() && item->_iClass == ICLASS_WEAPON && (weapon == nullptr || ItemGrantsSpirit(*weapon)))
			weapon = item;
	}
	return weapon;
}

DefaultAttack AttackForHands(const Player &player, const Item &left, const Item &right)
{
	const Item *weapon = MainWeapon(left, right);
	if (weapon == nullptr)
		return left._itype == ItemType::Shield || right._itype == ItemType::Shield ? DefaultAttack::ShieldBash : DefaultAttack::Punch;
	switch (weapon->_itype) {
	case ItemType::Sword:
		return AllItemsList[weapon->IDidx].iItemId == UITYPE_DAGGER ? DefaultAttack::DaggerStab : DefaultAttack::SwordStrike;
	case ItemType::Axe:
		return DefaultAttack::AxeChop;
	case ItemType::Mace:
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
	return SkillSlot::ForDefault(static_cast<uint8_t>(DefaultAttackFor(player)));
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
	default:
		return _("Punch");
	}
}

bool IsRangedDefaultAttack(DefaultAttack attack)
{
	return attack == DefaultAttack::BowShot || attack == DefaultAttack::StaffBolt;
}

bool FiresStaffBolt(const Player &player)
{
	return gbIsPoe2 && DefaultAttackFor(player) == DefaultAttack::StaffBolt;
}

bool FightsWithBareFists(const Player &player)
{
	if (!gbIsPoe2 || player._pClass != HeroClass::Monk)
		return false;
	// Like the gear's weapon damage in CalcPlrItemVals: only what the hero can use counts.
	for (const Item &item : player.InvBody) {
		if (!item.isEmpty() && item._iStatFlag && (item._iMinDam != 0 || item._iMaxDam != 0))
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
	// The plain heavy quarterstaffs of the item data, by drop level.
	std::vector<Staff> staffs;
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		const ItemData &data = AllItemsList[i];
		const BaseMeta *base = BaseMetaFor(static_cast<int>(i));
		if (data.dropRate <= 0 || base == nullptr || base->itemClass != "quarterstaff" || base->identity != "heavy" || data.iMinDam <= 0)
			continue;
		staffs.push_back({ data.iMinMLvl, data.iMinDam, std::max<int>(data.iMaxDam, data.iMinDam) });
	}
	if (staffs.empty())
		return { 1, 1 };
	std::sort(staffs.begin(), staffs.end(), [](const Staff &a, const Staff &b) { return a.level < b.level; });

	const int level = CharacterLevelForGemLevel(gemLevel);
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
	return { player._pIMinDam, player._pIMaxDam };
}

int RollStaffBoltDamage(const Player &player)
{
	const int base = RandomIntBetween(player._pIMinDam, std::max(player._pIMinDam, player._pIMaxDam));
	const int bonus = (base * player._pIBonusDam) / 100;
	return std::max(1, ((base + bonus) * StaffBoltDamagePercent / 100) + (player._pMagic * StaffBoltMagicPercent / 100));
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
