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
#include <string>
#include <string_view>
#include <vector>

#include "engine/world_tile.hpp"
#include "poe2/player_data.hpp"
#include "tables/spelldat.h"

namespace devilution {

struct Item;
struct ItemData;
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
	/** The skill already has a Power support, and a skill takes only one. */
	PowerTaken,
	/** The skill has a support that rules this one out. */
	Conflict,
	/** A lineage support can be in use on only one skill. */
	LineageInUse,
	/** The socket holds a spell inside a meta gem. */
	SpellInSocket,
};

/** @brief The support gem an item represents, or SupportGem::None. */
SupportGem SupportGemForItem(const Item &item);
/** @brief Flat-damage supports are the Power family; a skill takes only one of them. */
bool IsPowerSupport(SupportGem gem);
/** @brief The Power support already in a skill's sockets other than the given one, if any. */
SupportGem OtherPowerSupport(const SupportSockets &supports, std::optional<size_t> exceptSocket);
SupportGem SupportGemForMiscId(int miscId);
/** @brief The support gem an item base is, including the table supports, or SupportGem::None. */
SupportGem SupportGemForItemData(const ItemData &data);
std::string_view SupportGemName(SupportGem gem);
std::string_view SupportGemDescription(SupportGem gem);
/** @brief What a tier of the support does. */
std::string_view SupportGemDescription(SupportGem gem, int tier);

/** @brief Makes a fresh support gem item of a tier (1 to 3), e.g. when a linked gem is swapped back out. */
bool CreateSupportGemItem(Item &item, SupportGem gem, int tier = 1);
/** @brief The tier of a support gem item, 1 to 3. */
int SupportGemItemTier(const Item &item);
/** @brief Gives the support gem item its tier and the name that goes with it ("Brutality II Support"). */
void SetSupportGemItemTier(Item &item, int tier);
/** @brief Lineage supports have no tiers. */
bool SupportGemHasTiers(SupportGem gem);
/** @brief "I", "II" or "III". */
std::string_view SupportTierNumeral(int tier);
/** @brief "Brutality II"; just the name at tier I. */
std::string SupportGemTierName(SupportGem gem, int tier);

/** @brief A support gem and its tier. */
struct TieredSupport {
	SupportGem gem = SupportGem::None;
	int tier = 1;
};

GemColor SupportGemColor(SupportGem gem);
std::string_view GemColorName(GemColor color);

/** @brief What kind of skill the gem fits, e.g. "Needs a melee attack"; empty for gems that fit any skill. */
std::string_view SupportGemNeeds(SupportGem gem);
/** @brief Whether the gem does anything for the given skill. */
bool SupportGemCanSupport(SupportGem gem, SkillSlot skill);
inline bool SupportGemCanSupport(SupportGem gem, SpellID spell) { return SupportGemCanSupport(gem, SkillSlot::ForSpell(spell)); }
/** @brief Whether the skill takes support gems at all (Spirit gems don't). */
bool SkillTakesSupports(SkillSlot skill);

/** @brief Support sockets every skill starts with; Jeweller's Orbs open the rest. */
constexpr size_t BaseSupportSockets = 2;
/** @brief Support sockets open on the skill in a Skills panel row: 2, plus those Jeweller's Orbs opened. */
size_t SupportSocketCount(const Player &player, size_t row);
/** @brief Sockets heroes from before Jeweller's Orbs had opened by level (one at 10, 20 and 30), which they keep. */
uint8_t LevelOpenedSockets(int characterLevel);

/**
 * @brief Sockets a support gem into a skill in the Skills panel.
 * @param socket The socket to fill; the first free open socket if empty.
 * @param replaced Set to the support that was in the socket, if any.
 * @param tier The tier of the gem being socketed.
 * @param replacedTier If given, set to the tier of the support that was in the socket.
 */
SupportSocketResult SocketSupportGem(Player &player, size_t slot, std::optional<size_t> socket, SupportGem gem, SupportGem &replaced, int tier = 1, int *replacedTier = nullptr);
/** @brief The first Skills panel socket whose skill has a free socket the gem can go in. */
std::optional<size_t> FindSlotForSupport(const Player &player, SupportGem gem);
/** @brief Takes a support out of a socket. @param tier If given, set to the support's tier. */
SupportGem UnsocketSupportGem(Poe2PlayerData &data, size_t slot, size_t socket, int *tier = nullptr);
/** @brief Takes every support out of a slot, e.g. when its skill gem is taken out. */
std::vector<TieredSupport> TakeSlotSupports(Poe2PlayerData &data, size_t slot);

/** @brief Extra spell levels the spell's supports add. */
int SupportSpellLevelBonus(const Player &player, SpellID spell);
/** @brief Mana cost multiplier in percent from the skill's supports. */
/** @param lifeShare false for the full cost, before Blood Price and the like move part of it to life. */
int SupportManaCostPercent(const Player &player, SkillSlot skill, bool lifeShare = true);
inline int SupportManaCostPercent(const Player &player, SpellID spell, bool lifeShare = true) { return SupportManaCostPercent(player, SkillSlot::ForSpell(spell), lifeShare); }
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
/** @brief The skill of the attack in progress: the weapon skill, or the weapon's default attack when there is none. */
SkillSlot CurrentAttackSkill(const Player &player);
/** @brief The skill of a Skills panel row: the gem in a socket, or the default attack for the DefaultAttackRow. */
SkillSlot RowSkill(const Player &player, size_t row);
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
