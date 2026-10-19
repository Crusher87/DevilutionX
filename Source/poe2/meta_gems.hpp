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

#include "engine/point.hpp"
#include "poe2/player_data.hpp"

namespace devilution {

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

[[nodiscard]] bool IsMetaGem(PersistentSkill skill);
[[nodiscard]] bool IsMetaGem(SkillSlot skill);
/** @brief Whether the skill can go inside a meta gem: a spell that isn't a movement or a channelled one, like POE2's triggerable spells. */
[[nodiscard]] bool CanGoInMetaGem(SkillSlot skill);
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
/** @brief The Energy at which the row's meta gem casts: MetaEnergyPerSpell for each spell inside. */
[[nodiscard]] int MetaMaxEnergy(const Poe2PlayerData &data, size_t row);
/** @brief Energy the row's meta gem has now. */
[[nodiscard]] int MetaEnergy(const Player &player, size_t row);

/** @brief The old trigger support a meta gem replaced, which comes out as the meta gem from older heroes. */
[[nodiscard]] PersistentSkill MetaGemForRetiredSupport(SupportGem gem);
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
/** @brief Each game tick: Cast while Channelling gains Energy while a stream skill fires and loses it while not. */
void ProcessMetaGems(Player &player);

/** @brief Older heroes had Cast on Dodge, Cast when Hit and Cast while Channelling as supports: they come out as the new meta gems. */
void MigrateTriggerSupports(Player &player);

} // namespace devilution
