/**
 * @file poe2/support_table.hpp
 *
 * POE2 Mashup: the support gems planned in the skills design document, described by a table.
 * Skills carry tags (Attack, Spell, Projectile, Slam, Fire...) and a support fits a skill by its tags.
 * The supports' effects hook into hits, kills, casts and minions from here.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "engine/point.hpp"
#include "poe2/player_data.hpp"
#include "utils/fixed_point.hpp"

namespace devilution {

struct ItemData;
struct Missile;
struct Monster;
struct Player;
enum class DamageType : uint8_t;
enum class GemColor : uint8_t;

/** @brief What a skill is and does, for the supports to fit by. */
enum SkillTag : uint32_t {
	ST_Attack = 1U << 0,
	ST_Spell = 1U << 1,
	ST_Melee = 1U << 2,
	ST_Projectile = 1U << 3,
	ST_Area = 1U << 4,
	ST_Duration = 1U << 5,
	ST_Slam = 1U << 6,
	ST_Warcry = 1U << 7,
	ST_Curse = 1U << 8,
	ST_Minion = 1U << 9,
	ST_Rune = 1U << 10,
	ST_Movement = 1U << 11,
	ST_Fire = 1U << 12,
	ST_Lightning = 1U << 13,
	ST_Frost = 1U << 14,
	ST_Physical = 1U << 15,
	ST_Healing = 1U << 16,
	/** The skill hits for damage. */
	ST_Damage = 1U << 17,
	/** Walls, runes, sentries and totems: things the skill leaves on the ground. */
	ST_Placed = 1U << 18,
	/** A bow or crossbow attack. */
	ST_Bow = 1U << 19,
	/** A quarterstaff attack. */
	ST_Staff = 1U << 20,
	/** The skill deals damage over time. */
	ST_DamageOverTime = 1U << 21,
	/** The skill keeps firing while it is used, like Barrage's stream. */
	ST_Channelling = 1U << 22,
	ST_Strike = 1U << 23,
	/** A herald: its explosions hit for it, and only supports that change those fit. */
	ST_Herald = 1U << 24,
	/** The skill deals chaos damage. Not elemental. */
	ST_Chaos = 1U << 25,
	ST_Elemental = ST_Fire | ST_Lightning | ST_Frost,
};

enum SupportFlag : uint8_t {
	/** A plain damage support: a skill takes only one. */
	SF_Power = 1 << 0,
	/** A unique lineage support: found, not cut, and one of each in use. */
	SF_Lineage = 1 << 1,
	/** A trigger support: goes on a spell, which then casts itself. */
	SF_Trigger = 1 << 2,
};

struct SupportData {
	SupportGem gem;
	std::string_view name;
	std::string_view description;
	/** What kind of skill it fits, e.g. "Needs a slam"; empty for any skill. */
	std::string_view needs;
	GemColor color;
	/** The gem's level: an uncut support gem of this level can be cut into it. */
	uint8_t level;
	/** Mana cost multiplier in percent. */
	uint16_t mana;
	/** The skill needs at least one of these tags (if any), all of needAll and none of exclude. */
	uint32_t needAny;
	uint32_t needAll;
	uint32_t exclude;
	uint8_t flags;
	/** Supports of the same family rule each other out on a skill; 0 for none. */
	uint8_t family;
};

/** @brief The skill's tags. */
uint32_t SkillTags(SkillSlot skill);
/** @brief Uses a skill as a trigger does: free (the caller pays), at this damage in percent, and it can't set off more supports. */
void UseTriggeredSkill(Player &player, SkillSlot skill, Point target, int damagePercent = 100);
/** @brief The skill of the hit in progress; empty outside a hit. */
SkillSlot SupportHitSkill();
/** @brief The nearest monster's tile within range, else the tile in front of the player. */
Point NearestMonsterTarget(const Player &player, Point from, int range);
/** @brief Spirit the supports of a Spirit gem add to its reservation, like Deadly Herald's 20. */
int SupportAddedSpirit(const Player &player, SkillSlot skill);

std::span<const SupportData> SupportTable();
/** @brief The table entry of a support, or nullptr for the first supports, which have their own code. */
const SupportData *FindSupportData(SupportGem gem);
/** @brief The table support an item is, from its name ("Volley Support"). */
SupportGem SupportGemForTableItem(const ItemData &data);
bool IsLineageSupport(SupportGem gem);
/** @brief Whether a table support fits a skill by its tags. */
bool TableSupportFits(const SupportData &data, SkillSlot skill);
/** @brief Whether two supports rule each other out on one skill. */
bool SupportsConflict(SupportGem a, SupportGem b);
/** @brief Whether the character has a lineage support socketed anywhere other than the given socket. */
bool LineageSupportInUse(const Poe2PlayerData &data, SupportGem gem, size_t slot, size_t socket);

/**
 * @brief While alive, the damage the local player deals is a hit of the given skill: its supports' damage changes apply,
 * and when it ends the supports' on-hit effects go off on the monster that was hit.
 */
class SupportHitScope {
public:
	SupportHitScope(const Player &player, SkillSlot skill);
	~SupportHitScope();
	SupportHitScope(const SupportHitScope &) = delete;
	SupportHitScope &operator=(const SupportHitScope &) = delete;

private:
	bool active_;
};

/** @brief The skill a missile was fired by, for its supports. */
SkillSlot MissileSkill(const Missile &missile);
void SetMissileSkill(Missile &missile, const Player &player, SkillSlot skill);

/** @brief Damage applied to a monster; counted for the hit in progress. */
void NoteSupportedDamage(Monster &monster, DamageType type, Fixed26_6 damage);
/**
 * @brief The share of a hit's damage of this type that got through to the monster, in percent of what the hit had
 * before the monster's resistance and damage taken (MonsterDamageTakenPercent); poison counts the hit from before them.
 */
void NoteHitDamageTakenPercent(const Monster &monster, DamageType type, int percent);
/**
 * POE2's poison (Curtis 2026-10-30): each second for 2 seconds it deals 20% of the hit's physical and chaos damage, as
 * counted before the monster's resistance. Only the strongest poison on a monster deals damage, unless the skill or
 * support says its poisons stack.
 */
constexpr int PoisonPercentPerSecond = 20;
constexpr int PoisonTicks = 2 * 20;
/**
 * @brief Poisons a monster: total is the poison's whole damage, as a raw Fixed26_6, spread over ticks. With maxStacks 1
 * only the strongest poison counts: a weaker one is lost, a stronger one takes over. More stacks add up to that many.
 */
void InflictPoison(const Player &player, Monster &monster, int32_t total, int ticks, int maxStacks = 1);
/** @brief What one poison tick deals the monster, after its chaos resistance, as a raw Fixed26_6. */
[[nodiscard]] int32_t PoisonTickDamage(const Monster &monster, int32_t perTick);
/** @brief More or less damage from the supports of the hit in progress, and from wither, in percent. */
int SupportHitDamagePercent(const Monster &monster, DamageType type);
/** @brief How much more damage a monster's Withered stacks make it take, in percent. */
[[nodiscard]] int WitheredDamageTakenPercent(const Monster &monster);
/** @brief The penetration supports of the hit in progress, against the damage type's resistance. */
int SupportHitPenetration(DamageType type);
/** @brief Ailment chance of the hit in progress, in percent of normal. */
int SupportAilmentChancePercent(DamageType type);
/** @brief Ailment strength of the hit in progress (ignite damage, cold buildup), in percent of normal. */
int SupportAilmentEffectPercent(DamageType type);
/** @brief A hit of the player froze a monster. */
void OnSupportedFreeze(const Player &player);

/** @brief A skill went off (a cast, a strike, a landing); the supports that repeat or add to it act now. */
void OnSkillUsed(Player &player, SkillSlot skill, Point target);
/** @brief A monster the local player hit last died. */
void OnSupportedKill(Monster &monster);
/** @brief A monster hit the local player. */
void OnPlayerHitBySupportedMonster(Player &player, Monster &monster);
/** @brief The local player started a dodge roll. */
void OnSupportedDodge(Player &player, int tiles);
/** @brief The attack Mirage Archer's mirage shoots: the first bow attack in the Skills panel the weapon allows. */
SkillSlot MirageArcherAttack(const Player &player);
/** @brief Mirages shooting now. */
int MirageCount();
/** @brief Repeats, totems, decoys, seals and timers; once per game tick for the local player. */
void ProcessSupportEffects(Player &player);
/** @brief Poison and wither; once per game tick per monster. */
void ProcessSupportMonsterEffects(Monster &monster);
void ClearSupportEffects();

/** @brief A table skill's radius with the supports' area changes. */
int SupportAreaRadius(const Player &player, SkillSlot skill, int radius);
/** @brief A skill's duration in ticks with the supports' duration changes. */
int SupportDurationTicks(const Player &player, SkillSlot skill, int ticks);
/** @brief Extra projectiles from Volley, Stored Volley's seals and Kashya's Volley. */
int SupportExtraProjectileCount(const Player &player, SkillSlot skill);
/** @brief Projectiles go out in a circle (Nova Projectiles); call once per use, as Nova Projectiles II waits between novas. */
bool SupportFiresNova(const Player &player, SkillSlot skill);
/** @brief Projectiles a nova fires besides one in each direction (Nova Projectiles II). */
int SupportNovaExtraProjectiles(const Player &player, SkillSlot skill);
/** @brief A spell was cast, for Faster Casting III. */
void NoteRecentSpell(SupportRuntime &state, uint8_t spell);
/** @brief How many different spells were cast lately (Faster Casting III). */
int RecentSpellCount(const SupportRuntime &state);
/** @brief How many zones of a skill can be out at once: the limit, 1 more with Endless Horde (totems, storms). */
int SupportZoneLimit(const Player &player, SkillSlot skill, int limit);
/**
 * @brief POE2 Mashup: traps, runes and sentries out at once, per skill; placing another takes away the oldest, as in POE2
 * (Curtis 2026-10-29: he spammed runes and killed the Butcher in 9 seconds).
 */
constexpr int MaxPlacedTraps = 3;
/** @brief Endless Horde adds this many to MaxPlacedTraps (Curtis 2026-10-29); to other limits 1. */
constexpr int LegionExtraTraps = 2;
/** @brief How many traps, runes or sentries of a skill can be out at once: MaxPlacedTraps, more with Endless Horde. */
int SupportTrapLimit(const Player &player, SkillSlot skill);
/** @brief Cast speed the skill's supports take away, in percent. */
int SupportCastSlowPercent(const Player &player, SkillSlot skill);
/** @brief Attack animation frames skipped (or added, if negative) by the skill's supports. */
int SupportAttackFrameBonus(const Player &player, SkillSlot skill);
/** @brief Mana multiplier in percent of the life-cost supports (Blood Price, Bartuc's Bloodletting). */
int SupportLifeManaPercent(const Player &player, SkillSlot skill);
/** @brief Pays the life part of a skill's cost; @param manaCost the cost the skill would have without them. */
void PaySupportLifeCost(Player &player, SkillSlot skill, int manaCost);
/** @brief Paid in Blood (the pact of Bartuc's Road): each melee attack costs 1% of maximum life. */
void PayMeleeAttackLife(Player &player);
/** @brief Places a Horadric Totem at the target that casts the spells inside the gem of a Skills panel row; the oldest goes past the limit. */
void AddHoradricTotem(Player &player, size_t row, Point target, int limit);
/** @brief How many Horadric Totems stand. */
int HoradricTotemCount();

/** @brief Minion damage dealt, damage taken and life from the minion skill's supports, in percent. */
int SupportMinionDamagePercent(const Player &owner, const Monster &minion);
int SupportMinionDamageTakenPercent(const Player &owner, const Monster &minion);
int SupportMinionLifePercent(const Player &owner, PersistentSkill skill);
int SupportExtraMinions(const Player &owner, PersistentSkill skill);
void OnSupportedMinionDied(Player &owner, Monster &minion);

/** @brief Seals a skill's Horadric Seals or Stored Volley holds, for its skill bar button. */
struct SealDisplay {
	int seals;
	int maxSeals;
	/** How far towards the next seal, 0 to 100; 100 once all are stored. */
	int progressPercent;
};
std::optional<SealDisplay> SkillSealDisplay(const Player &player, SkillSlot skill);

/** @brief Ricochet: a projectile hit a wall. @return true if it bounced. */
bool OnSupportedMissileHitWall(Missile &missile, Point wall);
/** @brief Lineage supports drop rarely from any monster, more often from their boss. */
void TryDropLineageSupport(const Monster &monster, bool sendmsg);

} // namespace devilution
