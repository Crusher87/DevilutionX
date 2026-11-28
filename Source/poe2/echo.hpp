/**
 * @file poe2/echo.hpp
 *
 * Blending in (feel("echo")): the game's own sounds ring a little in the space round the hero, more and for longer the
 * bigger that space is. No new sounds: what rings is whatever the game just played. Sound only; it never touches the
 * game itself.
 */
#pragma once

#include <array>
#include <vector>

namespace devilution::echo {

/**
 * @brief The ringing of a stone room: takes one stream of sound in and gives back its soft tail, a little different in
 * each ear. Plain arithmetic with no ties to the game or the sound card, so it can be tried on its own.
 */
class Room {
public:
	/** @brief Sets the room up for sound at this many samples a second, silent. */
	void Setup(int rate);
	/** @brief How long the room rings (0.5 short, 0.9 long) and how much it dulls the ring each time round (0..1). */
	void Shape(float feedback, float damp);
	/**
	 * @brief Works the next stretch: `in` is one sample a frame, `out` gets `channels` (1 or 2) samples a frame, the
	 * ring alone with none of the sound that went in.
	 */
	void Process(const float *in, float *out, int frames, int channels);
	/** @brief Whether the ring has died away to nothing (so nothing needs working out until a new sound comes). */
	[[nodiscard]] bool Silent() const { return quiet_ >= QuietFrames; }

private:
	static constexpr int Combs = 6;
	static constexpr int Passes = 3;
	static constexpr int QuietFrames = 4096;
	struct Line {
		std::vector<float> samples;
		int at = 0;
		float kept = 0;
	};
	std::array<std::array<Line, Combs>, 2> combs_;
	std::array<std::array<Line, Passes>, 2> passes_;
	float feedback_ = 0.8F;
	float damp_ = 0.5F;
	int quiet_ = QuietFrames;
};

/** @brief How open the space round the hero is, 0 (a passage) to 1 (a great hall or cavern), from the level's walls. */
float OpenSpace();

/** @brief Once a frame, from the game: measures the space now and then and sets how the sounds should ring. */
void Update();

/** @brief How much of a sound played now goes into the ring (0 none: town, or switched off). Safe from any thread. */
float Send();

/** @brief How long the ring lasts now (the room's feedback). Safe from any thread. */
float Feedback();

/** @brief For trying it out: holds the space at this openness (0..1); below 0 goes back to measuring. */
void Hold(float openness);

} // namespace devilution::echo
