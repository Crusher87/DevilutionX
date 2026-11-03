/**
 * @file poe2/effect_primitives.hpp
 *
 * POE2 Mashup: small effects that more than one system can grant (build spec step 4's primitives, the ones the
 * shrines need). Each adds up its sources; today those are the shrine buffs, and runes and essences can join later.
 */
#pragma once

#include <cstdint>

namespace devilution {

struct Player;
enum class DamageType : uint8_t;

/** @brief Gain this percent of each hit's damage as extra damage of the type (fire, lightning or cold). */
[[nodiscard]] int GainAsExtraPercent(const Player &player, DamageType type);
/** @brief Whether the hero avoids ailments: poison, bleeding and chilled ground leave them alone. */
[[nodiscard]] bool AvoidsAilments(const Player &player);

} // namespace devilution
