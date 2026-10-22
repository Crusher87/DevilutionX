/**
 * @file poe2/skill_lab.hpp
 *
 * POE2 Mashup Skill Lab: a test room opened from the main menu. A stand-in hero casts any skill at monsters that
 * are held in place (or at the bare floor), with loop, slow motion and frame stepping. It runs the game itself,
 * so every effect, animation and sound is the real one.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "poe2/gemcutting.hpp"
#include "poe2/player_data.hpp"
#include "tables/playerdat.hpp"

namespace devilution {

struct GameData;
struct Monster;

/** @brief A skill the lab can show. */
struct LabSkill {
	SkillSlot skill;
	std::string name;
	/** The gemcutting tab it's listed in. */
	GemCategory category;
	/** The gem level an uncut gem needs to cut it (1 for the weapon attack). */
	int gemLevel;
	/** 1, 3, 5 ... 13, like the gemcutting rows. */
	int tier;
};

/** @brief Floors the arena can use: the four Diablo dungeons and Hellfire's two. */
enum class LabFloor : uint8_t {
	Cathedral,
	Catacombs,
	Caves,
	Hell,
	Nest,
	Crypt,
	Count,
};

enum class LabTargets : uint8_t {
	Monsters,
	Floor,
};

/** @brief Whether the game running now is the Skill Lab. */
bool SkillLabActive();

/** @brief Called by the main menu: the next single player game is the lab. */
void SkillLabBegin();
/** @brief Called when the lab game has ended and the main menu is back. */
void SkillLabEnd();

/**
 * @brief Stands in for the hero selection screen in the lab: makes the stand-in hero in its own save slot,
 * which the hero list never shows.
 * @return false when the lab is closing, which goes back to the main menu.
 */
bool SkillLabPrepareHero(GameData *gameData);
/** @brief After a new game's levels and quests are set up: no quests, and the game starts on the lab's floor. */
void SkillLabPrepareNewGame();
/** @brief After a level has loaded: clears it and sets up the arena. */
void SkillLabLevelLoaded();

/** @brief Whether this game tick is held back (paused, and no single step asked for). */
bool SkillLabHoldsTick();
/** @brief Runs once per game tick, after the monsters and missiles. */
void SkillLabTick();
/** @brief Puts the cursor back on the lab's target while a lab cast is in flight. */
void SkillLabAfterCursorMove();
/** @brief How many milliseconds the effects advance for this much real time: slowed, held while paused. */
float SkillLabEffectMs(float elapsedMs);
/** @brief Whether the time between ticks should stay as it is (paused). */
bool SkillLabFreezesAnimation();

/** @brief Whether the monster is one of the lab's targets: it stands still, never fights back and never dies. */
bool IsLabDummy(const Monster &monster);

// The panel (panels/skill_lab_panel.cpp) reads and changes these.

const std::vector<LabSkill> &LabSkills();
/** @brief Index into LabSkills of the skill on show, or -1. */
int LabSelectedSkill();
void LabSelectSkill(int index);
void LabCast();
bool LabLooping();
void LabSetLooping(bool loop);
/** @brief 1, 2 or 4: how many times slower than normal. */
int LabSlowdown();
void LabSetSlowdown(int slowdown);
bool LabPaused();
void LabSetPaused(bool paused);
void LabStep();
LabTargets LabTargetMode();
void LabSetTargetMode(LabTargets targets);
int LabTargetCount();
void LabSetTargetCount(int count);
/** @brief The name of the monster the targets are, or empty. */
std::string LabMonsterName();
void LabCycleMonster(int delta);
LabFloor LabCurrentFloor();
void LabSetFloor(LabFloor floor);
std::string_view LabFloorName(LabFloor floor);
bool LabFloorAvailable(LabFloor floor);
HeroClass LabHeroClass();
/** @brief Whether the class follows the skill rather than the user's choice. */
bool LabHeroAuto();
void LabCycleHeroClass(int delta);
/** @brief The weapon the hero holds, e.g. "Mace + Shield". */
std::string LabWeaponName();
bool LabWeaponAuto();
void LabCycleWeapon(int delta);
int LabGemLevel();
void LabSetGemLevel(int level);
void LabExit();
/** @brief Game ticks since the latest cast began, or -1 before the first one. */
int LabTicksSinceCast();

} // namespace devilution
