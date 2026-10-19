#include "poe2/belt_uniques.hpp"

#include <algorithm>

#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/charms.hpp"
#include "poe2/flasks.hpp"
#include "tables/itemdat.h"
#include "utils/format.hpp"
#include "utils/is_of.hpp"
#include "utils/str_cat.hpp"
#include "utils/utf8.hpp"

namespace devilution {

namespace {

using enum BeltUniqueId;
using Kind = BeltUniqueKind;

std::vector<BeltUnique> MakeTable()
{
	constexpr int16_t NoRoll = 0;
	return {
		{ RedMercy, "Red Mercy", Kind::LifeFlask, CharmEffect::None, 0, UniqueTier::Common, 12, "global", 8, 16, {}, NoRoll, NoRoll,
		    { "50% of Recovery is instant", "Remaining Recovery occurs over 6 seconds" } },
		{ LastDraught, "Last Draught", Kind::LifeFlask, CharmEffect::None, 1, UniqueTier::Common, 24, "global", 8, 16, {}, NoRoll, NoRoll,
		    { "Overhealing from this Flask: take 40% less damage for 4 seconds" } },
		{ SaintsBlood, "Saint's Blood", Kind::LifeFlask, CharmEffect::None, 2, UniqueTier::Uncommon, 36, "global", 9, 18, {}, NoRoll, NoRoll,
		    { "Recovers 20% more Life" } },
		{ BorrowedBreath, "Borrowed Breath", Kind::LifeFlask, CharmEffect::None, 3, UniqueTier::Uncommon, 52, "global", 9, 18, {}, NoRoll, NoRoll,
		    { "Recovery is doubled while on Low Life", "This Flask cannot be used above 70% Life" } },
		{ LongRecovery, "The Long Recovery", Kind::LifeFlask, CharmEffect::None, 4, UniqueTier::Rare, 70, "global", 10, 20, {}, NoRoll, NoRoll,
		    { "40% of Life recovered is also granted to your nearby Minions" } },
		{ Heartglass, "Heartglass", Kind::LifeFlask, CharmEffect::None, 5, UniqueTier::Rare, 80, "global", 10, 20, {}, NoRoll, NoRoll,
		    { "If this Flask has at least 80% Charges, lethal damage instead leaves you at 1 Life and empties the Flask, 60 second cooldown", "50% less Recovery Amount" } },
		{ GodsLastBreath, "God's Last Breath", Kind::LifeFlask, CharmEffect::None, 5, UniqueTier::Rare, 80, "the_starved_god", NoRoll, NoRoll,
		    { { UniqueRule::ResourceSpentDamage, 30 }, { UniqueRule::ResourceLock, 1 } }, 8, 12,
		    { "Works from the belt:", "For every 100 total resources spent recently, gain 2% more Damage, maximum 30%", "You cannot recover resources for 1 second after reaching the 30% bonus" } },
		{ BlueSilence, "Blue Silence", Kind::ManaFlask, CharmEffect::None, 0, UniqueTier::Common, 12, "global", 8, 16, {}, NoRoll, NoRoll,
		    { "40% of Recovery is instant" } },
		{ ScholarsReserve, "Scholar's Reserve", Kind::ManaFlask, CharmEffect::None, 1, UniqueTier::Common, 24, "global", 8, 16, {}, NoRoll, NoRoll,
		    { "Mana recovered above maximum grants Spell Damage for 6 seconds, 1% per 20 Mana, maximum 25%" } },
		{ DeepWell, "Deep Well", Kind::ManaFlask, CharmEffect::None, 2, UniqueTier::Uncommon, 36, "global", 9, 18, {}, NoRoll, NoRoll,
		    { "After use, your next Skill costs no Mana", "50% more Charges used" } },
		{ BorrowedThought, "Borrowed Thought", Kind::ManaFlask, CharmEffect::None, 3, UniqueTier::Uncommon, 52, "global", 9, 18, {}, NoRoll, NoRoll,
		    { "Recovery is doubled while below 30% Mana" } },
		{ LastIdea, "The Last Idea", Kind::ManaFlask, CharmEffect::None, 4, UniqueTier::Rare, 70, "global", 10, 20, {}, NoRoll, NoRoll,
		    { "40% of Mana recovered is also granted to nearby allies (in multiplayer)" } },
		{ Moonwater, "Moonwater", Kind::ManaFlask, CharmEffect::None, 5, UniqueTier::Rare, 80, "global", 10, 20, {}, NoRoll, NoRoll,
		    { "During Flask effect, Mana Recovery also recovers Energy Shield at 50% value", "You cannot gain Charges for Life Flasks during this effect" } },
		{ LittleGuardian, "Little Guardian", Kind::Charm, CharmEffect::Fire, 0, UniqueTier::Common, 8, "global", 10, 20, {}, NoRoll, NoRoll,
		    { "After this Charm ends, keep 40% of its Resistance for 3 seconds" } },
		{ SecondBell, "The Second Bell", Kind::Charm, CharmEffect::Lightning, 0, UniqueTier::Common, 20, "global", 10, 20, {}, NoRoll, NoRoll,
		    { "Every third activation consumes no Charges" } },
		{ QuietWard, "Quiet Ward", Kind::Charm, CharmEffect::Mana, 0, UniqueTier::Uncommon, 36, "global", 11, 23, {}, NoRoll, NoRoll,
		    { "50% of this Charm's effect also applies to nearby allies (in multiplayer)" } },
		{ Witchglass, "Witchglass", Kind::Charm, CharmEffect::Stun, 0, UniqueTier::Uncommon, 52, "global", 11, 23, {}, NoRoll, NoRoll,
		    { "When this Charm activates, gain 15% more Damage for 3 seconds", "25% reduced Charm Duration" } },
		{ SaintsToken, "Saint's Token", Kind::Charm, CharmEffect::Guard, 0, UniqueTier::Rare, 68, "global", 13, 26, {}, NoRoll, NoRoll,
		    { "If this Charm would activate while empty, it activates anyway and rests for 30 seconds" } },
		{ LastWarning, "The Last Warning", Kind::Charm, CharmEffect::Guard, 0, UniqueTier::Mythic, 80, "global", 15, 30, {}, NoRoll, NoRoll,
		    { "This Charm also breaks Stuns", "30% increased Charges used" } },
		{ FrozenStep, "Frozen Step", Kind::Charm, CharmEffect::Guard, 0, UniqueTier::Rare, 60, "the_white_hart", NoRoll, NoRoll,
		    { { UniqueRule::FreezeStoresChill, 30 }, { UniqueRule::CannotIgnite, 1 } }, 8, 12,
		    { "Works from the belt:", "Freezing an enemy stores its Chill, your next Hit consumes it for up to 30% more Damage", "You cannot Ignite" } },
	};
}

uint32_t Hash(uint32_t seed, uint32_t salt)
{
	uint32_t hash = seed ^ salt;
	hash ^= hash >> 16;
	hash *= 0x7FEB352DU;
	hash ^= hash >> 15;
	hash *= 0x846CA68BU;
	hash ^= hash >> 16;
	return hash;
}

int Roll(const Item &item, int min, int max, uint32_t salt)
{
	const int span = max - min + 1;
	return min + (span > 1 ? static_cast<int>(Hash(static_cast<uint32_t>(item._iSeed), salt) % static_cast<uint32_t>(span)) : 0);
}

bool IsBeltItem(const Item &item)
{
	return IsFlaskPotion(item) || IsCharm(item);
}

} // namespace

const std::vector<BeltUnique> &BeltUniques()
{
	static const std::vector<BeltUnique> Table = MakeTable();
	return Table;
}

const BeltUnique *GetBeltUnique(BeltUniqueId id)
{
	for (const BeltUnique &unique : BeltUniques()) {
		if (unique.id == id)
			return &unique;
	}
	return nullptr;
}

const BeltUnique *FindBeltUnique(std::string_view name)
{
	for (const BeltUnique &unique : BeltUniques()) {
		if (unique.name == name)
			return &unique;
	}
	return nullptr;
}

const BeltUnique *GetBeltUnique(const Item &item)
{
	if (!gbIsPoe2 || item.isEmpty() || !IsBeltItem(item))
		return nullptr;
	const auto id = static_cast<BeltUniqueId>((item.dwBuff & CF_POE2_BELT_UNIQUE) >> 6);
	if (id == BeltUniqueId::None || id >= BeltUniqueId::Count)
		return nullptr;
	return GetBeltUnique(id);
}

bool IsBeltUnique(const Item &item, BeltUniqueId id)
{
	const BeltUnique *unique = GetBeltUnique(item);
	return unique != nullptr && unique->id == id;
}

std::optional<int> BeltUniqueBase(const BeltUnique &unique)
{
	item_misc_id misc = IMISC_HEAL;
	if (unique.kind == Kind::ManaFlask) {
		misc = IMISC_MANA;
	} else if (unique.kind == Kind::Charm) {
		switch (unique.charm) {
		case CharmEffect::Life: misc = IMISC_CHARM_LIFE; break;
		case CharmEffect::Mana: misc = IMISC_CHARM_MANA; break;
		case CharmEffect::Guard: misc = IMISC_CHARM_GUARD; break;
		case CharmEffect::Fire: misc = IMISC_CHARM_FIRE; break;
		case CharmEffect::Lightning: misc = IMISC_CHARM_LIGHTNING; break;
		case CharmEffect::Magic: misc = IMISC_CHARM_MAGIC; break;
		case CharmEffect::Stun: misc = IMISC_CHARM_STUN; break;
		default: misc = IMISC_CHARM_GOLDEN; break;
		}
	}
	std::optional<int> base;
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		if (AllItemsList[i].iMiscId != misc)
			continue;
		// Prefer a base that drops.
		if (AllItemsList[i].dropRate > 0)
			return static_cast<int>(i);
		if (!base)
			base = static_cast<int>(i);
	}
	return base;
}

void MakeBeltUnique(Item &item, BeltUniqueId id)
{
	item.dwBuff = (item.dwBuff & ~CF_POE2_BELT_UNIQUE) | ((static_cast<uint32_t>(id) << 6) & CF_POE2_BELT_UNIQUE);
	if (IsFlaskPotion(item)) {
		// A unique flask has no other mods.
		item.dwBuff &= ~(CF_POE2_FLASK_PREFIX | CF_POE2_FLASK_SUFFIX | CF_POE2_FLASK_QUALITY);
		item.dwBuff |= CF_POE2 | CF_POE2_FLASK_ROLLED;
	}
	FinishBeltUnique(item);
}

bool FinishBeltUnique(Item &item)
{
	const BeltUnique *unique = GetBeltUnique(item);
	if (unique == nullptr)
		return false;
	item._iMagical = ITEM_QUALITY_UNIQUE;
	item._iIdentified = true;
	CopyUtf8(item._iIName, unique->name, ItemNameLength);
	return true;
}

int BeltUniqueEffect(const Item &item)
{
	const BeltUnique *unique = GetBeltUnique(item);
	if (unique == nullptr || unique->effectMax <= 0)
		return 0;
	return Roll(item, unique->effectMin, unique->effectMax, 0xB17EU);
}

int BeltUniqueResistance(const Item &item)
{
	const BeltUnique *unique = GetBeltUnique(item);
	if (unique == nullptr || unique->resistMax <= 0)
		return 0;
	return Roll(item, unique->resistMin, unique->resistMax, 0x2E51U);
}

std::vector<std::string> BeltUniqueText(const Item &item)
{
	std::vector<std::string> lines;
	const BeltUnique *unique = GetBeltUnique(item);
	if (unique == nullptr)
		return lines;
	if (const int effect = BeltUniqueEffect(item); effect > 0)
		lines.push_back(FormatRuntime(unique->kind == Kind::Charm ? "{:d}% increased Charm Duration" : "{:d}% increased Flask Effect", effect));
	for (const std::string_view line : unique->lines)
		lines.emplace_back(line);
	if (const int resist = BeltUniqueResistance(item); resist > 0)
		lines.push_back(FormatRuntime("+{:d}% to all Elemental Resistances", resist));
	return lines;
}

void AddBeltUniqueRules(const Item &item, UniqueRuleTotals &totals)
{
	const BeltUnique *unique = GetBeltUnique(item);
	if (unique == nullptr)
		return;
	for (const auto &[rule, value] : unique->rules) {
		int16_t &total = totals[static_cast<size_t>(rule)];
		total = static_cast<int16_t>(std::clamp(total + value, -30000, 30000));
	}
}

int BeltUniqueChargePercent(const Item &item)
{
	if (IsBeltUnique(item, DeepWell))
		return 150;
	if (IsBeltUnique(item, LastWarning))
		return 130;
	return 100;
}

bool TryHeartglassSave(Player &player)
{
	if (!gbIsPoe2 || player.poe2.uniqueState.heartglassCooldown > 0)
		return false;
	for (size_t slot = 0; slot < NumFlasks; slot++) {
		const Item &flask = player.SpdList[slot];
		if (!IsBeltUnique(flask, Heartglass))
			continue;
		uint8_t &charges = player.poe2.flaskCharges[slot];
		if (charges * 100 < MaxFlaskChargesFor(flask) * HeartglassChargePercent)
			continue;
		charges = 0;
		player.poe2.uniqueState.heartglassCooldown = HeartglassCooldownTicks;
		player._pHitPoints = Fixed26_6::fromInt(1);
		player._pHPBase = player._pMaxHPBase - player._pMaxHP + player._pHitPoints;
		return true;
	}
	return false;
}

} // namespace devilution
