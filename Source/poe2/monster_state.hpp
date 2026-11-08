/**
 * @file poe2/monster_state.hpp
 *
 * POE2 Mashup per-monster state that only lasts while the level is loaded.
 */
#pragma once

#include <array>
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
	/** @brief Curses from the skill table, in game ticks left: Weaken makes it hit softer, Decrepify slows it, Iron Maiden turns its
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
	/**
	 * @brief Confuse's Berserk (Curtis 2026-11-07, Q36): a rare or unique it was aimed at from gem level 10 hits this much
	 * harder, in percent (Berserk's 120-129%), while confuseTicks last; 0 when it isn't berserk (not saved).
	 */
	uint8_t berserkPercent = 0;
	/** @brief Attract (Q36): game ticks the monster draws the monsters near it onto itself (not saved). */
	uint8_t attractTicks = 0;
	/** @brief Attract's roll at the cast, so each rare near it obeys or not for the whole curse, alike on every machine. */
	uint8_t attractSeed = 0;
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
	/** @brief Each table curse's duration as cast, by TableCurse, so Hex Rupture can tell when half of it has run out. */
	std::array<uint8_t, 16> curseFullTicks {};
	/** @brief The skill that last hit the monster (a SkillSlot's kind and id) and the player who used it, for kill supports. */
	uint8_t lastHitSkillKind = 0;
	uint8_t lastHitSkillId = 0;
	int8_t lastHitPlayer = -1;
	/** @brief Withered by Withering Touch: takes more damage per stack. */
	uint8_t witherStacks = 0;
	uint8_t witherTicks = 0;
	/**
	 * @brief Poison, chaos damage over time, as a raw Fixed26_6 value per tick before the monster's chaos resistance. Venom's
	 * is POE2's, the strongest only; supports alone stack theirs, Twin Fangs and Andariel's Anguish (InflictPoison).
	 */
	uint8_t poisonTicks = 0;
	int8_t poisonSource = -1;
	int32_t poisonDamagePerTick = 0;
	/** @brief The poison is Cobra Strike's own, which its next strike doesn't consume (Curtis 2026-11-07, Q42; not saved). */
	bool poisonFromCobra = false;
	/** @brief The bleed is Bloodletting's own, which its next stab doesn't consume (the Cobra Strike rule, Q42; not saved). */
	bool bleedFromBloodletting = false;
	/**
	 * @brief Pyre Blow (Curtis 2026-11-07, Q48; not saved): it dies within this many game ticks of the blow and it bursts
	 * into flame, for this much (a raw Fixed26_6 value), from this player.
	 */
	uint8_t pyreTicks = 0;
	int8_t pyreSource = -1;
	int32_t pyreBurst = 0;
	/** @brief Plague Burst: the poison builds plague, the chaos its ticks dealt as a raw Fixed26_6 value, which bursts on death. */
	bool poisonPlague = false;
	int32_t plagueStore = 0;
	/** @brief Hindered by caltrops or Herald of Plague: slowed like a chill, in game ticks left. */
	uint8_t hinderTicks = 0;
	/** @brief How much the hinder slows, in percent; 0 for the usual HinderPercent (Witch's Bog's is milder; not saved). */
	uint8_t hinderPercent = 0;
	/**
	 * @brief Blood and Venom (POE2's Admixture): the poison deals more while the monster bleeds, and the bleed more while
	 * it is poisoned, when the skill that inflicted it had the support (not saved).
	 */
	bool poisonVsBleed = false;
	bool bleedVsPoison = false;
	/**
	 * @brief Rathma's Doom and Festering Hex (not saved): the hand-cast curses (one bit for each TableCurse) that burst in
	 * poison as they run out, or that poison the monster while it is cursed, and the player who cast them. The burst's
	 * chaos hit and the hex's poison (raw Fixed26_6 values) are counted when the curse is cast.
	 */
	uint16_t doomCurses = 0;
	uint16_t festerCurses = 0;
	int8_t curseSupportSource = -1;
	int32_t doomDamage = 0;
	int32_t festerPoison = 0;
	/** @brief Game ticks to Festering Hex's next poison, and until the monster can take another doom burst. */
	uint8_t festerTicks = 0;
	uint8_t doomCooldown = 0;
	/** @brief A doomed curse ran out this tick: its burst goes off on the caster's machine (ProcessCurseSupports). */
	bool doomPending = false;
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
	/** @brief The Whirling Volley cast whose ring of arrows hit it, which hits it once (not saved). */
	uint16_t volleyCastHit = 0;
	/** @brief The Hellfire Roar use whose fire burst last hit it, which burns it once a use (not saved). */
	uint16_t hellfireUseHit = 0;
	/** @brief Game ticks a fire hit on it counts as fresh, for Swamp Gas Arrow's clouds (FireHitTicks; not saved). */
	uint8_t fireHitTicks = 0;
	/**
	 * @brief Summon Commands (poe2/summon_commands.hpp; none saved). A minion's Command waiting for its attack's action
	 * frame (a SummonCommand, 0 for none) and the tile it's aimed at.
	 */
	uint8_t commandAction = 0;
	uint8_t commandAimX = 0;
	uint8_t commandAimY = 0;
	/** @brief A minion's Bone Frenzy, Frost Shell and Last Rites, in game ticks left, and its extra turns building up to 100. */
	uint8_t commandFrenzyTicks = 0;
	uint8_t commandShellTicks = 0;
	uint8_t commandRitesTicks = 0;
	uint8_t commandHasteCarry = 0;
	/** @brief An enemy's Grave Howl: deals this much less damage, in percent, for the game ticks left. */
	uint8_t commandWeakenTicks = 0;
	uint8_t commandWeakenPercent = 0;
	/**
	 * @brief An enemy's Command stagger: game ticks it can't act; whether it broke off its attack (normal and magic
	 * monsters) or only can't start another (rares and uniques); and a unique's game ticks of no more Command staggers.
	 */
	uint8_t commandStaggerTicks = 0;
	bool commandStaggerHard = false;
	uint8_t commandStaggerImmuneTicks = 0;
};

} // namespace devilution
