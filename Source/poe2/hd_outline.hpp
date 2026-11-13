#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "poe2/hd.hpp"

namespace devilution {

namespace hd {

// The smooth outlines of the HD renderer, without the renderer: their shape, how they fade in under the cursor, and
// how their glow breathes. The world's outlines (hd.cpp) and the outlines of item pictures in the UI (hd_ui.cpp) share
// them, so both look the same.

/** @brief The kinds of smooth outline made round a sprite. */
enum class OutlineCut : uint8_t {
	/** @brief As thick as the game's: one game pixel. */
	Line,
	/** @brief Round rare and unique monsters. */
	Thick,
	/** @brief A soft glow that fades out away from the sprite, adding light to what is under it. */
	Glow,
};
constexpr size_t NumOutlineCuts = 3;

/** @brief The cut an outline is drawn in: thick round rares and bosses, else the game's width. Glows are drawn under it. */
[[nodiscard]] OutlineCut OutlineCutFor(const OutlineLook &look);

/** @brief How far each cut of outline reaches past the sprite, in game pixels. */
[[nodiscard]] float OutlineReach(OutlineCut cut);

/** @brief A smooth outline's shape: how much of each of its pixels it covers (0-255), and where it sits over its sprite. */
struct OutlineShape {
	int width = 0;
	int height = 0;
	std::vector<uint8_t> alpha;
	/** @brief Its place over the sprite's own rectangle, in the sprite's game pixels (it reaches past the rectangle). */
	float x = 0;
	float y = 0;
	float w = 0;
	float h = 0;

	[[nodiscard]] bool empty() const { return width <= 0 || height <= 0; }
};

/**
 * @brief Makes a smooth outline: a band OutlineReach(cut) game pixels wide round the `body` pixels, its outer edge
 * anti-aliased (a glow fades out across all of it), and left off where the sprite covers (`cover`, 0 to 255), since
 * outlines are drawn over their sprite.
 *
 * Both are width x height, at scaleX x scaleY pixels to the game pixel. The band reaches as far as the nearest body
 * pixel is, so it goes round in curves instead of the stair steps of the pixels it goes round.
 */
[[nodiscard]] OutlineShape MakeOutlineShape(const std::vector<uint8_t> &body, const std::vector<uint8_t> &cover, int width, int height, float scaleX, float scaleY, OutlineCut cut);

/** @brief How long the outline of the thing under the cursor takes to fade in, in ms (so it doesn't pop on). */
constexpr float HoverFadeMs = 140.F;
/** @brief How long a glow takes to swell and settle again, in ms: slow, like breathing. */
constexpr uint32_t GlowBreathMs = 2400;
/** @brief How much of a glow shows, settled and swollen. */
constexpr float GlowLow = 0.35F;
constexpr float GlowHigh = 0.8F;
/** @brief An outline not drawn for this long (ms) is gone, and the cursor coming back to its thing fades it in again. */
constexpr uint32_t OutlineGoneMs = 150;

/** @brief How much of a glow shows at this time (ms), between GlowLow and GlowHigh. */
[[nodiscard]] float GlowStrength(uint32_t now);

using OutlineColour = std::array<float, 3>;

/** @brief Remembers how each thing's outline was last drawn, so the cursor's fades in. */
class OutlineFader {
public:
	/**
	 * @brief The outline of a thing (look.thing) as it shows now. The cursor's fades in over HoverFadeMs: from the
	 * outline that thing had (a rank's, a ground item's, or one in another colour), or from nothing.
	 */
	void Fade(const OutlineLook &look, uint32_t now, OutlineColour &colour, float &alpha);

	/** @brief Forgets the outlines not drawn for a while, once there are many. */
	void Prune(uint32_t now);

	[[nodiscard]] size_t size() const { return shown_.size(); }

private:
	/** @brief How a thing's outline was last drawn, and for the cursor's: since when it fades in, and from what. */
	struct Shown {
		/** @brief Its colour and how much of it showed, when (ms), and whether it was the cursor's. */
		OutlineColour colour {};
		float alpha = 0;
		uint32_t at = 0;
		bool seen = false;
		bool hovered = false;
		uint32_t hoverSince = 0;
		OutlineColour fromColour {};
		float fromAlpha = 0;
	};
	std::unordered_map<uint32_t, Shown> shown_;
};

} // namespace hd

} // namespace devilution
