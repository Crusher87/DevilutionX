#include "poe2/defenses.hpp"

#include <algorithm>
#include <string_view>

#include "engine/backbuffer_state.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "levels/gendung_defs.hpp"
#include "player.h"
#include "poe2/bases.hpp"
#include "poe2/charms.hpp"
#include "poe2/flasks.hpp"
#include "poe2/meta_gems.hpp"
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

void SplitDefense(DefenseType type, int defense, int &armour, int &evasion, int &energyShield)
{
	armour = 0;
	evasion = 0;
	energyShield = 0;
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
		evasion = defense / 2;
		armour = defense - evasion;
		break;
	case DefenseType::ArmourEnergyShield:
		energyShield = defense / 2;
		armour = defense - energyShield;
		break;
	case DefenseType::EvasionEnergyShield:
		energyShield = defense / 2;
		evasion = defense - energyShield;
		break;
	case DefenseType::All:
		evasion = defense / 3;
		energyShield = defense / 3;
		armour = defense - evasion - energyShield;
		break;
	}
}

int ArmourRating(const Player &player)
{
	const int armour = std::max(player._pIAC + player._pIBonusAC, 0);
	return armour * (100 + FlaskArmourPercent(player)) / 100 * UniqueDefencePercent(player) / 100;
}

int EvasionRating(const Player &player)
{
	return std::max(player.poe2.evasion + player._pDexterity / 5, 0) * UniqueDefencePercent(player) / 100 * (100 + UnarmedEvasionPercent(player)) / 100;
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
	return Fixed26_6::fromRaw(damage.raw() - damage.raw() * percent / 100);
}

Fixed26_6 AbsorbWithEnergyShield(Player &player, Fixed26_6 damage)
{
	if (!gbIsPoe2 || damage <= 0)
		return damage;
	damage = ApplyGuard(player, damage);
	Poe2PlayerData &data = player.poe2;
	data.energyShieldRechargeTicks = 0;
	data.energyShieldRemainder = 0;
	const int32_t absorbed = std::min(data.energyShield, damage.raw());
	if (absorbed <= 0)
		return damage;
	data.energyShield -= absorbed;
	TelemetryShieldAbsorbed(player, Fixed26_6::fromRaw(absorbed));
	OnMetaShieldLost(player, Fixed26_6::fromRaw(absorbed));
	if (&player == MyPlayer)
		RedrawComponent(PanelDrawComponent::Health);
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

} // namespace devilution
