/**
 * @file itemlabels.h
 *
 * Adds item labels QoL feature
 */
#pragma once

#include "engine/point.hpp"
#include "engine/surface.hpp"

namespace devilution {

void ToggleItemLabelHighlight();
void HighlightKeyPressed(bool pressed);
bool IsItemLabelHighlighted();
void ResetItemlabelHighlighted();
bool IsHighlightingLabelsEnabled();
void AddItemToLabelQueue(int id, Point position);
void DrawItemNameLabels(const Surface &out);
/**
 * @brief POE2 Mashup: notes again the labels the world last drawn noted, for a frame that shows that world again
 * instead of drawing it (it stands still under the game menu).
 */
void QueueKeptItemNameLabels();

} // namespace devilution
