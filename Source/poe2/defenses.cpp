#include "poe2/defenses.hpp"

#include <algorithm>
#include <string_view>

#include "engine/backbuffer_state.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "levels/gendung_defs.hpp"
#include "monster.h"
#include "player.h"
#include "poe2/area_levels.hpp"
#include "poe2/bases.hpp"
#include "poe2/charms.hpp"
#include "poe2/flasks.hpp"
#include "poe2/meta_gems.hpp"
#include "poe2/monster_levels.hpp"
#include "poe2/spirit_auras.hpp"
#include "poe2/spirit_buffs.hpp"
#include "poe2/spirit_uses.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/unique_effects.hpp"
#include "utils/is_of.hpp"

namespace devilution {

namespace {

constexpr int TicksPerSecond = 20;

} // namespace

DefenseType ItemDefenseType(const Item &item)
{
	if (item.isEmpty() || item.IDidx < 0 || static_cast<size_t>(item.IDidx) >= AllItemsList.size())
		return DefenseType::Armour;
	if (const BaseMeta *meta = ItemBaseMeta(item); meta != nullptr && meta->defense)
		return *meta->defense;
	const std::string_view base = AllItemsList[static_cast<size_t>(item.IDidx)].iName;
	if (IsAnyOf(base, "Rags", "Cape", "Cloak", "Robe", "Cap", "Crown", "Twig Focus", "Woven Focus", "Crystal Focus"))
		return DefenseType::EnergyShield;
	if (IsAnyOf(base, "Quilted Armor", "Leather Armor", "Hard Leather Armor", "Studded Leather Armor", "Skull Cap", "Buckler"))
		return DefenseType::Evasion;
	if (IsAnyOf(base, "Ring Mail", "Chain Mail", "Scale Mail", "Helm", "Small Shield"))
		return DefenseType::ArmourEvasion;
	// POE2 body armour bases, one line per defense mix.
	if (IsAnyOf(base, "Leather Vest", "Pathfinder Coat", "Rhoahide Coat", "Scout's Vest", "Corsair Vest", "Hardleather Coat", "Armoured Vest"))
		return DefenseType::Evasion;
	if (IsAnyOf(base, "Tattered Robe", "Hexer's Robe", "Silk Robe", "Votive Raiment", "Elementalist Robe", "Plated Raiment", "Enlightened Robe"))
		return DefenseType::EnergyShield;
	if (IsAnyOf(base, "Chain Mail Vest", "Rogue Armour", "Cloaked Mail", "Scale Armour", "Knight Armour", "Gladiator Armour", "Heroic Armour"))
		return DefenseType::ArmourEvasion;
	if (IsAnyOf(base, "Pilgrim Vestments", "Pelt Mantle", "Mail Vestments", "Shaman Mantle", "Cleric Vestments", "Revered Vestments", "Corvus Mantle"))
		return DefenseType::ArmourEnergyShield;
	if (IsAnyOf(base, "Hermit Garb", "Waxed Jacket", "Marabout Garb", "Wayfarer Jacket", "Scalper's Jacket", "Monastic Garb", "Devout Garb"))
		return DefenseType::EvasionEnergyShield;
	if (base == "Grand Regalia")
		return DefenseType::All;
	return DefenseType::Armour;
}

BodyArmourImplicit GetBodyArmourImplicit(const Item &item)
{
	BodyArmourImplicit implicit;
	if (!gbIsPoe2 || item.isEmpty() || item.IDidx < 0 || static_cast<size_t>(item.IDidx) >= AllItemsList.size())
		return implicit;
	const std::string_view base = AllItemsList[static_cast<size_t>(item.IDidx)].iName;
	if (base == "Heroic Armour")
		implicit.life = 20;
	else if (base == "Corvus Mantle")
		implicit.spirit = 20;
	else if (base == "Revered Vestments")
		implicit.allResistances = 10;
	else if (base == "Devout Garb")
		implicit.magicResistance = 20;
	else if (base == "Enlightened Robe")
		implicit.manaRegenPercent = 30;
	else if (base == "Armoured Vest")
		implicit.evasionPercent = 15;
	return implicit;
}

void SplitDefense(DefenseType type, int defense, int &armour, int &evasion, int &energyShield, const DefenseSplit &split)
{
	armour = 0;
	evasion = 0;
	energyShield = 0;
	// The second kind's share of a hybrid's defence, rounded: the base's split, or half.
	const auto share = [defense](int percent) { return std::max((defense * percent) + 50, 0) / 100; };
	const int second = split.parts >= 1 ? share(100 - split.percent[0]) : defense / 2;
	switch (type) {
	case DefenseType::Armour:
		armour = defense;
		break;
	case DefenseType::Evasion:
		evasion = defense;
		break;
	case DefenseType::EnergyShield:
		energyShield = defense;
		break;
	case DefenseType::ArmourEvasion:
		evasion = second;
		armour = defense - evasion;
		break;
	case DefenseType::ArmourEnergyShield:
		energyShield = second;
		armour = defense - energyShield;
		break;
	case DefenseType::EvasionEnergyShield:
		energyShield = second;
		evasion = defense - energyShield;
		break;
	case DefenseType::All:
		if (split.parts >= 2) {
			evasion = share(split.percent[1]);
			energyShield = share(std::max(100 - split.percent[0] - split.percent[1], 0));
		} else {
			evasion = defense / 3;
			energyShield = defense / 3;
		}
		armour = defense - evasion - energyShield;
		break;
	}
}

namespace {

/**
 * @brief One kind of an item's defence after its local % increases and its quality, which POE2 multiplies, rounded
 * like POE2. As GetBonusAC, any change gives at least a point.
 */
int ScaledDefence(int defence, int increased, int quality)
{
	if (defence <= 0)
		return defence;
	const int scale = (100 + increased) * (100 + quality);
	const int scaled = std::max((defence * scale + 5000) / 10000, 0);
	if (scaled == defence && scale != 10000)
		return scale > 10000 ? defence + 1 : defence - 1;
	return scaled;
}

} // namespace

void SplitItemDefense(const Item &item, DefensePoints &base, DefensePoints &bonus)
{
	const auto flat = [&](Poe2ItemStat stat) { return std::max<int>(item._iPoe2Stats[static_cast<size_t>(stat)], 0); };
	DefensePoints flats { flat(Poe2ItemStat::FlatArmour), flat(Poe2ItemStat::FlatEvasion), flat(Poe2ItemStat::FlatEnergyShield) };
	const int split = std::max(item._iAC - flats.armour - flats.evasion - flats.energyShield, 0);
	SplitDefense(ItemDefenseType(item), split, base.armour, base.evasion, base.energyShield, ItemDefenseSplit(item, split));
	base.armour += flats.armour;
	base.evasion += flats.evasion;
	base.energyShield = (base.energyShield + flats.energyShield) * EnergyShieldPerDefense;
	// Each kind scales on its own and energy shield in the points shown, so a hybrid's energy shield and a small
	// robe's go up with quality too (Curtis 2026-10-23: 10% quality left a robe's 45 energy shield at 45).
	const int quality = LocalQuality(item, QualityKind::Defence);
	const int increased = item._iPLAC - quality;
	bonus.armour = ScaledDefence(base.armour, increased, quality) - base.armour;
	bonus.evasion = ScaledDefence(base.evasion, increased, quality) - base.evasion;
	bonus.energyShield = ScaledDefence(base.energyShield, increased, quality) - base.energyShield;
}

int ArmourRating(const Player &player, bool withSpiritBuffs)
{
	// Barkskin adds armour from energy shield lost, up to the gear's; Defiance Banner gives more.
	const int armour = std::max(player._pIAC + player._pIBonusAC, 0) + (withSpiritBuffs ? SpiritBuffArmour(player) : 0);
	return armour * (100 + FlaskArmourPercent(player)) / 100 * UniqueDefencePercent(player) / 100 * (100 + (withSpiritBuffs ? BannerDefencePercent(player) : 0)) / 100
	    * (100 + (withSpiritBuffs ? ScavengedArmourPercent(player) : 0)) / 100;
}

int EvasionRating(const Player &player, bool withSpiritBuffs)
{
	return std::max(player.poe2.evasion + player._pDexterity / 5, 0) * UniqueDefencePercent(player) / 100 * (100 + UnarmedEvasionPercent(player)) / 100
	    * (100 + (withSpiritBuffs ? SpiritBuffEvasionPercent(player) : 0)) / 100 * (100 + (withSpiritBuffs ? BannerDefencePercent(player) : 0)) / 100;
}

int HitDefense(const Player &player)
{
	return gbIsPoe2 ? EvasionRating(player) : player.GetArmor();
}

int ArmourReductionPercent(int armour, Fixed26_6 damage)
{
	if (armour <= 0 || damage <= 0)
		return 0;
	const int64_t hit = std::max<int64_t>(damage.raw(), 1);
	// armour and damage in the same units: armour * 64 against the raw hit.
	const int64_t scaledArmour = static_cast<int64_t>(armour) * 64;
	const int percent = static_cast<int>(scaledArmour * 100 / (scaledArmour + 10 * hit));
	return std::min(percent, MaxArmourReductionPercent);
}

Fixed26_6 ApplyArmour(const Player &player, Fixed26_6 damage)
{
	if (!gbIsPoe2)
		return damage;
	const int percent = ArmourReductionPercent(ArmourRating(player), damage);
	const Fixed26_6 stopped = Fixed26_6::fromRaw(damage.raw() * percent / 100);
	// Iron Ward stores what armour stops.
	OnSpiritArmourStopped(player, stopped);
	return Fixed26_6::fromRaw(damage.raw() - stopped.raw());
}

Fixed26_6 AbsorbWithEnergyShield(Player &player, Fixed26_6 damage)
{
	if (!gbIsPoe2 || damage <= 0)
		return damage;
	TelemetryHitStep(true, "after unique effects", damage.raw());
	damage = ApplyGuard(player, damage);
	TelemetryHitStep(true, "after guard", damage.raw());
	Poe2PlayerData &data = player.poe2;
	// Convalescence keeps hits from stopping the recharge for a while.
	if (!RechargeCantBeInterrupted(player))
		data.energyShieldRechargeTicks = 0;
	data.energyShieldRemainder = 0;
	const int32_t absorbed = std::min(data.energyShield, damage.raw());
	// Every hit that lands comes through here: Wind Dancer, Ghost Dance and Barkskin.
	OnSpiritHitTaken(player, Fixed26_6::fromRaw(std::max<int32_t>(absorbed, 0)));
	if (absorbed <= 0)
		return damage;
	data.energyShield -= absorbed;
	TelemetryShieldAbsorbed(player, Fixed26_6::fromRaw(absorbed));
	OnMetaShieldLost(player, Fixed26_6::fromRaw(absorbed));
	if (&player == MyPlayer)
		RedrawComponent(PanelDrawComponent::Health);
	TelemetryHitStep(true, "after energy shield", damage.raw() - absorbed);
	return Fixed26_6::fromRaw(damage.raw() - absorbed);
}

void RechargeEnergyShield(Player &player)
{
	Poe2PlayerData &data = player.poe2;
	if (!gbIsPoe2 || player.hasNoLife() || data.maxEnergyShield <= 0)
		return;
	const int32_t maxRaw = data.maxEnergyShield * 64;
	if (data.energyShield >= maxRaw) {
		data.energyShield = maxRaw;
		data.energyShieldRemainder = 0;
		return;
	}
	if (data.energyShieldRechargeTicks < EnergyShieldRechargeDelayTicks) {
		data.energyShieldRechargeTicks++;
		return;
	}
	// POE2 Mashup: some uniques slow or stop the recharge.
	const int rechargePercent = EnergyShieldRechargePercent(player);
	if (rechargePercent <= 0)
		return;
	constexpr int64_t Divisor = 100 * 1000 * TicksPerSecond;
	const int64_t total = static_cast<int64_t>(maxRaw) * EnergyShieldRechargePerMille * rechargePercent + data.energyShieldRemainder;
	data.energyShieldRemainder = static_cast<int>(total % Divisor);
	data.energyShield = std::min<int32_t>(data.energyShield + static_cast<int32_t>(total / Divisor), maxRaw);
	if (&player == MyPlayer)
		RedrawComponent(PanelDrawComponent::Health);
}

void SetMaxEnergyShield(Player &player, int maxEnergyShield)
{
	Poe2PlayerData &data = player.poe2;
	maxEnergyShield = std::max(maxEnergyShield, 0);
	const int32_t missing = std::max<int32_t>(data.maxEnergyShield * 64 - data.energyShield, 0);
	data.maxEnergyShield = maxEnergyShield;
	data.energyShield = std::clamp<int32_t>(maxEnergyShield * 64 - missing, 0, maxEnergyShield * 64);
}

int DeepestDungeonLevel(const Player &player)
{
	int deepest = player.plrIsOnSetLevel ? 0 : player.plrlevel;
	for (int level = 0; level < NUMLEVELS; level++) {
		if (player._pLvlVisited[level])
			deepest = std::max(deepest, level);
	}
	return deepest;
}

int ResistancePenalty(const Player &player)
{
	// Cathedral 1-4, Catacombs 5-8, Caves 9-12, then Hell, the Hive and the Crypt.
	const int area = std::min(DeepestDungeonLevel(player) > 0 ? (DeepestDungeonLevel(player) - 1) / 4 : 0, 3);
	const int difficulty = std::clamp(static_cast<int>(sgGameInitInfo.nDifficulty), 0, 2);
	return (area * ResistancePenaltyPerArea) + (difficulty * ResistancePenaltyPerDifficulty);
}

int Poe2Resistance(int resistance, int penalty, int maxResistance)
{
	return std::clamp(resistance - penalty, MinPoe2Resistance, maxResistance);
}

int AccuracyRating(const Player &player)
{
	const int accuracy = AccuracyPerLevel * player.getCharacterLevel() + AccuracyPerDexterity * player._pDexterity;
	return accuracy * (100 + BannerAccuracyPercent(player)) / 100;
}

namespace {

/** @brief The evasion of an ordinary monster of a level: about 1.16 times the accuracy of a hero of that level with ordinary Dexterity (20, and half a point more a level), so such a hero hits about 9 in 10 (Curtis 2026-10-21: his Warrior missed 1 attack in 4). */
int UsualEvasion(int level)
{
	return 140 + level * 21 / 2;
}

} // namespace

int MonsterEvasionRating(const Monster &monster, int armorClass)
{
	const int evasion = UsualEvasion(static_cast<int>(monster.level(sgGameInitInfo.nDifficulty)));
	// Diablo's armour class tells nimble and armoured kinds apart, against what's usual for the kind's level.
	const int usual = 3 * monster.data().level + 30;
	const int percent = std::clamp((armorClass + 30) * 100 / std::max(usual, 1), 60, 160);
	return evasion * percent / 100;
}

int TypicalHitChance(const Player &player)
{
	return std::clamp(Poe2HitChance(AccuracyRating(player), UsualEvasion(player.getCharacterLevel())) + player._pIBonusToHit, 5, 100);
}

int AccuracyAtDistancePercent(int tiles)
{
	if (tiles <= AccuracyFalloffStartTiles)
		return 100;
	if (tiles >= AccuracyFalloffEndTiles)
		return MinAccuracyAtRangePercent;
	const int lost = (100 - MinAccuracyAtRangePercent) * (tiles - AccuracyFalloffStartTiles) / (AccuracyFalloffEndTiles - AccuracyFalloffStartTiles);
	return 100 - lost;
}

int Poe2HitChance(int accuracy, int evasion)
{
	accuracy = std::max(accuracy, 1);
	const int64_t chance = int64_t { 125 } * accuracy * 10 / (int64_t { accuracy } * 10 + int64_t { std::max(evasion, 0) } * 3);
	return std::clamp(static_cast<int>(chance), 5, 100);
}

bool UsesPoe2MonsterAccuracy(const Monster &monster)
{
	return gbIsPoe2 && !monster.isPlayerMinion() && (monster.flags & MFLAG_POE2_TABLES) != 0 && HasMonsterLevelTable()
	    && Poe2MonsterLevel(monster, sgGameInitInfo.nDifficulty) > 0;
}

int MonsterAccuracyRating(const Monster &monster, int baseToHit)
{
	const int level = Poe2MonsterLevel(monster, sgGameInitInfo.nDifficulty);
	const int accuracy = MonsterLevelAccuracy(level);
	const int ref = MonsterTypeRow(monster.type().type).toHitRef10;
	int64_t permille = 1000;
	if (ref > 0)
		permille = std::clamp<int64_t>(int64_t { std::max(baseToHit, 0) } * 10000 / ref, MinMonsterTypePercent * 10, MaxMonsterTypePercent * 10);
	return std::max(static_cast<int>(accuracy * permille / 1000), 1);
}

int MonsterHitChance(const Monster &monster, int baseToHit, int evasion, int distancePercent)
{
	const int accuracy = static_cast<int>(int64_t { MonsterAccuracyRating(monster, baseToHit) } * std::clamp(distancePercent, 0, 100) / 100);
	return Poe2HitChance(accuracy, evasion);
}

int BlockChanceAgainstMonster(const Player &player, const Monster *monster, bool melee)
{
	if (melee) {
		int block = player.GetBlockChance();
		if (!gbIsPoe2 && monster != nullptr)
			block -= static_cast<int>(monster->level(sgGameInitInfo.nDifficulty)) * 2;
		return block;
	}
	int block = player.GetBlockChance(false);
	if (!gbIsPoe2 && monster != nullptr)
		block -= (static_cast<int>(monster->level(sgGameInitInfo.nDifficulty)) - player.getCharacterLevel()) * 2;
	return block;
}

int LightStunChance(int64_t damage, int64_t maxLife)
{
	if (damage <= 0 || maxLife <= 0)
		return 0;
	const int64_t chance = std::min<int64_t>(damage * LightStunMultiplierPercent / maxLife, 100);
	return chance > LightStunMinChancePercent ? static_cast<int>(chance) : 0;
}

} // namespace devilution
