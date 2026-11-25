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

/**
 * @brief How a fire is worked out: the sizes and speeds of its noise, how it moves and cools, and how it is coloured.
 * The defaults are the title logo's (live/GAME_SPEC.md); LineRecipe is the menu intro's thin burning lines'.
 */
struct FireRecipe {
	/** @brief The swirling air is worked out on a grid this many times smaller each way. */
	int airDivisor = 4;
	/** @brief The swirl's two stream functions (fbm2): size (grid pixels), rise speed (pixels a second), rate in time, strength. */
	float swirlSize[2] = { 70.F, 26.F };
	float swirlSpeed[2] = { 108.F, 216.F };
	float swirlRate[2] = { 1.5F, 2.5F };
	float swirlStrength[2] = { 36.F, 30.F };
	/** @brief The step of its central difference, in grid pixels. */
	float swirlStep = 2.F;
	/** @brief The erosion (fbm3) that tears flames apart and the tongues (fbm2) at their base, and the tongues' range. */
	float erosionSize = 30.F;
	float erosionSpeed = 324.F;
	float erosionRate = 2.F;
	float tongueSize = 22.F;
	float tongueRate = 2.F;
	float tongueMin = 0.F;
	float tongueMax = 1.5F;
	/** @brief Its movement and cooling, before FireSettings' height and amount. */
	float rise = 112.F;
	float buoy = 176.F;
	float cool = 0.22F;
	float erode = 1.25F;
	float emit = 9.F;
	float swirlX = 1.3F;
	float swirlY = 1.2F;
	/** @brief The swirl and the erosion grow with the height above the source (FireSource::rise); off: the same everywhere. */
	bool byHeight = true;
	/** @brief How far below its top the heat fades out, in grid pixels. */
	float topFade = 34.F;
	/** @brief The flicker on its colour (fbm2), and how hot what is shown is of the heat. */
	float detailSize = 9.F;
	float detailSpeed = 432.F;
	float detailRate = 3.F;
	float shown = 0.86F;
	/** @brief The glow behind: the heat's mip level it reads (3..5) and how much it shows. */
	float glowLevel = 4.6F;
	float glowAlpha = 0.55F;
	/** @brief How far from its top and its sides it fades out, in grid pixels. */
	float edgeTop = 26.F;
	float edgeSide = 16.F;
	/** @brief Sparks a second from its spark spots, before FireSettings::sparks. */
	float sparkRate = 38.F;
};

/** @brief The menu intro's burning lines round its subtitle: the logo's fire made finer and shorter (FS_SIM2, FS_FIRE2). */
FireRecipe LineRecipe();

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
	/**
	 * @brief Columns of the grid either side of its maps' picture, where nothing feeds: room for flames that lean out
	 * past the picture's edges. `width` counts them (the picture is width - 2 * pad wide); every x (sparks, zones) is
	 * in grid pixels, so the picture's own x is grid x - pad. The light on what goes over the fire covers the picture.
	 */
	int pad = 0;
	/** @brief How much heat goes in each second, before the zone's gain (feed map R * 1.6). */
	std::vector<float> feed;
	/** @brief 0..1, how far above its source a point is (feed map G): the air swirls and tears more up there. */
	std::vector<float> rise;
	/** @brief Where sparks start (feed map B > 64, every other pixel), x and y. */
	std::vector<float> sparkX;
	std::vector<float> sparkY;
	/** @brief Its zones; none: one gain of 1 everywhere (no breathing or flare-ups). */
	std::vector<FireZone> zones;
	/** @brief Extra cooling a second at each point (width * height, row 0 the bottom one); empty for none. */
	std::vector<float> cool;
};

/**
 * @brief Reads a fire's maps from an RGBA picture (R: feed, G: height above the source, B: spark spots), top row first,
 * into a grid `pad` columns wider than it on each side (FireSource::pad): no feed or sparks out there, and the rise of
 * the picture's edge column.
 * @return false for a picture with no pixels.
 */
bool SourceFromMaps(const uint8_t *rgba, int width, int height, int pitch, FireSource &source, int pad = 0);

/** @brief The zones of the title logo's letters (D I A B L O) on its 1100 x 432 grid (twice the 550 x 216 logo). */
std::vector<FireZone> LogoZones();

/** @brief The title logo's grid: twice its 550 x 216 picture. */
constexpr int LogoGridWidth = 1100;
constexpr int LogoGridHeight = 432;
/** @brief How far the title logo's fire reaches past its picture on each side, on its grid (80 logo pixels). */
constexpr int LogoGridPad = 160;

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
	LiveFire(FireSource source, FireSettings settings, uint32_t seed = 77, int threads = -1, const FireRecipe &recipe = {});
	~LiveFire();
	LiveFire(const LiveFire &) = delete;
	LiveFire &operator=(const LiveFire &) = delete;

	int Width() const { return width_; }
	int Height() const { return height_; }
	/** @brief Columns either side of the maps' picture (FireSource::pad), and the picture's own width. */
	int Pad() const { return pad_; }
	int PictureWidth() const { return width_ - 2 * pad_; }
	/** @brief The light map's size: a quarter of the maps' picture (not the padding) each way. */
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
	/** @brief At most `count` worker threads help work it out (with the caller's); -1 for all it has. */
	void LimitWorkers(int count);
	/** @brief One step of the simulation. */
	void Step(float dt);
	/** @brief Runs it this long in steps of 1/22.5 s without sparks, so it opens already burning. */
	void WarmUp(float seconds = 2.F);

	/** @brief The pointer moved from (x0, y0) to (x1, y1) in grid pixels (y up) over `seconds`: it blows the fire. */
	void Push(float x0, float y0, float x1, float y1, float seconds);
	/** @brief Makes a zone flare up now, with a burst of sparks from it. */
	void Flare(size_t zone);
	/** @brief Makes a zone flare up as if it had `ago` seconds ago (past its dip), its sparks starting `sparkDelay` on. */
	void FlareAgo(size_t zone, float ago, float sparkDelay);

	/** @brief Puts out the fire: no heat, no wind, no sparks (its time goes on). */
	void Clear();
	/** @brief Where its heat goes in (FireSource::feed), to change it as it burns. */
	std::vector<float> &Feed() { return source_.feed; }
	/** @brief A gust of wind at (x, y), grid pixels: as the pointer's, without its limits. */
	void Blow(float x, float y, float vx, float vy);
	/**
	 * @brief Blows outward from (x, y) over the whole wind grid, `strength` of the way, the grid going that way at
	 * 1 - 0.02^dt a step: as when the logo flies at the viewer (x 1.2, y 1.3, plus a quarter of x upward).
	 */
	void BlowOutward(float x, float y, float strength, float dt);
	/** @brief A spark at (x, y), grid pixels, starting `delay` on, thrown `push` times as hard as the fire's own. */
	void AddSparkAt(float x, float y, float delay, float push) { AddSpark(x, y, delay, push); }
	/**
	 * @brief What Paint adds while the menu intro's logo flies: its heat trailing toward the viewer from (trail) and
	 * the light leaking through from behind the letters before they catch (back); 0 for none.
	 */
	void SetPaintExtras(float trail, float back);

	/**
	 * @brief Paints the fire's colours, sparks included, as RGBA bytes (alpha 255) top row first, to be added over
	 * what is under it (black shows the fire as it is). `cover`, if given (width * height bytes, top row first),
	 * scales it: 0 hides it there, 255 shows it all.
	 */
	void Paint(uint8_t *rgba, int pitch, const uint8_t *cover = nullptr);

	/**
	 * @brief Paints the firelight on what goes over the fire (the logo's letters), LightWidth * LightHeight, RGBA
	 * bytes, top row first: `multiply` is what their colour is multiplied by (halved when `doubled`, for a blend that
	 * multiplies by twice it), `add` what is added where they are dark, both stretched over the maps' picture (the
	 * grid without its padding).
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
	void FlareWith(size_t zone, double at, float sparkDelay);
	float Random();
	void BuildRamp();
	void BuildTrail();
	void BuildBack();

	FireRecipe recipe_;
	float trail_ = 0;
	float back_ = 0;
	/** @brief The heat trailing toward the viewer, at the heat's first mip level's size. */
	std::vector<float> trailMap_;
	/** @brief The light from behind the letters: their feed map blurred, a quarter of the picture each way. */
	std::vector<float> backMap_;
	int backWidth_ = 0;
	int backHeight_ = 0;

	int width_;
	int height_;
	int pad_;
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
