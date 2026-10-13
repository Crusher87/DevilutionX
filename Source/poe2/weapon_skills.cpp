#include "poe2/weapon_skills.hpp"

#include <algorithm>
#include <cstdlib>

#include "cursor.h"
#include "engine/backbuffer_state.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "missiles.h"
#include "monster.h"
#include "msg.h"
#include "multi.h"
#include "player.h"
#include "plrmsg.h"
#include "poe2/dodge_roll.hpp"
#include "poe2/support_gems.hpp"
#include "utils/format.hpp"
#include "utils/is_of.hpp"
#include "utils/language.h"

namespace devilution {

namespace {

/** A queued skill is dropped if no attack starts within this many game ticks. */
constexpr uint8_t PendingSkillTicks = 40;
constexpr int LeapSlamTiles = 5;
constexpr int ShieldChargeTiles = 6;
constexpr int EscapeShotTiles = 3;
/** Game ticks a dash skill may take to land before it's dropped. */
constexpr uint8_t DashSkillTimeoutTicks = 40;
/** Battle Cry empowers melee hits for 8 seconds. */
constexpr uint8_t BattleCryTicks = 8 * 20;

/** @brief The targeted monster's tile, else the cursor tile, else the tile in front of the player. */
Point SkillTarget(const Player &player)
{
	Point target = pcursmonst != -1 ? static_cast<Point>(Monsters[pcursmonst].position.tile) : cursPosition;
	if (target == player.position.tile || !InDungeonBounds(target))
		target = player.position.tile + player._pdir;
	return target;
}

void PaySkillMana(Player &player, WeaponSkill skill)
{
	const Fixed26_6 cost = Fixed26_6::fromInt(WeaponSkillManaCost(player, skill));
	player._pMana -= cost;
	player._pManaBase -= cost;
	if (&player == MyPlayer)
		RedrawComponent(PanelDrawComponent::Mana);
}

/** @brief Dashes at the target; the skill hits when the dash lands (see ProcessPlayers). */
bool StartDashSkill(Player &player, WeaponSkill skill, int tiles)
{
	if (!DashTowards(player, SkillTarget(player), tiles))
		return false;
	PaySkillMana(player, skill);
	// Debris kicked up at take-off.
	AddVisualEffect(player.position.tile, skill == WeaponSkill::LeapSlam ? MissileGraphicID::StoneCurseShatter : MissileGraphicID::BoneHit);
	player.poe2.dashSkill = skill;
	player.poe2.dashSkillTicks = DashSkillTimeoutTicks;
	return true;
}

bool CanStartSkill(const Player &player)
{
	return IsAnyOf(player._pmode, PM_STAND, PM_WALK_NORTHWARDS, PM_WALK_SOUTHWARDS, PM_WALK_SIDEWAYS) && !player.poe2.isDodging();
}

/** @brief Pays for the skill and plays its cast animation; it goes off on the action frame (see ExecuteSkillCast). */
bool StartSkillCast(Player &player, WeaponSkill skill, player_graphic graphic)
{
	if (!CanStartSkill(player))
		return false;
	PaySkillMana(player, skill);
	BeginSkillAnimation(player, skill, graphic, SkillTarget(player));
	return true;
}

/** @brief Fires missiles in a fan towards a target, one tile apart at the target. */
void FireFan(const Player &player, Point target, MissileID missile, int count, int damage, int spellLevel)
{
	const Direction dir = GetDirection(player.position.tile, target);
	const Displacement side { Left(Left(dir)) };
	for (int i = 0; i < count; i++) {
		// 0, -1, 1, -2, 2, ...
		const int offset = (i + 1) / 2 * (i % 2 == 0 ? 1 : -1);
		AddMissile(player.position.tile, target + side * offset, dir, missile, TARGET_MONSTERS, player, damage, spellLevel);
	}
}

/** @brief Plays an effect on every tile at the given distance around a tile. */
void EffectRing(Point center, int radius, MissileGraphicID graphic, int delay)
{
	for (int y = -radius; y <= radius; y++) {
		for (int x = -radius; x <= radius; x++) {
			if (std::max(std::abs(x), std::abs(y)) == radius)
				AddVisualEffect(center + Displacement { x, y }, graphic, delay);
		}
	}
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
	case WeaponSkill::ShieldCharge:
		return _("Shield Charge");
	case WeaponSkill::BattleCry:
		return _("Battle Cry");
	case WeaponSkill::LightningArrow:
		return _("Lightning Arrow");
	case WeaponSkill::EscapeShot:
		return _("Escape Shot");
	case WeaponSkill::EmberFusillade:
		return _("Ember Fusillade");
	case WeaponSkill::Spark:
		return _("Spark");
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
	case WeaponSkill::ShieldCharge:
		return _("Charge in, bash and knock back");
	case WeaponSkill::BattleCry:
		return _("Knock back foes, +50% melee damage");
	case WeaponSkill::LightningArrow:
		return _("Arrow bursts into lightning");
	case WeaponSkill::EscapeShot:
		return _("Fire three arrows and leap back");
	case WeaponSkill::EmberFusillade:
		return _("Hurls a fan of five firebolts");
	case WeaponSkill::Spark:
		return _("Sprays erratic lightning sparks");
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
	case WeaponSkill::ShieldCharge:
		return 8;
	case WeaponSkill::BattleCry:
		return 10;
	case WeaponSkill::LightningArrow:
		return 8;
	case WeaponSkill::EscapeShot:
		return 9;
	case WeaponSkill::EmberFusillade:
		return 16;
	case WeaponSkill::Spark:
		return 10;
	default:
		return 0;
	}
}

int WeaponSkillManaCost(const Player &player, WeaponSkill skill)
{
	return WeaponSkillManaCost(skill) * SupportManaCostPercent(player, SkillSlot::ForAttack(skill)) / 100;
}

bool IsRangedWeaponSkill(WeaponSkill skill)
{
	return IsAnyOf(skill, WeaponSkill::Barrage, WeaponSkill::RainOfArrows, WeaponSkill::LightningArrow);
}

bool WeaponSkillNeedsWeapon(WeaponSkill skill)
{
	return IsNoneOf(skill, WeaponSkill::None, WeaponSkill::BattleCry, WeaponSkill::EmberFusillade, WeaponSkill::Spark);
}

bool ItemAllowsWeaponSkill(const Item &item, WeaponSkill skill)
{
	if (item.isEmpty())
		return false;
	if (skill == WeaponSkill::ShieldCharge)
		return item._itype == ItemType::Shield;
	if (item._iClass != ICLASS_WEAPON)
		return false;
	switch (skill) {
	case WeaponSkill::Cleave:
		return IsAnyOf(item._itype, ItemType::Sword, ItemType::Axe);
	case WeaponSkill::Flurry:
		return item._itype == ItemType::Sword;
	case WeaponSkill::LeapSlam:
		return IsAnyOf(item._itype, ItemType::Mace, ItemType::Axe, ItemType::Sword, ItemType::Staff);
	case WeaponSkill::RainOfArrows:
	case WeaponSkill::LightningArrow:
	case WeaponSkill::EscapeShot:
		return item._itype == ItemType::Bow;
	default:
		return skill != WeaponSkill::None && WeaponSkillForItem(item) == skill;
	}
}

bool WeaponAllowsSkill(const Player &player, WeaponSkill skill)
{
	if (!WeaponSkillNeedsWeapon(skill))
		return skill != WeaponSkill::None;
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
	case WeaponSkill::LightningArrow:
	case WeaponSkill::EscapeShot:
		return _("bow");
	case WeaponSkill::ShieldCharge:
		return _("shield");
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
	if (leveltype == DTYPE_TOWN) {
		EventPlrMsg(_("Skills can't be used in town."), UiFlags::ColorRed);
		return false;
	}

	if (!WeaponAllowsSkill(player, skill)) {
		EventPlrMsg(FormatRuntime(_("{:s} needs a {:s}."), WeaponSkillName(skill), WeaponSkillWeapons(skill)), UiFlags::ColorRed);
		return false;
	}
	if (player._pMana < Fixed26_6::fromInt(WeaponSkillManaCost(player, skill))) {
		EventPlrMsg(FormatRuntime(_("Not enough mana for {:s}."), WeaponSkillName(skill)), UiFlags::ColorRed);
		return false;
	}

	switch (skill) {
	case WeaponSkill::LeapSlam:
		return StartDashSkill(player, skill, LeapSlamTiles);
	case WeaponSkill::ShieldCharge:
		return StartDashSkill(player, skill, ShieldChargeTiles);
	case WeaponSkill::BattleCry:
		return StartSkillCast(player, skill, player_graphic::Magic);
	case WeaponSkill::EscapeShot:
		return StartSkillCast(player, skill, player_graphic::Attack);
	case WeaponSkill::EmberFusillade:
		return StartSkillCast(player, skill, player_graphic::Fire);
	case WeaponSkill::Spark:
		return StartSkillCast(player, skill, player_graphic::Lightning);
	default:
		break;
	}

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

	const Fixed26_6 cost = Fixed26_6::fromInt(WeaponSkillManaCost(player, skill));
	if (player._pMana < cost)
		return;
	player._pMana -= cost;
	player._pManaBase -= cost;
	if (&player == MyPlayer)
		RedrawComponent(PanelDrawComponent::Mana);
	player.poe2.activeSkill = skill;
}

void BeginSkillAnimation(Player &player, WeaponSkill skill, player_graphic graphic, Point target)
{
	const Direction dir = target == player.position.tile ? player._pdir : GetDirection(player.position.tile, target);
	const bool attack = graphic == player_graphic::Attack;
	const int8_t actionFrame = attack ? player._pAFNum - 1 : player._pSFNum;
	NewPlrAnim(player, graphic, dir, AnimationDistributionFlags::ProcessAnimationPending, 0, attack ? player._pAFNum : player._pSFNum);
	player._pmode = PM_SPELL;
	FixPlayerLocation(player, dir);
	FixPlrWalkTags(player);
	player.occupyTile(player.position.tile, false);
	SetPlayerOld(player);
	player.position.temp = target;
	player.poe2.castSkill = skill;
	player.poe2.castFrame = actionFrame;
	player.poe2.castGraphic = static_cast<uint8_t>(graphic);
}

bool ExecuteSkillCast(Player &player, WeaponSkill skill, Point target)
{
	PlaySkillEffects(player, skill, target);
	switch (skill) {
	case WeaponSkill::BattleCry:
		player.poe2.warcryTicks = BattleCryTicks;
		player.Say(HeroSpeech::TimeToDie);
		for (int i = 0; i < 8; i++) {
			Monster *monster = FindMonsterAtPosition(player.position.tile + static_cast<Direction>(i));
			if (monster != nullptr && !CanTalkToMonst(*monster) && !monster->hasNoLife() && monster->mode != MonsterMode::Petrified)
				M_GetKnockback(*monster, player.position.tile);
		}
		return false;
	case WeaponSkill::EmberFusillade:
		// The missiles take their damage from Magic and the gem level, like the spells.
		FireFan(player, target, MissileID::Firebolt, 5, 0, GemSpellLevel(player.getCharacterLevel()));
		return false;
	case WeaponSkill::Spark:
		FireFan(player, target, MissileID::ChargedBolt, 6, 0, GemSpellLevel(player.getCharacterLevel()));
		return false;
	case WeaponSkill::EscapeShot: {
		// Arrows take their damage from the bow when they hit, like normal arrows.
		FireFan(player, target, MissileID::Arrow, 3, 4, 0);
		PlaySfxLoc(SfxID::ShootBow2, player.position.tile);
		// Leap back as soon as the arrows are loosed.
		const Direction dir = GetDirection(player.position.tile, target);
		StartStand(player, player._pdir);
		player.poe2.castSkill = WeaponSkill::None;
		DashTowards(player, player.position.tile + Displacement { Opposite(dir) } * EscapeShotTiles, EscapeShotTiles);
		return true;
	}
	default:
		return false;
	}
}

void PlaySkillEffects(const Player &player, WeaponSkill skill, Point target)
{
	const Point tile = player.position.tile;
	const Direction dir = player._pdir;
	switch (skill) {
	case WeaponSkill::Cleave:
		// A spray of sparks sweeping across the arc.
		AddVisualEffect(tile + Right(dir), MissileGraphicID::MetalHit, 0, 3);
		AddVisualEffect(tile + dir, MissileGraphicID::MetalHit, 1, 5);
		AddVisualEffect(tile + Left(dir), MissileGraphicID::MetalHit, 2, 3);
		AddVisualEffect(tile + dir, MissileGraphicID::BloodHit, 2);
		break;
	case WeaponSkill::GroundSlam:
		// The ground bursts in front and a shockwave of rubble rolls outwards.
		AddVisualEffect(tile + dir, MissileGraphicID::ApocalypseBoom, 0, 8);
		EffectRing(tile, 1, MissileGraphicID::StoneCurseShatter, 1);
		EffectRing(tile, 2, MissileGraphicID::StoneCurseShatter, 4);
		PlaySfxLoc(SfxID::SpellStoneCurse, tile);
		break;
	case WeaponSkill::WhirlingStaff:
		// A ring of arcane light spins out around the player.
		AddVisualEffect(tile, MissileGraphicID::FlashBottom, 0, 7);
		AddVisualEffect(tile, MissileGraphicID::FlashTop);
		PlaySfxLoc(SfxID::SpellNova, tile);
		break;
	case WeaponSkill::Flurry:
		// One burst per strike.
		AddVisualEffect(target, MissileGraphicID::MetalHit, 0, 4);
		AddVisualEffect(target, MissileGraphicID::MetalHit, 3, 4);
		AddVisualEffect(target, MissileGraphicID::BloodHit, 3);
		break;
	case WeaponSkill::LeapSlam:
		// A fiery crater where the player lands.
		AddVisualEffect(tile, MissileGraphicID::BigExplosion, 0, 10);
		EffectRing(tile, 1, MissileGraphicID::StoneCurseShatter, 2);
		EffectRing(tile, 2, MissileGraphicID::StoneCurseShatter, 5);
		PlaySfxLoc(SfxID::BigExplosion, tile);
		break;
	case WeaponSkill::ShieldCharge:
		// A blinding flash where the shield hits.
		AddVisualEffect(tile + dir, MissileGraphicID::HolyBoltExplosion, 0, 8);
		AddVisualEffect(tile + Left(dir), MissileGraphicID::MetalHit, 1);
		AddVisualEffect(tile + Right(dir), MissileGraphicID::MetalHit, 1);
		PlaySfxLoc(SfxID::SpellHolyBolt, tile);
		break;
	case WeaponSkill::BattleCry:
		// A blood-red shockwave rolling out from the player.
		AddVisualEffect(tile, MissileGraphicID::BloodStarExplosion, 0, 8);
		EffectRing(tile, 1, MissileGraphicID::BloodStarExplosion, 2);
		EffectRing(tile, 2, MissileGraphicID::BloodStarExplosion, 5);
		PlaySfxLoc(SfxID::SpellBloodStar, tile);
		break;
	case WeaponSkill::Barrage:
		AddVisualEffect(tile + dir, MissileGraphicID::MetalHit, 0, 4);
		break;
	case WeaponSkill::RainOfArrows:
		// Arrowheads spark as the volley comes down.
		for (size_t i = 0; i < RainOfArrowsVolley.size(); i++)
			AddVisualEffect(target + RainOfArrowsVolley[i], MissileGraphicID::MetalHit, 6 + static_cast<int>(i), i == 0 ? 4 : 0);
		break;
	case WeaponSkill::EscapeShot:
		AddVisualEffect(tile, MissileGraphicID::BoneHit, 0, 3);
		break;
	case WeaponSkill::EmberFusillade:
		// Flames burst from the caster's hands.
		AddVisualEffect(tile + dir, MissileGraphicID::MagmaBallExplosion, 0, 8);
		break;
	case WeaponSkill::Spark:
		AddVisualEffect(tile + dir, MissileGraphicID::Lightning, 0, 8);
		AddVisualEffect(tile, MissileGraphicID::Lightning, 2);
		break;
	default:
		break;
	}
}

int8_t WeaponSkillSkippedFrames(const Player &player)
{
	const int8_t flurry = player.poe2.activeSkill == WeaponSkill::Flurry ? 3 : 0;
	return flurry + SupportAttackSkippedFrames(player, player.poe2.activeSkill);
}

int LightningArrowDamage(const Player &player)
{
	return player.getCharacterLevel() * 2 + player._pDexterity / 4;
}

} // namespace devilution
