/**
 * @file poe2/monster_state.hpp
 *
 * POE2 Mashup per-monster state that only lasts while the level is loaded.
 */
#pragma once

#include <cstdint>

namespace devilution {

enum class WeaponSkill : uint8_t;

struct Poe2MonsterState {
	uint8_t igniteTicks = 0;
	int8_t igniteSource = -1;
	/** @brief Fire damage per tick as a raw Fixed26_6 value. */
	int32_t igniteDamagePerTick = 0;
	/** @brief POE2's Flammability: the chance in per mille that a fire hit ignites, built up by fire hits. */
	int16_t flammability = 0;
	/** @brief Flammability fades this many ticks after the last fire hit. */
	uint8_t flammabilityTicks = 0;
	uint8_t shockTicks = 0;
	int regenRemainder = 0;
	uint8_t chillTicks = 0;
	/** @brief How much a chill slows the monster, in percent. */
	uint8_t chillPercent = 0;
	/** @brief Slowed ticks owed by the chill, in percent of a tick. */
	uint8_t chillCarry = 0;
	/** @brief Map hastes collect here; at 100 the monster takes an extra turn. */
	uint8_t hasteCarry = 0;
	uint8_t freezeTicks = 0;
	/** @brief Cold damage built up towards a freeze, as a raw Fixed26_6 value. */
	int32_t freezeBuildup = 0;
	/** @brief Bleeding from a unique's critical hits: physical damage per tick as a raw Fixed26_6 value. */
	uint8_t bleedTicks = 0;
	int8_t bleedSource = -1;
	int32_t bleedDamagePerTick = 0;
	/** @brief The table skill that caused the bleeding (Haunt, Locust Swarm...), or None: the play log credits it the damage. */
	WeaponSkill bleedSkill {};
	/** @brief Exposure: takes more damage of one element (a DamageType). */
	uint8_t exposureTicks = 0;
	uint8_t exposureElement = 0;
	/** @brief Electrocuted: can't act and takes more lightning damage. */
	uint8_t electrocuteTicks = 0;
	/** @brief The ignite already spread to monsters around (Warden of Embers uniques). */
	bool igniteSpread = false;
	uint8_t heavyStunTicks = 0;
	/** @brief Physical damage built up towards a heavy stun, as a raw Fixed26_6 value. */
	int32_t stunBuildup = 0;
	/** @brief Cursed with Amplify Damage: takes more physical damage; the curse's strength in percent. */
	uint8_t amplifyTicks = 0;
	uint8_t amplifyPercent = 0;
	/** @brief Armour broken by Smite: takes more physical damage. */
	uint8_t armourBreakTicks = 0;
	/** @brief Curses from the skill table, in game ticks left: Weaken and Decrepify make it hit softer, Iron Maiden turns its
	 * melee hits back on it, Lower Resist and Death Mark make it take more damage, Confuse sets it on other monsters. */
	uint8_t weakenTicks = 0;
	uint8_t decrepifyTicks = 0;
	uint8_t ironMaidenTicks = 0;
	uint8_t lowerResistTicks = 0;
	uint8_t deathMarkTicks = 0;
	/** @brief Despair: -30% chaos resistance (not saved, like the other curses). */
	uint8_t despairTicks = 0;
	uint8_t confuseTicks = 0;
	/** @brief Confuse made the monster turn on its kind, so its flags go back when the curse ends. */
	bool confusedByCurse = false;
	/** @brief Haunt and Locust Swarm drain the monster and move on to another when it dies. */
	uint8_t hauntTicks = 0;
	uint8_t locustTicks = 0;
	/**
	 * @brief Chaos damage over time from Haunt, Locust Swarm and Decompose's plague, as a raw Fixed26_6 value per tick before
	 * Withered and chaos resistance. Its own slot, so a bleed and a chaos drain don't overwrite each other (not saved).
	 */
	uint8_t chaosTicks = 0;
	int8_t chaosSource = -1;
	int32_t chaosDamagePerTick = 0;
	/** @brief The table skill that put it there: the play log credits it the damage. */
	WeaponSkill chaosSkill {};
	/** @brief Curses Blasphemy put on, one bit for each TableCurse, and how strong they are in percent (not saved, like the curses). */
	uint16_t auraCurses = 0;
	uint8_t auraCurseStrength = 100;
	/** @brief The skill that last hit the monster (a SkillSlot's kind and id) and the player who used it, for kill supports. */
	uint8_t lastHitSkillKind = 0;
	uint8_t lastHitSkillId = 0;
	int8_t lastHitPlayer = -1;
	/** @brief Withered by Withering Touch: takes more damage per stack. */
	uint8_t witherStacks = 0;
	uint8_t witherTicks = 0;
	/**
	 * @brief Poison, chaos damage over time, as a raw Fixed26_6 value per tick before the monster's chaos resistance. Venom's
	 * is POE2's, the strongest only; Andariel's Anguish and Ravenous Swarm stack theirs (InflictPoison).
	 */
	uint8_t poisonTicks = 0;
	int8_t poisonSource = -1;
	int32_t poisonDamagePerTick = 0;
	/** @brief Hindered by caltrops or Herald of Plague: slowed like a chill, in game ticks left. */
	uint8_t hinderTicks = 0;
	/** @brief Attrition: game ticks the rare or unique monster has spent near the hero (not saved). */
	uint16_t attritionTicks = 0;
	/** @brief Raised by Revive: a minion that keeps the strength it had. */
	bool revived = false;
	/** @brief Killed by a Cull (set where the Cull lands, read when it dies; not saved, since the death follows at once). */
	bool culled = false;
	/**
	 * @brief The Butcher's Road hook (not saved, like the ailments): game ticks left of the drag, which it can't act
	 * through, and the tile it is dragged to; the tile its last step left, to draw the step smoothly, while hookStepped.
	 */
	uint8_t hookTicks = 0;
	uint8_t hookToX = 0;
	uint8_t hookToY = 0;
	uint8_t hookFromX = 0;
	uint8_t hookFromY = 0;
	bool hookStepped = false;
	/** @brief The hook's slow on a rare, and a unique's flinch (it can't attack), in game ticks left (not saved). */
	uint8_t hookSlowTicks = 0;
	uint8_t flinchTicks = 0;
	/** @brief The Butcher's Road pact: enraged, in game ticks left, and its extra turns building up to 100 (not saved). */
	uint8_t enrageTicks = 0;
	uint8_t enrageCarry = 0;
	/** @brief It has attacked the hero once: the Maiden's Road pact no longer hides her from it (not saved). */
	bool struckHero = false;
	/** @brief A player's minion heading back to its owner, ignoring enemies until it's close again. */
	bool minionReturning = false;
	/** @brief Times in a row a minion failed to find a way back to its owner. */
	uint8_t minionStuck = 0;
	/** @brief Game ticks before a slow minion (a Sniper or a Brute) attacks again (not saved). */
	uint8_t minionAttackWait = 0;
	/** @brief A Skeletal Reaver's Rage, and the game ticks before it starts to fade (not saved). */
	uint8_t minionRage = 0;
	uint8_t minionRageTicks = 0;
	/** @brief A Skeletal Cleric's game ticks before its next heal (not saved). */
	uint8_t clericTicks = 0;
	/** @brief Game ticks a short-lived minion (a raging spirit, an illusion) has left; 0 for one that stays (not saved). */
	uint8_t minionLifeTicks = 0;
	/** @brief Lingering Illusion's illusion of the hero, which draws enemies near it (not saved). */
	bool decoy = false;
	/** @brief Game ticks before Mana Drain can take mana from this monster again (not saved). */
	uint8_t manaDrainTicks = 0;
	/** @brief Decompose's plague: game ticks left; a monster that dies with it passes it on (not saved). */
	uint8_t plagueTicks = 0;
	/** @brief The per-target granted cast whose projectile last hit it, which hits it once (not saved). */
	uint16_t grantedCastHit = 0;
};

} // namespace devilution
