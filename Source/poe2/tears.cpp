#include "poe2/tears.hpp"

#include "effects.h"
#include "engine/backbuffer_state.hpp"
#include "msg.h"
#include "objects.h"
#include "stores.h"

namespace devilution {

namespace {

/** Where the open window's fountain stands, so the trade can use it up. */
WorldTilePosition TearsFountainPosition;

int StartingValue(const Player &player, CharacterAttribute attribute)
{
	const ClassAttributes &base = player.getClassAttributes();
	switch (attribute) {
	case CharacterAttribute::Strength: return base.baseStr;
	case CharacterAttribute::Magic: return base.baseMag;
	case CharacterAttribute::Dexterity: return base.baseDex;
	case CharacterAttribute::Vitality: return base.baseVit;
	}
	return 0;
}

void ModifyAttribute(Player &player, CharacterAttribute attribute, int delta)
{
	switch (attribute) {
	case CharacterAttribute::Strength: ModifyPlrStr(player, delta); break;
	case CharacterAttribute::Magic: ModifyPlrMag(player, delta); break;
	case CharacterAttribute::Dexterity: ModifyPlrDex(player, delta); break;
	case CharacterAttribute::Vitality: ModifyPlrVit(player, delta); break;
	}
}

} // namespace

bool TearsCanGive(const Player &player, CharacterAttribute attribute)
{
	return player.GetBaseAttributeValue(attribute) > StartingValue(player, attribute);
}

bool TearsCanTake(const Player &player, CharacterAttribute attribute)
{
	return player.GetBaseAttributeValue(attribute) < player.GetMaximumAttributeValue(attribute);
}

bool TearsTrade(Player &player, CharacterAttribute from, CharacterAttribute to)
{
	if (from == to || !TearsCanGive(player, from) || !TearsCanTake(player, to))
		return false;
	ModifyAttribute(player, from, -1);
	ModifyAttribute(player, to, 1);
	CheckStats(player);
	return true;
}

void OpenTearsFountain(const Object &fountain)
{
	TearsFountainPosition = fountain.position;
	StartStore(TalkID::Tears);
}

bool FinishTearsFountain(CharacterAttribute from, CharacterAttribute to)
{
	Object *fountain = FindObjectAtPosition(TearsFountainPosition);
	if (fountain == nullptr || fountain->_otype != OBJ_TEARFTN || !fountain->canInteractWith())
		return false;
	if (!TearsTrade(*MyPlayer, from, to))
		return false;
	PlaySfxLoc(SfxID::OperateFountain, fountain->position);
	fountain->selectionRegion = SelectionRegion::None;
	NetSendCmdLoc(MyPlayerId, false, CMD_OPERATEOBJ, fountain->position);
	RedrawEverything();
	return true;
}

} // namespace devilution
