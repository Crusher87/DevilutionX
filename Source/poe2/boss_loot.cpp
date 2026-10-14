#include "poe2/boss_loot.hpp"

#include <algorithm>

#include "game_mode.hpp"
#include "levels/gendung_defs.hpp"
#include "monster.h"

namespace devilution {

const std::vector<BossLootTable> &BossLootTables()
{
	// Pools from the bundle, keeping only the uniques the game has: boots, gloves, belts and quivers have no slot yet,
	// and the Keeper of the Deep's chaos pool waits for chaos damage.
	static const std::vector<BossLootTable> Tables = {
		{ "saint_of_iron", "Saint of Iron", 18, { "The Butcher" }, false, { { "Iron Testament", 70 }, { "Hammer of Saints", 24 } } },
		{ "the_hollow_queen", "The Hollow Queen", 18, { "Skeleton King" }, false, { { "Mother's Veil", 70 }, { "Bone Choir", 24 }, { "Cradle of Teeth", 6 } } },
		{ "warden_of_embers", "Warden of Embers", 18, { "Madburner" }, false, { { "Cinder Crown", 70 }, { "Furnace Oath", 24 }, { "Ashen Circle", 6 } } },
		{ "storm_tyrant", "Storm Tyrant", 18, { "Stormbane", "Black Jade" }, false, { { "Tyrant's Spark", 24 } } },
		{ "the_white_hart", "The White Hart", 18, { "Nightwing the Cold", "Bluehorn" }, false, { { "Winterstring", 24 }, { "Frozen Step", 6 } } },
		{ "the_clockwork_judge", "The Clockwork Judge", 100, { "Warlord of Blood" }, false, { { "Clockface", 60 }, { "Final Verdict", 30 }, { "Second Hand", 10 } } },
		{ "mother_of_thorns", "Mother of Thorns", 100, { "The Defiler" }, false, { { "Thornheart", 60 }, { "Red Briar", 30 } } },
		{ "the_sunless_king", "The Sunless King", 100, { "Arch-Bishop Lazarus" }, false, { { "Crown Without Dawn", 60 }, { "Nightglass", 30 }, { "Sunless Seal", 10 } } },
		{ "oracle_of_glass", "Oracle of Glass", 100, { "Zhar the Mad" }, false, { { "Oracle's Lens", 60 }, { "Glass Thesis", 30 }, { "Prophecy Unmade", 10 } } },
		{ "the_last_colossus", "The Last Colossus", 100, { "Na-Krul" }, false, { { "Colossus Hide", 60 }, { "Worldhammer", 30 } } },
		{ "the_starved_god", "The Starved God", 100, {}, true, { { "Hunger Eternal", 60 }, { "Empty Cup", 30 }, { "God's Last Breath", 10 } } },
	};
	return Tables;
}

const BossLootTable *BossLootTableFor(const Monster &monster)
{
	const bool isDiablo = monster.type().type == MT_DIABLO;
	if (!monster.isUnique() && !isDiablo)
		return nullptr;
	// The untranslated name, so a translated game finds its boss too.
	const std::string_view name = monster.isUnique() ? std::string_view(UniqueMonstersData[static_cast<size_t>(monster.uniqueType)].mName) : std::string_view();
	for (const BossLootTable &table : BossLootTables()) {
		if (isDiablo && table.diabloOnHell && sgGameInitInfo.nDifficulty == DIFF_HELL)
			return &table;
		if (monster.isUnique() && std::find(table.monsters.begin(), table.monsters.end(), name) != table.monsters.end())
			return &table;
	}
	return nullptr;
}

} // namespace devilution
