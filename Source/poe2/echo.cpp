/**
 * @file poe2/echo.cpp
 *
 * Blending in: room echo on the game's own sounds. See echo.hpp.
 */
#include "poe2/echo.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>

#include <SDL.h>

#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "levels/tile_properties.hpp"
#include "player.h"
#include "poe2/atmosphere.hpp"

namespace devilution::echo {

namespace {

/** Line lengths for sound at 44100 samples a second: the long-used "Freeverb" set, which rings evenly with no one note standing out. */
constexpr std::array<int, 6> CombLengths = { 1116, 1188, 1277, 1356, 1422, 1491 };
constexpr std::array<int, 3> PassLengths = { 556, 441, 341 };
/** The second ear's lines are this much longer, so the ring is wide instead of sitting in the middle. */
constexpr int EarSpread = 23;
/** Brings the ring of a middling room (feedback 0.8) to about the loudness of the sound that made it, before the send. */
constexpr float RingGain = 0.096F;

std::atomic<float> SendNow { 0.F };
std::atomic<float> FeedbackNow { 0.8F };
float HeldOpen = -1.F;
float Open = 0.F;
float OpenTarget = 0.F;
uint32_t MeasuredAt = 0;
uint32_t UpdatedAt = 0;

} // namespace

void Room::Setup(int rate)
{
	const double scale = std::max(rate, 8000) / 44100.0;
	for (int ear = 0; ear < 2; ear++) {
		for (int i = 0; i < Combs; i++) {
			combs_[ear][i].samples.assign(std::max(8, static_cast<int>((CombLengths[i] + ear * EarSpread) * scale)), 0.F);
			combs_[ear][i].at = 0;
			combs_[ear][i].kept = 0;
		}
		for (int i = 0; i < Passes; i++) {
			passes_[ear][i].samples.assign(std::max(4, static_cast<int>((PassLengths[i] + ear * EarSpread) * scale)), 0.F);
			passes_[ear][i].at = 0;
		}
	}
	quiet_ = QuietFrames;
}

void Room::Shape(float feedback, float damp)
{
	feedback_ = std::clamp(feedback, 0.F, 0.93F);
	damp_ = std::clamp(damp, 0.F, 0.95F);
}

void Room::Process(const float *in, float *out, int frames, int channels)
{
	if (combs_[0][0].samples.empty()) {
		std::fill(out, out + frames * channels, 0.F);
		return;
	}
	const int ears = channels >= 2 ? 2 : 1;
	for (int frame = 0; frame < frames; frame++) {
		const float sound = in[frame] * RingGain;
		float loudest = std::abs(in[frame]);
		for (int ear = 0; ear < ears; ear++) {
			float ring = 0;
			for (Line &comb : combs_[ear]) {
				const float back = comb.samples[comb.at];
				comb.kept = back * (1.F - damp_) + comb.kept * damp_;
				if (std::abs(comb.kept) < 1e-20F)
					comb.kept = 0;
				comb.samples[comb.at] = sound + comb.kept * feedback_;
				if (++comb.at >= static_cast<int>(comb.samples.size()))
					comb.at = 0;
				ring += back;
			}
			for (Line &pass : passes_[ear]) {
				const float back = pass.samples[pass.at];
				pass.samples[pass.at] = ring + back * 0.5F;
				if (++pass.at >= static_cast<int>(pass.samples.size()))
					pass.at = 0;
				ring = back - ring;
			}
			out[frame * channels + ear] = ring;
			loudest = std::max(loudest, std::abs(ring));
		}
		for (int extra = ears; extra < channels; extra++)
			out[frame * channels + extra] = out[frame * channels];
		quiet_ = loudest > 2e-5F ? 0 : std::min(quiet_ + 1, QuietFrames);
	}
}

float OpenSpace()
{
	if (MyPlayer == nullptr)
		return 0;
	// Looks out sixteen ways from the hero until a wall: the further the walls, the bigger the room.
	constexpr int Ways = 16;
	constexpr int Far = 14;
	const Point from = MyPlayer->position.tile;
	float total = 0;
	for (int way = 0; way < Ways; way++) {
		const float angle = way * 6.2831853F / Ways;
		const float stepX = std::cos(angle);
		const float stepY = std::sin(angle);
		int reach = Far;
		for (int step = 1; step <= Far; step++) {
			const Point at { from.x + static_cast<int>(std::lround(stepX * step)), from.y + static_cast<int>(std::lround(stepY * step)) };
			if (!InDungeonBounds(at) || TileHasAny(at, TileProperties::Solid)) {
				reach = step - 1;
				break;
			}
		}
		total += reach;
	}
	return std::clamp((total / Ways - 2.F) / 6.F, 0.F, 1.F);
}

void Update()
{
	const uint32_t now = SDL_GetTicks();
	const float seconds = std::min((now - UpdatedAt) / 1000.F, 0.25F);
	UpdatedAt = now;
	const float amount = std::clamp(atmosphere::Setting(atmosphere::Knob::Echo), 0.F, 3.F) * std::clamp(atmosphere::Value("echo", 1.F), 0.F, 3.F);
	if (amount <= 0 || MyPlayer == nullptr || leveltype == DTYPE_TOWN) {
		// Under the open sky nothing rings.
		SendNow.store(0.F, std::memory_order_relaxed);
		Open = OpenTarget = 0;
		MeasuredAt = 0;
		return;
	}
	if (HeldOpen >= 0) {
		OpenTarget = HeldOpen;
	} else if (MeasuredAt == 0 || now - MeasuredAt > 400) {
		const bool first = MeasuredAt == 0;
		MeasuredAt = now == 0 ? 1 : now;
		OpenTarget = OpenSpace();
		if (first)
			Open = OpenTarget;
	}
	// The ring follows the hero from room to room over a second or so, never in a jump.
	const float step = seconds * 0.8F;
	Open += std::clamp(OpenTarget - Open, -step, step);
	FeedbackNow.store(0.70F + 0.17F * Open, std::memory_order_relaxed);
	SendNow.store(std::min(amount * (0.14F + 0.14F * Open), 0.9F), std::memory_order_relaxed);
}

float Send()
{
	return SendNow.load(std::memory_order_relaxed);
}

float Feedback()
{
	return FeedbackNow.load(std::memory_order_relaxed);
}

void Hold(float openness)
{
	HeldOpen = openness < 0 ? -1.F : std::min(openness, 1.F);
}

} // namespace devilution::echo
