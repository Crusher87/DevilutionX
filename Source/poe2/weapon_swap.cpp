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
#include "quests.h"
#include "poe2/telemetry.hpp"
#include "poe2/unique_effects.hpp"
#include "poe2/weapon_skills.hpp"
#include "utils/format.hpp"
#include "utils/is_of.hpp"
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
	// The weapon type held before, for Knife in the Dark (a shield or empty hands hold none).
	ItemType before = ItemType::None;
	for (const Item *item : { &player.InvBody[INVLOC_HAND_RIGHT], &player.InvBody[INVLOC_HAND_LEFT] }) {
		if (!item->isEmpty() && IsAnyOf(item->_itype, ItemType::Sword, ItemType::Axe, ItemType::Mace, ItemType::Bow, ItemType::Staff))
			before = item->_itype;
	}
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
	OnWeaponsSwapped(player, before);
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

std::string_view SpecBookPlace(int book)
{
	switch (book) {
	case 0:
		return _("the Cathedral");
	case 1:
		return _("the Chamber of Bone");
	case 2:
		return _("the Caves");
	case 3:
		return _("Hell");
	case 4:
		return _("Lazarus's Lair");
	default:
		return _("the Hive");
	}
}

std::string_view SpecBookSource(int book)
{
	switch (book) {
	case 0:
		return _("Taken from a lord of the Cathedral or the Catacombs");
	case 1:
		return _("Found in the Chamber of Bone");
	case 2:
		return _("Taken from a lord of the Caves");
	case 3:
		return _("Taken from a lord of Hell");
	case 4:
		return _("Earned by ending Lazarus");
	default:
		return _("Earned by destroying the Defiler");
	}
}

std::string_view SpecBookWhere(int book)
{
	switch (book) {
	case 0:
		return _("Held by a lord of the Cathedral or the Catacombs");
	case 1:
		return _("Handed over when the Chamber of Bone is opened");
	case 2:
		return _("Held by a lord of the Caves");
	case 3:
		return _("Held by a lord of Hell");
	case 4:
		return _("Handed over when Lazarus dies");
	default:
		return _("Handed over when the Defiler dies");
	}
}

int ShelfSlotBook(size_t slot)
{
	// Act 1: the lord's Book, then the Chamber of Bone; Act 2: the Caves, then the Defiler; Act 3: Hell, then Lazarus.
	constexpr int Shelf[] = { 0, 1, 2, 5, 3, 4 };
	return slot < std::size(Shelf) ? Shelf[slot] : -1;
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
	case UniqueMonsterType::Zhar:
		return 0;
	case UniqueMonsterType::WarlordOfBlood:
		return 3;
	// Lazarus's and the Defiler's Books come with their quests (HandOutQuestBooks), and the Hive and Crypt hold no lord's Book.
	case UniqueMonsterType::Lazarus:
	case UniqueMonsterType::RedVex:
	case UniqueMonsterType::BlackJade:
	case UniqueMonsterType::HorkDemon:
	case UniqueMonsterType::Defiler:
	case UniqueMonsterType::NaKrul:
	case UniqueMonsterType::Lachdan:
		return -1;
	default:
		break;
	}
	if (!monster.isUnique() || leveltype == DTYPE_TOWN || setlevel)
		return -1;
	if (currlevel >= 3 && currlevel <= 8)
		return 0;
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

namespace {

/** @brief The quest whose end hands over each quest Book; single player always has these three. */
constexpr std::pair<quest_id, int> QuestBooks[] = { { Q_SCHAMB, 1 }, { Q_DEFILER, 5 }, { Q_BETRAYER, 4 } };

} // namespace

void HandOutQuestBooks(Player &player)
{
	if (!gbIsPoe2 || &player != MyPlayer)
		return;
	player.poe2.questsRewarded |= BooksOnlyFlag;
	for (const auto &[quest, book] : QuestBooks) {
		// The quest's bit says its Book was handed over: once per hero, across games.
		const uint32_t handed = 1U << quest;
		if (Quests[quest]._qactive != QUEST_DONE || (player.poe2.questsRewarded & handed) != 0)
			continue;
		player.poe2.questsRewarded |= handed;
		if ((player.poe2.specBooks & (1 << book)) != 0 || SpecBookAround(player, book))
			continue;
		// It lands at the hero's feet, so it waits on the ground when the bag is full.
		if (const std::optional<size_t> index = SpecBookItemIndex(book); index) {
			SpawnBaseItem(player.position.tile, static_cast<_item_indexes>(*index), true);
			TelemetryLog("weaponset", FormatRuntime("quest {:d} handed over book {:d}", static_cast<int>(quest), book));
		}
	}
}

int MigrateToBooks(Player &player)
{
	uint32_t &quests = player.poe2.questsRewarded;
	if ((quests & BooksOnlyFlag) != 0)
		return 0;
	const uint8_t old = player.poe2.specBooks;
	// Before Books only, each finished quest gave 2 points (24 at most) and the Cathedral and the Catacombs had a Book each.
	const int before = std::min(std::popcount(quests) * 2, 24) + std::popcount(static_cast<unsigned>(old)) * PassivePointsPerBook;
	auto had = [&](int bit) { return (old & (1 << bit)) != 0; };
	auto finished = [&](quest_id quest) { return (quests & (1U << quest)) != 0; };
	uint8_t books = 0;
	if (had(0) || had(1))
		books |= 1 << 0;
	if (finished(Q_SCHAMB))
		books |= 1 << 1;
	if (had(2))
		books |= 1 << 2;
	if (had(3))
		books |= 1 << 3;
	if (had(4) || finished(Q_BETRAYER))
		books |= 1 << 4;
	if (had(5) || finished(Q_DEFILER))
		books |= 1 << 5;
	player.poe2.specBooks = books;
	// A Book quest already finished has handed over its Book, so it isn't handed over again.
	quests = BooksOnlyFlag;
	for (const auto &[quest, book] : QuestBooks) {
		if ((books & (1 << book)) != 0)
			quests |= 1U << quest;
	}
	const int after = std::popcount(static_cast<unsigned>(books)) * PassivePointsPerBook;
	TelemetryLog("weaponset", FormatRuntime("moved to Books only: books {:d} to {:d}, points {:+d}", old, books, after - before));
	return after - before;
}

} // namespace devilution
