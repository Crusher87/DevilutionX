#include "poe2/flasks.hpp"

#include <algorithm>

#include "effects.h"
#include "game_mode.hpp"
#include "inv.h"
#include "items.h"
#include "monster.h"
#include "player.h"
#include "plrmsg.h"
#include "poe2/charms.hpp"
#include "utils/is_of.hpp"
#include "utils/language.h"

namespace devilution {

bool IsFlaskPotion(const Item &item)
{
	return !item.isEmpty() && IsAnyOf(item._iMiscId, IMISC_HEAL, IMISC_FULLHEAL, IMISC_MANA, IMISC_FULLMANA, IMISC_REJUV, IMISC_FULLREJUV);
}

bool IsFlaskSlot(const Player &player, size_t beltSlot)
{
	return gbIsPoe2 && beltSlot < NumFlasks && IsFlaskPotion(player.SpdList[beltSlot]);
}

bool CanUseFlask(const Player &player, size_t beltSlot)
{
	return IsFlaskSlot(player, beltSlot) && player.poe2.flaskCharges[beltSlot] >= FlaskChargesPerUse;
}

bool UseFlask(Player &player, size_t beltSlot)
{
	if (!IsFlaskSlot(player, beltSlot))
		return false;
	if (!CanUseFlask(player, beltSlot)) {
		if (&player == MyPlayer)
			EventPlrMsg(_("That flask is empty. Kill monsters to refill it."), UiFlags::ColorRed);
		return false;
	}

	player.poe2.flaskCharges[beltSlot] -= FlaskChargesPerUse;
	const Item &flask = player.SpdList[beltSlot];
	if (&player == MyPlayer)
		PlaySFX(ItemInvSnds[ItemCAnimTbl[flask._iCurs]]);
	UseItem(player, flask._iMiscId, flask._iSpell, static_cast<int>(INVITEM_BELT_FIRST + beltSlot));
	return true;
}

uint8_t FlaskChargesForKill(const Monster &monster)
{
	return monster.isUnique() ? 10 : 2;
}

void GainFlaskCharges(Player &player, uint8_t charges)
{
	for (uint8_t &flaskCharges : player.poe2.flaskCharges)
		flaskCharges = static_cast<uint8_t>(std::min<int>(MaxFlaskCharges, flaskCharges + charges));
	GainCharmCharges(player, charges);
}

void RefillFlasks(Player &player)
{
	player.poe2.flaskCharges.fill(MaxFlaskCharges);
	RefillCharms(player);
}

} // namespace devilution
