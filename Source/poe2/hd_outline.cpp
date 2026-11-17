#include "poe2/hd_outline.hpp"

#include <algorithm>
#include <cmath>

namespace devilution {

namespace hd {

OutlineCut OutlineCutFor(const OutlineLook &look)
{
	return look.thick ? OutlineCut::Thick : OutlineCut::Line;
}

float OutlineReach(OutlineCut cut)
{
	switch (cut) {
	case OutlineCut::Thick:
		return 1.75F;
	case OutlineCut::Glow:
		return 4.5F;
	default:
		return 1.F;
	}
}

OutlineShape MakeOutlineShape(const std::vector<uint8_t> &body, const std::vector<uint8_t> &cover, int width, int height, float scaleX, float scaleY, OutlineCut cut)
{
	OutlineShape shape;
	if (width <= 0 || height <= 0 || body.size() < static_cast<size_t>(width) * height || cover.size() < body.size())
		return shape;
	// Only the body and the band round it are worked on.
	int bodyLeft = width;
	int bodyTop = height;
	int bodyRight = -1;
	int bodyBottom = -1;
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			if (body[static_cast<size_t>(y) * width + x] == 0)
				continue;
			bodyLeft = std::min(bodyLeft, x);
			bodyTop = std::min(bodyTop, y);
			bodyRight = std::max(bodyRight, x);
			bodyBottom = std::max(bodyBottom, y);
		}
	}
	if (bodyRight < 0)
		return shape;
	const float radius = OutlineReach(cut) * scaleX;
	const int pad = static_cast<int>(std::ceil(radius)) + 1;
	const int reach = pad + 1;
	// The area worked on, from (x0, y0) in the picture, pad pixels round the body.
	const int x0 = bodyLeft - pad;
	const int y0 = bodyTop - pad;
	const int w = bodyRight - bodyLeft + 1 + pad * 2;
	const int h = bodyBottom - bodyTop + 1 + pad * 2;
	const auto isBody = [&](int x, int y) {
		const int px = x0 + x;
		const int py = y0 + y;
		return px >= 0 && py >= 0 && px < width && py < height && body[static_cast<size_t>(py) * width + px] != 0;
	};
	constexpr int Far = 1 << 14;
	// How far each pixel is from the nearest body pixel in its own row; then the nearest one within `reach` rows.
	std::vector<int> along(static_cast<size_t>(w) * h, Far);
	for (int y = pad; y < h - pad; y++) {
		int *row = &along[static_cast<size_t>(y) * w];
		int last = -Far;
		for (int x = 0; x < w; x++) {
			if (isBody(x, y))
				last = x;
			row[x] = std::min(x - last, Far);
		}
		int next = Far * 2;
		for (int x = w - 1; x >= 0; x--) {
			if (isBody(x, y))
				next = x;
			row[x] = std::min(row[x], next - x);
		}
	}
	std::vector<uint8_t> alpha(static_cast<size_t>(w) * h, 0);
	int left = w;
	int top = h;
	int right = -1;
	int bottom = -1;
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			int nearest = Far;
			for (int dy = std::max(-reach, -y); dy <= std::min(reach, h - 1 - y); dy++) {
				const int across = along[static_cast<size_t>(y + dy) * w + x];
				if (across <= reach)
					nearest = std::min(nearest, across * across + dy * dy);
			}
			if (nearest >= Far)
				continue;
			const float distance = std::sqrt(static_cast<float>(nearest));
			float ring = std::clamp(radius + 1.F - distance, 0.F, 1.F);
			if (cut == OutlineCut::Glow) {
				// Full under the line, then fading out softly to nothing at its reach.
				const float line = OutlineReach(OutlineCut::Line) * scaleX;
				const float fade = std::clamp(1.F - (distance - line) / (radius + 1.F - line), 0.F, 1.F);
				ring = fade * fade;
			}
			const int px = x0 + x;
			const int py = y0 + y;
			const float covered = px >= 0 && py >= 0 && px < width && py < height ? cover[static_cast<size_t>(py) * width + px] / 255.F : 0.F;
			// Squared, so where the sprite's own edge is soft the band mostly goes over it and little shows between.
			const auto value = static_cast<uint8_t>(ring * (1 - covered * covered) * 255.F + 0.5F);
			if (value == 0)
				continue;
			alpha[static_cast<size_t>(y) * w + x] = value;
			left = std::min(left, x);
			top = std::min(top, y);
			right = std::max(right, x);
			bottom = std::max(bottom, y);
		}
	}
	if (right < 0)
		return shape;
	shape.width = right - left + 1;
	shape.height = bottom - top + 1;
	shape.alpha.resize(static_cast<size_t>(shape.width) * shape.height);
	for (int y = 0; y < shape.height; y++) {
		for (int x = 0; x < shape.width; x++)
			shape.alpha[static_cast<size_t>(y) * shape.width + x] = alpha[static_cast<size_t>(top + y) * w + left + x];
	}
	shape.x = static_cast<float>(x0 + left) / scaleX;
	shape.y = static_cast<float>(y0 + top) / scaleY;
	shape.w = static_cast<float>(shape.width) / scaleX;
	shape.h = static_cast<float>(shape.height) / scaleY;
	return shape;
}

float GlowStrength(uint32_t now)
{
	constexpr float TwoPi = 6.2831853F;
	const float breath = 0.5F - 0.5F * std::cos(TwoPi * static_cast<float>(now % GlowBreathMs) / static_cast<float>(GlowBreathMs));
	return GlowLow + (GlowHigh - GlowLow) * breath;
}

void OutlineFader::Fade(const OutlineLook &look, uint32_t now, OutlineColour &colour, float &alpha)
{
	Shown &shown = shown_[look.thing];
	const bool showing = shown.seen && now - shown.at <= OutlineGoneMs;
	if (look.hovered) {
		if (!showing || !shown.hovered) {
			shown.hoverSince = now;
			shown.fromColour = showing ? shown.colour : colour;
			shown.fromAlpha = showing ? shown.alpha : 0.F;
		}
		const float t = std::clamp(static_cast<float>(now - shown.hoverSince) / HoverFadeMs, 0.F, 1.F);
		const float eased = t * t * (3 - 2 * t);
		for (int c = 0; c < 3; c++)
			colour[c] = shown.fromColour[c] + (colour[c] - shown.fromColour[c]) * eased;
		alpha = shown.fromAlpha + (alpha - shown.fromAlpha) * eased;
	}
	shown.colour = colour;
	shown.alpha = alpha;
	shown.at = now;
	shown.seen = true;
	shown.hovered = look.hovered;
}

void OutlineFader::Prune(uint32_t now)
{
	if (shown_.size() <= 256)
		return;
	std::erase_if(shown_, [now](const auto &shown) { return now - shown.second.at > 1000; });
}

} // namespace hd

} // namespace devilution
