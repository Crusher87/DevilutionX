#include "poe2/weapon_skills.hpp"

#include <algorithm>
#include <cstdlib>

#include "cursor.h"
#include "engine/backbuffer_state.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "headless_mode.hpp"
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
#include "poe2/aim.hpp"
#include "poe2/camera_shake.hpp"
#include "poe2/dodge_roll.hpp"
#include "poe2/gem_levels.hpp"
#include "poe2/skill_resources.hpp"
#include "poe2/spell_base.hpp"
#include "poe2/spirit.hpp"
#include "poe2/support_gems.hpp"
#include "poe2/support_table.hpp"
#include "poe2/table_skills.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/unique_effects.hpp"
#include "poe2/vfx.hpp"
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
/** Bolts Rapid Fire looses before its gem level adds more. */
constexpr int RapidFireBolts = 6;
constexpr int RapidFireShotTicks = 3;
/** Frost Nova's shards fly this many ticks, about 4 tiles. */
constexpr int FrostNovaShardTicks = 9;
constexpr int FrostNovaShards = 16;
/** Riposte and Shield Bash hit this hard before the gem curve. */
constexpr int RiposteDamagePercent = 180;
constexpr int ShieldBashDamagePercent = 120;
constexpr int ShadowStepDamagePercent = 150;

bool IsDagger(const Item &item)
{
	return item._itype == ItemType::Sword && (AllItemsList[item.IDidx].iItemId == UITYPE_DAGGER || AllItemsList[item.IDidx].iSName == "Dagger");
}

bool IsCrossbow(const Item &item)
{
	return item._itype == ItemType::Bow && AllItemsList[item.IDidx].iSName == "Crossbow";
}

/** @brief Ice Bolt's damage before the gem curve: like Firebolt's, from Magic and the gem level. */
int IceBoltDamage(const Player &player, int gemLevel)
{
	return RollSpellBase(player, gemLevel);
}

/** @brief Fires a skill's ice shard, which deals cold (Magic) damage and so chills and freezes. */
Missile *FireIceShard(const Player &player, WeaponSkill skill, Point from, Point target, int damagePercent)
{
	const int gemLevel = std::max(player.poe2.skillLevel(skill), 1);
	Missile *shard = AddMissile(from, target, GetDirection(from, target), MissileID::BlueFlare, TARGET_MONSTERS, player, IceBoltDamage(player, gemLevel), gemLevel);
	if (shard != nullptr) {
		shard->poe2.damagePercent = static_cast<int16_t>(GemDamagePercent(SkillSlot::ForAttack(skill), gemLevel) * damagePercent / 100);
		SetMissileSkill(*shard, player, SkillSlot::ForAttack(skill));
	}
	return shard;
}

/** @brief The monster standing in front of the player, if any can be hit. */
Monster *MonsterInFront(const Player &player)
{
	const Point position = player.position.tile + player._pdir;
	Monster *monster = FindMonsterAtPosition(position);
	if (monster == nullptr || CanTalkToMonst(*monster) || monster->position.old != position || monster->hasNoLife())
		return nullptr;
	return monster;
}

/** @brief Steps through the shadows to the tile behind the targeted monster. */
bool ShadowStepBehind(Player &player)
{
	if (pcursmonst == -1)
		return false;
	const Monster &monster = Monsters[pcursmonst];
	if (monster.hasNoLife() || player.position.tile.WalkingDistance(monster.position.tile) > 8)
		return false;
	const Direction through = GetDirection(player.position.tile, monster.position.tile);
	std::optional<Point> destination;
	for (const Direction dir : { through, Left(through), Right(through), Left(Left(through)), Right(Right(through)) }) {
		const Point tile = monster.position.tile + dir;
		if (InDungeonBounds(tile) && PosOkPlayer(player, tile)) {
			destination = tile;
			break;
		}
	}
	if (!destination)
		return false;
	AddVisualEffect(player.position.tile, MissileGraphicID::Etherealize);
	MovePlayerTo(player, *destination);
	player._pdir = GetDirection(player.position.tile, monster.position.tile);
	AddVisualEffect(player.position.tile, MissileGraphicID::BoneHit);
	return true;
}

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
	const bool free = TakeFreeSkill(player);
	const Fixed26_6 cost = free ? Fixed26_6::fromInt(0) : Fixed26_6::fromInt(WeaponSkillManaCost(player, skill));
	player._pMana -= cost;
	player._pManaBase -= cost;
	NoteResourceSpent(player, cost);
	if (!free)
		PaySupportLifeCost(player, SkillSlot::ForAttack(skill), WeaponSkillManaCost(player, skill, false));
	if (&player == MyPlayer)
		RedrawComponent(PanelDrawComponent::Mana);
	TelemetrySkillUsed(player, WeaponSkillName(skill), cost.whole(), player.poe2.skillLevel(skill));
}

bool CanStartSkill(const Player &player);

/** @brief Dashes at the target; the skill hits when the dash lands (see ProcessPlayers). */
bool StartDashSkill(Player &player, WeaponSkill skill, int tiles)
{
	if (!CanStartSkill(player))
		return false;
	// With no room to dash, such as an enemy right next to you, the skill goes off where you stand (Curtis 2026-10-21:
	// Whirlwind did nothing when he clicked an enemy in melee).
	const Point target = SkillTarget(player);
	if (!DashTowards(player, target, tiles) && target != player.position.tile)
		player._pdir = GetDirection(player.position.tile, target);
	PaySkillMana(player, skill);
	if (!player.UsesRangedWeapon())
		PayMeleeAttackLife(player);
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
	if (graphic == player_graphic::Attack && !player.UsesRangedWeapon())
		PayMeleeAttackLife(player);
	Point target = SkillTarget(player);
	// Projectiles with nothing under the cursor fly exactly where it points, at any angle.
	if (pcursmonst == -1 && &player == MyPlayer && IsAnyOf(skill, WeaponSkill::EmberFusillade, WeaponSkill::Spark, WeaponSkill::IceBolt))
		target = AimAlongCursor(player, AimTiles);
	BeginSkillAnimation(player, skill, graphic, target);
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
	count += GemExtraProjectiles(slot, gemLevel) + SupportExtraProjectileCount(player, slot);
	const int damagePercent = GemDamagePercent(slot, gemLevel);
	const Direction dir = GetDirection(player.position.tile, target);
	const Displacement side { Left(Left(dir)) };
	for (int i = 0; i < count; i++) {
		// 0, -1, 1, -2, 2, ...
		const int offset = (i + 1) / 2 * (i % 2 == 0 ? 1 : -1);
		Missile *fired = AddMissile(player.position.tile, target + side * offset, dir, missile, TARGET_MONSTERS, player, damage, spellLevel);
		if (fired == nullptr)
			continue;
		SetMissileSkill(*fired, player, slot);
		fired->poe2.damagePercent = static_cast<int16_t>(damagePercent);
		// Spark's sparks crawl on through a crowd: each pierces a couple of enemies.
		if (skill == WeaponSkill::Spark) {
			fired->poe2.spell = SpellID::ChargedBolt;
			fired->poe2.piercesLeft = PierceCount;
			fired->poe2.pierceLessPercent = static_cast<int8_t>(100 - PierceDamagePercent);
		}
	}
}

/** @brief A point far along the line from the player through the target, so a fan aimed at it stays narrow. */
Point FarAlong(const Player &player, Point target, int tiles)
{
	if (target == player.position.tile)
		return player.position.tile + Displacement { player._pdir } * tiles;
	return FarAlongLine(player.position.tile, target, tiles);
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
		if (arrow != nullptr) {
			arrow->poe2.damagePercent = static_cast<int16_t>(GemDamagePercent(slot, gemLevel));
			SetMissileSkill(*arrow, player, slot);
		}
		PlaySfxLoc(index == 0 ? SfxID::ShootBow2 : SfxID::ShootBow, tile);
		// The opening volley is drawn by PlaySkillEffects; each later arrow adds a shaft or two.
		if (index > 0)
			vfx::Volley(tile, aim, 2, false);
		break;
	}
	case WeaponSkill::RapidFire: {
		// Bolts fly dead straight, one after another, harder than Barrage's arrows but fewer.
		Missile *bolt = AddMissile(tile, target, dir, MissileID::Arrow, TARGET_MONSTERS, player, ElementalArrowBaseDamage, 0);
		if (bolt != nullptr) {
			bolt->poe2.damagePercent = static_cast<int16_t>(GemDamagePercent(slot, gemLevel));
			SetMissileSkill(*bolt, player, slot);
		}
		PlaySfxLoc(index == 0 ? SfxID::ShootBow2 : SfxID::ShootBow, tile);
		if (index > 0)
			vfx::Volley(tile, target, 1, false);
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
			SetMissileSkill(*ember, player, slot);
			// Molten embers, not firebolts.
			if (GetMissileSpriteData(MissileGraphicID::MagmaBall).sprites)
				ember->setAnimation(MissileGraphicID::MagmaBall);
		}
		AddVisualEffect(origin, MissileGraphicID::MagmaBallExplosion, 0, 3);
		PlaySfxLoc(SfxID::SpellFirebolt, origin);
		break;
	}
	default:
		if (const TableSkillData *table = FindTableSkill(skill); table != nullptr)
			FireTableShot(player, *table, target, index);
		break;
	}
}

bool TableWeaponAllows(const Item &item, TableWeapon weapon)
{
	if (weapon == TableWeapon::Shield)
		return item._itype == ItemType::Shield;
	if (item._iClass != ICLASS_WEAPON)
		return false;
	switch (weapon) {
	case TableWeapon::Sword:
		return item._itype == ItemType::Sword && !IsDagger(item);
	case TableWeapon::Dagger:
		return IsDagger(item);
	case TableWeapon::Axe:
		return item._itype == ItemType::Axe;
	case TableWeapon::AxeOrSword:
		return IsAnyOf(item._itype, ItemType::Axe, ItemType::Sword);
	case TableWeapon::Mace:
		return item._itype == ItemType::Mace;
	case TableWeapon::Staff:
		return item._itype == ItemType::Staff;
	case TableWeapon::Bow:
		return item._itype == ItemType::Bow && !IsCrossbow(item);
	case TableWeapon::Crossbow:
		return IsCrossbow(item);
	case TableWeapon::Melee:
		return IsAnyOf(item._itype, ItemType::Sword, ItemType::Axe, ItemType::Mace, ItemType::Staff);
	default:
		return false;
	}
}

std::string_view TableWeaponName(TableWeapon weapon)
{
	switch (weapon) {
	case TableWeapon::Sword:
		return _("sword");
	case TableWeapon::Dagger:
		return _("dagger");
	case TableWeapon::Axe:
		return _("axe");
	case TableWeapon::AxeOrSword:
		return _("axe or sword");
	case TableWeapon::Mace:
		return _("mace");
	case TableWeapon::Staff:
		return _("staff");
	case TableWeapon::Bow:
		return _("bow");
	case TableWeapon::Crossbow:
		return _("crossbow");
	case TableWeapon::Shield:
		return _("shield");
	case TableWeapon::Melee:
		return _("melee weapon");
	default:
		return {};
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
	const WeaponSkill right = WeaponSkillForItem(player.InvBody[INVLOC_HAND_RIGHT]);
	// A sceptre held with another weapon leaves the skill to that weapon.
	if (left != WeaponSkill::None && !(ItemGrantsSpirit(player.InvBody[INVLOC_HAND_LEFT]) && right != WeaponSkill::None))
		return left;
	return right;
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
	case WeaponSkill::IceBolt:
		return _("Ice Bolt");
	case WeaponSkill::FrostNova:
		return _("Frost Nova");
	case WeaponSkill::Riposte:
		return _("Riposte");
	case WeaponSkill::ShadowStep:
		return _("Shadow Step");
	case WeaponSkill::Frenzy:
		return _("Frenzy");
	case WeaponSkill::Smite:
		return _("Smite");
	case WeaponSkill::FistsOfThunder:
		return _("Fists of Thunder");
	case WeaponSkill::RapidFire:
		return _("Rapid Fire");
	case WeaponSkill::ShieldBash:
		return _("Shield Bash");
	case WeaponSkill::AmplifyDamage:
		return _("Amplify Damage");
	default:
		if (const TableSkillData *table = FindTableSkill(skill); table != nullptr)
			return _(table->name.data());
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
	case WeaponSkill::IceBolt:
		return _("A shard of ice that chills and freezes");
	case WeaponSkill::FrostNova:
		return _("A ring of frost shards bursts out");
	case WeaponSkill::Riposte:
		return _("After a block: a crushing counter");
	case WeaponSkill::ShadowStep:
		return _("Step behind the target and stab");
	case WeaponSkill::Frenzy:
		return _("Fast strike, builds Frenzy charges");
	case WeaponSkill::Smite:
		return _("Crushing blow that breaks armour");
	case WeaponSkill::FistsOfThunder:
		return _("Fast strikes; every third: lightning");
	case WeaponSkill::RapidFire:
		return _("A rapid stream of crossbow bolts");
	case WeaponSkill::ShieldBash:
		return _("Bash, stun and knock back");
	case WeaponSkill::AmplifyDamage:
		return _("Curse: foes take more physical damage");
	default:
		if (const TableSkillData *table = FindTableSkill(skill); table != nullptr)
			return _(table->description.data());
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
	case WeaponSkill::IceBolt:
		return 7;
	case WeaponSkill::FrostNova:
		return 14;
	case WeaponSkill::Riposte:
		return 5;
	case WeaponSkill::ShadowStep:
		return 8;
	case WeaponSkill::Frenzy:
		return 5;
	case WeaponSkill::Smite:
		return 7;
	case WeaponSkill::FistsOfThunder:
		return 4;
	case WeaponSkill::RapidFire:
		return 8;
	case WeaponSkill::ShieldBash:
		return 6;
	case WeaponSkill::AmplifyDamage:
		return 8;
	default:
		if (const TableSkillData *table = FindTableSkill(skill); table != nullptr)
			return table->mana;
		return 0;
	}
}

int WeaponSkillManaCost(const Player &player, WeaponSkill skill, bool lifeShare)
{
	const int cost = GemBaseManaCost(SkillSlot::ForAttack(skill), player.poe2.skillLevel(skill));
	return cost * SupportManaCostPercent(player, SkillSlot::ForAttack(skill), lifeShare) / 100;
}

bool IsRangedWeaponSkill(WeaponSkill skill)
{
	return IsAnyOf(skill, WeaponSkill::Barrage, WeaponSkill::RainOfArrows, WeaponSkill::LightningArrow);
}

bool WeaponSkillNeedsWeapon(WeaponSkill skill)
{
	if (const TableSkillData *table = FindTableSkill(skill); table != nullptr)
		return table->weapon != TableWeapon::None;
	return IsNoneOf(skill, WeaponSkill::None, WeaponSkill::BattleCry, WeaponSkill::EmberFusillade, WeaponSkill::Spark, WeaponSkill::IceBolt, WeaponSkill::FrostNova,
	    WeaponSkill::AmplifyDamage);
}

bool ItemAllowsWeaponSkill(const Item &item, WeaponSkill skill)
{
	if (item.isEmpty())
		return false;
	if (const TableSkillData *table = FindTableSkill(skill); table != nullptr)
		return TableWeaponAllows(item, table->weapon);
	if (IsAnyOf(skill, WeaponSkill::ShieldCharge, WeaponSkill::ShieldBash))
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
	case WeaponSkill::Riposte:
		return item._itype == ItemType::Sword && !IsDagger(item);
	case WeaponSkill::ShadowStep:
		return IsDagger(item);
	case WeaponSkill::Frenzy:
		return IsAnyOf(item._itype, ItemType::Axe, ItemType::Sword);
	case WeaponSkill::Smite:
		return item._itype == ItemType::Mace;
	case WeaponSkill::FistsOfThunder:
		return item._itype == ItemType::Staff;
	case WeaponSkill::RapidFire:
		return IsCrossbow(item);
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
	case WeaponSkill::ShieldBash:
		return _("shield");
	case WeaponSkill::Riposte:
		return _("sword");
	case WeaponSkill::ShadowStep:
		return _("dagger");
	case WeaponSkill::Frenzy:
		return _("axe or sword");
	case WeaponSkill::Smite:
		return _("mace");
	case WeaponSkill::FistsOfThunder:
		return _("staff");
	case WeaponSkill::RapidFire:
		return _("crossbow");
	default:
		if (const TableSkillData *table = FindTableSkill(skill); table != nullptr)
			return TableWeaponName(table->weapon);
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
	case WeaponSkill::IceBolt:
	case WeaponSkill::FrostNova:
	case WeaponSkill::AmplifyDamage:
		return StartSkillCast(player, skill, player_graphic::Magic);
	case WeaponSkill::Riposte:
		if (player.poe2.riposteTicks == 0) {
			EventPlrMsg(_("Riposte is ready only just after you block."), UiFlags::ColorRed);
			return false;
		}
		return StartSkillCast(player, skill, player_graphic::Attack);
	case WeaponSkill::ShadowStep:
		if (!CanStartSkill(player))
			return false;
		if (!ShadowStepBehind(player)) {
			EventPlrMsg(_("Shadow Step needs a monster nearby to step behind."), UiFlags::ColorRed);
			return false;
		}
		return StartSkillCast(player, skill, player_graphic::Attack);
	case WeaponSkill::RapidFire:
	case WeaponSkill::ShieldBash:
		return StartSkillCast(player, skill, player_graphic::Attack);
	default:
		if (const TableSkillData *table = FindTableSkill(skill); table != nullptr) {
			if (!CanStartSkill(player) || !StartTableSkill(player, *table))
				return false;
			if (table->shape == TableShape::Dash)
				return StartDashSkill(player, skill, table->count);
			constexpr player_graphic Graphics[] = { player_graphic::Attack, player_graphic::Magic, player_graphic::Fire, player_graphic::Lightning };
			return StartSkillCast(player, skill, Graphics[std::min<int>(table->graphic, 3)]);
		}
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
	// Shots fly exactly where the cursor points; Rain of Arrows lands on the spot itself.
	if (ranged && skill != WeaponSkill::RainOfArrows)
		target = AimAlongCursor(player, AimTiles);
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
	const bool free = TakeFreeSkill(player);
	const Fixed26_6 cost = free ? Fixed26_6::fromInt(0) : Fixed26_6::fromInt(WeaponSkillManaCost(player, skill));
	if (player._pMana < cost) {
		TelemetrySkillFailed(player, WeaponSkillName(skill), "not enough mana when the attack landed");
		return;
	}
	player._pMana -= cost;
	player._pManaBase -= cost;
	if (!free)
		PaySupportLifeCost(player, SkillSlot::ForAttack(skill), WeaponSkillManaCost(player, skill, false));
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
	if (SpinsWhileStriking(skill)) {
		player.poe2.spinFrom = static_cast<uint8_t>(dir);
		const TableSkillData *table = FindTableSkill(skill);
		vfx::Whirl(player.position.tile, table != nullptr ? std::max<int>(table->radius, 1) : 1, vfx::Element::Physical, false, WeaponElementLook(player));
		PlaySfxLoc(SfxID::Swing2, player.position.tile);
	}
}

bool SpinsWhileStriking(WeaponSkill skill)
{
	return skill == WeaponSkill::Whirlwind;
}

namespace {

/**
 * @brief The facing a spinning hero shows on an animation frame: one facing a frame, round once in eight frames. He
 * turns to his left, so his right-handed swing carries on round with the spin instead of fighting it.
 */
Direction SpinFacing(Direction from, int frame)
{
	return static_cast<Direction>((static_cast<int>(from) + 8 - frame % 8) % 8);
}

} // namespace

void TurnWithSpin(Player &player)
{
	// One facing a frame: round once in eight frames, all eight directions in turn.
	const Direction dir = SpinFacing(static_cast<Direction>(player.poe2.spinFrom), player.AnimInfo.currentFrame);
	if (dir == player._pdir)
		return;
	player._pdir = dir;
	if (!HeadlessMode)
		player.AnimInfo.sprites = player.AnimationData[static_cast<size_t>(player_graphic::Attack)].spritesForDirection(dir);
}

bool SpinsWhileSwinging(WeaponSkill skill)
{
	return skill == WeaponSkill::WhirlingStaff;
}

void BeginSpinningSwing(const Player &player)
{
	// Whirlwind's look, with a staff (Curtis 2026-10-22: "quite similar to the Warrior's Whirlwind"): the hero turns
	// through all eight facings inside a funnel of wind, both ends of the staff sweeping round out to its far reach.
	vfx::Whirl(player.position.tile, WhirlingStaffReach, vfx::Element::Physical, true, WeaponElementLook(player));
	PlaySfxLoc(SfxID::Swing2, player.position.tile);
}

void TurnSpriteWithSpin(Player &player)
{
	const PlayerAnimationData &attack = player.AnimationData[static_cast<size_t>(player_graphic::Attack)];
	if (HeadlessMode || !attack.sprites)
		return;
	player.AnimInfo.sprites = attack.spritesForDirection(SpinFacing(player._pdir, player.AnimInfo.currentFrame));
}

bool HoldsHeavyWeapon(const Player &player)
{
	const auto holds = [&](ItemType type) { return player.InvBody[INVLOC_HAND_LEFT]._itype == type || player.InvBody[INVLOC_HAND_RIGHT]._itype == type; };
	return !holds(ItemType::Sword) && (holds(ItemType::Axe) || holds(ItemType::Mace) || holds(ItemType::Staff));
}

vfx::SwingStyle WeaponSwingStyle(const Player &player)
{
	// The effect follows the attack animation the hero plays, and every class swings right-handed. Axe and mace attacks
	// come down from overhead (Curtis 2026-10-21: "the hero swings top-down, but the effect sweeps sideways"); a bare fist
	// or a shield punches straight out; a sword or a staff sweeps across from the hero's right to his left (Curtis
	// 2026-10-22).
	switch (static_cast<PlayerWeaponGraphic>(player._pgfxnum & 0xFU)) {
	case PlayerWeaponGraphic::Axe:
	case PlayerWeaponGraphic::Mace:
	case PlayerWeaponGraphic::MaceShield:
		return vfx::SwingStyle::Chop;
	case PlayerWeaponGraphic::Unarmed:
	case PlayerWeaponGraphic::UnarmedShield:
		return vfx::SwingStyle::Jab;
	default:
		return vfx::SwingStyle::Slash;
	}
}

vfx::Element WeaponElementLook(const Player &player)
{
	if (HasAnyOf(player._pIFlags, ItemSpecialEffect::FireDamage))
		return vfx::Element::Fire;
	if (HasAnyOf(player._pIFlags, ItemSpecialEffect::LightningDamage))
		return vfx::Element::Lightning;
	return vfx::Element::Physical;
}

bool ExecuteSkillCast(Player &player, WeaponSkill skill, Point target)
{
	PlaySkillEffects(player, skill, target);
	OnSkillUsed(player, SkillSlot::ForAttack(skill), target);
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
	case WeaponSkill::IceBolt: {
		const int extra = GemExtraProjectiles(SkillSlot::ForAttack(skill), player.poe2.skillLevel(skill));
		const Direction dir = GetDirection(player.position.tile, target);
		for (int i = 0; i <= extra; i++) {
			const int offset = (i + 1) / 2 * (i % 2 == 0 ? 1 : -1);
			FireIceShard(player, skill, player.position.tile, FarAlong(player, target, 8) + Displacement { Left(Left(dir)) } * offset, 100);
		}
		return false;
	}
	case WeaponSkill::FrostNova:
		// Shards burst out all around and melt away after a few tiles.
		for (int i = 0; i < FrostNovaShards; i++) {
			constexpr int Ring[16][2] = { { 4, 0 }, { 4, 2 }, { 4, 4 }, { 2, 4 }, { 0, 4 }, { -2, 4 }, { -4, 4 }, { -4, 2 }, { -4, 0 }, { -4, -2 }, { -4, -4 }, { -2, -4 }, { 0, -4 }, { 2, -4 }, { 4, -4 }, { 4, -2 } };
			Missile *shard = FireIceShard(player, skill, player.position.tile, player.position.tile + Displacement { Ring[i][0], Ring[i][1] }, 100);
			if (shard != nullptr)
				shard->duration = FrostNovaShardTicks;
		}
		return false;
	case WeaponSkill::AmplifyDamage:
		CastAmplifyDamage(player, target, player.poe2.skillLevel(skill));
		return false;
	case WeaponSkill::Riposte: {
		player.poe2.riposteTicks = 0;
		for (const Direction dir : { player._pdir, Left(player._pdir), Right(player._pdir) }) {
			const Point tile = player.position.tile + dir;
			Monster *monster = FindMonsterAtPosition(tile);
			if (monster != nullptr && !CanTalkToMonst(*monster) && monster->position.old == tile && !monster->hasNoLife())
				SkillHitMonster(player, *monster, dir == player._pdir ? RiposteDamagePercent : RiposteDamagePercent / 2);
		}
		return false;
	}
	case WeaponSkill::ShadowStep:
		if (Monster *monster = MonsterInFront(player); monster != nullptr)
			SkillHitMonster(player, *monster, ShadowStepDamagePercent);
		return false;
	case WeaponSkill::ShieldBash:
		if (Monster *monster = MonsterInFront(player); monster != nullptr) {
			if (SkillHitMonster(player, *monster, ShieldBashDamagePercent) && !monster->hasNoLife() && monster->mode != MonsterMode::Petrified) {
				HeavyStunMonster(*monster);
				M_GetKnockback(*monster, player.position.tile);
			}
		}
		return false;
	case WeaponSkill::RapidFire:
		StartSkillVolley(player, skill, target, RapidFireBolts + GemExtraProjectiles(SkillSlot::ForAttack(skill), player.poe2.skillLevel(skill)));
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
		if (const TableSkillData *table = FindTableSkill(skill); table != nullptr)
			ExecuteTableSkill(player, *table, target);
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
	int ticks = EmberShotTicks;
	if (data.volleySkill == WeaponSkill::Barrage)
		ticks = BarrageShotTicks;
	else if (data.volleySkill == WeaponSkill::RapidFire)
		ticks = RapidFireShotTicks;
	else if (const TableSkillData *table = FindTableSkill(data.volleySkill); table != nullptr)
		ticks = std::max<int>(table->interval, 1);
	data.volleyTicks = static_cast<uint8_t>(ticks - 1);
}

void PlaySkillEffects(const Player &player, WeaponSkill skill, Point target)
{
	const Point tile = player.position.tile;
	const Direction dir = player._pdir;
	const bool mine = &player == MyPlayer;
	switch (skill) {
	case WeaponSkill::Cleave:
		// Its own long slice through the target and both sides, not the plain swing (Curtis 2026-10-21); an axe
		// brings it down from overhead first, as the hero's swing does.
		vfx::Cleave(tile, tile + dir, vfx::Element::Physical, WeaponSwingStyle(player) == vfx::SwingStyle::Chop, WeaponElementLook(player));
		// A spray of sparks sweeping across the arc, right to left.
		AddVisualEffect(tile + Right(dir), MissileGraphicID::MetalHit, 0, 3);
		AddVisualEffect(tile + dir, MissileGraphicID::MetalHit, 1, 5);
		AddVisualEffect(tile + Left(dir), MissileGraphicID::MetalHit, 2, 3);
		AddVisualEffect(tile + dir, MissileGraphicID::BloodHit, 2);
		PlaySfxLoc(SfxID::Swing2, tile);
		if (mine)
			ShakeCamera(ShakeStrength::Light, 3);
		break;
	case WeaponSkill::GroundSlam:
		vfx::Slam(tile + dir, 2, vfx::Element::Physical);
		vfx::Fissure(tile + dir, tile + Displacement { dir } * GroundSlamReach, vfx::Element::Fire);
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
		// Whirlwind's look with a staff (Curtis 2026-10-22): the spin and its funnel of wind started with the swing (see
		// BeginSpinningSwing); here the staff cracks into everything around, without the old flashes of holy light.
		EffectRing(tile, 1, MissileGraphicID::MetalHit, 0);
		vfx::Sparks(tile, vfx::Element::Physical, 1.3F);
		PlaySfxLoc(SfxID::Swing, tile);
		if (mine)
			ShakeCamera(ShakeStrength::Light, 4);
		break;
	case WeaponSkill::Flurry: {
		// Two real swings: a slash, then a backhand coming back across it that hits harder (Curtis 2026-10-21).
		const bool backhand = player.poe2.flurryStrike > 0;
		vfx::SwingAs(tile, target, vfx::Element::Physical, backhand ? vfx::SwingStyle::Backhand : vfx::SwingStyle::Slash, false, WeaponElementLook(player));
		vfx::Sparks(target, vfx::Element::Physical, backhand ? 1.5F : 1.F);
		AddVisualEffect(target, MissileGraphicID::MetalHit, 0, 4);
		if (backhand) {
			AddVisualEffect(target, MissileGraphicID::BloodHit, 1);
			PlaySfxLoc(SfxID::Swing2, tile);
			if (mine)
				ShakeCamera(ShakeStrength::Light, 3);
		}
		break;
	}
	case WeaponSkill::LeapSlam:
		vfx::Slam(tile, 2, vfx::Element::Fire);
		// A fiery crater where the player lands, and the shockwave rolling out of it.
		AddVisualEffect(tile, MissileGraphicID::BigExplosion, 0, 10);
		EffectRing(tile, 1, MissileGraphicID::StoneCurseShatter, 2);
		EffectRing(tile, 2, MissileGraphicID::StoneCurseShatter, 5);
		PlaySfxLoc(SfxID::BigExplosion, tile);
		if (mine)
			ShakeCamera(ShakeStrength::Heavy, 8);
		break;
	case WeaponSkill::ShieldCharge:
		vfx::Impact(tile + dir, vfx::Element::Holy, 1.F);
		// A blinding flash where the shield hits.
		AddVisualEffect(tile + dir, MissileGraphicID::HolyBoltExplosion, 0, 8);
		AddVisualEffect(tile + Left(dir), MissileGraphicID::MetalHit, 1);
		AddVisualEffect(tile + Right(dir), MissileGraphicID::MetalHit, 1);
		PlaySfxLoc(SfxID::SpellHolyBolt, tile);
		if (mine)
			ShakeCamera(ShakeStrength::Medium, 6);
		break;
	case WeaponSkill::BattleCry:
		vfx::Impact(tile, vfx::Element::Blood, 1.4F);
		// A blood-red shockwave rolling out from the player.
		AddVisualEffect(tile, MissileGraphicID::BloodStarExplosion, 0, 8);
		EffectRing(tile, 1, MissileGraphicID::BloodStarExplosion, 2);
		EffectRing(tile, 2, MissileGraphicID::BloodStarExplosion, 5);
		PlaySfxLoc(SfxID::SpellBloodStar, tile);
		if (mine)
			ShakeCamera(ShakeStrength::Medium, 8);
		break;
	case WeaponSkill::Barrage:
		// A hail of arrows leaving the bow, not the spark sprite that read as lightning at the Rogue's feet (Curtis 2026-10-20).
		vfx::Volley(tile, target, 5, true);
		break;
	case WeaponSkill::RainOfArrows:
		vfx::Sigil(target, 1, 400, vfx::Element::Physical);
		// Arrowheads spark as the volley comes down.
		for (size_t i = 0; i < RainOfArrowsVolley.size(); i++)
			AddVisualEffect(target + RainOfArrowsVolley[i], MissileGraphicID::MetalHit, 6 + static_cast<int>(i), i == 0 ? 4 : 0);
		if (mine)
			ShakeCamera(ShakeStrength::Light, 4);
		break;
	case WeaponSkill::LightningArrow:
		vfx::Sparks(tile + dir, vfx::Element::Lightning, 0.8F);
		if (mine)
			ShakeCamera(ShakeStrength::Light, 3);
		break;
	case WeaponSkill::EscapeShot:
		vfx::Impact(tile, vfx::Element::Cold, 0.8F);
		AddVisualEffect(tile, MissileGraphicID::BoneHit, 0, 3);
		break;
	case WeaponSkill::EmberFusillade:
		vfx::Impact(tile, vfx::Element::Fire, 0.7F);
		// Heat gathers around the caster before the embers fly.
		AddVisualEffect(tile, MissileGraphicID::Inferno, 0, 8);
		break;
	case WeaponSkill::IceBolt:
		vfx::Impact(tile + dir, vfx::Element::Cold, 0.5F);
		// Frost gathers in the caster's hand, with a bright crystal zing (the Stone Curse thud was dull, Curtis 2026-10-16).
		AddVisualEffect(tile + dir, MissileGraphicID::StoneCurseShatter, 0, 4);
		PlaySfxLoc(SfxID::SpellHolyBolt, tile);
		break;
	case WeaponSkill::FrostNova:
		vfx::Impact(tile, vfx::Element::Cold, 1.6F);
		// A ring of frost cracks the floor all around.
		EffectRing(tile, 1, MissileGraphicID::StoneCurseShatter, 0);
		EffectRing(tile, 2, MissileGraphicID::StoneCurseShatter, 3);
		PlaySfxLoc(SfxID::SpellNova, tile);
		if (mine)
			ShakeCamera(ShakeStrength::Light, 5);
		break;
	case WeaponSkill::AmplifyDamage:
		vfx::Sigil(target, 1, 600, vfx::Element::Blood);
		// A blood-red curse settles over the spot.
		AddVisualEffect(target, MissileGraphicID::HolyBoltExplosion, 0, 6);
		EffectRing(target, 1, MissileGraphicID::BloodHit, 1);
		PlaySfxLoc(SfxID::SpellBloodStar, target);
		break;
	case WeaponSkill::Riposte:
		vfx::SwingAs(tile, tile + dir, vfx::Element::Holy, WeaponSwingStyle(player), HoldsHeavyWeapon(player));
		// A flash of steel sweeping across the arc with the blade, right to left.
		AddVisualEffect(tile + Right(dir), MissileGraphicID::MetalHit, 0, 4);
		AddVisualEffect(tile + dir, MissileGraphicID::HolyBoltExplosion, 1, 6);
		AddVisualEffect(tile + Left(dir), MissileGraphicID::MetalHit, 2, 4);
		PlaySfxLoc(SfxID::ItemSword, tile);
		if (mine)
			ShakeCamera(ShakeStrength::Light, 4);
		break;
	case WeaponSkill::ShadowStep:
		vfx::Impact(target, vfx::Element::Chaos, 0.7F);
		vfx::SwingAs(tile, target, vfx::Element::Chaos, WeaponSwingStyle(player), false);
		AddVisualEffect(target, MissileGraphicID::BloodHit, 0, 4);
		AddVisualEffect(target, MissileGraphicID::MetalHit, 1, 4);
		break;
	case WeaponSkill::Frenzy:
		vfx::SwingAs(tile, target, vfx::Element::Blood, WeaponSwingStyle(player), HoldsHeavyWeapon(player));
		vfx::Sparks(target, vfx::Element::Blood, 0.9F);
		AddVisualEffect(target, MissileGraphicID::BloodHit, 0, 4);
		AddVisualEffect(target, MissileGraphicID::MetalHit, 2, 3);
		break;
	case WeaponSkill::Smite:
		vfx::SkyStrike(target, vfx::Element::Holy);
		vfx::Slam(target, 1, vfx::Element::Holy);
		// The blow lands like a falling hammer.
		AddVisualEffect(target, MissileGraphicID::StoneCurseShatter, 0, 6);
		AddVisualEffect(target, MissileGraphicID::HolyBoltExplosion, 1, 6);
		PlaySfxLoc(SfxID::SpellStoneCurse, tile);
		if (mine)
			ShakeCamera(ShakeStrength::Medium, 6);
		break;
	case WeaponSkill::FistsOfThunder:
		vfx::SwingAs(tile, target, vfx::Element::Lightning, WeaponSwingStyle(player), HoldsHeavyWeapon(player));
		vfx::Arc(tile, target, vfx::Element::Lightning);
		AddVisualEffect(target, MissileGraphicID::ThinLightning, 0, 4);
		AddVisualEffect(target, MissileGraphicID::Lightning, 1);
		break;
	case WeaponSkill::RapidFire:
		vfx::Volley(tile, target, 1, true);
		break;
	case WeaponSkill::ShieldBash:
		vfx::Swing(tile, tile + dir, vfx::Element::Physical, false);
		vfx::Impact(tile + dir, vfx::Element::Physical, 0.9F);
		AddVisualEffect(tile + dir, MissileGraphicID::HolyBoltExplosion, 0, 6);
		AddVisualEffect(tile + dir, MissileGraphicID::BoneHit, 1);
		PlaySfxLoc(SfxID::ItemShield, tile);
		if (mine)
			ShakeCamera(ShakeStrength::Medium, 5);
		break;
	case WeaponSkill::Spark:
		vfx::Arc(tile, tile + Displacement { dir } * 2, vfx::Element::Lightning);
		// Lightning crackles from the caster's hands.
		AddVisualEffect(tile + dir, MissileGraphicID::ThinLightning, 0, 8);
		AddVisualEffect(tile + dir, MissileGraphicID::Lightning, 0, 8);
		AddVisualEffect(tile, MissileGraphicID::Lightning, 2);
		break;
	default:
		if (const TableSkillData *table = FindTableSkill(skill); table != nullptr)
			PlayTableSkillEffects(player, *table, target);
		break;
	}
}

int8_t WeaponSkillSkippedFrames(const Player &player)
{
	int8_t skillFrames = 0;
	if (player.poe2.activeSkill == WeaponSkill::Flurry)
		skillFrames = 3;
	else if (IsAnyOf(player.poe2.activeSkill, WeaponSkill::Frenzy, WeaponSkill::FistsOfThunder))
		skillFrames = 2;
	// Each Frenzy charge makes every attack a little faster.
	skillFrames = static_cast<int8_t>(skillFrames + player.poe2.frenzyCharges);
	return static_cast<int8_t>(skillFrames + SupportAttackSkippedFrames(player, player.poe2.activeSkill));
}

int LightningArrowDamage(const Player &player)
{
	return player.getCharacterLevel() * 2 + player._pDexterity / 4;
}

} // namespace devilution
