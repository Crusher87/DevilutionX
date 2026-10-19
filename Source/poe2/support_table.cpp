#include "poe2/support_table.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <vector>

#include "engine/backbuffer_state.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "levels/tile_properties.hpp"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/default_attack.hpp"
#include "poe2/gem_levels.hpp"
#include "poe2/meta_gems.hpp"
#include "poe2/minion_gems.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/skill_resources.hpp"
#include "poe2/spirit.hpp"
#include "poe2/spirit_auras.hpp"
#include "poe2/spirit_skills.hpp"
#include "poe2/spirit_uses.hpp"
#include "poe2/support_gems.hpp"
#include "poe2/support_tiers.hpp"
#include "poe2/table_skills.hpp"
#include "poe2/unique_effects.hpp"
#include "poe2/waystones.hpp"
#include "poe2/weapon_skills.hpp"
#include "spells.h"
#include "utils/is_of.hpp"

namespace devilution {

namespace {

using SG = SupportGem;
using GC = GemColor;

enum Family : uint8_t {
	NoFamily,
	MoreProjectiles,
	Infusion,
	Repeat,
	Leech,
	Runes,
};

constexpr uint32_t Any = 0;
constexpr uint32_t DamageSkill = ST_Damage;
constexpr uint32_t PhysicalHit = ST_Physical;

// clang-format off
const SupportData Table[] = {
	{ SG::IncreasedArea, "Increased Area", "30% larger area. Mana +30%", "Needs an area skill", GC::Magic, 3, 130, ST_Area, 0, 0, 0, NoFamily },
	{ SG::IncreasedDuration, "Increased Duration", "Lasts 50% longer. Mana +20%", "Needs a skill with a duration", GC::Strength, 2, 120, ST_Duration, 0, 0, 0, NoFamily },
	{ SG::Volley, "Volley", "2 extra projectiles side by side.\n25% less damage. Mana +20%", "Needs a projectile skill", GC::Dexterity, 4, 120, ST_Projectile, 0, 0, 0, MoreProjectiles },
	{ SG::SpellEcho, "Spell Echo", "Each cast repeats once. 30% slower\ncasts. Mana +50%", "Needs a spell", GC::Magic, 8, 150, 0, ST_Spell, ST_Channelling | ST_Movement, 0, Repeat },
	{ SG::Multistrike, "Multistrike", "Each melee hit strikes twice more;\nthe repeats deal 25% less. Mana +40%", "Needs a melee attack", GC::Strength, 7, 140, 0, ST_Melee, 0, 0, Repeat },
	{ SG::Aftershock, "Aftershock", "A second, smaller slam where the\nfirst landed, for 40% less. Mana +30%", "Needs a slam", GC::Strength, 5, 130, 0, ST_Slam, 0, 0, NoFamily },
	{ SG::CloseCombat, "Close Combat", "Power: up to 25% more damage the closer\nthe monster, none beyond 4 tiles. Mana +20%", "Needs an attack", GC::Strength, 3, 120, 0, ST_Attack, 0, SF_Power, NoFamily },
	{ SG::InfusedFlames, "Infused Flames", "Half of physical damage becomes fire.\nMana +20%", "Needs a physical skill", GC::Strength, 4, 120, 0, PhysicalHit, 0, 0, Infusion },
	{ SG::InfusedStorm, "Infused Storm", "Half of physical damage becomes\nlightning. Mana +20%", "Needs a physical skill", GC::Dexterity, 4, 120, 0, PhysicalHit, 0, 0, Infusion },
	{ SG::InfusedFrost, "Infused Frost", "Half of physical damage becomes cold.\nMana +20%", "Needs a physical skill", GC::Magic, 4, 120, 0, PhysicalHit, 0, 0, Infusion },
	{ SG::HolyInfusion, "Holy Infusion", "Power: hits add 20% of their damage\nas arcane. Mana +30%", "Needs a damaging skill", GC::Magic, 5, 130, 0, DamageSkill, 0, SF_Power, NoFamily },
	{ SG::BurningGround, "Burning Ground", "Hits leave burning ground. 20% less\ndamage with hits. Mana +30%", "Needs a fire, area or projectile skill", GC::Strength, 6, 130, ST_Fire | ST_Area | ST_Projectile, ST_Damage, 0, 0, NoFamily },
	{ SG::Overcharge, "Overcharge", "Shocks twice as often. 25% less\ndamage with hits. Mana +10%", "Needs a lightning skill", GC::Dexterity, 4, 110, 0, ST_Lightning, 0, 0, NoFamily },
	{ SG::DeepFreeze, "Deep Freeze", "Freezes sooner. 20% less damage\nwith hits. Mana +10%", "Needs a cold skill", GC::Magic, 4, 110, 0, ST_Frost, 0, 0, NoFamily },
	{ SG::WitheringTouch, "Withering Touch", "Hits wither: 5% more damage taken\na stack, up to 10. 15% less damage. Mana +20%", "Needs a damaging skill", GC::Magic, 6, 120, 0, DamageSkill, 0, 0, NoFamily },
	{ SG::CastOnKill, "Cast on Kill", "Retired: it's the Reaper's Invocation\nSpirit gem now, which holds the spells", "Retired", GC::Magic, 6, 120, 0, ST_Spell | ST_Damage, ST_Movement | ST_Channelling, SF_Trigger, NoFamily },
	{ SG::CastWhenHit, "Cast when Hit", "Retired: it's the Cast when Damage Taken\nSpirit gem now, which holds the spells", "Retired", GC::Strength, 6, 120, 0, ST_Spell, ST_Movement | ST_Channelling, SF_Trigger, NoFamily },
	{ SG::CastOnDodge, "Cast on Dodge", "Retired: it's the Cast on Dodge Spirit\ngem now, which holds the spells", "Retired", GC::Dexterity, 6, 120, 0, ST_Spell, ST_Movement | ST_Channelling, SF_Trigger, NoFamily },
	{ SG::CastWhileChannelling, "Cast while Channelling", "Retired: it's the Cast while Channelling\nSpirit gem now, which holds the spells", "Retired", GC::Magic, 9, 130, 0, ST_Spell | ST_Damage, ST_Movement | ST_Channelling, SF_Trigger, NoFamily },
	{ SG::BlinkStrike, "Blink Strike", "Where you land bursts with arcane\ndamage. 20% slower casts. Mana +40%", "Needs a movement skill", GC::Magic, 7, 140, 0, ST_Movement, 0, 0, NoFamily },
	{ SG::EchoingShout, "Echoing Shout", "The warcry repeats twice, each echo\n50% weaker. Mana +30%", "Needs a warcry", GC::Strength, 5, 130, 0, ST_Warcry, 0, 0, Repeat },
	{ SG::RallyingCry, "Rallying Cry", "The warcry hits all around for 150%\nweapon damage. Mana +30%", "Needs a warcry", GC::Strength, 5, 130, 0, ST_Warcry, 0, 0, NoFamily },
	{ SG::BoneArmy, "Bone Army", "2 more skeletons, with 30% less life\nand damage each", "Needs skeleton minions", GC::Magic, 6, 100, 0, ST_Minion, 0, 0, NoFamily },
	{ SG::FeedingFrenzy, "Feeding Frenzy", "Minions deal 30% more damage, but\ntake 20% more", "Needs a minion skill", GC::Strength, 5, 100, 0, ST_Minion, 0, 0, NoFamily },
	{ SG::GravePact, "Grave Pact", "Dying minions explode. Minions have\n20% less life", "Needs a minion skill", GC::Magic, 6, 100, 0, ST_Minion, 0, 0, NoFamily },
	{ SG::CurseOnHit, "Curse on Hit", "Retired: it's the Blasphemy Spirit gem\nnow, which holds the curses", "Retired", GC::Magic, 7, 130, 0, DamageSkill, ST_Curse, 0, NoFamily },
	{ SG::CurseMastery, "Curse Mastery", "Larger, longer curses. Mana +30%", "Needs a curse", GC::Magic, 5, 130, 0, ST_Curse, 0, 0, NoFamily },
	{ SG::RuneMastery, "Rune Mastery", "Places 2 runes at once. Mana +30%", "Needs a rune", GC::Dexterity, 5, 130, 0, ST_Rune, 0, 0, Runes },
	{ SG::ClusterRunes, "Cluster Runes", "Places 3 runes in a ring. Mana +40%", "Needs a rune", GC::Dexterity, 8, 140, 0, ST_Rune, 0, 0, Runes },
	{ SG::Sanctify, "Sanctify", "Each heal pulses arcane damage around\nyou. Mana +40%", "Needs a healing spell", GC::Magic, 5, 140, 0, ST_Healing, 0, 0, NoFamily },
	{ SG::Shatter, "Shatter", "Frozen or petrified monsters you slam\nburst. 15% less damage to others. Mana +20%", "Needs a slam or a cold skill", GC::Strength, 6, 120, ST_Slam | ST_Frost, ST_Damage, 0, 0, NoFamily },
	{ SG::LifeLeech, "Life Leech", "Leeches 3% of damage as life. 10% less\ndamage. Mana +20%", "Needs a damaging skill", GC::Strength, 3, 120, 0, DamageSkill, 0, 0, Leech },
	{ SG::ManaLeech, "Mana Leech", "Leeches 2% of damage as mana. 10% less\ndamage. Mana +10%", "Needs a damaging skill", GC::Magic, 3, 110, 0, DamageSkill, 0, 0, Leech },
	{ SG::BloodPrice, "Blood Price", "Costs life instead of mana, 20% more", {}, GC::Strength, 4, 100, 0, 0, 0, 0, NoFamily },
	// From the build research.
	{ SG::ElementalFocus, "Elemental Focus", "Power: 25% more elemental damage, but\nno ailments. Mana +20%", "Needs an elemental skill", GC::Magic, 6, 120, ST_Elemental, 0, 0, SF_Power, NoFamily },
	{ SG::Hellfire, "Hellfire", "Ignites burn 75% harder. 25% less\ndamage with hits. Mana +10%", "Needs a fire skill", GC::Strength, 6, 110, 0, ST_Fire, 0, 0, NoFamily },
	{ SG::FuelThePyre, "Fuel the Pyre", "30% of hits as extra fire against\nburning monsters; can't ignite. Mana +20%", "Needs a damaging skill", GC::Strength, 7, 120, 0, DamageSkill, 0, 0, NoFamily },
	{ SG::Frostbite, "Frostbite", "Freezing a monster gives your hits 25%\nextra cold for 5 seconds. Mana +10%", "Needs a cold skill", GC::Magic, 5, 110, 0, ST_Frost, 0, 0, NoFamily },
	{ SG::Conductor, "Conductor", "Hits on shocked monsters arc to one\nmore; can't shock. Mana +20%", "Needs a lightning skill", GC::Dexterity, 6, 120, 0, ST_Lightning, 0, 0, NoFamily },
	{ SG::SerratedEdge, "Serrated Edge", "Hits have a 25% chance to make\nmonsters bleed. Mana +10%", "Needs a physical attack", GC::Dexterity, 3, 110, 0, ST_Attack | PhysicalHit, 0, 0, NoFamily },
	{ SG::Envenom, "Envenom", "Hits poison for 30% of their damage over\n2 seconds; stacks. 15% less damage. Mana +10%", "Needs a physical skill", GC::Dexterity, 5, 110, 0, PhysicalHit, 0, 0, NoFamily },
	{ SG::Bloodlust, "Bloodlust", "30% more physical damage against\nbleeding monsters. Mana +10%", "Needs a melee attack", GC::Strength, 5, 110, 0, ST_Melee, 0, 0, NoFamily },
	{ SG::SunderArmour, "Sunder Armour", "Hits break armour: 20% more physical\ndamage taken for 6 seconds. Mana +10%", "Needs a physical attack", GC::Strength, 4, 110, 0, ST_Attack | PhysicalHit, 0, 0, NoFamily },
	{ SG::Pestilence, "Pestilence", "A monster dying with your bleed, poison or\nignite on it passes it to 2 more. Mana +30%", "Needs a damaging skill", GC::Magic, 8, 130, 0, DamageSkill, 0, 0, NoFamily },
	{ SG::HoradricSeals, "Horadric Seals", "Stores a seal every 1.5s, up to 2; the next\ncast repeats once per seal at 50%. Mana +50%", "Needs a spell", GC::Magic, 9, 150, 0, ST_Spell, ST_Channelling | ST_Movement, 0, Repeat },
	{ SG::StoredVolley, "Stored Volley", "Stores a seal every second, up to 6; the\nnext use fires 1 more projectile each. Mana +20%", "Needs a projectile skill", GC::Dexterity, 7, 120, 0, ST_Projectile, 0, 0, MoreProjectiles },
	{ SG::Cascade, "Cascade", "The area repeats on each side of the\ntarget. 30% less damage. Mana +30%", "Needs an area spell", GC::Magic, 8, 130, 0, ST_Spell | ST_Area, ST_Movement, 0, Repeat },
	{ SG::NovaProjectiles, "Nova Projectiles", "Fires in a circle around you. 20% less\ndamage. Mana +20%", "Needs a projectile skill", GC::Dexterity, 6, 120, 0, ST_Projectile, 0, 0, MoreProjectiles },
	{ SG::Ricochet, "Ricochet", "Projectiles that hit a wall bounce to a\nmonster for 20% less. Mana +10%", "Needs a projectile skill", GC::Dexterity, 4, 110, 0, ST_Projectile, 0, 0, NoFamily },
	{ SG::Momentum, "Momentum", "40% more damage after moving at least\n2 tiles. Mana +20%", "Needs a damaging skill", GC::Strength, 5, 120, 0, DamageSkill, 0, 0, NoFamily },
	{ SG::CrushingBlow, "Crushing Blow", "Power: 35% more physical damage, 10%\nslower attacks. Mana +10%", "Needs a melee attack", GC::Strength, 6, 110, 0, ST_Melee, 0, SF_Power, NoFamily },
	{ SG::DeliberateCasting, "Deliberate Casting", "Power: 35% more damage, 15% slower\ncasts. Mana +15%", "Needs a spell", GC::Magic, 6, 115, 0, ST_Spell | ST_Damage, 0, SF_Power, NoFamily },
	{ SG::BrimmingMana, "Brimming Mana", "Power: 30% more damage while above 90%\nmana. Mana +20%", "Needs a spell", GC::Magic, 5, 120, 0, ST_Spell | ST_Damage, 0, SF_Power, NoFamily },
	{ SG::CoupDeGrace, "Coup de Grace", "Power: 40% more damage to monsters\nbelow 35% life. Mana +10%", "Needs a damaging skill", GC::Strength, 6, 110, 0, DamageSkill, 0, SF_Power, NoFamily },
	{ SG::Wrathforged, "Wrathforged", "Spends 10 Rage when it can for 35% more\ndamage. Mana +10%", "Needs an attack", GC::Strength, 6, 110, 0, ST_Attack | ST_Damage, 0, 0, NoFamily },
	{ SG::ComboFinisher, "Combo Finisher", "Spends all Combo for 3% more damage a\npoint. Mana +10%", "Needs a quarterstaff attack", GC::Dexterity, 6, 110, 0, ST_Staff | ST_Damage, 0, 0, NoFamily },
	{ SG::HoradricTotem, "Horadric Totem", "Retired: it's the Horadric Totem Spirit\ngem now, which holds the spells", "Retired", GC::Magic, 9, 120, 0, ST_Spell | ST_Damage, ST_Channelling | ST_Movement, 0, NoFamily },
	{ SG::Decoy, "Decoy", "Retired: it's the Mirage Archer Spirit\ngem now", "Retired", GC::Dexterity, 8, 130, 0, ST_Bow, 0, 0, NoFamily },
	{ SG::Legion, "Legion", "1 more out at a time, 50% shorter.\nMana +10%", "Needs walls, runes, sentries or totems", GC::Strength, 7, 110, 0, ST_Placed, 0, 0, NoFamily },
	{ SG::MixedHost, "Mixed Host", "Minions deal 7% more damage for each\nkind of minion you have", "Needs a minion skill", GC::Magic, 7, 100, 0, ST_Minion, 0, 0, NoFamily },
	// Lineage supports.
	{ SG::ButchersHook, "The Butcher's Hook", "Hits drag the monster to you and make it\nbleed; 15% slower attacks", "Needs a melee attack", GC::Strength, 0, 110, 0, ST_Melee, 0, SF_Lineage, NoFamily },
	{ SG::RathmasCovenant, "Rathma's Covenant", "A minion that dies rises as a bone\nspirit that bursts on a monster", "Needs a minion skill", GC::Magic, 0, 100, 0, ST_Minion, 0, SF_Lineage, NoFamily },
	{ SG::LazarusDeceit, "Lazarus' Deceit", "When a cursed monster dies, its curses\njump to the 2 nearest", "Needs a curse", GC::Magic, 0, 110, 0, ST_Curse, 0, SF_Lineage, NoFamily },
	{ SG::DiablosRedLightning, "Diablo's Red Lightning", "Can't shock; monsters hit below 20% life\nburst and arc to 3 more", "Needs a lightning skill", GC::Dexterity, 0, 120, 0, ST_Lightning, 0, SF_Lineage, NoFamily },
	{ SG::ZharsMadness, "Zhar's Madness", "25% chance on cast to also cast another\nof your spell gems, at its own cost", "Needs a spell", GC::Magic, 0, 110, 0, ST_Spell, ST_Movement, SF_Lineage, NoFamily },
	{ SG::WarlordsBloodPact, "Warlord's Blood Pact", "Kills give faster attacks, up to 40%;\ngetting hit takes it all away", "Needs a melee attack", GC::Strength, 0, 110, 0, ST_Melee, 0, SF_Lineage, NoFamily },
	{ SG::GharbadsGreed, "Gharbad's Greed", "Kills have a 30% chance to drop another\nitem. 15% less damage", "Needs a damaging skill", GC::Dexterity, 0, 110, 0, DamageSkill, 0, SF_Lineage, NoFamily },
	{ SG::NaKrulsPrison, "Na-Krul's Prison", "Walls form a ring around the target;\n25% shorter", "Needs a wall", GC::Magic, 0, 120, 0, ST_Placed, ST_Rune, SF_Lineage, NoFamily },
	{ SG::TalRashasPrism, "Tal Rasha's Prism", "Hits add 30% as the next element in turn;\nall three in a row: next cast 50% more", "Needs an elemental spell", GC::Magic, 0, 120, ST_Elemental, ST_Spell, 0, SF_Lineage, NoFamily },
	{ SG::BartucsBloodletting, "Bartuc's Bloodletting", "Half the mana cost is paid in life; hits\ngain 3 times the life paid as physical", {}, GC::Strength, 0, 100, 0, 0, 0, SF_Lineage, NoFamily },
	{ SG::TragOulsBalance, "Trag'Oul's Balance", "Minions can't deal or take damage; you\ndeal 5% more for each one alive", "Needs a minion skill", GC::Magic, 0, 100, 0, ST_Minion, 0, SF_Lineage, NoFamily },
	{ SG::ImmortalKingsStride, "Immortal King's Stride", "Each slam is followed by 2 aftershocks\nnearby; 20% smaller area", "Needs a slam", GC::Strength, 0, 120, 0, ST_Slam, 0, SF_Lineage, NoFamily },
	{ SG::KashyasVolley, "Kashya's Volley", "Fires 3 extra arrows in a fan for 30%\nless each", "Needs a bow or crossbow attack", GC::Dexterity, 0, 120, 0, ST_Bow, 0, SF_Lineage, NoFamily },
	{ SG::AndarielsAnguish, "Andariel's Anguish", "Hits leave Anguish: 10% of the hit again\neach second for 4s, up to 5; can't ignite", "Needs a damaging skill", GC::Dexterity, 0, 120, 0, DamageSkill, 0, SF_Lineage, NoFamily },
	{ SG::BaalsEcho, "Baal's Echo", "A shadow repeats the skill half a second\nlater at 40% damage", "Needs an attack or spell", GC::Magic, 0, 130, ST_Attack | ST_Spell, ST_Damage, ST_Channelling | ST_Movement, SF_Lineage, Repeat },
	{ SG::WirtsLeg, "Wirt's Leg", "10% chance for a hit to deal 300% damage\nand knock back; you move 10% slower", "Needs a melee attack", GC::Strength, 0, 100, 0, ST_Melee, 0, SF_Lineage, NoFamily },
	// Penetration: hits get past part of the monster's resistance, never below 0.
	{ SG::FirePenetration, "Fire Penetration", "Fire hits penetrate 15% of the\nmonster's fire resistance", "Needs a fire skill", GC::Strength, 3, 100, 0, ST_Fire, 0, 0, NoFamily },
	{ SG::LightningPenetration, "Lightning Penetration", "Lightning hits penetrate 15% of the\nmonster's lightning resistance", "Needs a lightning skill", GC::Dexterity, 3, 100, 0, ST_Lightning, 0, 0, NoFamily },
	{ SG::ColdPenetration, "Cold Penetration", "Cold hits penetrate 15% of the\nmonster's magic resistance", "Needs a cold skill", GC::Magic, 3, 100, 0, ST_Frost, 0, 0, NoFamily },
	// POE2's herald-only support (Curtis 2026-10-19).
	{ SG::DeadlyHerald, "Deadly Herald", "The herald's explosions deal 30% more\ndamage. Reserves 20 more Spirit", "Needs a herald", GC::Dexterity, 6, 100, 0, ST_Herald, 0, 0, NoFamily },
};
// clang-format on

constexpr size_t FirstTableSupport = static_cast<size_t>(SupportGem::IncreasedArea);
static_assert(std::size(Table) == static_cast<size_t>(SupportGem::Count) - FirstTableSupport);

// Effect numbers.
constexpr int VolleyExtra = 2;
/** Shadow Warrior repeats a skill this many game ticks after it. */
constexpr int ShadowWarriorDelay = 8;
constexpr int KashyaExtra = 3;
constexpr int MaxStoredVolleySeals = 6;
constexpr int MaxHoradricSeals = 2;
constexpr int HoradricSealTicks = 30;
constexpr int StoredVolleySealTicks = 20;
constexpr int MultistrikePercent = 75;
/** Immortal King's Stride's aftershocks hit for 40% less. */
constexpr int AftershockPercent = 60;
constexpr int WitherPercentPerStack = 5;
constexpr int TicksPerSecond = 20;
/** Life Leech III keeps leech for 2 seconds at full life. */
constexpr int LeechPoolTicks = 2 * TicksPerSecond;
/** Faster Casting III counts the spells cast in the last 8 seconds. */
constexpr int RecentSpellTicks = 8 * TicksPerSecond;
constexpr int MaxWitherStacks = 10;
constexpr int WitherTicks = 4 * 20;
/** Mirage Archer's mirage: it shoots for 5 seconds with 30% less damage, and a dodge leaves one at most every 10 seconds. */
constexpr int MirageTicks = 5 * 20;
constexpr int MirageCooldownTicks = 10 * 20;
constexpr int MirageDamagePercent = 70;
constexpr int DecoyShotTicks = 12;
constexpr int MaxWarlordStacks = 10;
constexpr int FrostbiteTicks = 5 * 20;
constexpr int SpentBonusTicks = 20;

/** @brief A delayed burst or a pulsing patch on the ground. */
struct GroundBurst {
	Point center;
	int8_t radius;
	int16_t ticks;
	int8_t pulsesLeft;
	int8_t interval;
	DamageType type;
	MissileGraphicID effect;
	Fixed26_6 damage;
};

/** @brief A skill that goes off again: a repeat, an echo, a strike of Multistrike or a trigger. */
struct PendingUse {
	SkillSlot skill;
	Point target;
	int16_t ticks;
	int16_t percent;
	/** Multistrike's strikes hit this monster again; -1 to use the skill again. */
	int16_t monster;
};

/** @brief A Horadric Totem casting the spells inside its gem, or a mirage repeating an attack. */
struct Stand {
	SkillSlot skill;
	Point position;
	int16_t ticksLeft;
	int16_t cooldown;
	bool decoy;
	/** The Skills panel row of a totem's gem, and the socket of the spell it casts next. */
	int8_t row = -1;
	uint8_t next = 0;
};

std::vector<GroundBurst> Bursts;
std::vector<PendingUse> Pending;
std::vector<Stand> Stands;

/** @brief The hit in progress. */
struct HitState {
	const Player *player = nullptr;
	SkillSlot skill {};
	int depth = 0;
	bool finishing = false;
	Monster *monster = nullptr;
	int64_t physical = 0;
	int64_t total = 0;
	DamageType lastType = DamageType::Physical;
	bool wirt = false;
};
HitState Hit;

/** @brief Damage of repeats in progress, in percent, and whether supports may set off more. */
int RepeatPercent = 100;
bool Repeating = false;

bool Has(const Player &player, SkillSlot skill, SupportGem gem)
{
	return !skill.isEmpty() && player.poe2.hasSupport(skill, gem);
}

bool HitHas(SupportGem gem)
{
	return Hit.player != nullptr && Has(*Hit.player, Hit.skill, gem);
}

/** @brief A number of the support at the tier it has on the skill (see poe2/support_tiers.hpp); 0 if the skill doesn't have it. */
int Value(const Player &player, SkillSlot skill, SupportGem gem, size_t index)
{
	return skill.isEmpty() ? 0 : SupportValue(player.poe2, skill, gem, index);
}

int HitValue(SupportGem gem, size_t index)
{
	return Hit.player != nullptr ? Value(*Hit.player, Hit.skill, gem, index) : 0;
}

int MinionValue(const Player &owner, PersistentSkill skill, SupportGem gem, size_t index);

int Percent(int64_t value, int percent)
{
	return static_cast<int>(std::clamp<int64_t>(value * percent / 100, INT32_MIN, INT32_MAX));
}

bool CanHit(const Monster &monster)
{
	return !monster.hasNoLife() && !monster.isPlayerMinion() && monster.isPossibleToHit() && monster.mode != MonsterMode::Death;
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

std::vector<Monster *> NearestMonsters(Point from, int range, size_t count, const Monster *except)
{
	std::vector<Monster *> found;
	ForEachMonsterNear(from, range, [&](Monster &monster) {
		if (&monster != except)
			found.push_back(&monster);
	});
	std::sort(found.begin(), found.end(), [&](const Monster *a, const Monster *b) {
		return from.WalkingDistance(a->position.tile) < from.WalkingDistance(b->position.tile);
	});
	if (found.size() > count)
		found.resize(count);
	return found;
}

int SkillLevel(const Player &player, SkillSlot skill)
{
	return std::max(player.poe2.skillLevel(skill), 1);
}

/** @brief A hit's worth of the skill's damage, for bursts: from Magic for spells, from the weapon for attacks. */
Fixed26_6 SkillBaseDamage(const Player &player, SkillSlot skill, int percent)
{
	const int level = SkillLevel(player, skill);
	int base;
	if ((SkillTags(skill) & ST_Spell) != 0 || skill.kind == SkillKind::Spell) {
		base = 5 + (player._pMagic / 6) + (2 * level);
	} else {
		base = (player._pIMinDam + player._pIMaxDam) / 2;
		base += base * player._pIBonusDam / 100 + player._pIBonusDamMod + player._pDamageMod;
	}
	int64_t raw = static_cast<int64_t>(std::max(base, 1)) * 64;
	raw = raw * GemDamagePercent(skill, level) / 100 * percent / 100;
	return Fixed26_6::fromRaw(static_cast<int32_t>(std::clamp<int64_t>(raw, 64, INT32_MAX / 2)));
}

/** @brief Whether the hit in progress is a spell's or an attack's, for the rules that stop ailments. */
AilmentSource HitSource()
{
	return !Hit.skill.isEmpty() && (SkillTags(Hit.skill) & ST_Spell) != 0 ? AilmentSource::Spell : AilmentSource::Strike;
}

/** @brief Damage straight from a support, outside the hit: no more supports apply, but ailments do. */
void DealExtra(const Player &player, Monster &monster, DamageType type, Fixed26_6 damage)
{
	if (damage <= 0 || !CanHit(monster) || &player != MyPlayer)
		return;
	const Fixed26_6 dealt = Fixed26_6::fromRaw(Percent(damage.raw(), MonsterDamageTakenPercent(monster, type)));
	ApplyMonsterDamage(type, monster, dealt);
	monster.tag(player);
	TryInflictAilment(monster, player, type, dealt, HitSource());
	if (monster.hasNoLife())
		M_StartKill(monster, player);
}

void AddBurst(Point center, int radius, int delay, DamageType type, MissileGraphicID effect, Fixed26_6 damage, int pulses = 1, int interval = 0)
{
	Bursts.push_back({ center, static_cast<int8_t>(radius), static_cast<int16_t>(std::max(delay, 0)), static_cast<int8_t>(pulses), static_cast<int8_t>(interval), type, effect, damage });
}

void QueueUse(SkillSlot skill, Point target, int ticks, int percent, int monster = -1)
{
	Pending.push_back({ skill, target, static_cast<int16_t>(ticks), static_cast<int16_t>(percent), static_cast<int16_t>(monster) });
}

/** @brief Uses a skill for free, as a repeat or a trigger: it can't set off more supports. */
void UseSkillFree(Player &player, SkillSlot skill, Point source, Point target, int percent)
{
	if (player.hasNoLife() || leveltype == DTYPE_TOWN)
		return;
	const bool wasRepeating = Repeating;
	const int oldPercent = RepeatPercent;
	Repeating = true;
	RepeatPercent = RepeatPercent * percent / 100;
	if (skill.kind == SkillKind::Spell) {
		CastFreeSpell(player, skill.spell(), source, target, RepeatPercent);
	} else if (skill.kind == SkillKind::Attack) {
		const WeaponSkill previous = player.poe2.castSkill;
		player.poe2.castSkill = skill.attack();
		ExecuteSkillCast(player, skill.attack(), target);
		player.poe2.castSkill = previous;
	}
	Repeating = wasRepeating;
	RepeatPercent = oldPercent;
}

std::optional<size_t> SlotOf(const Player &player, SkillSlot skill)
{
	for (size_t slot = 0; slot < NumSkillSlots; slot++) {
		if (player.poe2.skillSlots[slot] == skill)
			return slot;
	}
	return std::nullopt;
}

Point NearestTarget(const Player &player, Point from, int range)
{
	const std::vector<Monster *> nearest = NearestMonsters(from, range, 1, nullptr);
	if (!nearest.empty())
		return nearest.front()->position.tile;
	return from + player._pdir;
}

PersistentSkill MinionSkillOf(const Monster &minion);

int MinionValue(const Player &owner, PersistentSkill skill, SupportGem gem, size_t index)
{
	return Value(owner, SkillSlot::ForPersistent(skill), gem, index);
}

PersistentSkill MinionSkillOf(const Monster &minion)
{
	if (minion.poe2.revived)
		return PersistentSkill::Revive;
	if (const MinionGemData *gem = FindMinionGemByType(minion.type().type); gem != nullptr)
		return gem->skill;
	if (static_cast<size_t>(minion.goalVar3) < Players.size()) {
		if (const PersistentSkill golem = ActiveGolemSkill(Players[static_cast<size_t>(minion.goalVar3)]); golem != PersistentSkill::None)
			return golem;
	}
	return PersistentSkill::SummonGolem;
}

bool MinionHas(const Player &owner, const Monster &minion, SupportGem gem)
{
	return Has(owner, SkillSlot::ForPersistent(MinionSkillOf(minion)), gem);
}

/** @brief Minions alive, and how many different kinds. */
std::pair<int, int> CountMinions(const Player &owner)
{
	int alive = 0;
	std::array<bool, static_cast<size_t>(PersistentSkill::Last) + 1> kinds {};
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		const Monster &monster = Monsters[ActiveMonsters[i]];
		if (!monster.isPlayerMinion() || monster.hasNoLife() || static_cast<size_t>(monster.goalVar3) != owner.getId() || monster.position.tile == GolemHoldingCell)
			continue;
		alive++;
		kinds[static_cast<size_t>(MinionSkillOf(monster)) % kinds.size()] = true;
	}
	return { alive, static_cast<int>(std::count(kinds.begin(), kinds.end(), true)) };
}

bool AnyMinionSkillHas(const Player &player, SupportGem gem)
{
	for (const PersistentSkill skill : { PersistentSkill::SummonGolem, PersistentSkill::BloodGolem, PersistentSkill::FireGolem, PersistentSkill::Revive }) {
		if (Has(player, SkillSlot::ForPersistent(skill), gem))
			return true;
	}
	for (const PersistentSkill skill : MinionCountSkills) {
		if (Has(player, SkillSlot::ForPersistent(skill), gem))
			return true;
	}
	return false;
}

void InflictPoison(const Player &player, Monster &monster, int32_t total, int ticks, int maxStacks)
{
	if (!AilmentAllowed(player, HitSource()))
		return;
	Poe2MonsterState &state = monster.poe2;
	const int32_t perTick = std::max<int32_t>(total / std::max(ticks, 1), 1);
	const int32_t cap = perTick * maxStacks;
	state.poisonDamagePerTick = std::min<int32_t>(state.poisonTicks > 0 ? state.poisonDamagePerTick + perTick : perTick, std::max<int32_t>(cap, state.poisonDamagePerTick));
	state.poisonTicks = static_cast<uint8_t>(std::max<int>(state.poisonTicks, ticks));
	state.poisonSource = static_cast<int8_t>(player.getId());
}

void InflictSupportBleed(const Player &player, Monster &monster, int32_t physical)
{
	Poe2MonsterState &state = monster.poe2;
	constexpr int Ticks = 4 * 20;
	const int32_t perTick = std::max<int32_t>(physical * 70 / 100 / Ticks, 1);
	if (state.bleedTicks == 0 || perTick >= state.bleedDamagePerTick) {
		state.bleedDamagePerTick = perTick;
		state.bleedSource = static_cast<int8_t>(player.getId());
	}
	state.bleedTicks = static_cast<uint8_t>(std::max<int>(state.bleedTicks, Ticks));
}

DamageType PrismElement(uint8_t element)
{
	constexpr DamageType Elements[] = { DamageType::Fire, DamageType::Lightning, DamageType::Magic };
	return Elements[element % 3];
}

/** @brief The supports' on-hit effects on the monster the hit landed on. */
/** @brief Fan the Flames II: a burning monster's ignite spreads to the 2 nearest. */
void SpreadIgnite(const Player &player, const Monster &monster)
{
	const Poe2MonsterState &state = monster.poe2;
	for (Monster *other : NearestMonsters(monster.position.tile, 3, 2, &monster)) {
		Poe2MonsterState &target = other->poe2;
		target.igniteTicks = std::max(target.igniteTicks, state.igniteTicks);
		target.igniteDamagePerTick = std::max(target.igniteDamagePerTick, state.igniteDamagePerTick);
		target.igniteSource = static_cast<int8_t>(player.getId());
		other->tag(player);
	}
}

void FinishHit()
{
	Monster *target = Hit.monster;
	const Player *source = Hit.player;
	if (target == nullptr || source == nullptr || Hit.total <= 0)
		return;
	Player &player = Players[source->getId()];
	Monster &monster = *target;
	Hit.finishing = true;
	const SkillSlot skill = Hit.skill;
	const int32_t total = static_cast<int32_t>(std::min<int64_t>(Hit.total, INT32_MAX));
	const int32_t physical = static_cast<int32_t>(std::min<int64_t>(Hit.physical, INT32_MAX));
	monster.poe2.lastHitSkillKind = static_cast<uint8_t>(skill.kind);
	monster.poe2.lastHitSkillId = skill.id;
	monster.poe2.lastHitPlayer = static_cast<int8_t>(player.getId());
	SupportRuntime &state = player.poe2.supportState;

	// Leech first, so it counts the whole hit.
	if (Has(player, skill, SG::LifeLeech) && player._pHitPoints > 0) {
		const Fixed26_6 heal = Fixed26_6::fromRaw(Percent(total, Value(player, skill, SG::LifeLeech, 0)));
		// Life Leech III: what full life can't take is kept for a moment.
		const int32_t room = std::max<int32_t>(player._pMaxHP.raw() - player._pHitPoints.raw(), 0);
		if (Value(player, skill, SG::LifeLeech, 1) > 0 && heal.raw() > room) {
			state.leechPool = std::min<int32_t>(state.leechPool + heal.raw() - room, player._pMaxHP.raw());
			state.leechPoolTicks = LeechPoolTicks;
		}
		player._pHitPoints = std::min(player._pHitPoints + heal, player._pMaxHP);
		player._pHPBase = std::min(player._pHPBase + heal, player._pMaxHPBase);
		RedrawComponent(PanelDrawComponent::Health);
	}
	if (Has(player, skill, SG::ManaLeech)) {
		const Fixed26_6 mana = Fixed26_6::fromRaw(total * 2 / 100);
		player._pMana = std::min(player._pMana + mana, player._pMaxMana);
		player._pManaBase = std::min(player._pManaBase + mana, player._pMaxManaBase);
		RedrawComponent(PanelDrawComponent::Mana);
	}

	const bool alive = CanHit(monster);
	if (alive) {
		if (Has(player, skill, SG::WitheringTouch)) {
			monster.poe2.witherStacks = static_cast<uint8_t>(std::min<int>(monster.poe2.witherStacks + 1, MaxWitherStacks));
			monster.poe2.witherTicks = WitherTicks;
		}
		if (Has(player, skill, SG::SerratedEdge) && physical > 0 && GenerateRnd(100) < Value(player, skill, SG::SerratedEdge, 0))
			InflictSupportBleed(player, monster, physical);
		if (Has(player, skill, SG::Envenom) && physical > 0 && GenerateRnd(100) < Value(player, skill, SG::Envenom, 0))
			InflictPoison(player, monster, physical * 30 / 100, 40, 20);
		if (Has(player, skill, SG::AndarielsAnguish))
			InflictPoison(player, monster, total * 10 / 100 * 4, 80, 5);
		if (Has(player, skill, SG::SunderArmour) && physical > 0) {
			BreakMonsterArmour(monster, &player);
			monster.poe2.armourBreakTicks = static_cast<uint8_t>(std::max<int>(monster.poe2.armourBreakTicks, Value(player, skill, SG::SunderArmour, 0) * TicksPerSecond));
		}
		if (Has(player, skill, SG::ButchersHook)) {
			InflictSupportBleed(player, monster, std::max(physical, total / 2));
			if (!monster.isUnique() && monster.position.tile.WalkingDistance(player.position.tile) > 1) {
				const Point behind = monster.position.tile + (monster.position.tile - player.position.tile);
				M_GetKnockback(monster, behind);
			}
		}
		if (Hit.wirt && !monster.isUnique())
			M_GetKnockback(monster, player.position.tile);
	}

	// Extra damage: infusions, Holy Infusion, Fuel the Pyre, Frostbite and Tal Rasha's Prism.
	if (alive) {
		if (physical > 0) {
			if (Has(player, skill, SG::InfusedFlames))
				DealExtra(player, monster, DamageType::Fire, Fixed26_6::fromRaw(physical));
			else if (Has(player, skill, SG::InfusedStorm))
				DealExtra(player, monster, DamageType::Lightning, Fixed26_6::fromRaw(physical));
			else if (Has(player, skill, SG::InfusedFrost))
				DealExtra(player, monster, DamageType::Magic, Fixed26_6::fromRaw(physical));
		}
		if (Has(player, skill, SG::HolyInfusion))
			DealExtra(player, monster, DamageType::Magic, Fixed26_6::fromRaw(total / 5));
		if (Has(player, skill, SG::FuelThePyre) && monster.poe2.igniteTicks > 0) {
			DealExtra(player, monster, DamageType::Fire, Fixed26_6::fromRaw(Percent(total, Value(player, skill, SG::FuelThePyre, 0))));
			// Fan the Flames II: the burn spreads.
			if (Value(player, skill, SG::FuelThePyre, 1) > 0)
				SpreadIgnite(player, monster);
		}
		if (Has(player, skill, SG::Frostbite) && state.frostbiteTicks > 0)
			DealExtra(player, monster, DamageType::Magic, Fixed26_6::fromRaw(total / 4));
		if (Has(player, skill, SG::TalRashasPrism))
			DealExtra(player, monster, PrismElement(state.prismElement), Fixed26_6::fromRaw(total * 3 / 10));
		if (Has(player, skill, SG::BartucsBloodletting) && state.bartucTicks > 0)
			DealExtra(player, monster, DamageType::Physical, Fixed26_6::fromRaw(state.bartucDamage));
	}

	// Things that spread from the hit.
	if (Has(player, skill, SG::Conductor) && monster.poe2.shockTicks > 0) {
		for (Monster *other : NearestMonsters(monster.position.tile, 5, 1, &monster)) {
			AddVisualEffect(other->position.tile, MissileGraphicID::Lightning);
			DealExtra(player, *other, DamageType::Lightning, Fixed26_6::fromRaw(total / 2));
		}
	}
	if (Has(player, skill, SG::DiablosRedLightning) && Hit.lastType == DamageType::Lightning && monster.hitPoints.raw() * 5 < monster.maxHitPoints.raw()) {
		AddVisualEffect(monster.position.tile, MissileGraphicID::RedFlareExplosion, 0, 6);
		for (Monster *other : NearestMonsters(monster.position.tile, 6, 3, &monster)) {
			AddVisualEffect(other->position.tile, MissileGraphicID::Lightning);
			DealExtra(player, *other, DamageType::Lightning, Fixed26_6::fromRaw(total / 2));
		}
	}
	if (Has(player, skill, SG::Shatter) && (SkillTags(skill) & ST_Slam) != 0 && (monster.poe2.freezeTicks > 0 || monster.mode == MonsterMode::Petrified)) {
		AddVisualEffect(monster.position.tile, MissileGraphicID::StoneCurseShatter, 0, 6);
		for (Monster *other : NearestMonsters(monster.position.tile, 2, 8, &monster))
			DealExtra(player, *other, DamageType::Magic, Fixed26_6::fromRaw(total * 3 / 10));
	}
	if (Has(player, skill, SG::BurningGround) && state.burningGroundCooldown == 0) {
		state.burningGroundCooldown = 20;
		AddBurst(monster.position.tile, Value(player, skill, SG::BurningGround, 1), 0, DamageType::Fire, MissileGraphicID::MagmaBallExplosion, Fixed26_6::fromRaw(std::max<int32_t>(total / 10, 64)), Value(player, skill, SG::BurningGround, 0), 10);
	}
	// Multistrike: the hit strikes twice more.
	if (!Repeating && Has(player, skill, SG::Multistrike) && alive) {
		QueueUse(skill, monster.position.tile, 4, MultistrikePercent, static_cast<int>(monster.getId()));
		QueueUse(skill, monster.position.tile, 8, MultistrikePercent, static_cast<int>(monster.getId()));
	}
	Hit.finishing = false;
}

void PassDamageOverTime(const Player &player, const Monster &from)
{
	const Poe2MonsterState &state = from.poe2;
	for (Monster *other : NearestMonsters(from.position.tile, 4, 2, &from)) {
		Poe2MonsterState &target = other->poe2;
		if (state.bleedTicks > 0) {
			target.bleedTicks = std::max(target.bleedTicks, state.bleedTicks);
			target.bleedDamagePerTick = std::max(target.bleedDamagePerTick, state.bleedDamagePerTick * 4 / 5);
			target.bleedSource = static_cast<int8_t>(player.getId());
		}
		if (state.poisonTicks > 0) {
			target.poisonTicks = std::max(target.poisonTicks, state.poisonTicks);
			target.poisonDamagePerTick = std::max(target.poisonDamagePerTick, state.poisonDamagePerTick * 4 / 5);
			target.poisonSource = static_cast<int8_t>(player.getId());
		}
		if (state.igniteTicks > 0) {
			target.igniteTicks = std::max(target.igniteTicks, state.igniteTicks);
			target.igniteDamagePerTick = std::max(target.igniteDamagePerTick, state.igniteDamagePerTick * 4 / 5);
			target.igniteSource = static_cast<int8_t>(player.getId());
		}
		other->tag(player);
		AddVisualEffect(other->position.tile, MissileGraphicID::BloodHit);
	}
}

void PassCurses(const Player &player, const Monster &from)
{
	const Poe2MonsterState &state = from.poe2;
	for (Monster *other : NearestMonsters(from.position.tile, 6, 2, &from)) {
		Poe2MonsterState &target = other->poe2;
		target.weakenTicks = std::max(target.weakenTicks, state.weakenTicks);
		target.decrepifyTicks = std::max(target.decrepifyTicks, state.decrepifyTicks);
		target.ironMaidenTicks = std::max(target.ironMaidenTicks, state.ironMaidenTicks);
		target.lowerResistTicks = std::max(target.lowerResistTicks, state.lowerResistTicks);
		target.deathMarkTicks = std::max(target.deathMarkTicks, state.deathMarkTicks);
		if (state.amplifyTicks > 0) {
			target.amplifyTicks = std::max(target.amplifyTicks, state.amplifyTicks);
			target.amplifyPercent = std::max(target.amplifyPercent, state.amplifyPercent);
		}
		other->tag(player);
		AddVisualEffect(other->position.tile, MissileGraphicID::BloodStarExplosion);
	}
}

bool IsCursed(const Monster &monster)
{
	const Poe2MonsterState &state = monster.poe2;
	return state.weakenTicks > 0 || state.decrepifyTicks > 0 || state.ironMaidenTicks > 0 || state.lowerResistTicks > 0 || state.deathMarkTicks > 0 || state.amplifyTicks > 0;
}

void ProcessBursts(Player &player)
{
	for (size_t i = 0; i < Bursts.size();) {
		GroundBurst &burst = Bursts[i];
		if (burst.ticks > 0) {
			burst.ticks--;
			i++;
			continue;
		}
		AddVisualEffect(burst.center, burst.effect, 0, 4);
		const GroundBurst copy = burst;
		std::vector<Monster *> targets;
		ForEachMonsterNear(copy.center, copy.radius, [&](Monster &monster) { targets.push_back(&monster); });
		for (Monster *monster : targets)
			DealExtra(player, *monster, copy.type, copy.damage);
		Bursts[i].pulsesLeft--;
		if (Bursts[i].pulsesLeft <= 0) {
			Bursts.erase(Bursts.begin() + static_cast<std::ptrdiff_t>(i));
		} else {
			Bursts[i].ticks = Bursts[i].interval;
			i++;
		}
	}
}

void ProcessPending(Player &player)
{
	std::vector<PendingUse> due;
	for (size_t i = 0; i < Pending.size();) {
		if (Pending[i].ticks-- > 0) {
			i++;
			continue;
		}
		due.push_back(Pending[i]);
		Pending.erase(Pending.begin() + static_cast<std::ptrdiff_t>(i));
	}
	for (const PendingUse &use : due) {
		if (use.monster >= 0) {
			Monster &monster = Monsters[use.monster];
			if (!CanHit(monster) || monster.position.tile.WalkingDistance(player.position.tile) > 2)
				continue;
			const bool wasRepeating = Repeating;
			const int oldPercent = RepeatPercent;
			Repeating = true;
			RepeatPercent = use.percent;
			const WeaponSkill previous = player.poe2.castSkill;
			player.poe2.castSkill = use.skill.attack();
			AddVisualEffect(monster.position.tile, MissileGraphicID::MetalHit);
			SkillHitMonster(player, monster, 100);
			player.poe2.castSkill = previous;
			Repeating = wasRepeating;
			RepeatPercent = oldPercent;
		} else {
			// The shadow of the hero: a flash where the echo comes from.
			AddVisualEffect(player.position.tile, MissileGraphicID::Etherealize);
			UseSkillFree(player, use.skill, player.position.tile, use.target, use.percent);
		}
	}
}

void ProcessStands(Player &player)
{
	for (size_t i = 0; i < Stands.size();) {
		Stand &stand = Stands[i];
		if (--stand.ticksLeft <= 0) {
			AddVisualEffect(stand.position, MissileGraphicID::StoneCurseShatter);
			Stands.erase(Stands.begin() + static_cast<std::ptrdiff_t>(i));
			continue;
		}
		// A totem goes when its gem leaves the row, or the row has nothing it can cast.
		uint8_t next = stand.next;
		if (!stand.decoy && (stand.row < 0 || NextTotemSpell(player, static_cast<size_t>(stand.row), next).isEmpty())) {
			AddVisualEffect(stand.position, MissileGraphicID::StoneCurseShatter);
			Stands.erase(Stands.begin() + static_cast<std::ptrdiff_t>(i));
			continue;
		}
		if (stand.ticksLeft % 20 == 0)
			AddVisualEffect(stand.position, stand.decoy ? MissileGraphicID::Etherealize : MissileGraphicID::HolyBoltExplosion);
		if (stand.cooldown > 0) {
			stand.cooldown--;
			i++;
			continue;
		}
		const std::vector<Monster *> nearest = NearestMonsters(stand.position, 8, 1, nullptr);
		if (nearest.empty()) {
			i++;
			continue;
		}
		const Point target = nearest.front()->position.tile;
		if (stand.decoy) {
			stand.cooldown = DecoyShotTicks;
			Missile *arrow = AddMissile(stand.position, target, GetDirection(stand.position, target), MissileID::Arrow, TARGET_MONSTERS, player, ElementalArrowBaseDamage, 0);
			if (arrow != nullptr) {
				arrow->poe2.damagePercent = static_cast<int16_t>(std::min(GemDamagePercent(stand.skill, SkillLevel(player, stand.skill)) * MirageDamagePercent / 100, 32000));
				SetMissileSkill(*arrow, player, stand.skill);
			}
			PlaySfxLoc(SfxID::ShootBow, stand.position);
		} else {
			const SkillSlot skill = NextTotemSpell(player, static_cast<size_t>(stand.row), stand.next);
			stand.cooldown = HoradricTotemCastTicks;
			stand.skill = skill;
			const Point from = stand.position;
			AddVisualEffect(from, MissileGraphicID::HolyBoltExplosion);
			UseSkillFree(player, skill, from, target, 100);
		}
		i++;
	}
}

int CountStands(bool decoy)
{
	return static_cast<int>(std::count_if(Stands.begin(), Stands.end(), [&](const Stand &stand) { return stand.decoy == decoy; }));
}

} // namespace

uint32_t SkillTags(SkillSlot skill)
{
	constexpr uint32_t Hit = ST_Damage;
	switch (skill.kind) {
	case SkillKind::Persistent:
		switch (skill.persistent()) {
		case PersistentSkill::SummonGolem:
		case PersistentSkill::SkeletalWarriors:
		case PersistentSkill::SkeletalMages:
		case PersistentSkill::SkeletalSnipers:
		case PersistentSkill::SkeletalFrostMages:
		case PersistentSkill::SkeletalStormMages:
		case PersistentSkill::SkeletalReavers:
		case PersistentSkill::SkeletalBrutes:
		case PersistentSkill::SkeletalClerics:
		case PersistentSkill::BloodGolem:
		case PersistentSkill::Revive:
			return ST_Minion;
		case PersistentSkill::FireGolem:
			return ST_Minion | ST_Fire;
		// A herald's explosion is an area hit of its element.
		case PersistentSkill::HeraldOfAsh:
			return ST_Herald | ST_Area | ST_Fire | Hit;
		case PersistentSkill::HeraldOfThunder:
			return ST_Herald | ST_Area | ST_Lightning | Hit;
		case PersistentSkill::HeraldOfFrost:
			return ST_Herald | ST_Area | ST_Frost | Hit;
		case PersistentSkill::HeraldOfPlague:
			return ST_Herald | ST_Area | ST_Physical | ST_DamageOverTime;
		case PersistentSkill::HeraldOfBlood:
			return ST_Herald | ST_Area | ST_Physical | Hit;
		default:
			return 0;
		}
	case SkillKind::Spell:
		switch (skill.spell()) {
		case SpellID::Firebolt:
			return ST_Spell | ST_Projectile | ST_Fire | Hit;
		case SpellID::Fireball:
			return ST_Spell | ST_Projectile | ST_Area | ST_Fire | Hit;
		case SpellID::FireWall:
		case SpellID::RingOfFire:
			return ST_Spell | ST_Area | ST_Duration | ST_Placed | ST_Fire | Hit;
		case SpellID::FlameWave:
		case SpellID::Inferno:
		case SpellID::Immolation:
		case SpellID::Apocalypse:
			return ST_Spell | ST_Area | ST_Fire | Hit;
		case SpellID::Elemental:
			return ST_Spell | ST_Projectile | ST_Area | ST_Fire | Hit;
		case SpellID::Guardian:
			return ST_Spell | ST_Duration | ST_Placed | ST_Projectile | ST_Fire | Hit;
		case SpellID::Lightning:
		case SpellID::ChargedBolt:
			return ST_Spell | ST_Projectile | ST_Lightning | Hit;
		case SpellID::ChainLightning:
		case SpellID::Nova:
		case SpellID::Flash:
			return ST_Spell | ST_Area | ST_Lightning | Hit;
		case SpellID::LightningWall:
			return ST_Spell | ST_Area | ST_Duration | ST_Placed | ST_Lightning | Hit;
		case SpellID::HolyBolt:
		case SpellID::BloodStar:
		case SpellID::BoneSpirit:
			return ST_Spell | ST_Projectile | Hit;
		case SpellID::RuneOfFire:
		case SpellID::RuneOfImmolation:
			return ST_Spell | ST_Rune | ST_Placed | ST_Area | ST_Fire | Hit;
		case SpellID::RuneOfLight:
		case SpellID::RuneOfNova:
			return ST_Spell | ST_Rune | ST_Placed | ST_Area | ST_Lightning | Hit;
		case SpellID::RuneOfStone:
			return ST_Spell | ST_Rune | ST_Placed | ST_Area | ST_Curse;
		case SpellID::StoneCurse:
			return ST_Spell | ST_Curse | ST_Duration;
		case SpellID::Healing:
		case SpellID::HealOther:
			return ST_Spell | ST_Healing;
		case SpellID::Teleport:
		case SpellID::Phasing:
		case SpellID::Warp:
			return ST_Spell | ST_Movement;
		case SpellID::ManaShield:
		case SpellID::Etherealize:
		case SpellID::Reflect:
		case SpellID::Berserk:
			return ST_Spell | ST_Duration;
		case SpellID::Golem:
			return ST_Spell | ST_Minion;
		case SpellID::Invalid:
			return 0;
		default:
			return ST_Spell;
		}
	case SkillKind::Attack:
		break;
	case SkillKind::Default:
		switch (static_cast<DefaultAttack>(skill.id)) {
		case DefaultAttack::BowShot:
			return ST_Attack | ST_Projectile | ST_Bow | ST_Physical | Hit;
		case DefaultAttack::StaffBolt:
			return ST_Attack | ST_Projectile | ST_Staff | ST_Physical | Hit;
		case DefaultAttack::StaffStrike:
			return ST_Attack | ST_Melee | ST_Strike | ST_Staff | ST_Physical | Hit;
		default:
			return ST_Attack | ST_Melee | ST_Strike | ST_Physical | Hit;
		}
	default:
		return 0;
	}

	const WeaponSkill attack = skill.attack();
	switch (attack) {
	case WeaponSkill::Cleave:
		return ST_Attack | ST_Melee | ST_Area | ST_Strike | ST_Physical | Hit;
	case WeaponSkill::GroundSlam:
		return ST_Attack | ST_Melee | ST_Area | ST_Slam | ST_Physical | Hit;
	case WeaponSkill::Barrage:
		return ST_Attack | ST_Projectile | ST_Bow | ST_Channelling | ST_Physical | Hit;
	case WeaponSkill::WhirlingStaff:
		return ST_Attack | ST_Melee | ST_Area | ST_Staff | ST_Physical | Hit;
	case WeaponSkill::Flurry:
	case WeaponSkill::Riposte:
	case WeaponSkill::Frenzy:
	case WeaponSkill::ShieldBash:
		return ST_Attack | ST_Melee | ST_Strike | ST_Physical | Hit;
	case WeaponSkill::LeapSlam:
		return ST_Attack | ST_Melee | ST_Area | ST_Slam | ST_Movement | ST_Physical | Hit;
	case WeaponSkill::RainOfArrows:
		return ST_Attack | ST_Projectile | ST_Area | ST_Bow | ST_Physical | Hit;
	case WeaponSkill::ShieldCharge:
		return ST_Attack | ST_Melee | ST_Movement | ST_Physical | Hit;
	case WeaponSkill::BattleCry:
		return ST_Warcry | ST_Area | ST_Duration;
	case WeaponSkill::LightningArrow:
		return ST_Attack | ST_Projectile | ST_Area | ST_Bow | ST_Lightning | Hit;
	case WeaponSkill::EscapeShot:
		return ST_Attack | ST_Projectile | ST_Bow | ST_Movement | ST_Physical | Hit;
	case WeaponSkill::EmberFusillade:
		return ST_Spell | ST_Projectile | ST_Channelling | ST_Fire | Hit;
	case WeaponSkill::Spark:
		return ST_Spell | ST_Projectile | ST_Duration | ST_Lightning | Hit;
	case WeaponSkill::IceBolt:
		return ST_Spell | ST_Projectile | ST_Frost | Hit;
	case WeaponSkill::FrostNova:
		return ST_Spell | ST_Area | ST_Frost | Hit;
	case WeaponSkill::ShadowStep:
		return ST_Attack | ST_Melee | ST_Movement | ST_Strike | ST_Physical | Hit;
	case WeaponSkill::Smite:
		return ST_Attack | ST_Melee | ST_Slam | ST_Area | ST_Physical | ST_Lightning | Hit;
	case WeaponSkill::FistsOfThunder:
		return ST_Attack | ST_Melee | ST_Staff | ST_Strike | ST_Lightning | Hit;
	case WeaponSkill::RapidFire:
		return ST_Attack | ST_Projectile | ST_Bow | ST_Channelling | ST_Physical | Hit;
	case WeaponSkill::AmplifyDamage:
		return ST_Spell | ST_Curse | ST_Area | ST_Duration;
	default:
		break;
	}

	const TableSkillData *table = FindTableSkill(attack);
	if (table == nullptr)
		return 0;
	uint32_t tags = 0;
	const bool spell = table->weapon == TableWeapon::None && (table->flags & TF_Spell) != 0;
	tags |= spell ? ST_Spell : ST_Attack;
	if (table->weapon == TableWeapon::None && table->shape == TableShape::Curse)
		tags = ST_Spell;
	if (IsTableMeleeSkill(*table))
		tags |= ST_Melee;
	if (IsTableProjectileSkill(*table))
		tags |= ST_Projectile;
	if (IsTableAreaSkill(*table))
		tags |= ST_Area;
	if (IsAnyOf(table->weapon, TableWeapon::Bow, TableWeapon::Crossbow))
		tags |= ST_Bow;
	if (table->weapon == TableWeapon::Staff)
		tags |= ST_Staff;
	switch (table->shape) {
	case TableShape::Strike:
	case TableShape::Flurry:
	case TableShape::Seven:
		tags |= ST_Strike;
		break;
	case TableShape::Dash:
		tags |= ST_Movement;
		break;
	case TableShape::Stream:
		tags |= ST_Channelling;
		break;
	case TableShape::Zone:
		tags |= ST_Duration;
		if (IsAnyOf(table->zone, TableZone::Turret, TableZone::Trap) || table->skill == WeaponSkill::BladeSentinel)
			tags |= ST_Placed;
		break;
	case TableShape::Curse:
		tags |= ST_Curse | ST_Duration | ST_Area;
		break;
	case TableShape::Buff:
		tags |= ST_Duration;
		if (table->buff == TableBuff::WarCry)
			tags = (tags & ~(ST_Attack | ST_Spell)) | ST_Warcry | ST_Area;
		break;
	default:
		break;
	}
	if (table->weapon == TableWeapon::Mace && IsAnyOf(table->shape, TableShape::Burst, TableShape::Line, TableShape::Around, TableShape::Arc))
		tags |= ST_Slam;
	if (table->skill == WeaponSkill::FallingSword)
		tags |= ST_Slam;
	const bool damages = table->damagePercent > 0 && table->shape != TableShape::Buff && (table->shape != TableShape::Curse || IsAnyOf(table->curse, TableCurse::Haunt, TableCurse::Locust))
	    && table->shape != TableShape::SoulHarvest;
	if (damages) {
		tags |= Hit;
		switch (table->type) {
		case DamageType::Fire:
			tags |= ST_Fire;
			break;
		case DamageType::Lightning:
			tags |= ST_Lightning;
			break;
		case DamageType::Magic:
			tags |= ST_Frost;
			break;
		default:
			tags |= ST_Physical;
			break;
		}
		if ((table->flags & TF_Elements) != 0)
			tags |= ST_Elemental;
		if ((table->flags & TF_Bleed) != 0 || table->shape == TableShape::Curse)
			tags |= ST_DamageOverTime;
	}
	return tags;
}

std::span<const SupportData> SupportTable()
{
	return Table;
}

const SupportData *FindSupportData(SupportGem gem)
{
	const auto index = static_cast<size_t>(gem);
	if (index < FirstTableSupport || index >= static_cast<size_t>(SupportGem::Count))
		return nullptr;
	return &Table[index - FirstTableSupport];
}

SupportGem SupportGemForTableItem(const ItemData &data)
{
	for (const SupportData &support : Table) {
		const std::string_view name = data.iName;
		if (name.size() == support.name.size() + 8 && name.starts_with(support.name) && name.ends_with(" Support"))
			return support.gem;
	}
	return SupportGem::None;
}

bool IsLineageSupport(SupportGem gem)
{
	const SupportData *data = FindSupportData(gem);
	return data != nullptr && (data->flags & SF_Lineage) != 0;
}

bool TableSupportFits(const SupportData &data, SkillSlot skill)
{
	const uint32_t tags = SkillTags(skill);
	if (skill.kind == SkillKind::Persistent) {
		if ((tags & ST_Herald) != 0) {
			// Like POE2, supports for skills you use yourself don't fit a herald: what it pays with, moving first and
			// the curse. Gharbad's Greed would drop loot from every monster of a chain of explosions.
			if (IsAnyOf(data.gem, SG::BloodPrice, SG::BartucsBloodletting, SG::Momentum, SG::GharbadsGreed))
				return false;
		} else if ((data.needAll & ST_Minion) == 0) {
			return false;
		}
	}
	if (data.needAny != 0 && (tags & data.needAny) == 0)
		return false;
	if ((tags & data.needAll) != data.needAll || (tags & data.exclude) != 0)
		return false;
	// The default attack doesn't go through OnSkillUsed, so supports that act when a skill is used don't fit it.
	if (skill.kind == SkillKind::Default
	    && IsAnyOf(data.gem, SG::SpellEcho, SG::HoradricSeals, SG::BaalsEcho, SG::EchoingShout, SG::Cascade, SG::RuneMastery, SG::ClusterRunes, SG::NaKrulsPrison,
	        SG::Aftershock, SG::ImmortalKingsStride, SG::RallyingCry, SG::BlinkStrike, SG::Sanctify, SG::Wrathforged, SG::ComboFinisher, SG::TalRashasPrism,
	        SG::KashyasVolley, SG::ZharsMadness))
		return false;
	switch (data.gem) {
	case SG::BoneArmy:
		return FindMinionGem(skill.persistent()) != nullptr;
	case SG::NaKrulsPrison:
		return IsAnyOf(skill.spell(), SpellID::FireWall, SpellID::LightningWall);
	default:
		return true;
	}
}

bool SupportsConflict(SupportGem a, SupportGem b)
{
	if (a == SupportGem::None || b == SupportGem::None || a == b)
		return false;
	const auto familyOf = [](SupportGem gem) -> int {
		if (gem == SupportGem::MultipleProjectiles)
			return MoreProjectiles;
		const SupportData *data = FindSupportData(gem);
		return data != nullptr ? data->family : NoFamily;
	};
	const int family = familyOf(a);
	if (family != NoFamily && family == familyOf(b))
		return true;
	// Infusions turn physical damage into an element, which Brutality forbids.
	const auto infusion = [](SupportGem gem) { return IsAnyOf(gem, SG::InfusedFlames, SG::InfusedStorm, SG::InfusedFrost, SG::HolyInfusion); };
	return (infusion(a) && b == SupportGem::Brutality) || (infusion(b) && a == SupportGem::Brutality);
}

bool LineageSupportInUse(const Poe2PlayerData &data, SupportGem gem, size_t slot, size_t socket)
{
	for (size_t s = 0; s <= DefaultAttackRow; s++) {
		for (size_t i = 0; i < MaxSupportSockets; i++) {
			if ((s != slot || i != socket) && data.supportsAt(s)[i] == gem)
				return true;
		}
	}
	return false;
}

SupportHitScope::SupportHitScope(const Player &player, SkillSlot skill)
    : active_(gbIsPoe2 && &player == MyPlayer)
{
	if (!active_)
		return;
	if (Hit.depth++ > 0)
		return;
	Hit.player = &player;
	Hit.skill = skill;
	Hit.monster = nullptr;
	Hit.physical = 0;
	Hit.total = 0;
	Hit.finishing = false;
	Hit.wirt = Has(player, skill, SG::WirtsLeg) && GenerateRnd(100) < 10;
}

SupportHitScope::~SupportHitScope()
{
	if (!active_ || --Hit.depth > 0)
		return;
	FinishHit();
	Hit.player = nullptr;
	Hit.monster = nullptr;
	Hit.skill = {};
}

SkillSlot MissileSkill(const Missile &missile)
{
	return { static_cast<SkillKind>(missile.poe2.skillKind), missile.poe2.skillId };
}

void SetMissileSkill(Missile &missile, const Player &player, SkillSlot skill)
{
	missile.poe2.skillKind = static_cast<uint8_t>(skill.kind);
	missile.poe2.skillId = skill.id;
	if (Has(player, skill, SG::Ricochet) && GenerateRnd(100) < Value(player, skill, SG::Ricochet, 0))
		missile.poe2.ricochet = true;
}

void NoteSupportedDamage(Monster &monster, DamageType type, Fixed26_6 damage)
{
	if (Hit.depth == 0 || Hit.finishing || Hit.player == nullptr || damage <= 0)
		return;
	if (Hit.monster != nullptr && Hit.monster != &monster) {
		// One scope hit several monsters: finish the first before counting the next.
		FinishHit();
		Hit.physical = 0;
		Hit.total = 0;
	}
	Hit.monster = &monster;
	Hit.total += damage.raw();
	Hit.lastType = type;
	// Trinity: the hit's element builds Resonance.
	OnSpiritElementalHit(*Hit.player, type);
	if (type == DamageType::Physical)
		Hit.physical += damage.raw();
}

int SupportHitPenetration(DamageType type)
{
	if (Hit.depth == 0 || Hit.finishing || Hit.player == nullptr || Hit.skill.isEmpty())
		return 0;
	switch (type) {
	case DamageType::Fire:
		return Value(*Hit.player, Hit.skill, SG::FirePenetration, 0);
	case DamageType::Lightning:
		return Value(*Hit.player, Hit.skill, SG::LightningPenetration, 0);
	case DamageType::Magic:
		return Value(*Hit.player, Hit.skill, SG::ColdPenetration, 0);
	default:
		return 0;
	}
}

int SupportHitDamagePercent(const Monster &monster, DamageType type)
{
	int percent = 100;
	if (monster.poe2.witherStacks > 0)
		percent += monster.poe2.witherStacks * WitherPercentPerStack;
	if (Hit.depth == 0 || Hit.finishing || Hit.player == nullptr)
		return percent;
	const Player &player = *Hit.player;
	const SkillSlot skill = Hit.skill;
	const SupportRuntime &state = player.poe2.supportState;
	const auto more = [&](int amount) { percent = percent * (100 + amount) / 100; };
	more(RepeatPercent - 100);
	if (skill.isEmpty())
		return percent;
	const bool elemental = IsAnyOf(type, DamageType::Fire, DamageType::Lightning, DamageType::Magic);
	for (const SupportGem gem : player.poe2.supportsFor(skill)) {
		switch (gem) {
		case SG::Volley:
			more(-25);
			break;
		case SG::CloseCombat: {
			// The most next to the monster, none 4 tiles away.
			const int distance = std::max(player.position.tile.WalkingDistance(monster.position.tile), 1);
			more(std::max(0, Value(player, skill, gem, 0) * (4 - distance) / 3));
			break;
		}
		case SG::MartialTempo:
			more(-Value(player, skill, gem, 1));
			break;
		case SG::Brutality:
			// Brutality III: some hits ignore armour, as if it were broken.
			if (type == DamageType::Physical && monster.poe2.armourBreakTicks == 0 && GenerateRnd(100) < Value(player, skill, gem, 1))
				more(ArmourBreakDamagePercent);
			break;
		case SG::InfusedFlames:
		case SG::InfusedStorm:
		case SG::InfusedFrost:
			if (type == DamageType::Physical)
				more(-50);
			break;
		case SG::BurningGround:
		case SG::DeepFreeze:
			more(-20);
			break;
		case SG::Overcharge:
		case SG::Hellfire:
			more(-25);
			break;
		case SG::WitheringTouch:
		case SG::Envenom:
		case SG::GharbadsGreed:
			more(-15);
			break;
		case SG::LifeLeech:
		case SG::ManaLeech:
			more(-10);
			break;
		case SG::ElementalFocus:
			if (elemental)
				more(25);
			break;
		case SG::DeadlyHerald:
			more(Value(player, skill, gem, 0));
			break;
		case SG::Bloodlust:
			if (type == DamageType::Physical && monster.poe2.bleedTicks > 0)
				more(30);
			break;
		case SG::Cascade:
			more(-30);
			break;
		case SG::NovaProjectiles:
			more(-Value(player, skill, gem, 1));
			break;
		case SG::Momentum:
			if (state.movedTiles >= 2)
				more(40);
			break;
		case SG::CrushingBlow:
			if (type == DamageType::Physical)
				more(35);
			break;
		case SG::DeliberateCasting:
			more(35);
			break;
		case SG::BrimmingMana:
			if (player._pMaxMana > 0 && player._pMana.raw() * 10 >= player._pMaxMana.raw() * 9)
				more(Value(player, skill, gem, 0));
			break;
		case SG::CoupDeGrace:
			if (monster.hitPoints.raw() * 100 < monster.maxHitPoints.raw() * Value(player, skill, gem, 1))
				more(Value(player, skill, gem, 0));
			break;
		case SG::Shatter:
			if (monster.poe2.freezeTicks == 0 && monster.mode != MonsterMode::Petrified)
				more(-15);
			break;
		case SG::KashyasVolley:
			break;
		case SG::WirtsLeg:
			if (Hit.wirt)
				more(200);
			break;
		default:
			break;
		}
	}
	if (state.spentBonusTicks > 0)
		more(state.spentBonusPercent);
	// Trag'Oul's Balance: more damage for every minion alive.
	if (AnyMinionSkillHas(player, SG::TragOulsBalance))
		more(5 * CountMinions(player).first);
	return std::max(percent, 0);
}

int SupportAilmentChancePercent(DamageType type)
{
	if (Hit.depth == 0 || Hit.player == nullptr)
		return 100;
	int percent = 100;
	if (HitHas(SG::ElementalFocus))
		return 0;
	if (type == DamageType::Lightning) {
		if (HitHas(SG::Conductor) || HitHas(SG::DiablosRedLightning))
			return 0;
		if (HitHas(SG::Overcharge))
			percent *= 2;
	}
	if (type == DamageType::Fire && (HitHas(SG::FuelThePyre) || HitHas(SG::AndarielsAnguish)))
		return 0;
	return percent;
}

int SupportAilmentEffectPercent(DamageType type)
{
	if (Hit.depth == 0 || Hit.player == nullptr)
		return 100;
	if (type == DamageType::Fire && HitHas(SG::Hellfire))
		return 100 + HitValue(SG::Hellfire, 0);
	if (type == DamageType::Magic && HitHas(SG::DeepFreeze))
		return 175;
	if (type == DamageType::Magic && HitHas(SG::ElementalFocus))
		return 0;
	return 100;
}

void OnSupportedFreeze(const Player &player)
{
	if (HitHas(SG::Frostbite))
		player.poe2.supportState.frostbiteTicks = FrostbiteTicks;
}

void OnSkillUsed(Player &player, SkillSlot skill, Point target)
{
	if (!gbIsPoe2 || &player != MyPlayer || skill.isEmpty() || Repeating || IsCastingRepeat(player))
		return;
	// Shadow Warrior: a shadow beside you repeats the skill.
	if (ShadowWarriorActive(player)) {
		QueueUse(skill, target, ShadowWarriorDelay, ShadowWarriorPercent);
		AddVisualEffect(player.position.tile + Left(Left(player._pdir)), MissileGraphicID::Etherealize, 0, 2);
	}
	SupportRuntime &state = player.poe2.supportState;
	const uint32_t tags = SkillTags(skill);
	const std::optional<size_t> slot = SlotOf(player, skill);
	const Point tile = player.position.tile;
	state.spentBonusTicks = 0;
	if (skill.kind == SkillKind::Spell)
		NoteRecentSpell(state, skill.id);

	for (const SupportGem gem : player.poe2.supportsFor(skill)) {
		switch (gem) {
		case SG::SpellEcho:
			QueueUse(skill, target, 6, 100);
			break;
		case SG::HoradricSeals:
			if (slot) {
				for (int i = 0; i < state.seals[*slot]; i++)
					QueueUse(skill, target, 6 * (i + 1), 50);
				state.seals[*slot] = 0;
				state.sealTicks[*slot] = HoradricSealTicks;
			}
			break;
		case SG::BaalsEcho:
			QueueUse(skill, target, 10, 40);
			break;
		case SG::EchoingShout:
			QueueUse(skill, target, 15, 50);
			QueueUse(skill, target, 30, 50);
			break;
		case SG::Cascade: {
			const Direction dir = GetDirection(tile, target);
			const Displacement side { Left(Left(dir)) };
			QueueUse(skill, target + side * 3, 4, 100);
			QueueUse(skill, target - side * 3, 8, 100);
			break;
		}
		case SG::RuneMastery:
			QueueUse(skill, target + Displacement { Left(Left(GetDirection(tile, target))) } * 2, 3, 100);
			break;
		case SG::ClusterRunes:
			QueueUse(skill, target + Displacement { Direction::North } * 2, 3, 65);
			QueueUse(skill, target + Displacement { Direction::SouthWest } * 2, 6, 65);
			break;
		case SG::NaKrulsPrison:
			for (const Direction dir : { Direction::North, Direction::East, Direction::West }) {
				player.tempDirection = Left(Left(dir));
				UseSkillFree(player, skill, tile, target + Displacement { dir } * 3, 100);
			}
			break;
		case SG::Aftershock:
			if (GenerateRnd(100) < Value(player, skill, gem, 0))
				AddBurst(target, 2, 12, DamageType::Physical, MissileGraphicID::StoneCurseShatter, SkillBaseDamage(player, skill, 100));
			break;
		case SG::ImmortalKingsStride:
			for (int i = 0; i < 2; i++) {
				const Point at = target + Displacement { static_cast<Direction>(GenerateRnd(8)) } * 2;
				AddBurst(at, 1, 10 + (8 * i), DamageType::Physical, MissileGraphicID::StoneCurseShatter, SkillBaseDamage(player, skill, AftershockPercent));
			}
			break;
		case SG::RallyingCry:
			AddBurst(tile, 3, 0, DamageType::Physical, MissileGraphicID::MetalHit, SkillBaseDamage(player, SkillSlot::ForAttack(WeaponSkill::Cleave), 150));
			break;
		case SG::BlinkStrike:
			AddBurst(skill.kind == SkillKind::Spell ? target : tile, 2, 2, DamageType::Magic, MissileGraphicID::HolyBoltExplosion, SkillBaseDamage(player, skill, 120));
			break;
		case SG::Sanctify:
			AddBurst(tile, 3, 0, DamageType::Magic, MissileGraphicID::HolyBoltExplosion, SkillBaseDamage(player, skill, 150));
			break;
		case SG::Wrathforged:
			if (player.poe2.rage >= 10) {
				// Rageforged II: sometimes the Rage isn't spent.
				if (GenerateRnd(100) >= Value(player, skill, gem, 1))
					player.poe2.rage -= 10;
				state.spentBonusPercent = static_cast<uint8_t>(Value(player, skill, gem, 0));
				state.spentBonusTicks = SpentBonusTicks;
			}
			break;
		case SG::ComboFinisher:
			if (player.poe2.combo >= Value(player, skill, gem, 1)) {
				state.spentBonusPercent = static_cast<uint8_t>(Value(player, skill, gem, 0));
				state.spentBonusTicks = SpentBonusTicks;
				player.poe2.combo = 0;
			}
			break;
		case SG::TalRashasPrism:
			if (state.prismReady) {
				state.prismReady = false;
				state.spentBonusPercent = 50;
				state.spentBonusTicks = SpentBonusTicks;
			}
			state.prismElements |= static_cast<uint8_t>(1 << state.prismElement);
			if (state.prismElements == 7) {
				state.prismElements = 0;
				state.prismReady = true;
			}
			break;
		case SG::KashyasVolley:
			for (const int side : { -1, 0, 1 }) {
				const Direction dir = GetDirection(tile, target);
				const Point aim = target + Displacement { Left(Left(dir)) } * (side * 2);
				Missile *arrow = AddMissile(tile, aim, dir, MissileID::Arrow, TARGET_MONSTERS, player, ElementalArrowBaseDamage, 0);
				if (arrow != nullptr) {
					arrow->poe2.damagePercent = static_cast<int16_t>(std::min(GemDamagePercent(skill, SkillLevel(player, skill)) * 70 / 100, 32000));
					arrow->poe2.skillKind = static_cast<uint8_t>(SkillKind::None);
				}
			}
			break;
		case SG::ZharsMadness:
			if (GenerateRnd(100) < 25) {
				std::vector<SkillSlot> spells;
				for (const SkillSlot other : player.poe2.skillSlots) {
					if (!other.isEmpty() && other != skill && other.kind == SkillKind::Spell && (SkillTags(other) & ST_Damage) != 0)
						spells.push_back(other);
				}
				if (!spells.empty()) {
					const SkillSlot other = spells[static_cast<size_t>(GenerateRnd(static_cast<int>(spells.size())))];
					const Fixed26_6 cost = GetManaAmount(player, other.spell());
					if (player._pMana >= cost) {
						player._pMana -= cost;
						player._pManaBase -= cost;
						UseSkillFree(player, other, tile, target, 100);
					}
				}
			}
			break;
		default:
			break;
		}
	}
	if (Has(player, skill, SG::TalRashasPrism))
		state.prismElement = static_cast<uint8_t>((state.prismElement + 1) % 3);
	(void)tags;
}

void OnSupportedKill(Monster &monster)
{
	if (!gbIsPoe2 || monster.poe2.lastHitPlayer < 0 || static_cast<size_t>(monster.poe2.lastHitPlayer) >= Players.size())
		return;
	Player &player = Players[static_cast<size_t>(monster.poe2.lastHitPlayer)];
	if (&player != MyPlayer)
		return;
	const SkillSlot skill { static_cast<SkillKind>(monster.poe2.lastHitSkillKind), monster.poe2.lastHitSkillId };
	SupportRuntime &state = player.poe2.supportState;
	if (Has(player, skill, SG::Pestilence) && (monster.poe2.bleedTicks > 0 || monster.poe2.poisonTicks > 0 || monster.poe2.igniteTicks > 0))
		PassDamageOverTime(player, monster);
	if (Has(player, skill, SG::WarlordsBloodPact))
		state.warlordStacks = static_cast<uint8_t>(std::min<int>(state.warlordStacks + 1, MaxWarlordStacks));
	if (Has(player, skill, SG::GharbadsGreed) && GenerateRnd(100) < 30)
		SpawnItem(monster, monster.position.tile, true);
	if (IsCursed(monster)) {
		for (const SkillSlot slot : player.poe2.skillSlots) {
			if (!slot.isEmpty() && Has(player, slot, SG::LazarusDeceit)) {
				PassCurses(player, monster);
				break;
			}
		}
	}
	OnMetaKill(player, monster, skill);
}

void OnPlayerHitBySupportedMonster(Player &player, Monster &monster)
{
	if (!gbIsPoe2 || &player != MyPlayer)
		return;
	player.poe2.supportState.warlordStacks = 0;
	OnMetaDamageTaken(player, monster.position.tile);
}

void OnSupportedDodge(Player &player, int tiles)
{
	if (!gbIsPoe2 || &player != MyPlayer)
		return;
	OnMetaDodge(player, tiles);
	// Mirage Archer: the roll leaves a mirage that shoots the bow attack, at most every 10 seconds.
	SupportRuntime &state = player.poe2.supportState;
	if (state.mirageCooldown > 0 || !IsPersistentSkillActive(player, PersistentSkill::MirageArcher))
		return;
	const SkillSlot attack = MirageArcherAttack(player);
	if (attack.isEmpty())
		return;
	state.mirageCooldown = MirageCooldownTicks;
	Stands.push_back({ attack, player.position.tile, MirageTicks, 0, true });
	AddVisualEffect(player.position.tile, MissileGraphicID::Etherealize, 0, 4);
	while (CountStands(true) > 1)
		Stands.erase(std::find_if(Stands.begin(), Stands.end(), [](const Stand &stand) { return stand.decoy; }));
}

SkillSlot MirageArcherAttack(const Player &player)
{
	for (const SkillSlot skill : player.poe2.skillSlots) {
		if (skill.kind == SkillKind::Attack && (SkillTags(skill) & ST_Bow) != 0 && WeaponAllowsSkill(player, skill.attack()))
			return skill;
	}
	return {};
}

int MirageCount()
{
	return CountStands(true);
}

void ProcessSupportEffects(Player &player)
{
	if (!gbIsPoe2 || &player != MyPlayer)
		return;
	SupportRuntime &state = player.poe2.supportState;
	if (state.mirageCooldown > 0)
		state.mirageCooldown--;
	for (size_t slot = 0; slot < NumSkillSlots; slot++) {
		const SkillSlot skill = player.poe2.skillSlots[slot];
		int maxSeals = 0;
		int every = 0;
		if (Has(player, skill, SG::HoradricSeals)) {
			maxSeals = MaxHoradricSeals;
			every = HoradricSealTicks;
		} else if (Has(player, skill, SG::StoredVolley)) {
			maxSeals = MaxStoredVolleySeals;
			every = StoredVolleySealTicks;
		}
		if (maxSeals == 0) {
			state.seals[slot] = 0;
			continue;
		}
		if (state.seals[slot] < maxSeals && ++state.sealTicks[slot] >= every) {
			state.sealTicks[slot] = 0;
			state.seals[slot]++;
		}
	}
	if (state.frostbiteTicks > 0)
		state.frostbiteTicks--;
	if (state.bartucTicks > 0)
		state.bartucTicks--;
	if (state.spentBonusTicks > 0)
		state.spentBonusTicks--;
	if (state.burningGroundCooldown > 0)
		state.burningGroundCooldown--;
	for (uint8_t &wait : state.novaCooldown) {
		if (wait > 0)
			wait--;
	}
	for (size_t i = 0; i < state.recentSpells.size(); i++) {
		if (state.recentSpellTicks[i] > 0 && --state.recentSpellTicks[i] == 0)
			state.recentSpells[i] = 0;
	}
	// Life Leech III: the kept leech heals once there's room.
	if (state.leechPoolTicks > 0) {
		const int32_t room = std::max<int32_t>(player._pMaxHP.raw() - player._pHitPoints.raw(), 0);
		const Fixed26_6 heal = Fixed26_6::fromRaw(std::min<int32_t>(state.leechPool, room));
		if (heal > 0 && player._pHitPoints > 0) {
			player._pHitPoints += heal;
			player._pHPBase += heal;
			state.leechPool -= heal.raw();
			RedrawComponent(PanelDrawComponent::Health);
		}
		if (--state.leechPoolTicks == 0 || state.leechPool <= 0) {
			state.leechPool = 0;
			state.leechPoolTicks = 0;
		}
	}
	// Momentum: count the tiles walked lately.
	const Point tile = player.position.tile;
	if (state.lastX != tile.x || state.lastY != tile.y) {
		if (state.lastX >= 0)
			state.movedTiles = static_cast<uint8_t>(std::min(state.movedTiles + 1, 10));
		state.lastX = static_cast<int16_t>(tile.x);
		state.lastY = static_cast<int16_t>(tile.y);
		state.moveDecayTicks = 30;
	} else if (state.moveDecayTicks > 0 && --state.moveDecayTicks == 0) {
		state.movedTiles = 0;
	}
	ProcessMetaGems(player);
	if (leveltype == DTYPE_TOWN || player.hasNoLife()) {
		ClearSupportEffects();
		return;
	}
	ProcessBursts(player);
	ProcessPending(player);
	ProcessStands(player);
}

void ProcessSupportMonsterEffects(Monster &monster)
{
	Poe2MonsterState &state = monster.poe2;
	if (state.witherTicks > 0 && --state.witherTicks == 0)
		state.witherStacks = 0;
	if (state.poisonTicks > 0) {
		state.poisonTicks--;
		if (state.poisonTicks % 10 == 0)
			AddVisualEffect(monster.position.tile, MissileGraphicID::Acid);
		if (state.poisonSource >= 0 && state.poisonSource == static_cast<int8_t>(MyPlayerId) && MyPlayer != nullptr && !monster.hasNoLife()) {
			ApplyMonsterDamage(DamageType::Physical, monster, Fixed26_6::fromRaw(state.poisonDamagePerTick));
			OnSpiritPoisonDealt(*MyPlayer, Fixed26_6::fromRaw(state.poisonDamagePerTick));
			if (monster.hasNoLife())
				M_StartKill(monster, *MyPlayer);
		}
		if (state.poisonTicks == 0)
			state.poisonDamagePerTick = 0;
	}
}

void ClearSupportEffects()
{
	Bursts.clear();
	Pending.clear();
	Stands.clear();
}

void UseTriggeredSkill(Player &player, SkillSlot skill, Point target, int damagePercent)
{
	UseSkillFree(player, skill, player.position.tile, target, damagePercent);
}

SkillSlot SupportHitSkill()
{
	return Hit.depth > 0 ? Hit.skill : SkillSlot {};
}

Point NearestMonsterTarget(const Player &player, Point from, int range)
{
	return NearestTarget(player, from, range);
}

int SupportAddedSpirit(const Player &player, SkillSlot skill)
{
	return Value(player, skill, SG::DeadlyHerald, 1);
}

int SupportAreaRadius(const Player &player, SkillSlot skill, int radius)
{
	if (radius <= 0)
		return radius;
	int percent = 100;
	if (Has(player, skill, SG::IncreasedArea))
		percent += Value(player, skill, SG::IncreasedArea, 0);
	if (Has(player, skill, SG::Cascade))
		percent -= 20;
	if (Has(player, skill, SG::ImmortalKingsStride))
		percent -= 20;
	if (Has(player, skill, SupportGem::ConcentratedEffect))
		percent -= 50;
	int scaled = (radius * percent + 50) / 100;
	if (Has(player, skill, SG::CurseMastery))
		scaled++;
	if (percent > 100)
		scaled = std::max(scaled, radius + 1);
	return std::max(scaled, 1);
}

int SupportDurationTicks(const Player &player, SkillSlot skill, int ticks)
{
	int percent = 100;
	if (Has(player, skill, SG::IncreasedDuration))
		percent += Value(player, skill, SG::IncreasedDuration, 0);
	if (Has(player, skill, SG::CurseMastery))
		percent += 50;
	if (Has(player, skill, SG::Legion))
		percent -= 50;
	if (Has(player, skill, SG::NaKrulsPrison))
		percent -= 25;
	return std::max(ticks * percent / 100, 1);
}

int SupportExtraProjectileCount(const Player &player, SkillSlot skill)
{
	int extra = 0;
	if (Has(player, skill, SG::Volley))
		extra += VolleyExtra;
	if (Has(player, skill, SG::StoredVolley)) {
		if (const std::optional<size_t> slot = SlotOf(player, skill); slot) {
			extra += player.poe2.supportState.seals[*slot];
			player.poe2.supportState.seals[*slot] = 0;
			player.poe2.supportState.sealTicks[*slot] = 0;
		}
	}
	return extra;
}

bool SupportFiresNova(const Player &player, SkillSlot skill)
{
	if (!Has(player, skill, SG::NovaProjectiles))
		return false;
	// Nova Projectiles II: at most one nova every few seconds; the skill fires normally in between.
	if (const int seconds = Value(player, skill, SG::NovaProjectiles, 2); seconds > 0) {
		const std::optional<size_t> slot = SlotOf(player, skill);
		if (!slot)
			return false;
		uint8_t &wait = player.poe2.supportState.novaCooldown[*slot];
		if (wait > 0)
			return false;
		wait = static_cast<uint8_t>(std::min(seconds * TicksPerSecond, 255));
	}
	return true;
}

int SupportNovaExtraProjectiles(const Player &player, SkillSlot skill)
{
	return Value(player, skill, SG::NovaProjectiles, 0);
}

void NoteRecentSpell(SupportRuntime &state, uint8_t spell)
{
	// Spell ids are kept one up, so 0 is an empty entry.
	const uint8_t id = static_cast<uint8_t>(spell + 1);
	size_t entry = 0;
	for (size_t i = 0; i < state.recentSpells.size(); i++) {
		if (state.recentSpells[i] == id) {
			entry = i;
			break;
		}
		if (state.recentSpellTicks[i] < state.recentSpellTicks[entry])
			entry = i;
	}
	state.recentSpells[entry] = id;
	state.recentSpellTicks[entry] = RecentSpellTicks;
}

int RecentSpellCount(const SupportRuntime &state)
{
	return static_cast<int>(std::count_if(state.recentSpells.begin(), state.recentSpells.end(), [](uint8_t id) { return id != 0; }));
}

int SupportZoneLimit(const Player &player, SkillSlot skill, int limit)
{
	return Has(player, skill, SG::Legion) ? limit + 1 : limit;
}

int SupportCastSlowPercent(const Player &player, SkillSlot skill)
{
	int slow = 0;
	if (Has(player, skill, SG::SpellEcho))
		slow += 30;
	if (Has(player, skill, SG::DeliberateCasting))
		slow += 15;
	if (Has(player, skill, SG::BlinkStrike))
		slow += 20;
	return slow;
}

int SupportAttackFrameBonus(const Player &player, SkillSlot skill)
{
	int frames = 0;
	if (Has(player, skill, SG::WarlordsBloodPact))
		frames += player.poe2.supportState.warlordStacks / 3;
	if (Has(player, skill, SG::CrushingBlow))
		frames -= 1;
	if (Has(player, skill, SG::ButchersHook))
		frames -= 1;
	return frames;
}

namespace {

/** @brief Bartuc's Hunger (the toll of Bartuc's Road): this share of every mana cost is paid in life instead. */
constexpr int HungerLifePercent = 25;

bool HasBartucsHunger(const Player &player)
{
	return gbIsPoe2 && PassiveTotal(player, PassiveStat::BartucsHunger) > 0;
}

/** @brief Takes life paid as a cost: it stops at 1 life, bypasses energy shield and isn't a hit. @return the life taken. */
Fixed26_6 PayLife(Player &player, Fixed26_6 life)
{
	const Fixed26_6 cost = std::min(life, player._pHitPoints - Fixed26_6::fromInt(1));
	if (cost <= 0)
		return Fixed26_6::fromInt(0);
	player._pHitPoints -= cost;
	player._pHPBase -= cost;
	RedrawComponent(PanelDrawComponent::Health);
	AddVisualEffect(player.position.tile, MissileGraphicID::BloodHit);
	return cost;
}

} // namespace

int SupportLifeManaPercent(const Player &player, SkillSlot skill)
{
	// With a life-cost support on the skill, the larger life share applies, not Bartuc's Hunger as well.
	if (Has(player, skill, SG::BloodPrice))
		return 0;
	if (Has(player, skill, SG::BartucsBloodletting))
		return 50;
	if (HasBartucsHunger(player))
		return 100 - HungerLifePercent;
	return 100;
}

void PaySupportLifeCost(Player &player, SkillSlot skill, int manaCost)
{
	if (!gbIsPoe2 || manaCost <= 0)
		return;
	int life = 0;
	if (Has(player, skill, SG::BloodPrice))
		life = manaCost * 120 / 100;
	else if (Has(player, skill, SG::BartucsBloodletting))
		life = manaCost / 2;
	else if (HasBartucsHunger(player))
		life = std::max(manaCost * HungerLifePercent / 100, 1);
	if (life <= 0)
		return;
	const Fixed26_6 cost = PayLife(player, Fixed26_6::fromInt(life));
	if (cost <= 0)
		return;
	if (Has(player, skill, SG::BartucsBloodletting)) {
		player.poe2.supportState.bartucDamage = cost.raw() * 3;
		player.poe2.supportState.bartucTicks = SpentBonusTicks;
	}
}

void PayMeleeAttackLife(Player &player)
{
	if (!gbIsPoe2 || PassiveTotal(player, PassiveStat::PaidInBlood) <= 0)
		return;
	PayLife(player, std::max(Fixed26_6::fromRaw(player._pMaxHP.raw() / 100), Fixed26_6::fromInt(1)));
}

void AddHoradricTotem(Player &player, size_t row, Point target, int limit)
{
	Point at = target;
	if (!InDungeonBounds(at) || IsTileSolid(at) || at.WalkingDistance(player.position.tile) > 8)
		at = player.position.tile;
	// It casts its first spell half a second after it lands.
	Stands.push_back({ SkillSlot::ForPersistent(PersistentSkill::HoradricTotem), at, HoradricTotemTicks, 10, false, static_cast<int8_t>(row), 0 });
	while (CountStands(false) > std::max(limit, 1))
		Stands.erase(std::find_if(Stands.begin(), Stands.end(), [](const Stand &stand) { return !stand.decoy; }));
	AddVisualEffect(at, MissileGraphicID::HolyBoltExplosion, 0, 6);
	PlaySfxLoc(SfxID::SpellStoneCurse, at);
}

int HoradricTotemCount()
{
	return CountStands(false);
}

int SupportMinionDamagePercent(const Player &owner, const Monster &minion)
{
	int percent = 100;
	if (MinionHas(owner, minion, SG::TragOulsBalance))
		return 0;
	if (MinionHas(owner, minion, SG::BoneArmy))
		percent = percent * 70 / 100;
	if (MinionHas(owner, minion, SG::FeedingFrenzy))
		percent = percent * (100 + MinionValue(owner, MinionSkillOf(minion), SG::FeedingFrenzy, 0)) / 100;
	if (MinionHas(owner, minion, SG::MixedHost))
		percent = percent * (100 + 7 * CountMinions(owner).second) / 100;
	// Not a support, but every minion hit comes here: War Banner.
	return percent * (100 + BannerMinionDamagePercent(owner, minion)) / 100;
}

int SupportMinionDamageTakenPercent(const Player &owner, const Monster &minion)
{
	if (MinionHas(owner, minion, SG::TragOulsBalance))
		return 0;
	return 100 + MinionValue(owner, MinionSkillOf(minion), SG::FeedingFrenzy, 1);
}

int SupportMinionLifePercent(const Player &owner, PersistentSkill skill)
{
	int percent = 100;
	if (Has(owner, SkillSlot::ForPersistent(skill), SG::BoneArmy))
		percent = percent * 70 / 100;
	if (Has(owner, SkillSlot::ForPersistent(skill), SG::GravePact))
		percent = percent * 80 / 100;
	return percent;
}

int SupportExtraMinions(const Player &owner, PersistentSkill skill)
{
	return Has(owner, SkillSlot::ForPersistent(skill), SG::BoneArmy) ? 2 : 0;
}

void OnSupportedMinionDied(Player &owner, Monster &minion)
{
	if (!gbIsPoe2 || &owner != MyPlayer)
		return;
	OnMetaMinionDied(owner, minion);
	OnSpiritMinionDied(owner, minion);
	const Fixed26_6 life = Fixed26_6::fromRaw(std::max<int32_t>(minion.maxHitPoints.raw() / 4, 64));
	if (MinionHas(owner, minion, SG::GravePact)) {
		AddVisualEffect(minion.position.tile, MissileGraphicID::BloodStarExplosion, 0, 6);
		AddBurst(minion.position.tile, 2, 0, DamageType::Physical, MissileGraphicID::BloodHit, Fixed26_6::fromRaw(Percent(life.raw(), MinionValue(owner, MinionSkillOf(minion), SG::GravePact, 0))));
	}
	if (MinionHas(owner, minion, SG::RathmasCovenant)) {
		const std::vector<Monster *> nearest = NearestMonsters(minion.position.tile, 7, 1, nullptr);
		if (!nearest.empty()) {
			AddVisualEffect(minion.position.tile, MissileGraphicID::Etherealize);
			AddBurst(nearest.front()->position.tile, 1, 8, DamageType::Magic, MissileGraphicID::StoneCurseShatter, Fixed26_6::fromRaw(life.raw() * 2));
		}
	}
}

bool OnSupportedMissileHitWall(Missile &missile, Point wall)
{
	if (!missile.poe2.ricochet || missile.sourceType() != MissileSource::Player)
		return false;
	missile.poe2.ricochet = false;
	Player &player = *missile.sourcePlayer();
	const Point from = missile.position.tile == wall ? Point(missile.position.start) : Point(missile.position.tile);
	const std::vector<Monster *> nearest = NearestMonsters(from, 7, 1, nullptr);
	if (nearest.empty() || IsTileSolid(from))
		return false;
	const Point target = nearest.front()->position.tile;
	Missile *bounce = AddMissile(from, target, GetDirection(from, target), missile._mitype, missile._micaster, player, missile._midam, missile._mispllvl);
	if (bounce == nullptr)
		return false;
	bounce->poe2 = missile.poe2;
	bounce->poe2.ricochet = false;
	AddVisualEffect(from, MissileGraphicID::MetalHit);
	return true;
}

void TryDropLineageSupport(const Monster &monster, bool sendmsg)
{
	if (!gbIsPoe2 || MyPlayer == nullptr || monster.isPlayerMinion())
		return;
	SupportGem signature = SupportGem::None;
	if (monster.type().type == MT_DIABLO) {
		signature = SG::DiablosRedLightning;
	} else {
		switch (monster.uniqueType) {
		case UniqueMonsterType::Butcher:
			signature = SG::ButchersHook;
			break;
		case UniqueMonsterType::SkeletonKing:
			signature = SG::RathmasCovenant;
			break;
		case UniqueMonsterType::Lazarus:
			signature = SG::LazarusDeceit;
			break;
		case UniqueMonsterType::Zhar:
			signature = SG::ZharsMadness;
			break;
		case UniqueMonsterType::WarlordOfBlood:
			signature = SG::WarlordsBloodPact;
			break;
		case UniqueMonsterType::Garbud:
			signature = SG::GharbadsGreed;
			break;
		case UniqueMonsterType::NaKrul:
			signature = SG::NaKrulsPrison;
			break;
		default:
			break;
		}
	}
	// Any waystone map boss drops the map lineage supports more often; Baal's Echo needs tier 10.
	std::vector<SupportGem> mapPool;
	if (IsMapBoss(monster)) {
		for (const SupportGem gem : { SG::TalRashasPrism, SG::BartucsBloodletting, SG::TragOulsBalance, SG::ImmortalKingsStride, SG::KashyasVolley, SG::AndarielsAnguish })
			mapPool.push_back(gem);
		if (MyPlayer->poe2.mapTier >= 10)
			mapPool.push_back(SG::BaalsEcho);
	}
	SupportGem drop = SupportGem::None;
	if (signature != SupportGem::None && GenerateRnd(100) < 2)
		drop = signature;
	else if (!mapPool.empty() && GenerateRnd(100) < 2)
		drop = mapPool[static_cast<size_t>(GenerateRnd(static_cast<int>(mapPool.size())))];
	else if (GenerateRnd(2000) == 0) {
		std::vector<SupportGem> all;
		for (const SupportData &data : Table) {
			if ((data.flags & SF_Lineage) != 0 && data.gem != SG::BaalsEcho)
				all.push_back(data.gem);
		}
		drop = all[static_cast<size_t>(GenerateRnd(static_cast<int>(all.size())))];
	}
	if (drop == SupportGem::None)
		return;
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		if (SupportGemForItemData(AllItemsList[i]) == drop) {
			SpawnBaseItem(monster.position.tile, static_cast<_item_indexes>(i), sendmsg);
			return;
		}
	}
}

} // namespace devilution
