/**
 * @file poe2/support_gems.hpp
 *
 * POE2 Mashup support gems: items that link to a spell and change how it behaves.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "engine/world_tile.hpp"
#include "poe2/player_data.hpp"
#include "tables/spelldat.h"

namespace devilution {

struct Item;
struct Missile;
struct Monster;
struct Player;

/** @brief Extra monsters a Pierce-supported projectile passes through. */
constexpr int8_t PierceCount = 2;
/** @brief Times a Chain-supported projectile jumps to a new monster. */
constexpr int8_t ChainCount = 2;
/** @brief Furthest a chain looks for its next target, in tiles. */
constexpr int ChainRange = 7;
/** @brief Angle of each fork either side of the projectile's path. */
constexpr int ForkDegrees = 30;
constexpr int ConcentratedEffectDamagePercent = 130;
constexpr int FasterProjectilesSpeedPercent = 160;

constexpr int BrutalityDamagePercent = 125;
constexpr int8_t MartialTempoFrames = 2;

// Like POE2's supports, most carry a price beyond their mana: damage, speed or a damage type.
/** @brief Damage of a spell with Multiple Projectiles: 35% less. */
constexpr int MultipleProjectilesDamagePercent = 65;
/** @brief Cast speed Multiple Projectiles takes away, in percent. */
constexpr int MultipleProjectilesSlowPercent = 20;
/** @brief Cast speed Empower takes away, in percent. */
constexpr int EmpowerSlowPercent = 10;
/** @brief Damage of a projectile once it has pierced a monster: 20% less. */
constexpr int PierceDamagePercent = 80;
/** @brief Damage of each fork: 30% less. */
constexpr int ForkDamagePercent = 70;
/** @brief Damage of every hit of a Chain-supported spell: 30% less. */
constexpr int ChainDamagePercent = 70;
/** @brief Speed of Concentrated Effect's heavier projectiles: 30% slower. */
constexpr int ConcentratedEffectSpeedPercent = 70;

/** @brief The attribute a support gem draws on, like POE2's red, green and blue gems. */
enum class GemColor : uint8_t {
	Strength,
	Dexterity,
	Magic,
};

enum class SupportSocketResult : uint8_t {
	Socketed,
	/** Socketed in place of another support, which comes back out. */
	Replaced,
	/** There's no skill gem in the slot. */
	NoSkill,
	NotApplicable,
	AlreadySocketed,
	/** The socket isn't open yet at the character's level. */
	Locked,
	/** Every open socket of the skill is taken. */
	NoFreeSocket,
	/** The character's attribute can't carry another support of this color. */
	NoCapacity,
};

/** @brief The support gem an item represents, or SupportGem::None. */
SupportGem SupportGemForItem(const Item &item);
SupportGem SupportGemForMiscId(int miscId);
std::string_view SupportGemName(SupportGem gem);
std::string_view SupportGemDescription(SupportGem gem);

/** @brief Makes a fresh support gem item, e.g. when a linked gem is swapped back out. */
bool CreateSupportGemItem(Item &item, SupportGem gem);

GemColor SupportGemColor(SupportGem gem);
std::string_view GemColorName(GemColor color);

/** @brief What kind of skill the gem fits, e.g. "Needs a melee attack"; empty for gems that fit any skill. */
std::string_view SupportGemNeeds(SupportGem gem);
/** @brief Whether the gem does anything for the given skill. */
bool SupportGemCanSupport(SupportGem gem, SkillSlot skill);
inline bool SupportGemCanSupport(SupportGem gem, SpellID spell) { return SupportGemCanSupport(gem, SkillSlot::ForSpell(spell)); }
/** @brief Whether the skill takes support gems at all (Spirit gems don't). */
bool SkillTakesSupports(SkillSlot skill);

/** @brief Support sockets open on every skill: 2, plus one at levels 10, 20 and 30. */
size_t SupportSocketCount(int characterLevel);
size_t SupportSocketCount(const Player &player);

/** @brief Points of an attribute that allow one more support of its color: each +5 attribute pick on the tree. */
constexpr int SupportAttributeStep = 5;

/** @brief How many supports of a color the character can socket: 1, plus 1 per 5 points of the attribute. */
int SupportCapacity(const Player &player, GemColor color);
int SupportsUsed(const Poe2PlayerData &data, GemColor color);

/**
 * @brief Sockets a support gem into a skill in the Skills panel.
 * @param socket The socket to fill; the first free open socket if empty.
 * @param replaced Set to the support that was in the socket, if any.
 */
SupportSocketResult SocketSupportGem(Player &player, size_t slot, std::optional<size_t> socket, SupportGem gem, SupportGem &replaced);
/** @brief The first Skills panel socket whose skill has a free socket the gem can go in. */
std::optional<size_t> FindSlotForSupport(const Player &player, SupportGem gem);
/** @brief Takes a support out of a socket. */
SupportGem UnsocketSupportGem(Poe2PlayerData &data, size_t slot, size_t socket);
/** @brief Takes every support out of a slot, e.g. when its skill gem is taken out. */
std::vector<SupportGem> TakeSlotSupports(Poe2PlayerData &data, size_t slot);

/** @brief Extra spell levels the spell's supports add. */
int SupportSpellLevelBonus(const Player &player, SpellID spell);
/** @brief Mana cost multiplier in percent from the skill's supports. */
int SupportManaCostPercent(const Player &player, SkillSlot skill);
inline int SupportManaCostPercent(const Player &player, SpellID spell) { return SupportManaCostPercent(player, SkillSlot::ForSpell(spell)); }
/** @brief Animation frames skipped when casting the spell. */
int8_t SupportCastSkippedFrames(const Player &player, SpellID spell);
/**
 * @brief Game ticks the spell's supports hold the character still after casting, like POE2's "less cast speed".
 * @param castFrames Length of the cast animation.
 */
int SupportCastRecoveryTicks(const Player &player, SpellID spell, int castFrames);
/** @brief Damage multiplier in percent the spell's supports put on every hit of the spell. */
int SupportSpellDamagePercent(const Player &player, SpellID spell);
/** @brief Additional projectiles fired with the spell. */
int SupportExtraProjectiles(const Player &player, SpellID spell);

/** @brief The weapon skill whose attack or animation is in progress. */
WeaponSkill CurrentWeaponSkill(const Player &player);
/** @brief Damage multiplier in percent for a melee hit, from the current skill's gem level and supports. */
int SupportMeleeDamagePercent(const Player &player);
/** @brief Whether a melee hit of the current skill knocks the monster back. */
bool SupportMeleeKnocksBack(const Player &player);
/** @brief Whether the current skill's supports take away the weapon's fire and lightning damage (Brutality). */
bool SupportMeleeIsPhysicalOnly(const Player &player);
/** @brief Animation frames skipped by the attack of the given weapon skill. */
int8_t SupportAttackSkippedFrames(const Player &player, WeaponSkill skill);

/**
 * @brief Rotates a target around a source, in steps of 15 degrees up to 45.
 *
 * Uses integer math so every machine in a multiplayer game computes the same tiles.
 */
WorldTilePosition RotateSpellTarget(WorldTilePosition src, WorldTilePosition dst, int degrees);

/** @brief Gives a freshly cast missile the pierce, fork, chain, damage and speed of the spell's supports. */
void ApplySupportsToMissile(Missile &missile, const Player &player, SpellID spell);
/** @brief Scales a hit's damage by the missile's supports. */
void ScaleSupportedMissileDamage(const Missile &missile, int &minDamage, int &maxDamage);
/** @brief Whether the missile already hit this monster and must pass through it instead. */
bool SupportedMissileIgnoresMonster(const Missile &missile, int monsterId);
/**
 * @brief Pierces, forks or chains after a projectile hits a monster directly.
 * @return true if the projectile keeps flying through the monster.
 */
bool OnSupportedMissileHit(Missile &missile, const Monster &monster);

} // namespace devilution
