#include "poe2/weapon_swap.hpp"

#include <bit>
#include <optional>
#include <utility>

#include "effects.h"
#include "game_mode.hpp"
#include "inv.h"
#include "items.h"
#include "levels/gendung.h"
#include "monster.h"
#include "msg.h"
#include "player.h"
#include "plrmsg.h"
#include "poe2/telemetry.hpp"
#include "poe2/weapon_skills.hpp"
#include "utils/format.hpp"
#include "utils/language.h"

namespace devilution {

namespace {

size_t SwapIndex(size_t handSlot)
{
	return handSlot == INVLOC_HAND_RIGHT ? 1 : 0;
}

} // namespace

Item &InactiveHand(Player &player, size_t handSlot)
{
	return player.swapHands[SwapIndex(handSlot)];
}

const Item &InactiveHand(const Player &player, size_t handSlot)
{
	return player.swapHands[SwapIndex(handSlot)];
}

bool CanSwapWeapons(const Player &player)
{
	if (!gbIsPoe2 || player.hasNoLife() || player.poe2.isDodging())
		return false;
	// Not in the middle of a swing or a cast, like POE2.
	return player._pmode == PM_STAND || player.isWalking();
}

bool SwapWeapons(Player &player)
{
	if (!CanSwapWeapons(player))
		return false;
	for (const size_t hand : { static_cast<size_t>(INVLOC_HAND_LEFT), static_cast<size_t>(INVLOC_HAND_RIGHT) })
		std::swap(player.InvBody[hand], InactiveHand(player, hand));
	player.poe2.weaponSet = static_cast<uint8_t>((player.poe2.weaponSet + 1) % NumWeaponSets);
	if (&player == MyPlayer) {
		for (const uint8_t hand : { static_cast<uint8_t>(INVLOC_HAND_LEFT), static_cast<uint8_t>(INVLOC_HAND_RIGHT) }) {
			if (player.InvBody[hand].isEmpty())
				NetSendCmdDelItem(false, hand);
			else
				NetSendCmdChItem(false, hand);
		}
		const Item &weapon = !player.InvBody[INVLOC_HAND_LEFT].isEmpty() ? player.InvBody[INVLOC_HAND_LEFT] : player.InvBody[INVLOC_HAND_RIGHT];
		if (!weapon.isEmpty())
			PlaySFX(ItemInvSnds[ItemCAnimTbl[weapon._iCurs]]);
	}
	CalcPlrInv(player, true);
	return true;
}

bool OtherWeaponSetAllowsSkill(const Player &player, WeaponSkill skill)
{
	return ItemAllowsWeaponSkill(InactiveHand(player, INVLOC_HAND_LEFT), skill) || ItemAllowsWeaponSkill(InactiveHand(player, INVLOC_HAND_RIGHT), skill);
}

std::string_view WeaponSetName(uint8_t set)
{
	return set == 0 ? "I" : "II";
}

int WeaponSetPointsTotal(const Player &player)
{
	return std::popcount(static_cast<unsigned>(player.poe2.specBooks)) * WeaponSetPointsPerBook;
}

int SpecBookIndex(const Item &item)
{
	if (item.isEmpty() || item._iMiscId != IMISC_SPEC_BOOK)
		return -1;
	// The books are listed in order in the item table.
	int book = 0;
	for (int i = 0; i < item.IDidx && i < static_cast<int>(AllItemsList.size()); i++) {
		if (AllItemsList[i].iMiscId == IMISC_SPEC_BOOK)
			book++;
	}
	return book < static_cast<int>(NumSpecBooks) ? book : -1;
}

std::string_view SpecBookSource(int book)
{
	switch (book) {
	case 0:
		return _("Taken from a lord of the Cathedral");
	case 1:
		return _("Taken from a lord of the Catacombs");
	case 2:
		return _("Taken from a lord of the Caves");
	case 3:
		return _("Taken from a lord of Hell");
	case 4:
		return _("Taken from the Arch-Bishop's circle");
	default:
		return _("Taken from a horror beyond Tristram");
	}
}

bool UseSpecBook(Player &player, const Item &item)
{
	const int book = SpecBookIndex(item);
	if (book < 0)
		return false;
	if ((player.poe2.specBooks & (1 << book)) != 0) {
		EventPlrMsg(_("You have already learned what this book teaches."), UiFlags::ColorRed);
		return false;
	}
	player.poe2.specBooks |= static_cast<uint8_t>(1 << book);
	EventPlrMsg(FormatRuntime(_("+{:d} Weapon Set Passive Points"), WeaponSetPointsPerBook), UiFlags::ColorWhitegold);
	TelemetryLog("weaponset", FormatRuntime("read book {:d}, now {:d} weapon set points", book, WeaponSetPointsTotal(player)));
	return true;
}

namespace {

/** @brief The book a boss guards; -1 for none. Quest bosses come first, and the first named monster of each area stands in when its quest isn't in this game. */
int SpecBookForMonster(const Monster &monster)
{
	switch (monster.uniqueType) {
	case UniqueMonsterType::SkeletonKing:
	case UniqueMonsterType::Butcher:
	case UniqueMonsterType::Garbud:
	case UniqueMonsterType::SnotSpill:
		return 0;
	case UniqueMonsterType::Zhar:
		return 1;
	case UniqueMonsterType::WarlordOfBlood:
		return 3;
	case UniqueMonsterType::Lazarus:
	case UniqueMonsterType::RedVex:
	case UniqueMonsterType::BlackJade:
		return 4;
	case UniqueMonsterType::HorkDemon:
	case UniqueMonsterType::Defiler:
	case UniqueMonsterType::NaKrul:
		return 5;
	case UniqueMonsterType::Lachdan:
		return -1;
	default:
		break;
	}
	if (!monster.isUnique() || leveltype == DTYPE_TOWN || setlevel)
		return -1;
	if (currlevel >= 3 && currlevel <= 4)
		return 0;
	if (currlevel >= 5 && currlevel <= 8)
		return 1;
	if (currlevel >= 9 && currlevel <= 12)
		return 2;
	if (currlevel >= 13 && currlevel <= 14)
		return 3;
	return -1;
}

std::optional<size_t> SpecBookItemIndex(int book)
{
	int found = 0;
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		if (AllItemsList[i].iMiscId != IMISC_SPEC_BOOK)
			continue;
		if (found == book)
			return i;
		found++;
	}
	return std::nullopt;
}

/** @brief Whether a copy of the book already lies on this level or in the hero's bags, so a second named monster doesn't drop another. */
bool SpecBookAround(const Player &player, int book)
{
	for (uint8_t i = 0; i < ActiveItemCount; i++) {
		if (SpecBookIndex(Items[ActiveItems[i]]) == book)
			return true;
	}
	for (int i = 0; i < player._pNumInv; i++) {
		if (SpecBookIndex(player.InvList[i]) == book)
			return true;
	}
	for (const Item &item : player.SpdList) {
		if (SpecBookIndex(item) == book)
			return true;
	}
	return SpecBookIndex(player.HoldItem) == book;
}

} // namespace

void DropSpecBook(const Monster &monster, bool sendmsg)
{
	if (!gbIsPoe2 || MyPlayer == nullptr || monster.isPlayerMinion())
		return;
	const int book = SpecBookForMonster(monster);
	if (book < 0 || (MyPlayer->poe2.specBooks & (1 << book)) != 0 || SpecBookAround(*MyPlayer, book))
		return;
	if (const std::optional<size_t> index = SpecBookItemIndex(book); index)
		SpawnBaseItem(monster.position.tile, static_cast<_item_indexes>(*index), sendmsg);
}

} // namespace devilution
