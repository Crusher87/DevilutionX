#include "poe2/moments.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_timer.h>
#else
#include <SDL.h>
#endif

#include "levels/gendung.h"
#include "monster.h"
#include "player.h"
#include "poe2/atmosphere.hpp"

namespace devilution::moments {

namespace {

float SmoothStep(float edge0, float edge1, float x)
{
	const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.F, 1.F);
	return t * t * (3 - 2 * t);
}

float Mean(const std::array<float, 3> &colour)
{
	return (colour[0] + colour[1] + colour[2]) / 3;
}

// Idea 49: going down.

enum class Descent : uint8_t {
	None,
	/** @brief The level is fading out: the dark closes in on the hero. */
	Closing,
	/** @brief The new level is up: the dark opens out from the hero. */
	Opening,
};
Descent DescentNow = Descent::None;
/** @brief How much of the screen's fade is left (1 not faded, 0 black), as WorldTint last saw it. */
float FadeNow = 1;
/** @brief The screen's tint when nothing fades (the brightness setting's), to measure a fade against. */
std::array<float, 3> PlainTint { 1, 1, 1 };
/** @brief How far the dark has opened on the new level, 0 to 1, and when it was last moved on. */
float Opened = 0;
uint32_t OpenedAt = 0;

/** @brief The world dims as this power of the fade while the dark closes in, so the closing pool is what is seen. */
constexpr float WorldFadePower = 0.4F;
/** @brief The slowest frame the opening counts in full, so a new level's slow first frames don't use it up unseen. */
constexpr uint32_t LongestFrameMs = 34;

// Idea 50: after the fight.

/** @brief How far from the hero a monster counts as part of the fight, in tiles. */
constexpr int FightReach = 12;
/** @brief A fight shorter than this leaves no quiet behind it. */
constexpr float ShortestFightMs = 600;
/** @brief No awake monster for this long ends the fight. */
constexpr float CalmMs = 250;
constexpr float ScanEveryMs = 120;

bool Fighting = false;
bool FightKilled = false;
float FightMs = 0;
float CalmFor = 0;
float SinceScan = 0;
/** @brief How much the fight has driven the mist off: 0 none, 1 all it does. */
float FightAir = 0;
float QuietLength = 0;
float QuietLeft = 0;
bool QuietStarted = false;

// Idea 53: death.

float DeathFor = 0;

// Idea 47: doors.

std::vector<std::array<int, 2>> OpenedDoors;

float DescentStrength()
{
	return std::min(atmosphere::Strength(atmosphere::Knob::Descent), 1.F);
}

float QuietStrength()
{
	if (leveltype == DTYPE_TOWN)
		return 0;
	return std::min(atmosphere::Strength(atmosphere::Knob::Quiet), 2.F);
}

/** @brief How far the hero's death has gone, 0 to 1, times the switch. */
float DeathAmount()
{
	if (DeathFor <= 0)
		return 0;
	const float strength = std::min(atmosphere::Strength(atmosphere::Knob::Death), 1.F);
	if (strength <= 0)
		return 0;
	return strength * SmoothStep(0, std::max(atmosphere::Value("death_ms", 1200), 1.F), DeathFor);
}

void ScanFight(float ms)
{
	int awake = 0;
	bool dying = false;
	if (MyPlayer != nullptr && leveltype != DTYPE_TOWN) {
		const Point hero = MyPlayer->position.tile;
		for (size_t i = 0; i < ActiveMonsterCount; i++) {
			const Monster &monster = Monsters[ActiveMonsters[i]];
			if (monster.isPlayerMinion())
				continue;
			const Point at = monster.position.tile;
			if (std::abs(at.x - hero.x) > FightReach || std::abs(at.y - hero.y) > FightReach)
				continue;
			if (monster.mode == MonsterMode::Death || monster.hasNoLife()) {
				dying = true;
				continue;
			}
			if (monster.activeForTicks > 0)
				awake++;
		}
	}
	if (awake > 0) {
		Fighting = true;
		FightMs += ms;
		FightKilled = FightKilled || dying;
		CalmFor = 0;
		// A new fight cuts the quiet short.
		QuietLeft = std::min(QuietLeft, 200.F);
		return;
	}
	if (!Fighting)
		return;
	FightKilled = FightKilled || dying;
	CalmFor += ms;
	if (CalmFor < CalmMs)
		return;
	if (FightKilled && FightMs >= ShortestFightMs && QuietStrength() > 0) {
		QuietLength = std::max(atmosphere::Value("quiet_ms", 1500), 300.F);
		QuietLeft = QuietLength;
		QuietStarted = true;
	}
	Fighting = false;
	FightKilled = false;
	FightMs = 0;
	CalmFor = 0;
}

/** @brief The quiet's rise and fall: up in a fifth of a second, down over its last half second. */
float QuietNow()
{
	if (QuietLeft <= 0)
		return 0;
	return std::min({ (QuietLength - QuietLeft) / 200.F, QuietLeft / 500.F, 1.F });
}

} // namespace

void LeavingLevel(bool down)
{
	DescentNow = down && DescentStrength() > 0 ? Descent::Closing : Descent::None;
	FadeNow = 1;
}

void ArrivingOnLevel()
{
	const bool closed = DescentNow == Descent::Closing;
	Reset();
	if (!closed || DescentStrength() <= 0)
		return;
	DescentNow = Descent::Opening;
	Opened = 0;
	OpenedAt = SDL_GetTicks();
}

void Advance(float ms)
{
	// The hero's death.
	const bool dead = MyPlayer != nullptr && (MyPlayerIsDead || MyPlayer->_pmode == PM_DEATH);
	DeathFor = dead ? std::min(DeathFor + ms, 60000.F) : 0.F;

	// The fight round the hero, looked at a few times a second.
	SinceScan += ms;
	if (SinceScan >= ScanEveryMs) {
		ScanFight(SinceScan);
		SinceScan = 0;
	}
	QuietLeft = std::max(QuietLeft - ms, 0.F);
	if (Fighting)
		FightAir = std::min(FightAir + ms / 2000.F, 1.F);
	else
		FightAir = std::max(FightAir - ms / std::max(QuietLength, 1000.F), 0.F);
}

std::array<float, 3> WorldTint(const std::array<float, 3> &fade)
{
	if (DescentNow == Descent::None) {
		PlainTint = fade;
		FadeNow = 1;
		return fade;
	}
	const float plain = Mean(PlainTint);
	FadeNow = plain > 0 ? std::clamp(Mean(fade) / plain, 0.F, 1.F) : 1.F;
	// Slower than the fade: the same at both ends, lighter between.
	const float lift = std::pow(std::max(FadeNow, 0.02F), WorldFadePower - 1);
	std::array<float, 3> tint;
	for (int c = 0; c < 3; c++)
		tint[c] = std::min(fade[c] * lift, std::max(PlainTint[c], fade[c]));
	return tint;
}

float Iris(float &darkness)
{
	darkness = 1;
	const float death = DeathAmount();
	if (death > 0) {
		darkness = std::clamp(atmosphere::Value("death_dark", 0.85F), 0.F, 1.F) * death;
		return 1 - death;
	}
	const float strength = DescentStrength();
	if (strength <= 0)
		DescentNow = Descent::None;
	switch (DescentNow) {
	case Descent::None:
		return 1;
	case Descent::Closing:
		darkness = strength;
		// The pool is down to the hero's own light by the middle of the fade and gone by its end.
		return FadeNow * FadeNow;
	case Descent::Opening: {
		const uint32_t now = SDL_GetTicks();
		const float length = std::max(atmosphere::Value("descent_open_ms", 480), 50.F);
		Opened += static_cast<float>(std::min(now - OpenedAt, LongestFrameMs)) / length;
		OpenedAt = now;
		if (Opened >= 1) {
			DescentNow = Descent::None;
			return 1;
		}
		darkness = strength;
		// Eased at both ends: the hero's surroundings are back within a few frames, the far edges last.
		return SmoothStep(0, 1, Opened);
	}
	}
	return 1;
}

float IrisFloor()
{
	if (DeathAmount() > 0)
		return std::max(atmosphere::Value("death_pool", 3), 0.5F);
	return 0.6F;
}

float RedDrain()
{
	return DeathAmount() * std::clamp(atmosphere::Value("death_red", 1), 0.F, 1.F);
}

float HeroLightKept()
{
	return 1 - (1 - std::clamp(atmosphere::Value("death_light", 0.25F), 0.F, 1.F)) * DeathAmount();
}

bool FlamesDimmed()
{
	return DeathAmount() > 0;
}

float FlameKept(float x, float y)
{
	const float death = DeathAmount();
	if (death <= 0 || MyPlayer == nullptr)
		return 1;
	const Point hero = MyPlayer->position.tile;
	const float distance = std::hypot(x - static_cast<float>(hero.x), y - static_cast<float>(hero.y));
	const float near = 1 - SmoothStep(5, 9, distance);
	return 1 - std::clamp(atmosphere::Value("death_flames", 0.5F), 0.F, 1.F) * death * near;
}

float FlickerKept()
{
	return 1 - std::clamp(atmosphere::Value("quiet_flicker", 0.5F) * QuietStrength(), 0.F, 1.F) * QuietNow();
}

float MistKept()
{
	return 1 - std::clamp(atmosphere::Value("quiet_mist", 0.4F) * QuietStrength(), 0.F, 1.F) * FightAir;
}

bool TakeQuietStarted()
{
	const bool started = QuietStarted;
	QuietStarted = false;
	return started;
}

bool Quiet()
{
	return QuietLeft > 0;
}

float DoorRevealMs()
{
	const float strength = atmosphere::Strength(atmosphere::Knob::Doors);
	if (strength <= 0 || leveltype == DTYPE_TOWN)
		return 0;
	return atmosphere::Value("door_reveal_ms", 330) * std::min(strength, 3.F);
}

void DoorOpened(int x, int y)
{
	if (OpenedDoors.size() < 4)
		OpenedDoors.push_back({ x, y });
}

bool TakeDoorOpened(int &x, int &y)
{
	if (OpenedDoors.empty())
		return false;
	x = OpenedDoors.back()[0];
	y = OpenedDoors.back()[1];
	OpenedDoors.pop_back();
	return true;
}

void Reset()
{
	DescentNow = Descent::None;
	FadeNow = 1;
	Opened = 0;
	Fighting = false;
	FightKilled = false;
	FightMs = 0;
	CalmFor = 0;
	SinceScan = 0;
	FightAir = 0;
	QuietLeft = 0;
	QuietStarted = false;
	DeathFor = 0;
	OpenedDoors.clear();
}

} // namespace devilution::moments
