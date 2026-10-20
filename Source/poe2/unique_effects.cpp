#include "poe2/unique_effects.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <functional>
#include <optional>

#include "engine/backbuffer_state.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/belt_uniques.hpp"
#include "poe2/defenses.hpp"
#include "poe2/map_play.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/monster_resistance.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/profane_roads.hpp"
#include "poe2/skill_resources.hpp"
#include "poe2/spirit.hpp"
#include "poe2/spirit_auras.hpp"
#include "poe2/spirit_buffs.hpp"
#include "poe2/spirit_uses.hpp"
#include "poe2/spirit_skills.hpp"
#include "poe2/support_gems.hpp"
#include "poe2/meta_gems.hpp"
#include "poe2/support_table.hpp"
#include "poe2/table_skills.hpp"
#include "poe2/telemetry.hpp"
#include "spells.h"
#include "utils/is_of.hpp"

namespace devilution {

namespace {

constexpr int TicksPerSecond = 20;
/** @brief Ticks between the casts of a repeated spell. */
constexpr int RepeatDelayTicks = 6;
/** @brief Resources and energy shield spent count as "recently" for about 4 seconds. */
constexpr int RecentTicks = 4 * TicksPerSecond;
/** @brief Losing this share of maximum life within 2 seconds is a big hit. */
constexpr int BigHitPercent = 30;
/** @brief A hit taking this share of the monster's life is a heavy hit, like a crit. */
constexpr int HeavyHitPercent = 10;
/** @brief POE2 armour above which Colossus Hide slows the hero. */
constexpr int HeavyArmour = 10000;
/** @brief Passive tree capstones: how long their buffs last, how far a staff arc reaches. */
constexpr int BlockRiposteTicks = 3 * TicksPerSecond;
constexpr int ElementRotationTicks = 3 * TicksPerSecond;
constexpr int SwapStrikeTicks = 4 * TicksPerSecond;
constexpr uint8_t SwapStrikeHits = 3;
constexpr uint8_t MaxUnarmedStacks = 10;
constexpr int StaffArcTiles = 3;
constexpr int MinionSpellDamageCap = 40;
/** @brief Fresh Meat and Gharbad's Bargain: Frenzy Charges they give, at most, and for how long. */
constexpr uint8_t RuleFrenzyCharges = 2;
constexpr uint16_t RuleFrenzyTicks = 4 * TicksPerSecond;

int Rule(const Player &player, UniqueRule rule)
{
	return gbIsPoe2 ? player.poe2.rule(rule) : 0;
}

UniqueRuleState &State(const Player &player)
{
	return player.poe2.uniqueState;
}

int32_t Percent(int32_t raw, int percent)
{
	return static_cast<int32_t>(static_cast<int64_t>(raw) * percent / 100);
}

/** @brief The weapon in the hero's hands (a shield is not one), or nullptr when unarmed. */
const Item *HeldWeapon(const Player &player)
{
	for (const Item &item : { std::cref(player.InvBody[INVLOC_HAND_LEFT]), std::cref(player.InvBody[INVLOC_HAND_RIGHT]) }) {
		if (!item.isEmpty() && IsAnyOf(item._itype, ItemType::Sword, ItemType::Axe, ItemType::Mace, ItemType::Bow, ItemType::Staff))
			return &item;
	}
	return nullptr;
}

ItemType HeldWeaponType(const Player &player)
{
	const Item *weapon = HeldWeapon(player);
	return weapon != nullptr ? weapon->_itype : ItemType::None;
}

/** @brief Two-handed melee weapons, staves included (they take the tree's two-handed damage too). */
bool HoldsTwoHandMelee(const Player &player)
{
	const Item *weapon = HeldWeapon(player);
	return weapon != nullptr && weapon->_iLoc == ILOC_TWOHAND && weapon->_itype != ItemType::Bow;
}

/** @brief A Frenzy Charge from a passive rule: up to two, for 4 seconds, keeping a longer one from a skill. */
void GainRuleFrenzyCharge(Player &player)
{
	Poe2PlayerData &data = player.poe2;
	if (data.frenzyCharges >= RuleFrenzyCharges)
		return;
	data.frenzyCharges++;
	data.frenzyTicks = std::max(data.frenzyTicks, RuleFrenzyTicks);
	AddVisualEffect(player.position.tile, MissileGraphicID::BloodStarRedExplosion);
}

/** @brief The closest other living enemy within the range, or nullptr. */
Monster *NearestOtherMonster(const Monster &from, int range)
{
	Monster *nearest = nullptr;
	int best = range + 1;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (&monster == &from || monster.hasNoLife() || monster.isPlayerMinion())
			continue;
		const int distance = std::max(std::abs(monster.position.tile.x - from.position.tile.x), std::abs(monster.position.tile.y - from.position.tile.y));
		if (distance < best) {
			best = distance;
			nearest = &monster;
		}
	}
	return nearest;
}

/** @brief More damage from the state of the monster hit (The Maiden's Whisper, Overhead Chop), in percent. */
int HitConditionPercent(const Player &player, const Monster &monster)
{
	int percent = 100;
	// The Maiden's Road toll adds as much again against a monster that hasn't noticed the hero.
	if (const int more = Rule(player, UniqueRule::FirstStrike); more > 0 && monster.hitPoints >= monster.maxHitPoints)
		percent = percent * (100 + more + UnawareFirstStrikeBonus(player, monster)) / 100;
	if (const int more = Rule(player, UniqueRule::MoreVsStunned); more > 0 && monster.poe2.heavyStunTicks > 0)
		percent = percent * (100 + more) / 100;
	return percent;
}

Fixed26_6 DealPacket(const Player &player, Monster &monster, DamageType type, int32_t raw);

/** @brief The Sickness Road toll's fever: 2 seconds in which the hero's own hits inflict no ailment. */
constexpr uint8_t FeverTicks = 2 * TicksPerSecond;
/** @brief The Sickness Road pact: this share of the tree's own melee damage also applies to spell hits. */
constexpr int HiveBridgePercent = 75;
/** @brief The Sickness Road pact: spell hits on a monster carrying no ailment deal this much less. */
constexpr int HiveHealthyLessPercent = 20;

/** @brief The monster whose ailments Kill Burst's contagion passed on as it died (the pact's death spread skips it). */
std::optional<size_t> BurstSpreadMonster;

bool HasDarkSickness(const Player &player)
{
	return PassiveTotal(player, PassiveStat::DarkSickness) > 0;
}

bool HasHiveBreath(const Player &player)
{
	return PassiveTotal(player, PassiveStat::HiveBreath) > 0;
}

/**
 * @brief The Sickness Road pact's bridge: 75% of the tree's own increased melee damage for the weapon in hand, never a
 * bridged stat (design v8 §5, S6). Nothing with a bow.
 */
int HiveBridge(const Player &player)
{
	if (!HasHiveBreath(player))
		return 0;
	const auto passive = [&player](PassiveStat stat) { return PassiveTotal(player, stat); };
	const Item *weapon = HeldWeapon(player);
	int melee;
	if (weapon == nullptr)
		melee = passive(PassiveStat::MeleeDamage) + passive(PassiveStat::StaffDamage);
	else if (weapon->_itype == ItemType::Bow)
		melee = 0;
	else if (weapon->_itype == ItemType::Staff)
		melee = passive(PassiveStat::MeleeDamage) + passive(PassiveStat::StaffDamage) + passive(PassiveStat::TwoHandDamage);
	else
		melee = passive(PassiveStat::MeleeDamage) + passive(weapon->_iLoc == ILOC_TWOHAND ? PassiveStat::TwoHandDamage : PassiveStat::OneHandDamage);
	return std::max(melee, 0) * HiveBridgePercent / 100;
}

/** @brief The Sickness Road pact's price on a spell hit: less damage to a monster carrying no ailment, in percent. */
int HiveHealthyPercent(const Player &player, const Monster &monster)
{
	return HasHiveBreath(player) && !CarriesAilment(monster) ? 100 - HiveHealthyLessPercent : 100;
}

/**
 * @brief Kill Burst: a monster killed in melee bursts and hurts the enemies beside it. Its kills don't burst. With the
 * Sickness Road toll, the enemies it hits catch the corpse's ignite, shock, chill and poison, and the hero the fever.
 */
void BurstCorpse(const Player &player, const Monster &corpse, int percent)
{
	static bool bursting = false;
	const int32_t raw = Percent(corpse.maxHitPoints.raw(), percent);
	if (bursting || raw <= 0)
		return;
	bursting = true;
	const bool contagion = HasDarkSickness(player);
	bool passed = false;
	AddVisualEffect(corpse.position.tile, contagion ? MissileGraphicID::AcidSplat : MissileGraphicID::BloodStarExplosion);
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (&monster == &corpse || monster.hasNoLife() || monster.isPlayerMinion())
			continue;
		if (std::max(std::abs(monster.position.tile.x - corpse.position.tile.x), std::abs(monster.position.tile.y - corpse.position.tile.y)) > 1)
			continue;
		// It catches the sickness before the blow lands, so a monster the burst kills dies carrying it.
		if (contagion && CopyAilments(corpse, monster, player)) {
			passed = true;
			AddVisualEffect(monster.position.tile, MissileGraphicID::AcidSplat);
		}
		DealPacket(player, monster, DamageType::Physical, raw);
		if (monster.hasNoLife())
			M_StartKill(monster, player);
	}
	if (contagion) {
		BurstSpreadMonster = corpse.getId();
		// You carry it: a new burst sets the fever back to its full time, never longer.
		if (passed)
			State(player).feverTicks = FeverTicks;
	}
	bursting = false;
}

/** @brief The element (a DamageType) of fire, cold and lightning, in the order spells cycle them. */
DamageType CycleElement(int index)
{
	constexpr std::array<DamageType, 3> Elements { DamageType::Fire, DamageType::Magic, DamageType::Lightning };
	return Elements[static_cast<size_t>(index % 3)];
}

bool IsElemental(DamageType type)
{
	return IsAnyOf(type, DamageType::Fire, DamageType::Lightning, DamageType::Magic);
}

/** @brief Armour as POE2 counts it, from the game's armour. */
int Poe2Armour(const Player &player)
{
	return ArmourRating(player) * 100 / GameArmourPer100;
}

/** @brief The share of the hero's maximum life missing, in percent. */
int MissingLifePercent(const Player &player)
{
	if (player._pMaxHP <= 0)
		return 0;
	const int64_t missing = std::max<int64_t>(player._pMaxHP.raw() - player._pHitPoints.raw(), 0);
	return static_cast<int>(missing * 100 / player._pMaxHP.raw());
}

int ResourceSpentBonus(const Player &player)
{
	const int cap = Rule(player, UniqueRule::ResourceSpentDamage);
	if (cap <= 0)
		return 0;
	// 2% more per 100 resources spent recently.
	return std::min<int32_t>(State(player).resourcesSpent / 64 / 100 * 2, cap);
}

constexpr int SpiritDamageCap = 30;

/** @brief Spells' increased damage in percent (100 = none): the tree's and gear's spell damage, Scholar and mana-scaled damage. */
int SpellIncreasedDamage(const Player &player)
{
	int increased = 100 + Rule(player, UniqueRule::SpellDamage) + State(player).scholarPercent;
	if (const int share = Rule(player, UniqueRule::ManaSpellDamage); share > 0)
		increased += std::max<int32_t>(player._pMaxMana.whole(), 0) * share / 100;
	return increased;
}

/** @brief The rules' "more damage" for every hit, attack or spell, in percent. */
int GeneralDamagePercent(const Player &player)
{
	const UniqueRuleState &state = State(player);
	int percent = 100;
	if (const int cap = Rule(player, UniqueRule::HighestAttributeDamage); cap > 0) {
		const int highest = std::max({ player._pStrength, player._pMagic, player._pDexterity });
		percent += std::min(highest / 50, cap);
	}
	// Unreserved Spirit's bonus (the Shimmering Shrine) stops at 30% more.
	if (const int perTen = Rule(player, UniqueRule::SpiritDamage); perTen > 0)
		percent += std::min(std::max(MaxSpirit(player) - ReservedSpirit(player), 0) / 10 * perTen, SpiritDamageCap);
	if (const int more = Rule(player, UniqueRule::DistanceRule); more > 0 && state.nearestEnemy > FarRangeTiles)
		percent = percent * (100 + more) / 100;
	percent = percent * (100 + ResourceSpentBonus(player)) / 100;
	if (state.witchglassTicks > 0)
		percent = percent * (100 + WitchglassDamagePercent) / 100;
	return percent * MapPlayerDamagePercent() / 100;
}

/** @brief Names a packet of a split hit, for the deep damage breakdown. */
const char *DamageTypePacketName(DamageType type)
{
	switch (type) {
	case DamageType::Fire: return "fire part";
	case DamageType::Lightning: return "lightning part";
	case DamageType::Magic: return "magic part";
	case DamageType::Acid: return "acid part";
	default: return "physical part";
	}
}

/** @brief Deals one packet of a hit to the monster, after its resistance, shock and the rules. */
Fixed26_6 DealPacket(const Player &player, Monster &monster, DamageType type, int32_t raw)
{
	if (raw <= 0 || monster.hasNoLife() || monster.isImmune(MissileID::Arrow, type))
		return Fixed26_6::fromInt(0);
	// Element damage adds to the attack's increased damage rather than multiplying it.
	if (const int element = PassiveElementDamage(player, type); element != 0) {
		const int64_t increased = std::max(100 + player._pIBonusDam, 1);
		raw = static_cast<int32_t>(static_cast<int64_t>(raw) * std::max<int64_t>(increased + element, 0) / increased);
	}
	TelemetryHitStep(false, DamageTypePacketName(type), raw, false, true);
	raw = Percent(raw, MonsterDamageTakenPercent(monster, type));
	const Fixed26_6 damage = ApplyShock(monster, Fixed26_6::fromRaw(raw));
	ApplyMonsterDamage(type, monster, damage);
	TryInflictAilment(monster, player, type, damage);
	return damage;
}

void InflictBleed(const Player &player, Monster &monster, int32_t physical)
{
	Poe2MonsterState &state = monster.poe2;
	const int32_t perTick = std::max<int32_t>(Percent(physical, BleedPercentPerSecond) / TicksPerSecond, 1);
	if (state.bleedTicks == 0 || perTick >= state.bleedDamagePerTick) {
		state.bleedDamagePerTick = perTick;
		state.bleedSource = static_cast<int8_t>(player.getId());
	}
	state.bleedTicks = BleedTicks;
}

void CastRepeat(Player &player)
{
	UniqueRuleState &state = State(player);
	const SpellType type = player.executedSpell.spellType;
	state.castingRepeat = true;
	state.castDamagePercent = state.repeatPercent;
	// Repeats take the cast's supports, which only spells cast from mana get.
	player.executedSpell.spellType = SpellType::Spell;
	CastSpell(player, state.repeatSpell, player.position.tile, WorldTilePosition(static_cast<WorldTileCoord>(state.repeatX), static_cast<WorldTileCoord>(state.repeatY)), state.repeatLevel);
	player.executedSpell.spellType = type;
	state.castingRepeat = false;
	state.castDamagePercent = 100;
	if (state.repeatManaDrain && player._pMaxMana > 0) {
		const Fixed26_6 drain = Fixed26_6::fromRaw(Percent(player._pMaxMana.raw(), Rule(player, UniqueRule::RepeatManaDrain)));
		player._pMana = std::max(player._pMana - drain, Fixed26_6::fromInt(0));
		player._pManaBase -= drain;
		if (&player == MyPlayer)
			RedrawComponent(PanelDrawComponent::Mana);
	}
}

void QueueRepeat(const Player &player, SpellID spell, WorldTilePosition target, int level, int count, int percent)
{
	UniqueRuleState &state = State(player);
	// A bigger repeat replaces a smaller one still waiting.
	if (state.repeatsLeft >= count)
		return;
	state.repeatSpell = spell;
	state.repeatsLeft = static_cast<uint8_t>(count);
	state.repeatTicks = RepeatDelayTicks;
	state.repeatLevel = static_cast<uint8_t>(std::clamp(level, 0, 255));
	state.repeatPercent = static_cast<int16_t>(percent);
	state.repeatX = static_cast<int16_t>(target.x);
	state.repeatY = static_cast<int16_t>(target.y);
	state.repeatManaDrain = Rule(player, UniqueRule::RepeatManaDrain) > 0;
}

uint8_t NearestEnemyDistance(const Player &player)
{
	int nearest = 255;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		const Monster &monster = Monsters[ActiveMonsters[i]];
		if (monster.hasNoLife() || monster.isPlayerMinion())
			continue;
		const int dx = std::abs(monster.position.tile.x - player.position.tile.x);
		const int dy = std::abs(monster.position.tile.y - player.position.tile.y);
		nearest = std::min(nearest, std::max(dx, dy));
	}
	return static_cast<uint8_t>(nearest);
}

void SpreadIgnite(Monster &source)
{
	Poe2MonsterState &burning = source.poe2;
	burning.igniteSpread = true;
	if (burning.igniteSource < 0 || static_cast<size_t>(burning.igniteSource) >= Players.size())
		return;
	Player &player = Players[static_cast<size_t>(burning.igniteSource)];
	const int metres = Rule(player, UniqueRule::IgniteSpread);
	if (metres <= 0)
		return;
	const int range = metres <= 2 ? CloseRangeTiles : FarRangeTiles;
	bool spread = false;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (&monster == &source || monster.hasNoLife() || monster.isPlayerMinion() || monster.poe2.igniteTicks > 0)
			continue;
		if (std::max(std::abs(monster.position.tile.x - source.position.tile.x), std::abs(monster.position.tile.y - source.position.tile.y)) > range)
			continue;
		monster.poe2.igniteTicks = IgniteTicks;
		monster.poe2.igniteDamagePerTick = burning.igniteDamagePerTick;
		monster.poe2.igniteSource = burning.igniteSource;
		// A spread ignite doesn't spread again.
		monster.poe2.igniteSpread = true;
		spread = true;
	}
	if (!spread)
		return;
	AddVisualEffect(source.position.tile, MissileGraphicID::BigExplosion);
	if (const int selfDamage = Rule(player, UniqueRule::IgniteSpreadSelfDamage); selfDamage > 0 && &player == MyPlayer && !player.hasNoLife()) {
		TelemetryNoteAttacker("own spreading Ignite");
		ApplyPlrDamage(DamageType::Fire, player, Fixed26_6::fromRaw(Percent(player._pMaxHP.raw(), selfDamage)));
	}
}

} // namespace

bool IsOnLowLife(const Player &player)
{
	return player._pMaxHP > 0 && static_cast<int64_t>(player._pHitPoints.raw()) * 100 <= static_cast<int64_t>(player._pMaxHP.raw()) * LowLifePercent;
}

void ProcessUniqueRules(Player &player)
{
	if (!gbIsPoe2 || player.hasNoLife())
		return;
	UniqueRuleState &state = State(player);
	state.lowLife = IsOnLowLife(player);

	if (player.isWalking()) {
		state.stillTicks = 0;
		state.sinceMovedTicks = 0;
	} else {
		state.stillTicks = static_cast<uint16_t>(std::min(state.stillTicks + 1, 1000));
		state.sinceMovedTicks = static_cast<uint16_t>(std::min(state.sinceMovedTicks + 1, 1000));
	}

	const auto countDown = [](uint8_t &ticks) {
		if (ticks > 0)
			ticks--;
	};
	if (state.guardTicks > 0 && --state.guardTicks == 0)
		state.guardStacks = 0;
	countDown(state.memoryTicks);
	countDown(state.bossEchoTicks);
	countDown(state.bigHitCooldown);
	countDown(state.critSpeedTicks);
	countDown(state.recoveryLockTicks);
	countDown(state.witchglassTicks);
	countDown(state.moonwaterTicks);
	countDown(state.blockRiposteTicks);
	countDown(state.rotationTicks);
	// The fever shows: the hero coughs up a sickly green splash now and then while it runs.
	if (state.feverTicks > 0 && --state.feverTicks % 10 == 9 && &player == MyPlayer)
		AddVisualEffect(player.position.tile, MissileGraphicID::AcidSplat);
	countDown(state.swapStrikeCooldown);
	if (state.swapStrikeTicks > 0 && --state.swapStrikeTicks == 0)
		state.swapStrikes = 0;
	if (state.unarmedTicks > 0 && --state.unarmedTicks == 0)
		state.unarmedStacks = 0;
	for (uint8_t &ticks : state.guardianTicks)
		countDown(ticks);
	if (state.scholarTicks > 0 && --state.scholarTicks == 0)
		state.scholarPercent = 0;
	if (state.heartglassCooldown > 0)
		state.heartglassCooldown--;
	if (state.saintsTokenCooldown > 0)
		state.saintsTokenCooldown--;
	if (state.minionFrenzyTicks > 0 && --state.minionFrenzyTicks == 0)
		state.minionFrenzy = 0;
	if (state.stolenTicks > 0 && --state.stolenTicks == 0)
		state.stolenMod = 0;
	state.resourcesSpent = std::max<int32_t>(state.resourcesSpent - std::max<int32_t>(state.resourcesSpent / RecentTicks, 1), 0);
	state.shieldSpent = std::max<int32_t>(state.shieldSpent - std::max<int32_t>(state.shieldSpent / RecentTicks, 1), 0);

	// Life lost in the last 2 seconds, kept in eight buckets of 5 ticks.
	if (++state.lifeLostTick >= 5) {
		state.lifeLostTick = 0;
		std::rotate(state.lifeLost.rbegin(), state.lifeLost.rbegin() + 1, state.lifeLost.rend());
		state.lifeLost[0] = 0;
		if (Rule(player, UniqueRule::DistanceRule) > 0)
			state.nearestEnemy = NearestEnemyDistance(player);
	}
	if (const int recover = Rule(player, UniqueRule::BigHitRecovery); recover > 0 && state.bigHitCooldown == 0 && player._pMaxHP > 0) {
		int64_t lost = 0;
		for (const int32_t raw : state.lifeLost)
			lost += raw;
		if (lost * 100 >= static_cast<int64_t>(player._pMaxHP.raw()) * BigHitPercent) {
			state.lifeLost.fill(0);
			state.bigHitCooldown = 2 * TicksPerSecond;
			const Fixed26_6 heal = Fixed26_6::fromRaw(Percent(player._pMaxHP.raw(), recover));
			player._pHitPoints = std::min(player._pHitPoints + heal, player._pMaxHP);
			player._pHPBase = std::min(player._pHPBase + heal, player._pMaxHPBase);
			AddVisualEffect(player.position.tile, MissileGraphicID::RedFlareExplosion);
			if (&player == MyPlayer)
				RedrawComponent(PanelDrawComponent::Health);
		}
	}

	if (const int stolen = state.stolenMod; stolen == static_cast<int>(MonsterMod::Regenerating) + 1 && player._pHitPoints < player._pMaxHP) {
		const Fixed26_6 heal = Fixed26_6::fromRaw(std::max<int32_t>(Percent(player._pMaxHP.raw(), StolenRegenPerMille(player)) / 10 / TicksPerSecond, 1));
		player._pHitPoints = std::min(player._pHitPoints + heal, player._pMaxHP);
		player._pHPBase = std::min(player._pHPBase + heal, player._pMaxHPBase);
	}

	if (state.repeatsLeft > 0 && (state.repeatTicks == 0 || --state.repeatTicks == 0)) {
		state.repeatsLeft--;
		state.repeatTicks = RepeatDelayTicks;
		if (leveltype != DTYPE_TOWN)
			CastRepeat(player);
	}
}

bool RollAttackCrit(const Player &player)
{
	const ClassAttributes &classAttributes = GetClassAttributes(player._pClass);
	int chance = HasAnyOf(classAttributes.classFlags, PlayerClassFlag::CriticalStrike) ? player.getCharacterLevel() : 0;
	if (gbIsPoe2) {
		chance += BaseAttackCritPercent;
		// Third Eye: the next attack's place in the count of three.
		if (const int more = Rule(player, UniqueRule::ThirdHitCritChance); more > 0)
			chance += (State(player).attacks + 1) % 3 == 0 ? more : -Rule(player, UniqueRule::OtherHitsLessCrit);
	}
	if (chance <= 0 || GenerateRnd(100) >= chance)
		return false;
	if (gbIsPoe2 && Rule(player, UniqueRule::CritActionSpeed) > 0)
		State(player).critSpeedTicks = 2 * TicksPerSecond;
	return true;
}

int CritDamagePercent(const Player &player)
{
	if (!gbIsPoe2)
		return 200;
	int bonus = BaseCritBonusPercent + Rule(player, UniqueRule::CritDamageBonus);
	// Lucky crits: the bonus rolls twice between half and all of it, and the better roll counts.
	if (Rule(player, UniqueRule::LuckyCrits) > 0)
		bonus = bonus / 2 + std::max(GenerateRnd(bonus / 2 + 1), GenerateRnd(bonus / 2 + 1));
	return 100 + (bonus * MapCritBonusPercent() / 100);
}

bool AttacksCannotBeEvaded(const Player &player)
{
	return Rule(player, UniqueRule::LowLifeUnevadable) > 0 && State(player).lowLife;
}

int UniqueAttackSpeedPercent(const Player &player)
{
	if (!gbIsPoe2)
		return 0;
	int percent = 0;
	if (const int cap = Rule(player, UniqueRule::MissingLifeAttackSpeed); cap > 0)
		percent += std::min(MissingLifePercent(player) / 4, cap);
	if (State(player).critSpeedTicks > 0)
		percent += Rule(player, UniqueRule::CritActionSpeed);
	// Ceaseless Hands: an attack frame faster per 5 stacks.
	if (Rule(player, UniqueRule::UnarmedHitStacks) > 0 && HeldWeapon(player) == nullptr)
		percent += State(player).unarmedStacks / 5 * 10;
	return percent;
}

Fixed26_6 ApplyPlayerAttackDamage(const Player &player, Monster &monster, Fixed26_6 damage, bool crit, bool ranged)
{
	if (!gbIsPoe2) {
		const Fixed26_6 dealt = ApplyShock(monster, damage);
		ApplyMonsterDamage(DamageType::Physical, monster, dealt);
		TryInflictAilment(monster, player, DamageType::Physical, dealt);
		return dealt;
	}
	UniqueRuleState &state = State(player);
	state.lastHitBySpell = false;
	state.lastHitRanged = ranged;
	// Leoric's Road: any hit puts a monster still rising down for good.
	if (TryPutDownRisen(player, monster))
		return damage;
	state.attacks++;
	const ItemType weapon = HeldWeaponType(player);
	const bool staffHit = !ranged && weapon == ItemType::Staff;
	const bool wasShocked = monster.poe2.shockTicks > 0;

	const int generalPercent = GeneralDamagePercent(player);
	int64_t raw = Percent(damage.raw(), generalPercent);
	TelemetryHitStep(false, "more damage from uniques, spirit and maps", generalPercent, true);
	if (!crit)
		raw = raw * (100 - Rule(player, UniqueRule::NonCritLess)) / 100;
	raw = raw * (100 + Rule(player, UniqueRule::GuardStackDamage) * state.guardStacks) / 100;
	if (const int per = Rule(player, UniqueRule::FireOvercapDamage); per > 0) {
		const int overcap = player.poe2.uncappedResistances[0] - player.poe2.maxResistances[0];
		if (overcap > 0)
			raw = raw * (100 + overcap / per) / 100;
	}
	if (const int more = Rule(player, UniqueRule::BleedHinder); more > 0 && monster.poe2.bleedTicks > 0)
		raw = raw * (100 + more) / 100;
	if (const int more = Rule(player, UniqueRule::BleedMovingDamage); more > 0 && monster.poe2.bleedTicks > 0)
		raw = raw * (state.stillTicks > 0 ? 100 - Rule(player, UniqueRule::BleedStationaryLess) : 100 + more) / 100;
	if (const int per = Rule(player, UniqueRule::MissingLifeDamage); per > 0)
		raw = raw * (100 + MissingLifePercent(player) / per) / 100;
	if (state.storedChill > 0) {
		raw = raw * (100 + state.storedChill) / 100;
		state.storedChill = 0;
	}
	raw = raw * HitConditionPercent(player, monster) / 100;
	// War Banner: more attack damage in its area. Every attack hit builds the banners' Glory.
	raw = raw * (100 + BannerAttackDamagePercent(player)) / 100;
	OnSpiritAttackHit(player, monster);
	TelemetryHitStep(false, "after hit conditions and banners", raw);
	if (const int more = Rule(player, UniqueRule::BlockRiposte); more > 0 && !ranged && state.blockRiposteTicks > 0) {
		raw = raw * (100 + more) / 100;
		state.blockRiposteTicks = 0;
	}
	if (const int more = Rule(player, UniqueRule::SwapStrike); more > 0 && state.swapStrikes > 0) {
		raw = raw * (100 + more) / 100;
		state.swapStrikes--;
	}
	if (const int seconds = Rule(player, UniqueRule::UnarmedHitStacks); seconds > 0 && !ranged && weapon == ItemType::None) {
		state.unarmedStacks = static_cast<uint8_t>(std::min<int>(state.unarmedStacks + 1, MaxUnarmedStacks));
		state.unarmedTicks = static_cast<uint8_t>(std::min(seconds * TicksPerSecond, 255));
	}

	// Split the hit into physical and elemental packets.
	std::array<int64_t, 4> packets {}; // physical, fire, lightning, cold
	packets[0] = raw;
	const int third = Rule(player, UniqueRule::ThirdAttackElemental);
	if (third > 0 && state.attacks % 3 == 0) {
		const bool noCold = Rule(player, UniqueRule::NoColdDamage) > 0;
		const int element = 1 + GenerateRnd(noCold ? 2 : 3);
		packets[static_cast<size_t>(element)] = packets[0] * (100 + third) / 100;
		packets[0] = 0;
		if (Rule(player, UniqueRule::ThirdAttackExposure) > 0) {
			constexpr std::array<DamageType, 4> Types { DamageType::Physical, DamageType::Fire, DamageType::Lightning, DamageType::Magic };
			monster.poe2.exposureTicks = ExposureTicks;
			monster.poe2.exposureElement = static_cast<uint8_t>(Types[static_cast<size_t>(element)]);
		}
	} else if (third > 0) {
		packets[0] = packets[0] * (100 - Rule(player, UniqueRule::OtherAttacksLessPhysical)) / 100;
	}
	// Fire converts first; a staff's lightning (Staff of the Storm Gods) takes what is left.
	const int toFire = std::clamp(Rule(player, UniqueRule::PhysicalToFire), 0, 100);
	const int toLightning = staffHit ? std::clamp(Rule(player, UniqueRule::StaffToLightning), 0, 100 - toFire) : 0;
	if (toFire > 0 || toLightning > 0) {
		const int64_t fire = packets[0] * toFire / 100;
		const int64_t lightning = packets[0] * toLightning / 100;
		packets[0] -= fire + lightning;
		packets[1] += fire;
		packets[2] += lightning;
	}
	// Fire in the Hand: spell damage adds to the increased damage of the attack's fire.
	if (const int share = Rule(player, UniqueRule::SpellDamageToFireAttacks); share > 0 && packets[1] > 0) {
		const int64_t increased = std::max(100 + player._pIBonusDam, 1);
		const int64_t extra = std::max(SpellIncreasedDamage(player) - 100, 0) * share / 100;
		packets[1] = packets[1] * (increased + extra) / increased;
	}
	if (Rule(player, UniqueRule::NoColdDamage) > 0)
		packets[3] = 0;

	const int32_t physical = static_cast<int32_t>(packets[0]);
	// Briarpatch stores part of the physical damage as thorns.
	OnSpiritPhysicalAttack(player, physical);
	Fixed26_6 dealt = DealPacket(player, monster, DamageType::Physical, physical);
	dealt += DealPacket(player, monster, DamageType::Fire, static_cast<int32_t>(packets[1]));
	dealt += DealPacket(player, monster, DamageType::Lightning, static_cast<int32_t>(packets[2]));
	dealt += DealPacket(player, monster, DamageType::Magic, static_cast<int32_t>(packets[3]));
	if (crit) {
		OnMetaCrit(Players[player.getId()], monster, dealt);
		OnSpiritCrit(Players[player.getId()], monster);
	}

	if (crit && !monster.hasNoLife()) {
		if (const int bleed = Rule(player, UniqueRule::CritBleed); bleed > 0 && physical > 0)
			InflictBleed(player, monster, Percent(physical, 100 + bleed));
	}
	const int stacks = Rule(player, UniqueRule::GuardOnHeavyHit);
	if (stacks > 0 && (crit || static_cast<int64_t>(dealt.raw()) * 100 >= static_cast<int64_t>(monster.maxHitPoints.raw()) * HeavyHitPercent)) {
		state.guardStacks = static_cast<uint8_t>(std::min(state.guardStacks + 1, stacks));
		state.guardTicks = GuardStackTicks;
	}

	// Staff of the Storm Gods: a staff hit on a shocked enemy arcs on as lightning.
	if (const int arc = Rule(player, UniqueRule::StaffArc); arc > 0 && staffHit && wasShocked) {
		if (Monster *next = NearestOtherMonster(monster, StaffArcTiles); next != nullptr) {
			AddVisualEffect(next->position.tile, MissileGraphicID::ThinLightning);
			DealPacket(player, *next, DamageType::Lightning, static_cast<int32_t>(std::min<int64_t>(raw * arc / 100, INT32_MAX)));
			if (next->hasNoLife())
				M_StartKill(*next, player);
		}
	}
	ApplyCull(player, monster);

	// Every 5th attack repeats once, at less damage and without counting as another attack.
	static bool repeating = false;
	if (const int repeat = Rule(player, UniqueRule::FifthAttackRepeat); repeat > 0 && !repeating && state.attacks % 5 == 0 && !monster.hasNoLife()) {
		repeating = true;
		state.attacks--;
		dealt += ApplyPlayerAttackDamage(player, monster, Fixed26_6::fromRaw(Percent(damage.raw(), repeat)), false, ranged);
		repeating = false;
	}
	return dealt;
}

int UniqueSpellDamagePercent(const Player &player, SpellID spell)
{
	if (!gbIsPoe2)
		return 100;
	const UniqueRuleState &state = State(player);
	int percent = SpellIncreasedDamage(player) * state.castDamagePercent / 100;
	if (state.castingRepeat)
		percent = percent * (100 - Rule(player, UniqueRule::RepeatedSpellLess)) / 100;
	(void)spell;
	return std::max(percent, 0);
}

int UniqueSpellHitPercent(const Player &player, const Monster &monster, DamageType type)
{
	if (!gbIsPoe2)
		return 100;
	UniqueRuleState &state = State(player);
	state.lastHitBySpell = true;
	int percent = GeneralDamagePercent(player);
	// Element damage adds to the spell's increased damage rather than multiplying it, and so does the Sickness Road's
	// melee bridge.
	const int increased = std::max(SpellIncreasedDamage(player), 1);
	percent = percent * std::max(increased + PassiveElementDamage(player, type) + HiveBridge(player), 0) / increased;
	if (const int more = Rule(player, UniqueRule::SpellElementCycle); more > 0 && IsElemental(type)) {
		if (type == CycleElement(state.cycleElement))
			percent = percent * (100 + more) / 100;
		else
			percent = percent * (100 - Rule(player, UniqueRule::OffElementLess)) / 100;
	}
	// Wheel of the Elements: the first hit of each spell decides whether its element differs from the last one's.
	if (const int more = Rule(player, UniqueRule::ElementRotation); more > 0 && IsElemental(type)) {
		if (state.rotationSpell != state.spells) {
			const auto element = static_cast<uint8_t>(static_cast<int>(type) + 1);
			state.rotationSpell = state.spells;
			state.rotationBonus = state.rotationTicks > 0 && state.rotationElement != 0 && state.rotationElement != element;
			state.rotationElement = element;
			state.rotationTicks = ElementRotationTicks;
		}
		if (state.rotationBonus)
			percent = percent * (100 + more) / 100;
	}
	percent = percent * HitConditionPercent(player, monster) / 100;
	if (const int cap = Rule(player, UniqueRule::SpellsSpendEnergyShield); cap > 0 && player.poe2.maxEnergyShield > 0)
		percent = percent * (100 + std::min<int>(static_cast<int>(static_cast<int64_t>(state.shieldSpent) * 100 / (player.poe2.maxEnergyShield * 64)), cap)) / 100;
	if (const int seconds = Rule(player, UniqueRule::BossEchoes); seconds > 0 && state.bossEchoTicks == 0
	    && (monster.isUnique() || GetMonsterAffixes(monster).rarity == MonsterRarity::Rare)) {
		state.echoes = static_cast<uint8_t>(std::min(state.echoes + 1, 255));
		state.bossEchoTicks = static_cast<uint8_t>(std::min(seconds * TicksPerSecond, 255));
	}
	return percent * HiveHealthyPercent(player, monster) / 100;
}

int SicknessSpellHitPercent(const Player &player, const Monster &monster)
{
	if (!gbIsPoe2 || !HasHiveBreath(player))
		return 100;
	const int increased = std::max(SpellIncreasedDamage(player), 1);
	return (increased + HiveBridge(player)) * HiveHealthyPercent(player, monster) / increased;
}

bool AilmentAllowed(const Player &player, AilmentSource source)
{
	if (!gbIsPoe2 || source == AilmentSource::Spread)
		return true;
	if (State(player).feverTicks > 0)
		return false;
	return source != AilmentSource::Spell || !HasHiveBreath(player);
}

int SicknessFeverTicks(const Player &player)
{
	return gbIsPoe2 ? State(player).feverTicks : 0;
}

bool TakeBurstSpread(const Monster &monster)
{
	if (BurstSpreadMonster != monster.getId())
		return false;
	BurstSpreadMonster = std::nullopt;
	return true;
}

void OnSpellCast(const Player &player, SpellID spell, WorldTilePosition target, int spellLevel)
{
	if (!gbIsPoe2 || IsCastingRepeat(player))
		return;
	OnSpiritSpellCast(player);
	UniqueRuleState &state = State(player);
	state.spells++;
	state.cycleElement = static_cast<uint8_t>((state.spells - 1) % 3);
	if (const int echoes = Rule(player, UniqueRule::SpellEchoes); echoes > 0 && state.echoes >= echoes) {
		state.echoes = 0;
		QueueRepeat(player, spell, target, spellLevel, 2, 100);
	}
	if (const int percent = Rule(player, UniqueRule::SixthSpellRepeat); percent > 0 && state.spells % 6 == 0)
		QueueRepeat(player, spell, target, spellLevel, 3, percent);
	if (const int every = Rule(player, UniqueRule::StillSpellRepeat); every > 0 && state.stillTicks >= TicksPerSecond && state.spells % every == 0)
		QueueRepeat(player, spell, target, spellLevel, 1, 100);
}

bool IsCastingRepeat(const Player &player)
{
	return State(player).castingRepeat;
}

void CastFreeSpell(Player &player, SpellID spell, Point source, Point target, int percent)
{
	UniqueRuleState &state = State(player);
	if (state.castingRepeat)
		return;
	const SpellType type = player.executedSpell.spellType;
	state.castingRepeat = true;
	state.castDamagePercent = static_cast<int16_t>(std::clamp(percent, 0, 1000));
	player.executedSpell.spellType = SpellType::Spell;
	const int level = player.GetSpellLevel(spell) + SupportSpellLevelBonus(player, spell);
	CastSpell(player, spell, WorldTilePosition(static_cast<WorldTileCoord>(source.x), static_cast<WorldTileCoord>(source.y)), WorldTilePosition(static_cast<WorldTileCoord>(target.x), static_cast<WorldTileCoord>(target.y)), std::max(level, 1));
	player.executedSpell.spellType = type;
	state.castingRepeat = false;
	state.castDamagePercent = 100;
}

bool TakeFreeSkill(const Player &player)
{
	UniqueRuleState &state = State(player);
	if (!gbIsPoe2 || !state.freeSkill)
		return false;
	state.freeSkill = false;
	return true;
}

int UniqueManaCostPercent(const Player &player)
{
	return std::max((100 + Rule(player, UniqueRule::ManaCost)) * MapSkillCostPercent() / 100, 0);
}

Fixed26_6 SpendableEnergyShield(const Player &player)
{
	if (Rule(player, UniqueRule::SpellsSpendEnergyShield) <= 0)
		return Fixed26_6::fromInt(0);
	return Fixed26_6::fromRaw(std::max<int32_t>(player.poe2.energyShield, 0));
}

Fixed26_6 PaySpellCostWithEnergyShield(Player &player, Fixed26_6 cost)
{
	NoteResourceSpent(player, cost);
	const int32_t paid = std::min(SpendableEnergyShield(player).raw(), cost.raw());
	if (paid <= 0)
		return cost;
	player.poe2.energyShield -= paid;
	State(player).shieldSpent += paid;
	// Spending energy shield on spells restarts its recharge, like taking a hit.
	player.poe2.energyShieldRechargeTicks = 0;
	if (&player == MyPlayer)
		RedrawComponent(PanelDrawComponent::Health);
	return Fixed26_6::fromRaw(cost.raw() - paid);
}

void NoteResourceSpent(const Player &player, Fixed26_6 amount)
{
	if (Rule(player, UniqueRule::ResourceSpentDamage) <= 0 || amount <= 0)
		return;
	const int before = ResourceSpentBonus(player);
	UniqueRuleState &state = State(player);
	state.resourcesSpent = std::min<int32_t>(state.resourcesSpent + amount.raw(), 1000000 * 64);
	const int cap = Rule(player, UniqueRule::ResourceSpentDamage);
	if (before < cap && ResourceSpentBonus(player) >= cap && Rule(player, UniqueRule::ResourceLock) > 0)
		state.recoveryLockTicks = static_cast<uint8_t>(Rule(player, UniqueRule::ResourceLock) * TicksPerSecond);
}

int UniqueCastSlowPercent(const Player &player)
{
	if (State(player).sinceMovedTicks >= TicksPerSecond)
		return 0;
	return std::clamp(Rule(player, UniqueRule::MovingCastSlow), 0, 90);
}

int UniqueMovementSlowPercent(const Player &player)
{
	if (!gbIsPoe2)
		return 0;
	int slow = Rule(player, UniqueRule::GuardStackSlow) * State(player).guardStacks;
	if (const int heavy = Rule(player, UniqueRule::HeavyArmourSlow); heavy > 0 && Poe2Armour(player) > HeavyArmour)
		slow += heavy;
	slow += MapPlayerSlowPercent(player);
	return std::clamp(slow, 0, 90);
}

bool UniqueSlowSkipsWalkTick(Player &player)
{
	const int slow = UniqueMovementSlowPercent(player);
	if (slow <= 0)
		return false;
	UniqueRuleState &state = State(player);
	state.walkCarry = static_cast<uint8_t>(state.walkCarry + slow);
	if (state.walkCarry < 100)
		return false;
	state.walkCarry = static_cast<uint8_t>(state.walkCarry - 100);
	return true;
}

void OnMonsterKilled(Player &player, const Monster &monster)
{
	if (!gbIsPoe2)
		return;
	UniqueRuleState &state = State(player);
	const bool meleeKill = !state.lastHitBySpell && !state.lastHitRanged;
	// Leoric's Road pact: Fresh Meat fires on bow kills too.
	const bool bowKill = !state.lastHitBySpell && state.lastHitRanged && player.isHoldingItem(ItemType::Bow)
	    && PassiveTotal(player, PassiveStat::KingsDeadPact) > 0;
	if (Rule(player, UniqueRule::TwoHandKillFrenzy) > 0 && ((meleeKill && HoldsTwoHandMelee(player)) || bowKill))
		GainRuleFrenzyCharge(player);
	// The Sickness Road toll: Kill Burst needs a monster carrying an ailment.
	if (const int burst = Rule(player, UniqueRule::KillBurst); burst > 0 && meleeKill && (!HasDarkSickness(player) || CarriesAilment(monster)))
		BurstCorpse(player, monster, burst);
	if (Rule(player, UniqueRule::SpellEchoes) > 0 && state.lastHitBySpell)
		state.echoes = static_cast<uint8_t>(std::min(state.echoes + 1, 255));
	if (const int seconds = Rule(player, UniqueRule::StealRareMod); seconds > 0 && GetMonsterAffixes(monster).rarity == MonsterRarity::Rare) {
		const MonsterAffixes affixes = GetMonsterAffixes(monster);
		for (const MonsterMod mod : { MonsterMod::Regenerating, MonsterMod::Resistant, MonsterMod::ExtraLife }) {
			if (!affixes.has(mod))
				continue;
			state.stolenMod = static_cast<uint8_t>(static_cast<int>(mod) + 1);
			state.stolenTicks = static_cast<uint16_t>(seconds * TicksPerSecond);
			break;
		}
	}
}

void OnMinionDied(Player &owner)
{
	if (!gbIsPoe2 || Rule(owner, UniqueRule::MinionFrenzy) <= 0)
		return;
	UniqueRuleState &state = State(owner);
	constexpr uint8_t MaxStacks = 5;
	if (const int seconds = Rule(owner, UniqueRule::MinionResummon); seconds > 0 && state.minionFrenzy >= MaxStacks) {
		const auto quick = static_cast<uint8_t>(std::min(seconds * TicksPerSecond, 255));
		owner.poe2.golemCooldown = std::min(owner.poe2.golemCooldown, quick);
		HastenMinionRevives(owner, quick);
	}
	state.minionFrenzy = static_cast<uint8_t>(std::min<int>(state.minionFrenzy + 1, MaxStacks));
	state.minionFrenzyTicks = 4 * TicksPerSecond;
}

int UniqueMinionDamagePercent(const Player &owner, bool minionLowLife)
{
	const int lowLife = minionLowLife ? Rule(owner, UniqueRule::MinionLowLifeDamage) : 0;
	return (100 + Rule(owner, UniqueRule::MinionFrenzy) * State(owner).minionFrenzy) * (100 + lowLife) / 100;
}

int UniqueMinionLifePercent(const Player &owner)
{
	return std::max(100 - Rule(owner, UniqueRule::LessMinionLife) + PassiveTotal(owner, PassiveStat::MinionLife), 10);
}

int UniqueIgniteChancePercent(const Player &player)
{
	if (Rule(player, UniqueRule::CannotIgnite) > 0)
		return 0;
	return 100 + Rule(player, UniqueRule::IgniteBuildup) + PassiveTotal(player, PassiveStat::IgniteChance);
}

int UniqueStunBuildupPercent(const Player &player)
{
	const int per = Rule(player, UniqueRule::ArmourStunBuildup);
	const int passive = PassiveTotal(player, PassiveStat::StunBuildup);
	if (per <= 0)
		return std::max(100 + passive, 0);
	return std::max(100 + passive + Poe2Armour(player) / per, 0);
}

void OnAilmentInflicted(const Player &player, Monster &monster, DamageType type, bool wasShocked)
{
	if (!gbIsPoe2)
		return;
	UniqueRuleState &state = State(player);
	if (const int seconds = Rule(player, UniqueRule::AilmentMemory); seconds > 0) {
		state.memoryElement = static_cast<uint8_t>(type);
		state.memoryTicks = static_cast<uint8_t>(std::min(seconds * TicksPerSecond, 255));
	}
	if (type == DamageType::Fire)
		monster.poe2.igniteSpread = false;
	if (type == DamageType::Lightning && wasShocked && Rule(player, UniqueRule::Electrocute) > 0) {
		if (monster.poe2.electrocuteTicks == 0)
			AddVisualEffect(monster.position.tile, MissileGraphicID::BlueFlareExplosion);
		monster.poe2.electrocuteTicks = ElectrocuteTicks;
		OnSpiritFreezeOrElectrocute(Players[player.getId()]);
	}
}

void OnMonsterFrozen(const Player &player)
{
	if (const int chill = Rule(player, UniqueRule::FreezeStoresChill); chill > 0)
		State(player).storedChill = static_cast<uint8_t>(std::clamp(chill, 0, 255));
}

void ProcessUniqueMonsterAilments(Monster &monster)
{
	if (!gbIsPoe2 || monster.hasNoLife())
		return;
	Poe2MonsterState &state = monster.poe2;
	if (state.exposureTicks > 0)
		state.exposureTicks--;
	if (state.electrocuteTicks > 0)
		state.electrocuteTicks--;
	if (state.igniteTicks == IgniteTicks / 2 && !state.igniteSpread)
		SpreadIgnite(monster);
	if (state.bleedTicks > 0) {
		state.bleedTicks--;
		if (state.bleedTicks % 10 == 0)
			AddVisualEffect(monster.position.tile, MissileGraphicID::RedFlareExplosion);
		// Only the player who caused the bleeding applies its damage, which the game then syncs.
		if (state.bleedSource >= 0 && state.bleedSource == static_cast<int8_t>(MyPlayerId) && MyPlayer != nullptr) {
			TelemetryNoteDamageSource("bleeding");
			ApplyMonsterDamage(DamageType::Physical, monster, Fixed26_6::fromRaw(state.bleedDamagePerTick));
			if (monster.hasNoLife())
				M_StartKill(monster, *MyPlayer);
		}
	}
}

int MonsterDamageTakenPercent(const Monster &monster, DamageType type)
{
	if (!gbIsPoe2)
		return 100;
	int percent = 100;
	// Amplify Damage and Smite's armour break.
	if (type == DamageType::Physical) {
		if (monster.poe2.amplifyTicks > 0)
			percent += monster.poe2.amplifyPercent;
		if (monster.poe2.armourBreakTicks > 0)
			percent += ArmourBreakDamagePercent;
	}
	const bool deep = TelemetryDeepDamage();
	// Death Mark and Decrepify.
	percent += TableCurseDamageTakenPercent(monster, type);
	if (type == DamageType::Lightning && IsMonsterElectrocuted(monster) && MyPlayer != nullptr)
		percent += Rule(*MyPlayer, UniqueRule::ElectrocutedLightning);
	// Talismans and desecrated modifiers that favour hunting rare and unique monsters.
	if (MyPlayer != nullptr) {
		if (monster.isUnique() || GetMonsterAffixes(monster).rarity == MonsterRarity::Rare)
			percent += Rule(*MyPlayer, UniqueRule::DamageVsRareUnique);
		else
			percent -= Rule(*MyPlayer, UniqueRule::LessDamageVsNormal);
	}
	if (deep)
		TelemetryHitStep(false, "curses, armour break and rules against this monster", percent, true, true);
	// The supports of the hit in progress, and wither.
	const int supportPercent = SupportHitDamagePercent(monster, type);
	percent = percent * supportPercent / 100;
	// Elemental Conflux and Attrition.
	const int spiritPercent = SpiritBuffDamagePercent(monster, type);
	percent = percent * spiritPercent / 100;
	// The monster's resistance, which curses and exposure lower and the hit's penetration gets past.
	const int resistance = MonsterHitResistance(monster, type, HitPenetration(type));
	percent = percent * (100 - resistance) / 100;
	if (deep) {
		TelemetryHitStep(false, "supports and wither", supportPercent, true, true);
		TelemetryHitStep(false, "spirit buffs", spiritPercent, true, true);
		TelemetryHitStep(false, "monster resistance after penetration", resistance, true, true);
		TelemetryHitStep(false, "damage taken in all", std::max(percent, 0), true, true);
	}
	return std::max(percent, 0);
}

int HitPenetration(DamageType type)
{
	if (!gbIsPoe2)
		return 0;
	int penetration = SupportHitPenetration(type);
	// Storm Thesis and its kind: spells penetrate the resistance of the remembered element.
	if (MyPlayer != nullptr) {
		const UniqueRuleState &state = State(*MyPlayer);
		if (const int memory = Rule(*MyPlayer, UniqueRule::MemoryPenetration); memory > 0 && state.lastHitBySpell && state.memoryTicks > 0 && static_cast<uint8_t>(type) == state.memoryElement)
			penetration += memory;
	}
	return penetration;
}

bool IsMonsterElectrocuted(const Monster &monster)
{
	return gbIsPoe2 && monster.poe2.electrocuteTicks > 0;
}

int MonsterHinderPercent(const Monster &monster)
{
	const Poe2MonsterState &state = monster.poe2;
	// Caltrops and Herald of Plague.
	if (gbIsPoe2 && state.hinderTicks > 0)
		return HinderPercent;
	if (!gbIsPoe2 || state.bleedTicks == 0 || state.bleedSource < 0 || static_cast<size_t>(state.bleedSource) >= Players.size())
		return 0;
	return Rule(Players[static_cast<size_t>(state.bleedSource)], UniqueRule::BleedHinder) > 0 ? HinderPercent : 0;
}

int UniqueDefencePercent(const Player &player)
{
	if (const int more = Rule(player, UniqueRule::StationaryDefences); more > 0 && State(player).stillTicks > 0)
		return 100 + more;
	return 100;
}

constexpr int MaxSpellEvasion = 25;

int SpellEvasion(const Player &player)
{
	const int share = Rule(player, UniqueRule::EvasionVsSpells);
	if (share <= 0)
		return 0;
	// At most 25 points off a spell's chance to hit, so spells still land.
	return std::min(EvasionRating(player) * share / 100, MaxSpellEvasion);
}

Fixed26_6 ApplyUniqueHitTaken(Player &player, Fixed26_6 damage, DamageType type)
{
	if (!gbIsPoe2 || damage <= 0)
		return damage;
	UniqueRuleState &state = State(player);
	int64_t raw = damage.raw() * MapDamageTakenPercent(player) / 100;
	if (const int more = Rule(player, UniqueRule::MovingVulnerability); more > 0 && state.sinceMovedTicks < TicksPerSecond)
		raw = raw * (100 + more) / 100;
	if (const int cap = Rule(player, UniqueRule::OvercapResistGuard); cap > 0) {
		int overcap = 0;
		for (size_t i = 0; i < player.poe2.maxResistances.size(); i++)
			overcap += std::max(player.poe2.uncappedResistances[i] - player.poe2.maxResistances[i], 0);
		raw = raw * (100 - std::min(overcap / 10, cap)) / 100;
	}
	if (IsElemental(type)) {
		if (const int share = Rule(player, UniqueRule::ArmourVsElements); share > 0)
			raw -= raw * ArmourReductionPercent(ArmourRating(player) * share / 100, Fixed26_6::fromRaw(static_cast<int32_t>(raw))) / 100;
		if (state.stolenMod == static_cast<int>(MonsterMod::Resistant) + 1)
			raw = raw * 80 / 100;
	}
	if (type == DamageType::Physical) {
		if (const int cap = Rule(player, UniqueRule::ArmourPhysicalReduction); cap > 0)
			raw = raw * (100 - std::min(Poe2Armour(player) / 5000, cap)) / 100;
	}
	if (state.stolenMod == static_cast<int>(MonsterMod::ExtraLife) + 1)
		raw = raw * 90 / 100;
	if (const int less = Rule(player, UniqueRule::LessHitDamage); less > 0)
		raw = raw * (100 - std::min(less, 50)) / 100;
	if (const int less = Rule(player, UniqueRule::FullLifeLessHitDamage); less > 0 && player._pHitPoints >= player._pMaxHP)
		raw = raw * (100 - std::min(less, 50)) / 100;
	// Black Reservoir: part of the hit is paid from mana while there is mana.
	if (const int share = std::clamp(Rule(player, UniqueRule::HitDamageFromMana), 0, 100); share > 0 && raw > 0) {
		const int64_t fromMana = std::min<int64_t>(raw * share / 100, std::max<int32_t>(player._pMana.raw(), 0));
		player._pMana -= Fixed26_6::fromRaw(static_cast<int32_t>(fromMana));
		player._pManaBase -= Fixed26_6::fromRaw(static_cast<int32_t>(fromMana));
		raw -= fromMana;
	}
	raw = std::max<int64_t>(raw, 0);
	// Berserk and Mana Shield.
	raw = ApplySpiritHitTaken(player, Fixed26_6::fromRaw(static_cast<int32_t>(std::min<int64_t>(raw, INT32_MAX / 2)))).raw();
	state.lifeLost[0] = static_cast<int32_t>(std::min<int64_t>(state.lifeLost[0] + raw, INT32_MAX / 2));
	return Fixed26_6::fromRaw(static_cast<int32_t>(raw));
}

int UniqueStunThresholdPercent(const Player &player)
{
	return 100 + Rule(player, UniqueRule::StunThreshold);
}

int LifeRecoveryPercent(const Player &player, bool fromFlask)
{
	if (IsRecoveryLocked(player))
		return 0;
	int percent = (100 - Rule(player, UniqueRule::LessLifeRecovery)) * MapRecoveryPercent() / 100;
	if (fromFlask)
		percent = percent * (100 - Rule(player, UniqueRule::LessLifeFlaskRecovery)) / 100;
	return std::max(percent, 0);
}

void OnLifeRecovered(Player &player, int32_t raw)
{
	const int share = Rule(player, UniqueRule::LifeRecoveryToShield);
	if (share <= 0 || raw <= 0 || player.poe2.maxEnergyShield <= 0)
		return;
	player.poe2.energyShield = std::min<int32_t>(player.poe2.energyShield + Percent(raw, share), player.poe2.maxEnergyShield * 64);
}

int ManaRecoveryPercent(const Player &player)
{
	return std::max(100 - Rule(player, UniqueRule::LessManaRecovery), 0);
}

bool IsRecoveryLocked(const Player &player)
{
	return gbIsPoe2 && State(player).recoveryLockTicks > 0;
}

int EnergyShieldRechargePercent(const Player &player)
{
	if (Rule(player, UniqueRule::NoEnergyShieldRecharge) > 0)
		return 0;
	return std::max((100 - Rule(player, UniqueRule::LessEnergyShieldRecharge)) * (100 + PassiveTotal(player, PassiveStat::EnergyShieldRecharge)) / 100, 0);
}

int StolenRegenPerMille(const Player &player)
{
	return State(player).stolenMod == static_cast<int>(MonsterMod::Regenerating) + 1 ? RegenPerMillePerSecond : 0;
}

void ApplyCull(const Player &player, Monster &monster)
{
	// Attrition's Culling Strike takes rares and uniques that stayed near the hero long enough.
	if (AttritionCulls(player, monster)) {
		AddVisualEffect(monster.position.tile, MissileGraphicID::BloodStarRedExplosion, 0, 4);
		TelemetryLog("spirit", "Attrition culled a monster");
		monster.poe2.culled = true;
		ApplyMonsterDamage(DamageType::Physical, monster, monster.hitPoints);
		return;
	}
	const int cull = Rule(player, UniqueRule::Cull);
	if (cull <= 0 || monster.hasNoLife() || monster.isUnique() || monster.type().type == MT_DIABLO || monster.isPlayerMinion())
		return;
	if (GetMonsterAffixes(monster).rarity == MonsterRarity::Rare)
		return;
	if (static_cast<int64_t>(monster.hitPoints.raw()) * 100 > static_cast<int64_t>(monster.maxHitPoints.raw()) * cull)
		return;
	AddVisualEffect(monster.position.tile, MissileGraphicID::BloodHit);
	// Leoric's Road reads it when the monster dies: a culled monster stays dead.
	monster.poe2.culled = true;
	ApplyMonsterDamage(DamageType::Physical, monster, monster.hitPoints);
}

int ArrowPierceCount(const Player &player)
{
	return std::max(Rule(player, UniqueRule::ArrowPierce), 0);
}

int ArrowDamageModPercent(const Player &player)
{
	return Rule(player, UniqueRule::ArrowDamageMod);
}

void OnPlayerBlocked(const Player &player)
{
	if (Rule(player, UniqueRule::BlockRiposte) > 0)
		State(player).blockRiposteTicks = BlockRiposteTicks;
	OnSpiritBlocked(Players[player.getId()]);
}

void OnWeaponsSwapped(const Player &player, ItemType before)
{
	UniqueRuleState &state = State(player);
	if (Rule(player, UniqueRule::SwapStrike) <= 0 || state.swapStrikeCooldown > 0 || HeldWeaponType(player) == before)
		return;
	state.swapStrikes = SwapStrikeHits;
	state.swapStrikeTicks = SwapStrikeTicks;
	state.swapStrikeCooldown = SwapStrikeTicks;
}

void OnFlaskDrunk(Player &player)
{
	if (Rule(player, UniqueRule::FlaskFrenzy) > 0)
		GainRuleFrenzyCharge(player);
}

int MinionSpellDamageBonus(const Player &player)
{
	const int share = Rule(player, UniqueRule::MinionSpellDamage);
	if (share <= 0)
		return 0;
	return std::min(std::max(SpellIncreasedDamage(player) - 100, 0) * share / 100, MinionSpellDamageCap);
}

int UnarmedEvasionPercent(const Player &player)
{
	const UniqueRuleState &state = State(player);
	if (Rule(player, UniqueRule::UnarmedHitStacks) <= 0 || state.unarmedTicks == 0 || HeldWeapon(player) != nullptr)
		return 0;
	return state.unarmedStacks * 2;
}

} // namespace devilution
