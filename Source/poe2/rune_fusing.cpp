#include "poe2/rune_fusing.hpp"

#include <algorithm>
#include <array>

#include "effects.h"
#include "game_mode.hpp"
#include "inv.h"
#include "items.h"
#include "msg.h"
#include "player.h"
#include "plrmsg.h"
#include "poe2/stacking.hpp"
#include "poe2/stash_layouts.hpp"
#include "poe2/waystones.hpp"
#include "qol/stash.h"
#include "quests.h"
#include "stores.h"
#include "utils/format.hpp"
#include "utils/language.h"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

bool IsRuneStack(const Item &item, AugmentId id)
{
	return !item.isEmpty() && item._iMiscId == IMISC_AUGMENT && CurrentAugmentId(GetAugmentId(item)) == id;
}

/** @brief Takes up to count of the rune from the bag; returns how many it took. */
int TakeRunesFromBag(Player &player, AugmentId id, int count)
{
	int taken = 0;
	for (int i = player._pNumInv - 1; i >= 0 && taken < count; i--) {
		Item &item = player.InvList[i];
		if (!IsRuneStack(item, id))
			continue;
		const int have = StackCount(item);
		const int take = std::min(have, count - taken);
		taken += take;
		if (take >= have) {
			player.RemoveInvItem(i);
		} else {
			SetStackCount(item, have - take);
			NetSyncInvItem(player, i);
		}
	}
	return taken;
}

/** @brief Takes up to count of the rune from the stash; returns how many it took. */
int TakeRunesFromStash(AugmentId id, int count)
{
	int taken = 0;
	while (taken < count) {
		const auto found = std::find_if(Stash.stashList.begin(), Stash.stashList.end(), [id](const Item &item) { return IsRuneStack(item, id); });
		if (found == Stash.stashList.end())
			break;
		const auto index = static_cast<uint16_t>(found - Stash.stashList.begin());
		const Item part = TakeFromStash(index, count - taken);
		if (part.isEmpty())
			break;
		taken += StackCount(part);
	}
	return taken;
}

} // namespace

bool PerfectFusingOpen(const Player &player)
{
	// Diablo dead in this game, or by this hero on any difficulty (saved with the hero), or past the story: a harder
	// difficulty or an open map, where Perfect runes drop.
	return Quests[Q_DIABLO]._qactive == QUEST_DONE || player.pDiabloKillLevel > 0 || sgGameInitInfo.nDifficulty > DIFF_NORMAL
	    || player.poe2.mapTier > 0 || ActiveMapTier() > 0;
}

std::optional<RuneFusion> GetRuneFusion(RuneFamily family, RuneStrength from)
{
	if (family >= RuneFamily::Count || from >= RuneStrength::Perfect)
		return std::nullopt;
	const auto to = static_cast<RuneStrength>(static_cast<int>(from) + 1);
	RuneFusion fusion;
	fusion.family = family;
	fusion.from = from;
	fusion.input = RuneOf(family, from);
	fusion.output = RuneOf(family, to);
	if (fusion.input == AugmentId::None || fusion.output == AugmentId::None)
		return std::nullopt;
	const bool perfect = to == RuneStrength::Perfect;
	fusion.count = perfect ? RunesPerPerfectFusion : RunesPerFusion;
	fusion.gold = perfect ? PerfectFusionGold : 0;
	fusion.needsActThreeBoss = perfect;
	return fusion;
}

int OwnedRuneCount(const Player &player, AugmentId id)
{
	int count = 0;
	for (int i = 0; i < player._pNumInv; i++) {
		if (IsRuneStack(player.InvList[i], id))
			count += StackCount(player.InvList[i]);
	}
	// The stash is the local hero's alone.
	if (&player == MyPlayer) {
		for (const Item &item : Stash.stashList) {
			if (IsRuneStack(item, id))
				count += StoredStackCount(item);
		}
	}
	return count;
}

std::vector<RuneFusion> RuneFusionsToOffer(const Player &player)
{
	std::vector<RuneFusion> fusions;
	for (int family = 0; family < static_cast<int>(RuneFamily::Count); family++) {
		for (int from = 0; from < static_cast<int>(RuneStrength::Perfect); from++) {
			const std::optional<RuneFusion> fusion = GetRuneFusion(static_cast<RuneFamily>(family), static_cast<RuneStrength>(from));
			if (fusion && OwnedRuneCount(player, fusion->input) >= fusion->count)
				fusions.push_back(*fusion);
		}
	}
	return fusions;
}

std::string RuneFusionText(const RuneFusion &fusion)
{
	// Every rune's name ends in "Rune", so an s makes it plural.
	return FormatRuntime(_(/* TRANSLATORS: Cain's rune fusing: how many runes, their name, and the rune they make. */ "{:d} {:s}s: 1 {:s}"), fusion.count, GetAugment(fusion.input).name, GetAugment(fusion.output).name);
}

std::string_view RuneFusionRefusal(const Player &player, const RuneFusion &fusion)
{
	if (OwnedRuneCount(player, fusion.input) < fusion.count)
		return _("You don't have enough of those runes.");
	if (fusion.needsActThreeBoss && !PerfectFusingOpen(player))
		return _("Cain: Not until Diablo is dead. Then I'll make a Perfect rune.");
	if (fusion.gold > 0 && (&player != MyPlayer || !PlayerCanAfford(fusion.gold)))
		return _("You don't have enough gold.");
	return {};
}

bool FuseRunes(Player &player, RuneFamily family, RuneStrength from, uint32_t seed)
{
	const std::optional<RuneFusion> fusion = GetRuneFusion(family, from);
	if (!fusion)
		return false;
	if (const std::string_view refusal = RuneFusionRefusal(player, *fusion); !refusal.empty()) {
		EventPlrMsg(refusal, UiFlags::ColorRed);
		return false;
	}
	std::optional<_item_indexes> base;
	for (size_t i = 0; i < AllItemsList.size() && !base; i++) {
		if (AllItemsList[i].iMiscId == IMISC_AUGMENT && IsItemAvailable(static_cast<int>(i)))
			base = static_cast<_item_indexes>(i);
	}
	if (!base)
		return false;
	Item rune {};
	SetupAllItems(player, rune, *base, static_cast<int>(seed >> 1), 1, 1, /*onlygood=*/false, /*pregen=*/false);
	SetCraftingItemVariant(rune, static_cast<uint8_t>(fusion->output));
	FinishCraftingItem(rune);
	rune._iIdentified = true;
	rune._iStatFlag = true;

	const int fromBag = TakeRunesFromBag(player, fusion->input, fusion->count);
	if (fromBag < fusion->count && &player == MyPlayer)
		TakeRunesFromStash(fusion->input, fusion->count - fromBag);
	if (fusion->gold > 0)
		TakePlrsMoney(fusion->gold);
	// The bag first, then the stash, as the runes came; the hero's feet when neither has room.
	if (!AutoPlaceItemInInventory(player, rune) && !(&player == MyPlayer && AutoPlaceItemInAnyStashTab(rune)))
		GiveItemToPlayer(player, rune);
	CalcPlrInv(player, true);
	if (&player == MyPlayer) {
		PlaySFX(SfxID::ItemRock);
		EventPlrMsg(CainFusingLine(seed), UiFlags::ColorWhitegold);
		EventPlrMsg(StrCat(_("Fused: "), GetAugment(fusion->output).name));
	}
	return true;
}

std::string_view CainFusingLine(uint32_t seed)
{
	static const std::array<const char *, 4> Lines = {
		N_("Cain: There. One rune, and a stronger one."),
		N_("Cain: The old Horadric way still works."),
		N_("Cain: There. Mind the dust."),
		N_("Cain: Stone remembers. Use it well."),
	};
	const char *line = Lines[seed % Lines.size()];
	return LanguageTranslate(line);
}

} // namespace devilution
