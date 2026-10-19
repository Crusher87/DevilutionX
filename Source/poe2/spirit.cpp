#include "poe2/spirit.hpp"

#include <algorithm>
#include <optional>
#include <vector>

#include "engine/path.h"
#include "engine/random.hpp"
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
#include "poe2/meta_gems.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/spirit_buffs.hpp"
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
	/** Herald of Plague: the dead monster's poison, to spread. */
	uint8_t poisonTicks = 0;
	int32_t poisonPerTick = 0;
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

/** @brief Herald of Plague: the poison of the poisoned monster that died passes to those around, unless theirs is stronger. */
void SpreadPlague(const HeraldTrigger &trigger)
{
	Player &player = *MyPlayer;
	AddVisualEffect(trigger.position, MissileGraphicID::AcidSplat);
	const int radius = SupportAreaRadius(player, SkillSlot::ForPersistent(PersistentSkill::HeraldOfPlague), HeraldOfPlagueRadius);
	const int hinderChance = HeraldOfPlagueHinderChance(std::max(player.poe2.skillLevel(PersistentSkill::HeraldOfPlague), 1));
	int spread = 0;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (!CanHeraldHit(monster) || trigger.position.ExactDistance(monster.position.tile) > radius)
			continue;
		Poe2MonsterState &state = monster.poe2;
		if (state.poisonTicks > 0 && state.poisonDamagePerTick > trigger.poisonPerTick)
			continue;
		state.poisonDamagePerTick = trigger.poisonPerTick;
		state.poisonTicks = std::max(state.poisonTicks, trigger.poisonTicks);
		state.poisonSource = static_cast<int8_t>(MyPlayerId);
		monster.tag(player);
		AddVisualEffect(monster.position.tile, MissileGraphicID::Acid);
		if (GenerateRnd(100) < hinderChance)
			state.hinderTicks = std::max<uint8_t>(state.hinderTicks, HeraldOfPlagueHinderTicks);
		spread++;
	}
	if (spread > 0)
		TelemetryLog("spirit", StrCat("Herald of Plague spread poison to ", spread));
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
	case PersistentSkill::CastOnDodge:
		return _("Cast on Dodge");
	case PersistentSkill::CastWhenDamageTaken:
		return _("Cast when Damage Taken");
	case PersistentSkill::CastWhileChannelling:
		return _("Cast while Channelling");
	case PersistentSkill::ReapersInvocation:
		return _("Reaper's Invocation");
	case PersistentSkill::MirageArcher:
		return _("Mirage Archer");
	case PersistentSkill::Blasphemy:
		return _("Blasphemy");
	case PersistentSkill::HoradricTotem:
		return _("Horadric Totem");
	case PersistentSkill::CastOnCritical:
		return _("Cast on Critical");
	case PersistentSkill::CastOnElementalAilment:
		return _("Cast on Elemental Ailment");
	case PersistentSkill::CastOnMinionDeath:
		return _("Cast on Minion Death");
	case PersistentSkill::BarrierInvocation:
		return _("Barrier Invocation");
	case PersistentSkill::ElementalInvocation:
		return _("Elemental Invocation");
	case PersistentSkill::WindDancer:
		return _("Wind Dancer");
	case PersistentSkill::GhostDance:
		return _("Ghost Dance");
	case PersistentSkill::Barkskin:
		return _("Barkskin");
	case PersistentSkill::ArcticArmour:
		return _("Arctic Armour");
	case PersistentSkill::HeraldOfPlague:
		return _("Herald of Plague");
	case PersistentSkill::CombatFrenzy:
		return _("Combat Frenzy");
	case PersistentSkill::EternalRage:
		return _("Eternal Rage");
	case PersistentSkill::ElementalConflux:
		return _("Elemental Conflux");
	case PersistentSkill::AlchemistsBoon:
		return _("Alchemist's Boon");
	case PersistentSkill::OverwhelmingPresence:
		return _("Overwhelming Presence");
	case PersistentSkill::TrailOfCaltrops:
		return _("Trail of Caltrops");
	case PersistentSkill::ManaRemnants:
		return _("Mana Remnants");
	case PersistentSkill::Attrition:
		return _("Attrition");
	case PersistentSkill::Blink:
		return _("Blink");
	case PersistentSkill::IronWard:
		return _("Iron Ward");
	case PersistentSkill::PlagueBearer:
		return _("Plague Bearer");
	case PersistentSkill::Convalescence:
		return _("Convalescence");
	case PersistentSkill::GrimFeast:
		return _("Grim Feast");
	case PersistentSkill::WarBanner:
		return _("War Banner");
	case PersistentSkill::DefianceBanner:
		return _("Defiance Banner");
	case PersistentSkill::DreadBanner:
		return _("Dread Banner");
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
	case PersistentSkill::CastOnDodge:
		return _("Meta gem: holds spells in its sockets. Dodge rolling fills its Energy, and when it's full the spells inside all cast");
	case PersistentSkill::CastWhenDamageTaken:
		return _("Meta gem: holds spells in its sockets. Each hit you take fills its Energy, and when it's full the spells inside all cast");
	case PersistentSkill::CastWhileChannelling:
		return _("Meta gem: holds spells in its sockets. Channelling a stream skill fills its Energy, and when it's full the spells inside all cast");
	case PersistentSkill::ReapersInvocation:
		return _("Meta gem: holds spells in its sockets. Melee kills store Energy, up to 500; use it from the skill tray to cast the spells inside");
	case PersistentSkill::MirageArcher:
		return _("Each dodge roll leaves a mirage that shoots your bow attack for 5 seconds, with 30% less damage; once every 10 seconds");
	case PersistentSkill::Blasphemy:
		return _("Meta gem: holds curses in its sockets. They curse every enemy around you, at half strength; it reserves 60 Spirit for each curse");
	case PersistentSkill::HoradricTotem:
		return _("Meta gem: holds spells in its sockets. Use it from the skill tray to place a totem that casts them at enemies for 8 seconds, 25% slower");
	case PersistentSkill::CastOnCritical:
		return _("Meta gem: holds spells in its sockets. Critical hits fill its Energy, more on stronger enemies; the spells inside deal 20% less damage");
	case PersistentSkill::CastOnElementalAilment:
		return _("Meta gem: holds spells in its sockets. Freezing, shocking and igniting enemies fills its Energy, and when it's full the spells inside all cast");
	case PersistentSkill::CastOnMinionDeath:
		return _("Meta gem: holds spells in its sockets. Each of your minions that dies fills its Energy, and when it's full the spells inside all cast");
	case PersistentSkill::BarrierInvocation:
		return _("Meta gem: holds spells in its sockets. Energy shield lost to hits stores Energy, up to 500; use it from the skill tray to cast the spells inside");
	case PersistentSkill::ElementalInvocation:
		return _("Meta gem: holds spells in its sockets. Freezing, shocking and igniting enemies stores Energy, up to 500; use it from the skill tray to cast the spells inside");
	case PersistentSkill::WindDancer:
		return _("Every 1.5 seconds gain a stage of 10% more evasion, up to 3. A hit on you spends them in a burst of wind that knocks enemies back, bigger with more stages");
	case PersistentSkill::GhostDance:
		return _("Every 12 seconds gain a Ghost Shroud, up to 3. A hit on you takes one, and for 4 seconds energy shield regenerates by 2% of your evasion per second");
	case PersistentSkill::Barkskin:
		return _("Energy shield lost to hits turns into armour for 6 seconds, up to the armour of your gear");
	case PersistentSkill::ArcticArmour:
		return _("Gains icy stages over time. A melee hit on you spends one to blast the attacker with cold that chills and freezes twice as hard");
	case PersistentSkill::HeraldOfPlague:
		return _("Poisoned enemies you kill spread their poison to those nearby, which may be hindered");
	case PersistentSkill::CombatFrenzy:
		return _("Freezing or electrocuting an enemy gives a Frenzy charge, at most once every 8 seconds");
	case PersistentSkill::EternalRage:
		return _("You regenerate Rage, and it doesn't fade while this is up");
	case PersistentSkill::ElementalConflux:
		return _("Deal 40% more damage of one element: fire, lightning or cold. The element changes every 8 seconds");
	case PersistentSkill::AlchemistsBoon:
		return _("Your flasks gain charges over time, and life flasks also heal your minions nearby");
	case PersistentSkill::OverwhelmingPresence:
		return _("Aura: enemies near you are easier to stun, freeze, ignite and shock");
	case PersistentSkill::TrailOfCaltrops:
		return _("Each dodge roll leaves caltrops behind you. Enemies that step on them take your weapon's damage and are hindered");
	case PersistentSkill::ManaRemnants:
		return _("Enemies with an elemental ailment may leave a mana remnant when they die or take a critical hit. Walk over it for mana");
	case PersistentSkill::Attrition:
		return _("Rare and unique enemies near you take more damage the longer they stay near you, and after 40 seconds you can cull them");
	case PersistentSkill::Blink:
		return _("Your dodge roll becomes a short teleport, every 4.5 seconds. It isn't a roll, so nothing set off by rolling goes");
	case PersistentSkill::IronWard:
		return _("Stores the physical damage your armour stops. Use it from the skill tray to release it all as a nova that stuns hard, wider the more it holds");
	case PersistentSkill::PlagueBearer:
		return _("Stores 4% of the poison damage you deal. Use it from the skill tray to release it as a nova that poisons everything it hits");
	case PersistentSkill::Convalescence:
		return _("Use it from the skill tray for 20 mana: your energy shield starts recharging at once, and hits can't stop it for 3 seconds. Every 30 seconds");
	case PersistentSkill::GrimFeast:
		return _("Your skeletons that die near you leave life remnants. Walk over them to store the life, and use it from the skill tray to raise fallen skeletons at once");
	case PersistentSkill::WarBanner:
		return _("Attack hits build Glory. At 200, use it from the skill tray to plant a banner: in its area you and your minions deal 25% more attack damage, with more accuracy and attack speed");
	case PersistentSkill::DefianceBanner:
		return _("Attack hits build Glory. At 200, use it from the skill tray to plant a banner: in its area you have 30% more armour and evasion and move 30% faster");
	case PersistentSkill::DreadBanner:
		return _("Attack hits build Glory. At 200, use it from the skill tray to plant a banner: in its area your maximum resistances are 4% higher and your flasks gain charges");
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
	// POE2's Archmage reserves 100 (Curtis 2026-10-19).
	case PersistentSkill::VizjereiFocus:
		return 100;
	case PersistentSkill::Berserk:
		return 30;
	case PersistentSkill::Reflect:
		return 30;
	// POE2's meta gems.
	case PersistentSkill::CastOnDodge:
	case PersistentSkill::CastWhenDamageTaken:
	case PersistentSkill::CastWhileChannelling:
	case PersistentSkill::CastOnCritical:
	case PersistentSkill::CastOnElementalAilment:
		return 100;
	case PersistentSkill::ReapersInvocation:
	case PersistentSkill::CastOnMinionDeath:
	case PersistentSkill::BarrierInvocation:
		return 30;
	case PersistentSkill::MirageArcher:
	case PersistentSkill::ElementalInvocation:
		return 60;
	// The other POE2 Spirit gems, at POE2's reservations.
	case PersistentSkill::WindDancer:
	case PersistentSkill::GhostDance:
	case PersistentSkill::Barkskin:
	case PersistentSkill::ArcticArmour:
	case PersistentSkill::HeraldOfPlague:
	case PersistentSkill::CombatFrenzy:
	case PersistentSkill::AlchemistsBoon:
	case PersistentSkill::OverwhelmingPresence:
	case PersistentSkill::TrailOfCaltrops:
	case PersistentSkill::ManaRemnants:
	case PersistentSkill::Attrition:
	case PersistentSkill::IronWard:
	case PersistentSkill::PlagueBearer:
	case PersistentSkill::Convalescence:
	case PersistentSkill::GrimFeast:
	case PersistentSkill::WarBanner:
	case PersistentSkill::DefianceBanner:
	case PersistentSkill::DreadBanner:
		return 30;
	case PersistentSkill::ElementalConflux:
	case PersistentSkill::Blink:
		return 60;
	case PersistentSkill::EternalRage:
		return 100;
	// For each curse inside (GemSpiritCost counts them).
	case PersistentSkill::Blasphemy:
		return BlasphemySpiritPerCurse;
	// A totem costs mana when you place it, not Spirit.
	default:
		return 0;
	}
}

bool IsSpiritGemSwitchable(PersistentSkill skill)
{
	return skill != PersistentSkill::HoradricTotem;
}

int GemSpiritCost(const Player &player, PersistentSkill skill)
{
	int spirit = SpiritReservation(skill);
	if (skill == PersistentSkill::Blasphemy)
		spirit *= BlasphemyCurseCount(player);
	spirit += SupportAddedSpirit(player, SkillSlot::ForPersistent(skill));
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
	if (skill == PersistentSkill::Blasphemy)
		return FormatRuntime(_("Reserves {:d} Spirit per curse"), SpiritReservation(skill));
	if (!IsSpiritGemSwitchable(skill))
		return FormatRuntime(_("Costs {:d} mana"), HoradricTotemManaCost(gemLevel));
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
		// A gem switched off in the Skills panel reserves nothing and does nothing; a totem is never switched off.
		if (!player.poe2.spiritGemOn(socket) && IsSpiritGemSwitchable(skill)) {
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
	if (skill == PersistentSkill::None || !IsSpiritGemSwitchable(skill))
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
	const bool plague = monster.poe2.poisonTicks > 0 && monster.poe2.poisonDamagePerTick > 0 && IsPersistentSkillActive(player, PersistentSkill::HeraldOfPlague);
	if (!ash && !thunder && !frost && !plague)
		return;
	HeraldTrigger trigger { monster.position.tile, monster.maxHitPoints.raw(), ash, thunder, frost };
	if (plague) {
		trigger.poisonTicks = monster.poe2.poisonTicks;
		trigger.poisonPerTick = monster.poe2.poisonDamagePerTick;
	}
	PendingHeralds.push_back(trigger);
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
		if (trigger.poisonTicks > 0)
			SpreadPlague(trigger);
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
