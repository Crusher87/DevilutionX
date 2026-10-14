#include "poe2/table_skills.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <optional>
#include <vector>

#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "levels/tile_properties.hpp"
#include "missiles.h"
#include "monster.h"
#include "panels/spell_icons.hpp"
#include "player.h"
#include "plrmsg.h"
#include "poe2/camera_shake.hpp"
#include "poe2/gem_levels.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/skill_resources.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/unique_effects.hpp"
#include "poe2/weapon_skills.hpp"
#include "utils/is_of.hpp"
#include "utils/language.h"

namespace devilution {

namespace {

constexpr uint8_t Icon(SpellIcon icon)
{
	return static_cast<uint8_t>(icon);
}

/** @brief Plays an effect on every tile at the given distance around a tile. */
void EffectRingAround(Point center, int radius, MissileGraphicID graphic, int delay)
{
	for (int y = -radius; y <= radius; y++) {
		for (int x = -radius; x <= radius; x++) {
			if (std::max(std::abs(x), std::abs(y)) == radius)
				AddVisualEffect(center + Displacement { x, y }, graphic, delay);
		}
	}
}

using enum TableShape;
using WS = WeaponSkill;
using TW = TableWeapon;
using DT = DamageType;
using MG = MissileGraphicID;

/** @brief Damage percents of curses and buffs follow no curve: Boneshatter's steps scaled to stay flat. */
constexpr int16_t Flat = 100;

// clang-format off
const TableSkillData Table[] = {
	// Sword
	{ WS::Whirlwind, "Whirlwind", "Spin through the enemies in your path, hitting all around where you stop", 5, 10, TW::AxeOrSword, 0, Dash, 110, 4, 1, 0, 0, 0, DT::Physical, MissileID::Null, MG::MetalHit, 0, TableZone::None, TableCurse::None, TableBuff::None, TT_Sword | TT_Axe, Icon(SpellIcon::Berserk), TableCurve::Boneshatter, 300 },
	{ WS::Sacrifice, "Sacrifice", "A brutal strike paid for with 5% of your life", 7, 6, TW::Sword, 0, Strike, 260, 0, 0, 0, 0, 0, DT::Physical, MissileID::Null, MG::BloodHit, TF_LifeCost, TableZone::None, TableCurse::None, TableBuff::None, TT_Sword, Icon(SpellIcon::BloodBoil), TableCurve::Boneshatter, 0 },
	{ WS::Zeal, "Zeal", "Four lightning-fast strikes at the enemy in front", 9, 8, TW::Sword, 0, Flurry, 60, 4, 0, 0, 0, 0, DT::Physical, MissileID::Null, MG::MetalHit, 0, TableZone::None, TableCurse::None, TableBuff::None, TT_Sword, Icon(SpellIcon::Jester), TableCurve::Boneshatter, 290 },
	{ WS::Vengeance, "Vengeance", "A strike that bursts into fire, lightning and cold", 11, 10, TW::Sword, 0, Strike, 130, 0, 0, 0, 0, 0, DT::Physical, MissileID::Null, MG::HolyBoltExplosion, TF_Elements, TableZone::None, TableCurse::None, TableBuff::None, TT_Sword | TT_Elemental, Icon(SpellIcon::Elemental), TableCurve::Boneshatter, 0 },
	{ WS::ArkainesWrath, "Arkaine's Wrath", "A sweeping blow that calls down lightning and staggers", 13, 14, TW::Sword, 0, Arc, 200, 0, 0, 0, 0, 0, DT::Lightning, MissileID::Null, MG::Lightning, TF_Stun, TableZone::None, TableCurse::None, TableBuff::None, TT_Sword | TT_Elemental, Icon(SpellIcon::LightningWall), TableCurve::Boneshatter, 0 },
	// Dagger
	{ WS::FanOfBlades, "Fan of Blades", "Throw daggers in every direction", 5, 9, TW::Dagger, 0, Projectiles, 70, 8, 0, 0, 0, 0, DT::Physical, MissileID::Arrow, MG::MetalHit, 0, TableZone::None, TableCurse::None, TableBuff::None, TT_Sword, Icon(SpellIcon::Sentinel), TableCurve::LightningArrow, 280 },
	{ WS::ViperStrike, "Viper Strike", "A poisoned stab that keeps hurting", 7, 7, TW::Dagger, 0, Strike, 130, 0, 0, 0, 0, 0, DT::Physical, MissileID::Null, MG::BloodHit, TF_Bleed, TableZone::None, TableCurse::None, TableBuff::None, TT_Sword, Icon(SpellIcon::DoomSerpents), TableCurve::Boneshatter, 0 },
	{ WS::BladeSentinel, "Blade Sentinel", "A whirling blade that guards the target spot for 5 seconds", 9, 12, TW::Dagger, 0, Zone, 50, 0, 1, 100, 10, 0, DT::Physical, MissileID::Null, MG::MetalHit, 0, TableZone::Pulse, TableCurse::None, TableBuff::None, TT_Sword, Icon(SpellIcon::Sentinel), TableCurve::Boneshatter, 260 },
	{ WS::DeathMark, "Death Mark", "Marks an enemy to take 25% more damage", 11, 9, TW::Dagger, 1, Curse, Flat, 0, 0, 160, 0, 0, DT::Physical, MissileID::Null, MG::BloodHit, 0, TableZone::None, TableCurse::DeathMark, TableBuff::None, TT_Sword | TT_Occult, Icon(SpellIcon::RedSkull), TableCurve::Boneshatter, 100 },
	{ WS::BladeFury, "Blade Fury", "A rapid stream of thrown daggers", 13, 12, TW::Dagger, 0, Stream, 70, 8, 0, 0, 2, 0, DT::Physical, MissileID::Arrow, MG::MetalHit, 0, TableZone::None, TableCurse::None, TableBuff::None, TT_Sword, Icon(SpellIcon::Jester), TableCurve::LightningArrow, 290 },
	// Axe
	{ WS::DoubleThrow, "Double Throw", "Hurl two axes, and one more for each Frenzy charge", 7, 7, TW::Axe, 0, Projectiles, 110, 2, 0, 0, 0, 0, DT::Physical, MissileID::Arrow, MG::MetalHit, TF_Frenzy, TableZone::None, TableCurse::None, TableBuff::None, TT_Axe, Icon(SpellIcon::Telekinesis), TableCurve::LightningArrow, 290 },
	{ WS::Rend, "Rend", "A tearing sweep that leaves enemies bleeding", 9, 7, TW::Axe, 0, Arc, 100, 0, 0, 0, 0, 0, DT::Physical, MissileID::Null, MG::BloodHit, TF_Bleed, TableZone::None, TableCurse::None, TableBuff::None, TT_Axe, Icon(SpellIcon::BloodBoil), TableCurve::Boneshatter, 0 },
	{ WS::WarCry, "War Cry", "Stuns enemies around you and empowers melee hits for 8 seconds", 11, 10, TW::None, 1, Buff, Flat, 0, 2, 160, 0, 0, DT::Physical, MissileID::Null, MG::StoneCurseShatter, TF_Stun, TableZone::None, TableCurse::None, TableBuff::WarCry, TT_Axe | TT_Mace | TT_Sword, Icon(SpellIcon::LongHorn), TableCurve::Boneshatter, 200 },
	{ WS::CallOfTheAncients, "Call of the Ancients", "Spends all Rage: ancestral warriors strike all around, 5% harder per Rage", 13, 12, TW::Axe, 0, CallOfTheAncients, 150, 3, 2, 0, 0, 0, DT::Physical, MissileID::Null, MG::HolyBoltExplosion, TF_Rage, TableZone::None, TableCurse::None, TableBuff::None, TT_Axe, Icon(SpellIcon::Golem), TableCurve::Boneshatter, 0 },
	// Mace
	{ WS::Fissure, "Fissure", "The ground splits open in a line, staggering what it hits", 7, 10, TW::Mace, 0, Line, 120, 6, 1, 0, 0, 0, DT::Physical, MissileID::Null, MG::StoneCurseShatter, TF_Stun, TableZone::None, TableCurse::None, TableBuff::None, TT_Mace, Icon(SpellIcon::PentaStar), TableCurve::Earthquake, 0 },
	{ WS::BlessedHammer, "Blessed Hammer", "Holy hammers spiral around you for 4 seconds", 9, 10, TW::None, 1, Zone, 70, 0, 2, 80, 10, 0, DT::Physical, MissileID::Null, MG::HolyBoltExplosion, TF_Spell | TF_FollowPlayer | TF_AtPlayer, TableZone::Pulse, TableCurse::None, TableBuff::None, TT_Mace | TT_Occult, Icon(SpellIcon::HolyBolt), TableCurve::Fireball, 250 },
	{ WS::Shockwave, "Shockwave", "A slam whose shockwave runs far ahead and stuns", 11, 10, TW::Mace, 0, Line, 150, 5, 1, 0, 0, 0, DT::Physical, MissileID::Null, MG::StoneCurseShatter, TF_Stun | TF_Knockback, TableZone::None, TableCurse::None, TableBuff::None, TT_Mace, Icon(SpellIcon::Nova), TableCurve::Earthquake, 0 },
	{ WS::FistOfTheHeavens, "Fist of the Heavens", "Lightning strikes the target and sprays bolts all around", 13, 16, TW::None, 3, Burst, 300, 6, 1, 0, 0, 0, DT::Lightning, MissileID::ChargedBolt, MG::Lightning, TF_Spell, TableZone::None, TableCurse::None, TableBuff::None, TT_Mace | TT_Elemental, Icon(SpellIcon::ChainLightning), TableCurve::Fireball, 250 },
	// Quarterstaff
	{ WS::DashingStrike, "Dashing Strike", "Dash through the enemies and strike where you land", 5, 7, TW::Staff, 0, Dash, 150, 4, 1, 0, 0, 0, DT::Physical, MissileID::Null, MG::MetalHit, TF_Combo, TableZone::None, TableCurse::None, TableBuff::None, TT_Staff, Icon(SpellIcon::Phasing), TableCurve::LeapSlam, 320 },
	{ WS::LashingTailKick, "Lashing Tail Kick", "A spinning kick that throws back all around", 7, 7, TW::Staff, 0, Around, 140, 0, 1, 0, 0, 0, DT::Physical, MissileID::Null, MG::BoneHit, TF_Knockback | TF_Combo, TableZone::None, TableCurse::None, TableBuff::None, TT_Staff, Icon(SpellIcon::Search), TableCurve::Boneshatter, 0 },
	{ WS::WaveOfLight, "Wave of Light", "A wave of holy light bursts out around you", 9, 10, TW::Staff, 0, Around, 130, 0, 3, 0, 0, 0, DT::Fire, MissileID::Null, MG::HolyBoltExplosion, TF_Combo, TableZone::None, TableCurse::None, TableBuff::None, TT_Staff | TT_Elemental, Icon(SpellIcon::FlameWave), TableCurve::Boneshatter, 280 },
	{ WS::CycloneStrike, "Cyclone Strike", "A storm of strikes all around you, crackling with lightning", 11, 10, TW::Staff, 0, Around, 120, 0, 3, 0, 0, 0, DT::Lightning, MissileID::Null, MG::Lightning, TF_Combo, TableZone::None, TableCurse::None, TableBuff::None, TT_Staff | TT_Elemental, Icon(SpellIcon::ChainLightning), TableCurve::Boneshatter, 280 },
	{ WS::SevenSidedStrike, "Seven-Sided Strike", "Strike up to seven enemies nearby, one after another", 13, 14, TW::Staff, 0, Seven, 90, 7, 4, 0, 0, 0, DT::Physical, MissileID::Null, MG::MetalHit, TF_Combo, TableZone::None, TableCurse::None, TableBuff::None, TT_Staff, Icon(SpellIcon::Teleport), TableCurve::Boneshatter, 0 },
	// Bow
	{ WS::Multishot, "Multishot", "A wide fan of seven arrows", 9, 8, TW::Bow, 0, Projectiles, 75, 7, 0, 0, 0, 0, DT::Physical, MissileID::Arrow, MG::MetalHit, 0, TableZone::None, TableCurse::None, TableBuff::None, TT_Bow, Icon(SpellIcon::FireCloud), TableCurve::LightningArrow, 280 },
	{ WS::GuidedArrow, "Guided Arrow", "One heavy arrow that pierces through several enemies", 11, 8, TW::Bow, 0, Projectiles, 220, 1, 0, 0, 0, 0, DT::Physical, MissileID::Arrow, MG::MetalHit, TF_Pierce, TableZone::None, TableCurse::None, TableBuff::None, TT_Bow, Icon(SpellIcon::Search), TableCurve::LightningArrow, 0 },
	{ WS::FreezingArrow, "Freezing Arrow", "An arrow that bursts into freezing cold where it lands", 13, 10, TW::Bow, 0, Projectiles, 140, 1, 2, 0, 0, 0, DT::Magic, MissileID::Arrow, MG::BlueFlareExplosion, TF_LandingBurst, TableZone::None, TableCurse::None, TableBuff::None, TT_Bow | TT_Elemental, Icon(SpellIcon::StoneCurse), TableCurve::LightningArrow, 0 },
	// Crossbow
	{ WS::Impale, "Impale", "A crossbow bolt that skewers through enemies", 3, 6, TW::Crossbow, 0, Projectiles, 250, 1, 0, 0, 0, 0, DT::Physical, MissileID::Arrow, MG::MetalHit, TF_Pierce, TableZone::None, TableCurse::None, TableBuff::None, TT_Bow, Icon(SpellIcon::Search), TableCurve::LightningArrow, 0 },
	{ WS::ClusterBolt, "Cluster Bolt", "A bolt that bursts into flame where it lands", 5, 8, TW::Crossbow, 0, Projectiles, 100, 1, 1, 0, 0, 0, DT::Fire, MissileID::Arrow, MG::FireArrowExplosion, TF_LandingBurst, TableZone::None, TableCurse::None, TableBuff::None, TT_Bow | TT_Elemental, Icon(SpellIcon::Fireball), TableCurve::LightningArrow, 0 },
	{ WS::SpikeTrap, "Spike Trap", "Sets a trap that bursts when an enemy steps close", 7, 8, TW::Crossbow, 0, Zone, 200, 0, 2, 200, 0, 0, DT::Physical, MissileID::Null, MG::StoneCurseShatter, TF_Stun, TableZone::Trap, TableCurse::None, TableBuff::None, TT_Bow, Icon(SpellIcon::TrapDisarm), TableCurve::LightningArrow, 0 },
	{ WS::Sentry, "Sentry", "Plants a turret that shoots the nearest enemy for 8 seconds", 9, 12, TW::Crossbow, 0, Zone, 70, 0, 8, 160, 12, 0, DT::Physical, MissileID::Arrow, MG::MetalHit, TF_AtPlayer, TableZone::Turret, TableCurse::None, TableBuff::None, TT_Bow, Icon(SpellIcon::Golem), TableCurve::LightningArrow, 0 },
	{ WS::ExplosiveBolt, "Explosive Bolt", "A bolt that explodes where it lands", 11, 9, TW::Crossbow, 0, Projectiles, 120, 1, 2, 0, 0, 0, DT::Fire, MissileID::Arrow, MG::FireArrowExplosion, TF_LandingBurst, TableZone::None, TableCurse::None, TableBuff::None, TT_Bow | TT_Elemental, Icon(SpellIcon::Fireball), TableCurve::LightningArrow, 0 },
	{ WS::SiegeVolley, "Siege Volley", "Bolts rain down on the target area", 13, 14, TW::Crossbow, 0, Zone, 100, 0, 3, 50, 6, 0, DT::Physical, MissileID::Null, MG::MetalHit, 0, TableZone::RandomStrikes, TableCurse::None, TableBuff::None, TT_Bow, Icon(SpellIcon::FireCloud), TableCurve::RainOfArrows, 0 },
	// Shield
	{ WS::HolyShield, "Holy Shield", "Take 15% less damage from melee hits for 10 seconds", 5, 10, TW::Shield, 1, Buff, Flat, 0, 0, 200, 0, 0, DT::Physical, MissileID::Null, MG::HolyBoltExplosion, 0, TableZone::None, TableCurse::None, TableBuff::HolyShield, TT_Shield, Icon(SpellIcon::Pentagram), TableCurve::Boneshatter, 100 },
	{ WS::BlessedShield, "Blessed Shield", "Your shield crashes down on the target in a holy burst", 7, 8, TW::Shield, 0, Burst, 130, 0, 1, 0, 0, 0, DT::Physical, MissileID::Null, MG::HolyBoltExplosion, TF_Stun, TableZone::None, TableCurse::None, TableBuff::None, TT_Shield, Icon(SpellIcon::HolyBolt), TableCurve::LeapSlam, 340 },
	{ WS::Condemn, "Condemn", "After a moment, holy fire bursts out all around you", 9, 12, TW::Shield, 1, Zone, 250, 0, 3, 0, 0, 20, DT::Fire, MissileID::Null, MG::HolyBoltExplosion, TF_Spell | TF_FollowPlayer | TF_AtPlayer, TableZone::Delayed, TableCurse::None, TableBuff::None, TT_Shield | TT_Occult, Icon(SpellIcon::Apocalypse), TableCurve::Fireball, 250 },
	{ WS::ShieldWall, "Shield Wall", "Take 40% less damage from melee hits for 6 seconds", 11, 10, TW::Shield, 1, Buff, Flat, 0, 0, 120, 0, 0, DT::Physical, MissileID::Null, MG::StoneCurseShatter, 0, TableZone::None, TableCurse::None, TableBuff::ShieldWall, TT_Shield, Icon(SpellIcon::Reflect), TableCurve::Boneshatter, 100 },
	{ WS::FallingSword, "Falling Sword", "Leap into the air and crash down on the enemies", 13, 14, TW::Melee, 0, Dash, 250, 6, 2, 0, 0, 0, DT::Physical, MissileID::Null, MG::HolyBoltExplosion, TF_Stun, TableZone::None, TableCurse::None, TableBuff::None, TT_Shield | TT_Sword, Icon(SpellIcon::RedSkull), TableCurve::LeapSlam, 0 },
	// Fire and lightning
	{ WS::Meteor, "Meteor", "After a moment, a meteor crashes down on the target", 13, 25, TW::None, 2, Zone, 450, 0, 2, 0, 0, 30, DT::Fire, MissileID::Null, MG::ApocalypseBoom, TF_Spell, TableZone::Delayed, TableCurse::None, TableBuff::None, TT_Elemental, Icon(SpellIcon::FireCloud), TableCurve::Fireball, 250 },
	{ WS::Thunderstorm, "Thunderstorm", "A storm follows you for 10 seconds, striking the nearest enemy", 13, 20, TW::None, 3, Zone, 220, 0, 6, 200, 20, 0, DT::Lightning, MissileID::Null, MG::Lightning, TF_Spell | TF_FollowPlayer | TF_AtPlayer, TableZone::NearestStrike, TableCurse::None, TableBuff::None, TT_Elemental, Icon(SpellIcon::ChainLightning), TableCurve::Fireball, 250 },
	// Frost
	{ WS::GlacialSpike, "Glacial Spike", "An ice spike that shatters into a freezing burst", 5, 10, TW::None, 1, Projectiles, 220, 1, 1, 0, 0, 0, DT::Magic, MissileID::BlueFlare, MG::BlueFlareExplosion, TF_Spell | TF_LandingBurst, TableZone::None, TableCurse::None, TableBuff::None, TT_Elemental, Icon(SpellIcon::StoneCurse), TableCurve::Fireball, 250 },
	{ WS::FrozenArmor, "Frozen Armor", "For 15 seconds, take 20% less damage from melee hits and chill those who strike you", 7, 12, TW::None, 1, Buff, Flat, 0, 0, 300, 0, 0, DT::Magic, MissileID::Null, MG::StoneCurseShatter, 0, TableZone::None, TableCurse::None, TableBuff::FrozenArmor, TT_Elemental, Icon(SpellIcon::ManaShield), TableCurve::Boneshatter, 100 },
	{ WS::Blizzard, "Blizzard", "Ice shards rain on the target area for 4 seconds", 9, 16, TW::None, 1, Zone, 60, 0, 3, 80, 4, 0, DT::Magic, MissileID::Null, MG::BlueFlareExplosion, TF_Spell, TableZone::RandomStrikes, TableCurse::None, TableBuff::None, TT_Elemental, Icon(SpellIcon::Elemental), TableCurve::Fireball, 250 },
	{ WS::IceBarrage, "Ice Barrage", "A rapid stream of ice shards", 11, 14, TW::None, 1, Stream, 70, 8, 0, 0, 2, 0, DT::Magic, MissileID::BlueFlare, MG::StoneCurseShatter, TF_Spell, TableZone::None, TableCurse::None, TableBuff::None, TT_Elemental, Icon(SpellIcon::StoneCurse), TableCurve::Fireball, 250 },
	{ WS::FrozenOrb, "Frozen Orb", "An orb of frost bursts at the target into a ring of shards", 13, 22, TW::None, 1, Zone, 90, 16, 0, 0, 0, 15, DT::Magic, MissileID::BlueFlare, MG::BlueFlareExplosion, TF_Spell, TableZone::ShardRing, TableCurse::None, TableBuff::None, TT_Elemental, Icon(SpellIcon::Nova), TableCurve::Fireball, 250 },
	// Arcane
	{ WS::Haunt, "Haunt", "A spirit drains an enemy and moves on to the next when it dies", 3, 8, TW::None, 1, Curse, 150, 0, 0, 100, 0, 0, DT::Physical, MissileID::Null, MG::BloodHit, TF_Spell, TableZone::None, TableCurse::Haunt, TableBuff::None, TT_Occult, Icon(SpellIcon::BoneSpirit), TableCurve::Fireball, 250 },
	{ WS::LocustSwarm, "Locust Swarm", "A plague that eats at enemies and spreads when they die", 7, 12, TW::None, 1, Curse, 120, 0, 1, 100, 0, 0, DT::Physical, MissileID::Null, MG::BloodHit, TF_Spell, TableZone::None, TableCurse::Locust, TableBuff::None, TT_Occult, Icon(SpellIcon::DoomSerpents), TableCurve::Fireball, 250 },
	{ WS::CorpseExplosion, "Corpse Explosion", "Blows up a corpse near the target, burning everything around it", 9, 10, TW::None, 1, CorpseBurst, 260, 0, 2, 0, 0, 0, DT::Fire, MissileID::Null, MG::FireArrowExplosion, TF_Spell, TableZone::None, TableCurse::None, TableBuff::None, TT_Occult, Icon(SpellIcon::BloodStar), TableCurve::Fireball, 250 },
	{ WS::SoulHarvest, "Soul Harvest", "Takes the souls of nearby corpses: 3% more damage each, up to 10, for 10 seconds", 11, 10, TW::None, 1, SoulHarvest, Flat, 10, 4, 200, 0, 0, DT::Physical, MissileID::Null, MG::Resurrect, TF_Spell, TableZone::None, TableCurse::None, TableBuff::None, TT_Occult, Icon(SpellIcon::Resurrect), TableCurve::Boneshatter, 100 },
	{ WS::BonePrison, "Bone Prison", "Bones trap the enemies around the target for 3 seconds", 11, 10, TW::None, 1, Curse, Flat, 0, 1, 60, 0, 0, DT::Physical, MissileID::Null, MG::BoneHit, TF_Spell, TableZone::None, TableCurse::BonePrison, TableBuff::None, TT_Occult, Icon(SpellIcon::BoneSpirit), TableCurve::Boneshatter, 100 },
	// Curses
	{ WS::Weaken, "Weaken", "Curses enemies to hit 30% softer", 3, 8, TW::None, 1, Curse, Flat, 0, 2, 200, 0, 0, DT::Physical, MissileID::Null, MG::BloodHit, TF_Spell, TableZone::None, TableCurse::Weaken, TableBuff::None, TT_Occult, Icon(SpellIcon::Mana), TableCurve::Boneshatter, 100 },
	{ WS::IronMaiden, "Iron Maiden", "Curses enemies so their melee hits wound them as well", 5, 8, TW::None, 1, Curse, Flat, 0, 2, 200, 0, 0, DT::Physical, MissileID::Null, MG::BloodHit, TF_Spell, TableZone::None, TableCurse::IronMaiden, TableBuff::None, TT_Occult, Icon(SpellIcon::Reflect), TableCurve::Boneshatter, 100 },
	{ WS::Decrepify, "Decrepify", "Curses enemies to move slowly, hit softer and take more physical damage", 7, 10, TW::None, 1, Curse, Flat, 0, 2, 160, 0, 0, DT::Physical, MissileID::Null, MG::BloodHit, TF_Spell, TableZone::None, TableCurse::Decrepify, TableBuff::None, TT_Occult, Icon(SpellIcon::StoneCurse), TableCurve::Boneshatter, 100 },
	{ WS::LowerResist, "Lower Resist", "Curses enemies to take 30% more fire, lightning and cold damage", 9, 10, TW::None, 1, Curse, Flat, 0, 2, 200, 0, 0, DT::Physical, MissileID::Null, MG::BloodHit, TF_Spell, TableZone::None, TableCurse::LowerResist, TableBuff::None, TT_Occult | TT_Elemental, Icon(SpellIcon::Elemental), TableCurve::Boneshatter, 100 },
	{ WS::Confuse, "Confuse", "Curses enemies to attack each other for 6 seconds", 11, 12, TW::None, 1, Curse, Flat, 0, 2, 120, 0, 0, DT::Physical, MissileID::Null, MG::BloodHit, TF_Spell, TableZone::None, TableCurse::Confuse, TableBuff::None, TT_Occult, Icon(SpellIcon::Jester), TableCurve::Boneshatter, 100 },
	{ WS::Attract, "Attract", "Turns a whole pack on each other for 3 seconds", 13, 14, TW::None, 1, Curse, Flat, 0, 5, 60, 0, 0, DT::Physical, MissileID::Null, MG::BloodHit, TF_Spell, TableZone::None, TableCurse::Attract, TableBuff::None, TT_Occult, Icon(SpellIcon::Telekinesis), TableCurve::Boneshatter, 100 },
};
// clang-format on

/** @brief Melee hits from Weaken, Decrepify and the player's defensive buffs land this much softer, in percent. */
constexpr int WeakenPercent = 30;
constexpr int DecrepifyPercent = 25;
constexpr int FrozenArmorPercent = 20;
constexpr int HolyShieldPercent = 15;
constexpr int ShieldWallPercent = 40;
constexpr int LowerResistPercent = 30;
constexpr int DeathMarkPercent = 25;
constexpr int SoulDamagePercent = 3;
constexpr int MaxSouls = 10;
constexpr int RagePercent = 5;
/** @brief Haunt jumps this far to its next victim; Locust Swarm spreads to two within this range. */
constexpr int HauntJumpTiles = 6;
constexpr int LocustSpreadTiles = 3;
constexpr int LocustSpreads = 2;

struct SkillZone {
	WeaponSkill skill;
	Point center;
	int16_t ticksLeft;
	int16_t delay;
	int16_t counter;
	Fixed26_6 damage;
};

std::vector<SkillZone> Zones;

int GemLevel(const Player &player, WeaponSkill skill)
{
	return std::max(player.poe2.skillLevel(skill), 1);
}

int CurvePercent(const Player &player, WeaponSkill skill)
{
	return GemDamagePercent(SkillSlot::ForAttack(skill), player.poe2.skillLevel(skill));
}

bool CanHit(const Monster &monster)
{
	return !monster.hasNoLife() && !monster.isPlayerMinion() && monster.isPossibleToHit() && monster.mode != MonsterMode::Death && !CanTalkToMonst(monster);
}

template <typename Fn>
void ForEachMonsterNear(Point center, int radius, Fn fn)
{
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (CanHit(monster) && center.ExactDistance(monster.position.tile) <= radius)
			fn(monster);
	}
}

/** @brief The base of a table spell's damage, like Firebolt's: from Magic and the gem level. */
int SpellBase(const Player &player, int gemLevel)
{
	return GenerateRnd(10) + (player._pMagic / 8) + gemLevel + 2;
}

/** @brief A table skill's damage as a fixed amount, for zones and spell hits: from Magic, or from an average weapon hit. */
Fixed26_6 FixedDamage(const Player &player, const TableSkillData &data)
{
	int base;
	if ((data.flags & TF_Spell) != 0) {
		base = SpellBase(player, GemLevel(player, data.skill));
	} else {
		base = (player._pIMinDam + player._pIMaxDam) / 2;
		base += base * player._pIBonusDam / 100 + player._pIBonusDamMod + player._pDamageMod;
	}
	int64_t raw = static_cast<int64_t>(std::max(base, 1)) * 64;
	raw = raw * CurvePercent(player, data.skill) / 100;
	raw = raw * data.damagePercent / 100;
	raw = raw * (100 + SoulHarvestDamagePercent(player)) / 100;
	return Fixed26_6::fromRaw(static_cast<int32_t>(std::clamp<int64_t>(raw, 64, INT32_MAX / 2)));
}

void SpellHitMonster(const Player &player, Monster &monster, DamageType type, Fixed26_6 damage)
{
	if (&player != MyPlayer || !CanHit(monster) || damage <= 0)
		return;
	const Fixed26_6 dealt = Fixed26_6::fromRaw(static_cast<int32_t>(static_cast<int64_t>(damage.raw()) * MonsterDamageTakenPercent(monster, type) / 100));
	ApplyMonsterDamage(type, monster, dealt);
	monster.tag(player);
	TryInflictAilment(monster, player, type, dealt);
	if (monster.hasNoLife())
		M_StartKill(monster, player);
	else if (type == DamageType::Physical)
		M_StartHit(monster, player, dealt.raw());
}

void InflictTableBleed(const Player &player, Monster &monster, Fixed26_6 total, int ticks)
{
	Poe2MonsterState &state = monster.poe2;
	ticks = std::clamp(ticks, 1, 255);
	const int32_t perTick = std::max<int32_t>(total.raw() / ticks, 1);
	if (state.bleedTicks == 0 || perTick >= state.bleedDamagePerTick) {
		state.bleedDamagePerTick = perTick;
		state.bleedSource = static_cast<int8_t>(player.getId());
	}
	state.bleedTicks = static_cast<uint8_t>(std::max<int>(ticks, state.bleedTicks));
}

struct CastResult {
	bool hit = false;
};

/** @brief A weapon hit from a table skill, with the skill's extras. */
void WeaponHit(Player &player, Monster &monster, const TableSkillData &data, int damagePercent, CastResult &result)
{
	if (!SkillHitMonster(player, monster, damagePercent))
		return;
	result.hit = true;
	const bool alive = !monster.hasNoLife() && monster.mode != MonsterMode::Death;
	if ((data.flags & TF_Stun) != 0 && alive)
		HeavyStunMonster(monster);
	if ((data.flags & TF_Knockback) != 0 && alive && !monster.isUnique() && monster.mode != MonsterMode::Petrified)
		M_GetKnockback(monster, player.position.tile);
	if ((data.flags & TF_ArmourBreak) != 0 && alive)
		BreakMonsterArmour(monster);
	if ((data.flags & TF_Bleed) != 0 && alive)
		InflictTableBleed(player, monster, FixedDamage(player, data), 80);
	if (!alive)
		return;
	// Elemental attacks: part of the hit lands again as the skill's element.
	if (data.type != DamageType::Physical) {
		const Fixed26_6 extra = Fixed26_6::fromRaw(FixedDamage(player, data).raw() / 3);
		SpellHitMonster(player, monster, data.type, extra);
	}
	if ((data.flags & TF_Elements) != 0) {
		const Fixed26_6 extra = Fixed26_6::fromRaw(FixedDamage(player, data).raw() / 4);
		for (const DamageType type : { DamageType::Fire, DamageType::Lightning, DamageType::Magic })
			SpellHitMonster(player, monster, type, extra);
		AddVisualEffect(monster.position.tile, MissileGraphicID::Lightning, 1);
	}
}

void AfterCast(Player &player, const TableSkillData &data, const CastResult &result)
{
	if (!result.hit)
		return;
	if ((data.flags & TF_Combo) != 0)
		AddCombo(player);
	if ((data.flags & TF_Frenzy) != 0)
		GainFrenzyCharge(player);
}

Monster *TargetMonsterNear(Point target, int radius)
{
	Monster *best = nullptr;
	int bestDistance = radius + 1;
	ForEachMonsterNear(target, radius, [&](Monster &monster) {
		const int distance = target.WalkingDistance(monster.position.tile);
		if (distance < bestDistance) {
			best = &monster;
			bestDistance = distance;
		}
	});
	return best;
}

Monster *MonsterInFrontOf(const Player &player)
{
	const Point tile = player.position.tile + player._pdir;
	Monster *monster = FindMonsterAtPosition(tile);
	if (monster == nullptr || !CanHit(*monster) || monster->position.old != tile)
		return nullptr;
	return monster;
}

void AddZone(const Player &player, const TableSkillData &data, Point center, int delay)
{
	SkillZone zone;
	zone.skill = data.skill;
	zone.center = center;
	zone.ticksLeft = static_cast<int16_t>(std::max<int>(data.duration, 1));
	zone.delay = static_cast<int16_t>(delay);
	zone.counter = 0;
	zone.damage = FixedDamage(player, data);
	Zones.push_back(zone);
}

/** @brief A burst of a zone or a landing: hits everything within the radius. */
void ZoneBurst(const Player &player, const TableSkillData &data, Point center, int radius, Fixed26_6 damage)
{
	AddVisualEffect(center, data.effect, 0, 4);
	ForEachMonsterNear(center, radius, [&](Monster &monster) {
		SpellHitMonster(player, monster, data.type, damage);
		if ((data.flags & TF_Stun) != 0 && !monster.hasNoLife())
			HeavyStunMonster(monster);
	});
}

Missile *FireTableMissile(Player &player, const TableSkillData &data, Point from, Point target)
{
	const int gemLevel = GemLevel(player, data.skill);
	const bool spell = (data.flags & TF_Spell) != 0;
	const MissileID type = data.missile == MissileID::Null ? MissileID::Arrow : data.missile;
	// Weapon projectiles take their damage from the weapon when they hit, like arrows.
	const int damage = spell ? SpellBase(player, gemLevel) : ElementalArrowBaseDamage;
	Missile *missile = AddMissile(from, target, GetDirection(from, target), type, TARGET_MONSTERS, player, damage, spell ? gemLevel : 0);
	if (missile == nullptr)
		return nullptr;
	// Souls add to weapon projectiles through the weapon hit (see ResourceAttackDamagePercent).
	const int souls = spell ? SoulHarvestDamagePercent(player) : 0;
	missile->poe2.damagePercent = static_cast<int16_t>(std::min(CurvePercent(player, data.skill) * data.damagePercent / 100 * (100 + souls) / 100, INT16_MAX));
	if ((data.flags & TF_Pierce) != 0)
		missile->poe2.piercesLeft = 3;
	if ((data.flags & TF_LandingBurst) != 0) {
		// The burst goes off about when the projectile gets there.
		const int ticks = std::clamp(from.WalkingDistance(target) * 2, 2, 30);
		SkillZone zone { data.skill, target, 1, static_cast<int16_t>(ticks), 0, Fixed26_6::fromRaw(FixedDamage(player, data).raw() * 2 / 3) };
		Zones.push_back(zone);
	}
	return missile;
}

Point Along(Point from, Point target, int tiles)
{
	const Direction dir = GetDirection(from, target);
	return from + Displacement { dir } * tiles;
}

void FireFanOf(Player &player, const TableSkillData &data, Point target, int count)
{
	const Point tile = player.position.tile;
	if (count >= 8) {
		for (int i = 0; i < count; i++) {
			const Direction dir = static_cast<Direction>(i % 8);
			FireTableMissile(player, data, tile, tile + Displacement { dir } * 8);
		}
		return;
	}
	const Direction dir = GetDirection(tile, target);
	const Displacement side { Left(Left(dir)) };
	const Point far = Along(tile, target, 8);
	for (int i = 0; i < count; i++) {
		const int offset = (i + 1) / 2 * (i % 2 == 0 ? 1 : -1);
		// A one-projectile skill keeps its exact aim, so landing bursts land on the target.
		FireTableMissile(player, data, tile, count == 1 ? target : far + side * offset);
	}
}

void ApplyCurse(const Player &player, Monster &monster, const TableSkillData &data)
{
	Poe2MonsterState &state = monster.poe2;
	const uint8_t ticks = static_cast<uint8_t>(std::min<int>(data.duration, 255));
	switch (data.curse) {
	case TableCurse::Weaken:
		state.weakenTicks = ticks;
		break;
	case TableCurse::IronMaiden:
		state.ironMaidenTicks = ticks;
		break;
	case TableCurse::Decrepify:
		state.decrepifyTicks = ticks;
		state.chillTicks = ticks;
		state.chillPercent = static_cast<uint8_t>(std::max<int>(state.chillPercent, 40));
		break;
	case TableCurse::LowerResist:
		state.lowerResistTicks = ticks;
		break;
	case TableCurse::DeathMark:
		state.deathMarkTicks = ticks;
		break;
	case TableCurse::Confuse:
	case TableCurse::Attract:
		if (monster.isUnique())
			break;
		if ((monster.flags & MFLAG_BERSERK) == 0) {
			monster.flags |= MFLAG_BERSERK | MFLAG_GOLEM;
			state.confusedByCurse = true;
		}
		state.confuseTicks = ticks;
		break;
	case TableCurse::BonePrison:
		if (!monster.isUnique()) {
			state.heavyStunTicks = ticks;
			state.stunBuildup = 0;
		}
		break;
	case TableCurse::Haunt:
		state.hauntTicks = ticks;
		InflictTableBleed(player, monster, FixedDamage(player, data), ticks);
		break;
	case TableCurse::Locust:
		state.locustTicks = ticks;
		InflictTableBleed(player, monster, FixedDamage(player, data), ticks);
		break;
	default:
		break;
	}
	monster.tag(player);
	AddVisualEffect(monster.position.tile, data.effect);
}

std::optional<Point> CorpseNear(Point center, int radius)
{
	std::optional<Point> best;
	int bestDistance = radius + 1;
	for (int y = -radius; y <= radius; y++) {
		for (int x = -radius; x <= radius; x++) {
			const Point tile = center + Displacement { x, y };
			if (!InDungeonBounds(tile) || dCorpse[tile.x][tile.y] == 0)
				continue;
			const int distance = center.WalkingDistance(tile);
			if (distance < bestDistance) {
				best = tile;
				bestDistance = distance;
			}
		}
	}
	return best;
}

Point SkillTargetTile(const Player &player)
{
	Point target = pcursmonst != -1 ? static_cast<Point>(Monsters[pcursmonst].position.tile) : cursPosition;
	if (target == player.position.tile || !InDungeonBounds(target))
		target = player.position.tile + player._pdir;
	return target;
}

} // namespace

std::span<const TableSkillData> TableSkills()
{
	return Table;
}

const TableSkillData *FindTableSkill(WeaponSkill skill)
{
	for (const TableSkillData &data : Table) {
		if (data.skill == skill)
			return &data;
	}
	return nullptr;
}

const TableSkillData *FindTableSkillGem(const ItemData &data)
{
	if (data.iMiscId != IMISC_SKILLGEM_TABLE)
		return nullptr;
	const std::string_view name = data.iName;
	for (const TableSkillData &skill : Table) {
		if (name.size() == skill.name.size() + 4 && name.starts_with(skill.name) && name.ends_with(" Gem"))
			return &skill;
	}
	return nullptr;
}

bool IsTableMeleeSkill(const TableSkillData &data)
{
	return (data.flags & TF_Spell) == 0 && IsAnyOf(data.shape, Strike, Arc, Around, Flurry, Dash, Line, Seven, CallOfTheAncients)
	    && IsNoneOf(data.weapon, TableWeapon::Bow, TableWeapon::Crossbow);
}

bool IsTableProjectileSkill(const TableSkillData &data)
{
	return IsAnyOf(data.shape, Projectiles, Stream) || data.zone == TableZone::Turret || data.zone == TableZone::ShardRing;
}

bool IsTableAreaSkill(const TableSkillData &data)
{
	return IsAnyOf(data.shape, Around, Burst, Dash, Line, Zone, Curse, CorpseBurst, CallOfTheAncients) || (data.flags & TF_LandingBurst) != 0;
}

bool StartTableSkill(Player &player, const TableSkillData &data)
{
	// Checks that need the world; the caller pays for and starts the skill.
	if (data.shape == CorpseBurst && !CorpseNear(SkillTargetTile(player), data.radius)) {
		EventPlrMsg(_("Corpse Explosion needs a corpse near the target."), UiFlags::ColorRed);
		return false;
	}
	if (data.shape == SoulHarvest && !CorpseNear(player.position.tile, data.radius)) {
		EventPlrMsg(_("Soul Harvest needs corpses nearby."), UiFlags::ColorRed);
		return false;
	}
	if ((data.flags & TF_LifeCost) != 0) {
		const Fixed26_6 cost = Fixed26_6::fromRaw(player._pHitPoints.raw() / 20);
		if (player._pHitPoints - cost < Fixed26_6::fromInt(1))
			return false;
		player._pHitPoints -= cost;
		player._pHPBase -= cost;
	}
	return true;
}

void ExecuteTableSkill(Player &player, const TableSkillData &data, Point target)
{
	// The cast effects were played by PlaySkillEffects just before.
	CastResult result;
	const Point tile = player.position.tile;
	switch (data.shape) {
	case Strike:
		if (Monster *monster = MonsterInFrontOf(player); monster != nullptr)
			WeaponHit(player, *monster, data, data.damagePercent, result);
		break;
	case Arc:
		for (const Direction dir : { player._pdir, Left(player._pdir), Right(player._pdir) }) {
			Monster *monster = FindMonsterAtPosition(tile + dir);
			if (monster != nullptr && CanHit(*monster) && monster->position.old == tile + dir)
				WeaponHit(player, *monster, data, data.damagePercent, result);
		}
		break;
	case Around:
	case Dash: {
		std::vector<Monster *> targets;
		ForEachMonsterNear(tile, std::max<int>(data.radius, 1), [&](Monster &monster) { targets.push_back(&monster); });
		for (Monster *monster : targets)
			WeaponHit(player, *monster, data, data.damagePercent, result);
		break;
	}
	case Flurry:
		for (int i = 0; i < data.count; i++) {
			Monster *monster = MonsterInFrontOf(player);
			if (monster == nullptr)
				break;
			WeaponHit(player, *monster, data, data.damagePercent, result);
		}
		break;
	case Projectiles: {
		int count = data.count + GemExtraProjectiles(SkillSlot::ForAttack(data.skill), player.poe2.skillLevel(data.skill));
		if ((data.flags & TF_Frenzy) != 0)
			count += player.poe2.frenzyCharges;
		FireFanOf(player, data, target, count);
		break;
	}
	case Stream:
		StartSkillVolley(player, data.skill, target, data.count + GemExtraProjectiles(SkillSlot::ForAttack(data.skill), player.poe2.skillLevel(data.skill)));
		break;
	case Burst:
		if ((data.flags & TF_Spell) != 0) {
			ZoneBurst(player, data, target, data.radius, FixedDamage(player, data));
		} else {
			std::vector<Monster *> targets;
			ForEachMonsterNear(target, data.radius, [&](Monster &monster) { targets.push_back(&monster); });
			for (Monster *monster : targets)
				WeaponHit(player, *monster, data, data.damagePercent, result);
		}
		// Bolts spray out from where it lands.
		if (data.missile != MissileID::Null) {
			for (int i = 0; i < data.count; i++) {
				const Displacement out { static_cast<Direction>(i * 8 / std::max<int>(data.count, 1) % 8) };
				FireTableMissile(player, data, target, target + out * 6);
			}
		}
		break;
	case Line: {
		const Direction dir = GetDirection(tile, target);
		std::vector<Monster *> hit;
		for (int step = 1; step <= data.count; step++) {
			const Point at = tile + Displacement { dir } * step;
			if (!InDungeonBounds(at) || IsTileSolid(at))
				break;
			AddVisualEffect(at, data.effect, step * 2, step == 1 ? 4 : 0);
			ForEachMonsterNear(at, data.radius, [&](Monster &monster) {
				if (std::find(hit.begin(), hit.end(), &monster) == hit.end())
					hit.push_back(&monster);
			});
		}
		for (Monster *monster : hit)
			WeaponHit(player, *monster, data, data.damagePercent, result);
		break;
	}
	case Zone:
		AddZone(player, data, (data.flags & TF_AtPlayer) != 0 ? tile : target, data.delay);
		break;
	case Curse:
		if (data.radius == 0) {
			if (Monster *monster = TargetMonsterNear(target, 1); monster != nullptr)
				ApplyCurse(player, *monster, data);
		} else {
			ForEachMonsterNear(target, data.radius, [&](Monster &monster) { ApplyCurse(player, monster, data); });
		}
		break;
	case Buff:
		switch (data.buff) {
		case TableBuff::FrozenArmor:
			player.poe2.frozenArmorTicks = data.duration;
			break;
		case TableBuff::HolyShield:
			player.poe2.holyShieldTicks = data.duration;
			break;
		case TableBuff::ShieldWall:
			player.poe2.shieldWallTicks = data.duration;
			break;
		case TableBuff::WarCry:
			player.poe2.warcryTicks = static_cast<uint8_t>(std::min<int>(data.duration, 255));
			player.Say(HeroSpeech::TimeToDie);
			ForEachMonsterNear(tile, data.radius, [&](Monster &monster) {
				HeavyStunMonster(monster);
				monster.tag(player);
			});
			break;
		default:
			break;
		}
		break;
	case Seven: {
		std::vector<Monster *> targets;
		ForEachMonsterNear(tile, data.radius, [&](Monster &monster) { targets.push_back(&monster); });
		std::sort(targets.begin(), targets.end(), [&](const Monster *a, const Monster *b) {
			return tile.WalkingDistance(a->position.tile) < tile.WalkingDistance(b->position.tile);
		});
		if (targets.size() > data.count)
			targets.resize(data.count);
		int delay = 0;
		for (Monster *monster : targets) {
			AddVisualEffect(monster->position.tile, MissileGraphicID::Etherealize, delay);
			WeaponHit(player, *monster, data, data.damagePercent, result);
			delay += 2;
		}
		break;
	}
	case CorpseBurst:
		if (const std::optional<Point> corpse = CorpseNear(target, data.radius); corpse) {
			dCorpse[corpse->x][corpse->y] = 0;
			ZoneBurst(player, data, *corpse, data.radius, FixedDamage(player, data));
			PlaySfxLoc(SfxID::BigExplosion, *corpse);
		}
		break;
	case SoulHarvest: {
		int souls = 0;
		for (int y = -data.radius; y <= data.radius && souls < MaxSouls; y++) {
			for (int x = -data.radius; x <= data.radius && souls < MaxSouls; x++) {
				const Point at = tile + Displacement { x, y };
				if (!InDungeonBounds(at) || dCorpse[at.x][at.y] == 0)
					continue;
				dCorpse[at.x][at.y] = 0;
				AddVisualEffect(at, MissileGraphicID::Resurrect, souls);
				souls++;
			}
		}
		player.poe2.soulStacks = static_cast<uint8_t>(std::min(player.poe2.soulStacks + souls, MaxSouls));
		player.poe2.soulTicks = data.duration;
		break;
	}
	case CallOfTheAncients: {
		const int rage = player.poe2.rage;
		player.poe2.rage = 0;
		std::vector<Monster *> targets;
		ForEachMonsterNear(tile, data.radius, [&](Monster &monster) { targets.push_back(&monster); });
		for (int wave = 0; wave < data.count; wave++) {
			EffectRingAround(tile, 1 + wave % 2, data.effect, wave * 3);
			for (Monster *monster : targets) {
				if (!monster->hasNoLife())
					WeaponHit(player, *monster, data, data.damagePercent * (100 + rage * RagePercent) / 100 / data.count, result);
			}
		}
		break;
	}
	}
	AfterCast(player, data, result);
}

void FireTableShot(Player &player, const TableSkillData &data, Point target, int index)
{
	const Point tile = player.position.tile;
	const Direction dir = GetDirection(tile, target);
	constexpr int Wobble[] = { 0, 1, -1, 0, -1, 1 };
	FireTableMissile(player, data, tile, Along(tile, target, 8) + Displacement { Left(Left(dir)) } * Wobble[index % 6]);
	PlaySfxLoc((data.flags & TF_Spell) != 0 ? SfxID::SpellStoneCurse : SfxID::ShootBow, tile);
}

void PlayTableSkillEffects(const Player &player, const TableSkillData &data, Point target)
{
	const Point tile = player.position.tile;
	const Direction dir = player._pdir;
	const bool mine = &player == MyPlayer;
	// A flash in the skill's colour where it starts, from graphics every level has loaded.
	MissileGraphicID flash = MissileGraphicID::MetalHit;
	switch (data.type) {
	case DamageType::Fire:
		flash = MissileGraphicID::Inferno;
		break;
	case DamageType::Lightning:
		flash = MissileGraphicID::Lightning;
		break;
	case DamageType::Magic:
		flash = MissileGraphicID::StoneCurseShatter;
		break;
	default:
		if ((data.flags & TF_Spell) != 0 || data.shape == Curse)
			flash = MissileGraphicID::HolyBoltExplosion;
		break;
	}
	AddVisualEffect(tile + dir, flash, 0, 3);
	switch (data.shape) {
	case Strike:
	case Flurry:
		AddVisualEffect(tile + dir, data.effect, 1, 4);
		PlaySfxLoc(SfxID::Swing2, tile);
		break;
	case Arc:
		AddVisualEffect(tile + Left(dir), data.effect, 0);
		AddVisualEffect(tile + dir, data.effect, 1, 4);
		AddVisualEffect(tile + Right(dir), data.effect, 2);
		PlaySfxLoc(SfxID::Swing2, tile);
		break;
	case Around:
	case Dash:
	case CallOfTheAncients:
		EffectRingAround(tile, 1, data.effect, 0);
		if (data.radius > 1)
			EffectRingAround(tile, data.radius, data.effect, 3);
		PlaySfxLoc(SfxID::Swing, tile);
		if (mine)
			ShakeCamera(ShakeStrength::Light, 4);
		break;
	case Burst:
		AddVisualEffect(target, data.effect, 0, 6);
		if (data.radius > 0)
			EffectRingAround(target, data.radius, data.effect, 2);
		if (mine)
			ShakeCamera(ShakeStrength::Medium, 5);
		break;
	case Curse:
		EffectRingAround(target, std::max<int>(data.radius, 1), data.effect, 0);
		PlaySfxLoc(SfxID::SpellBloodStar, target);
		break;
	case Buff:
		EffectRingAround(tile, 1, data.effect, 0);
		PlaySfxLoc(SfxID::SpellManaShield, tile);
		break;
	case SoulHarvest:
		EffectRingAround(tile, 2, MissileGraphicID::BloodHit, 0);
		break;
	default:
		break;
	}
}

void ProcessSkillZones(Player &player)
{
	if (Zones.empty())
		return;
	if (player.hasNoLife() || leveltype == DTYPE_TOWN) {
		Zones.clear();
		return;
	}
	for (size_t i = 0; i < Zones.size();) {
		SkillZone &zone = Zones[i];
		const TableSkillData *data = FindTableSkill(zone.skill);
		bool done = data == nullptr;
		if (!done && (data->flags & TF_FollowPlayer) != 0)
			zone.center = player.position.tile;
		if (!done && zone.delay > 0) {
			zone.delay--;
		} else if (!done) {
			// Landing bursts of projectiles, and Delayed zones, go off once.
			if ((data->flags & TF_LandingBurst) != 0 || data->zone == TableZone::Delayed) {
				ZoneBurst(player, *data, zone.center, std::max<int>(data->radius, 1), zone.damage);
				if (data->zone == TableZone::Delayed) {
					PlaySfxLoc(SfxID::BigExplosion, zone.center);
					ShakeCamera(ShakeStrength::Medium, 6);
				}
				done = true;
			} else if (data->zone == TableZone::ShardRing) {
				AddVisualEffect(zone.center, data->effect, 0, 6);
				for (int s = 0; s < data->count; s++) {
					constexpr int Ring[16][2] = { { 4, 0 }, { 4, 2 }, { 4, 4 }, { 2, 4 }, { 0, 4 }, { -2, 4 }, { -4, 4 }, { -4, 2 }, { -4, 0 }, { -4, -2 }, { -4, -4 }, { -2, -4 }, { 0, -4 }, { 2, -4 }, { 4, -4 }, { 4, -2 } };
					if (Missile *shard = FireTableMissile(player, *data, zone.center, zone.center + Displacement { Ring[s % 16][0], Ring[s % 16][1] }); shard != nullptr)
						shard->duration = 9;
				}
				done = true;
			} else if (data->zone == TableZone::Trap) {
				Monster *victim = TargetMonsterNear(zone.center, 1);
				if (victim != nullptr) {
					ZoneBurst(player, *data, zone.center, data->radius, zone.damage);
					EffectRingAround(zone.center, 1, data->effect, 1);
					done = true;
				} else if (zone.counter % 20 == 0) {
					AddVisualEffect(zone.center, MissileGraphicID::MetalHit);
				}
			} else if (zone.counter % std::max<int>(data->interval, 1) == 0) {
				switch (data->zone) {
				case TableZone::Pulse:
					ZoneBurst(player, *data, zone.center, data->radius, zone.damage);
					break;
				case TableZone::RandomStrikes: {
					const Point at = zone.center + Displacement { GenerateRnd(2 * data->radius + 1) - data->radius, GenerateRnd(2 * data->radius + 1) - data->radius };
					if (InDungeonBounds(at))
						ZoneBurst(player, *data, at, 1, zone.damage);
					break;
				}
				case TableZone::NearestStrike:
					if (Monster *monster = TargetMonsterNear(zone.center, data->radius); monster != nullptr) {
						AddVisualEffect(monster->position.tile, MissileGraphicID::ThinLightning, 0, 6);
						AddVisualEffect(monster->position.tile, data->effect);
						SpellHitMonster(player, *monster, data->type, zone.damage);
						PlaySfxLoc(SfxID::SpellLightning, monster->position.tile);
					}
					break;
				case TableZone::Turret:
					if (Monster *monster = TargetMonsterNear(zone.center, data->radius); monster != nullptr) {
						FireTableMissile(player, *data, zone.center, monster->position.tile);
						AddVisualEffect(zone.center, MissileGraphicID::MetalHit);
						PlaySfxLoc(SfxID::ShootBow, zone.center);
					}
					break;
				default:
					break;
				}
			}
			zone.counter++;
			if (--zone.ticksLeft <= 0 && data->zone != TableZone::Trap)
				done = true;
			if (data->zone == TableZone::Trap && zone.counter > data->duration)
				done = true;
		}
		if (done)
			Zones.erase(Zones.begin() + static_cast<std::ptrdiff_t>(i));
		else
			i++;
	}
}

void ClearSkillZones()
{
	Zones.clear();
}

size_t SkillZoneCount()
{
	return Zones.size();
}

Fixed26_6 ApplyTableSkillDefences(Monster &monster, Player &player, Fixed26_6 damage)
{
	if (!gbIsPoe2)
		return damage;
	int percent = 100;
	if (monster.poe2.weakenTicks > 0)
		percent -= WeakenPercent;
	if (monster.poe2.decrepifyTicks > 0)
		percent -= DecrepifyPercent;
	if (player.poe2.frozenArmorTicks > 0) {
		percent -= FrozenArmorPercent;
		monster.poe2.chillTicks = 60;
		monster.poe2.chillPercent = static_cast<uint8_t>(std::max<int>(monster.poe2.chillPercent, 30));
	}
	if (player.poe2.holyShieldTicks > 0)
		percent -= HolyShieldPercent;
	if (player.poe2.shieldWallTicks > 0)
		percent -= ShieldWallPercent;
	damage = Fixed26_6::fromRaw(damage.raw() * std::max(percent, 10) / 100);
	// Iron Maiden: the monster takes its own hit.
	if (monster.poe2.ironMaidenTicks > 0 && !monster.hasNoLife() && &player == MyPlayer) {
		ApplyMonsterDamage(DamageType::Physical, monster, damage);
		AddVisualEffect(monster.position.tile, MissileGraphicID::BloodHit);
		if (monster.hasNoLife())
			M_StartKill(monster, player);
	}
	return damage;
}

void ProcessTableCurses(Monster &monster)
{
	Poe2MonsterState &state = monster.poe2;
	for (uint8_t *ticks : { &state.weakenTicks, &state.decrepifyTicks, &state.ironMaidenTicks, &state.lowerResistTicks, &state.deathMarkTicks, &state.hauntTicks, &state.locustTicks }) {
		if (*ticks > 0)
			(*ticks)--;
	}
	if (state.confuseTicks > 0 && --state.confuseTicks == 0 && state.confusedByCurse) {
		monster.flags &= ~(MFLAG_BERSERK | MFLAG_GOLEM);
		state.confusedByCurse = false;
	}
}

int TableCurseDamageTakenPercent(const Monster &monster, DamageType type)
{
	int percent = 0;
	if (monster.poe2.deathMarkTicks > 0)
		percent += DeathMarkPercent;
	if (monster.poe2.lowerResistTicks > 0 && IsAnyOf(type, DamageType::Fire, DamageType::Lightning, DamageType::Magic))
		percent += LowerResistPercent;
	if (monster.poe2.decrepifyTicks > 0 && type == DamageType::Physical)
		percent += DecrepifyPercent;
	return percent;
}

void OnTableCursedMonsterKilled(const Monster &monster)
{
	const Poe2MonsterState &state = monster.poe2;
	if ((state.hauntTicks == 0 && state.locustTicks == 0) || state.bleedSource < 0 || static_cast<size_t>(state.bleedSource) >= Players.size())
		return;
	const Player &player = Players[state.bleedSource];
	const Point from = monster.position.tile;
	const bool haunt = state.hauntTicks > 0;
	const int range = haunt ? HauntJumpTiles : LocustSpreadTiles;
	std::vector<Monster *> next;
	ForEachMonsterNear(from, range, [&](Monster &other) {
		if (&other != &monster)
			next.push_back(&other);
	});
	std::sort(next.begin(), next.end(), [&](const Monster *a, const Monster *b) {
		return from.WalkingDistance(a->position.tile) < from.WalkingDistance(b->position.tile);
	});
	const size_t jumps = haunt ? 1 : LocustSpreads;
	if (next.size() > jumps)
		next.resize(jumps);
	const uint8_t ticks = haunt ? state.hauntTicks : state.locustTicks;
	for (Monster *other : next) {
		Poe2MonsterState &target = other->poe2;
		(haunt ? target.hauntTicks : target.locustTicks) = ticks;
		target.bleedTicks = std::max(target.bleedTicks, ticks);
		target.bleedDamagePerTick = std::max(target.bleedDamagePerTick, state.bleedDamagePerTick);
		target.bleedSource = state.bleedSource;
		other->tag(player);
		AddVisualEffect(other->position.tile, MissileGraphicID::BloodHit);
	}
}

int SoulHarvestDamagePercent(const Player &player)
{
	return player.poe2.soulStacks * SoulDamagePercent;
}

} // namespace devilution
