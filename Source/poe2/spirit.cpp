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
#include "utils/format.hpp"
#include "utils/language.h"

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

/** @brief Deals herald damage to a monster on behalf of the local player, killing it if it drops. */
void HeraldHit(Monster &monster, DamageType damageType, Fixed26_6 damage)
{
	Player &player = *MyPlayer;
	TelemetryNoteDamageSource(damageType == DamageType::Fire ? "Herald of Ash" : damageType == DamageType::Lightning ? "Herald of Thunder"
	                                                                                                                 : "Herald of Frost");
	ApplyMonsterDamage(damageType, monster, damage);
	monster.tag(player);
	TryInflictAilment(monster, player, damageType, damage);
	if (monster.hasNoLife())
		M_StartKill(monster, player);
}

void ExplodeAsh(const HeraldTrigger &trigger)
{
	AddVisualEffect(trigger.position, MissileGraphicID::BigExplosion);
	const Fixed26_6 damage = Fixed26_6::fromRaw(static_cast<int32_t>(static_cast<int64_t>(trigger.lifeRaw) * HeraldOfAshPercent * MapAuraPercent() / 10000));
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (CanHeraldHit(monster) && trigger.position.ExactDistance(monster.position.tile) <= HeraldOfAshRadius)
			HeraldHit(monster, DamageType::Fire, damage);
	}
}

void ShatterFrost(const HeraldTrigger &trigger)
{
	AddVisualEffect(trigger.position, MissileGraphicID::BlueFlareExplosion);
	const Fixed26_6 damage = Fixed26_6::fromRaw(static_cast<int32_t>(static_cast<int64_t>(trigger.lifeRaw) * HeraldOfFrostPercent * MapAuraPercent() / 10000));
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (CanHeraldHit(monster) && trigger.position.ExactDistance(monster.position.tile) <= HeraldOfFrostRadius)
			HeraldHit(monster, DamageType::Magic, damage);
	}
}

void ReleaseThunder(const HeraldTrigger &trigger)
{
	std::vector<Monster *> targets;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (CanHeraldHit(monster) && trigger.position.ExactDistance(monster.position.tile) <= HeraldOfThunderRadius)
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
		HeraldHit(*monster, DamageType::Lightning, damage);
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

bool ItemGrantsSpirit(const Item &item)
{
	if (item.isEmpty() || item.IDidx < 0 || static_cast<size_t>(item.IDidx) >= AllItemsList.size())
		return false;
	const std::string_view name = AllItemsList[static_cast<size_t>(item.IDidx)].iName;
	return name.ends_with("Sceptre");
}

int MinionDamagePercent(const Player &player)
{
	return 100 + GetPassiveTotals(player)[static_cast<size_t>(PassiveStat::MinionDamage)] + player.poe2.itemStats[static_cast<size_t>(Poe2ItemStat::MinionDamage)];
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

namespace {

/** @brief Walks the Skills panel in order, reserving Spirit for each usable Spirit gem that fits. */
template <typename F>
int ReserveSpirit(const Player &player, F &&onActive)
{
	const int max = MaxSpirit(player);
	int reserved = 0;
	for (const SkillSlot &slot : player.poe2.skillSlots) {
		const PersistentSkill skill = slot.persistent();
		if (skill == PersistentSkill::None || !MeetsGemRequirements(player, skill))
			continue;
		// POE2 Mashup: some uniques raise every reservation.
		const int cost = SpiritReservation(skill) * (100 + player.poe2.rule(UniqueRule::MoreSpiritReservation)) / 100;
		if (reserved + cost > max)
			continue;
		reserved += cost;
		onActive(skill);
	}
	return reserved;
}

} // namespace

int ReservedSpirit(const Player &player)
{
	return ReserveSpirit(player, [](PersistentSkill) {});
}

bool IsPersistentSkillActive(const Player &player, PersistentSkill skill)
{
	if (!gbIsPoe2 || skill == PersistentSkill::None)
		return false;
	bool active = false;
	ReserveSpirit(player, [&](PersistentSkill activeSkill) {
		if (activeSkill == skill)
			active = true;
	});
	return active;
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

template <typename Cooldown>
void KeepSkeletonsAlive(Player &player, PersistentSkill skill, _monster_id type, int count, Cooldown &cooldown)
{
	if (!IsPersistentSkillActive(player, skill)) {
		cooldown = 0;
		if (CountSkeletonMinions(player, type) > 0)
			KillSkeletonMinions(player, type);
		return;
	}
	count += SupportExtraMinions(player, skill);
	if (!CanRaiseMinions(player) || CountSkeletonMinions(player, type) >= static_cast<size_t>(std::max(count, 0))) {
		cooldown = SkeletonReviveTicks / 4;
		return;
	}
	if (cooldown > 0) {
		cooldown--;
		return;
	}
	const std::optional<Point> position = FindMinionSpawnPosition(player);
	if (!position)
		return;
	if (SpawnSkeletonMinion(player, *position, static_cast<uint8_t>(std::max(player.poe2.skillLevel(skill), 1)), type))
		AddVisualEffect(*position, MissileGraphicID::Resurrect);
	cooldown = SkeletonReviveTicks;
}

} // namespace

void ProcessPersistentSkills(Player &player)
{
	if (!gbIsPoe2 || &player != MyPlayer || player.hasNoLife())
		return;
	KeepGolemAlive(player);
	KeepSkeletonsAlive(player, PersistentSkill::SkeletalWarriors, SkeletonMinionType, SkeletalWarriorCount(player.getCharacterLevel()), player.poe2.skeletonCooldown);
	KeepSkeletonsAlive(player, PersistentSkill::SkeletalMages, SkeletonMageMinionType, SkeletalMageCount(player.getCharacterLevel()), player.poe2.mageCooldown);
	ProcessSpiritSkills(player);
}

} // namespace devilution
