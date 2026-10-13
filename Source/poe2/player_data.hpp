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

#include "poe2/flasks.hpp"
#include "tables/spelldat.h"

namespace devilution {

/** @brief Most nodes a passive tree may have. */
constexpr size_t MaxPassiveNodes = 1024;

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
	Last = Spark,
};

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
	Count,
};

/** @brief Spirit gems: persistent buffs and minions that reserve Spirit while socketed. */
enum class PersistentSkill : uint8_t {
	None,
	/** Monsters you kill explode in flames, burning those around them. */
	HeraldOfAsh,
	/** Monsters you kill release lightning that strikes enemies nearby. */
	HeraldOfThunder,
	/** An aura granting resistance to fire, lightning and magic. */
	PurityOfElements,
	/** A golem that fights at your side and comes back when it dies. */
	SummonGolem,
	/** Skeletal warriors that fight at your side and rise again when they fall. */
	SkeletalWarriors,
	Last = SkeletalWarriors,
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

	bool operator==(const SkillSlot &other) const = default;
};

/** @brief Skill gem sockets in the Skills panel (K). The first 6 were the old skill bar's slots. */
constexpr size_t NumSkillSlots = 12;
/** @brief Sockets heroes had before the Skills panel grew to 12 (hero data before version 8). */
constexpr size_t OldNumSkillSlots = 6;

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
	/** Diablo's left click: walks, attacks and picks things up. */
	MoveAndAttack,
	/** Walks, picks things up, opens and talks, but never attacks. */
	Interact,
	/** Diablo's right click: casts the readied spell. */
	ReadiedSpell,
	/** Uses the skill gem in a Skills panel socket. */
	Gem,
	Last = Gem,
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
		return { TrayAction::MoveAndAttack, 0 };
	case TraySlot::RightMouse:
		return { TrayAction::ReadiedSpell, 0 };
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

/** @brief Support gems a spell could have before supports moved into the skill bar (old saves). */
constexpr size_t MaxSupportsPerSpell = 2;
constexpr size_t MaxPoe2Spells = 64;
static_assert(static_cast<size_t>(SpellID::LAST) < MaxPoe2Spells);

/** @brief Most support sockets a skill can have; how many are open depends on the character level. */
constexpr size_t MaxSupportSockets = 5;
using SupportSockets = std::array<SupportGem, MaxSupportSockets>;

/**
 * @brief All POE2 Mashup character state that isn't part of the original hero file.
 *
 * Saved in its own "poe2hero" entry of the hero save, so heroes stay loadable by other builds.
 */
struct Poe2PlayerData {
	/** @brief Support gems socketed in each Skills panel socket's skill. */
	std::array<SupportSockets, NumSkillSlots> slotSupports {};
	/** @brief Charges of the flasks in belt slots 1 to 5. */
	std::array<uint8_t, NumFlasks> flaskCharges {};
	/** @brief Skill gems socketed in the Skills panel. */
	std::array<SkillSlot, NumSkillSlots> skillSlots {};
	/** @brief Level of the gem in each Skills panel socket, 1 to 20. */
	std::array<uint8_t, NumSkillSlots> slotLevels {};
	/** @brief What each skill tray slot does. */
	std::array<TrayBinding, NumTraySlots> tray {};
	/** @brief Game ticks left in the current dodge roll (not saved). */
	uint8_t dodgeTicks = 0;
	/** @brief Game ticks until the next dodge roll is allowed (not saved). */
	uint8_t dodgeCooldown = 0;
	/** @brief Weapon skill queued for the next attack (not saved). */
	WeaponSkill pendingSkill = WeaponSkill::None;
	/** @brief Game ticks before a queued weapon skill is dropped (not saved). */
	uint8_t pendingSkillTicks = 0;
	/** @brief Weapon skill used by the attack in progress (not saved). */
	WeaponSkill activeSkill = WeaponSkill::None;
	/** @brief Skill that hits when the current dash lands, e.g. Leap Slam (not saved). */
	WeaponSkill dashSkill = WeaponSkill::None;
	/** @brief Game ticks the dash skill may take to land before it's dropped; 0 if not dashing (not saved). */
	uint8_t dashSkillTicks = 0;
	/** @brief Game ticks left of Battle Cry's empowered melee hits (not saved). */
	uint8_t warcryTicks = 0;
	/** @brief Game ticks the character stands still after a skill slowed by its supports (not saved). */
	uint8_t recoveryTicks = 0;
	/** @brief Skill whose cast animation is playing; it goes off on castFrame (not saved). */
	WeaponSkill castSkill = WeaponSkill::None;
	int8_t castFrame = 0;
	/** @brief The player_graphic of the cast skill's animation (not saved). */
	uint8_t castGraphic = 0;
	/** @brief Allocated passive tree nodes, by node id; the class start node isn't stored. */
	std::bitset<MaxPassiveNodes> passives;
	/** @brief Animation frames skipped by attacks and casts thanks to passives (not saved; from CalcPlrItemVals). */
	int8_t passiveAttackFrames = 0;
	int8_t passiveCastFrames = 0;
	/** @brief Life regenerated per second from gear (not saved; from CalcPlrItemVals). */
	int lifeRegen = 0;
	/** @brief Increased mana regeneration in percent from gear (not saved; from CalcPlrItemVals). */
	int manaRegenBonus = 0;
	/** @brief Fractions of a raw life and mana point carried to the next tick (not saved). */
	int lifeRegenRemainder = 0;
	int manaRegenRemainder = 0;
	/** @brief Evasion rating from gear (not saved; from CalcPlrItemVals). */
	int evasion = 0;
	/** @brief Maximum energy shield from gear (not saved; from CalcPlrItemVals). */
	int maxEnergyShield = 0;
	/** @brief Current energy shield as a raw Fixed26_6 value (not saved; refills when loaded). */
	int32_t energyShield = 0;
	/** @brief Game ticks since the last hit, up to the recharge delay (not saved). */
	int energyShieldRechargeTicks = 0;
	int energyShieldRemainder = 0;
	/** @brief Game ticks before Summon Golem brings the golem back (not saved). */
	uint8_t golemCooldown = 0;
	/** @brief Game ticks before Skeletal Warriors raises the next skeleton (not saved). */
	uint8_t skeletonCooldown = 0;
	/** @brief The map (POE2 endgame) the player opened: its dungeon level and tier; tier 0 when there's none. */
	uint8_t mapLevel = 0;
	uint8_t mapTier = 0;
	/** @brief Raw life and mana still to recover from the flasks being drunk, and the game ticks left (not saved). */
	int32_t lifeFlaskRaw = 0;
	uint8_t lifeFlaskTicks = 0;
	int32_t manaFlaskRaw = 0;
	uint8_t manaFlaskTicks = 0;
	/** @brief Game ticks left of Guard from an Iron Charm (not saved). */
	uint8_t guardTicks = 0;
	/** @brief Game ticks before another charm can go off (not saved). */
	uint8_t charmCooldown = 0;
	/** @brief Game ticks left of the fire, lightning and magic resistance from Ruby, Topaz and Amethyst charms (not saved). */
	std::array<uint8_t, 3> resistCharmTicks {};
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
		flaskCharges.fill(MaxFlaskCharges);
		lifeFlaskRaw = 0;
		lifeFlaskTicks = 0;
		manaFlaskRaw = 0;
		manaFlaskTicks = 0;
		skillSlots.fill({});
		slotLevels.fill(1);
		for (size_t slot = 0; slot < NumTraySlots; slot++)
			tray[slot] = DefaultTrayBinding(static_cast<TraySlot>(slot));
		dodgeTicks = 0;
		dodgeCooldown = 0;
		pendingSkill = WeaponSkill::None;
		pendingSkillTicks = 0;
		activeSkill = WeaponSkill::None;
		dashSkill = WeaponSkill::None;
		dashSkillTicks = 0;
		warcryTicks = 0;
		recoveryTicks = 0;
		castSkill = WeaponSkill::None;
		castFrame = 0;
		passives.reset();
		passiveAttackFrames = 0;
		passiveCastFrames = 0;
		lifeRegen = 0;
		manaRegenBonus = 0;
		lifeRegenRemainder = 0;
		manaRegenRemainder = 0;
		evasion = 0;
		maxEnergyShield = 0;
		energyShield = 0;
		energyShieldRechargeTicks = 0;
		energyShieldRemainder = 0;
		golemCooldown = 0;
		skeletonCooldown = 0;
		guardTicks = 0;
		mapLevel = 0;
		mapTier = 0;
		charmCooldown = 0;
		resistCharmTicks = {};
		stunImmuneTicks = 0;
		oxFlaskTicks = 0;
		springsFlaskTicks = 0;
		resistancePenalty = -1;
	}

	[[nodiscard]] bool isDodging() const { return dodgeTicks > 0; }

	/** @brief Advances the dodge roll and weapon skill timers by one game tick. */
	void tick()
	{
		if (dodgeTicks > 0)
			dodgeTicks--;
		if (dodgeCooldown > 0)
			dodgeCooldown--;
		if (pendingSkillTicks > 0 && --pendingSkillTicks == 0)
			pendingSkill = WeaponSkill::None;
		if (warcryTicks > 0)
			warcryTicks--;
		if (recoveryTicks > 0)
			recoveryTicks--;
	}

	/** @brief The supports of the Skills panel socket holding the skill; none if it isn't socketed. */
	[[nodiscard]] const SupportSockets &supportsFor(SkillSlot skill) const;
	[[nodiscard]] const SupportSockets &supportsFor(SpellID spell) const { return supportsFor(SkillSlot::ForSpell(spell)); }
	[[nodiscard]] bool hasSupport(SkillSlot skill, SupportGem gem) const;
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

	/** @brief Level of the socketed gem for the skill; 0 if it isn't socketed. */
	[[nodiscard]] int skillLevel(SkillSlot skill) const
	{
		for (size_t socket = 0; socket < skillSlots.size(); socket++) {
			if (!skill.isEmpty() && skillSlots[socket] == skill)
				return gemLevel(socket);
		}
		return 0;
	}
	[[nodiscard]] int skillLevel(SpellID spell) const { return skillLevel(SkillSlot::ForSpell(spell)); }
	[[nodiscard]] int skillLevel(WeaponSkill skill) const { return skillLevel(SkillSlot::ForAttack(skill)); }
	[[nodiscard]] int skillLevel(PersistentSkill skill) const { return skillLevel(SkillSlot::ForPersistent(skill)); }
};

} // namespace devilution
