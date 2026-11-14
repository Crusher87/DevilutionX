#include "poe2/rune_icons.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "engine/palette.h"
#include "engine/surface.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "poe2/hd.hpp"
#include "poe2/crafting_items.hpp"

namespace devilution {

namespace {

/** A glyph stroke from one point to another on a 7 by 9 grid, which the stone shows at twice that size. */
struct Stroke {
	int8_t x0, y0, x1, y1;
};

using Glyph = std::span<const Stroke>;

constexpr Stroke RalGlyph[] = { { 1, 0, 1, 8 }, { 1, 0, 5, 2 }, { 5, 2, 1, 4 }, { 1, 4, 5, 8 } };
constexpr Stroke ThulGlyph[] = { { 0, 0, 3, 4 }, { 6, 0, 3, 4 }, { 3, 4, 3, 8 } };
constexpr Stroke OrtGlyph[] = { { 3, 0, 3, 8 }, { 0, 3, 6, 3 }, { 1, 7, 5, 7 } };
constexpr Stroke OhmGlyph[] = { { 0, 8, 0, 2 }, { 0, 2, 3, 0 }, { 3, 0, 6, 2 }, { 6, 2, 6, 8 } };
constexpr Stroke AmnGlyph[] = { { 0, 0, 6, 8 }, { 6, 0, 0, 8 }, { 3, 0, 3, 8 } };
constexpr Stroke VexGlyph[] = { { 0, 0, 3, 8 }, { 3, 8, 6, 0 }, { 1, 4, 5, 4 } };
constexpr Stroke IoGlyph[] = { { 3, 0, 3, 8 }, { 1, 0, 5, 0 }, { 1, 8, 5, 8 } };
constexpr Stroke TirGlyph[] = { { 3, 0, 3, 8 }, { 0, 3, 3, 0 }, { 6, 3, 3, 0 } };
constexpr Stroke SolGlyph[] = { { 0, 4, 3, 0 }, { 3, 0, 6, 4 }, { 6, 4, 3, 8 }, { 3, 8, 0, 4 } };
constexpr Stroke GulGlyph[] = { { 0, 0, 6, 0 }, { 6, 0, 6, 8 }, { 0, 4, 6, 4 }, { 0, 0, 0, 4 } };
constexpr Stroke FalGlyph[] = { { 1, 0, 1, 8 }, { 1, 1, 5, 3 }, { 1, 4, 5, 6 } };
constexpr Stroke KoGlyph[] = { { 1, 0, 1, 8 }, { 5, 0, 1, 4 }, { 1, 4, 5, 8 } };
constexpr Stroke LumGlyph[] = { { 0, 8, 3, 0 }, { 3, 0, 6, 8 }, { 3, 4, 3, 8 } };
constexpr Stroke IthGlyph[] = { { 0, 0, 0, 8 }, { 6, 0, 6, 8 }, { 0, 4, 6, 4 } };
constexpr Stroke ShaelGlyph[] = { { 6, 0, 0, 3 }, { 0, 3, 6, 5 }, { 6, 5, 0, 8 } };
constexpr Stroke ZodGlyph[] = { { 0, 0, 6, 0 }, { 6, 0, 0, 8 }, { 0, 8, 6, 8 } };
constexpr Stroke VenomGlyph[] = { { 3, 0, 3, 6 }, { 1, 8, 3, 6 }, { 5, 8, 3, 6 }, { 0, 2, 6, 2 } };

constexpr std::array<Glyph, static_cast<size_t>(RuneFamily::Count)> FamilyGlyphs = {
	RalGlyph, ThulGlyph, OrtGlyph, OhmGlyph, AmnGlyph, VexGlyph, IoGlyph, TirGlyph, SolGlyph, GulGlyph, FalGlyph, KoGlyph, LumGlyph,
	IthGlyph, ShaelGlyph, ZodGlyph, VenomGlyph
};

constexpr Stroke TalicGlyph[] = { { 0, 8, 0, 0 }, { 0, 0, 3, 3 }, { 3, 3, 6, 0 }, { 6, 0, 6, 8 } };
constexpr Stroke MadawcGlyph[] = { { 0, 4, 6, 4 }, { 4, 2, 6, 4 }, { 4, 6, 6, 4 }, { 1, 0, 1, 8 } };
constexpr Stroke KorlicGlyph[] = { { 3, 0, 0, 8 }, { 3, 0, 6, 8 }, { 1, 5, 5, 5 } };
constexpr Stroke BulKathosGlyph[] = { { 0, 0, 6, 8 }, { 6, 0, 0, 8 }, { 0, 4, 6, 4 } };
constexpr Stroke TalRashaGlyph[] = { { 3, 0, 0, 4 }, { 3, 0, 6, 4 }, { 0, 4, 3, 8 }, { 6, 4, 3, 8 }, { 3, 0, 3, 8 } };
constexpr Stroke HorazonGlyph[] = { { 0, 8, 0, 0 }, { 0, 0, 6, 0 }, { 6, 0, 6, 8 }, { 0, 8, 6, 8 }, { 3, 2, 3, 6 } };
constexpr Stroke JeredCainGlyph[] = { { 1, 0, 1, 8 }, { 1, 8, 5, 8 }, { 5, 8, 5, 3 }, { 5, 3, 1, 3 } };
constexpr Stroke ZoltunKulleGlyph[] = { { 0, 0, 6, 8 }, { 0, 8, 6, 0 }, { 3, 0, 3, 2 }, { 3, 6, 3, 8 } };

constexpr std::array<Glyph, RunesOfLegendCount> LegendGlyphs = {
	TalicGlyph, MadawcGlyph, KorlicGlyph, BulKathosGlyph, TalRashaGlyph, HorazonGlyph, JeredCainGlyph, ZoltunKulleGlyph
};

/** The glow round the stone, and the colour its carving takes, by strength; Runes of Legend glow gold. */
struct Glow {
	/** Rings of glow round the stone, 0 to 2. */
	int rings;
	uint8_t inner;
	uint8_t outer;
	/** The carving's colour; 0 keeps it dark. */
	uint8_t carving;
};

Glow GlowFor(AugmentId id)
{
	if (IsRuneOfLegend(id))
		return { 2, PAL16_YELLOW + 2, PAL16_ORANGE + 6, PAL16_YELLOW + 1 };
	const std::optional<RuneKind> rune = GetRuneKind(id);
	if (!rune)
		return { 0, 0, 0, 0 };
	switch (rune->strength) {
	case RuneStrength::Normal:
		return { 1, PAL16_BLUE + 3, 0, 0 };
	case RuneStrength::Greater:
		return { 2, PAL16_YELLOW + 4, PAL16_YELLOW + 10, PAL16_YELLOW + 3 };
	case RuneStrength::Perfect:
		return { 2, PAL16_RED + 3, PAL16_ORANGE + 8, PAL16_ORANGE + 2 };
	default:
		// Lesser runes are plain stone.
		return { 0, 0, 0, 0 };
	}
}

Glyph GlyphFor(AugmentId id)
{
	if (IsRuneOfLegend(id))
		return LegendGlyphs[static_cast<size_t>(id) - static_cast<size_t>(FirstRuneOfLegend)];
	if (const std::optional<RuneKind> rune = GetRuneKind(id))
		return FamilyGlyphs[static_cast<size_t>(rune->family)];
	return {};
}

/** The names of the runes' painted pictures (hd/runes/<key>_<strength>.png), in the order of FamilyGlyphs and LegendGlyphs. */
constexpr std::array<std::string_view, static_cast<size_t>(RuneFamily::Count)> FamilyKeys = {
	"ral", "thul", "ort", "ohm", "amn", "vex", "io", "tir", "sol", "gul", "fal", "ko", "lum", "ith", "shael", "zod", "venom"
};
constexpr std::array<std::string_view, RunesOfLegendCount> LegendKeys = {
	"talic", "madawc", "korlic", "bulkathos", "talrasha", "horazon", "jeredcain", "zoltunkulle"
};

/**
 * The name of the rune's painted picture: its family's key and its strength. Shael, Zod and Venom come in one
 * strength and the Runes of Legend in none, so each has one picture, its plain one.
 */
std::string PictureName(AugmentId id)
{
	std::string name = "runes/";
	if (IsRuneOfLegend(id)) {
		name += LegendKeys[static_cast<size_t>(id) - static_cast<size_t>(FirstRuneOfLegend)];
		return name + "_normal";
	}
	const std::optional<RuneKind> rune = GetRuneKind(id);
	if (!rune)
		return {};
	name += FamilyKeys[static_cast<size_t>(rune->family)];
	if (rune->family == RuneFamily::Shael || rune->family == RuneFamily::Zod || rune->family == RuneFamily::Venom)
		return name + "_normal";
	switch (rune->strength) {
	case RuneStrength::Greater:
		return name + "_greater";
	case RuneStrength::Perfect:
		return name + "_perfect";
	default:
		return name + "_normal";
	}
}

void Plot(const Surface &out, Point point, uint8_t color)
{
	if (out.InBounds(point))
		out.SetPixel(point, color);
}

/** Whether a point is inside the stone: a rounded tablet, a little wider at its top. */
bool InStone(int dx, int dy, int rx, int ry)
{
	// Wider at the top than the bottom, like a worn stone.
	const int width = dy < 0 ? rx : rx - ((dy * 2) / ry);
	if (width <= 0)
		return false;
	return (dx * dx * ry * ry) + (dy * dy * width * width) <= width * width * ry * ry;
}

template <typename F>
void ForLine(int x0, int y0, int x1, int y1, F &&plot)
{
	const int dx = std::abs(x1 - x0);
	const int dy = -std::abs(y1 - y0);
	const int sx = x0 < x1 ? 1 : -1;
	const int sy = y0 < y1 ? 1 : -1;
	int error = dx + dy;
	while (true) {
		plot(x0, y0);
		if (x0 == x1 && y0 == y1)
			break;
		const int e2 = 2 * error;
		if (e2 >= dy) {
			error += dy;
			x0 += sx;
		}
		if (e2 <= dx) {
			error += dx;
			y0 += sy;
		}
	}
}

} // namespace

bool IsDrawnRune(AugmentId id)
{
	return GetRuneKind(id).has_value() || IsRuneOfLegend(id);
}

bool IsDrawnRuneItem(const Item &item)
{
	return gbIsPoe2 && !item.isEmpty() && item._iMiscId == IMISC_AUGMENT && IsDrawnRune(GetAugmentId(item));
}

void DrawRuneIcon(const Surface &out, const Rectangle &box, AugmentId id, bool dim)
{
	const Point center = box.position + Displacement { box.size.width / 2, box.size.height / 2 };
	const int rx = std::max(std::min(box.size.width, box.size.height) / 2 - 4, 4);
	const int ry = std::max(box.size.height / 2 - 3, 5);
	const Glow glow = dim ? Glow { 0, 0, 0, 0 } : GlowFor(id);

	// HD: what was in the box before the rune goes on, so its painted picture can show over it (an empty slot's dim
	// rune has none). Without the picture, or with HD off, the stone drawn here stays.
	static std::vector<uint8_t> before;
	const bool record = !dim && hd::RecordUiPictureHook != nullptr && box.size.width > 0 && box.size.height > 0;
	if (record) {
		before.assign(static_cast<size_t>(box.size.width) * box.size.height, 0);
		for (int y = 0; y < box.size.height; y++) {
			for (int x = 0; x < box.size.width; x++) {
				const Point point = box.position + Displacement { x, y };
				if (out.InBounds(point))
					before[static_cast<size_t>(y) * box.size.width + x] = out[point];
			}
		}
	}

	// The glow: rings just outside the stone.
	for (int ring = glow.rings; ring >= 1; ring--) {
		const uint8_t color = ring == 1 ? glow.inner : glow.outer;
		if (color == 0)
			continue;
		for (int dy = -ry - ring; dy <= ry + ring; dy++) {
			for (int dx = -rx - ring; dx <= rx + ring; dx++) {
				if (InStone(dx, dy, rx + ring, ry + ring) && !InStone(dx, dy, rx + ring - 1, ry + ring - 1))
					Plot(out, center + Displacement { dx, dy }, color);
			}
		}
	}

	// The stone: grey, lit from the top left, with a dark rim. Shade 15 of a ramp is the palette's white, so 14 is the darkest.
	const int darken = dim ? 4 : 0;
	for (int dy = -ry; dy <= ry; dy++) {
		for (int dx = -rx; dx <= rx; dx++) {
			if (!InStone(dx, dy, rx, ry))
				continue;
			const bool rim = !InStone(dx, dy, rx - 1, ry - 1);
			int shade = rim ? 12 : 7 + ((dx + dy) / 5);
			// A few pits so it reads as stone, the same on every rune of the id.
			if (!rim && ((dx * 7) + (dy * 13) + static_cast<int>(id)) % 11 == 0)
				shade += 2;
			Plot(out, center + Displacement { dx, dy }, static_cast<uint8_t>(PAL16_GRAY + std::clamp(shade + darken, 2, 14)));
		}
	}

	// The carving: a dark groove with a lit edge below it, glowing on the stronger runes.
	const Glyph glyph = GlyphFor(id);
	const Point origin = center - Displacement { 6, 8 };
	const uint8_t groove = glow.carving != 0 ? glow.carving : static_cast<uint8_t>(PAL16_GRAY + 14);
	const uint8_t edge = static_cast<uint8_t>(PAL16_GRAY + (dim ? 10 : 4));
	for (const Stroke &stroke : glyph) {
		ForLine(stroke.x0 * 2, stroke.y0 * 2, stroke.x1 * 2, stroke.y1 * 2, [&](int x, int y) {
			Plot(out, origin + Displacement { x + 1, y + 1 }, edge);
		});
	}
	for (const Stroke &stroke : glyph) {
		ForLine(stroke.x0 * 2, stroke.y0 * 2, stroke.x1 * 2, stroke.y1 * 2, [&](int x, int y) {
			Plot(out, origin + Displacement { x, y }, groove);
		});
	}

	if (record) {
		const std::string name = PictureName(id);
		if (!name.empty())
			hd::RecordUiPictureHook(out, box, name, before.data());
	}
}

} // namespace devilution
