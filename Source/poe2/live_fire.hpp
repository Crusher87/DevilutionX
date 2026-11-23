#pragma once

/**
 * @file poe2/live_fire.hpp
 *
 * POE2 Mashup: a live fire, worked out fresh every frame on the CPU (the SDL 2 renderer has no shaders). Heat is fed in
 * where a feed map says, carried up by swirling air made of noise that changes all the time, torn apart and cooled, and
 * coloured through a ramp from dark red to near white. Sparks fly off it and a coarse wind grid (the mouse) pushes it.
 *
 * Nothing in it knows about the logo: a fire is its maps (where heat goes in, how high above its source each point
 * is, where sparks start), its zones (parts that breathe and flare up on their own, a logo's letters) and its
 * settings. The menus' logo is the first (hd_ui.cpp); torches and braziers can be others, with maps of their own.
 *
 * Units: grid pixels and seconds, y up (row 0 of every grid is the bottom one). Pictures in and out are top row first.
 * Cosmetic only: nothing in it touches the game's state or its random numbers.
 */

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace devilution {

namespace fire {

/** @brief The knobs of a fire, each 1 for as approved (red: 0 yellow, 1 hell-red). */
struct FireSettings {
	float height = 1.F;
	float amount = 1.F;
	float wild = 1.F;
	float red = 0.5F;
	float wind = 0.25F;
	float sparks = 1.F;
	float flares = 1.F;
};

/** @brief A part of a fire that breathes and flares up on its own (a letter of the logo), in grid pixels. */
struct FireZone {
	/** @brief Its middle and half its width: its feed fades out as sqrt(1 - distance / halfWidth) round the middle. */
	float centre = 0;
	float halfWidth = 0;
	/** @brief How fast it breathes (four seconds over this) and where in the breath it starts. */
	float beat = 3.F;
	float phase = 0;
	/** @brief How far either side of its middle its flare-up's sparks start; 0: its half width. */
	float sparkHalfWidth = 0;
};

/** @brief What a fire burns from: its grid and maps, all width * height, row 0 the bottom one. */
struct FireSource {
	int width = 0;
	int height = 0;
	/** @brief How much heat goes in each second, before the zone's gain (feed map R * 1.6). */
	std::vector<float> feed;
	/** @brief 0..1, how far above its source a point is (feed map G): the air swirls and tears more up there. */
	std::vector<float> rise;
	/** @brief Where sparks start (feed map B > 64, every other pixel), x and y. */
	std::vector<float> sparkX;
	std::vector<float> sparkY;
	/** @brief Its zones; none: one gain of 1 everywhere (no breathing or flare-ups). */
	std::vector<FireZone> zones;
};

/**
 * @brief Reads a fire's maps from an RGBA picture (R: feed, G: height above the source, B: spark spots), top row first.
 * @return false for a picture with no pixels.
 */
bool SourceFromMaps(const uint8_t *rgba, int width, int height, int pitch, FireSource &source);

/** @brief The zones of the title logo's letters (D I A B L O) on its 1100 x 432 grid (twice the 550 x 216 logo). */
std::vector<FireZone> LogoZones();

/** @brief The title logo's grid: twice its 550 x 216 picture. */
constexpr int LogoGridWidth = 1100;
constexpr int LogoGridHeight = 432;

/** @brief A colour of the fire's ramp: 0..255 r, g, b and 0..1 alpha. */
struct RampColour {
	float r, g, b, a;
};

/** @brief The fire's colour at a heat of 0..1.7, `red` of the way from the yellow ramp to the hell-red one. */
RampColour RampAt(float heat, float red);

/** @brief Size of the light map PaintLight writes: a quarter of the grid each way. */
constexpr int LightDivisor = 4;

class LiveFire {
public:
	/**
	 * @param threads Worker threads besides the caller's: -1 picks from the computer's cores; 0 does it all on the
	 * caller's thread. The result is the same whatever the number.
	 */
	LiveFire(FireSource source, FireSettings settings, uint32_t seed = 77, int threads = -1);
	~LiveFire();
	LiveFire(const LiveFire &) = delete;
	LiveFire &operator=(const LiveFire &) = delete;

	int Width() const { return width_; }
	int Height() const { return height_; }
	int LightWidth() const { return lightWidth_; }
	int LightHeight() const { return lightHeight_; }
	float Time() const { return static_cast<float>(time_); }
	const FireSettings &Settings() const { return settings_; }
	void SetSettings(const FireSettings &settings);
	/** @brief The heat, width * height, row 0 the bottom one. */
	const std::vector<float> &Heat() const { return heat_; }
	/** @brief The coarse wind grid (70 x 28 cells of x and y velocity, row 0 the bottom one). */
	const std::vector<float> &Wind() const { return wind_; }
	size_t SparkCount() const { return sparks_.size(); }

	/** @brief Moves it on by a frame's time: at most 0.05 s, in two half steps when over 0.024 s. */
	void Advance(float seconds);
	/** @brief One step of the simulation. */
	void Step(float dt);
	/** @brief Runs it this long in steps of 1/22.5 s without sparks, so it opens already burning. */
	void WarmUp(float seconds = 2.F);

	/** @brief The pointer moved from (x0, y0) to (x1, y1) in grid pixels (y up) over `seconds`: it blows the fire. */
	void Push(float x0, float y0, float x1, float y1, float seconds);
	/** @brief Makes a zone flare up now, with a burst of sparks from it. */
	void Flare(size_t zone);

	/**
	 * @brief Paints the fire's colours, sparks included, as RGBA bytes (alpha 255) top row first, to be added over
	 * what is under it (black shows the fire as it is). `cover`, if given (width * height bytes, top row first),
	 * scales it: 0 hides it there, 255 shows it all.
	 */
	void Paint(uint8_t *rgba, int pitch, const uint8_t *cover = nullptr);

	/**
	 * @brief Paints the firelight on what goes over the fire (the logo's letters), LightWidth * LightHeight, RGBA
	 * bytes, top row first: `multiply` is what their colour is multiplied by (halved when `doubled`, for a blend that
	 * multiplies by twice it), `add` what is added where they are dark, both stretched over the grid's rect.
	 */
	void PaintLight(uint8_t *multiply, int multiplyPitch, uint8_t *add, int addPitch, bool doubled);

	/** @brief The light on the letters at a grid point (0..1): their blurred heat * 1.6. For tests. */
	float LightAt(float x, float y) const;

	struct Spark {
		float x, y, vx, vy, age, life, p, s;
		float ox, oy;
		bool moved;
	};

private:
	class Workers;

	void BuildAir();
	void Simulate(float dt);
	void BuildMips();
	float SampleMip(int level, float u, float v) const;
	void Splat(float x, float y, float vx, float vy);
	void WindAt(float x, float y, float &wx, float &wy) const;
	void AddSpark(float x, float y, float delay, float push);
	void UpdateSparks(float dt);
	void DrawSparks(uint8_t *rgba, int pitch, const uint8_t *cover) const;
	float FlareGain(size_t zone, double t) const;
	float Random();
	void BuildRamp();

	int width_;
	int height_;
	int airWidth_;
	int airHeight_;
	int lightWidth_;
	int lightHeight_;
	FireSource source_;
	FireSettings settings_;
	double time_ = 0;
	double nextFlare_ = 2.5;
	uint32_t rng_;
	bool warming_ = false;

	std::vector<float> heat_;
	std::vector<float> next_;
	/** @brief The swirling air at a quarter of the grid each way: x, y velocity, erosion, tongue, interleaved. */
	std::vector<float> air_;
	std::vector<float> wind_;
	bool windAny_ = false;
	/** @brief Each column's feed gain this step (the zones' breathing). */
	std::vector<float> columnGain_;
	std::vector<double> flareAt_;
	/** @brief The heat's mip levels 1.. (each half the last), as the graphics card's: for the glow and the light. */
	std::vector<std::vector<float>> mips_;
	std::vector<int> mipWidth_;
	std::vector<int> mipHeight_;
	std::vector<Spark> sparks_;
	/** @brief The ramp, 1024 steps over heats 0..1.7: r, g, b premultiplied by alpha (0..255), then alpha. */
	std::vector<float> ramp_;
	float glowR_ = 0, glowG_ = 0, glowB_ = 0;
	std::unique_ptr<Workers> workers_;
};

} // namespace fire

} // namespace devilution
