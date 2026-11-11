/**
 * @file poe2/hit_trace.hpp
 *
 * POE2 Mashup hit trace: follows each of the hero's spell projectiles from cast to end and writes
 * one line per projectile to the play log (where it flew, which monsters it passed, why a hit was
 * refused, the damage it dealt and how it ended), so "my spell went through the monster" can be
 * traced from the log. hittrace(true) in the console also logs every tile it entered.
 */
#pragma once

#include <string_view>

#include "engine/point.hpp"

namespace devilution {

struct Missile;
struct Monster;

/** @brief Whether every tile a traced projectile enters is logged too, not just its summary. */
void HitTraceSetDetail(bool detail);
[[nodiscard]] bool HitTraceDetail();

/** @brief The projectile is about to check a tile it entered. */
void HitTraceTile(const Missile &missile, Point tile);
/** @brief The projectile was stopped by a wall or other blocking tile. */
void HitTraceWall(const Missile &missile, Point tile);
/** @brief The projectile has been removed; writes its summary line. */
void HitTraceEnd(const Missile &missile);

/** @brief While alive, hits and refusals are counted against this projectile. */
class HitTraceScope {
public:
	explicit HitTraceScope(const Missile &missile);
	~HitTraceScope();
	HitTraceScope(const HitTraceScope &) = delete;
	HitTraceScope &operator=(const HitTraceScope &) = delete;

private:
	const Missile *previous_;
};

/** @brief The projectile reached a monster but didn't hit it, and why. */
void HitTraceRefused(const Monster &monster, std::string_view reason);
/** @brief The projectile hit a monster for this much damage (in 1/64 life). */
void HitTraceHit(const Monster &monster, int damage, bool resisted);

} // namespace devilution
