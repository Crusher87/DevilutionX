/**
 * @file panels/poe2_widgets.hpp
 *
 * Drawing helpers shared by the POE2 Mashup panels, which are drawn from primitives until they get their own art.
 */
#pragma once

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "engine/palette.h"
#include "engine/point.hpp"
#include "engine/rectangle.hpp"
#include "engine/render/primitive_render.hpp"
#include "engine/render/text_render.hpp"
#include "engine/surface.hpp"

namespace devilution::poe2ui {

constexpr uint8_t PanelColor = PAL16_GRAY + 15;
constexpr uint8_t BorderColor = PAL16_YELLOW + 8;
constexpr uint8_t InnerBorderColor = PAL16_YELLOW + 12;
constexpr uint8_t RowColor = PAL16_GRAY + 14;
constexpr uint8_t RowHoverColor = PAL16_GRAY + 13;
constexpr uint8_t TooltipColor = PAL16_GRAY + 14;
constexpr uint8_t SocketRimColor = PAL16_GRAY + 8;
constexpr uint8_t SocketColor = PAL16_GRAY + 13;
constexpr uint8_t SocketLockedColor = PAL16_GRAY + 14;
constexpr uint8_t HoverColor = PAL16_BEIGE;

inline void FillRectangle(const Surface &out, const Rectangle &rect, uint8_t color)
{
	FillRect(out, rect.position.x, rect.position.y, rect.size.width, rect.size.height, color);
}

inline void DrawFrame(const Surface &out, const Rectangle &rect, uint8_t color)
{
	DrawHorizontalLine(out, rect.position, rect.size.width, color);
	DrawHorizontalLine(out, rect.position + Displacement { 0, rect.size.height - 1 }, rect.size.width, color);
	DrawVerticalLine(out, rect.position, rect.size.height, color);
	DrawVerticalLine(out, rect.position + Displacement { rect.size.width - 1, 0 }, rect.size.height, color);
}

/** @brief A dark panel with a gold double frame. */
inline void DrawPanel(const Surface &out, const Rectangle &rect)
{
	FillRectangle(out, rect, PanelColor);
	DrawFrame(out, rect, BorderColor);
	if (rect.size.width > 6 && rect.size.height > 6)
		DrawFrame(out, { rect.position + Displacement { 2, 2 }, { rect.size.width - 4, rect.size.height - 4 } }, InnerBorderColor);
}

inline void FillCircle(const Surface &out, Point center, int radius, uint8_t color)
{
	for (int y = -radius; y <= radius; y++) {
		int width = 0;
		while ((width + 1) * (width + 1) + y * y <= radius * radius)
			width++;
		DrawHorizontalLine(out, center + Displacement { -width, y }, (2 * width) + 1, color);
	}
}

/** @brief A box of text lines next to the mouse, kept inside the bounds. */
inline void DrawTooltip(const Surface &out, const Rectangle &bounds, Point mouse, const std::string &title, UiFlags titleColor,
    const std::vector<std::pair<std::string, UiFlags>> &lines, uint8_t frameColor = BorderColor)
{
	constexpr int LineHeight = 16;
	constexpr int Width = 250;
	int lineCount = 0;
	for (const auto &line : lines)
		lineCount += static_cast<int>(std::count(line.first.begin(), line.first.end(), '\n')) + 1;
	const int height = 30 + (lineCount * LineHeight);
	Point position = mouse + Displacement { 16, 8 };
	position.x = std::clamp(position.x, bounds.position.x + 2, std::max(bounds.position.x + bounds.size.width - Width - 2, bounds.position.x + 2));
	position.y = std::clamp(position.y, bounds.position.y + 2, std::max(bounds.position.y + bounds.size.height - height - 2, bounds.position.y + 2));
	const Rectangle box { position, { Width, height } };
	FillRectangle(out, box, TooltipColor);
	DrawFrame(out, box, frameColor);

	Rectangle line { position + Displacement { 8, 6 }, { Width - 16, 20 } };
	DrawString(out, title, line, { .flags = titleColor | UiFlags::VerticalCenter });
	line.position.y += 22;
	for (const auto &[text, color] : lines) {
		const int count = static_cast<int>(std::count(text.begin(), text.end(), '\n')) + 1;
		line.size.height = LineHeight * count;
		DrawString(out, text, line, { .flags = color, .lineHeight = LineHeight });
		line.position.y += line.size.height;
	}
}

} // namespace devilution::poe2ui
