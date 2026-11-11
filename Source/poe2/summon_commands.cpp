#include "poe2/summon_commands.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <vector>

#include "engine/backbuffer_state.hpp"
#include "engine/path.h"
#include "game_mode.hpp"
#include "levels/gendung.h"
#include "levels/tile_properties.hpp"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/camera_shake.hpp"
#include "poe2/effect_primitives.hpp"
#include "poe2/minion_gems.hpp"
#include "poe2/monster_armour.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/spirit.hpp"
#include "poe2/spirit_buffs.hpp"
#include "poe2/spirit_skills.hpp"
#include "poe2/support_table.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/unique_effects.hpp"
#include "poe2/vfx.hpp"
#include "utils/is_of.hpp"
#include "utils/language.h"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

using SC = SummonCommand;

constexpr std::array<SummonCommand, static_cast<size_t>(SummonCommand::Count) - 1> Commands {
	SC::FlameVolley,
	SC::PoisonArrow,
	SC::FrostShell,
	SC::ThunderCall,
	SC::BoneFrenzy,
	SC::GraveHowl,
	SC::PackRush,
	SC::QuakeStomp,
	SC::BloodGift,
	SC::GolemsBlaze,
	SC::FireSweep,
	SC::Switch,
	SC::LastRites,
};

size_t Index(SummonCommand command)
{
	return static_cast<size_t>(command);
}

bool On(const Player &player, PersistentSkill gem)
{
	return gbIsPoe2 && gem != PersistentSkill::None && IsPersistentSkillActive(player, gem);
}

bool Alive(const Monster &monster)
{
	return !monster.hasNoLife() && monster.mode != MonsterMode::Death;
}

/** @brief An enemy a Command can hit: alive, not anyone's minion, and hittable. */
bool IsEnemy(const Monster &monster)
{
	return Alive(monster) && !monster.isPlayerMinion() && monster.isPossibleToHit();
}

Player &Owner(const Monster &minion)
{
	return Players[static_cast<size_t>(minion.goalVar3)];
}

bool HasOwner(const Monster &minion)
{
	return minion.isPlayerMinion() && minion.goalVar3 >= 0 && static_cast<size_t>(minion.goalVar3) < Players.size();
}

bool IsGolemGem(PersistentSkill gem)
{
	return IsAnyOf(gem, PersistentSkill::SummonGolem, PersistentSkill::BloodGolem, PersistentSkill::FireGolem);
}

/** @brief Whether the monster is one of the gem's minions, standing on this level. */
bool IsCommandMinion(const Player &player, const Monster &monster, PersistentSkill gem)
{
	if (!IsMinionOf(monster, player) || !Alive(monster) || monster.position.tile == GolemHoldingCell)
		return false;
	if (gem == PersistentSkill::Revive)
		return monster.poe2.revived;
	if (IsGolemGem(gem))
		return monster.type().type == MT_GOLEM && ActiveGolemSkill(player) == gem;
	const MinionGemData *data = MinionGemOf(monster);
	return data != nullptr && data->skill == gem;
}

std::vector<Monster *> CommandMinions(const Player &player, PersistentSkill gem)
{
	std::vector<Monster *> minions;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (IsCommandMinion(player, monster, gem))
			minions.push_back(&monster);
	}
	return minions;
}

std::vector<Missile *> Guardians(const Player &player)
{
	std::vector<Missile *> guardians;
	for (Missile &missile : Missiles) {
		if (missile._mitype == MissileID::Guardian && !missile._miDelFlag && missile._misource == static_cast<int>(player.getId()) && missile._micaster == TARGET_MONSTERS)
			guardians.push_back(&missile);
	}
	return guardians;
}

/** @brief The enemy the request names, or the nearest one within 2 tiles of its tile. */
Monster *CommandEnemy(const SummonCommandRequest &request)
{
	if (request.targetMonster >= 0 && static_cast<size_t>(request.targetMonster) < MaxMonsters) {
		Monster &named = Monsters[static_cast<size_t>(request.targetMonster)];
		if (IsEnemy(named))
			return &named;
	}
	Monster *nearest = nullptr;
	int best = 3;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (!IsEnemy(monster))
			continue;
		const int distance = request.target.WalkingDistance(monster.position.tile);
		if (distance < best) {
			best = distance;
			nearest = &monster;
		}
	}
	return nearest;
}

Point CommandAim(const SummonCommandRequest &request)
{
	const Monster *enemy = CommandEnemy(request);
	return enemy != nullptr ? Point { enemy->position.tile } : request.target;
}

bool CanShootAt(const Monster &minion, Point aim)
{
	return minion.position.tile != aim && minion.position.tile.WalkingDistance(aim) <= FlameVolleySightTiles && LineClearMissile(minion.position.tile, aim);
}

int32_t Percent(int64_t raw, int percent)
{
	return static_cast<int32_t>(std::clamp<int64_t>(raw * percent / 100, 0, INT32_MAX / 4));
}

/** @brief The bones of fallen skeletons within Thunder Call's reach of the tile, nearest first, at most its strikes. */
std::vector<Point> BonesNear(const Player &player, Point center)
{
	std::vector<Point> bones;
	for (const CommandBones &fallen : player.poe2.commandState.bones) {
		const Point tile { fallen.x, fallen.y };
		if (fallen.ticks > 0 && center.ExactDistance(tile) <= ThunderCallReach && std::find(bones.begin(), bones.end(), tile) == bones.end())
			bones.push_back(tile);
	}
	std::stable_sort(bones.begin(), bones.end(), [&](Point a, Point b) { return center.ExactDistance(a) < center.ExactDistance(b); });
	if (bones.size() > static_cast<size_t>(ThunderCallMaxStrikes))
		bones.resize(ThunderCallMaxStrikes);
	return bones;
}

bool ShadowBlocked(const Player &player)
{
	const SummonCommandRuntime &state = player.poe2.commandState;
	const Point shadow { state.shadowX, state.shadowY };
	return shadow == player.position.tile || !InDungeonBounds(shadow) || !PosOkPlayer(player, shadow) || !LineClearMissile(player.position.tile, shadow);
}

/** @brief A Guardian fireball's middle, in 1/64 points: Diablo's 1 to 10, plus half its spell level, plus 1. */
int32_t GuardianBoltRaw(int spellLevel)
{
	return ((2 * (std::max<int>(spellLevel, 1) / 2)) + 11) * 32;
}

/** @brief Whether a tile lies in Fire Sweep's arc: within 45 degrees either side of the way to the aim. */
bool InSweepArc(Point from, Point aim, Point tile)
{
	const Displacement way = aim - from;
	const Displacement to = tile - from;
	if (to.deltaX == 0 && to.deltaY == 0)
		return true;
	if (way.deltaX == 0 && way.deltaY == 0)
		return true;
	const int64_t dot = (static_cast<int64_t>(way.deltaX) * to.deltaX) + (static_cast<int64_t>(way.deltaY) * to.deltaY);
	if (dot <= 0)
		return false;
	const int64_t wayLength = (static_cast<int64_t>(way.deltaX) * way.deltaX) + (static_cast<int64_t>(way.deltaY) * way.deltaY);
	const int64_t toLength = (static_cast<int64_t>(to.deltaX) * to.deltaX) + (static_cast<int64_t>(to.deltaY) * to.deltaY);
	// cos(angle) >= cos(45 degrees): dot^2 >= |way|^2 |to|^2 / 2.
	return dot * dot * 2 >= wayLength * toLength;
}

/**
 * @brief Hits an enemy for a Command, in the hero's name, by the minions' hit rules: its damage taken and, for a
 * physical hit, its armour (as damage alongside a hit: Brutality's roll is the hero's own skill's, not a Command's).
 */
bool CommandHit(Player &player, Monster &monster, DamageType type, int32_t raw, std::string_view source)
{
	if (raw <= 0 || !IsEnemy(monster))
		return false;
	TelemetryNoteDamageSource(source);
	const bool lives = SpiritHitMonster(player, monster, type, Fixed26_6::fromRaw(raw), 100, ArmourHit::Extra);
	TelemetryNoteDamageSource({});
	return lives;
}

void AddGround(SummonCommandRuntime &state, const CommandGround &ground)
{
	auto slot = std::find_if(state.grounds.begin(), state.grounds.end(), [](const CommandGround &g) { return g.command == 0 || g.ticks == 0; });
	if (slot == state.grounds.end())
		slot = std::min_element(state.grounds.begin(), state.grounds.end(), [](const CommandGround &a, const CommandGround &b) { return a.ticks < b.ticks; });
	*slot = ground;
}

void SetPending(SummonCommandRuntime &state, SummonCommand command, Point tile, int32_t raw)
{
	CommandPending &pending = state.pending[Index(command)];
	pending.ticks = static_cast<uint8_t>(CommandFallbackTicks);
	pending.x = static_cast<uint8_t>(tile.x);
	pending.y = static_cast<uint8_t>(tile.y);
	pending.raw = raw;
}

Monster *CommandGolem(const Player &player, PersistentSkill gem)
{
	Monster *golem = FindGolemForPlayer(player);
	if (golem == nullptr || !IsCommandMinion(player, *golem, gem))
		return nullptr;
	return golem;
}

bool GolemTooHurt(const Monster &golem)
{
	return static_cast<int64_t>(golem.hitPoints.raw()) * 100 < static_cast<int64_t>(golem.maxHitPoints.raw()) * BloodGiftMinLifePercent;
}

// The Commands' effects. Each plays where it goes off, weighty enough to read in a fight.

void ApplyFrostShell(Player &player, Point center)
{
	vfx::Nova(center, FrostShellRadius, vfx::Element::Cold);
	ShakeCamera(ShakeStrength::Light, 4);
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (!IsMinionOf(monster, player) || !Alive(monster) || monster.position.tile == GolemHoldingCell || center.ExactDistance(monster.position.tile) > FrostShellRadius)
			continue;
		monster.poe2.commandShellTicks = static_cast<uint8_t>(FrostShellTicks);
		vfx::Impact(monster.position.tile, vfx::Element::Cold, 0.9F);
		AddVisualEffect(monster.position.tile, MissileGraphicID::BlueFlareExplosion);
	}
}

void ThunderStrikes(Player &player, Point center, int32_t raw)
{
	const std::vector<Point> bones = BonesNear(player, center);
	if (bones.empty())
		return;
	ShakeCamera(ShakeStrength::Medium, 6);
	for (const Point tile : bones) {
		vfx::SkyStrike(tile, vfx::Element::Lightning);
		vfx::Impact(tile, vfx::Element::Lightning, 1.1F);
		AddVisualEffect(tile, MissileGraphicID::LightningArrow);
		for (size_t i = 0; i < ActiveMonsterCount; i++) {
			Monster &monster = Monsters[ActiveMonsters[i]];
			if (IsEnemy(monster) && tile.WalkingDistance(monster.position.tile) <= ThunderCallRadius)
				CommandHit(player, monster, DamageType::Lightning, Percent(raw, ThunderCallPercent), "Thunder Call");
		}
		AddGround(player.poe2.commandState, { static_cast<uint8_t>(SC::ThunderCall), static_cast<uint8_t>(tile.x), static_cast<uint8_t>(tile.y), static_cast<uint8_t>(ThunderCallRadius), static_cast<uint16_t>(ShockedGroundTicks), 0 });
	}
}

void QuakeStomp(Player &player, Point center, int32_t raw)
{
	vfx::Slam(center, QuakeStompRadius, vfx::Element::Physical);
	for (const Direction dir : { Direction::North, Direction::NorthEast, Direction::East, Direction::SouthEast, Direction::South, Direction::SouthWest, Direction::West, Direction::NorthWest })
		vfx::Fissure(center, center + Displacement { dir } * QuakeStompRadius, vfx::Element::Physical);
	ShakeCamera(ShakeStrength::Heavy, 8);
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (!IsEnemy(monster) || center.ExactDistance(monster.position.tile) > QuakeStompRadius)
			continue;
		if (CommandHit(player, monster, DamageType::Physical, raw, "Quake Stomp"))
			CommandStagger(monster);
	}
}

void BloodGift(Player &player, Monster &golem)
{
	if (GolemTooHurt(golem) || player.hasNoLife())
		return;
	const int32_t given = Percent(golem.maxHitPoints.raw(), BloodGiftLifePercent);
	const int32_t heal = std::min<int32_t>(given, Percent(player._pMaxHP.raw(), BloodGiftHealCapPercent));
	golem.hitPoints = Fixed26_6::fromRaw(std::max<int32_t>(golem.hitPoints.raw() - given, 64));
	player._pHitPoints = std::min<Fixed26_6>(player._pHitPoints + Fixed26_6::fromRaw(heal), player._pMaxHP);
	player._pHPBase = std::min<Fixed26_6>(player._pHPBase + Fixed26_6::fromRaw(heal), player._pMaxHPBase);
	RedrawComponent(PanelDrawComponent::Health);
	vfx::Impact(golem.position.tile, vfx::Element::Blood, 1.1F);
	vfx::Arc(golem.position.tile, player.position.tile, vfx::Element::Blood);
	vfx::Impact(player.position.tile, vfx::Element::Blood, 0.7F);
	AddVisualEffect(golem.position.tile, MissileGraphicID::BloodHit);
	ShakeCamera(ShakeStrength::Light, 3);
}

void StartBlaze(Player &player, const Monster &golem, int32_t raw)
{
	SummonCommandRuntime &state = player.poe2.commandState;
	state.blazeTicks = static_cast<uint8_t>(GolemsBlazeTicks);
	state.blazeRaw = raw;
	vfx::Nova(golem.position.tile, GolemsBlazeRadius, vfx::Element::Fire);
	vfx::Impact(golem.position.tile, vfx::Element::Fire, 1.3F);
	ShakeCamera(ShakeStrength::Light, 4);
}

/**
 * @brief One of the Burning Dead howls: every monster near it is shaken and hits softer, by the sliding scale. Archers
 * and casters too, even one a hit can't reach just now (a counselor fading out): only the interrupt needs a hittable one.
 */
void GraveHowl(Player &player, const Monster &brute)
{
	const Point center = brute.position.tile;
	vfx::Nova(center, GraveHowlRadius, vfx::Element::Fire);
	ShakeCamera(ShakeStrength::Medium, 6);
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (!Alive(monster) || monster.isPlayerMinion() || monster.talkMsg != TEXT_NONE || center.ExactDistance(monster.position.tile) > GraveHowlRadius)
			continue;
		const CommandTierScale &scale = CommandTierScales[static_cast<size_t>(CommandTierOf(monster))];
		Poe2MonsterState &state = monster.poe2;
		state.commandWeakenPercent = std::max<uint8_t>(state.commandWeakenTicks > 0 ? state.commandWeakenPercent : uint8_t { 0 }, scale.weakenPercent);
		state.commandWeakenTicks = static_cast<uint8_t>(GraveHowlTicks);
		if (scale.interrupts && monster.isPossibleToHit())
			M_StartStagger(monster);
		monster.tag(player);
		vfx::Sparks(monster.position.tile, vfx::Element::Physical, 0.6F);
	}
}

/** @brief A wolf that leapt bites its target. */
void PackBite(Player &player, Monster &wolf)
{
	if ((wolf.flags & MFLAG_TARGETS_MONSTER) == 0 || (wolf.flags & MFLAG_NO_ENEMY) != 0)
		return;
	Monster &target = Monsters[wolf.enemy];
	if (!IsEnemy(target) || wolf.position.tile.WalkingDistance(target.position.tile) > 2)
		return;
	vfx::Cut(target.position.tile, vfx::Element::Physical);
	vfx::Impact(target.position.tile, vfx::Element::Blood, 0.8F);
	CommandHit(player, target, DamageType::Physical, Percent(MinionBlowRaw(wolf), PackRushPercent), "Pack Rush");
}

void RunPending(Player &player, SummonCommand command, Monster *minion)
{
	SummonCommandRuntime &state = player.poe2.commandState;
	CommandPending &pending = state.pending[Index(command)];
	if (pending.ticks == 0)
		return;
	pending.ticks = 0;
	const Point tile { pending.x, pending.y };
	const PersistentSkill gem = CommandGem(command);
	switch (command) {
	case SC::FrostShell:
		ApplyFrostShell(player, tile);
		break;
	case SC::ThunderCall:
		ThunderStrikes(player, tile, pending.raw);
		break;
	case SC::QuakeStomp: {
		Monster *golem = minion != nullptr ? minion : CommandGolem(player, gem);
		QuakeStomp(player, golem != nullptr ? Point { golem->position.tile } : tile, pending.raw);
		break;
	}
	case SC::BloodGift:
		if (Monster *golem = minion != nullptr ? minion : CommandGolem(player, gem); golem != nullptr)
			BloodGift(player, *golem);
		break;
	case SC::GolemsBlaze:
		if (Monster *golem = minion != nullptr ? minion : CommandGolem(player, gem); golem != nullptr)
			StartBlaze(player, *golem, pending.raw);
		break;
	default:
		break;
	}
}

void BurnAroundGolem(Player &player)
{
	SummonCommandRuntime &state = player.poe2.commandState;
	if (state.blazeTicks == 0)
		return;
	const Monster *golem = CommandGolem(player, PersistentSkill::FireGolem);
	if (golem == nullptr) {
		state.blazeTicks = 0;
		return;
	}
	const Point center = golem->position.tile;
	if (state.blazeTicks % 4 == 0) {
		const Direction dir = static_cast<Direction>((state.blazeTicks / 4) % 8);
		AddVisualEffect(center + Displacement { dir } * (1 + ((state.blazeTicks / 4) % 2)), MissileGraphicID::Inferno);
		vfx::Sparks(center, vfx::Element::Fire, 0.6F);
	}
	// Once a second it sets everything near it alight: the ignite's slot, the strongest burn only.
	if (state.blazeTicks % 20 == 0) {
		const int ticks = std::min<int>(20, state.blazeTicks);
		for (size_t i = 0; i < ActiveMonsterCount; i++) {
			Monster &monster = Monsters[ActiveMonsters[i]];
			if (!IsEnemy(monster) || center.ExactDistance(monster.position.tile) > GolemsBlazeRadius)
				continue;
			const int32_t perTick = std::max<int32_t>(Percent(state.blazeRaw, MonsterDamageTakenPercent(monster, DamageType::Fire)) / 20, 1);
			Poe2MonsterState &burn = monster.poe2;
			if (burn.igniteTicks == 0 || perTick >= burn.igniteDamagePerTick) {
				burn.igniteDamagePerTick = perTick;
				burn.igniteSource = static_cast<int8_t>(player.getId());
				burn.igniteTicks = static_cast<uint8_t>(std::max<int>(burn.igniteTicks, ticks));
			}
			monster.tag(player);
		}
	}
	state.blazeTicks--;
}

void TickGrounds(Player &player)
{
	SummonCommandRuntime &state = player.poe2.commandState;
	for (CommandGround &ground : state.grounds) {
		if (ground.command == 0 || ground.ticks == 0)
			continue;
		const Point center { ground.x, ground.y };
		const auto command = static_cast<SummonCommand>(ground.command);
		for (size_t i = 0; i < ActiveMonsterCount; i++) {
			Monster &monster = Monsters[ActiveMonsters[i]];
			if (!IsEnemy(monster) || center.ExactDistance(monster.position.tile) > ground.radius)
				continue;
			if (command == SC::ThunderCall) {
				// Standing on it shocks: 20% more damage taken, POE2's shock.
				monster.poe2.shockTicks = std::max<uint8_t>(monster.poe2.shockTicks, 4);
			} else if (command == SC::PoisonArrow && ground.ticks % 20 == 0) {
				const int32_t total = Percent(ground.hitRaw, PoisonCloudPercentPerSecond * PoisonCloudPoisonTicks / 20);
				InflictPoison(player, monster, total, PoisonCloudPoisonTicks);
				monster.tag(player);
			}
		}
		// The cloud's green murk rolls over its tiles, the shocked ground crackles.
		if (ground.ticks % 5 == 0) {
			const Direction dir = static_cast<Direction>((ground.ticks / 5) % 8);
			const Point tile = center + Displacement { dir } * (1 + ((ground.ticks / 5) % std::max<int>(ground.radius, 1)));
			if (command == SC::PoisonArrow) {
				AddVisualEffect(tile, MissileGraphicID::AcidSplat);
				vfx::Sparks(tile, vfx::Element::Chaos, 0.4F);
			} else {
				vfx::Sparks(ground.ticks % 10 == 0 ? center : tile, vfx::Element::Lightning, 0.5F);
			}
		}
		if (--ground.ticks == 0)
			ground = {};
	}
}

} // namespace

CommandTier CommandTierOf(const Monster &monster)
{
	if (monster.isUnique())
		return CommandTier::Boss;
	switch (GetMonsterAffixes(monster).rarity) {
	case MonsterRarity::Rare:
		return CommandTier::Rare;
	case MonsterRarity::Magic:
		return CommandTier::Magic;
	default:
		return CommandTier::Normal;
	}
}

SummonCommand CommandForGem(PersistentSkill gem)
{
	switch (gem) {
	case PersistentSkill::SkeletalMages:
		return SC::FlameVolley;
	case PersistentSkill::SkeletalSnipers:
		return SC::PoisonArrow;
	case PersistentSkill::SkeletalFrostMages:
		return SC::FrostShell;
	case PersistentSkill::SkeletalStormMages:
		return SC::ThunderCall;
	case PersistentSkill::SkeletalReavers:
		return SC::BoneFrenzy;
	case PersistentSkill::SkeletalBrutes:
		return SC::GraveHowl;
	case PersistentSkill::WolfPack:
		return SC::PackRush;
	case PersistentSkill::SummonGolem:
		return SC::QuakeStomp;
	case PersistentSkill::BloodGolem:
		return SC::BloodGift;
	case PersistentSkill::FireGolem:
		return SC::GolemsBlaze;
	case PersistentSkill::Guardian:
		return SC::FireSweep;
	case PersistentSkill::ShadowWarrior:
		return SC::Switch;
	case PersistentSkill::Revive:
		return SC::LastRites;
	default:
		return SC::None;
	}
}

PersistentSkill CommandGem(SummonCommand command)
{
	switch (command) {
	case SC::FlameVolley:
		return PersistentSkill::SkeletalMages;
	case SC::PoisonArrow:
		return PersistentSkill::SkeletalSnipers;
	case SC::FrostShell:
		return PersistentSkill::SkeletalFrostMages;
	case SC::ThunderCall:
		return PersistentSkill::SkeletalStormMages;
	case SC::BoneFrenzy:
		return PersistentSkill::SkeletalReavers;
	case SC::GraveHowl:
		return PersistentSkill::SkeletalBrutes;
	case SC::PackRush:
		return PersistentSkill::WolfPack;
	case SC::QuakeStomp:
		return PersistentSkill::SummonGolem;
	case SC::BloodGift:
		return PersistentSkill::BloodGolem;
	case SC::GolemsBlaze:
		return PersistentSkill::FireGolem;
	case SC::FireSweep:
		return PersistentSkill::Guardian;
	case SC::Switch:
		return PersistentSkill::ShadowWarrior;
	case SC::LastRites:
		return PersistentSkill::Revive;
	default:
		return PersistentSkill::None;
	}
}

std::span<const SummonCommand> AllSummonCommands()
{
	return Commands;
}

std::string_view SummonCommandName(SummonCommand command)
{
	switch (command) {
	case SC::FlameVolley:
		return _("Flame Volley");
	case SC::PoisonArrow:
		return _("Poison Arrow");
	case SC::FrostShell:
		return _("Frost Shell");
	case SC::ThunderCall:
		return _("Thunder Call");
	case SC::BoneFrenzy:
		return _("Bone Frenzy");
	case SC::GraveHowl:
		return _("Grave Howl");
	case SC::PackRush:
		return _("Pack Rush");
	case SC::QuakeStomp:
		return _("Quake Stomp");
	case SC::BloodGift:
		return _("Blood Gift");
	case SC::GolemsBlaze:
		return _("Golem's Blaze");
	case SC::FireSweep:
		return _("Fire Sweep");
	case SC::Switch:
		return _("Switch");
	case SC::LastRites:
		return _("Last Rites");
	default:
		return {};
	}
}

std::string_view SummonCommandDescription(SummonCommand command)
{
	switch (command) {
	case SC::FlameVolley:
		return _("Every mage fires at the target");
	case SC::PoisonArrow:
		return _("An arrow that bursts into a poison cloud");
	case SC::FrostShell:
		return _("Ice wraps your minions, soaking damage");
	case SC::ThunderCall:
		return _("Lightning strikes your fallen skeletons' bones");
	case SC::BoneFrenzy:
		return _("Captains hit harder and faster for 4 seconds");
	case SC::GraveHowl:
		return _("Nearby enemies are shaken and hit softer");
	case SC::PackRush:
		return _("Every wolf leaps at the target");
	case SC::QuakeStomp:
		return _("The golem stomps, staggering all around it");
	case SC::BloodGift:
		return _("The golem gives you part of its life");
	case SC::GolemsBlaze:
		return _("The golem flares up, burning all near it");
	case SC::FireSweep:
		return _("The Guardian breathes a wide arc of fire");
	case SC::Switch:
		return _("Swap places with your shadow");
	case SC::LastRites:
		return _("Revived monsters rush the target, hitting harder");
	default:
		return {};
	}
}

int SummonCommandCooldownTicks(SummonCommand command)
{
	// POE2's own Command cooldowns (PathOfBuilding-PoE2's minion.lua), by the closest Command.
	switch (command) {
	case SC::FlameVolley:
	case SC::ThunderCall:
	case SC::Switch:
		return 5 * 20;
	case SC::PoisonArrow:
	case SC::PackRush:
	case SC::FireSweep:
		return 6 * 20;
	case SC::GraveHowl:
	case SC::QuakeStomp:
	case SC::LastRites:
	case SC::BoneFrenzy:
		return 8 * 20;
	case SC::BloodGift:
	case SC::GolemsBlaze:
		return 10 * 20;
	case SC::FrostShell:
		return 12 * 20;
	default:
		return 0;
	}
}

uint32_t SummonCommandTags(SummonCommand command)
{
	uint32_t tags = ST_Minion | ST_Command;
	switch (command) {
	case SC::FlameVolley:
	case SC::GolemsBlaze:
	case SC::FireSweep:
		tags |= ST_Fire | ST_Damage;
		break;
	case SC::PoisonArrow:
		tags |= ST_Chaos | ST_Damage | ST_Area | ST_Duration;
		break;
	case SC::FrostShell:
		tags |= ST_Frost | ST_Area | ST_Duration;
		break;
	case SC::ThunderCall:
		tags |= ST_Lightning | ST_Damage | ST_Area;
		break;
	case SC::PackRush:
	case SC::QuakeStomp:
	case SC::LastRites:
		tags |= ST_Physical;
		if (command != SC::LastRites)
			tags |= ST_Damage;
		break;
	default:
		break;
	}
	if (command == SC::GolemsBlaze)
		tags |= ST_Area | ST_Duration | ST_DamageOverTime;
	if (command == SC::QuakeStomp || command == SC::FireSweep)
		tags |= ST_Area;
	if (IsAnyOf(command, SC::BoneFrenzy, SC::GraveHowl, SC::LastRites))
		tags |= ST_Duration;
	if (command == SC::GraveHowl)
		tags |= ST_Area;
	return tags;
}

std::string SummonCommandTagText(SummonCommand command)
{
	const uint32_t tags = SummonCommandTags(command);
	std::string text = "Minion, Command";
	for (const auto &[tag, name] : { std::pair { ST_Fire, "Fire" }, std::pair { ST_Lightning, "Lightning" }, std::pair { ST_Frost, "Cold" }, std::pair { ST_Physical, "Physical" }, std::pair { ST_Chaos, "Chaos" } }) {
		if ((tags & tag) != 0)
			StrAppend(text, ", ", name);
	}
	return text;
}

SummonCommand SocketCommand(const Player &player, size_t socket)
{
	if (!gbIsPoe2 || socket >= NumSkillSlots)
		return SC::None;
	return CommandForGem(player.poe2.skillSlots[socket].persistent());
}

std::vector<size_t> CommandSockets(const Player &player)
{
	std::vector<size_t> sockets;
	for (size_t socket = 0; socket < NumSkillSlots; socket++) {
		if (SocketCommand(player, socket) != SC::None)
			sockets.push_back(socket);
	}
	return sockets;
}

CommandResult CheckSummonCommand(const Player &player, const SummonCommandRequest &request)
{
	const SummonCommand command = CommandForGem(request.gem);
	if (command == SC::None || !gbIsPoe2)
		return CommandResult::NoCommand;
	if (!On(player, request.gem))
		return CommandResult::Off;
	if (leveltype == DTYPE_TOWN)
		return CommandResult::Town;
	if (player.poe2.commandState.cooldowns[Index(command)] > 0)
		return CommandResult::Cooldown;
	switch (command) {
	case SC::FireSweep:
		return Guardians(player).empty() ? CommandResult::NoMinions : CommandResult::Used;
	case SC::Switch:
		if (player.poe2.commandState.shadowTicks == 0)
			return CommandResult::NoMinions;
		return ShadowBlocked(player) ? CommandResult::Blocked : CommandResult::Used;
	default:
		break;
	}
	const std::vector<Monster *> minions = CommandMinions(player, request.gem);
	if (minions.empty())
		return CommandResult::NoMinions;
	switch (command) {
	case SC::FlameVolley:
	case SC::PoisonArrow: {
		const Point aim = CommandAim(request);
		return std::any_of(minions.begin(), minions.end(), [&](const Monster *minion) { return CanShootAt(*minion, aim); }) ? CommandResult::Used : CommandResult::NoSight;
	}
	case SC::ThunderCall:
		return BonesNear(player, request.target).empty() ? CommandResult::NoBones : CommandResult::Used;
	case SC::PackRush: {
		const Monster *enemy = CommandEnemy(request);
		if (enemy == nullptr)
			return CommandResult::NoTarget;
		return std::any_of(minions.begin(), minions.end(), [&](const Monster *wolf) { return wolf->position.tile.WalkingDistance(enemy->position.tile) <= PackRushReach; }) ? CommandResult::Used : CommandResult::NoSight;
	}
	case SC::BloodGift:
		return GolemTooHurt(*minions.front()) ? CommandResult::GolemHurt : CommandResult::Used;
	default:
		return CommandResult::Used;
	}
}

CommandResult IssueSummonCommand(Player &player, const SummonCommandRequest &request)
{
	// The minions are the local hero's to command, like the rest of the Spirit gems.
	if (&player != MyPlayer)
		return CommandResult::NoCommand;
	const CommandResult check = CheckSummonCommand(player, request);
	if (check != CommandResult::Used)
		return check;
	const SummonCommand command = CommandForGem(request.gem);
	SummonCommandRuntime &state = player.poe2.commandState;
	const auto commandId = static_cast<uint8_t>(command);
	std::vector<Monster *> minions = CommandMinions(player, request.gem);
	Monster *enemy = CommandEnemy(request);
	const Point aim = enemy != nullptr ? Point { enemy->position.tile } : request.target;

	switch (command) {
	case SC::FlameVolley: {
		// Every mage with a clear line looses one bolt at once, on its own cast's frame.
		int fired = 0;
		for (Monster *mage : minions) {
			if (!CanShootAt(*mage, aim) || !StartCommandShot(*mage, aim, commandId, FlameVolleyPercent))
				continue;
			vfx::Sparks(mage->position.tile, vfx::Element::Fire, 0.8F);
			fired++;
		}
		if (fired > 0)
			vfx::Sigil(aim, 1, 500, vfx::Element::Fire);
		break;
	}
	case SC::PoisonArrow: {
		// The archer nearest the target with a clear line.
		Monster *archer = nullptr;
		for (Monster *candidate : minions) {
			if (CanShootAt(*candidate, aim) && (archer == nullptr || candidate->position.tile.WalkingDistance(aim) < archer->position.tile.WalkingDistance(aim)))
				archer = candidate;
		}
		if (archer != nullptr && StartCommandShot(*archer, aim, commandId, PoisonArrowPercent))
			vfx::Sparks(archer->position.tile, vfx::Element::Chaos, 0.7F);
		break;
	}
	case SC::FrostShell: {
		// "The target spot or minion": the tile, whoever stands there.
		const Point center = request.target;
		for (Monster *lich : minions) {
			if (StartCommandMove(*lich, center, commandId, true))
				vfx::Sparks(lich->position.tile, vfx::Element::Cold, 0.7F);
		}
		state.shellRetaliateRaw = Percent(static_cast<int64_t>(MinionShotAverage(*minions.front(), FindMinionGem(request.gem)->shotPercent)) * 64, FrostShellRetaliatePercent);
		SetPending(state, command, center, 0);
		break;
	}
	case SC::ThunderCall: {
		const int32_t bolt = static_cast<int32_t>(MinionShotAverage(*minions.front(), FindMinionGem(request.gem)->shotPercent)) * 64;
		for (Monster *lich : minions) {
			if (StartCommandMove(*lich, request.target, commandId, true))
				vfx::Sparks(lich->position.tile, vfx::Element::Lightning, 0.7F);
		}
		SetPending(state, command, request.target, bolt);
		break;
	}
	case SC::BoneFrenzy:
		ShakeCamera(ShakeStrength::Light, 3);
		for (Monster *captain : minions) {
			captain->poe2.commandFrenzyTicks = static_cast<uint8_t>(BoneFrenzyTicks);
			vfx::Impact(captain->position.tile, vfx::Element::Blood, 1.0F);
			AddVisualEffect(captain->position.tile, MissileGraphicID::BloodHit);
		}
		break;
	case SC::GraveHowl:
		// Each of the Burning Dead throws its head back and howls; the howl goes off on its swing's frame.
		for (Monster *brute : minions) {
			if (!StartCommandMove(*brute, brute->position.tile + Displacement { brute->direction }, commandId, false))
				GraveHowl(player, *brute);
		}
		break;
	case SC::PackRush: {
		ShakeCamera(ShakeStrength::Light, 3);
		for (Monster *wolf : minions) {
			if (wolf->position.tile.WalkingDistance(enemy->position.tile) > PackRushReach || IsAnyOf(wolf->mode, MonsterMode::Death, MonsterMode::Petrified))
				continue;
			const Point from = wolf->position.tile;
			std::optional<Point> landing = from.WalkingDistance(enemy->position.tile) <= 1 ? std::optional<Point> { from } : std::nullopt;
			if (!landing) {
				landing = FindClosestValidPosition([](Point tile) { return InDungeonBounds(tile) && IsTileWalkable(tile) && !IsTileOccupied(tile); }, enemy->position.tile, 1, 2);
			}
			if (!landing)
				continue;
			if (*landing != from) {
				// A low, fast leap: dust where it springs, a streak along the way, a thud where it lands.
				vfx::Impact(from, vfx::Element::Physical, 0.6F);
				vfx::Swing(from, *landing, vfx::Element::Physical, false, true);
				LeapMinionTo(*wolf, *landing);
				vfx::Impact(*landing, vfx::Element::Physical, 0.8F);
			}
			wolf->enemy = static_cast<int>(enemy->getId());
			wolf->flags |= MFLAG_TARGETS_MONSTER;
			wolf->flags &= ~MFLAG_NO_ENEMY;
			wolf->direction = GetDirection(wolf->position.tile, enemy->position.tile);
			if (!StartCommandMove(*wolf, enemy->position.tile, commandId, false))
				PackBite(player, *wolf);
		}
		break;
	}
	case SC::QuakeStomp:
	case SC::BloodGift:
	case SC::GolemsBlaze: {
		Monster &golem = *minions.front();
		int32_t raw = 0;
		if (command == SC::QuakeStomp)
			raw = Percent(MinionBlowRaw(golem), QuakeStompPercent);
		else if (command == SC::GolemsBlaze)
			raw = Percent(MinionBlowRaw(golem), GolemsBlazePercentPerSecond);
		// The golem winds up its blow: the stomp lands, the blood sprays, the fire flares on its frame.
		const Point toward = command == SC::BloodGift ? player.position.tile : golem.position.tile + Displacement { golem.direction };
		SetPending(state, command, golem.position.tile, raw);
		if (!StartCommandMove(golem, toward, commandId, false))
			RunPending(player, command, &golem);
		break;
	}
	case SC::FireSweep:
		for (Missile *guardian : Guardians(player)) {
			const Point from = guardian->position.tile;
			const Point toward = aim != from ? aim : from + Displacement { player._pdir };
			// Its heads turn as it breathes.
			guardian->setFrameGroup<GuardianFrame>(GuardianFrame::Attack);
			guardian->var2 = 6;
			vfx::Cleave(from, toward, vfx::Element::Fire, false);
			const Direction way = GetDirection(from, toward);
			for (const Direction ray : { Left(way), way, Right(way) }) {
				for (int reach = 2; reach <= FireSweepReach; reach += 2)
					AddVisualEffect(from + Displacement { ray } * reach, MissileGraphicID::FireArrowExplosion, reach / 2);
			}
			const int32_t raw = Percent(GuardianBoltRaw(guardian->_mispllvl), FireSweepPercent);
			for (size_t i = 0; i < ActiveMonsterCount; i++) {
				Monster &monster = Monsters[ActiveMonsters[i]];
				if (!IsEnemy(monster) || from.ExactDistance(monster.position.tile) > FireSweepReach || !InSweepArc(from, toward, monster.position.tile))
					continue;
				vfx::Impact(monster.position.tile, vfx::Element::Fire, 1.0F);
				CommandHit(player, monster, DamageType::Fire, raw, "Fire Sweep");
			}
		}
		ShakeCamera(ShakeStrength::Medium, 5);
		break;
	case SC::Switch: {
		const Point from = player.position.tile;
		const Point to { state.shadowX, state.shadowY };
		AddVisualEffect(from, MissileGraphicID::Etherealize);
		AddVisualEffect(to, MissileGraphicID::Etherealize);
		vfx::Arc(from, to, vfx::Element::Arcane);
		ClrPlrPath(player);
		MovePlayerTo(player, to);
		StartStand(player, player._pdir);
		vfx::Impact(to, vfx::Element::Arcane, 0.8F);
		// The shadow takes the hero's old place, so the next Switch goes back.
		state.shadowX = static_cast<uint8_t>(from.x);
		state.shadowY = static_cast<uint8_t>(from.y);
		state.shadowTicks = static_cast<uint8_t>(ShadowStandTicks);
		break;
	}
	case SC::LastRites:
		ShakeCamera(ShakeStrength::Light, 4);
		for (Monster *revived : minions) {
			revived->poe2.commandRitesTicks = static_cast<uint8_t>(LastRitesTicks);
			vfx::Impact(revived->position.tile, vfx::Element::Blood, 0.9F);
			AddVisualEffect(revived->position.tile, MissileGraphicID::Resurrect);
			if (enemy != nullptr) {
				revived->enemy = static_cast<int>(enemy->getId());
				revived->flags |= MFLAG_TARGETS_MONSTER;
				revived->flags &= ~MFLAG_NO_ENEMY;
				revived->poe2.minionReturning = false;
				vfx::Arc(revived->position.tile, enemy->position.tile, vfx::Element::Blood);
			}
		}
		break;
	default:
		break;
	}
	state.cooldowns[Index(command)] = static_cast<uint16_t>(CooldownTicks(player, SummonCommandCooldownTicks(command)));
	TelemetryLog("command", SummonCommandName(command));
	return CommandResult::Used;
}

int SummonCommandCooldownLeft(const Player &player, SummonCommand command)
{
	return command == SC::None ? 0 : player.poe2.commandState.cooldowns[Index(command)];
}

std::string_view CommandResultText(CommandResult result)
{
	switch (result) {
	case CommandResult::NoCommand:
		return _("No Command");
	case CommandResult::Off:
		return _("Its gem is switched off");
	case CommandResult::Town:
		return _("Not in town");
	case CommandResult::Cooldown:
		return _("Not ready yet");
	case CommandResult::NoMinions:
		return _("No minions to command");
	case CommandResult::NoTarget:
		return _("No enemy there");
	case CommandResult::NoSight:
		return _("None of them can reach the target");
	case CommandResult::NoBones:
		return _("No fallen skeletons near the target");
	case CommandResult::GolemHurt:
		return _("The golem is too hurt");
	case CommandResult::Blocked:
		return _("Can't switch there");
	default:
		return {};
	}
}

void RunCommandAction(Monster &minion)
{
	const auto command = static_cast<SummonCommand>(minion.poe2.commandAction);
	minion.poe2.commandAction = 0;
	if (!gbIsPoe2 || !HasOwner(minion) || command == SC::None || command >= SC::Count)
		return;
	Player &owner = Owner(minion);
	if (&owner != MyPlayer)
		return;
	const Point aim { minion.poe2.commandAimX, minion.poe2.commandAimY };
	switch (command) {
	case SC::FrostShell:
		vfx::Arc(minion.position.tile, aim, vfx::Element::Cold);
		RunPending(owner, command, &minion);
		break;
	case SC::ThunderCall:
		vfx::Arc(minion.position.tile, aim, vfx::Element::Lightning);
		RunPending(owner, command, &minion);
		break;
	case SC::QuakeStomp:
	case SC::BloodGift:
	case SC::GolemsBlaze:
		RunPending(owner, command, &minion);
		break;
	case SC::GraveHowl:
		GraveHowl(owner, minion);
		break;
	case SC::PackRush:
		PackBite(owner, minion);
		break;
	default:
		break;
	}
}

void OnCommandShotFired(const Monster &minion, uint8_t command, Point aim, Missile &shot)
{
	if (static_cast<SummonCommand>(command) != SC::PoisonArrow)
		return;
	shot.poe2.commandShot = command;
	shot.poe2.commandX = static_cast<uint8_t>(aim.x);
	shot.poe2.commandY = static_cast<uint8_t>(aim.y);
	// A green glint on the arrow's way out.
	vfx::Sparks(minion.position.tile, vfx::Element::Chaos, 0.5F);
}

void OnCommandShotLanded(const Missile &shot)
{
	if (static_cast<SummonCommand>(shot.poe2.commandShot) != SC::PoisonArrow || shot._misource < 0 || static_cast<size_t>(shot._misource) >= Players.size())
		return;
	Player &owner = Players[static_cast<size_t>(shot._misource)];
	if (&owner != MyPlayer)
		return;
	const Point tile = shot.position.tile;
	vfx::Nova(tile, PoisonCloudRadius, vfx::Element::Chaos);
	vfx::Impact(tile, vfx::Element::Chaos, 1.2F);
	for (const Direction dir : { Direction::North, Direction::East, Direction::South, Direction::West })
		AddVisualEffect(tile + Displacement { dir }, MissileGraphicID::AcidSplat);
	AddVisualEffect(tile, MissileGraphicID::AcidSplat);
	ShakeCamera(ShakeStrength::Light, 4);
	AddGround(owner.poe2.commandState, { static_cast<uint8_t>(SC::PoisonArrow), static_cast<uint8_t>(tile.x), static_cast<uint8_t>(tile.y), static_cast<uint8_t>(PoisonCloudRadius), static_cast<uint16_t>(PoisonCloudTicks), std::max<int32_t>(shot._midam, 1) * 64 });
}

int CommandMinionDamagePercent(const Monster &minion)
{
	int percent = 100;
	if (minion.poe2.commandFrenzyTicks > 0)
		percent = percent * (100 + BoneFrenzyDamagePercent) / 100;
	if (minion.poe2.commandRitesTicks > 0)
		percent = percent * (100 + LastRitesDamagePercent) / 100;
	return percent;
}

int CommandWeakenedDamagePercent(const Monster &monster)
{
	return monster.poe2.commandWeakenTicks > 0 ? 100 - monster.poe2.commandWeakenPercent : 100;
}

int CommandShellDamagePercent(const Monster &minion)
{
	return minion.poe2.commandShellTicks > 0 ? 100 - FrostShellLessPercent : 100;
}

void OnShelledMinionHit(Monster &attacker, const Monster &minion)
{
	if (!gbIsPoe2 || minion.poe2.commandShellTicks == 0 || !HasOwner(minion) || !IsEnemy(attacker))
		return;
	Player &owner = Owner(minion);
	if (&owner != MyPlayer)
		return;
	const int32_t raw = owner.poe2.commandState.shellRetaliateRaw;
	if (raw <= 0)
		return;
	vfx::Impact(attacker.position.tile, vfx::Element::Cold, 0.7F);
	CommandHit(owner, attacker, DamageType::Cold, raw, "Frost Shell");
}

bool CommandExtraTick(Monster &monster)
{
	if (!gbIsPoe2 || !monster.isPlayerMinion() || monster.hasNoLife())
		return false;
	int haste = 0;
	if (monster.poe2.commandFrenzyTicks > 0 && monster.mode == MonsterMode::MeleeAttack)
		haste += BoneFrenzySpeedPercent;
	if (monster.poe2.commandRitesTicks > 0 && IsMonsterModeMove(monster.mode))
		haste += LastRitesSpeedPercent;
	if (haste == 0)
		return false;
	monster.poe2.commandHasteCarry = static_cast<uint8_t>(monster.poe2.commandHasteCarry + std::min<int>(haste, 99));
	if (monster.poe2.commandHasteCarry < 100)
		return false;
	monster.poe2.commandHasteCarry = static_cast<uint8_t>(monster.poe2.commandHasteCarry - 100);
	return true;
}

bool CommandStaggerHolds(const Monster &monster)
{
	const Poe2MonsterState &state = monster.poe2;
	if (!gbIsPoe2 || state.commandStaggerTicks == 0 || monster.hasNoLife())
		return false;
	// A rare or a unique finishes the attack it's in; it only can't start another.
	return state.commandStaggerHard || IsNoneOf(monster.mode, MonsterMode::MeleeAttack, MonsterMode::RangedAttack, MonsterMode::SpecialMeleeAttack, MonsterMode::SpecialRangedAttack);
}

bool CommandStagger(Monster &monster)
{
	if (!gbIsPoe2 || monster.hasNoLife() || monster.isPlayerMinion())
		return false;
	Poe2MonsterState &state = monster.poe2;
	// A boss a Command staggered lately shrugs the next one off; it still takes the damage.
	if (state.commandStaggerImmuneTicks > 0)
		return false;
	const CommandTier tier = CommandTierOf(monster);
	const CommandTierScale &scale = CommandTierScales[static_cast<size_t>(tier)];
	// The light stun's own path: it breaks off what the monster was doing.
	if (scale.interrupts)
		M_StartStagger(monster);
	state.commandStaggerTicks = std::max<uint8_t>(state.commandStaggerTicks, scale.staggerTicks);
	state.commandStaggerHard = scale.interrupts;
	if (tier == CommandTier::Boss)
		state.commandStaggerImmuneTicks = static_cast<uint8_t>(CommandStaggerImmuneTicks);
	vfx::Sparks(monster.position.tile, vfx::Element::Physical, 0.7F);
	return true;
}

void TickCommandMonster(Monster &monster)
{
	Poe2MonsterState &state = monster.poe2;
	if (state.commandWeakenTicks > 0 && --state.commandWeakenTicks == 0)
		state.commandWeakenPercent = 0;
	if (state.commandStaggerTicks > 0)
		state.commandStaggerTicks--;
	if (state.commandStaggerImmuneTicks > 0)
		state.commandStaggerImmuneTicks--;
	const Point tile = monster.position.tile;
	if (state.commandShellTicks > 0 && --state.commandShellTicks % 10 == 0)
		vfx::Sparks(tile, vfx::Element::Cold, 0.35F);
	if (state.commandRitesTicks > 0 && --state.commandRitesTicks % 8 == 0)
		vfx::Sparks(tile, vfx::Element::Blood, 0.4F);
	if (state.commandFrenzyTicks > 0) {
		// POE2 Enrage's cost: a share of its life each second, never the last of it.
		if (state.commandFrenzyTicks % 20 == 0 && monster.hitPoints.raw() > 64) {
			const int32_t loss = std::max<int32_t>(Percent(monster.maxHitPoints.raw(), BoneFrenzyLifeLossPercent), 1);
			monster.hitPoints = Fixed26_6::fromRaw(std::max<int32_t>(monster.hitPoints.raw() - loss, 64));
		}
		if (--state.commandFrenzyTicks % 6 == 0)
			vfx::Sparks(tile, vfx::Element::Blood, 0.45F);
	}
}

void OnCommandMinionDied(const Player &owner, const Monster &minion)
{
	if (!gbIsPoe2 || &owner != MyPlayer || leveltype == DTYPE_TOWN)
		return;
	// Only the skeletons leave bones.
	const MinionGemData *gem = MinionGemOf(minion);
	if (gem == nullptr || !IsMinionCountSkill(gem->skill))
		return;
	SummonCommandRuntime &state = owner.poe2.commandState;
	auto slot = std::min_element(state.bones.begin(), state.bones.end(), [](const CommandBones &a, const CommandBones &b) { return a.ticks < b.ticks; });
	*slot = { static_cast<uint8_t>(minion.position.tile.x), static_cast<uint8_t>(minion.position.tile.y), static_cast<uint16_t>(CommandBonesTicks) };
}

void OnShadowAppeared(Player &player, Point tile)
{
	if (!gbIsPoe2 || &player != MyPlayer || !InDungeonBounds(tile))
		return;
	SummonCommandRuntime &state = player.poe2.commandState;
	state.shadowX = static_cast<uint8_t>(tile.x);
	state.shadowY = static_cast<uint8_t>(tile.y);
	state.shadowTicks = static_cast<uint8_t>(ShadowStandTicks);
}

void ProcessSummonCommands(Player &player)
{
	if (!gbIsPoe2 || &player != MyPlayer)
		return;
	SummonCommandRuntime &state = player.poe2.commandState;
	for (uint16_t &cooldown : state.cooldowns) {
		if (cooldown > 0)
			cooldown--;
	}
	// A Command whose minion never reached its action frame goes off anyway.
	for (const SummonCommand command : Commands) {
		CommandPending &pending = state.pending[Index(command)];
		if (pending.ticks > 0 && pending.ticks-- == 1) {
			pending.ticks = 1;
			RunPending(player, command, nullptr);
		}
	}
	for (CommandBones &bones : state.bones) {
		if (bones.ticks > 0)
			bones.ticks--;
	}
	if (!On(player, PersistentSkill::ShadowWarrior))
		state.shadowTicks = 0;
	if (state.shadowTicks > 0 && --state.shadowTicks % 20 == 0)
		vfx::Sparks({ state.shadowX, state.shadowY }, vfx::Element::Arcane, 0.4F);
	if (leveltype == DTYPE_TOWN) {
		state.grounds = {};
		state.blazeTicks = 0;
		return;
	}
	BurnAroundGolem(player);
	TickGrounds(player);
}

void ClearSummonCommands()
{
	if (MyPlayer == nullptr)
		return;
	SummonCommandRuntime &state = MyPlayer->poe2.commandState;
	state.grounds = {};
	state.bones = {};
	state.pending = {};
	state.blazeTicks = 0;
	state.shadowTicks = 0;
}

int CommandGroundCount(const Player &player, SummonCommand command)
{
	const auto &grounds = player.poe2.commandState.grounds;
	return static_cast<int>(std::count_if(grounds.begin(), grounds.end(), [&](const CommandGround &ground) { return ground.ticks > 0 && ground.command == static_cast<uint8_t>(command); }));
}

CommandDamageShare CommandDamageOverCooldown(SummonCommand command, const Monster &minion, int guardianLevel)
{
	const int64_t window = SummonCommandCooldownTicks(command);
	if (command == SC::FireSweep) {
		// A Guardian looks for a target every 16 ticks and breathes one fireball.
		const int64_t bolt = GuardianBoltRaw(guardianLevel);
		return { Percent(bolt, FireSweepPercent), bolt * window / 16 };
	}
	const MinionGemData *gem = MinionGemOf(minion);
	const MonsterData &data = minion.data();
	const auto attack = static_cast<size_t>(MonsterGraphic::Attack);
	const int64_t animation = std::max<int64_t>(static_cast<int64_t>(data.frames[attack]) * std::max<int>(data.rate[attack], 1), 1);
	const int64_t period = std::max<int64_t>(animation, gem != nullptr ? gem->attackTicks : 0);
	int64_t hit = MinionBlowRaw(minion);
	int64_t shot = 0;
	if (gem != nullptr && gem->shot != MinionShot::None) {
		shot = static_cast<int64_t>(MinionShotAverage(minion, gem->shotPercent)) * 64;
		hit = shot * (gem->shot == MinionShot::ChargedBolts ? 2 : 1);
	}
	// Fire Golem's blows add their share as fire.
	if (CommandGem(command) == PersistentSkill::FireGolem)
		hit = hit * (100 + FireGolemFirePercent) / 100;
	const int64_t normal = hit * window / period;
	switch (command) {
	case SC::FlameVolley:
		return { static_cast<int64_t>(MinionShotAverage(minion, gem->shotPercent * FlameVolleyPercent / 100)) * 64, normal };
	case SC::PoisonArrow: {
		// The arrow, then the cloud's poison on a target that stays in it for the rest of the cooldown.
		const int64_t arrow = static_cast<int64_t>(MinionShotAverage(minion, gem->shotPercent * PoisonArrowPercent / 100)) * 64;
		return { arrow + (arrow * PoisonCloudPercentPerSecond / 100 * std::min<int64_t>(window, PoisonCloudTicks) / 20), normal };
	}
	case SC::ThunderCall:
		// One strike on the target, and the shocked ground's 20% more on the minion's own hits while it stands there.
		return { shot * ThunderCallPercent / 100 + (normal * ShockDamagePercent / 100 * std::min<int64_t>(window, ShockedGroundTicks) / window), normal };
	case SC::BoneFrenzy:
		// More damage and faster attacks for its time.
		return { normal * (((100 + BoneFrenzyDamagePercent) * (100 + BoneFrenzySpeedPercent)) - 10000) / 10000 * std::min<int64_t>(window, BoneFrenzyTicks) / window, normal };
	case SC::PackRush:
		return { MinionBlowRaw(minion) * PackRushPercent / 100, normal };
	case SC::QuakeStomp:
		return { MinionBlowRaw(minion) * QuakeStompPercent / 100, normal };
	case SC::GolemsBlaze:
		return { MinionBlowRaw(minion) * GolemsBlazePercentPerSecond / 100 * GolemsBlazeTicks / 20, normal };
	case SC::LastRites:
		return { normal * LastRitesDamagePercent / 100 * std::min<int64_t>(window, LastRitesTicks) / window, normal };
	default:
		return { 0, normal };
	}
}

} // namespace devilution
