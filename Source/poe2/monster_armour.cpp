#include "poe2/monster_armour.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include <magic_enum/magic_enum.hpp>

#include "data/file.hpp"
#include "data/record_reader.hpp"
#include "game_mode.hpp"
#include "monster.h"
#include "player.h"
#include "poe2/area_levels.hpp"
#include "poe2/map_mods.hpp"
#include "poe2/monster_levels.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/support_table.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/unique_effects.hpp"
#include "poe2/waystones.hpp"
#include "tables/monstdat.h"
#include "utils/language.h"
#include "utils/log.hpp"

namespace devilution {

namespace {

/** By _monster_id, in percent of the level table; kinds not listed have none. */
std::unordered_map<int, int> ArmourPercents;

constexpr int MaxKindArmourPercent = 300;

} // namespace

void LoadMonsterArmour()
{
	ArmourPercents.clear();
	if (!gbIsPoe2)
		return;
	constexpr std::string_view Filename = "txtdata\\poe2\\monster_armour.tsv";
	auto dataFile = DataFile::load(Filename);
	if (!dataFile.has_value()) {
		// No monster has armour without the table.
		LogError("Couldn't load the monster armour: {}", Filename);
		return;
	}
	dataFile->skipHeaderOrDie(Filename);
	for (DataFileRecord record : *dataFile) {
		RecordReader reader { record, Filename };
		std::string name;
		int percent = 0;
		reader.readString("type", name);
		reader.readInt("armourPercent", percent);
		const std::optional<_monster_id> type = magic_enum::enum_cast<_monster_id>(name);
		if (!type) {
			LogError("Unknown monster type {} in {}", name, Filename);
			continue;
		}
		ArmourPercents[static_cast<int>(*type)] = std::clamp(percent, 0, MaxKindArmourPercent);
	}
}

int MonsterTypeArmourPercent(int monsterType)
{
	if (!gbIsPoe2)
		return 0;
	const auto it = ArmourPercents.find(monsterType);
	return it != ArmourPercents.end() ? it->second : 0;
}

int MonsterArmourPercent(const Monster &monster)
{
	if (!gbIsPoe2 || monster.isPlayerMinion() || (monster.flags & MFLAG_GOLEM) != 0 || (monster.flags & MFLAG_POE2_TABLES) == 0)
		return 0;
	int percent = MonsterTypeArmourPercent(static_cast<int>(monster.type().type));
	// Plated: armour even on a kind that has none, and twice a knight's.
	if (GetMonsterAffixes(monster).has(MonsterMod::Plated))
		percent = std::max(percent, PlatedModMinimumPercent) * PlatedModMultiplier;
	// The waystone's "Armoured: monsters have X% increased armour" (nothing on a kind without armour, as in POE2).
	percent = percent * (100 + ActiveMapEffects()[MapDanger::MonsterArmour]) / 100;
	return std::max(percent, 0);
}

int MonsterArmourRating(const Monster &monster)
{
	// Fully broken armour counts as none (Curtis: break drops it to 0 and keeps the +25% physical damage taken).
	if (monster.poe2.armourBreakTicks > 0)
		return 0;
	const int percent = MonsterArmourPercent(monster);
	if (percent <= 0)
		return 0;
	const int level = Poe2MonsterLevel(monster, sgGameInitInfo.nDifficulty);
	if (level <= 0)
		return 0;
	const int64_t armour = static_cast<int64_t>(MonsterLevelArmour(level)) * percent / 100 * ArmourScalePercent / 100;
	return static_cast<int>(std::clamp<int64_t>(armour, 0, std::numeric_limits<int32_t>::max() / 64));
}

int ArmourReductionPercentCapped(int armour, int64_t rawHit, int capPercent)
{
	if (armour <= 0 || rawHit <= 0)
		return 0;
	// Armour and the hit in the same units: armour * 64 against the raw hit.
	const int64_t scaledArmour = static_cast<int64_t>(armour) * 64;
	const int64_t hit = std::min<int64_t>(rawHit, std::numeric_limits<int64_t>::max() / 64);
	const int percent = static_cast<int>(scaledArmour * 100 / (scaledArmour + (static_cast<int64_t>(ArmourHitRatio) * hit)));
	return std::min(percent, capPercent);
}

int MonsterArmourReductionPercent(const Monster &monster, int64_t rawHit)
{
	return ArmourReductionPercentCapped(MonsterArmourRating(monster), rawHit, MaxMonsterArmourReductionPercent);
}

int64_t MonsterHitDamage(const Player &player, const Monster &monster, DamageType type, int64_t raw, ArmourHit armour)
{
	if (!gbIsPoe2)
		return raw;
	// The damage taken first: it makes Brutality's one roll for this packet.
	const MonsterDamageTaken taken = MonsterDamageTakenParts(monster, type);
	const bool ignore = armour == ArmourHit::Ignore || (TakeHitIgnoresArmour() && armour == ArmourHit::Hit);
	const bool local = &player == MyPlayer;
	int reduction = 0;
	if (type == DamageType::Physical && raw > 0 && !ignore && !monster.isPlayerMinion()) {
		// POE2 works the reduction out from the hit after the attacker's more and less, before the monster's damage taken.
		reduction = MonsterArmourReductionPercent(monster, raw * taken.attackerPercent / 100);
		if (local && TelemetryDeepDamage())
			TelemetryHitStep(false, "monster armour", reduction, true, true);
	}
	if (type == DamageType::Physical && local)
		NoteHitArmourPercent(monster, reduction);
	const int64_t stopped = raw * reduction / 100;
	const int64_t dealt = std::clamp<int64_t>((raw - stopped) * taken.percent / 100, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max());
	if (local && stopped > 0)
		TelemetryArmourStopped(monster, stopped * taken.percent / 100);
	return dealt;
}

std::string_view ArmourTierText(int percent)
{
	if (percent <= 0)
		return {};
	if (percent <= LightArmourMaxPercent)
		return _("Light armour");
	if (percent <= ArmouredMaxPercent)
		return _("Armoured");
	return _("Heavy armour");
}

std::string_view MonsterArmourText(const Monster &monster)
{
	if (!gbIsPoe2)
		return {};
	if (monster.poe2.armourBreakTicks > 0 && !monster.isPlayerMinion())
		return _("Armour broken");
	return ArmourTierText(MonsterArmourPercent(monster));
}

} // namespace devilution
