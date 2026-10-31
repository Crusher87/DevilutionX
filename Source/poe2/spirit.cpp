#include "poe2/spirit.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <vector>

#include "engine/path.h"
#include "engine/random.hpp"
#include "engine/point.hpp"
#include "game_mode.hpp"
#include "inv.h"
#include "items.h"
#include "levels/gendung.h"
#include "levels/tile_properties.hpp"
#include "missiles.h"
#include "monster.h"
#include "msg.h"
#include "objects.h"
#include "player.h"
#include "plrmsg.h"
#include "poe2/defenses.hpp"
#include "poe2/gemcutting.hpp"
#include "poe2/item_stats.hpp"
#include "poe2/map_play.hpp"
#include "poe2/meta_gems.hpp"
#include "poe2/minion_gems.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/spirit_auras.hpp"
#include "poe2/spirit_buffs.hpp"
#include "poe2/spirit_skills.hpp"
#include "poe2/support_table.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/unique_effects.hpp"
#include "poe2/vfx.hpp"
#include "poe2/weapon_kinds.hpp"
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
	/** Herald of Blood: the dead monster was bleeding. */
	bool blood = false;
};

std::vector<HeraldTrigger> PendingHeralds;

bool MeetsGemRequirements(const Player &player, PersistentSkill skill)
{
	const SkillSlot slot = SkillSlot::ForPersistent(skill);
	if (!SkillGemItemIndex(slot))
		return false;
	return MeetsGemAttributeNeeds(player, SkillGemAttributeNeeds(slot, SocketedSkillGemLevel(player, slot)));
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
	// Huge map bosses could overflow int32: clamp before the cast.
	const int64_t raw = static_cast<int64_t>(trigger.lifeRaw) * HeraldOfAshPercent * MapAuraPercent() / 10000;
	const Fixed26_6 damage = Fixed26_6::fromRaw(static_cast<int32_t>(std::clamp<int64_t>(raw, 0, std::numeric_limits<int32_t>::max())));
	const int radius = SupportAreaRadius(*MyPlayer, SkillSlot::ForPersistent(PersistentSkill::HeraldOfAsh), HeraldOfAshRadius);
	BreakObjectsInArea(*MyPlayer, trigger.position, radius);
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

/** @brief Herald of Blood: the bleeding monster that died bursts, and those its blood hits may bleed. */
void BurstBlood(const HeraldTrigger &trigger)
{
	Player &player = *MyPlayer;
	AddVisualEffect(trigger.position, MissileGraphicID::BloodStarRedExplosion);
	vfx::Impact(trigger.position, vfx::Element::Blood, 1.5F);
	const Fixed26_6 damage = Fixed26_6::fromRaw(static_cast<int32_t>(static_cast<int64_t>(trigger.lifeRaw) * HeraldOfBloodPercent * MapAuraPercent() / 10000));
	const int radius = SupportAreaRadius(player, SkillSlot::ForPersistent(PersistentSkill::HeraldOfBlood), HeraldOfBloodRadius);
	BreakObjectsInArea(player, trigger.position, radius);
	const int bleedChance = HeraldOfBloodBleedChance(std::max(player.poe2.skillLevel(PersistentSkill::HeraldOfBlood), 1));
	// A bleed deals 70% of the hit over 4 seconds, like Serrated Edge's.
	constexpr int BleedTicks = 4 * 20;
	const int32_t perTick = std::max<int32_t>(static_cast<int32_t>(static_cast<int64_t>(damage.raw()) * 70 / 100 / BleedTicks), 1);
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (!CanHeraldHit(monster) || trigger.position.ExactDistance(monster.position.tile) > radius)
			continue;
		HeraldHit(monster, PersistentSkill::HeraldOfBlood, DamageType::Physical, damage);
		if (!CanHeraldHit(monster) || GenerateRnd(100) >= bleedChance)
			continue;
		Poe2MonsterState &state = monster.poe2;
		if (state.bleedTicks == 0 || perTick >= state.bleedDamagePerTick) {
			state.bleedDamagePerTick = perTick;
			state.bleedSource = static_cast<int8_t>(MyPlayerId);
			state.bleedSkill = WeaponSkill::None;
		}
		state.bleedTicks = std::max<uint8_t>(state.bleedTicks, BleedTicks);
		AddVisualEffect(monster.position.tile, MissileGraphicID::BloodHit);
	}
}

void ShatterFrost(const HeraldTrigger &trigger)
{
	AddVisualEffect(trigger.position, MissileGraphicID::BlueFlareExplosion);
	const Fixed26_6 damage = Fixed26_6::fromRaw(static_cast<int32_t>(static_cast<int64_t>(trigger.lifeRaw) * HeraldOfFrostPercent * MapAuraPercent() / 10000));
	const int radius = SupportAreaRadius(*MyPlayer, SkillSlot::ForPersistent(PersistentSkill::HeraldOfFrost), HeraldOfFrostRadius);
	BreakObjectsInArea(*MyPlayer, trigger.position, radius);
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (CanHeraldHit(monster) && trigger.position.ExactDistance(monster.position.tile) <= radius)
			HeraldHit(monster, PersistentSkill::HeraldOfFrost, DamageType::Cold, damage);
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
		return _("Holy Fire");
	case PersistentSkill::HeraldOfThunder:
		return _("Holy Shock");
	case PersistentSkill::PurityOfElements:
		return _("Salvation");
	case PersistentSkill::SummonGolem:
		return _("Clay Golem");
	case PersistentSkill::SkeletalWarriors:
		return _("Skeleton Warriors");
	case PersistentSkill::HeraldOfFrost:
		return _("Holy Freeze");
	case PersistentSkill::SkeletalMages:
		return _("Skeletal Mages");
	case PersistentSkill::BloodGolem:
		return _("Blood Golem");
	case PersistentSkill::FireGolem:
		return _("Fire Golem");
	case PersistentSkill::Guardian:
		return _("Guardian Spirit");
	case PersistentSkill::ShadowWarrior:
		return _("Shadow Warrior");
	case PersistentSkill::Revive:
		return _("Revive");
	case PersistentSkill::ManaShield:
		return _("Mana Ward");
	case PersistentSkill::VizjereiFocus:
		return _("Vizjerei Focus");
	case PersistentSkill::Berserk:
		return _("Blood Frenzy");
	case PersistentSkill::Reflect:
		return _("Iron Reflection");
	case PersistentSkill::CastOnDodge:
		return _("Sigil of Evasion");
	case PersistentSkill::CastWhenDamageTaken:
		return _("Sigil of Retribution");
	case PersistentSkill::CastWhileChannelling:
		return _("Sigil of Focus");
	case PersistentSkill::ReapersInvocation:
		return _("Reaper's Call");
	case PersistentSkill::MirageArcher:
		return _("Phantom Archer");
	case PersistentSkill::Blasphemy:
		return _("Profane Aura");
	case PersistentSkill::HoradricTotem:
		return _("Horadric Totem");
	case PersistentSkill::CastOnCritical:
		return _("Sigil of Precision");
	case PersistentSkill::CastOnElementalAilment:
		return _("Sigil of the Elements");
	case PersistentSkill::CastOnMinionDeath:
		return _("Sigil of the Dead");
	case PersistentSkill::BarrierInvocation:
		return _("Arcane Barrier");
	case PersistentSkill::ElementalInvocation:
		return _("Vizjerei Conduit");
	case PersistentSkill::WindDancer:
		return _("Fade");
	case PersistentSkill::GhostDance:
		return _("Shadow Veil");
	case PersistentSkill::Barkskin:
		return _("Oak Sage");
	case PersistentSkill::ArcticArmour:
		return _("Chilling Armor");
	case PersistentSkill::HeraldOfPlague:
		return _("Plague Aura");
	case PersistentSkill::CombatFrenzy:
		return _("Battle Fervor");
	case PersistentSkill::EternalRage:
		return _("Unending Fury");
	case PersistentSkill::ElementalConflux:
		return _("Shifting Elements");
	case PersistentSkill::AlchemistsBoon:
		return _("Horadric Elixir");
	case PersistentSkill::OverwhelmingPresence:
		return _("Conviction");
	case PersistentSkill::TrailOfCaltrops:
		return _("Caltrops");
	case PersistentSkill::ManaRemnants:
		return _("Mana Wells");
	case PersistentSkill::Attrition:
		return _("Unrelenting");
	case PersistentSkill::Blink:
		return _("Dimension Step");
	case PersistentSkill::IronWard:
		return _("Iron Skin");
	case PersistentSkill::PlagueBearer:
		return _("Corpse Plague");
	case PersistentSkill::Convalescence:
		return _("Prayer");
	case PersistentSkill::GrimFeast:
		return _("Feast of Souls");
	case PersistentSkill::WarBanner:
		return _("War Standard");
	case PersistentSkill::DefianceBanner:
		return _("Bulwark Standard");
	case PersistentSkill::DreadBanner:
		return _("Terror Standard");
	case PersistentSkill::Briarpatch:
		return _("Thorns");
	case PersistentSkill::MagmaBarrier:
		return _("Hellfire Ward");
	case PersistentSkill::ScavengedPlating:
		return _("Bone Armor");
	case PersistentSkill::TimeOfNeed:
		return _("Sanctuary");
	case PersistentSkill::ChargeRegulation:
		return _("Charge Mastery");
	case PersistentSkill::WitheringPresence:
		return _("Aura of Decay");
	case PersistentSkill::Trinity:
		return _("Trinity");
	case PersistentSkill::SiphonElements:
		return _("Elemental Drain");
	case PersistentSkill::HeraldOfBlood:
		return _("Blood Rite");
	case PersistentSkill::SkeletalSnipers:
		return _("Skeleton Archers");
	case PersistentSkill::SkeletalFrostMages:
		return _("Frost Liches");
	case PersistentSkill::SkeletalStormMages:
		return _("Storm Liches");
	case PersistentSkill::SkeletalReavers:
		return _("Skeleton Captains");
	case PersistentSkill::SkeletalBrutes:
		return _("Burning Dead");
	case PersistentSkill::SkeletalClerics:
		return _("Bone Priests");
	case PersistentSkill::WolfPack:
		return _("Dire Wolves");
	case PersistentSkill::RagingSpirits:
		return _("Burning Souls");
	case PersistentSkill::RavenousSwarm:
		return _("Fetid Swarm");
	case PersistentSkill::Sacrifice:
		return _("Blood Offering");
	case PersistentSkill::LingeringIllusion:
		return _("Phantom Double");
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
		return _("Aura: +25% to fire, lightning and cold resistance");
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
	case PersistentSkill::Briarpatch:
		return _("Your attacks store 10% of their physical damage as thorns. Casting a spell spreads them as thorny ground around you for 6 seconds, which hurts and hinders enemies in it. Every 8 seconds");
	case PersistentSkill::MagmaBarrier:
		return _("With a shield: 25% increased block chance. Every 8 seconds your next block sprays fire from the shield at the enemies in front of you");
	case PersistentSkill::ScavengedPlating:
		return _("Breaking an enemy's armour gives you plating, more from stronger enemies, up to 10. Each gives 4% more armour and thorns that hurt enemies hitting you in melee");
	case PersistentSkill::TimeOfNeed:
		return _("Every 10 seconds, the next time you're hurt you recover 10% of your maximum life at once");
	case PersistentSkill::ChargeRegulation:
		return _("Spends your Frenzy charges on speed: each one gives 8 seconds of 20% more attack and cast speed, taken as the last runs low");
	case PersistentSkill::WitheringPresence:
		return _("Aura: enemies near you wither, taking 5% more chaos damage a stack, up to 10 stacks");
	case PersistentSkill::Trinity:
		return _("Fire, lightning and cold hits build Resonance of their element and take some from the other two. Every 30 Resonance in all gives more elemental damage, so mixing elements pays");
	case PersistentSkill::SiphonElements:
		return _("Freezing, shocking and igniting enemies may leave an infusion of that element, for Meteor, Thunderstorm, Frozen Orb and Apocalypse to eat");
	case PersistentSkill::HeraldOfBlood:
		return _("Bleeding enemies you kill burst, hitting those nearby for 15% of their life as physical damage and making them bleed");
	case PersistentSkill::SkeletalSnipers:
		return _("Skeletal archers shoot heavy arrows from far away, taking aim between shots, and rise again when they fall");
	case PersistentSkill::SkeletalFrostMages:
		return _("Skeletal mages hurl frost bolts that chill and freeze, and rise again when they fall");
	case PersistentSkill::SkeletalStormMages:
		return _("Skeletal mages hurl two crackling charged bolts at a time that shock, and rise again when they fall");
	case PersistentSkill::SkeletalReavers:
		return _("Fierce skeletons that build Rage with every blow, hitting harder the longer they fight, and rise again when they fall");
	case PersistentSkill::SkeletalBrutes:
		return _("Tough, slow skeletons whose heavy blows stun, and who rise again when they fall");
	case PersistentSkill::SkeletalClerics:
		return _("Skeletal priests that don't fight: they heal your most hurt minion nearby and raise your fallen minions twice as fast");
	case PersistentSkill::WolfPack:
		return _("A pack of shadow beasts hunts at your side, more of them as the gem levels, and they rise again when they fall");
	case PersistentSkill::RagingSpirits:
		return _("Hits of fire spells you cast yourself call flaming spirits beside the enemy, two at most each cast. They bite with fire for a few seconds, then burn out");
	case PersistentSkill::RavenousSwarm:
		return _("While enemies are near, a swarm of insects leaves you every few seconds to bite and poison one of them. Swarms can't be hit");
	case PersistentSkill::Sacrifice:
		return _("Corpse Explosion and Soul Harvest can use your skeletons as corpses; they rise again later. All your minions rise again more slowly");
	case PersistentSkill::LingeringIllusion:
		return _("Each dodge roll leaves an illusion of you that draws enemies near it. If they destroy it, it leaves a remnant that gives a Frenzy charge");
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
	case PersistentSkill::SkeletalSnipers:
	case PersistentSkill::SkeletalFrostMages:
	case PersistentSkill::SkeletalStormMages:
	case PersistentSkill::SkeletalReavers:
	case PersistentSkill::SkeletalBrutes:
	case PersistentSkill::SkeletalClerics:
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
	case PersistentSkill::Briarpatch:
	case PersistentSkill::MagmaBarrier:
	case PersistentSkill::ScavengedPlating:
	case PersistentSkill::TimeOfNeed:
	case PersistentSkill::ChargeRegulation:
	case PersistentSkill::WitheringPresence:
	case PersistentSkill::SiphonElements:
	case PersistentSkill::HeraldOfBlood:
	case PersistentSkill::RagingSpirits:
	case PersistentSkill::RavenousSwarm:
	case PersistentSkill::LingeringIllusion:
		return 30;
	case PersistentSkill::Trinity:
		return 100;
	case PersistentSkill::ElementalConflux:
	case PersistentSkill::Blink:
	case PersistentSkill::WolfPack:
	case PersistentSkill::Sacrifice:
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
	return IsAnySceptre(item);
}

int SceptreSpiritOf(const Item &item)
{
	// Its increased Spirit counts once identified, as the gear's other modifiers do; quality doesn't change it.
	const bool modsCount = item._iMagical == ITEM_QUALITY_NORMAL || item._iIdentified;
	const int local = modsCount ? item._iPoe2Stats[static_cast<size_t>(Poe2ItemStat::LocalSpiritPercent)] : 0;
	return std::max(SceptreSpirit * (100 + local) / 100, 0);
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

void GiveUncutSpiritGem(Player &player)
{
	if (!gbIsPoe2 || &player != MyPlayer)
		return;
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		if (AllItemsList[i].iMiscId != IMISC_UNCUT_SPIRIT || AllItemsList[i].dropRate == 0)
			continue;
		Item gem {};
		SetupAllItems(player, gem, static_cast<_item_indexes>(i), AdvanceRndSeed(), 1, 1, /*onlygood=*/false, /*pregen=*/false);
		// A reward, so the highest level the hero can use now.
		SetUncutGemLevel(gem, MaxGemLevelForCharacter(player.getCharacterLevel()));
		gem._iIdentified = true;
		SetupItem(gem);
		if (!AutoPlaceItemInInventory(player, gem, true) && ActiveItemCount < MAXITEMS) {
			// A full bag: it lands at the hero's feet.
			const int ii = AllocateItem();
			Items[ii] = gem;
			GetSuperItemSpace(player.position.tile, static_cast<int8_t>(ii));
			NetSendCmdPItem(false, CMD_DROPITEM, Items[ii].position, Items[ii]);
		}
		EventPlrMsg(_("An Uncut Spirit Gem forms from the skull's energies."), UiFlags::ColorWhitegold);
		TelemetryLog("spirit", "the skull gave an Uncut Spirit Gem");
		return;
	}
}

namespace {

/** @brief The skull a boss guards, like POE2's Spirit quest bosses; -1 for none. */
int SkullForMonster(const Monster &monster, const Player &player)
{
	if (monster.uniqueType == UniqueMonsterType::Lazarus)
		return 2;
	if (!monster.isUnique())
		return -1;
	// The first comes from the first named monster beaten in the Catacombs or deeper, like POE2's King in the Mists
	// partway through Act 1 (Curtis 2026-10-20: 30 Spirit from the Skeleton King came too early). The second comes
	// from the first one in the caves or deeper, once the first is broken.
	if (currlevel >= 5 && (player.poe2.spiritSkulls & 1) == 0)
		return 0;
	if (currlevel >= 9)
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

/** @brief Whether a copy of the skull already lies on this level or in the hero's bags, so a second named monster doesn't drop another. */
bool SpiritSkullAround(const Player &player, int skull)
{
	for (uint8_t i = 0; i < ActiveItemCount; i++) {
		if (SpiritSkullIndex(Items[ActiveItems[i]]) == skull)
			return true;
	}
	for (int i = 0; i < player._pNumInv; i++) {
		if (SpiritSkullIndex(player.InvList[i]) == skull)
			return true;
	}
	for (const Item &item : player.SpdList) {
		if (SpiritSkullIndex(item) == skull)
			return true;
	}
	return SpiritSkullIndex(player.HoldItem) == skull;
}

} // namespace

void DropSpiritSkull(const Monster &monster, bool sendmsg)
{
	if (!gbIsPoe2 || MyPlayer == nullptr || monster.isPlayerMinion())
		return;
	const int skull = SkullForMonster(monster, *MyPlayer);
	if (skull < 0 || (MyPlayer->poe2.spiritSkulls & (1 << skull)) != 0 || SpiritSkullAround(*MyPlayer, skull))
		return;
	if (const std::optional<size_t> index = SpiritSkullItemIndex(skull); index)
		SpawnBaseItem(monster.position.tile, static_cast<_item_indexes>(*index), sendmsg);
}

int MaxSpirit(const Player &player)
{
	int spirit = BaseSpirit + SkullSpirit(player) + GetPassiveTotals(player)[static_cast<size_t>(PassiveStat::Spirit)] + player.poe2.itemStats[static_cast<size_t>(Poe2ItemStat::Spirit)];
	for (const inv_body_loc hand : { INVLOC_HAND_LEFT, INVLOC_HAND_RIGHT }) {
		const Item &item = player.InvBody[hand];
		if (IsAnySceptre(item) && item._iStatFlag)
			spirit += SceptreSpiritOf(item);
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

int RevivingMinionIndex(PersistentSkill skill)
{
	for (size_t i = 0; i < RevivingMinionSkills.size(); i++) {
		if (RevivingMinionSkills[i] == skill)
			return static_cast<int>(i);
	}
	return -1;
}

int MinionReviveTicks(const Player &player)
{
	if (!IsPersistentSkillActive(player, PersistentSkill::Sacrifice))
		return SkeletonReviveTicks;
	return SkeletonReviveTicks * SacrificeRevivePercent(std::max(player.poe2.skillLevel(PersistentSkill::Sacrifice), 1)) / 100;
}

int FreeMinions(PersistentSkill skill)
{
	const MinionGemData *gem = FindMinionGem(skill);
	return gem != nullptr ? gem->freeMinions : 0;
}

int StartingMinionCount(PersistentSkill skill)
{
	const MinionGemData *gem = FindMinionGem(skill);
	return gem != nullptr ? gem->startingCount : 1;
}

int BaseSpiritPerMinion(PersistentSkill skill, int gemLevel)
{
	const MinionGemData *gem = FindMinionGem(skill);
	if (gem == nullptr)
		return 0;
	static_assert(std::tuple_size_v<decltype(MinionGemData::spirit)> == MaxGemLevel);
	return gem->spirit[static_cast<size_t>(std::clamp(gemLevel, 1, MaxGemLevel) - 1)];
}

int SpiritPerMinion(const Player &player, PersistentSkill skill)
{
	const int base = BaseSpiritPerMinion(skill, player.poe2.effectiveLevel(skill));
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

/** @brief The Spirit skill a held weapon's grant is, or None: a sceptre's; a staff's or wand's is an attack. */
PersistentSkill GrantedPersistentSkill(const GrantedSkill &entry)
{
	return entry.baseLevel > 0 ? entry.skill.persistent() : PersistentSkill::None;
}

struct GemReservation {
	PersistentSkill skill;
	/** The socket, or GrantedSpiritOffBit and the hand for a granted Spirit skill: its spiritOrder index. */
	size_t index;
	/** Switched on in the Skills panel (a totem always is). */
	bool on;
	/** Switched on, but off until there's Spirit for it: the newest switch off first when Spirit runs short. */
	bool waiting;
	/** Minions the gem keeps, or 1 for a gem without a count; 0 when it's inactive. */
	int count;
	int spirit;
	/** Spirit the gems before this one left; for a gem switched off, the Spirit not reserved. */
	int spiritLeft;
};

struct SpiritEntry {
	PersistentSkill skill;
	size_t index;
	bool on;
	uint8_t order;
};

/**
 * @brief Reserves Spirit for the usable Spirit gems that are switched on, oldest switched on first. When they need more
 * than there is, the newest switch off until the rest fit (Curtis 2026-10-30): a minion gem keeps as many minions as fit
 * (its free ones always), and once one falls short every newer gem that reserves Spirit waits. Waiting gems come back on
 * their own when Spirit is free again; a gem that reserves nothing never waits.
 */
template <typename F>
int ReserveSpirit(const Player &player, F &&onGem)
{
	std::array<SpiritEntry, NumSkillSlots + 2> entries {};
	size_t count = 0;
	for (size_t socket = 0; socket < player.poe2.skillSlots.size(); socket++) {
		const PersistentSkill skill = player.poe2.skillSlots[socket].persistent();
		if (skill == PersistentSkill::None || !MeetsGemRequirements(player, skill))
			continue;
		// A gem switched off in the Skills panel reserves nothing and does nothing; a totem is never switched off.
		entries[count++] = { skill, socket, player.poe2.spiritGemOn(socket) || !IsSpiritGemSwitchable(skill), player.poe2.spiritOrder[socket] };
	}
	// Then the Spirit skills held sceptres grant (caster-weapons-plan-v2.md 6.9), each once: a gem of it in the Skills
	// panel wins, and two sceptres that grant the same one reserve for it once, on while either hand's is on.
	for (size_t i = 0; i < player.poe2.granted.size(); i++) {
		const PersistentSkill skill = GrantedPersistentSkill(player.poe2.granted[i]);
		if (skill == PersistentSkill::None || player.poe2.skillLevel(skill) > 0)
			continue;
		if (i > 0 && GrantedPersistentSkill(player.poe2.granted[0]) == skill)
			continue;
		SpiritEntry entry { skill, GrantedSpiritOffBit + i, false, 0 };
		for (const GrantedSkill &granted : player.poe2.granted) {
			if (GrantedPersistentSkill(granted) != skill || player.poe2.grantedOff(granted.hand))
				continue;
			entry.on = true;
			entry.order = std::max(entry.order, player.poe2.spiritOrder[GrantedSpiritOffBit + granted.hand]);
		}
		entries[count++] = entry;
	}
	// Oldest first; ones switched on together (a loaded hero's) go in the Skills panel's order.
	std::stable_sort(entries.begin(), entries.begin() + static_cast<std::ptrdiff_t>(count), [](const SpiritEntry &a, const SpiritEntry &b) { return a.order < b.order; });

	const int max = MaxSpirit(player);
	int reserved = 0;
	bool shortOfSpirit = false;
	for (size_t i = 0; i < count; i++) {
		const SpiritEntry &entry = entries[i];
		if (!entry.on)
			continue;
		const int spiritLeft = max - reserved;
		if (IsMinionCountSkill(entry.skill)) {
			const int chosen = ChosenMinionCount(player, entry.skill);
			const int free = std::min(chosen, FreeMinions(entry.skill));
			const int perMinion = SpiritPerMinion(player, entry.skill);
			const int paid = shortOfSpirit ? 0 : std::min(chosen - free, std::max(spiritLeft, 0) / perMinion);
			if (paid < chosen - free)
				shortOfSpirit = true;
			reserved += paid * perMinion;
			onGem(GemReservation { entry.skill, entry.index, true, chosen > 0 && free + paid == 0, free + paid, paid * perMinion, spiritLeft });
			continue;
		}
		const int cost = GemSpiritCost(player, entry.skill);
		if (cost > 0 && (shortOfSpirit || cost > spiritLeft)) {
			shortOfSpirit = true;
			onGem(GemReservation { entry.skill, entry.index, true, true, 0, 0, spiritLeft });
			continue;
		}
		reserved += cost;
		onGem(GemReservation { entry.skill, entry.index, true, false, 1, cost, spiritLeft });
	}
	// A gem switched on goes in as the newest, with what the others leave.
	for (size_t i = 0; i < count; i++) {
		if (!entries[i].on)
			onGem(GemReservation { entries[i].skill, entries[i].index, false, false, 0, 0, max - reserved });
	}
	return reserved;
}

/** @brief Its granted skills reserve after the gems: one a gem of its own already reserves for isn't here twice. */
std::optional<GemReservation> FindGemReservation(const Player &player, PersistentSkill skill)
{
	std::optional<GemReservation> found;
	ReserveSpirit(player, [&](const GemReservation &gem) {
		if (gem.skill == skill && !found)
			found = gem;
	});
	return found;
}

/** @brief Spirit the skill needs free to be switched on: a minion gem's first minion, unless it has free ones or none are chosen. */
int SpiritToSwitchOn(const Player &player, PersistentSkill skill)
{
	if (!IsMinionCountSkill(skill))
		return GemSpiritCost(player, skill);
	if (ChosenMinionCount(player, skill) == 0 || FreeMinions(skill) > 0)
		return 0;
	return SpiritPerMinion(player, skill);
}

/**
 * @brief Before a Spirit skill is switched on: whether the Spirit not reserved covers it (else the player is told what
 * it needs), and if so its place as the newest of the ones running, ahead of any waiting for Spirit.
 */
bool MakeRoomToSwitchOn(Player &player, PersistentSkill skill, size_t index)
{
	const int needed = SpiritToSwitchOn(player, skill);
	const int available = AvailableSpirit(player);
	if (needed > available) {
		if (&player == MyPlayer)
			EventPlrMsg(FormatRuntime(_("Not enough Spirit: {:s} needs {:d}, and {:d} is free."), PersistentSkillName(skill), needed, std::max(available, 0)), UiFlags::ColorRed);
		TelemetryLog("spirit", FormatRuntime("{:s} not switched on: needs {:d} Spirit, {:d} free", PersistentSkillName(skill), needed, available));
		return false;
	}
	std::array<size_t, NumSkillSlots + 2> running {};
	std::array<size_t, NumSkillSlots + 2> waiting {};
	size_t runningCount = 0;
	size_t waitingCount = 0;
	ReserveSpirit(player, [&](const GemReservation &gem) {
		if (!gem.on || gem.index == index)
			return;
		if (gem.waiting)
			waiting[waitingCount++] = gem.index;
		else
			running[runningCount++] = gem.index;
	});
	std::array<uint8_t, NumSkillSlots + 2> &order = player.poe2.spiritOrder;
	order.fill(0);
	uint8_t next = 1;
	for (size_t i = 0; i < runningCount; i++)
		order[running[i]] = next++;
	order[index] = next++;
	for (size_t i = 0; i < waitingCount; i++)
		order[waiting[i]] = next++;
	return true;
}

/** @brief The Spirit skill at a spiritOrder index: a socket's gem, or a hand's granted skill. */
PersistentSkill SpiritSkillAt(const Player &player, size_t index)
{
	if (index < NumSkillSlots)
		return player.poe2.skillSlots[index].persistent();
	return GrantedSpiritSkill(player, static_cast<int>(index - GrantedSpiritOffBit));
}

/** @brief Tells the player when Spirit gems switch off for lack of Spirit, and when they come back on. */
void NoteWaitingSpiritGems(Player &player)
{
	uint16_t waiting = 0;
	uint16_t running = 0;
	ReserveSpirit(player, [&](const GemReservation &gem) {
		if (gem.waiting)
			waiting |= static_cast<uint16_t>(1U << gem.index);
		else if (gem.on)
			running |= static_cast<uint16_t>(1U << gem.index);
	});
	const uint16_t changed = waiting ^ player.poe2.spiritWaiting;
	if (changed == 0)
		return;
	player.poe2.spiritWaiting = waiting;
	for (size_t index = 0; index < NumSkillSlots + 2; index++) {
		const auto bit = static_cast<uint16_t>(1U << index);
		if ((changed & bit) == 0)
			continue;
		const std::string_view name = PersistentSkillName(SpiritSkillAt(player, index));
		if ((waiting & bit) != 0) {
			EventPlrMsg(FormatRuntime(_("{:s} is off: not enough Spirit. It comes back on when there is."), name), UiFlags::ColorRed);
			TelemetryLog("spirit", FormatRuntime("{:s} switched off: not enough Spirit ({:d}/{:d})", name, ReservedSpirit(player), MaxSpirit(player)));
		} else if ((running & bit) != 0) {
			EventPlrMsg(FormatRuntime(_("{:s} is back on."), name));
			TelemetryLog("spirit", FormatRuntime("{:s} back on: Spirit for it again ({:d}/{:d})", name, ReservedSpirit(player), MaxSpirit(player)));
		}
	}
	// Spirit gems can change stats, like Purity of Elements' resistances.
	CalcPlrInv(player, false);
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
	// A gem goes on only with the Spirit for it (Curtis 2026-10-30).
	if (!player.poe2.spiritGemOn(socket) && !MakeRoomToSwitchOn(player, skill, socket))
		return true;
	player.poe2.spiritGemsOn ^= static_cast<uint16_t>(1U << socket);
	TelemetryLog("spirit", StrCat(PersistentSkillName(skill), player.poe2.spiritGemOn(socket) ? " switched on" : " switched off"));
	// Spirit gems can change stats, like Purity of Elements' resistances.
	CalcPlrInv(player, false);
	return true;
}

void CarrySpiritGemSwitch(Player &player, size_t socket, SkillSlot previous, SkillSlot skill)
{
	Poe2PlayerData &data = player.poe2;
	if (socket >= NumSkillSlots || previous == skill)
		return;
	const auto bit = static_cast<uint16_t>(1U << socket);
	const bool wasOn = previous.kind == SkillKind::Persistent && (data.spiritGemsOn & bit) != 0;
	const uint8_t wasOrder = data.spiritOrder[socket];
	data.spiritGemsOn &= static_cast<uint16_t>(~bit);
	if (skill.persistent() != PersistentSkill::None && skill.persistent() == data.liftedSpiritGem) {
		data.spiritGemsOn |= bit;
		data.spiritOrder[socket] = data.liftedSpiritOrder;
		data.liftedSpiritGem = PersistentSkill::None;
	}
	if (wasOn) {
		data.liftedSpiritGem = previous.persistent();
		data.liftedSpiritOrder = wasOrder;
	}
}

PersistentSkill GrantedSpiritSkill(const Player &player, int hand)
{
	if (!gbIsPoe2)
		return PersistentSkill::None;
	for (const GrantedSkill &entry : player.poe2.granted) {
		if (entry.hand == hand && GrantedPersistentSkill(entry) != PersistentSkill::None)
			return GrantedPersistentSkill(entry);
	}
	return PersistentSkill::None;
}

bool ToggleGrantedSpiritSkill(Player &player, int hand)
{
	const PersistentSkill skill = GrantedSpiritSkill(player, hand);
	if (skill == PersistentSkill::None || !IsSpiritGemSwitchable(skill))
		return false;
	// Like a gem, it goes on only with the Spirit for it, unless a gem of it or the other sceptre's grant already reserves.
	const bool reservedElsewhere = player.poe2.skillLevel(skill) > 0 || (GrantedSpiritSkill(player, 1 - hand) == skill && !player.poe2.grantedOff(1 - hand));
	if (player.poe2.grantedOff(hand) && !reservedElsewhere && !MakeRoomToSwitchOn(player, skill, GrantedSpiritOffBit + static_cast<size_t>(hand)))
		return true;
	player.poe2.toggleGranted(hand);
	TelemetryLog("spirit", StrCat(PersistentSkillName(skill), player.poe2.grantedOff(hand) ? " (sceptre) switched off" : " (sceptre) switched on"));
	// Like a gem's switch: Salvation's resistances and the rest change with it.
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

bool IsSpiritSkillWaiting(const Player &player, PersistentSkill skill)
{
	if (!gbIsPoe2 || skill == PersistentSkill::None)
		return false;
	const std::optional<GemReservation> gem = FindGemReservation(player, skill);
	return gem && gem->waiting;
}

int ActiveMinionCount(const Player &player, PersistentSkill skill)
{
	if (!gbIsPoe2 || !IsMinionCountSkill(skill))
		return 0;
	const std::optional<GemReservation> gem = FindGemReservation(player, skill);
	return gem ? gem->count : 0;
}

int KeptMinionCount(const Player &player, PersistentSkill skill)
{
	if (skill == PersistentSkill::WolfPack)
		return IsPersistentSkillActive(player, skill) ? WolfPackSize(std::max(player.poe2.skillLevel(skill), 1)) : 0;
	return ActiveMinionCount(player, skill);
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
	const bool blood = monster.poe2.bleedTicks > 0 && IsPersistentSkillActive(player, PersistentSkill::HeraldOfBlood);
	if (!ash && !thunder && !frost && !plague && !blood)
		return;
	HeraldTrigger trigger { monster.position.tile, monster.maxHitPoints.raw(), ash, thunder, frost };
	trigger.blood = blood;
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
		if (trigger.blood)
			BurstBlood(trigger);
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
		// A golem gem switched off, taken out or short of Spirit takes its golem with it, as the skeleton gems do theirs;
		// the Golem spell's golem stays (Curtis 2026-10-30).
		if (golem != nullptr && !FindSkillSlot(player, SkillSlot::ForSpell(SpellID::Golem)))
			KillGolem(*golem);
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
		// Its minion damage applies to each hit (ScaleMonsterHit), with no 255 cap.
		// The golem skill's supports change its life.
		if (const int percent = GolemLifePercent(player); percent != 100) {
			golem->maxHitPoints = Fixed26_6::fromRaw(std::max<int32_t>(golem->maxHitPoints.raw() * percent / 100, 64));
			golem->hitPoints = golem->maxHitPoints;
		}
	}
	AddVisualEffect(*position, MissileGraphicID::Resurrect);
	player.poe2.golemCooldown = GolemReviveTicks;
}

/** @brief Whether the level loaded the monster type: when its type table is full, minion types are left out. */
bool MonsterTypeOnLevel(_monster_id type)
{
	return std::any_of(LevelMonsterTypes, LevelMonsterTypes + LevelMonsterTypeCount, [type](const CMonster &levelType) { return levelType.type == type; });
}

/** @brief Kills the skeleton of the type farthest from its owner, when the count is lowered. */
void KillFarthestSkeletonMinion(const Player &player, _monster_id type)
{
	Monster *farthest = nullptr;
	int farthestDistance = -1;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (monster.type().type != type || !monster.isPlayerMinion() || monster.goalVar3 != player.getId() || monster.hasNoLife() || monster.poe2.revived)
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
	return FindMinionGem(skill)->type;
}

int TargetMinions(const Player &player, PersistentSkill skill, int active)
{
	return std::min(active + SupportExtraMinions(player, skill), static_cast<int>(MinionRevives {}.ticks.size()));
}

/**
 * @brief Keeps the gem's skeletons (or wolves) at its count. Like POE2, each one that falls rises again after the revive
 * time; ones that haven't been raised yet (on a new level, or when the count goes up) come up at once, a few ticks apart.
 */
void KeepSkeletonsAlive(Player &player, PersistentSkill skill, _monster_id type, MinionRevives &revives, bool clericStands)
{
	const int active = KeptMinionCount(player, skill);
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
	const int reviveTicks = MinionReviveTicks(player);
	for (int fallen = revives.lastAlive - alive; fallen > 0 && revives.pending < revives.ticks.size(); fallen--)
		revives.ticks[revives.pending++] = static_cast<uint16_t>(reviveTicks);
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
	// A Skeletal Cleric raises the fallen twice as fast.
	const int step = clericStands ? 2 : 1;
	for (size_t i = 0; i < revives.pending; i++) {
		revives.ticks[i] = static_cast<uint16_t>(std::max(revives.ticks[i] - step, 0));
		if (revives.ticks[i] == 0 && !ready)
			ready = i;
	}
	if (revives.raiseTicks > 0)
		revives.raiseTicks--;
	const bool raiseNew = alive + revives.pending < target && revives.raiseTicks == 0;
	// Without the type on the level, the spawn would fail every tick.
	if ((ready || raiseNew) && MonsterTypeOnLevel(type)) {
		const std::optional<Point> position = FindMinionSpawnPosition(player);
		if (position && SpawnSkeletonMinion(player, *position, static_cast<uint8_t>(std::max(player.poe2.effectiveLevel(skill), 1)), type)) {
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
	const int index = RevivingMinionIndex(skill);
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
	const int active = KeptMinionCount(player, skill);
	if (active <= 0 || !CanRaiseMinions(player))
		return 0;
	return TargetMinions(player, skill, active);
}

int AliveMinionCount(const Player &player, PersistentSkill skill)
{
	return IsRevivingMinionSkill(skill) ? static_cast<int>(CountSkeletonMinions(player, MinionMonsterType(skill))) : 0;
}

void ProcessPersistentSkills(Player &player)
{
	if (!gbIsPoe2 || &player != MyPlayer || player.hasNoLife())
		return;
	NoteWaitingSpiritGems(player);
	KeepGolemAlive(player);
	const bool clericStands = ClericStands(player);
	for (size_t i = 0; i < RevivingMinionSkills.size(); i++) {
		const PersistentSkill skill = RevivingMinionSkills[i];
		KeepSkeletonsAlive(player, skill, MinionMonsterType(skill), player.poe2.minionRevives[i], clericStands);
	}
	ProcessMinionGems(player);
	ProcessSpiritSkills(player);
}

} // namespace devilution
