#pragma once

/**
 * @file poe2/menu_intro.hpp
 *
 * POE2 Mashup: the main menu's intro (approved by Curtis 11-23, art/hd-menus/logo/intro/index.html v18 and its
 * INTRO_SPEC.md). DIABLO flies in from behind the viewer burning, lands where the main menu's logo is, four claws rake
 * the space under it and leave "Sanctuary Reforged" in the gouges, white-hot and cooling to worn metal, between two
 * burning lines; then the menu fades in.
 *
 * This file is its CPU half, with no renderer: the timeline (every number is the reference's), the pictures worked out
 * on the CPU each frame (the subtitle, the claw gouges, the room's glow), the embers across the screen and the burning
 * lines' fire (a second, finer live fire, poe2/live_fire.hpp). poe2/menu_intro_sdl.cpp puts it on the screen.
 *
 * Units: "browser pixels" are the reference's 1600 x 900 stage, y up, where the landed logo's 1100 x 560 box (its
 * 1100 x 432 picture at twice the 550 x 216 logo, and 128 rows under it for the subtitle) is 0.9 of a pixel a box pixel
 * with its point (550, 278) at (800, 600). "Box pixels" are that box's, y up from its bottom.
 * Cosmetic only: nothing here touches the game's state or its random numbers.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "poe2/live_fire.hpp"

namespace devilution {

namespace intro {

// ---- the timeline, seconds from the start ----

/** @brief The logo starts its flight. */
constexpr float FlightStart = 1.0F;
/** @brief It lands and catches. */
constexpr float Landing = 3.6F;
/** @brief The fire and the room's glow dip before the claws. */
constexpr float Hush = 4.8F;
/** @brief The claws. */
constexpr float Slash = 5.4F;
/** @brief The words are stamped (a small second hit). */
constexpr float Stamp = Slash + 0.10F;
/** @brief The menu starts fading in. */
constexpr float MenuAt = 8.4F;
/** @brief The end: from here it is the main menu. */
constexpr float End = 11.5F;

/** @brief The landed logo's scale on the reference stage (browser pixels a box pixel). */
constexpr float RestScale = 0.9F;
/** @brief The point of the box that stays put as it flies (box pixels, y up), and where it is on the stage. */
constexpr float AnchorX = 550.F;
constexpr float AnchorY = 278.F;
constexpr float StageAnchorX = 800.F;
constexpr float StageAnchorY = 600.F;
/** @brief The box: the logo's picture (twice the logo) and the room under it for the subtitle. */
constexpr float BoxWidth = 1100.F;
constexpr float BoxHeight = 560.F;
constexpr float PictureHeight = 432.F;
/** @brief The claws' way across the screen (browser pixels, y up), and across it. */
constexpr float ClawDirX = -0.982F;
constexpr float ClawDirY = -0.191F;

float SmoothStep(float a, float b, float x);

/** @brief The logo's size as it flies, lands and settles, 1 for landed; 0 before it shows. */
float LogoScale(float t);
/** @brief How far the claws knock the logo along their way, in browser pixels. */
float Knock(float t);
/** @brief How much the logo's fire is fed (its FireSettings::amount). */
float FireAmount(float t);
/** @brief The light leaking through from behind the letters as it flies (LiveFire::SetPaintExtras). */
float BackLight(float t);
/** @brief How far the fire trails toward the viewer as it flies (LiveFire::SetPaintExtras). */
float Trail(float t);
/** @brief The room's red glow behind it all. */
float RoomGlow(float t);
/** @brief Time runs this fast: the hit-stops on the landing and the claws (12 ms at 0.075). */
float TimeWarp(float t);
/** @brief How far the burning lines have caught, 0..1, from the middle out. */
float LineReach(float t);
/** @brief How much the lines' fire is fed (FireSettings::amount): a burst as they catch. */
float LineAmount(float t);
/** @brief The menu fading in, 0..1. */
float MenuAlpha(float t);
/**
 * @brief The intro's own look over the menu (its lens, the room's glow, the embers): 1 until the menu shows, fading out
 * by the end so it ends on the menu as it always is.
 */
float IntroLook(float t);

/** @brief The lens at a moment (FS_POST): shake (browser pixels, y up), punch zoom, speed blur, rays and the rest. */
struct Lens {
	float shakeX = 0;
	float shakeY = 0;
	float punch = 0;
	float zoom = 0;
	float rays = 0;
	float aberration = 0;
	float flash = 0;
	float dim = 1;
};
/** @brief The lens at `t`; `jitterX` and `jitterY` (-0.5..0.5) shake it a little as the logo rushes in. */
Lens LensAt(float t, float jitterX, float jitterY);

/** @brief A sound of the intro: when it starts (seconds of real time from the start), how loud and how it fades in. */
struct SoundCue {
	/** @brief Its file under hd\ui_art\intro\. For the claws, one of two at random (`alternative`). */
	const char *file;
	const char *alternative;
	float at;
	float volume;
	float fadeIn;
};
/** @brief The intro's sounds, in the order they start. */
const std::array<SoundCue, 4> &SoundCues();

/** @brief The reference's random numbers (mulberry32): the same seed, the same intro. */
class Random {
public:
	explicit Random(uint32_t seed = 77)
	    : state_(seed)
	{
	}
	float Next();

private:
	uint32_t state_;
};

// ---- value noise as the reference's (3D, smooth, about -1.73..1.73) ----

float ValueNoise(float x, float y, float z);
float Fbm2(float x, float y, float z);
float Fbm3(float x, float y, float z);

// ---- the subtitle ----

/**
 * @brief "Sanctuary Reforged" (FS_SUB): white-hot as the claws leave it, cooling to worn bronze-grey metal with grime,
 * light from above and a warm ember glow under it, a glow round it (the mask's G) and one glint across it at 8.1 s.
 * Its picture covers the words' box, 845 x 93.6 box pixels at (127.5, 48.2).
 */
class Subtitle {
public:
	/** @brief Box pixels of the words' box. */
	static constexpr float Left = 127.5F;
	static constexpr float Bottom = 48.2F;
	static constexpr float Width = 845.F;
	static constexpr float Height = 93.6F;

	/**
	 * @brief Reads the metal picture and the mask (RGBA, top row first, the same size or not) into a picture of
	 * `width` x `height`. False when either is missing.
	 */
	bool Load(const uint8_t *steel, int steelWidth, int steelHeight, int steelPitch, const uint8_t *mask, int maskWidth, int maskHeight, int maskPitch, int width = 900, int height = 100);
	bool Loaded() const { return !worn_.empty(); }
	int PictureWidth() const { return width_; }
	int PictureHeight() const { return height_; }

	/** @brief Whether it shows at `sinceSlash` seconds after the claws. */
	static bool Shows(float sinceSlash);
	/** @brief How much bigger than its box it is drawn, popping in as it is stamped. */
	static float Pop(float sinceSlash);

	/** @brief Paints it, alpha premultiplied, top row first; `t` is the fire's clock, for its flicker and embers. */
	void Paint(uint8_t *rgba, int pitch, float t, float sinceSlash, float red) const;

private:
	int width_ = 0;
	int height_ = 0;
	/** @brief Per pixel: the metal's alpha, the mask's glow, and the worn metal's colour (r, g, b). */
	std::vector<float> alpha_;
	std::vector<float> glow_;
	std::vector<float> worn_;
};

// ---- the claws ----

/**
 * @brief The four claw gouges (FS_OVR): they rake across under the logo in about four frames, white-hot, cool to molten
 * orange and fade by about 3.6 s. Worked out across and along the claws' way: its picture's x goes along it (u, from
 * Left to Right browser pixels from the claws' middle) and its rows across it (v, from Top down to Bottom), so it is
 * drawn turned to the claws' way, its middle at the claws' middle plus (MidU, MidV) along and across.
 */
class Claws {
public:
	static constexpr float Left = -470.F;
	static constexpr float Right = 490.F;
	static constexpr float Top = 190.F;
	static constexpr float Bottom = -180.F;
	/** @brief The claws' middle on the reference stage (browser pixels, y up). */
	static constexpr float CentreX = 800.F;
	static constexpr float CentreY = 408.F;
	/** @brief How long after the claws they show. */
	static constexpr float Lasts = 4.5F;

	/** @brief Works out what doesn't change, at `perPixel` picture pixels a browser pixel. */
	void Prepare(float perPixel = 0.5F);
	bool Prepared() const { return width_ > 0; }
	int Width() const { return width_; }
	int Height() const { return height_; }

	/** @brief Paints them to be added over the screen (RGBA, alpha 255), top row first. False when nothing shows. */
	bool Paint(uint8_t *rgba, int pitch, float t, float sinceSlash, float red) const;

private:
	int width_ = 0;
	int height_ = 0;
	/** @brief Per claw and pixel: the gouge's core and its glow (row by row). */
	/** @brief Per pixel, then claw: its gouge's core and its glow (eight floats a pixel, read in one go). */
	std::vector<float> lit_;
	/** @brief Per claw and column: where along it the column is (0..1; outside: not on it). */
	std::vector<float> along_[4];
	/** @brief Per claw and column: the first and last rows it lights (none: first past last). */
	std::vector<int> first_[4];
	std::vector<int> last_[4];
};

// ---- the room ----

/**
 * @brief The room behind it all (FS_BG): a hell-red glow with drifting smoke, brighter low down. Painted small over the
 * whole screen (its x and y 0..1 across and down it, as the reference's stage), to be stretched over it.
 */
void PaintRoom(uint8_t *rgba, int pitch, int width, int height, float t, float glow);

// ---- the embers ----

/** @brief An ember or spark flying across the screen, in browser pixels (y up). */
struct Ember {
	float x, y, vx, vy, age, life, p, s;
	float ox, oy;
	/** @brief 0: drifting; 1: thrown by the claws, falling; 2: blown out by the landing, slowing. */
	int kind;
	float dim;
	bool moved;
};

/** @brief A streak to draw: from its tail (no light) to its head, how wide, its colour (0..1) and brightness. */
struct Streak {
	float tailX, tailY, headX, headY, halfWidth;
	float r, g, b, a;
};

/**
 * @brief The embers across the whole screen: a few in the dark, a storm once it has landed, a blast as it lands and
 * sparks thrown off the claws. The screen is (left, bottom) to (right, top) in browser pixels; it may be bigger than the
 * reference's stage (more of them, as many to the pixel).
 */
class Embers {
public:
	void SetScreen(float left, float bottom, float right, float top);
	/** @brief Moves them on by `dt` (intro time `t` after it), adding new ones. */
	void Step(float dt, float t, Random &random);
	/** @brief The blast as the logo lands. */
	void Landing(Random &random);
	/** @brief The sparks thrown off the claws. */
	void Rake(Random &random);
	void Clear() { embers_.clear(); }
	size_t Count() const { return embers_.size(); }
	/** @brief Their streaks, brightness times `fade`. */
	void Streaks(std::vector<Streak> &out, float fade) const;

private:
	std::vector<Ember> embers_;
	float left_ = 0;
	float bottom_ = 0;
	float right_ = 1600;
	float top_ = 900;
};

// ---- the burning lines ----

/** @brief The lines' fire grid: 1120 x 250 box pixels at (-10, 16). */
constexpr int LineGridX = -10;
constexpr int LineGridY = 16;
constexpr int LineGridWidth = 1120;
constexpr int LineGridHeight = 250;
/** @brief The long line above the words and the short one under them: height and half width (box pixels), middle x. */
constexpr float TopLineY = 132.F;
constexpr float TopLineHalfWidth = 480.F;
constexpr float BottomLineY = 58.F;
constexpr float BottomLineHalfWidth = 230.F;
constexpr float LineMiddleX = 550.F;

/** @brief The lines' fire: its grid, fed nowhere yet (LineFeed), cooled hard just over the lower line. */
fire::FireSource LineSource();
/** @brief Where the lines feed their fire when they have caught `reach` of the way out (FireSource::feed). */
void LineFeed(std::vector<float> &feed, float reach);
/** @brief Adds the lines' sparks (about 14 a second) for a step of `dt`. */
void LineSparks(fire::LiveFire &fire, float dt, float reach, Random &random);
/**
 * @brief Paints the lines themselves over their fire's picture (LiveFire::Paint's, top row first): a thin white-orange
 * seam with a soft glow, flickering along its length.
 */
void PaintSeams(uint8_t *rgba, int pitch, float t, float reach, float red);

// ---- the faint glow spots after the landing ----

/** @brief A soft spot of light drifting across the screen: where (x and y 0..1 across and down it), how big, how bright. */
struct GlowSpot {
	float x, y, radius, bright;
};
/** @brief The twelve spots at intro time `t` (radius in browser pixels; brightness 0..1, before the colour). */
std::array<GlowSpot, 12> GlowSpots(float t);
/** @brief A spot's light at `d` of its radius from its middle. */
float GlowSpotProfile(float d);

} // namespace intro

} // namespace devilution
