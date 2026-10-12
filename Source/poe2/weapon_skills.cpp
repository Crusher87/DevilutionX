#include "poe2/weapon_skills.hpp"

#include "cursor.h"
#include "engine/backbuffer_state.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/dun_tile_data.hpp"
#include "monster.h"
#include "msg.h"
#include "multi.h"
#include "player.h"
#include "plrmsg.h"
#include "poe2/dodge_roll.hpp"
#include "utils/format.hpp"
#include "utils/is_of.hpp"
#include "utils/language.h"

namespace devilution {

namespace {

/** A queued skill is dropped if no attack starts within this many game ticks. */
constexpr uint8_t PendingSkillTicks = 40;
constexpr int LeapSlamTiles = 5;
/** Game ticks a Leap Slam may take to land before it's dropped. */
constexpr uint8_t LeapSlamTimeoutTicks = 40;

bool StartLeapSlam(Player &player)
{
	Point target = pcursmonst != -1 ? static_cast<Point>(Monsters[pcursmonst].position.tile) : cursPosition;
	if (!InDungeonBounds(target))
		target = player.position.tile + player._pdir;
	if (!DashTowards(player, target, LeapSlamTiles))
		return false;

	const Fixed26_6 cost = Fixed26_6::fromInt(WeaponSkillManaCost(WeaponSkill::LeapSlam));
	player._pMana -= cost;
	player._pManaBase -= cost;
	RedrawComponent(PanelDrawComponent::Mana);
	player.poe2.leapSlamTicks = LeapSlamTimeoutTicks;
	return true;
}

} // namespace

WeaponSkill WeaponSkillForItem(const Item &item)
{
	if (item.isEmpty())
		return WeaponSkill::None;
	switch (item._itype) {
	case ItemType::Sword:
		if (AllItemsList[item.IDidx].iItemId == UITYPE_DAGGER)
			return WeaponSkill::Flurry;
		return WeaponSkill::Cleave;
	case ItemType::Axe:
		return WeaponSkill::Cleave;
	case ItemType::Mace:
		return WeaponSkill::GroundSlam;
	case ItemType::Bow:
		return WeaponSkill::Barrage;
	case ItemType::Staff:
		return WeaponSkill::WhirlingStaff;
	default:
		return WeaponSkill::None;
	}
}

WeaponSkill WeaponSkillFor(const Player &player)
{
	const WeaponSkill left = WeaponSkillForItem(player.InvBody[INVLOC_HAND_LEFT]);
	if (left != WeaponSkill::None)
		return left;
	return WeaponSkillForItem(player.InvBody[INVLOC_HAND_RIGHT]);
}

std::string_view WeaponSkillName(WeaponSkill skill)
{
	switch (skill) {
	case WeaponSkill::Cleave:
		return _("Cleave");
	case WeaponSkill::GroundSlam:
		return _("Ground Slam");
	case WeaponSkill::Barrage:
		return _("Barrage");
	case WeaponSkill::WhirlingStaff:
		return _("Whirling Staff");
	case WeaponSkill::Flurry:
		return _("Flurry");
	case WeaponSkill::LeapSlam:
		return _("Leap Slam");
	case WeaponSkill::RainOfArrows:
		return _("Rain of Arrows");
	default:
		return {};
	}
}

std::string_view WeaponSkillDescription(WeaponSkill skill)
{
	switch (skill) {
	case WeaponSkill::Cleave:
		return _("Hits the target and both sides");
	case WeaponSkill::GroundSlam:
		return _("Hits all around, knocks back");
	case WeaponSkill::Barrage:
		return _("Fires a fan of five arrows");
	case WeaponSkill::WhirlingStaff:
		return _("Hits everything around you");
	case WeaponSkill::Flurry:
		return _("Very fast double strike");
	case WeaponSkill::LeapSlam:
		return _("Leap to the target, slam all around");
	case WeaponSkill::RainOfArrows:
		return _("A volley of arrows over an area");
	default:
		return {};
	}
}

int WeaponSkillManaCost(WeaponSkill skill)
{
	switch (skill) {
	case WeaponSkill::Cleave:
		return 5;
	case WeaponSkill::GroundSlam:
		return 8;
	case WeaponSkill::Barrage:
		return 7;
	case WeaponSkill::WhirlingStaff:
		return 6;
	case WeaponSkill::Flurry:
		return 4;
	case WeaponSkill::LeapSlam:
		return 10;
	case WeaponSkill::RainOfArrows:
		return 12;
	default:
		return 0;
	}
}

bool IsRangedWeaponSkill(WeaponSkill skill)
{
	return IsAnyOf(skill, WeaponSkill::Barrage, WeaponSkill::RainOfArrows);
}

bool ItemAllowsWeaponSkill(const Item &item, WeaponSkill skill)
{
	if (item.isEmpty() || item._iClass != ICLASS_WEAPON)
		return false;
	switch (skill) {
	case WeaponSkill::Cleave:
		return IsAnyOf(item._itype, ItemType::Sword, ItemType::Axe);
	case WeaponSkill::Flurry:
		return item._itype == ItemType::Sword;
	case WeaponSkill::LeapSlam:
		return IsAnyOf(item._itype, ItemType::Mace, ItemType::Axe, ItemType::Sword, ItemType::Staff);
	case WeaponSkill::RainOfArrows:
		return item._itype == ItemType::Bow;
	default:
		return skill != WeaponSkill::None && WeaponSkillForItem(item) == skill;
	}
}

bool WeaponAllowsSkill(const Player &player, WeaponSkill skill)
{
	return ItemAllowsWeaponSkill(player.InvBody[INVLOC_HAND_LEFT], skill) || ItemAllowsWeaponSkill(player.InvBody[INVLOC_HAND_RIGHT], skill);
}

std::string_view WeaponSkillWeapons(WeaponSkill skill)
{
	switch (skill) {
	case WeaponSkill::Cleave:
		return _("sword or axe");
	case WeaponSkill::GroundSlam:
		return _("mace");
	case WeaponSkill::Barrage:
		return _("bow");
	case WeaponSkill::WhirlingStaff:
		return _("staff");
	case WeaponSkill::Flurry:
		return _("sword or dagger");
	case WeaponSkill::LeapSlam:
		return _("melee weapon");
	case WeaponSkill::RainOfArrows:
		return _("bow");
	default:
		return {};
	}
}

bool TryUseWeaponSkill(Player &player, WeaponSkill skill)
{
	// Weapon skills aren't sent over the network yet, so they're single player only.
	if (!gbIsPoe2 || gbIsMultiplayer || skill == WeaponSkill::None)
		return false;
	if (player.hasNoLife() || player._pInvincible)
		return false;

	if (!WeaponAllowsSkill(player, skill)) {
		EventPlrMsg(FormatRuntime(_("{:s} needs a {:s}."), WeaponSkillName(skill), WeaponSkillWeapons(skill)), UiFlags::ColorRed);
		return false;
	}
	if (player._pMana < Fixed26_6::fromInt(WeaponSkillManaCost(skill))) {
		EventPlrMsg(FormatRuntime(_("Not enough mana for {:s}."), WeaponSkillName(skill)), UiFlags::ColorRed);
		return false;
	}

	if (skill == WeaponSkill::LeapSlam)
		return StartLeapSlam(player);

	player.poe2.pendingSkill = skill;
	player.poe2.pendingSkillTicks = PendingSkillTicks;

	const bool ranged = IsRangedWeaponSkill(skill);
	if (pcursmonst != -1) {
		NetSendCmdParam1(true, ranged ? CMD_RATTACKID : CMD_ATTACKID, static_cast<uint16_t>(pcursmonst));
		return true;
	}

	Point target = cursPosition;
	if (target == player.position.tile || !InDungeonBounds(target))
		target = player.position.tile + player._pdir;
	NetSendCmdLoc(player.getId(), true, ranged ? CMD_RATTACKXY : CMD_SATTACKXY, target);
	return true;
}

void ActivateQueuedWeaponSkill(Player &player, bool isRangedAttack)
{
	player.poe2.activeSkill = WeaponSkill::None;
	const WeaponSkill skill = player.poe2.pendingSkill;
	if (skill == WeaponSkill::None || IsRangedWeaponSkill(skill) != isRangedAttack)
		return;

	player.poe2.pendingSkill = WeaponSkill::None;
	player.poe2.pendingSkillTicks = 0;

	const Fixed26_6 cost = Fixed26_6::fromInt(WeaponSkillManaCost(skill));
	if (player._pMana < cost)
		return;
	player._pMana -= cost;
	player._pManaBase -= cost;
	if (&player == MyPlayer)
		RedrawComponent(PanelDrawComponent::Mana);
	player.poe2.activeSkill = skill;
}

int8_t WeaponSkillSkippedFrames(const Player &player)
{
	return player.poe2.activeSkill == WeaponSkill::Flurry ? 3 : 0;
}

} // namespace devilution
