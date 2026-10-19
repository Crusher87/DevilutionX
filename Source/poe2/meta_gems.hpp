/**
 * @file poe2/meta_gems.hpp
 *
 * POE2 Mashup meta gems: Spirit gems like POE2's Cast on Dodge that hold spells in their sockets (Curtis 2026-10-19).
 * An event fills the gem's Energy, and when it's full every spell inside casts at once and the Energy empties.
 * Supports in the row work on the spells inside, and a spell inside a meta gem is in no other socket.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "engine/point.hpp"
#include "poe2/player_data.hpp"
#include "utils/fixed_point.hpp"

namespace devilution {

struct Monster;
struct Player;

/** @brief Energy each spell inside adds to the bar: POE2's 10 per 0.1 second of cast time, with every spell counted as 0.8 seconds. */
constexpr int MetaEnergyPerSpell = 80;
/** @brief Each gem level above 1 gains 3% more Energy, 57% at level 20, like POE2's meta gems. */
constexpr int MetaEnergyPercentPerLevel = 3;
/** @brief Cast on Dodge's Energy for each tile rolled: one spell every 2 rolls at gem level 1, every roll at level 20. */
constexpr int CastOnDodgeEnergyPerTile = 17;
/** @brief Cast when Damage Taken's Energy for each hit you take (POE2). */
constexpr int CastWhenDamageTakenEnergy = 30;
/** @brief Cast while Channelling's Energy each second while channelling, and what it loses each second while not (POE2). */
constexpr int ChannellingEnergyPerSecond = 60;
constexpr int ChannellingDrainPerSecond = 25;
/** @brief Reaper's Invocation's Energy for a melee kill of a normal, magic, rare and unique monster, and the most it stores. */
constexpr int ReaperEnergyNormal = 30;
constexpr int ReaperEnergyMagic = 60;
constexpr int ReaperEnergyRare = 150;
constexpr int ReaperEnergyUnique = 600;
/** @brief The most Energy an invocation stores (POE2). */
constexpr int InvocationMaxEnergy = 500;
constexpr int ReaperMaxEnergy = InvocationMaxEnergy;
/** @brief Cast on Critical's Energy for a critical hit: up to this for each 1 of the monster's power, by how much of its life the hit took. */
constexpr int CritEnergyPerPower = 100;
/** @brief Cast on Critical's least Energy for each 1 of the monster's power, however small the hit. */
constexpr int CritEnergyLeastPerPower = 5;
/** @brief Cast on Critical's spells deal 20% less damage (POE2). */
constexpr int CastOnCriticalDamagePercent = 80;
/** @brief Energy for freezing, shocking and igniting a monster, for each 1 of its power: POE2's freeze counts ten times a shock or ignite. */
constexpr int FreezeEnergyPerPower = 40;
constexpr int ShockEnergyPerPower = 4;
constexpr int IgniteEnergyPerPower = 4;
/** @brief Cast on Minion Death's Energy for each of your minions that dies. */
constexpr int MinionDeathEnergy = 50;
/** @brief Barrier Invocation stores 1 Energy for each this much energy shield lost to hits. */
constexpr int BarrierShieldPerEnergy = 10;
/** @brief Blasphemy reserves this much Spirit for each curse inside (POE2). */
constexpr int BlasphemySpiritPerCurse = 60;
/** @brief Blasphemy's curses reach this many tiles around you, and their strength in percent at gem level 1 and 20. */
constexpr int BlasphemyRadius = 3;
constexpr int BlasphemyStrengthLevel1 = 50;
constexpr int BlasphemyStrengthLevel20 = 59;
/** @brief Blasphemy curses the enemies around you every half second, for a second, so a curse ends soon after an enemy leaves. */
constexpr int BlasphemyPulseTicks = 10;
constexpr int BlasphemyCurseTicks = 20;
/** @brief Horadric Totem's totem lasts 8 seconds, and casts each 0.8 seconds, which is POE2's 25% less cast speed on a 0.6 second spell. */
constexpr int HoradricTotemTicks = 8 * 20;
constexpr int HoradricTotemCastTicks = 21;
/** @brief Horadric Totem places a second totem at once from this gem level (POE2's Spell Totem). */
constexpr int HoradricTotemSecondAtLevel = 14;

[[nodiscard]] bool IsMetaGem(PersistentSkill skill);
[[nodiscard]] bool IsMetaGem(SkillSlot skill);
/** @brief A meta gem that stores Energy and casts when you use it from the skill tray, like POE2's invocations. */
[[nodiscard]] bool IsInvocationGem(SkillSlot skill);
/** @brief A meta gem that fills with Energy and casts the skills inside: neither Blasphemy nor the Horadric Totem. */
[[nodiscard]] bool UsesMetaEnergy(SkillSlot skill);
/** @brief Whether the skill can go inside a meta gem: a spell that isn't a movement or a channelled one, like POE2's triggerable spells. */
[[nodiscard]] bool CanGoInMetaGem(SkillSlot skill);
/** @brief Whether the skill can go inside this meta gem: Blasphemy takes curses and the rest take spells. */
[[nodiscard]] bool CanGoInMetaGem(PersistentSkill meta, SkillSlot skill);
/** @brief What a meta gem's sockets take, for the Skills panel. */
[[nodiscard]] std::string_view MetaGemTakesText(PersistentSkill meta);
/** @brief What goes in a meta gem: "curse" for Blasphemy, else "spell". */
[[nodiscard]] std::string_view MetaSkillWord(PersistentSkill meta);
/** @brief The Skills panel's hint for a meta gem with nothing inside. */
[[nodiscard]] std::string_view MetaEmptyHint(PersistentSkill meta);
/** @brief When the skills inside the meta gem of a Skills panel row go off. */
[[nodiscard]] std::string MetaInsideText(const Player &player, size_t row);
/** @brief How strong Blasphemy's curses are at a gem level, in percent of a curse cast by hand. */
[[nodiscard]] int BlasphemyStrength(int gemLevel);
/** @brief Curses inside the local player's Blasphemy, which reserves Spirit for each. */
[[nodiscard]] int BlasphemyCurseCount(const Player &player);
/** @brief Horadric Totem's mana cost at a gem level: POE2 Spell Totem's. */
[[nodiscard]] int HoradricTotemManaCost(int gemLevel);
/** @brief How many Horadric Totems can stand at once at a gem level. */
[[nodiscard]] int HoradricTotemLimit(int gemLevel);
/** @brief A monster's power for Cast on Critical and Cast on Elemental Ailment: 1 normal, 2 magic, 5 rare, 20 unique, like POE2's monster power. */
[[nodiscard]] int MonsterPower(const Monster &monster);

/** @brief Whether a support fits the spells of a meta gem: supports that act when you use a skill yourself don't (POE2). */
[[nodiscard]] bool SupportFitsTriggeredSpells(SupportGem gem);
/** @brief Whether a support fits the meta gem of a row: it must fit one of the spells inside. */
[[nodiscard]] bool MetaGemTakesSupport(const Poe2PlayerData &data, size_t row, SupportGem gem);
/** @brief Whether a support fits the meta gem wherever the local player has it socketed. */
[[nodiscard]] bool MetaGemTakesSupport(SkillSlot meta, SupportGem gem);
/**
 * @brief The supports that work on a skill inside a meta gem: those of the meta gem's row that fit the skill.
 * Empty if the skill isn't inside one.
 */
[[nodiscard]] const SupportSockets &MetaSupportsFor(const Poe2PlayerData &data, SkillSlot skill);

/** @brief Skills inside the meta gem of a Skills panel row. */
[[nodiscard]] int MetaSkillCount(const Poe2PlayerData &data, size_t row);
/** @brief The Energy at which the row's meta gem casts, or that an invocation spends: MetaEnergyPerSpell for each spell inside. */
[[nodiscard]] int MetaMaxEnergy(const Poe2PlayerData &data, size_t row);
/** @brief The most Energy the row's meta gem holds: what it casts at, or InvocationMaxEnergy for an invocation. */
[[nodiscard]] int MetaEnergyCap(const Poe2PlayerData &data, size_t row);
/** @brief Energy the row's meta gem has now. */
[[nodiscard]] int MetaEnergy(const Player &player, size_t row);

/** @brief The Spirit gem that replaced an old support, which comes out as that gem from older heroes. */
[[nodiscard]] PersistentSkill SpiritGemForRetiredSupport(SupportGem gem);
[[nodiscard]] bool IsRetiredSupport(SupportGem gem);

enum class MetaSocketResult : uint8_t {
	Socketed,
	/** A skill or support was in the socket and came out. */
	Replaced,
	/** The row holds no meta gem. */
	NotMeta,
	/** The socket isn't open yet. */
	Locked,
	/** The skill can't be triggered: not a spell, or a movement or channelled spell. */
	NotTriggerable,
	/** The skill is already in another socket of the Skills panel. */
	AlreadySocketed,
};

/** @brief Puts a skill gem in a meta gem's socket; what was there comes out (a skill in @p replaced, a support as an item). */
MetaSocketResult SocketSkillInMeta(Player &player, size_t row, size_t socket, SkillSlot skill, int level, SkillSlot &replaced, int &replacedLevel);
/** @brief Takes the skill out of a meta socket; empty if there was none. */
SkillSlot UnsocketMetaSkill(Poe2PlayerData &data, size_t row, size_t socket, int *level = nullptr);
/** @brief Gives back the skills inside a row's meta gem as gems, when the meta gem comes out. */
void ReturnMetaSkills(Player &player, size_t row);

/** @brief A dodge roll of this many tiles: Cast on Dodge gains Energy. */
void OnMetaDodge(Player &player, int tiles);
/** @brief The player took damage from a hit: Cast when Damage Taken gains Energy. */
void OnMetaDamageTaken(Player &player, Point attacker);
/** @brief The player killed a monster with this skill: a melee kill stores Energy in Reaper's Invocation. */
void OnMetaKill(Player &player, const Monster &monster, SkillSlot skill);
/** @brief A critical hit of the player's dealt this much: Cast on Critical gains Energy, more for stronger monsters and harder hits. */
void OnMetaCrit(Player &player, const Monster &monster, Fixed26_6 dealt);
enum class MetaAilment : uint8_t {
	Freeze,
	Shock,
	Ignite,
};
/** @brief The player froze, shocked or ignited a monster: Cast on Elemental Ailment and Elemental Invocation gain Energy. */
void OnMetaAilment(Player &player, const Monster &monster, MetaAilment ailment);
/** @brief One of the player's minions died: Cast on Minion Death gains Energy. */
void OnMetaMinionDied(Player &player, const Monster &minion);
/** @brief Hits took this much of the player's energy shield: Barrier Invocation stores Energy. */
void OnMetaShieldLost(Player &player, Fixed26_6 lost);
/** @brief Each game tick: Cast while Channelling gains Energy while a stream skill fires and loses it while not, and Blasphemy curses. */
void ProcessMetaGems(Player &player);

enum class InvokeResult : uint8_t {
	Cast,
	/** The row holds no invocation, or it's switched off or short of Spirit. */
	Off,
	NoSpells,
	Town,
	NotEnoughEnergy,
	NoMana,
};

/** @brief Uses the row's invocation: with the Energy for it, the spells inside cast at the target and it spends that Energy. */
InvokeResult InvokeMetaGem(Player &player, size_t row, Point target);
/** @brief Uses the row's Horadric Totem: for its mana it places a totem at the target that casts the spells inside. */
InvokeResult PlaceHoradricTotem(Player &player, size_t row, Point target);
/** @brief The next spell a Horadric Totem in this row casts, going round the spells inside; empty when there are none. */
SkillSlot NextTotemSpell(const Player &player, size_t row, uint8_t &next);

/** @brief Older heroes had Cast on Dodge, Cast when Hit, Cast while Channelling, Cast on Kill, Decoy, Curse on Hit and Horadric Totem as
 * supports: they come out as the new Spirit gems. */
void MigrateTriggerSupports(Player &player);

} // namespace devilution
