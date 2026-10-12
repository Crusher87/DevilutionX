/**
 * @file poe2/support_gems.hpp
 *
 * POE2 Mashup support gems: items that link to a spell and change how it behaves.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

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
constexpr int ConcentratedEffectDamagePercent = 140;
constexpr int FasterProjectilesSpeedPercent = 160;

enum class SupportLinkResult : uint8_t {
	Linked,
	/** Linked, and the oldest support was pushed out to make room. */
	Replaced,
	NoReadiedSpell,
	NotApplicable,
	AlreadyLinked,
};

/** @brief The support gem an item represents, or SupportGem::None. */
SupportGem SupportGemForItem(const Item &item);
SupportGem SupportGemForMiscId(int miscId);
std::string_view SupportGemName(SupportGem gem);
std::string_view SupportGemDescription(SupportGem gem);

/** @brief Makes a fresh support gem item, e.g. when a linked gem is swapped back out. */
bool CreateSupportGemItem(Item &item, SupportGem gem);

/** @brief Whether the gem does anything for the given spell. */
bool SupportGemCanSupport(SupportGem gem, SpellID spell);

/**
 * @brief Links a support gem to a spell.
 * @param replaced Set to the support that was pushed out when both slots were full.
 */
SupportLinkResult LinkSupportGem(Poe2PlayerData &data, SpellID spell, SupportGem gem, SupportGem &replaced);

/** @brief Extra spell levels the spell's supports add. */
int SupportSpellLevelBonus(const Player &player, SpellID spell);
/** @brief Mana cost multiplier in percent from the spell's supports. */
int SupportManaCostPercent(const Player &player, SpellID spell);
/** @brief Animation frames skipped when casting the spell. */
int8_t SupportCastSkippedFrames(const Player &player, SpellID spell);
/** @brief Additional projectiles fired with the spell. */
int SupportExtraProjectiles(const Player &player, SpellID spell);

/**
 * @brief Rotates a target around a source, in steps of 15 degrees up to 45.
 *
 * Uses integer math so every machine in a multiplayer game computes the same tiles.
 */
WorldTilePosition RotateSpellTarget(WorldTilePosition src, WorldTilePosition dst, int degrees);

/** @brief Gives a freshly cast projectile the pierce, fork, chain, damage and speed of the spell's supports. */
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
