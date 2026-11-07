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
#include "engine/path.h"
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
#include "poe2/monster_armour.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/skill_looks.hpp"
#include "poe2/skill_resources.hpp"
#include "poe2/spell_base.hpp"
#include "poe2/spirit_skills.hpp"
#include "poe2/support_gems.hpp"
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
	{ WS::Whirlwind, "Whirlwind", "Spin through the enemies in your path, hitting each one on the way once", 5, 8, TW::AxeOrSword, 0, Dash, 70, 4, 1, 0, 0, 0, DT::Physical, MissileID::Null, MG::MetalHit, 0, TableZone::None, TableCurse::None, TableBuff::None, TT_Sword | TT_Axe, Icon(SpellIcon::Berserk), TableCurve::Boneshatter, 300 },
	{ WS::Sacrifice, "Sacrifice", "A brutal strike paid for with 5% of your life", 7, 6, TW::Sword, 0, Strike, 330, 0, 0, 0, 0, 0, DT::Physical, MissileID::Null, MG::BloodHit, TF_LifeCost, TableZone::None, TableCurse::None, TableBuff::None, TT_Sword, Icon(SpellIcon::BloodBoil), TableCurve::Boneshatter, 0 },
	{ WS::Zeal, "Zeal", "Quick stabs at the enemy in front; every fourth is a heavy blow at the three tiles in front", 15, 10, TW::Sword, 0, Flurry, 203, 4, 0, 0, 0, 0, DT::Physical, MissileID::Null, MG::MetalHit, 0, TableZone::None, TableCurse::None, TableBuff::None, TT_Sword, Icon(SpellIcon::Jester), TableCurve::Boneshatter, 290 },
	{ WS::Vengeance, "Vengeance", "A strike that bursts into fire, lightning and cold", 11, 5, TW::Sword, 0, Strike, 260, 0, 0, 0, 0, 0, DT::Physical, MissileID::Null, MG::HolyBoltExplosion, TF_Elements, TableZone::None, TableCurse::None, TableBuff::None, TT_Sword | TT_Elemental, Icon(SpellIcon::Elemental), TableCurve::Boneshatter, 0 },
	{ WS::ArkainesWrath, "Arkaine's Wrath", "A sweeping blow that calls down lightning and staggers", 15, 17, TW::Sword, 0, Arc, 240, 0, 0, 0, 0, 0, DT::Lightning, MissileID::Null, MG::Lightning, TF_Stun, TableZone::None, TableCurse::None, TableBuff::None, TT_Sword | TT_Elemental, Icon(SpellIcon::LightningWall), TableCurve::Boneshatter, 0 },
	// Dagger
	{ WS::FanOfBlades, "Fan of Blades", "Throw daggers in every direction", 5, 5, TW::Dagger, 0, Projectiles, 70, 8, 0, 0, 0, 0, DT::Physical, MissileID::Arrow, MG::MetalHit, 0, TableZone::None, TableCurse::None, TableBuff::None, TT_Dagger, Icon(SpellIcon::Sentinel), TableCurve::LightningArrow, 280 },
	{ WS::ViperStrike, "Cobra Strike", "A heavy poisoned stab: poison from your other skills on the enemy deals all it has left at once, then its own strong poison goes on", 7, 11, TW::Dagger, 0, Strike, 380, 0, 0, 0, 0, 0, DT::Physical, MissileID::Null, MG::BloodHit, TF_Poison, TableZone::None, TableCurse::None, TableBuff::None, TT_Dagger, Icon(SpellIcon::DoomSerpents), TableCurve::Boneshatter, 0, GrantedRider::None, 60 },
	{ WS::BladeSentinel, "Blade Sentinel", "A whirling blade that guards the target spot for 5 seconds", 9, 12, TW::Dagger, 0, Zone, 35, 0, 1, 100, 20, 0, DT::Physical, MissileID::Null, MG::MetalHit, 0, TableZone::Pulse, TableCurse::None, TableBuff::None, TT_Dagger, Icon(SpellIcon::Sentinel), TableCurve::Boneshatter, 260 },
	{ WS::DeathMark, "Death Mark", "Marks an enemy to take 25% more damage; your next Shadow Lunge or Cobra Strike on it uses the mark for double damage", 11, 9, TW::Dagger, 1, Curse, Flat, 0, 0, 160, 0, 0, DT::Physical, MissileID::Null, MG::BloodHit, 0, TableZone::None, TableCurse::DeathMark, TableBuff::None, TT_Dagger, Icon(SpellIcon::RedSkull), TableCurve::Boneshatter, 100 },
	{ WS::BladeFury, "Blade Fury", "A rapid stream of thrown daggers", 13, 12, TW::Dagger, 0, Stream, 70, 8, 0, 0, 2, 0, DT::Physical, MissileID::Arrow, MG::MetalHit, 0, TableZone::None, TableCurse::None, TableBuff::None, TT_Dagger, Icon(SpellIcon::Jester), TableCurve::LightningArrow, 290 },
	// Axe
	{ WS::DoubleThrow, "Double Throw", "Hurl two axes, and one more for each Frenzy charge", 1, 3, TW::Axe, 0, Projectiles, 127, 2, 0, 0, 0, 0, DT::Physical, MissileID::Arrow, MG::MetalHit, TF_Frenzy, TableZone::None, TableCurse::None, TableBuff::None, TT_Axe, Icon(SpellIcon::Telekinesis), TableCurve::LightningArrow, 290 },
	{ WS::Rend, "Rend", "A tearing sweep that leaves enemies bleeding and gives 3 Rage for each one hit", 9, 7, TW::Axe, 0, Arc, 100, 0, 0, 0, 0, 0, DT::Physical, MissileID::Null, MG::BloodHit, TF_Bleed, TableZone::None, TableCurse::None, TableBuff::None, TT_Axe, Icon(SpellIcon::BloodBoil), TableCurve::Boneshatter, 0 },
	{ WS::WarCry, "War Cry", "Stuns enemies around you", 11, 10, TW::None, 1, Buff, Flat, 0, 2, 160, 0, 0, DT::Physical, MissileID::Null, MG::StoneCurseShatter, TF_Stun, TableZone::None, TableCurse::None, TableBuff::WarCry, TT_Axe | TT_Mace | TT_Sword, Icon(SpellIcon::LongHorn), TableCurve::Boneshatter, 200 },
	{ WS::CallOfTheAncients, "Call of the Ancients", "Spends all Rage: ancestral warriors strike all around, 5% harder per Rage", 15, 14, TW::Axe, 0, CallOfTheAncients, 72, 3, 2, 0, 0, 0, DT::Physical, MissileID::Null, MG::HolyBoltExplosion, TF_Rage, TableZone::None, TableCurse::None, TableBuff::None, TT_Axe, Icon(SpellIcon::Golem), TableCurve::Boneshatter, 0 },
	// Mace
	{ WS::Fissure, "Fissure", "The ground splits open in a line, staggering what it hits", 7, 10, TW::Mace, 0, Line, 65, 6, 1, 0, 0, 0, DT::Physical, MissileID::Null, MG::StoneCurseShatter, TF_Stun, TableZone::None, TableCurse::None, TableBuff::None, TT_Mace, Icon(SpellIcon::PentaStar), TableCurve::Earthquake, 0 },
	{ WS::BlessedHammer, "Blessed Hammer", "Holy hammers spiral around you for 4 seconds", 9, 10, TW::None, 1, Zone, 20, 0, 1, 80, 20, 0, DT::Physical, MissileID::Null, MG::HolyBoltExplosion, TF_Spell | TF_FollowPlayer | TF_AtPlayer, TableZone::Pulse, TableCurse::None, TableBuff::None, TT_Mace | TT_Occult, Icon(SpellIcon::HolyBolt), TableCurve::Fireball, 312 },
	{ WS::Shockwave, "Shockwave", "A slam whose shockwave runs far ahead and stuns", 20, 14, TW::Mace, 0, Line, 95, 5, 1, 0, 0, 0, DT::Physical, MissileID::Null, MG::StoneCurseShatter, TF_Stun | TF_Knockback, TableZone::None, TableCurse::None, TableBuff::None, TT_Mace, Icon(SpellIcon::Nova), TableCurve::Earthquake, 0 },
	{ WS::FistOfTheHeavens, "Fist of the Heavens", "Lightning strikes the target and sprays bolts all around", 13, 16, TW::None, 3, Burst, 110, 6, 1, 0, 0, 0, DT::Lightning, MissileID::ChargedBolt, MG::Lightning, TF_Spell, TableZone::None, TableCurse::None, TableBuff::None, TT_Mace | TT_Elemental, Icon(SpellIcon::ChainLightning), TableCurve::Fireball, 312 },
	// Quarterstaff
	{ WS::DashingStrike, "Dashing Strike", "Dash through the enemies, striking each one on the way", 5, 7, TW::Staff, 0, Dash, 80, 4, 1, 0, 0, 0, DT::Physical, MissileID::Null, MG::MetalHit, TF_Combo, TableZone::None, TableCurse::None, TableBuff::None, TT_Staff, Icon(SpellIcon::Phasing), TableCurve::LeapSlam, 320 },
	{ WS::LashingTailKick, "Lashing Tail Kick", "A spinning kick that throws back all around", 15, 10, TW::Staff, 0, Around, 94, 0, 1, 0, 0, 0, DT::Physical, MissileID::Null, MG::BoneHit, TF_Knockback | TF_Combo, TableZone::None, TableCurse::None, TableBuff::None, TT_Staff, Icon(SpellIcon::Search), TableCurve::Boneshatter, 0 },
	{ WS::WaveOfLight, "Wave of Light", "A wave of holy light bursts out around you; at full Combo it drops a great bell that tolls when you strike it", 9, 10, TW::Staff, 0, Around, 50, 0, 3, 0, 0, 0, DT::Fire, MissileID::Null, MG::HolyBoltExplosion, TF_Combo, TableZone::None, TableCurse::None, TableBuff::None, TT_Staff | TT_Elemental, Icon(SpellIcon::FlameWave), TableCurve::Boneshatter, 280 },
	{ WS::CycloneStrike, "Cyclone Strike", "A storm of strikes all around you that drags enemies in, crackling with lightning", 11, 10, TW::Staff, 0, Around, 50, 0, 3, 0, 0, 0, DT::Lightning, MissileID::Null, MG::Lightning, TF_Combo, TableZone::None, TableCurse::None, TableBuff::None, TT_Staff | TT_Elemental, Icon(SpellIcon::ChainLightning), TableCurve::Boneshatter, 280 },
	{ WS::SevenSidedStrike, "Seven-Sided Strike", "Strike up to seven enemies nearby, one after another", 13, 11, TW::Staff, 0, Seven, 200, 7, 4, 0, 0, 0, DT::Physical, MissileID::Null, MG::MetalHit, TF_Combo, TableZone::None, TableCurse::None, TableBuff::None, TT_Staff, Icon(SpellIcon::Teleport), TableCurve::Boneshatter, 0 },
	// Bow. Fire Arrow and Explosive Arrow came from the crossbow (dropped 2026-10-27); Fire Arrow is tier I, beside the Rogue's starting Hail of Arrows
	{ WS::ClusterBolt, "Fire Arrow", "An arrow that bursts into flame where it lands", 1, 8, TW::Bow, 0, Projectiles, 100, 1, 1, 0, 0, 0, DT::Fire, MissileID::Arrow, MG::FireArrowExplosion, TF_LandingBurst, TableZone::None, TableCurse::None, TableBuff::None, TT_Bow | TT_Elemental, Icon(SpellIcon::Fireball), TableCurve::LightningArrow, 0 },
	{ WS::Multishot, "Multishot", "A wide fan of seven arrows", 9, 6, TW::Bow, 0, Projectiles, 95, 7, 0, 0, 0, 0, DT::Physical, MissileID::Arrow, MG::MetalHit, 0, TableZone::None, TableCurse::None, TableBuff::None, TT_Bow, Icon(SpellIcon::FireCloud), TableCurve::LightningArrow, 280 },
	{ WS::GuidedArrow, "Guided Arrow", "One heavy arrow that homes in on its target and pierces through several enemies", 11, 5, TW::Bow, 0, Projectiles, 220, 1, 0, 0, 0, 0, DT::Physical, MissileID::Arrow, MG::MetalHit, TF_Pierce, TableZone::None, TableCurse::None, TableBuff::None, TT_Bow, Icon(SpellIcon::Search), TableCurve::LightningArrow, 0 },
	{ WS::FreezingArrow, "Freezing Arrow", "An arrow that bursts into freezing cold where it lands", 13, 10, TW::Bow, 0, Projectiles, 100, 1, 2, 0, 0, 0, DT::Cold, MissileID::Arrow, MG::BlueFlareExplosion, TF_LandingBurst, TableZone::None, TableCurse::None, TableBuff::None, TT_Bow | TT_Elemental, Icon(SpellIcon::StoneCurse), TableCurve::LightningArrow, 0 },
	{ WS::ExplosiveBolt, "Explosive Arrow", "An arrow that sticks where it lands and explodes a second later, bursting poison clouds in fire", 11, 9, TW::Bow, 0, Projectiles, 110, 1, 2, 0, 0, 0, DT::Fire, MissileID::Arrow, MG::FireArrowExplosion, TF_LandingBurst, TableZone::None, TableCurse::None, TableBuff::None, TT_Bow | TT_Elemental, Icon(SpellIcon::Fireball), TableCurve::LightningArrow, 0 },
	// Crossbow: retired with the crossbows (see IsRetiredSkill); an old gem still loads and works with a crossbow
	{ WS::Impale, "Impale", "A crossbow bolt that skewers through enemies", 3, 6, TW::Crossbow, 0, Projectiles, 250, 1, 0, 0, 0, 0, DT::Physical, MissileID::Arrow, MG::MetalHit, TF_Pierce, TableZone::None, TableCurse::None, TableBuff::None, TT_Bow, Icon(SpellIcon::Search), TableCurve::LightningArrow, 0 },
	{ WS::SpikeTrap, "Spike Trap", "Sets a trap that bursts when an enemy steps close", 7, 8, TW::Crossbow, 0, Zone, 75, 0, 1, 200, 0, 0, DT::Physical, MissileID::Null, MG::StoneCurseShatter, TF_Stun, TableZone::Trap, TableCurse::None, TableBuff::None, TT_Bow, Icon(SpellIcon::TrapDisarm), TableCurve::LightningArrow, 0 },
	{ WS::Sentry, "Sentry", "Plants a turret that shoots the nearest enemy for 8 seconds", 9, 12, TW::Crossbow, 0, Zone, 60, 0, 8, 160, 12, 0, DT::Physical, MissileID::Arrow, MG::MetalHit, TF_AtPlayer, TableZone::Turret, TableCurse::None, TableBuff::None, TT_Bow, Icon(SpellIcon::Golem), TableCurve::LightningArrow, 0 },
	{ WS::SiegeVolley, "Ballista Barrage", "Bolts rain down on the target area", 13, 14, TW::Crossbow, 0, Zone, 75, 0, 3, 50, 6, 0, DT::Physical, MissileID::Null, MG::MetalHit, 0, TableZone::RandomStrikes, TableCurse::None, TableBuff::None, TT_Bow, Icon(SpellIcon::FireCloud), TableCurve::RainOfArrows, 0 },
	// Shield
	{ WS::HolyShield, "Holy Shield", "Take 15% less damage from melee hits and block 15% more often for 10 seconds", 5, 10, TW::Shield, 1, Buff, Flat, 0, 0, 200, 0, 0, DT::Physical, MissileID::Null, MG::HolyBoltExplosion, 0, TableZone::None, TableCurse::None, TableBuff::HolyShield, TT_Shield, Icon(SpellIcon::Pentagram), TableCurve::Boneshatter, 100 },
	{ WS::BlessedShield, "Blessed Shield", "Throw your shield: it bounces to up to 3 more enemies, stunning each", 7, 8, TW::Shield, 0, Projectiles, 150, 1, 0, 0, 0, 0, DT::Physical, MissileID::Arrow, MG::HolyBoltExplosion, TF_Stun, TableZone::None, TableCurse::None, TableBuff::None, TT_Shield, Icon(SpellIcon::HolyBolt), TableCurve::LeapSlam, 340 },
	{ WS::Condemn, "Condemn", "After a moment, holy fire bursts out all around you", 9, 12, TW::Shield, 1, Zone, 50, 0, 3, 0, 0, 20, DT::Fire, MissileID::Null, MG::HolyBoltExplosion, TF_Spell | TF_FollowPlayer | TF_AtPlayer, TableZone::Delayed, TableCurse::None, TableBuff::None, TT_Shield | TT_Occult, Icon(SpellIcon::Apocalypse), TableCurve::Fireball, 312 },
	{ WS::ShieldWall, "Shield Wall", "Raise a wall of earth across your front for 6 seconds; slams, Shield Charge and war cries shatter it", 11, 10, TW::Shield, 1, Buff, Flat, 0, 0, 120, 0, 0, DT::Physical, MissileID::Null, MG::StoneCurseShatter, 0, TableZone::None, TableCurse::None, TableBuff::ShieldWall, TT_Shield, Icon(SpellIcon::Reflect), TableCurve::Boneshatter, 100 },
	{ WS::FallingSword, "Falling Sword", "Fade out in light; a blade of light falls on the target and leaves holy ground, and you appear there", 20, 18, TW::Melee, 0, Burst, 90, 0, 2, 0, 0, 0, DT::Physical, MissileID::Null, MG::HolyBoltExplosion, TF_Stun, TableZone::None, TableCurse::None, TableBuff::None, TT_Shield | TT_Sword, Icon(SpellIcon::RedSkull), TableCurve::LeapSlam, 340 },
	// Fire and lightning
	{ WS::Meteor, "Meteor", "After a moment, a meteor crashes down on the target", 20, 25, TW::None, 2, Zone, 145, 0, 2, 0, 0, 30, DT::Fire, MissileID::Null, MG::ApocalypseBoom, TF_Spell, TableZone::Delayed, TableCurse::None, TableBuff::None, TT_Elemental, Icon(SpellIcon::FireCloud), TableCurve::Fireball, 312 },
	{ WS::Thunderstorm, "Thunderstorm", "A storm follows you for 10 seconds, striking the nearest enemy", 20, 20, TW::None, 3, Zone, 136, 0, 6, 200, 20, 0, DT::Lightning, MissileID::Null, MG::Lightning, TF_Spell | TF_FollowPlayer | TF_AtPlayer, TableZone::NearestStrike, TableCurse::None, TableBuff::None, TT_Elemental, Icon(SpellIcon::ChainLightning), TableCurve::Fireball, 312 },
	// Frost
	{ WS::GlacialSpike, "Glacial Spike", "A great spike of ice bursts out of the ground under the target, then shatters; frozen enemies take double and break free", 15, 10, TW::None, 1, Zone, 250, 0, 1, 0, 0, 3, DT::Cold, MissileID::Null, MG::BlueFlareExplosion, TF_Spell, TableZone::IceSpike, TableCurse::None, TableBuff::None, TT_Elemental, Icon(SpellIcon::StoneCurse), TableCurve::Fireball, 312 },
	{ WS::FrozenArmor, "Frozen Armor", "For 15 seconds, take 20% less damage from melee hits and chill those who strike you", 7, 12, TW::None, 1, Buff, Flat, 0, 0, 300, 0, 0, DT::Cold, MissileID::Null, MG::StoneCurseShatter, 0, TableZone::None, TableCurse::None, TableBuff::FrozenArmor, TT_Elemental, Icon(SpellIcon::ManaShield), TableCurve::Boneshatter, 100 },
	{ WS::Blizzard, "Blizzard", "Ice shards rain on the target area for 4 seconds", 9, 16, TW::None, 1, Zone, 21, 0, 3, 80, 4, 0, DT::Cold, MissileID::Null, MG::BlueFlareExplosion, TF_Spell, TableZone::RandomStrikes, TableCurse::None, TableBuff::None, TT_Elemental, Icon(SpellIcon::Elemental), TableCurve::Fireball, 312 },
	{ WS::IceBarrage, "Ray of Frost", "A rapid stream of ice shards", 11, 14, TW::None, 1, Stream, 70, 8, 0, 0, 2, 0, DT::Cold, MissileID::BlueFlare, MG::StoneCurseShatter, TF_Spell, TableZone::None, TableCurse::None, TableBuff::None, TT_Elemental, Icon(SpellIcon::StoneCurse), TableCurve::Fireball, 312 },
	{ WS::FrozenOrb, "Frozen Orb", "An orb of frost bursts at the target into a ring of shards", 20, 22, TW::None, 1, Zone, 70, 16, 0, 0, 0, 15, DT::Cold, MissileID::BlueFlare, MG::BlueFlareExplosion, TF_Spell, TableZone::ShardRing, TableCurse::None, TableBuff::None, TT_Elemental, Icon(SpellIcon::Nova), TableCurve::Fireball, 312 },
	// Arcane
	{ WS::Haunt, "Haunt", "A spirit drains the enemy with chaos over time. When it dies, the spirit jumps to the nearest enemy.", 3, 8, TW::None, 1, Curse, 350, 0, 0, 100, 0, 0, DT::Chaos, MissileID::Null, MG::BoneSpirit, TF_Spell, TableZone::None, TableCurse::Haunt, TableBuff::None, TT_Occult, Icon(SpellIcon::BoneSpirit), TableCurve::Fireball, 312 },
	// The poison skills plan (Curtis 2026-11-02): a poison now, at gem 10 (tier VII), 165%; was a chaos drain at gem 7, 130%.
	{ WS::LocustSwarm, "Locust Swarm", "A poison plague that eats at enemies and spreads when they die", 10, 12, TW::None, 1, Curse, LocustSwarmPercent, 0, 1, 40, 0, 0, DT::Chaos, MissileID::Null, MG::AcidSplat, TF_Spell, TableZone::None, TableCurse::Locust, TableBuff::None, TT_Occult, Icon(SpellIcon::DoomSerpents), TableCurve::Fireball, 312 },
	{ WS::CorpseExplosion, "Corpse Explosion", "Blows up a corpse near the target, burning everything around it", 10, 10, TW::None, 1, CorpseBurst, 60, 0, 2, 0, 0, 0, DT::Fire, MissileID::Null, MG::FireArrowExplosion, TF_Spell, TableZone::None, TableCurse::None, TableBuff::None, TT_Occult, Icon(SpellIcon::BloodStar), TableCurve::Fireball, 312 },
	{ WS::SoulHarvest, "Soul Harvest", "Takes the souls of nearby corpses: 3% more damage each, up to 10, for 10 seconds", 11, 10, TW::None, 1, SoulHarvest, Flat, 10, 4, 200, 0, 0, DT::Physical, MissileID::Null, MG::Resurrect, TF_Spell, TableZone::None, TableCurse::None, TableBuff::None, TT_Occult, Icon(SpellIcon::Resurrect), TableCurve::Boneshatter, 100 },
	// Skill-balance Q29: 160%, was 200%.
	{ WS::BonePrison, "Bone Prison", "Bone spikes burst up under the target, hurting the enemies there and trapping them for 3 seconds", 6, 10, TW::None, 1, Zone, 90, 0, 1, 60, 0, 3, DT::Physical, MissileID::Null, MG::BoneHit, TF_Spell | TF_Pin, TableZone::IceSpike, TableCurse::None, TableBuff::None, TT_Occult, Icon(SpellIcon::BoneSpirit), TableCurve::Fireball, 312 },
	// Curses
	{ WS::Weaken, "Weaken", "Curses enemies to hit 30% softer", 3, 8, TW::None, 1, Curse, Flat, 0, 2, 200, 0, 0, DT::Physical, MissileID::Null, MG::BloodHit, TF_Spell, TableZone::None, TableCurse::Weaken, TableBuff::None, TT_Occult, Icon(SpellIcon::Mana), TableCurve::Boneshatter, 100 },
	{ WS::IronMaiden, "Iron Maiden", "Curses enemies so their melee hits wound them as well", 20, 8, TW::None, 1, Curse, Flat, 0, 2, 200, 0, 0, DT::Physical, MissileID::Null, MG::BloodHit, TF_Spell, TableZone::None, TableCurse::IronMaiden, TableBuff::None, TT_Occult, Icon(SpellIcon::Reflect), TableCurve::Boneshatter, 100 },
	{ WS::Decrepify, "Decrepify", "Curses enemies to act 30% slower (rares 20%, uniques 10%)", 7, 10, TW::None, 1, Curse, Flat, 0, 2, 160, 0, 0, DT::Physical, MissileID::Null, MG::BloodHit, TF_Spell, TableZone::None, TableCurse::Decrepify, TableBuff::None, TT_Occult, Icon(SpellIcon::StoneCurse), TableCurve::Boneshatter, 100 },
	{ WS::LowerResist, "Lower Resist", "Curses enemies with -30% to fire, lightning and cold resistance", 9, 10, TW::None, 1, Curse, Flat, 0, 2, 200, 0, 0, DT::Physical, MissileID::Null, MG::BloodHit, TF_Spell, TableZone::None, TableCurse::LowerResist, TableBuff::None, TT_Elemental, Icon(SpellIcon::Elemental), TableCurve::Boneshatter, 100 },
	{ WS::Confuse, "Confuse", "Curses enemies to attack each other for 6 seconds; from level 10 a rare or unique you aim at goes berserk", 11, 12, TW::None, 1, Curse, Flat, 0, 2, 120, 0, 0, DT::Physical, MissileID::Null, MG::BloodHit, TF_Spell, TableZone::None, TableCurse::Confuse, TableBuff::None, TT_Occult, Icon(SpellIcon::Jester), TableCurve::Boneshatter, 100 },
	{ WS::Attract, "Attract", "Curses an enemy to draw the attacks of the monsters near it for 3 seconds", 15, 14, TW::None, 1, Curse, Flat, 0, 5, 60, 0, 0, DT::Physical, MissileID::Null, MG::BloodHit, TF_Spell, TableZone::None, TableCurse::Attract, TableBuff::None, TT_Occult, Icon(SpellIcon::Telekinesis), TableCurve::Boneshatter, 100 },
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
	{ WS::Despair, "Despair", "Curses enemies with -30% to chaos resistance", 15, 10, TW::None, 1, Curse, Flat, 0, 2, 200, 0, 0, DT::Physical, MissileID::Null, MG::BloodHit, TF_Spell, TableZone::None, TableCurse::Despair, TableBuff::None, TT_Occult, Icon(SpellIcon::Apocalypse), TableCurve::Boneshatter, 100 },
	{ WS::PlagueArrow, "Plague Arrow", "An arrow that bursts into poison on the first enemy it hits", 1, 8, TW::Bow, 0, Projectiles, 90, 1, 1, 0, 0, 0, DT::Physical, MissileID::Arrow, MG::AcidSplat, TF_PoisonBurst, TableZone::None, TableCurse::None, TableBuff::None, TT_Bow, Icon(SpellIcon::DoomSerpents), TableCurve::LightningArrow, 0 },
	// Skill-balance Q29: 200% a burst, was 160%, and a monster takes one burst a cast (RuptureHexes).
	{ WS::HexRupture, "Hex Rupture", "Bursts the curses half run out on enemies around the target", 20, 12, TW::None, 1, HexBurst, 350, 0, 3, 0, 0, 0, DT::Chaos, MissileID::Null, MG::BloodHit, TF_Spell, TableZone::None, TableCurse::None, TableBuff::None, TT_Occult, Icon(SpellIcon::BloodBoil), TableCurve::Fireball, 312 },
	{ WS::BoneSpikes, "Bone Spikes", "Bones burst out of the ground in a line ahead of you, one spike wide and then two, staggering what they hit", 3, 8, TW::None, 1, Line, 80, 5, 0, 0, 0, 0, DT::Physical, MissileID::Null, MG::BoneHit, TF_Spell | TF_Stun, TableZone::None, TableCurse::None, TableBuff::None, TT_Occult, Icon(SpellIcon::BoneSpirit), TableCurve::Fireball, 312 },
	{ WS::BoneSpear, "Bone Spear", "A heavy spear of bone that flies through every enemy in its path and builds up twice the stun", 10, 12, TW::None, 1, Projectiles, 220, 1, 0, 0, 0, 0, DT::Physical, MissileID::OrangeFlare, MG::BoneHit, TF_Spell | TF_Pierce, TableZone::None, TableCurse::None, TableBuff::None, TT_Occult, Icon(SpellIcon::BoneSpirit), TableCurve::Fireball, 312, GrantedRider::StunBuildup },
	{ WS::BloodNova, "Blood Nova", "A ring of blood bursts out around you and leaves enemies bleeding; costs 5% of your life", 15, 10, TW::None, 1, Around, 110, 0, 2, 0, 0, 0, DT::Physical, MissileID::Null, MG::BloodHit, TF_Spell | TF_LifeCost | TF_Bleed, TableZone::None, TableCurse::None, TableBuff::None, TT_Occult, Icon(SpellIcon::BloodBoil), TableCurve::Fireball, 312 },
	// The poison skills plan (Curtis 2026-11-02, poison-skills-numbers.md): the Rogue's dagger and bow poisons, then the
	// Sorcerer's. Poison strength is the hit's percent a second for 2 seconds (TablePoisonPercentPerSecond).
	{ WS::PoisonDagger, "Poison Dagger", "Quick stabs with the heaviest poison", 1, 4, TW::Dagger, 0, Strike, 150, 0, 0, 0, 0, 0, DT::Physical, MissileID::Null, MG::AcidSplat, TF_Poison, TableZone::None, TableCurse::None, TableBuff::None, TT_Dagger, Icon(SpellIcon::DoomSerpents), TableCurve::Boneshatter, 0, GrantedRider::None, 100 },
	{ WS::AcidFlask, "Acid Flask", "Throws a flask that bursts into a pool of acid", 12, 15, TW::Dagger, 0, Zone, 70, 0, 1, 40, 10, 0, DT::Physical, MissileID::Null, MG::AcidSplat, TF_Lobbed | TF_Poison, TableZone::PoisonGround, TableCurse::None, TableBuff::None, TT_Dagger, Icon(SpellIcon::FireCloud), TableCurve::Boneshatter, 0, GrantedRider::None, 40 },
	{ WS::VenomSentry, "Venom Sentry", "Throws a gas trap that fills the area with poison when an enemy comes near", 20, 20, TW::Dagger, 0, Zone, 16, 0, 2, 80, 10, 0, DT::Physical, MissileID::Null, MG::AcidSplat, TF_Lobbed | TF_Poison, TableZone::GasTrap, TableCurse::None, TableBuff::None, TT_Dagger, Icon(SpellIcon::TrapDisarm), TableCurve::Boneshatter, 0, GrantedRider::None, 60 },
	{ WS::ThornvineArrow, "Thornvine Arrow", "An arrow that grows a thorny vine where it lands, poisoning and slowing enemies", 10, 12, TW::Bow, 0, Projectiles, 100, 1, 2, 80, 10, 0, DT::Physical, MissileID::Arrow, MG::AcidSplat, TF_LandingZone, TableZone::PoisonGround, TableCurse::None, TableBuff::None, TT_Bow, Icon(SpellIcon::Infravision), TableCurve::LightningArrow, 0 },
	{ WS::BloatedPods, "Bloated Pods", "Shoots three seed pods that burst into poison when enemies come near", 15, 18, TW::Bow, 0, Zone, 75, 3, 1, 120, 0, 0, DT::Physical, MissileID::Null, MG::AcidSplat, TF_Lobbed | TF_Poison, TableZone::Pod, TableCurse::None, TableBuff::None, TT_Bow, Icon(SpellIcon::PentaStar), TableCurve::LightningArrow, 0, GrantedRider::None, 40 },
	{ WS::PlagueEffigy, "Plague Effigy", "Raises an effigy that shoots the enemies you have poisoned for 8 seconds", 20, 15, TW::Bow, 0, Zone, 85, 0, 6, 160, 10, 0, DT::Physical, MissileID::Arrow, MG::AcidSplat, TF_PoisonedOnly, TableZone::Turret, TableCurse::None, TableBuff::None, TT_Bow, Icon(SpellIcon::RedSkull), TableCurve::LightningArrow, 0 },
	{ WS::WitchsBog, "Witch's Bog", "A bog around you slows enemies, and your arrows that cross it poison", 7, 10, TW::Bow, 1, Zone, 0, 0, 2, 160, 10, 0, DT::Physical, MissileID::Null, MG::AcidSplat, TF_AtPlayer, TableZone::Bog, TableCurse::None, TableBuff::None, TT_Bow, Icon(SpellIcon::BloodRitual), TableCurve::LightningArrow, 0, GrantedRider::None, 30 },
	{ WS::VenomBolt, "Venom Bolt", "A bolt of venom whose poison eats at the enemy", 1, 6, TW::None, 1, Projectiles, 130, 1, 0, 0, 0, 0, DT::Chaos, MissileID::OrangeFlare, MG::AcidSplat, TF_Spell | TF_Poison, TableZone::None, TableCurse::None, TableBuff::None, TT_Occult, Icon(SpellIcon::Invisibility), TableCurve::Fireball, 312, GrantedRider::None, 60 },
	{ WS::PoisonExplosion, "Poison Explosion", "Blows up a corpse near the target into a cloud of poison", 8, 12, TW::None, 1, CorpseBurst, 30, 0, 2, 40, 10, 0, DT::Chaos, MissileID::Null, MG::AcidSplat, TF_Spell | TF_Poison, TableZone::PoisonGround, TableCurse::None, TableBuff::None, TT_Occult, Icon(SpellIcon::Immolation), TableCurve::Fireball, 312, GrantedRider::None, 40 },
	{ WS::PoisonNova, "Poison Nova", "A ring of poison bursts out around you", 22, 25, TW::None, 1, Around, 60, 0, 3, 0, 0, 0, DT::Chaos, MissileID::Null, MG::AcidSplat, TF_Spell | TF_Poison, TableZone::None, TableCurse::None, TableBuff::None, TT_Occult, Icon(SpellIcon::Nova), TableCurve::Fireball, 312, GrantedRider::None, 60 },
	{ WS::AcidCloud, "Acid Cloud", "Acid rains on the target and leaves a pool that poisons", 25, 28, TW::None, 1, Zone, 95, 0, 2, 60, 10, 8, DT::Chaos, MissileID::Null, MG::AcidSplat, TF_Spell | TF_Poison, TableZone::PoisonGround, TableCurse::None, TableBuff::None, TT_Occult, Icon(SpellIcon::FireCloud), TableCurve::Fireball, 312, GrantedRider::None, 40 },
};
// clang-format on

/** @brief Melee hits from Weaken and the player's defensive buffs land this much softer, in percent. */
constexpr int WeakenPercent = 30;
constexpr int FrozenArmorPercent = 20;
constexpr int HolyShieldPercent = 15;
/**
 * Thunderstorm, Blessed Hammer and Blizzard out at once: one cast, and a recast replaces it (Curtis 2026-10-28; recast
 * as their cast time allowed, Thunderstorm reached 11 at once).
 */
constexpr int MaxStackingZones = 1;
constexpr int DeathMarkPercent = 25;
constexpr int SoulDamagePercent = 3;
constexpr int TicksPerSecond = 20;
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
	/** The key of its effects (vfx::EndKeyed), or 0. */
	uint32_t key = 0;
	/** Poisoned ground that came without a burst of its own (Thornvine Arrow's vine, Poison Explosion's cloud). */
	bool cloud = false;
	/** Venom Sentry's trap went off: it puffs gas until its time runs out. */
	bool armed = false;
	/** Falling Sword's holy ground: it burns what stands on it once a second. */
	bool holyGround = false;
	/** An Explosive Arrow burst the poison cloud in fire: it ends now. */
	bool detonated = false;
};

std::vector<SkillZone> Zones;

/** @brief Shield Wall's segments of earth (Curtis 2026-11-07, Q37): the local hero's, like the zones. */
struct WallSegment {
	Point tile;
	int32_t life;
	int16_t ticksLeft;
	uint32_t key;
};
std::vector<WallSegment> WallSegments;

/** @brief An Explosive Arrow's burst set off a poison cloud: it bursts in fire after the zones are done this tick. */
struct Detonation {
	Point center;
	int radius;
	Fixed26_6 damage;
};
std::vector<Detonation> PendingDetonations;

/** @brief Wave of Light's bell (Curtis 2026-11-07, Q38): one at a time, the local hero's, like the zones. */
struct WaveBellState {
	Point tile;
	int16_t ticksLeft;
	uint8_t cooldown;
	uint32_t key;
};
std::optional<WaveBellState> WaveBell;
/** @brief The effects' key for the next zone; effects only, so it needn't match on other machines. */
uint32_t NextZoneKey = 0;

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
	const Fixed26_6 dealt = Fixed26_6::fromRaw(static_cast<int32_t>(MonsterHitDamage(player, monster, type, damage.raw())));
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
	NoteSupportedBleed(player, monster);
}

/** @brief Haunt's and Decompose's chaos damage over time: the strongest counts (Locust Swarm's is a poison now). */
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

/** @brief A table skill's poison of a hit of this much: its strength a second for PoisonTicks, as a raw Fixed26_6. */
int32_t TablePoisonTotal(Fixed26_6 hit, const TableSkillData &data)
{
	return static_cast<int32_t>(std::min<int64_t>(static_cast<int64_t>(hit.raw()) * TablePoisonPercentPerSecond(data) * PoisonTicks / 20 / 100, INT32_MAX / 2));
}

/** @brief Thornvine Arrow's vine and Witch's Bog slow what's in them: a short hinder, renewed while it stays; a stronger one stays. */
void HinderMonster(Monster &monster, int percent)
{
	Poe2MonsterState &state = monster.poe2;
	const int current = state.hinderTicks > 0 ? (state.hinderPercent != 0 ? state.hinderPercent : HinderPercent) : 0;
	if (percent < current)
		return;
	state.hinderPercent = static_cast<uint8_t>(percent == HinderPercent ? 0 : std::clamp<int>(percent, 1, 99));
	state.hinderTicks = std::max<uint8_t>(state.hinderTicks, static_cast<uint8_t>(PoisonGroundHinderTicks));
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

/**
 * @brief Cobra Strike (Curtis 2026-11-07, Q38): the poison on the monster deals all it has left at once, as chaos, and
 * ends, before the strike's own poison goes on. Only another skill's poison: its own stays (Q42), so Poison Dagger or
 * another poisoner sets it up.
 */
void ConsumePoison(const Player &player, Monster &monster, const TableSkillData &data)
{
	Poe2MonsterState &state = monster.poe2;
	if (state.poisonTicks == 0 || state.poisonDamagePerTick <= 0 || state.poisonFromCobra)
		return;
	const int64_t left = static_cast<int64_t>(state.poisonDamagePerTick) * state.poisonTicks;
	state.poisonTicks = 0;
	state.poisonDamagePerTick = 0;
	state.poisonPlague = false;
	state.poisonVsBleed = false;
	AddVisualEffect(monster.position.tile, MissileGraphicID::Acid);
	vfx::Impact(monster.position.tile, vfx::Element::Poison, 1.2F);
	SpellHitMonster(player, monster, DamageType::Chaos, Fixed26_6::fromRaw(static_cast<int32_t>(std::clamp<int64_t>(left, 1, INT32_MAX / 2))), SourceOf(data));
}

/** @brief A weapon hit from a table skill, with the skill's extras. */
void WeaponHit(Player &player, Monster &monster, const TableSkillData &data, int damagePercent, CastResult &result)
{
	// Death Mark (Q38): Cobra Strike uses up the mark for double; a miss leaves it.
	const uint8_t mark = data.skill == WeaponSkill::ViperStrike ? monster.poe2.deathMarkTicks : 0;
	if (mark > 0)
		damagePercent = damagePercent * ConsumeDeathMark(monster) / 100;
	if (!SkillHitMonster(player, monster, damagePercent)) {
		if (mark > 0)
			monster.poe2.deathMarkTicks = mark;
		return;
	}
	result.hit = true;
	// Rend (Q38): Rage for each monster it hits.
	if (data.skill == WeaponSkill::Rend)
		AddRage(player, RendRagePerHit);
	if (data.skill == WeaponSkill::ViperStrike && !monster.hasNoLife() && monster.mode != MonsterMode::Death)
		ConsumePoison(player, monster, data);
	const bool alive = !monster.hasNoLife() && monster.mode != MonsterMode::Death;
	if ((data.flags & TF_Stun) != 0 && alive)
		HeavyStunMonster(monster);
	if ((data.flags & TF_Knockback) != 0 && alive && !monster.isUnique() && monster.mode != MonsterMode::Petrified)
		M_GetKnockback(monster, player.position.tile);
	if ((data.flags & TF_ArmourBreak) != 0 && alive)
		BreakMonsterArmour(monster, &player);
	if ((data.flags & TF_Bleed) != 0 && alive)
		InflictTableBleed(player, monster, data.skill, FixedDamage(player, data), 80);
	// Cobra Strike and Poison Dagger: POE2's poison for 2 seconds, of the row's strength (Cobra's 60% of the hit a second;
	// Poison Dagger's 100%). One poison each: only supports (Twin Fangs) add stacks (Curtis 2026-11-03).
	if ((data.flags & TF_Poison) != 0 && alive && InflictPoison(player, monster, TablePoisonTotal(FixedDamage(player, data), data), PoisonTicks))
		monster.poe2.poisonFromCobra = data.skill == WeaponSkill::ViperStrike;
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

void EndWaveBell()
{
	if (WaveBell)
		vfx::EndKeyed(WaveBell->key);
	WaveBell.reset();
}

/** @brief Wave of Light's weapon hit at the percent on every monster within the radius of the tile. */
void WaveBellHit(Player &player, const TableSkillData &wave, Point tile, int radius, int percent)
{
	TableSkillData hit = wave;
	hit.flags = static_cast<uint32_t>(hit.flags & ~TF_Combo);
	std::vector<Monster *> targets;
	ForEachMonsterNear(tile, radius, [&](Monster &monster) { targets.push_back(&monster); });
	CastResult result;
	for (Monster *monster : targets)
		WeaponHit(player, *monster, hit, percent, result);
}

/**
 * @brief Wave of Light at full Combo (Curtis 2026-11-07, Q38): a giant bell of light drops on the tile in front of the
 * hero, a WaveBellPercent hit in WaveBellRadius, and stands for WaveBellTicks. One bell at a time.
 */
void DropWaveBell(Player &player, const TableSkillData &wave, Point tile)
{
	if (&player != MyPlayer)
		return;
	EndWaveBell();
	NextZoneKey = NextZoneKey == UINT32_MAX ? 1 : NextZoneKey + 1;
	// Its own landing doesn't toll it.
	WaveBell = WaveBellState { tile, static_cast<int16_t>(WaveBellTicks), static_cast<uint8_t>(WaveBellTollCooldown), NextZoneKey };
	vfx::Bell(tile, WaveBellTicks * gnTickDelay, NextZoneKey);
	vfx::Slam(tile, WaveBellRadius, vfx::Element::Holy);
	EffectRingAround(tile, WaveBellRadius, MissileGraphicID::HolyBoltExplosion, 1);
	ShakeCamera(ShakeStrength::Medium, 6);
	BreakObjectsInArea(player, tile, WaveBellRadius);
	WaveBellHit(player, wave, tile, WaveBellRadius, WaveBellPercent);
}

/** @brief The bell's time runs out, and it may toll again. */
void ProcessWaveBell(const Player &player)
{
	if (!WaveBell)
		return;
	if (player.hasNoLife() || leveltype == DTYPE_TOWN || --WaveBell->ticksLeft <= 0) {
		EndWaveBell();
		return;
	}
	if (WaveBell->cooldown > 0)
		WaveBell->cooldown--;
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

/** @brief Plague Effigy's target: the nearest monster near the spot that the hero's poison is on. */
Monster *PoisonedMonsterNear(const Player &player, Point target, int radius)
{
	Monster *best = nullptr;
	int bestDistance = radius + 1;
	ForEachMonsterNear(target, radius, [&](Monster &monster) {
		const int distance = target.WalkingDistance(monster.position.tile);
		if (distance < bestDistance && PoisonedBy(monster, player)) {
			best = &monster;
			bestDistance = distance;
		}
	});
	return best;
}

/** @brief How long a thrown flask, canister or pod takes to land, in game ticks. */
int LobTicks(Point from, Point to)
{
	return std::clamp<int>(4 + from.WalkingDistance(to), 6, 14);
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
	case MissileGraphicID::AcidSplat:
		// Venom spat like the acid beasts' spit.
		return Sfx(SfxID::SpellAcid);
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
	case MissileGraphicID::AcidSplat:
		// Poison splashes as the acid beasts' spit does.
		return Sfx(SfxID::SpellAcid1);
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
	return data.shape == Zone && (IsAnyOf(data.zone, TableZone::Delayed, TableZone::IceSpike, TableZone::ShardRing) || (data.flags & TF_Lobbed) != 0);
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

/** @brief How many zones of a skill can be out at once; the oldest goes. 0 for no limit. */
int ZoneLimit(const Player &player, const TableSkillData &data)
{
	const SkillSlot slot = SkillSlot::ForAttack(data.skill);
	switch (data.skill) {
	// Thunderstorm, Blessed Hammer and Blizzard one; the poison skills plan's numbers for the rest.
	case WeaponSkill::Thunderstorm:
	case WeaponSkill::BlessedHammer:
	case WeaponSkill::Blizzard:
	case WeaponSkill::ThornvineArrow:
	case WeaponSkill::WitchsBog:
	case WeaponSkill::PlagueEffigy:
		return SupportZoneLimit(player, slot, MaxStackingZones);
	case WeaponSkill::AcidFlask:
		return SupportZoneLimit(player, slot, AcidFlaskPools);
	case WeaponSkill::AcidCloud:
		return SupportZoneLimit(player, slot, AcidCloudPools);
	default:
		break;
	}
	// Traps, sentries and pods: only a few of each (MaxPlacedTraps, 2 more with Endless Horde, Curtis 2026-10-29).
	if (IsAnyOf(data.zone, TableZone::Trap, TableZone::Turret, TableZone::GasTrap, TableZone::Pod))
		return SupportTrapLimit(player, slot);
	return 0;
}

vfx::LobKind LobKindOf(const TableSkillData &data)
{
	switch (data.zone) {
	case TableZone::GasTrap: return vfx::LobKind::Canister;
	case TableZone::Pod: return vfx::LobKind::Seed;
	default: return vfx::LobKind::Flask;
	}
}

void AddZone(const Player &player, const TableSkillData &data, Point center, int delay, bool cloud = false)
{
	SkillZone zone;
	NextZoneKey = NextZoneKey == UINT32_MAX ? 1 : NextZoneKey + 1;
	zone.key = NextZoneKey;
	zone.cloud = cloud;
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
	// Traps and sentries: only a few of each can be out at once; Thunderstorm, Blessed Hammer and Blizzard one. The
	// oldest goes, and its effects with it.
	if (const int limit = ZoneLimit(player, data); limit > 0) {
		while (std::count_if(Zones.begin(), Zones.end(), [&](const SkillZone &other) { return other.skill == data.skill; }) >= limit) {
			const auto oldest = std::find_if(Zones.begin(), Zones.end(), [&](const SkillZone &other) { return other.skill == data.skill; });
			vfx::EndKeyed(oldest->key);
			Zones.erase(oldest);
		}
	}
	Zones.push_back(zone);
	// The poison skills' effects that start with the cast: a throw in the air, the effigy, the bog, the gathering acid.
	if ((data.flags & TF_Lobbed) != 0)
		vfx::Lob(player.position.tile, center, delay * gnTickDelay, LobKindOf(data));
	else if (data.zone == TableZone::Turret && (data.flags & TF_PoisonedOnly) != 0)
		vfx::Effigy(center, zone.ticksLeft * gnTickDelay, zone.key);
	else if (data.zone == TableZone::Bog)
		vfx::Miasma(center, std::max<int>(data.radius, 1), zone.ticksLeft * gnTickDelay, vfx::MiasmaKind::Bog, zone.key);
	else if (data.zone == TableZone::PoisonGround && delay > 0 && (data.flags & TF_Spell) != 0)
		vfx::AcidRain(center, std::max<int>(data.radius, 1), delay * gnTickDelay);
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
	// The poison skills' bursts and novas poison what they hit, of the row's strength.
	if ((data.flags & TF_Poison) != 0)
		InflictPoison(player, monster, TablePoisonTotal(hit, data), PoisonTicks);
}

/**
 * @brief A burst of a zone, a landing or an area spell: hits everything within the radius. A staff's granted skill priced
 * per target (TF_PerTarget) hits the monster nearest the aim in full and the others for GrantedExtraTargetPercent.
 */
void ZoneBurst(const Player &player, const TableSkillData &data, Point center, int radius, Fixed26_6 damage, std::optional<Point> aim = std::nullopt)
{
	// Fire Arrow comes down in a little burst of flame; wider landing bursts keep their explosion (without the particle
	// effects, the sprite is all there is to show).
	if (TableLandingBurstIsSmall(data) && vfx::Enabled())
		vfx::Impact(center, LookOf(data), SmallLandingBurstSize);
	else
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
		Fixed26_6 hit = extraPercent < 100 && &monster != primary ? extra : damage;
		// Glacial Spike (Curtis 2026-11-07, Q38): a frozen monster takes double and its freeze shatters.
		const bool shatters = data.skill == WeaponSkill::GlacialSpike && monster.poe2.freezeTicks > 0;
		if (shatters)
			hit = Fixed26_6::fromRaw(static_cast<int32_t>(std::min<int64_t>(static_cast<int64_t>(hit.raw()) * GlacialSpikeFrozenPercent / 100, INT32_MAX / 2)));
		AreaSpellHit(player, data, monster, hit);
		if (shatters) {
			monster.poe2.freezeTicks = 0;
			AddVisualEffect(monster.position.tile, MissileGraphicID::StoneCurseShatter);
			vfx::Impact(monster.position.tile, vfx::Element::Cold, 1.3F);
		}
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
	// A heavy green splash of venom on the enemy struck, rolling out over the burst (Curtis, Rogue run 2: it should read
	// as a poison arrow, not a purple chaos puff). The acid beasts' splat sprite only loads with them, so the effects
	// draw it when they can.
	if (vfx::Enabled()) {
		vfx::Impact(center, vfx::Element::Poison, PlagueArrowSplashSize);
		vfx::Nova(center, std::max(radius, 1), vfx::Element::Poison);
	} else {
		AddVisualEffect(center, data.effect, 0, 4);
	}
	PlayTableSkillHitSound(data, center);
	const Fixed26_6 base = FixedDamage(player, data);
	const int32_t poison = static_cast<int32_t>(std::min<int64_t>(static_cast<int64_t>(base.raw()) * TablePoisonPercentPerSecond(data) * PoisonTicks / 20 / 100, INT32_MAX / 2));
	const Fixed26_6 splash = Fixed26_6::fromRaw(std::max<int32_t>(static_cast<int32_t>(static_cast<int64_t>(base.raw()) * PlagueArrowSplashPercent / 100), 1));
	std::vector<Monster *> targets;
	ForEachMonsterNear(center, radius, [&](Monster &monster) { targets.push_back(&monster); });
	for (Monster *monster : targets) {
		InflictPoison(player, *monster, poison, PoisonTicks);
		if (monster != &struck)
			SpellHitMonster(player, *monster, DamageType::Physical, splash, SourceOf(data));
	}
}

/** @brief A projectile's landing burst: a set share of a base hit (Freezing Arrow, Explosive Arrow, Fire Arrow), or two thirds of the projectile's hit. */
Fixed26_6 LandingBurstDamage(const Player &player, const TableSkillData &data)
{
	if (IsAnyOf(data.skill, WeaponSkill::FreezingArrow, WeaponSkill::ExplosiveBolt, WeaponSkill::ClusterBolt)) {
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
		if (data.type == DamageType::Physical || LookOf(data) == vfx::Element::Poison)
			missile->poe2.vfxElement = static_cast<int8_t>(LookOf(data));
	}
	// Plague Arrow is a poison arrow: drawn green, dripping venom as it flies (Curtis, Rogue run 2).
	if (!spell && (data.flags & TF_PoisonBurst) != 0)
		missile->poe2.vfxElement = static_cast<int8_t>(vfx::Element::Poison);
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
	// Guided Arrow homes in on the monster it was shot at (Diablo 2; Curtis 2026-11-07, Q37).
	if (data.skill == WeaponSkill::GuidedArrow) {
		if (const Monster *aimed = TargetMonsterNear(target, 1); aimed != nullptr)
			missile->poe2.homingMonster = static_cast<int16_t>(aimed->getId());
	}
	// Thrown blades and axes spin as they fly (Curtis 2026-11-07, Q39).
	if (IsThrowingSkill(data.skill))
		missile->poe2.look = vfx::SpinningBladeLook;
	// Blessed Shield: the thrown shield spins as it flies, and bounces on (OnTableMissileHit).
	if (data.skill == WeaponSkill::BlessedShield) {
		missile->poe2.look = vfx::SpinningBladeLook;
		missile->poe2.chainsLeft = static_cast<int8_t>(BlessedShieldBounces);
	}
	if ((data.flags & TF_LandingBurst) != 0) {
		// The burst goes off about when the projectile gets there; Explosive Arrow's sticks and goes off a second later.
		const int flight = std::clamp(from.WalkingDistance(target) * 2, 2, 30);
		const int ticks = flight + (data.skill == WeaponSkill::ExplosiveBolt ? ExplosiveArrowFuseTicks : 0);
		SkillZone zone { data.skill, target, 1, static_cast<int16_t>(ticks), 0, LandingBurstDamage(player, data), static_cast<int8_t>(std::min<int>(data.radius, INT8_MAX)) };
		Zones.push_back(zone);
		// Its fuse: a ring of fire glows on the ground where the arrow stuck, charging until it blows.
		if (data.skill == WeaponSkill::ExplosiveBolt)
			vfx::Sigil(target, std::max<int>(data.radius, 1), ticks * gnTickDelay, vfx::Element::Fire);
	}
	// Thornvine Arrow: the vine grows where the arrow comes down, poisoning with the arrow's hit.
	if ((data.flags & TF_LandingZone) != 0)
		AddZone(player, data, LastTileInSight(from, target), std::clamp<int>(from.WalkingDistance(target) * 2, 2, 30), true);
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
		// Only the slow, like POE2's Temporal Chains (Curtis 2026-11-07, Q36). Its chill is a curse's, which the Sickness
		// Road can stop.
		if (AilmentAllowed(player, AilmentSource::Spell)) {
			state.chillTicks = ticks;
			state.chillPercent = static_cast<uint8_t>(std::max<int>(state.chillPercent, DecrepifySlowPercentFor(monster)));
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
		if (monster.isUnique())
			break;
		if ((monster.flags & MFLAG_BERSERK) == 0) {
			monster.flags |= MFLAG_BERSERK | MFLAG_GOLEM;
			state.confusedByCurse = true;
		}
		state.confuseTicks = ticks;
		break;
	case TableCurse::Attract:
		// Diablo 2's Attract (Curtis 2026-11-07, Q36): the monster becomes a decoy for the monsters near it (UpdateEnemy).
		state.attractTicks = ticks;
		state.attractSeed = static_cast<uint8_t>(GenerateRnd(256));
		break;
	case TableCurse::Haunt:
		state.hauntTicks = ticks;
		InflictTableChaos(player, monster, data.skill, FixedDamage(player, data), ticks);
		break;
	case TableCurse::Locust:
		// The poison skills plan: its plague is a poison now, with no hit of its own, so Deadly Venom has nothing to add.
		state.locustTicks = ticks;
		InflictPoison(player, monster, FixedDamage(player, data).raw(), ticks, 1, false);
		break;
	default:
		break;
	}
	// Rathma's Doom and Festering Hex: a curse cast by hand bursts as it runs out, or poisons while it lasts.
	if (CurseTicks(state, data.curse) != nullptr) {
		const SkillSlot slot = SkillSlot::ForAttack(data.skill);
		const uint16_t bit = AuraCurseBit(data.curse);
		const bool doom = player.poe2.hasSupport(slot, SupportGem::RathmasDoom);
		const bool fester = player.poe2.hasSupport(slot, SupportGem::FesteringHex);
		state.doomCurses = static_cast<uint16_t>(doom ? state.doomCurses | bit : state.doomCurses & ~bit);
		state.festerCurses = static_cast<uint16_t>(fester ? state.festerCurses | bit : state.festerCurses & ~bit);
		if (doom || fester) {
			const int gemLevel = GemLevel(player, data.skill);
			const int64_t base = static_cast<int64_t>(SpellBase(player, gemLevel)) * 64 * FireballCurvePercent(312, gemLevel) / 100;
			state.curseSupportSource = static_cast<int8_t>(player.getId());
			if (doom)
				state.doomDamage = static_cast<int32_t>(std::min<int64_t>(base * RathmasDoomPercent / 100, INT32_MAX / 2));
			if (fester) {
				state.festerPoison = static_cast<int32_t>(std::min<int64_t>(base * FesteringHexPercent * PoisonTicks / 20 / 100, INT32_MAX / 2));
				state.festerTicks = 0;
			}
		}
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

/**
 * @brief Hex Rupture: each curse at least half run out near the target bursts in chaos on its monster and is gone. A
 * monster takes one burst a cast, however many reach it (skill-balance Q29).
 */
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
	std::vector<const Monster *> struck;
	for (const auto &[monster, curse] : hexes) {
		const Point at = monster->position.tile;
		vfx::Impact(at, vfx::Element::Chaos, 1.1F);
		AddVisualEffect(at, data.effect, 0, 4);
		BreakObjectsInArea(player, at, HexRuptureBurstRadius);
		std::vector<Monster *> fresh;
		ForEachMonsterNear(at, HexRuptureBurstRadius, [&](Monster &other) {
			if (std::find(struck.begin(), struck.end(), &other) == struck.end())
				fresh.push_back(&other);
		});
		for (Monster *other : fresh) {
			struck.push_back(other);
			AreaSpellHit(player, data, *other, damage);
		}
		// The curse goes with its burst: its icon leaves the banner. Burst, it doesn't run out: no doom.
		*CurseTicks(monster->poe2, curse) = 0;
		monster->poe2.doomCurses &= static_cast<uint16_t>(~AuraCurseBit(curse));
		monster->poe2.festerCurses &= static_cast<uint16_t>(~AuraCurseBit(curse));
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

/** @brief Where Falling Sword's hero comes out of the light: the target tile, or the free tile nearest it. */
std::optional<Point> FallingSwordLanding(const Player &player, Point target)
{
	return FindClosestValidPosition([&player](Point tile) { return InDungeonBounds(tile) && PosOkPlayer(player, tile); }, target, 0, 2);
}

/** @brief Falling Sword's holy ground: radius 2 for 3 seconds, burning for 20% of the hit a second (Q37). */
void AddHolyGround(const Player &player, const TableSkillData &data, Point center)
{
	SkillZone zone;
	NextZoneKey = NextZoneKey == UINT32_MAX ? 1 : NextZoneKey + 1;
	zone.key = NextZoneKey;
	zone.skill = data.skill;
	zone.center = center;
	zone.ticksLeft = static_cast<int16_t>(FallingSwordGroundTicks);
	zone.delay = 0;
	zone.counter = 0;
	zone.damage = Fixed26_6::fromRaw(std::max<int32_t>(static_cast<int32_t>(static_cast<int64_t>(FixedDamage(player, data).raw()) * FallingSwordGroundPercent / 100), 1));
	zone.radius = static_cast<int8_t>(std::min<int>(FallingSwordGroundRadius, INT8_MAX));
	zone.holyGround = true;
	Zones.push_back(zone);
	vfx::Sigil(center, FallingSwordGroundRadius, FallingSwordGroundTicks * gnTickDelay, vfx::Element::Holy);
	EffectRingAround(center, FallingSwordGroundRadius, MissileGraphicID::HolyBoltExplosion, 2);
}

/** @brief Whether Explosive Arrow sets this zone's poison off: the poison clouds and grounds of the poison skills (Q37). */
bool IsDetonatablePoison(WeaponSkill skill)
{
	return IsAnyOf(skill, WeaponSkill::BloatedPods, WeaponSkill::ThornvineArrow, WeaponSkill::WitchsBog, WeaponSkill::AcidFlask, WeaponSkill::VenomSentry);
}

/** @brief Explosive Arrow's blast touched these poison zones: each bursts in fire for the blast's damage in its own area and ends. */
void DetonatePoisonClouds(Point center, int radius, Fixed26_6 damage)
{
	for (SkillZone &zone : Zones) {
		if (zone.detonated || !IsDetonatablePoison(zone.skill) || zone.delay > 0)
			continue;
		if (center.WalkingDistance(zone.center) > radius + std::max<int>(zone.radius, 1))
			continue;
		zone.detonated = true;
		PendingDetonations.push_back({ zone.center, std::max<int>(zone.radius, 1), damage });
	}
}

void RemoveWallSegment(size_t index)
{
	vfx::EndKeyed(WallSegments[index].key);
	WallSegments.erase(WallSegments.begin() + static_cast<std::ptrdiff_t>(index));
}

/** @brief Shield Wall: 5 segments of earth in a line across in front of the hero, each with 20% of the hero's life. */
void RaiseShieldWall(const Player &player, const TableSkillData &data)
{
	// The wall is the local hero's, like the zones; one wall at a time.
	if (&player != MyPlayer)
		return;
	while (!WallSegments.empty())
		RemoveWallSegment(WallSegments.size() - 1);
	const Direction dir = player._pdir;
	const Point front = player.position.tile + Displacement { dir } * 2;
	const Displacement across { Left(Left(dir)) };
	const int32_t life = std::max<int32_t>(static_cast<int32_t>(static_cast<int64_t>(player._pMaxHP.raw()) * ShieldWallLifePercent / 100), 64);
	const int ticks = std::max<int>(data.duration, 1);
	for (int i = -(ShieldWallSegments / 2); i <= ShieldWallSegments / 2; i++) {
		const Point tile = front + across * i;
		if (!InDungeonBounds(tile) || !IsTileWalkable(tile) || dPlayer[tile.x][tile.y] != 0)
			continue;
		NextZoneKey = NextZoneKey == UINT32_MAX ? 1 : NextZoneKey + 1;
		WallSegments.push_back({ tile, life, static_cast<int16_t>(std::min(ticks, static_cast<int>(INT16_MAX))), NextZoneKey });
		vfx::EarthWall(tile, ticks * gnTickDelay, NextZoneKey);
		AddVisualEffect(tile, MissileGraphicID::StoneCurseShatter);
	}
	vfx::Fissure(front - across * (ShieldWallSegments / 2), front + across * (ShieldWallSegments / 2), vfx::Element::Physical);
	ShakeCamera(ShakeStrength::Light, 4);
}

/**
 * @brief The wall's segments wear out, and the monsters next to one break it: each hits it once a second for its blow
 * (they must break a segment to pass, see IsShieldWallTile).
 */
void ProcessShieldWall(const Player &player)
{
	if (WallSegments.empty())
		return;
	if (player.hasNoLife() || leveltype == DTYPE_TOWN) {
		while (!WallSegments.empty())
			RemoveWallSegment(WallSegments.size() - 1);
		return;
	}
	for (size_t i = 0; i < WallSegments.size();) {
		WallSegment &segment = WallSegments[i];
		if (--segment.ticksLeft <= 0) {
			AddVisualEffect(segment.tile, MissileGraphicID::StoneCurseShatter);
			RemoveWallSegment(i);
			continue;
		}
		if (segment.ticksLeft % TicksPerSecond == 0) {
			for (int d = 0; d < 8; d++) {
				const Point at = segment.tile + static_cast<Direction>(d);
				Monster *monster = FindMonsterAtPosition(at);
				if (monster == nullptr || monster->isPlayerMinion() || !CanHit(*monster) || monster->mode == MonsterMode::Petrified || monster->position.tile != at)
					continue;
				segment.life -= std::max(ScaleMonsterBlow(*monster, (monster->minDamage + monster->maxDamage) * 32), 64);
				AddVisualEffect(segment.tile, MissileGraphicID::MetalHit);
			}
		}
		if (segment.life <= 0) {
			AddVisualEffect(segment.tile, MissileGraphicID::StoneCurseShatter);
			PlaySfxLoc(SfxID::BarrelBreak, segment.tile);
			RemoveWallSegment(i);
			continue;
		}
		i++;
	}
}

} // namespace

std::span<const TableSkillData> TableSkills()
{
	return Table;
}

int TablePoisonPercentPerSecond(const TableSkillData &data)
{
	return data.poisonPercent > 0 ? data.poisonPercent : PoisonPercentPerSecond;
}

bool ArrowCrossedBog(const Player &player, Point from, Point to)
{
	// The zones are the local hero's own.
	if (!gbIsPoe2 || &player != MyPlayer)
		return false;
	std::vector<const SkillZone *> bogs;
	for (const SkillZone &zone : Zones) {
		const TableSkillData *data = FindTableSkill(zone.skill);
		if (data != nullptr && data->zone == TableZone::Bog && zone.delay == 0)
			bogs.push_back(&zone);
	}
	if (bogs.empty())
		return false;
	Point step = from;
	for (int guard = 0; guard < 64; guard++) {
		for (const SkillZone *bog : bogs) {
			if (step.ExactDistance(bog->center) <= bog->radius)
				return true;
		}
		if (step == to)
			break;
		step = step + GetDirection(step, to);
	}
	return false;
}

std::vector<Point> PodTiles(Point from, Point target, int count)
{
	std::vector<Point> tiles { target };
	const Displacement side { Left(Left(GetDirection(from, target))) };
	for (int i = 1; static_cast<int>(tiles.size()) < count && i <= 2 * count; i++) {
		const Point at = target + side * (PodSpreadTiles * ((i + 1) / 2) * (i % 2 == 1 ? 1 : -1));
		// A pod that would land in a wall lands on the target instead.
		tiles.push_back(InDungeonBounds(at) && !IsTileSolid(at) ? at : target);
	}
	return tiles;
}

bool PoisonedBy(const Monster &monster, const Player &player)
{
	return monster.poe2.poisonTicks > 0 && monster.poe2.poisonSource == static_cast<int8_t>(player.getId());
}

Displacement BoneSpikesSide(Direction dir)
{
	const Displacement left { Left(Left(dir)) };
	const Displacement right { Right(Right(dir)) };
	// Lower on the screen is a larger x + y; further right on it, a larger x - y.
	const int leftLow = left.deltaX + left.deltaY;
	const int rightLow = right.deltaX + right.deltaY;
	if (leftLow != rightLow)
		return leftLow > rightLow ? left : right;
	return left.deltaX - left.deltaY > right.deltaX - right.deltaY ? left : right;
}

std::vector<BoneSpikesTile> BoneSpikesTiles(Point from, Direction dir, int length)
{
	std::vector<BoneSpikesTile> tiles;
	const Displacement side = BoneSpikesSide(dir);
	const auto open = [](Point at) { return InDungeonBounds(at) && !IsTileSolid(at); };
	for (int step = 1; step <= length; step++) {
		const Point at = from + Displacement { dir } * step;
		if (!open(at))
			break;
		tiles.push_back({ at, step, false });
		if (step > BoneSpikesSingleTiles && open(at + side))
			tiles.push_back({ at + side, step, true });
	}
	return tiles;
}

int TableLandingBurstPercent(const TableSkillData &data)
{
	switch (data.skill) {
	case WeaponSkill::FreezingArrow:
		return FreezingArrowBurstPercent;
	case WeaponSkill::ExplosiveBolt:
		return ExplosiveBoltBurstPercent;
	case WeaponSkill::ClusterBolt:
		return data.damagePercent * FireArrowBurstShare / 100;
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
		state.chillPercent = static_cast<uint8_t>(std::max<int>(state.chillPercent, DecrepifySlowPercentFor(monster) * state.auraCurseStrength / 100));
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
	// Acid Flask's flask and Bloated Pods' pods fly as projectiles; Venom Sentry's canister is set down as a trap.
	return IsAnyOf(data.shape, Projectiles, Stream) || data.zone == TableZone::Turret || data.zone == TableZone::ShardRing
	    || ((data.flags & TF_Lobbed) != 0 && data.zone != TableZone::GasTrap);
}

bool IsTableAreaSkill(const TableSkillData &data)
{
	return IsAnyOf(data.shape, Around, Burst, Dash, Line, Zone, Curse, CorpseBurst, CallOfTheAncients, HexBurst) || (data.flags & (TF_LandingBurst | TF_PoisonBurst | TF_LandingZone)) != 0;
}

bool StartTableSkill(Player &player, const TableSkillData &data)
{
	// Checks that need the world; the caller pays for and starts the skill.
	// Sacrifice: the hero's skeletons count as corpses.
	if (data.shape == CorpseBurst && !CorpseNear(SkillTargetTile(player), data.radius) && SacrificeMinionNear(player, SkillTargetTile(player), data.radius) == nullptr) {
		EventPlrMsg(data.skill == WeaponSkill::PoisonExplosion ? _("Poison Explosion needs a corpse near the target.") : _("Corpse Explosion needs a corpse near the target."), UiFlags::ColorRed);
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

void BeginTableSkillCast(Player &player, const TableSkillData &data)
{
	if (data.skill != WeaponSkill::FallingSword)
		return;
	player.poe2.untouchableTicks = static_cast<uint8_t>(FallingSwordFadeTicks);
	AddVisualEffect(player.position.tile, MissileGraphicID::HolyBoltExplosion);
	vfx::Impact(player.position.tile, vfx::Element::Holy, 1.2F);
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
	// A slam or a war cry shatters the hero's Shield Wall near it (Curtis 2026-11-07, Q37).
	if ((SkillTags(slot) & (ST_Slam | ST_Warcry)) != 0)
		ShatterShieldWall(player, (SkillTags(slot) & ST_Warcry) != 0 ? tile : target);
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
		// Wave of Light (Q38): at full Combo a cast spends it all on a bell in front of the hero; below, it works as before.
		if (data.skill == WeaponSkill::WaveOfLight && player.poe2.combo >= MaxCombo && &player == MyPlayer) {
			player.poe2.combo = 0;
			player.poe2.comboTicks = 0;
			data.flags = static_cast<uint32_t>(data.flags & ~TF_Combo);
			DropWaveBell(player, data, tile + player._pdir);
			break;
		}
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
		// Dashing Strike and Whirlwind hit each monster along the dash once, not only those where it lands (Curtis
		// 2026-10-28; Whirlwind 2026-11-07, Q39).
		const bool alongPath = IsAnyOf(data.skill, WeaponSkill::DashingStrike, WeaponSkill::Whirlwind);
		const std::vector<Point> centers = alongPath ? DashPath(player, data.count) : std::vector<Point> { tile };
		if (alongPath)
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
		// Zeal (Curtis 2026-11-07, Q37): quick stabs at its percent, and every fourth use a heavy blow at 150% of that on
		// the three tiles in front, as Cleave sweeps (Q43). The count starts over 3 seconds after the last Zeal.
		if (data.skill == WeaponSkill::Zeal && perHit > 0) {
			const int percent = data.damagePercent * perHit / 100;
			player.poe2.zealTicks = static_cast<uint8_t>(ZealResetTicks);
			if (++player.poe2.zealCount >= ZealHeavyEvery) {
				player.poe2.zealCount = 0;
				for (const Direction dir : { player._pdir, Left(player._pdir), Right(player._pdir) }) {
					const Point at = tile + dir;
					BreakObjectAt(player, at);
					Monster *monster = FindMonsterAtPosition(at);
					if (monster != nullptr && CanHit(*monster) && monster->position.old == at)
						WeaponHit(player, *monster, data, percent * ZealHeavyPercent / 100, result);
				}
				vfx::Cleave(tile, tile + player._pdir, vfx::Element::Physical, false, WeaponElementLook(player));
				ShakeCamera(ShakeStrength::Light, 4);
			} else if (Monster *monster = MonsterInFrontOf(player); monster != nullptr) {
				WeaponHit(player, *monster, data, percent, result);
			}
			break;
		}
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
			// Falling Sword (Curtis 2026-11-07, Q37): the hero faded out as the cast began (TryUseWeaponSkill); a blade of
			// light falls on the target, and the hero comes back out of the light there.
			if (data.skill == WeaponSkill::FallingSword) {
				vfx::SkyStrike(target, vfx::Element::Holy);
				ShakeCamera(ShakeStrength::Medium, 6);
				if (const std::optional<Point> landing = FallingSwordLanding(player, target); landing && *landing != tile) {
					AddVisualEffect(tile, MissileGraphicID::HolyBoltExplosion);
					MovePlayerTo(player, *landing);
					player._pdir = GetDirection(player.position.tile, target);
				}
			}
			BreakObjectsInArea(player, target, data.radius);
			std::vector<Monster *> targets;
			ForEachMonsterNear(target, data.radius, [&](Monster &monster) { targets.push_back(&monster); });
			for (Monster *monster : targets)
				WeaponHit(player, *monster, data, data.damagePercent, result);
			// It leaves holy ground: 20% of the hit a second, physical, for 3 seconds.
			if (data.skill == WeaponSkill::FallingSword) {
				AddHolyGround(player, data, target);
				player.poe2.untouchableTicks = 0;
			}
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
		const auto take = [&hit](Monster &monster) {
			if (std::find(hit.begin(), hit.end(), &monster) == hit.end())
				hit.push_back(&monster);
		};
		if ((data.flags & TF_Spell) != 0) {
			// Bone Spikes: a clump of bone bursts up on each of its tiles as the line rolls out, one wide and then two, and
			// each enemy on it takes the spell's hit once.
			Point end = tile;
			for (const BoneSpikesTile &spike : BoneSpikesTiles(tile, dir, data.count)) {
				if (!spike.beside)
					end = spike.tile;
				AddVisualEffect(spike.tile, data.effect, spike.step * 2, spike.step == 1 ? 4 : 0);
				vfx::BoneSpikes(spike.tile, spike.step * 2 * gnTickDelay, 0.55F + 0.05F * static_cast<float>(spike.step));
				BreakObjectsInArea(player, spike.tile, data.radius);
				ForEachMonsterNear(spike.tile, data.radius, take);
			}
			if (end != tile) {
				vfx::Fissure(tile, end, LookOf(data));
				ShakeCamera(ShakeStrength::Light, 4);
			}
			const Fixed26_6 damage = FixedDamage(player, data);
			for (Monster *monster : hit) {
				AreaSpellHit(player, data, *monster, damage);
				result.hit = true;
			}
			break;
		}
		Point end = tile;
		for (int step = 1; step <= data.count; step++) {
			const Point at = tile + Displacement { dir } * step;
			if (!InDungeonBounds(at) || IsTileSolid(at))
				break;
			end = at;
			AddVisualEffect(at, data.effect, step * 2, step == 1 ? 4 : 0);
			BreakObjectsInArea(player, at, data.radius);
			ForEachMonsterNear(at, data.radius, take);
		}
		if (end != tile)
			vfx::Fissure(tile, end, LookOf(data));
		for (Monster *monster : hit)
			WeaponHit(player, *monster, data, data.damagePercent, result);
		break;
	}
	case Zone:
		if (data.zone == TableZone::IceSpike)
			target = IceSpikeCenter(player, data, target);
		// POE2 Mashup: a trap or sentry goes where the hero can see, never past a wall or a closed door (Curtis 2026-10-29).
		// So does what the hero throws.
		if ((IsAnyOf(data.zone, TableZone::Trap, TableZone::Turret) || (data.flags & TF_Lobbed) != 0) && (data.flags & TF_AtPlayer) == 0)
			target = LastTileInSight(tile, target);
		if (data.zone == TableZone::Pod) {
			// Bloated Pods: the pods land in a spread across the shot, one after another.
			int order = 0;
			for (const Point at : PodTiles(tile, target, data.count))
				AddZone(player, data, at, LobTicks(tile, at) + order++);
			break;
		}
		AddZone(player, data, (data.flags & TF_AtPlayer) != 0 ? tile : target, data.delay + ((data.flags & TF_Lobbed) != 0 ? LobTicks(tile, target) : 0));
		break;
	case Curse:
		if (data.radius == 0) {
			if (Monster *monster = TargetMonsterNear(target, 1); monster != nullptr)
				ApplyCurse(player, *monster, data);
		} else {
			ForEachMonsterNear(target, data.radius, [&](Monster &monster) { ApplyCurse(player, monster, data); });
		}
		// Berserk merged into Confuse (Curtis 2026-11-07, Q36): from gem level 10 the rare or unique it is aimed at goes
		// berserk, as Diablo's Berserk spell made a monster.
		if (data.curse == TableCurse::Confuse && GemLevel(player, data.skill) >= ConfuseBerserkGemLevel) {
			if (Monster *aimed = TargetMonsterNear(target, 1); aimed != nullptr && !aimed->isPlayerMinion() && aimed->type().type != MT_DIABLO
			    && (aimed->isUnique() || GetMonsterAffixes(*aimed).rarity == MonsterRarity::Rare)) {
				GiveConfuseBerserk(*aimed, data.duration);
				aimed->tag(player);
			}
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
			// POE2's wall of earth (Curtis 2026-11-07, Q37): no longer a defence buff.
			RaiseShieldWall(player, data);
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
	case CorpseBurst: {
		std::optional<Point> at;
		if (const std::optional<Point> corpse = CorpseNear(target, data.radius); corpse) {
			dCorpse[corpse->x][corpse->y] = 0;
			at = corpse;
		} else if (Monster *skeleton = SacrificeMinionNear(player, target, data.radius); skeleton != nullptr) {
			at = skeleton->position.tile;
			SacrificeMinion(*skeleton);
		}
		if (!at)
			break;
		ZoneBurst(player, data, *at, data.radius, FixedDamage(player, data));
		PlaySkillSound(TableSkillSounds(data).spell, *at);
		// Poison Explosion: the body bursts in green and leaves a cloud that poisons for 2 seconds.
		if (data.zone == TableZone::PoisonGround) {
			vfx::Impact(*at, vfx::Element::Poison, 1.6F);
			vfx::Nova(*at, std::max<int>(data.radius, 1), vfx::Element::Poison);
			if (&player == MyPlayer)
				ShakeCamera(ShakeStrength::Medium, 5);
			AddZone(player, data, *at, 0, true);
		}
		break;
	}
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

/**
 * @brief Blessed Shield's shield bounces from the monster it hit to the nearest other within BlessedShieldBounceTiles,
 * not back to the one before: the Arcing support's chaining. Every bounce hits for BlessedShieldBouncePercent of the throw.
 */
void BounceBlessedShield(const Missile &missile, const Monster &hit, const TableSkillData &data)
{
	Missile *bounce = ChainMissileOn(missile, hit, BlessedShieldBounceTiles);
	if (bounce == nullptr)
		return;
	if (missile.poe2.chainsLeft >= BlessedShieldBounces)
		bounce->poe2.damagePercent = static_cast<int16_t>(std::max(missile.poe2.damagePercent * BlessedShieldBouncePercent / 100, 1));
	AddVisualEffect(hit.position.tile, data.effect);
}

void OnTableMissileHit(const Missile &missile, Player &player, Monster &monster, int damage, bool isDamageShifted)
{
	if (!gbIsPoe2)
		return;
	const SkillSlot skill = MissileSkill(missile);
	const TableSkillData *data = skill.kind == SkillKind::Attack ? FindTableSkill(static_cast<WeaponSkill>(skill.id)) : nullptr;
	// The poison of the hit: physical and chaos before the monster's defences, or what the projectile dealt.
	const auto poisonBase = [&]() {
		const int32_t base = SupportHitPoisonBase(monster);
		const int64_t raw = isDamageShifted ? damage : static_cast<int64_t>(damage) * 64;
		return Fixed26_6::fromRaw(base > 0 ? base : static_cast<int32_t>(std::clamp<int64_t>(raw, 1, INT32_MAX / 2)));
	};
	// Witch's Bog: the hero's own arrows shot from or over it poison what they hit; a turret's or a minion's don't.
	const bool heroArrow = IsAnyOf(missile._mitype, MissileID::Arrow, MissileID::FireArrow, MissileID::LightningArrow) && !missile.poe2.minionShot
	    && (data == nullptr || data->shape != Zone);
	if (heroArrow && CanHit(monster) && ArrowCrossedBog(player, missile.position.start, monster.position.tile)) {
		if (const TableSkillData *bog = FindTableSkill(WeaponSkill::WitchsBog); bog != nullptr && SupportHitPoisonBase(monster) > 0)
			InflictPoison(player, monster, TablePoisonTotal(Fixed26_6::fromRaw(SupportHitPoisonBase(monster)), *bog), PoisonTicks);
	}
	if (skill.kind != SkillKind::Attack)
		return;
	// Blessed Shield (Curtis 2026-11-07, Q37): the thrown shield stuns what it hits and bounces on.
	if (data != nullptr && data->skill == WeaponSkill::BlessedShield) {
		if (!monster.hasNoLife() && monster.mode != MonsterMode::Death && monster.mode != MonsterMode::Petrified)
			M_StartStagger(monster);
		if (missile.poe2.chainsLeft > 0)
			BounceBlessedShield(missile, monster, *data);
	}
	if (data != nullptr && (data->flags & TF_PoisonBurst) != 0)
		PoisonBurst(player, monster, *data);
	// Venom Bolt: the bolt's poison, of the row's strength.
	if (data != nullptr && (data->flags & (TF_Spell | TF_Poison)) == (TF_Spell | TF_Poison) && CanHit(monster))
		InflictPoison(player, monster, TablePoisonTotal(poisonBase(), *data), PoisonTicks);
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

std::optional<MissileGraphicID> TableSkillCastFlash(const TableSkillData &data)
{
	// Only lightning crackles where a skill starts. The others' flash read as something that doesn't belong at the hero's
	// feet (Curtis, Rogue run 2): physical skills' borrowed a blue Charged Bolt spark ("lightning" on Plague Arrow and
	// every arrow and swing), fire skills' a burst of flame under Fire Arrow.
	if (data.type == DamageType::Lightning)
		return MissileGraphicID::Lightning;
	return std::nullopt;
}

bool TableLandingBurstIsSmall(const TableSkillData &data)
{
	return (data.flags & TF_LandingBurst) != 0 && data.radius <= 1;
}

void PlayTableSkillEffects(const Player &player, const TableSkillData &data, Point target)
{
	const SkillLookScope skillLook(data.skill);
	const Point tile = player.position.tile;
	const Direction dir = player._pdir;
	const bool mine = &player == MyPlayer;
	if (const std::optional<MissileGraphicID> flash = TableSkillCastFlash(data); flash)
		AddVisualEffect(tile + dir, *flash, 0, 3);
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
		// A poisoned blade spits venom where it goes in.
		if (LookOf(data) == vfx::Element::Poison)
			vfx::Impact(tile + dir, vfx::Element::Poison, 0.7F);
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
		if ((data.flags & TF_Spell) != 0 && LookOf(data) == vfx::Element::Poison) {
			// Poison Nova: a heavy green wave rolls out, flinging venom, and leaves the floor stained and steaming.
			vfx::Impact(tile, vfx::Element::Poison, 1.5F);
			vfx::Nova(tile, std::max<int>(data.radius, 1), vfx::Element::Poison);
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
	// The poison skills are sickly green, whatever their hit: Cobra Strike and Plague Arrow with them.
	if ((data.flags & (TF_Poison | TF_PoisonBurst)) != 0 || IsAnyOf(data.zone, TableZone::PoisonGround, TableZone::GasTrap, TableZone::Pod, TableZone::Bog)
	    || (data.flags & TF_PoisonedOnly) != 0 || data.curse == TableCurse::Locust)
		return vfx::Element::Poison;
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
	// The poison skills: what's thrown lands with a splash or a clank, and bursts with a wet splat.
	case WeaponSkill::AcidFlask:
		sounds.spell = Sfx(SfxID::ItemPotion);
		sounds.hit = Sfx(SfxID::SpellAcid1);
		break;
	case WeaponSkill::VenomSentry:
		sounds.spell = Sfx(SfxID::SpellTrapDisarm);
		sounds.hit = Sfx(SfxID::SpellAcid);
		break;
	case WeaponSkill::BloatedPods:
		sounds.cast = Sfx(SfxID::ShootBow2);
		sounds.spell = Sfx(SfxID::SpellPuddle);
		sounds.hit = Sfx(SfxID::SpellAcid1);
		break;
	case WeaponSkill::ThornvineArrow:
		sounds.hit = Sfx(SfxID::SpellStoneCurse);
		break;
	case WeaponSkill::PlagueArrow:
		// Its burst spits like the acid beasts' spit (Curtis, Rogue run 2).
		sounds.hit = Sfx(SfxID::SpellAcid);
		break;
	case WeaponSkill::PlagueEffigy:
		// It is driven into the ground; then it shoots.
		sounds.spell = Sfx(SfxID::SpellStoneCurse);
		break;
	case WeaponSkill::WitchsBog:
		sounds.spell = Sfx(SfxID::SpellPuddle);
		break;
	case WeaponSkill::PoisonNova:
		sounds.spell = Sfx(SfxID::SpellNova);
		break;
	case WeaponSkill::AcidCloud:
		sounds.spell = Sfx(SfxID::SpellAcid);
		sounds.hit = Sfx(SfxID::SpellAcid1);
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

namespace {

vfx::MiasmaKind MiasmaOf(const TableSkillData &data)
{
	return data.skill == WeaponSkill::PoisonExplosion ? vfx::MiasmaKind::Gas : vfx::MiasmaKind::Acid;
}

/** @brief Poisoned ground comes down: an acid pool bursts as the flask breaks or the rain lands; a vine or a cloud just grows. */
void PoisonGroundLands(Player &player, const TableSkillData &data, const SkillZone &zone)
{
	const int radius = std::max<int>(zone.radius, 1);
	const int ms = zone.ticksLeft * gnTickDelay;
	if ((data.flags & TF_LandingZone) != 0) {
		vfx::Thorns(zone.center, radius, ms, zone.key);
		vfx::Impact(zone.center, vfx::Element::Poison, 0.8F);
	} else {
		vfx::Miasma(zone.center, radius, ms, MiasmaOf(data), zone.key);
	}
	if (zone.cloud)
		return;
	ZoneBurst(player, data, zone.center, radius, zone.damage);
	vfx::Impact(zone.center, vfx::Element::Poison, 1.3F);
	// A thrown flask's breaking waited for it to land (ZoneSoundsAtBurst).
	if ((data.flags & TF_Lobbed) != 0)
		PlaySkillSound(TableSkillSounds(data).spell, zone.center);
	PlayTableSkillHitSound(data, zone.center);
	if (&player == MyPlayer)
		ShakeCamera(ShakeStrength::Light, 4);
}

/**
 * @brief Poisoned ground's pulse: everything in it is poisoned of the hit it came with (no hit of its own, so Deadly
 * Venom adds nothing), and the vine hinders; the bog only hinders.
 */
void PoisonGroundPulse(Player &player, const TableSkillData &data, const SkillZone &zone)
{
	std::vector<Monster *> inside;
	ForEachMonsterNear(zone.center, std::max<int>(zone.radius, 1), [&](Monster &monster) { inside.push_back(&monster); });
	for (Monster *monster : inside) {
		if (data.zone == TableZone::Bog) {
			HinderMonster(*monster, WitchsBogHinderPercent);
			continue;
		}
		InflictPoison(player, *monster, TablePoisonTotal(zone.damage, data), PoisonTicks, 1, false);
		if ((data.flags & TF_LandingZone) != 0)
			HinderMonster(*monster, ThornvineHinderPercent);
		monster->tag(player);
	}
}

/** @brief Rathma's Doom: a curse that ran out bursts in poison around its monster. */
void DoomBurst(Player &player, Monster &cursed)
{
	Poe2MonsterState &state = cursed.poe2;
	const Point center = cursed.position.tile;
	const Fixed26_6 damage = Fixed26_6::fromRaw(std::max<int32_t>(state.doomDamage, 64));
	const int32_t poison = static_cast<int32_t>(std::min<int64_t>(static_cast<int64_t>(damage.raw()) * RathmasDoomPoisonPercent * PoisonTicks / 20 / 100, INT32_MAX / 2));
	AddVisualEffect(center, MissileGraphicID::AcidSplat, 0, 4);
	vfx::Impact(center, vfx::Element::Poison, 1.4F);
	vfx::Nova(center, RathmasDoomRadius, vfx::Element::Poison);
	PlaySfxLoc(SfxID::SpellAcid1, center);
	std::vector<Monster *> around;
	ForEachMonsterNear(center, RathmasDoomRadius, [&](Monster &monster) { around.push_back(&monster); });
	for (Monster *monster : around) {
		SpellHitMonster(player, *monster, DamageType::Chaos, damage, AilmentSource::Spell);
		if (CanHit(*monster))
			InflictPoison(player, *monster, poison, PoisonTicks);
	}
}

} // namespace

void ProcessCurseSupports(Player &player)
{
	if (!gbIsPoe2 || &player != MyPlayer || leveltype == DTYPE_TOWN)
		return;
	const auto id = static_cast<int8_t>(player.getId());
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		Poe2MonsterState &state = monster.poe2;
		if (state.curseSupportSource != id || !CanHit(monster))
			continue;
		// Festering Hex: a 2 second poison, renewed each second while the curse lasts.
		if (state.festerCurses != 0 && state.festerTicks == 0) {
			state.festerTicks = static_cast<uint8_t>(FesteringHexEvery);
			InflictPoison(player, monster, state.festerPoison, PoisonTicks, 1, false);
			monster.tag(player);
		}
		if (state.doomPending) {
			state.doomPending = false;
			if (state.doomCooldown == 0) {
				state.doomCooldown = static_cast<uint8_t>(RathmasDoomCooldownTicks);
				DoomBurst(player, monster);
			}
		}
	}
}

void ProcessSkillZones(Player &player)
{
	ProcessCurseSupports(player);
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
	ProcessShieldWall(player);
	ProcessWaveBell(player);
	if (Zones.empty())
		return;
	if (player.hasNoLife() || leveltype == DTYPE_TOWN) {
		for (const SkillZone &zone : Zones)
			vfx::EndKeyed(zone.key);
		Zones.clear();
		return;
	}
	for (size_t i = 0; i < Zones.size();) {
		SkillZone &zone = Zones[i];
		const TableSkillData *data = FindTableSkill(zone.skill);
		bool done = data == nullptr || zone.detonated;
		const SupportHitScope scope(player, SkillSlot::ForAttack(zone.skill));
		const SkillLookScope skillLook(zone.skill);
		const int radius = zone.radius;
		if (!done && (data->flags & TF_FollowPlayer) != 0)
			zone.center = player.position.tile;
		if (!done && zone.delay > 0) {
			zone.delay--;
		} else if (!done) {
			// Falling Sword's holy ground: once a second it burns what stands on it.
			if (zone.holyGround) {
				if (zone.counter % TicksPerSecond == 0) {
					TableSkillData ground = *data;
					ground.flags = 0;
					ground.type = DamageType::Physical;
					ZoneBurst(player, ground, zone.center, std::max(radius, 1), zone.damage);
				}
			// Landing bursts of projectiles, Delayed zones and a granted skill's echo go off once.
			} else if ((data->flags & TF_LandingBurst) != 0 || IsAnyOf(data->zone, TableZone::Delayed, TableZone::IceSpike) || data->rider == GrantedRider::Echo) {
				// Bone Prison traps for its supported duration, which the zone kept while it waited.
				TableSkillData burst = *data;
				burst.duration = static_cast<uint16_t>(std::max<int>(zone.ticksLeft, 1));
				ZoneBurst(player, burst, zone.center, std::max(radius, 1), zone.damage);
				if (data->skill == WeaponSkill::ExplosiveBolt)
					DetonatePoisonClouds(Zones[i].center, std::max(radius, 1), Zones[i].damage);
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
			} else if (data->zone == TableZone::GasTrap && !zone.armed) {
				// Venom Sentry: the canister waits for an enemy to come close, then bursts into a cloud of gas.
				if (zone.counter == 0) {
					vfx::Canister(zone.center, VenomSentryArmTicks * gnTickDelay, zone.key);
					PlaySkillSound(TableSkillSounds(*data).spell, zone.center);
				}
				if (TargetMonsterNear(zone.center, radius) != nullptr) {
					zone.armed = true;
					zone.counter = -1;
					vfx::EndKeyed(zone.key);
					vfx::Miasma(zone.center, radius, zone.ticksLeft * gnTickDelay, vfx::MiasmaKind::Gas, zone.key);
					vfx::Impact(zone.center, vfx::Element::Poison, 1.2F);
					PlayTableSkillHitSound(*data, zone.center);
				}
			} else if (data->zone == TableZone::GasTrap) {
				if (zone.counter % std::max<int>(data->interval, 1) == 0)
					ZoneBurst(player, *data, zone.center, radius, zone.damage);
			} else if (data->zone == TableZone::Pod) {
				// Bloated Pods: a pod lands and swells, and bursts when an enemy comes next to it or its time runs out.
				if (zone.counter == 0) {
					vfx::Pod(zone.center, zone.ticksLeft * gnTickDelay, zone.key);
					vfx::Impact(zone.center, vfx::Element::Poison, 0.5F);
					PlaySkillSound(TableSkillSounds(*data).spell, zone.center);
				}
				if (TargetMonsterNear(zone.center, PodTriggerTiles) != nullptr || zone.ticksLeft <= 1) {
					ZoneBurst(player, *data, zone.center, std::max<int>(radius, 1), zone.damage);
					vfx::EndKeyed(zone.key);
					vfx::Impact(zone.center, vfx::Element::Poison, 1.1F);
					PlayTableSkillHitSound(*data, zone.center);
					done = true;
				}
			} else if (IsAnyOf(data->zone, TableZone::PoisonGround, TableZone::Bog)) {
				if (zone.counter == 0 && data->zone == TableZone::PoisonGround)
					PoisonGroundLands(player, *data, zone);
				if (zone.counter % std::max<int>(data->interval, 1) == 0)
					PoisonGroundPulse(player, *data, zone);
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
				case TableZone::Turret: {
					// Plague Effigy shoots only the enemies the hero's poison is on.
					const bool poisoned = (data->flags & TF_PoisonedOnly) != 0;
					if (Monster *monster = poisoned ? PoisonedMonsterNear(player, zone.center, radius) : TargetMonsterNear(zone.center, radius); monster != nullptr) {
						FireTableMissile(player, *data, zone.center, monster->position.tile);
						if (poisoned)
							vfx::EffigyShot(zone.center);
						else
							AddVisualEffect(zone.center, MissileGraphicID::MetalHit);
						PlayTableSkillHitSound(*data, zone.center);
					}
					break;
				}
				default:
					break;
				}
			}
			zone.counter++;
			// A sentry waiting for an enemy keeps its cloud's time, and is gone if none comes.
			const bool waiting = data->zone == TableZone::GasTrap && !zone.armed;
			if (waiting) {
				if (zone.counter > VenomSentryArmTicks)
					done = true;
			} else if (--zone.ticksLeft <= 0 && data->zone != TableZone::Trap) {
				done = true;
			}
			if (data->zone == TableZone::Trap && zone.counter > data->duration)
				done = true;
		}
		if (done) {
			vfx::EndKeyed(Zones[i].key);
			Zones.erase(Zones.begin() + static_cast<std::ptrdiff_t>(i));
		} else {
			i++;
		}
	}
	// The poison clouds an Explosive Arrow touched burst in fire, each for the blast's damage in its own area.
	if (!PendingDetonations.empty()) {
		const std::vector<Detonation> detonations = std::move(PendingDetonations);
		PendingDetonations.clear();
		if (const TableSkillData *arrow = FindTableSkill(WeaponSkill::ExplosiveBolt); arrow != nullptr) {
			const SupportHitScope scope(player, SkillSlot::ForAttack(arrow->skill));
			TableSkillData fire = *arrow;
			fire.type = DamageType::Fire;
			fire.flags = 0;
			fire.effect = MissileGraphicID::BigExplosion;
			for (const Detonation &burst : detonations) {
				ZoneBurst(player, fire, burst.center, burst.radius, burst.damage);
				vfx::Impact(burst.center, vfx::Element::Fire, 1.4F);
				ShakeCamera(ShakeStrength::Light, 4);
			}
		}
	}
}

int ConsumeDeathMark(Monster &monster)
{
	if (monster.poe2.deathMarkTicks == 0)
		return 100;
	monster.poe2.deathMarkTicks = 0;
	AddVisualEffect(monster.position.tile, MissileGraphicID::BloodStarExplosion);
	vfx::Impact(monster.position.tile, vfx::Element::Blood, 1.3F);
	return DeathMarkConsumePercent;
}

std::optional<Point> WaveBellTile()
{
	if (!WaveBell)
		return std::nullopt;
	return WaveBell->tile;
}

void StrikeWaveBell(const Player &player, Point tile)
{
	if (!gbIsPoe2 || !WaveBell || &player != MyPlayer || WaveBell->tile != tile || WaveBell->cooldown > 0)
		return;
	const TableSkillData *wave = FindTableSkill(WeaponSkill::WaveOfLight);
	if (wave == nullptr)
		return;
	WaveBell->cooldown = static_cast<uint8_t>(WaveBellTollCooldown);
	Player &hero = *MyPlayer;
	const SupportHitScope scope(hero, SkillSlot::ForAttack(WeaponSkill::WaveOfLight));
	const SkillLookScope skillLook(WeaponSkill::WaveOfLight);
	vfx::BellToll(tile, WaveBellTollRadius);
	AddVisualEffect(tile, MissileGraphicID::HolyBoltExplosion);
	PlayTableSkillHitSound(*wave, tile);
	WaveBellHit(hero, *wave, tile, WaveBellTollRadius, WaveBellTollPercent);
}

bool IsShieldWallTile(Point tile)
{
	return std::any_of(WallSegments.begin(), WallSegments.end(), [tile](const WallSegment &segment) { return segment.tile == tile; });
}

size_t ShieldWallSegmentCount()
{
	return WallSegments.size();
}

int ShatterShieldWall(Player &player, Point at)
{
	// The wall is the local hero's, like the zones.
	if (WallSegments.empty() || &player != MyPlayer)
		return 0;
	const TableSkillData *wall = FindTableSkill(WeaponSkill::ShieldWall);
	if (wall == nullptr)
		return 0;
	TableSkillData blast = *wall;
	blast.flags = 0;
	blast.damagePercent = static_cast<int16_t>(ShieldWallShatterPercent);
	const SupportHitScope scope(player, SkillSlot::ForAttack(WeaponSkill::ShieldWall));
	int shattered = 0;
	for (size_t i = 0; i < WallSegments.size();) {
		const Point tile = WallSegments[i].tile;
		if (tile.WalkingDistance(at) > ShieldWallShatterTiles && tile.WalkingDistance(player.position.tile) > ShieldWallShatterTiles) {
			i++;
			continue;
		}
		RemoveWallSegment(i);
		shattered++;
		AddVisualEffect(tile, MissileGraphicID::StoneCurseShatter);
		vfx::Slam(tile, 1, vfx::Element::Physical);
		std::vector<Monster *> targets;
		ForEachMonsterNear(tile, 1, [&](Monster &monster) { targets.push_back(&monster); });
		CastResult result;
		for (Monster *monster : targets)
			WeaponHit(player, *monster, blast, blast.damagePercent, result);
	}
	if (shattered > 0) {
		PlaySfxLoc(SfxID::BarrelBreak, at);
		ShakeCamera(ShakeStrength::Medium, 5);
	}
	return shattered;
}

void ClearSkillZones()
{
	Zones.clear();
	WallSegments.clear();
	PendingDetonations.clear();
	EndWaveBell();
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
	if (player.poe2.frozenArmorTicks > 0) {
		percent -= FrozenArmorPercent;
		if (AilmentAllowed(player, AilmentSource::Spell)) {
			monster.poe2.chillTicks = 60;
			monster.poe2.chillPercent = static_cast<uint8_t>(std::max<int>(monster.poe2.chillPercent, 30));
		}
	}
	if (player.poe2.holyShieldTicks > 0)
		percent -= HolyShieldPercent;
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
	// Rathma's Doom: a hand-cast curse running out on a living monster bursts (ProcessCurseSupports), on its caster's
	// machine. Blasphemy's aura curses don't, nor one Hex Rupture burst.
	if ((state.doomCurses | state.festerCurses) != 0) {
		for (const TableCurse curse : { TableCurse::Weaken, TableCurse::IronMaiden, TableCurse::Decrepify, TableCurse::LowerResist, TableCurse::DeathMark, TableCurse::Despair }) {
			const uint8_t left = *CurseTicks(state, curse);
			const auto bit = AuraCurseBit(curse);
			if (left == 0 || (state.auraCurses & bit) != 0) {
				state.doomCurses &= static_cast<uint16_t>(~bit);
				state.festerCurses &= static_cast<uint16_t>(~bit);
				continue;
			}
			if (left == 1 && (state.doomCurses & bit) != 0) {
				state.doomCurses &= static_cast<uint16_t>(~bit);
				state.festerCurses &= static_cast<uint16_t>(~bit);
				if (!monster.hasNoLife() && state.curseSupportSource == static_cast<int8_t>(MyPlayerId))
					state.doomPending = true;
			} else if (left == 1) {
				state.festerCurses &= static_cast<uint16_t>(~bit);
			}
		}
	}
	if (state.festerTicks > 0)
		state.festerTicks--;
	if (state.doomCooldown > 0)
		state.doomCooldown--;
	for (uint8_t *ticks : { &state.weakenTicks, &state.decrepifyTicks, &state.ironMaidenTicks, &state.lowerResistTicks, &state.deathMarkTicks, &state.despairTicks, &state.hauntTicks, &state.locustTicks, &state.plagueTicks }) {
		if (*ticks > 0)
			(*ticks)--;
	}
	if (state.attractTicks > 0)
		state.attractTicks--;
	if (state.confuseTicks > 0 && --state.confuseTicks == 0) {
		state.berserkPercent = 0;
		if (state.confusedByCurse) {
			monster.flags &= ~(MFLAG_BERSERK | MFLAG_GOLEM);
			state.confusedByCurse = false;
		}
	}
}

int DecrepifySlowPercentFor(const Monster &monster)
{
	if (monster.isUnique() || monster.type().type == MT_DIABLO)
		return DecrepifyUniqueSlowPercent;
	if (GetMonsterAffixes(monster).rarity == MonsterRarity::Rare)
		return DecrepifyRareSlowPercent;
	return DecrepifySlowPercent;
}

bool MonsterObeysAttract(const Monster &monster, const Monster &cursed)
{
	if (monster.isPlayerMinion() || monster.isUnique() || monster.type().type == MT_DIABLO)
		return false;
	if (GetMonsterAffixes(monster).rarity == MonsterRarity::Rare)
		return ((cursed.poe2.attractSeed + monster.getId()) & 1) == 0;
	return true;
}

void GiveConfuseBerserk(Monster &monster, int ticks)
{
	Poe2MonsterState &state = monster.poe2;
	if ((monster.flags & MFLAG_BERSERK) == 0) {
		monster.flags |= MFLAG_BERSERK | MFLAG_GOLEM;
		state.confusedByCurse = true;
	}
	state.confuseTicks = static_cast<uint8_t>(std::clamp(std::max<int>(ticks, state.confuseTicks), 1, 255));
	state.berserkPercent = static_cast<uint8_t>(120 + GenerateRnd(10));
}

int TableCurseDamageTakenPercent(const Monster &monster, DamageType type)
{
	int percent = 0;
	if (monster.poe2.deathMarkTicks > 0)
		percent += DeathMarkPercent * TableCurseStrength(monster, TableCurse::DeathMark) / 100;
	return percent;
}

int TableCurseShownPercent(const Monster &monster, TableCurse curse)
{
	const Poe2MonsterState &state = monster.poe2;
	switch (curse) {
	case TableCurse::Weaken:
		return state.weakenTicks > 0 ? WeakenPercent * TableCurseStrength(monster, curse) / 100 : 0;
	case TableCurse::Decrepify:
		return state.decrepifyTicks > 0 ? DecrepifySlowPercentFor(monster) * TableCurseStrength(monster, curse) / 100 : 0;
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
	const Point from = monster.position.tile;
	const auto nearest = [&](int range, size_t count) {
		std::vector<Monster *> next;
		ForEachMonsterNear(from, range, [&](Monster &other) {
			if (&other != &monster)
				next.push_back(&other);
		});
		std::sort(next.begin(), next.end(), [&](const Monster *a, const Monster *b) {
			return from.WalkingDistance(a->position.tile) < from.WalkingDistance(b->position.tile);
		});
		if (next.size() > count)
			next.resize(count);
		return next;
	};
	// Locust Swarm's poison spreads to the nearest with the ticks it had left; a stronger poison there stays.
	if (state.locustTicks > 0 && state.poisonTicks > 0 && state.poisonSource >= 0 && static_cast<size_t>(state.poisonSource) < Players.size()) {
		const Player &player = Players[state.poisonSource];
		for (Monster *other : nearest(LocustSpreadTiles, LocustSpreads)) {
			Poe2MonsterState &target = other->poe2;
			target.locustTicks = std::max<uint8_t>(target.locustTicks, state.locustTicks);
			if (target.poisonTicks == 0 || state.poisonDamagePerTick >= target.poisonDamagePerTick) {
				target.poisonTicks = target.poisonDamagePerTick == state.poisonDamagePerTick ? std::max<uint8_t>(target.poisonTicks, state.poisonTicks) : state.poisonTicks;
				target.poisonDamagePerTick = state.poisonDamagePerTick;
				target.poisonSource = state.poisonSource;
			}
			other->tag(player);
			AddVisualEffect(other->position.tile, MissileGraphicID::AcidSplat);
		}
	}
	if ((state.hauntTicks == 0 && state.plagueTicks == 0) || state.chaosSource < 0 || static_cast<size_t>(state.chaosSource) >= Players.size())
		return;
	const Player &player = Players[state.chaosSource];
	const bool haunt = state.hauntTicks > 0;
	// Decompose's plague passes to the one nearest monster.
	const bool plague = !haunt;
	std::vector<Monster *> next = nearest(haunt ? HauntJumpTiles : PlagueSpreadTiles, 1);
	const uint8_t ticks = haunt ? state.hauntTicks : static_cast<uint8_t>(PlagueTicks);
	// Haunt is a spirit, not a wound (Curtis 2026-10-28: "is it a bleed skill, or a ghost?"): it leaves the body and
	// shows up on the next one.
	const MissileGraphicID look = haunt ? MissileGraphicID::BoneSpirit : MissileGraphicID::BloodHit;
	if (haunt && !next.empty())
		AddVisualEffect(from, look);
	for (Monster *other : next) {
		Poe2MonsterState &target = other->poe2;
		(plague ? target.plagueTicks : target.hauntTicks) = ticks;
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
