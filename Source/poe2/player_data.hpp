/**
 * @file poe2/player_data.hpp
 *
 * POE2 Mashup character state that isn't part of the original hero file.
 */
#pragma once

#include <algorithm>
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include "engine/point.hpp"
#include "poe2/craft_types.hpp"
#include "poe2/economy_state.hpp"
#include "poe2/flasks.hpp"
#include "poe2/item_stats.hpp"
#include "poe2/map_state.hpp"
#include "poe2/unique_rules.hpp"
#include "tables/spelldat.h"

namespace devilution {

/** @brief Most nodes a passive tree may have (stage 2 of the tree plan reaches about 1,400 with the ascendancies). */
constexpr size_t MaxPassiveNodes = 2048;

/** @brief Skills granted by skill gems that aren't spells; most need a certain weapon type. */
enum class WeaponSkill : uint8_t {
	None,
	/** Swords and axes: hits the target and both sides. */
	Cleave,
	/** Maces, hammers and mauls: hits everything around and knocks it back. */
	GroundSlam,
	/** Bows: fires a fan of five arrows. */
	Barrage,
	/** Staves: hits everything around. */
	WhirlingStaff,
	/** Daggers: a very fast double strike. */
	Flurry,
	/** Maces, axes, swords and staves: leaps to the target and slams the ground there. */
	LeapSlam,
	/** Bows: a volley of arrows over an area. */
	RainOfArrows,
	/** Shields: charges at the target and bashes everything in front, knocking it back. */
	ShieldCharge,
	/** Any weapon: a war cry that knocks back adjacent enemies and empowers melee hits. */
	BattleCry,
	/** Bows: an arrow that bursts into lightning, hitting everything around the target. */
	LightningArrow,
	/** Bows: fires three arrows and leaps back from the target. */
	EscapeShot,
	/** No weapon needed: a fan of five firebolts. */
	EmberFusillade,
	/** No weapon needed: a spray of erratic lightning sparks. */
	Spark,
	/** No weapon needed: a shard of ice that chills and freezes. */
	IceBolt,
	/** No weapon needed: a ring of frost bursting outward. */
	FrostNova,
	/** Swords: a counter-attack, only ready after you block. */
	Riposte,
	/** Daggers: steps through the shadows behind the target and stabs it. */
	ShadowStep,
	/** Axes and swords: a fast strike that builds Frenzy charges. */
	Frenzy,
	/** Maces: a crushing blow that breaks armour. */
	Smite,
	/** Staves: lightning-fast strikes that build Combo; every third bursts with lightning. */
	FistsOfThunder,
	/** Crossbows: a rapid stream of bolts. */
	RapidFire,
	/** Shields: a bash that stuns and knocks back. */
	ShieldBash,
	/** No weapon needed: a curse that makes monsters take more physical damage. */
	AmplifyDamage,
	/** The rest of the roster: skills described by a data table (see poe2/table_skills.cpp). */
	Whirlwind,
	Sacrifice,
	Zeal,
	Vengeance,
	ArkainesWrath,
	FanOfBlades,
	ViperStrike,
	BladeSentinel,
	DeathMark,
	BladeFury,
	DoubleThrow,
	Rend,
	WarCry,
	CallOfTheAncients,
	Fissure,
	BlessedHammer,
	Shockwave,
	FistOfTheHeavens,
	DashingStrike,
	LashingTailKick,
	WaveOfLight,
	CycloneStrike,
	SevenSidedStrike,
	Multishot,
	GuidedArrow,
	FreezingArrow,
	Impale,
	ClusterBolt,
	SpikeTrap,
	Sentry,
	ExplosiveBolt,
	SiegeVolley,
	HolyShield,
	BlessedShield,
	Condemn,
	ShieldWall,
	FallingSword,
	Meteor,
	Thunderstorm,
	GlacialSpike,
	FrozenArmor,
	Blizzard,
	IceBarrage,
	FrozenOrb,
	Haunt,
	LocustSwarm,
	CorpseExplosion,
	SoulHarvest,
	BonePrison,
	Weaken,
	IronMaiden,
	Decrepify,
	LowerResist,
	Confuse,
	Attract,
	/**
	 * The skills the caster weapons rework's staves and wands grant (caster-weapons-plan-v2.md 6.12): table skills with
	 * TF_Granted, cast with a left click, with no gem of their own.
	 */
	FreezingShards,
	LightningBolt,
	SolarOrb,
	ChimingBolt,
	Soulrend,
	Reap,
	FrostLance,
	EchoingBlast,
	EnervatingNova,
	ConsecratedGround,
	DarkPact,
	FeastOfFlesh,
	WitheringBolt,
	BoneBolt,
	ManaDrain,
	PowerSiphon,
	VolatileBolt,
	GalvanicBolt,
	Decompose,
	Exsanguinate,
	FrostBolt,
	TormentBolt,
	CriticalBolt,
	WitheringRay,
	TwinBolts,
	/** Gem skills appended after the granted ones (saved by value, so only ever appended). */
	Despair,
	Last = Despair,
};

/** @brief The first skill a caster weapon grants, and the last: every one between them is granted too. */
constexpr WeaponSkill FirstGrantedSkill = WeaponSkill::FreezingShards;
constexpr WeaponSkill LastGrantedSkill = WeaponSkill::TwinBolts;

/** @brief Whether the skill is one a caster weapon grants rather than a gem. */
constexpr bool IsGrantedSkill(WeaponSkill skill)
{
	return skill >= FirstGrantedSkill && skill <= LastGrantedSkill;
}

enum class SupportGem : uint8_t {
	None,
	MultipleProjectiles,
	Empower,
	FasterCasting,
	Efficiency,
	Pierce,
	Fork,
	Chain,
	ConcentratedEffect,
	FasterProjectiles,
	/** Melee attack skills deal more damage. */
	Brutality,
	/** Attack skills attack faster. */
	MartialTempo,
	/** Melee attack skills knock enemies back. */
	Knockback,
	/** Melee hits with the skill build Rage. */
	BattleRage,
	/** Planned supports from the skills design document (poe2/support_table.hpp describes them). */
	IncreasedArea,
	IncreasedDuration,
	Volley,
	SpellEcho,
	Multistrike,
	Aftershock,
	CloseCombat,
	InfusedFlames,
	InfusedStorm,
	InfusedFrost,
	HolyInfusion,
	BurningGround,
	Overcharge,
	DeepFreeze,
	WitheringTouch,
	CastOnKill,
	CastWhenHit,
	CastOnDodge,
	CastWhileChannelling,
	BlinkStrike,
	EchoingShout,
	RallyingCry,
	BoneArmy,
	FeedingFrenzy,
	GravePact,
	CurseOnHit,
	CurseMastery,
	RuneMastery,
	ClusterRunes,
	Sanctify,
	Shatter,
	LifeLeech,
	ManaLeech,
	BloodPrice,
	/** Supports from the build research. */
	ElementalFocus,
	Hellfire,
	FuelThePyre,
	Frostbite,
	Conductor,
	SerratedEdge,
	Envenom,
	Bloodlust,
	SunderArmour,
	Pestilence,
	HoradricSeals,
	StoredVolley,
	Cascade,
	NovaProjectiles,
	Ricochet,
	Momentum,
	CrushingBlow,
	DeliberateCasting,
	BrimmingMana,
	CoupDeGrace,
	Wrathforged,
	ComboFinisher,
	HoradricTotem,
	Decoy,
	Legion,
	MixedHost,
	/** Lineage supports: unique, found rather than cut. */
	ButchersHook,
	RathmasCovenant,
	LazarusDeceit,
	DiablosRedLightning,
	ZharsMadness,
	WarlordsBloodPact,
	GharbadsGreed,
	NaKrulsPrison,
	TalRashasPrism,
	BartucsBloodletting,
	TragOulsBalance,
	ImmortalKingsStride,
	KashyasVolley,
	AndarielsAnguish,
	BaalsEcho,
	WirtsLeg,
	/** Penetration supports (Curtis 2026-10-18, with POE2-style monster resistances). */
	FirePenetration,
	LightningPenetration,
	ColdPenetration,
	/** Herald supports (Curtis 2026-10-19): POE2's Deadly Herald. */
	DeadlyHerald,
	/** Chaos supports (chaos gems addendum, Curtis 2026-10-31): POE2's Escalating Poison and Deadly Poison I-II. */
	TwinFangs,
	DeadlyVenom,
	Count,
};

/** @brief Spirit gems: persistent buffs and minions that reserve Spirit while socketed. */
enum class PersistentSkill : uint8_t {
	None,
	/** Monsters you kill explode in flames, burning those around them. */
	HeraldOfAsh,
	/** Monsters you kill release lightning that strikes enemies nearby. */
	HeraldOfThunder,
	/** An aura granting resistance to fire, lightning and cold. */
	PurityOfElements,
	/** A golem that fights at your side and comes back when it dies. */
	SummonGolem,
	/** Skeletal warriors that fight at your side and rise again when they fall. */
	SkeletalWarriors,
	/** Monsters you kill while they're chilled or frozen shatter, chilling those around them. */
	HeraldOfFrost,
	/** Skeletal mages that hurl firebolts from behind your lines. */
	SkeletalMages,
	/** A golem of blood whose blows heal you. */
	BloodGolem,
	/** A golem of fire that burns what stands near it. */
	FireGolem,
	/** A fire-breathing guardian rises near you when enemies come close. */
	Guardian,
	/** A shadow of you repeats your skills at less damage. */
	ShadowWarrior,
	/** Monsters you kill rise to fight for you, keeping their strength. */
	Revive,
	/** Part of the damage you take comes from mana. */
	ManaShield,
	/** Spells deal more damage the more mana you have, and cost more. */
	VizjereiFocus,
	/** Melee hits build Rage and hit harder, but you take more damage. */
	Berserk,
	/** Melee hits on you are partly turned back on the attacker. */
	Reflect,
	/** Meta gems (POE2): they hold spells in their sockets and cast them all when their Energy fills. Dodge rolls fill this one. */
	CastOnDodge,
	/** Hits you take fill it. */
	CastWhenDamageTaken,
	/** Channelling a stream skill fills it. */
	CastWhileChannelling,
	/** Melee kills store its Energy, up to 500, and using it from the skill tray casts the spells inside. */
	ReapersInvocation,
	/** Not a meta gem: a dodge roll leaves a mirage that shoots your bow attack for a few seconds. */
	MirageArcher,
	/** Meta gem holding curses: they become an aura around you, weaker than cast by hand (POE2). */
	Blasphemy,
	/** Meta gem used from the skill tray: it places a totem that casts the spells inside for a few seconds. No Spirit; it costs mana. */
	HoradricTotem,
	/** Critical hits fill it, more for stronger monsters and harder hits; the spells inside deal 20% less damage. */
	CastOnCritical,
	/** Freezing, shocking and igniting monsters fill it. */
	CastOnElementalAilment,
	/** Your minions dying fill it. */
	CastOnMinionDeath,
	/** Invocation: energy shield lost to hits stores its Energy. */
	BarrierInvocation,
	/** Invocation: freezing, shocking and igniting monsters store its Energy. */
	ElementalInvocation,
	/** Gains stages of more evasion; a hit on you spends them in a burst of wind that throws enemies back. */
	WindDancer,
	/** Gains Ghost Shrouds; a hit on you takes one, and energy shield regenerates from your evasion for a while. */
	GhostDance,
	/** Energy shield lost to hits turns into armour for a few seconds. */
	Barkskin,
	/** Gains icy stages; a melee hit on you spends one to blast the attacker with cold. */
	ArcticArmour,
	/** Killing a poisoned enemy spreads its poison to those nearby, which may be hindered. */
	HeraldOfPlague,
	/** Freezing or electrocuting an enemy gives a Frenzy charge, once in a while. */
	CombatFrenzy,
	/** You regenerate Rage, and it doesn't fade while this is up. */
	EternalRage,
	/** Much more damage of one element, which changes every 8 seconds. */
	ElementalConflux,
	/** Flasks gain charges over time; life flasks also heal your minions nearby. */
	AlchemistsBoon,
	/** Aura: enemies near you are easier to stun, freeze, ignite and shock. */
	OverwhelmingPresence,
	/** Dodge rolls leave caltrops that hurt and hinder the enemies stepping on them. */
	TrailOfCaltrops,
	/** Enemies with elemental ailments leave mana remnants when they die or take a critical hit. */
	ManaRemnants,
	/** Rare and unique enemies near you take more damage the longer they stay, then can be culled. */
	Attrition,
	/** Your dodge roll becomes a short teleport. */
	Blink,
	/** Stores the physical damage your armour stops; use it from the skill tray to release it as a nova. */
	IronWard,
	/** Stores part of your poison damage; use it from the skill tray to release a poison nova. */
	PlagueBearer,
	/** Use it from the skill tray to start energy shield recharge at once; hits can't interrupt it for a few seconds. */
	Convalescence,
	/** Your skeletons that die leave life remnants; use it from the skill tray to spend that life raising them at once. */
	GrimFeast,
	/** Banners (POE2): attack hits build Glory; at 200, use it from the skill tray to plant a banner that empowers attacks. */
	WarBanner,
	/** Banner of more armour, evasion and movement speed. */
	DefianceBanner,
	/** Banner of higher maximum resistances and flask charges. */
	DreadBanner,
	/** Physical attack hits store thorns; casting a spell spreads them as thorny ground around you. */
	Briarpatch,
	/** More block chance with a shield; a charged block sprays fire ahead of you. */
	MagmaBarrier,
	/** Breaking an enemy's armour gives stacks of more armour and thorns. */
	ScavengedPlating,
	/** Every few seconds, when you're hurt, you recover part of your life. */
	TimeOfNeed,
	/** Spends Frenzy charges on faster attacks and casts. */
	ChargeRegulation,
	/** Aura: enemies near you wither. */
	WitheringPresence,
	/** Elemental hits build Resonance of their element; the more you have, the more elemental damage you deal. */
	Trinity,
	/** Freezing, shocking and igniting enemies may leave element infusions. */
	SiphonElements,
	/** Bleeding enemies you kill burst, making those nearby bleed. */
	HeraldOfBlood,
	/** Skeletal Snipers shoot arrows from far away. */
	SkeletalSnipers,
	/** Skeletal Frost Mages hurl frost bolts that chill and freeze. */
	SkeletalFrostMages,
	/** Skeletal Storm Mages hurl charged bolts that shock. */
	SkeletalStormMages,
	/** Skeletal Reavers build Rage as they hit, hitting harder. */
	SkeletalReavers,
	/** Skeletal Brutes are tough and slow, and their heavy blows stun. */
	SkeletalBrutes,
	/** Skeletal Clerics heal your other minions and raise the fallen sooner. */
	SkeletalClerics,
	/** A pack of wolves (Shadow Beasts) that rise again when they fall. */
	WolfPack,
	/** Your own fire spells' hits call flaming spirits that fight for a few seconds. */
	RagingSpirits,
	/** Swarms of insects leave you to bite and poison enemies near you. */
	RavenousSwarm,
	/** Corpse Explosion and Soul Harvest can use your skeletons as corpses; minions rise again more slowly. */
	Sacrifice,
	/** Each dodge roll leaves an illusion of you that draws enemies; one they destroy leaves Frenzy remnants. */
	LingeringIllusion,
	Last = LingeringIllusion,
};

/** @brief What a socketed skill gem grants. */
enum class SkillKind : uint8_t {
	None,
	/** A spell; the id is a SpellID. */
	Spell,
	/** A weapon attack; the id is a WeaponSkill. */
	Attack,
	/** A Spirit gem that is active while socketed; the id is a PersistentSkill. */
	Persistent,
	/** The weapon's own attack, which costs no mana; the id is a DefaultAttack. It isn't a gem: it has its own row in the Skills panel. */
	Default,
};

/** @brief A Skills panel socket: the skill of the gem socketed in it. */
struct SkillSlot {
	SkillKind kind = SkillKind::None;
	uint8_t id = 0;

	[[nodiscard]] bool isEmpty() const { return kind == SkillKind::None; }
	[[nodiscard]] SpellID spell() const { return kind == SkillKind::Spell ? static_cast<SpellID>(id) : SpellID::Invalid; }
	[[nodiscard]] WeaponSkill attack() const { return kind == SkillKind::Attack ? static_cast<WeaponSkill>(id) : WeaponSkill::None; }
	[[nodiscard]] PersistentSkill persistent() const { return kind == SkillKind::Persistent ? static_cast<PersistentSkill>(id) : PersistentSkill::None; }

	static SkillSlot ForSpell(SpellID spell) { return { SkillKind::Spell, static_cast<uint8_t>(spell) }; }
	static SkillSlot ForAttack(WeaponSkill skill) { return { SkillKind::Attack, static_cast<uint8_t>(skill) }; }
	static SkillSlot ForPersistent(PersistentSkill skill) { return { SkillKind::Persistent, static_cast<uint8_t>(skill) }; }
	/** @param attack A DefaultAttack (poe2/default_attack.hpp). */
	static SkillSlot ForDefault(uint8_t attack) { return { SkillKind::Default, attack }; }

	bool operator==(const SkillSlot &other) const = default;
};

/** @brief Skill gem sockets in the Skills panel (K). The first 6 were the old skill bar's slots. */
constexpr size_t NumSkillSlots = 12;
/** @brief Sockets heroes had before the Skills panel grew to 12 (hero data before version 8). */
constexpr size_t OldNumSkillSlots = 6;
/** @brief The Skills panel row of the weapon's default attack, after the gem sockets: it takes supports like a gem. */
constexpr size_t DefaultAttackRow = NumSkillSlots;
/**
 * @brief The Skills panel rows of the Spirit skills held sceptres grant, main hand then off hand, under the default
 * attack's row (caster-weapons-plan-v2.md 6.9). They have no support sockets and aren't gem sockets: nothing indexes a
 * socket array with them.
 */
constexpr size_t GrantedRowMain = DefaultAttackRow + 1;
constexpr size_t GrantedRowOff = DefaultAttackRow + 2;
/** @brief The spiritGemsOn bit of the main hand's granted Spirit skill; the off hand's is the next. Set means off. */
constexpr size_t GrantedSpiritOffBit = NumSkillSlots;

/** @brief Slots of the skill tray: the three mouse buttons, then six keys (Q, E, R, T, F and G by default). */
enum class TraySlot : uint8_t {
	LeftMouse,
	MiddleMouse,
	RightMouse,
	Key1,
	Key2,
	Key3,
	Key4,
	Key5,
	Key6,
	Count,
};
constexpr size_t NumTraySlots = static_cast<size_t>(TraySlot::Count);
constexpr size_t NumTrayKeys = 6;

constexpr TraySlot TrayKeySlot(size_t key)
{
	return static_cast<TraySlot>(static_cast<size_t>(TraySlot::Key1) + key);
}

/** @brief What a skill tray slot does. */
enum class TrayAction : uint8_t {
	None,
	/** Old saves only: Diablo's left click. Loading turns it into the weapon attack. */
	MoveAndAttack,
	/** Old saves only: walked and picked things up without attacking. Loading turns it into the weapon attack. */
	Interact,
	/** Old saves only: the readied spell. Loading clears it, and an empty right button casts the readied spell. */
	ReadiedSpell,
	/** Uses the skill gem in a Skills panel socket. */
	Gem,
	/**
	 * A weapon set's default attack, which costs no mana; the socket is the set (0 or 1). Any slot can take it, and
	 * using it swaps to its set first, like POE2.
	 */
	WeaponAttack,
	Last = WeaponAttack,
};

/** @brief A skill tray slot's action, and for gems the Skills panel socket it uses. */
struct TrayBinding {
	TrayAction action = TrayAction::None;
	uint8_t socket = 0;

	[[nodiscard]] bool isGem() const { return action == TrayAction::Gem; }
	static TrayBinding ForGem(size_t socket) { return { TrayAction::Gem, static_cast<uint8_t>(socket) }; }

	bool operator==(const TrayBinding &other) const = default;
};

/** @brief What a tray slot does on a new hero, and once its gem is taken out. */
constexpr TrayBinding DefaultTrayBinding(TraySlot slot)
{
	switch (slot) {
	case TraySlot::LeftMouse:
	case TraySlot::MiddleMouse:
		return { TrayAction::WeaponAttack, 0 };
	default:
		return {};
	}
}

/**
 * @brief Spell level socketed spell gems had before gems had their own levels: it grew with the character.
 * Heroes from then keep it as their gems' level.
 */
constexpr int GemSpellLevel(int characterLevel)
{
	const int level = ((std::max(characterLevel, 1) - 1) / 4) + 1;
	return std::min(level, 15);
}

/** @brief Highest skill gem level, like POE2's uncorrupted gems. */
constexpr int MaxGemLevel = 20;
/** @brief Room for the passive tree's stat totals (PassiveStat::Count must fit). */
constexpr size_t MaxPassiveStats = 112;

/**
 * @brief Character level each gem level needs: POE2's table (0 to 90 for levels 1 to 20) scaled to Diablo's level 50 cap.
 */
constexpr std::array<uint8_t, MaxGemLevel> GemLevelCharacterLevels { 1, 2, 3, 6, 8, 10, 12, 14, 17, 20, 23, 26, 29, 32, 36, 37, 40, 43, 47, 50 };

constexpr int CharacterLevelForGemLevel(int gemLevel)
{
	return GemLevelCharacterLevels[std::clamp(gemLevel, 1, MaxGemLevel) - 1];
}

/** @brief The highest gem level a character of this level can use. */
constexpr int MaxGemLevelForCharacter(int characterLevel)
{
	int level = 1;
	while (level < MaxGemLevel && CharacterLevelForGemLevel(level + 1) <= characterLevel)
		level++;
	return level;
}

/** @brief Spirit skulls, like POE2's campaign Spirit rewards (30, 30 and 40). */
constexpr size_t NumSpiritSkulls = 3;
constexpr uint8_t AllSpiritSkulls = (1 << NumSpiritSkulls) - 1;
constexpr std::array<int, NumSpiritSkulls> SpiritSkullSpirit { 30, 30, 40 };

/** @brief Support gems a spell could have before supports moved into the skill bar (old saves). */
constexpr size_t MaxSupportsPerSpell = 2;
constexpr size_t MaxPoe2Spells = 64;
static_assert(static_cast<size_t>(SpellID::LAST) < MaxPoe2Spells);

/** @brief Most support sockets a skill can have; how many are open depends on the character level. */
constexpr size_t MaxSupportSockets = 5;
using SupportSockets = std::array<SupportGem, MaxSupportSockets>;
/** @brief The tier (1 to 3) of the support in each socket of a skill. */
using SupportTiers = std::array<uint8_t, MaxSupportSockets>;

/** @brief Support gems come in tiers I to III, like POE2's; a higher tier is cut from a higher uncut support gem (see poe2/support_tiers.hpp). */
constexpr int MaxSupportTier = 3;

/** @brief Most jewel sockets the passive tree can have. */
constexpr size_t MaxJewelSockets = 12;

/** @brief Timers and counters of the support gems, per character (not saved; see poe2/support_table.cpp). */
struct SupportRuntime {
	/** Seals stored by Horadric Seals and Stored Volley, and the ticks towards the next one, per Skills panel row (the last is the default attack's). */
	std::array<uint8_t, NumSkillSlots + 1> seals {};
	std::array<uint8_t, NumSkillSlots + 1> sealTicks {};
	/** Warlord's Blood Pact: attack speed stacks from kills. */
	uint8_t warlordStacks = 0;
	/** Frostbite: hits gain extra cold damage after a freeze. */
	uint8_t frostbiteTicks = 0;
	/** Tal Rasha's Prism: the next element, the elements cast in a row (bits) and whether the next cast is empowered. */
	uint8_t prismElement = 0;
	uint8_t prismElements = 0;
	bool prismReady = false;
	/** Momentum: tiles walked in the last moments and the tile the character was on. */
	uint8_t movedTiles = 0;
	uint8_t moveDecayTicks = 0;
	int16_t lastX = -1;
	int16_t lastY = -1;
	/** Bartuc's Bloodletting: extra physical damage from the life paid, as a raw Fixed26_6 value, and the ticks it lasts. */
	int32_t bartucDamage = 0;
	uint8_t bartucTicks = 0;
	/** Wrathforged and Combo Finisher: more damage for the cast that spent Rage or Combo, in percent, and the ticks it lasts. */
	uint8_t spentBonusPercent = 0;
	uint8_t spentBonusTicks = 0;
	/** Burning Ground: ticks before another patch can be left. */
	uint8_t burningGroundCooldown = 0;
	/** Faster Casting III: the spells cast lately and the ticks each stays counted. */
	std::array<uint8_t, 4> recentSpells {};
	std::array<uint8_t, 4> recentSpellTicks {};
	/** Life Leech III: life leeched at full life, as a raw Fixed26_6 value, and the ticks it is kept. */
	int32_t leechPool = 0;
	uint8_t leechPoolTicks = 0;
	/** Nova Projectiles II: ticks before each Skills panel socket's skill can fire another nova. */
	std::array<uint8_t, NumSkillSlots + 1> novaCooldown {};
	/** Energy of the meta gem in each Skills panel socket, in hundredths (see poe2/meta_gems.hpp), and the events since it last cast. */
	std::array<uint16_t, NumSkillSlots> metaEnergy {};
	std::array<uint8_t, NumSkillSlots> metaEvents {};
	/** Cast while Channelling: ticks of the current second of channelling. */
	uint8_t metaChannelTicks = 0;
	/** Mirage Archer: ticks until a dodge roll can leave another mirage. */
	uint16_t mirageCooldown = 0;
	/** Blasphemy: ticks until its curses next reach the enemies around you. */
	uint8_t blasphemyTicks = 0;
};

/** @brief One of Ravenous Swarm's swarms (see poe2/minion_gems.cpp). */
struct RavenousSwarm {
	/** The monster it bites, or -1 while it looks for one. */
	int16_t target = -1;
	/** Game ticks it has left (0: none out), and until its next bite. */
	uint16_t ticks = 0;
	uint8_t biteTicks = 0;
};

/** @brief Timers of the Spirit skills and the element infusions, per character (not saved; see poe2/spirit_skills.cpp). */
struct SpiritRuntime {
	uint8_t guardianCooldown = 0;
	uint8_t reviveCooldown = 0;
	uint8_t auraTicks = 0;
	uint8_t glowTicks = 0;
	/** Element infusions carried: bit 0 fire, bit 1 lightning, bit 2 cold. */
	uint8_t infusions = 0;
	/** Wind Dancer's stages and the timer to the next; Gale Force's cooldown and the stages it spends next tick (-1 for none). */
	uint8_t windStages = 0;
	uint8_t windTicks = 0;
	uint8_t galeCooldown = 0;
	int8_t galePending = -1;
	/** Ghost Dance's shrouds, the timer to the next, and the energy shield regeneration after losing one. */
	uint8_t shrouds = 0;
	uint16_t shroudTicks = 0;
	uint8_t shroudRegenTicks = 0;
	uint8_t shroudRegenCarry = 0;
	/** Arctic Armour's stages and the timer to the next. */
	uint8_t arcticStages = 0;
	uint8_t arcticTicks = 0;
	/** Barkskin's armour stacks and the game ticks each has left (BarkskinMaxStacks). */
	std::array<uint16_t, 8> barkArmour {};
	std::array<uint8_t, 8> barkTicks {};
	uint8_t frenzyCooldown = 0;
	uint8_t remnantCooldown = 0;
	/** Eternal Rage and Alchemist's Boon build up fractions of a point here. */
	uint16_t rageCarry = 0;
	uint16_t flaskCarry = 0;
	/** Elemental Conflux's element (a DamageType), -1 while it's down, and the ticks until it changes. */
	int8_t confluxElement = -1;
	uint8_t confluxTicks = 0;
	/** Iron Ward's and Plague Bearer's stored damage, and Grim Feast's stored life, in 1/64 points. */
	int32_t ironWardStore = 0;
	int32_t plagueStore = 0;
	int32_t grimFeastStore = 0;
	/** Grim Feast: the life of the last minion of each reviving minion gem that died, in 1/64 points, to price a revive. */
	std::array<int32_t, 9> grimMinionLife {};
	uint8_t grimFeastCooldown = 0;
	/** Convalescence: the game ticks its buff has left, and its cooldown. */
	uint8_t convalescenceTicks = 0;
	uint16_t convalescenceCooldown = 0;
	/** Each banner's Glory, in War, Defiance and Dread order. */
	std::array<uint16_t, 3> glory {};
	/** The banner standing (one at a time): its kind (0 to 2, -1 for none), place and the game ticks it has left. */
	int8_t bannerKind = -1;
	Point bannerPosition {};
	uint8_t bannerTicks = 0;
	/** Whether the hero stood in the banner's area last tick, for the resistances it raises. */
	bool inDreadBanner = false;
	uint16_t bannerFlaskCarry = 0;
	/** Defiance Banner's movement speed, in hundredths of a walk frame skipped. */
	uint8_t walkCarry = 0;
	/** Briarpatch: the thorns stored and its cooldown; the thorny ground's place, game ticks left and damage each pulse, in 1/64 points. */
	int32_t briarStore = 0;
	uint8_t briarCooldown = 0;
	Point briarPosition {};
	uint8_t briarTicks = 0;
	int32_t briarPulse = 0;
	/** Magma Barrier: game ticks until the next block sprays fire (0 when charged), and the block chance it adds now, in percent. */
	uint8_t magmaTicks = 0;
	uint8_t blockPercent = 0;
	/** Scavenged Plating's stacks and the game ticks they have left. */
	uint8_t platingStacks = 0;
	uint8_t platingTicks = 0;
	/** Time of Need: game ticks until it can heal again. */
	uint8_t needTicks = 0;
	/** Charge Regulation: game ticks of faster attacks and casts left. */
	uint16_t regulationTicks = 0;
	/** Withering Presence: game ticks to the next pulse. */
	uint8_t witherPulseTicks = 0;
	/** Trinity: Resonance of fire, lightning and cold; the elements that gained this tick (bits); game ticks since the last gain. */
	std::array<uint8_t, 3> resonance {};
	uint8_t resonanceGained = 0;
	uint8_t resonanceIdleTicks = 0;
	/** Raging Spirits: the fire spell the hero cast last, the spirits its hits may still call, and the game ticks left to call them. */
	SkillSlot ragingSkill {};
	uint8_t ragingBudget = 0;
	uint8_t ragingTicks = 0;
	/** Ravenous Swarm: game ticks until the next swarm can leave, and the swarms out now. */
	uint8_t swarmTicks = 0;
	std::array<RavenousSwarm, 3> swarms {};
};

/** @brief Spirit gems whose number of minions is set in the Skills panel, like POE2's "Set Minion Count". */
constexpr std::array<PersistentSkill, 8> MinionCountSkills {
	PersistentSkill::SkeletalWarriors,
	PersistentSkill::SkeletalMages,
	PersistentSkill::SkeletalSnipers,
	PersistentSkill::SkeletalFrostMages,
	PersistentSkill::SkeletalStormMages,
	PersistentSkill::SkeletalReavers,
	PersistentSkill::SkeletalBrutes,
	PersistentSkill::SkeletalClerics,
};
/** @brief Most minions one gem keeps, besides the ones its supports add. */
constexpr uint8_t MaxMinionsPerGem = 12;
/** @brief A minion count the player hasn't chosen yet: the gem raises its default number. */
constexpr uint8_t DefaultMinionCount = 0xFF;

constexpr std::array<uint8_t, MinionCountSkills.size()> DefaultMinionCounts()
{
	std::array<uint8_t, MinionCountSkills.size()> counts {};
	counts.fill(DefaultMinionCount);
	return counts;
}

/** @brief Spirit gems whose minions rise again after they fall: the skeleton gems, then Wolf Pack. */
constexpr std::array<PersistentSkill, MinionCountSkills.size() + 1> RevivingMinionSkills = [] {
	std::array<PersistentSkill, MinionCountSkills.size() + 1> skills {};
	for (size_t i = 0; i < MinionCountSkills.size(); i++)
		skills[i] = MinionCountSkills[i];
	skills.back() = PersistentSkill::WolfPack;
	return skills;
}();
static_assert(RevivingMinionSkills.size() == std::tuple_size_v<decltype(SpiritRuntime::grimMinionLife)>);

/** @brief Revive timers of one minion gem (not saved; see poe2/spirit.cpp). */
struct MinionRevives {
	/** Game ticks until each fallen minion rises again. */
	std::array<uint16_t, MaxMinionsPerGem + 4> ticks {};
	uint8_t pending = 0;
	/** Minions alive last tick, to tell a death from a minion that hasn't been raised yet. */
	uint8_t lastAlive = 0;
	/** Game ticks until the next minion that hasn't been raised yet comes up. */
	uint8_t raiseTicks = 0;
	/** The level the minions were on, so changing level raises them at once instead of reviving them. */
	int16_t level = -1;
};

/** @brief POE2 Mashup: what a minion that goes with the hero to the next level is (see poe2/minion_travel.hpp). */
enum class MinionTravelKind : uint8_t {
	/** A skeleton (or wolf) of a skeleton gem or a sceptre's granted skill. */
	Gem,
	Golem,
	/** A monster raised by Revive. */
	Revived,
};

/**
 * @brief POE2 Mashup: one of the hero's minions going with the hero to the next level, or waiting while the hero is in
 * town, where the game keeps no monsters (Curtis 2026-10-29: "they should follow me like POE2").
 */
struct TravellingMinion {
	MinionTravelKind kind = MinionTravelKind::Gem;
	/** Its _monster_id. */
	uint16_t type = 0;
	/** Its life and maximum life, as raw Fixed26_6 values. */
	int32_t life = 0;
	int32_t maxLife = 0;
	/** A revived monster's own stats; the others take theirs from the gem again. */
	uint8_t minDamage = 0;
	uint8_t maxDamage = 0;
	uint8_t armorClass = 0;
	uint16_t resistance = 0;
};
/** @brief Most minions that can go with the hero at once. */
constexpr size_t MaxTravellingMinions = 48;

struct Poe2PlayerData;

/**
 * @brief A skill a held caster weapon grants (caster-weapons-plan-v2.md 6.8): its skill (empty while the base's grant
 * isn't built yet), the level the weapon's item level gives, and the hand (0 left, 1 right).
 */
struct GrantedSkill {
	SkillSlot skill {};
	uint8_t baseLevel = 0;
	uint8_t hand = 0;
};

/** @brief The granted skills' counters and timers (caster-weapons-plan-v2.md 6.12; not saved). */
struct GrantedRuntime {
	/** @brief Critical Bolt's casts since its last double hit. */
	uint8_t criticalCasts = 0;
	/** @brief Chiming Bolt's casts since its last Sigil of Power. */
	uint8_t chimingCasts = 0;
	/** @brief The Sigil of Power: game ticks left, and the tile it lies on. */
	uint8_t sigilTicks = 0;
	uint8_t sigilX = 0;
	uint8_t sigilY = 0;
	/** @brief Consecrated Ground's life regeneration: game ticks left, and the raw life still to give. */
	uint8_t regenTicks = 0;
	int32_t regenRaw = 0;
	/** @brief The per-target granted skills' casts, counted from 1, and each recent cast's first monster hit. */
	uint16_t castSerial = 0;
	std::array<uint16_t, 4> castFirstSerial {};
	std::array<int16_t, 4> castFirst {};
};

/** @brief Levels gear adds to the skill, like "+1 to Level of all Melee Skills"; in poe2/modifiers.cpp. */
int ItemSkillLevelBonus(const Poe2PlayerData &data, SkillSlot skill);

/**
 * @brief All POE2 Mashup character state that isn't part of the original hero file.
 *
 * Saved in its own "poe2hero" entry of the hero save, so heroes stay loadable by other builds.
 */
struct Poe2PlayerData {
	/** @brief Support gems socketed in each Skills panel socket's skill. */
	std::array<SupportSockets, NumSkillSlots> slotSupports {};
	/** @brief The tier of each socketed support. */
	std::array<SupportTiers, NumSkillSlots> slotSupportTiers {};
	/** @brief Supports of the weapon's default attack (the DefaultAttackRow) and their tiers. */
	SupportSockets defaultSupports {};
	SupportTiers defaultSupportTiers {};
	/** @brief Support sockets each Skills panel row has opened with Jeweller's Orbs, beyond the first two; the last is the default attack's. */
	std::array<uint8_t, NumSkillSlots + 1> openedSockets {};
	/** @brief Charges of the flasks in belt slots 1 to 5. */
	std::array<uint8_t, NumFlasks> flaskCharges {};
	/** @brief Which flask or charm was in each belt slot last tick (not saved), so one put in from the bag or the ground starts empty. */
	std::array<uint64_t, 8> flaskSlotKeys {};
	bool flaskSlotKeysKnown = false;
	/** @brief The flask or charm lifted from the belt onto the cursor (not saved): put back in the belt, it keeps its charges. */
	uint64_t liftedBeltKey = 0;
	uint8_t liftedFlaskCharges = 0;
	/** @brief Skill gems socketed in the Skills panel. */
	std::array<SkillSlot, NumSkillSlots> skillSlots {};
	/** @brief The skills the held caster weapons grant, at most one a hand (not saved: CalcPlrItemVals fills it). */
	std::array<GrantedSkill, 2> granted {};
	/** @brief The granted skills' counters and timers (not saved). */
	GrantedRuntime grantedState {};
	/** @brief Level of the gem in each Skills panel socket, 1 to 20. */
	std::array<uint8_t, NumSkillSlots> slotLevels {};
	/**
	 * @brief Skill gems inside a meta gem's sockets, like the spells Cast on Dodge casts, and their levels.
	 * A socket holds a support (slotSupports) or a skill, never both, and a skill inside a meta gem is in no other socket.
	 */
	std::array<std::array<SkillSlot, MaxSupportSockets>, NumSkillSlots> metaSkills {};
	std::array<std::array<uint8_t, MaxSupportSockets>, NumSkillSlots> metaSkillLevels {};
	/**
	 * @brief Spirit gems switched on with a right-click in the Skills panel, one bit per socket; an off gem reserves
	 * nothing. Bits 12 and 13 are the other way round: set, the main or off hand's granted Spirit skill is off
	 * (grantedOff), so every hero saved before it starts with them on.
	 */
	uint16_t spiritGemsOn = 0;
	/**
	 * @brief When the Spirit gem in each socket, then each hand's granted Spirit skill, was last switched on, higher for
	 * newer: short of Spirit, the newest switch off first (Curtis 2026-10-30). Not saved: a hero loads with the Skills
	 * panel's order.
	 */
	std::array<uint8_t, NumSkillSlots + 2> spiritOrder {};
	/** @brief The switched-on Spirit gems that are off for lack of Spirit, as last seen, by spiritOrder index (not saved). */
	uint16_t spiritWaiting = 0;
	/** @brief A switched-on Spirit gem taken out of its socket, and its spiritOrder, so it is still on when socketed again (not saved). */
	PersistentSkill liftedSpiritGem = PersistentSkill::None;
	uint8_t liftedSpiritOrder = 0;
	/** @brief Spirit skulls broken, one bit per skull (see SpiritSkullSpirit). */
	uint8_t spiritSkulls = 0;
	/** @brief How many minions each of the MinionCountSkills keeps, or DefaultMinionCount. */
	std::array<uint8_t, MinionCountSkills.size()> minionCounts = DefaultMinionCounts();
	/** @brief What each skill tray slot does. */
	std::array<TrayBinding, NumTraySlots> tray {};
	/** @brief Game ticks left in the current dodge roll (not saved). */
	uint8_t dodgeTicks = 0;
	/** @brief Game ticks until the next dodge roll is allowed (not saved). */
	uint8_t dodgeCooldown = 0;
	/** @brief Game ticks left that hits and missiles pass through the hero, from the start of a roll (not saved). */
	uint8_t untouchableTicks = 0;
	/** @brief Weapon skill queued for the next attack (not saved). */
	WeaponSkill pendingSkill = WeaponSkill::None;
	/** @brief Game ticks before a queued weapon skill is dropped (not saved). */
	uint8_t pendingSkillTicks = 0;
	/** @brief Weapon skill used by the attack in progress (not saved). */
	WeaponSkill activeSkill = WeaponSkill::None;
	/** @brief Which of Flurry's strikes the attack in progress is: 0 the first, 1 the backhand (not saved). */
	uint8_t flurryStrike = 0;
	/** @brief The facing a spinning skill's animation started from (not saved). */
	uint8_t spinFrom = 0;
	/** @brief Skill that hits when the current dash lands, e.g. Leap Slam (not saved). */
	WeaponSkill dashSkill = WeaponSkill::None;
	/** @brief Game ticks the dash skill may take to land before it's dropped; 0 if not dashing (not saved). */
	uint8_t dashSkillTicks = 0;
	/** @brief Where the current dash ends: the skill lands as soon as the hero stands there (not saved). */
	Point dashEnd {};
	/** @brief Where the current dash started, for Dashing Strike's hits along the way (not saved). */
	Point dashStart {};
	/** @brief How far the current dash goes, in percent of the skill's full length, for its added time (not saved). */
	uint8_t dashDistancePercent = 0;
	/** @brief Game ticks left of Battle Cry's empowered melee hits (not saved). */
	uint8_t warcryTicks = 0;
	/** @brief Game ticks Riposte stays ready after a block (not saved). */
	uint8_t riposteTicks = 0;
	/** @brief Build resources (not saved): Frenzy charges and their timer, Combo and Rage and the ticks before they fade. */
	uint8_t frenzyCharges = 0;
	uint16_t frenzyTicks = 0;
	uint8_t combo = 0;
	uint8_t comboTicks = 0;
	uint8_t rage = 0;
	uint8_t rageTicks = 0;
	/** @brief Defensive skill buffs (not saved): Frozen Armor, Holy Shield and Shield Wall, in game ticks left. */
	uint16_t frozenArmorTicks = 0;
	uint16_t holyShieldTicks = 0;
	uint16_t shieldWallTicks = 0;
	/** @brief Soul Harvest: souls taken from corpses, each adding damage until the timer runs out (not saved). */
	uint8_t soulStacks = 0;
	uint16_t soulTicks = 0;
	/** @brief A skill still firing a stream of shots, one every few ticks, e.g. Barrage's arrows (not saved). */
	WeaponSkill volleySkill = WeaponSkill::None;
	uint8_t volleyShotsLeft = 0;
	uint8_t volleyShotsFired = 0;
	uint8_t volleyTicks = 0;
	int16_t volleyTargetX = 0;
	int16_t volleyTargetY = 0;
	/** @brief Hits a per-hit skill landed, one a use (PerHitBurst): which side an ember springs from, Seven-Sided Strike's next enemy; see TakePerHitIndex (not saved). */
	uint16_t perHitCount = 0;
	/**
	 * @brief The hand the next swing uses while the hero holds two weapons: 0 the left hand, 1 the right. Every swing (an
	 * attack, a strike of a skill, a skill use) uses one weapon's damage and the next swing the other's, as in POE2; see
	 * EndSwing (not saved: it is simulated like perHitCount, and a fresh hero or game starts with the left hand).
	 */
	uint8_t swingHand = 0;
	/**
	 * @brief Each hand's damage while the hero holds two weapons, worked out by CalcPlrInv (not saved): the weapon's own
	 * range, in the units the hero's increased damage (_pIBonusDam) scales, so the other weapon's local increase doesn't
	 * count, and its fire and lightning without the other weapon's. All zero when the hero doesn't hold two weapons.
	 */
	struct HandDamage {
		int32_t minDamage = 0;
		int32_t maxDamage = 0;
		int32_t minFire = 0;
		int32_t maxFire = 0;
		int32_t minLightning = 0;
		int32_t maxLightning = 0;
	};
	std::array<HandDamage, 2> handDamage {};
	bool dualWield = false;
	/** @brief Game ticks the character stands still after a skill slowed by its supports (not saved). */
	uint8_t recoveryTicks = 0;
	/** @brief Skill whose cast animation is playing; it goes off on castFrame (not saved). */
	WeaponSkill castSkill = WeaponSkill::None;
	int8_t castFrame = 0;
	/** @brief The player_graphic of the cast skill's animation (not saved). */
	uint8_t castGraphic = 0;
	/** @brief The monster the cast skill was aimed at, or -1 (not saved). */
	int16_t castTargetMonster = -1;
	/**
	 * @brief POE2 Mashup: what the attack or cast under way paid as it started, as raw mana and life, and whether it took
	 * Deep Well's free skill; a dodge roll that breaks it off before it goes off gives it all back (not saved; see
	 * ActionPaymentScope) (Curtis 2026-10-29).
	 */
	int32_t actionPaidMana = 0;
	int32_t actionPaidLife = 0;
	bool actionTookFreeSkill = false;
	void forgetActionPaid()
	{
		actionPaidMana = 0;
		actionPaidLife = 0;
		actionTookFreeSkill = false;
	}
	/** @brief Allocated passive tree nodes, by node id; the class start node isn't stored. */
	std::bitset<MaxPassiveNodes> passives;
	/** @brief The attribute picked on each allocated "+5 to any Attribute" node, as a PassiveAttribute. */
	std::array<uint8_t, MaxPassiveNodes> passiveAttributes {};
	/** @brief Nodes allocated with weapon-set points; they only count while their set is out. */
	std::array<std::bitset<MaxPassiveNodes>, 2> setPassives {};
	/** @brief The weapon set that is out: 0 for I, 1 for II (see poe2/weapon_swap.hpp). */
	uint8_t weaponSet = 0;
	/** @brief Books of Specialisation read, one bit per book (see poe2/weapon_swap.hpp). */
	uint8_t specBooks = 0;
	/** @brief A hardcore hero: on death they move to softcore with only what they wear and carry (poe2/hardcore.hpp). */
	bool hardcore = false;
	/** @brief The shape of the passive tree the allocations were made on (see PassiveTreeHash); a different tree refunds them. */
	uint32_t passiveTreeHash = 0;
	/** @brief Quests that handed over their Book, one bit per quest id, once per hero across games; the top bit
	 * (BooksOnlyFlag) marks a hero moved over from quest points (poe2/weapon_swap.hpp). */
	uint32_t questsRewarded = 0;
	/** @brief Animation frames skipped by attacks and casts thanks to passives (not saved; from CalcPlrItemVals). */
	int8_t passiveAttackFrames = 0;
	int8_t passiveCastFrames = 0;
	/** @brief Increased attack and cast speed in percent from the passive tree and gear, for POE2's timing (not saved; from CalcPlrItemVals). */
	int16_t attackSpeedPercent = 0;
	int16_t castSpeedPercent = 0;
	/** @brief The fraction of a game tick, in 1/65536ths, the last attack or cast carried to the next (see TakeActionTicks; not saved). */
	uint16_t actionTimeRemainder = 0;
	/** @brief Life regenerated per second from gear (not saved; from CalcPlrItemVals). */
	int lifeRegen = 0;
	/** @brief Increased mana regeneration in percent from gear (not saved; from CalcPlrItemVals). */
	int manaRegenBonus = 0;
	/** @brief Fractions of a raw life and mana point carried to the next tick (not saved). */
	int lifeRegenRemainder = 0;
	/** @brief Life regenerated per second in tenths of a percent of maximum life, from the passive tree (not saved). */
	int lifeRegenPerMille = 0;
	/** @brief Block chance added by the passive tree, in percent (not saved). */
	int passiveBlockChance = 0;
	/** @brief The held shield's own block chance, in percent; 0 without a shield (not saved; from CalcPlrItemVals). */
	int shieldBlockChance = 0;
	/** @brief The passive tree's stat totals as of the last CalcPlrItemVals (not saved), indexed by PassiveStat. */
	std::array<int16_t, MaxPassiveStats> passiveTotals {};
	int manaRegenRemainder = 0;
	/** @brief Evasion rating from gear (not saved; from CalcPlrItemVals). */
	int evasion = 0;
	/** @brief POE2 stats the gear's modifiers add up to (not saved; from CalcPlrItemVals). */
	std::array<int16_t, static_cast<size_t>(Poe2ItemStat::Count)> itemStats {};
	/** @brief Rules of the worn bundle uniques, added up (not saved; from CalcPlrItemVals). */
	UniqueRuleTotals uniqueRules {};
	/** @brief Counters and timers of the unique rules; mutable so hits on monsters can count (not saved). */
	mutable UniqueRuleState uniqueState {};
	/** @brief Fire, lightning and cold resistance before the cap, and each one's cap (not saved; from CalcPlrItemVals). */
	static constexpr size_t FireResistanceIndex = 0;
	static constexpr size_t LightningResistanceIndex = 1;
	static constexpr size_t ColdResistanceIndex = 2;
	std::array<int16_t, 3> uncappedResistances {};
	std::array<int16_t, 3> maxResistances {};
	/** @brief Chaos resistance, before its cap, and its cap (not saved; from CalcPlrItemVals). Kept apart from the
	 * elements' arrays above, which unique rules treat as "the elemental resistances". */
	int16_t chaosResistance = 0;
	int16_t uncappedChaosResistance = 0;
	int16_t maxChaosResistance = 0;
	/** @brief Maximum energy shield from gear (not saved; from CalcPlrItemVals). */
	int maxEnergyShield = 0;
	/** @brief Current energy shield as a raw Fixed26_6 value (not saved; refills when loaded). */
	int32_t energyShield = 0;
	/** @brief Game ticks since the last hit, up to the recharge delay (not saved). */
	int energyShieldRechargeTicks = 0;
	int energyShieldRemainder = 0;
	/** @brief Game ticks before Summon Golem brings the golem back (not saved). */
	uint8_t golemCooldown = 0;
	/** @brief Revive timers of the RevivingMinionSkills' minions (not saved). */
	std::array<MinionRevives, RevivingMinionSkills.size()> minionRevives {};
	/**
	 * @brief POE2 Mashup: the minions going with the hero to the next level; while the hero is in town they wait here
	 * for the next dungeon level (saved; see poe2/minion_travel.hpp) (Curtis 2026-10-29).
	 */
	std::array<TravellingMinion, MaxTravellingMinions> travellingMinions {};
	uint8_t travellingMinionCount = 0;
	/** @brief Support gem timers and counters; mutable so hits can count (not saved). */
	mutable SupportRuntime supportState {};
	/** @brief Spirit skill timers and the element infusions carried (not saved). */
	mutable SpiritRuntime spiritState {};
	/** @brief The map (POE2 endgame) the player opened: its dungeon level and tier; tier 0 when there's none. */
	uint8_t mapLevel = 0;
	uint8_t mapTier = 0;
	/** @brief The open map's waystone: its tier, rarity and modifiers. */
	Poe2Craft mapWaystone {};
	/** @brief Revives left in the open map. */
	uint8_t mapRevives = 0;
	/** @brief The open map's boss (a monster id), -1 before the level is made, and whether it died. */
	int16_t mapBoss = -1;
	bool mapBossKilled = false;
	/** @brief Maps the hero completed, for the Atlas. */
	uint16_t mapsCompleted = 0;
	/** @brief The open map's tablets, encounters, reward chests and pinnacle boss. */
	MapRunState mapRun {};
	/** @brief The Atlas passives; each completed map gives a point. */
	AtlasState atlas {};
	/** @brief Currency shards and the gambler's charges. */
	EconomyState economy {};
	/** @brief Raw life and mana still to recover from the flasks being drunk, and the game ticks left (not saved). */
	int32_t lifeFlaskRaw = 0;
	uint8_t lifeFlaskTicks = 0;
	int32_t manaFlaskRaw = 0;
	uint8_t manaFlaskTicks = 0;
	/** @brief Game ticks left of Guard from an Iron Charm (not saved). */
	uint8_t guardTicks = 0;
	/** @brief Game ticks before another charm can go off (not saved). */
	uint8_t charmCooldown = 0;
	/** @brief Which essence, augment or catalyst is on the cursor waiting to be used (not saved). */
	uint8_t currencyVariant = 0;
	/** @brief Game ticks left of the fire, lightning, chaos and cold resistance from Ruby, Topaz, Amethyst and Sapphire charms (not saved). */
	std::array<uint8_t, 4> resistCharmTicks {};
	/** @brief Game ticks left of stun immunity from a Stone Charm (not saved). */
	uint8_t stunImmuneTicks = 0;
	/** @brief Game ticks left of a Flask of the Ox's armour (not saved). */
	uint8_t oxFlaskTicks = 0;
	/** @brief Game ticks towards the next charge for Flasks of the Springs (not saved). */
	uint8_t springsFlaskTicks = 0;
	/** @brief Resistance penalty the resistances were last calculated with; -1 before the first level loads (not saved). */
	int resistancePenalty = -1;

	void clear()
	{
		for (SupportSockets &supports : slotSupports)
			supports.fill(SupportGem::None);
		for (SupportTiers &tiers : slotSupportTiers)
			tiers.fill(1);
		defaultSupports.fill(SupportGem::None);
		defaultSupportTiers.fill(1);
		openedSockets.fill(0);
		flaskCharges.fill(MaxFlaskCharges);
		lifeFlaskRaw = 0;
		lifeFlaskTicks = 0;
		currencyVariant = 0;
		manaFlaskRaw = 0;
		manaFlaskTicks = 0;
		skillSlots.fill({});
		granted = {};
		grantedState = {};
		slotLevels.fill(1);
		for (auto &skills : metaSkills)
			skills.fill({});
		for (auto &levels : metaSkillLevels)
			levels.fill(1);
		spiritGemsOn = 0;
		spiritOrder.fill(0);
		spiritWaiting = 0;
		liftedSpiritGem = PersistentSkill::None;
		liftedSpiritOrder = 0;
		spiritSkulls = 0;
		minionCounts.fill(DefaultMinionCount);
		for (size_t slot = 0; slot < NumTraySlots; slot++)
			tray[slot] = DefaultTrayBinding(static_cast<TraySlot>(slot));
		dodgeTicks = 0;
		dodgeCooldown = 0;
		untouchableTicks = 0;
		pendingSkill = WeaponSkill::None;
		pendingSkillTicks = 0;
		activeSkill = WeaponSkill::None;
		flurryStrike = 0;
		spinFrom = 0;
		dashSkill = WeaponSkill::None;
		dashSkillTicks = 0;
		dashEnd = {};
		dashStart = {};
		dashDistancePercent = 0;
		warcryTicks = 0;
		volleySkill = WeaponSkill::None;
		volleyShotsLeft = 0;
		volleyShotsFired = 0;
		perHitCount = 0;
		swingHand = 0;
		handDamage = {};
		dualWield = false;
		volleyTicks = 0;
		recoveryTicks = 0;
		castSkill = WeaponSkill::None;
		castFrame = 0;
		castTargetMonster = -1;
		forgetActionPaid();
		passives.reset();
		passiveAttributes.fill(0);
		for (auto &nodes : setPassives)
			nodes.reset();
		weaponSet = 0;
		specBooks = 0;
		hardcore = false;
		passiveTreeHash = 0;
		questsRewarded = 0;
		passiveAttackFrames = 0;
		passiveCastFrames = 0;
		attackSpeedPercent = 0;
		castSpeedPercent = 0;
		actionTimeRemainder = 0;
		lifeRegen = 0;
		manaRegenBonus = 0;
		lifeRegenRemainder = 0;
		lifeRegenPerMille = 0;
		passiveBlockChance = 0;
		shieldBlockChance = 0;
		passiveTotals.fill(0);
		manaRegenRemainder = 0;
		evasion = 0;
		itemStats.fill(0);
		uniqueRules.fill(0);
		uniqueState = {};
		uncappedResistances.fill(0);
		maxResistances.fill(0);
		chaosResistance = 0;
		uncappedChaosResistance = 0;
		maxChaosResistance = 0;
		maxEnergyShield = 0;
		energyShield = 0;
		energyShieldRechargeTicks = 0;
		energyShieldRemainder = 0;
		golemCooldown = 0;
		minionRevives = {};
		travellingMinions = {};
		travellingMinionCount = 0;
		supportState = {};
		spiritState = {};
		frozenArmorTicks = 0;
		holyShieldTicks = 0;
		shieldWallTicks = 0;
		soulStacks = 0;
		soulTicks = 0;
		riposteTicks = 0;
		frenzyCharges = 0;
		frenzyTicks = 0;
		combo = 0;
		comboTicks = 0;
		rage = 0;
		rageTicks = 0;
		guardTicks = 0;
		mapLevel = 0;
		mapTier = 0;
		mapWaystone = {};
		mapRevives = 0;
		mapBoss = -1;
		mapBossKilled = false;
		mapsCompleted = 0;
		mapRun = {};
		atlas = {};
		economy = {};
		charmCooldown = 0;
		resistCharmTicks = {};
		stunImmuneTicks = 0;
		oxFlaskTicks = 0;
		springsFlaskTicks = 0;
		resistancePenalty = -1;
	}

	[[nodiscard]] bool isDodging() const { return dodgeTicks > 0; }
	[[nodiscard]] bool isUntouchable() const { return untouchableTicks > 0; }

	/** @brief Advances the dodge roll and weapon skill timers by one game tick. */
	void tick()
	{
		if (dodgeTicks > 0)
			dodgeTicks--;
		if (dodgeCooldown > 0)
			dodgeCooldown--;
		if (untouchableTicks > 0)
			untouchableTicks--;
		if (pendingSkillTicks > 0 && --pendingSkillTicks == 0)
			pendingSkill = WeaponSkill::None;
		if (warcryTicks > 0)
			warcryTicks--;
		if (riposteTicks > 0)
			riposteTicks--;
		if (frozenArmorTicks > 0)
			frozenArmorTicks--;
		if (holyShieldTicks > 0)
			holyShieldTicks--;
		if (shieldWallTicks > 0)
			shieldWallTicks--;
		if (soulTicks > 0 && --soulTicks == 0)
			soulStacks = 0;
		if (frenzyTicks > 0 && --frenzyTicks == 0)
			frenzyCharges = 0;
		if (comboTicks > 0 && --comboTicks == 0)
			combo = 0;
		// Rage drains a point every 4 ticks once 2 seconds pass without a hit.
		if (rageTicks > 0) {
			rageTicks--;
		} else if (rage > 0) {
			rage--;
			rageTicks = 3;
		}
		if (recoveryTicks > 0)
			recoveryTicks--;
	}

	/** @brief The total of a rule of the worn bundle uniques. */
	[[nodiscard]] int rule(UniqueRule rule) const { return uniqueRules[static_cast<size_t>(rule)]; }

	/** @brief The supports of the Skills panel socket holding the skill; none if it isn't socketed. */
	[[nodiscard]] SupportSockets supportsFor(SkillSlot skill) const;
	[[nodiscard]] SupportSockets supportsFor(SpellID spell) const { return supportsFor(SkillSlot::ForSpell(spell)); }
	[[nodiscard]] bool hasSupport(SkillSlot skill, SupportGem gem) const;
	/** @brief The tier of the support on the skill, 1 to 3; 0 if the skill doesn't have it. */
	[[nodiscard]] int supportTier(SkillSlot skill, SupportGem gem) const;
	/** @brief The tier of the support in a socket, 1 to 3. */
	[[nodiscard]] int socketTier(size_t slot, size_t socket) const
	{
		return std::clamp<int>(tiersAt(slot)[socket], 1, MaxSupportTier);
	}
	/** @brief The support sockets of a Skills panel row: a gem socket, or the DefaultAttackRow. */
	[[nodiscard]] SupportSockets &supportsAt(size_t row) { return row == DefaultAttackRow ? defaultSupports : slotSupports[row]; }
	[[nodiscard]] const SupportSockets &supportsAt(size_t row) const { return row == DefaultAttackRow ? defaultSupports : slotSupports[row]; }
	[[nodiscard]] SupportTiers &tiersAt(size_t row) { return row == DefaultAttackRow ? defaultSupportTiers : slotSupportTiers[row]; }
	[[nodiscard]] const SupportTiers &tiersAt(size_t row) const { return row == DefaultAttackRow ? defaultSupportTiers : slotSupportTiers[row]; }
	[[nodiscard]] bool hasSupport(SpellID spell, SupportGem gem) const { return hasSupport(SkillSlot::ForSpell(spell), gem); }

	/** @brief Whether a gem for the spell is socketed in the Skills panel. */
	[[nodiscard]] bool hasSpellGem(SpellID spell) const
	{
		for (const SkillSlot &slot : skillSlots) {
			if (slot.spell() == spell)
				return true;
		}
		return false;
	}

	/** @brief Level of the gem in a Skills panel socket. */
	[[nodiscard]] int gemLevel(size_t socket) const
	{
		return std::clamp<int>(slotLevels[socket], 1, MaxGemLevel);
	}

	/** @brief Whether the Spirit gem in a Skills panel socket is switched on. */
	[[nodiscard]] bool spiritGemOn(size_t socket) const
	{
		return socket < NumSkillSlots && ((spiritGemsOn >> socket) & 1U) != 0;
	}

	/** @brief Whether the Spirit skill the sceptre in a hand (0 main, 1 off) grants is switched off in the Skills panel. */
	[[nodiscard]] bool grantedOff(int hand) const
	{
		return hand >= 0 && hand < 2 && ((spiritGemsOn >> (GrantedSpiritOffBit + static_cast<size_t>(hand))) & 1U) != 0;
	}

	/** @brief Switches the Spirit skill the sceptre in a hand grants off, or back on. */
	void toggleGranted(int hand)
	{
		if (hand >= 0 && hand < 2)
			spiritGemsOn ^= static_cast<uint16_t>(1U << (GrantedSpiritOffBit + static_cast<size_t>(hand)));
	}

	/** @brief The Skills panel row and socket of the meta gem the skill is inside, if it is. */
	[[nodiscard]] std::optional<std::pair<size_t, size_t>> metaSocketOf(SkillSlot skill) const
	{
		if (skill.isEmpty())
			return std::nullopt;
		for (size_t row = 0; row < NumSkillSlots; row++) {
			for (size_t socket = 0; socket < MaxSupportSockets; socket++) {
				if (metaSkills[row][socket] == skill)
					return std::make_pair(row, socket);
			}
		}
		return std::nullopt;
	}

	/** @brief Level the skill is used at: its socketed gem's level plus what gear adds; 0 if it isn't socketed. */
	[[nodiscard]] int skillLevel(SkillSlot skill) const
	{
		for (size_t socket = 0; socket < skillSlots.size(); socket++) {
			if (!skill.isEmpty() && skillSlots[socket] == skill)
				return std::clamp(gemLevel(socket) + ItemSkillLevelBonus(*this, skill), 1, static_cast<int>(MaxGemLevel));
		}
		// A spell inside a meta gem has its own level.
		if (const auto meta = metaSocketOf(skill))
			return std::clamp(metaSkillLevels[meta->first][meta->second] + ItemSkillLevelBonus(*this, skill), 1, static_cast<int>(MaxGemLevel));
		return 0;
	}
	[[nodiscard]] int skillLevel(SpellID spell) const { return skillLevel(SkillSlot::ForSpell(spell)); }
	[[nodiscard]] int skillLevel(WeaponSkill skill) const { return skillLevel(SkillSlot::ForAttack(skill)); }
	[[nodiscard]] int skillLevel(PersistentSkill skill) const { return skillLevel(SkillSlot::ForPersistent(skill)); }

	/**
	 * @brief Level a held caster weapon grants the skill at: its item level's gem level plus what gear adds; 0 if no held
	 * weapon grants it.
	 */
	[[nodiscard]] int grantedLevel(SkillSlot skill) const
	{
		if (skill.isEmpty())
			return 0;
		for (const GrantedSkill &entry : granted) {
			if (entry.baseLevel > 0 && entry.skill == skill)
				return std::clamp(entry.baseLevel + ItemSkillLevelBonus(*this, skill), 1, static_cast<int>(MaxGemLevel));
		}
		return 0;
	}

	/** @brief Whether a held caster weapon grants the skill. */
	[[nodiscard]] bool grants(SkillSlot skill) const
	{
		if (skill.isEmpty())
			return false;
		for (const GrantedSkill &entry : granted) {
			if (entry.baseLevel > 0 && entry.skill == skill)
				return true;
		}
		return false;
	}

	/**
	 * @brief Whether the skill is a held weapon's granted attack with no gem of its own in the Skills panel: it takes the
	 * default attack row's supports, as the left click it is (caster-weapons-plan-v2.md 6.7).
	 */
	[[nodiscard]] bool takesDefaultSupports(SkillSlot skill) const
	{
		if (skill.kind != SkillKind::Attack || !grants(skill))
			return false;
		for (const SkillSlot &socketed : skillSlots) {
			if (socketed == skill)
				return false;
		}
		return !metaSocketOf(skill);
	}

	/** @brief Level the skill is used at: the socketed gem's (skillLevel) if there is one, else a held weapon's grant. */
	[[nodiscard]] int effectiveLevel(SkillSlot skill) const
	{
		const int socketed = skillLevel(skill);
		return socketed > 0 ? socketed : grantedLevel(skill);
	}
	[[nodiscard]] int effectiveLevel(WeaponSkill skill) const { return effectiveLevel(SkillSlot::ForAttack(skill)); }
	[[nodiscard]] int effectiveLevel(PersistentSkill skill) const { return effectiveLevel(SkillSlot::ForPersistent(skill)); }
};

} // namespace devilution
