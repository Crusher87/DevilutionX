/**
 * @file poe2/summon_commands.hpp
 *
 * POE2 Mashup Summon Commands, like POE2's minion Command skills: a minion gem also grants a Command, picked for the
 * skill tray like any skill, with its own cooldown and no mana cost. Using it makes that gem's minions do their special
 * move: the mages fire a volley, the golem stomps, the wolves leap. It needs one of those minions standing.
 *
 * The Command comes from the socketed gem, so nothing about it is saved but the tray slot it sits on. A use goes
 * through one request (SummonCommandRequest) and its effects are plain values on the hero and the monsters, so it can
 * be sent and replayed like a game command. Damage is a share of the commanding minion's own hit; stagger and weaken
 * slide by the monster's rarity, and bosses always take something.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "engine/point.hpp"
#include "poe2/player_data.hpp"

namespace devilution {

struct Missile;
struct Monster;
struct Player;

/** @brief The Commands, one for each minion gem that has one (not saved: the gem gives it). */
enum class SummonCommand : uint8_t {
	None,
	FlameVolley,
	PoisonArrow,
	FrostShell,
	ThunderCall,
	BoneFrenzy,
	GraveHowl,
	PackRush,
	QuakeStomp,
	BloodGift,
	GolemsBlaze,
	FireSweep,
	Switch,
	LastRites,
	Count,
};
static_assert(static_cast<size_t>(SummonCommand::Count) <= std::tuple_size_v<decltype(SummonCommandRuntime::cooldowns)>, "a cooldown for each Command");

/** @brief Numbers from summon-commands-numbers.md (the skill gem balance thread, 2026-11-02). */
constexpr int FlameVolleyPercent = 150;
/** @brief Mages further than this from the target, or without a clear line to it, don't fire. */
constexpr int FlameVolleySightTiles = 12;
constexpr int PoisonArrowPercent = 80;
constexpr int PoisonCloudRadius = 2;
constexpr int PoisonCloudTicks = 8 * 20;
/** @brief The cloud poisons once a second: standard poison, this share of the arrow's hit a second for 2 seconds. */
constexpr int PoisonCloudPercentPerSecond = 20;
constexpr int PoisonCloudPoisonTicks = 2 * 20;
constexpr int FrostShellRadius = 4;
constexpr int FrostShellTicks = 6 * 20;
constexpr int FrostShellLessPercent = 30;
/** @brief An attacker that hits a shelled minion takes this share of a Frost Lich's bolt as cold. */
constexpr int FrostShellRetaliatePercent = 50;
constexpr int ThunderCallReach = 5;
constexpr int ThunderCallMaxStrikes = 6;
constexpr int ThunderCallPercent = 100;
constexpr int ThunderCallRadius = 1;
constexpr int ShockedGroundTicks = 6 * 20;
/** @brief A fallen skeleton's bones count for Thunder Call this long (our own number). */
constexpr int CommandBonesTicks = 30 * 20;
constexpr int BoneFrenzyTicks = 4 * 20;
constexpr int BoneFrenzyDamagePercent = 30;
constexpr int BoneFrenzySpeedPercent = 30;
/** @brief A frenzied captain loses this share of its life a second (POE2 Enrage's cost). */
constexpr int BoneFrenzyLifeLossPercent = 2;
constexpr int GraveHowlRadius = 4;
constexpr int GraveHowlTicks = 4 * 20;
constexpr int PackRushPercent = 150;
/** @brief Wolves further than this from the target don't join. */
constexpr int PackRushReach = 8;
constexpr int QuakeStompPercent = 100;
constexpr int QuakeStompRadius = 3;
constexpr int BloodGiftLifePercent = 15;
/** @brief The heal is at most this share of the hero's life, and the golem won't give below this share of its own. */
constexpr int BloodGiftHealCapPercent = 10;
constexpr int BloodGiftMinLifePercent = 30;
constexpr int GolemsBlazeRadius = 2;
constexpr int GolemsBlazePercentPerSecond = 60;
constexpr int GolemsBlazeTicks = 4 * 20;
constexpr int FireSweepPercent = 200;
constexpr int FireSweepReach = 4;
/** @brief Shadow Warrior's shadow stays where it last struck this long, for Switch (our own number). */
constexpr int ShadowStandTicks = 8 * 20;
constexpr int LastRitesTicks = 4 * 20;
constexpr int LastRitesDamagePercent = 40;
constexpr int LastRitesSpeedPercent = 20;
/** @brief A unique or boss a Command staggered ignores Command staggers this long; it still takes the damage. */
constexpr int CommandStaggerImmuneTicks = 10 * 20;
/** @brief A Command whose minion's action frame never comes (it was hit mid-swing) goes off this long after the use. */
constexpr int CommandFallbackTicks = 24;

/** @brief How hard a Command shakes a monster: by its rarity, uniques and quest bosses last. */
enum class CommandTier : uint8_t {
	Normal,
	Magic,
	Rare,
	Boss,
};
struct CommandTierScale {
	/** Quake Stomp's stagger, in game ticks. */
	uint8_t staggerTicks;
	/** Grave Howl: the monster deals this much less damage, in percent. */
	uint8_t weakenPercent;
	/** Whether the Command breaks off the attack it's in. */
	bool interrupts;
};
constexpr std::array<CommandTierScale, 4> CommandTierScales { {
	{ 20, 20, true },
	{ 15, 20, true },
	{ 10, 15, false },
	{ 5, 10, false },
} };
[[nodiscard]] CommandTier CommandTierOf(const Monster &monster);

/** @brief The gem's Command, or None: Skeleton Warriors, Bone Priests, Burning Souls and Phantom Double have none. */
[[nodiscard]] SummonCommand CommandForGem(PersistentSkill gem);
[[nodiscard]] PersistentSkill CommandGem(SummonCommand command);
[[nodiscard]] std::span<const SummonCommand> AllSummonCommands();
[[nodiscard]] std::string_view SummonCommandName(SummonCommand command);
/** @brief One short line for the tooltip. */
[[nodiscard]] std::string_view SummonCommandDescription(SummonCommand command);
[[nodiscard]] int SummonCommandCooldownTicks(SummonCommand command);
/** @brief Its tags (SkillTag): Minion, Command and its element. */
[[nodiscard]] uint32_t SummonCommandTags(SummonCommand command);
/** @brief Like "Minion, Command, Fire". */
[[nodiscard]] std::string SummonCommandTagText(SummonCommand command);
/** @brief The Command of the gem in a Skills panel socket, or None. */
[[nodiscard]] SummonCommand SocketCommand(const Player &player, size_t socket);
/** @brief The Skills panel sockets whose gems grant a Command, in socket order: what the skill picker offers. */
[[nodiscard]] std::vector<size_t> CommandSockets(const Player &player);

/** @brief One use of a Command: whose gem, the tile aimed at and the monster there (-1 for none). */
struct SummonCommandRequest {
	PersistentSkill gem = PersistentSkill::None;
	Point target {};
	int16_t targetMonster = -1;
};

enum class CommandResult : uint8_t {
	Used,
	NoCommand,
	/** The gem is switched off or short of Spirit. */
	Off,
	Town,
	Cooldown,
	/** None of the gem's minions are standing (or no shadow, for Switch). */
	NoMinions,
	/** Pack Rush needs an enemy to leap at. */
	NoTarget,
	/** No mage or archer has a clear line to the target. */
	NoSight,
	/** Thunder Call: no fallen skeleton's bones near the target. */
	NoBones,
	/** Blood Gift: the golem is too hurt to give. */
	GolemHurt,
	/** Switch: the shadow's tile can't be stood on or seen. */
	Blocked,
};

/** @brief Whether the Command could be used now, and why not. */
[[nodiscard]] CommandResult CheckSummonCommand(const Player &player, const SummonCommandRequest &request);
/** @brief Uses the Command: the one way in, for the tray now and the network later. Only the local hero's minions obey. */
CommandResult IssueSummonCommand(Player &player, const SummonCommandRequest &request);
[[nodiscard]] int SummonCommandCooldownLeft(const Player &player, SummonCommand command);
/** @brief Why a Command can't be used, in a few words (empty for Used). */
[[nodiscard]] std::string_view CommandResultText(CommandResult result);

/** @brief A minion's attack reached its action frame with a Command on it (a minion's cast or blow). */
void RunCommandAction(Monster &minion);
/** @brief A skeleton fired a Command's shot: Poison Arrow's arrow will burst. */
void OnCommandShotFired(const Monster &minion, uint8_t command, Point aim, Missile &shot);
/** @brief A Command's shot hit or reached its tile. */
void OnCommandShotLanded(const Missile &shot);

/** @brief Bone Frenzy's and Last Rites' more damage of a minion's blows, in percent (100 for none). */
[[nodiscard]] int CommandMinionDamagePercent(const Monster &minion);
/** @brief Grave Howl: what an enemy it shook still deals, in percent (100 for none). */
[[nodiscard]] int CommandWeakenedDamagePercent(const Monster &monster);
/** @brief Frost Shell: what a shelled minion still takes from a hit, in percent (100 for none). */
[[nodiscard]] int CommandShellDamagePercent(const Monster &minion);
/** @brief A monster hit one of the hero's minions: a shelled one's ice bites back. */
void OnShelledMinionHit(Monster &attacker, const Monster &minion);
/** @brief Bone Frenzy's faster attacks and Last Rites' faster rush: whether the minion takes another turn this tick. */
bool CommandExtraTick(Monster &monster);
/** @brief Whether a Command's stagger keeps the monster from acting this tick. */
[[nodiscard]] bool CommandStaggerHolds(const Monster &monster);
/** @brief Staggers the monster by the sliding scale; a boss then ignores Command staggers a while. @return Whether it was staggered. */
bool CommandStagger(Monster &monster);
/** @brief Once a game tick for every monster: its Command timers, and a frenzied captain's lost life. */
void TickCommandMonster(Monster &monster);
/** @brief One of the hero's minions died: a skeleton leaves bones for Thunder Call. */
void OnCommandMinionDied(const Player &owner, const Monster &minion);
/** @brief Shadow Warrior's shadow struck from this tile; it stands there a while. */
void OnShadowAppeared(Player &player, Point tile);

/** @brief Once a game tick for the local hero: cooldowns, Commands waiting on their minion, the clouds and shocked ground, the blaze. */
void ProcessSummonCommands(Player &player);
/** @brief Forgets the clouds, bones and shadow, on a new level. */
void ClearSummonCommands();
/** @brief The patches of ground of a kind out now, for the HUD and tests. */
[[nodiscard]] int CommandGroundCount(const Player &player, SummonCommand command);

/**
 * @brief For the balance test: the damage a Command adds on one target in one cooldown, and what the gem's minion
 * deals in that time, both in 1/64 points, for a minion of the gem (the golem, for a golem; the Guardian's level for
 * Fire Sweep). Zero for a Command that deals no damage.
 */
struct CommandDamageShare {
	int64_t added;
	int64_t normal;
};
[[nodiscard]] CommandDamageShare CommandDamageOverCooldown(SummonCommand command, const Monster &minion, int guardianLevel = 1);

} // namespace devilution
