#include "poe2/hud_globes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "control/control.hpp"
#include "control/control_flasks.hpp"
#include "control/control_panel.hpp"
#include "diablo.h"
#include "engine/light_tables.hpp"
#include "engine/palette.h"
#include "engine/render/clx_render.hpp"
#include "engine/render/primitive_render.hpp"
#include "engine/render/text_render.hpp"
#include "game_mode.hpp"
#include "panels/skill_bar.hpp"
#include "player.h"
#include "poe2/map_play.hpp"
#include "poe2/skill_resources.hpp"
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
/** @brief The Rage bar's colours: a dark frame, a darker red track and a red fill lit along its top. */
constexpr uint8_t RageBarFrameColor = PAL16_GRAY + 14;
constexpr uint8_t RageBarTrackColor = PAL16_RED + 14;
constexpr uint8_t RageBarFillTopColor = PAL16_RED + 2;
constexpr uint8_t RageBarFillColor = PAL16_RED + 5;
constexpr uint8_t RageBarFillBottomColor = PAL16_RED + 8;

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

void DrawShadowedValue(const Surface &out, std::string_view text, Point position, UiFlags color)
{
	constexpr int Width = 80;
	const Rectangle rect { position - Displacement { Width / 2, 0 }, { Width, 12 } };
	DrawString(out, text, { rect.position + Displacement { 1, 1 }, rect.size }, { .flags = UiFlags::ColorBlack | UiFlags::AlignCenter | UiFlags::KerningFitSpacing, .spacing = 0 });
	DrawString(out, text, rect, { .flags = color | UiFlags::AlignCenter | UiFlags::KerningFitSpacing, .spacing = 0 });
}

/** @brief Text with a soft shadow one pixel down and right, aligned in the rectangle by the flags. */
void DrawShadowedText(const Surface &out, std::string_view text, const Rectangle &rect, UiFlags color, UiFlags align)
{
	DrawString(out, text, { rect.position + Displacement { 1, 1 }, rect.size }, { .flags = UiFlags::ColorBlack | align | UiFlags::KerningFitSpacing, .spacing = 0 });
	DrawString(out, text, rect, { .flags = color | align | UiFlags::KerningFitSpacing, .spacing = 0 });
}

/**
 * @brief Shadowed text in a colour the palette lacks: drawn white, then each letter pixel is shown in @p colour at the
 * white's brightness through the UI's full-colour pixels. Where those aren't drawn, it is @p fallback from the palette.
 */
void DrawTrueColourText(const Surface &out, std::string_view text, const Rectangle &rect, const std::array<uint8_t, 3> &colour, UiFlags fallback)
{
	if (!CanTintUi(out)) {
		DrawShadowedText(out, text, rect, fallback, UiFlags::None);
		return;
	}
	DrawString(out, text, { rect.position + Displacement { 1, 1 }, rect.size }, { .flags = UiFlags::ColorBlack | UiFlags::KerningFitSpacing, .spacing = 0 });
	// Letters can reach a few rows past the line's box.
	const int x0 = std::max(rect.position.x - 2, 0);
	const int y0 = std::max(rect.position.y - 4, 0);
	const int x1 = std::min(rect.position.x + rect.size.width + 2, out.w());
	const int y1 = std::min(rect.position.y + rect.size.height + 4, out.h());
	if (x0 >= x1 || y0 >= y1)
		return;
	const int width = x1 - x0;
	std::vector<uint8_t> before(static_cast<size_t>(width) * static_cast<size_t>(y1 - y0));
	for (int y = y0; y < y1; y++)
		for (int x = x0; x < x1; x++)
			before[(static_cast<size_t>(y - y0) * width) + (x - x0)] = *out.at(x, y);
	DrawString(out, text, rect, { .flags = UiFlags::ColorWhite | UiFlags::KerningFitSpacing, .spacing = 0 });
	for (int y = y0; y < y1; y++) {
		for (int x = x0; x < x1; x++) {
			const uint8_t index = *out.at(x, y);
			if (index == before[(static_cast<size_t>(y - y0) * width) + (x - x0)])
				continue;
			const SDL_Color &shade = logical_palette[index];
			const int bright = std::max({ shade.r, shade.g, shade.b });
			const auto scale = [bright](uint8_t channel) { return static_cast<uint8_t>(std::min(channel * bright / 230, 255)); };
			DrawUiTrueColourPixel(out, { x, y }, index, scale(colour[0]), scale(colour[1]), scale(colour[2]));
		}
	}
}

bool Overlaps(const Rectangle &a, const Rectangle &b)
{
	return a.position.x < b.position.x + b.size.width && b.position.x < a.position.x + a.size.width
	    && a.position.y < b.position.y + b.size.height && b.position.y < a.position.y + a.size.height;
}

bool IsCoveredBySidePanel(const Rectangle &rect)
{
	return (IsLeftPanelOpen() && Overlaps(rect, GetLeftPanel())) || (IsRightPanelOpen() && Overlaps(rect, GetRightPanel()));
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

GlobeLevels ReadGlobeLevels(const Player &player)
{
	GlobeLevels levels;
	levels.life = player._pHitPoints.whole();
	levels.maxLife = player._pMaxHP.whole();
	levels.energyShield = player.poe2.energyShield / 64;
	levels.maxEnergyShield = player.poe2.maxEnergyShield;
	const bool noMana = HasAnyOf(player._pIFlags, ItemSpecialEffect::NoMana);
	levels.mana = noMana || player.hasNoMana() ? 0 : player._pMana.whole();
	levels.maxMana = noMana ? 0 : player._pMaxMana.whole();
	levels.maxSpirit = MaxSpirit(player);
	levels.reservedSpirit = levels.maxSpirit > 0 ? ReservedSpirit(player) : 0;
	levels.rage = player.poe2.rage;
	levels.combo = player.poe2.combo;
	levels.frenzyCharges = player.poe2.frenzyCharges;
	return levels;
}

std::vector<GlobeLevelLine> LifeGlobeLevelLines(const GlobeLevels &levels)
{
	std::vector<GlobeLevelLine> lines;
	lines.push_back({ "Life", StrCat(std::max(levels.life, 0), "/", levels.maxLife), UiFlags::ColorWhite });
	if (levels.maxEnergyShield > 0)
		lines.push_back({ "Energy", StrCat(std::max(levels.energyShield, 0), "/", levels.maxEnergyShield), UiFlags::ColorBlue });
	if (levels.frenzyCharges > 0)
		lines.push_back({ "Frenzy", StrCat(levels.frenzyCharges, "/", MaxFrenzyCharges), UiFlags::ColorOrange });
	return lines;
}

std::vector<GlobeLevelLine> ManaGlobeLevelLines(const GlobeLevels &levels)
{
	std::vector<GlobeLevelLine> lines;
	lines.push_back({ "Mana", StrCat(std::max(levels.mana, 0), "/", levels.maxMana), UiFlags::ColorWhite });
	// Like POE2's mana globe: the Spirit the hero's skills reserve, out of all of it.
	if (levels.maxSpirit > 0)
		lines.push_back({ "Spirit", StrCat(levels.reservedSpirit, "/", levels.maxSpirit), UiFlags::ColorGold });
	// Rage and Combo only show while built up.
	if (levels.rage > 0)
		lines.push_back({ "Rage", StrCat(levels.rage, "/", MaxRage), UiFlags::ColorRed });
	if (levels.combo > 0)
		lines.push_back({ "Combo", StrCat(levels.combo, "/", MaxCombo), UiFlags::ColorWhite });
	return lines;
}

bool ShowingGlobeLevels()
{
	// Like POE2, the numbers are always there (Curtis 2026-11-06: Rage drawn inside the life globe was unreadable).
	return gbIsPoe2;
}

void DrawPoe2GlobeLevels(const Surface &out)
{
	const GlobeLevels levels = ReadGlobeLevels(*MyPlayer);
	const Point panel = GetMainPanel().position;
	// The lowest line sits just over the top of the globe's dome, which sticks out above the panel.
	const int bottom = panel.y + GlobeTopRow - GlobePanelTopRow - GlobeLevelGap;
	const std::optional<Rectangle> tray = SkillTrayRect();
	const std::optional<Rectangle> levelButtonLabel = LevelButtonLabelArea();
	const auto drawLines = [&](int left, const std::vector<GlobeLevelLine> &lines) {
		const int height = static_cast<int>(lines.size()) * GlobeLevelLineHeight;
		Rectangle block { { left, bottom - height }, { GlobeLevelBlockWidth, height } };
		if (IsCoveredBySidePanel(block))
			return;
		// The life numbers step right of the "Passive Points" label over the level-up button, while it shows.
		if (levelButtonLabel && block.position.x < levelButtonLabel->position.x + levelButtonLabel->size.width)
			block.position.x = levelButtonLabel->position.x + levelButtonLabel->size.width + 2;
		// Where a side panel pushed the skill tray up over a globe, the numbers go over the tray.
		if (tray && Overlaps(block, *tray))
			block.position.y = tray->position.y - 2 - height;
		// Like POE2 (Curtis 2026-11-06): the numbers start just past the longest name, names in a soft blue-grey and
		// numbers in a light grey-white.
		int labelWidth = 0;
		for (const GlobeLevelLine &levelLine : lines)
			labelWidth = std::max(labelWidth, GetLineWidth(levelLine.label, GameFont12, 0));
		const int valueLeft = labelWidth + GlobeLevelValueGap;
		for (const GlobeLevelLine &levelLine : lines) {
			const Rectangle label { block.position, { valueLeft, GlobeLevelLineHeight } };
			const Rectangle value { block.position + Displacement { valueLeft, 0 }, { GlobeLevelBlockWidth - valueLeft, GlobeLevelLineHeight } };
			DrawTrueColourText(out, levelLine.label, label, GlobeLevelLabelColour, UiFlags::ColorUiSilver);
			DrawTrueColourText(out, levelLine.value, value, GlobeLevelValueColour, levelLine.color);
			block.position.y += GlobeLevelLineHeight;
		}
	};
	drawLines(panel.x + LifeGlobeX + (GlobeArtSize / 2) - (GlobeLevelBlockWidth / 2), LifeGlobeLevelLines(levels));
	drawLines(panel.x + ManaGlobeX + 2, ManaGlobeLevelLines(levels));
}

int RageBarFill(int rage, int maxRage, int trackWidth)
{
	if (rage <= 0 || maxRage <= 0 || trackWidth <= 0)
		return 0;
	if (rage >= maxRage)
		return trackWidth;
	return std::clamp(rage * trackWidth / maxRage, 1, trackWidth);
}

void DrawRageBar(const Surface &out, const Rectangle &bar, int rage, int maxRage)
{
	if (rage <= 0 || bar.size.width < 3 || bar.size.height < 3)
		return;
	FillRect(out, bar.position.x, bar.position.y, bar.size.width, bar.size.height, RageBarFrameColor);
	const Rectangle track { bar.position + Displacement { 1, 1 }, { bar.size.width - 2, bar.size.height - 2 } };
	FillRect(out, track.position.x, track.position.y, track.size.width, track.size.height, RageBarTrackColor);
	const int filled = RageBarFill(rage, maxRage, track.size.width);
	for (int row = 0; row < track.size.height; row++) {
		uint8_t color = RageBarFillColor;
		if (row == 0)
			color = RageBarFillTopColor;
		else if (row == track.size.height - 1)
			color = RageBarFillBottomColor;
		FillRect(out, track.position.x, track.position.y + row, filled, 1, color);
	}
}

void DrawPoe2RageBar(const Surface &out)
{
	if (!gbIsPoe2 || MyPlayer == nullptr)
		return;
	const Point panel = GetMainPanel().position;
	DrawRageBar(out, { panel + Displacement { BeltRect.position.x - 1, RageBarY }, { BeltRect.size.width + 2, RageBarHeight } }, MyPlayer->poe2.rage, MaxRage);
}

void DrawPoe2GlobeValues(const Surface &out)
{
	const Player &player = *MyPlayer;
	const Point panel = GetMainPanel().position;
	// The Vizjerei Road toll's backlash: one line per element still biting, with its seconds left.
	int backlashY = 58;
	for (const auto &[element, name, colour] : { std::tuple { DamageType::Fire, "Burning", UiFlags::ColorOrange }, std::tuple { DamageType::Cold, "Chilled", UiFlags::ColorBlue },
	         std::tuple { DamageType::Lightning, "Shocked", UiFlags::ColorYellow } }) {
		if (const int ticks = VizjereiBacklashTicks(player, element); ticks > 0) {
			DrawShadowedValue(out, StrCat(name, " ", (ticks + 19) / 20, "s"), panel + Displacement { ManaGlobeX + (GlobeArtSize / 2), backlashY }, colour);
			backlashY += 14;
		}
	}
	// A Venomous map's poison: chaos damage over time that goes around energy shield.
	if (const int ticks = MapPoisonTicksLeft(); ticks > 0)
		DrawShadowedValue(out, StrCat("Poisoned ", (ticks + 19) / 20, "s"), panel + Displacement { ManaGlobeX + (GlobeArtSize / 2), backlashY }, UiFlags::ColorWhite);
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
