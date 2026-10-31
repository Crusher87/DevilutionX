#include "poe2/table_skills.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include "diablo.h"
#include "effects.h"
#include "engine/backbuffer_state.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "levels/tile_properties.hpp"
#include "missiles.h"
#include "monster.h"
#include "objects.h"
#include "panels/spell_icons.hpp"
#include "player.h"
#include "plrmsg.h"
#include "poe2/camera_shake.hpp"
#include "poe2/attack_speed.hpp"
#include "poe2/default_attack.hpp"
#include "poe2/dodge_roll.hpp"
#include "poe2/gem_levels.hpp"
#include "poe2/minion_gems.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/skill_looks.hpp"
#include "poe2/skill_resources.hpp"
#include "poe2/spell_base.hpp"
#include "poe2/spirit_skills.hpp"
#include "poe2/support_table.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/unique_effects.hpp"
#include "poe2/vfx.hpp"
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
	{ WS::Whirlwind, "Whirlwind", "Spin through the enemies in your path, hitting all around where you stop", 5, 8, TW::AxeOrSword, 0, Dash, 85, 4, 1, 0, 0, 0, DT::Physical, MissileID::Null, MG::MetalHit, 0, TableZone::None, TableCurse::None, TableBuff::None, TT_Sword | TT_Axe, Icon(SpellIcon::Berserk), TableCurve::Boneshatter, 300 },
	{ WS::Sacrifice, "Sacrifice", "A brutal strike paid for with 5% of your life", 7, 6, TW::Sword, 0, Strike, 330, 0, 0, 0, 0, 0, DT::Physical, MissileID::Null, MG::BloodHit, TF_LifeCost, TableZone::None, TableCurse::None, TableBuff::None, TT_Sword, Icon(SpellIcon::BloodBoil), TableCurve::Boneshatter, 0 },
	{ WS::Zeal, "Zeal", "Four lightning-fast strikes at the enemy in front", 9, 5, TW::Sword, 0, Flurry, 140, 4, 0, 0, 0, 0, DT::Physical, MissileID::Null, MG::MetalHit, 0, TableZone::None, TableCurse::None, TableBuff::None, TT_Sword, Icon(SpellIcon::Jester), TableCurve::Boneshatter, 290 },
	{ WS::Vengeance, "Vengeance", "A strike that bursts into fire, lightning and cold", 11, 5, TW::Sword, 0, Strike, 260, 0, 0, 0, 0, 0, DT::Physical, MissileID::Null, MG::HolyBoltExplosion, TF_Elements, TableZone::None, TableCurse::None, TableBuff::None, TT_Sword | TT_Elemental, Icon(SpellIcon::Elemental), TableCurve::Boneshatter, 0 },
	{ WS::ArkainesWrath, "Arkaine's Wrath", "A sweeping blow that calls down lightning and staggers", 13, 14, TW::Sword, 0, Arc, 200, 0, 0, 0, 0, 0, DT::Lightning, MissileID::Null, MG::Lightning, TF_Stun, TableZone::None, TableCurse::None, TableBuff::None, TT_Sword | TT_Elemental, Icon(SpellIcon::LightningWall), TableCurve::Boneshatter, 0 },
	// Dagger
	{ WS::FanOfBlades, "Fan of Blades", "Throw daggers in every direction", 5, 5, TW::Dagger, 0, Projectiles, 70, 8, 0, 0, 0, 0, DT::Physical, MissileID::Arrow, MG::MetalHit, 0, TableZone::None, TableCurse::None, TableBuff::None, TT_Sword, Icon(SpellIcon::Sentinel), TableCurve::LightningArrow, 280 },
	{ WS::ViperStrike, "Cobra Strike", "A poisoned stab: the poison deals chaos damage over time", 7, 5, TW::Dagger, 0, Strike, 200, 0, 0, 0, 0, 0, DT::Physical, MissileID::Null, MG::BloodHit, TF_Poison, TableZone::None, TableCurse::None, TableBuff::None, TT_Sword, Icon(SpellIcon::DoomSerpents), TableCurve::Boneshatter, 0 },
	{ WS::BladeSentinel, "Blade Sentinel", "A whirling blade that guards the target spot for 5 seconds", 9, 12, TW::Dagger, 0, Zone, 35, 0, 1, 100, 20, 0, DT::Physical, MissileID::Null, MG::MetalHit, 0, TableZone::Pulse, TableCurse::None, TableBuff::None, TT_Sword, Icon(SpellIcon::Sentinel), TableCurve::Boneshatter, 260 },
	{ WS::DeathMark, "Death Mark", "Marks an enemy to take 25% more damage", 11, 9, TW::Dagger, 1, Curse, Flat, 0, 0, 160, 0, 0, DT::Physical, MissileID::Null, MG::BloodHit, 0, TableZone::None, TableCurse::DeathMark, TableBuff::None, TT_Sword | TT_Occult, Icon(SpellIcon::RedSkull), TableCurve::Boneshatter, 100 },
	{ WS::BladeFury, "Blade Fury", "A rapid stream of thrown daggers", 13, 12, TW::Dagger, 0, Stream, 70, 8, 0, 0, 2, 0, DT::Physical, MissileID::Arrow, MG::MetalHit, 0, TableZone::None, TableCurse::None, TableBuff::None, TT_Sword, Icon(SpellIcon::Jester), TableCurve::LightningArrow, 290 },
	// Axe
	{ WS::DoubleThrow, "Double Throw", "Hurl two axes, and one more for each Frenzy charge", 7, 5, TW::Axe, 0, Projectiles, 230, 2, 0, 0, 0, 0, DT::Physical, MissileID::Arrow, MG::MetalHit, TF_Frenzy, TableZone::None, TableCurse::None, TableBuff::None, TT_Axe, Icon(SpellIcon::Telekinesis), TableCurve::LightningArrow, 290 },
	{ WS::Rend, "Rend", "A tearing sweep that leaves enemies bleeding", 9, 7, TW::Axe, 0, Arc, 100, 0, 0, 0, 0, 0, DT::Physical, MissileID::Null, MG::BloodHit, TF_Bleed, TableZone::None, TableCurse::None, TableBuff::None, TT_Axe, Icon(SpellIcon::BloodBoil), TableCurve::Boneshatter, 0 },
	{ WS::WarCry, "War Cry", "Stuns enemies around you", 11, 10, TW::None, 1, Buff, Flat, 0, 2, 160, 0, 0, DT::Physical, MissileID::Null, MG::StoneCurseShatter, TF_Stun, TableZone::None, TableCurse::None, TableBuff::WarCry, TT_Axe | TT_Mace | TT_Sword, Icon(SpellIcon::LongHorn), TableCurve::Boneshatter, 200 },
	{ WS::CallOfTheAncients, "Call of the Ancients", "Spends all Rage: ancestral warriors strike all around, 5% harder per Rage", 13, 12, TW::Axe, 0, CallOfTheAncients, 60, 3, 2, 0, 0, 0, DT::Physical, MissileID::Null, MG::HolyBoltExplosion, TF_Rage, TableZone::None, TableCurse::None, TableBuff::None, TT_Axe, Icon(SpellIcon::Golem), TableCurve::Boneshatter, 0 },
	// Mace
	{ WS::Fissure, "Fissure", "The ground splits open in a line, staggering what it hits", 7, 10, TW::Mace, 0, Line, 65, 6, 1, 0, 0, 0, DT::Physical, MissileID::Null, MG::StoneCurseShatter, TF_Stun, TableZone::None, TableCurse::None, TableBuff::None, TT_Mace, Icon(SpellIcon::PentaStar), TableCurve::Earthquake, 0 },
	{ WS::BlessedHammer, "Blessed Hammer", "Holy hammers spiral around you for 4 seconds", 9, 10, TW::None, 1, Zone, 20, 0, 1, 80, 20, 0, DT::Physical, MissileID::Null, MG::HolyBoltExplosion, TF_Spell | TF_FollowPlayer | TF_AtPlayer, TableZone::Pulse, TableCurse::None, TableBuff::None, TT_Mace | TT_Occult, Icon(SpellIcon::HolyBolt), TableCurve::Fireball, 312 },
	{ WS::Shockwave, "Shockwave", "A slam whose shockwave runs far ahead and stuns", 11, 10, TW::Mace, 0, Line, 70, 5, 1, 0, 0, 0, DT::Physical, MissileID::Null, MG::StoneCurseShatter, TF_Stun | TF_Knockback, TableZone::None, TableCurse::None, TableBuff::None, TT_Mace, Icon(SpellIcon::Nova), TableCurve::Earthquake, 0 },
	{ WS::FistOfTheHeavens, "Fist of the Heavens", "Lightning strikes the target and sprays bolts all around", 13, 16, TW::None, 3, Burst, 110, 6, 1, 0, 0, 0, DT::Lightning, MissileID::ChargedBolt, MG::Lightning, TF_Spell, TableZone::None, TableCurse::None, TableBuff::None, TT_Mace | TT_Elemental, Icon(SpellIcon::ChainLightning), TableCurve::Fireball, 312 },
	// Quarterstaff
	{ WS::DashingStrike, "Dashing Strike", "Dash through the enemies, striking each one on the way", 5, 7, TW::Staff, 0, Dash, 80, 4, 1, 0, 0, 0, DT::Physical, MissileID::Null, MG::MetalHit, TF_Combo, TableZone::None, TableCurse::None, TableBuff::None, TT_Staff, Icon(SpellIcon::Phasing), TableCurve::LeapSlam, 320 },
	{ WS::LashingTailKick, "Lashing Tail Kick", "A spinning kick that throws back all around", 7, 7, TW::Staff, 0, Around, 65, 0, 1, 0, 0, 0, DT::Physical, MissileID::Null, MG::BoneHit, TF_Knockback | TF_Combo, TableZone::None, TableCurse::None, TableBuff::None, TT_Staff, Icon(SpellIcon::Search), TableCurve::Boneshatter, 0 },
	{ WS::WaveOfLight, "Wave of Light", "A wave of holy light bursts out around you", 9, 10, TW::Staff, 0, Around, 50, 0, 3, 0, 0, 0, DT::Fire, MissileID::Null, MG::HolyBoltExplosion, TF_Combo, TableZone::None, TableCurse::None, TableBuff::None, TT_Staff | TT_Elemental, Icon(SpellIcon::FlameWave), TableCurve::Boneshatter, 280 },
	{ WS::CycloneStrike, "Cyclone Strike", "A storm of strikes all around you that drags enemies in, crackling with lightning", 11, 10, TW::Staff, 0, Around, 50, 0, 3, 0, 0, 0, DT::Lightning, MissileID::Null, MG::Lightning, TF_Combo, TableZone::None, TableCurse::None, TableBuff::None, TT_Staff | TT_Elemental, Icon(SpellIcon::ChainLightning), TableCurve::Boneshatter, 280 },
	{ WS::SevenSidedStrike, "Seven-Sided Strike", "Strike up to seven enemies nearby, one after another", 13, 11, TW::Staff, 0, Seven, 200, 7, 4, 0, 0, 0, DT::Physical, MissileID::Null, MG::MetalHit, TF_Combo, TableZone::None, TableCurse::None, TableBuff::None, TT_Staff, Icon(SpellIcon::Teleport), TableCurve::Boneshatter, 0 },
	// Bow. Fire Arrow and Explosive Arrow came from the crossbow (dropped 2026-10-27); Fire Arrow is tier I, beside the Rogue's starting Hail of Arrows
	{ WS::ClusterBolt, "Fire Arrow", "An arrow that bursts into flame where it lands", 1, 8, TW::Bow, 0, Projectiles, 100, 1, 1, 0, 0, 0, DT::Fire, MissileID::Arrow, MG::FireArrowExplosion, TF_LandingBurst, TableZone::None, TableCurse::None, TableBuff::None, TT_Bow | TT_Elemental, Icon(SpellIcon::Fireball), TableCurve::LightningArrow, 0 },
	{ WS::Multishot, "Multishot", "A wide fan of seven arrows", 9, 6, TW::Bow, 0, Projectiles, 95, 7, 0, 0, 0, 0, DT::Physical, MissileID::Arrow, MG::MetalHit, 0, TableZone::None, TableCurse::None, TableBuff::None, TT_Bow, Icon(SpellIcon::FireCloud), TableCurve::LightningArrow, 280 },
	{ WS::GuidedArrow, "Guided Arrow", "One heavy arrow that pierces through several enemies", 11, 5, TW::Bow, 0, Projectiles, 220, 1, 0, 0, 0, 0, DT::Physical, MissileID::Arrow, MG::MetalHit, TF_Pierce, TableZone::None, TableCurse::None, TableBuff::None, TT_Bow, Icon(SpellIcon::Search), TableCurve::LightningArrow, 0 },
	{ WS::FreezingArrow, "Freezing Arrow", "An arrow that bursts into freezing cold where it lands", 13, 10, TW::Bow, 0, Projectiles, 100, 1, 2, 0, 0, 0, DT::Cold, MissileID::Arrow, MG::BlueFlareExplosion, TF_LandingBurst, TableZone::None, TableCurse::None, TableBuff::None, TT_Bow | TT_Elemental, Icon(SpellIcon::StoneCurse), TableCurve::LightningArrow, 0 },
	{ WS::ExplosiveBolt, "Explosive Arrow", "An arrow that explodes where it lands", 11, 9, TW::Bow, 0, Projectiles, 110, 1, 2, 0, 0, 0, DT::Fire, MissileID::Arrow, MG::FireArrowExplosion, TF_LandingBurst, TableZone::None, TableCurse::None, TableBuff::None, TT_Bow | TT_Elemental, Icon(SpellIcon::Fireball), TableCurve::LightningArrow, 0 },
	// Crossbow: retired with the crossbows (see IsRetiredSkill); an old gem still loads and works with a crossbow
	{ WS::Impale, "Impale", "A crossbow bolt that skewers through enemies", 3, 6, TW::Crossbow, 0, Projectiles, 250, 1, 0, 0, 0, 0, DT::Physical, MissileID::Arrow, MG::MetalHit, TF_Pierce, TableZone::None, TableCurse::None, TableBuff::None, TT_Bow, Icon(SpellIcon::Search), TableCurve::LightningArrow, 0 },
	{ WS::SpikeTrap, "Spike Trap", "Sets a trap that bursts when an enemy steps close", 7, 8, TW::Crossbow, 0, Zone, 75, 0, 1, 200, 0, 0, DT::Physical, MissileID::Null, MG::StoneCurseShatter, TF_Stun, TableZone::Trap, TableCurse::None, TableBuff::None, TT_Bow, Icon(SpellIcon::TrapDisarm), TableCurve::LightningArrow, 0 },
	{ WS::Sentry, "Sentry", "Plants a turret that shoots the nearest enemy for 8 seconds", 9, 12, TW::Crossbow, 0, Zone, 60, 0, 8, 160, 12, 0, DT::Physical, MissileID::Arrow, MG::MetalHit, TF_AtPlayer, TableZone::Turret, TableCurse::None, TableBuff::None, TT_Bow, Icon(SpellIcon::Golem), TableCurve::LightningArrow, 0 },
	{ WS::SiegeVolley, "Ballista Barrage", "Bolts rain down on the target area", 13, 14, TW::Crossbow, 0, Zone, 75, 0, 3, 50, 6, 0, DT::Physical, MissileID::Null, MG::MetalHit, 0, TableZone::RandomStrikes, TableCurse::None, TableBuff::None, TT_Bow, Icon(SpellIcon::FireCloud), TableCurve::RainOfArrows, 0 },
	// Shield
	{ WS::HolyShield, "Holy Shield", "Take 15% less damage from melee hits for 10 seconds", 5, 10, TW::Shield, 1, Buff, Flat, 0, 0, 200, 0, 0, DT::Physical, MissileID::Null, MG::HolyBoltExplosion, 0, TableZone::None, TableCurse::None, TableBuff::HolyShield, TT_Shield, Icon(SpellIcon::Pentagram), TableCurve::Boneshatter, 100 },
	{ WS::BlessedShield, "Blessed Shield", "Your shield crashes down on the target in a holy burst", 7, 8, TW::Shield, 0, Burst, 70, 0, 1, 0, 0, 0, DT::Physical, MissileID::Null, MG::HolyBoltExplosion, TF_Stun, TableZone::None, TableCurse::None, TableBuff::None, TT_Shield, Icon(SpellIcon::HolyBolt), TableCurve::LeapSlam, 340 },
	{ WS::Condemn, "Condemn", "After a moment, holy fire bursts out all around you", 9, 12, TW::Shield, 1, Zone, 50, 0, 3, 0, 0, 20, DT::Fire, MissileID::Null, MG::HolyBoltExplosion, TF_Spell | TF_FollowPlayer | TF_AtPlayer, TableZone::Delayed, TableCurse::None, TableBuff::None, TT_Shield | TT_Occult, Icon(SpellIcon::Apocalypse), TableCurve::Fireball, 312 },
	{ WS::ShieldWall, "Shield Wall", "Take 40% less damage from melee hits for 6 seconds", 11, 10, TW::Shield, 1, Buff, Flat, 0, 0, 120, 0, 0, DT::Physical, MissileID::Null, MG::StoneCurseShatter, 0, TableZone::None, TableCurse::None, TableBuff::ShieldWall, TT_Shield, Icon(SpellIcon::Reflect), TableCurve::Boneshatter, 100 },
	{ WS::FallingSword, "Falling Sword", "Leap into the air and crash down on the enemies", 13, 14, TW::Melee, 0, Dash, 100, 6, 2, 0, 0, 0, DT::Physical, MissileID::Null, MG::HolyBoltExplosion, TF_Stun, TableZone::None, TableCurse::None, TableBuff::None, TT_Shield | TT_Sword, Icon(SpellIcon::RedSkull), TableCurve::LeapSlam, 340 },
	// Fire and lightning
	{ WS::Meteor, "Meteor", "After a moment, a meteor crashes down on the target", 13, 25, TW::None, 2, Zone, 150, 0, 2, 0, 0, 30, DT::Fire, MissileID::Null, MG::ApocalypseBoom, TF_Spell, TableZone::Delayed, TableCurse::None, TableBuff::None, TT_Elemental, Icon(SpellIcon::FireCloud), TableCurve::Fireball, 312 },
	{ WS::Thunderstorm, "Thunderstorm", "A storm follows you for 10 seconds, striking the nearest enemy", 13, 20, TW::None, 3, Zone, 100, 0, 6, 200, 20, 0, DT::Lightning, MissileID::Null, MG::Lightning, TF_Spell | TF_FollowPlayer | TF_AtPlayer, TableZone::NearestStrike, TableCurse::None, TableBuff::None, TT_Elemental, Icon(SpellIcon::ChainLightning), TableCurve::Fireball, 312 },
	// Frost
	{ WS::GlacialSpike, "Glacial Spike", "A great spike of ice bursts out of the ground under the target, then shatters", 5, 10, TW::None, 1, Zone, 200, 0, 1, 0, 0, 3, DT::Cold, MissileID::Null, MG::BlueFlareExplosion, TF_Spell, TableZone::IceSpike, TableCurse::None, TableBuff::None, TT_Elemental, Icon(SpellIcon::StoneCurse), TableCurve::Fireball, 312 },
	{ WS::FrozenArmor, "Frozen Armor", "For 15 seconds, take 20% less damage from melee hits and chill those who strike you", 7, 12, TW::None, 1, Buff, Flat, 0, 0, 300, 0, 0, DT::Cold, MissileID::Null, MG::StoneCurseShatter, 0, TableZone::None, TableCurse::None, TableBuff::FrozenArmor, TT_Elemental, Icon(SpellIcon::ManaShield), TableCurve::Boneshatter, 100 },
	{ WS::Blizzard, "Blizzard", "Ice shards rain on the target area for 4 seconds", 9, 16, TW::None, 1, Zone, 21, 0, 3, 80, 4, 0, DT::Cold, MissileID::Null, MG::BlueFlareExplosion, TF_Spell, TableZone::RandomStrikes, TableCurse::None, TableBuff::None, TT_Elemental, Icon(SpellIcon::Elemental), TableCurve::Fireball, 312 },
	{ WS::IceBarrage, "Ray of Frost", "A rapid stream of ice shards", 11, 14, TW::None, 1, Stream, 70, 8, 0, 0, 2, 0, DT::Cold, MissileID::BlueFlare, MG::StoneCurseShatter, TF_Spell, TableZone::None, TableCurse::None, TableBuff::None, TT_Elemental, Icon(SpellIcon::StoneCurse), TableCurve::Fireball, 312 },
	{ WS::FrozenOrb, "Frozen Orb", "An orb of frost bursts at the target into a ring of shards", 13, 22, TW::None, 1, Zone, 70, 16, 0, 0, 0, 15, DT::Cold, MissileID::BlueFlare, MG::BlueFlareExplosion, TF_Spell, TableZone::ShardRing, TableCurse::None, TableBuff::None, TT_Elemental, Icon(SpellIcon::Nova), TableCurve::Fireball, 312 },
	// Arcane
	{ WS::Haunt, "Haunt", "A spirit drains the enemy with chaos over time. When it dies, the spirit jumps to the nearest enemy.", 3, 8, TW::None, 1, Curse, 350, 0, 0, 100, 0, 0, DT::Chaos, MissileID::Null, MG::BoneSpirit, TF_Spell, TableZone::None, TableCurse::Haunt, TableBuff::None, TT_Occult, Icon(SpellIcon::BoneSpirit), TableCurve::Fireball, 312 },
	{ WS::LocustSwarm, "Locust Swarm", "A chaos plague that eats at enemies and spreads when they die", 7, 12, TW::None, 1, Curse, 130, 0, 1, 40, 0, 0, DT::Chaos, MissileID::Null, MG::BloodHit, TF_Spell, TableZone::None, TableCurse::Locust, TableBuff::None, TT_Occult, Icon(SpellIcon::DoomSerpents), TableCurve::Fireball, 312 },
	{ WS::CorpseExplosion, "Corpse Explosion", "Blows up a corpse near the target, burning everything around it", 9, 10, TW::None, 1, CorpseBurst, 50, 0, 2, 0, 0, 0, DT::Fire, MissileID::Null, MG::FireArrowExplosion, TF_Spell, TableZone::None, TableCurse::None, TableBuff::None, TT_Occult, Icon(SpellIcon::BloodStar), TableCurve::Fireball, 312 },
	{ WS::SoulHarvest, "Soul Harvest", "Takes the souls of nearby corpses: 3% more damage each, up to 10, for 10 seconds", 11, 10, TW::None, 1, SoulHarvest, Flat, 10, 4, 200, 0, 0, DT::Physical, MissileID::Null, MG::Resurrect, TF_Spell, TableZone::None, TableCurse::None, TableBuff::None, TT_Occult, Icon(SpellIcon::Resurrect), TableCurve::Boneshatter, 100 },
	{ WS::BonePrison, "Bone Prison", "Bone spikes burst up under the target, hurting the enemies there and trapping them for 3 seconds", 5, 10, TW::None, 1, Zone, 200, 0, 1, 60, 0, 3, DT::Physical, MissileID::Null, MG::BoneHit, TF_Spell | TF_Pin, TableZone::IceSpike, TableCurse::None, TableBuff::None, TT_Occult, Icon(SpellIcon::BoneSpirit), TableCurve::Fireball, 312 },
	// Curses
	{ WS::Weaken, "Weaken", "Curses enemies to hit 30% softer", 3, 8, TW::None, 1, Curse, Flat, 0, 2, 200, 0, 0, DT::Physical, MissileID::Null, MG::BloodHit, TF_Spell, TableZone::None, TableCurse::Weaken, TableBuff::None, TT_Occult, Icon(SpellIcon::Mana), TableCurve::Boneshatter, 100 },
	{ WS::IronMaiden, "Iron Maiden", "Curses enemies so their melee hits wound them as well", 5, 8, TW::None, 1, Curse, Flat, 0, 2, 200, 0, 0, DT::Physical, MissileID::Null, MG::BloodHit, TF_Spell, TableZone::None, TableCurse::IronMaiden, TableBuff::None, TT_Occult, Icon(SpellIcon::Reflect), TableCurve::Boneshatter, 100 },
	{ WS::Decrepify, "Decrepify", "Curses enemies to move slowly, hit softer and take more physical damage", 7, 10, TW::None, 1, Curse, Flat, 0, 2, 160, 0, 0, DT::Physical, MissileID::Null, MG::BloodHit, TF_Spell, TableZone::None, TableCurse::Decrepify, TableBuff::None, TT_Occult, Icon(SpellIcon::StoneCurse), TableCurve::Boneshatter, 100 },
	{ WS::LowerResist, "Lower Resist", "Curses enemies with -30% to fire, lightning and cold resistance", 9, 10, TW::None, 1, Curse, Flat, 0, 2, 200, 0, 0, DT::Physical, MissileID::Null, MG::BloodHit, TF_Spell, TableZone::None, TableCurse::LowerResist, TableBuff::None, TT_Occult | TT_Elemental, Icon(SpellIcon::Elemental), TableCurve::Boneshatter, 100 },
	{ WS::Confuse, "Confuse", "Curses enemies to attack each other for 6 seconds", 11, 12, TW::None, 1, Curse, Flat, 0, 2, 120, 0, 0, DT::Physical, MissileID::Null, MG::BloodHit, TF_Spell, TableZone::None, TableCurse::Confuse, TableBuff::None, TT_Occult, Icon(SpellIcon::Jester), TableCurve::Boneshatter, 100 },
	{ WS::Attract, "Attract", "Turns a whole pack on each other for 3 seconds", 13, 14, TW::None, 1, Curse, Flat, 0, 5, 60, 0, 0, DT::Physical, MissileID::Null, MG::BloodHit, TF_Spell, TableZone::None, TableCurse::Attract, TableBuff::None, TT_Occult, Icon(SpellIcon::Telekinesis), TableCurve::Boneshatter, 100 },
	// Granted by the caster weapons rework's staves and wands (caster-weapons-plan-v2.md 6.12 and 7.2): no gem, no mana, no
	// tab; each hit is set for its cast time (skill_speed.tsv), so the rows take no damage_pct.
	// POE2 Mashup: three shards a cast, not five; each keeps its hit (Curtis 2026-10-29).
	{ WS::FreezingShards, "Freezing Shards", "A fan of three shards of ice that chill what they hit", 0, 0, TW::None, 1, Projectiles, 64, 3, 0, 0, 0, 0, DT::Cold, MissileID::BlueFlare, MG::StoneCurseShatter, TF_Spell | TF_Granted | TF_PerTarget, TableZone::None, TableCurse::None, TableBuff::None, 0, Icon(SpellIcon::StoneCurse), TableCurve::Fireball, 312 },
	{ WS::LightningBolt, "Lightning Bolt", "Lightning strikes the target and everything next to it", 0, 0, TW::None, 3, Burst, 46, 0, 1, 0, 0, 0, DT::Lightning, MissileID::Null, MG::Lightning, TF_Spell | TF_Granted | TF_PerTarget, TableZone::None, TableCurse::None, TableBuff::None, 0, Icon(SpellIcon::Lightning), TableCurve::Fireball, 312 },
	{ WS::ChimingBolt, "Chiming Bolt", "A ringing bolt of lightning that pierces through enemies; every fourth cast leaves a Sigil of Power under you", 0, 0, TW::None, 3, Projectiles, 46, 1, 0, 0, 0, 0, DT::Lightning, MissileID::YellowFlare, MG::Lightning, TF_Spell | TF_Granted | TF_Pierce | TF_PerTarget, TableZone::None, TableCurse::None, TableBuff::None, 0, Icon(SpellIcon::ChargedBolt), TableCurve::Fireball, 312, GrantedRider::SigilEveryFourth },
	{ WS::SolarOrb, "Solar Orb", "An orb of fire at the target that burns what's near it for 10 seconds; one at a time", 0, 0, TW::None, 2, Zone, 71, 0, 1, 200, 28, 0, DT::Fire, MissileID::Null, MG::FireArrowExplosion, TF_Spell | TF_Granted | TF_PerTarget, TableZone::Pulse, TableCurse::None, TableBuff::None, 0, Icon(SpellIcon::Fireball), TableCurve::Fireball, 312 },
	{ WS::Soulrend, "Soulrend", "A chaos bolt that tears through enemies and leaves them bleeding", 0, 0, TW::None, 1, Projectiles, 28, 1, 0, 0, 0, 0, DT::Chaos, MissileID::RedFlare, MG::BloodHit, TF_Spell | TF_Granted | TF_Pierce | TF_Bleed | TF_PerTarget, TableZone::None, TableCurse::None, TableBuff::None, 0, Icon(SpellIcon::BloodBoil), TableCurve::Fireball, 312 },
	{ WS::Reap, "Reap", "A bloody scythe sweeps the target and everything next to it", 0, 0, TW::None, 1, Burst, 64, 0, 1, 0, 0, 0, DT::Physical, MissileID::Null, MG::BloodHit, TF_Spell | TF_Granted | TF_PerTarget, TableZone::None, TableCurse::None, TableBuff::None, 0, Icon(SpellIcon::BloodStar), TableCurve::Fireball, 312 },
	{ WS::FrostLance, "Frost Lance", "A lance of ice that pierces through enemies and chills them", 0, 0, TW::None, 1, Projectiles, 48, 1, 0, 0, 0, 0, DT::Cold, MissileID::BlueFlare, MG::StoneCurseShatter, TF_Spell | TF_Granted | TF_Pierce | TF_PerTarget, TableZone::None, TableCurse::None, TableBuff::None, 0, Icon(SpellIcon::Elemental), TableCurve::Fireball, 312 },
	{ WS::EnervatingNova, "Enervating Nova", "A ring of lightning bursts out around you", 0, 0, TW::None, 3, Around, 48, 0, 3, 0, 0, 0, DT::Lightning, MissileID::Null, MG::Lightning, TF_Spell | TF_Granted | TF_PerTarget, TableZone::None, TableCurse::None, TableBuff::None, 0, Icon(SpellIcon::Nova), TableCurve::Fireball, 312 },
	{ WS::ConsecratedGround, "Consecrated Ground", "Holy fire bursts out around you, then heals you for 4 seconds", 0, 0, TW::None, 2, Around, 63, 0, 2, 0, 0, 0, DT::Fire, MissileID::Null, MG::HolyBoltExplosion, TF_Spell | TF_Granted | TF_PerTarget, TableZone::None, TableCurse::None, TableBuff::None, 0, Icon(SpellIcon::HolyBolt), TableCurve::Fireball, 312, GrantedRider::RegenBuff },
	{ WS::DarkPact, "Dark Pact", "A burst of chaos at the target, paid with 5% of your life", 0, 0, TW::None, 1, Burst, 50, 0, 1, 0, 0, 0, DT::Chaos, MissileID::Null, MG::BloodHit, TF_Spell | TF_Granted | TF_LifeCost | TF_PerTarget, TableZone::None, TableCurse::None, TableBuff::None, 0, Icon(SpellIcon::BloodBoil), TableCurve::Fireball, 312 },
	{ WS::FeastOfFlesh, "Feast of Flesh", "Bones burst out around you; what they kill feeds you life and mana", 0, 0, TW::None, 1, Around, 45, 0, 2, 0, 0, 0, DT::Physical, MissileID::Null, MG::BoneHit, TF_Spell | TF_Granted | TF_PerTarget, TableZone::None, TableCurse::None, TableBuff::None, 0, Icon(SpellIcon::BoneSpirit), TableCurve::Fireball, 312, GrantedRider::KillRestoreBoth },
	{ WS::WitheringBolt, "Withering Bolt", "A chaos bolt that withers what it hits, so it takes more chaos damage", 0, 0, TW::None, 1, Projectiles, 37, 1, 0, 0, 0, 0, DT::Chaos, MissileID::BloodStar, MG::BloodHit, TF_Spell | TF_Granted, TableZone::None, TableCurse::None, TableBuff::None, 0, Icon(SpellIcon::DoomSerpents), TableCurve::Fireball, 312, GrantedRider::Wither },
	{ WS::BoneBolt, "Bone Bolt", "A shard of bone that builds up twice the stun", 0, 0, TW::None, 1, Projectiles, 38, 1, 0, 0, 0, 0, DT::Physical, MissileID::OrangeFlare, MG::BoneHit, TF_Spell | TF_Granted, TableZone::None, TableCurse::None, TableBuff::None, 0, Icon(SpellIcon::BoneSpirit), TableCurve::Fireball, 312, GrantedRider::StunBuildup },
	// POE2 Mashup: no damage, as its name says: it only drains mana and slows (Curtis 2026-10-29; was 15%).
	{ WS::ManaDrain, "Mana Drain", "A bolt that deals no damage, but drains mana and slows", 0, 0, TW::None, 1, Projectiles, 0, 1, 0, 0, 0, 0, DT::Cold, MissileID::BlueFlare2, MG::StoneCurseShatter, TF_Spell | TF_Granted, TableZone::None, TableCurse::None, TableBuff::None, 0, Icon(SpellIcon::Mana), TableCurve::Fireball, 312, GrantedRider::ManaDrain },
	{ WS::PowerSiphon, "Power Siphon", "A bolt that kills what it leaves on low life, for a Frenzy charge", 0, 0, TW::None, 1, Projectiles, 35, 1, 0, 0, 0, 0, DT::Physical, MissileID::YellowFlare, MG::BloodHit, TF_Spell | TF_Granted, TableZone::None, TableCurse::None, TableBuff::None, 0, Icon(SpellIcon::Telekinesis), TableCurve::Fireball, 312, GrantedRider::CullFrenzy },
	{ WS::TormentBolt, "Torment Bolt", "A bolt that weakens what it hits for 2 seconds", 0, 0, TW::None, 1, Projectiles, 38, 1, 0, 0, 0, 0, DT::Physical, MissileID::RedFlare, MG::BloodHit, TF_Spell | TF_Granted, TableZone::None, TableCurse::None, TableBuff::None, 0, Icon(SpellIcon::Reflect), TableCurve::Fireball, 312, GrantedRider::Weaken },
	{ WS::CriticalBolt, "Critical Bolt", "A bolt whose every fifth cast hits twice as hard", 0, 0, TW::None, 1, Projectiles, 34, 1, 0, 0, 0, 0, DT::Physical, MissileID::OrangeFlare, MG::MetalHit, TF_Spell | TF_Granted, TableZone::None, TableCurse::None, TableBuff::None, 0, Icon(SpellIcon::Search), TableCurve::Fireball, 312, GrantedRider::EveryFifthDouble },
	{ WS::EchoingBlast, "Echoing Blast", "A blast of fire at the target that goes off again a moment later", 0, 0, TW::None, 2, Burst, 31, 0, 1, 0, 0, 0, DT::Fire, MissileID::Null, MG::FireArrowExplosion, TF_Spell | TF_Granted | TF_PerTarget, TableZone::None, TableCurse::None, TableBuff::None, 0, Icon(SpellIcon::Inferno), TableCurve::Fireball, 312, GrantedRider::Echo },
	{ WS::VolatileBolt, "Volatile Bolt", "A fire bolt; what it kills bursts into flame", 0, 0, TW::None, 2, Projectiles, 40, 1, 0, 0, 0, 0, DT::Fire, MissileID::OrangeFlare, MG::FireArrowExplosion, TF_Spell | TF_Granted, TableZone::None, TableCurse::None, TableBuff::None, 0, Icon(SpellIcon::Fireball), TableCurve::Fireball, 312, GrantedRider::BurstOnKill },
	{ WS::GalvanicBolt, "Galvanic Bolt", "A bolt of lightning that zaps one more enemy nearby", 0, 0, TW::None, 3, Projectiles, 34, 1, 0, 0, 0, 0, DT::Lightning, MissileID::YellowFlare, MG::Lightning, TF_Spell | TF_Granted, TableZone::None, TableCurse::None, TableBuff::None, 0, Icon(SpellIcon::ChainLightning), TableCurve::Fireball, 312, GrantedRider::ChainOnce },
	{ WS::Decompose, "Decompose", "A rotting chaos bolt whose plague passes on when its victim dies", 0, 0, TW::None, 1, Projectiles, 22, 1, 0, 0, 0, 0, DT::Chaos, MissileID::BloodStar, MG::BloodHit, TF_Spell | TF_Granted, TableZone::None, TableCurse::None, TableBuff::None, 0, Icon(SpellIcon::DoomSerpents), TableCurve::Fireball, 312, GrantedRider::Plague },
	{ WS::Exsanguinate, "Exsanguinate", "A blood bolt that bleeds; killing a bleeding enemy heals you", 0, 0, TW::None, 1, Projectiles, 24, 1, 0, 0, 0, 0, DT::Physical, MissileID::RedFlare, MG::BloodHit, TF_Spell | TF_Granted | TF_Bleed, TableZone::None, TableCurse::None, TableBuff::None, 0, Icon(SpellIcon::BloodBoil), TableCurve::Fireball, 312, GrantedRider::KillRestoreLife },
	{ WS::FrostBolt, "Frost Bolt", "A bolt of frost that chills what it hits", 0, 0, TW::None, 1, Projectiles, 40, 1, 0, 0, 0, 0, DT::Cold, MissileID::BlueFlare, MG::StoneCurseShatter, TF_Spell | TF_Granted, TableZone::None, TableCurse::None, TableBuff::None, 0, Icon(SpellIcon::StoneCurse), TableCurve::Fireball, 312 },
	{ WS::WitheringRay, "Withering Ray", "A stream of four chaos shards that wither what they hit", 0, 0, TW::None, 1, Stream, 9, 4, 0, 0, 2, 0, DT::Chaos, MissileID::RedFlare, MG::BloodHit, TF_Spell | TF_Granted, TableZone::None, TableCurse::None, TableBuff::None, 0, Icon(SpellIcon::DoomSerpents), TableCurve::Fireball, 312, GrantedRider::Wither },
	{ WS::TwinBolts, "Twin Bolts", "Two bolts at once", 0, 0, TW::None, 1, Projectiles, 19, 2, 0, 0, 0, 0, DT::Physical, MissileID::OrangeFlare, MG::MetalHit, TF_Spell | TF_Granted, TableZone::None, TableCurse::None, TableBuff::None, 0, Icon(SpellIcon::BloodStar), TableCurve::Fireball, 312 },
	// Gem skills appended after the granted ones.
	{ WS::Despair, "Despair", "Curses enemies with -30% to chaos resistance", 9, 10, TW::None, 1, Curse, Flat, 0, 2, 200, 0, 0, DT::Physical, MissileID::Null, MG::BloodHit, TF_Spell, TableZone::None, TableCurse::Despair, TableBuff::None, TT_Occult, Icon(SpellIcon::Apocalypse), TableCurve::Boneshatter, 100 },
	{ WS::PlagueArrow, "Plague Arrow", "An arrow that bursts into poison on the first enemy it hits", 1, 8, TW::Bow, 0, Projectiles, 90, 1, 1, 0, 0, 0, DT::Physical, MissileID::Arrow, MG::AcidSplat, TF_PoisonBurst, TableZone::None, TableCurse::None, TableBuff::None, TT_Bow, Icon(SpellIcon::DoomSerpents), TableCurve::LightningArrow, 0 },
	{ WS::HexRupture, "Hex Rupture", "Bursts the curses half run out on enemies around the target", 11, 12, TW::None, 1, HexBurst, 160, 0, 3, 0, 0, 0, DT::Chaos, MissileID::Null, MG::BloodHit, TF_Spell, TableZone::None, TableCurse::None, TableBuff::None, TT_Occult, Icon(SpellIcon::BloodBoil), TableCurve::Fireball, 312 },
	{ WS::BoneSpikes, "Bone Spikes", "Bones burst out of the ground in a line ahead of you, staggering what they hit", 3, 8, TW::None, 1, Line, 107, 5, 1, 0, 0, 0, DT::Physical, MissileID::Null, MG::BoneHit, TF_Spell | TF_Stun, TableZone::None, TableCurse::None, TableBuff::None, TT_Occult, Icon(SpellIcon::BoneSpirit), TableCurve::Fireball, 312 },
	{ WS::BoneSpear, "Bone Spear", "A heavy spear of bone that flies through every enemy in its path and builds up twice the stun", 7, 12, TW::None, 1, Projectiles, 180, 1, 0, 0, 0, 0, DT::Physical, MissileID::OrangeFlare, MG::BoneHit, TF_Spell | TF_Pierce, TableZone::None, TableCurse::None, TableBuff::None, TT_Occult, Icon(SpellIcon::BoneSpirit), TableCurve::Fireball, 312, GrantedRider::StunBuildup },
	{ WS::BloodNova, "Blood Nova", "A ring of blood bursts out around you and leaves enemies bleeding; costs 5% of your life", 9, 10, TW::None, 1, Around, 120, 0, 2, 0, 0, 0, DT::Physical, MissileID::Null, MG::BloodHit, TF_Spell | TF_LifeCost | TF_Bleed, TableZone::None, TableCurse::None, TableBuff::None, TT_Occult, Icon(SpellIcon::BloodBoil), TableCurve::Fireball, 312 },
};
// clang-format on

/** @brief Melee hits from Weaken, Decrepify and the player's defensive buffs land this much softer, in percent. */
constexpr int WeakenPercent = 30;
constexpr int DecrepifyPercent = 25;
constexpr int FrozenArmorPercent = 20;
constexpr int HolyShieldPercent = 15;
/**
 * Thunderstorm, Blessed Hammer and Blizzard out at once: one cast, and a recast replaces it (Curtis 2026-10-28; recast
 * as their cast time allowed, Thunderstorm reached 11 at once).
 */
constexpr int MaxStackingZones = 1;
constexpr int ShieldWallPercent = 40;
constexpr int DeathMarkPercent = 25;
/** Decrepify chills the cursed monster this much, in percent. */
constexpr int DecrepifyChillPercent = 40;
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
	/** The skill's radius with its supports' area changes. */
	int8_t radius;
};

std::vector<SkillZone> Zones;

int GemLevel(const Player &player, WeaponSkill skill)
{
	return std::max(player.poe2.effectiveLevel(skill), 1);
}

/**
 * @brief A granted skill takes the spell damage of gear, the tree and Spirit skills, as Staff Bolt and the gem spells do
 * (UniqueSpellDamagePercent); the element's own damage comes on the hit. 100 for every other row.
 */
int GrantedSpellDamagePercent(const Player &player, const TableSkillData &data)
{
	if (!IsGrantedTableSkill(data))
		return 100;
	return UniqueSpellDamagePercent(player, SpellID::Firebolt) * SpiritSpellDamagePercent(player) / 100;
}

int CurvePercent(const Player &player, WeaponSkill skill)
{
	return GemDamagePercent(SkillSlot::ForAttack(skill), player.poe2.effectiveLevel(skill));
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
	return RollSpellBase(player, gemLevel);
}

/** @brief A table skill's damage as a fixed amount, for zones and spell hits: from Magic, or from an average weapon hit. */
Fixed26_6 FixedDamage(const Player &player, const TableSkillData &data)
{
	int base;
	if ((data.flags & TF_Spell) != 0) {
		base = SpellBase(player, GemLevel(player, data.skill));
	} else {
		const auto [minDamage, maxDamage] = AttackWeaponDamage(player, SkillSlot::ForAttack(data.skill));
		base = (minDamage + maxDamage) / 2;
		base += base * player._pIBonusDam / 100 + player._pIBonusDamMod + player._pDamageMod;
	}
	int64_t raw = static_cast<int64_t>(std::max(base, 1)) * 64;
	raw = raw * CurvePercent(player, data.skill) / 100;
	raw = raw * data.damagePercent / 100;
	raw = raw * GrantedSpellDamagePercent(player, data) / 100;
	raw = raw * (100 + SoulHarvestDamagePercent(player)) / 100;
	return Fixed26_6::fromRaw(static_cast<int32_t>(std::clamp<int64_t>(raw, 64, INT32_MAX / 2)));
}

/** @brief Whether a table skill's hits count as a spell's or an attack's, for the rules that stop ailments. */
AilmentSource SourceOf(const TableSkillData &data)
{
	// As the supports tag them: a spell needs no weapon.
	return data.weapon == TableWeapon::None && (data.flags & TF_Spell) != 0 ? AilmentSource::Spell : AilmentSource::Strike;
}

void SpellHitMonster(const Player &player, Monster &monster, DamageType type, Fixed26_6 damage, AilmentSource source)
{
	if (&player != MyPlayer || !CanHit(monster) || damage <= 0)
		return;
	// The Sickness Road pact reaches table spells too: its melee bridge, and less against a healthy monster. So does
	// the Sigil of Power's +20%.
	if (source == AilmentSource::Spell)
		damage = Fixed26_6::fromRaw(static_cast<int32_t>(std::min<int64_t>(static_cast<int64_t>(damage.raw()) * VizjereiSpellHitPercent(player, monster) / 100 * SigilSpellDamagePercent(player) / 100, INT32_MAX / 2)));
	const Fixed26_6 dealt = Fixed26_6::fromRaw(static_cast<int32_t>(static_cast<int64_t>(damage.raw()) * MonsterDamageTakenPercent(monster, type) / 100));
	ApplyMonsterDamage(type, monster, dealt);
	monster.tag(player);
	TryInflictAilment(monster, player, type, dealt, source);
	if (monster.hasNoLife())
		M_StartKill(monster, player);
	else if (type == DamageType::Physical)
		M_StartHit(monster, player, dealt.raw());
}

void InflictTableBleed(const Player &player, Monster &monster, WeaponSkill skill, Fixed26_6 total, int ticks)
{
	Poe2MonsterState &state = monster.poe2;
	ticks = std::clamp(ticks, 1, 255);
	const int32_t perTick = std::max<int32_t>(total.raw() / ticks, 1);
	if (state.bleedTicks == 0 || perTick >= state.bleedDamagePerTick) {
		state.bleedDamagePerTick = perTick;
		state.bleedSource = static_cast<int8_t>(player.getId());
		state.bleedSkill = skill;
	}
	state.bleedTicks = static_cast<uint8_t>(std::max<int>(ticks, state.bleedTicks));
}

/** @brief Haunt's, Locust Swarm's and Decompose's chaos damage over time: the strongest counts. */
void InflictTableChaos(const Player &player, Monster &monster, WeaponSkill skill, Fixed26_6 total, int ticks)
{
	Poe2MonsterState &state = monster.poe2;
	ticks = std::clamp(ticks, 1, 255);
	const int32_t perTick = std::max<int32_t>(total.raw() / ticks, 1);
	if (state.chaosTicks == 0 || perTick >= state.chaosDamagePerTick) {
		state.chaosDamagePerTick = perTick;
		state.chaosSource = static_cast<int8_t>(player.getId());
		state.chaosSkill = skill;
	}
	state.chaosTicks = static_cast<uint8_t>(std::max<int>(ticks, state.chaosTicks));
}

struct CastResult {
	bool hit = false;
};

/**
 * @brief The tiles Dashing Strike's dash crossed, from where it started to where the hero landed; only where he stands
 * when he didn't dash (no start, or one further off than the dash goes).
 */
std::vector<Point> DashPath(const Player &player, int tiles)
{
	std::vector<Point> path;
	const Point end = player.position.tile;
	Point step = player.poe2.dashStart;
	if (step != Point {} && step.WalkingDistance(end) <= tiles + 1) {
		while (step != end) {
			path.push_back(step);
			step = step + GetDirection(step, end);
		}
	}
	path.push_back(end);
	return path;
}

/**
 * @brief Cyclone Strike drags what it hits a step in towards the hero, so it gathers a pack where Wave of Light only
 * hits it (Curtis 2026-10-28, "Cyclone pulls"). The nearest go first, so the inner ring makes room for the next.
 */
void PullTowards(Point center, std::vector<Monster *> monsters)
{
	std::stable_sort(monsters.begin(), monsters.end(), [center](const Monster *a, const Monster *b) {
		return center.WalkingDistance(a->position.tile) < center.WalkingDistance(b->position.tile);
	});
	for (Monster *monster : monsters) {
		if (monster->hasNoLife() || monster->mode == MonsterMode::Death || monster->mode == MonsterMode::Petrified || monster->isUnique())
			continue;
		const Point tile = monster->position.tile;
		if (center.WalkingDistance(tile) <= 1)
			continue;
		// A knockback from beyond the monster moves it towards the hero.
		M_GetKnockback(*monster, tile + (tile - center));
	}
}

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
		BreakMonsterArmour(monster, &player);
	if ((data.flags & TF_Bleed) != 0 && alive)
		InflictTableBleed(player, monster, data.skill, FixedDamage(player, data), 80);
	// Cobra Strike: POE2's poison, 20% of the hit a second for 2 seconds.
	if ((data.flags & TF_Poison) != 0 && alive)
		InflictPoison(player, monster, static_cast<int32_t>(static_cast<int64_t>(FixedDamage(player, data).raw()) * PoisonPercentPerSecond * PoisonTicks / 20 / 100), PoisonTicks);
	if (!alive)
		return;
	// Elemental attacks: part of the hit lands again as the skill's element.
	if (data.type != DamageType::Physical) {
		const Fixed26_6 extra = Fixed26_6::fromRaw(FixedDamage(player, data).raw() / 3);
		SpellHitMonster(player, monster, data.type, extra, SourceOf(data));
	}
	if ((data.flags & TF_Elements) != 0) {
		const Fixed26_6 extra = Fixed26_6::fromRaw(FixedDamage(player, data).raw() / 4);
		for (const DamageType type : { DamageType::Fire, DamageType::Lightning, DamageType::Cold })
			SpellHitMonster(player, monster, type, extra, SourceOf(data));
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

} // namespace

Point PredictMonsterTile(const Monster &monster, int ticks)
{
	if (IsNoneOf(monster.mode, MonsterMode::MoveNorthwards, MonsterMode::MoveSouthwards, MonsterMode::MoveSideways))
		return monster.position.tile;
	// A walking monster's tile is the one it left until the step ends; then it takes another step the same way each
	// walk, as long as the way is open.
	const int ticksPerFrame = std::max<int>(monster.animInfo.ticksPerFrame, 1);
	const int ticksPerTile = std::max(monster.animInfo.numberOfFrames * ticksPerFrame, 1);
	const int elapsed = (monster.animInfo.currentFrame * ticksPerFrame) + monster.animInfo.tickCounterOfCurrentFrame;
	const int remaining = std::max(ticksPerTile - elapsed, 1);
	if (ticks < remaining)
		return monster.position.tile;
	const int steps = std::min(1 + ((ticks - remaining) / ticksPerTile), MaxPredictedSteps);
	Point at = monster.position.future;
	const Displacement step { monster.var1, monster.var2 };
	for (int i = 1; i < steps; i++) {
		const Point next = at + step;
		if (!InDungeonBounds(next) || IsTileSolid(next))
			break;
		at = next;
	}
	return at;
}

namespace {

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

vfx::Element LookOf(const TableSkillData &data)
{
	return TableSkillLook(data);
}

// POE2 Mashup: the skills' sounds (see SkillSounds).
constexpr std::string_view BlizzardCastSound = "sfx\\poe2\\blizzard_cast.wav";
constexpr std::string_view GlacialSpikeSound = "sfx\\poe2\\glacial_spike.wav";
/** @brief An ice shard: Blizzard's four, played in turn so a run of them doesn't drone. */
constexpr std::string_view ShardSounds[] = { "sfx\\poe2\\blizzard_shard1.wav", "sfx\\poe2\\blizzard_shard2.wav", "sfx\\poe2\\blizzard_shard3.wav", "sfx\\poe2\\blizzard_shard4.wav" };
size_t NextShardSound = 0;
/** @brief Game ticks until each skill's next hit sound may play (HitSoundGapTicks), by WeaponSkill. */
std::array<uint8_t, 256> HitSoundCooldown {};
/** @brief The same for the Blood Star gem spell's bolts, which are no table skill's. */
uint8_t BloodStarHitCooldown = 0;

constexpr SkillSound Sfx(SfxID sfx)
{
	return SkillSound { sfx, {} };
}

constexpr SkillSound ModSound(std::string_view path)
{
	return SkillSound { SfxID::None, path };
}

bool IsBowSkill(const TableSkillData &data)
{
	return IsAnyOf(data.weapon, TableWeapon::Bow, TableWeapon::Crossbow);
}

/** @brief A spell's incantation, by its element: Diablo's cast2 for fire, cast4 for lightning, cast6 for the rest. */
SkillSound IncantationOf(const TableSkillData &data)
{
	if (data.shape == Buff)
		return Sfx(SfxID::CastHealing);
	switch (data.type) {
	case DamageType::Fire:
		return Sfx(SfxID::CastFire);
	case DamageType::Lightning:
		return Sfx(SfxID::CastLightning);
	default:
		return Sfx(SfxID::CastSkill);
	}
}

/** @brief A spell's bolt leaving the hero's hand, by its element and look. */
SkillSound BoltLaunchOf(const TableSkillData &data)
{
	switch (data.type) {
	case DamageType::Fire:
		return Sfx(SfxID::SpellFirebolt);
	case DamageType::Lightning:
		return Sfx(SfxID::SpellChargedBolt);
	case DamageType::Cold:
		// Cold: Ice Bolt's bright crystal zing (Curtis 2026-10-16: the Stone Curse thud was dull).
		return Sfx(SfxID::SpellHolyBolt);
	default:
		break;
	}
	switch (data.effect) {
	case MissileGraphicID::BloodHit:
		return Sfx(SfxID::SpellBloodStar);
	case MissileGraphicID::BoneHit:
	case MissileGraphicID::BoneSpirit:
		return Sfx(SfxID::SpellBoneSpirit);
	case MissileGraphicID::HolyBoltExplosion:
		return Sfx(SfxID::SpellHolyBolt);
	default:
		return Sfx(SfxID::SpellElemental);
	}
}

/** @brief What the skill's hits sound like where they land, by its element and look. */
SkillSound ImpactOf(const TableSkillData &data)
{
	// Cold shatters like Blizzard's shards.
	if (data.type == DamageType::Cold)
		return ModSound(ShardSounds[0]);
	switch (data.effect) {
	case MissileGraphicID::Lightning:
	case MissileGraphicID::ThinLightning:
	case MissileGraphicID::HolyBoltExplosion:
		// Holy Bolt lands with Diablo's lightning crack too.
		return Sfx(SfxID::SpellLightningHit);
	case MissileGraphicID::FireArrowExplosion:
	case MissileGraphicID::Inferno:
	case MissileGraphicID::MagmaBallExplosion:
	case MissileGraphicID::ApocalypseBoom:
		return Sfx(SfxID::SpellFireHit);
	case MissileGraphicID::StoneCurseShatter:
		return Sfx(SfxID::SpellStoneCurse);
	case MissileGraphicID::BloodHit:
		return Sfx(SfxID::SpellBloodStarHit);
	case MissileGraphicID::BoneHit:
	case MissileGraphicID::BoneSpirit:
		return Sfx(SfxID::SpellBoneSpiritHit);
	default:
		break;
	}
	switch (data.type) {
	case DamageType::Fire:
		return Sfx(SfxID::SpellFireHit);
	case DamageType::Lightning:
		return Sfx(SfxID::SpellLightningHit);
	default:
		// A spell's plain bolt lands with Blood Star's thud; a blade bites with the heavy swing.
		return Sfx((data.flags & TF_Spell) != 0 ? SfxID::SpellBloodStarHit : SfxID::Swing2);
	}
}

/** @brief Whether a zone's spell sound waits for its burst (see SkillSounds::spell). */
bool ZoneSoundsAtBurst(const TableSkillData &data)
{
	return data.shape == Zone && IsAnyOf(data.zone, TableZone::Delayed, TableZone::IceSpike, TableZone::ShardRing);
}

/**
 * @brief Where Glacial Spike erupts: under the monster it was cast at, where that monster will stand when the spike comes
 * up, rather than where it stood when the cast began (Curtis 2026-10-28: it came up behind moving enemies).
 */
Point IceSpikeCenter(const Player &player, const TableSkillData &data, Point target)
{
	const int id = player.poe2.castTargetMonster;
	if (player.poe2.castSkill != data.skill || id < 0 || static_cast<size_t>(id) >= MaxMonsters)
		return target;
	const Monster &monster = Monsters[id];
	// One that died, or that a trigger or repeat isn't aimed at, keeps the spike on the target.
	if (!CanHit(monster) || monster.position.tile.WalkingDistance(target) > IceSpikeTrackTiles)
		return target;
	// The zone bursts on its delay's last tick, after the one it's made on.
	return PredictMonsterTile(monster, data.delay + 1);
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
	zone.radius = static_cast<int8_t>(std::min<int>(data.radius, INT8_MAX));
	// A granted skill's zone (Solar Orb) is one at a time: a new one replaces the old.
	if (IsGrantedTableSkill(data))
		Zones.erase(std::remove_if(Zones.begin(), Zones.end(), [&](const SkillZone &other) { return other.skill == data.skill; }), Zones.end());
	// Traps and sentries: only a few of each can be out at once (MaxPlacedTraps, 2 more with Endless Horde, Curtis
	// 2026-10-29); Thunderstorm, Blessed Hammer and Blizzard one. The oldest goes.
	int limit = 0;
	if (IsAnyOf(data.zone, TableZone::Trap, TableZone::Turret))
		limit = SupportTrapLimit(player, SkillSlot::ForAttack(data.skill));
	else if (IsAnyOf(data.skill, WeaponSkill::Thunderstorm, WeaponSkill::BlessedHammer, WeaponSkill::Blizzard))
		limit = SupportZoneLimit(player, SkillSlot::ForAttack(data.skill), MaxStackingZones);
	if (limit > 0) {
		while (std::count_if(Zones.begin(), Zones.end(), [&](const SkillZone &other) { return other.skill == data.skill; }) >= limit)
			Zones.erase(std::find_if(Zones.begin(), Zones.end(), [&](const SkillZone &other) { return other.skill == data.skill; }));
	}
	Zones.push_back(zone);
	// A delayed blast draws its rune circle on the ground while it charges.
	if (data.zone == TableZone::Delayed)
		vfx::Sigil(center, std::max<int>(data.radius, 1), delay * gnTickDelay, LookOf(data));
	else if (data.zone == TableZone::IceSpike && (data.flags & TF_Pin) != 0)
		vfx::BoneCage(center, delay * gnTickDelay, std::max<int>(data.radius, 1), data.duration * gnTickDelay);
	else if (data.zone == TableZone::IceSpike)
		vfx::IceSpike(center, delay * gnTickDelay, std::max<int>(data.radius, 1));
}

/** @brief Bone Prison: a non-unique survivor of its hit is trapped (heavily stunned, its buildup spent) for the ticks given. */
void PinMonster(Monster &monster, int ticks)
{
	if (monster.hasNoLife() || monster.mode == MonsterMode::Death || monster.isUnique())
		return;
	monster.poe2.heavyStunTicks = static_cast<uint8_t>(std::clamp<int>(std::max<int>(ticks, monster.poe2.heavyStunTicks), 1, UINT8_MAX));
	monster.poe2.stunBuildup = 0;
}

/** @brief A burst of a zone or a landing: hits everything within the radius. */
/** @brief Heals the hero by a percent of maximum life and of maximum mana. */
void RestorePercent(const Player &hero, int lifePercent, int manaPercent)
{
	Player &player = Players[hero.getId()];
	if (player.hasNoLife())
		return;
	if (lifePercent > 0) {
		const Fixed26_6 life = Fixed26_6::fromRaw(static_cast<int32_t>(static_cast<int64_t>(player._pMaxHP.raw()) * lifePercent / 100));
		player._pHitPoints = std::min(player._pHitPoints + life, player._pMaxHP);
		player._pHPBase = std::min(player._pHPBase + life, player._pMaxHPBase);
		RedrawComponent(PanelDrawComponent::Health);
	}
	if (manaPercent > 0) {
		const Fixed26_6 mana = Fixed26_6::fromRaw(static_cast<int32_t>(static_cast<int64_t>(player._pMaxMana.raw()) * manaPercent / 100));
		player._pMana = std::min(player._pMana + mana, player._pMaxMana);
		player._pManaBase = std::min(player._pManaBase + mana, player._pMaxManaBase);
		RedrawComponent(PanelDrawComponent::Mana);
	}
}

/** @brief A granted skill's riders on a monster its hit (of this much) just killed. */
void OnGrantedKill(const Player &player, const Monster &monster, const TableSkillData &data, Fixed26_6 hit)
{
	switch (data.rider) {
	case GrantedRider::BurstOnKill: {
		// Volatile Bolt: the body bursts for half the hit around it.
		const Fixed26_6 burst = Fixed26_6::fromRaw(static_cast<int32_t>(static_cast<int64_t>(hit.raw()) * VolatileBurstPercent / 100));
		AddVisualEffect(monster.position.tile, data.effect, 0, 4);
		std::vector<Monster *> around;
		ForEachMonsterNear(monster.position.tile, 1, [&](Monster &other) {
			if (&other != &monster)
				around.push_back(&other);
		});
		for (Monster *other : around)
			SpellHitMonster(player, *other, data.type, burst, SourceOf(data));
		break;
	}
	case GrantedRider::KillRestoreLife:
		// Exsanguinate: only a bleeding enemy feeds the hero.
		if (monster.poe2.bleedTicks > 0)
			RestorePercent(player, ExsanguinateLifePercent, 0);
		break;
	case GrantedRider::KillRestoreBoth:
		RestorePercent(player, FeastRestorePercent, FeastRestorePercent);
		break;
	default:
		break;
	}
}

/** @brief One monster in a spell's area takes its hit (of this much), with the skill's stun, trap and bleed. */
void AreaSpellHit(const Player &player, const TableSkillData &data, Monster &monster, Fixed26_6 hit)
{
	SpellHitMonster(player, monster, data.type, hit, SourceOf(data));
	if (IsGrantedTableSkill(data) && monster.hasNoLife())
		OnGrantedKill(player, monster, data, hit);
	if (monster.hasNoLife())
		return;
	if ((data.flags & TF_Stun) != 0)
		HeavyStunMonster(monster);
	if ((data.flags & TF_Pin) != 0)
		PinMonster(monster, data.duration);
	// Blood Nova: the hit bleeds for GrantedBleedPercent of itself over 5 seconds, as the granted blood bolts do.
	if ((data.flags & TF_Bleed) != 0)
		InflictTableBleed(player, monster, data.skill, Fixed26_6::fromRaw(static_cast<int32_t>(static_cast<int64_t>(hit.raw()) * GrantedBleedPercent / 100)), GrantedBleedTicks);
}

/**
 * @brief A burst of a zone, a landing or an area spell: hits everything within the radius. A staff's granted skill priced
 * per target (TF_PerTarget) hits the monster nearest the aim in full and the others for GrantedExtraTargetPercent.
 */
void ZoneBurst(const Player &player, const TableSkillData &data, Point center, int radius, Fixed26_6 damage, std::optional<Point> aim = std::nullopt)
{
	AddVisualEffect(center, data.effect, 0, 4);
	BreakObjectsInArea(player, center, radius);
	const int extraPercent = GrantedExtraTargetPercent(data);
	const Monster *primary = nullptr;
	if (extraPercent < 100) {
		const Point from = aim.value_or(center);
		int nearest = std::numeric_limits<int>::max();
		ForEachMonsterNear(center, radius, [&](Monster &monster) {
			const Displacement offset = static_cast<Point>(monster.position.tile) - from;
			const int distance = (offset.deltaX * offset.deltaX) + (offset.deltaY * offset.deltaY);
			if (distance < nearest) {
				nearest = distance;
				primary = &monster;
			}
		});
	}
	const Fixed26_6 extra = Fixed26_6::fromRaw(std::max<int32_t>(static_cast<int32_t>(static_cast<int64_t>(damage.raw()) * extraPercent / 100), 1));
	ForEachMonsterNear(center, radius, [&](Monster &monster) {
		AreaSpellHit(player, data, monster, extraPercent < 100 && &monster != primary ? extra : damage);
	});
}

/**
 * @brief Plague Arrow: the arrow's first hit bursts in green, poisoning every enemy around it (POE2's poison, 20% of the
 * arrow's hit a second for 2 seconds) and hitting the others for half the arrow.
 */
void PoisonBurst(Player &player, const Monster &struck, const TableSkillData &data)
{
	const Point center = struck.position.tile;
	const int radius = std::clamp(SupportAreaRadius(player, SkillSlot::ForAttack(data.skill), data.radius), 0, 12);
	AddVisualEffect(center, data.effect, 0, 4);
	vfx::Impact(center, vfx::Element::Chaos, 1.0F);
	const Fixed26_6 base = FixedDamage(player, data);
	const int32_t poison = static_cast<int32_t>(std::min<int64_t>(static_cast<int64_t>(base.raw()) * PoisonPercentPerSecond * PoisonTicks / 20 / 100, INT32_MAX / 2));
	const Fixed26_6 splash = Fixed26_6::fromRaw(std::max<int32_t>(static_cast<int32_t>(static_cast<int64_t>(base.raw()) * PlagueArrowSplashPercent / 100), 1));
	std::vector<Monster *> targets;
	ForEachMonsterNear(center, radius, [&](Monster &monster) { targets.push_back(&monster); });
	for (Monster *monster : targets) {
		InflictPoison(player, *monster, poison, PoisonTicks);
		if (monster != &struck)
			SpellHitMonster(player, *monster, DamageType::Physical, splash, SourceOf(data));
	}
}

/** @brief A projectile's landing burst: a set share of a base hit (Freezing Arrow, Explosive Arrow), or two thirds of the projectile's hit. */
Fixed26_6 LandingBurstDamage(const Player &player, const TableSkillData &data)
{
	if (IsAnyOf(data.skill, WeaponSkill::FreezingArrow, WeaponSkill::ExplosiveBolt)) {
		TableSkillData burst = data;
		burst.damagePercent = static_cast<int16_t>(TableLandingBurstPercent(data));
		return FixedDamage(player, burst);
	}
	return Fixed26_6::fromRaw(FixedDamage(player, data).raw() * 2 / 3);
}

Missile *FireTableMissile(Player &player, const TableSkillData &data, Point from, Point target)
{
	const int gemLevel = GemLevel(player, data.skill);
	const bool spell = (data.flags & TF_Spell) != 0;
	const MissileID type = data.missile == MissileID::Null ? MissileID::Arrow : data.missile;
	// Weapon projectiles take their damage from the weapon when they hit, like arrows.
	const int damage = spell ? SpellBase(player, gemLevel) : ElementalArrowBaseDamage;
	// POE2 Mashup: silent as it leaves; the skill's launch sounds once a cast or shot (SkillSounds), not once a projectile.
	Missile *missile = AddMissile(from, target, GetDirection(from, target), type, TARGET_MONSTERS, player, damage, spell ? gemLevel : 0, nullptr, SfxID::None);
	if (missile == nullptr)
		return nullptr;
	SetMissileSkill(*missile, player, SkillSlot::ForAttack(data.skill));
	// A spell's bolt borrows a flare's look but keeps its own element, and a physical one looks like bone or blood.
	if (spell) {
		missile->poe2.damageType = static_cast<int8_t>(data.type);
		if (data.type == DamageType::Physical)
			missile->poe2.vfxElement = static_cast<int8_t>(LookOf(data));
	}
	// A per-target skill's projectiles hit each monster once a cast, the first in full (GrantedMissileTargetPercent).
	if ((data.flags & TF_PerTarget) != 0)
		missile->poe2.grantedCast = player.poe2.grantedState.castSerial;
	// Souls add to weapon projectiles through the weapon hit (see ResourceAttackDamagePercent).
	const int souls = spell ? SoulHarvestDamagePercent(player) : 0;
	missile->poe2.damagePercent = static_cast<int16_t>(std::min<int>(CurvePercent(player, data.skill) * data.damagePercent / 100 * GrantedSpellDamagePercent(player, data) / 100 * (100 + souls) / 100, INT16_MAX));
	if ((data.flags & TF_Pierce) != 0)
		missile->poe2.piercesLeft = static_cast<int8_t>(std::min<int>(missile->poe2.piercesLeft + (data.skill == WeaponSkill::BoneSpear ? BoneSpearPierces : 3), INT8_MAX));
	// Bone Spear is drawn as a great spear, not a bolt.
	if (data.skill == WeaponSkill::BoneSpear)
		missile->poe2.look = 2;
	if ((data.flags & TF_LandingBurst) != 0) {
		// The burst goes off about when the projectile gets there.
		const int ticks = std::clamp(from.WalkingDistance(target) * 2, 2, 30);
		SkillZone zone { data.skill, target, 1, static_cast<int16_t>(ticks), 0, LandingBurstDamage(player, data), static_cast<int8_t>(std::min<int>(data.radius, INT8_MAX)) };
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
	int novaExtra = 0;
	if (SupportFiresNova(player, SkillSlot::ForAttack(data.skill))) {
		count = std::max(count, 8);
		novaExtra = SupportNovaExtraProjectiles(player, SkillSlot::ForAttack(data.skill));
	}
	if (count >= 8) {
		for (int i = 0; i < count; i++) {
			const Direction dir = static_cast<Direction>(i % 8);
			FireTableMissile(player, data, tile, tile + Displacement { dir } * 8);
		}
		// Nova Projectiles II: more, between the others.
		for (int i = 0; i < novaExtra; i++) {
			const Direction dir = static_cast<Direction>(i % 8);
			FireTableMissile(player, data, tile, tile + Displacement { dir } * 8 + Displacement { Right(Right(dir)) } * 3);
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

constexpr uint16_t AuraCurseBit(TableCurse curse)
{
	return static_cast<uint16_t>(1U << static_cast<unsigned>(curse));
}

uint8_t *CurseTicks(Poe2MonsterState &state, TableCurse curse)
{
	switch (curse) {
	case TableCurse::Weaken:
		return &state.weakenTicks;
	case TableCurse::IronMaiden:
		return &state.ironMaidenTicks;
	case TableCurse::Decrepify:
		return &state.decrepifyTicks;
	case TableCurse::LowerResist:
		return &state.lowerResistTicks;
	case TableCurse::DeathMark:
		return &state.deathMarkTicks;
	case TableCurse::Despair:
		return &state.despairTicks;
	default:
		return nullptr;
	}
}

void ApplyCurse(const Player &player, Monster &monster, const TableSkillData &data)
{
	Poe2MonsterState &state = monster.poe2;
	const uint8_t ticks = static_cast<uint8_t>(std::min<int>(data.duration, 255));
	if (static_cast<size_t>(data.curse) < state.curseFullTicks.size())
		state.curseFullTicks[static_cast<size_t>(data.curse)] = ticks;
	// Cast by hand, the curse has its full strength.
	state.auraCurses &= static_cast<uint16_t>(~AuraCurseBit(data.curse));
	switch (data.curse) {
	case TableCurse::Weaken:
		state.weakenTicks = ticks;
		break;
	case TableCurse::IronMaiden:
		state.ironMaidenTicks = ticks;
		break;
	case TableCurse::Decrepify:
		state.decrepifyTicks = ticks;
		// Its chill is a curse's, which the Sickness Road can stop.
		if (AilmentAllowed(player, AilmentSource::Spell)) {
			state.chillTicks = ticks;
			state.chillPercent = static_cast<uint8_t>(std::max<int>(state.chillPercent, DecrepifyChillPercent));
		}
		break;
	case TableCurse::LowerResist:
		state.lowerResistTicks = ticks;
		break;
	case TableCurse::DeathMark:
		state.deathMarkTicks = ticks;
		break;
	case TableCurse::Despair:
		state.despairTicks = ticks;
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
	case TableCurse::Haunt:
		state.hauntTicks = ticks;
		InflictTableChaos(player, monster, data.skill, FixedDamage(player, data), ticks);
		break;
	case TableCurse::Locust:
		state.locustTicks = ticks;
		InflictTableChaos(player, monster, data.skill, FixedDamage(player, data), ticks);
		break;
	default:
		break;
	}
	monster.tag(player);
	AddVisualEffect(monster.position.tile, data.effect);
}

/** @brief The cast curse on the monster Hex Rupture can burst: at least half run out, the most run out first; None if none. */
TableCurse RupturableCurse(Poe2MonsterState &state)
{
	TableCurse best = TableCurse::None;
	int bestLeftPermille = 501;
	for (const TableCurse curse : { TableCurse::Weaken, TableCurse::IronMaiden, TableCurse::Decrepify, TableCurse::LowerResist, TableCurse::DeathMark, TableCurse::Despair }) {
		const uint8_t *left = CurseTicks(state, curse);
		// Blasphemy's curses are an aura's, not cast ones: they stay.
		if (left == nullptr || *left == 0 || (state.auraCurses & AuraCurseBit(curse)) != 0)
			continue;
		const int full = std::max<int>(state.curseFullTicks[static_cast<size_t>(curse)], *left);
		const int leftPermille = *left * 1000 / full;
		if (leftPermille < bestLeftPermille) {
			best = curse;
			bestLeftPermille = leftPermille;
		}
	}
	return best;
}

/** @brief Hex Rupture: each curse at least half run out near the target bursts in chaos on its monster and is gone. */
void RuptureHexes(const Player &player, const TableSkillData &data, Point target)
{
	std::vector<std::pair<Monster *, TableCurse>> hexes;
	ForEachMonsterNear(target, data.radius, [&](Monster &monster) {
		if (const TableCurse curse = RupturableCurse(monster.poe2); curse != TableCurse::None)
			hexes.emplace_back(&monster, curse);
	});
	// Nearest the target first, then in the monsters' order, the same on every machine.
	std::sort(hexes.begin(), hexes.end(), [&](const auto &a, const auto &b) {
		const int da = target.WalkingDistance(a.first->position.tile);
		const int db = target.WalkingDistance(b.first->position.tile);
		return da != db ? da < db : a.first->getId() < b.first->getId();
	});
	if (hexes.size() > static_cast<size_t>(HexRuptureMaxBursts))
		hexes.resize(HexRuptureMaxBursts);
	const Fixed26_6 damage = FixedDamage(player, data);
	for (const auto &[monster, curse] : hexes) {
		const Point at = monster->position.tile;
		vfx::Impact(at, vfx::Element::Chaos, 1.1F);
		ZoneBurst(player, data, at, HexRuptureBurstRadius, damage);
		// The curse goes with its burst: its icon leaves the banner.
		*CurseTicks(monster->poe2, curse) = 0;
	}
	if (!hexes.empty())
		PlaySkillSound(TableSkillSounds(data).spell, target);
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

int TableLandingBurstPercent(const TableSkillData &data)
{
	switch (data.skill) {
	case WeaponSkill::FreezingArrow:
		return FreezingArrowBurstPercent;
	case WeaponSkill::ExplosiveBolt:
		return ExplosiveBoltBurstPercent;
	default:
		return data.damagePercent * 2 / 3;
	}
}

void ApplyTableCurse(const Player &player, Monster &monster, WeaponSkill skill, int durationPercent)
{
	const TableSkillData *data = FindTableSkill(skill);
	if (data == nullptr || data->curse == TableCurse::None)
		return;
	TableSkillData scaled = *data;
	scaled.duration = static_cast<uint16_t>(std::max(data->duration * durationPercent / 100, 1));
	ApplyCurse(player, monster, scaled);
}

void ApplyAuraCurse(const Player &player, Monster &monster, WeaponSkill skill, int strengthPercent, int ticks)
{
	const TableSkillData *data = FindTableSkill(skill);
	if (data == nullptr)
		return;
	Poe2MonsterState &state = monster.poe2;
	uint8_t *left = CurseTicks(state, data->curse);
	const uint16_t bit = AuraCurseBit(data->curse);
	if (left == nullptr || (*left > ticks && (state.auraCurses & bit) == 0))
		return;
	const bool fresh = *left == 0;
	*left = static_cast<uint8_t>(std::clamp(ticks, 1, 255));
	state.auraCurses |= bit;
	state.auraCurseStrength = static_cast<uint8_t>(std::clamp(strengthPercent, 1, 100));
	if (data->curse == TableCurse::Decrepify && AilmentAllowed(player, AilmentSource::Spell)) {
		state.chillTicks = std::max(state.chillTicks, *left);
		state.chillPercent = static_cast<uint8_t>(std::max<int>(state.chillPercent, DecrepifyChillPercent * state.auraCurseStrength / 100));
	}
	monster.tag(player);
	if (fresh)
		AddVisualEffect(monster.position.tile, data->effect);
}

int TableCurseStrength(const Monster &monster, TableCurse curse)
{
	return (monster.poe2.auraCurses & AuraCurseBit(curse)) != 0 ? monster.poe2.auraCurseStrength : 100;
}

int SevenSidedStrikeSharePercent(int targets, int burst)
{
	const int full = std::clamp(std::min(SevenSidedStrikeFullTargets, burst), 1, 100);
	return std::clamp(targets, 0, full) * 100 / full;
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
	return IsAnyOf(data.shape, Around, Burst, Dash, Line, Zone, Curse, CorpseBurst, CallOfTheAncients, HexBurst) || (data.flags & (TF_LandingBurst | TF_PoisonBurst)) != 0;
}

bool StartTableSkill(Player &player, const TableSkillData &data)
{
	// Checks that need the world; the caller pays for and starts the skill.
	// Sacrifice: the hero's skeletons count as corpses.
	if (data.shape == CorpseBurst && !CorpseNear(SkillTargetTile(player), data.radius) && SacrificeMinionNear(player, SkillTargetTile(player), data.radius) == nullptr) {
		EventPlrMsg(_("Corpse Explosion needs a corpse near the target."), UiFlags::ColorRed);
		return false;
	}
	if (data.shape == SoulHarvest && !CorpseNear(player.position.tile, data.radius) && SacrificeMinionNear(player, player.position.tile, data.radius) == nullptr) {
		EventPlrMsg(_("Soul Harvest needs corpses nearby."), UiFlags::ColorRed);
		return false;
	}
	if ((data.flags & TF_LifeCost) != 0) {
		const Fixed26_6 cost = Fixed26_6::fromRaw(player._pHitPoints.raw() / 20);
		if (player._pHitPoints - cost < Fixed26_6::fromInt(1))
			return false;
		// POE2 Mashup: a dodge roll before the skill goes off gives this back (Curtis 2026-10-29).
		const ActionPaymentScope price(player);
		player._pHitPoints -= cost;
		player._pHPBase -= cost;
	}
	return true;
}

void ExecuteTableSkill(Player &player, const TableSkillData &unsupported, Point target)
{
	const SkillLookScope skillLook(unsupported.skill);
	// The supports change the skill's area and duration, and its hits are the skill's hits.
	const SkillSlot slot = SkillSlot::ForAttack(unsupported.skill);
	TableSkillData data = unsupported;
	data.radius = static_cast<uint8_t>(std::clamp(SupportAreaRadius(player, slot, unsupported.radius), 0, 12));
	if (unsupported.duration > 0)
		data.duration = static_cast<uint16_t>(std::clamp(SupportDurationTicks(player, slot, unsupported.duration), 1, static_cast<int>(UINT16_MAX)));
	const SupportHitScope scope(player, slot);
	// Meteor, Thunderstorm and Frozen Orb eat an element infusion for a much bigger cast that adds its element.
	if (const uint8_t infusion = TakeInfusion(player, slot); infusion != 0) {
		data.damagePercent = static_cast<int16_t>(std::min(data.damagePercent * (100 + InfusionDamagePercent) / 100, static_cast<int>(INT16_MAX)));
		SkillBurst(player, target, std::max<int>(data.radius, 2), InfusionDamageType(infusion), Fixed26_6::fromRaw(FixedDamage(player, data).raw() / 2), MissileGraphicID::ApocalypseBoom);
	}
	// Chiming Bolt: every fourth cast leaves a Sigil of Power under the hero.
	if (data.rider == GrantedRider::SigilEveryFourth) {
		GrantedRuntime &state = player.poe2.grantedState;
		if (++state.chimingCasts >= SigilEvery) {
			state.chimingCasts = 0;
			state.sigilTicks = static_cast<uint8_t>(SigilTicks);
			state.sigilX = static_cast<uint8_t>(player.position.tile.x);
			state.sigilY = static_cast<uint8_t>(player.position.tile.y);
			vfx::Sigil(player.position.tile, SigilRadius, SigilTicks * gnTickDelay, LookOf(data));
		}
	}
	// Critical Bolt: every fifth cast hits for double.
	if (data.rider == GrantedRider::EveryFifthDouble) {
		uint8_t &casts = player.poe2.grantedState.criticalCasts;
		if (++casts >= CriticalBoltEvery) {
			casts = 0;
			data.damagePercent = static_cast<int16_t>(std::min(data.damagePercent * 2, static_cast<int>(INT16_MAX)));
		}
	}
	// The cast effects were played by PlaySkillEffects just before.
	CastResult result;
	const Point tile = player.position.tile;
	// A per-hit skill (POE2 Mashup) lands one hit of its burst a use, each for PerHitDamagePercent of a hit.
	const int perHit = PerHitBurst(data.skill) > 0 ? PerHitDamagePercent(player, data.skill) : 0;
	switch (data.shape) {
	case Strike:
		if (Monster *monster = MonsterInFrontOf(player); monster != nullptr)
			WeaponHit(player, *monster, data, data.damagePercent, result);
		else
			BreakObjectAt(player, tile + player._pdir);
		break;
	case Arc:
		for (const Direction dir : { player._pdir, Left(player._pdir), Right(player._pdir) }) {
			// The skills smash the barrels and urns in their area too (Curtis 2026-10-22).
			BreakObjectAt(player, tile + dir);
			Monster *monster = FindMonsterAtPosition(tile + dir);
			if (monster != nullptr && CanHit(*monster) && monster->position.old == tile + dir)
				WeaponHit(player, *monster, data, data.damagePercent, result);
		}
		break;
	case Around:
		if ((data.flags & TF_Spell) != 0) {
			// A nova around the hero (the caster weapons' granted skills): it hits like a burst, with the spell base.
			EffectRingAround(tile, std::max<int>(data.radius, 1), data.effect, 0);
			ZoneBurst(player, data, tile, std::max<int>(data.radius, 1), FixedDamage(player, data), target);
			// Consecrated Ground: then 1% of maximum life a second for 4 seconds, started over by each cast.
			if (data.rider == GrantedRider::RegenBuff) {
				GrantedRuntime &state = player.poe2.grantedState;
				state.regenTicks = static_cast<uint8_t>(ConsecrateRegenTicks);
				state.regenRaw = static_cast<int32_t>(static_cast<int64_t>(player._pMaxHP.raw()) * ConsecrateRegenPercentPerSecond * ConsecrateRegenTicks / 20 / 100);
			}
			break;
		}
		[[fallthrough]];
	case Dash: {
		const int radius = std::max<int>(data.radius, 1);
		std::vector<Monster *> targets;
		// Dashing Strike hits each monster along its dash once, not only those where it lands (Curtis 2026-10-28).
		const std::vector<Point> centers = data.skill == WeaponSkill::DashingStrike ? DashPath(player, data.count) : std::vector<Point> { tile };
		if (data.skill == WeaponSkill::DashingStrike)
			player.poe2.dashStart = {};
		for (const Point center : centers) {
			BreakObjectsInArea(player, center, radius);
			ForEachMonsterNear(center, radius, [&](Monster &monster) {
				if (std::find(targets.begin(), targets.end(), &monster) == targets.end())
					targets.push_back(&monster);
			});
		}
		std::vector<Monster *> hits;
		for (Monster *monster : targets) {
			CastResult one;
			WeaponHit(player, *monster, data, data.damagePercent, one);
			if (one.hit) {
				result.hit = true;
				hits.push_back(monster);
			}
		}
		if (data.skill == WeaponSkill::CycloneStrike)
			PullTowards(tile, std::move(hits));
		break;
	}
	case Flurry:
		BreakObjectAt(player, tile + player._pdir);
		for (int i = 0; i < data.count; i++) {
			Monster *monster = MonsterInFrontOf(player);
			if (monster == nullptr)
				break;
			if (perHit > 0) {
				WeaponHit(player, *monster, data, data.damagePercent * perHit / 100, result);
				break;
			}
			WeaponHit(player, *monster, data, data.damagePercent, result);
		}
		break;
	case Projectiles: {
		int count = data.count + GemExtraProjectiles(SkillSlot::ForAttack(data.skill), player.poe2.effectiveLevel(data.skill)) + SupportExtraProjectileCount(player, slot);
		if ((data.flags & TF_Frenzy) != 0)
			count += player.poe2.frenzyCharges;
		if ((data.flags & TF_PerTarget) != 0) {
			uint16_t &serial = player.poe2.grantedState.castSerial;
			serial = static_cast<uint16_t>(serial == UINT16_MAX ? 1 : serial + 1);
		}
		FireFanOf(player, data, target, count);
		break;
	}
	case Stream:
		if (perHit > 0) {
			TableSkillData shot = data;
			shot.damagePercent = static_cast<int16_t>(std::min(data.damagePercent * perHit / 100, static_cast<int>(INT16_MAX)));
			FireTableShot(player, shot, target, TakePerHitIndex(player));
			break;
		}
		StartSkillVolley(player, data.skill, target, data.count + GemExtraProjectiles(SkillSlot::ForAttack(data.skill), player.poe2.effectiveLevel(data.skill)) + SupportExtraProjectileCount(player, slot));
		break;
	case Burst:
		if ((data.flags & TF_Spell) != 0) {
			const Fixed26_6 damage = FixedDamage(player, data);
			ZoneBurst(player, data, target, data.radius, damage);
			// Echoing Blast goes off again where it landed, a moment later and weaker.
			if (data.rider == GrantedRider::Echo) {
				const Fixed26_6 echo = Fixed26_6::fromRaw(static_cast<int32_t>(static_cast<int64_t>(damage.raw()) * EchoPercent / 100));
				Zones.push_back({ data.skill, target, 1, static_cast<int16_t>(EchoDelayTicks), 0, echo, static_cast<int8_t>(std::min<int>(data.radius, INT8_MAX)) });
			}
		} else {
			BreakObjectsInArea(player, target, data.radius);
			std::vector<Monster *> targets;
			ForEachMonsterNear(target, data.radius, [&](Monster &monster) { targets.push_back(&monster); });
			for (Monster *monster : targets)
				WeaponHit(player, *monster, data, data.damagePercent, result);
		}
		// Bolts spray out from where it lands, each for a share of a base hit (Fist of the Heavens).
		if (data.missile != MissileID::Null) {
			TableSkillData bolt = data;
			bolt.damagePercent = static_cast<int16_t>(BurstBoltDamagePercent);
			for (int i = 0; i < data.count; i++) {
				const Displacement out { static_cast<Direction>(i * 8 / std::max<int>(data.count, 1) % 8) };
				FireTableMissile(player, bolt, target, target + out * 6);
			}
		}
		break;
	case Line: {
		const Direction dir = GetDirection(tile, target);
		std::vector<Monster *> hit;
		Point end = tile;
		for (int step = 1; step <= data.count; step++) {
			const Point at = tile + Displacement { dir } * step;
			if (!InDungeonBounds(at) || IsTileSolid(at))
				break;
			end = at;
			AddVisualEffect(at, data.effect, step * 2, step == 1 ? 4 : 0);
			BreakObjectsInArea(player, at, data.radius);
			ForEachMonsterNear(at, data.radius, [&](Monster &monster) {
				if (std::find(hit.begin(), hit.end(), &monster) == hit.end())
					hit.push_back(&monster);
			});
		}
		if (end != tile)
			vfx::Fissure(tile, end, LookOf(data));
		if ((data.flags & TF_Spell) != 0) {
			// Bone Spikes: a clump of bone bursts up on each step as the line rolls out, and each enemy on it takes the spell's hit once.
			for (int step = 1; step <= tile.WalkingDistance(end); step++)
				vfx::BoneSpikes(tile + Displacement { dir } * step, step * 2 * gnTickDelay, 0.55F + 0.05F * static_cast<float>(step));
			if (end != tile)
				ShakeCamera(ShakeStrength::Light, 4);
			const Fixed26_6 damage = FixedDamage(player, data);
			for (Monster *monster : hit) {
				AreaSpellHit(player, data, *monster, damage);
				result.hit = true;
			}
			break;
		}
		for (Monster *monster : hit)
			WeaponHit(player, *monster, data, data.damagePercent, result);
		break;
	}
	case Zone:
		if (data.zone == TableZone::IceSpike)
			target = IceSpikeCenter(player, data, target);
		// POE2 Mashup: a trap or sentry goes where the hero can see, never past a wall or a closed door (Curtis 2026-10-29).
		if (IsAnyOf(data.zone, TableZone::Trap, TableZone::Turret) && (data.flags & TF_AtPlayer) == 0)
			target = LastTileInSight(tile, target);
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
			// Stun only (Curtis 2026-10-23): it no longer empowers melee hits, which took Battle Cry's value (+50% with no
			// Battle Cry socketed) and shared Battle Cry's timer.
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
		// Per hit: one strike a use, at the nearest enemies in turn. With fewer of them than SevenSidedStrikeFullTargets,
		// each strike deals their share of that many, so a lone monster takes a third of each (Curtis 2026-10-27, "Raise
		// each strike"; it was a seventh, the whole burst's share).
		if (perHit > 0 && !targets.empty()) {
			const int count = static_cast<int>(targets.size());
			const int index = TakePerHitIndex(player);
			Monster &monster = *targets[index % count];
			AddVisualEffect(monster.position.tile, MissileGraphicID::Etherealize, 0);
			WeaponHit(player, monster, data, data.damagePercent * perHit / 100 * SevenSidedStrikeSharePercent(count, data.count) / 100, result);
			// A full set of strikes builds the combo one use built.
			if ((index + 1) % std::max<int>(data.count, 1) != 0)
				data.flags = static_cast<uint32_t>(data.flags & ~TF_Combo);
			break;
		}
		int delay = 0;
		for (Monster *monster : targets) {
			AddVisualEffect(monster->position.tile, MissileGraphicID::Etherealize, delay);
			WeaponHit(player, *monster, data, data.damagePercent, result);
			delay += 2;
		}
		break;
	}
	case HexBurst:
		RuptureHexes(player, data, target);
		break;
	case CorpseBurst:
		if (const std::optional<Point> corpse = CorpseNear(target, data.radius); corpse) {
			dCorpse[corpse->x][corpse->y] = 0;
			ZoneBurst(player, data, *corpse, data.radius, FixedDamage(player, data));
			PlaySkillSound(TableSkillSounds(data).spell, *corpse);
		} else if (Monster *skeleton = SacrificeMinionNear(player, target, data.radius); skeleton != nullptr) {
			const Point at = skeleton->position.tile;
			SacrificeMinion(*skeleton);
			ZoneBurst(player, data, at, data.radius, FixedDamage(player, data));
			PlaySkillSound(TableSkillSounds(data).spell, at);
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
		// Sacrifice: the hero's skeletons give their souls too, once the corpses run out.
		while (souls < MaxSouls) {
			Monster *skeleton = SacrificeMinionNear(player, tile, data.radius);
			if (skeleton == nullptr)
				break;
			AddVisualEffect(skeleton->position.tile, MissileGraphicID::Resurrect, souls);
			SacrificeMinion(*skeleton);
			souls++;
		}
		player.poe2.soulStacks = static_cast<uint8_t>(std::min(player.poe2.soulStacks + souls, MaxSouls));
		player.poe2.soulTicks = data.duration;
		break;
	}
	case CallOfTheAncients: {
		const int rage = player.poe2.rage;
		player.poe2.rage = 0;
		BreakObjectsInArea(player, tile, data.radius);
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

int GrantedExtraTargetPercent(const TableSkillData &data)
{
	if ((data.flags & TF_PerTarget) == 0 || data.damagePercent <= 0)
		return 100;
	// What one hit may deal each extra target: ExtraTargetPercentPerSecond of the spell base for each second of the cast,
	// or of the pulse for a zone, its bleed and echo included.
	int windowMs = 0;
	if (data.shape == TableShape::Zone) {
		windowMs = data.interval * 50;
	} else if (const SkillSpeedRow *speed = FindSkillSpeedRow(SkillSlot::ForAttack(data.skill)); speed != nullptr) {
		windowMs = speed->castMs + speed->addedMs;
	}
	if (windowMs <= 0)
		windowMs = 1000;
	int rider = 100;
	if ((data.flags & TF_Bleed) != 0)
		rider += GrantedBleedPercent;
	if (data.rider == GrantedRider::Echo)
		rider += EchoPercent;
	const int64_t cap = static_cast<int64_t>(ExtraTargetPercentPerSecond) * windowMs * 100 * 100 / 1000;
	return static_cast<int>(std::clamp<int64_t>(cap / (static_cast<int64_t>(data.damagePercent) * rider), 1, 100));
}

bool GrantedMissileSkipsMonster(const Missile &missile, const Monster &monster)
{
	return gbIsPoe2 && missile.poe2.grantedCast != 0 && monster.poe2.grantedCastHit == missile.poe2.grantedCast;
}

int GrantedMissileTargetPercent(const Missile &missile, Player &player, Monster &monster)
{
	const uint16_t serial = missile.poe2.grantedCast;
	if (!gbIsPoe2 || serial == 0)
		return 100;
	const SkillSlot skill = MissileSkill(missile);
	const TableSkillData *data = skill.kind == SkillKind::Attack ? FindTableSkill(static_cast<WeaponSkill>(skill.id)) : nullptr;
	if (data == nullptr)
		return 100;
	monster.poe2.grantedCastHit = serial;
	// The cast's first monster takes the hit in full; the casts in flight each keep their own first.
	GrantedRuntime &state = player.poe2.grantedState;
	const size_t slot = serial % state.castFirst.size();
	const auto id = static_cast<int16_t>(monster.getId());
	if (state.castFirstSerial[slot] != serial) {
		state.castFirstSerial[slot] = serial;
		state.castFirst[slot] = id;
		return 100;
	}
	return state.castFirst[slot] == id ? 100 : GrantedExtraTargetPercent(*data);
}

GrantedRider HitGrantedRider()
{
	const SkillSlot skill = SupportHitSkill();
	if (skill.kind != SkillKind::Attack)
		return GrantedRider::None;
	const TableSkillData *data = FindTableSkill(static_cast<WeaponSkill>(skill.id));
	return data != nullptr ? data->rider : GrantedRider::None;
}

void OnTableMissileHit(const Missile &missile, Player &player, Monster &monster, int damage, bool isDamageShifted)
{
	if (!gbIsPoe2)
		return;
	const SkillSlot skill = MissileSkill(missile);
	if (skill.kind != SkillKind::Attack)
		return;
	const TableSkillData *data = FindTableSkill(static_cast<WeaponSkill>(skill.id));
	if (data != nullptr && (data->flags & TF_PoisonBurst) != 0)
		PoisonBurst(player, monster, *data);
	// Bone Spear: each enemy it runs through bursts in splinters as it flies on.
	if (data != nullptr && !IsGrantedTableSkill(*data) && (data->flags & TF_Spell) != 0 && (data->flags & TF_Pierce) != 0) {
		AddVisualEffect(monster.position.tile, data->effect);
		vfx::Impact(monster.position.tile, LookOf(*data), 1.1F);
	}
	if (data == nullptr || !IsGrantedTableSkill(*data))
		return;
	const int64_t raw = isDamageShifted ? damage : static_cast<int64_t>(damage) * 64;
	const Fixed26_6 hit = Fixed26_6::fromRaw(static_cast<int32_t>(std::clamp<int64_t>(raw, 1, INT32_MAX / 2)));
	const auto share = [&hit](int percent) { return Fixed26_6::fromRaw(static_cast<int32_t>(static_cast<int64_t>(hit.raw()) * percent / 100)); };
	if (monster.hasNoLife() || monster.mode == MonsterMode::Death) {
		OnGrantedKill(player, monster, *data, hit);
		return;
	}
	// Soulrend and Exsanguinate: the hit bleeds for GrantedBleedPercent of itself over 5 seconds.
	if ((data->flags & TF_Bleed) != 0)
		InflictTableBleed(player, monster, data->skill, share(GrantedBleedPercent), GrantedBleedTicks);
	Poe2MonsterState &state = monster.poe2;
	switch (data->rider) {
	case GrantedRider::Wither:
		// A stack a hit up to the rider's cap; stacks from elsewhere (Withering Touch) stay.
		if (state.witherStacks < GrantedWitherMaxStacks)
			state.witherStacks++;
		state.witherTicks = std::max<uint8_t>(state.witherTicks, GrantedWitherTicks);
		break;
	case GrantedRider::ManaDrain:
		if (state.manaDrainTicks == 0) {
			state.manaDrainTicks = ManaDrainCooldownTicks;
			const Fixed26_6 mana = Fixed26_6::fromInt(ManaDrainMana(player.poe2.effectiveLevel(data->skill)));
			player._pMana = std::min(player._pMana + mana, player._pMaxMana);
			player._pManaBase = std::min(player._pManaBase + mana, player._pMaxManaBase);
			RedrawComponent(PanelDrawComponent::Mana);
		}
		// Its slow is a chill's, as Decrepify's is.
		state.chillTicks = std::max<uint8_t>(state.chillTicks, ManaDrainSlowTicks);
		state.chillPercent = static_cast<uint8_t>(std::max<int>(state.chillPercent, ManaDrainSlowPercent));
		break;
	case GrantedRider::CullFrenzy: {
		if (monster.type().type == MT_DIABLO || monster.isPlayerMinion())
			break;
		const int cull = monster.isUnique() ? PowerSiphonUniqueCullPercent : PowerSiphonCullPercent;
		if (static_cast<int64_t>(monster.hitPoints.raw()) * 100 > static_cast<int64_t>(monster.maxHitPoints.raw()) * cull)
			break;
		AddVisualEffect(monster.position.tile, MissileGraphicID::BloodHit);
		// The Butcher's Road reads it when the monster dies: a Cull throws the hook.
		state.culled = true;
		ApplyMonsterDamage(DamageType::Physical, monster, monster.hitPoints);
		GainFrenzyCharge(player);
		break;
	}
	case GrantedRider::ChainOnce: {
		// Galvanic Bolt: the nearest other enemy within reach takes half the hit.
		Monster *next = nullptr;
		int best = GalvanicChainTiles + 1;
		ForEachMonsterNear(monster.position.tile, GalvanicChainTiles, [&](Monster &other) {
			const int distance = monster.position.tile.WalkingDistance(other.position.tile);
			if (&other != &monster && distance < best) {
				best = distance;
				next = &other;
			}
		});
		if (next != nullptr) {
			AddVisualEffect(next->position.tile, MissileGraphicID::ThinLightning, 0, 4);
			SpellHitMonster(player, *next, data->type, share(GalvanicChainPercent), SourceOf(*data));
		}
		break;
	}
	case GrantedRider::Plague:
		// Decompose: the hit again as chaos over 4 seconds, passed on when the monster dies (OnTableCursedMonsterKilled).
		InflictTableChaos(player, monster, data->skill, hit, PlagueTicks);
		state.plagueTicks = static_cast<uint8_t>(PlagueTicks);
		monster.tag(player);
		break;
	case GrantedRider::Weaken:
		// Cast by hand, at full strength, as the Weaken gem's curse.
		state.auraCurses &= static_cast<uint16_t>(~AuraCurseBit(TableCurse::Weaken));
		state.weakenTicks = std::max<uint8_t>(state.weakenTicks, TormentWeakenTicks);
		monster.tag(player);
		break;
	default:
		break;
	}
}

void FireTableShot(Player &player, const TableSkillData &data, Point target, int index)
{
	const SkillLookScope skillLook(data.skill);
	const Point tile = player.position.tile;
	const Direction dir = GetDirection(tile, target);
	constexpr int Wobble[] = { 0, 1, -1, 0, -1, 1 };
	FireTableMissile(player, data, tile, Along(tile, target, 8) + Displacement { Left(Left(dir)) } * Wobble[index % 6]);
	// POE2 Mashup: each shot of a stream sounds as it leaves (see SkillSounds::spell).
	PlaySkillSound(TableSkillSounds(data).spell, tile);
}

void PlayTableSkillEffects(const Player &player, const TableSkillData &data, Point target)
{
	const SkillLookScope skillLook(data.skill);
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
	case DamageType::Cold:
		flash = MissileGraphicID::StoneCurseShatter;
		break;
	default:
		// A physical spell flashes bone or blood (Blessed Hammer holy); curses keep their pale flash.
		if ((data.flags & TF_Spell) != 0 && data.shape != Curse) {
			switch (LookOf(data)) {
			case vfx::Element::Blood:
				flash = MissileGraphicID::BloodHit;
				break;
			case vfx::Element::Bone:
				flash = MissileGraphicID::BoneHit;
				break;
			default:
				flash = MissileGraphicID::HolyBoltExplosion;
				break;
			}
		} else if ((data.flags & TF_Spell) != 0 || data.shape == Curse) {
			flash = MissileGraphicID::HolyBoltExplosion;
		}
		break;
	}
	AddVisualEffect(tile + dir, flash, 0, 3);
	// POE2 Mashup: every skill is heard. An attack's swing, throw or shot sounds as it lands (a spell's incantation
	// began with its animation, see BeginSkillAnimation), then the skill going off: a Stream's with each shot
	// (FireTableShot), a corpse's blast only if it finds one, a zone that bursts later as it bursts.
	const SkillSounds sounds = TableSkillSounds(data);
	if (data.graphic == 0)
		PlaySkillSound(sounds.cast, tile);
	if (IsNoneOf(data.shape, Stream, CorpseBurst) && !ZoneSoundsAtBurst(data)) {
		const bool atTarget = IsAnyOf(data.shape, Burst, Curse) || (data.shape == Zone && (data.flags & TF_AtPlayer) == 0);
		PlaySkillSound(sounds.spell, atTarget ? target : tile);
	}
	switch (data.shape) {
	case Strike:
	case Flurry:
		AddVisualEffect(tile + dir, data.effect, 1, 4);
		vfx::SwingAs(tile, tile + dir, LookOf(data), WeaponSwingStyle(player), HoldsHeavyWeapon(player), WeaponElementLook(player));
		break;
	case Arc:
		if (WeaponSwingStyle(player) == vfx::SwingStyle::Chop)
			vfx::Cleave(tile, tile + dir, LookOf(data), true, WeaponElementLook(player));
		else
			vfx::Swing(tile, tile + dir, LookOf(data), true, false, WeaponElementLook(player));
		// The hits follow the blade across, from the hero's right to his left.
		AddVisualEffect(tile + Right(dir), data.effect, 0);
		AddVisualEffect(tile + dir, data.effect, 1, 4);
		AddVisualEffect(tile + Left(dir), data.effect, 2);
		break;
	case Dash:
		// Whirlwind's spin and its funnel of wind started with its animation (see BeginSkillAnimation); here its blades
		// bite into everything around, without the slam's glowing cracks that read as lightning (Curtis 2026-10-21).
		if (SpinsWhileStriking(data.skill)) {
			EffectRingAround(tile, 1, data.effect, 0);
			vfx::Sparks(tile, LookOf(data), 1.3F);
			if (mine)
				ShakeCamera(ShakeStrength::Light, 4);
			break;
		}
		[[fallthrough]];
	case Around:
	case CallOfTheAncients:
		EffectRingAround(tile, 1, data.effect, 0);
		if (data.radius > 1)
			EffectRingAround(tile, data.radius, data.effect, 3);
		if ((data.flags & TF_Spell) != 0 && data.type == DamageType::Physical) {
			// A physical nova has weight: blood rolls out in a wave, bone bursts up in a ring, and the screen shakes.
			const vfx::Element look = LookOf(data);
			const int radius = std::max<int>(data.radius, 1);
			vfx::Impact(tile, look, 1.5F);
			if (look == vfx::Element::Blood) {
				vfx::Nova(tile, radius, look);
			} else {
				vfx::Slam(tile, radius, look);
				for (int i = 0; i < 8; i++)
					vfx::BoneSpikes(tile + Displacement { static_cast<Direction>(i) } * radius, 40 + 15 * i, 0.5F);
			}
			if (mine)
				ShakeCamera(ShakeStrength::Medium, 5);
			break;
		}
		if ((data.flags & TF_Spell) != 0)
			vfx::Impact(tile, LookOf(data), 1.2F);
		else
			vfx::Slam(tile, std::max<int>(data.radius, 1), LookOf(data));
		if (mine)
			ShakeCamera(ShakeStrength::Light, 4);
		break;
	case Burst:
		AddVisualEffect(target, data.effect, 0, 6);
		if (data.radius > 0)
			EffectRingAround(target, data.radius, data.effect, 2);
		if ((data.flags & TF_Spell) == 0)
			vfx::Slam(target, std::max<int>(data.radius, 1), LookOf(data));
		else if (data.type == DamageType::Lightning)
			vfx::SkyStrike(target, LookOf(data));
		if (mine)
			ShakeCamera(ShakeStrength::Medium, 5);
		break;
	case Curse:
		EffectRingAround(target, std::max<int>(data.radius, 1), data.effect, 0);
		break;
	case HexBurst:
		// A dark sigil over the area; the bursts come from the curses themselves.
		vfx::Sigil(target, std::max<int>(data.radius, 1), 450, vfx::Element::Chaos);
		break;
	case Buff:
		EffectRingAround(tile, 1, data.effect, 0);
		break;
	case SoulHarvest:
		EffectRingAround(tile, 2, MissileGraphicID::BloodHit, 0);
		break;
	default:
		break;
	}
}

vfx::Element TableSkillLook(const TableSkillData &data)
{
	const bool spell = (data.flags & TF_Spell) != 0;
	if (spell && data.type == DamageType::Physical) {
		if (data.effect == MissileGraphicID::HolyBoltExplosion)
			return vfx::Element::Holy;
		if (data.effect == MissileGraphicID::BloodHit)
			return vfx::Element::Blood;
	}
	return vfx::ElementOf(data.type, spell);
}

SkillSounds TableSkillSounds(const TableSkillData &data)
{
	SkillSounds sounds;
	const bool spell = (data.flags & TF_Spell) != 0;
	const bool bow = IsBowSkill(data);
	// The cast: a spell's incantation, or the hero's swing, throw or shot.
	if (data.graphic != 0)
		sounds.cast = IncantationOf(data);
	else if (bow && (IsAnyOf(data.shape, Projectiles, Stream) || data.zone == TableZone::RandomStrikes))
		sounds.cast = Sfx(SfxID::ShootBow2);
	else if (data.weapon == TableWeapon::Shield)
		sounds.cast = Sfx(SfxID::ItemShield);
	else
		sounds.cast = Sfx(SfxID::Swing);
	// The skill going off, and its hits.
	switch (data.shape) {
	case Projectiles:
	case Stream:
		// A bow's arrows leave with its shot, thrown blades and axes with the heavy swing's whoosh.
		if (spell)
			sounds.spell = BoltLaunchOf(data);
		else
			sounds.spell = Sfx(bow ? SfxID::ShootBow : SfxID::Swing2);
		// Arrows and thrown blades land with the monster's own cry; bolts and landing bursts with their element.
		if (spell || (data.flags & TF_LandingBurst) != 0)
			sounds.hit = ImpactOf(data);
		break;
	case Burst:
		// Lightning comes down from the sky, as Diablo's Lightning spell; the rest lands with its element.
		sounds.spell = data.type == DamageType::Lightning ? Sfx(SfxID::SpellLightning) : ImpactOf(data);
		sounds.hit = ImpactOf(data);
		break;
	case Around:
		// A spell's ring bursts out like Diablo's Nova; a blow lands with its look.
		sounds.spell = spell && data.type == DamageType::Lightning ? Sfx(SfxID::SpellNova) : ImpactOf(data);
		break;
	case Zone:
		switch (data.zone) {
		case TableZone::Pulse:
			sounds.spell = spell ? BoltLaunchOf(data) : Sfx(SfxID::Swing2);
			sounds.hit = spell ? ImpactOf(data) : Sfx(SfxID::Swing);
			break;
		case TableZone::RandomStrikes:
			sounds.spell = spell ? BoltLaunchOf(data) : Sfx(bow ? SfxID::ShootBow : SfxID::Swing2);
			sounds.hit = ImpactOf(data);
			break;
		case TableZone::NearestStrike:
			sounds.spell = Sfx(SfxID::SpellLightningWall);
			sounds.hit = Sfx(SfxID::SpellLightning);
			break;
		case TableZone::Trap:
			sounds.spell = Sfx(SfxID::SpellTrapDisarm);
			sounds.hit = Sfx(SfxID::TriggerTrap);
			break;
		case TableZone::Turret:
			sounds.spell = Sfx(SfxID::OperateLever);
			sounds.hit = Sfx(SfxID::ShootBow);
			break;
		case TableZone::Delayed:
			sounds.spell = Sfx(SfxID::BigExplosion);
			break;
		case TableZone::IceSpike:
			// Bone Prison's cage grinds up out of the earth like stone.
			sounds.spell = data.type == DamageType::Physical ? Sfx(SfxID::SpellStoneCurse) : ModSound(GlacialSpikeSound);
			break;
		case TableZone::ShardRing:
			// The ring of shards bursts out like Frost Nova's, and each shard shatters as it lands.
			sounds.spell = Sfx(SfxID::SpellNova);
			sounds.hit = ImpactOf(data);
			break;
		default:
			sounds.spell = ImpactOf(data);
			break;
		}
		break;
	case Curse:
		switch (data.effect) {
		case MissileGraphicID::BoneSpirit:
			sounds.spell = Sfx(SfxID::SpellBoneSpirit);
			break;
		case MissileGraphicID::BoneHit:
			sounds.spell = Sfx(SfxID::SpellBoneSpiritHit);
			break;
		default:
			sounds.spell = Sfx(SfxID::SpellBloodStar);
			break;
		}
		break;
	case Buff:
		sounds.spell = Sfx(SfxID::SpellManaShield);
		break;
	case CorpseBurst:
		sounds.spell = Sfx(SfxID::BigExplosion);
		break;
	case HexBurst:
		sounds.spell = Sfx(SfxID::SpellBloodStar);
		sounds.hit = ImpactOf(data);
		break;
	case SoulHarvest:
		// The souls rise out of the corpses as Diablo's Resurrect raises one.
		sounds.spell = Sfx(SfxID::SpellResurrect);
		break;
	case CallOfTheAncients:
		// The ancestors come as Diablo's Golem does.
		sounds.spell = Sfx(SfxID::SpellGolem);
		break;
	case Seven:
		// The hero flickers from one enemy to the next.
		sounds.spell = Sfx(SfxID::SpellEtherealize);
		break;
	default:
		// Strikes, flurries, arcs, dashes and lines land with their look.
		sounds.spell = ImpactOf(data);
		break;
	}
	// The skills whose own sounds the shape and look don't give.
	switch (data.skill) {
	case WeaponSkill::Blizzard:
		// Its own cast, as the storm gathers over the target; each shard that strikes shatters.
		sounds.spell = ModSound(BlizzardCastSound);
		break;
	case WeaponSkill::FrozenOrb:
		sounds.cast = ModSound(BlizzardCastSound);
		break;
	case WeaponSkill::SolarOrb:
		sounds.spell = Sfx(SfxID::SpellInferno);
		break;
	case WeaponSkill::ExplosiveBolt:
		sounds.hit = Sfx(SfxID::BigExplosion);
		break;
	case WeaponSkill::ManaDrain:
		// Diablo's own mana theft.
		sounds.hit = Sfx(SfxID::SpellEnd);
		break;
	case WeaponSkill::LocustSwarm:
		sounds.spell = Sfx(SfxID::SpellDoomSerpents);
		break;
	case WeaponSkill::WarCry:
		// A shout that stuns, not a ward.
		sounds.cast = Sfx(SfxID::CastSkill);
		sounds.spell = Sfx(SfxID::SpellStoneCurse);
		break;
	case WeaponSkill::SpikeTrap:
	case WeaponSkill::Sentry:
		// Set down by hand, not shot.
		sounds.cast = Sfx(SfxID::Swing);
		break;
	default:
		break;
	}
	return sounds;
}

void PlaySkillSound(const SkillSound &sound, Point tile)
{
	if (sound.sfx != SfxID::None) {
		PlaySfxLoc(sound.sfx, tile);
		return;
	}
	if (sound.mod.empty())
		return;
	if (sound.mod == ShardSounds[0]) {
		PlayModSfxLoc(ShardSounds[NextShardSound++ % std::size(ShardSounds)], tile);
		return;
	}
	PlayModSfxLoc(sound.mod, tile);
}

void PlayTableSkillHitSound(const TableSkillData &data, Point tile)
{
	uint8_t &cooldown = HitSoundCooldown[static_cast<size_t>(data.skill)];
	if (cooldown > 0)
		return;
	const SkillSound sound = TableSkillSounds(data).hit;
	if (sound.empty())
		return;
	cooldown = static_cast<uint8_t>(HitSoundGapTicks);
	PlaySkillSound(sound, tile);
}

std::optional<SfxID> SkillMissileLandSound(const Missile &missile)
{
	// Only the hero's (Missile::sourceType, which isn't const).
	if (!gbIsPoe2 || missile._misource == -1 || missile._micaster == TARGET_PLAYERS)
		return std::nullopt;
	const SkillSlot skill = MissileSkill(missile);
	if (skill.kind == SkillKind::Attack) {
		const auto weaponSkill = static_cast<WeaponSkill>(skill.id);
		// Only the skill's own projectiles: a granted skill's name also rides on the weapon's default bolt.
		const TableSkillData *data = FindTableSkill(weaponSkill);
		if (data != nullptr && missile._mitype == (data->missile == MissileID::Null ? MissileID::Arrow : data->missile)) {
			PlayTableSkillHitSound(*data, missile.position.tile);
			return SfxID::None;
		}
		// Ice Bolt's and Frost Nova's shards shatter as ice, not as the flare's fire, and a nova of them as one.
		if (IsAnyOf(weaponSkill, WeaponSkill::IceBolt, WeaponSkill::FrostNova)) {
			uint8_t &cooldown = HitSoundCooldown[static_cast<size_t>(weaponSkill)];
			if (cooldown == 0) {
				cooldown = static_cast<uint8_t>(HitSoundGapTicks);
				PlaySkillSound(ModSound(ShardSounds[0]), missile.position.tile);
			}
			return SfxID::None;
		}
		return std::nullopt;
	}
	// The Blood Star gem spell's bolt bursts with Diablo's Blood Star impact (its missile has none of its own, which the
	// Succubi's share).
	if (missile._mitype == MissileID::BloodStar) {
		if (BloodStarHitCooldown == 0) {
			BloodStarHitCooldown = static_cast<uint8_t>(HitSoundGapTicks);
			PlaySfxLoc(SfxID::SpellBloodStarHit, missile.position.tile);
		}
		return SfxID::None;
	}
	return std::nullopt;
}

void ProcessSkillZones(Player &player)
{
	// The skills' hit sounds come round again (see PlayTableSkillHitSound).
	for (uint8_t &cooldown : HitSoundCooldown) {
		if (cooldown > 0)
			cooldown--;
	}
	if (BloodStarHitCooldown > 0)
		BloodStarHitCooldown--;
	// Consecrated Ground's life, a tick's share at a time, and the Sigil of Power's time.
	GrantedRuntime &granted = player.poe2.grantedState;
	if (granted.sigilTicks > 0)
		granted.sigilTicks = player.hasNoLife() || leveltype == DTYPE_TOWN ? 0 : granted.sigilTicks - 1;
	if (granted.regenTicks > 0) {
		if (player.hasNoLife() || leveltype == DTYPE_TOWN) {
			granted.regenTicks = 0;
			granted.regenRaw = 0;
		} else {
			const Fixed26_6 life = Fixed26_6::fromRaw(granted.regenRaw / granted.regenTicks);
			granted.regenRaw -= life.raw();
			granted.regenTicks--;
			player._pHitPoints = std::min(player._pHitPoints + life, player._pMaxHP);
			player._pHPBase = std::min(player._pHPBase + life, player._pMaxHPBase);
			RedrawComponent(PanelDrawComponent::Health);
		}
	}
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
		const SupportHitScope scope(player, SkillSlot::ForAttack(zone.skill));
		const SkillLookScope skillLook(zone.skill);
		const int radius = zone.radius;
		if (!done && (data->flags & TF_FollowPlayer) != 0)
			zone.center = player.position.tile;
		if (!done && zone.delay > 0) {
			zone.delay--;
		} else if (!done) {
			// Landing bursts of projectiles, Delayed zones and a granted skill's echo go off once.
			if ((data->flags & TF_LandingBurst) != 0 || IsAnyOf(data->zone, TableZone::Delayed, TableZone::IceSpike) || data->rider == GrantedRider::Echo) {
				// Bone Prison traps for its supported duration, which the zone kept while it waited.
				TableSkillData burst = *data;
				burst.duration = static_cast<uint16_t>(std::max<int>(zone.ticksLeft, 1));
				ZoneBurst(player, burst, zone.center, std::max(radius, 1), zone.damage);
				if ((data->flags & TF_Pin) != 0)
					EffectRingAround(zone.center, std::max(radius, 1), data->effect, 1);
				if (data->zone == TableZone::Delayed) {
					PlaySkillSound(TableSkillSounds(*data).spell, zone.center);
					ShakeCamera(ShakeStrength::Medium, 6);
				} else if (data->zone == TableZone::IceSpike) {
					PlaySkillSound(TableSkillSounds(*data).spell, zone.center);
					ShakeCamera(ShakeStrength::Medium, 5);
				} else {
					// POE2 Mashup: a projectile's landing burst and Echoing Blast's echo are heard too.
					PlayTableSkillHitSound(*data, zone.center);
				}
				done = true;
			} else if (data->zone == TableZone::ShardRing) {
				AddVisualEffect(zone.center, data->effect, 0, 6);
				// POE2 Mashup: the orb bursts with a sound (see SkillSounds::spell).
				PlaySkillSound(TableSkillSounds(*data).spell, zone.center);
				for (int s = 0; s < data->count; s++) {
					constexpr int Ring[16][2] = { { 4, 0 }, { 4, 2 }, { 4, 4 }, { 2, 4 }, { 0, 4 }, { -2, 4 }, { -4, 4 }, { -4, 2 }, { -4, 0 }, { -4, -2 }, { -4, -4 }, { -2, -4 }, { 0, -4 }, { 2, -4 }, { 4, -4 }, { 4, -2 } };
					if (Missile *shard = FireTableMissile(player, *data, zone.center, zone.center + Displacement { Ring[s % 16][0], Ring[s % 16][1] }); shard != nullptr)
						shard->duration = 9;
				}
				done = true;
			} else if (data->zone == TableZone::Trap) {
				Monster *victim = TargetMonsterNear(zone.center, 1);
				if (victim != nullptr) {
					ZoneBurst(player, *data, zone.center, radius, zone.damage);
					EffectRingAround(zone.center, 1, data->effect, 1);
					// POE2 Mashup: the trap springs with Diablo's trap sound.
					PlayTableSkillHitSound(*data, zone.center);
					done = true;
				} else if (zone.counter % 20 == 0) {
					AddVisualEffect(zone.center, MissileGraphicID::MetalHit);
				}
			} else if (zone.counter % std::max<int>(data->interval, 1) == 0) {
				switch (data->zone) {
				case TableZone::Pulse:
					ZoneBurst(player, *data, zone.center, radius, zone.damage);
					// POE2 Mashup: each pulse is heard (Blade Sentinel, Blessed Hammer, Solar Orb).
					PlayTableSkillHitSound(*data, zone.center);
					break;
				case TableZone::RandomStrikes: {
					const Point at = zone.center + Displacement { GenerateRnd(2 * radius + 1) - radius, GenerateRnd(2 * radius + 1) - radius };
					if (InDungeonBounds(at)) {
						ZoneBurst(player, *data, at, 1, zone.damage);
						// A Blizzard drops a lot of these, so only some make a sound: soft, short and one of four.
						PlayTableSkillHitSound(*data, at);
					}
					break;
				}
				case TableZone::NearestStrike:
					if (Monster *monster = TargetMonsterNear(zone.center, radius); monster != nullptr) {
						AddVisualEffect(monster->position.tile, MissileGraphicID::ThinLightning, 0, 6);
						AddVisualEffect(monster->position.tile, data->effect);
						SpellHitMonster(player, *monster, data->type, zone.damage, SourceOf(*data));
						PlayTableSkillHitSound(*data, monster->position.tile);
					}
					break;
				case TableZone::Turret:
					if (Monster *monster = TargetMonsterNear(zone.center, radius); monster != nullptr) {
						FireTableMissile(player, *data, zone.center, monster->position.tile);
						AddVisualEffect(zone.center, MissileGraphicID::MetalHit);
						PlayTableSkillHitSound(*data, zone.center);
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
	// A new level: the Sigil of Power stays behind.
	if (MyPlayer != nullptr)
		MyPlayer->poe2.grantedState.sigilTicks = 0;
}

int SigilSpellDamagePercent(const Player &player)
{
	const GrantedRuntime &state = player.poe2.grantedState;
	if (!gbIsPoe2 || state.sigilTicks == 0)
		return 100;
	return player.position.tile.ExactDistance(Point { state.sigilX, state.sigilY }) <= SigilRadius ? SigilSpellDamagePercentInside : 100;
}

size_t SkillZoneCount()
{
	return Zones.size();
}

std::optional<Point> SkillZoneCenter(size_t index)
{
	if (index >= Zones.size())
		return std::nullopt;
	return Zones[index].center;
}

Fixed26_6 ApplyTableSkillDefences(Monster &monster, Player &player, Fixed26_6 damage)
{
	if (!gbIsPoe2)
		return damage;
	int percent = 100;
	if (monster.poe2.weakenTicks > 0)
		percent -= WeakenPercent * TableCurseStrength(monster, TableCurse::Weaken) / 100;
	if (monster.poe2.decrepifyTicks > 0)
		percent -= DecrepifyPercent * TableCurseStrength(monster, TableCurse::Decrepify) / 100;
	if (player.poe2.frozenArmorTicks > 0) {
		percent -= FrozenArmorPercent;
		if (AilmentAllowed(player, AilmentSource::Spell)) {
			monster.poe2.chillTicks = 60;
			monster.poe2.chillPercent = static_cast<uint8_t>(std::max<int>(monster.poe2.chillPercent, 30));
		}
	}
	if (player.poe2.holyShieldTicks > 0)
		percent -= HolyShieldPercent;
	if (player.poe2.shieldWallTicks > 0)
		percent -= ShieldWallPercent;
	damage = Fixed26_6::fromRaw(damage.raw() * std::max(percent, 10) / 100);
	// Iron Maiden: the monster takes its own hit.
	if (monster.poe2.ironMaidenTicks > 0 && !monster.hasNoLife() && &player == MyPlayer) {
		ApplyMonsterDamage(DamageType::Physical, monster, Fixed26_6::fromRaw(damage.raw() * TableCurseStrength(monster, TableCurse::IronMaiden) / 100));
		AddVisualEffect(monster.position.tile, MissileGraphicID::BloodHit);
		if (monster.hasNoLife())
			M_StartKill(monster, player);
	}
	return damage;
}

void ProcessTableCurses(Monster &monster)
{
	Poe2MonsterState &state = monster.poe2;
	for (uint8_t *ticks : { &state.weakenTicks, &state.decrepifyTicks, &state.ironMaidenTicks, &state.lowerResistTicks, &state.deathMarkTicks, &state.despairTicks, &state.hauntTicks, &state.locustTicks, &state.plagueTicks }) {
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
		percent += DeathMarkPercent * TableCurseStrength(monster, TableCurse::DeathMark) / 100;
	if (monster.poe2.decrepifyTicks > 0 && type == DamageType::Physical)
		percent += DecrepifyPercent * TableCurseStrength(monster, TableCurse::Decrepify) / 100;
	return percent;
}

int TableCurseShownPercent(const Monster &monster, TableCurse curse)
{
	const Poe2MonsterState &state = monster.poe2;
	switch (curse) {
	case TableCurse::Weaken:
		return state.weakenTicks > 0 ? WeakenPercent * TableCurseStrength(monster, curse) / 100 : 0;
	case TableCurse::Decrepify:
		return state.decrepifyTicks > 0 ? DecrepifyPercent * TableCurseStrength(monster, curse) / 100 : 0;
	case TableCurse::DeathMark:
		return state.deathMarkTicks > 0 ? DeathMarkPercent * TableCurseStrength(monster, curse) / 100 : 0;
	case TableCurse::IronMaiden:
		return state.ironMaidenTicks > 0 ? TableCurseStrength(monster, curse) : 0;
	default:
		return 0;
	}
}

void OnTableCursedMonsterKilled(const Monster &monster)
{
	const Poe2MonsterState &state = monster.poe2;
	if ((state.hauntTicks == 0 && state.locustTicks == 0 && state.plagueTicks == 0) || state.chaosSource < 0 || static_cast<size_t>(state.chaosSource) >= Players.size())
		return;
	const Player &player = Players[state.chaosSource];
	const Point from = monster.position.tile;
	const bool haunt = state.hauntTicks > 0;
	// Decompose's plague passes to the one nearest monster, as a weaker Locust Swarm.
	const bool plague = !haunt && state.locustTicks == 0;
	const int range = haunt ? HauntJumpTiles : (plague ? PlagueSpreadTiles : LocustSpreadTiles);
	std::vector<Monster *> next;
	ForEachMonsterNear(from, range, [&](Monster &other) {
		if (&other != &monster)
			next.push_back(&other);
	});
	std::sort(next.begin(), next.end(), [&](const Monster *a, const Monster *b) {
		return from.WalkingDistance(a->position.tile) < from.WalkingDistance(b->position.tile);
	});
	const size_t jumps = haunt || plague ? 1 : LocustSpreads;
	if (next.size() > jumps)
		next.resize(jumps);
	const uint8_t ticks = haunt ? state.hauntTicks : (plague ? static_cast<uint8_t>(PlagueTicks) : state.locustTicks);
	// Haunt is a spirit, not a wound (Curtis 2026-10-28: "is it a bleed skill, or a ghost?"): it leaves the body and
	// shows up on the next one.
	const MissileGraphicID look = haunt ? MissileGraphicID::BoneSpirit : MissileGraphicID::BloodHit;
	if (haunt && !next.empty())
		AddVisualEffect(from, look);
	for (Monster *other : next) {
		Poe2MonsterState &target = other->poe2;
		(haunt ? target.hauntTicks : (plague ? target.plagueTicks : target.locustTicks)) = ticks;
		target.chaosTicks = std::max(target.chaosTicks, ticks);
		target.chaosDamagePerTick = std::max(target.chaosDamagePerTick, state.chaosDamagePerTick);
		target.chaosSource = state.chaosSource;
		target.chaosSkill = state.chaosSkill;
		other->tag(player);
		AddVisualEffect(other->position.tile, look, haunt ? 2 : 0);
	}
}

int SoulHarvestDamagePercent(const Player &player)
{
	return player.poe2.soulStacks * SoulDamagePercent;
}

} // namespace devilution
