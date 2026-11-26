#include "poe2/hd_spell_icons.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <vector>

namespace devilution {

namespace hd {

namespace {

/** @brief The part of a row or column of the source one new pixel covers: its first pixel, and how much of each. */
struct Span {
	int first = 0;
	std::vector<float> weights;
};

/** @brief For each of `to` new pixels, the part of `from` old pixels it covers. */
std::vector<Span> SpansOf(int from, int to)
{
	std::vector<Span> spans(static_cast<size_t>(to));
	const double step = static_cast<double>(from) / to;
	for (int i = 0; i < to; i++) {
		const double begin = i * step;
		const double end = std::min((i + 1) * step, static_cast<double>(from));
		Span &span = spans[static_cast<size_t>(i)];
		span.first = std::min(static_cast<int>(std::floor(begin)), from - 1);
		for (int at = span.first; at < from; at++) {
			const double covered = std::min(end, static_cast<double>(at) + 1) - std::max(begin, static_cast<double>(at));
			if (covered <= 1e-9) {
				if (at > span.first)
					break;
				continue;
			}
			span.weights.resize(static_cast<size_t>(at - span.first) + 1, 0.F);
			span.weights.back() = static_cast<float>(covered);
		}
		// A new pixel thinner than rounding allows still takes the pixel it is in.
		if (span.weights.empty())
			span.weights.push_back(1.F);
	}
	return spans;
}

uint8_t ToByte(float value)
{
	return static_cast<uint8_t>(std::clamp(value, 0.F, 255.F) + 0.5F);
}

} // namespace

std::string_view SpellIconPlateName(SpellType tint, bool large)
{
	switch (tint) {
	case SpellType::Skill:
		return large ? "spellicons/plate_skill" : "spellicons/small/plate_skill";
	case SpellType::Spell:
		return large ? "spellicons/plate_spell" : "spellicons/small/plate_spell";
	case SpellType::Scroll:
		return large ? "spellicons/plate_scroll" : "spellicons/small/plate_scroll";
	case SpellType::Charges:
		return large ? "spellicons/plate_charges" : "spellicons/small/plate_charges";
	case SpellType::Invalid:
		return large ? "spellicons/plate_invalid" : "spellicons/small/plate_invalid";
	}
	return {};
}

std::string_view SpellIconPlateFallback(std::string_view plate)
{
	constexpr std::string_view Small = "spellicons/small/plate_";
	if (plate.substr(0, Small.size()) != Small)
		return {};
	for (const SpellType tint : { SpellType::Skill, SpellType::Spell, SpellType::Scroll, SpellType::Charges, SpellType::Invalid }) {
		if (SpellIconPlateName(tint, false) == plate)
			return SpellIconPlateName(tint, true);
	}
	return {};
}

std::string SpellIconLayerName(uint8_t frame, bool large)
{
	if (!SpellIconHasHdArt(frame) || frame == SpellIconEmptyFrame)
		return {};
	std::string name = large ? "spellicons/big/" : "spellicons/small/";
	name += static_cast<char>('0' + frame / 10);
	name += static_cast<char>('0' + frame % 10);
	return name;
}

std::string SpellIconArtName(uint8_t frame, bool large, SpellType tint)
{
	const std::string_view plate = SpellIconPlateName(tint, large);
	if (!SpellIconHasHdArt(frame) || plate.empty())
		return {};
	std::string name(plate);
	if (frame != SpellIconEmptyFrame) {
		name += '+';
		name += SpellIconLayerName(frame, large);
	}
	return name;
}

void LayPictureOver(uint8_t *under, int underPitch, const uint8_t *over, int overPitch, int width, int height)
{
	for (int y = 0; y < height; y++) {
		uint8_t *to = under + static_cast<ptrdiff_t>(y) * underPitch;
		const uint8_t *from = over + static_cast<ptrdiff_t>(y) * overPitch;
		for (int x = 0; x < width; x++, to += 4, from += 4) {
			const int overAlpha = from[3];
			if (overAlpha == 0)
				continue;
			if (overAlpha == 255) {
				std::copy(from, from + 4, to);
				continue;
			}
			// How much of each shows, out of 255 * 255.
			const int overPart = overAlpha * 255;
			const int underPart = to[3] * (255 - overAlpha);
			const int total = overPart + underPart;
			for (int c = 0; c < 3; c++)
				to[c] = static_cast<uint8_t>((from[c] * overPart + to[c] * underPart + total / 2) / total);
			to[3] = static_cast<uint8_t>((total + 127) / 255);
		}
	}
}

void ShrinkPicture(const uint8_t *src, int srcWidth, int srcHeight, int srcPitch, uint8_t *dst, int dstWidth, int dstHeight, int dstPitch)
{
	if (srcWidth <= 0 || srcHeight <= 0 || dstWidth <= 0 || dstHeight <= 0)
		return;
	const std::vector<Span> columns = SpansOf(srcWidth, dstWidth);
	const std::vector<Span> rows = SpansOf(srcHeight, dstHeight);
	for (int y = 0; y < dstHeight; y++) {
		const Span &row = rows[static_cast<size_t>(y)];
		uint8_t *to = dst + static_cast<ptrdiff_t>(y) * dstPitch;
		for (int x = 0; x < dstWidth; x++, to += 4) {
			const Span &column = columns[static_cast<size_t>(x)];
			// Colours weighted by their alpha: a see-through pixel's colour (often black) counts for nothing.
			float colour[3] = { 0.F, 0.F, 0.F };
			float alpha = 0.F;
			float area = 0.F;
			for (size_t j = 0; j < row.weights.size(); j++) {
				const uint8_t *from = src + static_cast<ptrdiff_t>(row.first + static_cast<int>(j)) * srcPitch + static_cast<ptrdiff_t>(column.first) * 4;
				for (size_t i = 0; i < column.weights.size(); i++, from += 4) {
					const float weight = row.weights[j] * column.weights[i];
					const float shows = weight * static_cast<float>(from[3]);
					colour[0] += shows * static_cast<float>(from[0]);
					colour[1] += shows * static_cast<float>(from[1]);
					colour[2] += shows * static_cast<float>(from[2]);
					alpha += shows;
					area += weight;
				}
			}
			if (alpha <= 0.F || area <= 0.F) {
				to[0] = to[1] = to[2] = to[3] = 0;
				continue;
			}
			to[0] = ToByte(colour[0] / alpha);
			to[1] = ToByte(colour[1] / alpha);
			to[2] = ToByte(colour[2] / alpha);
			to[3] = ToByte(alpha / area);
		}
	}
}

int FirstScreenPixelOf(int uiPixel, float scale)
{
	return static_cast<int>(std::ceil(static_cast<float>(uiPixel) * scale - 0.5F));
}

} // namespace hd

} // namespace devilution
