#include "poe2/weapon_skills.hpp"

#include <algorithm>
#include <cstdlib>

#include "cursor.h"
#include "engine/backbuffer_state.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "levels/tile_properties.hpp"
#include "missiles.h"
#include "monster.h"
#include "msg.h"
#include "multi.h"
#include "player.h"
#include "plrmsg.h"
#include "poe2/camera_shake.hpp"
#include "poe2/dodge_roll.hpp"
#include "poe2/gem_levels.hpp"
#include "poe2/support_gems.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/unique_effects.hpp"
#include "tables/misdat.h"
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
	// POE2 Mashup: Deep Well makes the next skill free.
	const Fixed26_6 cost = TakeFreeSkill(player) ? Fixed26_6::fromInt(0) : Fixed26_6::fromInt(WeaponSkillManaCost(player, skill));
	player._pMana -= cost;
	player._pManaBase -= cost;
	NoteResourceSpent(player, cost);
	if (&player == MyPlayer)
		RedrawComponent(PanelDrawComponent::Mana);
	TelemetrySkillUsed(player, WeaponSkillName(skill), cost.whole(), player.poe2.skillLevel(skill));
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

/**
 * @brief Fires a skill's missiles in a fan towards a target, one tile apart at the target.
 * The gem level adds projectiles and damage along the skill's curve.
 */
void FireFan(const Player &player, WeaponSkill skill, Point target, MissileID missile, int count, int damage, int spellLevel)
{
	const SkillSlot slot = SkillSlot::ForAttack(skill);
	const int gemLevel = player.poe2.skillLevel(skill);
	count += GemExtraProjectiles(slot, gemLevel);
	const int damagePercent = GemDamagePercent(slot, gemLevel);
	const Direction dir = GetDirection(player.position.tile, target);
	const Displacement side { Left(Left(dir)) };
	for (int i = 0; i < count; i++) {
		// 0, -1, 1, -2, 2, ...
		const int offset = (i + 1) / 2 * (i % 2 == 0 ? 1 : -1);
		Missile *fired = AddMissile(player.position.tile, target + side * offset, dir, missile, TARGET_MONSTERS, player, damage, spellLevel);
		if (fired == nullptr)
			continue;
		fired->poe2.damagePercent = static_cast<int16_t>(damagePercent);
		// Spark's sparks crawl on through a crowd: each pierces a couple of enemies.
		if (skill == WeaponSkill::Spark) {
			fired->poe2.spell = SpellID::ChargedBolt;
			fired->poe2.piercesLeft = PierceCount;
		}
	}
}

/** @brief A point far along the line from the player through the target, so a fan aimed at it stays narrow. */
Point FarAlong(const Player &player, Point target, int tiles)
{
	const Direction dir = GetDirection(player.position.tile, target);
	return player.position.tile + Displacement { dir } * tiles;
}

/** @brief Fires one shot of a skill's stream. */
void FireVolleyShot(Player &player, WeaponSkill skill, Point target, int index)
{
	const Point tile = player.position.tile;
	const Direction dir = GetDirection(tile, target);
	const SkillSlot slot = SkillSlot::ForAttack(skill);
	const int gemLevel = player.poe2.skillLevel(skill);
	switch (skill) {
	case WeaponSkill::Barrage: {
		MissileID type = MissileID::Arrow;
		int damage = ElementalArrowBaseDamage;
		if (HasAnyOf(player._pIFlags, ItemSpecialEffect::FireArrows))
			type = MissileID::FireArrow;
		if (HasAnyOf(player._pIFlags, ItemSpecialEffect::LightningArrows))
			type = MissileID::LightningArrow;
		if (HasAllOf(player._pIFlags, ItemSpecialEffect::FireArrows | ItemSpecialEffect::LightningArrows)) {
			damage = RandomIntBetween(player._pIFMinDam, player._pIFMaxDam);
			type = MissileID::SpectralArrow;
		}
		// Each arrow wobbles a little so the stream reads as a rapid flurry rather than one arrow.
		constexpr int Wobble[] = { 0, 1, -1, 0, 1, -1, 0 };
		const Point aim = target + Displacement { Left(Left(dir)) } * Wobble[index % 7];
		Missile *arrow = AddMissile(tile, aim, dir, type, TARGET_MONSTERS, player, damage, 0);
		if (arrow != nullptr)
			arrow->poe2.damagePercent = static_cast<int16_t>(GemDamagePercent(slot, gemLevel));
		PlaySfxLoc(index == 0 ? SfxID::ShootBow2 : SfxID::ShootBow, tile);
		AddVisualEffect(tile + dir, MissileGraphicID::MetalHit, 0, index == 0 ? 3 : 0);
		break;
	}
	case WeaponSkill::EmberFusillade: {
		// Embers spring up on alternating sides of the caster and all converge on the target.
		const Direction side = index % 2 == 0 ? Left(Left(dir)) : Right(Right(dir));
		Point origin = tile + side;
		if (!InDungeonBounds(origin) || IsTileSolid(origin))
			origin = tile;
		Missile *ember = AddMissile(origin, target, dir, MissileID::Firebolt, TARGET_MONSTERS, player, 0, std::max(gemLevel, 1));
		if (ember != nullptr) {
			ember->poe2.damagePercent = static_cast<int16_t>(GemDamagePercent(slot, gemLevel));
			// Molten embers, not firebolts.
			if (GetMissileSpriteData(MissileGraphicID::MagmaBall).sprites)
				ember->setAnimation(MissileGraphicID::MagmaBall);
		}
		AddVisualEffect(origin, MissileGraphicID::MagmaBallExplosion, 0, 3);
		PlaySfxLoc(SfxID::SpellFirebolt, origin);
		break;
	}
	default:
		break;
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
		if (AllItemsList[item.IDidx].iItemId == UITYPE_DAGGER || AllItemsList[item.IDidx].iSName == "Dagger")
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
		return 4;
	case WeaponSkill::GroundSlam:
		return 8;
	case WeaponSkill::Barrage:
		return 7;
	case WeaponSkill::WhirlingStaff:
		return 7;
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
	const int cost = GemBaseManaCost(SkillSlot::ForAttack(skill), player.poe2.skillLevel(skill));
	return cost * SupportManaCostPercent(player, SkillSlot::ForAttack(skill)) / 100;
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
		TelemetrySkillFailed(player, WeaponSkillName(skill), "not enough mana");
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

	// POE2 Mashup: Deep Well makes the next skill free.
	const Fixed26_6 cost = TakeFreeSkill(player) ? Fixed26_6::fromInt(0) : Fixed26_6::fromInt(WeaponSkillManaCost(player, skill));
	if (player._pMana < cost) {
		TelemetrySkillFailed(player, WeaponSkillName(skill), "not enough mana when the attack landed");
		return;
	}
	player._pMana -= cost;
	player._pManaBase -= cost;
	NoteResourceSpent(player, cost);
	if (&player == MyPlayer)
		RedrawComponent(PanelDrawComponent::Mana);
	TelemetrySkillUsed(player, WeaponSkillName(skill), cost.whole(), player.poe2.skillLevel(skill));
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
		// A stream of molten embers, one after another; they take their damage from Magic and the gem level, like the spells.
		StartSkillVolley(player, skill, target, EmberFusilladeEmbers + GemExtraProjectiles(SkillSlot::ForAttack(skill), player.poe2.skillLevel(skill)));
		return false;
	case WeaponSkill::Spark:
		// A tight cone of crawling sparks that pierce, unlike Charged Bolt's wide random spray.
		FireFan(player, skill, FarAlong(player, target, 8), MissileID::ChargedBolt, 6, 0, std::max(player.poe2.skillLevel(skill), 1));
		return false;
	case WeaponSkill::EscapeShot: {
		// Arrows take their damage from the bow when they hit, like normal arrows.
		FireFan(player, skill, target, MissileID::Arrow, 3, 4, 0);
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

void SetSpellMissileSpeed(Missile &missile, SpellID spell)
{
	int percent = 100;
	if (spell == SpellID::Fireball)
		percent = FireballSpeedPercent;
	else if (spell == SpellID::Firebolt)
		percent = FireboltSpeedPercent;
	if (percent == 100)
		return;
	missile.position.velocity = { missile.position.velocity.deltaX * percent / 100, missile.position.velocity.deltaY * percent / 100 };
}

void OnFireballDetonated(Missile &missile, Point position)
{
	if (!gbIsPoe2)
		return;
	const Player *player = missile.sourcePlayer();
	// A ring of flame bursts out and the blast throws back everything beside it.
	EffectRing(position, 1, MissileGraphicID::MagmaBallExplosion, 1);
	for (int i = 0; i < 8; i++) {
		Monster *monster = FindMonsterAtPosition(position + static_cast<Direction>(i));
		if (monster != nullptr && !monster->isPlayerMinion() && !monster->hasNoLife() && !monster->isUnique() && monster->mode != MonsterMode::Petrified)
			M_GetKnockback(*monster, position);
	}
	PlaySfxLoc(SfxID::BigExplosion, position);
	if (player == MyPlayer)
		ShakeCamera(ShakeStrength::Medium, 6);
}

bool InGroundSlamCone(Direction facing, Displacement offset)
{
	const Displacement forward { facing };
	const int dot = (offset.deltaX * forward.deltaX) + (offset.deltaY * forward.deltaY);
	if (dot <= 0)
		return false;
	// Within about 35 degrees of the facing direction: cos^2 >= 2/3.
	const int lengthSquared = (offset.deltaX * offset.deltaX) + (offset.deltaY * offset.deltaY);
	const int forwardSquared = (forward.deltaX * forward.deltaX) + (forward.deltaY * forward.deltaY);
	return 3 * dot * dot >= 2 * lengthSquared * forwardSquared;
}

void StartSkillVolley(Player &player, WeaponSkill skill, Point target, int shots)
{
	Poe2PlayerData &data = player.poe2;
	data.volleySkill = skill;
	data.volleyShotsLeft = static_cast<uint8_t>(std::clamp(shots, 0, 255));
	data.volleyShotsFired = 0;
	data.volleyTicks = 0;
	data.volleyTargetX = static_cast<int16_t>(target.x);
	data.volleyTargetY = static_cast<int16_t>(target.y);
	ProcessSkillVolley(player);
}

void ProcessSkillVolley(Player &player)
{
	Poe2PlayerData &data = player.poe2;
	if (data.volleyShotsLeft == 0)
		return;
	if (data.volleyTicks > 0) {
		data.volleyTicks--;
		return;
	}
	if (player.hasNoLife() || leveltype == DTYPE_TOWN) {
		data.volleyShotsLeft = 0;
		return;
	}
	FireVolleyShot(player, data.volleySkill, { data.volleyTargetX, data.volleyTargetY }, data.volleyShotsFired);
	data.volleyShotsFired++;
	data.volleyShotsLeft--;
	data.volleyTicks = static_cast<uint8_t>((data.volleySkill == WeaponSkill::Barrage ? BarrageShotTicks : EmberShotTicks) - 1);
}

void PlaySkillEffects(const Player &player, WeaponSkill skill, Point target)
{
	const Point tile = player.position.tile;
	const Direction dir = player._pdir;
	const bool mine = &player == MyPlayer;
	switch (skill) {
	case WeaponSkill::Cleave:
		// A spray of sparks sweeping across the arc, right to left.
		AddVisualEffect(tile + Right(dir), MissileGraphicID::MetalHit, 0, 3);
		AddVisualEffect(tile + dir, MissileGraphicID::MetalHit, 1, 5);
		AddVisualEffect(tile + Left(dir), MissileGraphicID::MetalHit, 2, 3);
		AddVisualEffect(tile + dir, MissileGraphicID::BloodHit, 2);
		break;
	case WeaponSkill::GroundSlam:
		// The ground bursts at the weapon and a wave of rubble rolls out through the cone, tile by tile.
		AddVisualEffect(tile + dir, MissileGraphicID::ApocalypseBoom, 0, 8);
		for (int dy = -GroundSlamReach; dy <= GroundSlamReach; dy++) {
			for (int dx = -GroundSlamReach; dx <= GroundSlamReach; dx++) {
				const Displacement offset { dx, dy };
				if (!InGroundSlamCone(dir, offset))
					continue;
				const int distance = std::max(std::abs(dx), std::abs(dy));
				AddVisualEffect(tile + offset, MissileGraphicID::StoneCurseShatter, distance * 2);
				if (distance == GroundSlamReach && offset == Displacement { dir } * GroundSlamReach)
					AddVisualEffect(tile + offset, MissileGraphicID::BigExplosion, distance * 2, 6);
			}
		}
		PlaySfxLoc(SfxID::SpellStoneCurse, tile);
		PlaySfxLoc(SfxID::BigExplosion, tile);
		if (mine)
			ShakeCamera(ShakeStrength::Heavy, 10);
		break;
	case WeaponSkill::WhirlingStaff:
		// Arcane light whips around the player twice, once per ring.
		AddVisualEffect(tile, MissileGraphicID::FlashBottom, 0, 7);
		AddVisualEffect(tile, MissileGraphicID::FlashTop);
		for (int i = 0; i < 8; i++) {
			const Direction around = static_cast<Direction>((static_cast<int>(dir) + i) % 8);
			AddVisualEffect(tile + around, MissileGraphicID::HolyBoltExplosion, i);
			AddVisualEffect(tile + Displacement { around } * WhirlingStaffReach, MissileGraphicID::MetalHit, 8 + i);
		}
		PlaySfxLoc(SfxID::SpellNova, tile);
		if (mine)
			ShakeCamera(ShakeStrength::Light, 6);
		break;
	case WeaponSkill::Flurry:
		// One burst per strike.
		AddVisualEffect(target, MissileGraphicID::MetalHit, 0, 4);
		AddVisualEffect(target, MissileGraphicID::MetalHit, 3, 4);
		AddVisualEffect(target, MissileGraphicID::BloodHit, 3);
		break;
	case WeaponSkill::LeapSlam:
		// A fiery crater where the player lands, and the shockwave rolling out of it.
		AddVisualEffect(tile, MissileGraphicID::BigExplosion, 0, 10);
		EffectRing(tile, 1, MissileGraphicID::StoneCurseShatter, 2);
		EffectRing(tile, 2, MissileGraphicID::StoneCurseShatter, 5);
		PlaySfxLoc(SfxID::BigExplosion, tile);
		if (mine)
			ShakeCamera(ShakeStrength::Heavy, 8);
		break;
	case WeaponSkill::ShieldCharge:
		// A blinding flash where the shield hits.
		AddVisualEffect(tile + dir, MissileGraphicID::HolyBoltExplosion, 0, 8);
		AddVisualEffect(tile + Left(dir), MissileGraphicID::MetalHit, 1);
		AddVisualEffect(tile + Right(dir), MissileGraphicID::MetalHit, 1);
		PlaySfxLoc(SfxID::SpellHolyBolt, tile);
		if (mine)
			ShakeCamera(ShakeStrength::Medium, 6);
		break;
	case WeaponSkill::BattleCry:
		// A blood-red shockwave rolling out from the player.
		AddVisualEffect(tile, MissileGraphicID::BloodStarExplosion, 0, 8);
		EffectRing(tile, 1, MissileGraphicID::BloodStarExplosion, 2);
		EffectRing(tile, 2, MissileGraphicID::BloodStarExplosion, 5);
		PlaySfxLoc(SfxID::SpellBloodStar, tile);
		if (mine)
			ShakeCamera(ShakeStrength::Medium, 8);
		break;
	case WeaponSkill::Barrage:
		// The bow strains as the stream begins.
		AddVisualEffect(tile + dir, MissileGraphicID::MetalHit, 0, 4);
		break;
	case WeaponSkill::RainOfArrows:
		// Arrowheads spark as the volley comes down.
		for (size_t i = 0; i < RainOfArrowsVolley.size(); i++)
			AddVisualEffect(target + RainOfArrowsVolley[i], MissileGraphicID::MetalHit, 6 + static_cast<int>(i), i == 0 ? 4 : 0);
		if (mine)
			ShakeCamera(ShakeStrength::Light, 4);
		break;
	case WeaponSkill::LightningArrow:
		if (mine)
			ShakeCamera(ShakeStrength::Light, 3);
		break;
	case WeaponSkill::EscapeShot:
		AddVisualEffect(tile, MissileGraphicID::BoneHit, 0, 3);
		break;
	case WeaponSkill::EmberFusillade:
		// Heat gathers around the caster before the embers fly.
		AddVisualEffect(tile, MissileGraphicID::Inferno, 0, 8);
		break;
	case WeaponSkill::Spark:
		// Lightning crackles from the caster's hands.
		AddVisualEffect(tile + dir, MissileGraphicID::ThinLightning, 0, 8);
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
