#include "poe2/menu_secret.hpp"

#if !defined(USE_SDL1) && !defined(USE_SDL3)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

#include "engine/sound.h"
#include "engine/sound_defs.hpp"
#include "options.h"
#include "poe2/live_fire.hpp"
#include "poe2/live_fire_sdl.hpp"
#include "poe2/secrets.hpp"
#include "utils/log.hpp"

namespace devilution {

namespace menu_secret {

namespace {

/** @brief The logo's zones are D I A B L O; lit from the outside in that is D, O, I, L, A, B. */
constexpr std::array<size_t, 6> Order = { 0, 5, 1, 4, 2, 3 };

/** @brief How high a rightly lit letter keeps burning, and how high they all roar after the last. */
constexpr float LitBoost = 1.45F;
constexpr float RoarBoost = 2.3F;
/** @brief After the roar starts, sweeps blow letters out from this long on, until this long. */
constexpr float SweepFrom = 0.45F;
constexpr float SweepUntil = 3.6F;
/** @brief How fast the pointer must cross a letter to blow it out, in the fire's grid pixels a second (the whole logo
 * is 1100 of them across: about a third of a second for a sweep across all six). */
constexpr float BlowSpeed = 2400.F;
/** @brief When all six are out: the far-off moo, then the fire creeping back over about five seconds. */
constexpr float MooAfter = 1.35F;
constexpr float CreepAfter = 3.2F;
constexpr float CreepRate = 0.2F;

enum class Stage : uint8_t {
	/** @brief Clicking letters (lighting them, once the rumour is heard). */
	Lighting,
	/** @brief All six roaring: sweep them out. */
	Roaring,
	/** @brief All six blown out: silence, a moo, then the fire comes back. */
	Out,
};

struct State {
	fire::FireLayer *layer = nullptr;
	std::vector<fire::FireZone> zones;
	int gridWidth = 0;
	int gridHeight = 0;
	Stage stage = Stage::Lighting;
	size_t lit = 0;
	float clock = 0;
	float stageAt = 0;
	std::array<bool, 6> blown {};
	bool mooed = false;
	bool creeping = false;
	bool clicked = false;
	bool hadPointer = false;
	float lastX = 0;
	float lastY = 0;
};

State Now;

#ifndef NOSOUND
std::unique_ptr<TSnd> RoarSound;
std::unique_ptr<TSnd> MooSound;
bool SoundsTried = false;

void Play(std::unique_ptr<TSnd> &sound, float gain)
{
	if (!SoundsTried) {
		SoundsTried = true;
		if (gbSndInited) {
			auto roar = SoundFileLoadWithStatus("sfx\\misc\\flamwave.wav");
			if (roar.has_value())
				RoarSound = std::move(roar).value();
			auto moo = SoundFileLoadWithStatus("sfx\\towners\\cow1.wav");
			if (moo.has_value())
				MooSound = std::move(moo).value();
		}
	}
	if (sound == nullptr || !gbSndInited || !gbSoundOn)
		return;
	const int log = gain <= 0.0001F ? ATTENUATION_MIN : std::max(ATTENUATION_MIN, static_cast<int>(std::lround(2000.F * std::log10(gain))));
	sound->DSB.PlayWithVolumeAndPan(log, *GetOptions().Audio.soundVolume, 0);
}
#endif

void PlayRoar()
{
#ifndef NOSOUND
	Play(RoarSound, 1.F);
#endif
}

void PlayMoo()
{
#ifndef NOSOUND
	// Far off: quiet.
	Play(MooSound, 0.35F);
#endif
}

/** @brief Sends every letter to `boost` at `rate` a second. */
void AllTo(fire::LiveFire &fire, float boost, float rate)
{
	for (size_t z = 0; z < Now.zones.size(); z++)
		fire.SetZoneBoost(z, boost, rate);
}

/** @brief The letter under grid point (x, y), top row first; -1 for none. */
int LetterAt(float x, float y)
{
	// The letters stand in the lower part of the logo; the flames rise above them.
	if (y < static_cast<float>(Now.gridHeight) * 0.3F || y > static_cast<float>(Now.gridHeight))
		return -1;
	for (size_t z = 0; z < Now.zones.size(); z++) {
		if (std::abs(x - Now.zones[z].centre) < Now.zones[z].halfWidth * 0.85F)
			return static_cast<int>(z);
	}
	return -1;
}

void Click(fire::LiveFire &fire, int letter)
{
	fire.Flare(static_cast<size_t>(letter));
	if (Now.stage != Stage::Lighting || !secrets::Has(secrets::Found::HeardRumour))
		return;
	if (static_cast<size_t>(letter) == Order[Now.lit]) {
		Now.lit++;
		fire.SetZoneBoost(static_cast<size_t>(letter), LitBoost, 2.F);
		if (Now.lit == Order.size()) {
			// They all roar up together.
			Now.stage = Stage::Roaring;
			Now.stageAt = Now.clock;
			Now.blown = {};
			AllTo(fire, RoarBoost, 5.F);
			for (size_t z = 0; z < Now.zones.size(); z++)
				fire.FlareAgo(z, 0.25F - static_cast<float>(z) * 0.04F, static_cast<float>(z) * 0.04F);
			PlayRoar();
		}
		return;
	}
	// A wrong letter: they all settle back down.
	Now.lit = 0;
	AllTo(fire, 1.F, 0.8F);
}

/** @brief The pointer went from (x0, y0) to (x1, y1) in `seconds`, grid pixels: fast enough, it blows out what it crosses. */
void Sweep(fire::LiveFire &fire, float x0, float y0, float x1, float y1, float seconds)
{
	if (seconds <= 0 || Now.clock - Now.stageAt < SweepFrom)
		return;
	const float speed = std::hypot(x1 - x0, y1 - y0) / seconds;
	if (speed < BlowSpeed)
		return;
	// Anywhere over the logo's height counts (and a little above and below it).
	const float h = static_cast<float>(Now.gridHeight);
	if (std::max(y0, y1) < -0.15F * h || std::min(y0, y1) > 1.15F * h)
		return;
	const float from = std::min(x0, x1);
	const float to = std::max(x0, x1);
	for (size_t z = 0; z < Now.zones.size() && z < Now.blown.size(); z++) {
		if (Now.blown[z] || Now.zones[z].centre < from || Now.zones[z].centre > to)
			continue;
		Now.blown[z] = true;
		fire.SetZoneBoost(z, 0.F, 7.F);
	}
	if (std::all_of(Now.blown.begin(), Now.blown.end(), [](bool b) { return b; })) {
		Now.stage = Stage::Out;
		Now.stageAt = Now.clock;
		Now.mooed = false;
		Now.creeping = false;
		secrets::Set(secrets::Found::LitName);
		LogVerbose("Secret: the name was lit and blown out");
	}
}

} // namespace

void NoteClick()
{
	Now.clicked = true;
}

void Update(fire::FireLayer &layer, const SDL_FRect &grid, float x, float y, float seconds)
{
	const bool clicked = Now.clicked;
	Now.clicked = false;
	if (Now.layer != &layer) {
		// A new fire (or the first): learn its letters (fixed once it is made).
		Forget();
		Now.layer = &layer;
		Now.zones = layer.Fire().Zones();
		Now.gridWidth = layer.GridWidth();
		Now.gridHeight = layer.GridHeight();
	}
	if (Now.zones.size() < Order.size() || !(grid.w > 0) || !(grid.h > 0))
		return;
	Now.clock += std::max(0.F, seconds);
	const float gx = (x - grid.x) / grid.w * static_cast<float>(Now.gridWidth);
	const float gy = (y - grid.y) / grid.h * static_cast<float>(Now.gridHeight);

	if (clicked) {
		const int letter = LetterAt(gx, gy);
		if (letter >= 0)
			Click(layer.Fire(), letter);
	}
	switch (Now.stage) {
	case Stage::Lighting:
		break;
	case Stage::Roaring:
		if (Now.hadPointer)
			Sweep(layer.Fire(), Now.lastX, Now.lastY, gx, gy, seconds);
		if (Now.stage == Stage::Roaring && Now.clock - Now.stageAt > SweepUntil) {
			// Too slow: the roar dies down and it starts again.
			Now.stage = Stage::Lighting;
			Now.lit = 0;
			AllTo(layer.Fire(), 1.F, 0.9F);
		}
		break;
	case Stage::Out:
		if (!Now.mooed && Now.clock - Now.stageAt > MooAfter) {
			Now.mooed = true;
			PlayMoo();
		}
		if (!Now.creeping && Now.clock - Now.stageAt > CreepAfter) {
			Now.creeping = true;
			AllTo(layer.Fire(), 1.F, CreepRate);
			Now.stage = Stage::Lighting;
			Now.lit = 0;
		}
		break;
	}
	Now.hadPointer = true;
	Now.lastX = gx;
	Now.lastY = gy;
}

void Leave()
{
	if (Now.layer == nullptr)
		return;
	// Whatever the letters were doing, the logo elsewhere burns as it always does.
	fire::LiveFire &fire = Now.layer->Fire();
	for (size_t z = 0; z < fire.Zones().size(); z++)
		fire.SetZoneBoost(z, 1.F);
	Now = State {};
}

void Forget()
{
	Now = State {};
}

} // namespace menu_secret

} // namespace devilution

#endif
