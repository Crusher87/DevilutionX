#include "poe2/spirit.hpp"

#include <algorithm>
#include <optional>
#include <vector>

#include "engine/path.h"
#include "engine/point.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "levels/tile_properties.hpp"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "plrmsg.h"
#include "poe2/defenses.hpp"
#include "poe2/map_play.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/spirit_skills.hpp"
#include "poe2/support_table.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/unique_effects.hpp"
#include "utils/format.hpp"
#include "utils/language.h"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

struct HeraldTrigger {
	Point position;
	int32_t lifeRaw;
	bool ash;
	bool thunder;
	bool frost;
};

std::vector<HeraldTrigger> PendingHeralds;

bool MeetsGemRequirements(const Player &player, PersistentSkill skill)
{
	const std::optional<size_t> gemIndex = SkillGemItemIndex(SkillSlot::ForPersistent(skill));
	if (!gemIndex)
		return false;
	const ItemData &gem = AllItemsList[*gemIndex];
	return player._pStrength >= gem.iMinStr && player._pMagic >= gem.iMinMag && player._pDexterity >= gem.iMinDex;
}

bool CanHeraldHit(const Monster &monster)
{
	return !monster.hasNoLife() && !monster.isPlayerMinion() && monster.isPossibleToHit() && monster.mode != MonsterMode::Death;
}

/**
 * @brief Deals herald damage to a monster on behalf of the local player, killing it if it drops.
 * It's a hit of the herald, so the herald's supports change it (POE2).
 */
void HeraldHit(Monster &monster, PersistentSkill herald, DamageType damageType, Fixed26_6 damage)
{
	Player &player = *MyPlayer;
	TelemetryNoteDamageSource(PersistentSkillName(herald));
	const SupportHitScope supported(player, SkillSlot::ForPersistent(herald));
	// The monster's resistance and curses count, like for any other hit.
	damage = Fixed26_6::fromRaw(static_cast<int32_t>(static_cast<int64_t>(damage.raw()) * MonsterDamageTakenPercent(monster, damageType) / 100));
	ApplyMonsterDamage(damageType, monster, damage);
	monster.tag(player);
	TryInflictAilment(monster, player, damageType, damage, AilmentSource::Spell);
	if (monster.hasNoLife())
		M_StartKill(monster, player);
}

void ExplodeAsh(const HeraldTrigger &trigger)
{
	AddVisualEffect(trigger.position, MissileGraphicID::BigExplosion);
	const Fixed26_6 damage = Fixed26_6::fromRaw(static_cast<int32_t>(static_cast<int64_t>(trigger.lifeRaw) * HeraldOfAshPercent * MapAuraPercent() / 10000));
	const int radius = SupportAreaRadius(*MyPlayer, SkillSlot::ForPersistent(PersistentSkill::HeraldOfAsh), HeraldOfAshRadius);
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (CanHeraldHit(monster) && trigger.position.ExactDistance(monster.position.tile) <= radius)
			HeraldHit(monster, PersistentSkill::HeraldOfAsh, DamageType::Fire, damage);
	}
}

void ShatterFrost(const HeraldTrigger &trigger)
{
	AddVisualEffect(trigger.position, MissileGraphicID::BlueFlareExplosion);
	const Fixed26_6 damage = Fixed26_6::fromRaw(static_cast<int32_t>(static_cast<int64_t>(trigger.lifeRaw) * HeraldOfFrostPercent * MapAuraPercent() / 10000));
	const int radius = SupportAreaRadius(*MyPlayer, SkillSlot::ForPersistent(PersistentSkill::HeraldOfFrost), HeraldOfFrostRadius);
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (CanHeraldHit(monster) && trigger.position.ExactDistance(monster.position.tile) <= radius)
			HeraldHit(monster, PersistentSkill::HeraldOfFrost, DamageType::Magic, damage);
	}
}

void ReleaseThunder(const HeraldTrigger &trigger)
{
	std::vector<Monster *> targets;
	const int radius = SupportAreaRadius(*MyPlayer, SkillSlot::ForPersistent(PersistentSkill::HeraldOfThunder), HeraldOfThunderRadius);
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (CanHeraldHit(monster) && trigger.position.ExactDistance(monster.position.tile) <= radius)
			targets.push_back(&monster);
	}
	std::sort(targets.begin(), targets.end(), [&trigger](const Monster *a, const Monster *b) {
		return trigger.position.ExactDistance(a->position.tile) < trigger.position.ExactDistance(b->position.tile);
	});
	if (targets.size() > HeraldOfThunderBolts)
		targets.resize(HeraldOfThunderBolts);

	AddVisualEffect(trigger.position, MissileGraphicID::BlueFlareExplosion2);
	const Fixed26_6 damage = Fixed26_6::fromRaw(static_cast<int32_t>(static_cast<int64_t>(trigger.lifeRaw) * HeraldOfThunderPercent * MapAuraPercent() / 10000));
	for (Monster *monster : targets) {
		AddVisualEffect(monster->position.tile, MissileGraphicID::LightningArrow);
		HeraldHit(*monster, PersistentSkill::HeraldOfThunder, DamageType::Lightning, damage);
	}
}

} // namespace

std::string_view PersistentSkillName(PersistentSkill skill)
{
	switch (skill) {
	case PersistentSkill::HeraldOfAsh:
		return _("Herald of Ash");
	case PersistentSkill::HeraldOfThunder:
		return _("Herald of Thunder");
	case PersistentSkill::PurityOfElements:
		return _("Purity of Elements");
	case PersistentSkill::SummonGolem:
		return _("Summon Golem");
	case PersistentSkill::SkeletalWarriors:
		return _("Skeletal Warriors");
	case PersistentSkill::HeraldOfFrost:
		return _("Herald of Frost");
	case PersistentSkill::SkeletalMages:
		return _("Skeletal Mages");
	case PersistentSkill::BloodGolem:
		return _("Blood Golem");
	case PersistentSkill::FireGolem:
		return _("Fire Golem");
	case PersistentSkill::Guardian:
		return _("Guardian");
	case PersistentSkill::ShadowWarrior:
		return _("Shadow Warrior");
	case PersistentSkill::Revive:
		return _("Revive");
	case PersistentSkill::ManaShield:
		return _("Mana Shield");
	case PersistentSkill::VizjereiFocus:
		return _("Vizjerei Focus");
	case PersistentSkill::Berserk:
		return _("Berserk");
	case PersistentSkill::Reflect:
		return _("Reflect");
	default:
		return {};
	}
}

std::string_view PersistentSkillDescription(PersistentSkill skill)
{
	switch (skill) {
	case PersistentSkill::HeraldOfAsh:
		return _("Enemies you kill explode, burning those nearby");
	case PersistentSkill::HeraldOfThunder:
		return _("Enemies you kill release bolts of lightning");
	case PersistentSkill::PurityOfElements:
		return _("Aura: +25% to fire, lightning and magic resistance");
	case PersistentSkill::SummonGolem:
		return _("A golem fights for you and returns when it falls");
	case PersistentSkill::SkeletalWarriors:
		return _("Skeletons fight for you and rise again when they fall");
	case PersistentSkill::HeraldOfFrost:
		return _("Chilled or frozen enemies you kill shatter, freezing those nearby");
	case PersistentSkill::SkeletalMages:
		return _("Skeletal mages hurl fire from range and rise again when they fall");
	case PersistentSkill::BloodGolem:
		return _("A tough golem of blood whose blows heal you for 20% of the damage. One golem at a time");
	case PersistentSkill::FireGolem:
		return _("A golem of fire: its blows burn, and so does standing next to it. One golem at a time");
	case PersistentSkill::Guardian:
		return _("Every 8 seconds a fire-breathing guardian rises near you when enemies come close");
	case PersistentSkill::ShadowWarrior:
		return _("Your shadow repeats every skill you use at 40% damage");
	case PersistentSkill::Revive:
		return _("The strongest monster you kill rises to fight for you, keeping its strength");
	case PersistentSkill::ManaShield:
		return _("30% of the damage you take comes from mana while you have mana");
	case PersistentSkill::VizjereiFocus:
		return _("Spells deal 4% more damage per 100 maximum mana, but cost 6% of maximum mana more");
	case PersistentSkill::Berserk:
		return _("Melee hits build Rage and you deal 20% more attack damage, but take 15% more damage");
	case PersistentSkill::Reflect:
		return _("Enemies that hit you in melee take 30% of the damage back");
	default:
		return {};
	}
}

int SpiritReservation(PersistentSkill skill)
{
	switch (skill) {
	case PersistentSkill::HeraldOfAsh:
	case PersistentSkill::HeraldOfThunder:
	case PersistentSkill::HeraldOfFrost:
		return 30;
	case PersistentSkill::PurityOfElements:
		return 60;
	case PersistentSkill::SummonGolem:
		return 40;
	case PersistentSkill::SkeletalWarriors:
		return 30;
	case PersistentSkill::SkeletalMages:
		return 40;
	case PersistentSkill::BloodGolem:
	case PersistentSkill::FireGolem:
		return 50;
	case PersistentSkill::Guardian:
		return 35;
	case PersistentSkill::ShadowWarrior:
		return 60;
	case PersistentSkill::Revive:
		return 50;
	case PersistentSkill::ManaShield:
		return 30;
	case PersistentSkill::VizjereiFocus:
		return 40;
	case PersistentSkill::Berserk:
		return 30;
	case PersistentSkill::Reflect:
		return 30;
	default:
		return 0;
	}
}

int GemSpiritCost(const Player &player, PersistentSkill skill)
{
	const int spirit = SpiritReservation(skill) + SupportAddedSpirit(player, SkillSlot::ForPersistent(skill));
	// POE2 Mashup: some uniques raise every reservation.
	return spirit * (100 + player.poe2.rule(UniqueRule::MoreSpiritReservation)) / 100;
}

bool ItemGrantsSpirit(const Item &item)
{
	if (item.isEmpty() || item.IDidx < 0 || static_cast<size_t>(item.IDidx) >= AllItemsList.size())
		return false;
	const std::string_view name = AllItemsList[static_cast<size_t>(item.IDidx)].iName;
	return name.ends_with("Sceptre");
}

int MinionDamagePercent(const Player &player)
{
	return 100 + GetPassiveTotals(player)[static_cast<size_t>(PassiveStat::MinionDamage)] + player.poe2.itemStats[static_cast<size_t>(Poe2ItemStat::MinionDamage)]
	    + MinionSpellDamageBonus(player);
}

int SkullSpirit(const Player &player)
{
	int spirit = 0;
	for (size_t skull = 0; skull < NumSpiritSkulls; skull++) {
		if ((player.poe2.spiritSkulls & (1 << skull)) != 0)
			spirit += SpiritSkullSpirit[skull];
	}
	return spirit;
}

int SpiritSkullIndex(const Item &item)
{
	if (item.isEmpty() || item._iMiscId != IMISC_SPIRIT_SKULL)
		return -1;
	// The skulls are listed in order in the item table.
	int skull = 0;
	for (int i = 0; i < item.IDidx && i < static_cast<int>(AllItemsList.size()); i++) {
		if (AllItemsList[i].iMiscId == IMISC_SPIRIT_SKULL)
			skull++;
	}
	return skull < static_cast<int>(NumSpiritSkulls) ? skull : -1;
}

bool UseSpiritSkull(Player &player, const Item &item)
{
	const int skull = SpiritSkullIndex(item);
	if (skull < 0)
		return false;
	if ((player.poe2.spiritSkulls & (1 << skull)) != 0) {
		EventPlrMsg(_("You have already released this skull's energies."), UiFlags::ColorRed);
		return false;
	}
	player.poe2.spiritSkulls |= static_cast<uint8_t>(1 << skull);
	EventPlrMsg(FormatRuntime(_("+{:d} to Maximum Spirit"), SpiritSkullSpirit[skull]), UiFlags::ColorWhitegold);
	TelemetryLog("spirit", FormatRuntime("broke {:s} for +{:d} Spirit, now {:d}", item._iIName, SpiritSkullSpirit[skull], MaxSpirit(player)));
	return true;
}

namespace {

/** @brief The skull a boss guards, like POE2's Spirit quest bosses; -1 for none. */
int SkullForMonster(const Monster &monster)
{
	switch (monster.uniqueType) {
	// The first comes from the early quest bosses; at least one of them is in every game.
	case UniqueMonsterType::SkeletonKing:
	case UniqueMonsterType::Butcher:
	case UniqueMonsterType::Garbud:
	case UniqueMonsterType::SnotSpill:
		return 0;
	case UniqueMonsterType::Lazarus:
		return 2;
	default:
		break;
	}
	// The second comes from the first unique monster beaten in the caves or deeper.
	if (monster.isUnique() && currlevel >= 9)
		return 1;
	return -1;
}

std::optional<size_t> SpiritSkullItemIndex(int skull)
{
	int found = 0;
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		if (AllItemsList[i].iMiscId != IMISC_SPIRIT_SKULL)
			continue;
		if (found == skull)
			return i;
		found++;
	}
	return std::nullopt;
}

} // namespace

void DropSpiritSkull(const Monster &monster, bool sendmsg)
{
	if (!gbIsPoe2 || MyPlayer == nullptr || monster.isPlayerMinion())
		return;
	const int skull = SkullForMonster(monster);
	if (skull < 0 || (MyPlayer->poe2.spiritSkulls & (1 << skull)) != 0)
		return;
	if (const std::optional<size_t> index = SpiritSkullItemIndex(skull); index)
		SpawnBaseItem(monster.position.tile, static_cast<_item_indexes>(*index), sendmsg);
}

int MaxSpirit(const Player &player)
{
	int spirit = BaseSpirit + SkullSpirit(player) + GetPassiveTotals(player)[static_cast<size_t>(PassiveStat::Spirit)] + player.poe2.itemStats[static_cast<size_t>(Poe2ItemStat::Spirit)];
	for (const inv_body_loc hand : { INVLOC_HAND_LEFT, INVLOC_HAND_RIGHT }) {
		const Item &item = player.InvBody[hand];
		if (ItemGrantsSpirit(item) && item._iStatFlag)
			spirit += SceptreSpirit;
	}
	const Item &chest = player.InvBody[INVLOC_CHEST];
	if (chest._iStatFlag)
		spirit += GetBodyArmourImplicit(chest).spirit;
	return spirit;
}

int MinionCountSkillIndex(PersistentSkill skill)
{
	for (size_t i = 0; i < MinionCountSkills.size(); i++) {
		if (MinionCountSkills[i] == skill)
			return static_cast<int>(i);
	}
	return -1;
}

int FreeMinions(PersistentSkill skill)
{
	return skill == PersistentSkill::SkeletalWarriors ? 2 : 0;
}

int StartingMinionCount(PersistentSkill skill)
{
	return skill == PersistentSkill::SkeletalWarriors ? 2 : 1;
}

int BaseSpiritPerMinion(PersistentSkill skill, int gemLevel)
{
	// POE2's Spirit per minion for gem levels 1 to 20: Skeletal Warrior, and Skeletal Arsonist for the fire-hurling mages.
	constexpr std::array<uint8_t, MaxGemLevel> Warrior { 60, 44, 33, 27, 25, 25, 23, 23, 23, 22, 22, 22, 21, 21, 21, 21, 20, 20, 20, 20 };
	constexpr std::array<uint8_t, MaxGemLevel> Mage { 90, 65, 50, 47, 47, 47, 44, 44, 44, 42, 42, 42, 40, 40, 40, 40, 39, 39, 39, 39 };
	const auto level = static_cast<size_t>(std::clamp(gemLevel, 1, MaxGemLevel) - 1);
	return (skill == PersistentSkill::SkeletalMages ? Mage : Warrior)[level];
}

int SpiritPerMinion(const Player &player, PersistentSkill skill)
{
	const int base = BaseSpiritPerMinion(skill, player.poe2.skillLevel(skill));
	// POE2 Mashup: some uniques raise every reservation.
	return std::max(base * (100 + player.poe2.rule(UniqueRule::MoreSpiritReservation)) / 100, 1);
}

std::string SpiritCostText(PersistentSkill skill, int gemLevel)
{
	if (!IsMinionCountSkill(skill))
		return FormatRuntime(_("Reserves {:d} Spirit"), SpiritReservation(skill));
	if (const int free = FreeMinions(skill); free > 0)
		return FormatRuntime(_("{:d} minions free, then {:d} Spirit each"), free, BaseSpiritPerMinion(skill, gemLevel));
	return FormatRuntime(_("Reserves {:d} Spirit per minion"), BaseSpiritPerMinion(skill, gemLevel));
}

int ChosenMinionCount(const Player &player, PersistentSkill skill)
{
	const int index = MinionCountSkillIndex(skill);
	if (index < 0)
		return 0;
	const uint8_t count = player.poe2.minionCounts[static_cast<size_t>(index)];
	return count == DefaultMinionCount ? StartingMinionCount(skill) : std::min<int>(count, MaxMinionsPerGem);
}

void SetChosenMinionCount(Player &player, PersistentSkill skill, int count)
{
	const int index = MinionCountSkillIndex(skill);
	if (index < 0)
		return;
	player.poe2.minionCounts[static_cast<size_t>(index)] = static_cast<uint8_t>(std::clamp<int>(count, 0, MaxMinionsPerGem));
}

int MinionLevelForGemLevel(int gemLevel)
{
	if (gemLevel >= MaxGemLevel)
		return CharacterLevelForGemLevel(MaxGemLevel);
	return std::max(CharacterLevelForGemLevel(gemLevel + 1) - 1, 1);
}

namespace {

struct GemReservation {
	PersistentSkill skill;
	/** Minions the gem keeps, or 1 for a gem without a count; 0 when it's inactive. */
	int count;
	int spirit;
	/** Spirit the gems before this one left. */
	int spiritLeft;
};

/**
 * @brief Walks the Skills panel in order, reserving Spirit for each usable Spirit gem that fits.
 * A minion gem reserves Spirit for each minion past its free ones and, like POE2, keeps as many as the Spirit left fits.
 */
template <typename F>
int ReserveSpirit(const Player &player, F &&onGem)
{
	const int max = MaxSpirit(player);
	int reserved = 0;
	for (size_t socket = 0; socket < player.poe2.skillSlots.size(); socket++) {
		const PersistentSkill skill = player.poe2.skillSlots[socket].persistent();
		if (skill == PersistentSkill::None || !MeetsGemRequirements(player, skill))
			continue;
		const int spiritLeft = max - reserved;
		// A gem switched off in the Skills panel reserves nothing and does nothing.
		if (!player.poe2.spiritGemOn(socket)) {
			onGem(GemReservation { skill, 0, 0, spiritLeft });
			continue;
		}
		if (IsMinionCountSkill(skill)) {
			const int chosen = ChosenMinionCount(player, skill);
			const int free = std::min(chosen, FreeMinions(skill));
			const int perMinion = SpiritPerMinion(player, skill);
			const int paid = std::min(chosen - free, std::max(spiritLeft, 0) / perMinion);
			reserved += paid * perMinion;
			onGem(GemReservation { skill, free + paid, paid * perMinion, spiritLeft });
			continue;
		}
		const int cost = GemSpiritCost(player, skill);
		if (cost > spiritLeft) {
			onGem(GemReservation { skill, 0, 0, spiritLeft });
			continue;
		}
		reserved += cost;
		onGem(GemReservation { skill, 1, cost, spiritLeft });
	}
	return reserved;
}

std::optional<GemReservation> FindGemReservation(const Player &player, PersistentSkill skill)
{
	std::optional<GemReservation> found;
	ReserveSpirit(player, [&](const GemReservation &gem) {
		if (gem.skill == skill && !found)
			found = gem;
	});
	return found;
}

} // namespace

int ReservedSpirit(const Player &player)
{
	return ReserveSpirit(player, [](const GemReservation &) {});
}

bool ToggleSpiritGem(Player &player, size_t socket)
{
	if (!gbIsPoe2 || socket >= NumSkillSlots)
		return false;
	const PersistentSkill skill = player.poe2.skillSlots[socket].persistent();
	if (skill == PersistentSkill::None)
		return false;
	player.poe2.spiritGemsOn ^= static_cast<uint16_t>(1U << socket);
	TelemetryLog("spirit", StrCat(PersistentSkillName(skill), player.poe2.spiritGemOn(socket) ? " switched on" : " switched off"));
	// Spirit gems can change stats, like Purity of Elements' resistances.
	CalcPlrInv(player, false);
	return true;
}

bool IsPersistentSkillActive(const Player &player, PersistentSkill skill)
{
	if (!gbIsPoe2 || skill == PersistentSkill::None)
		return false;
	const std::optional<GemReservation> gem = FindGemReservation(player, skill);
	return gem && gem->count > 0;
}

int ActiveMinionCount(const Player &player, PersistentSkill skill)
{
	if (!gbIsPoe2 || !IsMinionCountSkill(skill))
		return 0;
	const std::optional<GemReservation> gem = FindGemReservation(player, skill);
	return gem ? gem->count : 0;
}

int AffordableMinionCount(const Player &player, PersistentSkill skill)
{
	const std::optional<GemReservation> gem = FindGemReservation(player, skill);
	if (!gem || !IsMinionCountSkill(skill))
		return 0;
	return std::min(FreeMinions(skill) + std::max(gem->spiritLeft, 0) / SpiritPerMinion(player, skill), static_cast<int>(MaxMinionsPerGem));
}

int GemSpiritReservation(const Player &player, PersistentSkill skill)
{
	const std::optional<GemReservation> gem = FindGemReservation(player, skill);
	return gem ? gem->spirit : 0;
}

void QueueHeraldTriggers(const Player &player, const Monster &monster)
{
	if (!gbIsPoe2 || &player != MyPlayer)
		return;
	const bool ash = IsPersistentSkillActive(player, PersistentSkill::HeraldOfAsh);
	const bool thunder = IsPersistentSkillActive(player, PersistentSkill::HeraldOfThunder);
	const bool frost = (monster.poe2.chillTicks > 0 || monster.poe2.freezeTicks > 0) && IsPersistentSkillActive(player, PersistentSkill::HeraldOfFrost);
	if (!ash && !thunder && !frost)
		return;
	PendingHeralds.push_back({ monster.position.tile, monster.maxHitPoints.raw(), ash, thunder, frost });
}

void ProcessHeralds()
{
	if (PendingHeralds.empty() || MyPlayer == nullptr)
		return;
	// Kills from these heralds queue more, which go off on the next tick: a chain reaction, not recursion.
	std::vector<HeraldTrigger> triggers;
	triggers.swap(PendingHeralds);
	for (const HeraldTrigger &trigger : triggers) {
		if (trigger.ash)
			ExplodeAsh(trigger);
		if (trigger.thunder)
			ReleaseThunder(trigger);
		if (trigger.frost)
			ShatterFrost(trigger);
	}
}

void ClearHeralds()
{
	PendingHeralds.clear();
}

int SkeletalWarriorCount(int characterLevel)
{
	return std::min(2 + (characterLevel / 10), 4);
}

int SkeletalMageCount(int characterLevel)
{
	return std::min(1 + (characterLevel / 20), 3);
}

namespace {

std::optional<Point> FindMinionSpawnPosition(const Player &player)
{
	return FindClosestValidPosition(
	    [&player](Point target) { return !IsTileOccupied(target) && LineClearMissile(player.position.tile, target); },
	    player.position.tile, 1, 5);
}

bool CanRaiseMinions(const Player &player)
{
	return leveltype != DTYPE_TOWN && player.isLevelOwnedByLocalClient();
}

void KeepGolemAlive(Player &player)
{
	Monster *golem = FindGolemForPlayer(player);
	const PersistentSkill golemSkill = ActiveGolemSkill(player);
	if (golemSkill == PersistentSkill::None) {
		player.poe2.golemCooldown = 0;
		return;
	}
	if (golem != nullptr || !CanRaiseMinions(player)) {
		player.poe2.golemCooldown = GolemReviveTicks / 4;
		return;
	}
	if (player.poe2.golemCooldown > 0) {
		player.poe2.golemCooldown--;
		return;
	}
	const std::optional<Point> position = FindMinionSpawnPosition(player);
	if (!position)
		return;
	SpawnGolem(player, *position, static_cast<uint8_t>(std::max(player.poe2.skillLevel(golemSkill), 1)));
	if (Monster *golem = FindGolemForPlayer(player); golem != nullptr) {
		golem->minDamage = static_cast<uint8_t>(std::min(golem->minDamage * MinionDamagePercent(player) / 100, 255));
		golem->maxDamage = static_cast<uint8_t>(std::min(golem->maxDamage * MinionDamagePercent(player) / 100, 255));
		// The golem skill's supports change its life.
		if (const int percent = GolemLifePercent(player); percent != 100) {
			golem->maxHitPoints = Fixed26_6::fromRaw(std::max<int32_t>(golem->maxHitPoints.raw() * percent / 100, 64));
			golem->hitPoints = golem->maxHitPoints;
		}
	}
	AddVisualEffect(*position, MissileGraphicID::Resurrect);
	player.poe2.golemCooldown = GolemReviveTicks;
}

/** @brief Kills the skeleton of the type farthest from its owner, when the count is lowered. */
void KillFarthestSkeletonMinion(const Player &player, _monster_id type)
{
	Monster *farthest = nullptr;
	int farthestDistance = -1;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (monster.type().type != type || !monster.isPlayerMinion() || monster.goalVar3 != player.getId() || monster.hasNoLife() || monster.poe2.revived
		    || (monster.flags & MFLAG_KINGS_DEAD) != 0)
			continue;
		const int distance = monster.position.tile.WalkingDistance(player.position.tile);
		if (distance > farthestDistance) {
			farthest = &monster;
			farthestDistance = distance;
		}
	}
	if (farthest != nullptr)
		KillGolem(*farthest);
}

int16_t CurrentLevelKey()
{
	return static_cast<int16_t>(setlevel ? 1000 + static_cast<int>(setlvlnum) : static_cast<int>(currlevel));
}

void RemoveRevive(MinionRevives &revives, size_t index)
{
	revives.ticks[index] = revives.ticks[revives.pending - 1];
	revives.pending--;
}

_monster_id MinionMonsterType(PersistentSkill skill)
{
	return skill == PersistentSkill::SkeletalMages ? SkeletonMageMinionType : SkeletonMinionType;
}

int TargetMinions(const Player &player, PersistentSkill skill, int active)
{
	return std::min(active + SupportExtraMinions(player, skill), static_cast<int>(MinionRevives {}.ticks.size()));
}

/**
 * @brief Keeps the gem's skeletons at its count. Like POE2, each one that falls rises again after the revive time;
 * ones that haven't been raised yet (on a new level, or when the count goes up) come up at once, a few ticks apart.
 */
void KeepSkeletonsAlive(Player &player, PersistentSkill skill, _monster_id type, MinionRevives &revives)
{
	const int active = ActiveMinionCount(player, skill);
	if (active <= 0) {
		revives = {};
		if (CountSkeletonMinions(player, type) > 0)
			KillSkeletonMinions(player, type);
		return;
	}
	if (!CanRaiseMinions(player)) {
		revives = {};
		return;
	}
	if (revives.level != CurrentLevelKey()) {
		revives = {};
		revives.level = CurrentLevelKey();
	}
	const int target = TargetMinions(player, skill, active);
	int alive = static_cast<int>(CountSkeletonMinions(player, type));
	if (alive > target) {
		KillFarthestSkeletonMinion(player, type);
		revives.pending = 0;
		revives.lastAlive = static_cast<uint8_t>(alive - 1);
		return;
	}
	for (int fallen = revives.lastAlive - alive; fallen > 0 && revives.pending < revives.ticks.size(); fallen--)
		revives.ticks[revives.pending++] = SkeletonReviveTicks;
	// A lower count no longer needs the longest revives.
	while (revives.pending > target - alive) {
		size_t longest = 0;
		for (size_t i = 1; i < revives.pending; i++) {
			if (revives.ticks[i] > revives.ticks[longest])
				longest = i;
		}
		RemoveRevive(revives, longest);
	}
	std::optional<size_t> ready;
	for (size_t i = 0; i < revives.pending; i++) {
		if (revives.ticks[i] > 0)
			revives.ticks[i]--;
		if (revives.ticks[i] == 0 && !ready)
			ready = i;
	}
	if (revives.raiseTicks > 0)
		revives.raiseTicks--;
	const bool raiseNew = alive + revives.pending < target && revives.raiseTicks == 0;
	if (ready || raiseNew) {
		const std::optional<Point> position = FindMinionSpawnPosition(player);
		if (position && SpawnSkeletonMinion(player, *position, static_cast<uint8_t>(std::max(player.poe2.skillLevel(skill), 1)), type)) {
			AddVisualEffect(*position, MissileGraphicID::Resurrect);
			alive++;
			if (ready)
				RemoveRevive(revives, *ready);
			else
				revives.raiseTicks = SkeletonRaiseTicks;
		}
	}
	revives.lastAlive = static_cast<uint8_t>(alive);
}

} // namespace

void HastenMinionRevives(Player &player, int ticks)
{
	for (MinionRevives &revives : player.poe2.minionRevives) {
		for (size_t i = 0; i < revives.pending; i++)
			revives.ticks[i] = static_cast<uint16_t>(std::min<int>(revives.ticks[i], std::max(ticks, 0)));
	}
}

int PendingMinionRevives(const Player &player, PersistentSkill skill, int *ticksLeft)
{
	const int index = MinionCountSkillIndex(skill);
	if (index < 0)
		return 0;
	const MinionRevives &revives = player.poe2.minionRevives[static_cast<size_t>(index)];
	if (ticksLeft != nullptr) {
		int soonest = 0;
		for (size_t i = 0; i < revives.pending; i++)
			soonest = i == 0 ? revives.ticks[i] : std::min<int>(soonest, revives.ticks[i]);
		*ticksLeft = soonest;
	}
	return revives.pending;
}

int MinionTarget(const Player &player, PersistentSkill skill)
{
	const int active = ActiveMinionCount(player, skill);
	if (active <= 0 || !CanRaiseMinions(player))
		return 0;
	return TargetMinions(player, skill, active);
}

int AliveMinionCount(const Player &player, PersistentSkill skill)
{
	return IsMinionCountSkill(skill) ? static_cast<int>(CountSkeletonMinions(player, MinionMonsterType(skill))) : 0;
}

void ProcessPersistentSkills(Player &player)
{
	if (!gbIsPoe2 || &player != MyPlayer || player.hasNoLife())
		return;
	KeepGolemAlive(player);
	for (size_t i = 0; i < MinionCountSkills.size(); i++) {
		const PersistentSkill skill = MinionCountSkills[i];
		KeepSkeletonsAlive(player, skill, MinionMonsterType(skill), player.poe2.minionRevives[i]);
	}
	ProcessSpiritSkills(player);
}

} // namespace devilution
