/**
 * @file poe2/bound_demons.hpp
 *
 * POE2 Mashup: bound demons (build spec step 10), our take on POE2's essence monsters.
 *  - About every second floor, a rare monster of the floor's own kinds stands frozen in a pulsing blood-red crystal,
 *    chained to a Horadric sigil on the floor. It is drawn in code over the monster's own picture (no new art).
 *  - Near it a muffled heartbeat plays. Hovering it shows the essences it holds. It can't be hurt or targeted while
 *    bound, and it never wakes on its own: no monster comes from nowhere (Curtis's rule), the hero breaks the crystal.
 *  - Clicking it cracks the glass; a second later it roars and fights, a plain Diablo fight with its essences' powers
 *    (more life, harder hits, speed, an element added to its hits, resistances, armour), each shown as a coloured glow.
 *    Which essence gives which power is bound_demons.tsv.
 *  - It always drops the essences it holds: one or two. In Act 1 one Lesser essence; an Evil essence in one demon of
 *    six from Act 3.
 * The crystal is a monster flag, so it is kept with the level's save; in multiplayer the break goes through
 * CMD_POE2_BREAKCRYSTAL and the level's delta remembers it for players who come later.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "engine/point.hpp"
#include "utils/fixed_point.hpp"

namespace devilution {

struct Monster;
struct Player;
struct Surface;
class ClxSprite;
enum class DamageType : uint8_t;

/** @brief Still in its crystal: can't be hurt, doesn't act. Kept with the level's save (the flags are 32 bits there). */
constexpr uint32_t MFLAG_POE2_BOUND = 1U << 16;
/** @brief A bound demon, in its crystal or out: it has its essences' powers and drops them. */
constexpr uint32_t MFLAG_POE2_ESSENCE_DEMON = 1U << 17;

/** @brief A floor with floor packs has a bound demon one time in this many. */
constexpr int BoundDemonOneIn = 2;
/** @brief How long it stands after its crystal breaks before it fights: a second. */
constexpr int BoundDemonWakeTicks = 20;
/** @brief A bound demon stands at least this many steps from every other monster, so it is fought alone. */
constexpr int BoundDemonApartSteps = 6;
/** @brief One bound demon in this many holds an Evil essence, from Act 3. */
constexpr int EvilBoundDemonOneIn = 6;
/** @brief The first act whose bound demons can hold an Evil essence. */
constexpr int EvilBoundDemonAct = 3;
/** @brief The heartbeat's gap in game ticks, and how near (in tiles) it is heard. */
constexpr int HeartbeatTicks = 24;
constexpr int HeartbeatTiles = 8;
/** @brief The delta's mark for a demon whose crystal broke (DMonsterStr::poe2Mark; 0xFF is the empty entry). */
constexpr uint8_t DeltaBoundReleased = 1;

/** @brief What an essence gives the demon that holds it (bound_demons.tsv's power column). */
enum class BoundPower : uint8_t {
	None,
	/** More life, in percent. */
	Life,
	/** Harder hits, in percent. */
	Damage,
	/** Faster: extra turns, in percent of a turn. */
	Speed,
	/** Its melee hits add this percent of the hit as fire, cold or lightning. */
	FireHit,
	ColdHit,
	LightningHit,
	/** More resistance, in percent. */
	FireResist,
	ColdResist,
	LightningResist,
	ElementalResist,
	ChaosResist,
	/** Armour as a kind with this many percent more. */
	Armour,
	/** An Evil essence: more life and harder hits, both by the value. */
	Evil,
	Count,
};

/** @brief The glow a power shows: a palette ramp (bound_demons.tsv's glow column). */
enum class BoundGlow : uint8_t {
	Red,
	Orange,
	Yellow,
	Blue,
	Beige,
	Gray,
	Count,
};

struct BoundDemonPower {
	BoundPower power = BoundPower::None;
	int value = 0;
	BoundGlow glow = BoundGlow::Red;
};

/** @brief Reads bound_demons.tsv; outside POE2 mode, or when it fails, no essence gives a power. */
void LoadBoundDemonPowers();
/** @brief The power the essence kind (an EssenceKind) gives; None for a kind the table leaves out. */
const BoundDemonPower &GetBoundDemonPower(uint8_t essenceKind);

[[nodiscard]] bool IsBoundDemon(const Monster &monster);
[[nodiscard]] bool IsEssenceDemon(const Monster &monster);

/**
 * @brief The essences (their item variants) the demon holds: one or two, worked out from its item seed and the act and
 * area level here, so every machine and every load gets the same ones. Empty for a monster that isn't a bound demon
 * or when no essences are loaded.
 */
std::vector<uint8_t> BoundDemonEssences(const Monster &monster);
/** @brief The same for an item seed, in the act and area level given (what BoundDemonEssences uses). */
std::vector<uint8_t> EssencesForSeed(uint32_t seed, int act, int areaLevel);
/** @brief The essences' names, like "Lesser Essence of Body, Essence of Ice"; empty for none. */
std::string BoundDemonEssenceText(const Monster &monster);

/**
 * @brief Makes the monster a bound demon: rare, in its crystal, with its essences' life. Called by the floor packs
 * when the floor is made, so it is the same on every machine.
 * @return Whether it holds any essences (when none are loaded it is left as it was).
 */
bool BindDemon(Monster &monster);

/** @brief Cracks the crystal on this machine: the glass sound, and in a second it fights. Nothing for a monster out of one. */
void BreakBoundCrystal(Monster &monster);
/** @brief A demon whose crystal broke before this machine loaded the level (the delta says): out of it, and awake. */
void ReleaseBoundDemonOnLoad(Monster &monster);
/** @brief The hero clicked a bound demon: tells everyone its crystal breaks. Only for the local hero. */
void RequestBreakBoundCrystal(const Player &player, const Monster &monster);

/**
 * @brief One game tick of a bound demon: the heartbeat in its crystal, and the second it stands after the crystal
 * breaks. @return Whether it does nothing else this tick (it is bound or still waking).
 */
bool ProcessBoundDemon(Monster &monster);
/** @brief Whether its speed gives it a second turn this tick (adds up its speed like the map's haste). */
bool BoundDemonExtraTick(Monster &monster);

/** @brief Percent more damage its hits deal. */
[[nodiscard]] int BoundDemonDamagePercent(const Monster &monster);
/** @brief Resistance its essences add to the element. */
[[nodiscard]] int BoundDemonResistance(const Monster &monster, DamageType type);
/** @brief Armour percent its essences add. */
[[nodiscard]] int BoundDemonArmourPercent(const Monster &monster);
/** @brief The elemental damage its melee hit on the hero adds, after the hero's resistances. */
[[nodiscard]] Fixed26_6 BoundDemonExtraHitDamage(const Monster &monster, const Player &player, Fixed26_6 hit);

/** @brief Drops the essences it held where it died; part of its loot, so made the same on every machine. */
void DropBoundDemonEssences(const Monster &monster, bool sendmsg);

/** @brief Draws a bound demon: the sigil, the monster dark red inside its pulsing crystal, and the chains. */
void DrawBoundDemon(const Surface &out, const Monster &monster, Point tileBufferPosition, Point spritePosition, ClxSprite sprite);
/** @brief The outline colour of a released demon, cycling through its essences' glows; nullopt for other monsters. */
std::optional<uint8_t> BoundDemonGlowColor(const Monster &monster);

} // namespace devilution
