#include "poe2/hud_globes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

#include "control/control.hpp"
#include "control/control_flasks.hpp"
#include "diablo.h"
#include "engine/light_tables.hpp"
#include "engine/palette.h"
#include "engine/render/clx_render.hpp"
#include "engine/render/text_render.hpp"
#include "options.h"
#include "player.h"
#include "poe2/spirit.hpp"
#include "poe2/unique_effects.hpp"
#include "utils/str_cat.hpp"
#include "utils/timer.hpp"

namespace devilution {

namespace {

constexpr float Pi = 3.14159265F;
constexpr int ArcGap = 3;
constexpr int ArcThickness = 4;
/** @brief The light table the empty part of a flask is drawn with. */
constexpr int EmptyFlaskLightLevel = 11;

uint8_t RampColor(uint8_t rampBase, float shade)
{
	return static_cast<uint8_t>(rampBase + std::clamp(static_cast<int>(shade), 0, 15));
}

/** @brief Maps each fixed palette color to a lighter shade of its ramp. */
const std::array<uint8_t, 256> &BrightenTable()
{
	static const std::array<uint8_t, 256> Table = [] {
		std::array<uint8_t, 256> table {};
		for (int i = 0; i < 256; i++) {
			int color = i;
			if (i >= PAL8_BLUE && i < PAL16_BEIGE) {
				const int shade = (i - PAL8_BLUE) % 8;
				color = i - shade + std::max(0, shade - 3);
			} else if (i >= PAL16_BEIGE) {
				const int shade = (i - PAL16_BEIGE) % 16;
				color = i - shade + std::max(0, shade - 6);
			}
			table[i] = static_cast<uint8_t>(color);
		}
		return table;
	}();
	return Table;
}

void DrawGlobe(const Surface &out, const Surface &emptyArt, int globeX, GlobeLiquid liquid, int fill, bool dome, bool pulse,
    const GlobeArc &arc, int litSegments, bool showArc)
{
	if (!BottomBuffer)
		return;
	const Surface fullArt = BottomBuffer->subregion(globeX, 0, GlobeArtSize, GlobeArtSize);
	const GlobeShape shape = MeasureGlobe(emptyArt, fullArt);
	const Point panel = GetMainPanel().position;
	const Point origin = panel + Displacement { globeX, -GlobePanelTopRow };
	const uint32_t now = GetMillisecondsSinceStartup();
	DrawGlobeLiquid(out, origin, emptyArt, fullArt, shape, liquid, fill, dome, now, pulse);
	if (!showArc || !shape.valid)
		return;
	const Point center = origin + Displacement { static_cast<int>(shape.centerX), static_cast<int>(shape.centerY) };
	const int radius = static_cast<int>(std::max(shape.radiusX, shape.radiusY)) + ArcGap;
	DrawGlobeArc(out, center, radius, ArcThickness, arc, litSegments, dome ? 0 : panel.y, dome ? panel.y : out.h(), now);
}

bool IsHoveringGlobe(int globeX)
{
	const Point panel = GetMainPanel().position;
	const Rectangle globe { panel + Displacement { globeX, GlobeTopRow - GlobePanelTopRow }, { GlobeArtSize, GlobeBottomRow - GlobeTopRow } };
	return globe.contains(MousePosition);
}

void DrawShadowedValue(const Surface &out, std::string_view text, Point position, UiFlags color)
{
	constexpr int Width = 80;
	const Rectangle rect { position - Displacement { Width / 2, 0 }, { Width, 12 } };
	DrawString(out, text, { rect.position + Displacement { 1, 1 }, rect.size }, { .flags = UiFlags::ColorBlack | UiFlags::AlignCenter | UiFlags::KerningFitSpacing, .spacing = 0 });
	DrawString(out, text, rect, { .flags = color | UiFlags::AlignCenter | UiFlags::KerningFitSpacing, .spacing = 0 });
}

} // namespace

GlobeShape MeasureGlobe(const Surface &emptyArt, const Surface &fullArt)
{
	GlobeShape shape;
	int left = GlobeArtSize;
	int right = -1;
	int top = GlobeBottomRow;
	int bottom = -1;
	for (int y = GlobeTopRow; y < GlobeBottomRow; y++) {
		const bool domeRow = y < GlobePanelTopRow;
		for (int x = domeRow ? GlobeDomeLeft : 0; x < (domeRow ? GlobeDomeRight : GlobeArtSize); x++) {
			if (*emptyArt.at(x, y) == *fullArt.at(x, y))
				continue;
			left = std::min(left, x);
			right = std::max(right, x);
			top = std::min(top, y);
			bottom = std::max(bottom, y);
		}
	}
	if (right < left || bottom < top)
		return shape;
	shape.centerX = (left + right + 1) / 2.F;
	shape.centerY = (top + bottom + 1) / 2.F;
	shape.radiusX = (right - left + 1) / 2.F;
	shape.radiusY = (bottom - top + 1) / 2.F;
	shape.top = top;
	shape.bottom = bottom;
	shape.valid = true;
	return shape;
}

int LitArcSegments(int current, int maximum, int segments)
{
	if (maximum <= 0 || current <= 0)
		return 0;
	if (current >= maximum)
		return segments;
	return std::clamp((current * segments + (maximum / 2)) / maximum, 1, segments);
}

float GlobeLiquidLevel(const GlobeShape &shape, int fill)
{
	const int height = shape.bottom - shape.top + 1;
	return static_cast<float>(shape.bottom + 1) - (static_cast<float>(std::clamp(fill, 0, GlobeFull)) * height / GlobeFull);
}

void DrawGlobeLiquid(const Surface &out, Point origin, const Surface &emptyArt, const Surface &fullArt, const GlobeShape &shape,
    GlobeLiquid liquid, int fill, bool dome, uint32_t timeMs, bool pulse)
{
	const int rowBegin = dome ? GlobeTopRow : GlobePanelTopRow;
	const int rowEnd = dome ? GlobePanelTopRow : GlobeBottomRow;
	const int columnBegin = dome ? GlobeDomeLeft : 0;
	const int columnEnd = dome ? GlobeDomeRight : GlobeArtSize;
	const float time = static_cast<float>(timeMs % 3600000) / 1000.F;
	const uint8_t ramp = liquid == GlobeLiquid::Life ? PAL16_RED : PAL16_BLUE;
	const float level = shape.valid ? GlobeLiquidLevel(shape, fill) : 0.F;
	const float throb = pulse ? (0.5F + (0.5F * std::sin(time * 6.F))) * 3.F : 0.F;

	// Bubbles rise from the bottom to the surface.
	constexpr int BubbleCount = 5;
	std::array<Point, BubbleCount> bubbles {};
	if (shape.valid) {
		const float depth = std::max(1.F, static_cast<float>(shape.bottom) - level);
		for (int i = 0; i < BubbleCount; i++) {
			const float rise = std::fmod((time * (9.F + (4.F * i))) + (i * 29.F), depth);
			bubbles[i] = { static_cast<int>(shape.centerX + (shape.radiusX * 0.5F * std::sin((i * 1.9F) + (time * (0.5F + (0.2F * i)))))),
				static_cast<int>(static_cast<float>(shape.bottom) - rise) };
		}
	}

	for (int y = rowBegin; y < rowEnd; y++) {
		const int screenY = origin.y + y;
		if (screenY < 0 || screenY >= out.h())
			continue;
		for (int x = columnBegin; x < columnEnd; x++) {
			const int screenX = origin.x + x;
			if (screenX < 0 || screenX >= out.w())
				continue;
			uint8_t &pixel = *out.at(screenX, screenY);
			const uint8_t empty = *emptyArt.at(x, y);
			const bool isLiquid = shape.valid && empty != *fullArt.at(x, y);
			float surface = level;
			if (fill <= 0)
				surface = 1000.F;
			else if (fill < GlobeFull)
				surface += (1.2F * std::sin((x * 0.23F) + (time * 2.7F))) + (0.7F * std::sin((x * 0.11F) - (time * 1.9F)));
			else
				surface = -1000.F;
			if (!isLiquid || static_cast<float>(y) < surface) {
				if (!dome || empty != 0)
					pixel = empty;
				continue;
			}

			const float nx = ((x + 0.5F) - shape.centerX) / shape.radiusX;
			const float ny = ((y + 0.5F) - shape.centerY) / shape.radiusY;
			const float distance = std::min(1.F, (nx * nx) + (ny * ny));
			const float nz = std::sqrt(1.F - distance);
			const float light = (-0.45F * nx) + (-0.55F * ny) + (0.70F * nz);
			const float swirl = std::sin((nx * 4.F) + (time * 1.1F) + (1.7F * std::sin((ny * 3.F) - (time * 0.7F))))
			    * std::cos((ny * 5.F) + (time * 0.8F) + (nx * 2.F));
			float shade = 8.5F - (light * 5.5F) + (swirl * 1.6F) - throb;
			if (distance > 0.82F)
				shade += (distance - 0.82F) * 12.F;
			const float highlightX = nx + 0.38F;
			const float highlightY = ny + 0.46F;
			if (((highlightX * highlightX) / 0.06F) + ((highlightY * highlightY) / 0.025F) < 1.F)
				shade -= 4.F;
			const float belowSurface = static_cast<float>(y) - surface;
			if (belowSurface < 1.5F)
				shade = std::min(shade, 1.5F + belowSurface);
			for (const Point &bubble : bubbles) {
				if (bubble.x == x && bubble.y == y && static_cast<float>(y) > surface + 1.F)
					shade = 0.F;
			}
			pixel = RampColor(ramp, shade);
		}
	}
}

void DrawGlobeArc(const Surface &out, Point center, int radius, int thickness, const GlobeArc &arc, int litSegments,
    int clipTop, int clipBottom, uint32_t timeMs)
{
	const float time = static_cast<float>(timeMs % 3600000) / 1000.F;
	const int outer = radius + thickness;
	const float span = arc.endDegrees - arc.startDegrees;
	for (int dy = -outer; dy <= outer; dy++) {
		const int screenY = center.y + dy;
		if (screenY < std::max(clipTop, 0) || screenY >= std::min(clipBottom, out.h()))
			continue;
		for (int dx = -outer; dx <= outer; dx++) {
			const int screenX = center.x + dx;
			if (screenX < 0 || screenX >= out.w())
				continue;
			const float distance = std::sqrt(static_cast<float>((dx * dx) + (dy * dy)));
			if (distance < static_cast<float>(radius) || distance >= static_cast<float>(outer))
				continue;
			float angle = std::atan2(static_cast<float>(dy), static_cast<float>(dx)) * 180.F / Pi;
			while (angle < arc.startDegrees)
				angle += 360.F;
			const float position = (angle - arc.startDegrees) / span;
			if (position > 1.F)
				continue;
			const float segmentPosition = position * arc.segments;
			const int segment = std::min(static_cast<int>(segmentPosition), arc.segments - 1);
			if (segmentPosition - segment < 0.14F)
				continue;
			const int fillIndex = arc.fillFromEnd ? arc.segments - 1 - segment : segment;
			const float edge = (distance - radius) / thickness;
			float bevel = 0.F;
			if (edge < 0.25F)
				bevel = 2.F;
			else if (edge > 0.75F)
				bevel = -1.F;
			uint8_t &pixel = *out.at(screenX, screenY);
			if (fillIndex < litSegments) {
				const float glint = std::sin((position * 9.F) - (time * 3.2F));
				pixel = RampColor(arc.rampBase, 3.F - (2.F * glint) + bevel);
			} else {
				pixel = RampColor(PAL16_GRAY, 12.F + bevel);
			}
		}
	}
}

void DrawPoe2LifeGlobe(const Surface &out, bool dome)
{
	if (!pLifeBuff)
		return;
	const Player &player = *MyPlayer;
	const bool lowLife = player._pHPPer > 0 && player._pHPPer < GlobeFull * 3 / 10;
	const int energyShield = player.poe2.energyShield / 64;
	DrawGlobe(out, *pLifeBuff, LifeGlobeX, GlobeLiquid::Life, player._pHPPer, dome, lowLife, EnergyShieldArc,
	    LitArcSegments(energyShield, player.poe2.maxEnergyShield, EnergyShieldArc.segments), player.poe2.maxEnergyShield > 0);
}

void DrawPoe2ManaGlobe(const Surface &out, bool dome)
{
	if (!pManaBuff)
		return;
	const Player &player = *MyPlayer;
	const int maxSpirit = MaxSpirit(player);
	DrawGlobe(out, *pManaBuff, ManaGlobeX, GlobeLiquid::Mana, player._pManaPer, dome, false, SpiritArc,
	    LitArcSegments(AvailableSpirit(player), maxSpirit, SpiritArc.segments), maxSpirit > 0);
}

bool IsHoveringLifeGlobe()
{
	return IsHoveringGlobe(LifeGlobeX);
}

bool IsHoveringManaGlobe()
{
	return IsHoveringGlobe(ManaGlobeX);
}

void DrawPoe2GlobeValues(const Surface &out)
{
	const Player &player = *MyPlayer;
	const Point panel = GetMainPanel().position;
	if (player.poe2.maxEnergyShield > 0 && (*GetOptions().Gameplay.showHealthValues || IsHoveringGlobe(LifeGlobeX))) {
		DrawShadowedValue(out, StrCat("ES ", player.poe2.energyShield / 64, "/", player.poe2.maxEnergyShield),
		    panel + Displacement { LifeGlobeX + (GlobeArtSize / 2), 44 }, UiFlags::ColorBlue);
	}
	const int maxSpirit = MaxSpirit(player);
	if (maxSpirit > 0 && (*GetOptions().Gameplay.showManaValues || IsHoveringGlobe(ManaGlobeX))) {
		// Like POE2, the Spirit not reserved yet.
		DrawShadowedValue(out, StrCat("Spirit ", AvailableSpirit(player), "/", maxSpirit),
		    panel + Displacement { ManaGlobeX + (GlobeArtSize / 2), 44 }, UiFlags::ColorGold);
	}
	// Rage, Combo and Frenzy charges only show while built up.
	if (player.poe2.rage > 0)
		DrawShadowedValue(out, StrCat("Rage ", player.poe2.rage), panel + Displacement { LifeGlobeX + (GlobeArtSize / 2), 30 }, UiFlags::ColorRed);
	if (player.poe2.combo > 0)
		DrawShadowedValue(out, StrCat("Combo ", player.poe2.combo), panel + Displacement { ManaGlobeX + (GlobeArtSize / 2), 30 }, UiFlags::ColorWhite);
	if (player.poe2.frenzyCharges > 0)
		DrawShadowedValue(out, StrCat("Frenzy ", player.poe2.frenzyCharges), panel + Displacement { LifeGlobeX + (GlobeArtSize / 2), 58 }, UiFlags::ColorOrange);
	// The Sickness Road toll's fever, while it keeps the hero's own hits from inflicting ailments.
	if (const int fever = SicknessFeverTicks(player); fever > 0)
		DrawShadowedValue(out, StrCat("Fever ", (fever + 19) / 20, "s"), panel + Displacement { ManaGlobeX + (GlobeArtSize / 2), 58 }, UiFlags::ColorYellow);
}

void DrawFlaskLiquidLevel(const Surface &out, Point position, ClxSprite sprite, int charges, int maxCharges)
{
	if (maxCharges <= 0)
		return;
	const int width = sprite.width();
	const int height = sprite.height();
	const int top = position.y - height + 1;
	if (position.x < 0 || top < 0 || position.x + width > out.w() || position.y >= out.h())
		return;
	const int emptyRows = (height * (maxCharges - std::clamp(charges, 0, maxCharges))) / maxCharges;
	if (emptyRows > 0) {
		const Surface empty = out.subregion(position.x, top, width, emptyRows);
		ClxDrawTRN(empty, { 0, height - 1 }, sprite, LightTables[EmptyFlaskLightLevel].data());
	}
	if (emptyRows > 0 && emptyRows < height) {
		const Surface surface = out.subregion(position.x, top + emptyRows, width, 1);
		ClxDrawTRN(surface, { 0, height - 1 - emptyRows }, sprite, BrightenTable().data());
	}
}

} // namespace devilution
