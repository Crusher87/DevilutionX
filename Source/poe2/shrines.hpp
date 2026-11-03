/**
 * @file poe2/shrines.hpp
 *
 * POE2 Mashup shrines (build spec step 8): Diablo's shrines keep their names and models but give POE2's timed shrine
 * powers. The table is txtdata/poe2/shrines.tsv. A hero holds at most two shrine buffs; a third replaces the oldest,
 * and the same shrine again restarts its timer (except Glowing). Diablo's lasting shrine effects are gone in POE2 mode;
 * the Town shrine is unchanged.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "engine/point.hpp"
#include "poe2/shrine_state.hpp"

namespace devilution {

class DiabloGenerator;
struct Monster;
struct Player;

/** @brief A row of shrines.tsv. */
struct ShrineData {
	ShrineKind kind = ShrineKind::Count;
	/** @brief Diablo's name, as the shrine is called on the floor. */
	std::string name;
	/** @brief POE2's power, as the buff is called. */
	std::string power;
	ShrineEffect effect = ShrineEffect::None;
	/** @brief How long the buff lasts; 0 for the shrines that give another's. */
	int seconds = 0;
	/** @brief The hero's glow while it is the newest buff: a palette index. */
	uint8_t glow = 0;
	/** @brief A SpellIcon for the icon row and over the hero's head. */
	uint8_t icon = 0;
	/** @brief Whether touching the same shrine again restarts its timer. */
	bool refresh = true;
	/** @brief Whether it is placed on floors in POE2 mode. */
	bool spawns = true;
	/** @brief One line saying what it does. */
	std::string text;
};

/** @brief Shrines a floor gets at most: one on floors 1 to 4, two deeper. */
constexpr int EarlyShrineFloors = 4;
constexpr int EarlyFloorShrineCap = 1;
constexpr int DeepFloorShrineCap = 2;
/** @brief An icon blinks for the last seconds of its buff. */
constexpr int ShrineBlinkTicks = 5 * 20;
/** @brief A Murphy's curse lasts this long. */
constexpr int ShrineCurseTicks = 20 * 20;

// The powers' numbers.
constexpr int GloomChancePercent = 40;
constexpr int GloomLifePercent = 25;
constexpr int GloomRadius = 2;
constexpr int HexingChancePercent = 25;
constexpr int ShrineEchoDelayTicks = 6;
constexpr int ReplenishLifePermillePerSecond = 40;
constexpr int ReplenishManaPermillePerSecond = 100;
constexpr int ImpenetrableDefencesPercent = 100;
constexpr int ResistanceShrineResistances = 50;
constexpr int ResistanceShrineMaxResistances = 5;
constexpr int CovetousRarityPercent = 50;
constexpr int CovetousQuantityPercent = 15;
constexpr int CovetousDamageTakenPercent = 10;
constexpr int AccelerationMovePercent = 40;
constexpr int AccelerationSkillSpeedPercent = 60;
constexpr int GreedGoldPercent = 100;
constexpr int SkeletalShrineSkeletons = 3;
constexpr int BrutalMoreDamagePercent = 50;
constexpr int BrutalStunDurationPercent = 30;
constexpr int ThornedReflectPercent = 18;
constexpr int EnduringLifePercent = 40;
constexpr int EnduringAreaPercent = 40;
constexpr int EnduringIILifePercent = 50;
constexpr int EnduringIIDefencesPercent = 100;
constexpr int SeekingRarityPercent = 100;
constexpr int AccelerationIProjectileSpeedPercent = 80;
constexpr int AccelerationISkillSpeedPercent = 50;
constexpr int ExtraElementPercent = 50;
constexpr int EnlighteningExperiencePercent = 50;
constexpr int SolarElementDamagePercent = 50;
constexpr int SolarNightProjectiles = 2;
/** @brief Murphy's curses. */
constexpr int VulnerabilityDamageTakenPercent = 20;
constexpr int ElementalWeaknessResistances = -20;
constexpr int TemporalChainsSlowPercent = 20;
/** @brief The storms: game ticks between strikes, and each strike's damage in percent of a white monster's life at the area level. */
constexpr int MeteorTicks = 20;
constexpr int MeteorFallTicks = 8;
constexpr int MeteorRange = 7;
constexpr int MeteorLifePercent = 40;
constexpr int TempestTicks = 40;
constexpr int TempestRadius = 3;
constexpr int TempestLifePercent = 25;
constexpr int FreezingTicks = 6;
constexpr int FreezingLifePercent = 15;
constexpr int ArcTicks = 20;
constexpr int ArcRange = 6;
constexpr int ArcTargets = 3;
constexpr int ArcLifePercent = 20;

/** @brief Loads txtdata/poe2/shrines.tsv; empty outside POE2 mode or when the table is broken. */
void LoadShrineData();
[[nodiscard]] bool HasShrineData();
/** @brief The shrine's row; an empty row (no effect) without the table. */
[[nodiscard]] const ShrineData &GetShrineData(ShrineKind kind);
/** @brief Reads a glow name of shrines.tsv (red, orange, yellow, gold, blue, iceblue, white, beige, gray) as a palette index. */
[[nodiscard]] std::optional<uint8_t> ParseShrineGlow(std::string_view name);

// The buffs on their own, as the state that is saved and sent.

/**
 * @brief Gives a shrine's buff. The same shrine again restarts its timer and becomes the newest, unless @p refresh
 * is false, when nothing changes; with two buffs up, a new one replaces the oldest.
 * @return Whether anything changed.
 */
bool AddShrineBuff(ShrineBuffState &state, ShrineKind kind, uint8_t variant, int ticks, bool refresh = true);
/** @brief Takes a shrine's buff off, if it is up. */
bool RemoveShrineBuff(ShrineBuffState &state, ShrineKind kind);
/** @brief Counts the buffs and the curse down one game tick; the shrines whose buffs ran out are returned. */
std::vector<ShrineKind> TickShrineBuffState(ShrineBuffState &state, bool *curseEnded = nullptr);
/** @brief The newest buff, or nullptr. */
[[nodiscard]] const ShrineBuff *NewestShrineBuff(const ShrineBuffState &state);
[[nodiscard]] size_t ShrineBuffCount(const ShrineBuffState &state);
/** @brief The buffs and curse as the bytes of the "poe2shrines" save entry (and the network). */
[[nodiscard]] std::vector<uint8_t> PackShrineBuffs(const ShrineBuffState &state);
/** @brief Reads PackShrineBuffs's bytes; false (and no buffs) when they make no sense. Unknown kinds are dropped. */
bool UnpackShrineBuffs(ShrineBuffState &state, std::span<const uint8_t> data);

// Placement.

/** @brief The most shrines a dungeon level gets. */
[[nodiscard]] int ShrineCapForLevel(int level);
/** @brief Diablo's and Na-Krul's floors, quest levels and maps: Divine shrines aren't placed there, and a Divine buff ends there. */
[[nodiscard]] bool IsShrineBossFloor(int level, bool isSetLevel, bool inMap);
/** @brief The shrines a level can get in POE2 mode, of Diablo's first @p shrineCount (26 without Hellfire). */
[[nodiscard]] std::vector<ShrineKind> SpawnableShrines(int level, bool bossFloor, int shrineCount);
/** @brief The level here: dungeon level, and whether it is a boss floor. */
[[nodiscard]] bool OnShrineBossFloor();

// Touching a shrine.

/** @brief What touching a shrine gives: the buff's shrine (another's for Mysterious and Murphy's), Solar's variant and a curse. */
struct ShrineGrant {
	ShrineKind kind = ShrineKind::Count;
	uint8_t variant = 0;
	ShrineCurse curse = ShrineCurse::None;
};
/**
 * @brief What a shrine gives, rolled with the shrine's own seed so every client agrees; @p night is the toucher's clock,
 * for Solar.
 */
[[nodiscard]] ShrineGrant ResolveShrine(ShrineKind touched, DiabloGenerator &rng, bool night);
/** @brief Whether the clock on this computer says night: 20:00 to 6:00. */
[[nodiscard]] bool ShrineClockSaysNight();
/** @brief Puts a grant on the hero: the buff, the curse, stats and a line in the play log. */
void ApplyShrineGrant(Player &player, const ShrineGrant &grant);
/** @brief One line of what the buff does, for the message on touching it. */
[[nodiscard]] std::string ShrineGrantLine(const ShrineGrant &grant);
/** @brief What hovering a shrine shows under its name. */
[[nodiscard]] std::string ShrineHoverText(ShrineKind kind);

// Every game tick, and the level.

/** @brief Counts the hero's shrine buffs down and sets off their storms and regeneration; every game tick. */
void ProcessShrineBuffs(Player &player);
/** @brief The hero arrived on a level: Divine ends on a boss floor. */
void OnShrineLevelEntered(Player &player);

// What the buffs do: numbers the game's hooks read.

struct ShrineBonuses {
	int lifePercent = 0;
	int armourPercent = 0;
	int evasionPercent = 0;
	int energyShieldPercent = 0;
	int resistances = 0;
	int maxResistances = 0;
	int fireDamagePercent = 0;
	int coldDamagePercent = 0;
	int rarityPercent = 0;
	int quantityPercent = 0;
	int goldPercent = 0;
	/** @brief More damage taken, in percent. */
	int damageTakenPercent = 0;
	int movePercent = 0;
	/** @brief Increased attack and cast speed. */
	int skillSpeedPercent = 0;
	int projectileSpeedPercent = 0;
	int areaPercent = 0;
	int extraProjectiles = 0;
	/** @brief More damage dealt, in percent. */
	int moreDamagePercent = 0;
	int stunDurationPercent = 0;
	int reflectPercent = 0;
	/** @brief More experience, in percent. */
	int experiencePercent = 0;
	/** @brief Gain this percent of damage as extra fire, lightning and cold. */
	int extraFirePercent = 0;
	int extraLightningPercent = 0;
	int extraColdPercent = 0;
	int skeletons = 0;
	/** @brief Life and mana regenerated a second, in thousandths of the maximum. */
	int lifeRegenPermille = 0;
	int manaRegenPermille = 0;
	bool echoing = false;
	bool gloom = false;
	bool hexing = false;
	bool alwaysCrit = false;
	bool invulnerable = false;
	bool knockback = false;
	bool avoidAilments = false;
	bool noEquipmentDrops = false;
	bool equipmentToCurrency = false;
	bool corruptDrops = false;
	bool meteoric = false;
	bool tempest = false;
	bool freezing = false;
	bool arcs = false;
};
/** @brief What the hero's shrine buffs and curse add up to; nothing outside POE2 mode. */
[[nodiscard]] ShrineBonuses GetShrineBonuses(const Player &player);
/** @brief The same for a bare state (tests, tooltips). */
[[nodiscard]] ShrineBonuses ShrineStateBonuses(const ShrineBuffState &state);
[[nodiscard]] bool HasShrineEffect(const Player &player, ShrineEffect effect);
/** @brief Skeletal: the level its skeletons rise at, from the hero's level. */
[[nodiscard]] int ShrineSkeletonLevel(const Player &player);
/** @brief A storm's strike at an area level, as a raw Fixed26_6. */
[[nodiscard]] int32_t ShrineStrikeRaw(int areaLevel, int lifePercent);

/** @brief Gloom: a monster the hero killed may burst for a share of its life. */
void OnShrineKill(Player &player, const Monster &monster);
/** @brief Thorned: a monster's hit on the hero comes back at it. */
void ShrineReflectHit(Player &player, Monster &monster, int32_t damageTakenRaw);
/** @brief Hexing: a hit may curse the monster. */
void OnShrineHit(Player &player, Monster &monster);

/** @brief The hero's glow: the newest buff's colour, pulsing; none without a buff or outside POE2 mode. */
[[nodiscard]] std::optional<uint8_t> ShrineGlowColor(const Player &player);

/** @brief The tooltip line of a buff's icon: power, what it does and the seconds left. */
[[nodiscard]] std::string ShrineBuffTooltip(const ShrineBuff &buff);
[[nodiscard]] std::string ShrineCurseName(ShrineCurse curse);
[[nodiscard]] std::string ShrineCurseTooltip(ShrineCurse curse, int ticksLeft);

} // namespace devilution
