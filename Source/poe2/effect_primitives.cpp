#include "poe2/effect_primitives.hpp"

#include "game_mode.hpp"
#include "player.h"
#include "poe2/shrines.hpp"

namespace devilution {

int GainAsExtraPercent(const Player &player, DamageType type)
{
	if (!gbIsPoe2)
		return 0;
	const ShrineBonuses shrines = GetShrineBonuses(player);
	switch (type) {
	case DamageType::Fire:
		return shrines.extraFirePercent;
	case DamageType::Lightning:
		return shrines.extraLightningPercent;
	case DamageType::Cold:
		return shrines.extraColdPercent;
	default:
		return 0;
	}
}

bool AvoidsAilments(const Player &player)
{
	return gbIsPoe2 && GetShrineBonuses(player).avoidAilments;
}

} // namespace devilution
